/**
 ****************************************************************************************
 *
 *  @file bl_msg_tx.h
 *
 *  @brief TX function declarations
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


#ifndef _BL_MSG_TX_H_
#define _BL_MSG_TX_H_

#include "bl_defs.h"

bool is_non_blocking_msg(int id);
int bl_send_ke_msg(struct bl_hw *bl_hw, const void *msg_params,
                          uint16_t reqid);
int bl_send_caldata(struct bl_hw *bl_hw);
int bl_send_reset(struct bl_hw *bl_hw);
int bl_send_start(struct bl_hw *bl_hw);
int bl_send_version_req(struct bl_hw *bl_hw, struct mm_version_cfm *cfm);
int bl_send_add_if(struct bl_hw *bl_hw, const unsigned char *mac,
                        enum nl80211_iftype iftype, bool p2p, struct mm_add_if_cfm *cfm);
int bl_send_remove_if(struct bl_hw *bl_hw, u8 vif_index);
int bl_send_set_channel(struct bl_hw *bl_hw, int phy_idx,
                               struct mm_set_channel_cfm *cfm);
int bl_send_key_add(struct bl_hw *bl_hw, u8 vif_idx, u8 sta_idx, bool pairwise,
                          u8 *key, u8 key_len, u8 key_idx, u8 cipher_suite,
                          struct mm_key_add_cfm *cfm);
int bl_send_key_del(struct bl_hw *bl_hw, uint8_t hw_key_idx);
int bl_send_bcn_change(struct bl_hw *bl_hw, u8 vif_idx, u32 *bcn_addr,
                                u16 bcn_len, u16 tim_oft, u16 tim_len, u16 *csa_oft);
int bl_send_tim_update(struct bl_hw *bl_hw, u8 vif_idx, u16 aid,
                              u8 tx_status);
int bl_send_roc(struct bl_hw *bl_hw, struct bl_vif *vif,
                    struct ieee80211_channel *chan, unsigned int duration);
int bl_send_cancel_roc(struct bl_hw *bl_hw);
int bl_send_set_power(struct bl_hw *bl_hw,  u8 vif_idx, s8 pwr,
                              struct mm_set_power_cfm *cfm);
int bl_send_set_edca(struct bl_hw *bl_hw, u8 hw_queue, u32 param,
                           bool uapsd, u8 inst_nbr);
int bl_send_set_ps_mode(struct bl_hw *bl_hw, u8 ps_mode);

int bl_send_tdls_chan_switch_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                                   struct bl_sta *bl_sta, bool sta_initiator,
                                   u8 oper_class, struct cfg80211_chan_def *chandef,
                                   struct tdls_chan_switch_cfm *cfm);
int bl_send_tdls_cancel_chan_switch_req(struct bl_hw *bl_hw,
                                          struct bl_vif *bl_vif,
                                          struct bl_sta *bl_sta,
                                          struct tdls_cancel_chan_switch_cfm *cfm);

#ifdef CONFIG_BL_P2P_DEBUGFS
int bl_send_p2p_oppps_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                                   u8 ctw, struct mm_set_p2p_oppps_cfm *cfm);
int bl_send_p2p_noa_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                                int count, int interval, int duration,
                                bool dyn_noa, struct mm_set_p2p_noa_cfm *cfm);
#endif /* CONFIG_BL_P2P_DEBUGFS */


#ifdef CONFIG_BL_FULLMAC
int bl_send_me_config_req(struct bl_hw *bl_hw);
int bl_send_me_chan_config_req(struct bl_hw *bl_hw);
int bl_send_me_set_control_port_req(struct bl_hw *bl_hw, bool opened,
                                                u8 sta_idx);
int bl_send_me_sta_add(struct bl_hw *bl_hw, struct station_parameters *params,
                               const u8 *mac, u8 inst_nbr, struct me_sta_add_cfm *cfm);
