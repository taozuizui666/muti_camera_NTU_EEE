/**
 ******************************************************************************
 *
 *  @file bl_usb.c
 *
 *  Copyright (C) BouffaloLab 2017-2023
 *
 *  Licensed under the Apache License, Version 2.0 (the License);
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an ASIS BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************
 */
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/usb.h>
#include <asm/atomic.h>
#include <linux/slab.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "bl_bootrom.h"
#include "bl_defs.h"
#include "bl_usb.h"
#include "bl_irqs.h"
#include "bl_ipc_host.h"
#include "bl_nl_events.h"
#include "bl_msg_rx.h"
#include "bl_fwbin.h"
#include "bl_mpfwbin.h"
#include "bl_cmds.h"

#define BOOTROM_CMD_BUFFER_SIZE (1024 * 2)
#define BOOTROM_DATA_BUFFER_SIZE (512 * 2 * 2)

static struct usb_device_id bl_usb_ids[] = {
    /* 616u, using PID1 to dnld fw, using PID2 for wifi function */
    {USB_DEVICE(USB_VENDOR_ID_BFL, USB_DEVICE_ID_BFL_616_B)},
    {USB_DEVICE(USB_VENDOR_ID_BFL, USB_DEVICE_ID_BFL_618_B)},

    //debug
    {USB_DEVICE(0x75fb, 0x759b)},

    {USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_BFL, USB_DEVICE_ID_BFL_616_F, 
          WIFI_INTERFACE_CLASS, WIFI_INTERFACE_SUBCLASS, WIFI_INTERFACE_PRTO)},
    {USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_BFL, USB_DEVICE_ID_BFL_618_F, 
          WIFI_INTERFACE_CLASS, WIFI_INTERFACE_SUBCLASS, WIFI_INTERFACE_PRTO)},
    {} /*Terminating entry*/
};

MODULE_DEVICE_TABLE(usb, bl_usb_ids);

static int signature_mode = 0, encryption_mode = 0;
static struct ipc_shared_env_tag bl_ipc_chared_env_tag;

static int bl_usb_submit_rx_urb(struct urb_context *ctx, int size);
static int bl_usb_resubmit_rx_urb(struct urb_context *ctx, int size);

int bl_usb_platform_on(struct bl_hw *bl_hw, void *config)
{
    struct bl_usb_device *device;
    struct bl_plat *bl_plat = bl_hw->plat;
    int ret;
    
    device = (struct bl_usb_device *)(bl_hw->plat)->priv;
    if (bl_plat->enabled)
        return 0;

    if ((ret = bl_ipc_init(bl_hw, (u8 *)&bl_ipc_chared_env_tag)))
        return ret;

    bl_plat->enabled = true;
    usb_set_intfdata(device->intf, bl_hw);

    return 0;
}

void bl_usb_platform_off(struct bl_hw *bl_hw, void **config)
{
    struct sk_buff *skb = NULL;
    int i = 0;

    if (!bl_hw->plat->enabled) {
        return;
    }
    bl_usb_txrx_deinit(bl_hw);

    while (skb_queue_len(&bl_hw->rx_pkt_list)) {
        skb = skb_dequeue(&bl_hw->rx_pkt_list);
        if (skb)
            dev_kfree_skb_any(skb);
    }

    for(i = 0; i < NX_NB_TXQ; i++) {
        while (skb_queue_len(&bl_hw->transmitted_list[i])) {
            skb = skb_dequeue(&bl_hw->transmitted_list[i]);
            if (skb)
                dev_kfree_skb_any(skb);
        }
    }

    if (bl_hw->workqueue) {
        flush_workqueue(bl_hw->workqueue);
        destroy_workqueue(bl_hw->workqueue);
        bl_hw->workqueue = NULL;
    }
    if (bl_hw->rx_workqueue) {
        flush_workqueue(bl_hw->rx_workqueue);
        destroy_workqueue(bl_hw->rx_workqueue);
        bl_hw->rx_workqueue = NULL;
    }

    bl_ipc_deinit(bl_hw);

    bl_hw->plat->enabled = false;
}

void bl_device_deinit(struct bl_plat *bl_plat)
{
    kfree(bl_plat);
}

int bl_device_init(struct usb_interface *intf, struct bl_plat **bl_plat)
{
    struct usb_device *udev = interface_to_usbdev(intf);
    struct usb_host_interface *iface_desc = intf->cur_altsetting;
    struct usb_endpoint_descriptor *epd;
    struct bl_usb_device *device;
    u16 id_vendor, id_product, bcd_device;
    int ret=0, i;

    id_vendor = le16_to_cpu(udev->descriptor.idVendor);
    id_product = le16_to_cpu(udev->descriptor.idProduct);
    bcd_device = le16_to_cpu(udev->descriptor.bcdDevice);
    
    printk("info: VID/PID = %X/%X, Boot2 version = %X\n",
           id_vendor, id_product, bcd_device);
    printk("info: infClass=%#x infSubClass=%#x infProtocol=%#x\n",
           iface_desc->desc.bInterfaceClass, iface_desc->desc.bInterfaceSubClass,
           iface_desc->desc.bInterfaceProtocol);
    printk("info: bcdUSB=%#x Device Class=%#x SubClass=%#x Protocol=%#x\n",
           udev->descriptor.bcdUSB, udev->descriptor.bDeviceClass,
           udev->descriptor.bDeviceSubClass,
           udev->descriptor.bDeviceProtocol);

    *bl_plat = kzalloc(sizeof(struct bl_plat) + 
                       sizeof(struct bl_usb_device), GFP_KERNEL);
    if (!*bl_plat) {
        printk("kzalloc for bl_plat failed\n");
        return -ENOMEM;
    }

    device = (struct bl_usb_device *)(*bl_plat)->priv;

    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        epd = &iface_desc->endpoint[i].desc;
        
        if (usb_endpoint_dir_in(epd) &&
            usb_endpoint_num(epd) == BL_USB_EP_IN &&
            usb_endpoint_xfer_bulk(epd)) {
            printk("info: EP IN: ep_num: %d, max pkt size: %d, addr: %d\n",
                   usb_endpoint_num(epd),
                   le16_to_cpu(epd->wMaxPacketSize),
                   epd->bEndpointAddress);
                 
            device->rx_ep = usb_endpoint_num(epd);
            atomic_set(&(device->rx_cmd_urb_pending), 0);
            atomic_set(&(device->rx_data_urb_pending), 0);
        }

        if (usb_endpoint_dir_out(epd) &&
            usb_endpoint_num(epd) == BL_USB_EP_DATA &&
            usb_endpoint_xfer_bulk(epd)) 
        {
            printk("info: EP OUT: ep_num: %d, max pkt size: %d, addr: %d\n",
                   usb_endpoint_num(epd),
                   le16_to_cpu(epd->wMaxPacketSize),
                   epd->bEndpointAddress);
                 
            device->tx_ep = usb_endpoint_num(epd);
            atomic_set(&(device->tx_cmd_urb_pending), 0);
            atomic_set(&(device->tx_data_urb_pending), 0);
        }
    }

#if defined(CONFIG_CMD_USB_EP)
    device->tx_cmd_ep = 4;
    atomic_set(&(device->tx_cmd_urb_pending), 0);
    
    device->tx_cmd.ep = device->tx_cmd_ep;
#endif

    switch(id_product) {
        case USB_DEVICE_ID_BFL_616_B:
        case USB_DEVICE_ID_BFL_618_B:
            printk("download fw\n");
            device->usb_boot_state = BL616_FW_DNLD;
            break;
        case USB_DEVICE_ID_BFL_616_F:
        case USB_DEVICE_ID_BFL_618_F:
            printk("fw ready\n");
            device->usb_boot_state = BL616_FW_READY;
            break;
        default:
            printk("unknow id_product %#x\n", id_product);
            device->usb_boot_state = BL616_FW_DNLD;
            break;
    }

    device->intf = intf;
    device->udev = udev;

    (*bl_plat)->ops.usb_read_data = bl_read_data_sync;
    (*bl_plat)->ops.usb_write_data = bl_write_data_sync;
    (*bl_plat)->ops.platform_on = bl_usb_platform_on;
    (*bl_plat)->ops.platform_off = bl_usb_platform_off;
    (*bl_plat)->dev = intf;

    if(device->usb_boot_state == BL616_FW_DNLD) {
        ret = bl_usb_dnld_fw((*bl_plat));
        if(ret ==0) {
            printk("dnld fw success, wait for next enumeration\n");
            return BL616_FW_DNLD_OK;
        } else {
            printk("dnld fw fail\n");
            return BL616_FW_DNLD_FAIL;
        }
    }

    return ret;
}

static int bl_usb_run_fw(struct bl_plat *bl_plat)
{
    int ret;
    u8 *buff = NULL;

    BL_DBG(USB_FN_ENTRY_STR);

    buff = kzalloc(32, GFP_KERNEL);
    if (NULL == buff) {
        printk("NO MEM for alloc \n");
        return -ENOMEM;
    }

    memset(buff, 0x55, 32);
    printk("CMD bl_usb_run_fw len %d\n", 32);
    ret = bl_write_data_sync(bl_plat, buff, 32, BL_USB_EP_OUT, BL_USB_TIMEOUT);
    if (ret) {
        printk("ERR when write\n");
        goto err;
    }
    
    memset(buff, 0x55, 32);
    ret = bl_read_data_sync(bl_plat, buff, 32, BL_USB_EP_IN, BL_USB_TIMEOUT);
    if (ret < 0) {
        printk("response get err:%d",ret);
        goto err;
    }

    if ('O' == buff[0] && 'K' == buff[1]) {
        printk("[RSP] bl_usb_run_fw response OK\n");
        kfree(buff);
        return 0;
    } else if ('F' == buff[0] && 'L' == buff[1]) {
        printk("[RSP] bl_usb_run_fw response failed \n");
    } else {
        printk("[RSP] unkown status:%c%c\n", buff[0], buff[1]);
    }

err:
    kfree(buff);
    return -EFAULT;

}

