#include <linux/module.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include <linux/firmware.h>

#include "bl_defs.h"
#include "bl_irqs.h"

#include "bl_sdio.h"
#include "bl_bootrom.h"

#include "bl_fwbin.h"
#include "bl_mpfwbin.h"

#define SDIO_VENDOR_ID_BFL      0x424c
#define SD_DEVICE_ID_BFL        0x0606


#define BOOTROM_SDIO_PORT_USED 0x0001
#define BOOTROM_CMD_BUFFER_SIZE (1024 * 2)
#define BOOTROM_DATA_BUFFER_SIZE (512 * 2 * 2)

#define BOOTROM_SDIO_TEST_READ_BLOCK_COUNT (4 * 8)
#define BOOTROM_SDIO_TEST_READ_LOOP_COUNT (1024 * 100)
#define BOOTROM_SDIO_TEST_WRITE_BLOCK_COUNT (4 * 8)
#define BOOTROM_SDIO_TEST_WRITE_LOOP_COUNT (1024 * 100)

unsigned long time_start, time_end, time_diff, time_ms;

static int signature_mode = 0, encryption_mode = 0;

static const struct sdio_device_id bl_sdio_ids[] = {
    {SDIO_DEVICE(SDIO_VENDOR_ID_BFL, SD_DEVICE_ID_BFL)},
    {0,}
};
MODULE_DEVICE_TABLE(sdio, bl_sdio_ids);

#ifdef CONFIG_BL_BTSDU
extern int btsdio_probe(struct sdio_func *func, const 
                             struct sdio_device_id *id, struct bl_hw *bl_hw);
extern void btsdio_remove(struct sdio_func *func, struct bl_hw *bl_hw);
#endif

static struct ipc_shared_env_tag bl_ipc_chared_env_tag;

int bl_sdio_platform_on(struct bl_hw *bl_hw, void *config)
{
    struct bl_plat *bl_plat = bl_hw->plat;
    struct sdio_mmc_card *card;
    int ret = -1;
    int count = 0;
    u8 fw_ready = 0;

    if (bl_plat->enabled)
        return 0;

    bl_hw->mpa_rx_data.buf_ptr = 
           kzalloc(BL_RX_DATA_BUF_SIZE_16K + DMA_ALIGNMENT, GFP_KERNEL|GFP_DMA);
           
    if(!bl_hw->mpa_rx_data.buf_ptr){
        BL_DBG("allocate rx buf fail \n");
        ret = -ENOMEM;
        goto err_alloc_rx_data_buf;
    }
    
    bl_hw->mpa_rx_data.buf = (u8 *)ALIGN_ADDR(bl_hw->mpa_rx_data.buf_ptr, DMA_ALIGNMENT);

    bl_hw->mpa_tx_data.buf_ptr =
           kzalloc(BL_RX_DATA_BUF_SIZE_16K + DMA_ALIGNMENT, GFP_KERNEL|GFP_DMA);
    if(!bl_hw->mpa_tx_data.buf_ptr){
        BL_DBG("allocate tx buf fail \n");
        ret = -ENOMEM;
        
        goto err_alloc_tx_data_buf;
    }
    
    bl_hw->mpa_tx_data.buf =
          (u8 *)ALIGN_ADDR(bl_hw->mpa_tx_data.buf_ptr, DMA_ALIGNMENT);

    if ((ret = bl_ipc_init(bl_hw, (u8 *)&bl_ipc_chared_env_tag)))
        goto err_ipc_init;

#ifdef BL_FPGA
    printk("[FPGA][DBG] skip wait firwmare\n");
#else
    printk("Wait firmware...\n");
    
    do {
        bl_read_reg(bl_hw, CARD_FW_STATUS0_REG, &fw_ready);
        printk("FW Mark %c:%x\n", fw_ready, fw_ready);
        count++;
        mdelay(100);
    } while ('B' != fw_ready && count < 50);//TODO put 'B' into header file

    if(count >= 50) {
        printk("%s wait fw ready TO count=%d\n", __func__, count);
        //return -1;
    }
#endif

    bl_plat->enabled = true;

    card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    sdio_set_drvdata(card->func, bl_hw);

#ifndef BL_FPGA
    BL_DBG("Notify FW to start...\n");
    bl_write_reg(bl_hw, CARD_FW_STATUS0_REG, 'L');
#endif

err_ipc_init:
    if (ret) {
        kfree(bl_hw->mpa_tx_data.buf_ptr);
        bl_hw->mpa_tx_data.buf =NULL;
        bl_hw->mpa_tx_data.buf_ptr =NULL;
    }
    
err_alloc_tx_data_buf:
    if (ret) {
        kfree(bl_hw->mpa_rx_data.buf_ptr);
        bl_hw->mpa_rx_data.buf = NULL;
        bl_hw->mpa_rx_data.buf_ptr = NULL;
    }
    
err_alloc_rx_data_buf:
    return ret;
}

void bl_sdio_platform_off(struct bl_hw *bl_hw, void **config)
{
    struct sk_buff *skb = NULL;
    int i = 0;

    if (!bl_hw->plat->enabled) {
        return;
    }

#ifdef  BL_RX_REORDER
    while (skb_queue_len(&bl_hw->rx_pkt_list)) {
        skb = skb_dequeue(&bl_hw->rx_pkt_list);
        if (skb)
            dev_kfree_skb_any(skb);
    }
#endif

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

#ifdef  BL_RX_REORDER
    if (bl_hw->rx_workqueue) {
        flush_workqueue(bl_hw->rx_workqueue);
        destroy_workqueue(bl_hw->rx_workqueue);
        bl_hw->rx_workqueue = NULL;
    }
#endif

    if (bl_hw->mpa_rx_data.buf) {
        kfree(bl_hw->mpa_rx_data.buf_ptr);
        bl_hw->mpa_rx_data.buf_ptr = NULL;
        bl_hw->mpa_rx_data.buf = NULL;
    }
    if (bl_hw->mpa_tx_data.buf) {
        kfree(bl_hw->mpa_tx_data.buf_ptr);
        bl_hw->mpa_tx_data.buf_ptr =NULL;
        bl_hw->mpa_tx_data.buf =NULL;
    }

    bl_ipc_deinit(bl_hw);

    bl_hw->plat->enabled = false;
}

void bl_device_deinit(struct bl_plat *bl_plat)
{
    struct sdio_mmc_card *card;
    
    card = (struct sdio_mmc_card *)(bl_plat)->priv;

    sdio_claim_host(bl_plat->dev);
    sdio_release_irq(bl_plat->dev);
    sdio_disable_func(bl_plat->dev);
    sdio_release_host(bl_plat->dev);

    kfree(card->mp_regs_buf);    
    card->mp_regs = NULL;
    card->mp_regs_buf = NULL;

    kfree(bl_plat);
}

