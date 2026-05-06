/**
******************************************************************************
*
*  @file bl_mod_params.c
*
*  @brief Set configuration according to modules parameters
*
*  Copyright (C) BouffaloLab 2017-2023
*
*  Licensed under the Apache License, Version 2.0 (the License);
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
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
#include <linux/rtnetlink.h>

#include "bl_defs.h"
#include "bl_tx.h"
#include "bl_hal_desc.h"
#include "bl_cfgfile.h"
#include "bl_compat.h"

#define COMMON_PARAM(name, default_softmac, default_fullmac)    \
                    .name = default_fullmac,
#define SOFTMAC_PARAM(name, default)
#define FULLMAC_PARAM(name, default) .name = default,

struct bl_mod_params bl_mod_params = {
    /* common parameters */
    COMMON_PARAM(ht_on, true, true)
    COMMON_PARAM(vht_on, true, true)
    COMMON_PARAM(he_on, true, true)
    COMMON_PARAM(mcs_map, IEEE80211_VHT_MCS_SUPPORT_0_9, IEEE80211_VHT_MCS_SUPPORT_0_9)
    COMMON_PARAM(he_mcs_map, IEEE80211_HE_MCS_SUPPORT_0_9, IEEE80211_HE_MCS_SUPPORT_0_9)
    COMMON_PARAM(he_ul_on, false, false)
    //If 616L, mm_version_cfm got, then force ldpc to false
    COMMON_PARAM(ldpc_on, true, true)
    COMMON_PARAM(stbc_on, false, false)
    COMMON_PARAM(dcm_on, true, true)
    COMMON_PARAM(gf_rx_on, true, true)
    COMMON_PARAM(phy_cfg, 2, 2)
    COMMON_PARAM(uapsd_timeout, 300, 300)
    COMMON_PARAM(ap_uapsd_on, true, true)
    COMMON_PARAM(sgi, true, true)
    COMMON_PARAM(sgi80, false, false)
    //If 616L, mm_version_cfm got, then force this to false
#ifdef CONFIG_BL_SDIO    
    COMMON_PARAM(use_2040, 0, 0)
#else
    COMMON_PARAM(use_2040, 1, 1)
#endif
    COMMON_PARAM(nss, 1, 1)
    COMMON_PARAM(amsdu_rx_max, 0, 0)
    COMMON_PARAM(bfmee, true, true)
    COMMON_PARAM(bfmer, false, false)
    COMMON_PARAM(mesh, true, true)
    COMMON_PARAM(murx, true, true)
    COMMON_PARAM(mutx, true, true)
    COMMON_PARAM(mutx_on, true, true)
    COMMON_PARAM(use_80, false, false)
    COMMON_PARAM(custregd, false, false)
    COMMON_PARAM(custchan, false, false)
    COMMON_PARAM(roc_dur_max, 500, 500)
    COMMON_PARAM(listen_itv, 0, 0)
    COMMON_PARAM(listen_bcmc, true, true)
    COMMON_PARAM(lp_clk_ppm, 20, 20)
    COMMON_PARAM(ps_on, false, false)
    COMMON_PARAM(tx_lft, BL_TX_LIFETIME_MS, BL_TX_LIFETIME_MS)
    COMMON_PARAM(amsdu_maxnb, NX_TX_PAYLOAD_MAX, NX_TX_PAYLOAD_MAX)
    // By default, only enable UAPSD for Voice queue (see IEEE80211_DEFAULT_UAPSD_QUEUE comment)
   // COMMON_PARAM(uapsd_queues, IEEE80211_WMM_IE_STA_QOSINFO_AC_VO, IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
    COMMON_PARAM(uapsd_queues, 0, 0)
    COMMON_PARAM(tdls, true, true)
    COMMON_PARAM(uf, false, false)
    COMMON_PARAM(ftl, "", "")
    COMMON_PARAM(dpsm, true, true)
    COMMON_PARAM(tx_to_bk, 0, 0)
    COMMON_PARAM(tx_to_be, 0, 0)
    COMMON_PARAM(tx_to_vi, 0, 0)
    COMMON_PARAM(tx_to_vo, 0, 0)
    COMMON_PARAM(wifi_mac, NULL, NULL)
    COMMON_PARAM(mp_mode, false, false)

    COMMON_PARAM(tcp_ack_filter, false, false)
#ifdef CONFIG_BL_SDIO
    COMMON_PARAM(tcp_ack_max, 5, 5)
#else
    COMMON_PARAM(tcp_ack_max, 10, 10)
#endif
    COMMON_PARAM(cal_data_cfg, 0, 0)
    COMMON_PARAM(tx_pwr_cfg, 0, 0)
    COMMON_PARAM(country_pwr_cfg, 0, 0)

    COMMON_PARAM(country_code, 0, 0)

    COMMON_PARAM(opmode, BL_OPMODE_STA, BL_OPMODE_STA)

    COMMON_PARAM(rx_work, 1, 1)    
#ifdef CONFIG_BL_SDIO
    COMMON_PARAM(rx_reorder_to, 10, 10)
    COMMON_PARAM(tcp_ack_flush_to, 20, 20)
#else
    COMMON_PARAM(rx_reorder_to, 40, 40)
    COMMON_PARAM(tcp_ack_flush_to, 50, 50)
#endif
    COMMON_PARAM(tcp_rate, 96000, 96000)
    COMMON_PARAM(pn_check, false, false)

    /* SOFTMAC only parameters */
    SOFTMAC_PARAM(mfp_on, false)
    SOFTMAC_PARAM(gf_on, false)
    SOFTMAC_PARAM(bwsig_on, true)
    SOFTMAC_PARAM(dynbw_on, true)
    SOFTMAC_PARAM(agg_tx, true)
    SOFTMAC_PARAM(amsdu_force, 2)
    SOFTMAC_PARAM(rc_probes_on, false)
    SOFTMAC_PARAM(cmon, true)
    SOFTMAC_PARAM(hwscan, true)
    SOFTMAC_PARAM(autobcn, true)

    /* FULLMAC only parameters */
    FULLMAC_PARAM(ant_div, true)

#if defined(CONFIG_FW_COMBO) && defined(CONFIG_BL_BTUART) && !defined(CONFIG_BL_BTSDU)
    COMMON_PARAM(btble_uart_baud, 0, 0)
    COMMON_PARAM(btble_uart_flow, 1, 1)
    COMMON_PARAM(btble_uart_rts, 28, 28)
    COMMON_PARAM(btble_uart_cts, 29, 29)
#endif

    COMMON_PARAM(dump_dir, "/lib/firmware/", "/lib/firmware/")

};

module_param_named(dump_dir, bl_mod_params.dump_dir, charp, S_IRUGO);
MODULE_PARM_DESC(dump_dir, "dir for dump fw file (Default: /lib/firmware)");

module_param_named(wifi_mac, bl_mod_params.wifi_mac, charp, S_IRUGO);
MODULE_PARM_DESC(wifi_mac, "Wi-Fi MAC addr, 6Bytes hex value like aa:00:cc:dd:ee:ff, (1st prio use module param MAC, 2nd prio use eFuse MAC, 3rd prio use config file MAC)");

/* FULLMAC specific parameters*/
module_param_named(ant_div, bl_mod_params.ant_div, bool, S_IRUGO);
MODULE_PARM_DESC(ant_div, "Enable Antenna Diversity (Default: 1)");

module_param_named(ht_on, bl_mod_params.ht_on, bool, S_IRUGO);
MODULE_PARM_DESC(ht_on, "Enable HT (Default: 1)");

