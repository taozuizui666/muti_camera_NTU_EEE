/**
 ******************************************************************************
 *
 *  @file bl_irqs.c
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

#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/rtc.h>
#ifdef CONFIG_BL_BTSDU
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#endif

#include "softmac.h"

#include "bl_defs.h"
#include "bl_cmds.h"
#include "bl_ipc_host.h"
#if defined CONFIG_BL_SDIO
#include "bl_sdio.h"
#elif defined CONFIG_BL_USB
#include "bl_usb.h"
#endif
#include "bl_irqs.h"
#include "bl_strs.h"
#if defined(BL_BUS_LOOPBACK)
#include "bl_loopback.h"
#endif

#include "softmac.h"

#ifdef CONFIG_BL_BTSDU
extern int btsdio_rx_packet(struct btsdio_data *data,
                                 struct sk_buff *skb, uint32_t len);
#endif

#if defined CONFIG_BL_USB || defined CONFIG_BL_SDIO
/*
 * This function decodes a received packet.
 *
 * Based on the type, the packet is treated as either a data, or
 * a command response, or an event, and the correct handler
 * function is invoked.
 */
int bl_decode_rx_packet(struct bl_hw *bl_hw, struct sk_buff *skb)
{
    u8 tmp[sizeof(struct inf_hdr)];
    u32 pkt_len = 0;
    u32 pkt_type;
    u32 hw_idx;
    u32 reserved;
    u16 pad_len = 0;
    unsigned long now = jiffies;
    void *hostid = bl_hw->ipc_env->msga2e_hostid;
    u32 pld_len = le16_to_cpu(*(__le16 *) (skb->data));
    #ifndef  BL_RX_REORDER
    struct bl_agg_reodr_msg *agg_reord_msg = NULL;
    struct bl_agg_reord_pkt *reord_pkt = NULL;
    struct bl_agg_reord_pkt *reord_pkt_tmp = NULL;
    bool found = false;
    u8 count = 0;
    u16 sn_copy = 0;
    #endif
    #ifdef CONFIG_BL_BTSDU
    struct btsdio_data *btsdio_data = &bl_hw->btsdio_data;
    uint32_t btsdio_rx_len = 0;
    #endif

    if (!bl_hw->drv_ready) {
        dev_kfree_skb_any(skb);
        return 0;
    }

    if (skb->len < sizeof(struct inf_hdr)) {
        printk("%s: invalid skb->len: %d\n", __func__, skb->len);
        dev_kfree_skb_any(skb);
        
        return -1;
    }

    skb_copy_from_linear_data(skb, &tmp, sizeof(struct inf_hdr));

    pkt_len  = le16_to_cpu(*(__le16 *) (tmp + 0));
    pkt_type = le16_to_cpu(*(__le16 *) (tmp + 2));
    hw_idx   = le16_to_cpu(*(__le16 *) (tmp + 4));
    reserved = le16_to_cpu(*(__le16 *) (tmp + 6));

    if (bl_hw->mod_params->mp_mode == 0) {
        if (pkt_type == BL_TYPE_MSG || pkt_type == BL_TYPE_ACK) {
            static u32 last_rcv_msg_cnt = 0;

            BL_DBG_MSG("%s, rx msg or ack, pkt_len:%d, pkt_type:%d, hw_idx:%d, reserved:%d\n", 
                    __func__, pkt_len, pkt_type, hw_idx, reserved);
                    
            if ((last_rcv_msg_cnt != 0) && (last_rcv_msg_cnt+1 != reserved)) {
                BL_DBG("%s, last_rcv_msg_cnt:%u, cur:%u\n", 
                       __func__, last_rcv_msg_cnt, reserved);
            }
            
            last_rcv_msg_cnt = reserved;
        } else if (pkt_type == BL_TYPE_DATA) {
            BL_DBG("%s, rx data, reserved:%u, %u\n",
                   __func__, reserved, reserved>>4);
        } else if (pkt_type == BL_TYPE_TXCFM) {
            BL_DBG_MSG("%s, rx mgmt txcfm, reserved:%u, %u\n",
                   __func__, reserved, reserved>>4);
        } else {
            BL_DBG_MSG("%s, rx other pkt_type:%d, reserved:%u\n", 
                      __func__, pkt_type, reserved);
        }
        
        if(pkt_type == BL_TYPE_DATA || pkt_type == BL_TYPE_AGG_REORD_MSG) {
            pad_len = le16_to_cpu(*(__le16 *) (skb->data + 6)) & 0xF;
            
            BL_DBG("pad_len=%d\n", pad_len);
        }
    }
    
    skb_pull(skb, sizeof(struct inf_hdr)+pad_len);

    switch (pkt_type) {
        #ifdef CONFIG_BL_BTSDU
        case BL_TYPE_HCI:
            //printk("btsdio rx sdu len: %d, %d, %d\r\n", pkt_len, 
            //       (pkt_len + sizeof(struct inf_hdr) + (u8_l)(reserved & 0xF)), 
            //       bl_hw->mpa_rx_data.len_arr[pind]);
        
            btsdio_rx_len = *(uint32_t *)skb->data;
            
            BL_DBG("btsdio rx len over cmd, data:%d %d, %02x %02x %02x %02x %02x %02x %02x %02x\r\n", 
                   skb->len, btsdio_rx_len, 
                   skb->data[4], skb->data[5], skb->data[6], skb->data[7], 
                   skb->data[8], skb->data[9], skb->data[10], skb->data[11]);
                   
            skb_pull(skb, 4);  //BTSDU_MSG_LEN_FIELD takes 4 bytes
            pskb_trim(skb, btsdio_rx_len<skb->len?btsdio_rx_len:skb->len);
            
            btsdio_rx_packet(btsdio_data, skb, btsdio_rx_len);
            break;
        #endif
        
        case BL_TYPE_DATA:
            BL_DBG("info: ----------------------------------->>>Rx: Data packet\n");

            BL_DBG("pkt_type:%d, skb:0x%p, skb->len=%d, decoded data_count:%d\n", 
                   pkt_type, skb, skb->len, reserved>>4);
            
            bl_hw->rx_pkt_cnt++;
            bl_rxdataind(bl_hw, skb);
            bl_hw->stats.last_rx = now;
            break;

        case BL_TYPE_ACK:
            BL_DBG("info: ----------------------------------->>>Rx: ACK  packet\n");
            BL_DBG_MSG("Receive ACK: skb=0x%p-0x%p, host msga2e_cnt=%u, hostid:0x%p, fw msga2e_cnt=%u\n", 
                   skb, skb->data, bl_hw->ipc_env->msga2e_cnt, 
                   bl_hw->ipc_env->msga2e_hostid, ((*(u8 *)(skb->data)) & 0xFF));
            
            if (bl_hw->ipc_env->cb.recv_msgack_ind && hostid)
                bl_hw->ipc_env->cb.recv_msgack_ind(bl_hw, hostid);
            else
                BL_DBG_MSG("hostid is NULL 0x%p\n", hostid);

            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_MSG:
            BL_DBG_MSG("info: -------------------------------->>Rx: MSG  packet\n");

            if (bl_hw->ipc_env->cb.recv_msg_ind)
                bl_hw->ipc_env->cb.recv_msg_ind(bl_hw, (void *)(skb->data));
            else
                printk("bl_hw->ipc_env->cb.recv_msg_ind is NULL\n");

            dev_kfree_skb_any(skb);
            break;
            
        case BL_TYPE_DBG_DUMP_START:
            BL_DBG("info: --------------------------->Rx: DBG_DUMP_START packet\n");

            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_DBG_DUMP_END:
            BL_DBG("info: --------------------------->>>Rx: DBG_DUMP_END packet\n");

            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_DBG_LA_TRACE:
            BL_DBG("info: --------------------------->>>Rx: DBG_LA_TRACE packet\n");

            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_DBG_RBD_DESC:
            BL_DBG("info: --------------------------->>>Rx: DBG_RBD_DESC packet\n");
            
            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_DBG_RHD_DESC:
            BL_DBG("info: --------------------------->>>Rx: DBG_RHD_DESC packet\n");
            
            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_DBG_TX_DESC:
            BL_DBG("info: ---------------------------->>>Rx: DBG_TX_DESC packet\n");
            
            dev_kfree_skb_any(skb);
            break;
            
        case BL_TYPE_DUMP_INFO:
            BL_DBG("info: ------------------------------>>>Rx: DUMP_INFO packet\n");

            dev_kfree_skb_any(skb);
            break;

        //data tx cfm is handled in fw txu_cntrl_cfm, skip this part.
        case BL_TYPE_TXCFM:
            BL_DBG("info: ----------------------------------->>>Rx:TXCFM packet\n");
            {
                u32 i;
                u32 count;
                struct bl_hw_txhdr hw_hdr;
                struct bl_txq *txq = NULL;
                
                /* 1. get cfm count from skb->data */
                memcpy(&hw_hdr, (struct bl_hw_txhdr *)(skb->data),  
                       sizeof(struct bl_hw_txhdr));
                count = hw_hdr.cfm.count;
                
                BL_DBG("cfm.sn=%u, cfm.timestamp=%u, cfm.count=%u, cfm.credits=%d, status=%d\n", 
                       hw_hdr.cfm.sn, hw_hdr.cfm.timestamp, hw_hdr.cfm.count, 
                       hw_hdr.cfm.credits,  hw_hdr.cfm.status.value);
                //bl_dumpdata((u8 *)skb, skb->len);

                /* 2. handle cfm successively */
                for (i = 0; i < count; i++) {
                    ipc_host_tx_cfm_handler(bl_hw->ipc_env, hw_idx, 0, &hw_hdr, &txq);
                    
                    if(!txq)
                        goto TXCFM_EXIT;
                        
                    if ((hw_hdr.cfm.status.retry_required || 
                         hw_hdr.cfm.status.sw_retry_required) && (count == 1)) 
                    {
                        BL_DBG("Retry packet received, so we just exit\n");
                        goto RETRY_PACKET;
                    }
                }
                
                goto TXCFM_EXIT;

                //BL_DBG("get txq from cfm_handler: txq->idx=%d\n", txq->idx);

                /* 3. update txq->credits */
                if (txq->idx != TXQ_INACTIVE) {
                    if (hw_hdr.cfm.credits) {
                        BL_DBG("orign txq->idx=%d, txq->status=0x%x, txq->credits=%d\n", 
                                txq->idx, txq->status, txq->credits);
                        txq->credits += hw_hdr.cfm.credits;
                        BL_DBG("after add credits: hw_hdr.cfm.credits=%d, txq->idx=%d, txq->status=0x%x, txq->credits=%d\n",
                               hw_hdr.cfm.credits, txq->idx, txq->status, txq->credits);
                    }

                    /* continue service period */
                    if (unlikely(txq->push_limit && !bl_txq_is_full(txq))) {
                        bl_txq_add_to_hw_list(txq);
                    }
                }

                /* 4. update ampdu_size */
                if (hw_hdr.cfm.ampdu_size &&
                    hw_hdr.cfm.ampdu_size < IEEE80211_MAX_AMPDU_BUF)
                    bl_hw->stats.ampdus_tx[hw_hdr.cfm.ampdu_size - 1]++;

                /* 5. update amsdu_size */
                #ifdef CONFIG_BL_AMSDUS_TX
                txq->amsdu_len = hw_hdr.cfm.amsdu_size;
                #endif
            }
            
            bl_hw->stats.last_tx = now;
            
            TXCFM_EXIT:
            RETRY_PACKET:
            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_AGG_REORD_MSG:
            BL_DBG("info: ------------------------->>>Rx:AGG REORDER MSG packet\n");
            
            #ifndef BL_RX_REORDER
            agg_reord_msg = (struct bl_agg_reodr_msg *)(skb->data);

            sn_copy = agg_reord_msg->sn;

            BL_DBG("AGG_REORD_MSG: sn=%u, num=%u, sta_idx=%d, tid=%d, status=%d\n",
                    agg_reord_msg->sn, agg_reord_msg->num, agg_reord_msg->sta_idx, 
                    agg_reord_msg->tid, agg_reord_msg->status);

            RE_THOUGH_LIST:
            /*1. get the skb according sn*/
            if (!list_empty(&bl_hw->reorder_list[agg_reord_msg->sta_idx*NX_NB_TID_PER_STA + agg_reord_msg->tid])) 
            {
                list_for_each_entry_safe(reord_pkt, reord_pkt_tmp, &bl_hw->reorder_list[agg_reord_msg->sta_idx*NX_NB_TID_PER_STA + agg_reord_msg->tid], list) 
                {
                    BL_DBG("reord_pkt->sn=%u\n", reord_pkt->sn);
                    
                    if ((reord_pkt->sn == agg_reord_msg->sn) && (count < agg_reord_msg->num)) {
                        if (!found)
                            found = true;

                        BL_DBG("found pkt: sn=%u, next_sn=%u, count(%u) of num(%u)\n", 
                                agg_reord_msg->sn, 
                                (agg_reord_msg->sn + 1) % AGG_REORD_MSG_MAX_NUM, 
                                count, agg_reord_msg->num);
                                
                        /*2. modify the status in skb according agg_reodr_msg->status*/
                        memcpy(reord_pkt->skb->data, &(agg_reord_msg->status), 1);
                        /*3. call bl_rxdataind to handle the skb*/
                        bl_rxdataind(bl_hw, reord_pkt->skb);
                        /*4. delete this agg reodr pkt node*/
                        list_del(&reord_pkt->list);
                        kmem_cache_free(bl_hw->agg_reodr_pkt_cache, reord_pkt);
                        
                        /*5. find next pkt, if sn == 4095, next sn will change to 0*/
                        agg_reord_msg->sn = 
                               (agg_reord_msg->sn + 1) % AGG_REORD_MSG_MAX_NUM;
                        count++;
                    }
                        
                    BL_DBG("count=%u, agg_reord_msg->num=%u\n", 
                           count, agg_reord_msg->num);

                    if ((agg_reord_msg->sn == 0) && (count < agg_reord_msg->num)) {
                        BL_DBG("sn=0, re-though the list\n");
                        goto RE_THOUGH_LIST;
                    }

                    if (count == agg_reord_msg->num) {
                        count = 0;
                        agg_reord_msg->sn = 0;
                        break;
                    }
                }
                
                if(!found)
                    BL_DBG("Not found the packet, sn=%u\n", agg_reord_msg->sn);
            } 
            else 
            {
                BL_DBG("Oh! give me reodr msg, but no packet need to handle?\n");
            }
            #endif  //#ifndef BL_RX_REORDER
            
            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_DBG:
            BL_DBG("info: --------------------------------->>>Rx:DBG MSG packet\n");
            
            *(skb->data+pld_len) = '\0';

            //If customer does not want these fw printing, disable it by defining marco in Makefile
            #ifndef CONFIG_DISABLE_FWLOG
            if (strstr((char *)(skb->data), "ASSERT REC")) 
            {
                BL_DBG("fwlog: %s", (char *)(skb->data));
            }
            else 
            {
                printk("fwlog: %s", (char *)(skb->data));
            }
            #endif

            bl_error_ind(bl_hw);
            dev_kfree_skb_any(skb);
            break;    

        case BL_TYPE_TX_STOP:
            BL_DBG("info: --------------------------------->>>Rx:TX_STOP packet\n");
            bl_hw->recovery_flag = true;
            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_TX_RESUME:
            BL_DBG("info: ------------------------------->>>Rx:TX_RESUME packet\n");
            bl_hw->recovery_flag = false;
            dev_kfree_skb_any(skb);
            break;

        case BL_TYPE_BUS_LOOPBACK:
            BL_DBG("info: ------------------------------>>>Rx:Loopback packet\n\r");
            
            #ifdef BL_BUS_LOOPBACK
            //not reach here. FW use one buffer to attach, so free in bl_upload_hdl when check duplicate reserved filed
            do
            {
                extern ktime_t g_last_rxtime;
                static u32 rxcnt = 0, last_rxcnt = 0;
                static ktime_t rxtime;
                s64 delta = 0;
                uint32_t data_rate;

                rxcnt++;
                rxtime = ktime_get();

                delta = ktime_ms_delta(rxtime, g_last_rxtime);
                if (delta > 1000) {
                    data_rate = ((rxcnt-last_rxcnt)*skb->len)*8/(u32)delta;

                    printk("%s, data_rate:%d(kbps), rxcnt:%d, last_rxcnt:%d, delta:%lld, skblen:%d\n", 
                           __func__, data_rate, rxcnt, last_rxcnt, delta, skb->len);
                           
                    g_last_rxtime = rxtime;
                    last_rxcnt = rxcnt;
                }
            }while(0);
            #endif

            dev_kfree_skb_any(skb);
            break;

        default:
            BL_DBG("info: ------------------>>>Rx:UNKNOWN[%#x] packet\n", pkt_type);
            dev_kfree_skb_any(skb);
            break;
    }

    return 0;
}