int bl_device_init(struct sdio_func *func, struct bl_plat **bl_plat)
{
    struct sdio_mmc_card *card = NULL;
    u8 sdio_ireg;
    int ret = 0;

    *bl_plat = kzalloc(sizeof(struct bl_plat) + sizeof(struct sdio_mmc_card), GFP_KERNEL);
    if (!*bl_plat)
        return -ENOMEM;

    card = (struct sdio_mmc_card *)(*bl_plat)->priv;

    card->func = func;
    card->reg = &bl_reg_sd616;
    card->max_ports = 16;
    card->mp_agg_pkt_limit = 8;
    card->supports_sdio_new_mode = false;
    card->has_control_mask = true;
    card->supports_fw_dump = false;
    card->mp_tx_agg_buf_size = BL_MP_AGGR_BUF_SIZE_16K;
    card->mp_rx_agg_buf_size = BL_MP_AGGR_BUF_SIZE_16K;
    card->auto_tdls = false;

    func->card->quirks |= MMC_QUIRK_BLKSZ_FOR_BYTE_MODE | MMC_QUIRK_LENIENT_FN0;

    sdio_claim_host(func);
    
    /*1. enable func*/
    ret = sdio_enable_func(func);

    /*
     * 2. Read the host_int_status_reg for ACK the first interrupt got
     * from the bootloader. If we don't do this we get a interrupt
     * as soon as we register the irq.
     */
    sdio_readb(func, card->reg->host_int_status_reg, &ret);

    /* 3. Get SDIO ioport */
    card->io_port = (sdio_readb(func, card->reg->io_port_0_reg, &ret) & 0xff);
    card->io_port |= ((sdio_readb(func, card->reg->io_port_1_reg, &ret) & 0xff) << 8);
    card->io_port |= ((sdio_readb(func, card->reg->io_port_2_reg, &ret) & 0xff) << 16);
    BL_DBG("info: SDIO FUNC1 IO port: %#x\n", card->io_port);

#ifndef BL_INT_WRITE_CLEAR
    /* Set Host interrupt reset to read to clear */
    sdio_ireg = sdio_readb(func, card->reg->host_int_rsr_reg, &ret);
    if (!ret)
        sdio_writeb(func, sdio_ireg | card->reg->sdio_int_mask, 
                    card->reg->host_int_rsr_reg, &ret);
#else

    sdio_writeb(func, 0, card->reg->host_int_rsr_reg, &ret);
    if (ret) {
        printk("bl_sdio_init write host_int_rsr_reg fail: ret=%d\n", ret);
        sdio_release_host(func);
        return ret;
    }
    
    sdio_ireg = sdio_readb(func, card->reg->host_int_rsr_reg, &ret);
    if (ret)
        printk("bl_sdio_init read host_int_rsr_reg fail: ret=%d\n", ret);
#endif

    /* Dnld/Upld ready set to auto reset */
    sdio_ireg = sdio_readb(func, card->reg->card_misc_cfg_reg, &ret);
    printk("%s before write sdio_ireg=0x%x, ret=%d\n", __func__, sdio_ireg, ret);
    if(!ret)
        sdio_writeb(func, sdio_ireg | AUTO_RE_ENABLE_INT, 
                    card->reg->card_misc_cfg_reg, &ret);

    sdio_ireg = sdio_readb(func, card->reg->card_misc_cfg_reg, &ret);

    printk("%s after write sdio_ireg=0x%x, ret=%d\n", __func__, sdio_ireg, ret);

    /* 4. set block size*/
    sdio_set_block_size(func, BL_SDIO_BLOCK_SIZE);

    /* 4.1 Download Wi-Fi firmware*/
    (*bl_plat)->dev = func;
    ret = bl_sdio_init(*bl_plat);
    if (ret) {
        printk("bl_sdio_init failed: ret=%d\n", ret);
        sdio_release_host(func);
        return ret;
    }

    /* 5. request irq */
    ret = sdio_claim_irq(func, bl_sdio_irq_hdlr);
    if (ret) {
        printk("sdio_claim_irq failed: ret=%d\n", ret);
        sdio_release_host(func);
        return ret;
    }
    
    /* Simply write the mask to the register */
    sdio_writeb(func, UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK, 
                card->reg->host_int_mask_reg, &ret);
    if (ret) {
        printk("enable host interrupt failed\n");
        sdio_release_irq(func);
        sdio_release_host(func);
        return ret;
    }

    sdio_ireg = sdio_readb(func, card->reg->host_int_mask_reg, &ret);
    
    BL_DBG("irq_enable=0x%x\n", sdio_ireg);

    sdio_release_host(func);
    
    /* 6. Initialize SDIO variables in card */
    card->mp_rd_bitmap = 0;
    card->mp_wr_bitmap = 0;
    card->prev_rd_bitmap = 0;
    card->prev_wr_bitmap = 0;
    card->curr_rd_port = card->reg->start_rd_port;
    card->curr_wr_port = card->reg->start_wr_port;

    card->mp_regs_buf = kzalloc(card->reg->max_mp_regs + DMA_ALIGNMENT, GFP_KERNEL|GFP_DMA);
    if (!card->mp_regs_buf) {
        printk("kzalloc mp_regs failed!\n");
        return -ENOMEM;
    }
    card->mp_regs = (u8 *)ALIGN_ADDR(card->mp_regs_buf, DMA_ALIGNMENT);

    (*bl_plat)->ops.read_data = bl_read_data_sync;
    (*bl_plat)->ops.write_data = bl_write_data_sync;
    (*bl_plat)->ops.read_reg = bl_read_reg;
    (*bl_plat)->ops.write_reg = bl_write_reg;
    (*bl_plat)->ops.get_rd_port = bl_get_rd_port;
    (*bl_plat)->ops.get_wr_port = bl_get_wr_port;
    (*bl_plat)->ops.platform_on = bl_sdio_platform_on;
    (*bl_plat)->ops.platform_off = bl_sdio_platform_off;

    if (ret) {
        printk("bl_device_init failed!\n");
        
        sdio_claim_host(func);
        sdio_release_irq(func);
        ret = sdio_disable_func(func);
        sdio_release_host(func);
        
        kfree(card->mp_regs_buf);
        card->mp_regs_buf = NULL;
        card->mp_regs = NULL;
        kfree(*bl_plat);
    }
    
    return ret;
}

/*
 * This function gets the read port.
 *
 * If control port bit is set in MP read bitmap, the control port
 * is returned, otherwise the current read port is returned and
 * the value is increased (provided it does not reach the maximum
 * limit, in which case it is reset to 1)
 */
int bl_get_rd_port(struct bl_hw *bl_hw, u32 *port)
{
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    u32 rd_bitmap = card->mp_rd_bitmap;

    if (!(rd_bitmap & (CTRL_PORT_MASK | card->reg->data_port_mask))) {
        return -EBUSY;
    }

    if ((card->has_control_mask) && (card->mp_rd_bitmap & CTRL_PORT_MASK)) {
        card->mp_rd_bitmap &= (u32) (~CTRL_PORT_MASK);
        *port = CTRL_PORT;
        
        BL_DBG("ctrl: port=%d rd_bitmap=0x%08x -> 0x%08x\n",
               *port, rd_bitmap, card->mp_rd_bitmap);
        
        return 0;
    }

    if (!(card->mp_rd_bitmap & (1 << card->curr_rd_port))) {        
         BL_DBG("bl_hw->plat->mp_rd_bitmap %x and curr_rd_port %d out of sync \n",
                card->mp_rd_bitmap,card->curr_rd_port);

         do {
             if (++(card->curr_rd_port) == MAX_PORT_NUM)
                 card->curr_rd_port = card->reg->start_rd_port;
         } while(!(card->mp_rd_bitmap & (1 << card->curr_rd_port)));
    }

    /* We are now handling the SDIO data ports */
    card->mp_rd_bitmap &= (u32)(~(1 << card->curr_rd_port));
    
    /* Record last valid rd_bitmap for duplicate bitmap filter */
    if (card->mp_rd_bitmap & DATA_PORT_MSK)
        card->prev_rd_bitmap = card->mp_rd_bitmap;
        
    *port = card->curr_rd_port;
    bl_hw->data_recv = true;

    if (++(card->curr_rd_port) == MAX_PORT_NUM)
        card->curr_rd_port = card->reg->start_rd_port;

    BL_DBG("data: port=%d rd_bitmap=0x%08x -> 0x%08x\n",
           *port, rd_bitmap, card->mp_rd_bitmap);

    return 0;
}

/*
 * This function gets the write port for data.
 *
 * The current write port is returned if available and the value is
 * increased (provided it does not reach the maximum limit, in which
 * case it is reset to 1)
 */
int bl_get_wr_port(struct bl_hw *bl_hw, u32 *port)
{
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    u32 wr_bitmap = card->mp_wr_bitmap;
    
    if (!(wr_bitmap & card->reg->data_port_mask)) {
        printk("no available port\n");
        
        return -EBUSY;
    }

    if (wr_bitmap & (1 << card->curr_wr_port)) {
        card->mp_wr_bitmap &= (u32) (~(1 << card->curr_wr_port));
        
        //record last valid wr_bitmap for duplicate bitmap filter.
        if (card->mp_wr_bitmap & DATA_PORT_MSK)
            card->prev_wr_bitmap = card->mp_wr_bitmap;
            
        *port = card->curr_wr_port;
        
        if (++(card->curr_wr_port) == MAX_PORT_NUM)
            card->curr_wr_port = card->reg->start_wr_port;
            
        bl_hw->data_sent = true;
    } else {
        printk("no available port, port=%d wr_bitmap=0x%08x -> 0x%08x\r\n",
               card->curr_wr_port, wr_bitmap, card->mp_wr_bitmap);
            
        if(card->mp_wr_bitmap & DATA_PORT_MSK) {
            do{
                if (++(card->curr_wr_port) == MAX_PORT_NUM)
                    card->curr_wr_port = card->reg->start_wr_port;
            }while(!(card->mp_wr_bitmap & (1 << card->curr_wr_port)));
        } else
            return -EBUSY;

    }

    BL_TRACE(TRACE_TX, "data: port=%d wr_bitmap=0x%08x -> 0x%08x\n",
        *port, wr_bitmap, card->mp_wr_bitmap);

    return 0;
}

/*
 * This function writes multiple data into SDIO card memory.
 *
 * This does not work in suspended mode.
 */
int bl_write_data_sync_dnldfw(struct bl_plat *bl_plat, u8 *buffer,
                                       u32 pkt_len, u32 port)
{
    int ret, cnt = 5;
    u8 blk_mode = (pkt_len < BL_SDIO_BLOCK_SIZE) ? BYTE_MODE : BLOCK_MODE;
    u32 blk_size = (blk_mode == BLOCK_MODE) ? BL_SDIO_BLOCK_SIZE : 1;
    u32 blk_cnt = (blk_mode == BLOCK_MODE) ? ((pkt_len + BL_SDIO_BLOCK_SIZE -1)/BL_SDIO_BLOCK_SIZE) : pkt_len;

    u32 ioport = (port & BL_SDIO_IO_PORT_MASK);

    sdio_claim_host(bl_plat->dev);
    ret = sdio_writesb(bl_plat->dev, ioport, buffer, blk_cnt * blk_size);
    while (ret && cnt)
    {
        printk("sdio writesb fail %d, retry again \n", ret);
        
        sdio_f0_writeb(bl_plat->dev, 0x01, SDIO_CCCR_ABORT, &ret);
        
        if(!ret)
            ret = sdio_writesb(bl_plat->dev, ioport, buffer, blk_cnt * blk_size);
        else
            printk("cccr abort fail \n");
        cnt --;
    }
    sdio_release_host(bl_plat->dev);

    return ret;
}

/*
 * This function reads multiple data from SDIO card memory.
 */
int bl_read_data_sync_dnldfw(struct bl_plat *bl_plat, u8 *buffer, 
                                      u32 len, u32 port)
{
    int ret, cnt = 5;
    u8 blk_mode = (len < BL_SDIO_BLOCK_SIZE) ? BYTE_MODE : BLOCK_MODE;    
    u32 blk_size = (blk_mode == BLOCK_MODE) ? BL_SDIO_BLOCK_SIZE : 1;
    u32 blk_cnt = (blk_mode == BLOCK_MODE) ? ((len + BL_SDIO_BLOCK_SIZE -1)/BL_SDIO_BLOCK_SIZE) : len;
    u32 ioport = (port & BL_SDIO_IO_PORT_MASK);

    sdio_claim_host(bl_plat->dev);
    ret = sdio_readsb(bl_plat->dev, buffer, ioport, blk_cnt * blk_size);
    while (ret && cnt)
    {
        printk("sdio readsb fail %d, retry again \n", ret);        
        ret = sdio_readsb(bl_plat->dev, buffer, ioport, blk_cnt * blk_size);
        cnt --;
    }
    sdio_release_host(bl_plat->dev);

    return ret;
}

