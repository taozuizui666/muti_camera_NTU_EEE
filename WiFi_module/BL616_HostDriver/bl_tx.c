/**
 ******************************************************************************
 *
 *  @file bl_tx.c
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

#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <net/sock.h>

#include <linux/ip.h>
#include <net/tcp.h>


#include "bl_defs.h"
#include "bl_tx.h"
#include "bl_msg_tx.h"
#include "bl_mesh.h"
#include "bl_compat.h"
#include "bl_irqs.h"
#include "bl_ipc_host.h"
#if defined CONFIG_BL_SDIO
#include "bl_sdio.h"
#elif defined CONFIG_BL_USB
#include "bl_usb.h"
#endif
#include "bl_utils.h"

static u16 g_pkt_sn = 0;

/// default power table for each rate
int8_t bl_txpwr_vs_rate_table[PHY_TRPC_MODE_MAX][PHY_TRPC_NUM_MCS] = 
{
    // 11b 1,2,5.5,11 Mbps
    {
        [0] = 22,
        [1] = 22,
        [2] = 22,
        [3] = 22,
    },
    // 11g 6,9,12,18,24,36,48,54 Mbps
    {
        [0] = 21,  // BPSK 1/2
        [1] = 20,  // BPSK 3/4
        [2] = 20,  // QPSK 1/2
        [3] = 20,  // QPSK 3/4
        [4] = 20,  // 16QAM 1/2
        [5] = 20,  // 16QAM 3/4
        [6] = 18,  // 64QAM 2/3
        [7] = 18   // 64QAM 3/4
    },
    // 11n HT20 MCS0~MCS7
    {
        [0] = 20,  // BPSK 1/2
        [1] = 20,  // QPSK 1/2
        [2] = 20,  // QPSK 3/4
        [3] = 20,  // 16QAM 1/2
        [4] = 20,  // 16QAM 3/4
        [5] = 19,  // 64QAM 2/3
        [6] = 17,  // 64QAM 3/4
        [7] = 17   // 64QAM 5/6
    },
    // 11n HT40 MCS0~MCS7
    {
        [0] = 19,  // BPSK 1/2
        [1] = 19,  // QPSK 1/2
        [2] = 19,  // QPSK 3/4
        [3] = 19,  // 16QAM 1/2
        [4] = 19,  // 16QAM 3/4
        [5] = 18,  // 64QAM 2/3
        [6] = 16,  // 64QAM 3/4
        [7] = 16   // 64QAM 5/6
    },
    // 11ax HE20 MCS0~MCS9
    {
        [0] = 20,  // BPSK 1/2
        [1] = 20,  // QPSK 1/2
        [2] = 20,  // QPSK 3/4
        [3] = 20,  // 16QAM 1/2
        [4] = 20,  // 16QAM 3/4
        [5] = 19,  // 64QAM 2/3
        [6] = 17,  // 64QAM 3/4
        [7] = 17,  // 64QAM 5/6
        [8] = 15,  // 256QAM 3/4
        [9] = 15   // 256QAM 5/6
    },
    // 11ax HE40 MCS0~MCS9
    {
        [0] = 19,  // BPSK 1/2
        [1] = 19,  // QPSK 1/2
        [2] = 19,  // QPSK 3/4
        [3] = 19,  // 16QAM 1/2
        [4] = 19,  // 16QAM 3/4
        [5] = 18,  // 64QAM 2/3
        [6] = 16,  // 64QAM 3/4
        [7] = 16,  // 64QAM 5/6
        [8] = 14,  // 256QAM 3/4
        [9] = 14   // 256QAM 5/6
    }
};

int bl_set_default_tx_pwr(struct bl_hw *bl_hw)
{
    struct mm_cal_cfg_req cal_cfg_req;
    int ret = 0;

    memset((u8_l *)&cal_cfg_req, 0, sizeof(cal_cfg_req));

    cal_cfg_req.pwr_11b_valid = 0x5a;
    memcpy(cal_cfg_req.pwrtarget.pwr_11b,
           bl_txpwr_vs_rate_table[PHY_TRPC_11B], PHY_11B_RATE_NUM);

    cal_cfg_req.pwr_11g_valid = 0x5a;
    memcpy(cal_cfg_req.pwrtarget.pwr_11g, 
           bl_txpwr_vs_rate_table[PHY_TRPC_11G], PHY_11G_RATE_NUM);

    cal_cfg_req.pwr_11n_ht20_valid = 0x5a;
    memcpy(cal_cfg_req.pwrtarget.pwr_11n_ht20, 
           bl_txpwr_vs_rate_table[PHY_TRPC_11N_BW20], PHY_11N_RATE_NUM);

    cal_cfg_req.pwr_11n_ht40_valid = 0x5a;
    memcpy(cal_cfg_req.pwrtarget.pwr_11n_ht40, 
           bl_txpwr_vs_rate_table[PHY_TRPC_11N_BW40], PHY_11N_RATE_NUM);

    cal_cfg_req.pwr_11ax_he20_valid = 0x5a;
    memcpy(cal_cfg_req.pwrtarget.pwr_11ax_he20, 
           bl_txpwr_vs_rate_table[PHY_TRPC_11AX_BW20], PHY_11AX_RATE_NUM);

    cal_cfg_req.pwr_11ax_he40_valid = 0x5a;
    memcpy(cal_cfg_req.pwrtarget.pwr_11ax_he40,
           bl_txpwr_vs_rate_table[PHY_TRPC_11AX_BW40], PHY_11AX_RATE_NUM);

    #ifdef CONFIG_FW_COMBO
    cal_cfg_req.pwr_ble_valid = 0x5a;
    cal_cfg_req.pwr_bt_valid = 0x5a;
    cal_cfg_req.pwrtarget.pwr_ble = 20;
    cal_cfg_req.pwrtarget.pwr_bt[0] = 10;
    cal_cfg_req.pwrtarget.pwr_bt[1] = 10;
    cal_cfg_req.pwrtarget.pwr_bt[2] = 10;
    #endif

    ret = bl_send_cal_cfg(bl_hw, &cal_cfg_req);
    if (ret) {
        printk("%s, error=%d\n", __func__, ret);
    }

    return ret;
}
/******************************************************************************
 * Power Save functions
 *****************************************************************************/
/**
 * bl_set_traffic_status - Inform FW if traffic is available for STA in PS
 *
 * @bl_hw: Driver main data
 * @sta: Sta in PS mode
 * @available: whether traffic is buffered for the STA
 * @ps_id: type of PS data requested (@LEGACY_PS_ID or @UAPSD_ID)
  */
void bl_set_traffic_status(struct bl_hw *bl_hw, struct bl_sta *sta, 
                               bool available, u8 ps_id)
{
    if (sta->tdls.active) {
        bl_send_tdls_peer_traffic_ind_req(bl_hw,
                                            bl_hw->vif_table[sta->vif_idx]);
    } else {
        bool uapsd = (ps_id != LEGACY_PS_ID);
        bl_send_me_traffic_ind(bl_hw, sta->sta_idx, uapsd, available);
    }
}

/**
 * bl_ps_bh_enable - Enable/disable PS mode for one STA
 *
 * @bl_hw: Driver main data
 * @sta: Sta which enters/leaves PS mode
 * @enable: PS mode status
 *
 * This function will enable/disable PS mode for one STA.
 * When enabling PS mode:
 *  - Stop all STA's txq for BL_TXQ_STOP_STA_PS reason
 *  - Count how many buffers are already ready for this STA
 *  - For BC/MC sta, update all queued SKB to use hw_queue BCMC
 *  - Update TIM if some packet are ready
 *
 * When disabling PS mode:
 *  - Start all STA's txq for BL_TXQ_STOP_STA_PS reason
 *  - For BC/MC sta, update all queued SKB to use hw_queue AC_BE
 *  - Update TIM if some packet are ready (otherwise fw will not update TIM
 *    in beacon for this STA)
 *
 * All counter/skb updates are protected from TX path by taking tx_lock
 *
 * NOTE: _bh_ in function name indicates that this function is called
 * from a bottom_half tasklet.
 */
void bl_ps_bh_enable(struct bl_hw *bl_hw, struct bl_sta *sta, bool enable)
{
    struct bl_txq *txq;
    struct bl_vif *vif = bl_hw->vif_table[sta->vif_idx]; 
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif
    
    BL_DBG("%s enable=%d\n", __func__, enable);

    if (vif == NULL || vif->up == false) {
        BL_DBG("%s, return\n", __func__);
        
        return;
    }

    if (enable) {
        BL_TX_LOCK(&bl_hw->tx_lock, flags);
        sta->ps.active = true;
        sta->ps.sp_cnt[LEGACY_PS_ID] = 0;
        sta->ps.sp_cnt[UAPSD_ID] = 0;
        bl_txq_sta_stop(sta, BL_TXQ_STOP_STA_PS, bl_hw);

        if (is_multicast_sta(sta->sta_idx)) {
            txq = bl_txq_sta_get(sta, 0, bl_hw);
            sta->ps.pkt_ready[LEGACY_PS_ID] = skb_queue_len(&txq->sk_list);
            sta->ps.pkt_ready[UAPSD_ID] = 0;
            
            if (BL_VIF_TYPE(vif) == NL80211_IFTYPE_AP || 
                (BL_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO)) 
            {
                txq->hwq = &bl_hw->hwq[BL_HWQ_BE];//force into BE queue
            } else {
                txq->hwq = &bl_hw->hwq[BL_HWQ_BCMC];
            }
        } else {
            int i;
            
            sta->ps.pkt_ready[LEGACY_PS_ID] = 0;
            sta->ps.pkt_ready[UAPSD_ID] = 0;
            
            foreach_sta_txq(sta, txq, i, bl_hw) {
                sta->ps.pkt_ready[txq->ps_id] += skb_queue_len(&txq->sk_list);
            }
        }

        BL_TX_UNLOCK(&bl_hw->tx_lock, flags);

        if (sta->ps.pkt_ready[LEGACY_PS_ID])
            bl_set_traffic_status(bl_hw, sta, true, LEGACY_PS_ID);

        if (sta->ps.pkt_ready[UAPSD_ID])
            bl_set_traffic_status(bl_hw, sta, true, UAPSD_ID);
    } else {
        BL_TX_LOCK(&bl_hw->tx_lock, flags);
        sta->ps.active = false;

        if (is_multicast_sta(sta->sta_idx)) {
            txq = bl_txq_sta_get(sta, 0, bl_hw);
            bl_txq_del_from_hw_list(txq);
            txq->hwq = &bl_hw->hwq[BL_HWQ_BE];
            txq->push_limit = 0;
        } else {
            int i;
            
            foreach_sta_txq(sta, txq, i, bl_hw) {
                txq->push_limit = 0;
            }
        }

        bl_txq_sta_start(sta, BL_TXQ_STOP_STA_PS, bl_hw);
        BL_TX_UNLOCK(&bl_hw->tx_lock, flags);

        if (sta->ps.pkt_ready[LEGACY_PS_ID])
            bl_set_traffic_status(bl_hw, sta, false, LEGACY_PS_ID);

        if (sta->ps.pkt_ready[UAPSD_ID])
            bl_set_traffic_status(bl_hw, sta, false, UAPSD_ID);
    }
}

/**
 * bl_ps_bh_traffic_req - Handle traffic request for STA in PS mode
 *
 * @bl_hw: Driver main data
 * @sta: Sta which enters/leaves PS mode
 * @pkt_req: number of pkt to push
 * @ps_id: type of PS data requested (@LEGACY_PS_ID or @UAPSD_ID)
 *
 * This function will make sure that @pkt_req are pushed to fw
 * whereas the STA is in PS mode.
 * If request is 0, send all traffic
 * If request is greater than available pkt, reduce request
 * Note: request will also be reduce if txq credits are not available
 *
 * All counter updates are protected from TX path by taking tx_lock
 *
 * NOTE: _bh_ in function name indicates that this function is called
 * from the bottom_half tasklet.
 */