module_param_named(vht_on, bl_mod_params.vht_on, bool, S_IRUGO);
MODULE_PARM_DESC(vht_on, "Enable VHT (Default: 1)");

module_param_named(he_on, bl_mod_params.he_on, bool, S_IRUGO);
MODULE_PARM_DESC(he_on, "Enable HE (Default: 1)");

module_param_named(mcs_map, bl_mod_params.mcs_map, int, S_IRUGO);
MODULE_PARM_DESC(mcs_map,  "VHT MCS map value  0: MCS0_7, 1: MCS0_8, 2: MCS0_9"
                 " (Default: 2)");

module_param_named(he_mcs_map, bl_mod_params.he_mcs_map, int, S_IRUGO);
MODULE_PARM_DESC(he_mcs_map,  "HE MCS map value  0: MCS0_7, 1: MCS0_9, 2: MCS0_11"
                 " (Default: 1)");

module_param_named(he_ul_on, bl_mod_params.he_ul_on, bool, S_IRUGO);
MODULE_PARM_DESC(he_ul_on, "Enable HE OFDMA UL (Default: 0)");

module_param_named(amsdu_maxnb, bl_mod_params.amsdu_maxnb, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(amsdu_maxnb, "Maximum number of MSDUs inside an A-MSDU in TX: (Default: NX_TX_PAYLOAD_MAX)");

module_param_named(ps_on, bl_mod_params.ps_on, bool, S_IRUGO);
MODULE_PARM_DESC(ps_on, "Enable PowerSaving (Default: 0-Disable)");

module_param_named(tx_lft, bl_mod_params.tx_lft, int, 0644);
MODULE_PARM_DESC(tx_lft, "Tx lifetime (ms) - setting it to 0 disables retries "
                 "(Default: "__stringify(BL_TX_LIFETIME_MS)")");

module_param_named(ldpc_on, bl_mod_params.ldpc_on, bool, S_IRUGO);
MODULE_PARM_DESC(ldpc_on, "Enable LDPC (Default: 1)");

module_param_named(stbc_on, bl_mod_params.stbc_on, bool, S_IRUGO);
MODULE_PARM_DESC(stbc_on, "Enable STBC in RX (Default: 1)");

module_param_named(dcm_on, bl_mod_params.dcm_on, bool, S_IRUGO);
MODULE_PARM_DESC(dcm_on, "Enable dcm (Default: 0)");

module_param_named(gf_rx_on, bl_mod_params.gf_rx_on, bool, S_IRUGO);
MODULE_PARM_DESC(gf_rx_on, "Enable HT greenfield in reception (Default: 1)");

module_param_named(phycfg, bl_mod_params.phy_cfg, int, S_IRUGO);
MODULE_PARM_DESC(phycfg,
                 "0 <= phycfg <= 5 : RF Channel Conf (Default: 2(C0-A1-B2))");

module_param_named(uapsd_timeout, bl_mod_params.uapsd_timeout, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(uapsd_timeout,
                 "UAPSD Timer timeout, in ms (Default: 300). If 0, UAPSD is disabled");

module_param_named(uapsd_queues, bl_mod_params.uapsd_queues, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(uapsd_queues, "UAPSD Queues, integer value, must be seen as a bitfield\n"
                 "        Bit 0 = VO\n"
                 "        Bit 1 = VI\n"
                 "        Bit 2 = BK\n"
                 "        Bit 3 = BE\n"
                 "     -> uapsd_queues=7 will enable uapsd for VO, VI and BK queues");

module_param_named(ap_uapsd_on, bl_mod_params.ap_uapsd_on, bool, S_IRUGO);
MODULE_PARM_DESC(ap_uapsd_on, "Enable UAPSD in AP mode (Default: 1)");

module_param_named(sgi, bl_mod_params.sgi, bool, S_IRUGO);
MODULE_PARM_DESC(sgi, "Advertise Short Guard Interval support (Default: 1)");

module_param_named(sgi80, bl_mod_params.sgi80, bool, S_IRUGO);
MODULE_PARM_DESC(sgi80, "Advertise Short Guard Interval support for 80MHz (Default: 1)");

module_param_named(use_2040, bl_mod_params.use_2040, bool, S_IRUGO);
MODULE_PARM_DESC(use_2040, "Use tweaked 20-40MHz mode (Default: 1)");

module_param_named(use_80, bl_mod_params.use_80, bool, S_IRUGO);
MODULE_PARM_DESC(use_80, "Enable 80MHz (Default: 1)");

module_param_named(custregd, bl_mod_params.custregd, bool, S_IRUGO);
MODULE_PARM_DESC(custregd,
                 "Use permissive custom regulatory rules (for testing ONLY) (Default: 0)");

module_param_named(custchan, bl_mod_params.custchan, bool, S_IRUGO);
MODULE_PARM_DESC(custchan,
                 "Extend channel set to non-standard channels (for testing ONLY) (Default: 0)");

module_param_named(nss, bl_mod_params.nss, int, S_IRUGO);
MODULE_PARM_DESC(nss, "1 <= nss <= 2 : Supported number of Spatial Streams (Default: 1)");

module_param_named(amsdu_rx_max, bl_mod_params.amsdu_rx_max, int, S_IRUGO);
MODULE_PARM_DESC(amsdu_rx_max, "0 <= amsdu_rx_max <= 2 : Maximum A-MSDU size supported in RX\n"
                 "        0: 3895 bytes\n"
                 "        1: 7991 bytes\n"
                 "        2: 11454 bytes\n"
                 "        This value might be reduced according to the FW capabilities.\n"
                 "        Default: 2");

module_param_named(bfmee, bl_mod_params.bfmee, bool, S_IRUGO);
MODULE_PARM_DESC(bfmee, "Enable Beamformee Capability (Default: 1-Enabled)");

module_param_named(bfmer, bl_mod_params.bfmer, bool, S_IRUGO);
MODULE_PARM_DESC(bfmer, "Enable Beamformer Capability (Default: 0-Disabled)");

module_param_named(mesh, bl_mod_params.mesh, bool, S_IRUGO);
MODULE_PARM_DESC(mesh, "Enable Meshing Capability (Default: 1-Enabled)");

module_param_named(murx, bl_mod_params.murx, bool, S_IRUGO);
MODULE_PARM_DESC(murx, "Enable MU-MIMO RX Capability (Default: 1-Enabled)");

module_param_named(mutx, bl_mod_params.mutx, bool, S_IRUGO);
MODULE_PARM_DESC(mutx, "Enable MU-MIMO TX Capability (Default: 1-Enabled)");

module_param_named(mutx_on, bl_mod_params.mutx_on, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mutx_on, "Enable MU-MIMO transmissions (Default: 1-Enabled)");

module_param_named(roc_dur_max, bl_mod_params.roc_dur_max, int, S_IRUGO);
MODULE_PARM_DESC(roc_dur_max, "Maximum Remain on Channel duration");

module_param_named(listen_itv, bl_mod_params.listen_itv, int, S_IRUGO);
MODULE_PARM_DESC(listen_itv, "Maximum listen interval");

module_param_named(listen_bcmc, bl_mod_params.listen_bcmc, bool, S_IRUGO);
MODULE_PARM_DESC(listen_bcmc, "Wait for BC/MC traffic following DTIM beacon");

module_param_named(lp_clk_ppm, bl_mod_params.lp_clk_ppm, int, S_IRUGO);
MODULE_PARM_DESC(lp_clk_ppm, "Low Power Clock accuracy of the local device");

module_param_named(tdls, bl_mod_params.tdls, bool, S_IRUGO);
MODULE_PARM_DESC(tdls, "Enable TDLS (Default: 1-Enabled)");

module_param_named(uf, bl_mod_params.uf, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(uf, "Enable Unsupported HT Frame Logging (Default: 0-Disabled)");

module_param_named(ftl, bl_mod_params.ftl, charp, S_IRUGO);
MODULE_PARM_DESC(ftl, "Firmware trace level  (Default: \"\")");

module_param_named(dpsm, bl_mod_params.dpsm, bool, S_IRUGO);
MODULE_PARM_DESC(dpsm, "Enable Dynamic PowerSaving (Default: 1-Enabled)");

module_param_named(tx_to_bk, bl_mod_params.tx_to_bk, int, S_IRUGO);
MODULE_PARM_DESC(tx_to_bk,
     "TX timeout for BK, in ms (Default: 0, Max: 65535). If 0, default value is applied");

module_param_named(tx_to_be, bl_mod_params.tx_to_be, int, S_IRUGO);
MODULE_PARM_DESC(tx_to_be,
     "TX timeout for BE, in ms (Default: 0, Max: 65535). If 0, default value is applied");

module_param_named(tx_to_vi, bl_mod_params.tx_to_vi, int, S_IRUGO);
MODULE_PARM_DESC(tx_to_vi,
     "TX timeout for VI, in ms (Default: 0, Max: 65535). If 0, default value is applied");

module_param_named(tx_to_vo, bl_mod_params.tx_to_vo, int, S_IRUGO);
MODULE_PARM_DESC(tx_to_vo,
     "TX timeout for VO, in ms (Default: 0, Max: 65535). If 0, default value is applied");

module_param_named(mp_mode, bl_mod_params.mp_mode, bool, S_IRUGO);
MODULE_PARM_DESC(mp_mode, "MP mode: 0-normal driver, 1-mp test driver");

module_param_named(tcp_ack_filter, bl_mod_params.tcp_ack_filter, bool, S_IRUGO);
MODULE_PARM_DESC(tcp_ack_filter, "Enable TCP ACK filter flow (Default: 1-Enable)");

module_param_named(tcp_ack_max, bl_mod_params.tcp_ack_max, int, S_IRUGO);
MODULE_PARM_DESC(tcp_ack_max, "max tcp_ack number driver blocked (Default: 10)");

module_param_named(cal_data_cfg, bl_mod_params.cal_data_cfg, charp, S_IRUGO);
MODULE_PARM_DESC(cal_data_cfg, "cal_data_cfg: file path of cal data cfg for power offset and xtal cap");

module_param_named(tx_pwr_cfg, bl_mod_params.tx_pwr_cfg, charp, S_IRUGO);
MODULE_PARM_DESC(tx_pwr_cfg, "tx_pwr_cfg: file path of tx power cfg");

module_param_named(country_pwr_cfg, bl_mod_params.country_pwr_cfg, charp, S_IRUGO);
MODULE_PARM_DESC(country_pwr_cfg, "country_pwr_cfg: file path of country power cfg");

module_param_named(opmode, bl_mod_params.opmode, int, S_IRUGO);
MODULE_PARM_DESC(opmode, "Operation mode: 0-STA/AP, 1-Rsvd, 2-AP+STA concurrent, 3-AP+STA repeater, (Default: 0)");

module_param_named(rx_work, bl_mod_params.rx_work, int, S_IRUGO);
MODULE_PARM_DESC(rx_work, "use rx workqueue or not (Default: 1)");

module_param_named(rx_reorder_to, bl_mod_params.rx_reorder_to, int, S_IRUGO);
MODULE_PARM_DESC(rx_reorder_to, "rx reorder timeout value (Default: 60)");

module_param_named(tcp_ack_flush_to, bl_mod_params.tcp_ack_flush_to, int, S_IRUGO);
MODULE_PARM_DESC(tcp_ack_flush_to, "tcp ack flush timeout value (Default: 50)");

module_param_named(tcp_rate, bl_mod_params.tcp_rate, int, S_IRUGO);
MODULE_PARM_DESC(tcp_rate, "tcp rate value (Default: 52000)");

module_param_named(country_code, bl_mod_params.country_code, charp, S_IRUGO);
MODULE_PARM_DESC(country_code, "country_code: US, CN...");

module_param_named(pn_check, bl_mod_params.pn_check, bool, S_IRUGO);
MODULE_PARM_DESC(pn_check, "Enable Driver PN check (Default: 0-Disable)");

#if defined(CONFIG_FW_COMBO) && defined(CONFIG_BL_BTUART) && !defined(CONFIG_BL_BTSDU)
module_param_named(btble_uart_baud, bl_mod_params.btble_uart_baud, int, S_IRUGO);
MODULE_PARM_DESC(btble_uart_baud, "BTBLE UART BAUDRATE (Default: 0)");
module_param_named(btble_uart_flow, bl_mod_params.btble_uart_flow, int, S_IRUGO);
MODULE_PARM_DESC(btble_uart_flow, "BTBLE UART FLOW CTRL (Default: 0)");
module_param_named(btble_uart_rts, bl_mod_params.btble_uart_rts, int, S_IRUGO);
MODULE_PARM_DESC(btble_uart_rts, "BTBLE UART RTS PIN (Default: 28), depend on flow ctl");
module_param_named(btble_uart_cts, bl_mod_params.btble_uart_cts, int, S_IRUGO);
MODULE_PARM_DESC(btble_uart_cts, "BTBLE UART CTS PIN (Default: 29), depend on flow ctl");
#endif

/* Regulatory rules */
static struct ieee80211_regdomain bl_regdom = {
    .n_reg_rules = 2,
    .alpha2 = "99",
    .reg_rules = {
        REG_RULE(2390 - 10, 2510 + 10, 40, 0, 1000, 0),
        REG_RULE(5150 - 10, 5970 + 10, 80, 0, 1000, 0),
    }
};

static const int mcs_map_to_rate[4][3] = {
    [PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_7] = 65,
    [PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_8] = 78,
    [PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_9] = 78,
    [PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_7] = 135,
    [PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_8] = 162,
    [PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_9] = 180,
    [PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_7] = 292,
    [PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_8] = 351,
    [PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_9] = 390,
    [PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_7] = 585,
    [PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_8] = 702,
    [PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_9] = 780,
};

#define MAX_VHT_RATE(map, nss, bw) (mcs_map_to_rate[bw][map] * (nss))

/**
 * Do some sanity check
 *
 */
static int bl_check_fw_hw_feature(struct bl_hw *bl_hw,
                                    struct wiphy *wiphy)
{
    u32_l sys_feat = bl_hw->version_cfm.features;
    u32_l mac_feat = bl_hw->version_cfm.version_machw_1;
    u32_l phy_feat = bl_hw->version_cfm.version_phy_1;
    u32_l phy_vers = bl_hw->version_cfm.version_phy_2;
    u16_l max_sta_nb = bl_hw->version_cfm.max_sta_nb;
    u8_l max_vif_nb = bl_hw->version_cfm.max_vif_nb;
    int bw, res = 0;
    int amsdu_rx;

    if (!bl_hw->mod_params->custregd)
        bl_hw->mod_params->custchan = false;

    if (bl_hw->mod_params->custchan) {
        bl_hw->mod_params->mesh = false;
        bl_hw->mod_params->tdls = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_UMAC_BIT))) {
        wiphy_err(wiphy,
                  "Loading softmac firmware with fullmac driver\n");
        res = -1;
    }

    if (!(sys_feat & BIT(MM_FEAT_ANT_DIV_BIT))) {
        bl_hw->mod_params->ant_div = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_VHT_BIT))) {
        bl_hw->mod_params->vht_on = false;
    }

    // Check if HE is supported
    if (!(sys_feat & BIT(MM_FEAT_HE_BIT))) {
        bl_hw->mod_params->he_on = false;
        bl_hw->mod_params->he_ul_on = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_PS_BIT))) {
        bl_hw->mod_params->ps_on = false;
    }

    /* AMSDU (non)support implies different shared structure definition
       so insure that fw and drv have consistent compilation option */
    if (sys_feat & BIT(MM_FEAT_AMSDU_BIT)) {
#ifndef CONFIG_BL_SPLIT_TX_BUF
        wiphy_err(wiphy,
                  "AMSDU enabled in firmware but support not compiled in driver\n");
        res = -1;
#else
        /* Adjust amsdu_maxnb so that it stays in allowed bounds */
        bl_adjust_amsdu_maxnb(bl_hw);
#endif /* CONFIG_BL_SPLIT_TX_BUF */
    } else {
#ifdef CONFIG_BL_SPLIT_TX_BUF
        wiphy_err(wiphy,
                  "AMSDU disabled in firmware but support compiled in driver\n");
        res = -1;
#endif /* CONFIG_BL_SPLIT_TX_BUF */
    }

    if (!(sys_feat & BIT(MM_FEAT_UAPSD_BIT))) {
        bl_hw->mod_params->uapsd_timeout = 0;
    }

    if (!(sys_feat & BIT(MM_FEAT_BFMEE_BIT))) {
        bl_hw->mod_params->bfmee = false;
    }

    if ((sys_feat & BIT(MM_FEAT_BFMER_BIT))) {
#ifndef CONFIG_BL_BFMER
        wiphy_err(wiphy,
                  "BFMER enabled in firmware but support not compiled in driver\n");
        res = -1;
#endif /* CONFIG_BL_BFMER */
        // Check PHY and MAC HW BFMER support and update parameter accordingly
        if (!(phy_feat & MDM_BFMER_BIT) || !(mac_feat & NXMAC_BFMER_BIT)) {
            bl_hw->mod_params->bfmer = false;
            // Disable the feature in the bitfield so that it won't be displayed
            sys_feat &= ~BIT(MM_FEAT_BFMER_BIT);
        }
    } else {
#ifdef CONFIG_BL_BFMER
        wiphy_err(wiphy,
                  "BFMER disabled in firmware but support compiled in driver\n");
        res = -1;
#else
        bl_hw->mod_params->bfmer = false;
#endif /* CONFIG_BL_BFMER */
    }

    if (!(sys_feat & BIT(MM_FEAT_MESH_BIT))) {
        bl_hw->mod_params->mesh = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_TDLS_BIT))) {
        bl_hw->mod_params->tdls = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_UF_BIT))) {
        bl_hw->mod_params->uf = false;
    }

