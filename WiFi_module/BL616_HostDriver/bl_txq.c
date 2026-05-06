/**
 ******************************************************************************
 *
 *  @file bl_txq.c
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
#include <linux/poison.h>

#include "bl_defs.h"
#include "bl_tx.h"
#include "bl_ipc_host.h"
#ifdef CONFIG_BL_SDIO
#include "bl_sdio.h"
#endif
#ifdef CONFIG_BL_USB
#include "bl_usb.h"
#endif

/******************************************************************************
 * Utils functions
 *****************************************************************************/

#define NB_TXQ_TX_THRESH        7
#define NB_HWQ_TX_THRESH        15

const int nx_tid_prio[NX_NB_TID_PER_STA] = {7, 6, 5, 4, 3, 0, 2, 1};

static inline int bl_txq_sta_idx(struct bl_sta *sta, u8 tid)
{
    if (is_multicast_sta(sta->sta_idx))
        return NX_FIRST_VIF_TXQ_IDX + sta->vif_idx;
    else
        return (sta->sta_idx * NX_NB_TXQ_PER_STA) + tid;
}

static inline int bl_txq_vif_idx(struct bl_vif *vif, u8 type)
{
    return NX_FIRST_VIF_TXQ_IDX + master_vif_idx(vif) + (type * NX_VIRT_DEV_MAX);
}

struct bl_txq *bl_txq_sta_get(struct bl_sta *sta, u8 tid,
                                  struct bl_hw * bl_hw)
{
    if (tid >= NX_NB_TXQ_PER_STA)
        tid = 0;

    return &bl_hw->txq[bl_txq_sta_idx(sta, tid)];
}

struct bl_txq *bl_txq_vif_get(struct bl_vif *vif, u8 type)
{
    if (type > NX_UNK_TXQ_TYPE)
        type = NX_BCMC_TXQ_TYPE;

    return &vif->bl_hw->txq[bl_txq_vif_idx(vif, type)];
}

static inline struct bl_sta *bl_txq_2_sta(struct bl_txq *txq)
{
    return txq->sta;
}

/******************************************************************************
 * Init/Deinit functions
 *****************************************************************************/
/**
 * bl_txq_init - Initialize a TX queue
 *
 * @txq: TX queue to be initialized
 * @idx: TX queue index
 * @status: TX queue initial status
 * @hwq: Associated HW queue
 * @ndev: Net device this queue belongs to
 *        (may be null for non netdev txq)
 *
 * Each queue is initialized with the credit of @NX_TXQ_INITIAL_CREDITS.
 */
static void bl_txq_init(struct bl_txq *txq, int idx, u8 status,
                          struct bl_hwq *hwq, int tid,
                          struct bl_sta *sta, struct net_device *ndev)
{
    int i;

    txq->idx = idx;
    txq->status = status;
    txq->credits = NX_TXQ_INITIAL_CREDITS;
    txq->pkt_sent = 0;
    skb_queue_head_init(&txq->sk_list);
    txq->last_retry_skb = NULL;
    txq->nb_retry = 0;
    txq->hwq = hwq;
    txq->sta = sta;
    
    for (i = 0; i < CONFIG_USER_MAX ; i++)
        txq->pkt_pushed[i] = 0;
        
    txq->push_limit = 0;
    txq->tid = tid;
#ifdef CONFIG_MAC80211_TXQ
    txq->nb_ready_mac80211 = 0;
#endif
    txq->ps_id = LEGACY_PS_ID;
    
    if (idx < NX_FIRST_VIF_TXQ_IDX) {
        int sta_idx = sta->sta_idx;
        int tid = idx - (sta_idx * NX_NB_TXQ_PER_STA);
        if (tid < NX_NB_TID_PER_STA)
            txq->ndev_idx = NX_STA_NDEV_IDX(tid, sta_idx);
        else
            txq->ndev_idx = NDEV_NO_TXQ;
    } else if (idx < NX_FIRST_UNK_TXQ_IDX) {
        txq->ndev_idx = NX_BCMC_TXQ_NDEV_IDX;
    } else {
        txq->ndev_idx = NDEV_NO_TXQ;
    }
    
    txq->ndev = ndev;
#ifdef CONFIG_BL_AMSDUS_TX
    txq->amsdu = NULL;
    txq->amsdu_len = 0;
#endif /* CONFIG_BL_AMSDUS_TX */
}

/**
 * bl_txq_drop_skb - Drop the first buffer queued for a TXQ
 *
 * @txq TXQ to drop packet from.
 */
void bl_txq_drop_skb(struct bl_txq *txq, struct bl_hw *bl_hw)
{
    struct sk_buff *skb = skb_dequeue(&txq->sk_list);
    struct bl_sw_txhdr *sw_txhdr;
    unsigned long queued_time = 0;

    if (!skb)
        return;

    sw_txhdr = ((struct bl_txhdr *)skb->data)->sw_hdr;

#ifdef CONFIG_BL_FULLMAC
    queued_time = jiffies - sw_txhdr->jiffies;
#endif
    
#ifdef CONFIG_BL_AMSDUS_TX
    if (sw_txhdr->desc.host.packet_cnt > 1) {
        struct bl_amsdu_txhdr *amsdu_txhdr;
        
        list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
            dma_unmap_single(bl_hw->dev, amsdu_txhdr->dma_addr,
                             amsdu_txhdr->map_len, DMA_TO_DEVICE);
            dev_kfree_skb_any(amsdu_txhdr->skb);
        }
        
#ifdef CONFIG_BL_FULLMAC
        if (txq->amsdu == sw_txhdr)
            txq->amsdu = NULL;
#endif
    }
#endif

    kmem_cache_free(bl_hw->sw_txhdr_cache, sw_txhdr);
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    //TODO: free swtxhdr
#endif

    if (txq->nb_retry) {
        txq->nb_retry--;
        if (txq->nb_retry == 0) {
            WARN(skb != txq->last_retry_skb,
                 "last dropped retry buffer is not the expected one");
            txq->last_retry_skb = NULL;
        }
    }

    dev_kfree_skb_any(skb);
}

/**
 * bl_txq_flush - Flush all buffers queued for a TXQ
 *
 * @bl_hw: main driver data
 * @txq: txq to flush
 */
void bl_txq_flush(struct bl_hw *bl_hw, struct bl_txq *txq)
{
    int i, pushed = 0;

    while(!skb_queue_empty(&txq->sk_list)) {
        bl_txq_drop_skb(txq, bl_hw);
    }

    for (i = 0; i < CONFIG_USER_MAX; i++) {
        pushed += txq->pkt_pushed[i];
    }

    if (pushed)
        BL_DBG("TXQ[%d]: %d skb still pushed to the FW", txq->idx, pushed);
}

/**
 * bl_txq_deinit - De-initialize a TX queue
 *
 * @bl_hw: Driver main data
 * @txq: TX queue to be de-initialized
 * Any buffer stuck in a queue will be freed.
 */
static void bl_txq_deinit(struct bl_hw *bl_hw, struct bl_txq *txq)
{
    BL_DBG(BL_FN_ENTRY_STR);
    
    if (txq->idx == TXQ_INACTIVE)
        return;

    spin_lock_bh(&bl_hw->tx_lock);
    bl_txq_del_from_hw_list(txq);
    txq->idx = TXQ_INACTIVE;
    spin_unlock_bh(&bl_hw->tx_lock);

    bl_txq_flush(bl_hw, txq);
}

/**
 * bl_txq_vif_init - Initialize all TXQ linked to a vif
 *
 * @bl_hw: main driver data
 * @bl_vif: Pointer on VIF
 * @status: Intial txq status
 *
 * Softmac : 1 VIF TXQ per HWQ
 *
 * Fullmac : 1 VIF TXQ for BC/MC
 *           1 VIF TXQ for MGMT to unknown STA
 */