#ifdef CONFIG_BL_SDIO
#ifdef BL_TRX_PROFILE
static void bl_rec_irq_status(struct bl_hw *bl_hw, u8 sdio_ireg)
{
    ktime_t curr_t = ktime_get();
    ktime_t temp_interval = 0;

    if (!bl_hw->trx_profile.enable)
        return;

    if (sdio_ireg & UP_LD_HOST_INT_STATUS) {
        bl_hw->trx_profile.rx_irq_cnt++;
        temp_interval = curr_t - bl_hw->trx_profile.rx_irq_prev_ts;
        if (bl_hw->trx_profile.rx_irq_prev_ts) {
            bl_hw->trx_profile.rx_irq_max_interval = 
                temp_interval > bl_hw->trx_profile.rx_irq_max_interval ? \
                temp_interval : bl_hw->trx_profile.rx_irq_max_interval;
        }
        bl_hw->trx_profile.rx_irq_prev_ts = curr_t;
    } else {
        bl_hw->trx_profile.tx_irq_cnt++;
        temp_interval = curr_t - bl_hw->trx_profile.tx_irq_prev_ts;
        if (bl_hw->trx_profile.tx_irq_prev_ts) {
            bl_hw->trx_profile.tx_irq_max_interval = 
                temp_interval > bl_hw->trx_profile.tx_irq_max_interval ? \
                temp_interval : bl_hw->trx_profile.tx_irq_max_interval;
        }
        bl_hw->trx_profile.tx_irq_prev_ts = curr_t;
    }
}
#endif

