/**
 ****************************************************************************************
 *
 *  @file bl_msg_rx.c
 *
 *  @brief RX function definitions
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
#include "softmac.h"

#include "bl_defs.h"
#include "bl_tx.h"
#ifdef CONFIG_BL_BFMER
#include "bl_bfmer.h"
#endif //(CONFIG_BL_BFMER)
#ifdef CONFIG_BL_FULLMAC
#ifdef CONFIG_BL_DEBUGFS
#include "bl_debugfs.h"
#endif
#include "bl_msg_tx.h"
#include "bl_tdls.h"
#endif /* CONFIG_BL_FULLMAC */
#include "bl_events.h"
#include "bl_compat.h"
#include "bl_ipc_host.h"
#include "bl_irqs.h"
#include "bl_nl_events.h"
#include "bl_strs.h"

#include "softmac.h"

static u32_l bl_nl_bcst_seq = 0;

static int bl_freq_to_idx(struct bl_hw *bl_hw, int freq)
{
    struct ieee80211_supported_band *sband;
    int band, ch, idx = 0;

    for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
        sband = bl_hw->wiphy->bands[band];

        if (!sband) {
            continue;
        }

        for (ch = 0; ch < sband->n_channels; ch++, idx++) {
            if (sband->channels[ch].center_freq == freq) {
                goto exit;
            }
        }
    }

    BUG_ON(1);

exit:
    // Channel has been found, return the index
    return idx;
}

/***************************************************************************
 * Messages from MM task
 **************************************************************************/
static inline int bl_rx_chan_pre_switch_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct bl_vif *bl_vif;
    int chan_idx = ((struct mm_channel_pre_switch_ind *)msg->param)->chan_index;

    BL_DBG(BL_FN_ENTRY_STR);

    list_for_each_entry(bl_vif, &bl_hw->vifs, list) {
        if (bl_vif->up && bl_vif->ch_index == chan_idx) {
            bl_txq_vif_stop(bl_vif, BL_TXQ_STOP_CHAN, bl_hw);
        }
    }

    return 0;
}

static inline int bl_rx_chan_switch_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct bl_vif *bl_vif;
    int chan_idx = ((struct mm_channel_switch_ind *)msg->param)->chan_index;
    bool roc_req = ((struct mm_channel_switch_ind *)msg->param)->roc;
    bool roc_tdls = ((struct mm_channel_switch_ind *)msg->param)->roc_tdls;

    BL_DBG(BL_FN_ENTRY_STR);

    if (roc_tdls) {
        u8 vif_index = ((struct mm_channel_switch_ind *)msg->param)->vif_index;
        list_for_each_entry(bl_vif, &bl_hw->vifs, list) {
            if (bl_vif->vif_index == vif_index) {
                bl_vif->roc_tdls = true;
                bl_txq_tdls_sta_start(bl_vif, BL_TXQ_STOP_CHAN, bl_hw);
            }
        }
    } else if (!roc_req) {
        list_for_each_entry(bl_vif, &bl_hw->vifs, list) {
            if (bl_vif->up && bl_vif->ch_index == chan_idx) {
                bl_txq_vif_start(bl_vif, BL_TXQ_STOP_CHAN, bl_hw);
            }
        }
    } else {
        struct bl_roc *roc = bl_hw->roc;
        bl_vif = (roc)? roc->vif : NULL;
        
        if (!roc)
            return 0;
        
        if (!roc->internal) {
            // If RoC has been started by the user space, inform it that we have
            // switched on the requested off-channel
            cfg80211_ready_on_channel(&bl_vif->wdev, (u64)(bl_hw->roc_cookie),
                                      roc->chan, roc->duration, GFP_ATOMIC);
        }

        // Keep in mind that we have switched on the channel
        roc->on_chan = true;
        // Enable traffic on OFF channel queue
        bl_txq_offchan_start(bl_hw);
    }

    bl_hw->cur_chanctx = chan_idx;

    #ifdef CONFIG_BL_RADAR
    bl_radar_detection_enable_on_cur_channel(bl_hw);
    #endif
    
    return 0;
}

static inline int bl_rx_tdls_chan_switch_cfm(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    return 0;
}

static inline int bl_rx_tdls_chan_switch_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    // Enable traffic on OFF channel queue
    bl_txq_offchan_start(bl_hw);

    return 0;
}

static inline int bl_rx_tdls_chan_switch_base_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct bl_vif *bl_vif;
    u8 vif_index = ((struct tdls_chan_switch_base_ind *)msg->param)->vif_index;

    BL_DBG(BL_FN_ENTRY_STR);

    list_for_each_entry(bl_vif, &bl_hw->vifs, list) {
        if (bl_vif->vif_index == vif_index) {
            bl_vif->roc_tdls = false;
            bl_txq_tdls_sta_stop(bl_vif, BL_TXQ_STOP_CHAN, bl_hw);
        }
    }
    return 0;
}

static inline int bl_rx_tdls_peer_ps_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct bl_vif *bl_vif;
    u8 vif_index = ((struct tdls_peer_ps_ind *)msg->param)->vif_index;
    bool ps_on = ((struct tdls_peer_ps_ind *)msg->param)->ps_on;

    list_for_each_entry(bl_vif, &bl_hw->vifs, list) {
        if (bl_vif->vif_index == vif_index) {
            bl_vif->sta.tdls_sta->tdls.ps_on = ps_on;
            // Update PS status for the TDLS station
            bl_ps_bh_enable(bl_hw, bl_vif->sta.tdls_sta, ps_on);
        }
    }

    return 0;
}

static inline int bl_rx_remain_on_channel_exp_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct bl_roc *roc = bl_hw->roc;
    struct bl_vif *bl_vif = (roc)? roc->vif : NULL;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (!roc)
        return 0;
    
    if (!roc->internal && roc->on_chan) {
        // If RoC has been started by the user space and hasn't been cancelled,
        // inform it that off-channel period has expired
        cfg80211_remain_on_channel_expired(&bl_vif->wdev, (u64)(bl_hw->roc_cookie),
                                           roc->chan, GFP_ATOMIC);
    }

    bl_txq_offchan_deinit(bl_vif);

    // Increase the cookie (cannot be zero)
    bl_hw->roc_cookie++;
    if (bl_hw->roc_cookie == 0)
        bl_hw->roc_cookie = 1;

    kfree(roc);
    bl_hw->roc = NULL;

    return 0;
}

static inline int bl_rx_p2p_vif_ps_change_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    int vif_idx  = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->vif_index;
    int ps_state = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->ps_state;
    struct bl_vif *vif_entry;

    BL_DBG(BL_FN_ENTRY_STR);

    vif_entry = bl_hw->vif_table[vif_idx];

    if (vif_entry) {
        goto found_vif;
    }

    goto exit;

found_vif:

    if (ps_state == MM_PS_MODE_OFF) {
        // Start TX queues for provided VIF
        bl_txq_vif_start(vif_entry, BL_TXQ_STOP_VIF_PS, bl_hw);
    }
    else {
        // Stop TX queues for provided VIF
        bl_txq_vif_stop(vif_entry, BL_TXQ_STOP_VIF_PS, bl_hw);
    }

exit:
    return 0;
}

static inline int bl_rx_channel_survey_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mm_channel_survey_ind *ind = (struct mm_channel_survey_ind *)msg->param;
    // Get the channel index
    int idx = 0;
    // Get the survey
    struct bl_survey_info *bl_survey;

    BL_DBG(BL_FN_ENTRY_STR);

    BL_DBG_MSG("%s, ind->freq:%u\n", __func__, ind->freq);

    idx = bl_freq_to_idx(bl_hw, ind->freq);

    if (idx >  ARRAY_SIZE(bl_hw->survey))
        return 0;

    bl_survey = &bl_hw->survey[idx];

    // Store the received parameters
    bl_survey->chan_time_ms = ind->chan_time_ms;
    bl_survey->chan_time_busy_ms = ind->chan_time_busy_ms;
    bl_survey->noise_dbm = ind->noise_dbm;
    bl_survey->filled = (SURVEY_INFO_TIME |
                           SURVEY_INFO_TIME_BUSY);

    if (ind->noise_dbm != 0) {
        bl_survey->filled |= SURVEY_INFO_NOISE_DBM;
    }

    return 0;
}