/*
 * This function reads data from SDIO card register.
 */
static int bl_sdio_read_reg(struct bl_plat *card, u32 reg, u8 *data)
{
    int ret = -1;
    u8 val;

    sdio_claim_host(card->dev);
    val = sdio_readb(card->dev, reg, &ret);
    sdio_release_host(card->dev);

    *data = val;

    return ret;
}

/*
 * This function write data to SDIO card register.
 */
static int bl_sdio_write_reg(struct bl_plat *card, u32 reg, u8 data)
{
    int ret;

    sdio_claim_host(card->dev);
    sdio_writeb(card->dev, data, reg, &ret);
    sdio_release_host(card->dev);

    return ret;
}

int bl_sdio_read_reg_func0(struct sdio_mmc_card *card, u32 reg, u8 * data)
{
    int ret;
    u8 val;

    sdio_claim_host(card->func);
    val = sdio_f0_readb(card->func, reg, &ret);
    sdio_release_host(card->func);
    *data = val;

    return ret;
}

int bl_sdio_write_reg_func0(struct sdio_mmc_card *card, u32 reg, u8 data)
{
    int ret;

    sdio_claim_host(card->func);
    sdio_f0_writeb(card->func, data, reg, &ret);
    sdio_release_host(card->func);

    return ret;
}

//TODO TIMEOUT value
static int bl_io_write(struct bl_plat *bl_plat, u8* data, int len)
{
    u8 wr_bitmap_l = 0;
    u8 wr_bitmap_u = 0;
    u32 bitmap = 0;
    int ret;
    u32 count = 0;
    struct sdio_mmc_card *card;
    card = (struct sdio_mmc_card *)(bl_plat->priv);

    //SDIO_DBG(SDIO_FN_ENTRY_STR);

    while (0 == (bitmap & (1 << card->curr_wr_port))) {
        bl_sdio_read_reg(bl_plat, WR_BITMAP_L, &wr_bitmap_l);
        bl_sdio_read_reg(bl_plat, WR_BITMAP_U, &wr_bitmap_u);
        bitmap = wr_bitmap_l;
        bitmap |= wr_bitmap_u << 8;
        count++;

        #if 0
        BL_DBG("bl_io_write: wr_bitmap=0x%x, count %d, curr_wr_port %d\n",
            bitmap, count, card->curr_wr_port);
        #endif
    }
    
    //printk("bl_io_write: io_port=0x%x, wr_bitmap=0x%x, count %d\n", bitmap, count);
    
    ret = bl_write_data_sync_dnldfw(bl_plat, data, len,
                                    card->io_port + card->curr_wr_port);

    //SDIO_DBG(SDIO_FN_LEAVE_STR);

    return ret;
}

//caller must free the memory
int bl_io_read(struct bl_plat *bl_plat, u8 *buf, int rd_len)
{
    u8 rd_bitmap_l = 0;
    u8 rd_bitmap_u = 0;
    u32 bitmap = 0;
    u32 count = 0;
    int ret = 0;
    struct sdio_mmc_card *card;
    card = (struct sdio_mmc_card *)(bl_plat->priv);    

    //SDIO_DBG(SDIO_FN_ENTRY_STR);

    while (0 == (bitmap & (1 << card->curr_rd_port))) {
        bl_sdio_read_reg(bl_plat, RD_BITMAP_L, &rd_bitmap_l);
        bl_sdio_read_reg(bl_plat, RD_BITMAP_U, &rd_bitmap_u);
        bitmap = rd_bitmap_l;
        bitmap |= rd_bitmap_u << 8;
        card->mp_rd_bitmap = bitmap;
        count++;

        //BL_DBG("int: UPLD: rd_bitmap=0x%x count=%d\n", card->mp_rd_bitmap, count);
    }

    ret = bl_read_data_sync_dnldfw(bl_plat, buf, rd_len,
                                   card->io_port + card->curr_rd_port);

#if 0
    printk("[XXX] len:%d--->receive ok\n", rd_len);
    printk("Read 00: %02X %02X %02X %02X %02X %02X %02X %02X\n"
           "Read 10: %02X %02X %02X %02X %02X %02X %02X %02X\n"
           "Read 20: %02X %02X %02X %02X %02X %02X %02X %02X\n",
        buf[0], buf[1],
        buf[2], buf[3],
        buf[4], buf[5],
        buf[6], buf[7],
        buf[8 + 0], buf[8 + 1],
        buf[8 + 2], buf[8 + 3],
        buf[8 + 4], buf[8 + 5],
        buf[8 + 6], buf[8 + 7],
        buf[16 + 0], buf[16 + 1],
        buf[16 + 2], buf[16 + 3],
        buf[16 + 4], buf[16 + 5],
        buf[16 + 6], buf[16 + 7]
    );
#endif

    //SDIO_DBG(SDIO_FN_LEAVE_STR);

    return ret;
}

static int bl_sdio_run_fw(struct bl_plat *card)
{
    int ret = 0;

    printk("Enter bl_sdio_run_fw...\n");
    
    ret = bl_sdio_write_reg(card, CARD_FW_STATUS0_REG, 0x1);
    // Need a little duration wait FW ready
    mdelay(100);

    return ret;

}

static int firmware_dump_head(bootheader_t *header)
{
#if 0
    int i;

    printk("[************Header************]\n");
    printk(" magiccode: 0x%X\n", header->magiccode);
    printk(" rivison: 0x%X\n", header->rivison);
    printk(" flashCfg\n");
    printk("    magiccode: 0x%X\n", header->flashCfg.magiccode);
    printk("    cfg see below, size: %lu\n", sizeof(header->flashCfg.cfg));
    printk("    crc32: 0x%X\n", header->flashCfg.crc32);
    printk(" pllCfg\n");
    printk("    magiccode: 0x%X\n", header->pllCfg.magiccode);
    printk("    root_clk: %u\n", header->pllCfg.root_clk);
    printk("    xtal_type: %u\n", header->pllCfg.xtal_type);
    printk("    pll_clk: %u\n", header->pllCfg.pll_clk);
    printk("    hclk_div: %u\n", header->pllCfg.hclk_div);
    printk("    bclk_div: %u\n", header->pllCfg.bclk_div);
    printk("    flash_clk_div: %u\n", header->pllCfg.flash_clk_div);
    printk("    uart_clk_div: %u\n", header->pllCfg.uart_clk_div);
    printk("    sdu_clk_div: %u\n", header->pllCfg.sdu_clk_div);
    printk("    crc32: 0x%X\n", header->pllCfg.crc32);
    printk(" seccfg\n");
    printk("    bval in union\n");
    printk("        encrypt: %u\n", header->seccfg.bval.encrypt);
    printk("        sign: %u\n", header->seccfg.bval.sign);
    printk("        key_sel: %u\n", header->seccfg.bval.key_sel);
    printk("        rsvd6_31: %u\n", header->seccfg.bval.rsvd6_31);
    printk("    wval in union\n");
    printk("        wval: 0x%X\n", header->seccfg.wval);
    printk(" segment_cnt: %u\n", header->segment_cnt);
    printk(" bootentry: 0x%X\n", header->bootentry);
    printk(" flashoffset: 0x%X\n", header->flashoffset);
    printk(" bootentry: 0x%X\n", header->bootentry);
    printk(" hash:\n");

    for (i = 0; i < sizeof(header->hash); i+=4) {
        //XXX we may access over-flow
        printk("0x%08X ", ((uint32_t)header->hash[i]) << 24 |
            (uint32_t)header->hash[i + 1] << 16 |
            (uint32_t)header->hash[i + 2] << 8 |
            (uint32_t)header->hash[i + 3] << 0
        ); 
    }


    printk(" rsv1: %u\n", header->rsv1);
    printk(" rsv2: %u\n", header->rsv2);
    printk(" crc32: 0x%X\n", header->crc32);
    printk("--------\n");
#else
    BL_DBG("skip %s\n",__func__);
#endif
    return 0;
}