void bl_get_interrupt_status(struct bl_hw *bl_hw, u8 claim)
{
    u8 sdio_ireg;
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    u8 rd_bitmap_l = 0;
    u8 rd_bitmap_u = 0;
    u8 wr_bitmap_l = 0;
    u8 wr_bitmap_u = 0;
    u32 bitmap;
#ifdef BL_INT_WRITE_CLEAR
    u8 wclr_sdio_ireg;
#endif
    unsigned long flags;
    static u32  up_cnt=0;
    static u32  down_cnt=0;

    if (claim) {
        if (bl_read_data_sync(bl_hw, card->mp_regs, card->reg->max_mp_regs, 
                              REG_PORT)) 
        {
            printk("[WD]read mp_regs failed\n");
            return;
        }
    } else {
        if (bl_read_data_sync_claim0(bl_hw, card->mp_regs, 
                                    card->reg->max_mp_regs, REG_PORT)) 
        {
            printk("read mp_regs failed\n");
            return;
        }
    }

    sdio_ireg = card->mp_regs[card->reg->host_int_status_reg];
    
#ifdef BL_INT_WRITE_CLEAR
    wclr_sdio_ireg = (~sdio_ireg) & 0x3f;
    
    if (sdio_ireg > 0) {
        if (claim) {
            if(bl_write_reg(bl_hw, card->reg->host_int_status_reg,
                            wclr_sdio_ireg)) 
            {
                printk("[WD]write int_status_reg failed\n");
            }
        } 
        else {
            if(bl_write_reg_claim0(bl_hw, card->reg->host_int_status_reg, 
                                   wclr_sdio_ireg)) 
            {
                printk("[WD]write int_status_reg failed\n");
            }
        }
    }
#endif

    if (claim) {
        //printk("org sido_ireg = 0x%x manual polling\n", sdio_ireg);
        
        if (!card->mp_rd_bitmap && !(sdio_ireg & UP_LD_HOST_INT_STATUS) && 
            !bl_hw->data_recv)
        {
            rd_bitmap_l = card->mp_regs[card->reg->rd_bitmap_l];
            rd_bitmap_u = card->mp_regs[card->reg->rd_bitmap_u];
            bitmap = rd_bitmap_l;
            bitmap |= rd_bitmap_u << 8;
            
            if (bitmap) {
                up_cnt++;
                
                BL_TRACE(TRACE_RX, "[WD]U %d, bitmap=0x%x, mp_rb=0x%x, cur_rp=%d\n", 
                        up_cnt, bitmap, card->mp_rd_bitmap, card->curr_rd_port);
                        
                sdio_ireg |=UP_LD_HOST_INT_STATUS;
            }

        }

        if (!(card->mp_wr_bitmap & DATA_PORT_MSK) && 
            !(sdio_ireg & DN_LD_HOST_INT_STATUS) && !bl_hw->data_sent)
        {
            wr_bitmap_l = card->mp_regs[card->reg->wr_bitmap_l];
            wr_bitmap_u = card->mp_regs[card->reg->wr_bitmap_u];
            bitmap = wr_bitmap_l;
            bitmap |= wr_bitmap_u << 8;
            
            if (bitmap & DATA_PORT_MSK) {
                down_cnt ++;
                
                BL_TRACE(TRACE_TX, "[WD]D %d, bitmap=0x%x, mp_wb=0x%x, cur_wp=%d\n", 
                      down_cnt, bitmap, card->mp_wr_bitmap, card->curr_wr_port);
                      
                sdio_ireg |=DN_LD_HOST_INT_STATUS;
            }
        }
    }

    BL_DBG_MSG("bl_get_interrupt_status, sdio_ireg=0x%x, reg_wrbm-wrbm:0x%x 0x%x, reg_rdbm-rdbm:0x%x 0x%x\n", 
           sdio_ireg, 
           (card->mp_regs[card->reg->wr_bitmap_u]<<8)|card->mp_regs[card->reg->wr_bitmap_l],
           card->mp_wr_bitmap,
           (card->mp_regs[card->reg->rd_bitmap_u]<<8)|card->mp_regs[card->reg->rd_bitmap_l],
           card->mp_rd_bitmap);

    if (sdio_ireg) {
        spin_lock_irqsave(&bl_hw->int_lock, flags);
        bl_hw->int_status |= sdio_ireg;
        
#ifdef BL_TRX_PROFILE
        bl_rec_irq_status(bl_hw, sdio_ireg);
#endif

        spin_unlock_irqrestore(&bl_hw->int_lock, flags);
    }
}

/**
 * bl_irq_hdlr - IRQ handler
 *
 * Handler registerd by the platform driver
 */
void bl_sdio_irq_hdlr(struct sdio_func *func)
{
    struct bl_hw *bl_hw;
    
    BL_DBG(BL_FN_ENTRY_STR);
    
    bl_hw = sdio_get_drvdata(func);
    if(!bl_hw) {
        BL_DBG("get bl_hw failed!\n");
        
        return;
    }

    bl_get_interrupt_status(bl_hw, 0);
    bl_hw->int_cnt++;
    
    bl_main_process(bl_hw);
}
#endif

#ifdef CONFIG_BL_BTSDU
static int bl_pending_hci_msg_hdl(struct bl_hw *bl_hw)
{
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    struct btsdio_data *data = &bl_hw->btsdio_data;
    struct sk_buff *hci_skb;
    int err=0;
    u32 port;
    struct inf_hdr *sdu_hdr = NULL;
    int ret = 0;
    static u8 hci_ack_cnt = 0;
    u8 ack_cnt = 0;
    static unsigned int snd_idx = 0;

    do {
        u8 wr_bitmap_l = 0;

        spin_lock(&data->hci_cmd_lock);
        hci_skb = skb_dequeue(&data->txq);
        spin_unlock(&data->hci_cmd_lock);
        
        if (!hci_skb)
            break;

        if (hci_ack_cnt == 0) {
            ret = bl_read_reg(bl_hw, CARD_FW_STATUS0_REG, &ack_cnt);
            if (ret)
            {
                BL_DBG_MSG("%s, read scratch reg fail\n", __func__);
        
                return 0;
            }
        
            if (ack_cnt == 0)
            {
                BL_DBG_MSG("%s, prev not acked\n", __func__);
        
                return 0;
            }
        
            hci_ack_cnt = ack_cnt;
        }
   
        bl_read_reg(bl_hw, card->reg->wr_bitmap_l, &wr_bitmap_l);
        if ((wr_bitmap_l & CTRL_PORT_MASK) && list_empty(&bl_hw->cmd_mgr.cmds))
        {
            port = CTRL_PORT;
        }
        else 
        {
            ret = bl_get_wr_port(bl_hw, &port);
            if (ret)
            {
                BL_DBG_MSG("%s no available port for hci tx\n", __func__);

                spin_lock(&data->hci_cmd_lock);
                skb_queue_head(&data->txq, hci_skb);
                spin_unlock(&data->hci_cmd_lock);
                
                break;
            }
        }

        skb_push(hci_skb, sizeof(struct inf_hdr));
        sdu_hdr = (struct inf_hdr *)hci_skb->data;
        sdu_hdr->type = BL_TYPE_HCI;
        sdu_hdr->len = hci_skb->len - sizeof(struct inf_hdr);
        sdu_hdr->reserved = snd_idx++;
        sdu_hdr->queue_idx = 0;

        BL_DBG_MSG("%s, len:%u, seq:%u\n", __func__, hci_skb->len, sdu_hdr->reserved);

        bl_write_reg(bl_hw, CARD_FW_STATUS0_REG, 0);
        err = bl_write_data_sync(bl_hw, (u8 *)(hci_skb->data), 
                                 ((hci_skb->len + BL_SDIO_BLOCK_SIZE-1)/BL_SDIO_BLOCK_SIZE) *BL_SDIO_BLOCK_SIZE, 
                                 card->io_port + port);
        
        if (err < 0) {
            printk("%s, %s hci_sdu send fail, err:%d\r\n", 
                   __func__, data->hdev->name, err);
            
            data->hdev->stat.err_tx++;

            spin_lock(&data->hci_cmd_lock);
            skb_queue_head(&data->txq, hci_skb);
            spin_unlock(&data->hci_cmd_lock);
            
            break;
        } else {
            hci_ack_cnt = 0;
            
            data->hdev->stat.byte_tx += hci_skb->len;
            dev_kfree_skb_any(hci_skb);
            
            //break to only allow to send one ble pkt in one loop, control the speed because it is sharing wifi's sdio port.
            break;
        }
    } while(true);

    return 0;
}
#endif