#ifdef CONFIG_BL_FULLMAC
    if ((sys_feat & BIT(MM_FEAT_MON_DATA_BIT))) {
#ifndef CONFIG_BL_MON_DATA
        wiphy_err(wiphy,
                  "Monitor+Data interface support (MON_DATA) is enabled in firmware but support not compiled in driver\n");
        res = -1;
#endif /* CONFIG_BL_MON_DATA */
    } else {
#ifdef CONFIG_BL_MON_DATA
        wiphy_err(wiphy,
                  "Monitor+Data interface support (MON_DATA) disabled in firmware but support compiled in driver\n");
        res = -1;
#endif /* CONFIG_BL_MON_DATA */
    }
#endif

    // Check supported AMSDU RX size
    amsdu_rx = (sys_feat >> MM_AMSDU_MAX_SIZE_BIT0) & 0x03;
    if (amsdu_rx < bl_hw->mod_params->amsdu_rx_max) {
        bl_hw->mod_params->amsdu_rx_max = amsdu_rx;
    }

    // Check supported BW
    bw = (phy_feat & MDM_CHBW_MASK) >> MDM_CHBW_LSB;
    // Check if 80MHz BW is supported
    if (bw < 2) {
        bl_hw->mod_params->use_80 = false;
    }
    // Check if 40MHz BW is supported
    if (bw < 1)
        bl_hw->mod_params->use_2040 = false;

    // 80MHz BW shall be disabled if 40MHz is not enabled
    if (!bl_hw->mod_params->use_2040)
        bl_hw->mod_params->use_80 = false;

    // Check if HT is supposed to be supported. If not, disable VHT/HE too
    if (!bl_hw->mod_params->ht_on)
    {
        bl_hw->mod_params->vht_on = false;
        bl_hw->mod_params->he_on = false;
        bl_hw->mod_params->he_ul_on = false;
        bl_hw->mod_params->use_80 = false;
        bl_hw->mod_params->use_2040 = false;
    }

    // LDPC is mandatory for HE40 and above, so if LDPC is not supported, then disable
    // support for 40 and 80MHz
    if (bl_hw->mod_params->he_on && !bl_hw->mod_params->ldpc_on)
    {
        bl_hw->mod_params->use_80 = false;
        bl_hw->mod_params->use_2040 = false;
    }

    // HT greenfield is not supported in modem >= 3.0
    if (__MDM_MAJOR_VERSION(phy_vers) > 0) {
        bl_hw->mod_params->gf_rx_on = false;
    }

    if (!(sys_feat & BIT(MM_FEAT_MU_MIMO_RX_BIT)) ||
        !bl_hw->mod_params->bfmee) {
        bl_hw->mod_params->murx = false;
    }

    if ((sys_feat & BIT(MM_FEAT_MU_MIMO_TX_BIT))) {
#ifndef CONFIG_BL_MUMIMO_TX
        wiphy_err(wiphy,
                  "MU-MIMO TX enabled in firmware but support not compiled in driver\n");
        res = -1;
#endif /* CONFIG_BL_MUMIMO_TX */
        if (!bl_hw->mod_params->bfmer)
            bl_hw->mod_params->mutx = false;
        // Check PHY and MAC HW MU-MIMO TX support and update parameter accordingly
        else if (!(phy_feat & MDM_MUMIMOTX_BIT) || !(mac_feat & NXMAC_MU_MIMO_TX_BIT)) {
                bl_hw->mod_params->mutx = false;
                // Disable the feature in the bitfield so that it won't be displayed
                sys_feat &= ~BIT(MM_FEAT_MU_MIMO_TX_BIT);
        }
    } else {
#ifdef CONFIG_BL_MUMIMO_TX
        wiphy_err(wiphy,
                  "MU-MIMO TX disabled in firmware but support compiled in driver\n");
        res = -1;
#else
        bl_hw->mod_params->mutx = false;
#endif /* CONFIG_BL_MUMIMO_TX */
    }

    if (sys_feat & BIT(MM_FEAT_WAPI_BIT)) {
        bl_enable_wapi(bl_hw);
    }