static int firmware_dump_head(bootheader_t *header)
{
    // int i;

    // printk("[************Header************]\n");
    // printk(" magiccode: 0x%X\n", header->magiccode);
    // printk(" rivison: 0x%X\n", header->rivison);
    // printk(" flashCfg\n");
    // printk("    magiccode: 0x%X\n", header->flashCfg.magiccode);
    // printk("    cfg see below, size: %lu\n", sizeof(header->flashCfg.cfg));
    // printk("    crc32: 0x%X\n", header->flashCfg.crc32);
    // printk(" pllCfg\n");
    // printk("    magiccode: 0x%X\n", header->pllCfg.magiccode);
    // printk("    root_clk: %u\n", header->pllCfg.root_clk);
    // printk("    xtal_type: %u\n", header->pllCfg.xtal_type);
    // printk("    pll_clk: %u\n", header->pllCfg.pll_clk);
    // printk("    hclk_div: %u\n", header->pllCfg.hclk_div);
    // printk("    bclk_div: %u\n", header->pllCfg.bclk_div);
    // printk("    flash_clk_div: %u\n", header->pllCfg.flash_clk_div);
    // printk("    uart_clk_div: %u\n", header->pllCfg.uart_clk_div);
    // printk("    sdu_clk_div: %u\n", header->pllCfg.sdu_clk_div);
    // printk("    crc32: 0x%X\n", header->pllCfg.crc32);
    // printk(" seccfg\n");
    // printk("    bval in union\n");
    // printk("        encrypt: %u\n", header->seccfg.bval.encrypt);
    // printk("        sign: %u\n", header->seccfg.bval.sign);
    // printk("        key_sel: %u\n", header->seccfg.bval.key_sel);
    // printk("        rsvd6_31: %u\n", header->seccfg.bval.rsvd6_31);
    // printk("    wval in union\n");
    // printk("        wval: 0x%X\n", header->seccfg.wval);
    // printk(" segment_cnt: %u\n", header->segment_cnt);
    // printk(" bootentry: 0x%X\n", header->bootentry);
    // printk(" flashoffset: 0x%X\n", header->flashoffset);
    // printk(" bootentry: 0x%X\n", header->bootentry);
    // printk(" hash:\n");

    // for (i = 0; i < sizeof(header->hash); i+=4) {
    //     //XXX we may access over-flow
    //     printk("0x%08X ", ((uint32_t)header->hash[i]) << 24 |
    //         (uint32_t)header->hash[i + 1] << 16 |
    //         (uint32_t)header->hash[i + 2] << 8 |
    //         (uint32_t)header->hash[i + 3] << 0
    //     );
    // }


    // printk(" rsv1: %u\n", header->rsv1);
    // printk(" rsv2: %u\n", header->rsv2);
    // printk(" crc32: 0x%X\n", header->crc32);
    // printk("--------\n");

    return 0;
}

// static int firmware_dump_flashcfg(const boot_flash_cfg_t *cfg)
// {
    // printk("[************FLASH CFG(%lu)************]\n", sizeof(boot_flash_cfg_t));
    // printk(" magiccode: 0x%X\n", cfg->magiccode);
    // printk(" flash cfg\n");
    // printk("   ioMode: %u\n", cfg->cfg.ioMode);
    // printk("   cReadSupport: %u\n", cfg->cfg.cReadSupport);
    // printk("   clk_delay;: %u\n", cfg->cfg.clk_delay);
    // printk("   rsvd[1]: %X\n", cfg->cfg.rsvd[0]);
    // printk("   resetEnCmd: %u\n", cfg->cfg.resetEnCmd);
    // printk("   resetCmd: %u\n", cfg->cfg.resetCmd);
    // printk("   resetCreadCmd: %u\n", cfg->cfg.resetCreadCmd);
    // printk("   rsvd_reset[1]: %X\n", cfg->cfg.rsvd_reset[0]);
    // printk("   jedecIdCmd: %u(0x%X)\n", cfg->cfg.jedecIdCmd, cfg->cfg.jedecIdCmd);
    // printk("   jedecIdCmdDmyClk: %u(0x%X)\n", cfg->cfg.jedecIdCmdDmyClk, cfg->cfg.jedecIdCmdDmyClk);
    // printk("   qpiJedecIdCmd: %u(0x%X)\n", cfg->cfg.qpiJedecIdCmd, cfg->cfg.qpiJedecIdCmd);
    // printk("   qpiJedecIdCmdDmyClk: %u(0x%X)\n", cfg->cfg.qpiJedecIdCmdDmyClk, cfg->cfg.qpiJedecIdCmdDmyClk);
    // printk("   sectorSize: %u(0x%X)\n", cfg->cfg.sectorSize, cfg->cfg.sectorSize);
    // printk("   capBase: %u(0x%X)\n", cfg->cfg.capBase, cfg->cfg.capBase);
    // printk("   pageSize: %u(0x%X)\n", cfg->cfg.pageSize, cfg->cfg.pageSize);
    // printk("   chipEraseCmd: %u(0x%X)\n", cfg->cfg.chipEraseCmd, cfg->cfg.chipEraseCmd);
    // printk("   sectorEraseCmd: %u(0x%X)\n", cfg->cfg.sectorEraseCmd, cfg->cfg.sectorEraseCmd);
    // printk("   blk32EraseCmd: %u(0x%X)\n", cfg->cfg.blk32EraseCmd, cfg->cfg.blk32EraseCmd);
    // printk("   blk64EraseCmd: %u(0x%X)\n", cfg->cfg.blk64EraseCmd, cfg->cfg.blk64EraseCmd);
    // printk("   writeEnableCmd: %u(0x%X)\n", cfg->cfg.writeEnableCmd, cfg->cfg.writeEnableCmd);
    // printk("   pageProgramCmd: %u(0x%X)\n", cfg->cfg.pageProgramCmd, cfg->cfg.pageProgramCmd);
    // printk("   qpageProgramCmd: %u(0x%X)\n", cfg->cfg.qpageProgramCmd, cfg->cfg.qpageProgramCmd);
    // printk("   qppAddrMode: %u(0x%X)\n", cfg->cfg.qppAddrMode, cfg->cfg.qppAddrMode);
    // printk("   fastReadCmd: %u(0x%X)\n", cfg->cfg.fastReadCmd, cfg->cfg.fastReadCmd);
    // printk("   frDmyClk: %u(0x%X)\n", cfg->cfg.frDmyClk, cfg->cfg.frDmyClk);
    // printk("   qpiFastReadCmd: %u(0x%X)\n", cfg->cfg.qpiFastReadCmd, cfg->cfg.qpiFastReadCmd);
    // printk("   qpiFrDmyClk: %u(0x%X)\n", cfg->cfg.qpiFrDmyClk, cfg->cfg.qpiFrDmyClk);
    // printk("   fastReadDoCmd: %u(0x%X)\n", cfg->cfg.fastReadDoCmd, cfg->cfg.fastReadDoCmd);
    // printk("   frDoDmyClk: %u(0x%X)\n", cfg->cfg.frDoDmyClk, cfg->cfg.frDoDmyClk);
    // printk("   fastReadDioCmd: %u(0x%X)\n", cfg->cfg.fastReadDioCmd, cfg->cfg.fastReadDioCmd);
    // printk("   frDioDmyClk: %u(0x%X)\n", cfg->cfg.frDioDmyClk, cfg->cfg.frDioDmyClk);
    // printk("   fastReadQoCmd: %u(0x%X)\n", cfg->cfg.fastReadQoCmd, cfg->cfg.fastReadQoCmd);
    // printk("   frQoDmyClk: %u(0x%X)\n", cfg->cfg.frQoDmyClk, cfg->cfg.frQoDmyClk);
    // printk("   fastReadQioCmd: %u(0x%X)\n", cfg->cfg.fastReadQioCmd, cfg->cfg.fastReadQioCmd);
    // printk("   frQioDmyClk: %u(0x%X)\n", cfg->cfg.frQioDmyClk, cfg->cfg.frQioDmyClk);
    // printk("   qpiFastReadQioCmd: %u(0x%X)\n", cfg->cfg.qpiFastReadQioCmd, cfg->cfg.qpiFastReadQioCmd);
    // printk("   qpiFrQioDmyClk: %u(0x%X)\n", cfg->cfg.qpiFrQioDmyClk, cfg->cfg.qpiFrQioDmyClk);
    // printk("   qpiPageProgramCmd: %u(0x%X)\n", cfg->cfg.qpiPageProgramCmd, cfg->cfg.qpiPageProgramCmd);
    // printk("   writeVregEnableCmd: %u(0x%X)\n", cfg->cfg.writeVregEnableCmd, cfg->cfg.writeVregEnableCmd);
    // printk("   wrEnableIndex: %u(0x%X)\n", cfg->cfg.wrEnableIndex, cfg->cfg.wrEnableIndex);
    // printk("   qeIndex: %u(0x%X)\n", cfg->cfg.qeIndex, cfg->cfg.qeIndex);
    // printk("   busyIndex: %u(0x%X)\n", cfg->cfg.busyIndex, cfg->cfg.busyIndex);
    // printk("   wrEnableBit: %u(0x%X)\n", cfg->cfg.wrEnableBit, cfg->cfg.wrEnableBit);
    // printk("   qeBit: %u(0x%X)\n", cfg->cfg.qeBit, cfg->cfg.qeBit);
    // printk("   busyBit: %u(0x%X)\n", cfg->cfg.busyBit, cfg->cfg.busyBit);
    // printk("   wrEnableWriteRegLen: %u(0x%X)\n", cfg->cfg.wrEnableWriteRegLen, cfg->cfg.wrEnableWriteRegLen);
    // printk("   wrEnableReadRegLen: %u(0x%X)\n", cfg->cfg.wrEnableReadRegLen, cfg->cfg.wrEnableReadRegLen);
    // printk("   qeWriteRegLen: %u(0x%X)\n", cfg->cfg.qeWriteRegLen, cfg->cfg.qeWriteRegLen);
    // printk("   qeReadRegLen: %u(0x%X)\n", cfg->cfg.qeReadRegLen, cfg->cfg.qeReadRegLen);
    // printk("   rsvd1: %u(0x%X)\n", cfg->cfg.rsvd1, cfg->cfg.rsvd1);
    // printk("   busyReadRegLen: %u(0x%X)\n", cfg->cfg.busyReadRegLen, cfg->cfg.busyReadRegLen);
    // printk("   readRegCmd[4]: %u(0x%X), %u(0x%X), %u(0x%X), %u(0x%X)\n",
    //         cfg->cfg.readRegCmd[0], cfg->cfg.readRegCmd[0],
    //         cfg->cfg.readRegCmd[1], cfg->cfg.readRegCmd[1],
    //         cfg->cfg.readRegCmd[2], cfg->cfg.readRegCmd[2],
    //         cfg->cfg.readRegCmd[3], cfg->cfg.readRegCmd[3]
    // );
    // printk("   writeRegCmd[4]: %u(0x%X), %u(0x%X), %u(0x%X), %u(0x%X)\n",
    //     cfg->cfg.writeRegCmd[0], cfg->cfg.writeRegCmd[0],
    //     cfg->cfg.writeRegCmd[1], cfg->cfg.writeRegCmd[1],
    //     cfg->cfg.writeRegCmd[2], cfg->cfg.writeRegCmd[2],
    //     cfg->cfg.writeRegCmd[3], cfg->cfg.writeRegCmd[3]
    // );
    // printk("   enterQpi: %u(0x%X)\n", cfg->cfg.enterQpi, cfg->cfg.enterQpi);
    // printk("   exitQpi: %u(0x%X)\n", cfg->cfg.exitQpi, cfg->cfg.exitQpi);
    // printk("   cReadMode: %u(0x%X)\n", cfg->cfg.cReadMode, cfg->cfg.cReadMode);
    // printk("   cRExit: %u(0x%X)\n", cfg->cfg.cRExit, cfg->cfg.cRExit);
    // printk("   burstWrapCmd: %u(0x%X)\n", cfg->cfg.burstWrapCmd, cfg->cfg.burstWrapCmd);
    // printk("   burstWrapCmdDmyClk: %u(0x%X)\n", cfg->cfg.burstWrapCmdDmyClk, cfg->cfg.burstWrapCmdDmyClk);
    // printk("   burstWrapDataMode: %u(0x%X)\n", cfg->cfg.burstWrapDataMode, cfg->cfg.burstWrapDataMode);
    // printk("   burstWrapData: %u(0x%X)\n", cfg->cfg.burstWrapData, cfg->cfg.burstWrapData);
    // printk("   deBurstWrapCmd: %u(0x%X)\n", cfg->cfg.deBurstWrapCmd, cfg->cfg.deBurstWrapCmd);
    // printk("   deBurstWrapCmdDmyClk: %u(0x%X)\n", cfg->cfg.deBurstWrapCmdDmyClk, cfg->cfg.deBurstWrapCmdDmyClk);
    // printk("   deBurstWrapDataMode: %u(0x%X)\n", cfg->cfg.deBurstWrapDataMode, cfg->cfg.deBurstWrapDataMode);
    // printk("   deBurstWrapData: %u(0x%X)\n", cfg->cfg.deBurstWrapData, cfg->cfg.deBurstWrapData);
    // printk("   timeEsector: %u(0x%X)\n", cfg->cfg.timeEsector, cfg->cfg.timeEsector);
    // printk("   timeE32k: %u(0x%X)\n", cfg->cfg.timeE32k, cfg->cfg.timeE32k);
    // printk("   timeE64k: %u(0x%X)\n", cfg->cfg.timeE64k, cfg->cfg.timeE64k);
    // printk("   timePagePgm: %u(0x%X)\n", cfg->cfg.timePagePgm, cfg->cfg.timePagePgm);
    // printk("   timeCe: %u(0x%X)\n", cfg->cfg.timeCe, cfg->cfg.timeCe);
    // printk(" CRC32(%lu): %02X%02X%02X%02X\n",
    //     sizeof(cfg->crc32),
    //     (cfg->crc32 >> 24) & 0xFF,
    //     (cfg->crc32 >> 16) & 0xFF,
    //     (cfg->crc32 >> 8) & 0xFF,
    //     (cfg->crc32 >> 0) & 0xFF
    // );
    // printk("--------\n");