static inline int bl_rx_p2p_noa_upd_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    return 0;
}

static inline int bl_rx_rssi_status_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mm_rssi_status_ind *ind = (struct mm_rssi_status_ind *)msg->param;
    int vif_idx  = ind->vif_index;
    bool rssi_status = ind->rssi_status;

    struct bl_vif *vif_entry;

    BL_DBG(BL_FN_ENTRY_STR);

    vif_entry = bl_hw->vif_table[vif_idx];
    if (vif_entry) {
        cfg80211_cqm_rssi_notify(vif_entry->ndev,
                                 rssi_status ? NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW :
                                               NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
                                 ind->rssi, GFP_ATOMIC);
    }

    return 0;
}

static inline int bl_rx_pktloss_notify_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_BL_FULLMAC
    struct mm_pktloss_ind *ind = (struct mm_pktloss_ind *)msg->param;
    struct bl_vif *vif_entry;
    int vif_idx  = ind->vif_index;

    BL_DBG(BL_FN_ENTRY_STR);

    vif_entry = bl_hw->vif_table[vif_idx];
    if (vif_entry) {
        cfg80211_cqm_pktloss_notify(vif_entry->ndev, 
                                    (const u8 *)ind->mac_addr.array,
                                    ind->num_packets, GFP_ATOMIC);
    }
#endif /* CONFIG_BL_FULLMAC */

    return 0;
}

static int bl_rx_dbg_ke_stat_cfm(struct bl_hw *bl_hw, 
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct dbg_ke_stat_cfm *cfm = (struct dbg_ke_stat_cfm *)msg->param;

    printk("ver:%u, ts:%u\n", cfm->ke_statis.v_ts>>22, cfm->ke_statis.v_ts&0x3fffff);

    printk("rxl_err_cnt:%u\n", cfm->ke_statis.rxl_err_cnt);
    printk("rxu_err_cnt:%u\n", cfm->ke_statis.rxu_err_cnt);
    printk("rxl_rx_cnt:%u\n", cfm->ke_statis.rxl_rx_cnt);
    printk("rxu_rx_cnt:%u\n", cfm->ke_statis.rxu_rx_cnt);
    printk("intf_upld_cnt:%u\n", cfm->ke_statis.intf_upld_cnt);

    printk("intf_err_cnt:%u\n", cfm->ke_statis.intf_err_cnt);
    printk("intf_tight_cnt:%u\n", cfm->ke_statis.intf_tight_cnt);

    printk("intf_dnld_cnt:%u\n", cfm->ke_statis.intf_dnld_cnt);
    printk("txu_tx_cnt:%u\n", cfm->ke_statis.txu_tx_cnt);
    printk("txu_tx_succ_cnt:%u\n", cfm->ke_statis.txu_tx_succ_cnt);
    printk("txu_tx_fail_cnt:%u\n", cfm->ke_statis.txu_tx_fail_cnt);
    printk("txu_tx_retry_cnt:%u\n", cfm->ke_statis.txu_tx_retry_cnt);
    printk("txl_int_tx_cnt:%u\n", cfm->ke_statis.txl_int_tx_cnt);
    printk("txu_err_cnt:%u\n", cfm->ke_statis.txu_err_cnt);
    printk("txl_err_cnt:%u\n", cfm->ke_statis.txl_err_cnt);
    printk("txu_postpone_accu_cnt:%u\n", cfm->ke_statis.txu_postpone_accu_cnt);

    printk("rec_cnt:%u\n", cfm->ke_statis.rec_cnt);
    printk("rer_cnt:%u\n", cfm->ke_statis.rer_cnt);
    
    //on time counting.
    printk("txu_postpone_cnt:%u\n", cfm->ke_statis.txu_postpone_cnt);

    printk("cfm_cnt:%u %u %u %u %u\n", 
           cfm->ke_statis.cfm_cnt[0],
           cfm->ke_statis.cfm_cnt[1],
           cfm->ke_statis.cfm_cnt[2],
           cfm->ke_statis.cfm_cnt[3],
           cfm->ke_statis.cfm_cnt[4]);
    printk("txing_cnt:%u %u %u %u %u\n", 
           cfm->ke_statis.txing_cnt[0],
           cfm->ke_statis.txing_cnt[1],
           cfm->ke_statis.txing_cnt[2],
           cfm->ke_statis.txing_cnt[3],
           cfm->ke_statis.txing_cnt[4]);
    printk("frm_free_cnt:%u\n", cfm->ke_statis.frm_free_cnt);

    printk("buf_alloc_cnt:%u\n", cfm->ke_statis.buf_alloc_cnt);
    printk("buf_free_cnt:%u\n", cfm->ke_statis.buf_free_cnt);

    printk("bam_tx_delay_cnt:%u %u %u %u\n",
           cfm->ke_statis.bam_tx_delay_cnt[0],
           cfm->ke_statis.bam_tx_delay_cnt[1],
           cfm->ke_statis.bam_tx_delay_cnt[2],
           cfm->ke_statis.bam_tx_delay_cnt[3]);
           
    printk("sta_pp_cnt:%u %u %u %u %u, %u %u %u %u %u, %u %u %u %u %u, %u %u %u %u %u, %u %u %u %u %u, %u %u %u %u %u\n", 
           cfm->ke_statis.sta_pp_cnt[0][0], cfm->ke_statis.sta_pp_cnt[0][1],
           cfm->ke_statis.sta_pp_cnt[0][2], cfm->ke_statis.sta_pp_cnt[0][3],
           cfm->ke_statis.sta_pp_cnt[0][4],
           cfm->ke_statis.sta_pp_cnt[1][0], cfm->ke_statis.sta_pp_cnt[1][1],
           cfm->ke_statis.sta_pp_cnt[1][2], cfm->ke_statis.sta_pp_cnt[1][3],
           cfm->ke_statis.sta_pp_cnt[1][4],
           cfm->ke_statis.sta_pp_cnt[2][0], cfm->ke_statis.sta_pp_cnt[2][1],
           cfm->ke_statis.sta_pp_cnt[2][2], cfm->ke_statis.sta_pp_cnt[2][3],
           cfm->ke_statis.sta_pp_cnt[2][4],
           cfm->ke_statis.sta_pp_cnt[3][0], cfm->ke_statis.sta_pp_cnt[3][1],
           cfm->ke_statis.sta_pp_cnt[3][2], cfm->ke_statis.sta_pp_cnt[3][3],
           cfm->ke_statis.sta_pp_cnt[3][4],
           cfm->ke_statis.sta_pp_cnt[4][0], cfm->ke_statis.sta_pp_cnt[4][1],
           cfm->ke_statis.sta_pp_cnt[4][2], cfm->ke_statis.sta_pp_cnt[4][3],
           cfm->ke_statis.sta_pp_cnt[4][4],
           cfm->ke_statis.sta_pp_cnt[5][0], cfm->ke_statis.sta_pp_cnt[5][1],
           cfm->ke_statis.sta_pp_cnt[5][2], cfm->ke_statis.sta_pp_cnt[5][3],
           cfm->ke_statis.sta_pp_cnt[5][4]);

    return 0;
}

static inline int bl_rx_csa_counter_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mm_csa_counter_ind *ind = (struct mm_csa_counter_ind *)msg->param;
    struct bl_vif *vif;
    bool found = false;

    BL_DBG(BL_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(vif, &bl_hw->vifs, list) {
        if (vif->vif_index == ind->vif_index) {
            found=true;
            break;
        }
    }

    if (found) {
        if (vif->ap.csa) {
            BL_DBG_MSG("%s, csa count:%u\r\n", __func__, ind->csa_count);
            vif->ap.csa->count = ind->csa_count;
        } else {
            netdev_err(vif->ndev, "CSA counter update but no active CSA");
        }
    }

    return 0;
}