void bl_txq_vif_init(struct bl_hw *bl_hw, struct bl_vif *bl_vif, u8 status)
{
    struct bl_txq *txq;
    int idx;

    BL_DBG(BL_FN_ENTRY_STR);

    txq = bl_txq_vif_get(bl_vif, NX_BCMC_TXQ_TYPE);
    idx = bl_txq_vif_idx(bl_vif, NX_BCMC_TXQ_TYPE);
    bl_txq_init(txq, idx, status, &bl_hw->hwq[BL_HWQ_BE], 0,
                &bl_hw->sta_table[bl_vif->ap.bcmc_index], bl_vif->ndev);

    txq = bl_txq_vif_get(bl_vif, NX_UNK_TXQ_TYPE);
    idx = bl_txq_vif_idx(bl_vif, NX_UNK_TXQ_TYPE);
    bl_txq_init(txq, idx, status, &bl_hw->hwq[BL_HWQ_VO], TID_MGT,
                NULL, bl_vif->ndev);
}

/**
 * bl_txq_vif_deinit - Deinitialize all TXQ linked to a vif
 *
 * @bl_hw: main driver data
 * @bl_vif: Pointer on VIF
 */
void bl_txq_vif_deinit(struct bl_hw * bl_hw, struct bl_vif *bl_vif)
{
    struct bl_txq *txq;

    BL_DBG(BL_FN_ENTRY_STR);
    
    txq = bl_txq_vif_get(bl_vif, NX_BCMC_TXQ_TYPE);
    bl_txq_deinit(bl_hw, txq);

    txq = bl_txq_vif_get(bl_vif, NX_UNK_TXQ_TYPE);
    bl_txq_deinit(bl_hw, txq);
}


/**
 * bl_txq_sta_init - Initialize TX queues for a STA
 *
 * @bl_hw: Main driver data
 * @bl_sta: STA for which tx queues need to be initialized
 * @status: Intial txq status
 *
 * This function initialize all the TXQ associated to a STA.
 * Softmac : 1 TXQ per TID
 *
 * Fullmac : 1 TXQ per TID (limited to 8)
 *           1 TXQ for MGMT
 */
void bl_txq_sta_init(struct bl_hw *bl_hw, struct bl_sta *bl_sta, u8 status)
{
    struct bl_txq *txq;
    int tid, idx;
    struct bl_vif *bl_vif = bl_hw->vif_table[bl_sta->vif_idx];

    BL_DBG(BL_FN_ENTRY_STR);
    idx = bl_txq_sta_idx(bl_sta, 0);

    foreach_sta_txq(bl_sta, txq, tid, bl_hw) {
        bl_txq_init(txq, idx, status, &bl_hw->hwq[bl_tid2hwq[tid]], tid,
                    bl_sta, bl_vif->ndev);
                    
        txq->ps_id = bl_sta->uapsd_tids & (1 << tid) ? UAPSD_ID : LEGACY_PS_ID;
        idx++;
    }

    bl_ipc_sta_buffer_init(bl_hw, bl_sta->sta_idx);
}

/**
 * bl_txq_sta_deinit - Deinitialize TX queues for a STA
 *
 * @bl_hw: Main driver data
 * @bl_sta: STA for which tx queues need to be deinitialized
 */
void bl_txq_sta_deinit(struct bl_hw *bl_hw, struct bl_sta *bl_sta)
{
    struct bl_txq *txq;
    int tid;

    BL_DBG(BL_FN_ENTRY_STR);
    
    foreach_sta_txq(bl_sta, txq, tid, bl_hw) {
        bl_txq_deinit(bl_hw, txq);
    }
}

#ifdef CONFIG_BL_FULLMAC
/**
 * bl_txq_unk_vif_init - Initialize TXQ for unknown STA linked to a vif
 *
 * @bl_vif: Pointer on VIF
 */
void bl_txq_unk_vif_init(struct bl_vif *bl_vif)
{
    struct bl_hw *bl_hw = bl_vif->bl_hw;
    struct bl_txq *txq;
    int idx;

    txq = bl_txq_vif_get(bl_vif, NX_UNK_TXQ_TYPE);
    idx = bl_txq_vif_idx(bl_vif, NX_UNK_TXQ_TYPE);
    bl_txq_init(txq, idx, 0, &bl_hw->hwq[BL_HWQ_VO], TID_MGT, NULL, bl_vif->ndev);
}

/**
 * bl_txq_unk_vif_deinit - Deinitialize TXQ for unknown STA linked to a vif
 *
 * @bl_vif: Pointer on VIF
 */
void bl_txq_unk_vif_deinit(struct bl_vif *bl_vif)
{
    struct bl_txq *txq;

    txq = bl_txq_vif_get(bl_vif, NX_UNK_TXQ_TYPE);
    bl_txq_deinit(bl_vif->bl_hw, txq);
}

/**
 * bl_txq_offchan_init - Initialize TX queue for the transmission on a offchannel
 *
 * @vif: Interface for which the queue has to be initialized
 *
 * NOTE: Offchannel txq is only active for the duration of the ROC
 */
void bl_txq_offchan_init(struct bl_vif *bl_vif)
{
    struct bl_hw *bl_hw = bl_vif->bl_hw;
    struct bl_txq *txq;

    txq = &bl_hw->txq[NX_OFF_CHAN_TXQ_IDX];
    bl_txq_init(txq, NX_OFF_CHAN_TXQ_IDX, BL_TXQ_STOP_CHAN,
                &bl_hw->hwq[BL_HWQ_VO], TID_MGT, NULL, bl_vif->ndev);
}

/**
 * bl_deinit_offchan_txq - Deinitialize TX queue for offchannel
 *
 * @vif: Interface that manages the STA
 *
 * This function deintialize txq for one STA.
 * Any buffer stuck in a queue will be freed.
 */
void bl_txq_offchan_deinit(struct bl_vif *bl_vif)
{
    struct bl_txq *txq;

    txq = &bl_vif->bl_hw->txq[NX_OFF_CHAN_TXQ_IDX];
    bl_txq_deinit(bl_vif->bl_hw, txq);
}


/**
 * bl_txq_tdls_vif_init - Initialize TXQ vif for TDLS
 *
 * @bl_vif: Pointer on VIF
 */