static int bl_pending_msg_hdl(struct bl_hw *bl_hw)
{    
    struct bl_cmd *cur = NULL, *tmp;
    struct bl_cmd *hostid = NULL;
    int ret = 0;
#ifdef CONFIG_BL_SDIO
    u32 count = 0;
    u8 wr_bitmap_l = 0;
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
#endif

    spin_lock_bh(&bl_hw->cmd_mgr.lock);

    BL_DBG_MSG("%s, bl_hw->cmd_sent:%d, msga2e_hostid:0x%p\n", 
               __func__, bl_hw->cmd_sent, bl_hw->ipc_env->msga2e_hostid);

    hostid = (struct bl_cmd *)(bl_hw->ipc_env->msga2e_hostid);
    if (hostid == NULL) {
        spin_unlock_bh(&bl_hw->cmd_mgr.lock);

        return 0;
    }

    BL_DBG_MSG("%s, msga2e_hostid flags:0x%x\n", __func__, hostid->flags);
    
    list_for_each_entry_safe(cur, tmp, &bl_hw->cmd_mgr.cmds, list) {
        ASSERT_WARN(cur != NULL);
        ASSERT_WARN(hostid != NULL);

        //cmd_dump(cur);
        BL_DBG_MSG("%s, cur:0x%p, cmdid:0x%x, cur->tkn=%d, flags:0x%x, hostid->tkn=%d, queue_sz=%d, max_queue_sz=%d, dest id:0x%x, src id:0x%x\n",
                   __func__, cur, cur->id, cur->tkn, cur->flags, hostid->tkn, 
                   bl_hw->cmd_mgr.queue_sz, bl_hw->cmd_mgr.max_queue_sz, 
                   cur->a2e_msg->dest_id, cur->a2e_msg->src_id);
                   
        if (cur->tkn == hostid->tkn &&
            (cur->flags&BL_CMD_FLAG_DNLD_COMPLETE) == 0 && 
            (cur->flags&BL_CMD_FLAG_DNLDING) == 0)
        {
            static unsigned int snd_idx = 0;

            if (BL_TYPE_MSG == *((u16 *)(cur->a2e_msg)+1)) {
                if (softmac_task_ids_check(cur->a2e_msg->dest_id))
                {
                    BL_DBG_MSG("forward to softmac cmd tkn[%d], flags:0x%04x, \
                               cmd:0x%x-%-24s - reqcfm(0x%x-%-s)\n",
                               cur->tkn, cur->flags,  cur->id, BL_ID2STR(cur->id),
                               cur->reqid, 
                               cur->reqid != (lmac_msg_id_t)-1?BL_ID2STR(cur->reqid):"none");

                    softmac_handle_kmsg_from_drv(bl_hw, cur->a2e_msg);
                    kfree(cur->a2e_msg);
                    
                    if(cur->flags |BL_CMD_FLAG_WAIT_PUSH) {
                        cur->flags &= ~BL_CMD_FLAG_WAIT_PUSH;
                    }
                    cur->flags |= BL_CMD_FLAG_DNLD_COMPLETE;

                    break;
                }
            }

            spin_unlock_bh(&bl_hw->cmd_mgr.lock);

            snd_idx++;
            *((u16 *)(cur->a2e_msg)+3) = (snd_idx&0xffff);

            BL_DBG_MSG("%s, len:%u, seq:%u\n", __func__, 
                       ((sizeof(struct lmac_msg) + cur->a2e_msg->param_len + 3)/4) *4,
                       snd_idx&0xffff);

#if defined CONFIG_BL_SDIO
            bl_read_reg(bl_hw, card->reg->wr_bitmap_l, &wr_bitmap_l);
            while ((!(wr_bitmap_l & CTRL_PORT_MASK)) && count < 200) {
                msleep(5);
                count++;
                bl_read_reg(bl_hw, card->reg->wr_bitmap_l, &wr_bitmap_l);
                
                BL_DBG("wait for cmd port ready 0x%x!\n", wr_bitmap_l);
            };

            if (count >= 200) {
                BL_DBG_MSG("wait for cmd port timeout! 0x%x\n", wr_bitmap_l);

                snd_idx--;
                
                spin_lock_bh(&bl_hw->cmd_mgr.lock);

                break;
            }

            if (cur->flags | BL_CMD_FLAG_WAIT_PUSH) {
                cur->flags &= ~BL_CMD_FLAG_WAIT_PUSH;
            }

            ret = bl_write_data_sync(bl_hw, (u8 *)(cur->a2e_msg),
                               ((sizeof(struct lmac_msg) + cur->a2e_msg->param_len + 3)/4) *4, 
                               card->io_port + CTRL_PORT);

            if (ret == 0) {
                kfree(cur->a2e_msg);
                cur->flags |= BL_CMD_FLAG_DNLD_COMPLETE;

                bl_hw->cmd_sent = false;
            } else {
                snd_idx--;
                cur->flags |= BL_CMD_FLAG_WAIT_PUSH;
                
                printk("%s, sdio tx ret:%d\n", __func__, ret);
            }
#elif defined CONFIG_BL_USB
            if (cur->flags |BL_CMD_FLAG_WAIT_PUSH)
                cur->flags &= ~BL_CMD_FLAG_WAIT_PUSH;

            if (bl_hw->mod_params->mp_mode) 
                ret = bl_usb_host_to_card(bl_hw, (u8 *)(cur->a2e_msg), 
                                    ((sizeof(struct lmac_msg) + cur->a2e_msg->param_len + 3)/4) *4, 
                                    BL_USB_EP_OUT, BL_USB_EP_MSG, NULL);
            else
                ret = bl_usb_host_to_card(bl_hw, (u8 *)(cur->a2e_msg), 
                                    ((sizeof(struct lmac_msg) + cur->a2e_msg->param_len + 3)/4) *4, 
                                    #if defined(CONFIG_CMD_USB_EP)
                                    BL_USB_EP_CMD_OUT,
                                    #else
                                    BL_USB_EP_OUT,
                                    #endif
                                    BL_USB_EP_MSG, NULL);

            if (ret == 0) {
                cur->flags |= BL_CMD_FLAG_DNLDING;

                bl_hw->cmd_sent = false;
            } else {
                snd_idx--;
                cur->flags |= BL_CMD_FLAG_WAIT_PUSH;
                
                printk("%s, usb tx ret:%d\n", __func__, ret);
            }
#endif

            spin_lock_bh(&bl_hw->cmd_mgr.lock);

            BL_DBG_MSG("download cmd tkn[%d], flags:0x%04x, cmd:0x%x-%-24s - reqcfm(0x%x-%-s)\n",
                   cur->tkn, cur->flags,  cur->id, BL_ID2STR(cur->id),
                   cur->reqid, 
                   cur->reqid!=(lmac_msg_id_t)-1?BL_ID2STR(cur->reqid):"none");

            break;
        } 
    }

    BL_DBG("%s exit, bl_hw->cmd_sent:%d\n", __func__, bl_hw->cmd_sent);
    
    spin_unlock_bh(&bl_hw->cmd_mgr.lock);

    return 0;
}


#ifdef CONFIG_BL_SDIO
#ifdef BL_TRX_PROFILE
static void bl_rec_upload_en(struct bl_hw *bl_hw)
{
    bl_hw->trx_profile.enable = true;
}