void bl_ps_bh_traffic_req(struct bl_hw *bl_hw, struct bl_sta *sta,
                                u16 pkt_req, u8 ps_id)
{
    int pkt_ready_all;
    struct bl_txq *txq;
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif
    
    if (!sta->ps.active) {
        BL_DBG("sta %pM is not in Power Save mode\n",
               sta->mac_addr);
        return;
    }

    BL_TX_LOCK(&bl_hw->tx_lock, flags);

    /* Fw may ask to stop a service period with PS_SP_INTERRUPTED. This only
       happens for p2p-go interface if NOA starts during a service period */
    if ((pkt_req == PS_SP_INTERRUPTED) && (ps_id == UAPSD_ID)) {
        int tid;
        
        sta->ps.sp_cnt[ps_id] = 0;
        
        foreach_sta_txq(sta, txq, tid, bl_hw) {
            txq->push_limit = 0;
        }
        
        goto done;
    }

    pkt_ready_all = (sta->ps.pkt_ready[ps_id] - sta->ps.sp_cnt[ps_id]);

    /* Don't start SP until previous one is finished or we don't have
       packet ready (which must not happen for U-APSD) */
    if (sta->ps.sp_cnt[ps_id] || pkt_ready_all <= 0) {
        goto done;
    }

    /* Adapt request to what is available. */
    if (pkt_req == 0 || pkt_req > pkt_ready_all) {
        pkt_req = pkt_ready_all;
    }

    /* Reset the SP counter */
    sta->ps.sp_cnt[ps_id] = 0;

    /* "dispatch" the request between txq */
    if (is_multicast_sta(sta->sta_idx)) {
        txq = bl_txq_sta_get(sta, 0, bl_hw);
        
        if (txq->credits <= 0)
            goto done;
            
        if (pkt_req > txq->credits)
            pkt_req = txq->credits;
            
        txq->push_limit = pkt_req;
        sta->ps.sp_cnt[ps_id] = pkt_req;
        bl_txq_add_to_hw_list(txq);
    } else {
        int i, tid;

        foreach_sta_txq_prio(sta, txq, tid, i, bl_hw) {
            u16 txq_len = skb_queue_len(&txq->sk_list);

            if (txq->ps_id != ps_id)
                continue;

            if (txq_len > txq->credits)
                txq_len = txq->credits;

            if (txq_len == 0)
                continue;

            if (txq_len < pkt_req) {
                /* Not enough pkt queued in this txq, add this
                   txq to hwq list and process next txq */
                pkt_req -= txq_len;
                txq->push_limit = txq_len;
                sta->ps.sp_cnt[ps_id] += txq_len;
                bl_txq_add_to_hw_list(txq);
            } else {
                /* Enough pkt in this txq to comlete the request
                   add this txq to hwq list and stop processing txq */
                txq->push_limit = pkt_req;
                sta->ps.sp_cnt[ps_id] += pkt_req;
                bl_txq_add_to_hw_list(txq);
                break;
            }
        }
    }

  done:
    BL_TX_UNLOCK(&bl_hw->tx_lock, flags);
}

/******************************************************************************
 * TX functions
 *****************************************************************************/
#define PRIO_STA_NULL 0xAA

static const int bl_down_hwq2tid[3] = {
    [BL_HWQ_BK] = 2,
    [BL_HWQ_BE] = 3,
    [BL_HWQ_VI] = 5,
};

static void bl_rpt_dhcp_flags_change(struct bl_hw *bl_hw,  u8 * eth_hdr);

static void bl_downgrade_ac(struct bl_sta *sta, struct sk_buff *skb)
{
    int8_t ac = bl_tid2hwq[skb->priority];

    if (WARN((ac > BL_HWQ_VO), "Unexepcted ac %d for skb before downgrade", ac))
        ac = BL_HWQ_VO;

    while (sta->acm & BIT(ac)) {
        if (ac == BL_HWQ_BK) {
            skb->priority = 1;
            return;
        }
        
        ac--;
        skb->priority = bl_down_hwq2tid[ac];
    }
}

__attribute__((unused)) static void bl_tx_statistic(struct bl_hw *bl_hw, 
                            struct bl_txq *txq,
                            union bl_hw_txstatus bl_txst, unsigned int data_len)
{
    struct bl_sta *sta = txq->sta;
    
    if (!sta || !bl_txst.acknowledged)
        return;

    sta->stats.tx_pkts ++;
    sta->stats.tx_bytes += data_len;
    sta->stats.last_act = bl_hw->stats.last_tx;
}

u16 bl_select_txq(struct bl_vif *bl_vif, struct sk_buff *skb)
{
    struct bl_hw *bl_hw = bl_vif->bl_hw;
    struct wireless_dev *wdev = &bl_vif->wdev;
    struct bl_sta *sta = NULL;
    struct bl_txq *txq;
    u16 netdev_queue;
    bool tdls_mgmgt_frame = false;

    switch (wdev->iftype) {
    case NL80211_IFTYPE_STATION:
    case NL80211_IFTYPE_P2P_CLIENT:
    {
        struct ethhdr *eth;
        eth = (struct ethhdr *)skb->data;
        
        if (eth->h_proto == cpu_to_be16(ETH_P_TDLS)) {
            tdls_mgmgt_frame = true;
        }
        
        if ((bl_vif->tdls_status == TDLS_LINK_ACTIVE) &&
            (bl_vif->sta.tdls_sta != NULL) &&
            (memcmp(eth->h_dest, bl_vif->sta.tdls_sta->mac_addr, ETH_ALEN) == 0))
            sta = bl_vif->sta.tdls_sta;
        else
            sta = bl_vif->sta.ap;
        break;
    }
    case NL80211_IFTYPE_AP_VLAN:
        if (bl_vif->ap_vlan.sta_4a) {
            sta = bl_vif->ap_vlan.sta_4a;
            break;
        }

        /* AP_VLAN interface is not used for a 4A STA,
           fallback searching sta amongs all AP's clients */
        bl_vif = bl_vif->ap_vlan.master;
    case NL80211_IFTYPE_AP:
    case NL80211_IFTYPE_P2P_GO:
    {
        struct bl_sta *cur;
        struct ethhdr *eth = (struct ethhdr *)skb->data;

        if (is_multicast_ether_addr(eth->h_dest)) {
            sta = &bl_hw->sta_table[bl_vif->ap.bcmc_index];
        } else {
            list_for_each_entry(cur, &bl_vif->ap.sta_list, list) {
                if (!memcmp(cur->mac_addr, eth->h_dest, ETH_ALEN)) {
                    sta = cur;
                    break;
                }
            }
        }

        break;
    }
#ifdef CONFIG_MESH
    case NL80211_IFTYPE_MESH_POINT:
    {
        struct ethhdr *eth = (struct ethhdr *)skb->data;

        if (!bl_vif->is_resending) {
            /*
             * If ethernet source address is not the address of a mesh wireless interface, we are proxy for
             * this address and have to inform the HW
             */
            if (memcmp(&eth->h_source[0], &bl_vif->ndev->perm_addr[0], ETH_ALEN)) {
                /* Check if LMAC is already informed */
                if (!bl_get_mesh_proxy_info(bl_vif, (u8 *)&eth->h_source, true)) {
                    bl_send_mesh_proxy_add_req(bl_hw, bl_vif, 
                                               (u8 *)&eth->h_source);
                }
            }
        }

        if (is_multicast_ether_addr(eth->h_dest)) {
            sta = &bl_hw->sta_table[bl_vif->ap.bcmc_index];
        } else {
            /* Path to be used */
            struct bl_mesh_path *p_mesh_path = NULL;
            struct bl_mesh_path *p_cur_path;
            /* Check if destination is proxied by a peer Mesh STA */
            struct bl_mesh_proxy *p_mesh_proxy = 
                     bl_get_mesh_proxy_info(bl_vif, (u8 *)&eth->h_dest, false);
            /* Mesh Target address */
            struct mac_addr *p_tgt_mac_addr;

            if (p_mesh_proxy) {
                p_tgt_mac_addr = &p_mesh_proxy->proxy_addr;
            } else {
                p_tgt_mac_addr = (struct mac_addr *)&eth->h_dest;
            }

            /* Look for path with provided target address */
            list_for_each_entry(p_cur_path, &bl_vif->ap.mpath_list, list) {
                if (!memcmp(&p_cur_path->tgt_mac_addr, p_tgt_mac_addr, ETH_ALEN)) {
                    p_mesh_path = p_cur_path;
                    break;
                }
            }

            if (p_mesh_path) {
                sta = p_mesh_path->nhop_sta;
            } else {
                bl_send_mesh_path_create_req(bl_hw, bl_vif, (u8 *)p_tgt_mac_addr);
            }
        }

        break;
    }
#endif    
    default:
        break;
    }

    if (sta && sta->qos)
    {
        if (tdls_mgmgt_frame) {
            skb_set_queue_mapping(skb, NX_STA_NDEV_IDX(skb->priority, sta->sta_idx));
        } else {
            /* use the data classifier to determine what 802.1d tag the
             * data frame has */
            skb->priority =
               cfg80211_classify8021d(skb, NULL) & IEEE80211_QOS_CTL_TAG1D_MASK;
        }
        
        if (sta->acm)
            bl_downgrade_ac(sta, skb);

        txq = bl_txq_sta_get(sta, skb->priority, bl_hw);
        netdev_queue = txq->ndev_idx;
    }
    else if (sta)
    {
        skb->priority = 0xFF;
        txq = bl_txq_sta_get(sta, 0, bl_hw);
        netdev_queue = txq->ndev_idx;
    }
    else
    {
        /* This packet will be dropped in xmit function, still need to select
           an active queue for xmit to be called. As it most likely to happen
           for AP interface, select BCMC queue
           (TODO: select another queue if BCMC queue is stopped) */
        skb->priority = PRIO_STA_NULL;
        netdev_queue = NX_BCMC_TXQ_NDEV_IDX;
    }

    BUG_ON(netdev_queue >= NX_NB_NDEV_TXQ);

    return netdev_queue;
}

/**
 * bl_set_more_data_flag - Update MORE_DATA flag in tx sw desc
 *
 * @bl_hw: Driver main data
 * @sw_txhdr: Header for pkt to be pushed
 *
 * If STA is in PS mode
 *  - Set EOSP in case the packet is the last of the UAPSD service period
 *  - Set MORE_DATA flag if more pkt are ready for this sta
 *  - Update TIM if this is the last pkt buffered for this sta
 *
 * note: tx_lock already taken.
 */
static inline void bl_set_more_data_flag(struct bl_hw *bl_hw,
                                                struct bl_sw_txhdr *sw_txhdr)
{
    struct bl_sta *sta = sw_txhdr->bl_sta;
    struct bl_vif *vif = sw_txhdr->bl_vif;
    struct bl_txq *txq = sw_txhdr->txq;

    if (unlikely(sta->ps.active)) {
        sta->ps.pkt_ready[txq->ps_id]--;
        sta->ps.sp_cnt[txq->ps_id]--;

        if (((txq->ps_id == UAPSD_ID) || 
            (vif->wdev.iftype == NL80211_IFTYPE_MESH_POINT) || (sta->tdls.active))
            && !sta->ps.sp_cnt[txq->ps_id]) 
        {
            sw_txhdr->desc.host.flags |= TXU_CNTRL_EOSP;
        }

        if (sta->ps.pkt_ready[txq->ps_id]) {
            sw_txhdr->desc.host.flags |= TXU_CNTRL_MORE_DATA;
        } else {
            bl_set_traffic_status(bl_hw, sta, false, txq->ps_id);
        }
    }
}

/**
 * bl_get_tx_info - Get STA and tid for one skb
 *
 * @bl_vif: vif ptr
 * @skb: skb
 * @tid: pointer updated with the tid to use for this skb
 *
 * @return: pointer on the destination STA (may be NULL)
 *
 * skb has already been parsed in bl_select_queue function
 * simply re-read information form skb.
 */
static struct bl_sta *bl_get_tx_info(struct bl_vif *bl_vif,
                                         struct sk_buff *skb, u8 *tid)
{
    struct bl_hw *bl_hw = bl_vif->bl_hw;
    struct bl_sta *sta;
    int sta_idx;

    *tid = skb->priority;
    
    if (unlikely(skb->priority == PRIO_STA_NULL)) {
        return NULL;
    } else {
        int ndev_idx = skb_get_queue_mapping(skb);

        if (ndev_idx == NX_BCMC_TXQ_NDEV_IDX)
            sta_idx = NX_REMOTE_STA_MAX + master_vif_idx(bl_vif);
        else
            sta_idx = ndev_idx / NX_NB_TID_PER_STA;

        sta = &bl_hw->sta_table[sta_idx];
    }

    return sta;
}

/**
 *  bl_tx_push - Push one packet to fw
 *
 * @bl_hw: Driver main data
 * @txhdr: tx desc of the buffer to push
 * @flags: push flags (see @bl_push_flags)
 *
 * Push one packet to fw. Sw desc of the packet has already been updated.
 * Only MORE_DATA flag will be set if needed.
 */