int bl_send_me_sta_del(struct bl_hw *bl_hw, u8 sta_idx, bool tdls_sta);
int bl_send_me_traffic_ind(struct bl_hw *bl_hw, u8 sta_idx, bool uapsd, u8 tx_status);
int bl_send_twt_request(struct bl_hw *bl_hw,
                               u8 setup_type, u8 vif_idx,
                               struct twt_conf_tag *conf,
                               struct twt_setup_cfm *cfm);
int bl_send_twt_teardown(struct bl_hw *bl_hw,
                                 struct twt_teardown_req *twt_teardown,
                                 struct twt_teardown_cfm *cfm);
int bl_send_me_rc_stats(struct bl_hw *bl_hw, u8 sta_idx,
                                struct me_rc_stats_cfm *cfm);
int bl_send_me_rc_set_rate(struct bl_hw *bl_hw,
                                    u8 sta_idx, u16 rate_idx);
int bl_send_me_set_ps_mode(struct bl_hw *bl_hw, u8 ps_mode);
int bl_send_sm_connect_req(struct bl_hw *bl_hw,
                                     struct bl_vif *bl_vif,
                                     struct cfg80211_connect_params *sme,
                                     struct sm_connect_cfm *cfm);
int bl_send_sm_disconnect_req(struct bl_hw *bl_hw,
                                        struct bl_vif *bl_vif,
                                        u16 reason);
int bl_send_sm_external_auth_required_rsp(struct bl_hw *bl_hw,
                                                        struct bl_vif *bl_vif,
                                                        u16 status);
int bl_send_sm_ft_auth_rsp(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                                   uint8_t *ie, int ie_len);
int bl_send_apm_start_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                  struct cfg80211_ap_settings *settings,
                                  struct apm_start_cfm *cfm,
                                  struct bl_ipc_elem_var *elem);
int bl_send_apm_stop_req(struct bl_hw *bl_hw, struct bl_vif *vif);
int bl_send_apm_probe_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                    struct bl_sta *sta, struct apm_probe_client_cfm *cfm);
int bl_send_scanu_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                             struct cfg80211_scan_request *param);
int bl_send_user_scanu_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif, u8 enable);
int bl_send_apm_start_cac_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                        struct cfg80211_chan_def *chandef,
                                        struct apm_start_cac_cfm *cfm);
int bl_send_apm_stop_cac_req(struct bl_hw *bl_hw, struct bl_vif *vif);
int bl_send_tdls_peer_traffic_ind_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif);
int bl_send_config_monitor_req(struct bl_hw *bl_hw,
                                         struct cfg80211_chan_def *chandef,
                                         struct me_config_monitor_cfm *cfm);

#ifdef CONFIG_MESH
int bl_send_mesh_start_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                    const struct mesh_config *conf, 
                                    const struct mesh_setup *setup,
                                    struct mesh_start_cfm *cfm);
int bl_send_mesh_stop_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                    struct mesh_stop_cfm *cfm);
int bl_send_mesh_update_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                       u32 mask, const struct mesh_config *p_mconf, 
                                       struct mesh_update_cfm *cfm);
int bl_send_mesh_peer_info_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                          u8 sta_idx, struct mesh_peer_info_cfm *cfm);
void bl_send_mesh_peer_update_ntf(struct bl_hw *bl_hw, struct bl_vif *vif,
                                              u8 sta_idx, u8 mlink_state);
void bl_send_mesh_path_create_req(struct bl_hw *bl_hw, struct bl_vif *vif, u8 *tgt_addr);
int bl_send_mesh_path_update_req(struct bl_hw *bl_hw, struct bl_vif *vif, const u8 *tgt_addr,
                                              const u8 *p_nhop_addr, struct mesh_path_update_cfm *cfm);
void bl_send_mesh_proxy_add_req(struct bl_hw *bl_hw, struct bl_vif *vif, u8 *ext_addr);
#endif
int bl_change_ch_width_in_opmode(const u8 *var_pos, int len);
int bl_change_mpdu_density(const u8 *var_pos, int len);
#endif /* CONFIG_BL_FULLMAC */