static int firmware_dump_flashcfg(const boot_flash_cfg_t *cfg)
{
#if 0
    printk("[************FLASH CFG(%lu)************]\n", sizeof(boot_flash_cfg_t));
    printk(" magiccode: 0x%X\n", cfg->magiccode);
    printk(" flash cfg\n");
    printk("   ioMode: %u\n", cfg->cfg.ioMode);
    printk("   cReadSupport: %u\n", cfg->cfg.cReadSupport);
    printk("   clk_delay;: %u\n", cfg->cfg.clk_delay);
    printk("   rsvd[1]: %X\n", cfg->cfg.rsvd[0]);
    printk("   resetEnCmd: %u\n", cfg->cfg.resetEnCmd);
    printk("   resetCmd: %u\n", cfg->cfg.resetCmd);
    printk("   resetCreadCmd: %u\n", cfg->cfg.resetCreadCmd);
    printk("   rsvd_reset[1]: %X\n", cfg->cfg.rsvd_reset[0]);
    printk("   jedecIdCmd: %u(0x%X)\n", cfg->cfg.jedecIdCmd, cfg->cfg.jedecIdCmd);
    printk("   jedecIdCmdDmyClk: %u(0x%X)\n", cfg->cfg.jedecIdCmdDmyClk, cfg->cfg.jedecIdCmdDmyClk);
    printk("   qpiJedecIdCmd: %u(0x%X)\n", cfg->cfg.qpiJedecIdCmd, cfg->cfg.qpiJedecIdCmd);
    printk("   qpiJedecIdCmdDmyClk: %u(0x%X)\n", cfg->cfg.qpiJedecIdCmdDmyClk, cfg->cfg.qpiJedecIdCmdDmyClk);
    printk("   sectorSize: %u(0x%X)\n", cfg->cfg.sectorSize, cfg->cfg.sectorSize);
    printk("   capBase: %u(0x%X)\n", cfg->cfg.capBase, cfg->cfg.capBase);
    printk("   pageSize: %u(0x%X)\n", cfg->cfg.pageSize, cfg->cfg.pageSize);
    printk("   chipEraseCmd: %u(0x%X)\n", cfg->cfg.chipEraseCmd, cfg->cfg.chipEraseCmd);
    printk("   sectorEraseCmd: %u(0x%X)\n", cfg->cfg.sectorEraseCmd, cfg->cfg.sectorEraseCmd);
    printk("   blk32EraseCmd: %u(0x%X)\n", cfg->cfg.blk32EraseCmd, cfg->cfg.blk32EraseCmd);
    printk("   blk64EraseCmd: %u(0x%X)\n", cfg->cfg.blk64EraseCmd, cfg->cfg.blk64EraseCmd);
    printk("   writeEnableCmd: %u(0x%X)\n", cfg->cfg.writeEnableCmd, cfg->cfg.writeEnableCmd);
    printk("   pageProgramCmd: %u(0x%X)\n", cfg->cfg.pageProgramCmd, cfg->cfg.pageProgramCmd);
    printk("   qpageProgramCmd: %u(0x%X)\n", cfg->cfg.qpageProgramCmd, cfg->cfg.qpageProgramCmd);
    printk("   qppAddrMode: %u(0x%X)\n", cfg->cfg.qppAddrMode, cfg->cfg.qppAddrMode);
    printk("   fastReadCmd: %u(0x%X)\n", cfg->cfg.fastReadCmd, cfg->cfg.fastReadCmd);
    printk("   frDmyClk: %u(0x%X)\n", cfg->cfg.frDmyClk, cfg->cfg.frDmyClk);
    printk("   qpiFastReadCmd: %u(0x%X)\n", cfg->cfg.qpiFastReadCmd, cfg->cfg.qpiFastReadCmd);
    printk("   qpiFrDmyClk: %u(0x%X)\n", cfg->cfg.qpiFrDmyClk, cfg->cfg.qpiFrDmyClk);
    printk("   fastReadDoCmd: %u(0x%X)\n", cfg->cfg.fastReadDoCmd, cfg->cfg.fastReadDoCmd);
    printk("   frDoDmyClk: %u(0x%X)\n", cfg->cfg.frDoDmyClk, cfg->cfg.frDoDmyClk);
    printk("   fastReadDioCmd: %u(0x%X)\n", cfg->cfg.fastReadDioCmd, cfg->cfg.fastReadDioCmd);
    printk("   frDioDmyClk: %u(0x%X)\n", cfg->cfg.frDioDmyClk, cfg->cfg.frDioDmyClk);
    printk("   fastReadQoCmd: %u(0x%X)\n", cfg->cfg.fastReadQoCmd, cfg->cfg.fastReadQoCmd);
    printk("   frQoDmyClk: %u(0x%X)\n", cfg->cfg.frQoDmyClk, cfg->cfg.frQoDmyClk);
    printk("   fastReadQioCmd: %u(0x%X)\n", cfg->cfg.fastReadQioCmd, cfg->cfg.fastReadQioCmd);
    printk("   frQioDmyClk: %u(0x%X)\n", cfg->cfg.frQioDmyClk, cfg->cfg.frQioDmyClk);
    printk("   qpiFastReadQioCmd: %u(0x%X)\n", cfg->cfg.qpiFastReadQioCmd, cfg->cfg.qpiFastReadQioCmd);
    printk("   qpiFrQioDmyClk: %u(0x%X)\n", cfg->cfg.qpiFrQioDmyClk, cfg->cfg.qpiFrQioDmyClk);
    printk("   qpiPageProgramCmd: %u(0x%X)\n", cfg->cfg.qpiPageProgramCmd, cfg->cfg.qpiPageProgramCmd);
    printk("   writeVregEnableCmd: %u(0x%X)\n", cfg->cfg.writeVregEnableCmd, cfg->cfg.writeVregEnableCmd);
    printk("   wrEnableIndex: %u(0x%X)\n", cfg->cfg.wrEnableIndex, cfg->cfg.wrEnableIndex);
    printk("   qeIndex: %u(0x%X)\n", cfg->cfg.qeIndex, cfg->cfg.qeIndex);
    printk("   busyIndex: %u(0x%X)\n", cfg->cfg.busyIndex, cfg->cfg.busyIndex);
    printk("   wrEnableBit: %u(0x%X)\n", cfg->cfg.wrEnableBit, cfg->cfg.wrEnableBit);
    printk("   qeBit: %u(0x%X)\n", cfg->cfg.qeBit, cfg->cfg.qeBit);
    printk("   busyBit: %u(0x%X)\n", cfg->cfg.busyBit, cfg->cfg.busyBit);
    printk("   wrEnableWriteRegLen: %u(0x%X)\n", cfg->cfg.wrEnableWriteRegLen, cfg->cfg.wrEnableWriteRegLen);
    printk("   wrEnableReadRegLen: %u(0x%X)\n", cfg->cfg.wrEnableReadRegLen, cfg->cfg.wrEnableReadRegLen);
    printk("   qeWriteRegLen: %u(0x%X)\n", cfg->cfg.qeWriteRegLen, cfg->cfg.qeWriteRegLen);
    printk("   qeReadRegLen: %u(0x%X)\n", cfg->cfg.qeReadRegLen, cfg->cfg.qeReadRegLen);
    printk("   rsvd1: %u(0x%X)\n", cfg->cfg.rsvd1, cfg->cfg.rsvd1);
    printk("   busyReadRegLen: %u(0x%X)\n", cfg->cfg.busyReadRegLen, cfg->cfg.busyReadRegLen);
    printk("   readRegCmd[4]: %u(0x%X), %u(0x%X), %u(0x%X), %u(0x%X)\n",
            cfg->cfg.readRegCmd[0], cfg->cfg.readRegCmd[0],
            cfg->cfg.readRegCmd[1], cfg->cfg.readRegCmd[1],
            cfg->cfg.readRegCmd[2], cfg->cfg.readRegCmd[2],
            cfg->cfg.readRegCmd[3], cfg->cfg.readRegCmd[3]
    );
    printk("   writeRegCmd[4]: %u(0x%X), %u(0x%X), %u(0x%X), %u(0x%X)\n",
        cfg->cfg.writeRegCmd[0], cfg->cfg.writeRegCmd[0],
        cfg->cfg.writeRegCmd[1], cfg->cfg.writeRegCmd[1],
        cfg->cfg.writeRegCmd[2], cfg->cfg.writeRegCmd[2],
        cfg->cfg.writeRegCmd[3], cfg->cfg.writeRegCmd[3]
    );
    printk("   enterQpi: %u(0x%X)\n", cfg->cfg.enterQpi, cfg->cfg.enterQpi);
    printk("   exitQpi: %u(0x%X)\n", cfg->cfg.exitQpi, cfg->cfg.exitQpi);
    printk("   cReadMode: %u(0x%X)\n", cfg->cfg.cReadMode, cfg->cfg.cReadMode);
    printk("   cRExit: %u(0x%X)\n", cfg->cfg.cRExit, cfg->cfg.cRExit);
    printk("   burstWrapCmd: %u(0x%X)\n", cfg->cfg.burstWrapCmd, cfg->cfg.burstWrapCmd);
    printk("   burstWrapCmdDmyClk: %u(0x%X)\n", cfg->cfg.burstWrapCmdDmyClk, cfg->cfg.burstWrapCmdDmyClk);
    printk("   burstWrapDataMode: %u(0x%X)\n", cfg->cfg.burstWrapDataMode, cfg->cfg.burstWrapDataMode);
    printk("   burstWrapData: %u(0x%X)\n", cfg->cfg.burstWrapData, cfg->cfg.burstWrapData);
    printk("   deBurstWrapCmd: %u(0x%X)\n", cfg->cfg.deBurstWrapCmd, cfg->cfg.deBurstWrapCmd);
    printk("   deBurstWrapCmdDmyClk: %u(0x%X)\n", cfg->cfg.deBurstWrapCmdDmyClk, cfg->cfg.deBurstWrapCmdDmyClk);
    printk("   deBurstWrapDataMode: %u(0x%X)\n", cfg->cfg.deBurstWrapDataMode, cfg->cfg.deBurstWrapDataMode);
    printk("   deBurstWrapData: %u(0x%X)\n", cfg->cfg.deBurstWrapData, cfg->cfg.deBurstWrapData);
    printk("   timeEsector: %u(0x%X)\n", cfg->cfg.timeEsector, cfg->cfg.timeEsector);
    printk("   timeE32k: %u(0x%X)\n", cfg->cfg.timeE32k, cfg->cfg.timeE32k);
    printk("   timeE64k: %u(0x%X)\n", cfg->cfg.timeE64k, cfg->cfg.timeE64k);
    printk("   timePagePgm: %u(0x%X)\n", cfg->cfg.timePagePgm, cfg->cfg.timePagePgm);
    printk("   timeCe: %u(0x%X)\n", cfg->cfg.timeCe, cfg->cfg.timeCe);
    printk(" CRC32(%lu): %02X%02X%02X%02X\n",
        sizeof(cfg->crc32),
        (cfg->crc32 >> 24) & 0xFF,
        (cfg->crc32 >> 16) & 0xFF,
        (cfg->crc32 >> 8) & 0xFF,
        (cfg->crc32 >> 0) & 0xFF
    );
    printk("--------\n");
#else
    BL_DBG("skip %s\n",__func__);
#endif

    return 0;
}
#if 0
static int firmware_dump_pllcfg(const boot_pll_cfg_t *cfg)
{

    printk("[************PLL CFG(%lu)************]\n", sizeof(boot_pll_cfg_t));
    printk(" magiccode: 0x%08X\n", cfg->magiccode);
    printk(" root_clk: %u(0x%X)\n", cfg->root_clk, cfg->root_clk);
    printk(" xtal_type: %u(0x%X)\n", cfg->xtal_type, cfg->xtal_type);
    printk(" pll_clk: %u(0x%X)\n", cfg->pll_clk, cfg->pll_clk);
    printk(" hclk_div: %u(0x%X)\n", cfg->hclk_div, cfg->hclk_div);
    printk(" bclk_div: %u(0x%X)\n", cfg->bclk_div, cfg->bclk_div);
    printk(" flash_clk_div: %u(0x%X)\n", cfg->flash_clk_div, cfg->flash_clk_div);
    printk(" uart_clk_div: %u(0x%X)\n", cfg->uart_clk_div, cfg->uart_clk_div);
    printk(" sdu_clk_div: %u(0x%X)\n", cfg->sdu_clk_div, cfg->sdu_clk_div);
    printk(" CRC32(%lu): %02X%02X%02X%02X\n",
        sizeof(cfg->crc32),
        (cfg->crc32 >> 24) & 0xFF,
        (cfg->crc32 >> 16) & 0xFF,
        (cfg->crc32 >> 8) & 0xFF,
        (cfg->crc32 >> 0) & 0xFF
    );
    printk("--------\n");
    printk("skip %s\n",__func__);
    return 0;
}
#endif