static void bl_rec_upload_ts(struct bl_hw *bl_hw, u8 tag)
{
    ktime_t prev_ts;
    static ktime_t t10, t31, t32, t43, t54;
    static bool twice = false;

    if (bl_hw->trx_profile.enable == 0)
        return;

    prev_ts = bl_hw->trx_profile.rx_hdl_ts[tag];
    bl_hw->trx_profile.rx_hdl_ts[tag] = ktime_get();
    //BL_DBG("T:[%d]%lld\r\n", tag, ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[tag]));
    //printk("T:%d\n", tag);

    switch (tag) {
        case RX_HDL_START:
            bl_hw->trx_profile.rx_hdl_ts[RX_HDL_IRQ_BOTTOM] =  
                              bl_hw->trx_profile.rx_hdl_ts[RX_HDL_START] - 
                              bl_hw->trx_profile.rx_irq_prev_ts;
            bl_hw->trx_profile.rx_hdl_ts[RX_HDL_PERIOD] = 
                           bl_hw->trx_profile.rx_hdl_ts[RX_HDL_START] - prev_ts;
            bl_hw->trx_profile.rx_hdl_ts[RX_HDL_GAP] = 
                           bl_hw->trx_profile.rx_hdl_ts[RX_HDL_START] - 
                           bl_hw->trx_profile.rx_hdl_ts[RX_HDL_END];
            break;
        case RX_HDL_ALLOC:
            t10 = ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_ALLOC] - 
                              bl_hw->trx_profile.rx_hdl_ts[RX_HDL_START]);
            twice = false;
            break;
        case RX_HDL_ALLOC2:
            bl_hw->trx_profile.rx_hdl_ts[RX_HDL_TWICE_DIFF] = 
                               bl_hw->trx_profile.rx_hdl_ts[RX_HDL_ALLOC2] - 
                               bl_hw->trx_profile.rx_hdl_ts[RX_HDL_ALLOC];
            twice = true;
            break;
        case RX_HDL_READ:
            if(twice) {
                t32 = ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_READ] - 
                                  bl_hw->trx_profile.rx_hdl_ts[RX_HDL_ALLOC2]);
            } else {
                t31 = ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_READ] - 
                                  bl_hw->trx_profile.rx_hdl_ts[RX_HDL_ALLOC]);
            }
            break;
        case RX_HDL_QUEUE:
            t43 = ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_QUEUE] - 
                              bl_hw->trx_profile.rx_hdl_ts[RX_HDL_READ]);
            break;

        case RX_HDL_END:
            t54 = ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_END] -
                              bl_hw->trx_profile.rx_hdl_ts[RX_HDL_QUEUE]);
            bl_hw->trx_profile.rx_hdl_ts[RX_HDL_DUR_ALL] = 
                                   bl_hw->trx_profile.rx_hdl_ts[RX_HDL_END] - 
                                   bl_hw->trx_profile.rx_hdl_ts[RX_HDL_START];
                                   
            printk("period:%lld,dur_all:%lld,rx_gap=%lld,twice_diff=%lld,irq_delay=%lld;t10=%lld,t31=%lld,t32=%lld,t43=%lld,t54=%lld,port_avg=%d\n", 
                ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_PERIOD]), 
                ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_DUR_ALL]), 
                ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_GAP]), 
                ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_TWICE_DIFF]), 
                ktime_to_us(bl_hw->trx_profile.rx_hdl_ts[RX_HDL_IRQ_BOTTOM]), 
                t10, t31, t32, t43, t54,
                bl_hw->trx_profile.rx_port_avg);
                
            t10 = t31 = t32 = t43 = t54 = 0;
            break;

        default:
            break;
    }
}
#endif