#ifdef CONFIG_BL_BFMER
void bl_send_bfmer_enable(struct bl_hw *bl_hw, struct bl_sta *bl_sta,
                            const struct ieee80211_vht_cap *vht_cap);
#ifdef CONFIG_BL_MUMIMO_TX
int bl_send_mu_group_update_req(struct bl_hw *bl_hw, struct bl_sta *bl_sta);
#endif /* CONFIG_BL_MUMIMO_TX */
#endif /* CONFIG_BL_BFMER */

/* Debug messages */
int bl_send_dbg_trigger_req(struct bl_hw *bl_hw, char *msg);
int bl_send_dbg_mem_read_req(struct bl_hw *bl_hw, u32 mem_addr,
                                         u32 rd_len, struct dbg_mem_read_cfm *cfm);
int bl_send_dbg_mem_write_req(struct bl_hw *bl_hw, u32 mem_addr,
                                          u32 mem_data);
int bl_send_dbg_set_mod_filter_req(struct bl_hw *bl_hw, u32 filter);
int bl_send_dbg_set_sev_filter_req(struct bl_hw *bl_hw, u32 filter);
int bl_send_dbg_get_sys_stat_req(struct bl_hw *bl_hw,
                                            struct dbg_get_sys_stat_cfm *cfm);
int bl_send_dbg_ke_stat_req(struct bl_hw *bl_hw, u32 clear_on_read,
                                     u32 period_rpt, u32 period_print);
int bl_send_cfg_rssi_req(struct bl_hw *bl_hw, u8 vif_index, int rssi_thold, u32 rssi_hyst);
#ifdef BL_BUS_LOOPBACK
int bl_send_dbg_lbk_req(struct bl_hw *bl_hw, u32 exp_data_rate, u32 pkt_size,
                                struct dbg_lbk_cfm *cfm);
#endif
int bl_send_wmmcfg(struct bl_hw *bl_hw, u32 cfg_value);
int bl_send_rw_coex_param(struct bl_hw *bl_hw, struct dbg_coex_rw_param_req *req, 
                                    struct dbg_coex_rw_param_cfm *cfm);
int bl_send_mp_test_msg(struct bl_hw *bl_hw, char *mp_cmd, uint32_t cmd_len, char *mp_test_cfm, bool nonblock);
int bl_send_mp2_test_msg(struct bl_hw *bl_hw, char *mp_cmd, char *mp_test_cfm, bool nonblock);
int bl_send_cal_cfg(struct bl_hw *bl_hw, struct mm_cal_cfg_req *cal_cfg_req);
int bl_send_temp_read_req(struct bl_hw *bl_hw, int32_t * temp);
int bl_send_rssi_read_req(struct bl_hw *bl_hw, u8 vif_index, struct mm_get_rssi_cfm *cfm);
int bl_send_efuse_read_req(struct bl_hw *bl_hw, struct mm_read_efuse_cfm *cfm);

#if defined(CONFIG_FW_COMBO) && defined(CONFIG_BL_BTUART) && !defined(CONFIG_BL_BTSDU)
int bl_send_btble_uart_req(struct bl_hw *bl_hw, int baud, int flow, int rts, int cts);
#endif
int bl_send_set_phy_misc(struct bl_hw *bl_hw, uint32_t type, int32_t *misc_value, 
                                  uint32_t misc_value_len, 
                                  struct mm_set_phy_misc_cfm *phy_misc_cfm);
inline void *bl_msg_zalloc(lmac_msg_id_t const id,
                                lmac_task_id_t const dest_id,
                                lmac_task_id_t const src_id,
                                uint16_t const param_len);
inline void *bl_msg_ke_zalloc(lmac_msg_id_t const id,
                                lmac_task_id_t const dest_id,
                                lmac_task_id_t const src_id,
                                uint16_t const param_len);

#endif /* _BL_MSG_TX_H_ */