static int firmware_dump_encryptiondata(const u8 *encryption)
{
#if 0
    //FIXME NOT use magic number
    printk("[************ENCRYPTION DATA (20)************]\n");
    printk(" IV(16): %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
        encryption[0],
        encryption[1],
        encryption[2],
        encryption[3],
        encryption[4],
        encryption[5],
        encryption[6],
        encryption[7],
        encryption[8],
        encryption[9],
        encryption[10],
        encryption[11],
        encryption[12],
        encryption[13],
        encryption[14],
        encryption[15]
    );
    printk(" CRC32(4): %02X%02X%02X%02X\n",
        encryption[19],
        encryption[18],
        encryption[17],
        encryption[16]
    );
    printk("--------\n");
#else
    BL_DBG("skip %s\n",__func__);
#endif

    return 0;
}

static int firmware_dump_publickey(const pkey_cfg_t *key)
{
#if 0
    printk("[************PUBLIC KEY DATA(%lu)************]\n", sizeof(pkey_cfg_t));
    printk(" public key X(%lu):\n", sizeof(key->eckeyx));
    printk("   %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
        key->eckeyx[0],
        key->eckeyx[1],
        key->eckeyx[2],
        key->eckeyx[3],
        key->eckeyx[4],
        key->eckeyx[5],
        key->eckeyx[6],
        key->eckeyx[7],
        key->eckeyx[8],
        key->eckeyx[9],
        key->eckeyx[10],
        key->eckeyx[11],
        key->eckeyx[12],
        key->eckeyx[13],
        key->eckeyx[14],
        key->eckeyx[15],
        key->eckeyx[16],
        key->eckeyx[17],
        key->eckeyx[18],
        key->eckeyx[19],
        key->eckeyx[20],
        key->eckeyx[21],
        key->eckeyx[22],
        key->eckeyx[23],
        key->eckeyx[24],
        key->eckeyx[25],
        key->eckeyx[26],
        key->eckeyx[27],
        key->eckeyx[28],
        key->eckeyx[29],
        key->eckeyx[30],
        key->eckeyx[31]
    );
    printk(" public key Y(%lu):\n", sizeof(key->eckeyy));
    printk("   %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
        key->eckeyy[0],
        key->eckeyy[1],
        key->eckeyy[2],
        key->eckeyy[3],
        key->eckeyy[4],
        key->eckeyy[5],
        key->eckeyy[6],
        key->eckeyy[7],
        key->eckeyy[8],
        key->eckeyy[9],
        key->eckeyy[10],
        key->eckeyy[11],
        key->eckeyy[12],
        key->eckeyy[13],
        key->eckeyy[14],
        key->eckeyy[15],
        key->eckeyy[16],
        key->eckeyy[17],
        key->eckeyy[18],
        key->eckeyy[19],
        key->eckeyy[20],
        key->eckeyy[21],
        key->eckeyy[22],
        key->eckeyy[23],
        key->eckeyy[24],
        key->eckeyy[25],
        key->eckeyy[26],
        key->eckeyy[27],
        key->eckeyy[28],
        key->eckeyy[29],
        key->eckeyy[30],
        key->eckeyy[31]
    );
    printk(" CRC32(%lu): %02X%02X%02X%02X\n",
        sizeof(key->crc32),
        (key->crc32 >> 24) & 0xFF,
        (key->crc32 >> 16) & 0xFF,
        (key->crc32 >> 8) & 0xFF,
        (key->crc32 >> 0) & 0xFF
    );
    printk("--------\n");
#else
    BL_DBG("skip %s\n",__func__);
#endif

    return 0;
}

static int firmware_dump_signdata(const sign_cfg_t *sign)
{
#if 0
    int i;

    printk("[************SIGNATURE DATA(%lu)************]\n", sizeof(sign_cfg_t) + sign->sig_len);
    printk(" sig_len: %u(0x%X)\n", sign->sig_len, sign->sig_len);
    printk(" signature(%u):\n", sign->sig_len);

    for (i = 0; i < sign->sig_len; i+= 32) {
        printk("   %03u: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
            i,
            sign->signature[i + 0],
            sign->signature[i + 1],
            sign->signature[i + 2],
            sign->signature[i + 3],
            sign->signature[i + 4],
            sign->signature[i + 5],
            sign->signature[i + 6],
            sign->signature[i + 7],
            sign->signature[i + 8],
            sign->signature[i + 9],
            sign->signature[i + 10],
            sign->signature[i + 11],
            sign->signature[i + 12],
            sign->signature[i + 13],
            sign->signature[i + 14],
            sign->signature[i + 15],
            sign->signature[i + 16],
            sign->signature[i + 17],
            sign->signature[i + 18],
            sign->signature[i + 19],
            sign->signature[i + 20],
            sign->signature[i + 21],
            sign->signature[i + 22],
            sign->signature[i + 23],
            sign->signature[i + 24],
            sign->signature[i + 25],
            sign->signature[i + 26],
            sign->signature[i + 27],
            sign->signature[i + 28],
            sign->signature[i + 29],
            sign->signature[i + 30],
            sign->signature[i + 31]
        );
    }
    printk(" CRC32(%lu): %02X%02X%02X%02X\n",
        sizeof(sign->crc32),
        sign->signature[sign->sig_len],
        sign->signature[sign->sig_len + 1],
        sign->signature[sign->sig_len + 2],
        sign->signature[sign->sig_len + 3]
    );
    printk("--------\n");
#else
    BL_DBG("skip %s\n",__func__);
#endif

    return 0;
}

static int firmware_dump_segment_piece(const segment_header_t *segment)
{
#if 0
    printk("[************SEGMENT DUMP(%lu + %u = %lu)************]\n",
        sizeof(segment_header_t),
        segment->len,
        sizeof(segment_header_t) + segment->len
    );
    printk(" destaddr: %u(0x%X)\n", segment->destaddr, segment->destaddr);
    printk(" len: %u(0x%X)\n", segment->len, segment->len);
    printk(" rsv: %u(0x%X)\n", segment->rsv, segment->rsv);
    printk(" crc32: %u(0x%X)\n", segment->crc32, segment->crc32);
    printk("--------\n");
#else
    BL_DBG("skip %s\n",__func__);
#endif

    return 0;
}

static int firmware_download_head(struct bl_plat *card, 
                                bootrom_host_cmd_t *cmd, u8* response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int ret;

    SDIO_DBG(SDIO_FN_ENTRY_STR);
    BL_DBG("FUNC header: data ptr %p, data left %d\n", *data, *len);
    if (*len < sizeof(bootheader_t)) {
        printk("%s:len too small %zu:%d\n", __func__, sizeof(bootheader_t), *len);
        return -1;
    }

    firmware_dump_head(header);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_bootheader_load(cmd, header);
    BL_DBG("CMD bootheader len %d\n", cmd->len);
    
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("NULL response get");
        return -1;
    }
    
    bl_bootrom_cmd_bootheader_load_get_res((bootrom_res_bootheader_load_t*)response);

    *data += sizeof(bootheader_t);
    *len -= sizeof(bootheader_t);

    SDIO_DBG(SDIO_FN_LEAVE_STR);

    return 0;
}

__attribute__((unused)) static int firmware_download_flashcfg(struct bl_plat *card, 
                    bootrom_host_cmd_t *cmd, u8 *response, bootheader_t *header)
{
    firmware_dump_flashcfg(&(header->flashCfg));

    return 0;
}

#if 0
static int firmware_download_pllcfg(struct bl_plat *card, bootrom_host_cmd_t *cmd, u8* response,
    bootheader_t *header)
{
    firmware_dump_pllcfg(&(header->pllCfg));

    return 0;
}
#endif