//     return 0;
// }


static int firmware_dump_encryptiondata(const u8 *encryption)
{
    // //FIXME NOT use magic number
    // printk("[************ENCRYPTION DATA (20)************]\n");
    // printk(" IV(16): %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
    //     encryption[0],
    //     encryption[1],
    //     encryption[2],
    //     encryption[3],
    //     encryption[4],
    //     encryption[5],
    //     encryption[6],
    //     encryption[7],
    //     encryption[8],
    //     encryption[9],
    //     encryption[10],
    //     encryption[11],
    //     encryption[12],
    //     encryption[13],
    //     encryption[14],
    //     encryption[15]
    // );
    // printk(" CRC32(4): %02X%02X%02X%02X\n",
    //     encryption[19],
    //     encryption[18],
    //     encryption[17],
    //     encryption[16]
    // );
    // printk("--------\n");

    return 0;
}

static int firmware_dump_publickey(const pkey_cfg_t *key)
{
    // printk("[************PUBLIC KEY DATA(%lu)************]\n", sizeof(pkey_cfg_t));
    // printk(" public key X(%lu):\n", sizeof(key->eckeyx));
    // printk("   %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
    //     key->eckeyx[0],
    //     key->eckeyx[1],
    //     key->eckeyx[2],
    //     key->eckeyx[3],
    //     key->eckeyx[4],
    //     key->eckeyx[5],
    //     key->eckeyx[6],
    //     key->eckeyx[7],
    //     key->eckeyx[8],
    //     key->eckeyx[9],
    //     key->eckeyx[10],
    //     key->eckeyx[11],
    //     key->eckeyx[12],
    //     key->eckeyx[13],
    //     key->eckeyx[14],
    //     key->eckeyx[15],
    //     key->eckeyx[16],
    //     key->eckeyx[17],
    //     key->eckeyx[18],
    //     key->eckeyx[19],
    //     key->eckeyx[20],
    //     key->eckeyx[21],
    //     key->eckeyx[22],
    //     key->eckeyx[23],
    //     key->eckeyx[24],
    //     key->eckeyx[25],
    //     key->eckeyx[26],
    //     key->eckeyx[27],
    //     key->eckeyx[28],
    //     key->eckeyx[29],
    //     key->eckeyx[30],
    //     key->eckeyx[31]
    // );
    // printk(" public key Y(%lu):\n", sizeof(key->eckeyy));
    // printk("   %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
    //     key->eckeyy[0],
    //     key->eckeyy[1],
    //     key->eckeyy[2],
    //     key->eckeyy[3],
    //     key->eckeyy[4],
    //     key->eckeyy[5],
    //     key->eckeyy[6],
    //     key->eckeyy[7],
    //     key->eckeyy[8],
    //     key->eckeyy[9],
    //     key->eckeyy[10],
    //     key->eckeyy[11],
    //     key->eckeyy[12],
    //     key->eckeyy[13],
    //     key->eckeyy[14],
    //     key->eckeyy[15],
    //     key->eckeyy[16],
    //     key->eckeyy[17],
    //     key->eckeyy[18],
    //     key->eckeyy[19],
    //     key->eckeyy[20],
    //     key->eckeyy[21],
    //     key->eckeyy[22],
    //     key->eckeyy[23],
    //     key->eckeyy[24],
    //     key->eckeyy[25],
    //     key->eckeyy[26],
    //     key->eckeyy[27],
    //     key->eckeyy[28],
    //     key->eckeyy[29],
    //     key->eckeyy[30],
    //     key->eckeyy[31]
    // );
    // printk(" CRC32(%lu): %02X%02X%02X%02X\n",
    //     sizeof(key->crc32),
    //     (key->crc32 >> 24) & 0xFF,
    //     (key->crc32 >> 16) & 0xFF,
    //     (key->crc32 >> 8) & 0xFF,
    //     (key->crc32 >> 0) & 0xFF
    // );
    // printk("--------\n");

    return 0;
}

static int firmware_dump_signdata(const sign_cfg_t *sign)
{
    // int i;

    // printk("[************SIGNATURE DATA(%lu)************]\n", sizeof(sign_cfg_t) + sign->sig_len);
    // printk(" sig_len: %u(0x%X)\n", sign->sig_len, sign->sig_len);
    // printk(" signature(%u):\n", sign->sig_len);

    // for (i = 0; i < sign->sig_len; i+= 32) {
    //     printk("   %03u: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
    //         i,
    //         sign->signature[i + 0],
    //         sign->signature[i + 1],
    //         sign->signature[i + 2],
    //         sign->signature[i + 3],
    //         sign->signature[i + 4],
    //         sign->signature[i + 5],
    //         sign->signature[i + 6],
    //         sign->signature[i + 7],
    //         sign->signature[i + 8],
    //         sign->signature[i + 9],
    //         sign->signature[i + 10],
    //         sign->signature[i + 11],
    //         sign->signature[i + 12],
    //         sign->signature[i + 13],
    //         sign->signature[i + 14],
    //         sign->signature[i + 15],
    //         sign->signature[i + 16],
    //         sign->signature[i + 17],
    //         sign->signature[i + 18],
    //         sign->signature[i + 19],
    //         sign->signature[i + 20],
    //         sign->signature[i + 21],
    //         sign->signature[i + 22],
    //         sign->signature[i + 23],
    //         sign->signature[i + 24],
    //         sign->signature[i + 25],
    //         sign->signature[i + 26],
    //         sign->signature[i + 27],
    //         sign->signature[i + 28],
    //         sign->signature[i + 29],
    //         sign->signature[i + 30],
    //         sign->signature[i + 31]
    //     );
    // }
    // printk(" CRC32(%lu): %02X%02X%02X%02X\n",
    //     sizeof(sign->crc32),
    //     sign->signature[sign->sig_len],
    //     sign->signature[sign->sig_len + 1],
    //     sign->signature[sign->sig_len + 2],
    //     sign->signature[sign->sig_len + 3]
    // );
    // printk("--------\n");
    return 0;
}

static int firmware_dump_segment_piece(const segment_header_t *segment)
{
    // printk("[************SEGMENT DUMP(%lu + %u = %lu)************]\n",
    //     sizeof(segment_header_t),
    //     segment->len,
    //     sizeof(segment_header_t) + segment->len
    // );
    // printk(" destaddr: %u(0x%X)\n", segment->destaddr, segment->destaddr);
    // printk(" len: %u(0x%X)\n", segment->len, segment->len);
    // printk(" rsv: %u(0x%X)\n", segment->rsv, segment->rsv);
    // printk(" crc32: %u(0x%X)\n", segment->crc32, segment->crc32);
    // printk("--------\n");
    return 0;
}