void bl_txq_tdls_vif_init(struct bl_vif *bl_vif)
{
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    if (!(bl_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
        return;

    bl_txq_unk_vif_init(bl_vif);
}

/**
 * bl_txq_tdls_vif_deinit - Deinitialize TXQ vif for TDLS
 *
 * @bl_vif: Pointer on VIF
 */
void bl_txq_tdls_vif_deinit(struct bl_vif *bl_vif)
{
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    if (!(bl_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
        return;

    bl_txq_unk_vif_deinit(bl_vif);
}

/**
 * bl_txq_drop_old_traffic - Drop pkt queued for too long in a TXQ
 *
 * @txq: TXQ to process
 * @bl_hw: Driver main data
 * @skb_timeout: Max queue duration, in jiffies, for this queue
 * @dropped: Updated to inidicate if at least one skb was dropped
 *
 * @return Whether there is still pkt queued in this queue.
 */
static bool bl_txq_drop_old_traffic(struct bl_txq *txq, struct bl_hw *bl_hw,
                                       unsigned long skb_timeout, bool *dropped)
{
    struct sk_buff *skb, *skb_next;
    bool pkt_queued = false;

    if (txq->idx == TXQ_INACTIVE)
        return false;

    skb_queue_walk_safe(&txq->sk_list, skb, skb_next) {
        struct bl_sw_txhdr *sw_txhdr = ((struct bl_txhdr *)skb->data)->sw_hdr;

        if (!time_after(jiffies, sw_txhdr->jiffies + skb_timeout)) {
            pkt_queued = true;
            break;
        }

        *dropped = true;
        bl_txq_drop_skb(txq, bl_hw);
        if (txq->sta && txq->sta->ps.active) {
            txq->sta->ps.pkt_ready[txq->ps_id]--;
            
            if (txq->sta->ps.pkt_ready[txq->ps_id] == 0)
                bl_set_traffic_status(bl_hw, txq->sta, false, txq->ps_id);

            // drop packet during PS service period ...
            if (txq->sta->ps.sp_cnt[txq->ps_id]) {
                txq->sta->ps.sp_cnt[txq->ps_id] --;
                
                if (txq->push_limit)
                    txq->push_limit--;
                    
                if (WARN(((txq->ps_id == UAPSD_ID) &&
                          (txq->sta->ps.sp_cnt[txq->ps_id] == 0)),
                         "Drop last packet of UAPSD service period")) {
                    // TODO: inform FW to end SP
                }
            }
        }
    }

#ifdef CONFIG_BL_FULLMAC
    /* restart netdev queue if number no more queued buffer */
    if (unlikely(txq->status & BL_TXQ_NDEV_FLOW_CTRL) &&
        skb_queue_empty(&txq->sk_list)) {
        txq->status &= ~BL_TXQ_NDEV_FLOW_CTRL;
        netif_wake_subqueue(txq->ndev, txq->ndev_idx);
    }
#endif /* CONFIG_BL_FULLMAC */

    return pkt_queued;
}

/**
 * bl_txq_drop_ap_vif_old_traffic - Drop pkt queued for too long in TXQs
 * linked to an "AP" vif (AP, MESH, P2P_GO)
 *
 * @vif: Vif to process
 * @return Whether there is still pkt queued in any TXQ.
 */
static bool bl_txq_drop_ap_vif_old_traffic(struct bl_vif *vif)
{
    struct bl_sta *sta;
    unsigned long timeout = (vif->ap.bcn_interval * HZ * 3) >> 10;
    bool pkt_queued = false;
    bool pkt_dropped = false;

    // Should never be needed but still check VIF queues
    bl_txq_drop_old_traffic(bl_txq_vif_get(vif, NX_BCMC_TXQ_TYPE),
                              vif->bl_hw, BL_TXQ_MAX_QUEUE_JIFFIES, &pkt_dropped);
    bl_txq_drop_old_traffic(bl_txq_vif_get(vif, NX_UNK_TXQ_TYPE),
                              vif->bl_hw, BL_TXQ_MAX_QUEUE_JIFFIES, &pkt_dropped);
    WARN(pkt_dropped, "Dropped packet in BCMC/UNK queue");

    list_for_each_entry(sta, &vif->ap.sta_list, list) {
        struct bl_txq *txq;
        int tid;
        foreach_sta_txq(sta, txq, tid, vif->bl_hw) {
            pkt_queued |= bl_txq_drop_old_traffic(txq, vif->bl_hw,
                                                  timeout * sta->listen_interval,
                                                  &pkt_dropped);
        }
    }

    return pkt_queued;
}

/**
 * bl_txq_drop_sta_vif_old_traffic - Drop pkt queued for too long in TXQs
 * linked to a "STA" vif. In theory this should not be required as there is no
 * case where traffic can accumulate in a STA interface.
 *
 * @vif: Vif to process
 * @return Whether there is still pkt queued in any TXQ.
 */
static bool bl_txq_drop_sta_vif_old_traffic(struct bl_vif *vif)
{
    struct bl_txq *txq;
    bool pkt_queued = false, pkt_dropped = false;
    int tid;

    if (vif->tdls_status == TDLS_LINK_ACTIVE) {
        txq = bl_txq_vif_get(vif, NX_UNK_TXQ_TYPE);
        pkt_queued |= bl_txq_drop_old_traffic(txq, vif->bl_hw,
                                                BL_TXQ_MAX_QUEUE_JIFFIES,
                                                &pkt_dropped);
        foreach_sta_txq(vif->sta.tdls_sta, txq, tid, vif->bl_hw) {
            pkt_queued |= bl_txq_drop_old_traffic(txq, vif->bl_hw,
                                                  BL_TXQ_MAX_QUEUE_JIFFIES,
                                                  &pkt_dropped);
        }
    }

    if (vif->sta.ap) {
        foreach_sta_txq(vif->sta.ap, txq, tid, vif->bl_hw) {
            pkt_queued |= bl_txq_drop_old_traffic(txq, vif->bl_hw,
                                                  BL_TXQ_MAX_QUEUE_JIFFIES,
                                                  &pkt_dropped);
        }
    }

    if (pkt_dropped)
        netdev_warn(vif->ndev, "Dropped packet in STA interface TXQs");
    return pkt_queued;
}

/**
 * bl_txq_cleanup_timer_cb - callack for TXQ cleaup timer
 * Used to prevent pkt to accumulate in TXQ. The main use case is for AP
 * interface with client in Power Save mode but just in case all TXQs are
 * checked.
 *
 * @t: timer structure
 */
static void bl_txq_cleanup_timer_cb(struct timer_list *t)
{
    struct bl_hw *bl_hw = from_timer(bl_hw, t, txq_cleanup);
    struct bl_vif *vif;
    bool pkt_queue = false;

    list_for_each_entry(vif, &bl_hw->vifs, list) {
        switch (BL_VIF_TYPE(vif)) {
            case NL80211_IFTYPE_AP:
            case NL80211_IFTYPE_P2P_GO:
            case NL80211_IFTYPE_MESH_POINT:
                pkt_queue |= bl_txq_drop_ap_vif_old_traffic(vif);
                break;
            case NL80211_IFTYPE_STATION:
            case NL80211_IFTYPE_P2P_CLIENT:
                 pkt_queue |= bl_txq_drop_sta_vif_old_traffic(vif);
                 break;
            case NL80211_IFTYPE_AP_VLAN:
            case NL80211_IFTYPE_MONITOR:
            default:
                continue;
        }
    }

    if (pkt_queue)
        mod_timer(t, jiffies + BL_TXQ_CLEANUP_INTERVAL);
}

/**
 * bl_txq_start_cleanup_timer - Start 'cleanup' timer if not started
 *
 * @bl_hw: Driver main data
 */
void bl_txq_start_cleanup_timer(struct bl_hw *bl_hw, struct bl_sta *sta)
{
    if (sta && !is_multicast_sta(sta->sta_idx) &&
        !timer_pending(&bl_hw->txq_cleanup))
        mod_timer(&bl_hw->txq_cleanup, jiffies + BL_TXQ_CLEANUP_INTERVAL);
}

/**
 * bl_txq_prepare - Global initialization of txq
 *
 * @bl_hw: Driver main data
 */
void bl_txq_prepare(struct bl_hw *bl_hw)
{
    int i;

    for (i = 0; i < NX_NB_TXQ; i++) {
        bl_hw->txq[i].idx = TXQ_INACTIVE;
    }

    timer_setup(&bl_hw->txq_cleanup, bl_txq_cleanup_timer_cb, 0);
}

#endif

/******************************************************************************
 * Start/Stop functions
 *****************************************************************************/
/**
 * bl_txq_add_to_hw_list - Add TX queue to a HW queue schedule list.
 *
 * @txq: TX queue to add
 *
 * Add the TX queue if not already present in the HW queue list.
 * To be called with tx_lock hold
 */
void bl_txq_add_to_hw_list(struct bl_txq *txq)
{
    if (!(txq->status & BL_TXQ_IN_HWQ_LIST)) {
        txq->status |= BL_TXQ_IN_HWQ_LIST;
        txq->nb_once_sent = 0;
        list_add_tail(&txq->sched_list, &txq->hwq->list);
    }
}

/**
 * bl_txq_del_from_hw_list - Delete TX queue from a HW queue schedule list.
 *
 * @txq: TX queue to delete
 *
 * Remove the TX queue from the HW queue list if present.
 * To be called with tx_lock hold
 */
void bl_txq_del_from_hw_list(struct bl_txq *txq)
{
    if (txq->status & BL_TXQ_IN_HWQ_LIST) {
        txq->status &= ~BL_TXQ_IN_HWQ_LIST;
        list_del(&txq->sched_list);
        txq->nb_once_sent = 0;
    }
}

/**
 * bl_txq_skb_ready - Check if skb are available for the txq
 *
 * @txq: Pointer on txq
 * @return True if there are buffer ready to be pushed on this txq,
 * false otherwise
 */
static inline bool bl_txq_skb_ready(struct bl_txq *txq)
{
#ifdef CONFIG_MAC80211_TXQ
    if (txq->nb_ready_mac80211 != NOT_MAC80211_TXQ)
        return ((txq->nb_ready_mac80211 > 0) || !skb_queue_empty(&txq->sk_list));
    else
#endif
    return !skb_queue_empty(&txq->sk_list);
}

/**
 * bl_txq_start - Try to Start one TX queue
 *
 * @txq: TX queue to start
 * @reason: reason why the TX queue is started (among BL_TXQ_STOP_xxx)
 *
 * Re-start the TX queue for one reason.
 * If after this the txq is no longer stopped and some buffers are ready,
 * the TX queue is also added to HW queue list.
 * To be called with tx_lock hold
 */
void bl_txq_start(struct bl_txq *txq, u16 reason)
{
    BL_DBG(BL_FN_ENTRY_STR);

    BUG_ON(txq==NULL);
    if (txq->idx != TXQ_INACTIVE && (txq->status & reason))
    {
        txq->status &= ~reason;
        if (!bl_txq_is_stopped(txq) && bl_txq_skb_ready(txq))
            bl_txq_add_to_hw_list(txq);
    }
}

/**
 * bl_txq_stop - Stop one TX queue
 *
 * @txq: TX queue to stop
 * @reason: reason why the TX queue is stopped (among BL_TXQ_STOP_xxx)
 *
 * Stop the TX queue. It will remove the TX queue from HW queue list
 * To be called with tx_lock hold
 */
void bl_txq_stop(struct bl_txq *txq, u16 reason)
{
    BL_DBG(BL_FN_ENTRY_STR);

    BUG_ON(txq==NULL);
    if (txq->idx != TXQ_INACTIVE)
    {
        txq->status |= reason;
        bl_txq_del_from_hw_list(txq);
    }
}


/**
 * bl_txq_sta_start - Start all the TX queue linked to a STA
 *
 * @sta: STA whose TX queues must be re-started
 * @reason: Reason why the TX queue are restarted (among BL_TXQ_STOP_xxx)
 * @bl_hw: Driver main data
 *
 * This function will re-start all the TX queues of the STA for the reason
 * specified. It can be :
 * - BL_TXQ_STOP_STA_PS: the STA is no longer in power save mode
 * - BL_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - BL_TXQ_STOP_CHAN: the STA's VIF is now on the current active channel
 *
 * Any TX queue with buffer ready and not Stopped for other reasons, will be
 * added to the HW queue list
 * To be called with tx_lock hold
 */
void bl_txq_sta_start(struct bl_sta *bl_sta, u16 reason
#ifdef CONFIG_BL_FULLMAC
                        , struct bl_hw *bl_hw
#endif
                        )
{
    struct bl_txq *txq;
    int tid;

    foreach_sta_txq(bl_sta, txq, tid, bl_hw) {
        bl_txq_start(txq, reason);
    }
}


/**
 * bl_stop_sta_txq - Stop all the TX queue linked to a STA
 *
 * @sta: STA whose TX queues must be stopped
 * @reason: Reason why the TX queue are stopped (among BL_TX_STOP_xxx)
 * @bl_hw: Driver main data
 *
 * This function will stop all the TX queues of the STA for the reason
 * specified. It can be :
 * - BL_TXQ_STOP_STA_PS: the STA is in power save mode
 * - BL_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - BL_TXQ_STOP_CHAN: the STA's VIF is not on the current active channel
 *
 * Any TX queue present in a HW queue list will be removed from this list.
 * To be called with tx_lock hold
 */
void bl_txq_sta_stop(struct bl_sta *bl_sta, u16 reason
#ifdef CONFIG_BL_FULLMAC
                       , struct bl_hw *bl_hw
#endif
                       )
{
    struct bl_txq *txq;
    int tid;

    if (!bl_sta)
        return;

    foreach_sta_txq(bl_sta, txq, tid, bl_hw) {
        bl_txq_stop(txq, reason);
    }
}

void bl_txq_tdls_sta_start(struct bl_vif *bl_vif, u16 reason,
                                struct bl_hw *bl_hw)
{
    spin_lock_bh(&bl_hw->tx_lock);

    if (bl_vif->sta.tdls_sta)
        bl_txq_sta_start(bl_vif->sta.tdls_sta, reason, bl_hw);

    spin_unlock_bh(&bl_hw->tx_lock);
}

void bl_txq_tdls_sta_stop(struct bl_vif *bl_vif, u16 reason,
                               struct bl_hw *bl_hw)
{
    spin_lock_bh(&bl_hw->tx_lock);

    if (bl_vif->sta.tdls_sta)
        bl_txq_sta_stop(bl_vif->sta.tdls_sta, reason, bl_hw);

    spin_unlock_bh(&bl_hw->tx_lock);
}

#ifdef CONFIG_BL_FULLMAC
static inline void bl_txq_vif_for_each_sta(struct bl_hw *bl_hw, 
                               struct bl_vif *bl_vif,
                               void (*f)(struct bl_sta *, u16, struct bl_hw *),
                               u16 reason)
{
    switch (BL_VIF_TYPE(bl_vif)) {
    case NL80211_IFTYPE_STATION:
    case NL80211_IFTYPE_P2P_CLIENT:
    {
        if (bl_vif->tdls_status == TDLS_LINK_ACTIVE)
            f(bl_vif->sta.tdls_sta, reason, bl_hw);
        if (!WARN_ON(bl_vif->sta.ap == NULL))
            f(bl_vif->sta.ap, reason, bl_hw);
        break;
    }
    case NL80211_IFTYPE_AP_VLAN:
        bl_vif = bl_vif->ap_vlan.master;
    case NL80211_IFTYPE_AP:
    case NL80211_IFTYPE_MESH_POINT:
    case NL80211_IFTYPE_P2P_GO:
    {
        struct bl_sta *sta;

        list_for_each_entry(sta, &bl_vif->ap.sta_list, list) {
            f(sta, reason, bl_hw);
        }
        break;
    }
    default:
        BUG();
        break;
    }
}

#endif

/**
 * bl_txq_vif_start - START TX queues of all STA associated to the vif
 *                      and vif's TXQ
 *
 * @vif: Interface to start
 * @reason: Start reason (BL_TXQ_STOP_CHAN or BL_TXQ_STOP_VIF_PS)
 * @bl_hw: Driver main data
 *
 * Iterate over all the STA associated to the vif and re-start them for the
 * reason @reason
 * Take tx_lock
 */
void bl_txq_vif_start(struct bl_vif *bl_vif, u16 reason,
                         struct bl_hw *bl_hw)
{
    struct bl_txq *txq;

    spin_lock_bh(&bl_hw->tx_lock);

    //Reject if monitor interface
    if (bl_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
        goto end;

    if (bl_vif->roc_tdls && bl_vif->sta.tdls_sta && bl_vif->sta.tdls_sta->tdls.chsw_en) {
        bl_txq_sta_start(bl_vif->sta.tdls_sta, reason, bl_hw);
    }
    
    if (!bl_vif->roc_tdls) {
        bl_txq_vif_for_each_sta(bl_hw, bl_vif, bl_txq_sta_start, reason);
    }

    txq = bl_txq_vif_get(bl_vif, NX_BCMC_TXQ_TYPE);
    bl_txq_start(txq, reason);
    txq = bl_txq_vif_get(bl_vif, NX_UNK_TXQ_TYPE);
    bl_txq_start(txq, reason);

end:

    spin_unlock_bh(&bl_hw->tx_lock);
}


/**
 * bl_txq_vif_stop - STOP TX queues of all STA associated to the vif
 *
 * @vif: Interface to stop
 * @arg: Stop reason (BL_TXQ_STOP_CHAN or BL_TXQ_STOP_VIF_PS)
 * @bl_hw: Driver main data
 *
 * Iterate over all the STA associated to the vif and stop them for the
 * reason BL_TXQ_STOP_CHAN or BL_TXQ_STOP_VIF_PS
 * Take tx_lock
 */
void bl_txq_vif_stop(struct bl_vif *bl_vif, u16 reason,
                         struct bl_hw *bl_hw)
{
    struct bl_txq *txq;

    spin_lock_bh(&bl_hw->tx_lock);

    //Reject if monitor interface
    if (bl_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
        goto end;

    bl_txq_vif_for_each_sta(bl_hw, bl_vif, bl_txq_sta_stop, reason);

    txq = bl_txq_vif_get(bl_vif, NX_BCMC_TXQ_TYPE);
    bl_txq_stop(txq, reason);
    txq = bl_txq_vif_get(bl_vif, NX_UNK_TXQ_TYPE);
    bl_txq_stop(txq, reason);

end:

    spin_unlock_bh(&bl_hw->tx_lock);
}

#ifdef CONFIG_BL_FULLMAC
/**
 * bl_start_offchan_txq - START TX queue for offchannel frame
 *
 * @bl_hw: Driver main data
 */
void bl_txq_offchan_start(struct bl_hw *bl_hw)
{
    struct bl_txq *txq;
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif
    
    txq = &bl_hw->txq[NX_OFF_CHAN_TXQ_IDX];

    BL_TX_LOCK(&bl_hw->tx_lock, flags);
    bl_txq_start(txq, BL_TXQ_STOP_CHAN);
    BL_TX_UNLOCK(&bl_hw->tx_lock, flags);
}

/**
 * bl_switch_vif_sta_txq - Associate TXQ linked to a STA to a new vif
 *
 * @sta: STA whose txq must be switched
 * @old_vif: Vif currently associated to the STA (may no longer be active)
 * @new_vif: vif which should be associated to the STA for now on
 *
 * This function will switch the vif (i.e. the netdev) associated to all STA's
 * TXQ. This is used when AP_VLAN interface are created.
 * If one STA is associated to an AP_vlan vif, it will be moved from the master
 * AP vif to the AP_vlan vif.
 * If an AP_vlan vif is removed, then STA will be moved back to mastert AP vif.
 *
 */
void bl_txq_sta_switch_vif(struct bl_sta *sta, struct bl_vif *old_vif,
                                 struct bl_vif *new_vif)
{
    struct bl_hw *bl_hw = new_vif->bl_hw;
    struct bl_txq *txq;
    int i;

    /* start TXQ on the new interface, and update ndev field in txq */
    if (!netif_carrier_ok(new_vif->ndev))
        netif_carrier_on(new_vif->ndev);
        
    txq = bl_txq_sta_get(sta, 0, bl_hw);
    for (i = 0; i < NX_NB_TID_PER_STA; i++, txq++) {
        txq->ndev = new_vif->ndev;
        netif_wake_subqueue(txq->ndev, txq->ndev_idx);
    }
}
#endif /* CONFIG_BL_FULLMAC */

/******************************************************************************
 * TXQ queue/schedule functions
 *****************************************************************************/
/**
 * bl_txq_queue_skb - Queue a buffer in a TX queue
 *
 * @skb: Buffer to queue
 * @txq: TX Queue in which the buffer must be added
 * @bl_hw: Driver main data
 * @retry: Should it be queued in the retry list
 *
 * @return: Retrun 1 if txq has been added to hwq list, 0 otherwise
 *
 * Add a buffer in the buffer list of the TX queue
 * and add this TX queue in the HW queue list if the txq is not stopped.
 * If this is a retry packet it is added after the last retry packet or at the
 * beginning if there is no retry packet queued.
 *
 * If the STA is in PS mode and this is the first packet queued for this txq
 * update TIM.
 *
 * To be called with tx_lock hold
 */
int bl_txq_queue_skb(struct sk_buff *skb, struct bl_txq *txq,
                            struct bl_hw *bl_hw,  bool retry)
{
#ifndef CONFIG_BL_USB
    unsigned long flags;
#endif

#ifdef CONFIG_BL_FULLMAC
    if (unlikely(txq->sta && txq->sta->ps.active)) {
        txq->sta->ps.pkt_ready[txq->ps_id]++;
        
        if (txq->sta->ps.pkt_ready[txq->ps_id] == 1) {
            bl_set_traffic_status(bl_hw, txq->sta, true, txq->ps_id);
        }
    }
#endif

    BL_TX_LOCK(&bl_hw->tx_lock, flags);

    if (!retry) {
        /* add buffer in the sk_list */
        skb_queue_tail(&txq->sk_list, skb);
        
#ifdef CONFIG_BL_FULLMAC
        //to update for SOFTMAC
        //bl_ipc_sta_buffer(bl_hw, txq->sta, txq->tid,
        //                     ((struct bl_txhdr *)skb->data)->sw_hdr->frame_len);
        //bl_txq_start_cleanup_timer(bl_hw, txq->sta);
#endif
    } else {
        if (txq->last_retry_skb)
            skb_append(txq->last_retry_skb, skb, &txq->sk_list);
        else
            skb_queue_head(&txq->sk_list, skb);

        txq->last_retry_skb = skb;
        txq->nb_retry++;
    }

    /* Flowctrl corresponding netdev queue if needed */
#ifdef CONFIG_BL_FULLMAC
    /* If too many buffer are queued for this TXQ stop netdev queue */
    if ((txq->ndev_idx != NDEV_NO_TXQ) &&
        (skb_queue_len(&txq->sk_list) > BL_NDEV_FLOW_CTRL_STOP)) 
    {
        BL_TRACE(TRACE_TX, "stop: txq[%d], txq_netdev_idx[%d], skb_len[%d], txq->ndev->num_tx_queues[%d]\n", 
                 txq->idx, txq->ndev_idx, skb_queue_len(&txq->sk_list), 
                 txq->ndev->num_tx_queues);
                 
        txq->status |= BL_TXQ_NDEV_FLOW_CTRL;
        
        BL_DBG("%s:Flowctrl: skb_len: %d, txq->idx[%d], txq->ndev_idx[%d], txq_status=0x%x\n", 
               __func__, skb_queue_len(&txq->sk_list), txq->idx,
               txq->ndev_idx, txq->status);
               
        netif_stop_subqueue(txq->ndev, txq->ndev_idx);
    }
#else /* ! CONFIG_BL_FULLMAC */

    if (!retry && ++txq->hwq->len == txq->hwq->len_stop) {
         ieee80211_stop_queue(bl_hw->hw, txq->hwq->id);
         bl_hw->stats.queues_stops++;
     }
#endif /* CONFIG_BL_FULLMAC */

    /* add it in the hwq list if not stopped and not yet present */
    if (!bl_txq_is_stopped(txq)) {
        BL_DBG("add txq into hwq, txq->status=0x%x, txq->index=%d, cnt=%d\n",
               txq->status, txq->idx, skb_queue_len(&txq->sk_list));
               
        bl_txq_add_to_hw_list(txq);
        BL_TX_UNLOCK(&bl_hw->tx_lock, flags);
        
        return 1;
    } else {
        BL_DBG("stopped txq->status=0x%x, txq->index=%d\n", txq->status, txq->idx);
    }

    BL_TX_UNLOCK(&bl_hw->tx_lock, flags);
    
    return 0;
}

/**
 * bl_txq_confirm_any - Process buffer confirmed by fw
 *
 * @bl_hw: Driver main data
 * @txq: TX Queue
 * @hwq: HW Queue
 * @sw_txhdr: software descriptor of the confirmed packet
 *
 * Process a buffer returned by the fw. It doesn't check buffer status
 * and only does systematic counter update:
 * - hw credit
 * - buffer pushed to fw
 *
 * To be called with tx_lock hold
 */
void bl_txq_confirm_any(struct bl_hw *bl_hw, struct bl_txq *txq,
                              struct bl_hwq *hwq, struct bl_sw_txhdr *sw_txhdr)
{
    int user = 0;
#ifdef CONFIG_BL_MUMIMO_TX
    int group_id;

    user = BL_MUMIMO_INFO_POS_ID(sw_txhdr->desc.host.mumimo_info);
    group_id = BL_MUMIMO_INFO_GROUP_ID(sw_txhdr->desc.host.mumimo_info);

    if ((txq->idx != TXQ_INACTIVE) &&
        (txq->pkt_pushed[user] == 1) &&
        (txq->status & BL_TXQ_STOP_MU_POS))
        bl_txq_start(txq, BL_TXQ_STOP_MU_POS);

#endif /* CONFIG_BL_MUMIMO_TX */

    if (txq->pkt_pushed[user])
        txq->pkt_pushed[user]--;

    hwq->credits[user]++;
    bl_hw->stats.cfm_balance[hwq->id]--;
}

/******************************************************************************
 * HWQ processing
 *****************************************************************************/
static inline
bool bl_txq_take_mu_lock(struct bl_hw *bl_hw)
{
    bool res = false;
#ifdef CONFIG_BL_MUMIMO_TX
    if (bl_hw->mod_params->mutx)
        res = (down_trylock(&bl_hw->mu.lock) == 0);
#endif /* CONFIG_BL_MUMIMO_TX */
    return res;
}

static inline
void bl_txq_release_mu_lock(struct bl_hw *bl_hw)
{
#ifdef CONFIG_BL_MUMIMO_TX
    up(&bl_hw->mu.lock);
#endif /* CONFIG_BL_MUMIMO_TX */
}

static inline
void bl_txq_set_mu_info(struct bl_hw *bl_hw, struct bl_txq *txq,
                              int group_id, int pos)
{
#ifdef CONFIG_BL_MUMIMO_TX
    if (group_id) {
        txq->mumimo_info = group_id | (pos << 6);
        bl_mu_set_active_group(bl_hw, group_id);
    } else
        txq->mumimo_info = 0;
#endif /* CONFIG_BL_MUMIMO_TX */
}

static inline
s8 bl_txq_get_credits(struct bl_txq *txq)
{
    s8 cred = txq->credits;
    /* if destination is in PS mode, push_limit indicates the maximum
       number of packet that can be pushed on this txq. */
    if (txq->push_limit && (cred > txq->push_limit)) {
        cred = txq->push_limit;
    }
    return cred;
}

/**
 * skb_queue_extract - Extract buffer from skb list
 *
 * @list: List of skb to extract from
 * @head: List of skb to append to
 * @nb_elt: Number of skb to extract
 *
 * extract the first @nb_elt of @list and append them to @head
 * It is assume that:
 * - @list contains more that @nb_elt
 * - There is no need to take @list nor @head lock to modify them
 */
static inline void skb_queue_extract(struct sk_buff_head *list,
                                          struct sk_buff_head *head, int nb_elt)
{
    int i;
    struct sk_buff *first, *last, *ptr;

    first = ptr = list->next;
    for (i = 0; i < nb_elt; i++) {
        ptr = ptr->next;
    }
    last = ptr->prev;

    /* unlink nb_elt in list */
    list->qlen -= nb_elt;
    list->next = ptr;
    ptr->prev = (struct sk_buff *)list;

    /* append nb_elt at end of head */
    head->qlen += nb_elt;
    last->next = (struct sk_buff *)head;
    head->prev->next = first;
    first->prev = head->prev;
    head->prev = last;

    //BL_DBG("%s:first=%p, last=%p\n", __func__, first, last);
}


#ifdef CONFIG_MAC80211_TXQ
/**
 * bl_txq_mac80211_dequeue - Dequeue buffer from mac80211 txq and
 *                             add them to push list
 *
 * @bl_hw: Main driver data
 * @sk_list: List of buffer to push (initialized without lock)
 * @txq: TXQ to dequeue buffers from
 * @max: Max number of buffer to dequeue
 *
 * Dequeue buffer from mac80211 txq, prepare them for transmission and chain them
 * to the list of buffer to push.
 *
 * @return true if no more buffer are queued in mac80211 txq and false otherwise.
 */
static bool bl_txq_mac80211_dequeue(struct bl_hw *bl_hw,
                                               struct sk_buff_head *sk_list,
                                               struct bl_txq *txq, int max)
{
    struct ieee80211_txq *mac_txq;
    struct sk_buff *skb;
    unsigned long mac_txq_len;

    if (txq->nb_ready_mac80211 == NOT_MAC80211_TXQ)
        return true;

    mac_txq = container_of((void *)txq, struct ieee80211_txq, drv_priv);

    for (; max > 0; max--) {
        skb = bl_tx_dequeue_prep(bl_hw, mac_txq);
        if (skb == NULL)
            return true;

        __skb_queue_tail(sk_list, skb);
    }

    /* re-read mac80211 txq current length.
       It is mainly for debug purpose to trace dropped packet. There is no
       problems to have nb_ready_mac80211 != actual mac80211 txq length */
    ieee80211_txq_get_depth(mac_txq, &mac_txq_len, NULL);
        
    txq->nb_ready_mac80211 = mac_txq_len;

    return (txq->nb_ready_mac80211 == 0);
}
#endif

/**
 * bl_txq_get_skb_to_push - Get list of buffer to push for one txq
 *
 * @bl_hw: main driver data
 * @hwq: HWQ on wich buffers will be pushed
 * @txq: TXQ to get buffers from
 * @user: user postion to use
 * @sk_list_push: list to update
 *
 *
 * This function will returned a list of buffer to push for one txq.
 * It will take into account the number of credit of the HWQ for this user
 * position and TXQ (and push_limit).
 * This allow to get a list that can be pushed without having to test for
 * hwq/txq status after each push
 *
 * If a MU group has been selected for this txq, it will also update the
 * counter for the group
 *
 * @return true if txq no longer have buffer ready after the ones returned.
 *         false otherwise
 */
static
bool bl_txq_get_skb_to_push(struct bl_hw *bl_hw, struct bl_hwq *hwq,
                                    struct bl_txq *txq, int user,
                                    struct sk_buff_head *sk_list_push)
{
    int nb_ready = skb_queue_len(&txq->sk_list);
    int credits, unlink_cnt = 0;
    bool res = false;
    struct sk_buff *skb = NULL, *tmp = NULL;
#if defined CONFIG_BL_USB
    struct bl_usb_device *device = (struct bl_usb_device *)((bl_hw->plat)->priv);
    int tx_data_urb_pending = atomic_read(&device->tx_data_urb_pending);
#elif defined CONFIG_BL_SDIO
    int avail_port_num = 0;
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    u32 wr_bitmap = card->mp_wr_bitmap;
#endif    

    __skb_queue_head_init(sk_list_push);

#if defined CONFIG_BL_USB
    credits = BL_TX_DATA_URB - tx_data_urb_pending;

    BL_DBG("%s: nb_ready=%d, credits=%d, tx_data_urb_pending=%d, BL_TX_DATA_URB=%d, txq->idx=%d\n", 
           __func__, nb_ready, credits, tx_data_urb_pending,
           BL_TX_DATA_URB, txq->idx);
#elif defined CONFIG_BL_SDIO
    wr_bitmap &= DATA_PORT_MSK;
    while (wr_bitmap) {
        wr_bitmap = wr_bitmap & (wr_bitmap-1);
        avail_port_num++;
    }

    if (!(card->mp_wr_bitmap & (1 << card->curr_wr_port)) && 
        (card->mp_wr_bitmap & DATA_PORT_MSK)) 
    {
        BL_TRACE(TRACE_TX, "card->mp_wr_bitmap %x and curr_wr_port %d out of sync\n",
                 card->mp_wr_bitmap,card->curr_wr_port);
                 
        do {
            if (++(card->curr_wr_port) == MAX_PORT_NUM)
                card->curr_wr_port = card->reg->start_wr_port;
        } while(!(card->mp_wr_bitmap & (1 << card->curr_wr_port)));
    }

    credits = MIN(avail_port_num, MAX_AGG_BUF);

    BL_TRACE(TRACE_TX, "nb_ready=%d, txq->idx=%d, hwq->idx=%d, txq->status=0x%x, avail_port_num=%d, curr_port=%d, credits=%d\n",
            nb_ready, txq->idx, hwq->id, txq->status, avail_port_num,
            card->curr_wr_port, credits);
#endif

    if (credits == 0) {
         BL_DBG("credits=0, no credits or avail port\n");
         
         return true;
     }

    if (bl_hw->mod_params->tcp_ack_filter) {
        skb_queue_walk_safe(&txq->sk_list, skb, tmp) {
            if (bl_tcp_ack_process(bl_hw, skb, txq)) {
                __skb_unlink(skb, &txq->sk_list);
                unlink_cnt++;
            }
        }

        if(nb_ready == unlink_cnt)
            return true;
        else
            nb_ready -= unlink_cnt;
    }

#if defined CONFIG_BL_SDIO
    /*  HW limit: wr_port roll over case, can only use less than 7 buf.
     *  if use 8 buf, will got DNLD_RDY INT hung, or polling abnormal wr_bitmap.
    */
    if (credits == MAX_AGG_BUF && (nb_ready >= MAX_AGG_BUF) && 
        (card->curr_wr_port + credits > MAX_PORT_NUM))
        credits -= 1;
#endif

    if (credits >= nb_ready) {
        skb_queue_extract(&txq->sk_list, sk_list_push, nb_ready);
        credits = nb_ready;
        res = true;
    } else {
        skb_queue_extract(&txq->sk_list, sk_list_push, credits);

        /* When processing PS service period (i.e. push_limit != 0), no longer
           process this txq if the buffers extracted will complete the SP for
           this txq */
        if (txq->push_limit && (credits == txq->push_limit)) {
            printk("%s:txq->push_limit=%d, credits=%d, return true\n", 
                   __func__, txq->push_limit, credits);
                   
            res = true;
        }
    }

    return res;
}

/**
 * bl_txq_select_user - Select User queue for a txq
 *
 * @bl_hw: main driver data
 * @mu_lock: true is MU lock is taken
 * @txq: TXQ to select MU group for
 * @hwq: HWQ for the TXQ
 * @user: Updated with user position selected
 *
 * @return false if it is no possible to process this txq.
 *         true otherwise
 *
 * This function selects the MU group to use for a TXQ.
 * The selection is done as follow:
 *
 * - return immediately for STA that don't belongs to any group and select
 *   group 0 / user 0
 *
 * - If MU tx is disabled (by user mutx_on, or because mu group are being
 *   updated !mu_lock), select group 0 / user 0
 *
 * - Use the best group selected by @bl_mu_group_sta_select.
 *
 *   Each time a group is selected (except for the first case where sta
 *   doesn't belongs to a MU group), the function checks that no buffer is
 *   pending for this txq on another user position. If this is the case stop
 *   the txq (BL_TXQ_STOP_MU_POS) and return false.
 *
 */
__attribute__((unused)) static
bool bl_txq_select_user(struct bl_hw *bl_hw, bool mu_lock,
                             struct bl_txq *txq, struct bl_hwq *hwq, int *user)
{
    int pos = 0;
#ifdef CONFIG_BL_MUMIMO_TX
    int id, group_id = 0;
    struct bl_sta *sta = bl_txq_2_sta(txq);

    /* for sta that belong to no group return immediately */
    if (!sta || !sta->group_info.cnt)
        goto end;

    /* If MU is disabled, need to check user */
    if (!bl_hw->mod_params->mutx_on || !mu_lock)
        goto check_user;

    /* Use the "best" group selected */
    group_id = sta->group_info.group;

    if (group_id > 0)
        pos = bl_mu_group_sta_get_pos(bl_hw, sta, group_id);

  check_user:
    /* check that we can push on this user position */
#if CONFIG_USER_MAX == 2
    id = (pos + 1) & 0x1;
    if (txq->pkt_pushed[id]) {
        bl_txq_stop(txq, BL_TXQ_STOP_MU_POS);
        return false;
    }

#else
    for (id = 0 ; id < CONFIG_USER_MAX ; id++) {
        if (id != pos && txq->pkt_pushed[id]) {
            bl_txq_stop(txq, BL_TXQ_STOP_MU_POS);
            return false;
        }
    }
#endif

  end:
    bl_txq_set_mu_info(bl_hw, txq, group_id, pos);
#endif /* CONFIG_BL_MUMIMO_TX */

    *user = pos;
    
    return true;
}

/**
 * bl_hwq_process - Process one HW queue list
 *
 * @bl_hw: Driver main data
 * @hw_queue: HW queue index to process
 *
 * The function will iterate over all the TX queues linked in this HW queue
 * list. For each TX queue, push as many buffers as possible in the HW queue.
 * (NB: TX queue have at least 1 buffer, otherwise it wouldn't be in the list)
 * - If TX queue no longer have buffer, remove it from the list and check next
 *   TX queue
 * - If TX queue no longer have credits or has a push_limit (PS mode) and it
 *   is reached , remove it from the list and check next TX queue
 * - If HW queue is full, update list head to start with the next TX queue on
 *   next call if current TX queue already pushed "too many" pkt in a row, and
 *   return
 *
 * To be called when HW queue list is modified:
 * - when a buffer is pushed on a TX queue
 * - when new credits are received
 * - when a STA returns from Power Save mode or receives traffic request.
 * - when Channel context change
 *
 * To be called with tx_lock hold
 */
void bl_hwq_process(struct bl_hw *bl_hw, struct bl_hwq *hwq)
{
    struct bl_txq *txq, *next;
    int user = 0;
    #if defined CONFIG_BL_SDIO
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    #elif defined CONFIG_BL_USB
    struct bl_usb_device *device = (struct bl_usb_device *)((bl_hw->plat)->priv);
    int tx_data_urb_pending;
    #endif
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif

    BL_TX_LOCK(&bl_hw->tx_lock, flags);

    hwq->nb_ready = 0;
    hwq->nb_once_sent = 0;

    list_for_each_entry_safe(txq, next, &hwq->list, sched_list) {
        bool txq_empty;
        struct sk_buff_head sk_list_push;
        #if defined CONFIG_BL_USB
        struct sk_buff *skb;
        struct bl_txhdr *txhdr = NULL;
        #endif

        /* sanity check for debug */
        if (!(txq->status & BL_TXQ_IN_HWQ_LIST) && txq->idx != TXQ_INACTIVE) {
            if (txq->sched_list.prev == (struct list_head *)LIST_POISON2 || 
                txq->sched_list.next == (struct list_head *)LIST_POISON1 || 
                txq->sched_list.prev == NULL || txq->sched_list.next == NULL) 
            {
                //
            }
            else
            {
                BL_DBG("%s:txq-%x (status %x) was not in hwq, but txq->sched_list in hwq, delete!\n", 
                       __func__, txq->idx, txq->status);
                       
                list_del(&txq->sched_list);
                txq->nb_once_sent = 0;
            }

            break;
        } else if (txq->idx == TXQ_INACTIVE) {
            BL_DBG_MSG("%s %u, txq inactive, break\n", __func__, __LINE__);
            break;
        }
        
        if (!bl_txq_skb_ready(txq)) {
            BL_DBG("%s:there is no skb in txq...\n", __func__);

            goto txq_end;
        }

        #if defined CONFIG_BL_USB 
        tx_data_urb_pending = atomic_read(&device->tx_data_urb_pending);

        if (BL_TX_DATA_URB <= tx_data_urb_pending) {
            break;
        }
        #endif
        
        #if defined CONFIG_BL_SDIO
        if (!(card->mp_wr_bitmap & DATA_PORT_MSK)) {
            break;
        }
        #endif
        
        hwq->nb_ready += skb_queue_len(&txq->sk_list);

        #if defined CONFIG_BL_SDIO
        do {
            txq_empty = true;
            __skb_queue_head_init(&sk_list_push);
            
            if (txq->idx != TXQ_INACTIVE) 
            {
                txq_empty =
                    bl_txq_get_skb_to_push(bl_hw, hwq, txq, user, &sk_list_push);
            }
            
            BL_TX_UNLOCK(&bl_hw->tx_lock, flags);

            txq->nb_once_sent += skb_queue_len(&sk_list_push);
            
            if (skb_queue_len(&sk_list_push) != 0) {
                BL_DBG("bl_tx_multi push cnt:%d\n", skb_queue_len(&sk_list_push));
                
                bl_tx_multi_pkt_push(bl_hw, &sk_list_push);
            }

            BL_TX_LOCK(&bl_hw->tx_lock, flags);
            if (skb_queue_len(&sk_list_push) != 0) {
                struct sk_buff *tmp_skb;

                txq->nb_once_sent -= skb_queue_len(&sk_list_push);

                while ((tmp_skb = __skb_dequeue(&sk_list_push)) != NULL) {
                    skb_queue_head(&txq->sk_list, tmp_skb);
                }
                
                txq_empty = false;
            }            

            hwq->nb_once_sent += txq->nb_once_sent;
            hwq->nb_sent += txq->nb_once_sent;
            
        } while(!txq_empty && (card->mp_wr_bitmap & DATA_PORT_MSK));

        bl_hw->data_sent = false;

        #elif defined CONFIG_BL_USB
        txq_empty = true;
        __skb_queue_head_init(&sk_list_push);

        tx_data_urb_pending = atomic_read(&device->tx_data_urb_pending);

        if (BL_TX_DATA_URB > tx_data_urb_pending) {
            if (txq->idx != TXQ_INACTIVE) {
                txq_empty = bl_txq_get_skb_to_push(bl_hw, hwq, txq, user, &sk_list_push);
            }
        } else {
            BL_DBG("%s: no more urbs, tx_data_urb_pending=%d\n", __func__, 
                   atomic_read(&device->tx_data_urb_pending));
        }
        
        BL_TX_UNLOCK(&bl_hw->tx_lock, flags);

        txq->nb_once_sent += skb_queue_len(&sk_list_push);
        hwq->nb_once_sent += txq->nb_once_sent;
        hwq->nb_sent += txq->nb_once_sent;

        while ((skb = __skb_dequeue(&sk_list_push)) != NULL) {
            u16 data_offset = sizeof(struct bl_txhdr) +
                              sizeof(struct inf_hdr) +
                              sizeof(struct txdesc_api) + 
                              TX_MAX_MAC_HEADER_SIZE - 
                              sizeof(struct ethhdr);

            if (bl_trace_dyn_module&TRACE_MOD_DHCP)
                bl_skb_parsing(bl_hw, skb, 1, data_offset);

            txhdr = (struct bl_txhdr *)skb->data;
            bl_tx_push(bl_hw, txhdr, 0);
        }

        BL_TX_LOCK(&bl_hw->tx_lock, flags);
        #endif

        if (txq_empty) {
            if(skb_queue_len(&txq->sk_list) == 0) {
                txq->pkt_sent = 0;
            } else {
                BL_DBG("%s:txq sk_list is not empty!, should not delete it\n", __func__);
            }
        } else {
            BL_DBG("%s:txq_empty=false, should not delete it\n", __func__);
        }

        if (txq->nb_once_sent > NB_TXQ_TX_THRESH) {
            if (txq->idx != TXQ_INACTIVE) {
                BL_DBG("%s, rotate txq:0x%p, sta:0x%p, tid:%d\n",
                           __func__, txq, txq->sta, txq->tid);
                           
                bl_txq_del_from_hw_list(txq);
                bl_txq_add_to_hw_list(txq);
            }
        }

        if (txq->push_limit && txq->sta) {
            if (txq->ps_id == LEGACY_PS_ID) {
                txq->sta->ps.sp_cnt[txq->ps_id] -= txq->push_limit;
                txq->push_limit = 0;
                
                BL_DBG("%s: set txq->push_limit to 0\n", __func__);
            }
        }

        txq_end:
        /* restart netdev queue if number of queued buffer is below threshold */
        if (unlikely(txq->status & BL_TXQ_NDEV_FLOW_CTRL) &&
            skb_queue_len(&txq->sk_list) < BL_NDEV_FLOW_CTRL_RESTART) 
        {
            txq->status &= ~BL_TXQ_NDEV_FLOW_CTRL;
            
            BL_DBG("%s:Flowctrl restart: skb_len=%d, txq->idx[%d], txq->txq_netdev_idx[%d]\n", 
                   __func__, skb_queue_len(&txq->sk_list), txq->idx, txq->ndev_idx);

            netif_wake_subqueue(txq->ndev, txq->ndev_idx);
        }
    }

    BL_TX_UNLOCK(&bl_hw->tx_lock, flags);
}

/**
 * bl_hwq_process_all - Process all HW queue list
 *
 * @bl_hw: Driver main data
 *
 * Loop over all HWQ, and process them if needed
 * To be called with tx_lock hold
 */
void bl_hwq_process_all(struct bl_hw *bl_hw)
{
    int id, hwq_size, i;
    static int start_id = 0;
    int next_start_id = start_id;
    #ifdef CONFIG_BL_SDIO
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    #endif
    #if defined CONFIG_BL_USB
    struct bl_usb_device *device;
    int tx_data_urb_pending;
    int credits;

    device = (struct bl_usb_device *)((bl_hw->plat)->priv);
    tx_data_urb_pending = atomic_read(&device->tx_data_urb_pending);
    credits = BL_TX_DATA_URB - tx_data_urb_pending;

    BL_DBG_DATA("%s, start_id:%d, urb_free:%d\n", __func__, start_id, credits);

    if (credits <= (BL_TX_DATA_URB/2)) {
        BL_DBG_DATA("%s no enough free urb\n", __func__);
        
        return;
    }
    #endif

    #ifdef CONFIG_BL_SDIO
    BL_DBG("%s, start_id:%d\n", __func__, start_id);
    #endif

    hwq_size = ARRAY_SIZE(bl_hw->hwq);

    for (i = 0; i < hwq_size; i++) {
        #ifdef CONFIG_BL_SDIO
        if (!(card->mp_wr_bitmap & DATA_PORT_MSK)) {
            BL_DBG("%s no avail port \n", __func__);
            
            break;
         }
        #endif

        id = (start_id - i + hwq_size) % hwq_size;

        bl_hwq_process(bl_hw, &bl_hw->hwq[id]);
        
        #if defined CONFIG_BL_USB
        tx_data_urb_pending = atomic_read(&device->tx_data_urb_pending);

        BL_DBG_DATA("%s, id:%d, nb_ready:%u, nb_sent:%u, nb_once_sent:%u, usb free:%u\n",
               __func__, id, bl_hw->hwq[id].nb_ready, bl_hw->hwq[id].nb_sent,
               bl_hw->hwq[id].nb_once_sent,
               BL_TX_DATA_URB-tx_data_urb_pending);
        #else
        BL_DBG("%s, id:%d, nb_ready:%u, nb_sent:%u, nb_once_sent:%u\n",
               __func__, id, bl_hw->hwq[id].nb_ready, bl_hw->hwq[id].nb_sent,
               bl_hw->hwq[id].nb_once_sent);
        #endif

        if (bl_hw->hwq[id].nb_once_sent) {
            if (bl_hw->hwq[id].nb_sent > NB_HWQ_TX_THRESH ||
                bl_hw->hwq[id].nb_once_sent >= bl_hw->hwq[id].nb_ready)
            {
                next_start_id = (id>0?(id-1):(ARRAY_SIZE(bl_hw->hwq) - 1));
                bl_hw->hwq[id].nb_sent = 0;
            }
        }
    }

    start_id = next_start_id;

    BL_DBG_DATA("%s done, start_id:%d, next_start_id:%d\n", 
           __func__, start_id, next_start_id);
}

/**
 * bl_hwq_init - Initialize all hwq structures
 *
 * @bl_hw: Driver main data
 *
 */
void bl_hwq_init(struct bl_hw *bl_hw)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(bl_hw->hwq); i++) {
        struct bl_hwq *hwq = &bl_hw->hwq[i];

        for (j = 0 ; j < CONFIG_USER_MAX; j++)
            hwq->credits[j] = nx_txdesc_cnt[i];
            
        hwq->id = i;
        hwq->size = nx_txdesc_cnt[i];
        INIT_LIST_HEAD(&hwq->list);
    }
}