static int firmware_download_encryptiondata(struct bl_plat *card,
                                bootrom_host_cmd_t *cmd, u8 *response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int ret;

    SDIO_DBG(SDIO_FN_ENTRY_STR);

    if (bl_mod_params.mp_mode)
        return 0;

    BL_DBG("FUNC encryptiondata: data ptr %p, data left %d\n", *data, *len);
    if (0 == encryption_mode) {
        printk("no encrypt field detected\n");
        return 0;
    }

    BL_DBG("%s, Offset 0x%X\n", __func__, (PTR2UINT)(((u8*)*data) - ((u8*)header)));
    //FIXME NOT use magic number
    if (*len < 20) {
        printk("%s:len too small %d:%d\n", __func__, 20, *len);
        return -1;
    }

    firmware_dump_encryptiondata(*data);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_aesiv_load(cmd, *data);
    BL_DBG("CMD encryptiondata len %d\n", cmd->len);
    
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("NULL response get");
        return -1;
    }
    
    bl_bootrom_cmd_aesiv_load_get_res((bootrom_res_aesiv_load_t*)response);

    //FIXME NOT use magic number
    *data += 20;
    *len -= 20;

    SDIO_DBG(SDIO_FN_LEAVE_STR);

    return 0;
}

static int firmware_download_publickey1(struct bl_plat *card, 
                                bootrom_host_cmd_t *cmd, u8 *response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int ret;
    pkey_cfg_t *key;

    if (bl_mod_params.mp_mode)
        return 0;

    BL_DBG("FUNC publickey1: data ptr %p, data left %d\n", *data, *len);
    if (0 == signature_mode) {
        printk("no sign field detected\n");
        return 0;
    }

    BL_DBG("%s, Offset 0x%X\n", __func__, (PTR2UINT)(((u8*)*data) - ((u8*)header)));
    key = (pkey_cfg_t*)(*data);
    if (*len < sizeof(pkey_cfg_t)) {
        printk("%s:len too small %zu:%d\n", __func__, sizeof(pkey_cfg_t), *len);
        return -1;
    }
    firmware_dump_publickey(key);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_pkey1_load(cmd, key);
    BL_DBG("CMD public key1 len %d\n", cmd->len);
    
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("NULL response get");
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

#if 0
static int firmware_download_publickey2(struct bl_plat *card, bootrom_host_cmd_t *cmd, u8 *response,
    bootheader_t *header, const u8 **data, int *len)
{
    int ret;
    pkey_cfg_t *key;

    printk("FUNC publickey2: data ptr %p, data left %d\n", *data, *len);
    if (0 == header->seccfg.bval.sign) {
        printk("no sign field detected\n");
        return 0;
    }

    printk("%s, Offset 0x%X\n", __func__, (PTR2UINT)(((u8*)*data) - ((u8*)header)));
    key = (pkey_cfg_t*)(*data);
    if (*len < sizeof(pkey_cfg_t)) {
        printk("%s:len too small %lu:%d\n", __func__, sizeof(pkey_cfg_t), *len);
        return -1;
    }
    firmware_dump_publickey(key);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_pkey2_load(cmd, key);
    printk("CMD public key2 len %d\n", cmd->len);
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("NULL response get");
        return -1;
    }
    ret = bl_bootrom_cmd_pkey2_load_get_res((bootrom_res_pkey_load_t*)response);
    if (ret) {
        printk("Error response get pkey2\n");
        return -1;
    }

    *data += sizeof(pkey_cfg_t);
    *len -= sizeof(pkey_cfg_t);

    return 0;
}
#endif

static int firmware_download_signdata1(struct bl_plat *card, 
                                bootrom_host_cmd_t *cmd, u8 *response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int ret;
    sign_cfg_t *sign;

    if (bl_mod_params.mp_mode)
        return 0;

    BL_DBG("FUNC signdata1: data ptr %p, data left %d\n", *data, *len);
    if (0 == signature_mode) {
        printk("no sign field detected\n");
        return 0;
    }

    BL_DBG("%s, Offset 0x%X\n", __func__, (PTR2UINT)(((u8*)*data) - ((u8*)header)));
    sign = (sign_cfg_t*)(*data);

    if (*len < sizeof(sign_cfg_t) + sign->sig_len) {
        printk("%s:len too small %zu:%d\n", __func__, sizeof(sign_cfg_t), *len);
        return -1;
    }

    firmware_dump_signdata(sign);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_signature1_load(cmd, (uint8_t*)sign, sizeof(sign_cfg_t) + sign->sig_len);
    BL_DBG("CMD signa data1 len %d\n", cmd->len);
    
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("NULL response get");
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

#if 0
static int firmware_download_signdata2(struct bl_plat *card, bootrom_host_cmd_t *cmd, u8 *response,
    bootheader_t *header, const u8 **data, int *len)
{
    int ret;
    sign_cfg_t *sign;

    printk("FUNC signdata2: data ptr %p, data left %d\n", *data, *len);
    if (0 == header->seccfg.bval.sign) {
        printk("no sign field detected\n");
        return 0;
    }

    printk("%s, Offset 0x%X\n", __func__, (PTR2UINT)(((u8*)*data) - ((u8*)header)));
    sign = (sign_cfg_t*)(*data);
    if (*len < sizeof(sign_cfg_t) + sign->sig_len) {
        printk("%s:len too small %lu:%d\n", __func__, sizeof(sign_cfg_t), *len);
        return -1;
    }

    firmware_dump_signdata(sign);

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_signature2_load(cmd, (uint8_t*)sign, sizeof(sign_cfg_t) + sign->sig_len);
    printk("CMD signa data2 len %d\n", cmd->len);
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("NULL response get");
        return -1;
    }
    ret = bl_bootrom_cmd_signature2_get_res((bootrom_res_signature_load_t*)response);
    if (ret) {
        printk("Error response get signature2\n");
        return -1;
    }

    *data += (sizeof(sign_cfg_t) + sign->sig_len);
    *len -= (sizeof(sign_cfg_t) + sign->sig_len);

    return 0;
}
#endif

static int firmware_download_segment_piece(struct bl_plat *card,
                                bootrom_host_cmd_t *cmd, u8 *response,
                                bootheader_t *header, const u8 **data, int *len)
{
    const segment_header_t *segment;
    segment_header_t segment_plain;
    int ret = 0, i, pos, wr_len;

    SDIO_DBG(SDIO_FN_ENTRY_STR);

    BL_DBG("FUNC segment_piece: data ptr %p, data left %d\n", *data, *len);
    segment = (const segment_header_t*)(*data);
    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_sectionheader_load(cmd, segment);
    BL_DBG("CMD segment piece len %d\n", cmd->len);
    
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        return -1;
    }
    
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("NULL response get\n");
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

    pos = 0;
    i = 0;
    while (pos < segment->len) {
#define WR_LEN ((BOOTROM_DATA_BUFFER_SIZE - sizeof(bootrom_host_cmd_t)) & 0xFFFFFFF0)
        wr_len = segment->len - pos;
        wr_len = (wr_len < WR_LEN) ?  wr_len : WR_LEN;
        BL_DBG("------piece %03d[%03d] %d:%u------\n", i++, wr_len, pos, segment->len);
#if 1
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
#endif

        bl_bootrom_cmd_sectiondata_load(cmd, *data + sizeof(segment_header_t) + pos, wr_len);
#if 1
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
#endif

        BL_DBG("CMD segment piece loop len %d\n", cmd->len);
        ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
        if (ret) {
            printk("ERR when write\n");
            return -1;
        }
        
#if 1
        ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
        if (ret) {
            printk("NULL response get");
            return -1;
        }
        bl_bootrom_cmd_sectiondata_get_res((bootrom_res_sectiondata_load_t*)response);
#endif
        pos += wr_len;
    }

    *data += (sizeof(segment_header_t) + segment->len);
    *len -= (sizeof(segment_header_t) + segment->len);

    SDIO_DBG(SDIO_FN_LEAVE_STR);

    return 0;
}

static int firmware_download_segment(struct bl_plat *card, 
                                bootrom_host_cmd_t *cmd, u8 *response,
                                bootheader_t *header, const u8 **data, int *len)
{
    int i, ret;

    BL_DBG("SEGMENT AERA: @Offset 0x%X\n", (PTR2UINT)((u8*)(*data) - (u8*)(header)));
    BL_DBG("FUNC segment: data ptr %p, data left %d\n", *data, *len);
    for (i = 0; i < header->basic_cfg.img_len_cnt; i++) {
        BL_DBG("------SEGMENT[%02d]@0x%X------\n", 
               i, (PTR2UINT)((u8*)(*data) - (u8*)(header)));
               
        ret = firmware_download_segment_piece(card, cmd, response, header, data, len);
        if (ret) {
            break;
        }
    }

    return ret;
}

static int firmware_download_image_check(struct bl_plat *card, 
                    bootrom_host_cmd_t *cmd, u8 *response, bootheader_t *header)
{
    int ret;

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_checkimage_get(cmd);
    BL_DBG("CMD checkimage len %d\n", cmd->len);
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        
        return -1;
    }
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("NULL response get");
        
        return -1;
    }
    ret = bl_bootrom_cmd_checkimage_get_res((bootrom_res_checkimage_t*)response);
    if (ret) {
        printk("Error response get checkimage\n");
        
        return -1;
    }

    return 0;
}

static int firmware_download_image_run(struct bl_plat *card,
                    bootrom_host_cmd_t *cmd, u8 *response, bootheader_t *header)
{
    int ret;

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_runimage_get(cmd);
    BL_DBG("CMD runimage len %d\n", cmd->len);
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        
        return -1;
    }
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("NULL response get");
        
        return -1;
    }
    bl_bootrom_cmd_runimage_get_res((bootrom_res_runimage_t*)response);

    return 0;
}

