#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <linux/etherdevice.h>
#include <linux/signal.h>

#include "bl_loopback.h"
#include "bl_defs.h"
#include "bl_msg_tx.h"
#include "bl_tx.h"
#include "bl_hal_desc.h"
#include "bl_irqs.h"
#ifdef CONFIG_BL_DEBUGFS
#include "bl_debugfs.h"
#endif
#include "bl_cfgfile.h"
#if defined CONFIG_BL_SDIO
#include "bl_sdio.h"
#elif defined CONFIG_BL_USB
#include "bl_usb.h"
#endif
#include "bl_radar.h"
#include "bl_version.h"
#ifdef CONFIG_BL_BFMER
#include "bl_bfmer.h"
#endif //(CONFIG_BL_BFMER)
#include "bl_tdls.h"
#include "bl_compat.h"

static u16 g_lbk_pkt_sn = 0xDD;
static struct sk_buff *g_lbk_sdio_skb;
static ktime_t g_kt_start;
ktime_t g_last_rxtime;

static int bl_loopback_init(struct bl_hw *bl_hw)
{
	ploopbackdata ploopback;

	if (bl_hw->ploopback == NULL) {
		ploopback = (ploopbackdata)kmalloc(sizeof(loopbackdata), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
		if (ploopback == NULL) {
			printk("%s malloc(%d) fail!\n", __func__, sizeof(loopbackdata));
			return -1;
		}

        sema_init(&ploopback->sema, 8);
		ploopback->bstop = true;
		ploopback->exp_data_rate = 0;
		ploopback->pkt_size = 50;
        ploopback->test_dir = 0;
        ploopback->succ_cnt = 0;
        ploopback->fail_cnt = 0;

        ploopback->rx_succ_cnt = 0;
        ploopback->rx_fail_cnt = 0;
        ploopback->rx_total_succ_cnt = 0;
        ploopback->rx_total_fail_cnt = 0;
        //ploopback->last_rxtime = 0;
        //ploopback->rxtime = 0;
		g_lbk_pkt_sn = 0xDD;
		memset(ploopback->msg, 0, sizeof(ploopback->msg));

		bl_hw->ploopback = ploopback;
	}

	return 0;
}

static void bl_loopback_free(struct bl_hw *bl_hw)
{
	ploopbackdata ploopback;

	BL_DBG("%s \n", __func__);

	ploopback = bl_hw->ploopback;
	if (ploopback) {
		kfree((u8 *)ploopback);
		bl_hw->ploopback = NULL;
	}
}

static struct sk_buff *bl_build_loopbackpkt(struct bl_hw *bl_hw, u32 pkt_size)
{
	struct bl_txhdr *txhdr;
	struct bl_sw_txhdr *sw_txhdr;
	struct txdesc_api *desc;
	struct sk_buff *skb;
	u16 frame_len, headroom, frame_oft;
	u8 *data;
	int hdr_pads = 0;
    u16 i = 0;

	// BL_DBG(BL_FN_ENTRY_STR);

    headroom = sizeof(struct bl_txhdr) + sizeof(struct txdesc_api) + sizeof(struct inf_hdr);
	frame_len = pkt_size;

	/* Create a SK Buff object that will contain the random payload data */
	skb = dev_alloc_skb(headroom + frame_len);
	if (!skb) {
		printk("%s alloc skb fail!\n", __func__);
		return NULL;
	}
    // printk("0 skb->data=%x, skb->tail=%x\n", skb->data, skb->tail);

	/*
	 * Move skb->data pointer in order to reserve room for bl_txhdr
	 * headroom value will be equal to sizeof(struct bl_txhdr)
	 */
	skb_reserve(skb, headroom);
    // printk("1 %d skb->data=%x skb->tail=%x\n", headroom, skb->data, skb->tail);

	/*
	 * Extend the buffer data area in order to contain the provided packet
	 * len value (for skb) will be equal to param->len
	 */
	data = skb_put(skb, frame_len);
	/* Random the payload data */

    // printk("2 %d skb->data=%x, skb->tail=%x\n", frame_len, skb->data, skb->tail);
	get_random_bytes(data, frame_len);
    for (i=0; i<min(20, frame_len/2); i++) {
        *(data + i) = i;
        *(data + frame_len - 1 - i) = i;
    }
    
	// for (i = 0; i < frame_len; i++) {
    //     printk("[%d]0x%x=0x%x,", i, data+i, *(data+i));
    // }
	
	/*
	 * Go back to the beginning of the allocated data area
	 * skb->data pointer will move backward
	 */
	/* first we increase the area for tx descriptor */
	skb_push(skb, sizeof(struct txdesc_api));
    // printk("3 %d skb->data=%x, skb->tail=%x\n",sizeof(struct txdesc_api),  skb->data, skb->tail);
	/* then we increase the area for sdio header*/
#if defined(CONFIG_BL_SDIO)
	skb_push(skb, sizeof(struct inf_hdr));
    // printk("4 %d skb->data=%x, skb->tail=%x\n", sizeof(struct inf_hdr), skb->data, skb->tail);
#elif defined(CONFIG_BL_USB)
    skb_push(skb, sizeof(struct inf_hdr));
    // printk("4 %d skb->data=%x, skb->tail=%x\n", sizeof(struct inf_hdr), skb->data, skb->tail);
#endif
	/* then we increase the area for tx header */
	headroom = sizeof(struct bl_txhdr);
	skb_push(skb, headroom);
    // printk("5 %d, skb->data=%x, skb->tail=%x\n", headroom, skb->data, skb->tail);

	/* Fill the TX Header */
	txhdr = (struct bl_txhdr *)skb->data;
	txhdr->hw_hdr.cfm.status.value = 0;

	/* Fill the SW TX Header */
	sw_txhdr = kmem_cache_alloc(bl_hw->sw_txhdr_cache, GFP_ATOMIC);
	if (unlikely(sw_txhdr == NULL)) {
		printk("%s alloc sw_txhdr fail!\n", __func__);
		dev_kfree_skb(skb);
		return NULL;
	}
	txhdr->sw_hdr = sw_txhdr;

	sw_txhdr->hdr.type = BL_TYPE_BUS_LOOPBACK;
    sw_txhdr->hdr.len = frame_len + sizeof(struct txdesc_api) + sizeof(struct inf_hdr);
	sw_txhdr->hdr.queue_idx = 0;
	sw_txhdr->hdr.reserved = g_lbk_pkt_sn;
	
	// BL_DBG("build_loopback_pkt_sn: hw_idx=%d, sw_txhdr->hdr.len=%d, frame_len=%u, sn=%u\n", 
    //           sw_txhdr->hdr.queue_idx, sw_txhdr->hdr.len, frame_len, g_lbk_pkt_sn);
	g_lbk_pkt_sn++;

	sw_txhdr->txq = NULL;
	sw_txhdr->frame_len = frame_len;
	sw_txhdr->bl_sta = NULL;
	sw_txhdr->bl_vif = NULL;
	sw_txhdr->skb = skb;
	sw_txhdr->headroom = headroom;
	sw_txhdr->map_len = skb->len - offsetof(struct bl_txhdr, hw_hdr);

	/* Fill the Descriptor to be provided to the MAC SW */
	desc = &sw_txhdr->desc;

	desc->host.staid = 0xAA;
	desc->host.vif_idx = 0xBB;
	desc->host.tid = 0xCC;
    desc->host.ethertype = 0x888E;
    desc->host.pn[0] = 0x11;
    desc->host.pn[1] = 0x22;
    desc->host.pn[2] = 0x33;
    desc->host.pn[3] = 0x44;
    desc->host.eth_src_addr.array[0] = 0x1234;
    desc->host.eth_src_addr.array[1] = 0x5678;
    desc->host.eth_src_addr.array[2] = 0x9abc;

    desc->host.eth_dest_addr.array[0] = 0xfedc;
    desc->host.eth_dest_addr.array[1] = 0xba98;
    desc->host.eth_dest_addr.array[2] = 0x7654;
    
	desc->host.host_tx_padding = hdr_pads;
	desc->host.packet_len[0] = frame_len;

	frame_oft = sizeof(struct bl_txhdr) - offsetof(struct bl_txhdr, hw_hdr);

	desc->host.packet_addr[0] = 0x5a5a6a6a;//sw_txhdr->dma_addr + frame_oft;
	return skb;
}

#if defined(CONFIG_BL_USB)
static u8 bl_send_loopbackpkt(struct bl_hw *bl_hw, struct sk_buff *skb)
{
	//sdio_mpa_tx *mpa_tx_data = &bl_hw->mpa_tx_data;
	struct bl_txhdr *txhdr;	
	int ret ;
	u32 buf_block_len;
    int i = 0;
	// BL_DBG("%s \n", __func__);

	/*copy skb into one large buf*/
	txhdr = (struct bl_txhdr *)skb->data;
	txhdr->sw_hdr->hw_queue = 0;
	skb_pull(skb, txhdr->sw_hdr->headroom);
	txhdr->sw_hdr->hdr.queue_idx = 0;
    memcpy((void *)skb->data + sizeof(struct inf_hdr), &txhdr->sw_hdr->desc, sizeof(struct txdesc_api));
    memcpy((void *)skb->data, &txhdr->sw_hdr->hdr, sizeof(struct inf_hdr));
    // buf_block_len = (skb->len + 320 - 1) / 320; //BL_SDIO_BLOCK_SIZE=320
	// memcpy((void *)&mpa_tx_data.buf[mpa_tx_data.buf_len], skb->data, buf_block_len * 320);
	// mpa_tx_data.buf_len += buf_block_len * 320;
	// mpa_tx_data->pkt_cnt++;
    // mpa_tx_data->buf_len = skb->len;
		
	kmem_cache_free(bl_hw->sw_txhdr_cache, txhdr->sw_hdr);

#if 1
    //printk("%s, skb->len:%d, txhdr->sw_hdr->headroom:%d, inf_hdr->len:%d, inf_hdr->rsv:%d, g_lbk_pkt_sn:%d\n", 
    //         __func__, skb->len, txhdr->sw_hdr->headroom, txhdr->sw_hdr->hdr.len, txhdr->sw_hdr->hdr.reserved, g_lbk_pkt_sn);
    //printk("dump skb->data\n");
    //for(i = 0; i < min(100, skb->len); i++)
    //    printk("[%d]=0x%02x", i, skb->data[i]);
#endif

    ret = bl_usb_host_to_card(bl_hw, skb->data, skb->len, BL_USB_EP_OUT, BL_USB_EP_DATA, skb);
	if(ret) {
        // if (ret == -EBUSY) {
        //     msleep_interruptible(1000);
        // }
		printk("%s, call bl_usb_host_to_card failed, ret=%d\n", __func__, ret);
		return true;
	}

	return false;
}
#endif
uint32_t one_agg_sdio_max_cnt = 0;

#if defined(CONFIG_BL_SDIO)
static u8 bl_send_loopbackpkt(struct bl_hw *bl_hw, struct sk_buff *skb)
{
    int i = 0;
    sdio_mpa_tx mpa_tx_data = {0};
    struct bl_txhdr *txhdr;
    int ret ;
    u32 port = 0;
    u32 cmd53_port;
    u32 buf_block_len;
    struct sdio_mmc_card *card;
    card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    BL_DBG("%s \n", __func__);

    /*fix alloc buf pkt_size, such as 16K(2*8(aggr num))*/
    mpa_tx_data.buf = bl_hw->mpa_tx_data.buf;
    if(mpa_tx_data.buf == NULL){
        printk("mpa_tx_data.buf is null!\n");
        return true;
    }

    /*copy skb into one large buf*/
    txhdr = (struct bl_txhdr *)skb->data;
    txhdr->sw_hdr->hw_queue = 0;

    /*copy multi skbs into one large buf*/
    for(i = 0; i < 7; i++) {
        ret = bl_get_wr_port(bl_hw, &port);
        //printk("%s bl_get_wr_port ret:%d, port:%d, %d\n",__func__, ret, port, i);
        if(ret) {
            break;
        }

        skb_pull(skb, txhdr->sw_hdr->headroom);
        txhdr->sw_hdr->hdr.queue_idx = 0;
        memcpy((void *)skb->data, &txhdr->sw_hdr->hdr, sizeof(struct inf_hdr));
        memcpy((void *)skb->data + sizeof(struct inf_hdr), &txhdr->sw_hdr->desc, sizeof(struct txdesc_api));
        buf_block_len = (skb->len + BL_SDIO_BLOCK_SIZE - 1) / BL_SDIO_BLOCK_SIZE;
        memcpy((void *)&mpa_tx_data.buf[mpa_tx_data.buf_len], skb->data, buf_block_len * BL_SDIO_BLOCK_SIZE);
        mpa_tx_data.buf_len += buf_block_len * BL_SDIO_BLOCK_SIZE;

        if(!mpa_tx_data.pkt_cnt) {
            mpa_tx_data.start_port = port;
        }

        if(mpa_tx_data.start_port <= port) {
            mpa_tx_data.ports |= (1 << (mpa_tx_data.pkt_cnt));
        } else {
            mpa_tx_data.ports |= (1 << (mpa_tx_data.pkt_cnt + 1));
        }

        mpa_tx_data.pkt_cnt++;

        skb_push(skb, txhdr->sw_hdr->headroom);
    }

    if (mpa_tx_data.pkt_cnt) {
        /*send packet*/
        cmd53_port = (card->io_port | BL_SDIO_MPA_ADDR_BASE |
                        (mpa_tx_data.ports << 4)) + mpa_tx_data.start_port;

        #if 0
        printk("cmd53ports=0x%x:ports=0x%02x, start_port=%d, buf=%p, buf_len=%d, pkt_cnt=%d\n",
                cmd53_port,
                mpa_tx_data.ports, 
                mpa_tx_data.start_port,
                mpa_tx_data.buf,
                mpa_tx_data.buf_len,
                mpa_tx_data.pkt_cnt);
            if (!mpa_tx_data.pkt_cnt) {
                printk("pkt_cnt=0, buf_len=%d, start_port=%x, ports=%x, cmd53_port=%x\r\n", 
                     mpa_tx_data.buf_len, mpa_tx_data.start_port, mpa_tx_data.ports, cmd53_port);
            }
        //printk("dump skb_data=%x\n", skb->data);
        //for(i = 0; i < mpa_tx_data.buf_len; i++)
        //    printk("0x%x-0x%x:", mpa_tx_data.buf[i], skb->data[i]);
        //printk("\ndump end\n");
        #endif

        ret = bl_write_data_sync(bl_hw, mpa_tx_data.buf, mpa_tx_data.buf_len, cmd53_port);
        if(ret) {
            printk("%s, bl_write_data_sync fail, ret:%d\n", __func__, ret);
            bl_hw->ploopback->fail_cnt += mpa_tx_data.pkt_cnt;
        } else {
            bl_hw->ploopback->succ_cnt += mpa_tx_data.pkt_cnt;
        }
        
        if (mpa_tx_data.pkt_cnt > one_agg_sdio_max_cnt)
            one_agg_sdio_max_cnt = mpa_tx_data.pkt_cnt;
    }
    //printk("%s, post mpa_tx_data.pkt_cnt:%d, sdio write ret:%d\n", __func__, mpa_tx_data.pkt_cnt, ret);

    if(bl_hw->mpa_tx_data.buf)
        memset(bl_hw->mpa_tx_data.buf, 0, BL_RX_DATA_BUF_SIZE_16K);

    return false;
}
#endif

static u8 bl_pktcmp(struct bl_hw *bl_hw, u8 *txbuf, u32 txsz, u8 *rxbuf, u32 rxsz)
{
	u16 tx_payload_oft, rx_payload_oft;
	u8 ret = false;

#if defined(CONFIG_BL_SDIO)
	tx_payload_oft = sizeof(struct bl_txhdr) + sizeof(struct txdesc_api) + sizeof(struct inf_hdr);
#elif defined(CONFIG_BL_USB)
	tx_payload_oft = sizeof(struct bl_txhdr) + sizeof(struct txdesc_api) + sizeof(struct inf_hdr);
#endif

	rx_payload_oft = sizeof(struct hw_rxhdr) + 4;/*status occupy 4 bytes*/

	/* payload pkt_size check */
	if ((txsz - tx_payload_oft) != (rxsz - rx_payload_oft)) {
		printk("%s: ERROR! size not match tx/rx=%d/%d !\n",
			 __func__, (txsz - tx_payload_oft), (rxsz - rx_payload_oft));
		ret = false;
	} else {
		ret = (!memcmp(txbuf + tx_payload_oft, rxbuf + rx_payload_oft, txsz - tx_payload_oft));
		if (ret == false)
			printk("%s: ERROR! pkt payload mismatch!\n", __func__);
	}

	if (ret == false) {
		printk("\n%s: TX PKT total=%d, offset=%d, payload=%d\n",
			 __func__, txsz, tx_payload_oft, txsz - tx_payload_oft);

		printk("%s: TX payload size=%d\n", __func__, txsz - tx_payload_oft);
		//bl_dumpdata(txbuf + tx_payload_oft, txsz - tx_payload_oft);

		printk("\n%s: RX PKT read=%d offset=%d payload=%d\n",
			__func__, rxsz, rx_payload_oft, rxsz - rx_payload_oft);
		if ((rxsz - rx_payload_oft) != 0) {
			printk("%s: RX payload size=%d\n", __func__, rxsz);
			//bl_dumpdata(rxbuf + rx_payload_oft, rxsz - rx_payload_oft);
		} else {
			printk("%s: RX data size=%d\n", __func__, rxsz);
			//bl_dumpdata(rxbuf, rxsz);
		}
	}

	return ret;
}

int bl_lbk_thread(void *context)
{
	int err;
	struct bl_hw *bl_hw;
	ploopbackdata ploopback;
	struct sk_buff *skb;	
	u32 cnt, ok, fail;
	u32 total_size;
    u32 data_rate = 0;
    ktime_t kt_now;
    s64 measure_time;
    s64 delta;

	bl_hw = (struct bl_hw *)context;
	ploopback = bl_hw->ploopback;
	if (ploopback == NULL)
		return -1;
		
	cnt = 0;
	ok = 0;
	fail = 0;

	allow_signal(SIGTERM);
	BL_DBG(BL_FN_ENTRY_STR);

    printk("%s, pkt_size:%d, exp_data_rate:%d\n", __func__, ploopback->pkt_size, ploopback->exp_data_rate);
    
    msleep_interruptible(3000);

    g_kt_start = ktime_get();

	do {
	    #ifdef CONFIG_BL_USB
		skb = bl_build_loopbackpkt(bl_hw, ploopback->pkt_size);
		if (skb == NULL) {
			sprintf(ploopback->msg, "loopback FAIL! 2. build Packet FAIL!\r\n");
			printk("loopback FAIL! 2. build Packet FAIL!\n");
			break;
		}
		#else
        skb = g_lbk_sdio_skb;
		#endif

        //printk("%s, 1, cnt:%d\n", __func__, cnt);
		cnt++;

        down_interruptible(&ploopback->sema);

		err = bl_send_loopbackpkt(bl_hw, skb);
		if (err == true) {
			printk("bl_send_loopbackpkt FAIL! \n");
			if (skb)
				dev_kfree_skb(skb);
            continue;
		}

        kt_now = ktime_get();
        //data rate, bit per second
        measure_time = ktime_ms_delta(kt_now, ploopback->last_txtime);
        //printk("%s, 3, time:%d-%d, cnt:%d, measure_time:%lld\n", __func__, ploopback->last_txtime, kt_now, cnt, measure_time);
        if (measure_time > MEASURE_MS) {
            total_size = ploopback->succ_cnt * (ploopback->pkt_size + sizeof(struct txdesc_api) + sizeof(struct inf_hdr));
            ok += ploopback->succ_cnt;
            fail += ploopback->fail_cnt;
            ploopback->last_txtime = ktime_get();

            data_rate = total_size*8/(u32)measure_time;
            printk("%s, data_rate:%d(kbps), succ_cnt:%d, fail_cnt:%d, pkt_size:%d, total_size:%d, measure_time:%lld, one_agg_sdio_max_cnt:%d, all_cnt:%d\n", 
                    __func__, data_rate, ploopback->succ_cnt, ploopback->fail_cnt, ploopback->pkt_size, total_size, measure_time, one_agg_sdio_max_cnt, ok+fail);
            ploopback->fail_cnt = 0;
            ploopback->succ_cnt = 0;
            one_agg_sdio_max_cnt = 0;
        }

		if (ploopback->bstop == true) {
			u32 ok_rate, fail_rate, all;
			
			all = ok+fail;
			ok_rate = (ok * 100) / all;
			fail_rate = (fail * 100) / all;
            total_size = ok * (ploopback->pkt_size + sizeof(struct txdesc_api) + sizeof(struct inf_hdr));

            measure_time = ktime_ms_delta(ktime_get(), g_kt_start);
    
            data_rate = (total_size*8)/(u32)measure_time;
            printk("%s, total_size:%d, measure_time:%lld\n", __func__, total_size, measure_time);
            
			sprintf(ploopback->msg, \
				    "loopback result: ok=%d%%(%d/%d), error=%d%%(%d/%d), data_rate=%d(kbps)\r\n", \
				    ok_rate, ok, all, fail_rate, fail, all, data_rate);
			printk("loopback result: ok=%d%%(%d/%d), error=%d%%(%d/%d), data_rate=%d(kbps)\n", \
				    ok_rate, ok, all, fail_rate, fail, all, data_rate);
			break;
		}
	} while (1);

	ploopback->bstop = true;
	BL_DBG("%s exit\n", __func__);

	complete_and_exit(NULL, 0);
	return 0;
}

#ifdef CONFIG_BL_SDIO
int bl_lbk_process(struct bl_hw *bl_hw)
{
    int err;
    ploopbackdata ploopback;
    struct sk_buff *skb;    
    static u32 cnt = 0, ok = 0, fail = 0;
    static u32 total_size;
    static u32 data_rate = 0;
    static ktime_t kt_now;
    static s64 measure_time;
    s64 delta;

    ploopback = bl_hw->ploopback;
    if (ploopback == NULL)
        return -1;

    BL_DBG(BL_FN_ENTRY_STR);

    if (ploopback->bstop == true) {
        u32 ok_rate, fail_rate, all;
        
        all = ok+fail;
        ok_rate = (ok * 100) / all;
        fail_rate = (fail * 100) / all;
        total_size = ok * (ploopback->pkt_size + sizeof(struct txdesc_api) + sizeof(struct inf_hdr));

        measure_time = ktime_ms_delta(ktime_get(), g_kt_start);

        data_rate = (total_size*8)/(u32)measure_time;
        printk("%s, total_size:%d, measure_time:%lld\n", __func__, total_size, measure_time);
        
        sprintf(ploopback->msg, \
                "loopback result: ok=%d%%(%d/%d), error=%d%%(%d/%d), data_rate=%d(kbps)\r\n", \
                ok_rate, ok, all, fail_rate, fail, all, data_rate);
        printk("loopback result: ok=%d%%(%d/%d), error=%d%%(%d/%d), data_rate=%d(kbps)\n", \
                ok_rate, ok, all, fail_rate, fail, all, data_rate);

        return 0;
    }
    
    //printk("%s, exp_data_rate:%d\n", __func__, ploopback->exp_data_rate);

    skb = g_lbk_sdio_skb;

    //printk("%s, 1, cnt:%d\n", __func__, cnt,);
    cnt++;

    err = bl_send_loopbackpkt(bl_hw, skb);
    if (err == true) {
        printk("bl_send_loopbackpkt FAIL! \n");
        return -1;
    }

    kt_now = ktime_get();
    //data rate, bit per second
    measure_time = ktime_ms_delta(kt_now, ploopback->last_txtime);
    //printk("%s, 3, time:%d-%d, cnt:%d, measure_time:%lld\n", __func__, ploopback->last_txtime, kt_now, cnt, measure_time);
    if (measure_time > MEASURE_MS) {
        total_size = ploopback->succ_cnt * (ploopback->pkt_size + sizeof(struct txdesc_api) + sizeof(struct inf_hdr));
        ok += ploopback->succ_cnt;
        fail += ploopback->fail_cnt;
        ploopback->last_txtime = ktime_get();

        data_rate = total_size*8/(u32)measure_time;
        printk("%s, data_rate:%d(kbps), succ_cnt:%d, fail_cnt:%d, pkt_size:%d, total_size:%d, measure_time:%lld, all_cnt:%d, one_agg_sdio_max_cnt:%d\n", 
                __func__, data_rate, ploopback->succ_cnt, ploopback->fail_cnt, ploopback->pkt_size, total_size, measure_time, ok, one_agg_sdio_max_cnt);
        ploopback->fail_cnt = 0;
        ploopback->succ_cnt = 0;
        one_agg_sdio_max_cnt = 0;
    }

    return 0;
}
#endif

void bl_loopback_test(struct bl_hw *bl_hw, u32 exp_data_rate, u32 pkt_size, u32 test_dir, u8 *pmsg)
{
    ploopbackdata ploopback;
    u32 len;
    int err;
    BL_DBG(BL_FN_ENTRY_STR);

    if (test_dir == 1 || test_dir == 2) {
        struct dbg_lbk_cfm cfm;
        if ((err = bl_send_dbg_lbk_req(bl_hw, exp_data_rate, pkt_size, &cfm))) {
            printk("%s, req fw lbk fail\n", __func__);
        } else {
            printk("%s, req fw lbk status:%d\n", __func__, cfm.status);
        }

        if (test_dir == 1) {
            printk("%s, test_dir=1, fw to drv, return\n", __func__);
            return;
        } else {
            printk("%s, test_dir=2, fw to drv and drv to fw\n", __func__);
        }
    } else {
        printk("%s, test_dir=0, drv to fw\n", __func__);
    }


    ploopback = bl_hw->ploopback;

    if (ploopback) {
    	if (ploopback->bstop == false) {
            //wait lbk thread to exit if it is still running
    		ploopback->bstop = true;
            up(&ploopback->sema);
            msleep_interruptible(3000);
    	}
    	
    	len = 0;
    	
    	do {
    		len = strlen(ploopback->msg);
    		if (len)
    			break;
    		msleep_interruptible(1);
    	} while (1);
    	
    	memcpy(pmsg, ploopback->msg, min((len + 1), LBK_MSG_SIZE));
    	
    	#ifdef CONFIG_BL_SDIO
    	if (g_lbk_sdio_skb) {
            kmem_cache_free(bl_hw->sw_txhdr_cache, ((struct bl_txhdr *)g_lbk_sdio_skb->data)->sw_hdr);
    	    dev_kfree_skb_any(g_lbk_sdio_skb);
    	    g_lbk_sdio_skb = NULL;
    	}
    	#endif
    	
    	bl_loopback_free(bl_hw);
    }
	
    if (exp_data_rate == 0) {
        printk("%s, ploopback not null, free and return\n", __func__);
        return;
    } else {
        printk("%s, exp_data_rate != 0, ploopback not null, stop, free and restart\n", __func__);
    }

	err = bl_loopback_init(bl_hw);
	if (err) {
		sprintf(pmsg, "loopback FAIL! 1. init FAIL! error code=%d \r\n", err);
		printk("loopback FAIL! init FAIL! error code=%d \n", err);
		return;
	}

	ploopback = bl_hw->ploopback;

    if (pkt_size == 0) {
        get_random_bytes(&pkt_size, 4);
        ploopback->pkt_size = (pkt_size % 1535) + 1; /* 1~1535 */
    } else {
        ploopback->pkt_size = pkt_size;
    }

	ploopback->bstop = false;
	ploopback->exp_data_rate = exp_data_rate;
    ploopback->test_dir = test_dir;

    g_kt_start = ktime_get();
    g_last_rxtime = ktime_get();
    
    #ifdef CONFIG_BL_USB
	ploopback->lbkthread = kthread_run(bl_lbk_thread, bl_hw, "BL_LBK_THREAD");
	if (IS_ERR(ploopback->lbkthread)) {
		bl_loopback_free(bl_hw);
		ploopback->lbkthread = NULL;
		sprintf(ploopback->msg, "loopback start FAIL!\r\n");
		printk("loopback start FAIL!\n");
		return;
	}
    #else
    if (g_lbk_sdio_skb == NULL) {
        g_lbk_sdio_skb = bl_build_loopbackpkt(bl_hw, ploopback->pkt_size);
    }
    #if defined(LBK_SDIO_THREAD)
    //1st way to use sema to trigger lbk_thread in bl_loopback.c when SDIO loopback use sema
	ploopback->lbkthread = kthread_run(bl_lbk_thread, bl_hw, "BL_LBK_THREAD");
	if (IS_ERR(ploopback->lbkthread)) {
		bl_loopback_free(bl_hw);
		ploopback->lbkthread = NULL;
		sprintf(ploopback->msg, "loopback start FAIL!\r\n");
		printk("loopback start FAIL!\n");
		return;
	}
    #else
    //2nd way to directly lbk_process to send, not use lbk_thread.
    bl_lbk_process(bl_hw);
    bl_lbk_process(bl_hw);
    bl_lbk_process(bl_hw);
    #endif
    #endif
    ploopback->last_txtime = ktime_get();

	//sprintf(pmsg, "loopback start! exp_data_rate=%d pkt_size=%d", exp_data_rate, pkt_size);
	sprintf(ploopback->msg, "loopback start! pkt_size=%d \r\n", pkt_size);	
	printk("loopback start! pkt_size=%d \n", pkt_size);
}