static inline int bl_rx_csa_finish_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mm_csa_finish_ind *ind = (struct mm_csa_finish_ind *)msg->param;
    struct bl_vif *vif;
    bool found = false;

    BL_DBG(BL_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(vif, &bl_hw->vifs, list) {
        if (vif->vif_index == ind->vif_index) {
            found=true;
            break;
        }
    }

    BL_DBG("%s, vif_idx:%d, status:%d, chan_idx:%d, found:%d\n", __func__, 
           ind->vif_index, ind->status, ind->chan_idx, found);

    if (found) {
        if (ind->status == 0) {
            softmac_vif_update_local(ind->vif_index, VIF_FIELD_BSS_CHAN,
                                     (uint8_t *)&ind->chan, sizeof(ind->chan));
        }
        
        if (BL_VIF_TYPE(vif) == NL80211_IFTYPE_AP ||
            BL_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO) 
        {
            BL_DBG("%s, vif_idx:%d, csa:0x%p\n", 
                   __func__, vif->vif_index, vif->ap.csa);
                  
            if (vif->ap.csa) {
                vif->ap.csa->status = ind->status;
                vif->ap.csa->ch_idx = ind->chan_idx;
                schedule_work(&vif->ap.csa->work);
            } else
                netdev_err(vif->ndev, "CSA finish indication but no active CSA");
        } else {
            if (ind->status == 0) {
                bl_chanctx_unlink(vif);
                bl_chanctx_link(vif, ind->chan_idx, NULL);
                
                if (bl_hw->cur_chanctx == ind->chan_idx) {
                    #ifdef CONFIG_BL_RADAR
                    bl_radar_detection_enable_on_cur_channel(bl_hw);
                    #endif
                    
                    bl_txq_vif_start(vif, BL_TXQ_STOP_CHAN, bl_hw);
                } else {
                    bl_txq_vif_stop(vif, BL_TXQ_STOP_CHAN, bl_hw);
                }
            }
        }
    } else {
        printk("%s, fail\n", __func__);
    }

    return 0;
}

static inline int bl_rx_csa_traffic_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mm_csa_traffic_ind *ind = (struct mm_csa_traffic_ind *)msg->param;
    struct bl_vif *vif;
    bool found = false;

    BL_DBG(BL_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(vif, &bl_hw->vifs, list) {
        if (vif->vif_index == ind->vif_index) {
            found=true;
            break;
        }
    }

    if (found) {
        if (ind->enable)
            bl_txq_vif_start(vif, BL_TXQ_STOP_CSA, bl_hw);
        else
            bl_txq_vif_stop(vif, BL_TXQ_STOP_CSA, bl_hw);
    }

    return 0;
}

static inline int bl_rx_ps_change_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mm_ps_change_ind *ind = (struct mm_ps_change_ind *)msg->param;
    struct bl_sta *sta = &bl_hw->sta_table[ind->sta_idx];

    BL_DBG(BL_FN_ENTRY_STR);

    if (ind->sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
        wiphy_err(bl_hw->wiphy, "Invalid sta index reported by fw %d\n",
                  ind->sta_idx);
                  
        return 1;
    }

    //netdev_dbg(bl_hw->vif_table[sta->vif_idx]->ndev,
    //           "Sta %d, change PS mode to %s", sta->sta_idx,
    //           ind->ps_state ? "ON" : "OFF");

    if (sta->valid) {
        bl_ps_bh_enable(bl_hw, sta, ind->ps_state);
    } else if (test_bit(BL_DEV_ADDING_STA, &bl_hw->flags)) {
        sta->ps.active = ind->ps_state ? true : false;
    } else {
        netdev_err(bl_hw->vif_table[sta->vif_idx]->ndev,
                   "Ignore PS mode change on invalid sta\n");
    }

    return 0;
}

static inline int bl_rx_traffic_req_ind(struct bl_hw *bl_hw,
                                             struct bl_cmd *cmd,
                                             struct ipc_e2a_msg *msg)
{
    struct mm_traffic_req_ind *ind = (struct mm_traffic_req_ind *)msg->param;
    struct bl_sta *sta = &bl_hw->sta_table[ind->sta_idx];

    BL_DBG(BL_FN_ENTRY_STR);

    if (sta)
        BL_DBG("Sta 0x%p %d, mac:%02x %02x %02x %02x %02x %02x, asked for %d pkt\n",
                sta, sta->sta_idx,
                sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2], 
                sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5], 
                ind->pkt_cnt);

    bl_ps_bh_traffic_req(bl_hw, sta, ind->pkt_cnt,
                         ind->uapsd ? UAPSD_ID : LEGACY_PS_ID);

    return 0;
}
                                          
#if defined  BL_RX_REORDER && (defined CONFIG_BL_USB || defined CONFIG_BL_SDIO)
static inline int bl_addba_req_ind(struct bl_hw *bl_hw,
                                   struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mm_ba_add_req *ind = (struct mm_ba_add_req *)msg->param;
    struct rxreorder_list *reorder_list;    
    u8_l  i;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (ind->tid >= NX_NB_TID_PER_STA || 
        ind->sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX))
    {
        printk("invalid sta id %d or tid %d \n", ind->sta_idx, ind->tid);
        
        return 0;
    }
    
    BL_DBG("%s sta id %d tid %d start ssn %d  buf size %d\n", __func__,
           ind->sta_idx, ind->tid, le16_to_cpu(ind->ssn),ind->bufsz);

    reorder_list = &bl_hw->rx_reorder[ind->sta_idx][ind->tid];

    reorder_list->flag = true;
    reorder_list->check_start_win = true;
    reorder_list->start_win = le16_to_cpu(ind->ssn);
    reorder_list->win_size = RX_WIN_SIZE; //ind->bufsz;
    reorder_list->end_win = (reorder_list->start_win + RX_WIN_SIZE) % MAX_SEQ_VALUE;
    reorder_list->start_win_index = 0;
    reorder_list->flush = false;

    for (i=0; i< RX_WIN_SIZE; i++) {
        reorder_list->reorder_pkt[i] = NULL;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    timer_setup(&reorder_list->timer, bl_rx_reorder_flush, 0);
#else
    init_timer(&reorder_list->timer);
    reorder_list->timer.function = bl_rx_reorder_flush;
    reorder_list->timer.data = (void *)bl_hw;
#endif

    return 0;
}

static inline int bl_delba_req_ind(struct bl_hw *bl_hw,
                                   struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mm_ba_del_req *ind = (struct mm_ba_del_req *)msg->param;
    struct rxreorder_list *reorder_list;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (ind->tid >= NX_NB_TID_PER_STA || 
        ind->sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX))
    {
        printk("invalid sta id %d or tid %d \n", ind->sta_idx, ind->tid);
        
        return 0;
    }

    BL_DBG("%s sta id %d tid %d \n", __func__,ind->sta_idx, ind->tid);

    reorder_list = &bl_hw->rx_reorder[ind->sta_idx][ind->tid];

    reorder_list->flag = false;
    reorder_list->start_win = 0;
    reorder_list->end_win = 0;
    reorder_list->last_seq = 0;
    reorder_list->start_win_index = 0;
    reorder_list->flush = false;
    reorder_list->del_ba = true;
    bl_hw->flush_rx = true;

    bl_queue_rx_work(bl_hw);

    if (reorder_list->is_timer_set) {
        if (in_irq() || in_atomic() || irqs_disabled())
            del_timer(&reorder_list->timer);
        else    
            del_timer_sync(&reorder_list->timer);
    }
    
    reorder_list->is_timer_set = false;

    return 0;
}
#endif

/***************************************************************************
 * Messages from SCANU task
 **************************************************************************/