#ifdef CONFIG_BL_FULLMAC
    if (sys_feat & BIT(MM_FEAT_MFP_BIT)) {
        bl_enable_mfp(bl_hw);
    }

    if (mac_feat & NXMAC_GCMP_BIT) {
        bl_enable_gcmp(bl_hw);
    }
#endif

#define QUEUE_NAME "Broadcast/Multicast queue "

    if (sys_feat & BIT(MM_FEAT_BCN_BIT)) {
#if NX_TXQ_CNT == 4
        wiphy_err(wiphy, QUEUE_NAME
                  "enabled in firmware but support not compiled in driver\n");
        res = -1;
#endif /* NX_TXQ_CNT == 4 */
    } else {
#if NX_TXQ_CNT == 5
        wiphy_err(wiphy, QUEUE_NAME
                  "disabled in firmware but support compiled in driver\n");
        res = -1;
#endif /* NX_TXQ_CNT == 5 */
    }
#undef QUEUE_NAME

#ifdef CONFIG_BL_RADAR
    if (sys_feat & BIT(MM_FEAT_RADAR_BIT)) {
        /* Enable combination with radar detection */
        wiphy->n_iface_combinations++;
    }
#endif /* CONFIG_BL_RADAR */

#ifndef CONFIG_BL_SDM
    switch (__MDM_PHYCFG_FROM_VERS(phy_feat)) {
        case MDM_PHY_CONFIG_TRIDENT:
            bl_hw->mod_params->nss = 1;
            if ((bl_hw->mod_params->phy_cfg < 0) || (bl_hw->mod_params->phy_cfg > 2))
                bl_hw->mod_params->phy_cfg = 2;
            break;
        case MDM_PHY_CONFIG_KARST:
        case MDM_PHY_CONFIG_CATAXIA:
            {
                int nss_supp = (phy_feat & MDM_NSS_MASK) >> MDM_NSS_LSB;
                
                if (bl_hw->mod_params->nss > nss_supp)
                    bl_hw->mod_params->nss = nss_supp;
                    
                if ((bl_hw->mod_params->phy_cfg < 0) || (bl_hw->mod_params->phy_cfg > 1))
                    bl_hw->mod_params->phy_cfg = 0;
            }
            break;
        default:
            WARN_ON(1);
            break;
    }