void bl_tx_push(struct bl_hw *bl_hw, struct bl_txhdr *txhdr, int flags)
{
    struct bl_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
    struct sk_buff *skb = sw_txhdr->skb;
    struct bl_txq *txq = sw_txhdr->txq;
    u16 hw_queue = txq->hwq->id;
    int user = 0;
#ifdef CONFIG_BL_USB
    int ret = 0;
#endif

    lockdep_assert_held(&bl_hw->tx_lock);

    /* RETRY flag is not always set so retest here */
    if (txq->nb_retry) {
        flags |= BL_PUSH_RETRY;
        txq->nb_retry--;
        if (txq->nb_retry == 0) {
            WARN(skb != txq->last_retry_skb,
                 "last retry buffer is not the expected one");
                 
            txq->last_retry_skb = NULL;
        }
    } else if (!(flags & BL_PUSH_RETRY)) {
        txq->pkt_sent++;
    }

#ifdef CONFIG_BL_AMSDUS_TX
    if (txq->amsdu == sw_txhdr) {
        WARN((flags & BL_PUSH_RETRY), "End A-MSDU on a retry");
        
        bl_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].done++;
        txq->amsdu = NULL;
    }
    else if (!(flags & BL_PUSH_RETRY) &&
             !(sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU)) 
    {
        bl_hw->stats.amsdus[0].done++;
    }
#endif /* CONFIG_BL_AMSDUS_TX */

    /* Wait here to update hw_queue, as for multicast STA hwq may change
       between queue and push (because of PS) */
    sw_txhdr->hw_queue = hw_queue;

#ifdef CONFIG_BL_MUMIMO_TX
    /* MU group is only selected during hwq processing */
    sw_txhdr->desc.host.mumimo_info = txq->mumimo_info;
    user = BL_TXQ_POS_ID(txq);
#endif /* CONFIG_BL_MUMIMO_TX */

    if (sw_txhdr->bl_sta) {
        /* only for AP mode */
        bl_set_more_data_flag(bl_hw, sw_txhdr);
    }

    txq->pkt_pushed[user]++;

    if (txq->push_limit) {
        txq->push_limit--;
        
        BL_DBG("%s: txq->push_limit:%d\n", __func__, txq->push_limit);
    }

    BL_DBG("%s: skb->data=%p\n", __func__, skb->data);
    
#if defined CONFIG_BL_USB
    /*use usb interface to send the whole skb packet */
    /* fisrt, we should ignore the txhdr, after skb_pull, we got the real data */
    skb_pull(skb, sw_txhdr->headroom);
    /*use sw_txhdr to override the usb special header*/
    sw_txhdr->hdr.queue_idx = txq->hwq->id; //update queue idx to avoid ps mode modify it.
    memcpy((void *)skb->data, &sw_txhdr->hdr, sizeof(struct inf_hdr));
    memcpy((void *)skb->data + sizeof(struct inf_hdr), 
           &sw_txhdr->desc, sizeof(struct txdesc_api));

    BL_DBG_DATA("%s:bl_tx_push: send skb=%p, skb->len=%d, inf_hdr->len=%d, host.sn:%u\n", 
           __func__, skb, skb->len, sw_txhdr->hdr.len, sw_txhdr->desc.host.sn);

    ret = bl_usb_host_to_card(bl_hw, skb->data, skb->len,
                              BL_USB_EP_OUT, BL_USB_EP_DATA, skb);
    switch(ret) {
        case -1:
            printk("%s:usb submit urb failed, free skb: %p\n", __func__, skb);
            
            kmem_cache_free(bl_hw->sw_txhdr_cache, txhdr->sw_hdr);
            dev_kfree_skb_any(skb);
            
            goto end;
        case -ENOSR:
        case -EBUSY:
            printk("%s:no more urb, ret=%d, should never get here\n",
                   __func__, ret);

            kmem_cache_free(bl_hw->sw_txhdr_cache, txhdr->sw_hdr);
            dev_kfree_skb_any(skb);
            
            goto end;
        case 0:
        default:
            BL_DBG("%s:usb transfer sucess\n", __func__);
    }

end:
    return;
#endif
}

#ifdef CONFIG_BL_SDIO
void bl_tx_multi_pkt_push(struct bl_hw *bl_hw, 
                                 struct sk_buff_head *sk_list_push)
{
    struct sk_buff *skb;
    struct bl_txhdr *txhdr;
    sdio_mpa_tx mpa_tx_data = {0};
    int ret;
    u32 port;
    u32 cmd53_port;
    u32 buf_block_len;
    int flags = 0, cnt = 0, txq_pushed_cnt = 0, txq_num[17] = {0};
    struct sdio_mmc_card *card;
    
    card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;

    /*fix alloc buf size, such as 16K(2*8(aggr num))*/
    mpa_tx_data.buf = bl_hw->mpa_tx_data.buf;
    if(mpa_tx_data.buf == NULL){
        printk("mpa_tx_data.buf is null!\n");
        
        return;
    }
    
    /*copy multi skbs into one large buf*/
    while ((skb = __skb_dequeue(sk_list_push)) != NULL) {
        u16 data_offset = sizeof(struct bl_txhdr) + sizeof(struct inf_hdr) +
                          sizeof(struct txdesc_api) + TX_MAX_MAC_HEADER_SIZE - 
                          sizeof(struct ethhdr);

        ret = bl_get_wr_port(bl_hw, &port);
        if(ret) {
            skb_queue_head(sk_list_push, skb);
            
            printk("%s no available port for tx\n",__func__);
            
            return;
        }

        if (bl_trace_dyn_module&TRACE_MOD_DHCP)
            bl_skb_parsing(bl_hw, skb, 1, data_offset);

        txhdr = (struct bl_txhdr *)skb->data;

        /* RETRY flag is not always set so retest here */
        if (txhdr->sw_hdr->txq->nb_retry) {
            flags |= BL_PUSH_RETRY;
            txhdr->sw_hdr->txq->nb_retry--;
            
            if (txhdr->sw_hdr->txq->nb_retry == 0) {
                WARN(skb != txhdr->sw_hdr->txq->last_retry_skb,
                     "last retry buffer is not the expected one");
                     
                txhdr->sw_hdr->txq->last_retry_skb = NULL;
            }
        } else if (!(flags & BL_PUSH_RETRY)) {
            txhdr->sw_hdr->txq->pkt_sent++;
        }

        txhdr->sw_hdr->hw_queue = txhdr->sw_hdr->txq->hwq->id;

        if(txhdr->sw_hdr->bl_sta)
            bl_set_more_data_flag(bl_hw, txhdr->sw_hdr);

//        txhdr->sw_hdr->txq->credits--;

        txhdr->sw_hdr->txq->pkt_pushed[0]++;

//        if(txhdr->sw_hdr->txq->credits <= 0)
//            bl_txq_stop(txhdr->sw_hdr->txq, bl_TXQ_STOP_FULL);

        skb_pull(skb, txhdr->sw_hdr->headroom);
        txhdr->sw_hdr->hdr.queue_idx = txhdr->sw_hdr->txq->hwq->id;
        //record port number for FW debug
        txhdr->sw_hdr->hdr.reserved = port;
        memcpy((void *)skb->data, &txhdr->sw_hdr->hdr, sizeof(struct inf_hdr));
        memcpy((void *)skb->data + sizeof(struct inf_hdr),
               &txhdr->sw_hdr->desc, sizeof(struct txdesc_api));
        buf_block_len = (skb->len + BL_SDIO_BLOCK_SIZE - 1) / BL_SDIO_BLOCK_SIZE;
        memcpy((void *)&mpa_tx_data.buf[mpa_tx_data.buf_len], 
               skb->data, buf_block_len * BL_SDIO_BLOCK_SIZE);
        mpa_tx_data.buf_len += buf_block_len * BL_SDIO_BLOCK_SIZE;
        
        //printk("###%d: skb->len: %d, pad_len: %d\n", mpa_tx_data.pkt_cnt, skb->len, buf_block_len*bl_SDIO_BLOCK_SIZE);

        //mpa_tx_data.mp_wr_info[mpa_tx_data.pkt_cnt] = *(u16 *)skb->data;

        if(!mpa_tx_data.pkt_cnt) {
            mpa_tx_data.start_port = port;
        }

        if(mpa_tx_data.start_port <= port) {
            mpa_tx_data.ports |= (1 << (mpa_tx_data.pkt_cnt));
        } else {
            mpa_tx_data.ports |= (1 << (mpa_tx_data.pkt_cnt + NA_DATA_PORT_NUM));
        }

        mpa_tx_data.pkt_cnt++;

        skb_push(skb, txhdr->sw_hdr->headroom);
        
        if(txhdr->sw_hdr->desc.host.flags & TXU_CNTRL_MGMT){
            ret = ipc_host_txdesc_push(bl_hw->ipc_env, txhdr->sw_hdr->hw_queue, 0, skb);
            if (ret >= 0)
                txq_num[txq_pushed_cnt++] = ret;
                //txhdr->sw_hdr->txq->hwq->credits[0]--;
            bl_hw->stats.cfm_balance[txhdr->sw_hdr->hw_queue]++;
        } else {
            kmem_cache_free(bl_hw->sw_txhdr_cache, txhdr->sw_hdr);
            dev_kfree_skb_any(skb);
        }
    }

    /*
    printk("mpa_tx_data:ports=0x%02x, start_port=%d, buf=%p, buf_len=%d, pkt_cnt=%d\n",
            mpa_tx_data.ports, 
            mpa_tx_data.start_port,
            mpa_tx_data.buf,
            mpa_tx_data.buf_len,
            mpa_tx_data.pkt_cnt);
    */

    BL_DBG("T:s=%d,p=0x%02x,c=%d\n", mpa_tx_data.start_port, mpa_tx_data.ports, 
           mpa_tx_data.pkt_cnt);

    if (mpa_tx_data.pkt_cnt != 0) {
        /*send packet*/
        cmd53_port = (card->io_port | bl_SDIO_MPA_ADDR_BASE |
                        (mpa_tx_data.ports << 4)) + mpa_tx_data.start_port;
        //printk("cmd53_port=0x%08x\n", cmd53_port);

        ret = bl_write_data_sync(bl_hw, mpa_tx_data.buf, mpa_tx_data.buf_len, cmd53_port);
        if(ret) {
            printk("bl_write_data_sync failed, ret=%d, free[%d] failed data, pushed_cnt=%d\n", 
                   ret, mpa_tx_data.pkt_cnt, txq_pushed_cnt);
                   
            do{
                ret = bl_write_data_sync(bl_hw, mpa_tx_data.buf, 
                                         mpa_tx_data.buf_len, cmd53_port);
                cnt++;
            }while(ret && cnt < BL_SDIO_RD_WR_RETRY);

            if (cnt >= BL_SDIO_RD_WR_RETRY) {
                printk("retry over 10 times, free[%d] pkt, txq_pushed_cnt=%d\n",
                       mpa_tx_data.pkt_cnt, txq_pushed_cnt);
                       
                if (txq_pushed_cnt) {
                    do {
                        skb = skb_dequeue_tail(&bl_hw->transmitted_list[txq_num[--txq_pushed_cnt]]);
                        if (skb) {
                            txhdr = (struct bl_txhdr *)skb->data;
                            kmem_cache_free(bl_hw->sw_txhdr_cache, txhdr->sw_hdr);
                            dev_kfree_skb_any(skb);
                        }
                    } while(txq_pushed_cnt);
                }
            }
        }
    }
    
    if (bl_hw->mpa_tx_data.buf)
        memset(bl_hw->mpa_tx_data.buf, 0, BL_RX_DATA_BUF_SIZE_16K);
}
#endif

/**
 * bl_tx_retry - Push an AMPDU pkt that need to be retried
 *
 * @bl_hw: Driver main data
 * @skb: pkt to re-push
 * @txhdr: tx desc of the pkt to re-push
 * @sw_retry: Indicates if fw decide to retry this buffer
 *            (i.e. it has never been transmitted over the air)
 *
 * Called when a packet needs to be repushed to the firmware.
 * First update sw descriptor and then queue it in the retry list.
 */
static void bl_tx_retry(struct bl_hw *bl_hw, struct sk_buff *skb,
                           struct bl_txhdr *txhdr, bool sw_retry)
{
    struct bl_sw_txhdr *sw_txhdr = txhdr->sw_hdr;
    struct tx_cfm_tag *cfm = &txhdr->hw_hdr.cfm;
    struct bl_txq *txq = sw_txhdr->txq;