#ifdef CONFIG_BL_FULLMAC
void bl_nl_broadcast_event(struct bl_hw *bl_hw, u32 event_id,
                                  u8* payload, u16 len)
{
    struct sk_buff *skb = NULL;
    struct nlmsghdr *nl_hdr = NULL;
    struct sock *sk = bl_hw->netlink_sock;
    struct bl_nl_event nl_event;
    int ret = 0;

    if (sk) {
        skb = dev_alloc_skb(NLMSG_SPACE(BL_NL_BUF_MAX_LEN));
        if (!skb) {
            printk("ERR: no memory for nl msg\n");
            return;
        }

        memset(skb->data, 0, NLMSG_SPACE(BL_NL_BUF_MAX_LEN));

        nl_hdr = (struct nlmsghdr *)skb->data;
        nl_hdr->nlmsg_len = NLMSG_SPACE(len + sizeof(nl_event));
        nl_hdr->nlmsg_pid = 0;
        nl_hdr->nlmsg_flags = 0;
        nl_hdr->nlmsg_seq = bl_nl_bcst_seq++;

        BL_DBG("event_id=%d, nlmsg_len=%d seq=%d len=%d\n", 
                event_id, nl_hdr->nlmsg_len, nl_hdr->nlmsg_seq, len);

        skb_put(skb, nl_hdr->nlmsg_len);
        nl_event.event_id = event_id;
        nl_event.payload_len = len;
        memcpy(NLMSG_DATA(nl_hdr), &nl_event, sizeof(nl_event));
        memcpy((u8_l *)NLMSG_DATA(nl_hdr) + sizeof(nl_event), payload, len);

        NETLINK_CB(skb).portid = 0;
        NETLINK_CB(skb).dst_group = BL_NL_BCAST_GROUP_ID;

        ret = netlink_broadcast(sk, skb, 0, BL_NL_BCAST_GROUP_ID, GFP_ATOMIC);
        if (ret) {
            BL_DBG("WARN: broadcast fail ret = %d\n", ret);
        }
        
        BL_DBG("Finish event 0x%x broadcast\n", event_id);
    } else {
        BL_DBG("WARN: No available netlink socket\n");
    }
}

static inline int bl_rx_scanu_start_cfm(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    BL_DBG_MSG(BL_FN_ENTRY_STR);

    bl_ipc_elem_var_deallocs(bl_hw, &bl_hw->scan_ie);
    
    if (bl_hw->scan_request) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
        struct cfg80211_scan_info info = {
            .aborted = false,
        };

        cfg80211_scan_done(bl_hw->scan_request, &info);
#else
        cfg80211_scan_done(bl_hw->scan_request, false);
#endif
    }
    
    if (bl_hw->priv_scan.prob_req_en)
        bl_nl_broadcast_event(bl_hw, BL_EVENT_ID_SCAN_DONE, NULL, 0);

    bl_hw->scan_request = NULL;

    bl_queue_main_work(bl_hw);

    printk("bl_rx_scanu_start_cfm <==scan done\n");

    return 0;
}

static inline int bl_rx_scanu_result_ind(struct bl_hw *bl_hw,
                                   struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct cfg80211_bss *bss = NULL;
    struct ieee80211_channel *chan;
    struct scanu_result_ind *ind = (struct scanu_result_ind *)msg->param;

    BL_DBG(BL_FN_ENTRY_STR);

    BL_DBG_MSG("%s, len:%u, framectrl:0x%x, freq:%u, payload:0x%p, oft:%u\r\n", 
          __func__, ind->length, ind->framectrl, ind->center_freq,
          ind->payload,
          offsetof(struct ieee80211_mgmt, u.probe_resp.variable));

    //bl_dump((uint8_t *)ind->payload, ind->length);

    chan = ieee80211_get_channel(bl_hw->wiphy, ind->center_freq);

    if (chan != NULL) {
        bss = cfg80211_inform_bss_frame(bl_hw->wiphy, chan,
                                        (struct ieee80211_mgmt *)ind->payload,
                                        ind->length, (ind->rssi-2) * 100, 
                                        GFP_KERNEL);
    }
    
    if (bss != NULL) {
        BL_DBG_MSG("%s, bss->bssid:%02x:%02x:%02x:%02x:%02x:%02x\r\n",
                   __func__, bss->bssid[0], bss->bssid[1], bss->bssid[2],
                   bss->bssid[3], bss->bssid[4], bss->bssid[5]);
                   
        cfg80211_put_bss(bl_hw->wiphy, bss);
        
        if (bl_hw->priv_scan.prob_req_en)
            bl_nl_broadcast_event(bl_hw, BL_EVENT_ID_PROBE_RESPONSE, 
                                  (u8_l *)ind->payload, ind->length);
    }
    return 0;
}
#endif /* CONFIG_BL_FULLMAC */

/***************************************************************************
 * Messages from ME task
 **************************************************************************/
#ifdef CONFIG_BL_FULLMAC
static inline int bl_rx_me_tkip_mic_failure_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct me_tkip_mic_failure_ind *ind =
                      (struct me_tkip_mic_failure_ind *)msg->param;
    struct bl_vif *bl_vif = bl_hw->vif_table[ind->vif_idx];
    struct net_device *dev = bl_vif->ndev;

    BL_DBG(BL_FN_ENTRY_STR);

    cfg80211_michael_mic_failure(dev, (u8 *)&ind->addr, (ind->ga?NL80211_KEYTYPE_GROUP:
                                 NL80211_KEYTYPE_PAIRWISE), ind->keyid,
                                 (u8 *)&ind->tsc, GFP_ATOMIC);

    return 0;
}

static inline int bl_rx_me_tx_credits_update_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct me_tx_credits_update_ind *ind = 
                         (struct me_tx_credits_update_ind *)msg->param;

    BL_DBG(BL_FN_ENTRY_STR);

    bl_txq_credit_update(bl_hw, ind->sta_idx, ind->tid, ind->credits);

    return 0;
}
#endif /* CONFIG_BL_FULLMAC */


/***************************************************************************
 * Messages from SM task
 **************************************************************************/
#ifdef CONFIG_BL_FULLMAC
static inline int bl_rx_sm_connect_ind(struct bl_hw *bl_hw,
                                   struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct sm_connect_ind *ind = (struct sm_connect_ind *)msg->param;
    struct bl_vif *bl_vif = NULL;
    struct net_device *dev = NULL;
    const u8 *req_ie, *rsp_ie;
    const u8 *extcap_ie;
    const struct ieee_types_extcap *extcap;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX)) {
        printk("%s vif index invalid %d\n", __func__, ind->vif_idx);
        
        if (mutex_is_locked(&bl_hw->mutex)) {
            mutex_unlock(&bl_hw->mutex);

            printk("%s, mutex release\n", __func__);
        }
        
        return 0;
    }

    bl_vif = bl_hw->vif_table[ind->vif_idx];

    if (bl_vif && bl_vif->ndev) {
        dev = bl_vif->ndev;
    } else {
        printk("%s vif index %d, bl_vif NULL or ndev null\n", 
               __func__, ind->vif_idx);
        
        if (mutex_is_locked(&bl_hw->mutex)) {
            mutex_unlock(&bl_hw->mutex);

            printk("%s, mutex release\n", __func__);
        }

        return 0;
    }
    
    printk("%s, status code 0x%x, vif idx:%d\n", 
           __func__, ind->status_code, ind->vif_idx);

    /* Retrieve IE addresses and lengths */
    req_ie = (const u8 *)ind->assoc_ie_buf;
    rsp_ie = req_ie + ind->assoc_req_ie_len;

    // Fill-in the AP information
    if (ind->status_code == 0)
    {
        struct bl_sta *sta = &bl_hw->sta_table[ind->ap_idx];
        u8 txq_status;
        struct ieee80211_channel *chan;
        struct cfg80211_chan_def chandef;

        sta->valid = true;
        sta->sta_idx = ind->ap_idx;
        sta->ch_idx = ind->ch_idx;
        sta->vif_idx = ind->vif_idx;
        sta->vlan_idx = sta->vif_idx;
        sta->qos = ind->qos;
        sta->acm = ind->acm;
        sta->ps.active = false;
        sta->aid = ind->aid;
        sta->band = ind->chan.band;
        sta->width = ind->chan.type;
        sta->center_freq = ind->chan.prim20_freq;
        sta->center_freq1 = ind->chan.center1_freq;
        sta->center_freq2 = ind->chan.center2_freq;
        bl_vif->sta.ap = sta;
        bl_vif->rx_pn = 0;
        bl_vif->generation++;

        printk("%s, sta_idx=%d, chidx%d vifidx%d aid%d band%d width%d c%d cc%d chpri%d\n", 
               __func__, sta->sta_idx, sta->ch_idx, sta->vif_idx, sta->aid, 
               sta->band, sta->width, sta->center_freq, sta->center_freq2, 
               ind->chan.prim20_freq);

        chan = ieee80211_get_channel(bl_hw->wiphy, ind->chan.prim20_freq);
        cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
        
        if (!bl_hw->mod_params->ht_on)
            chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
        else
            chandef.width = chnl2bw[ind->chan.type];
            
        chandef.center_freq1 = ind->chan.center1_freq;
        chandef.center_freq2 = ind->chan.center2_freq;
        bl_chanctx_link(bl_vif, ind->ch_idx, &chandef);
        memcpy(sta->mac_addr, ind->bssid.array, ETH_ALEN);
        
        if (ind->ch_idx == bl_hw->cur_chanctx) {
            txq_status = 0;
        } else {
            txq_status = BL_TXQ_STOP_CHAN;
        }
        
        memcpy(sta->ac_param, ind->ac_param, sizeof(sta->ac_param));
        
        bl_txq_sta_init(bl_hw, sta, txq_status);
#ifdef CONFIG_BL_DEBUGFS
        bl_dbgfs_register_sta(bl_hw, sta);
#endif
        bl_txq_tdls_vif_init(bl_vif);
        bl_mu_group_sta_init(sta, NULL);
        
        /* Look for TDLS Channel Switch Prohibited flag in the Extended Capability
         * Information Element*/
        extcap_ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY,
                                     rsp_ie, ind->assoc_rsp_ie_len);
        
        if (extcap_ie && extcap_ie[1] >= 5) {
            extcap = (void *)(extcap_ie);
            bl_vif->tdls_chsw_prohibited = 
                   extcap->ext_capab[4] & WLAN_EXT_CAPA5_TDLS_CH_SW_PROHIBITED;
        }

        printk("%s, mutex release\n", __func__);
        mutex_unlock(&bl_hw->mutex);