#endif /* CONFIG_BL_SDM */

    if ((bl_hw->mod_params->nss < 1) || (bl_hw->mod_params->nss > 2))
        bl_hw->mod_params->nss = 1;

    if ((bl_hw->mod_params->mcs_map < 0) || (bl_hw->mod_params->mcs_map > 2))
        bl_hw->mod_params->mcs_map = 0;

#define PRINT_BL_PHY_FEAT(feat)                                   \
    (phy_feat & MDM_##feat##_BIT ? "["#feat"]" : "")

    wiphy_info(wiphy, "PHY features: [NSS=%d][CHBW=%d]%s%s%s%s%s%s%s\n",
               (phy_feat & MDM_NSS_MASK) >> MDM_NSS_LSB,
               20 * (1 << ((phy_feat & MDM_CHBW_MASK) >> MDM_CHBW_LSB)),
               (phy_feat & (MDM_LDPCDEC_BIT | MDM_LDPCENC_BIT)) ==
                       (MDM_LDPCDEC_BIT | MDM_LDPCENC_BIT) ? "[LDPC]" : "",
               PRINT_BL_PHY_FEAT(VHT),
               PRINT_BL_PHY_FEAT(HE),
               PRINT_BL_PHY_FEAT(BFMER),
               PRINT_BL_PHY_FEAT(BFMEE),
               PRINT_BL_PHY_FEAT(MUMIMOTX),
               PRINT_BL_PHY_FEAT(MUMIMORX)
               );

#define PRINT_BL_FEAT(feat)                                   \
    (sys_feat & BIT(MM_FEAT_##feat##_BIT) ? "["#feat"]" : "")

    wiphy_info(wiphy, "FW features: %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
               PRINT_BL_FEAT(BCN),
               PRINT_BL_FEAT(RADAR),
               PRINT_BL_FEAT(PS),
               PRINT_BL_FEAT(UAPSD),
               PRINT_BL_FEAT(AMPDU),
               PRINT_BL_FEAT(AMSDU),
               PRINT_BL_FEAT(P2P),
               PRINT_BL_FEAT(P2P_GO),
               PRINT_BL_FEAT(UMAC),
               PRINT_BL_FEAT(VHT),
               PRINT_BL_FEAT(HE),
               PRINT_BL_FEAT(BFMEE),
               PRINT_BL_FEAT(BFMER),
               PRINT_BL_FEAT(WAPI),
               PRINT_BL_FEAT(MFP),
               PRINT_BL_FEAT(MU_MIMO_RX),
               PRINT_BL_FEAT(MU_MIMO_TX),
               PRINT_BL_FEAT(MESH),
               PRINT_BL_FEAT(TDLS),
               PRINT_BL_FEAT(ANT_DIV),
               PRINT_BL_FEAT(UF),
               PRINT_BL_FEAT(TWT),
               PRINT_BL_FEAT(FTM_INIT),
               PRINT_BL_FEAT(FAKE_FTM_RSP));
#undef PRINT_BL_FEAT

    if(max_sta_nb != NX_REMOTE_STA_MAX)
    {
        wiphy_err(wiphy, "Different number of supported stations between driver and FW (%d != %d)\n",
                  NX_REMOTE_STA_MAX, max_sta_nb);
        res = -1;
    }

    if(max_vif_nb != NX_VIRT_DEV_MAX)
    {
        wiphy_err(wiphy, "Different number of supported virtual interfaces between driver and FW (%d != %d)\n",
                  NX_VIRT_DEV_MAX, max_vif_nb);
        res = -1;
    }

    return res;
}

static void bl_set_vht_capa_2g(struct bl_hw *bl_hw, struct wiphy *wiphy)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
    struct ieee80211_supported_band *band_2GHz = wiphy->bands[NL80211_BAND_2GHZ];
#endif
    struct ieee80211_sta_vht_cap *vht_cap = NULL;
    int i;
    int nss = bl_hw->mod_params->nss;
    int mcs_map;
    int mcs_map_max;
    int bw_max;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (!bl_hw->mod_params->vht_on)
        return;
    
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
    vht_cap = (struct ieee80211_sta_vht_cap *) &band_2GHz->vht_cap;
#else
    vht_cap = (struct ieee80211_sta_vht_cap *) &bl_hw->vht_cap;
#endif  

    vht_cap->vht_supported = true;
    if (bl_hw->mod_params->sgi80)
        vht_cap->cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
    if (bl_hw->mod_params->stbc_on)
        vht_cap->cap |= IEEE80211_VHT_CAP_RXSTBC_1;
    if (bl_hw->mod_params->ldpc_on)
        vht_cap->cap |= IEEE80211_VHT_CAP_RXLDPC;
    if (bl_hw->mod_params->bfmee) {
        vht_cap->cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
        vht_cap->cap |= 3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
    }
    if (nss > 1)
        vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;

    // Update the AMSDU max RX size (not shifted as located at offset 0 of the VHT cap)
    vht_cap->cap |= bl_hw->mod_params->amsdu_rx_max;
    // MAX_A_MPDU_LENGTH_EXPONENT
    vht_cap->cap |= 2 << 23;

    if (bl_hw->mod_params->bfmer) {
        vht_cap->cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
        /* Set number of sounding dimensions */
        vht_cap->cap |= (nss - 1) << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
    }
    if (bl_hw->mod_params->murx)
        vht_cap->cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
    if (bl_hw->mod_params->mutx)
        vht_cap->cap |= IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;

    /*
     * MCS map:
     * This capabilities are filled according to the mcs_map module parameter.
     * However currently we have some limitations due to FPGA clock constraints
     * that prevent always using the range of MCS that is defined by the
     * parameter:
     *   - in RX, 2SS, we support up to MCS7
     *   - in TX, 2SS, we support up to MCS8
     */
    // Get max supported BW
    if (bl_hw->mod_params->use_80)
        bw_max = PHY_CHNL_BW_80;
    else if (bl_hw->mod_params->use_2040)
        bw_max = PHY_CHNL_BW_40;
    else
        bw_max = PHY_CHNL_BW_20;

    // Check if MCS map should be limited to MCS0_8 due to the standard. Indeed in BW20,
    // MCS9 is not supported in 1 and 2 SS
    //force use mcs 8 for BW20/40
//    if (bl_hw->mod_params->use_2040)
//        mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_9;
//    else
        mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_8;

    mcs_map = min_t(int, bl_hw->mod_params->mcs_map, mcs_map_max);
    vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(0);
    
    for (i = 0; i < nss; i++) {
        vht_cap->vht_mcs.rx_mcs_map |= cpu_to_le16(mcs_map << (i*2));
        vht_cap->vht_mcs.rx_highest = MAX_VHT_RATE(mcs_map, nss, bw_max);
        mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_7;
    }
    
    for (; i < 8; i++) {
        vht_cap->vht_mcs.rx_mcs_map |= cpu_to_le16(
            IEEE80211_VHT_MCS_NOT_SUPPORTED << (i*2));
    }

    mcs_map = min_t(int, bl_hw->mod_params->mcs_map, mcs_map_max);
    vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(0);
    
    for (i = 0; i < nss; i++) {
        vht_cap->vht_mcs.tx_mcs_map |= cpu_to_le16(mcs_map << (i*2));
        vht_cap->vht_mcs.tx_highest = MAX_VHT_RATE(mcs_map, nss, bw_max);
        mcs_map = min_t(int, bl_hw->mod_params->mcs_map,
                        IEEE80211_VHT_MCS_SUPPORT_0_8);
    }
    
    for (; i < 8; i++) {
        vht_cap->vht_mcs.tx_mcs_map |= cpu_to_le16(
            IEEE80211_VHT_MCS_NOT_SUPPORTED << (i*2));
    }

    if (!bl_hw->mod_params->use_80) 
        vht_cap->cap &= ~IEEE80211_VHT_CAP_SHORT_GI_80;
    
}

__attribute__((unused)) static void bl_set_vht_capa(struct bl_hw *bl_hw, struct wiphy *wiphy)
{
    struct ieee80211_supported_band *band_5GHz = wiphy->bands[NL80211_BAND_5GHZ];
    int i;
    int nss = bl_hw->mod_params->nss;
    int mcs_map;
    int mcs_map_max;
    int bw_max;

    BL_DBG(BL_FN_ENTRY_STR);
    if (!bl_hw->mod_params->vht_on)
        return;

    band_5GHz->vht_cap.vht_supported = true;
    if (bl_hw->mod_params->sgi80)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
    if (bl_hw->mod_params->stbc_on)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_RXSTBC_1;
    if (bl_hw->mod_params->ldpc_on)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_RXLDPC;
    if (bl_hw->mod_params->bfmee) {
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
        band_5GHz->vht_cap.cap |= 3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
#endif
    }
    
    if (nss > 1)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_TXSTBC;

    // Update the AMSDU max RX size (not shifted as located at offset 0 of the VHT cap)
    band_5GHz->vht_cap.cap |= bl_hw->mod_params->amsdu_rx_max;

    if (bl_hw->mod_params->bfmer) {
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
        /* Set number of sounding dimensions */
        band_5GHz->vht_cap.cap |= (nss - 1) << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
#endif
    }
    
    if (bl_hw->mod_params->murx)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
    if (bl_hw->mod_params->mutx)
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;

    /*
     * MCS map:
     * This capabilities are filled according to the mcs_map module parameter.
     * However currently we have some limitations due to FPGA clock constraints
     * that prevent always using the range of MCS that is defined by the
     * parameter:
     *   - in RX, 2SS, we support up to MCS7
     *   - in TX, 2SS, we support up to MCS8
     */
    // Get max supported BW
    if (bl_hw->mod_params->use_80)
        bw_max = PHY_CHNL_BW_80;
    else if (bl_hw->mod_params->use_2040)
        bw_max = PHY_CHNL_BW_40;
    else
        bw_max = PHY_CHNL_BW_20;

    // Check if MCS map should be limited to MCS0_8 due to the standard. Indeed in BW20,
    // MCS9 is not supported in 1 and 2 SS
    if (bl_hw->mod_params->use_2040)
        mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_9;
    else
        mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_8;

    mcs_map = min_t(int, bl_hw->mod_params->mcs_map, mcs_map_max);
    band_5GHz->vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(0);
    
    for (i = 0; i < nss; i++) {
        band_5GHz->vht_cap.vht_mcs.rx_mcs_map |= cpu_to_le16(mcs_map << (i*2));
        band_5GHz->vht_cap.vht_mcs.rx_highest = MAX_VHT_RATE(mcs_map, nss, bw_max);
        mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_7;
    }
    for (; i < 8; i++) {
        band_5GHz->vht_cap.vht_mcs.rx_mcs_map |= cpu_to_le16(
            IEEE80211_VHT_MCS_NOT_SUPPORTED << (i*2));
    }

    mcs_map = min_t(int, bl_hw->mod_params->mcs_map, mcs_map_max);
    band_5GHz->vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(0);
    for (i = 0; i < nss; i++) {
        band_5GHz->vht_cap.vht_mcs.tx_mcs_map |= cpu_to_le16(mcs_map << (i*2));
        band_5GHz->vht_cap.vht_mcs.tx_highest = MAX_VHT_RATE(mcs_map, nss, bw_max);
        mcs_map = min_t(int, bl_hw->mod_params->mcs_map,
                        IEEE80211_VHT_MCS_SUPPORT_0_8);
    }
    for (; i < 8; i++) {
        band_5GHz->vht_cap.vht_mcs.tx_mcs_map |= cpu_to_le16(
            IEEE80211_VHT_MCS_NOT_SUPPORTED << (i*2));
    }

    if (!bl_hw->mod_params->use_80) {
#ifdef CONFIG_VENDOR_BL_VHT_NO80
        band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_NOT_SUP_WIDTH_80;
#endif
        band_5GHz->vht_cap.cap &= ~IEEE80211_VHT_CAP_SHORT_GI_80;
    }
}

static void bl_set_ht_capa(struct bl_hw *bl_hw, struct wiphy *wiphy)
{
    struct ieee80211_supported_band *band_5GHz = wiphy->bands[NL80211_BAND_5GHZ];
    struct ieee80211_supported_band *band_2GHz = wiphy->bands[NL80211_BAND_2GHZ];
    int i;
    int nss = bl_hw->mod_params->nss;

    BL_DBG(BL_FN_ENTRY_STR);
    if (!bl_hw->mod_params->ht_on) {
        band_2GHz->ht_cap.ht_supported = false;
        if(band_5GHz)
            band_5GHz->ht_cap.ht_supported = false;
        return;
    }

    if (bl_hw->mod_params->stbc_on)
        band_2GHz->ht_cap.cap |= 1 << IEEE80211_HT_CAP_RX_STBC_SHIFT;
    if (bl_hw->mod_params->ldpc_on)
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
        
    if (bl_hw->mod_params->use_2040) {
        band_2GHz->ht_cap.mcs.rx_mask[4] = 0x1; /* MCS32 */
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
        band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(135 * nss);
    } else {
        band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(65 * nss);
    }
    
    if (nss > 1)
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_TX_STBC;

    // Update the AMSDU max RX size
    if (bl_hw->mod_params->amsdu_rx_max)
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_MAX_AMSDU;

    if (bl_hw->mod_params->sgi) {
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;
        if (bl_hw->mod_params->use_2040) {
            band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
            band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(150 * nss);
        } else
            band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(72 * nss);
    }
    
    if (bl_hw->mod_params->gf_rx_on)
        band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_GRN_FLD;

    for (i = 0; i < nss; i++) {
        band_2GHz->ht_cap.mcs.rx_mask[i] = 0xFF;
    }

    if(band_5GHz)
        band_5GHz->ht_cap = band_2GHz->ht_cap;
    else
        printk("%s not support 5G HT\n", __func__);

    BL_DBG("%s, ht=%d, stbc=%d, ldpc=%d, rx_highest=%d, nss=%d, sgi=%d, gf=%d", 
            __func__,
            bl_hw->mod_params->ht_on, bl_hw->mod_params->stbc_on, 
            bl_hw->mod_params->ldpc_on,
            band_2GHz->ht_cap.mcs.rx_highest, nss, bl_hw->mod_params->sgi, 
            bl_hw->mod_params->gf_rx_on);
}

static void bl_set_he_capa(struct bl_hw *bl_hw, struct wiphy *wiphy)
{
    int i;
    int nss = bl_hw->mod_params->nss;
    struct ieee80211_sta_he_cap *he_cap;
    int mcs_map;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    struct ieee80211_supported_band *band_5GHz = wiphy->bands[NL80211_BAND_5GHZ];
    struct ieee80211_supported_band *band_2GHz = wiphy->bands[NL80211_BAND_2GHZ];
#endif

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (!bl_hw->mod_params->he_on) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
        band_2GHz->iftype_data = NULL;
        band_2GHz->n_iftype_data = 0;
        if(band_5GHz) {
            band_5GHz->iftype_data = NULL;
            band_5GHz->n_iftype_data = 0;
        }
#endif
        return;
    }
    
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    he_cap = (struct ieee80211_sta_he_cap *) &band_2GHz->iftype_data->he_cap;
#else
    he_cap = (struct ieee80211_sta_he_cap *) &bl_hw->he_cap;
#endif
    he_cap->has_he = true;

    #ifdef CONFIG_BL_FULLMAC
    if (bl_hw->version_cfm.features & BIT(MM_FEAT_TWT_BIT))
    {
        bl_hw->ext_capa[9] = WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT;
        he_cap->he_cap_elem.mac_cap_info[0] |= IEEE80211_HE_MAC_CAP0_TWT_REQ;
    }
    #endif

    he_cap->he_cap_elem.mac_cap_info[2] |= IEEE80211_HE_MAC_CAP2_ALL_ACK;
    if (bl_hw->mod_params->use_2040) {
        he_cap->he_cap_elem.phy_cap_info[0] |=
                        IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
        he_cap->he_cap_elem.phy_cap_info[0] |=
                        IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
        he_cap->ppe_thres[0] |= 0x10;
    }
    
#if 0
    if (bl_hw->mod_params->use_80) {
        he_cap->he_cap_elem.phy_cap_info[0] |=
                        IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
    }
#endif

    if (bl_hw->mod_params->ldpc_on) {
        he_cap->he_cap_elem.phy_cap_info[1] |= IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
    } else {
        // If no LDPC is supported, we have to limit to MCS0_9, as LDPC is mandatory
        // for MCS 10 and 11
        bl_hw->mod_params->he_mcs_map = min_t(int, bl_hw->mod_params->he_mcs_map,
                                                IEEE80211_HE_MCS_SUPPORT_0_9);
    }
    
    he_cap->he_cap_elem.phy_cap_info[1] |= IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
    he_cap->he_cap_elem.phy_cap_info[1] |= IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS;
    he_cap->he_cap_elem.phy_cap_info[2] |= IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
                                           IEEE80211_HE_PHY_CAP2_DOPPLER_RX;
    he_cap->he_cap_elem.phy_cap_info[2] |= IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS;

    if (bl_hw->mod_params->stbc_on)
        he_cap->he_cap_elem.phy_cap_info[2] |= IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ;

    if (bl_hw->mod_params->dcm_on) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
        he_cap->he_cap_elem.phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM |
                                               IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1 |
                                               IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU |
                                               IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM |
                                               IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 ;
#else
        he_cap->he_cap_elem.phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM |
                                               IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1 |
                                               IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA |
                                               IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM |
                                               IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1;
#endif
    }

    if (bl_hw->mod_params->bfmee) {
        he_cap->he_cap_elem.phy_cap_info[4] |= IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
        he_cap->he_cap_elem.phy_cap_info[4] |=
                     IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
    }
    
    he_cap->he_cap_elem.phy_cap_info[5] |= IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
                                           IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
    he_cap->he_cap_elem.phy_cap_info[6] |= IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
                                           IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
                                           IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
                                           IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB |
                                           IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO;
