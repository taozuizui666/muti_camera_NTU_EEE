/**
 ******************************************************************************
 *
 *  @file bl_mod_params.h
 *
 *  @brief Declaration of module parameters
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


#ifndef _BL_MOD_PARAM_H_
#define _BL_MOD_PARAM_H_

struct bl_mod_params {
    bool ht_on;
    bool vht_on;
    bool he_on;
    int mcs_map;
    int he_mcs_map;
    bool he_ul_on;
    bool ldpc_on;
    bool stbc_on;
    bool dcm_on;
    bool gf_rx_on;
    int phy_cfg;
    int uapsd_timeout;
    bool ap_uapsd_on;
    bool sgi;
    bool sgi80;
    bool use_2040;
    bool use_80;
    bool custregd;
    bool custchan;
    int nss;
    int amsdu_rx_max;
    bool bfmee;
    bool bfmer;
    bool mesh;
    bool murx;
    bool mutx;
    bool mutx_on;
    unsigned int roc_dur_max;
    int listen_itv;
    bool listen_bcmc;
    int lp_clk_ppm;
    bool ps_on;
    int tx_lft;
    int amsdu_maxnb;
    int uapsd_queues;
    bool tdls;
    bool uf;
    char *ftl;
    bool dpsm;
    int tx_to_bk;
    int tx_to_be;
    int tx_to_vi;
    int tx_to_vo;
    bool ant_div;
    char *wifi_mac;
    bool mp_mode;	
    bool tcp_ack_filter;
    int  tcp_ack_max;
    char *cal_data_cfg;
    char * tx_pwr_cfg;
    char * country_pwr_cfg;
    char * country_code;

    int opmode;
    int rx_work;
	int rx_reorder_to;
    int tcp_ack_flush_to;
    int tcp_rate;

    #if defined(CONFIG_FW_COMBO) && defined(CONFIG_BL_BTUART) && !defined(CONFIG_BL_BTSDU)
    int btble_uart_baud;
    int btble_uart_flow;
    int btble_uart_rts;
    int btble_uart_cts;
    #endif

    bool pn_check;
    
    char *dump_dir;
};

extern struct bl_mod_params bl_mod_params;

struct bl_hw;
struct wiphy;

int bl_handle_dynparams(struct bl_hw *bl_hw, struct wiphy *wiphy);
void bl_custregd(struct bl_hw *bl_hw, struct wiphy *wiphy);
void bl_enable_wapi(struct bl_hw *bl_hw);
void bl_enable_mfp(struct bl_hw *bl_hw);
void bl_enable_gcmp(struct bl_hw *bl_hw);
void bl_adjust_amsdu_maxnb(struct bl_hw *bl_hw);

#endif /* _BL_MOD_PARAM_H_ */