#ifdef CONFIG_BL_BFMER
        /* If Beamformer feature is activated, check if features can be used
         * with the new peer device
         */
        if (bl_hw->mod_params->bfmer) {
            const u8 *vht_capa_ie;
            const struct ieee80211_vht_cap *vht_cap;

            do {
                /* Look for VHT Capability Information Element */
                vht_capa_ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, rsp_ie,
                                               ind->assoc_rsp_ie_len);

                /* Stop here if peer device does not support VHT */
                if (!vht_capa_ie) {
                    break;
                }

                vht_cap = (const struct ieee80211_vht_cap *)(vht_capa_ie + 2);

                /* Send MM_BFMER_ENABLE_REQ message if needed */
                bl_send_bfmer_enable(bl_hw, sta, vht_cap);
            } while (0);
        }
#endif //(CONFIG_BL_BFMER)

#ifdef CONFIG_BL_MON_DATA
        // If there are 1 sta and 1 monitor interface active at the same time then
        // monitor interface channel context is always the same as the STA interface.
        // This doesn't work with 2 STA interfaces but we don't want to support it.
        if (bl_hw->monitor_vif != BL_INVALID_VIF) {
            struct bl_vif *bl_mon_vif = bl_hw->vif_table[bl_hw->monitor_vif];
            
            bl_chanctx_unlink(bl_mon_vif);
            bl_chanctx_link(bl_mon_vif, ind->ch_idx, NULL);
        }
#endif
    }
    else
    {
        printk("%s, mutex release\n", __func__);
        mutex_unlock(&bl_hw->mutex);
    }

    if (ind->roamed) {
        struct cfg80211_roam_info info;
        
        memset(&info, 0, sizeof(info));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0) 
        if (bl_vif->ch_index < NX_CHAN_CTXT_CNT)
            info.links[0].channel = 
                           bl_hw->chanctx_table[bl_vif->ch_index].chan_def.chan;
               
        info.links[0].bssid = (const u8 *)ind->bssid.array;
        info.valid_links = 1;
#else
        if (bl_vif->ch_index < NX_CHAN_CTXT_CNT)
            info.channel = bl_hw->chanctx_table[bl_vif->ch_index].chan_def.chan;
            
        info.bssid = (const u8 *)ind->bssid.array;
#endif

        info.req_ie = req_ie;
        info.req_ie_len = ind->assoc_req_ie_len;
        info.resp_ie = rsp_ie;
        info.resp_ie_len = ind->assoc_rsp_ie_len;
        cfg80211_roamed(dev, &info, GFP_ATOMIC);
    } else {
        cfg80211_connect_result(dev, (const u8 *)ind->bssid.array, req_ie,
                                ind->assoc_req_ie_len, rsp_ie,
                                ind->assoc_rsp_ie_len, ind->status_code,
                                GFP_ATOMIC);
    }

    if (ind->status_code == 0)
    {
        netif_tx_start_all_queues(dev);
        netif_carrier_on(dev);
    }

    return 0;
}

static inline int bl_rx_sm_disconnect_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct sm_disconnect_ind *ind = (struct sm_disconnect_ind *)msg->param;
    struct bl_vif *bl_vif = NULL;
    struct net_device *dev = NULL;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX)) {
        printk("%s vif index exception %d \n", __func__, ind->vif_idx);    
        return 0;
    }
    
    bl_vif = bl_hw->vif_table[ind->vif_idx];
    
    if (bl_vif && bl_vif->ndev)
        dev = bl_vif->ndev;
    else
        return 0;
    
    printk("%s vif=%d reason=%d, sta's ap:0x%p\n",
           __func__, ind->vif_idx, ind->reason_code, bl_vif->sta.ap);
    
    /* if vif is not up, bl_close has already been called */
    if (bl_vif->up) {
        if (!ind->reassoc) {
            cfg80211_disconnected(dev, ind->reason_code, NULL, 0,
                                  (ind->reason_code <= 1), GFP_ATOMIC);

            if (bl_vif->sta.ft_assoc_ies) {
                kfree(bl_vif->sta.ft_assoc_ies);
                bl_vif->sta.ft_assoc_ies = NULL;
                bl_vif->sta.ft_assoc_ies_len = 0;
            }
        }
        
        netif_tx_stop_all_queues(dev);
        netif_carrier_off(dev);
    }

    if (bl_vif->sta.ap == NULL) {
        printk("%s, sta previous sm_connect_ind with fail status, not connected\n", 
               __func__);
               
        return 0;
    }

#ifdef CONFIG_BL_BFMER
    if (bl_vif->sta.ap) {
        /* Disable Beamformer if supported */
        bl_bfmer_report_del(bl_hw, bl_vif->sta.ap);
    }
#endif //(CONFIG_BL_BFMER)
    
    if(bl_hw->mod_params->tcp_ack_filter)
        bl_tcp_ack_stream_clear(bl_hw);

    if (bl_vif->sta.ap) {
        bl_txq_sta_deinit(bl_hw, bl_vif->sta.ap);
    }
    
    bl_txq_tdls_vif_deinit(bl_vif);
    
#ifdef CONFIG_BL_DEBUGFS
    if (bl_vif->sta.ap) {
        bl_dbgfs_unregister_sta(bl_hw, bl_vif->sta.ap);
    }
#endif

    bl_vif->sta.ap->valid = false;
    bl_vif->sta.ap = NULL;
    bl_vif->generation++;
    bl_external_auth_disable(bl_vif);
    bl_chanctx_unlink(bl_vif);

    return 0;
}