#else
    he_cap->he_cap_elem.phy_cap_info[6] |= IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
                                           IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
                                           IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB |
                                           IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMER_FB |
                                           IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO;
#endif
    he_cap->he_cap_elem.phy_cap_info[7] |= IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI;
    he_cap->he_cap_elem.phy_cap_info[8] |= IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G;
    he_cap->he_cap_elem.phy_cap_info[9] |= IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US;
    he_cap->he_cap_elem.phy_cap_info[9] |= IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
                            IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB;
    mcs_map = bl_hw->mod_params->he_mcs_map;
    memset(&he_cap->he_mcs_nss_supp, 0, sizeof(he_cap->he_mcs_nss_supp));
    
    for (i = 0; i < nss; i++) {
        __le16 unsup_for_ss = cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i*2));
        he_cap->he_mcs_nss_supp.rx_mcs_80 |= cpu_to_le16(mcs_map << (i*2));
        he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
        mcs_map = IEEE80211_HE_MCS_SUPPORT_0_7;
    }
    
    for (; i < 8; i++) {
        __le16 unsup_for_ss = cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i*2));
        he_cap->he_mcs_nss_supp.rx_mcs_80 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
    }
    
    mcs_map = bl_hw->mod_params->he_mcs_map;
    for (i = 0; i < nss; i++) {
        __le16 unsup_for_ss = cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i*2));
        he_cap->he_mcs_nss_supp.tx_mcs_80 |= cpu_to_le16(mcs_map << (i*2));
        he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
        mcs_map = min_t(int, bl_hw->mod_params->he_mcs_map,
                        IEEE80211_HE_MCS_SUPPORT_0_7);
    }
    
    for (; i < 8; i++) {
        __le16 unsup_for_ss = cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i*2));
        he_cap->he_mcs_nss_supp.tx_mcs_80 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
        he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
    }
}