static int firmware_download_head(struct bl_plat *bl_plat, 
                                bootrom_host_cmd_t *cmd, u8* response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int ret;

    BL_DBG(USB_FN_ENTRY_STR);
    printk("FUNC header: data ptr %p, data left %d\n", *data, *len);
    
    if (*len < sizeof(bootheader_t)) {
        printk("%s:len too small %zu:%d\n", __func__, sizeof(bootheader_t), *len);
        return -1;
    }

    firmware_dump_head(header);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_bootheader_load(cmd, header);
    printk("CMD bootheader len %d\n", cmd->len);
    
    ret = bl_write_data_sync(bl_plat, (u8*)cmd, sizeof(bootrom_host_cmd_t)+ cmd->len, 
                             BL_USB_EP_OUT, BL_USB_TIMEOUT);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    
    ret = bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE,
                            BL_USB_EP_IN, BL_USB_TIMEOUT);
    if (ret < 0) {
        printk("response get err:%d",ret);
        return -1;
    }
    bl_bootrom_cmd_bootheader_load_get_res((bootrom_res_bootheader_load_t*)response);

    *data += sizeof(bootheader_t);
    *len -= sizeof(bootheader_t);

    BL_DBG(USB_FN_LEAVE_STR);

    return 0;
}

static int firmware_download_encryptiondata(struct bl_plat *bl_plat, 
                                bootrom_host_cmd_t *cmd, u8 *response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int ret;

    BL_DBG(USB_FN_ENTRY_STR);

    if (bl_mod_params.mp_mode)
        return 0;

    printk("FUNC encryptiondata: data ptr %p, data left %d\n", *data, *len);
    if (0 == encryption_mode) {
        printk("no encrypt field detected\n");
        return 0;
    }

    printk("%s, Offset 0x%X\n", __func__, (PTR2UINT)(((u8*)*data) - ((u8*)header)));
    //FIXME NOT use magic number
    if (*len < 20) {
        printk("%s:len too small %d:%d\n", __func__, 20, *len);
        return -1;
    }

    firmware_dump_encryptiondata(*data);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_aesiv_load(cmd, *data);
    printk("CMD encryptiondata len %d\n", cmd->len);
    
    ret = bl_write_data_sync(bl_plat, (u8*)cmd, sizeof(bootrom_host_cmd_t)+ cmd->len, 
                             BL_USB_EP_OUT, BL_USB_TIMEOUT);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    ret = bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE, 
                            BL_USB_EP_IN, BL_USB_TIMEOUT);
    if (ret < 0) {
        printk("response get err:%d",ret);
        return -1;
    }
    bl_bootrom_cmd_aesiv_load_get_res((bootrom_res_aesiv_load_t*)response);

    //FIXME NOT use magic number
    *data += 20;
    *len -= 20;

    BL_DBG(USB_FN_LEAVE_STR);

    return 0;
}

static int firmware_download_publickey1(struct bl_plat *bl_plat, 
                                bootrom_host_cmd_t *cmd, u8 *response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int ret;
    pkey_cfg_t *key;

    if (bl_mod_params.mp_mode)
        return 0;

    printk("FUNC publickey1: data ptr %p, data left %d\n", *data, *len);
    if (0 == signature_mode) {
        printk("no sign field detected\n");
        return 0;
    }

    printk("%s, Offset 0x%X\n", __func__, (PTR2UINT)(((u8*)*data) - ((u8*)header)));
    key = (pkey_cfg_t*)(*data);
    if (*len < sizeof(pkey_cfg_t)) {
        printk("%s:len too small %zu:%d\n", __func__, sizeof(pkey_cfg_t), *len);
        return -1;
    }
    firmware_dump_publickey(key);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_pkey1_load(cmd, key);
    printk("CMD public key1 len %d\n", cmd->len);
    
    ret = bl_write_data_sync(bl_plat, (u8*)cmd, sizeof(bootrom_host_cmd_t)+ cmd->len, 
          BL_USB_EP_OUT, BL_USB_TIMEOUT);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    ret = bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE, 
                            BL_USB_EP_IN, BL_USB_TIMEOUT);
    if (ret < 0) {
        printk("response get err:%d",ret);
        return -1;
    }
    ret = bl_bootrom_cmd_pkey1_load_get_res((bootrom_res_pkey_load_t*)response);
    if (ret) {
        printk("Error response get pkey1\n");
        return -1;
    }

    *data += sizeof(pkey_cfg_t);
    *len -= sizeof(pkey_cfg_t);

    return 0;
}


static int firmware_download_signdata1(struct bl_plat *bl_plat, 
                                bootrom_host_cmd_t *cmd, u8 *response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int ret;
    sign_cfg_t *sign;

    if (bl_mod_params.mp_mode)
        return 0;

    printk("FUNC signdata1: data ptr %p, data left %d\n", *data, *len);
    if (0 == signature_mode) {
        printk("no sign field detected\n");
        return 0;
    }

    printk("%s, Offset 0x%X\n", __func__, 
          (PTR2UINT)(((u8*)*data) - ((u8*)header)));
          
    sign = (sign_cfg_t*)(*data);
    if (*len < sizeof(sign_cfg_t) + sign->sig_len) {
        printk("%s:len too small %zu:%d\n", __func__, sizeof(sign_cfg_t), *len);
        return -1;
    }

    firmware_dump_signdata(sign);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_signature1_load(cmd, (uint8_t*)sign, 
                                   sizeof(sign_cfg_t) + sign->sig_len);
    printk("CMD signa data1 len %d\n", cmd->len);
    
    ret = bl_write_data_sync(bl_plat, (u8*)cmd, sizeof(bootrom_host_cmd_t)+ cmd->len, 
                             BL_USB_EP_OUT, BL_USB_TIMEOUT);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    ret = bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE, 
                            BL_USB_EP_IN, BL_USB_TIMEOUT);
    if (ret < 0) {
        printk("response get err:%d",ret);
        return -1;
    }
    ret = bl_bootrom_cmd_signature1_get_res((bootrom_res_signature_load_t*)response);
    if (ret) {
        printk("Error response get signature1\n");
        return -1;
    }

    *data += (sizeof(sign_cfg_t) + sign->sig_len);
    *len -= (sizeof(sign_cfg_t) + sign->sig_len);

    return 0;
}


static int firmware_download_segment_piece(struct bl_plat *bl_plat, 
                              bootrom_host_cmd_t *cmd, u8 *response,
                              bootheader_t *header, const u8 **data, int *len)
{
    const segment_header_t *segment;
    segment_header_t segment_plain;
    int ret = 0, i, pos, wr_len;

    BL_DBG(USB_FN_ENTRY_STR);

    BL_DBG("FUNC segment_piece: data ptr %p, data left %d\n", *data, *len);
    segment = (const segment_header_t*)(*data);
    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_sectionheader_load(cmd, segment);
    BL_DBG("CMD segment piece len %d\n", cmd->len);
    
    ret = bl_write_data_sync(bl_plat, (u8*)cmd, sizeof(bootrom_host_cmd_t)+ cmd->len,
                             BL_USB_EP_OUT, BL_USB_TIMEOUT);
    if (ret) {
        printk("ERR when write\n");
        
        return -1;
    }
    
    ret = bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE, 
                            BL_USB_EP_IN, BL_USB_TIMEOUT);
    if (ret < 0) {
        printk("response get err:%d",ret);
        
        return -1;
    }
    
    ret = bl_bootrom_cmd_sectionheader_get_res((bootrom_res_sectionheader_load_t*)response);
    if (ret) {
        printk("response Error\n");
        return -1;
    }
    
    memcpy(&segment_plain,
           &(((bootrom_res_sectionheader_load_t*)response)->header), 
           sizeof(segment_plain));
    segment = &segment_plain;
    
    BL_DBG("Get SEGMENT From BL616: destaddr: 0x%08x, len %d, rsv %08X, CRC32 %08X\n",
            segment->destaddr, segment->len, segment->rsv, segment->crc32);

    if (*len < (sizeof(segment_header_t) + segment->len)) {
        printk("SEGMENT: buffer too small %zu required:%d actual to 0x%x\n",
            sizeof(segment_header_t) + segment->len,
            *len,
            segment->destaddr
        );
        
        return -1;
    }
    firmware_dump_segment_piece(segment);

    if (bl_plat->chip_ver == CHIP_VER_616_A1)
        bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE, BL_USB_EP_IN, 5);

    pos = 0;
    i = 0;
    while (pos < segment->len) {
#define WR_LEN ((BOOTROM_DATA_BUFFER_SIZE - sizeof(bootrom_host_cmd_t)) & 0xFFFFFFF0)
        wr_len = segment->len - pos;
        wr_len = (wr_len < WR_LEN) ?  wr_len : WR_LEN;
        
        BL_DBG("------piece %03d[%03d] %d:%u------\n", i++, wr_len, pos, segment->len);

        /*data len is less than one packet*/
        BL_DBG("data SRC[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X\n",
            (*data + sizeof(segment_header_t) + pos)[0],
            (*data + sizeof(segment_header_t) + pos)[1],
            (*data + sizeof(segment_header_t) + pos)[2],
            (*data + sizeof(segment_header_t) + pos)[3],
            (*data + sizeof(segment_header_t) + pos)[4],
            (*data + sizeof(segment_header_t) + pos)[5],
            (*data + sizeof(segment_header_t) + pos)[6],
            (*data + sizeof(segment_header_t) + pos)[7]
        );

        bl_bootrom_cmd_sectiondata_load(cmd, *data + sizeof(segment_header_t) + pos, wr_len);

        BL_DBG("data[%u] CMD[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X\n"
               "     TAL[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X\n",
            cmd->len,
            cmd->data[0],
            cmd->data[1],
            cmd->data[2],
            cmd->data[3],
            cmd->data[4],
            cmd->data[5],
            cmd->data[6],
            cmd->data[7],
            cmd->data[cmd->len - 1 - 7],
            cmd->data[cmd->len - 1 - 6],
            cmd->data[cmd->len - 1 - 5],
            cmd->data[cmd->len - 1 - 4],
            cmd->data[cmd->len - 1 - 3],
            cmd->data[cmd->len - 1 - 2],
            cmd->data[cmd->len - 1 - 1],
            cmd->data[cmd->len - 1 - 0]
        );

        BL_DBG("CMD segment piece loop len %d\n", cmd->len);
        ret = bl_write_data_sync(bl_plat, (u8*)cmd, sizeof(bootrom_host_cmd_t)+ cmd->len,
                                 BL_USB_EP_OUT, BL_USB_TIMEOUT);
        if (ret) {
            printk("ERR when write\n");
            
            return -1;
        }
        
        ret = bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE,
                                BL_USB_EP_IN, BL_USB_TIMEOUT);
        if (ret < 0) {
            printk("response get err:%d",ret);
            
            return -1;
        }
        bl_bootrom_cmd_sectiondata_get_res((bootrom_res_sectiondata_load_t*)response);

        pos += wr_len;

        if (bl_plat->chip_ver == CHIP_VER_616_A1)
        {
            bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE, BL_USB_EP_IN, 5);
            //msleep(5);
        }
    }

    *data += (sizeof(segment_header_t) + segment->len);
    *len -= (sizeof(segment_header_t) + segment->len);

    BL_DBG(USB_FN_LEAVE_STR);

    return 0;
}