static int firmware_downloader(struct bl_plat *card, const u8 *data, int len)
{
    int ret = 0;
    bootheader_t *header;
    bootrom_host_cmd_t *cmd = NULL;
    u8 *response = NULL;

    if (len < sizeof(bootheader_t)) {
        printk("Illegal len when check bootheader_t\n");
        
        return -1;
    }
    header = (bootheader_t*)data;
    cmd = kzalloc(BOOTROM_DATA_BUFFER_SIZE, GFP_KERNEL);
    if (NULL == cmd) {
        printk("NO MEM for alloc host CMD\n");
        
        return -1;
    }

    response = kzalloc(BOOTROM_CMD_BUFFER_SIZE, GFP_KERNEL);
    if (NULL == response) {
        printk("NO MEM for alloc CMD response\n");
        
        ret = -1;
        goto err;
    }

    /*do NOT change the order bellow*/
    if (firmware_download_head(card, cmd, response, header, &data, &len)
        //|| firmware_download_flashcfg(card, cmd, response, header)
        //|| firmware_download_pllcfg(card, cmd, response, header)
        || firmware_download_publickey1(card, cmd, response, header, &data, &len)
        //|| firmware_download_publickey2(card, cmd, response, header, &data, &len)
        || firmware_download_signdata1(card, cmd, response, header, &data, &len)
        //|| firmware_download_signdata2(card, cmd, response, header, &data, &len)
        || firmware_download_encryptiondata(card, cmd, response, header, &data, &len)
        || firmware_download_segment(card, cmd, response, header, &data, &len)
        || firmware_download_image_check(card, cmd, response, header)) {
        printk("firmware load faield\n");
        
        ret = -1;
        goto err;
    }
#if 0
    /*check if we need to download the other image*/
    if (len > 0) {
        printk("Potential Second image found\n");
        header = (bootheader_t*)data;//update header to the next image
        if (firmware_download_head(card, cmd, response, header, &data, &len)
            || firmware_download_flashcfg(card, cmd, response, header)
            || firmware_download_pllcfg(card, cmd, response, header)
            || firmware_download_publickey1(card, cmd, response, header, &data, &len)
            //|| firmware_download_publickey2(card, cmd, response, header, &data, &len)
            || firmware_download_signdata1(card, cmd, response, header, &data, &len)
            //|| firmware_download_signdata2(card, cmd, response, header, &data, &len)
            || firmware_download_encryptiondata(card, cmd, response, header, &data, &len)
            || firmware_download_segment(card, cmd, response, header, &data, &len)
            || firmware_download_image_check(card, cmd, response, header)) {
            printk("Sencond firmware load faield\n");
            return -1;
        }
    }
#endif

    if (firmware_download_image_run(card, cmd, response, header)) {
        printk("firmware check/Run faield\n");
        
        ret = -1;
    }

err:
    if (cmd)
        kfree(cmd);
    if (response)
        kfree(response);
    return ret;
}

static int bl_sdio_checkversion(struct bl_plat *card)
{
    bootrom_host_cmd_t *cmd = NULL;
    int ret = 0;
    u8 *response = NULL;

    SDIO_DBG(SDIO_FN_ENTRY_STR);

    cmd = kzalloc(BOOTROM_CMD_BUFFER_SIZE, GFP_KERNEL);
    if (NULL == cmd) {
        printk("NO MEM for alloc host CMD\n");
        
        return -1;
    }
    response = kzalloc(BOOTROM_CMD_BUFFER_SIZE, GFP_KERNEL);
    if (NULL == response) {
    
        printk("NO MEM for alloc CMD response\n");
        
        ret = -1;
        goto err;
    }

    memset(cmd, 0, BOOTROM_CMD_BUFFER_SIZE);
    memset(response, 0, BOOTROM_CMD_BUFFER_SIZE);
    bl_bootrom_cmd_bootinfo_get(cmd);
    
    printk("CMD check version len %d\n", cmd->len);
    
    ret = bl_io_write(card, (u8*)cmd, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
        printk("ERR when write\n");
        
        ret = -1;
        goto err;
    }
    ret = bl_io_read(card, response, BOOTROM_CMD_BUFFER_SIZE);
    if (ret) {
    
        printk("NULL response get");
        
        ret = -1;
        goto err;
    }
    bl_bootrom_cmd_bootinfo_get_res((bootrom_res_bootinfo_t*)response);
    card->chip_ver = ((bootrom_res_bootinfo_t*)response)->version;

    signature_mode = ((bootrom_res_bootinfo_t*)response)->signature_mode;
    encryption_mode = ((bootrom_res_bootinfo_t*)response)->encryption_mode;

err:
    if (response) {
        kfree(response);
    }
    
    if (cmd) {
        kfree(cmd);
    }
    
    SDIO_DBG(SDIO_FN_LEAVE_STR);

    return ret;
}

static int bl_sdio_download_firmware(struct bl_plat *card)
{
    const struct firmware *fw_helper = NULL;
    int ret, imagelen;
    const u8 *image = NULL;
    ktime_t start;
    s64 consume_time;
    char *fw_name;
    
#ifdef CONFIG_BL_DNLD_FWBIN
    if (card->chip_ver == CHIP_VER_616L) {
#ifdef CONFIG_FW_COMBO
        fw_name = BL616L_SD_COMBO_FW_NAME;
#else
        fw_name = BL616L_SD_WLAN_FW_NAME;
#endif
        
        if (bl_mod_params.mp_mode) {
            fw_name = BL616L_SD_MFG_FW_NAME;
        }
    } else {
#ifdef CONFIG_FW_COMBO
        fw_name = BL616_SD_COMBO_FW_NAME;
#else
        fw_name = BL616_SD_WLAN_FW_NAME;
#endif
        
        if (bl_mod_params.mp_mode) {
            fw_name = BL616_SD_MFG_FW_NAME;
        }
    }
    
    printk("Enter bl_sdio_download_firmware...\n");
    printk("to download sdio fw bin %s\n", fw_name);
    
    ret = request_firmware(&fw_helper, fw_name, &(((struct sdio_func *)card->dev)->dev));
    if ((ret < 0) || !fw_helper) {
        printk("request_firmware %s failed, error code = %d", fw_name, ret);
        
        ret = -ENOENT;
        return ret;
    }

    image = fw_helper->data;
    imagelen = fw_helper->size;
#else //CONFIG_BL_DNLD_FWBIN
#ifdef CONFIG_BL_MP
    if (bl_mod_params.mp_mode){
        if (card->chip_ver == CHIP_VER_616L) {
            image = bl616l_sd_mp_bin;
            imagelen = bl616l_sd_mp_bin_len;  
        } else {
            image = bl616_sd_mp_bin;
            imagelen = bl616_sd_mp_bin_len;  
        }
        
        printk("to download sdio mfg fw array\n");
    } else {
#endif
#ifdef CONFIG_FW_COMBO
#ifdef CONFIG_BL_BTSDU
        if (card->chip_ver == CHIP_VER_616L) {
            image = bl616l_sd_combo_ble_sdu_bin;
            imagelen = bl616l_sd_combo_ble_sdu_bin_len;
        } else {
            image = bl616_sd_combo_ble_sdu_bin;
            imagelen = bl616_sd_combo_ble_sdu_bin_len;
        }
        
        printk("to download sd_combo_ble_sdu fw array\n");
#else
        if (card->chip_ver == CHIP_VER_616L) {
            image = bl616l_sd_combo_ble_uart_bin;
            imagelen = bl616l_sd_combo_ble_uart_bin_len;
        } else {
            image = bl616_sd_combo_ble_uart_bin;
            imagelen = bl616_sd_combo_ble_uart_bin_len;
        }
        
        printk("to download sd_combo_ble_uart fw array\n");
#endif
#else  //!CONFIG_FW_COMBO
        if (card->chip_ver == CHIP_VER_616L) {
            image = bl616l_sd_wlan_bin;
            imagelen = bl616l_sd_wlan_bin_len;
        } else {
            image = bl616_sd_wlan_bin;
            imagelen = bl616_sd_wlan_bin_len;
        }
        
        printk("to download sd_wifi only fw array\n");
#endif //end CONFIG_FW_COMBO
#ifdef CONFIG_BL_MP
    }
#endif    
#endif

    SDIO_DBG("Downloading image (%d bytes)\n", imagelen);
    
    start = ktime_get();    
    firmware_downloader(card, image, imagelen);
    consume_time = ktime_us_delta(ktime_get(), start);
    
    SDIO_DBG("Download fw time: %lld\n", consume_time);
#ifdef CONFIG_BL_DNLD_FWBIN
    release_firmware(fw_helper);
#endif

    return ret;

}