static void bl_set_wiphy_params(struct bl_hw *bl_hw, struct wiphy *wiphy)
{
    u8_l zero_addr[6] =  { 0, 0, 0, 0, 0, 0 };

    BL_DBG(BL_FN_ENTRY_STR);
    /* FULLMAC specific parameters */
    wiphy->flags |= WIPHY_FLAG_REPORTS_OBSS;
    wiphy->max_scan_ssids = SCAN_SSID_MAX;
    wiphy->max_scan_ie_len = SCANU_MAX_IE_LEN;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
    wiphy->support_mbssid = 1;
#endif

    if (bl_hw->mod_params->tdls) {
        /* TDLS support */
        wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
#ifdef CONFIG_BL_FULLMAC
        /* TDLS external setup support */
        wiphy->flags |= WIPHY_FLAG_TDLS_EXTERNAL_SETUP;
#endif
    }

    if (bl_hw->mod_params->ap_uapsd_on)
        wiphy->flags |= WIPHY_FLAG_AP_UAPSD;

#ifdef CONFIG_BL_FULLMAC
    if (bl_hw->mod_params->ps_on)
        wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
    else
        wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif

    if (bl_hw->mod_params->custregd) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
        // Apply custom regulatory. Note that for recent kernel versions we use instead the
        // REGULATORY_WIPHY_SELF_MANAGED flag, along with the regulatory_set_wiphy_regd()
        // function, that needs to be called after wiphy registration
        printk(KERN_CRIT
               "\n\n%s: CAUTION: USING PERMISSIVE CUSTOM REGULATORY RULES\n\n",
               __func__);
        wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG;
        wiphy->regulatory_flags |= REGULATORY_IGNORE_STALE_KICKOFF;
        wiphy_apply_custom_regulatory(wiphy, &bl_regdom);
#endif
#endif

        // Check if custom channel set shall be enabled. In such case only monitor mode is
        // supported
        if (bl_hw->mod_params->custchan) {
            wiphy->interface_modes = BIT(NL80211_IFTYPE_MONITOR);

            // Enable "extra" channels
            wiphy->bands[NL80211_BAND_2GHZ]->n_channels += 13;
            if(wiphy->bands[NL80211_BAND_5GHZ])
                wiphy->bands[NL80211_BAND_5GHZ]->n_channels += 59;
        }
    }

    if(bl_hw->mod_params->wifi_mac) {
        if (sscanf(bl_hw->mod_params->wifi_mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &zero_addr[0], &zero_addr[1], &zero_addr[2], &zero_addr[3],
             &zero_addr[4], &zero_addr[5]) == ETH_ALEN)
             memcpy(wiphy->perm_addr, zero_addr, ETH_ALEN);
        else
             printk("module param MAC format wrong, should be wifi_mac=xx:xx:xx:xx:xx:xx, using efuse mac addr or conf file mac addr\n");
    }
}