static int bl_upload_hdl(struct bl_hw *bl_hw)
{
    u8 rd_bitmap_l = 0;
    u8 rd_bitmap_u = 0;
    u32 len_reg_l = 0;
    u32 len_reg_u = 0;
    u32 bitmap;
    u32 rx_len;
    u32 port = CTRL_PORT;
    int ret = 0;
    struct sk_buff *skb;
    u8 cr;
    u32 pind;
    u8 *curr_ptr;
    u32 pkt_len;
    u32 pkt_type;
    u32 hw_idx;
    u32 reserved;
    u32 cmd53_port = 0;
    int avail_port_num = 0;
    u32 rd_bitmap = 0;
    bool rd_twice = false;
    bool rd_again = false;
    bool roll_over = false;
    //struct timespec ts;
    static u16_l last_msg_cnt = 0xFFFF;
    static u16_l last_data_cnt = 0xFFFF;
    //ktime_t cmd53_start_ts, cmd53_end_ts;
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    int cnt = 0;
    unsigned long flags;
#ifdef CONFIG_BL_BTSDU
    struct btsdio_data *btsdio_data = &bl_hw->btsdio_data;
    uint32_t btsdio_rx_len = 0;
#endif

    BL_DBG(BL_FN_ENTRY_STR);

    bl_hw->mpa_rx_data.pkt_cnt = 0;

    rd_bitmap_l = card->mp_regs[card->reg->rd_bitmap_l];
    rd_bitmap_u = card->mp_regs[card->reg->rd_bitmap_u];

    bitmap = rd_bitmap_l;
    bitmap |= rd_bitmap_u << 8;

    if (bitmap == 0) {
        BL_DBG("WARN: empty upld_rdy int\r\n");
    }

    /* improve the robust, hard to got this case */
    if ((bitmap & DATA_PORT_MSK) && 
        ((bitmap & DATA_PORT_MSK) != DATA_PORT_MSK) && 
        bitmap == card->prev_rd_bitmap) 
    {
        BL_DBG("WARN: skip duplicate upld_rdy bitmap=0x%x\r\n", bitmap);
        
        return 0;
    }

    card->mp_rd_bitmap = bitmap;
    rd_bitmap = bitmap;

    BL_DBG_MSG("upld_hdl: rd_bitmap=0x%x prev=0x%x, lost_int_flag=%d, ports=0x%x\n", 
           card->mp_rd_bitmap, card->prev_rd_bitmap, bl_hw->lost_int_flag, 
           bl_hw->mpa_rx_data.ports);
           
    card->prev_rd_bitmap = bitmap;

    rd_bitmap &= DATA_PORT_MSK;
    while (rd_bitmap) {
        rd_bitmap = rd_bitmap & (rd_bitmap-1);
        avail_port_num++;
    }
    
    BL_TRACE(TRACE_RX, "rd_bitmap=0x%x, available rd port nums=%d,-- ", 
             card->mp_rd_bitmap, avail_port_num);

#ifdef BL_TRX_PROFILE
    bl_hw->trx_profile.rx_port_total += avail_port_num;
    bl_hw->trx_profile.rx_port_avg = bl_hw->trx_profile.rx_port_total / \
       (bl_hw->trx_profile.rx_irq_cnt == 0 ? 1 : bl_hw->trx_profile.rx_irq_cnt);

    bl_rec_upload_ts(bl_hw, RX_HDL_START);
#endif

TWICE:

    while(true) {
        ret = bl_get_rd_port(bl_hw, &port);
        if (ret) {
            BL_DBG("info: no more rd_port available\n");
            
            break;
        }

        len_reg_l = card->mp_regs[card->reg->rd_len_p0_l + (port << 1)];
        len_reg_u = card->mp_regs[card->reg->rd_len_p0_u + (port << 1)];
        rx_len = (len_reg_u << 8) + len_reg_l;

        BL_TRACE(TRACE_RX, "rd_port: port=0x%x, rx_len=%d\n", port, rx_len);

        if (rx_len < sizeof(struct inf_hdr) || rx_len > 4096) {
            printk("%s, Invaild rx_len %d, pkt_cnt:%d\n",
                   __func__, rx_len, bl_hw->mpa_rx_data.pkt_cnt);
            
            bl_hw->data_recv = false;
            
            if(bl_hw->mpa_rx_data.pkt_cnt != 0) {
                break;
            } else {
                printk("%s, return err, %d\n", __func__, __LINE__);
                
                return -1;
            }
        }

        rx_len = ((rx_len + BL_SDIO_BLOCK_SIZE - 1)/BL_SDIO_BLOCK_SIZE) * BL_SDIO_BLOCK_SIZE;

        BL_DBG("after round: rx_len=%d, port=%d\n", rx_len, port);

        skb = __netdev_alloc_skb(NULL, rx_len, GFP_KERNEL);
        if (!skb) {
            printk("%s: failed to alloc skb, len:%d, pkt_cnt:%d\n",
                   __func__, rx_len, bl_hw->mpa_rx_data.pkt_cnt);
            
            bl_hw->data_recv = false;
            
            if(bl_hw->mpa_rx_data.pkt_cnt != 0) {
                break;
            } else {
                printk("%s, return err, %d\n", __func__, __LINE__);
                
                return -1;
            }
        } else {
            BL_DBG("%s alloc skb: 0x%p\n", __func__, skb);
        }

        if (port == CTRL_PORT) {
            /*port0 is not aggr port, ignore */
            BL_DBG("port0: cmd53_port=0x%08x, rx_len=%d\n", 
                   card->io_port + port, rx_len);

            //cmd53_start_ts = ktime_get();
            ret = bl_read_data_sync(bl_hw, skb->data, rx_len, card->io_port + port);
            if (ret) {
                do {
                    ret = bl_read_data_sync(bl_hw, skb->data, rx_len, 
                                            card->io_port + port);
                    cnt ++;
                } while (ret && cnt < BL_SDIO_RD_WR_RETRY);

                if (cnt >= BL_SDIO_RD_WR_RETRY) {
                    printk("bl_upload_hdl port[%d] read 10 times\n", port);
                    
                    dev_kfree_skb_any(skb);
                    
                    return -1;
                }
                
                printk("bl_read_data_sync retry=%d, ret=%d\n", cnt, ret);
            }
            
            //cmd53_end_ts = ktime_get();
            //printk("CMD  bitmap=0x%x,again=%d,avail_port=%d,pkt_cnt=%d,buf_len=%d,cmd53_port=0x%04x,dur=%lld\n", bitmap, rd_again, avail_port_num, 1,
            //        rx_len, card->io_port + port, ktime_to_us(cmd53_end_ts - cmd53_start_ts));

            pkt_len  = le16_to_cpu(*(__le16 *) (skb->data + 0));
            pkt_type = le16_to_cpu(*(__le16 *) (skb->data + 2));
            hw_idx   = le16_to_cpu(*(__le16 *) (skb->data + 4));
            reserved = le16_to_cpu(*(__le16 *) (skb->data + 6));

            BL_DBG_MSG("%s %u, rx_len:%u, pkt_len:%u\r\n", 
                       __func__, __LINE__, rx_len, pkt_len);
            if (pkt_len > rx_len) {
                #if 0
                bl_dump(skb->data, rx_len);
                BL_DBG_MSG("dump mp_rx_data:\n");
                bl_dump((uint8_t *)&bl_hw->mpa_rx_data, sizeof(bl_hw->mpa_rx_data));
                ASSERT_WARN(0);
                #endif

                dev_kfree_skb_any(skb);                
                continue;
            }
                
            #ifdef CONFIG_BL_BTSDU
            if (pkt_type == BL_TYPE_HCI) {
                BL_DBG_MSG("%s, hci, pkt_len:%d\n", __func__, pkt_len);
                
                skb_put(skb, pkt_len + sizeof(struct inf_hdr));

                #if 0
                //delay to process in rx_work queue if there is.
                BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
                skb_queue_tail(&bl_hw->rx_pkt_list, skb);
                BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
                
                if(bl_hw->rx_work_flag) {
                    bl_queue_rx_work(bl_hw);
                }
                #else
                skb_pull(skb, sizeof(struct inf_hdr));
                
                btsdio_rx_len = *(uint32_t *)skb->data;
                
                BL_DBG_MSG("btsdio rx len over cmd, data:%d %d, %02x %02x %02x %02x %02x %02x %02x %02x\r\n", 
                       skb->len, btsdio_rx_len, 
                       skb->data[4], skb->data[5], skb->data[6], skb->data[7], 
                       skb->data[8], skb->data[9], skb->data[10], skb->data[11]);
                       
                skb_pull(skb, 4);  //BTSDU_MSG_LEN_FIELD takes 4 bytes
                pskb_trim(skb, btsdio_rx_len<skb->len?btsdio_rx_len:skb->len);
                
                btsdio_rx_packet(btsdio_data, skb, btsdio_rx_len);
                #endif
                
                continue;
            }
            #endif

            if (!bl_hw->mod_params->mp_mode) {
                if (last_msg_cnt == (u16_l)reserved){
                    printk("duplicate msg, drop it last msg cnt %d current cnt %d\n",
                           last_msg_cnt,reserved);
                           
                    dev_kfree_skb_any(skb);
                    
                    continue;
                }
                last_msg_cnt = (u16_l)reserved;
            }

            skb_put(skb, rx_len);

            //bl_dump(skb->data, skb->len);

            //delay to process in rx_work queue if there is.
            BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
            skb_queue_tail(&bl_hw->rx_pkt_list, skb);
            BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
            
            if (bl_hw->rx_work_flag) {
                bl_queue_rx_work(bl_hw);
            }
            
            continue;
        }

        //printk("start to record multi port: port=0x%x\n", port);

        bl_hw->mpa_rx_data.buf_len += rx_len;
        
        if (!bl_hw->mpa_rx_data.pkt_cnt) {
            bl_hw->mpa_rx_data.start_port = port;
            
            if(bl_hw->mpa_rx_data.start_port + MIN(avail_port_num, MAX_AGG_BUF)
                > (MAX_PORT_NUM - 1))
            {
                roll_over = true;
            }
        }

        if (bl_hw->mpa_rx_data.start_port <= port) {
            bl_hw->mpa_rx_data.ports |= (1 << bl_hw->mpa_rx_data.pkt_cnt);
        } else {
            bl_hw->mpa_rx_data.ports |= 
                   (1 << (bl_hw->mpa_rx_data.pkt_cnt + NA_DATA_PORT_NUM));
        }

        BL_TRACE(TRACE_RX, "start to record multi port: port=0x%x, start_port=0x%x, data_ports=0x%x\n", 
                port, bl_hw->mpa_rx_data.start_port, bl_hw->mpa_rx_data.ports);
                
        bl_hw->mpa_rx_data.buf_arr[bl_hw->mpa_rx_data.pkt_cnt] = skb; 
        bl_hw->mpa_rx_data.len_arr[bl_hw->mpa_rx_data.pkt_cnt] = rx_len; 
        bl_hw->mpa_rx_data.mp_rd_port[bl_hw->mpa_rx_data.pkt_cnt] = port; 
        bl_hw->mpa_rx_data.pkt_cnt++;
        avail_port_num--;
        
#if 0
        /* single port read */
        if(bl_hw->mpa_rx_data.pkt_cnt == 1 && (avail_port_num - bl_hw->mpa_rx_data.pkt_cnt)) {
            rd_twice = true;
            break;
        }
#else
        /* multi agg port read */
        if ((bl_hw->mpa_rx_data.pkt_cnt == MAX_AGG_BUF) ||
            (roll_over && bl_hw->mpa_rx_data.pkt_cnt ==
                (MAX_AGG_BUF - NA_DATA_PORT_NUM)))
        {
            roll_over = false;
            
            if (avail_port_num > 0) {
                rd_twice = true;
                //bl_rec_upload_en(bl_hw);
            }
            
            BL_TRACE(TRACE_RX, "rd_twice=%d,pkt_cnts=%d,ports=0x%x,left=%d\n", 
                     rd_twice, bl_hw->mpa_rx_data.pkt_cnt, 
                     bl_hw->mpa_rx_data.ports, 
                     avail_port_num - bl_hw->mpa_rx_data.pkt_cnt);
            break;
        }
#endif
    }

#ifdef BL_TRX_PROFILE
    if (rd_again)
        bl_rec_upload_ts(bl_hw, RX_HDL_ALLOC2);
    else
        bl_rec_upload_ts(bl_hw, RX_HDL_ALLOC);
#endif

    if (bl_hw->mpa_rx_data.pkt_cnt != 0) {
        memset(bl_hw->mpa_rx_data.buf, 0, BL_RX_DATA_BUF_SIZE_16K);

        /*recv packet*/
        cmd53_port = (card->io_port | BL_SDIO_MPA_ADDR_BASE |
                     (bl_hw->mpa_rx_data.ports << 4)) + 
                     bl_hw->mpa_rx_data.start_port;

        BL_TRACE(TRACE_RX, "mpa_buf_len=%d, cmd53_port=0x%04x, mpa_rx_data.buf=%p\n", 
                 bl_hw->mpa_rx_data.buf_len, cmd53_port, bl_hw->mpa_rx_data.buf);

        //cmd53_start_ts = ktime_get();
        ret = bl_read_data_sync(bl_hw, bl_hw->mpa_rx_data.buf, 
                                bl_hw->mpa_rx_data.buf_len, cmd53_port);
        if (ret) {
            cnt = 0;
            
            do {
                BL_TRACE(TRACE_RX, "bl_read_data_sync retry %d\r\n", cnt);
                ret = bl_read_data_sync(bl_hw, bl_hw->mpa_rx_data.buf, 
                                        bl_hw->mpa_rx_data.buf_len, cmd53_port);
                cnt++;
            } while (ret && cnt < BL_SDIO_RD_WR_RETRY);

            if (cnt >= BL_SDIO_RD_WR_RETRY) {
                printk("bl_read_data_sync failed, ret=%d\n", ret);
            }
            
            printk("bl_read_data_sync retry=%d, ret=%d\n", cnt, ret);
        }
        
        //cmd53_end_ts = ktime_get();

        //printk("DATA bitmap=0x%x,again=%d,avail_port=%d,pkt_cnt=%d,buf_len=%d,cmd53_port=0x%04x,dur=%lld\n", 
        //        bitmap, rd_again, avail_port_num, bl_hw->mpa_rx_data.pkt_cnt,
        //        bl_hw->mpa_rx_data.buf_len, cmd53_port, 
        //        ktime_to_us(cmd53_end_ts - cmd53_start_ts));

#ifdef BL_TRX_PROFILE
        bl_rec_upload_ts(bl_hw, RX_HDL_READ);
#endif

        curr_ptr = bl_hw->mpa_rx_data.buf;
        
        //printk("mpa_rx_data.buf: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x \n",
        //                        curr_ptr[0],
        //                        curr_ptr[1],
        //                        curr_ptr[2],
        //                        curr_ptr[3],
        //                        curr_ptr[4],
        //                        curr_ptr[5],
        //                        curr_ptr[6],
        //                        curr_ptr[7]
        //        );

        for (pind = 0; pind < bl_hw->mpa_rx_data.pkt_cnt; pind++) {
            u16 data_idx = 0;
            
            skb = (struct sk_buff *)(bl_hw->mpa_rx_data.buf_arr[pind]);
            memcpy((u8 *)(skb->data), curr_ptr, bl_hw->mpa_rx_data.len_arr[pind]);
            curr_ptr += bl_hw->mpa_rx_data.len_arr[pind];

            pkt_len  = le16_to_cpu(*(__le16 *) ((skb->data + 0)));
            pkt_type = le16_to_cpu(*(__le16 *) ((skb->data + 2)));
            hw_idx   = le16_to_cpu(*(__le16 *) ((skb->data + 4)));
            reserved = le16_to_cpu(*(__le16 *) ((skb->data + 6)));

            BL_DBG("%s %u, index %d pkt, rx_len:%u, pkt_len:%u\r\n", 
                   __func__, __LINE__, pind, 
                   bl_hw->mpa_rx_data.len_arr[pind], pkt_len);

            if (pkt_len > bl_hw->mpa_rx_data.len_arr[pind]) {
                #if 0
                bl_dump(skb->data, bl_hw->mpa_rx_data.len_arr[pind]);
                BL_DBG_MSG("cmd53_port:0x%x, dump mp_rx_data:\n", cmd53_port);
                bl_dump((uint8_t *)&bl_hw->mpa_rx_data, sizeof(bl_hw->mpa_rx_data));
                BL_DBG_MSG("dump mp_rx_data.buf:\n");
                bl_dump((uint8_t *)bl_hw->mpa_rx_data.buf, bl_hw->mpa_rx_data.buf_len);
                ASSERT_WARN(0);
                #endif

                dev_kfree_skb_any(skb);                
                continue;
            }

            data_idx = (u16)((reserved & 0xFFF0) >> 4);

            BL_DBG("pkt_len=%d, pkt_type=%d, hw_idx=%d, reserved=0x%x, dc:%u, pad:%u\n", 
                   pkt_len, pkt_type, hw_idx, reserved, data_idx, (u8_l)(reserved & 0xF));

            if (bl_hw->mod_params->mp_mode == 0) {
                if (data_idx == last_data_cnt) {
                    #if 0
                    printk("**drop dup data cnt=%u-%u,pad=%d,rd_bm=%x,cur_rd_port=%d,start_port:%d,pkt_len:%u,rx_len:%u\n", 
                           data_idx, last_data_cnt, (u8_l)(reserved & 0xF), 
                           card->mp_rd_bitmap, card->curr_rd_port, 
                           bl_hw->mpa_rx_data.start_port,
                           pkt_len,
                           bl_hw->mpa_rx_data.len_arr[pind]);

                    bl_dump(skb->data, bl_hw->mpa_rx_data.len_arr[pind]);
                    #endif
                    
                    dev_kfree_skb_any(skb);
                    continue;
                }

                if ((reserved & 0xF) > 4) {
                    #if 0
                    printk("**drop too long padding data cnt=%u-%u,pad=%d,rd_bm=%x,cur_rd_port=%d,start_port:%d,pkt_len:%u,rx_len:%u\n", 
                           data_idx, last_data_cnt, (u8_l)(reserved & 0xF), 
                           card->mp_rd_bitmap, card->curr_rd_port, 
                           bl_hw->mpa_rx_data.start_port,
                           pkt_len,
                           bl_hw->mpa_rx_data.len_arr[pind]);
                           
                    bl_dump(skb->data, bl_hw->mpa_rx_data.len_arr[pind]);
                    #endif
                    
                    dev_kfree_skb_any(skb);
                    continue;
                }

                last_data_cnt = data_idx;
            }

            #ifdef CONFIG_BL_BTSDU
            if (pkt_type == BL_TYPE_HCI) {
                skb_put(skb, pkt_len + sizeof(struct inf_hdr));

                #if 0
                //delay to process in rx work queue
                BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
                skb_queue_tail(&bl_hw->rx_pkt_list, skb);
                BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
                
                if(bl_hw->rx_work_flag) {
                    bl_queue_rx_work(bl_hw);
                }
                #else
                skb_pull(skb, sizeof(struct inf_hdr));
                
                btsdio_rx_len = *(uint32_t *)skb->data;
                
                BL_DBG_MSG("btsdio rx len over cmd, data:%d %d, %02x %02x %02x %02x %02x %02x %02x %02x\r\n", 
                       skb->len, btsdio_rx_len, 
                       skb->data[4], skb->data[5], skb->data[6], skb->data[7], 
                       skb->data[8], skb->data[9], skb->data[10], skb->data[11]);
                       
                skb_pull(skb, 4);  //BTSDU_MSG_LEN_FIELD takes 4 bytes
                pskb_trim(skb, btsdio_rx_len<skb->len?btsdio_rx_len:skb->len);
                
                btsdio_rx_packet(btsdio_data, skb, btsdio_rx_len);
                #endif

                continue;
            }
            #endif

            if (pkt_len) {
                skb_put(skb, MIN((pkt_len + sizeof(struct inf_hdr) + 
                                 (u8_l)(reserved & 0xF)),
                                 bl_hw->mpa_rx_data.len_arr[pind]));
                                 
                BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
                skb_queue_tail(&bl_hw->rx_pkt_list, 
                         (struct sk_buff *)(bl_hw->mpa_rx_data.buf_arr[pind]));
                BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
                
                if(bl_hw->rx_work_flag) {
                    bl_queue_rx_work(bl_hw);
                }
            }
        }
        
        bl_hw->mpa_rx_data.pkt_cnt = 0;
        bl_hw->mpa_rx_data.ports = 0;
        bl_hw->mpa_rx_data.buf_len = 0;
        memset(bl_hw->mpa_rx_data.buf_arr, 0, sizeof(bl_hw->mpa_rx_data.buf_arr));
        memset(bl_hw->mpa_rx_data.len_arr, 0, sizeof(bl_hw->mpa_rx_data.len_arr));
        
#ifdef BL_TRX_PROFILE
        bl_rec_upload_ts(bl_hw, RX_HDL_QUEUE);
#endif
    }

    bl_hw->data_recv = false;

    if(rd_twice == true) {
        //printk("start read twice\n");
        rd_twice = false;
        rd_again = true;
        goto TWICE;
    }

#ifdef BL_TRX_PROFILE
    bl_rec_upload_ts(bl_hw, RX_HDL_END);
#endif

    return 0;

//term_cmd:
    /* terminate cmd */
    if (bl_read_reg(bl_hw, CONFIGURATION_REG, &cr))
        printk("read CFG reg failed\n");
    else
        printk("info: CFG reg val = %d\n", cr);

    if (bl_write_reg(bl_hw, CONFIGURATION_REG, (cr | 0x04)))
        printk("write CFG reg failed\n");
    else
        printk("info: write success\n");

    if (bl_read_reg(bl_hw, CONFIGURATION_REG, &cr))
        printk("read CFG reg failed\n");
    else
        printk("info: CFG reg val =%x\n", cr);

    return -1;
}