    if (!sw_retry) {
        /* update sw desc */
        sw_txhdr->desc.host.sn = cfm->sn;
        sw_txhdr->desc.host.pn[0] = cfm->pn[0];
        sw_txhdr->desc.host.pn[1] = cfm->pn[1];
        sw_txhdr->desc.host.pn[2] = cfm->pn[2];
        sw_txhdr->desc.host.pn[3] = cfm->pn[3];
        sw_txhdr->desc.host.timestamp = cfm->timestamp;
        sw_txhdr->desc.host.flags |= TXU_CNTRL_RETRY;

        #ifdef CONFIG_BL_AMSDUS_TX
        if (sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU)
            bl_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].failed++;
        #endif
    }

    /* MORE_DATA will be re-set if needed when pkt will be repushed */
    sw_txhdr->desc.host.flags &= ~TXU_CNTRL_MORE_DATA;

    cfm->status.value = 0;
    txq->credits++;
    if (txq->credits > 0)
        bl_txq_start(txq, BL_TXQ_STOP_FULL);

    /* Queue the buffer */
    bl_txq_queue_skb(skb, txq, bl_hw, true);
}

#ifdef CONFIG_BL_AMSDUS_TX
/* return size of subframe (including header) */
static inline int bl_amsdu_subframe_length(struct ethhdr *eth, int eth_len)
{
    /* ethernet header is replaced with amdsu header that have the same size
       Only need to check if LLC/SNAP header will be added */
    int len = eth_len;

    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        len += sizeof(rfc1042_header) + 2;
    }

    return len;
}

static inline bool bl_amsdu_is_aggregable(struct sk_buff *skb)
{
    /* need to add some check on buffer to see if it can be aggregated ? */
    return true;
}


/**
 * bl_amsdu_del_subframe_header - remove AMSDU header
 *
 * amsdu_txhdr: amsdu tx descriptor
 *
 * Move back the ethernet header at the "beginning" of the data buffer.
 * (which has been moved in @bl_amsdu_add_subframe_header)
 */
static void bl_amsdu_del_subframe_header(struct bl_amsdu_txhdr *amsdu_txhdr)
{
    struct sk_buff *skb = amsdu_txhdr->skb;
    struct ethhdr *eth;
    u8 *pos;

    pos = skb->data;
    pos += sizeof(struct bl_amsdu_txhdr);
    eth = (struct ethhdr*)pos;
    pos += amsdu_txhdr->pad + sizeof(struct ethhdr);

    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        pos += sizeof(rfc1042_header) + 2;
    }

    memmove(pos, eth, sizeof(*eth));
    skb_pull(skb, (pos - skb->data));
}

/**
 * bl_amsdu_add_subframe_header - Add AMSDU header and link subframe
 *
 * @bl_hw Driver main data
 * @skb Buffer to aggregate
 * @sw_txhdr Tx descriptor for the first A-MSDU subframe
 *
 * return 0 on sucess, -1 otherwise
 *
 * This functions Add A-MSDU header and LLC/SNAP header in the buffer
 * and update sw_txhdr of the first subframe to link this buffer.
 * If an error happens, the buffer will be queued as a normal buffer.
 *
 *
 *            Before           After
 *         +-------------+  +-------------+
 *         | HEADROOM    |  | HEADROOM    |
 *         |             |  +-------------+ <- data
 *         |             |  | amsdu_txhdr |
 *         |             |  | * pad size  |
 *         |             |  +-------------+
 *         |             |  | ETH hdr     | keep original eth hdr
 *         |             |  |             | to restore it once transmitted
 *         |             |  +-------------+ <- packet_addr[x]
 *         |             |  | Pad         |
 *         |             |  +-------------+
 * data -> +-------------+  | AMSDU HDR   |
 *         | ETH hdr     |  +-------------+
 *         |             |  | LLC/SNAP    |
 *         +-------------+  +-------------+
 *         | DATA        |  | DATA        |
 *         |             |  |             |
 *         +-------------+  +-------------+
 *
 * Called with tx_lock hold
 */
static int bl_amsdu_add_subframe_header(struct bl_hw *bl_hw,
                              struct sk_buff *skb, struct bl_sw_txhdr *sw_txhdr)
{
    struct bl_amsdu *amsdu = &sw_txhdr->amsdu;
    struct bl_amsdu_txhdr *amsdu_txhdr;
    struct ethhdr *amsdu_hdr, *eth = (struct ethhdr *)skb->data;
    int headroom_need, map_len, msdu_len;
    dma_addr_t dma_addr;
    u8 *pos, *map_start;

    msdu_len = skb->len - sizeof(*eth);
    headroom_need = sizeof(*amsdu_txhdr) + amsdu->pad + sizeof(*amsdu_hdr);
        
    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        headroom_need += sizeof(rfc1042_header) + 2;
        msdu_len += sizeof(rfc1042_header) + 2;
    }

    /* we should have enough headroom (checked in xmit) */
    if (WARN_ON(skb_headroom(skb) < headroom_need)) {
        return -1;
    }

    /* allocate headroom */
    pos = skb_push(skb, headroom_need);
    amsdu_txhdr = (struct bl_amsdu_txhdr *)pos;
    pos += sizeof(*amsdu_txhdr);

    /* move eth header */
    memmove(pos, eth, sizeof(*eth));
    eth = (struct ethhdr *)pos;
    pos += sizeof(*eth);

    /* Add padding from previous subframe */
    map_start = pos;
    memset(pos, 0, amsdu->pad);
    pos += amsdu->pad;

    /* Add AMSDU hdr */
    amsdu_hdr = (struct ethhdr *)pos;
    memcpy(amsdu_hdr->h_dest, eth->h_dest, ETH_ALEN);
    memcpy(amsdu_hdr->h_source, eth->h_source, ETH_ALEN);
    amsdu_hdr->h_proto = htons(msdu_len);
    pos += sizeof(*amsdu_hdr);

    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN) {
        memcpy(pos, rfc1042_header, sizeof(rfc1042_header));
        pos += sizeof(rfc1042_header);
    }

    /* MAP (and sync) memory for DMA */
    map_len = msdu_len + amsdu->pad + sizeof(*amsdu_hdr);
    dma_addr = dma_map_single(bl_hw->dev, map_start, map_len,
                              DMA_BIDIRECTIONAL);
    if (WARN_ON(dma_mapping_error(bl_hw->dev, dma_addr))) {
        pos -= sizeof(*eth);
        memmove(pos, eth, sizeof(*eth));
        skb_pull(skb, headroom_need);
        return -1;
    }

    /* update amdsu_txhdr */
    amsdu_txhdr->map_len = map_len;
    amsdu_txhdr->dma_addr = dma_addr;
    amsdu_txhdr->skb = skb;
    amsdu_txhdr->pad = amsdu->pad;
    amsdu_txhdr->msdu_len = msdu_len;

    /* update bl_sw_txhdr (of the first subframe) */
    BUG_ON(amsdu->nb != sw_txhdr->desc.host.packet_cnt);
    sw_txhdr->desc.host.packet_addr[amsdu->nb] = dma_addr;
    sw_txhdr->desc.host.packet_len[amsdu->nb] = map_len;
    sw_txhdr->desc.host.packet_cnt++;
    amsdu->nb++;

    amsdu->pad = AMSDU_PADDING(map_len - amsdu->pad);
    list_add_tail(&amsdu_txhdr->list, &amsdu->hdrs);
    amsdu->len += map_len;

    bl_ipc_sta_buffer(bl_hw, sw_txhdr->txq->sta,
                        sw_txhdr->txq->tid, msdu_len);

    return 0;
}

/**
 * bl_amsdu_add_subframe - Add this buffer as an A-MSDU subframe if possible
 *
 * @bl_hw Driver main data
 * @skb Buffer to aggregate if possible
 * @sta Destination STA
 * @txq sta's txq used for this buffer
 *
 * Tyr to aggregate the buffer in an A-MSDU. If it succeed then the
 * buffer is added as a new A-MSDU subframe with AMSDU and LLC/SNAP
 * headers added (so FW won't have to modify this subframe).
 *
 * To be added as subframe :
 * - sta must allow amsdu
 * - buffer must be aggregable (to be defined)
 * - at least one other aggregable buffer is pending in the queue
 *  or an a-msdu (with enough free space) is currently in progress
 *
 * returns true if buffer has been added as A-MDSP subframe, false otherwise
 *
 */
static bool bl_amsdu_add_subframe(struct bl_hw *bl_hw, struct sk_buff *skb,
                                             struct bl_sta *sta, struct bl_txq *txq)
{
    bool res = false;
    struct ethhdr *eth;
    unsigned long flags;
    
    /* Adjust the maximum number of MSDU allowed in A-MSDU */
    bl_adjust_amsdu_maxnb(bl_hw);

    /* immediately return if amsdu are not allowed for this sta */
    if (!txq->amsdu_len || bl_hw->mod_params->amsdu_maxnb < 2 ||
        !bl_amsdu_is_aggregable(skb))
        return false;

    BL_TX_LOCK(&bl_hw->tx_lock, flags);
    
    if (txq->amsdu) {
        /* aggreagation already in progress, add this buffer if enough space
           available, otherwise end the current amsdu */
        struct bl_sw_txhdr *sw_txhdr = txq->amsdu;
        
        eth = (struct ethhdr *)(skb->data);

        if (((sw_txhdr->amsdu.len + sw_txhdr->amsdu.pad +
               bl_amsdu_subframe_length(eth, skb->len)) > txq->amsdu_len) ||
             bl_amsdu_add_subframe_header(bl_hw, skb, sw_txhdr)) 
        {
            txq->amsdu = NULL;
            goto end;
        }

        if (sw_txhdr->amsdu.nb >= bl_hw->mod_params->amsdu_maxnb) {
            bl_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].done++;
            /* max number of subframes reached */
            txq->amsdu = NULL;
        }
    } else {
        /* Check if a new amsdu can be started with the previous buffer
           (if any) and this one */
        struct sk_buff *skb_prev = skb_peek_tail(&txq->sk_list);
        struct bl_txhdr *txhdr;
        struct bl_sw_txhdr *sw_txhdr;
        int len1, len2;

        if (!skb_prev || !bl_amsdu_is_aggregable(skb_prev))
            goto end;

        txhdr = (struct bl_txhdr *)skb_prev->data;
        sw_txhdr = txhdr->sw_hdr;
        if ((sw_txhdr->amsdu.len) ||
            (sw_txhdr->desc.host.flags & TXU_CNTRL_RETRY))
            /* previous buffer is already a complete amsdu or a retry */
            goto end;

        eth = (struct ethhdr *)(skb_prev->data + sw_txhdr->headroom);
        len1 = bl_amsdu_subframe_length(eth, (sw_txhdr->frame_len +
                                               sizeof(struct ethhdr)));

        eth = (struct ethhdr *)(skb->data);
        len2 = bl_amsdu_subframe_length(eth, skb->len);

        if (len1 + AMSDU_PADDING(len1) + len2 > txq->amsdu_len)
            /* not enough space to aggregate those two buffers */
            goto end;

        /* Add subframe header.
           Note: Fw will take care of adding AMDSU header for the first
           subframe while generating 802.11 MAC header */
        INIT_LIST_HEAD(&sw_txhdr->amsdu.hdrs);
        sw_txhdr->amsdu.len = len1;
        sw_txhdr->amsdu.nb = 1;
        sw_txhdr->amsdu.pad = AMSDU_PADDING(len1);
        
        if (bl_amsdu_add_subframe_header(bl_hw, skb, sw_txhdr))
            goto end;

        sw_txhdr->desc.host.flags |= TXU_CNTRL_AMSDU;

        if (sw_txhdr->amsdu.nb < bl_hw->mod_params->amsdu_maxnb)
            txq->amsdu = sw_txhdr;
        else
            bl_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].done++;
    }

    res = true;

  end:
    BL_TX_UNLOCK(&bl_hw->tx_lock);
    
    return res;
}
#endif /* CONFIG_BL_AMSDUS_TX */

static void skb_pacing_rate_update(struct sk_buff *skb, struct net_device *dev)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 10, 17)) 
    if(skb && skb->sk && (skb->sk->sk_protocol == 6)){    
        if((skb->sk->sk_pacing_rate >> 10) < bl_hw->mod_params->tcp_rate)
            skb->sk->sk_pacing_rate = (bl_hw->mod_params->tcp_rate << 10);
    }
#endif
}

/**
 * netdev_tx_t (*ndo_start_xmit)(struct sk_buff *skb,
 *                             struct net_device *dev);
 *    Called when a packet needs to be transmitted.
 *    Must return NETDEV_TX_OK , NETDEV_TX_BUSY.
 *        (can also return NETDEV_TX_LOCKED if NETIF_F_LLTX)
 *
 *  - Initialize the desciptor for this pkt (stored in skb before data)
 *  - Push the pkt in the corresponding Txq
 *  - If possible (i.e. credit available and not in PS) the pkt is pushed
 *    to fw
 */