__attribute__((unused)) static void bl_set_rf_params(struct bl_hw *bl_hw, 
                                                           struct wiphy *wiphy)
{
#ifndef CONFIG_BL_SDM
    struct ieee80211_supported_band *band_5GHz = wiphy->bands[NL80211_BAND_5GHZ];
    u32 mdm_phy_cfg = __MDM_PHYCFG_FROM_VERS(bl_hw->version_cfm.version_phy_1);
    struct bl_phy_conf_file phy_conf;
    
    BL_DBG(BL_FN_ENTRY_STR);
    /*
     * Get configuration file depending on the RF
     */
    if (mdm_phy_cfg == MDM_PHY_CONFIG_TRIDENT) {
        // Retrieve the Trident configuration
        bl_parse_phy_configfile(bl_hw, BL_PHY_CONFIG_TRD_NAME,
                                  &phy_conf, bl_hw->mod_params->phy_cfg);
        memcpy(&bl_hw->phy.cfg, &phy_conf.trd, sizeof(phy_conf.trd));
    } else if (mdm_phy_cfg == MDM_PHY_CONFIG_CATAXIA) {
        memset(&phy_conf.cataxia, 0, sizeof(phy_conf.cataxia));
        phy_conf.cataxia.path_used = bl_hw->mod_params->phy_cfg;
        memcpy(&bl_hw->phy.cfg, &phy_conf.cataxia, sizeof(phy_conf.cataxia));
    } else if (mdm_phy_cfg == MDM_PHY_CONFIG_KARST) {
        // We use the NSS parameter as is
        // Retrieve the Karst configuration
        bl_parse_phy_configfile(bl_hw, BL_PHY_CONFIG_KARST_NAME,
                                  &phy_conf, bl_hw->mod_params->phy_cfg);

        memcpy(&bl_hw->phy.cfg, &phy_conf.karst, sizeof(phy_conf.karst));
    } else {
        WARN_ON(1);
    }

    /*
     * adjust caps depending on the RF
     */
    switch (mdm_phy_cfg) {
        case MDM_PHY_CONFIG_TRIDENT:
        {
            wiphy_dbg(wiphy, "found Trident PHY .. limit BW to 40MHz\n");
            bl_hw->phy.limit_bw = true;
            if(band_5GHz) {
#ifdef CONFIG_VENDOR_BL_VHT_NO80
                band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_NOT_SUP_WIDTH_80;
#endif
                band_5GHz->vht_cap.cap &= ~(IEEE80211_VHT_CAP_SHORT_GI_80 |
                                        IEEE80211_VHT_CAP_RXSTBC_MASK);
            }
            break;
        }
        case MDM_PHY_CONFIG_CATAXIA:
        {
            wiphy_dbg(wiphy, "found CATAXIA PHY\n");
            break;
        }
        case MDM_PHY_CONFIG_KARST:
        {
            wiphy_dbg(wiphy, "found KARST PHY\n");
            break;
        }
        default:
            WARN_ON(1);
            break;
    }
#endif /* CONFIG_BL_SDM */
}

int bl_handle_dynparams(struct bl_hw *bl_hw, struct wiphy *wiphy)
{
    int ret;

    /* Check compatibility between requested parameters and HW/SW features */
    ret = bl_check_fw_hw_feature(bl_hw, wiphy);
    if (ret) {
        printk("bl_check_fw_hw_feature failed, return\n");
        return ret;
    }

    /* Set wiphy parameters */
    bl_set_wiphy_params(bl_hw, wiphy);

#if 0
    /* Set VHT capabilities */
    bl_set_vht_capa(bl_hw, wiphy);
#else
//    printk("%s, not support VHT, skip bl_set_vht_capa\n", __func__);
#endif
    /* Set HE capabilities */
    bl_set_he_capa(bl_hw, wiphy);

    /* Set VHT capabilities */
    bl_set_vht_capa_2g(bl_hw, wiphy);

    /* Set HT capabilities */
    bl_set_ht_capa(bl_hw, wiphy);

    return 0;
}

void bl_custregd(struct bl_hw *bl_hw, struct wiphy *wiphy)
{
// For older kernel version, the custom regulatory is applied before the wiphy
// registration (in bl_set_wiphy_params()), so nothing has to be done here
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
    if (!bl_hw->mod_params->custregd)
        return;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
    wiphy->regulatory_flags |= REGULATORY_IGNORE_STALE_KICKOFF;
#endif
    wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED;

    rtnl_lock();
    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
    if (regulatory_set_wiphy_regd_sync(wiphy, &bl_regdom))
    #else
    if (regulatory_set_wiphy_regd_sync_rtnl(wiphy, &bl_regdom))
    #endif
        wiphy_err(wiphy, "Failed to set custom regdomain\n");
    else
        wiphy_err(wiphy,"\n"
                  "*******************************************************\n"
                  "** CAUTION: USING PERMISSIVE CUSTOM REGULATORY RULES **\n"
                  "*******************************************************\n");
     rtnl_unlock();
#endif
}

void bl_adjust_amsdu_maxnb(struct bl_hw *bl_hw)
{
    if (bl_hw->mod_params->amsdu_maxnb > NX_TX_PAYLOAD_MAX)
        bl_hw->mod_params->amsdu_maxnb = NX_TX_PAYLOAD_MAX;
    else if (bl_hw->mod_params->amsdu_maxnb == 0)
        bl_hw->mod_params->amsdu_maxnb = 1;
}
