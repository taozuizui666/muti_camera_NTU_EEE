/**
 ******************************************************************************
 *
 *  @file bl_platorm.h
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


#ifndef _BL_PLAT_H_
#define _BL_PLAT_H_

#if defined CONFIG_BL_SDIO
#include <linux/mmc/sdio_func.h>
#elif defined CONFIG_BL_USB
#include <linux/usb.h>
#endif

#define BL616_SD_WLAN_FW_NAME       "bl616_sd_wlan.bin"
#ifdef CONFIG_BL_BTSDU
#define BL616_SD_COMBO_FW_NAME      "bl616_sd_combo_ble_sdu.bin"
#endif
#ifdef CONFIG_BL_BTUART
#define BL616_SD_COMBO_FW_NAME      "bl616_sd_combo_ble_uart.bin"
#endif
#define BL616_SD_MFG_FW_NAME        "bl616_sd_mp.bin"

#define BL616L_SD_WLAN_FW_NAME      "bl616l_sd_wlan.bin"
#ifdef CONFIG_BL_BTSDU
#define BL616L_SD_COMBO_FW_NAME     "bl616l_sd_combo_ble_sdu.bin"
#endif
#ifdef CONFIG_BL_BTUART
#define BL616L_SD_COMBO_FW_NAME     "bl616l_sd_combo_ble_uart.bin"
#endif
#define BL616L_SD_MFG_FW_NAME       "bl616l_sd_mp.bin"

#define BL616_USB_WLAN_FW_NAME      "bl616_usb_wlan.bin"
#define BL616_USB_COMBO_FW_NAME     "bl616_usb_combo.bin"
#define BL616_USB_MFG_FW_NAME       "bl616_usb_mp.bin"

#define BL616L_USB_WLAN_FW_NAME     "bl616l_usb_wlan.bin"
#define BL616L_USB_COMBO_FW_NAME    "bl616l_usb_combo.bin"
#define BL616L_USB_MFG_FW_NAME      "bl616l_usb_mp.bin"

#define BL_PHY_CONFIG_TRD_NAME        "bl_trident.ini"
#define BL_PHY_CONFIG_KARST_NAME      "bl_karst.ini"
#define BL_AGC_FW_NAME                "agcram.bin"
#define BL_LDPC_RAM_NAME              "ldpcram.bin"
#define BL_CATAXIA_FW_NAME            "cataxia.fw"
#if defined CONFIG_BL_FULLMAC
#define BL_MAC_FW_BASE_NAME           "fmacfw"
#endif

#ifdef CONFIG_BL_TL4
#define BL_MAC_FW_NAME BL_MAC_FW_BASE_NAME".hex"
#else
#define BL_MAC_FW_NAME  BL_MAC_FW_BASE_NAME".ihex"
#define BL_MAC_FW_NAME2 BL_MAC_FW_BASE_NAME".bin"
#endif

#define BL_FCU_FW_NAME                "fcuram.bin"

/**
 * Type of memory to access (cf bl_plat.get_address)
 *
 * @BL_ADDR_CPU To access memory of the embedded CPU
 * @BL_ADDR_SYSTEM To access memory/registers of one subsystem of the
 * embedded system
 *
 */
enum bl_platform_addr {
    BL_ADDR_CPU,
    BL_ADDR_SYSTEM,
    BL_ADDR_MAX,
};
struct bl_hw;
struct bl_plat;

typedef struct _bl_hal_if_ops{
    /*COMMON*/
    int (*platform_on)(struct bl_hw *bl_hw, void *config);
    void (*platform_off)(struct bl_hw *bl_hw, void **config);

    /*PCIE*/
    int (*enable)(struct bl_hw *bl_hw);
    int (*disable)(struct bl_hw *bl_hw);
    void (*deinit)(struct bl_plat *bl_plat);
    u8* (*get_address)(struct bl_plat *bl_plat, int addr_name,
                       unsigned int offset);
    void (*ack_irq)(struct bl_plat *bl_plat);
    int (*get_config_reg)(struct bl_plat *bl_plat, const u32 **list);
    
    /*SDIO*/
    int (*write_data)(struct bl_hw *bl_hw, u8 *buffer, u32 pkt_len, u32 port);
    int (*read_data)(struct bl_hw *bl_hw, u8 *buffer, u32 pkt_len, u32 port);
    int (*write_reg)(struct bl_hw *bl_hw, u32 reg, u8 data);
    int (*read_reg)(struct bl_hw *bl_hw, u32 reg, u8 *data);
    int (*get_rd_port)(struct bl_hw *bl_hw, u32 *rd_port);
    int (*get_wr_port)(struct bl_hw *bl_hw, u32 *wr_port);

    /*USB*/
    int (*usb_write_data)(struct bl_plat *bl_plat, u8 *pbuf,
				   u32 len, u8 ep, u32 timeout);
    int (*usb_read_data)(struct bl_plat *bl_plat, u8 *pbuf,
				  u32 len, u8 ep, u32 timeout);
}bl_hal_if_ops;

/**
 * struct bl_plat - Operation pointers for BL PCI platform
 *
 * @pci_dev: pointer to pci dev
 * @enabled: Set if embedded platform has been enabled (i.e. fw loaded and
 *          ipc started)
 * @enable: Configure communication with the fw (i.e. configure the transfers
 *         enable and register interrupt)
 * @disable: Stop communication with the fw
 * @deinit: Free all ressources allocated for the embedded platform
 * @get_address: Return the virtual address to access the requested address on
 *              the platform.
 * @ack_irq: Acknowledge the irq at link level.
 * @get_config_reg: Return the list (size + pointer) of registers to restore in
 * order to reload the platform while keeping the current configuration.
 *
 * @priv Private data for the link driver
 */
struct bl_plat {
    void *dev;
    bool enabled;

    bl_hal_if_ops ops;

    uint32_t chip_ver;
    
    u8 priv[0] __aligned(sizeof(void *));
};

#define BL_ADDR(plat, base, offset)           \
    plat->ops.get_address(plat, base, offset)

#define BL_REG_READ(plat, base, offset)               \
    readl(plat->ops.get_address(plat, base, offset))

#define BL_REG_WRITE(val, plat, base, offset)         \
    writel(val, plat->ops.get_address(plat, base, offset))

int bl_platform_init(struct bl_plat *bl_plat, void **platform_data);
void bl_platform_deinit(struct bl_hw *bl_hw);

int bl_platform_register_drv(void);
void bl_platform_unregister_drv(void);

static inline struct device *bl_platform_get_dev(struct bl_plat *bl_plat)
{
#if defined CONFIG_BL_SDIO
    struct sdio_func *dev =(struct sdio_func *)(bl_plat->dev);
#elif defined CONFIG_BL_USB
    struct usb_interface *dev =(struct usb_interface *)(bl_plat->dev);
#endif
    return &(dev->dev);
}

static inline unsigned int bl_platform_get_irq(struct bl_plat *bl_plat)
{
#if defined CONFIG_BL_SDIO
    //TODO:
    //return sdio_get_irq()?
    return 0;
#elif defined CONFIG_BL_USB
    return 0;
#endif
}

#endif /* _BL_PLAT_H_ */