netdev_tx_t bl_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;
    struct bl_txhdr *txhdr;
    struct bl_sw_txhdr *sw_txhdr;
    struct ethhdr *eth;
    struct txdesc_api *desc;
    struct bl_sta *sta;
    struct bl_txq *txq;
    int headroom;
    int max_headroom;
    int hdr_pads;
    u16 frame_len;
    u16 frame_oft;
    u8 tid;

    skb_pacing_rate_update(skb, dev);

    sk_pacing_shift_update(skb->sk, bl_hw->tcp_pacing_shift);
#if defined COFNIG_BL_PCIE
    max_headroom = sizeof(struct bl_txhdr) + 3;
#elif defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    /* FW append MAC header before real payload, we total got 14+32 byte for FW use.*/
    max_headroom = sizeof(struct bl_txhdr) + sizeof(struct txdesc_api) + 
                   sizeof(struct inf_hdr) + TX_MAX_MAC_HEADER_SIZE;
#endif

    /* check whether the current skb can be used */
    if (skb_shared(skb) || (skb_headroom(skb) < max_headroom) ||
        (skb_cloned(skb) && (dev->priv_flags & IFF_BRIDGE_PORT))) 
    {
        if (skb_cow_head(skb, max_headroom)) {
            printk("%s:skb_cow_head fail goto free\n", __func__);
            
            goto free;
        }
    }

    /* Get the STA id and TID information */
    sta = bl_get_tx_info(bl_vif, skb, &tid);
    if (!sta) {
        BL_DBG("%s:get sta NULL, vif_idx=%d, tid=%d, skb->goto free\n", 
               __func__, bl_vif->vif_index, tid);
        
        goto free;
    }

    txq = bl_txq_sta_get(sta, tid, bl_hw);
    if (txq->idx == TXQ_INACTIVE) {
        BL_DBG("%s:txq->idx=TXQ_INACTIVE, go to free\n", __func__);
        
        goto free;
    }
    
    if (txq->hwq == NULL) {
        printk("%s, txq:0x%p, txq->hwq:0x%p, tid:%d, sta_idx:%d, mapping idx:%d\n", 
               __func__, txq, txq->hwq, tid, sta->sta_idx, 
               skb_get_queue_mapping(skb));
               
        goto free;
    }

#ifdef CONFIG_BL_AMSDUS_TX
    if (bl_amsdu_add_subframe(bl_hw, skb, sta, txq))
        return NETDEV_TX_OK;
#endif

    /* Retrieve the pointer to the Ethernet data */
    eth = (struct ethhdr *)skb->data;

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    hdr_pads  = TX_MAX_MAC_HEADER_SIZE;
    headroom  = sizeof(struct bl_txhdr);
    skb_push(skb, sizeof(struct txdesc_api) + 
             TX_MAX_MAC_HEADER_SIZE - sizeof(struct ethhdr));
    skb_push(skb, sizeof(struct inf_hdr));
    skb_push(skb, headroom);
#endif

    txhdr = (struct bl_txhdr *)skb->data;
    sw_txhdr = kmem_cache_alloc(bl_hw->sw_txhdr_cache, GFP_ATOMIC);
    if (unlikely(sw_txhdr == NULL)) {
        printk("%s:sw_txhdr == NULL, goto free\n", __func__);
        
        goto free;
    }
    
    txhdr->sw_hdr = sw_txhdr;
    desc = &sw_txhdr->desc;

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    frame_len = (u16)skb->len - headroom - sizeof(struct inf_hdr) - 
                sizeof(struct txdesc_api) - hdr_pads;
    sw_txhdr->hdr.type = BL_TYPE_DATA;
    sw_txhdr->hdr.len = skb->len - headroom;
    sw_txhdr->hdr.queue_idx = txq->hwq->id;
    sw_txhdr->hdr.reserved = g_pkt_sn;
    g_pkt_sn++;
#endif

    BL_DBG_DATA("%s sn=%d ethertype=0x%x skblen=%d\n", 
           __func__, g_pkt_sn, eth->h_proto, skb->len);
           
    sw_txhdr->txq       = txq;
    sw_txhdr->frame_len = frame_len;
    sw_txhdr->bl_sta  = sta;
    sw_txhdr->bl_vif  = bl_vif;
    sw_txhdr->skb       = skb;
    sw_txhdr->headroom  = headroom;
    //sw_txhdr->map_len   = skb->len - offsetof(struct bl_txhdr, hw_hdr);
    sw_txhdr->jiffies   = jiffies;

#ifdef CONFIG_BL_AMSDUS_TX
    sw_txhdr->amsdu.len = 0;
    sw_txhdr->amsdu.nb = 0;
#endif
    // Fill-in the descriptor
    memcpy(&desc->host.eth_dest_addr, eth->h_dest, ETH_ALEN);
    memcpy(&desc->host.eth_src_addr, eth->h_source, ETH_ALEN);
    desc->host.ethertype = eth->h_proto;
    desc->host.staid = sta->sta_idx;
    desc->host.tid = tid;
    
    if (unlikely(bl_vif->wdev.iftype == NL80211_IFTYPE_AP_VLAN))
        desc->host.vif_idx = bl_vif->ap_vlan.master->vif_index;
    else
        desc->host.vif_idx = bl_vif->vif_index;

    BL_DBG_DATA("xmit vif=%d self=%pM Peer=%pM; DA=%pM SA=%pM, host.sn:%u\n", 
           bl_vif->wdev.iftype,
           bl_vif->wdev.netdev->dev_addr, sta->mac_addr,
           eth->h_dest, eth->h_source, g_pkt_sn);

    if (bl_vif->use_4addr && (sta->sta_idx < NX_REMOTE_STA_MAX)) {
        if (bl_hw->mod_params->opmode == BL_OPMODE_REPEATER) {
            desc->host.flags = 0;
            
            if (bl_vif->wdev.iftype == NL80211_IFTYPE_STATION) {
                if(is_multicast_ether_addr((u8_l *)&desc->host.eth_dest_addr.array) || 
                   is_broadcast_ether_addr((u8_l *)&desc->host.eth_dest_addr.array)) 
                {
                    bl_rpt_dhcp_flags_change(bl_hw, (u8 *)eth);
                }
                
                bl_rpt_eth_mac_change(bl_vif, (u8 *)eth, true);
            }
        } else {
            desc->host.flags = TXU_CNTRL_USE_4ADDR;
        }
    } else {
        desc->host.flags = 0;
    }
    
    /* Not the realy MAC head SN, just for debug use */
    desc->host.sn = g_pkt_sn;

    if ((bl_vif->tdls_status == TDLS_LINK_ACTIVE) &&
         bl_vif->sta.tdls_sta &&
        (memcmp(desc->host.eth_dest_addr.array,
                bl_vif->sta.tdls_sta->mac_addr, ETH_ALEN) == 0)) 
    {
        desc->host.flags |= TXU_CNTRL_TDLS;
        bl_vif->sta.tdls_sta->tdls.last_tid = desc->host.tid;
        bl_vif->sta.tdls_sta->tdls.last_sn = desc->host.sn;
    }

    if (bl_vif->wdev.iftype == NL80211_IFTYPE_MESH_POINT) {
        if (bl_vif->is_resending) {
            desc->host.flags |= TXU_CNTRL_MESH_FWD;
        }
    }

    desc->host.packet_len[0] = frame_len;

    txhdr->hw_hdr.cfm.status.value = 0;

#if defined CONFIG_BL_SDIO|| defined CONFIG_BL_USB
    /* Fill-in TX descriptor */
    frame_oft = sizeof(struct bl_txhdr) - offsetof(struct bl_txhdr, hw_hdr)
                + sizeof(*eth);
#endif

    desc->host.packet_addr[0] = g_pkt_sn;
#ifdef CONFIG_BL_SPLIT_TX_BUF
    //desc->host.packet_addr[0] = sw_txhdr->dma_addr + frame_oft;
    desc->host.packet_cnt = 1;
#endif
    //desc->host.status_desc_addr = sw_txhdr->dma_addr;
    desc->host.host_tx_padding = hdr_pads; 

    if (bl_txq_queue_skb(skb, txq, bl_hw, false)) {
        /* Update statistics */
        bl_vif->net_stats.tx_packets++;
        bl_vif->net_stats.tx_bytes += frame_len;
        
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
        bl_queue_main_work(bl_hw);
#endif
    } else {
        return NETDEV_TX_OK;
    }

    return NETDEV_TX_OK;

free:
    dev_kfree_skb_any(skb);

    return NETDEV_TX_OK;
}

/**
 * bl_start_mgmt_xmit - Transmit a management frame
 *
 * @vif: Vif that send the frame
 * @sta: Destination of the frame. May be NULL if the destiantion is unknown
 *       to the AP.
 * @buf: pointer to buf
 * @len: buf length
 * @offchan: Indicate whether the frame must be send via the offchan TXQ.
 *           (is is redundant with params->offchan ?)
 * @cookie: updated with a unique value to identify the frame with upper layer
 *
 */
int bl_start_mgmt_xmit(struct bl_vif *vif, struct bl_sta *sta,
                              const u8 *buf, size_t len, bool no_cck,
                              int n_csa_offsets, const u16 *csa_offsets,
                              bool offchan, u64 *cookie)