/* only used for system reset and fw re-download test */
int bl_sdio_reset(struct bl_hw *bl_hw)
{
    /// If system reset without sdu reset, should reset bitmap in host.
    int ret = 0, status = 0;
    u8_l val = 0, count = 0, fw_ready = 0;
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;

    printk("%s===================>test start\n", __func__);
    
    card->curr_rd_port = 0;
    card->curr_wr_port = 0;
    //ret = bl_write_reg(bl_hw, card->reg->host_int_enable, 0);
    ret = bl_write_reg(bl_hw, card->reg->host_int_mask_reg, 0);
    ret |= bl_write_reg(bl_hw, card->reg->wr_bitmap_l, 0);
    ret |= bl_write_reg(bl_hw, card->reg->wr_bitmap_u, 0);
    ret |= bl_write_reg(bl_hw, card->reg->rd_bitmap_l, 0);
    ret |= bl_write_reg(bl_hw, card->reg->rd_bitmap_u, 0);

    printk("disable sdio int, reset sdio bitmap ret=%d\n", ret);

    msleep(2000);
    printk("sleep 2s, then write fun0 addr6[3]\n");

    status = bl_sdio_read_reg_func0(card, CARD_IO_ABORT, &val);
    
    printk("Read F0 addr6[3] val=0x%x status=0x%x\r\n", val, status);
    
    val |= CARD_SYS_RESRT;
    status = 0;
    status = bl_sdio_write_reg_func0(card, CARD_IO_ABORT, val);
    
    printk("Write addr6[3] val=0x%x status0x%x\r\n", val, status);

#if 0
    /// if SDU not reset, scratch reg value should be keep
    printk("Enter check FW_STATUS 3\n");
    sdio_claim_host(bl_hw->plat->dev);
    val = sdio_readb(bl_hw->plat->dev, CARD_FW_STATUS0_REG, &ret);
    sdio_release_host(bl_hw->plat->dev);

    printk(" FW_STATUS : %d ret=%d\n", val, ret);
#endif
    mdelay(200);
    ret |= bl_sdio_download_firmware(bl_hw->plat);
    
    printk("Wait firmware...\n");
    
    do {
        bl_read_reg(bl_hw, CARD_FW_STATUS0_REG, &fw_ready);
        
        printk("FW Mark %c:%x\n", fw_ready, fw_ready);
        
        count++;
        mdelay(100);
    } while ('B' != fw_ready && count < 50);//TODO put 'B' into header file

    if(count >= 50) {
        printk("%s wait fw ready TO count=%d\n", __func__, count);
    }
    
    //comment out? duplicate?
    bl_write_reg(bl_hw, CARD_FW_STATUS0_REG, 'L');
    //bl_sdio_init(bl_hw->plat);

    printk("%s===================>test end ret=%d\n", __func__, ret);

    return ret;
}

/* only used for system reset */
int bl_sdio_sysreset(struct bl_hw *bl_hw)
{
    int status = 0;
    u8_l val = 0;
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    
    status = bl_sdio_read_reg_func0(card, CARD_IO_ABORT, &val);
    
    printk("Read F0 addr6[3] val=0x%x status=0x%x\r\n", val, status);
    
    val |= CARD_SYS_RESRT;
    status = 0;
    status = bl_sdio_write_reg_func0(card, CARD_IO_ABORT, val);
    
    printk("Write addr6[3] val=0x%x status0x%x\r\n", val, status);
    
    return status;
}

int bl_sdio_init(struct bl_plat *card)
{
    u32 ret = 0;
    //SDIO_DBG(SDIO_FN_ENTRY_STR);

#ifdef BL_FPGA
    /*directly write addr:0x60=1, */
    sdio_claim_host(card->dev);
    sdio_writeb(card->dev, 1, 0x60, &ret);
    sdio_release_host(card->dev);

    printk("[FPGA][DBG]%s,set addr:0x60 = 1, ret = %d\n", __func__, ret);
    printk("[FPGA][DBG]%s,  skip bl_sdio_checkversion ret = %d\n", __func__, ret);
    printk("[FPGA][DBG]%s,  skip bl_sdio_download_firmware ret = %d\n", __func__, ret);
#else
    ret = bl_sdio_run_fw(card);
    ret = bl_sdio_checkversion(card);
    ret = bl_sdio_download_firmware(card);
#endif

    SDIO_DBG(SDIO_FN_LEAVE_STR);

    return ret;
}

int bl_read_data_sync_claim0(struct bl_hw *bl_hw, u8 *buffer, u32 len, u32 port)
{
    int ret;
    u8 blk_mode = (len < BL_SDIO_BLOCK_SIZE) ? BYTE_MODE : BLOCK_MODE;    
    u32 blk_size = (blk_mode == BLOCK_MODE) ? BL_SDIO_BLOCK_SIZE : 1;
    u32 blk_cnt = (blk_mode == BLOCK_MODE) ? ((len + BL_SDIO_BLOCK_SIZE -1)/BL_SDIO_BLOCK_SIZE) : len;
    u32 ioport = (port & BL_SDIO_IO_PORT_MASK);

    ret = sdio_readsb(bl_hw->plat->dev, buffer, ioport, blk_cnt * blk_size);

    return ret;
}

/*
 * This function writes multiple data into SDIO card memory.
 *
 * This does not work in suspended mode.
 */
int bl_write_data_sync(struct bl_hw *bl_hw, u8 *buffer, u32 pkt_len, u32 port)
{
    int ret, cnt = 5;
    u8 blk_mode = (pkt_len < BL_SDIO_BLOCK_SIZE) ? BYTE_MODE : BLOCK_MODE;
    u32 blk_size = (blk_mode == BLOCK_MODE) ? BL_SDIO_BLOCK_SIZE : 1;
    u32 blk_cnt = (blk_mode == BLOCK_MODE) ? ((pkt_len + BL_SDIO_BLOCK_SIZE -1)/BL_SDIO_BLOCK_SIZE) : pkt_len;

    u32 ioport = (port & BL_SDIO_IO_PORT_MASK);

    sdio_claim_host(bl_hw->plat->dev);
    ret = sdio_writesb(bl_hw->plat->dev, ioport, buffer, blk_cnt * blk_size);
    while (ret && cnt)
    {
        printk("sdio writesb fail %d, retry again \n", ret);
        
        ret = sdio_writesb(bl_hw->plat->dev, ioport, buffer, blk_cnt * blk_size);
        cnt --;
    }    
    sdio_release_host(bl_hw->plat->dev);

    return ret;
}

/*
 * This function reads multiple data from SDIO card memory.
 */
int bl_read_data_sync(struct bl_hw *bl_hw, u8 *buffer, u32 len, u32 port)
{
    int ret, cnt = 5;
    u8 blk_mode = (len < BL_SDIO_BLOCK_SIZE) ? BYTE_MODE : BLOCK_MODE;    
    u32 blk_size = (blk_mode == BLOCK_MODE) ? BL_SDIO_BLOCK_SIZE : 1;
    u32 blk_cnt = (blk_mode == BLOCK_MODE) ? ((len + BL_SDIO_BLOCK_SIZE -1)/BL_SDIO_BLOCK_SIZE) : len;
    u32 ioport = (port & BL_SDIO_IO_PORT_MASK);

    sdio_claim_host(bl_hw->plat->dev);
    ret = sdio_readsb(bl_hw->plat->dev, buffer, ioport, blk_cnt * blk_size);
    while (ret && cnt)
    {
        printk("sdio readsb fail %d, retry again, cnt=%d\n", ret, cnt);  
        
        ret = sdio_readsb(bl_hw->plat->dev, buffer, ioport, blk_cnt * blk_size);
        cnt --;
    }

    sdio_release_host(bl_hw->plat->dev);

    return ret;
}

/*
 * This function reads data from SDIO card register.
 */
int bl_read_reg(struct bl_hw *bl_hw, u32 reg, u8 *data)
{
    int ret = -1;

    ret = bl_sdio_read_reg(bl_hw->plat, reg, data);

    return ret;
}

/*
 * This function write data to SDIO card register.
 */
int bl_write_reg(struct bl_hw *bl_hw, u32 reg, u8 data)
{
    int ret;

    ret = bl_sdio_write_reg(bl_hw->plat, reg, data);

    return ret;
}

/*
 * This function write data to SDIO card register.
 */
int bl_write_reg_claim0(struct bl_hw *bl_hw, u32 reg, u8 data)
{
    int ret;

    sdio_writeb(bl_hw->plat->dev, data, reg, &ret);

    return ret;
}

static int bl_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
    struct bl_plat *bl_plat = NULL;
    void *drvdata;
    int ret = -ENODEV;

    BL_DBG(BL_FN_ENTRY_STR);

    printk("%s info: vendor=0x%4.04X device=0x%4.04X class=%d function=%d\n", __func__,
           func->vendor, func->device, func->class, func->num);

    printk("max_blksiz=%d->512, enable_timeout=%d->200\n",
           func->max_blksize, func->enable_timeout);
           
    func->max_blksize = 512;
    func->enable_timeout = 200;
    
    printk("after1 max_blksiz=%d, enable_timeout=%d\n", 
           func->max_blksize, func->enable_timeout);

    ret = bl_device_init(func, &bl_plat);
    if (ret)
        return ret;

    bl_plat->dev = func;

    ret = bl_platform_init(bl_plat, &drvdata);
    sdio_set_drvdata(func, drvdata);

    #ifdef CONFIG_BL_BTSDU
    if (ret) {
        bl_device_deinit(bl_plat);
        return ret;
    }

    if (bl_mod_params.mp_mode == 0) {
        ret = btsdio_probe(func, NULL, (struct bl_hw *)drvdata);
    }
    #endif

    if (ret)
        bl_device_deinit(bl_plat);

    return ret;
}

static void bl_sdio_remove(struct sdio_func *func)
{    
    struct bl_hw *bl_hw;
    struct bl_plat *bl_plat;

    BL_DBG(BL_FN_ENTRY_STR);

    bl_hw = sdio_get_drvdata(func);
    if (bl_hw && bl_hw->plat){
        bl_plat = bl_hw->plat;

        bl_hw->surprise_removed = true;

        bl_platform_deinit(bl_hw);
        bl_device_deinit(bl_plat);

        #ifdef CONFIG_BL_BTSDU
        if (bl_mod_params.mp_mode == 0) {
            btsdio_remove(func, bl_hw);
        }
        #endif
    
        sdio_set_drvdata(func, NULL);

        bl_sdio_sysreset(bl_hw);
    }
}

static struct sdio_driver bl_sdio_drv = {
    .name     = KBUILD_MODNAME,
    .id_table = bl_sdio_ids,
    .probe    = bl_sdio_probe,
    .remove   = bl_sdio_remove
};

int bl_sdio_register_drv(void)
{
    return sdio_register_driver(&bl_sdio_drv);
}

void bl_sdio_unregister_drv(void)
{
    sdio_unregister_driver(&bl_sdio_drv);
}