static inline int bl_rx_sm_external_auth_required_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct sm_external_auth_required_ind *ind =
        (struct sm_external_auth_required_ind *)msg->param;
    struct bl_vif *bl_vif = bl_hw->vif_table[(ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))? 0 : ind->vif_idx];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || defined(BL_WPA3_COMPAT)
    struct net_device *dev = bl_vif->ndev;
    struct cfg80211_external_auth_params params;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
        printk("%s --vif index exception %d \n", __func__, ind->vif_idx);

    params.action = NL80211_EXTERNAL_AUTH_START;
    memcpy(params.bssid, ind->bssid.array, ETH_ALEN);
    params.ssid.ssid_len = ind->ssid.length;
    memcpy(params.ssid.ssid, ind->ssid.array,
           min_t(size_t, ind->ssid.length, sizeof(params.ssid.ssid)));
    params.key_mgmt_suite = ind->akm;

    if ((ind->vif_idx > NX_VIRT_DEV_MAX) || !bl_vif->up ||
        (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_STATION) ||
        cfg80211_external_auth_request(dev, &params, GFP_ATOMIC)) 
    {
        wiphy_err(bl_hw->wiphy, "Failed to start external auth on vif %d",
                  ind->vif_idx);
        bl_send_sm_external_auth_required_rsp(bl_hw, bl_vif,
                                              WLAN_STATUS_UNSPECIFIED_FAILURE);
        return 0;
    }

    bl_external_auth_enable(bl_vif);
    
#else
    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
        printk("%s --vif index exception %d\n", __func__, ind->vif_idx);

    bl_send_sm_external_auth_required_rsp(bl_hw, bl_vif,
                                          WLAN_STATUS_UNSPECIFIED_FAILURE);
#endif
    return 0;
}

static inline int bl_rx_sm_ft_auth_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct sm_ft_auth_ind *ind = (struct sm_ft_auth_ind *)msg->param;
    struct bl_vif *bl_vif = NULL;
    struct sk_buff *skb;
    size_t data_len = (offsetof(struct ieee80211_mgmt, u.auth.variable) +
                       ind->ft_ie_len);

    bl_vif = bl_hw->vif_table[(ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))? 0 : ind->vif_idx];
    
    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
        printk("%s --vif index exception %d \n",__func__, ind->vif_idx);

    //skb = dev_alloc_skb(data_len);
    skb = __netdev_alloc_skb(NULL, data_len, GFP_KERNEL);
    
    if (skb) {
        struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb_put(skb, data_len);
        
        mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH);
        memcpy(mgmt->u.auth.variable, ind->ft_ie_buf, ind->ft_ie_len);
        bl_rx_defer_skb(bl_hw, bl_vif, skb);
        
        dev_kfree_skb(skb);
    } else {
        netdev_warn(bl_vif->ndev, "Allocation failed for FT auth ind\n");
    }

    return 0;
}

/***************************************************************************
 * Messages from TWT task
 **************************************************************************/
static inline int bl_rx_twt_setup_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct twt_setup_ind *ind = (struct twt_setup_ind *)msg->param;
    struct bl_sta *bl_sta = &bl_hw->sta_table[ind->sta_idx];

    BL_DBG(BL_FN_ENTRY_STR);

    memcpy(&bl_sta->twt_ind, ind, sizeof(struct twt_setup_ind));
    printk("rsp_type=%d, flow_type=%d, int_exp=%d, dur_uint=%d, wake_dur=%d, int_mantissa=%d\n",
        ind->resp_type, ind->conf.flow_type, ind->conf.wake_int_exp, 
        ind->conf.wake_dur_unit, ind->conf.wake_dur_unit, ind->conf.wake_int_mantissa);

    return 0;
}
#ifdef CONFIG_MESH
static inline int bl_rx_mesh_path_create_cfm(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mesh_path_create_cfm *cfm = (struct mesh_path_create_cfm *)msg->param;
    struct bl_vif *bl_vif = bl_hw->vif_table[cfm->vif_idx];

    BL_DBG(BL_FN_ENTRY_STR);

    /* Check we well have a Mesh Point Interface */
    if (bl_vif && (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_MESH_POINT))
        bl_vif->ap.flags &= ~BL_AP_CREATE_MESH_PATH;

    return 0;
}

static inline int bl_rx_mesh_peer_update_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mesh_peer_update_ind *ind = (struct mesh_peer_update_ind *)msg->param;
    struct bl_vif *bl_vif = bl_hw->vif_table[ind->vif_idx];
    struct bl_sta *bl_sta = &bl_hw->sta_table[ind->sta_idx];

    BL_DBG(BL_FN_ENTRY_STR);

    if ((ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX)) ||
        (bl_vif && (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)) ||
        (ind->sta_idx >= NX_REMOTE_STA_MAX))
        return 1;

    if (bl_vif->ap.flags & BL_AP_USER_MESH_PM)
    {
        if (!ind->estab && bl_sta->valid) {
            /* There is no way to inform upper layer for lost of peer, still
               clean everything in the driver */
            bl_sta->ps.active = false;
            bl_sta->valid = false;

            /* Remove the station from the list of VIF's station */
            list_del_init(&bl_sta->list);

            bl_txq_sta_deinit(bl_hw, bl_sta);
#ifdef CONFIG_BL_DEBUGFS
            bl_dbgfs_unregister_sta(bl_hw, bl_sta);
#endif
        } else {
            WARN_ON(0);
        }
    } else {
        /* Check if peer link has been established or lost */
        if (ind->estab) {
            if (!bl_sta->valid) {
                u8 txq_status;

                bl_sta->valid = true;
                bl_sta->sta_idx = ind->sta_idx;
                bl_sta->ch_idx = bl_vif->ch_index;
                bl_sta->vif_idx = ind->vif_idx;
                bl_sta->vlan_idx = bl_sta->vif_idx;
                bl_sta->ps.active = false;
                bl_sta->qos = true;
                bl_sta->aid = ind->sta_idx + 1;
                //bl_sta->acm = ind->acm;
                
                memcpy(bl_sta->mac_addr, ind->peer_addr.array, ETH_ALEN);

                bl_chanctx_link(bl_vif, bl_sta->ch_idx, NULL);

                /* Add the station in the list of VIF's stations */
                INIT_LIST_HEAD(&bl_sta->list);
                list_add_tail(&bl_sta->list, &bl_vif->ap.sta_list);

                /* Initialize the TX queues */
                if (bl_sta->ch_idx == bl_hw->cur_chanctx) {
                    txq_status = 0;
                } else {
                    txq_status = BL_TXQ_STOP_CHAN;
                }

                bl_txq_sta_init(bl_hw, bl_sta, txq_status);
#ifdef CONFIG_BL_DEBUGFS
                bl_dbgfs_register_sta(bl_hw, bl_sta);
#endif

#ifdef CONFIG_BL_BFMER
                // TODO: update indication to contains vht capabilties
                if (bl_hw->mod_params->bfmer)
                    bl_send_bfmer_enable(bl_hw, bl_sta, NULL);

                bl_mu_group_sta_init(bl_sta, NULL);
#endif /* CONFIG_BL_BFMER */

            } else {
                WARN_ON(0);
            }
        } else {
            if (bl_sta->valid) {
                bl_sta->ps.active = false;
                bl_sta->valid = false;

                /* Remove the station from the list of VIF's station */
                list_del_init(&bl_sta->list);

                bl_txq_sta_deinit(bl_hw, bl_sta);
#ifdef CONFIG_BL_DEBUGFS
                bl_dbgfs_unregister_sta(bl_hw, bl_sta);
#endif
            } else {
                WARN_ON(0);
            }
        }
    }

    return 0;
}

static inline int bl_rx_mesh_path_update_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mesh_path_update_ind *ind = (struct mesh_path_update_ind *)msg->param;
    struct bl_vif *bl_vif = bl_hw->vif_table[ind->vif_idx];
    struct bl_mesh_path *mesh_path;
    bool found = false;

    BL_DBG(BL_FN_ENTRY_STR);

    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
        return 1;

    if (!bl_vif || (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT))
        return 0;

    /* Look for path with provided target address */
    list_for_each_entry(mesh_path, &bl_vif->ap.mpath_list, list) {
        if (mesh_path->path_idx == ind->path_idx) {
            found = true;
            break;
        }
    }

    /* Check if element has been deleted */
    if (ind->delete) {
        if (found) {
            /* Remove element from list */
            list_del_init(&mesh_path->list);
            /* Free the element */
            kfree(mesh_path);
        }
    }
    else {
        if (found) {
            // Update the Next Hop STA
            mesh_path->nhop_sta = &bl_hw->sta_table[ind->nhop_sta_idx];
        } else {
            // Allocate a Mesh Path structure
            mesh_path = kmalloc(sizeof(struct bl_mesh_path), GFP_ATOMIC);

            if (mesh_path) {
                INIT_LIST_HEAD(&mesh_path->list);

                mesh_path->path_idx = ind->path_idx;
                mesh_path->nhop_sta = &bl_hw->sta_table[ind->nhop_sta_idx];
                memcpy(&mesh_path->tgt_mac_addr, &ind->tgt_mac_addr, MAC_ADDR_LEN);

                // Insert the path in the list of path
                list_add_tail(&mesh_path->list, &bl_vif->ap.mpath_list);
            }
        }
    }

    return 0;
}