static int firmware_download_segment(struct bl_plat *bl_plat,
                                bootrom_host_cmd_t *cmd, u8 *response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int i, ret = 0;

    printk("SEGMENT AERA: @Offset 0x%X\n", (PTR2UINT)((u8*)(*data) - (u8*)(header)));
    printk("FUNC segment: data ptr %p, data left %d\n", *data, *len);
    
    for (i = 0; i < header->basic_cfg.img_len_cnt; i++) {
        printk("------SEGMENT[%02d]@0x%X------\n", i,
               (PTR2UINT)((u8*)(*data) - (u8*)(header)));
               
        ret = firmware_download_segment_piece(bl_plat, cmd, response, header, data, len);
        if (ret) {
            break;
        }
    }

    return ret;
}

static int firmware_download_image_check(struct bl_plat *bl_plat,
                   bootrom_host_cmd_t *cmd, u8 *response, bootheader_t *header)
{
    int ret;

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_checkimage_get(cmd);
    
    printk("CMD checkimage len %d\n", cmd->len);
    
    ret = bl_write_data_sync(bl_plat, (u8*)cmd, sizeof(bootrom_host_cmd_t)+ cmd->len, 
                             BL_USB_EP_OUT, BL_USB_TIMEOUT);
    if (ret) {
        printk("ERR when write\n");
        
        return -1;
    }
    
    ret = bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE, 
                            BL_USB_EP_IN, BL_USB_TIMEOUT);
    if (ret < 0) {
        printk("response get err:%d",ret);
        
        return -1;
    }
    
    ret = bl_bootrom_cmd_checkimage_get_res((bootrom_res_checkimage_t*)response);
    if (ret) {
        printk("Error response get checkimage\n");
        
        return -1;
    }

    return 0;
}

static int firmware_download_image_run(struct bl_plat *bl_plat, 
                   bootrom_host_cmd_t *cmd, u8 *response, bootheader_t *header)
{
    int ret;

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_runimage_get(cmd);
    printk("CMD runimage len %d\n", cmd->len);
    ret = bl_write_data_sync(bl_plat, (u8*)cmd, sizeof(bootrom_host_cmd_t)+ cmd->len, 
                             BL_USB_EP_OUT, BL_USB_TIMEOUT);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    
    ret = bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE, 
                            BL_USB_EP_IN, BL_USB_TIMEOUT);
    if (ret < 0) {
        printk("response get err:%d",ret);
        
        return -1;
    }
    bl_bootrom_cmd_runimage_get_res((bootrom_res_runimage_t*)response);

    return 0;
}

static int firmware_downloader(struct bl_plat *bl_plat, const u8 *data, int len)
{
    int ret = 0;
    bootheader_t *header;
    bootrom_host_cmd_t *cmd;
    u8 *response;

    if (len < sizeof(bootheader_t)) {
        printk("Illegal len when check bootheader_t\n");
        
        return -EFAULT;
    }
    header = (bootheader_t*)data;
    cmd = kzalloc(BOOTROM_DATA_BUFFER_SIZE, GFP_KERNEL);
    if (NULL == cmd) {
        printk("NO MEM for alloc host CMD\n");
        
        return -ENOMEM;
    }

    response = kzalloc(BOOTROM_CMD_BUFFER_SIZE, GFP_KERNEL);
    if (NULL == response) {
        printk("NO MEM for alloc CMD response\n");
        
        kfree(cmd);
        return -ENOMEM;
    }

    /*do NOT change the order bellow*/
    if (firmware_download_head(bl_plat, cmd, response, header, &data, &len)
        || firmware_download_publickey1(bl_plat, cmd, response, header, &data, &len)
        || firmware_download_signdata1(bl_plat, cmd, response, header, &data, &len)
        || firmware_download_encryptiondata(bl_plat, cmd, response, header, &data, &len)
        || firmware_download_segment(bl_plat, cmd, response, header, &data, &len)
        || firmware_download_image_check(bl_plat, cmd, response, header)) {
        printk("firmware load faield\n");
        return -EFAULT;
    }
    
    /*check if we need to download the other image*/
    // if (len > 0) {
    //     printk("Potential Second image found\n");
    //     header = (bootheader_t*)data;//update header to the next image
    //     if (firmware_download_head(bl_hw, cmd, response, header, &data, &len)
    //         || firmware_download_flashcfg(bl_hw, cmd, response, header)
    //         || firmware_download_pllcfg(bl_hw, cmd, response, header)
    //         || firmware_download_publickey1(bl_hw, cmd, response, header, &data, &len)
    //         //|| firmware_download_publickey2(bl_hw, cmd, response, header, &data, &len)
    //         || firmware_download_signdata1(bl_hw, cmd, response, header, &data, &len)
    //         //|| firmware_download_signdata2(bl_hw, cmd, response, header, &data, &len)
    //         || firmware_download_encryptiondata(bl_hw, cmd, response, header, &data, &len)
    //         || firmware_download_segment(bl_hw, cmd, response, header, &data, &len)
    //         || firmware_download_image_check(bl_hw, cmd, response, header)) {
    //         printk("Sencond firmware load faield\n");
    //         return -1;
    //     }
    // }
    
    if (firmware_download_image_run(bl_plat, cmd, response, header)) {
        printk("firmware check/Run faield\n");
        
        ret = -EFAULT;
    }

    kfree(cmd);
    kfree(response);
    return ret;
}