static int bl_download_hdl(struct bl_hw *bl_hw)
{
    u8 wr_bitmap_l = 0;
    u8 wr_bitmap_u = 0;
    u32 bitmap;

    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;

    BL_DBG(BL_FN_ENTRY_STR);

    wr_bitmap_l = card->mp_regs[card->reg->wr_bitmap_l];
    wr_bitmap_u = card->mp_regs[card->reg->wr_bitmap_u];
    bitmap = wr_bitmap_l;
    bitmap |= wr_bitmap_u << 8;

    if (bitmap == 0) {
        BL_TRACE(TRACE_TX, "WARN:empty dnld_rdy int\r\n");
    }

    /* For only 1 port available in bitmap, have corner case to get duplicated irq */
    if (((bitmap & DATA_PORT_MSK) != DATA_PORT_MSK) && 
         card->prev_wr_bitmap == bitmap) 
    {
        BL_TRACE(TRACE_TX, 
                 "WARN: skip duplicate dnld_rdy int bitmap=0x%x\r\n", bitmap);
        return 0;
    }

    if (card->mp_wr_bitmap & DATA_PORT_MSK) {
        BL_TRACE(TRACE_TX, 
                 "WARN: skip duplicate bitmap=0x%x wr=0x%x, prev_wr=0x%x\n",
                 bitmap, card->mp_wr_bitmap, card->prev_wr_bitmap);
        return 0;
    }

    card->mp_wr_bitmap = bitmap;

    BL_DBG_MSG("dnld_hdl: wr_bitmap=0x%x prev=0x%x\n", 
            card->mp_wr_bitmap, card->prev_wr_bitmap);

    card->prev_wr_bitmap = bitmap;

#if defined(BL_BUS_LOOPBACK)
#if defined(LBK_SDIO_THREAD)
    //1st way to use sema to trigger lbk_thread in bl_loopback.c when SDIO loopback use sema
    if ((bl_hw->ploopback != NULL) && (bitmap & card->reg->data_port_mask) &&
        (bitmap & (1 << card->curr_wr_port))) 
    {
       up(&bl_hw->ploopback->sema);
       up(&bl_hw->ploopback->sema);
       up(&bl_hw->ploopback->sema);
    }
#else
    //2nd way to directly lbk_process to send, not use lbk_thread.
    int bl_lbk_process(struct bl_hw *bl_hw);
    if ((bl_hw->ploopback != NULL) && (bitmap & card->reg->data_port_mask) &&
        (bitmap & (1 << card->curr_wr_port))) 
    {
       bl_lbk_process(bl_hw);
    }
    
    //printk( "int: DNLD 1: wr_bitmap=0x%x prev=0x%x curr_wr_port:%d\n", card->mp_wr_bitmap, card->prev_wr_bitmap, card->curr_wr_port);
    if ((bl_hw->ploopback != NULL) && (bitmap & card->reg->data_port_mask) && 
        (bitmap & (1 << card->curr_wr_port))) 
    {
       bl_lbk_process(bl_hw);
    }
    
    //printk( "int: DNLD 2: wr_bitmap=0x%x prev=0x%x curr_wr_port:%d\n", card->mp_wr_bitmap, card->prev_wr_bitmap, card->curr_wr_port);
    if ((bl_hw->ploopback != NULL) && (bitmap & card->reg->data_port_mask) &&
        (bitmap & (1 << card->curr_wr_port))) 
    {
       bl_lbk_process(bl_hw);
    }
    //printk( "int: DNLD 3: wr_bitmap=0x%x prev=0x%x curr_wr_port:%d\n", card->mp_wr_bitmap, card->prev_wr_bitmap, card->curr_wr_port);
#endif
#endif

    return 0;
}
#endif