{
    struct bl_hw *bl_hw = vif->bl_hw;
    struct bl_txhdr *txhdr;
    struct bl_sw_txhdr *sw_txhdr;
    struct txdesc_api *desc;
    struct sk_buff *skb;
    u16 frame_len, headroom, frame_oft;
    u8 *data;
    struct bl_txq *txq;
    bool robust;
    struct ieee80211_mgmt *mgmt = (void *)buf;
    u8 opmode[3] = {0xc7, 0x01, 0x01};
    bool add_opmode = false;

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    headroom = sizeof(struct bl_txhdr) + 
               sizeof(struct txdesc_api) + sizeof(struct inf_hdr);
#endif
    frame_len = len;

    BL_DBG("%s, mgmt->frame_control:0x%x, sa:%02x:%02x:%02x:%02x:%02x:%02x, \
           da:%02x:%02x:%02x:%02x:%02x:%02x\r\n", 
           __func__, mgmt->frame_control, 
           mgmt->sa[0], mgmt->sa[1], mgmt->sa[2],
           mgmt->sa[3], mgmt->sa[4], mgmt->sa[5],
           mgmt->da[0], mgmt->da[1], mgmt->da[2], 
           mgmt->da[3], mgmt->da[4], mgmt->da[5]);

    if (bl_hw->mod_params->he_on) {
        if (ieee80211_is_assoc_resp(mgmt->frame_control) ||
            ieee80211_is_reassoc_resp(mgmt->frame_control))
        {
            if((((mgmt->u.assoc_resp.variable[92]) & 0x02) == 0) ||
               (((mgmt->u.reassoc_resp.variable[92]) & 0x02) == 0)) 
            {
                opmode[2] = 0x0;
            }

            add_opmode = true;
            len +=3;
        }
    }

    /* Set TID and Queues indexes */
    if (sta) {
        txq = bl_txq_sta_get(sta, 8, bl_hw);
    } else {
        if (offchan)
            txq = &bl_hw->txq[NX_OFF_CHAN_TXQ_IDX];
        else
            txq = bl_txq_vif_get(vif, NX_UNK_TXQ_TYPE);
    }

    /* Ensure that TXQ is active */
    if (txq->idx == TXQ_INACTIVE) {
        netdev_dbg(vif->ndev, "TXQ inactive\n");
        
        return -EBUSY;
    }

    /*
     * Create a SK Buff object that will contain the provided data
     */
    skb = dev_alloc_skb(headroom + len);
    if (!skb) {
        return -ENOMEM;
    }

    *cookie = (unsigned long)skb;

    /*
     * Move skb->data pointer in order to reserve room for bl_txhdr
     * headroom value will be equal to sizeof(struct bl_txhdr)
     */
    skb_reserve(skb, headroom);

    /*
     * Extend the buffer data area in order to contain the provided packet
     * len value (for skb) will be equal to param->len
     */
    if (bl_hw->mod_params->he_on)
        data = skb_put(skb, len);
    else
        data = skb_put(skb, frame_len);
    /* Copy the provided data */
    memcpy(data, buf, frame_len);
    
    robust = ieee80211_is_robust_mgmt_frame(skb);
    
    if(add_opmode)
        memcpy(data + frame_len, opmode, 3);
        
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    /* Update CSA counter if present */
    if (unlikely(n_csa_offsets) &&
        vif->wdev.iftype == NL80211_IFTYPE_AP &&
        vif->ap.csa) {
        int i;

        data = skb->data;
        for (i = 0; i < n_csa_offsets ; i++) {
            data[csa_offsets[i]] = vif->ap.csa->count;
        }
    }
#endif

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    skb_push(skb, sizeof(struct txdesc_api));
    skb_push(skb, sizeof(struct inf_hdr));
    headroom = sizeof(struct bl_txhdr);
#endif
    /*
     * Go back to the beginning of the allocated data area
     * skb->data pointer will move backward
     */
    skb_push(skb, headroom);

    /* Fill the TX Header */
    txhdr = (struct bl_txhdr *)skb->data;
    txhdr->hw_hdr.cfm.status.value = 0;

    /* Fill the SW TX Header */
    sw_txhdr = kmem_cache_alloc(bl_hw->sw_txhdr_cache, GFP_ATOMIC);
    if (unlikely(sw_txhdr == NULL)) {
        dev_kfree_skb(skb);
        return -ENOMEM;
    }
    txhdr->sw_hdr = sw_txhdr;

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    sw_txhdr->hdr.type = BL_TYPE_DATA;
    sw_txhdr->hdr.len = len + sizeof(struct txdesc_api) + sizeof(struct inf_hdr);
    sw_txhdr->hdr.queue_idx = txq->hwq->id;
    sw_txhdr->hdr.reserved = 0;

    BL_DBG("xmit_mgmt_pkt_sn: hw_idx=%d, frame_len=%u, sn=%u\n",
           txq->hwq->id, len, g_pkt_sn);
    g_pkt_sn++;
#endif
    sw_txhdr->txq = txq;
    sw_txhdr->frame_len = len;
    sw_txhdr->bl_sta = sta;
    sw_txhdr->bl_vif = vif;
    sw_txhdr->skb = skb;
    sw_txhdr->headroom = headroom;
    //sw_txhdr->map_len = skb->len - offsetof(struct bl_txhdr, hw_hdr);
#ifdef CONFIG_BL_AMSDUS_TX
    sw_txhdr->amsdu.len = 0;
    sw_txhdr->amsdu.nb = 0;
#endif
    sw_txhdr->jiffies = jiffies;

    /* Fill the Descriptor to be provided to the MAC SW */
    desc = &sw_txhdr->desc;
    desc->host.staid = (sta) ? sta->sta_idx : 0xFF;
    desc->host.vif_idx = vif->vif_index;
    desc->host.tid = 0xFF;
    desc->host.flags = TXU_CNTRL_MGMT;
    if (robust)
        desc->host.flags |= TXU_CNTRL_MGMT_ROBUST;

    desc->host.packet_len[0] = len;

    if (no_cck) {
        desc->host.flags |= TXU_CNTRL_MGMT_NO_CCK;
    }
    
    /* Not the realy MAC head SN, just for debug use */
    desc->host.sn = g_pkt_sn;

    frame_oft = sizeof(struct bl_txhdr) - offsetof(struct bl_txhdr, hw_hdr);
    desc->host.packet_addr[0] = sw_txhdr->dma_addr + frame_oft;
#ifdef CONFIG_BL_SPLIT_TX_BUF
    desc->host.packet_cnt = 1;
#endif
    //desc->host.status_desc_addr = sw_txhdr->dma_addr;
    desc->host.host_tx_padding = 0; 

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    if (bl_txq_queue_skb(skb, txq, bl_hw, false)){
        BL_DBG("mgmt_xmit: queue_skb success success!\n");
        bl_queue_main_work(bl_hw);
    }else{
        BL_DBG("mgmt_xmit: queue_skb success failed!\n");
        
        return NETDEV_TX_OK;
    }
#endif

    return 0;
}

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
int bl_txdatacfm(void *pthis, void *host_id, void *_hw_hdr, void **txq_saved)
{
    struct bl_hw *bl_hw = (struct bl_hw *)pthis;
    struct bl_hw_txhdr *hw_hdr = (struct bl_hw_txhdr *)_hw_hdr;
    struct sk_buff *skb = host_id;
    struct bl_txhdr *txhdr;
    union  bl_hw_txstatus bl_txst;
    struct bl_sw_txhdr *sw_txhdr;
    struct bl_hwq *hwq;
    struct bl_txq *txq;

    txhdr = (struct bl_txhdr *)skb->data;
    sw_txhdr = txhdr->sw_hdr;
    bl_txst = hw_hdr->cfm.status;

    memcpy(&txhdr->hw_hdr, hw_hdr, sizeof(struct bl_hw_txhdr));

    txq = sw_txhdr->txq;
    *txq_saved = txq;
    /* don't use txq->hwq as it may have changed between push and confirm */
    hwq = &bl_hw->hwq[sw_txhdr->hw_queue];
    if(hwq == NULL) {
        printk("txdatacfm: hwq is NULL!\n");
        return -1;
    }

    bl_txq_confirm_any(bl_hw, txq, hwq, sw_txhdr);

    /* Update txq and HW queue credits */
    if (sw_txhdr->desc.host.flags & TXU_CNTRL_MGMT) {
        /* Confirm transmission to CFG80211 */
        cfg80211_mgmt_tx_status(&sw_txhdr->bl_vif->wdev,
                                (unsigned long)skb,
                                (skb->data + sw_txhdr->headroom + 
                                sizeof(struct txdesc_api) + sizeof(struct inf_hdr)),
                                sw_txhdr->frame_len,
                                !(bl_txst.retry_required || bl_txst.sw_retry_required),
                                GFP_ATOMIC);
    }
    else if ((txq->idx != TXQ_INACTIVE) &&
             (bl_txst.retry_required || bl_txst.sw_retry_required)) 
    {
        bool sw_retry = (bl_txst.sw_retry_required) ? true : false;

        /* Reset the status */
        txhdr->hw_hdr.cfm.status.value = 0;

        /* The confirmed packet was part of an AMPDU and not acked
         * correctly, so reinject it in the TX path to be retried */
        bl_tx_retry(bl_hw, skb, txhdr, sw_retry);
        
        return 0;
    }

    /* Update statistics */
    sw_txhdr->bl_vif->net_stats.tx_packets++;
    sw_txhdr->bl_vif->net_stats.tx_bytes += sw_txhdr->frame_len;

    /* Release SKBs */
#ifdef CONFIG_AMSDUS_TX
    if (sw_txhdr->desc.host.flags & TXU_CNTRL_AMSDU) {
        struct amsdu_txhdr *amsdu_txhdr;
        
        list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
            amsdu_del_subframe_header(amsdu_txhdr);
            dev_kfree_skb_any(amsdu_txhdr->skb);
        }
    }
#endif /* CONFIG_AMSDUS_TX */

    kmem_cache_free(bl_hw->sw_txhdr_cache, sw_txhdr);
    skb_pull(skb, sw_txhdr->headroom);
    
    BL_DBG("txdata_cfm, free skb=%p, sn=%u\n", skb, hw_hdr->cfm.sn);
    
    dev_kfree_skb_any(skb);

    return 0;
}
#endif

/**
 * bl_txq_credit_update - Update credit for one txq
 *
 * @bl_hw: Driver main data
 * @sta_idx: STA idx
 * @tid: TID
 * @update: offset to apply in txq credits
 *
 * Called when fw send ME_TX_CREDITS_UPDATE_IND message.
 * Apply @update to txq credits, and stop/start the txq if needed
 */
void bl_txq_credit_update(struct bl_hw *bl_hw, int sta_idx, u8 tid,
                                s8 update)
{
    struct bl_sta *sta = &bl_hw->sta_table[sta_idx];
    struct bl_txq *txq;
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif
    
    txq = bl_txq_sta_get(sta, tid, bl_hw);

    BL_DBG("%s, txq%d credits%d update%d\n",
           __func__, txq->idx, txq->credits, update);

    BL_TX_LOCK(&bl_hw->tx_lock, flags);

    if (txq->idx != TXQ_INACTIVE) {
        txq->credits += update;
        
#if 0
        if (txq->credits <= 0)
            bl_txq_stop(txq, BL_TXQ_STOP_FULL);
        else
            bl_txq_start(txq, BL_TXQ_STOP_FULL);
#endif
    }

    BL_TX_UNLOCK(&bl_hw->tx_lock, flags);
}


static inline void bl_tcp_ack_timer_stop(struct bl_tcp_stream * tcp_stream)
{
    if (tcp_stream->is_timer_set) {
        if(in_irq() || in_atomic() || irqs_disabled())
            del_timer(&tcp_stream->tcp_ack_timer);
        else
            del_timer_sync(&tcp_stream->tcp_ack_timer);
    }
    
    tcp_stream->is_timer_set = false;
    tcp_stream->is_timer_to = false;
}

static inline void bl_tcp_ack_timer_start(struct bl_tcp_stream * tcp_stream)
{
    mod_timer(&tcp_stream->tcp_ack_timer, jiffies + 
              tcp_stream->bl_hw->mod_params->tcp_ack_flush_to);
    tcp_stream->is_timer_set = true;
}

static inline void bl_tcp_ack_drop(struct bl_tcp_stream * tcp_stream, int prev)
{
    struct bl_txhdr * txhdr = NULL;
    struct bl_hw * bl_hw = tcp_stream->bl_hw;
    struct sk_buff  * skb =prev? tcp_stream->ack_skb_priv : tcp_stream->ack_skb_new;

    if(prev)
        tcp_stream->ack_skb_priv = NULL;
    else 
        tcp_stream->ack_skb_new  = NULL;

    if (skb && bl_hw) {
        txhdr = (struct bl_txhdr *)skb->data;
        kmem_cache_free(bl_hw->sw_txhdr_cache, txhdr->sw_hdr);
        consume_skb(skb);        
    }
}

static void bl_tcp_ack_flush(struct bl_tcp_stream * tcp_stream)
{
    bl_tcp_ack_drop(tcp_stream, 1);
    bl_tcp_ack_drop(tcp_stream, 0);
    bl_tcp_ack_timer_stop(tcp_stream);

    tcp_stream->tcp_ack_cnt  = 0;
    tcp_stream->ack_skb_new  = NULL;
    tcp_stream->txq = NULL;
}

static void bl_tcp_ack_pend(struct bl_tcp_stream * tcp_stream, 
                                  struct sk_buff * skb, struct bl_txq * txq)
{
    bl_tcp_ack_drop(tcp_stream, 1);
    bl_tcp_ack_timer_stop(tcp_stream);

    tcp_stream->tcp_ack_cnt++;
    tcp_stream->ack_skb_priv = tcp_stream->ack_skb_new;
    tcp_stream->ack_skb_new = skb;
    tcp_stream->txq = txq;

    bl_tcp_ack_timer_start(tcp_stream);
}

void bl_tcp_ack_stream_clear(struct bl_hw * bl_hw)
{
    struct bl_tcp_stream *tcp_stream = NULL, *node = NULL;

    list_for_each_entry_safe(tcp_stream, node, &bl_hw->tcp_stream_list, tcp_ack) {
        list_del(&tcp_stream->tcp_ack);
        
        if (tcp_stream->is_timer_set)
            bl_tcp_ack_flush(tcp_stream);
            
        kfree(tcp_stream);
    }
    INIT_LIST_HEAD(&bl_hw->tcp_stream_list);
}

static void bl_tcp_ack_timer_cb(struct timer_list *t)
{
    struct bl_tcp_stream * tcp_stream = from_timer(tcp_stream, t, tcp_ack_timer);
    struct bl_hw *bl_hw = tcp_stream->bl_hw;
    struct bl_txq * txq = tcp_stream->txq;
    struct sk_buff *skb = NULL;
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif

    if (!tcp_stream)
        return;

    BL_TX_LOCK(&bl_hw->tx_lock, flags);
    bl_tcp_ack_drop(tcp_stream, 1);
    skb = (struct sk_buff *)tcp_stream->ack_skb_new;
    tcp_stream->ack_skb_new = NULL;
    BL_TX_UNLOCK(&bl_hw->tx_lock, flags);
    
    if (skb) {
        BL_TX_LOCK(&bl_hw->tx_lock, flags);
        skb_queue_head(&txq->sk_list, skb);
        
        if (!bl_txq_is_stopped(txq)) {
            bl_txq_add_to_hw_list(txq);
            tcp_stream->is_timer_to = true;
            BL_TX_UNLOCK(&bl_hw->tx_lock, flags);
            
            bl_queue_main_work(bl_hw);
        } else {
            tcp_stream->is_timer_to = true;
            BL_TX_UNLOCK(&bl_hw->tx_lock, flags);
        }
    }

    return;
}

static struct bl_tcp_stream * bl_tcp_ack_get_stream(struct bl_hw *bl_hw, 
                                 struct iphdr * ip_hdr, struct tcphdr * tcp_hdr)
{
    struct bl_tcp_stream  *stream = NULL;