static inline int bl_rx_mesh_proxy_update_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct mesh_proxy_update_ind *ind = (struct mesh_proxy_update_ind *)msg->param;
    struct bl_vif *bl_vif = bl_hw->vif_table[ind->vif_idx];
    struct bl_mesh_proxy *mesh_proxy;
    bool found = false;

    BL_DBG(BL_FN_ENTRY_STR);

    if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
        return 1;

    if (!bl_vif || (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT))
        return 0;

    /* Look for path with provided external STA address */
    list_for_each_entry(mesh_proxy, &bl_vif->ap.proxy_list, list) {
        if (!memcmp(&ind->ext_sta_addr, &mesh_proxy->ext_sta_addr, ETH_ALEN)) {
            found = true;
            break;
        }
    }

    if (ind->delete && found) {
        /* Delete mesh path */
        list_del_init(&mesh_proxy->list);
        kfree(mesh_proxy);
    } else if (!ind->delete && !found) {
        /* Allocate a Mesh Path structure */
        mesh_proxy = (struct bl_mesh_proxy *)kmalloc(sizeof(*mesh_proxy),
                                                       GFP_ATOMIC);

        if (mesh_proxy) {
            INIT_LIST_HEAD(&mesh_proxy->list);

            memcpy(&mesh_proxy->ext_sta_addr, &ind->ext_sta_addr, MAC_ADDR_LEN);
            mesh_proxy->local = ind->local;

            if (!ind->local) {
                memcpy(&mesh_proxy->proxy_addr, &ind->proxy_mac_addr, MAC_ADDR_LEN);
            }

            /* Insert the path in the list of path */
            list_add_tail(&mesh_proxy->list, &bl_vif->ap.proxy_list);
        }
    }

    return 0;
}
#endif
/***************************************************************************
 * Messages from APM task
 **************************************************************************/
static inline int bl_rx_apm_probe_client_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    struct apm_probe_client_ind *ind = (struct apm_probe_client_ind *)msg->param;
    struct bl_vif *bl_vif = bl_hw->vif_table[ind->vif_idx];
    struct bl_sta *bl_sta = &bl_hw->sta_table[ind->sta_idx];

    bl_sta->stats.last_act = jiffies;
    cfg80211_probe_status(bl_vif->ndev, bl_sta->mac_addr, (u64)ind->probe_id,
                          ind->client_present, 0, false, GFP_ATOMIC);

    return 0;
}

#endif /* CONFIG_BL_FULLMAC */

/***************************************************************************
 * Messages from DEBUG task
 **************************************************************************/
static inline int bl_rx_dbg_error_ind(struct bl_hw *bl_hw,
                                     struct bl_cmd *cmd,struct ipc_e2a_msg *msg)
{
    BL_DBG(BL_FN_ENTRY_STR);

    bl_error_ind(bl_hw);

    return 0;
}

/***************************************************************************
 * Messages from MP task
 **************************************************************************/
static inline int bl_rx_mp_test_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    uint8_t *purge_ptr = NULL;
    int ret = 1;
    #if 0
    int i = 0;
    char *ch_ptr = NULL;
    uint32_t ch_len = 0;
    #endif
    
    if (!bl_mod_params.mp_mode) {
        printk("%s, bl driver not in mp_mode\n", __func__);
        return -EINVAL;
    }
    
    if (msg->param_len == 0 || msg->param_len > IWPRIV_IND_LEN_MAX/2) {
        return ret;
    }

    //debug, comment out when release.
    ((uint8_t *)msg->param)[msg->param_len] = '\0';
    BL_DBG_MSG("%u %u %u\n", msg->param_len, *(uint16_t *)msg->param, 
           bl_hw->iwp_var.iwpriv_ind_len);
    //bl_dump((uint8_t *)msg->param, msg->param_len);
   
    purge_ptr = bl_hw->iwp_var.iwpriv_ind;
    //Check if there is overflow after adding new msg->param
    while (bl_hw->iwp_var.iwpriv_ind_len > 0 && 
           purge_ptr - bl_hw->iwp_var.iwpriv_ind < bl_hw->iwp_var.iwpriv_ind_len &&
           purge_ptr - bl_hw->iwp_var.iwpriv_ind < IWPRIV_IND_LEN_MAX &&
           (bl_hw->iwp_var.iwpriv_ind_len - 
           (purge_ptr - bl_hw->iwp_var.iwpriv_ind) + msg->param_len) >= IWPRIV_IND_LEN_MAX) 
    {
        purge_ptr = purge_ptr + le16_to_cpu(*(__le16 *)purge_ptr) + 2;
    }

    if (purge_ptr >= bl_hw->iwp_var.iwpriv_ind + IWPRIV_IND_LEN_MAX) {
        memset(bl_hw->iwp_var.iwpriv_ind, 0, IWPRIV_IND_LEN_MAX);
        bl_hw->iwp_var.iwpriv_ind_len = 0;
    } else {
        if (purge_ptr != bl_hw->iwp_var.iwpriv_ind) {
            memcpy(bl_hw->iwp_var.iwpriv_ind, purge_ptr, 
                   bl_hw->iwp_var.iwpriv_ind_len - 
                   (purge_ptr - bl_hw->iwp_var.iwpriv_ind));
            bl_hw->iwp_var.iwpriv_ind_len = bl_hw->iwp_var.iwpriv_ind_len - 
                                        (purge_ptr - bl_hw->iwp_var.iwpriv_ind);
        }
    }

    memcpy(bl_hw->iwp_var.iwpriv_ind + bl_hw->iwp_var.iwpriv_ind_len, 
           (uint8_t *)msg->param, msg->param_len);
    bl_hw->iwp_var.iwpriv_ind_len = 
                            bl_hw->iwp_var.iwpriv_ind_len + msg->param_len;
    
    if (cmd) {
        BL_DBG("bl_rx_mp_test_ind is called\n");
        
        bl_hw->ipc_env->msga2e_hostid = NULL;
        cmd->flags &= ~BL_CMD_FLAG_WAIT_ACK;
        ret = 0;
    }else {
        BL_DBG("bl_rx_mp_test_ind is called with null cmd\n");
        
        ret = 0;
    }

    return ret;
}

static inline int bl_rx_mp2_test_ind(struct bl_hw *bl_hw,
                                    struct bl_cmd *cmd, struct ipc_e2a_msg *msg)
{
    if (!bl_mod_params.mp_mode) {
        printk("%s, bl driver not in mp_mode\n", __func__);
        return -EINVAL;
    }
    
    if (msg->param_len == 0) {
        printk("%s NULL\r\n", __func__);
        return 0;
    }

    printk("%s\n", (uint8_t *)msg->param);
    //extern void dump_buf_char(const void *buf, const unsigned long buf_len);
    //dump_buf_char(msg->param, msg->param_len);
   
    return 0;
}