void bl_queue_rx_work(struct bl_hw *bl_hw)
{
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (!bl_hw->rx_workqueue)
    {
        return;
    }
        
    //spin_lock_bh(&bl_hw->rx_process_lock);
    BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
    if (bl_hw->rx_processing) {
        bl_hw->more_rx_task_flag = true;
        BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
    } else {
        BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
        
        BL_DBG("bl_queue_rx_work: schedule rx work\n");

        queue_work(bl_hw->rx_workqueue, &bl_hw->rx_work);
    }
}

int bl_main_process(struct bl_hw *bl_hw)
{
#ifdef CONFIG_BL_SDIO
    u8 sdio_ireg;
#endif
    int ret = 0;
    unsigned long flags;
 
    BL_DBG(BL_FN_ENTRY_STR);
    
    BL_DBG(">>> %s(), %d, %d\n", __func__, bl_hw->bl_processing, 
           bl_hw->more_task_flag);
    
    if (!bl_hw->drv_ready || bl_hw->surprise_removed || !bl_hw->plat->enabled) {
        printk("%s, removed %d, suspended:%d, or early interrupt, nothing to do\r\n", 
               __func__, bl_hw->surprise_removed, bl_hw->drv_ready);
               
        return ret;
    }

    /*we know irq is enable, so here use spin_lock_irq*/
    spin_lock_irqsave(&bl_hw->main_proc_lock, flags);
    /* Check if already processing */
    if (bl_hw->bl_processing) {
        BL_DBG("bl_main_process: more_task_flag = true\n");
        
        bl_hw->more_task_flag = true;
        spin_unlock_irqrestore(&bl_hw->main_proc_lock, flags);
        
        goto exit_main_proc;
    } else {
        bl_hw->bl_processing = true;
        spin_unlock_irqrestore(&bl_hw->main_proc_lock, flags);
    }

    bl_hw->lost_int_flag = 0;

process_start:

#ifdef CONFIG_BL_SDIO
    spin_lock_irqsave(&bl_hw->int_lock, flags);
    sdio_ireg = bl_hw->int_status;
    bl_hw->int_status = 0;
    spin_unlock_irqrestore(&bl_hw->int_lock, flags);
#endif

#ifdef CONFIG_BL_SDIO
    /* 2.handle recv data */
    if (sdio_ireg & UP_LD_HOST_INT_STATUS) {
        ret = bl_upload_hdl(bl_hw);
        if(ret) {
            spin_lock_irqsave(&bl_hw->main_proc_lock, flags);
            bl_hw->bl_processing = false;
            spin_unlock_irqrestore(&bl_hw->main_proc_lock, flags);
            
            bl_queue_main_work(bl_hw);

            goto exit_main_proc;
        }
    }

    /* 3. if wr port ready, see if data need to transmit */
    if (sdio_ireg & DN_LD_HOST_INT_STATUS) {
        ret = bl_download_hdl(bl_hw);
    }
#endif

    if (!bl_hw->rx_work_flag && 
        (bl_hw->flush_rx || skb_queue_len(&bl_hw->rx_pkt_list)))
        bl_rx_wq_process(bl_hw);

    /* 1.handle the msg */
    if (!list_empty(&bl_hw->cmd_mgr.cmds)) {
        BL_DBG("%s, bl_hw->cmd_sent:%d\n", __func__, bl_hw->cmd_sent);
        
        ret = bl_pending_msg_hdl(bl_hw);
        if (ret) {
            printk("send msg failed, ret=%d\n", ret);

            spin_lock_irqsave(&bl_hw->main_proc_lock, flags);
            bl_hw->bl_processing = false;
            spin_unlock_irqrestore(&bl_hw->main_proc_lock, flags);
            
            bl_queue_main_work(bl_hw);

            goto exit_main_proc;
        }
    }

    #ifdef CONFIG_BL_BTSDU
    ret = bl_pending_hci_msg_hdl(bl_hw);
    if (ret) {
        printk("send hci msg failed, ret=%d\n", ret);

        spin_lock_irqsave(&bl_hw->main_proc_lock, flags);
        bl_hw->bl_processing = false;
        spin_unlock_irqrestore(&bl_hw->main_proc_lock, flags);
        
        bl_queue_main_work(bl_hw);

        return ret;
    }
    #endif

    /* 4. handle tx data second */
    BL_DBG("recovery_flag %d, scan:0x%p\n", 
           bl_hw->recovery_flag, bl_hw->scan_request);
           
    if (!bl_hw->recovery_flag && !bl_hw->scan_request)
        bl_hwq_process_all(bl_hw);

    spin_lock_irqsave(&bl_hw->main_proc_lock, flags);
    if (bl_hw->more_task_flag) {
        bl_hw->more_task_flag = false;
        spin_unlock_irqrestore(&bl_hw->main_proc_lock, flags);
        
        #ifdef CONFIG_BL_SDIO
        bl_hw->lost_int_flag = 1;
        #endif
        
        BL_DBG("go to process start\n");
        
        goto process_start;
    }
    
    bl_hw->bl_processing = false;
    spin_unlock_irqrestore(&bl_hw->main_proc_lock, flags);

    BL_DBG(BL_FN_EXIT_STR);
    
exit_main_proc:
    return ret;
}

void bl_main_wq_hdlr(struct work_struct *work)
{
    struct bl_hw *bl_hw = container_of(work, struct bl_hw, main_work);

    BL_DBG(BL_FN_ENTRY_STR);
    
    bl_main_process(bl_hw);
}

void bl_queue_main_work(struct bl_hw *bl_hw)
{
    unsigned long flags;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (!bl_hw->drv_ready || bl_hw->surprise_removed || !bl_hw->workqueue)
        return;
    
    spin_lock_irqsave(&bl_hw->main_proc_lock, flags);
    if (bl_hw->bl_processing) {
        bl_hw->more_task_flag = true;
        spin_unlock_irqrestore(&bl_hw->main_proc_lock, flags);
        BL_DBG("bl_queue_main_work: set more_task_flag true\n");
    } else {
        spin_unlock_irqrestore(&bl_hw->main_proc_lock, flags);
        BL_DBG("bl_queue_main_work: schedule work\n");
        queue_work(bl_hw->workqueue, &bl_hw->main_work);
    }
}

#endif