static int bl_usb_checkversion(struct bl_plat *bl_plat)
{
    bootrom_host_cmd_t *cmd;
    int ret = 0;
    u8 *response;

    BL_DBG(USB_FN_ENTRY_STR);

    cmd = kzalloc(BOOTROM_CMD_BUFFER_SIZE, GFP_KERNEL);
    if (NULL == cmd) {
        printk("NO MEM for alloc host CMD\n");
        
        return -ENOMEM;
    }
    
    response = kzalloc(BOOTROM_CMD_BUFFER_SIZE, GFP_KERNEL);
    if (NULL == response) {
        kfree(cmd);
        
        printk("NO MEM for alloc CMD response\n");
        
        return -ENOMEM;
    }

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    memset(response, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_bootinfo_get(cmd);
    
    printk("CMD check version len %d\n", cmd->len);
    
    ret = bl_write_data_sync(bl_plat, (u8*)cmd, 
                             sizeof(bootrom_host_cmd_t)+cmd->len, 
                             BL_USB_EP_OUT, BL_USB_TIMEOUT);
    if (ret) {
        kfree(response);
        kfree(cmd);

        printk("ERR when write\n");
        
        return -EFAULT;
    }
    
    ret = bl_read_data_sync(bl_plat, response, BOOTROM_CMD_BUFFER_SIZE, 
                            BL_USB_EP_IN, BL_USB_TIMEOUT);
    if (ret < 0) {
        kfree(response);
        kfree(cmd);
        
        printk("response get err:%d",ret);
        
        return -EFAULT;
    }
    
    bl_bootrom_cmd_bootinfo_get_res((bootrom_res_bootinfo_t*)response);
    bl_plat->chip_ver = ((bootrom_res_bootinfo_t*)response)->version;

    signature_mode = ((bootrom_res_bootinfo_t*)response)->signature_mode;
    encryption_mode = ((bootrom_res_bootinfo_t*)response)->encryption_mode;

    kfree(response);
    kfree(cmd);
    
    BL_DBG(USB_FN_LEAVE_STR);

    return ret;
}

static int bl_usb_download_firmware(struct bl_plat *bl_plat)
{
    const struct firmware *fw_helper = NULL;
    int ret = 0, imagelen;
    const u8 *image = NULL;
    ktime_t start;
    s64 consume_time;
    char *fw_name = NULL;
    struct bl_usb_device *device;
    
    device = (struct bl_usb_device *)(bl_plat->priv);

#ifdef CONFIG_BL_DNLD_FWBIN
    if (bl_plat->chip_ver == CHIP_VER_616L) {
#ifdef CONFIG_FW_COMBO
        fw_name = BL616L_USB_COMBO_FW_NAME;
#else
        fw_name = BL616L_USB_WLAN_FW_NAME;
#endif

        if (bl_mod_params.mp_mode) {
            fw_name = BL616L_USB_MFG_FW_NAME;        
        }
    } else {
#ifdef CONFIG_FW_COMBO
        fw_name = BL616_USB_COMBO_FW_NAME;
#else
        fw_name = BL616_USB_WLAN_FW_NAME;
#endif

        if (bl_mod_params.mp_mode) {
            fw_name = BL616_USB_MFG_FW_NAME;        
        }
    }
    
    printk("Enter bl_usb_download_firmware...\n");
    printk("to download usb fw bin %s\n", fw_name);
    
    ret = request_firmware(&fw_helper, fw_name, &device->intf->dev);
    if ((ret < 0) || !fw_helper) {
        printk("request_firmware failed, error code = %d", ret);
        
        ret = -ENOENT;
        return ret;
    }

    image = fw_helper->data;
    imagelen = fw_helper->size;
#else //CONFIG_BL_DNLD_FWBIN
#ifdef CONFIG_BL_MP
    if (bl_mod_params.mp_mode){
        if (bl_plat->chip_ver == CHIP_VER_616L) {
            image = bl616l_usb_mp_bin;
            imagelen = bl616l_usb_mp_bin_len;
        } else {
            image = bl616_usb_mp_bin;
            imagelen = bl616_usb_mp_bin_len;
        }
        
        printk("to download usb mfg fw array\n");
    } else {
#endif
#ifdef CONFIG_FW_COMBO
        if (bl_plat->chip_ver == CHIP_VER_616L) {
            image = bl616l_usb_combo_bin;
            imagelen = bl616l_usb_combo_bin_len;
        } else {
            image = bl616_usb_combo_bin;
            imagelen = bl616_usb_combo_bin_len;
        }
        
        printk("to download usb_combo fw array\n");
#else
        if (bl_plat->chip_ver == CHIP_VER_616L) {
            image = bl616l_usb_wlan_bin;
            imagelen = bl616l_usb_wlan_bin_len;
        } else {
            image = bl616_usb_wlan_bin;
            imagelen = bl616_usb_wlan_bin_len;
        }

        printk("to download usb_wifi only fw array\n");
#endif
#ifdef CONFIG_BL_MP
    }
#endif    
#endif
  
    BL_DBG("Downloading image (%d bytes)\n", imagelen);
    
    //time_start = jiffies;
    start = ktime_get();
    firmware_downloader(bl_plat, image, imagelen);
    consume_time = ktime_us_delta(ktime_get(), start);
    
    BL_DBG("Download fw time: %lld\n", consume_time);

    //time_end = jiffies;
#ifdef CONFIG_BL_DNLD_FWBIN
    release_firmware(fw_helper);
#endif

    return ret;

}

int bl_usb_dnld_fw(struct bl_plat *bl_plat)
{
    u32 ret = 0;

    printk("Enter bl_usb_dnld_fw ....\n");

    ret = bl_usb_run_fw(bl_plat);
    ret = bl_usb_checkversion(bl_plat);
    ret = bl_usb_download_firmware(bl_plat);

    return ret;
}

int bl_write_data_sync(struct bl_plat *bl_plat, u8 *pbuf,
                             u32 len, u8 ep, u32 timeout)
{
    int actual_length, ret;
    struct bl_usb_device *device;
    device = (struct bl_usb_device *)(bl_plat->priv);

    /* Send the data block */
    ret = usb_bulk_msg(device->udev, usb_sndbulkpipe(device->udev, ep), pbuf,
                       len, &actual_length, timeout);
    if (ret) {
        if (bl_plat->chip_ver != CHIP_VER_616_A1)
           printk("usb_bulk_msg for tx failed: %d\n", ret);
        
        /* Send the data block again*/
        ret = usb_bulk_msg(device->udev, usb_sndbulkpipe(device->udev, ep), pbuf,
                           len, &actual_length, timeout);
        if (ret) {
            if (bl_plat->chip_ver != CHIP_VER_616_A1)
                printk("usb_bulk_msg for tx failed again: %d\n", ret);
        }
    }

    return ret;
}

int bl_read_data_sync(struct bl_plat *bl_plat, u8 *pbuf,
                             u32 len, u8 ep, u32 timeout)
{
    int actual_length, ret;
    struct bl_usb_device *device;
    int try_cnt = 3;
    
    device = (struct bl_usb_device *)(bl_plat->priv);

retry:

    /* Receive the data response */
    ret = usb_bulk_msg(device->udev, usb_rcvbulkpipe(device->udev, ep), pbuf,
                        len, &actual_length, timeout);
    if (ret) {
        if (bl_plat->chip_ver != CHIP_VER_616_A1) {
            printk("usb_bulk_msg for rx failed: %d\n", ret);
        }
        
        return ret;
    }

    if (actual_length == 0 && try_cnt) {
        try_cnt--;
        goto retry;
    }
    
    return actual_length;
}

static int bl_usb_tx_init(struct bl_hw *bl_hw)
{
    int i = 0;
    struct bl_usb_device *device;
    
    device = (struct bl_usb_device *)(bl_hw->plat)->priv;
    /*init cmd urb*/
    device->tx_cmd.bl_hw = bl_hw;
    #if !defined(CONFIG_CMD_USB_EP)
    device->tx_cmd.ep = device->tx_ep;
    #endif
    device->tx_cmd.flag = BL_USB_EP_MSG; 
    device->tx_cmd.skb = NULL;
    device->tx_cmd.a2e_msg = NULL;
    device->tx_cmd.urb = usb_alloc_urb(0, GFP_KERNEL);
    
    if (!device->tx_cmd.urb) {
        printk("alloc tx_cmd urb failed\n");
        
        return -ENOMEM;
    }

    /*init data urb*/
    for (i = 0; i < BL_TX_DATA_URB; i++) {
        device->tx_data_list[i].bl_hw = bl_hw;
        device->tx_data_list[i].ep = device->tx_ep;
        device->tx_data_list[i].flag = BL_USB_EP_DATA;
        device->tx_data_list[i].skb = NULL;
        device->tx_data_list[i].urb = usb_alloc_urb(0, GFP_KERNEL);
        
        if (!device->tx_data_list[i].urb) {
            printk("alloc %d data urb failed\n", i);
            
            return -ENOMEM;
        }
    }

    return 0;
}

static int bl_usb_tx_deinit(struct bl_hw *bl_hw)
{
    int i = 0;
    struct bl_usb_device *device;
    
    device = (struct bl_usb_device *)(bl_hw->plat)->priv;
    
    if (device->tx_cmd.a2e_msg) {
        printk("%s, tx_cmd ongoing, call kill, skb:%p, a2e_msg:%p\n",
                __func__, device->tx_cmd.skb, device->tx_cmd.a2e_msg);
                
        usb_kill_urb(device->tx_cmd.urb);
    }
    
    if (device->tx_cmd.urb) {
        usb_free_urb(device->tx_cmd.urb);
        device->tx_cmd.urb = NULL;
    }

    for (i = 0; i < BL_TX_DATA_URB; i++) {
        if (device->tx_data_list[i].skb) {
            printk("%s, tx_data ongoing, call kill, skb:%p\n", 
                   __func__, device->tx_data_list[i].skb);
                   
            usb_kill_urb(device->tx_data_list[i].urb);
        }
        
        if (device->tx_data_list[i].urb) {
            usb_free_urb(device->tx_data_list[i].urb);
            device->tx_data_list[i].urb = NULL;
        }
    }

    return 0;
}

static void bl_usb_rx_complete(struct urb *urb)
{
    struct urb_context *context = (struct urb_context *)urb->context;
    struct bl_hw *bl_hw = context->bl_hw;
    struct sk_buff *skb = context->skb;
    int recv_length = urb->actual_length;
    struct bl_usb_device *device;
    int size, status;
    u16 pkt_type;
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif

    status = urb->status;
    device = (struct bl_usb_device *)(bl_hw->plat)->priv;
    atomic_dec(&device->rx_data_urb_pending);
    
    BL_DBG("%s: rx_data_urb_pending=%d\n",
           __func__, atomic_read(&device->rx_data_urb_pending));

    if(status) {
        dev_err(&device->intf->dev, "%s: urb status: %d\n",
            __func__, status);
            
        if (skb != NULL) {
            dev_kfree_skb_any(skb);
        }
        
        if (status == -ENOENT || status == -ECONNRESET || status == -ESHUTDOWN) {
            printk("%s, not submit rx urb again\n", __func__);
            
            return;
        }

        goto setup_for_next;
    }

    //In rare situation, host memory tight.
    if (skb == NULL) {
        dev_err(&device->intf->dev, "%s: urb status: %d, skb null\n",
                __func__, status);
                
        goto setup_for_next;
    }

    if (recv_length) {
        pkt_type = le16_to_cpu(*(__le16 *) (skb->data + 2));
        
        BL_DBG("pkt_type:%d, recv_len=%d, skb:0x%p, skb->len=%d\n", 
               pkt_type, recv_length, skb, skb->len);
        
        if (skb->len > recv_length)
            skb_trim(skb, recv_length);
        else
            skb_put(skb, recv_length - skb->len);

        if (!in_irq())
            BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
        else
            spin_lock(&bl_hw->rx_process_lock);
            
        skb_queue_tail(&bl_hw->rx_pkt_list, skb);
        
        if (!in_irq())
            BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
        else
            spin_unlock(&bl_hw->rx_process_lock);
            
        if (bl_hw->rx_work_flag)
            bl_queue_rx_work(bl_hw);
        else
            bl_queue_main_work(bl_hw);
    } else {
        BL_DBG("wrong recv_len=%d, skb->len=%d\n", recv_length, skb->len);
        
        dev_kfree_skb_any(skb);
    }
    
setup_for_next:
    context->skb = NULL;
    size = BL_RX_DATA_BUF_SIZE;

    if(unlikely(atomic_read(&device->rx_data_urb_pending) >= BL_RX_DATA_URB)) {
        BL_DBG("%s: rx overflow, rx_data_urb_pending=%d\n", __func__, 
               atomic_read(&device->rx_data_urb_pending));
    } else if (atomic_read(&device->rx_data_urb_pending) >= (BL_RX_DATA_URB/2)) {
        BL_DBG("%s: resubmit set false, rx_data_urb_pending=%d, urb=%p\n", 
                __func__, atomic_read(&device->rx_data_urb_pending), urb);
                
        context->resubmit = false;
    } else {
        BL_DBG("%s: resubmit urb, rx_data_urb_pending=%d\n", __func__, 
                atomic_read(&device->rx_data_urb_pending));
                
        context->resubmit = false;
        bl_usb_resubmit_rx_urb(context, size);
    }
    
    return;
}

static int bl_usb_resubmit_rx_urb(struct urb_context *ctx, int size)
{
    struct bl_hw *bl_hw = ctx->bl_hw;
    struct bl_usb_device *device;
    int i, cnt = 0;

    BL_DBG("Enter bl_usb_resubmit_rx_urb\n");
    
    device = (struct bl_usb_device *)(bl_hw->plat)->priv;
    if(bl_hw->surprise_removed)
        return 0;

    for (i = 0; i < BL_RX_DATA_URB; i++) {
        //check urb, make sure the context is valid
        if(device->rx_data_list[i].resubmit == false && 
           device->rx_data_list[i].urb) 
        {
            do {
                device->rx_data_list[i].skb = dev_alloc_skb(size);
                cnt ++;
            } while ((cnt < 10) && !device->rx_data_list[i].skb);
            
            if (!device->rx_data_list[i].skb) {
                device->rx_data_list[i].resubmit = false;

                printk("%s: urb[%d] dev_alloc_skb failed , urb pending cnt %d \n",
                      __func__, i, atomic_read(&device->rx_data_urb_pending));
                      
                bl_nl_broadcast_event(bl_hw, BL_EVENT_ID_OUT_MEM, NULL, 0);
                //return -ENOMEM;
                
                continue;
            }
            
            BL_DBG("%s: [%d]urb=%p\n", __func__, i, device->rx_data_list[i].urb);

            usb_fill_bulk_urb(device->rx_data_list[i].urb, device->udev,
                  usb_rcvbulkpipe(device->udev, device->rx_data_list[i].ep), 
                  device->rx_data_list[i].skb->data,
                  size, bl_usb_rx_complete, (void *)&(device->rx_data_list[i]));

            atomic_inc(&device->rx_data_urb_pending);
            
            BL_DBG("%s: rx_data_urb_pending=%d\n", __func__, 
                   atomic_read(&device->rx_data_urb_pending));

            if (usb_submit_urb(device->rx_data_list[i].urb, GFP_ATOMIC)) {
                printk("usb_submit_urb failed\n");
                
                dev_kfree_skb_any(device->rx_data_list[i].skb);
                device->rx_data_list[i].skb = NULL;
                atomic_dec(&device->rx_data_urb_pending);
                
                return -1;
            }

            device->rx_data_list[i].resubmit = true;            
        }
    }

    return 0;
}

static int bl_usb_submit_rx_urb(struct urb_context *ctx, int size)
{
    struct bl_hw *bl_hw = ctx->bl_hw;
    struct bl_usb_device *device;
    device = (struct bl_usb_device *)(bl_hw->plat)->priv;

    BL_DBG("Enter bl_usb_submit_rx_urb\n");

    ctx->skb = dev_alloc_skb(size);
    if (!ctx->skb) {
        printk("%s: dev_alloc_skb failed\n", __func__);
        
        return -ENOMEM;
    }

#if 0
    if (BL_USB_EP_MSG != ctx->flag) {
        ctx->skb = dev_alloc_skb(size);
        if (!ctx->skb) {
            printk("%s: dev_alloc_skb failed\n", __func__);
            return -ENOMEM;
        }
    }
#endif

    usb_fill_bulk_urb(ctx->urb, device->udev,
                      usb_rcvbulkpipe(device->udev, ctx->ep), ctx->skb->data,
                      size, bl_usb_rx_complete, (void *)ctx);

    //if (BL_USB_EP_MSG == ctx->flag)
    //    atomic_inc(&device->rx_cmd_urb_pending);
    //else
        atomic_inc(&device->rx_data_urb_pending);
        
    BL_DBG("%s: rx_data_urb_pending=%d\n", __func__, 
           atomic_read(&device->rx_data_urb_pending));

    if (usb_submit_urb(ctx->urb, GFP_ATOMIC)) {
        printk("usb_submit_urb failed\n");
        
        dev_kfree_skb_any(ctx->skb);
        ctx->skb = NULL;

        //if (BL_USB_EP_MSG == ctx->flag)
        //    atomic_dec(&device->rx_cmd_urb_pending);
        //else
            atomic_dec(&device->rx_data_urb_pending);

        return -1;
    }

    return 0;
}

static int bl_usb_rx_init(struct bl_hw *bl_hw)
{
    int i;
    struct bl_usb_device *device;
    device = (struct bl_usb_device *)(bl_hw->plat)->priv;

#if 0
    /*init cmd urb context*/
    device->rx_cmd.bl_hw = bl_hw;
    device->rx_cmd.ep = device->rx_ep;
    device->rx_cmd.flag = BL_USB_EP_MSG;
    device->rx_cmd.skb = NULL;
    device->rx_cmd.urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!device->rx_cmd.urb) {
        printk("alloc rx_cmd urb failed\n");
        return -ENOMEM;
    }

    device->rx_cmd.skb = dev_alloc_skb(BL_RX_CMD_BUF_SIZE);
    if (!device->rx_cmd.skb) {
        printk("alloc rx_cmd skb failed\n");
        return -ENOMEM;
    }

    if (bl_usb_submit_rx_urb(&device->rx_cmd, BL_RX_CMD_BUF_SIZE)) {
        printk("submit rx cmd urb failed\n");
        return -1;
    }
#endif

    /*init data urb context*/
    //init resubmit first, otherwise when recv DBG msg, it will cause NULL
    //pointer
    for (i = 0; i < BL_RX_DATA_URB; i++)
        device->rx_data_list[i].resubmit = true;

    for (i = 0; i < BL_RX_DATA_URB; i++) {
        device->rx_data_list[i].bl_hw = bl_hw;
        device->rx_data_list[i].ep = device->rx_ep;
        device->rx_data_list[i].flag = BL_USB_EP_DATA;

        device->rx_data_list[i].urb = usb_alloc_urb(0, GFP_KERNEL);
        if (!device->rx_data_list[i].urb) {
            printk("alloc %d rx data urb failed\n", i);
            
            return -1;
        }

        BL_DBG("%s: [%d]urb=%p\n", __func__, i, device->rx_data_list[i].urb);

        if (bl_usb_submit_rx_urb(&device->rx_data_list[i], BL_RX_DATA_BUF_SIZE)) 
        {
            printk("submit %d rx data urb failed\n", i);
            
            return -1;
        }
    }

    BL_DBG("%s: rx_data_urb_pending=%d\n", __func__, 
           atomic_read(&device->rx_data_urb_pending));

    return 0;
}