static msg_cb_fct mm_hdlrs[MSG_I(MM_MAX)] = {
    [MSG_I(MM_CHANNEL_SWITCH_IND)]     = bl_rx_chan_switch_ind,
    [MSG_I(MM_CHANNEL_PRE_SWITCH_IND)] = bl_rx_chan_pre_switch_ind,
    [MSG_I(MM_REMAIN_ON_CHANNEL_EXP_IND)] = bl_rx_remain_on_channel_exp_ind,
    [MSG_I(MM_PS_CHANGE_IND)]          = bl_rx_ps_change_ind,
    [MSG_I(MM_TRAFFIC_REQ_IND)]        = bl_rx_traffic_req_ind,
    [MSG_I(MM_P2P_VIF_PS_CHANGE_IND)]  = bl_rx_p2p_vif_ps_change_ind,
    [MSG_I(MM_CSA_COUNTER_IND)]        = bl_rx_csa_counter_ind,
    [MSG_I(MM_CSA_FINISH_IND)]         = bl_rx_csa_finish_ind,
    [MSG_I(MM_CSA_TRAFFIC_IND)]        = bl_rx_csa_traffic_ind,
    [MSG_I(MM_CHANNEL_SURVEY_IND)]     = bl_rx_channel_survey_ind,
    [MSG_I(MM_P2P_NOA_UPD_IND)]        = bl_rx_p2p_noa_upd_ind,
    [MSG_I(MM_RSSI_STATUS_IND)]        = bl_rx_rssi_status_ind,
    [MSG_I(MM_PKTLOSS_IND)]            = bl_rx_pktloss_notify_ind,
#if defined  BL_RX_REORDER && (defined CONFIG_BL_USB || defined CONFIG_BL_SDIO)
    [MSG_I(MM_BA_ADD_REQ)]             = bl_addba_req_ind,    
    [MSG_I(MM_BA_DEL_REQ)]             = bl_delba_req_ind,
#endif
};

static msg_cb_fct scanu_hdlrs[MSG_I(SCANU_MAX)] = {
    [MSG_I(SCANU_START_CFM)]           = bl_rx_scanu_start_cfm,
    [MSG_I(SCANU_RESULT_IND)]          = bl_rx_scanu_result_ind,
};

static msg_cb_fct me_hdlrs[MSG_I(ME_MAX)] = {
    [MSG_I(ME_TKIP_MIC_FAILURE_IND)]  = bl_rx_me_tkip_mic_failure_ind,
    [MSG_I(ME_TX_CREDITS_UPDATE_IND)] = bl_rx_me_tx_credits_update_ind,
};

static msg_cb_fct sm_hdlrs[MSG_I(SM_MAX)] = {
    [MSG_I(SM_CONNECT_IND)]    = bl_rx_sm_connect_ind,
    [MSG_I(SM_DISCONNECT_IND)] = bl_rx_sm_disconnect_ind,
    [MSG_I(SM_EXTERNAL_AUTH_REQUIRED_IND)] = bl_rx_sm_external_auth_required_ind,
    [MSG_I(SM_FT_AUTH_IND)]    = bl_rx_sm_ft_auth_ind,
};

static msg_cb_fct apm_hdlrs[MSG_I(APM_MAX)] = {
    [MSG_I(APM_PROBE_CLIENT_IND)] = bl_rx_apm_probe_client_ind,
};

static msg_cb_fct twt_hdlrs[MSG_I(TWT_MAX)] = {
    [MSG_I(TWT_SETUP_IND)]    = bl_rx_twt_setup_ind,
};

#ifdef CONFIG_MESH
static msg_cb_fct mesh_hdlrs[MSG_I(MESH_MAX)] = {
    [MSG_I(MESH_PATH_CREATE_CFM)]  = bl_rx_mesh_path_create_cfm,
    [MSG_I(MESH_PEER_UPDATE_IND)]  = bl_rx_mesh_peer_update_ind,
    [MSG_I(MESH_PATH_UPDATE_IND)]  = bl_rx_mesh_path_update_ind,
    [MSG_I(MESH_PROXY_UPDATE_IND)] = bl_rx_mesh_proxy_update_ind,
};
#endif

static msg_cb_fct dbg_hdlrs[MSG_I(DBG_MAX)] = {
    [MSG_I(DBG_ERROR_IND)]                = bl_rx_dbg_error_ind,
    [MSG_I(DBG_KE_STAT_CFM)]              = bl_rx_dbg_ke_stat_cfm,
#ifdef CONFIG_BL_MP
    [MSG_I(DBG_MP_CFM)]                   = bl_rx_mp_test_ind,
    [MSG_I(DBG_MP2_CFM)]                  = bl_rx_mp2_test_ind,
#endif
};

static msg_cb_fct tdls_hdlrs[MSG_I(TDLS_MAX)] = {
    [MSG_I(TDLS_CHAN_SWITCH_CFM)] = bl_rx_tdls_chan_switch_cfm,
    [MSG_I(TDLS_CHAN_SWITCH_IND)] = bl_rx_tdls_chan_switch_ind,
    [MSG_I(TDLS_CHAN_SWITCH_BASE_IND)] = bl_rx_tdls_chan_switch_base_ind,
    [MSG_I(TDLS_PEER_PS_IND)]     = bl_rx_tdls_peer_ps_ind,
};

static msg_cb_fct *msg_hdlrs[] = {
    [TASK_MM]    = mm_hdlrs,
    [TASK_DBG]   = dbg_hdlrs,
    [TASK_TDLS]  = tdls_hdlrs,
    [TASK_SCANU] = scanu_hdlrs,
    [TASK_ME]    = me_hdlrs,
    [TASK_SM]    = sm_hdlrs,
    [TASK_APM]   = apm_hdlrs,
#ifdef CONFIG_MESH
    [TASK_MESH]  = mesh_hdlrs,
#endif
    [TASK_TWT]   = twt_hdlrs,
};

/**
 *
 */
void bl_rx_handle_msg(struct bl_hw *bl_hw, struct ipc_e2a_msg *msg)
{
    bool abnormal_msg = 0;

    switch (MSG_T(msg->id)) {
        case TASK_MM:
            if(MSG_I(msg->id) >= MM_MAX)
                abnormal_msg = 1;
            break;

        case TASK_DBG:
            if(MSG_I(msg->id) >= DBG_MAX)
                abnormal_msg = 1;
            break;
            
        case TASK_TDLS:
            if(MSG_I(msg->id) >= TDLS_MAX)
                abnormal_msg = 1;
            break;
            
        case TASK_SCANU:
            if(MSG_I(msg->id) >= SCANU_MAX)
                abnormal_msg = 1;
            break;

        case TASK_ME:
            if(MSG_I(msg->id) >= ME_MAX)
                abnormal_msg = 1;
            break;

        case TASK_SM:
            if(MSG_I(msg->id) >= SM_MAX)
                abnormal_msg = 1;
            break;

        case TASK_APM:
            if(MSG_I(msg->id) >= APM_MAX)
                abnormal_msg = 1;
            break;

        case TASK_MESH:
            if(MSG_I(msg->id) >= MESH_MAX)
                abnormal_msg = 1;
            break;

        case TASK_TWT:
            if(MSG_I(msg->id) >= TWT_MAX)
                abnormal_msg = 1;
            break;

        default:
            abnormal_msg = 1;
            break;
    }

    BL_DBG_MSG("%s, id:0x%x, src_task:0x%x, dest_task:0x%x\n", 
           __func__, msg->id, msg->dummy_src_id, msg->dummy_dest_id);
          
    if (softmac_task_ids_check(msg->dummy_dest_id)) {
        dbg("%s, call softmac_handle_kmsg_from_fw\n", __func__);
        softmac_handle_kmsg_from_fw(bl_hw, NULL, msg);
        
        return;
    }

    if(!abnormal_msg && bl_hw->cmd_mgr.msgind)
    {    
        BL_DBG_MSG(KERN_CRIT "recv: msg:0x%x-%-24s, T:%d I:%d\n", 
                   msg->id, BL_ID2STR(msg->id), MSG_T(msg->id), MSG_I(msg->id));
        bl_hw->cmd_mgr.msgind(&bl_hw->cmd_mgr, msg, 
                              msg_hdlrs[MSG_T(msg->id)][MSG_I(msg->id)]);
    }
    else
    {
        printk(KERN_CRIT "abnormal taskid %d-%d, msg->id:0x%x\n", 
               MSG_T(msg->id), MSG_I(msg->id), msg->id);
    }
}