    list_for_each_entry (stream, &bl_hw->tcp_stream_list, tcp_ack) {
        if ((stream->saddr == ip_hdr->saddr) && 
            (stream->daddr == ip_hdr->daddr) &&
            (stream->source == tcp_hdr->source) && 
            (stream->dest == tcp_hdr->dest)) 
        {
            BL_DBG("%s ip:%x->%x port:%x->%x stream matched\n", __func__, 
                   stream->saddr, stream->daddr, stream->source, stream->dest);
                   
            return stream;
        }
    }
    BL_DBG("%s no matched tcp_ack_stream alloc a new one\n", __func__);

    stream = kzalloc(sizeof(struct bl_tcp_stream), GFP_ATOMIC);
    if (!stream)
        return NULL;

    stream->bl_hw  = bl_hw;
    stream->saddr  = ip_hdr->saddr;
    stream->daddr  = ip_hdr->daddr;
    stream->source = tcp_hdr->source;
    stream->dest   = tcp_hdr->dest;
    stream->tcp_ack_cnt = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    timer_setup(&stream->tcp_ack_timer, bl_tcp_ack_timer_cb, 0);
#else
    init_timer(&stream->tcp_ack_timer);
    stream->tcp_ack_timer.function = bl_tcp_ack_timer_cb;
    stream->tcp_ack_timer.data = (void *)&stream->tcp_ack_timer;
#endif

    list_add_tail(&stream->tcp_ack, &bl_hw->tcp_stream_list);

    return stream;
}


/*   return 0:  if we need normal tx,
 *   otherwise: we drop previous tcp_ack only keep latest tcp_ack,
 *              and directly return.
 */
int bl_tcp_ack_process(struct         bl_hw * bl_hw, 
                              struct sk_buff * skb, struct bl_txq * txq)
{
    int ret = 0;
    u32 old_ack_seq = 0;
    u16 headroom = 0;
    struct bl_tcp_stream *tcp_stream = NULL;
    struct ethhdr * eth_hdr = NULL;
    struct iphdr  * ip_hdr  = NULL;
    struct tcphdr * tcp_hdr = NULL;
    struct bl_txhdr * txhdr = (struct bl_txhdr *)skb->data;

    headroom = sizeof(struct bl_txhdr) + sizeof(struct inf_hdr) + 
               sizeof(struct txdesc_api) + TX_MAX_MAC_HEADER_SIZE - sizeof(struct ethhdr);
    eth_hdr = (struct ethhdr *)((u8 *)skb->data + headroom);

    if (txhdr->sw_hdr->desc.host.flags & TXU_CNTRL_MGMT)
        return 0;

    if (eth_hdr->h_proto != htons(ETH_P_IP))
        return 0;

    ip_hdr = (struct iphdr *)((u8 *)eth_hdr + ETH_HLEN);
    if (ip_hdr->protocol != IPPROTO_TCP)
        return 0;

    tcp_hdr = (struct tcphdr *)((u8 *)ip_hdr + (ip_hdr->ihl << 2));

    if (*((u8 *)tcp_hdr + BL_TCP_ACK_FLAG_OFFSET) == BL_TCP_ACK_FLAG) {
        if (ntohs(ip_hdr->tot_len) > ((ip_hdr->ihl + tcp_hdr->doff) << 2)) {
            //printk("WARN: tcp_ack with TCP payload, normal tx\n");
            return 0;
        }
        
        tcp_stream = bl_tcp_ack_get_stream(bl_hw, ip_hdr, tcp_hdr);
        if (!tcp_stream)
            return 0;

        old_ack_seq = tcp_stream->ack_seq;
        tcp_stream->ack_seq = ntohl(tcp_hdr->ack_seq);

        if (unlikely(tcp_stream->ack_seq < old_ack_seq)) {
            //printk("WARN: old tcp_ack, normal tx\n");
            return 0;
        }

        BL_DBG("%s ack_cnt=(%d,%d); old_skb=%p, new_skb=%p, eth=%p, ack_seq=%u, tcp_seq=%u\n", 
                __func__, 
                tcp_stream->tcp_ack_cnt, bl_hw->mod_params->tcp_ack_max,
                tcp_stream->ack_skb_priv, skb, eth_hdr, tcp_stream->ack_seq, 
                ntohl(tcp_hdr->seq));

        if (tcp_stream->tcp_ack_cnt > bl_hw->mod_params->tcp_ack_max || 
            tcp_stream->is_timer_to) 
        {
            bl_tcp_ack_flush(tcp_stream);
            ret = 0;
        } else {
            bl_tcp_ack_pend(tcp_stream, skb, txq);
            ret = 1;
        }
    }

    return ret;
}