static int bl_usb_rx_deinit(struct bl_hw *bl_hw)
{
    int i;
    struct bl_usb_device *device;
    device = (struct bl_usb_device *)(bl_hw->plat)->priv;

#if 0
    if (device->rx_cmd.skb) {
        BL_DBG("%s, rx_cmd ongoing, call kill, skb:0x%x\n", 
               __func__, device->rx_cmd.skb);
               
        usb_kill_urb(device->rx_cmd.urb);
    }

    if (device->rx_cmd.urb) {
        usb_free_urb(device->rx_cmd.urb);
        device->rx_cmd.urb = NULL;
    }
#endif

    for (i = 0; i < BL_RX_DATA_URB; i++) {
        if (device->rx_data_list[i].skb) {
            BL_DBG("%s, rx_data ongoing, call kill, skb:%p\n", __func__, 
                   device->rx_data_list[i].skb);
                   
            usb_kill_urb(device->rx_data_list[i].urb);
        }
            
        if (device->rx_data_list[i].urb) {
            usb_free_urb(device->rx_data_list[i].urb);
            device->rx_data_list[i].urb = NULL;
        }
    }

    return 0;
}

int bl_usb_txrx_init(struct bl_hw *bl_hw)
{
    bl_usb_tx_init(bl_hw);
    bl_usb_rx_init(bl_hw);

    return 0;
}

int bl_usb_txrx_deinit(struct bl_hw *bl_hw)
{
    bl_usb_tx_deinit(bl_hw);
    bl_usb_rx_deinit(bl_hw);

    return 0;
}

static void bl_usb_tx_complete(struct urb *urb)
{
    struct urb_context *context = (struct urb_context *)(urb->context);
    struct bl_hw *bl_hw = context->bl_hw;
    struct bl_txhdr *txhdr = NULL;
    struct bl_plat *bl_plat = bl_hw->plat;
    int status = urb->status;
    struct bl_usb_device *device;
#ifdef BL_BUS_LOOPBACK
    struct inf_hdr * uhdr = NULL;
#endif
    struct bl_cmd *cmd = NULL;

    BL_DBG("%s: status: %d, urb=%p, skb=%p, sn=%d\n", 
           __func__, urb->status, urb, context->skb, context->sn);

    device = (struct bl_usb_device *)(bl_plat)->priv;

    if(status != 0) {
        if (context->flag == BL_USB_EP_MSG) {
            printk("%s: usb transfer error, urb status: %d, a2e_msg:%p\n", 
                   __func__, status, context->a2e_msg);
        } else {
            printk("%s: usb transfer error, urb status: %d\n", __func__, status);
        }
        
#ifdef BL_BUS_LOOPBACK
        if (context->skb) {
            uhdr = (struct inf_hdr *)context->skb->data;
            if (bl_hw->ploopback && uhdr->type == BL_TYPE_BUS_LOOPBACK) {
                up(&bl_hw->ploopback->sema);
                bl_hw->ploopback->fail_cnt++;
            }
        }
#endif
    }
    else {
#ifdef BL_BUS_LOOPBACK
        if (context->skb) {
            uhdr = (struct inf_hdr *)context->skb->data;
            if (bl_hw->ploopback && uhdr->type == BL_TYPE_BUS_LOOPBACK) {
                up(&bl_hw->ploopback->sema);
                bl_hw->ploopback->succ_cnt++;
            }
        }
#endif
    }
    
    if (context->flag == BL_USB_EP_MSG) {        
        BL_DBG("%s: CMD\n", __func__);

        spin_lock(&bl_hw->cmd_mgr.lock);
        if(!status && bl_hw->ipc_env->msga2e_hostid) {
            cmd = (struct bl_cmd *)bl_hw->ipc_env->msga2e_hostid;
            cmd->flags |= BL_CMD_FLAG_DNLD_COMPLETE;
        }

        kfree(context->a2e_msg);
        context->a2e_msg = NULL;
        spin_unlock(&bl_hw->cmd_mgr.lock);
        
        atomic_dec(&device->tx_cmd_urb_pending);
    } else {
        BL_DBG("%s: DATA\n", __func__);
        
        if (context->flag == BL_USB_EP_DATA) {
            skb_push(context->skb, sizeof(struct bl_txhdr));
            
            BL_DBG("%s:skb->data=%p\n", __func__, context->skb->data);
            
            txhdr = (struct bl_txhdr *)(context->skb->data);
            
            if (status == 0) {
                if(!(txhdr->sw_hdr->desc.host.flags & TXU_CNTRL_MGMT)) {
                    BL_DBG("%s: usb transfer success,  free data skb: %p, sw_hdr=%p\n", 
                           __func__, context->skb, txhdr->sw_hdr);
                           
                    kmem_cache_free(bl_hw->sw_txhdr_cache, txhdr->sw_hdr);
                    dev_kfree_skb_any(context->skb);
                } else {
                    ipc_host_txdesc_push(bl_hw->ipc_env, txhdr->sw_hdr->hw_queue, 
                                         0, context->skb);
                    bl_hw->stats.cfm_balance[txhdr->sw_hdr->hw_queue]++;
                    
                    BL_DBG("%s: mgmt packet complete, free it in txcfm func\n",
                          __func__);
                }
            } else {
                kmem_cache_free(bl_hw->sw_txhdr_cache, txhdr->sw_hdr);
                dev_kfree_skb_any(context->skb);
            }
            
            context->skb = NULL;
            atomic_dec(&device->tx_data_urb_pending);
            
            BL_DBG("%s: tx_data_urb_pending=%d\n", __func__, 
                   atomic_read(&device->tx_data_urb_pending));
        }
    }

    if(status != -ENOENT && status != -ECONNRESET && status != -ESHUTDOWN) {
        bl_queue_main_work(bl_hw);
    } else {
        BL_DBG("%s, not schedule main work again\n", __func__);
    }
    
    return;
}

void bl_rx_urb_resubmit(struct bl_hw *bl_hw)
{
    int i, cnt = 0, size = BL_RX_DATA_BUF_SIZE;
    struct bl_usb_device *device;
    
    BL_DBG("Enter bl_rx_urb_resubmit\n");
    
    device = (struct bl_usb_device *)(bl_hw->plat)->priv;

    if(atomic_read(&device->rx_data_urb_pending) != 0 || bl_hw->surprise_removed)
        return;

    for (i = 0; i < BL_RX_DATA_URB; i++) {
        if(!device->rx_data_list[i].skb && 
            device->rx_data_list[i].resubmit == false &&
            device->rx_data_list[i].urb) 
        {
            if (bl_hw->surprise_removed)
                return;

            do {
               device->rx_data_list[i].skb = __dev_alloc_skb(size, GFP_KERNEL);
               cnt ++;
             } while ((cnt < 10) && !device->rx_data_list[i].skb);
            
            if (!device->rx_data_list[i].skb) {
                device->rx_data_list[i].resubmit = false;
                
                printk("%s: urb[%d] dev_alloc_skb failed , urb pending cnt %d \n", 
                       __func__, i, atomic_read(&device->rx_data_urb_pending));
                       
                bl_nl_broadcast_event(bl_hw, BL_EVENT_ID_OUT_MEM, NULL, 0);
                //return -ENOMEM;
                
                continue;
            }
            
            BL_DBG("%s: [%d]urb=%p\n", __func__, i, device->rx_data_list[i].urb);

            usb_fill_bulk_urb(device->rx_data_list[i].urb, device->udev,
                  usb_rcvbulkpipe(device->udev, device->rx_data_list[i].ep), 
                  device->rx_data_list[i].skb->data,
                  size, bl_usb_rx_complete, (void *)&(device->rx_data_list[i]));

            atomic_inc(&device->rx_data_urb_pending);
            
            BL_DBG("%s: rx_data_urb_pending=%d\n", __func__, 
                   atomic_read(&device->rx_data_urb_pending));

            if (usb_submit_urb(device->rx_data_list[i].urb, GFP_ATOMIC)) {
                printk("usb_submit_urb failed\n");
                
                dev_kfree_skb_any(device->rx_data_list[i].skb);
                device->rx_data_list[i].skb = NULL;
                atomic_dec(&device->rx_data_urb_pending);
                
                return;
            }

            device->rx_data_list[i].resubmit = true;            
        }
    }
}


/* This function write a command/data packet to card */
int bl_usb_host_to_card(struct bl_hw *bl_hw, u8 *data, u32 data_len, 
                               u8 ep, u8 flag, struct sk_buff *skb)
{
    struct bl_plat *bl_plat = bl_hw->plat;
    struct urb_context *context = NULL;
    struct urb *tx_urb = NULL;
    int  ret = -EINPROGRESS;
    struct bl_usb_device *device;
    u16 len;
    u16 type;
    u16 queue_idx;
    u16 reserved;
    int bulk_out_maxpktsize = 512;

    BL_DBG("%s: ep=%d\n", __func__, ep);
    
    device = (struct bl_usb_device *)(bl_plat)->priv;
    
    len = *(u16 *)data;
    type = *((u16 *)data+1);
    queue_idx = *((u16 *)data+2);
    reserved = *((u16 *)data+3);

    BL_DBG("%s: len=%d, type=0x%x, queue_idx=%d, reserved=%d\n", 
           __func__, len, type, queue_idx, reserved);
    
    if (flag == BL_USB_EP_MSG) {
        BL_DBG_MSG("%s: tx msg, len=%d, type=0x%x, queue_idx=%d, reserved=%d, ep:%d\n",
                   __func__, len, type, queue_idx, reserved, ep);
                   
        context = &device->tx_cmd;
        context->a2e_msg = (struct bl_cmd_a2emsg *)data;
    } else if(flag == BL_USB_EP_DATA){
        BL_DBG("%s: tx data, len=%d, type=0x%x, queue_idx=%d, reserved=%d\n",
               __func__, len, type, queue_idx, reserved);
               
        if (atomic_read(&device->tx_data_urb_pending) >= BL_TX_DATA_URB) {
            BL_DBG("%s: full: tx_data_urb_pending=%d\n", __func__, 
                    atomic_read(&device->tx_data_urb_pending));
                    
            device->block_status = true;
            
            return -EBUSY;
        }
        
        if (device->tx_data_idx >= BL_TX_DATA_URB)
            device->tx_data_idx = 0;

        context = &device->tx_data_list[device->tx_data_idx++];
    }

    if (context) {
        context->bl_hw = bl_hw;
        context->ep = ep;
        context->skb = skb;
        context->sn = reserved;
        tx_urb = context->urb;
    }

    if (!tx_urb) {
        printk("%s, tx urb is NULL, bl_hw:%p, context:%p\n", 
               __func__, bl_hw, context);
               
        return -EBUSY;
    }

    //bl_usb_dump_data(data, data_len);

    BL_DBG("%s: fill tx urb, urb=%p, skb=%p, sn=%d\n", __func__,
           tx_urb, context->skb, context->sn);

    if(data_len % bulk_out_maxpktsize == 0)
        data_len++;
    
    usb_fill_bulk_urb(tx_urb, device->udev, usb_sndbulkpipe(device->udev, ep),
                      data, data_len, bl_usb_tx_complete,
                      (void *)context);

    tx_urb->transfer_flags |= URB_ZERO_PACKET;

    if (BL_USB_EP_MSG == flag) {
        atomic_inc(&device->tx_cmd_urb_pending);
    } else {
        atomic_inc(&device->tx_data_urb_pending);
        
        BL_DBG("%s: add tx urb pending: tx_data_urb_pending=%d\n",
               __func__, atomic_read(&device->tx_data_urb_pending));
    }

#if 0
    if (ep != device->tx_cmd_ep &&
        atomic_read(&device->tx_data_urb_pending) ==
                    BL_TX_DATA_URB) {
        device->block_status = true;
        ret = -ENOSR;
    }
#endif

    if ((ret = usb_submit_urb(tx_urb, GFP_ATOMIC))) {
        printk("%s: usb_submit_urb failed, ret=%d\n", __func__, ret);
        
        if (BL_USB_EP_MSG == flag) {
            atomic_dec(&device->tx_cmd_urb_pending);
        } else {
            atomic_dec(&device->tx_data_urb_pending);
            
            printk("%s: usb_submit_urb failed: tx_data_urb_pending=%d\n", 
                   __func__, atomic_read(&device->tx_data_urb_pending));
                   
            device->block_status = false;
            if (device->tx_data_idx)
                device->tx_data_idx--;
            else
                device->tx_data_idx = BL_TX_DATA_URB;
        }
        
        ret = -1;
    }

    return ret;
}

static int bl_usb_probe(struct usb_interface *intf,
                             const struct usb_device_id *id)
{
    struct bl_plat *bl_plat = NULL;
    struct usb_device *udev = interface_to_usbdev(intf);
    void *drvdata = NULL;
    int ret = -ENODEV;

    BL_DBG(BL_FN_ENTRY_STR);

    ret = bl_device_init(intf, &bl_plat);

    /**
     * ret != 0, bl_device_init failed
     * ret = BL616_DNLD_FW_OK, return immediately for net enumeration
     * ret = BL616_DNLD_FW_FAIL, return immediately for error
     * ret = 0, dnld fw ok or dnld fw by jtag, go on
     */
    if (ret) {
        bl_device_deinit(bl_plat);
        return ret;
    }

    ret = bl_platform_init(bl_plat, &drvdata);

    if (ret) {
        bl_device_deinit(bl_plat);
    } else {
        usb_set_intfdata(intf, drvdata);
        usb_get_dev(udev);
    }

    return ret;
}
static void bl_usb_disconnect(struct usb_interface *intf)
{
    struct bl_hw *bl_hw;
    struct bl_plat *bl_plat;
    struct bl_usb_device *bl_device;

    BL_DBG(BL_FN_ENTRY_STR);
    bl_hw = usb_get_intfdata(intf);

    if (bl_hw && bl_hw->plat) {
        bl_plat = bl_hw->plat;        
        bl_device =  (struct bl_usb_device *)bl_plat->priv;
        bl_hw->surprise_removed = true;
    
        if(bl_device && bl_device->usb_boot_state != BL616_FW_DNLD) {
            bl_platform_deinit(bl_hw);
            bl_device_deinit(bl_plat);
            usb_put_dev(interface_to_usbdev(intf));
        }
    } 

}

static int bl_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
    return 0;
}

static int bl_usb_resume(struct usb_interface *intf)
{
    return 0;
}

static struct usb_driver bl_usb_drv = {
    .name     =   KBUILD_MODNAME,
    .id_table =   bl_usb_ids,
    .probe    =   bl_usb_probe,
    .disconnect = bl_usb_disconnect,
    .suspend =    bl_usb_suspend,
    .resume =     bl_usb_resume,
};

int bl_usb_register_drv(void)
{
    return usb_register(&bl_usb_drv);
}

void bl_usb_unregister_drv(void)
{
    usb_deregister(&bl_usb_drv);
}