void bl_skb_parsing(struct bl_hw *bl_hw, struct sk_buff *skb, 
                        u8 tx_rx, u16 data_offset)
{
    u16 eth_type = 0;
    u16 src_port = 0, dst_port = 0;
    struct ethhdr * eth_hdr = NULL;
    struct iphdr  * ip_hdr  = NULL;
    struct tcphdr * tcp_hdr = NULL;
    struct dhcp_info dhcp_info;
    struct arp_info arp_info;

    #define TX_RX   (tx_rx == 1 ? "TX" : "RX")

    eth_hdr = (struct ethhdr *)((u8 *)skb->data + data_offset);
    eth_type = ntohs(eth_hdr->h_proto);
    switch (eth_type) {
        case ETH_P_IPV6: //0x86DD
            break;
        case ETH_P_ARP:  //0x0806
            printk("%s ARP Packet Detected - Priority: %d\n", TX_RX, skb->priority);
            
            /* Parse ARP packet with data_offset using direct pointer calculation */
            {
                struct ethhdr *eth;
                struct arp_header *arp;
                u8 *arp_data;
                int min_packet_size;
                
                /* For both TX and RX paths, data_offset points to the Ethernet header */
                eth = (struct ethhdr *)((u8 *)skb->data + data_offset);
                arp_data = (u8 *)eth + sizeof(struct ethhdr);

                //bl_dump((uint8_t *)eth, skb->len-data_offset);
                
                /* For TX path, data_offset already includes all headers, so we only need Ethernet + ARP headers */
                if (tx_rx == 1) { /* TX path */
                    min_packet_size = sizeof(struct ethhdr) + sizeof(struct arp_header);
                } else { /* RX path */
                    min_packet_size = data_offset + sizeof(struct ethhdr) + sizeof(struct arp_header);
                }
                
                /* Check bounds for safety */
                if (((u8 *)skb->data + min_packet_size) > ((u8 *)skb->data + skb->len)) {
                    printk(KERN_WARNING "WiFi Driver: %s ARP packet too short (need %d, have %d)\n", 
                           TX_RX, min_packet_size, skb->len);
                    break;
                }
                
                arp = (struct arp_header *)arp_data;
                if (!arp) {
                    printk(KERN_WARNING "WiFi Driver: %s Failed to get ARP header\n", TX_RX);
                    break;
                }
                
                /* Extract ARP information directly */
                memset(&arp_info, 0, sizeof(struct arp_info));
                arp_info.hw_type = ntohs(arp->ar_hrd);
                //printk("format of hw addr:0x%p 0x%p\n", &arp->ar_hrd, &arp_info.hw_type);
                //bl_dump((uint8_t *)&arp->ar_hrd, 2);
                //bl_dump((uint8_t *)&arp_info.hw_type, 2);
                
                arp_info.proto_type = ntohs(arp->ar_pro);
                //printk("format of prot addr:0x%p 0x%p\n", &arp->ar_pro, &arp_info.proto_type);
                //bl_dump((uint8_t *)&arp->ar_pro, 2);
                //bl_dump((uint8_t *)&arp_info.proto_type, 2);
                
                arp_info.hw_len = arp->ar_hln;
                arp_info.proto_len = arp->ar_pln;
                
                arp_info.opcode = ntohs(arp->ar_op);
                //printk("opcode:0x%p 0x%p\n", &arp->ar_op, &arp_info.opcode);
                //bl_dump((uint8_t *)&arp->ar_op, 2);
                //bl_dump((uint8_t *)&arp_info.opcode, 2);
                
                /* Extract MAC addresses */
                memcpy(arp_info.sender_mac, arp->ar_sha, 6);
                //printk("send mac:0x%p 0x%p\n", arp->ar_sha, arp_info.sender_mac);
                //bl_dump(arp->ar_sha, 6);
                //bl_dump(arp_info.sender_mac, 6);

                /* Extract IP addresses - keep in network byte order for %pI4 format */
                arp_info.sender_ip = arp->ar_sip;
                //printk("send ip:0x%p 0x%p\n", &arp->ar_sip, &arp_info.sender_ip);
                //bl_dump((uint8_t *)&arp->ar_sip, 4);
                //bl_dump((uint8_t *)&arp_info.sender_ip, 4);
                
                memcpy(arp_info.target_mac, arp->ar_tha, 6);
                //printk("target mac:0x%p 0x%p\n", arp->ar_tha, arp_info.target_mac);
                //bl_dump(arp->ar_tha, 6);
                //bl_dump(arp_info.target_mac, 6);
                
                arp_info.target_ip = arp->ar_tip;
                //printk("target ip:0x%p 0x%p\n", &arp->ar_tip, &arp_info.target_ip);
                //bl_dump((uint8_t *)&arp->ar_tip, 4);
                //bl_dump((uint8_t *)&arp_info.target_ip, 4);
                
                /* Print ARP information for debugging */
                print_arp_info(&arp_info);
                
                /* Handle specific ARP operations */
                switch (arp_info.opcode) {
                case ARP_OP_REQUEST:
                    printk(KERN_INFO "WiFi Driver: %s ARP REQUEST - Who has %pI4? Tell %pM\n", 
                           TX_RX, &arp_info.target_ip, &arp_info.target_mac);
                    break;
                    
                case ARP_OP_REPLY:
                    printk(KERN_INFO "WiFi Driver: %s ARP REPLY - %pI4 is at %pM\n", 
                           TX_RX, &arp_info.sender_ip, &arp_info.sender_mac);
                    break;
                    
                case ARP_OP_RREQUEST:
                    printk(KERN_INFO "WiFi Driver: %s RARP REQUEST - Who is %pM? Tell %pI4\n", 
                           TX_RX, &arp_info.sender_mac, &arp_info.sender_ip);
                    break;
                    
                case ARP_OP_RREPLY:
                    printk(KERN_INFO "WiFi Driver: %s RARP REPLY - %pM is at %pI4\n", 
                           TX_RX, &arp_info.sender_mac, &arp_info.sender_ip);
                    break;
                    
                default:
                    printk(KERN_INFO "WiFi Driver: %s ARP UNKNOWN operation %d from %pM\n", 
                           TX_RX, arp_info.opcode, arp_info.sender_mac);
                    break;
                }
            }
            break;
            
        case ETH_P_RARP: //0x8035
            printk("%s RARP Packet Detected - Priority: %d\n", TX_RX, skb->priority);
            
            /* Parse RARP packet with data_offset using direct pointer calculation */
            {
                struct ethhdr *eth;
                struct arp_header *arp;
                u8 *arp_data;
                int min_packet_size;
                
                /* For both TX and RX paths, data_offset points to the Ethernet header */
                eth = (struct ethhdr *)((u8 *)skb->data + data_offset);
                arp_data = (u8 *)eth + sizeof(struct ethhdr);
                
                /* For TX path, data_offset already includes all headers, so we only need Ethernet + RARP headers */
                if (tx_rx == 1) { /* TX path */
                    min_packet_size = sizeof(struct ethhdr) + sizeof(struct arp_header);
                } else { /* RX path */
                    min_packet_size = data_offset + sizeof(struct ethhdr) + sizeof(struct arp_header);
                }
                
                /* Check bounds for safety */
                if (((u8 *)skb->data + min_packet_size) > ((u8 *)skb->data + skb->len)) {
                    printk(KERN_WARNING "WiFi Driver: %s RARP packet too short (need %d, have %d)\n", 
                           TX_RX, min_packet_size, skb->len);
                    break;
                }
                
                arp = (struct arp_header *)arp_data;
                if (!arp) {
                    printk(KERN_WARNING "WiFi Driver: %s Failed to get RARP header\n", TX_RX);
                    break;
                }
                
                /* Extract RARP information directly */
                memset(&arp_info, 0, sizeof(struct arp_info));
                arp_info.hw_type = ntohs(arp->ar_hrd);
                arp_info.proto_type = ntohs(arp->ar_pro);
                arp_info.hw_len = arp->ar_hln;
                arp_info.proto_len = arp->ar_pln;
                arp_info.opcode = ntohs(arp->ar_op);
                
                /* Extract MAC addresses */
                memcpy(arp_info.sender_mac, arp->ar_sha, 6);
                memcpy(arp_info.target_mac, arp->ar_tha, 6);
                
                /* Extract IP addresses - keep in network byte order for %pI4 format */
                arp_info.sender_ip = arp->ar_sip;
                arp_info.target_ip = arp->ar_tip;
                
                /* Print RARP information for debugging */
                print_arp_info(&arp_info);
                
                /* Handle specific RARP operations */
                switch (arp_info.opcode) {
                case ARP_OP_RREQUEST:
                    printk(KERN_INFO "WiFi Driver: %s RARP REQUEST - Who is %pM? Tell %pI4\n", 
                           TX_RX, &arp_info.sender_mac, &arp_info.sender_ip);
                    break;
                    
                case ARP_OP_RREPLY:
                    printk(KERN_INFO "WiFi Driver: %s RARP REPLY - %pM is at %pI4\n", 
                           TX_RX, &arp_info.sender_mac, &arp_info.sender_ip);
                    break;
                    
                default:
                    printk(KERN_INFO "WiFi Driver: %s RARP UNKNOWN operation %d from %pM\n", 
                           TX_RX, arp_info.opcode, arp_info.sender_mac);
                    break;
                }
            }
            break;
        case ETH_P_PAE:  //0x888E
            printk("%s EAPOL, len:%u\n", TX_RX, skb->len);
            //bl_dump((u8 *)skb->data+data_offset, skb->len-data_offset);
            break;
        case ETH_P_IP:   //0x0800
            ip_hdr = (struct iphdr *)((u8 *)eth_hdr + ETH_HLEN);
            
            //BL_DBG("%s IP 0x%x %d\n", TX_RX, ip_hdr->protocol, skb->priority);
            
            switch (ip_hdr->protocol) {
                case IPPROTO_ICMP://1
                    BL_DBG("%s ICMP\n", TX_RX);
                    break;
                case IPPROTO_IGMP://2
                    BL_DBG("%s IGMP\n", TX_RX);
                    break;
                case IPPROTO_TCP://6
                    tcp_hdr = (struct tcphdr *)((u8 *)ip_hdr + (ip_hdr->ihl << 2));
                    
                    if (*((u8 *)tcp_hdr + BL_TCP_ACK_FLAG_OFFSET) == BL_TCP_ACK_FLAG) 
                    {
                        BL_DBG("%s TCP_ACK:%d->%d, seq=%u, ack=%u, win:%d\n", TX_RX,
                                ntohs(tcp_hdr->source), ntohs(tcp_hdr->dest), 
                                ntohl(tcp_hdr->seq), ntohl(tcp_hdr->ack_seq), 
                                ntohs(tcp_hdr->window));
                    }
                    break;
                case IPPROTO_UDP://17
                    src_port = ntohs(*(u16 *)((u8 *)ip_hdr + 20));
                    dst_port = ntohs(*(u16 *)((u8 *)ip_hdr + 22));
                    
                    /* DHCP: Client Port 68, Server Port 67 */
                    if((src_port == 68 && dst_port == 67) || (src_port == 67 && dst_port == 68)){
                        printk("%s DHCP\n", TX_RX);
                    }

                    skb_pull(skb, data_offset);
                    /* Check if this is a DHCP packet */
                    if (is_dhcp_packet(skb)) {
                        /* Parse the DHCP packet */
                        if (parse_dhcp_packet(skb, &dhcp_info) == 0) {
                            /* Update statistics */
                            
                            /* Print DHCP information for debugging */
                            print_dhcp_info(&dhcp_info);
                            
                            /* Example: Handle specific DHCP messages */
                            switch (dhcp_info.message_type) {
                            case DHCP_DISCOVER:
                                printk(KERN_INFO "WiFi Driver: DHCP DISCOVER detected from %pM\n", 
                                       dhcp_info.client_mac);
                                break;
                                
                            case DHCP_REQUEST:
                                printk(KERN_INFO "WiFi Driver: DHCP REQUEST detected for IP %pI4\n", 
                                       &dhcp_info.your_ip);
                                break;
                                
                            case DHCP_OFFER:
                                printk(KERN_INFO "WiFi Driver: DHCP OFFER received, IP %pI4 offered to %pM\n", 
                                       &dhcp_info.your_ip, dhcp_info.client_mac);
                                break;
                                
                            case DHCP_ACK:
                                printk(KERN_INFO "WiFi Driver: DHCP ACK received, IP %pI4 assigned to %pM\n", 
                                       &dhcp_info.your_ip, dhcp_info.client_mac);
                                /* Could trigger network configuration updates here */
                                break;
                                
                            case DHCP_NAK:
                                printk(KERN_WARNING "WiFi Driver: DHCP NAK received for client %pM\n", 
                                          dhcp_info.client_mac);
                                break;
                            }
                        } else {
                            printk(KERN_WARNING "WiFi Driver: Failed to parse DHCP packet\n");
                        }
                    }
                    skb_push(skb, data_offset);
                    
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}


static void bl_rpt_dhcp_flags_change(struct bl_hw *bl_hw, u8 * eth_hdr)
{
    u16   eth_type = 0;
    u8 *  dhcp_ptr  = NULL;
    u16 * flags_ptr = NULL;
    struct iphdr  * ip_hdr  = NULL;
    struct udphdr * udp_hdr = NULL;

    eth_type = ntohs(((struct ethhdr *)eth_hdr)->h_proto);
    if (eth_type == ETH_P_IP) {
        ip_hdr = (struct iphdr *)((u8 *)eth_hdr + ETH_HLEN);
        if (ip_hdr->protocol == IPPROTO_UDP) {
            udp_hdr = (struct udphdr *)((u8 *)ip_hdr + (ip_hdr->ihl << 2));

            /* TX DHCP need change boot flag to broadcast */
            if(ntohs(udp_hdr->source) == BL_DHCP_CLIENT_PORT && 
               ntohs(udp_hdr->dest) == BL_DHCP_SERVER_PORT) 
            {
                int checksum = 0;
                
                dhcp_ptr = (u8 *)udp_hdr + sizeof(struct udphdr);

                if (ntohl(*(u32 *)(dhcp_ptr + BL_DHCP_MAGIC_OFFSET)) == BL_DHCP_MAGIC) {
                    flags_ptr = (u16 *)(dhcp_ptr + BL_DHCP_BOOTP_FLAG_OFFSET);
                    *flags_ptr |= htons(BL_DHCP_BOOTP_BROADCAST);

                    checksum = ~(udp_hdr->check) & 0xFFFF;
                    checksum += *flags_ptr;
                    while(checksum >> 16)
                        checksum = (checksum & 0xFFFF) + (checksum >> 16);
                    udp_hdr->check = ~checksum;
                }
            }
        }
    }

    return;
}

static u8 * bl_rpt_arp_mac_query(struct bl_vif * bl_vif, u32_l ip)
{
    int i = 0;
    struct bl_hw *bl_hw = bl_vif->bl_hw;
    struct bl_arp_table * curr_item = NULL;

    for (i = 0; i < BL_RPT_ARP_TABLE_SIZE; i++) {
        curr_item = &bl_hw->rpt_arp_table[i];

        BL_DBG("%s[%d]:ip=0x%x mac=%pM, query ip=0x%x\n", __func__, i,
            curr_item->ar_sip, curr_item->ar_sha, ip);

        if (curr_item->valid && (ip == curr_item->ar_sip)) {
            curr_item->ts = ktime_get();
            return curr_item->ar_sha;
        }
    }
    return NULL;
}

static void bl_rpt_arp_table_update(struct bl_vif * bl_vif, 
                                           u32_l arp_sip, u8_l * arp_sha)
{
    int i = 0;
    int valid_idx = -1, oldest_idx = 0;
    struct bl_arp_table * curr_item = NULL;
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    for (i = 0; i < BL_RPT_ARP_TABLE_SIZE; i++) {
        curr_item = &bl_hw->rpt_arp_table[i];
        
        if (curr_item->valid == 0) {
            valid_idx = i;
            break;
        }
        
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
        oldest_idx = (curr_item->ts.tv64 <= bl_hw->rpt_arp_table[oldest_idx].ts.tv64) ? i : oldest_idx;
#else
        oldest_idx = (curr_item->ts <= bl_hw->rpt_arp_table[oldest_idx].ts) ? i : oldest_idx;
#endif
    }

    BL_DBG("%s: valid_idx=%d oldest_idx=%d; old:ip=0x%x mac=%pM; new:ip=0x%x mac=%pM\n", 
           __func__, valid_idx, oldest_idx, curr_item->ar_sip, 
           curr_item->ar_sha, arp_sip, arp_sha);

    if (valid_idx < 0)
        valid_idx = oldest_idx;

    bl_hw->rpt_arp_table[valid_idx].ts = ktime_get();
    bl_hw->rpt_arp_table[valid_idx].valid = 1;
    bl_hw->rpt_arp_table[valid_idx].ar_sip = arp_sip;
    memcpy(bl_hw->rpt_arp_table[valid_idx].ar_sha, arp_sha, ETH_ALEN);
}

void bl_rpt_arp_table_init(struct bl_hw *bl_hw)
{
    memset(bl_hw->rpt_arp_table, 0, sizeof(bl_hw->rpt_arp_table));
}

void bl_rpt_eth_mac_change(struct bl_vif *bl_vif, u8 *eth_hdr, bool is_tx)
{
    u16 eth_type = 0;
    struct arphdr * arp_hdr = NULL;
    u32  arp_sip = 0, arp_tip = 0;
    u8 * arp_sha = NULL, *arp_tha = NULL;
    u8 * mac_ptr = NULL;

    eth_type = ntohs(((struct ethhdr *)eth_hdr)->h_proto);
    if (eth_type != ETH_P_ARP && eth_type != ETH_P_IP)
        return;

    if (likely(eth_type == ETH_P_IP)) {
        if (!is_tx) {
            struct iphdr  * ip_hdr  = NULL;
            
            ip_hdr = (struct iphdr *)((u8 *)eth_hdr + ETH_HLEN);
            if (ipv4_is_lbcast(ip_hdr->daddr) || ipv4_is_zeronet(ip_hdr->daddr)
                || ipv4_is_multicast(ip_hdr->daddr) 
                || ipv4_is_local_multicast(ip_hdr->daddr)) 
            {
                BL_DBG("Filter bc/mc IP data\n");
                return;
            }

            mac_ptr = bl_rpt_arp_mac_query(bl_vif, ip_hdr->daddr);
            
            BL_DBG("%s RX IP protocol=0x%x,saddr=0x%x,daddr=0x%x,arp_tha=0x%p\n", 
                   __func__, ip_hdr->protocol, ip_hdr->saddr, 
                   ip_hdr->daddr, mac_ptr);

            if (mac_ptr != NULL) {
                memcpy(((struct ethhdr *)eth_hdr)->h_dest, mac_ptr, ETH_ALEN);
                BL_DBG("ETH change to ip=0x%x, mac=0x%pM\n", ip_hdr->daddr, 
                       ((struct ethhdr *)eth_hdr)->h_dest);
            } else {
                BL_DBG("WARN %s IP daddr=0x%x unmatch with table\n", 
                       __func__, ip_hdr->daddr);
            }
        }
    } else {
        arp_hdr = (struct arphdr *)((u8 *)eth_hdr + ETH_HLEN);
        arp_sha = (u8 *)arp_hdr + sizeof(struct arphdr);
        arp_sip = *(u32 *)(arp_sha + ETH_ALEN);
        arp_tha = (u8 *)arp_sha + ETH_ALEN + sizeof(arp_sip);
        arp_tip = *(u32 *)(arp_tha + ETH_ALEN);
        
        BL_DBG("%s tx=%d ARP sip=0x%x,smac=0x%pM; tip=0x%x,tmac=0x%pM\n", 
               __func__, is_tx,
               arp_sip, arp_sha, arp_tip, arp_tha);

        if (is_tx) {
            if (arp_sip == 0)
                return;

            mac_ptr = bl_rpt_arp_mac_query(bl_vif, arp_sip);
            
            if (mac_ptr == NULL) {
                bl_rpt_arp_table_update(bl_vif, arp_sip, arp_sha);
            }
            memcpy(arp_sha, bl_vif->wdev.netdev->dev_addr, ETH_ALEN);
        } else {
            if (arp_tip == 0 || arp_sip == 0)
                return;

            mac_ptr = bl_rpt_arp_mac_query(bl_vif, arp_tip);
            if (mac_ptr != NULL) {
                memcpy(arp_tha, mac_ptr, ETH_ALEN);
                memcpy(((struct ethhdr *)eth_hdr)->h_dest, mac_ptr, ETH_ALEN);
                
                BL_DBG("ARP change to: ip=0x%x, mac=0x%pM\n", arp_tip, arp_tha);
            } else {
                BL_DBG("WARN: %s ARP tip=0x%x unmatch in table\n", 
                       __func__, arp_tip);
            }
        }
    }
    
    return;
}
