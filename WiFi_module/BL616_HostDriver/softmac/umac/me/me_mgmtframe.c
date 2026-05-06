#include "bl_utils.h"

#include "co_endian.h"
#include "co_utils.h"

#include "ke_env.h"
#include "hal_mac_desc.h"
#include "me_mgmtframe.h"
#include "mac_frame.h"
#include "mac_ie.h"
#include "mac.h"
#include "me.h"
#include "sm_task.h"
#include "ps.h"
#include "me_utils.h"
#include "sta_mgmt.h"

#define ME_DEFAULT_LISTEN_INTERVAL   1

/*
 * Information Element Management
 ****************************************************************************************
 */

uint32_t me_add_ie_ssid(PTR2UINT *frame_addr, uint8_t ssid_len, uint8_t *ssid)
{
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + ssid_len;

    // Tag Number - SSID
    co_write8p(*frame_addr + MAC_INFOELT_ID_OFT, MAC_ELTID_SSID);
    // Tag Length
    co_write8p(*frame_addr + MAC_INFOELT_LEN_OFT, ssid_len);
          
    if (ssid_len != 0)
    {
        // Copy the SSID
        co_pack8p(*frame_addr + MAC_INFOELT_INFO_OFT, (uint8_t *)ssid, ssid_len);
    }

    // Update the position in the frame
    *frame_addr += ie_len;

    // And the written length
    return (ie_len);
}

uint32_t me_add_ie_supp_rates(PTR2UINT *frame_addr,
                                       struct mac_rateset *rateset)
{
    // Length for Supported Rates - Only up to 8 rates can be added in the supported rate element
    uint32_t supp_len = co_min(rateset->length, MAC_RATES_ELMT_MAX_LEN);
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + supp_len;

    // Tag Number - Supported Rates
    co_write8p(*frame_addr + MAC_INFOELT_ID_OFT, MAC_ELTID_RATES);
    // Tag Length
    co_write8p(*frame_addr + MAC_INFOELT_LEN_OFT, supp_len);

    // Copy the rates
    co_pack8p(*frame_addr + MAC_INFOELT_INFO_OFT, rateset->array, supp_len);

    // Update the frame pointer
    *frame_addr += ie_len;

    // And the written length
    return (ie_len);
}

uint32_t me_add_ie_ext_supp_rates(PTR2UINT *frame_addr, 
                                            struct mac_rateset *rateset)
{
    // Length for Extended Supported Rates
    uint32_t ext_supp_len = rateset->length - MAC_RATES_ELMT_MAX_LEN;
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + ext_supp_len;

    // Tag Number - Extended Supported Rates
    co_write8p(*frame_addr + MAC_INFOELT_ID_OFT, MAC_ELTID_EXT_RATES);
    // Tag Length
    co_write8p(*frame_addr + MAC_INFOELT_LEN_OFT, ext_supp_len);

    // Copy the rates
    co_pack8p(*frame_addr + MAC_INFOELT_INFO_OFT, &rateset->array[MAC_RATES_ELMT_MAX_LEN],
              ext_supp_len);

    // Update the frame pointer
    *frame_addr += ie_len;

    // And the written length
    return (ie_len);
}

#if (RW_MESH_EN)
uint32_t me_add_ie_tim(uint32_t *frame_addr, uint8_t dtim_period)
{
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + 4;

    // Tag Number - TIM
    co_write8p(*frame_addr + MAC_INFOELT_ID_OFT, MAC_ELTID_TIM);
    // Tag Length
    co_write8p(*frame_addr + MAC_INFOELT_LEN_OFT, 4);

    // Write DTIM Period
    co_write8p(*frame_addr + MAC_TIM_PERIOD_OFT, dtim_period);
    // Other parameters will be updated in mm_bcn module before beacon transmission

    // Update the position in the frame
    *frame_addr += ie_len;

    // And the written length
    return (ie_len);
}

uint32_t me_add_ie_dsss_param(uint32_t *frame_addr, uint8_t chan)
{
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + 1;

    // Tag Number - DS Parameter Set
    co_write8p(*frame_addr + MAC_INFOELT_ID_OFT, MAC_ELTID_DS);
    // Tag Length
    co_write8p(*frame_addr + MAC_INFOELT_LEN_OFT, 1);

    // Copy the Channel
    co_write8p(*frame_addr + MAC_INFOELT_INFO_OFT, chan);

    // Update the position in the frame
    *frame_addr += ie_len;

    // And return the written length
    return (ie_len);
}
#endif //(RW_MESH_EN)

uint32_t me_add_ie_ht_capa(PTR2UINT *frame_addr)
{
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + MAC_HT_CAPA_ELMT_LEN;
    // Get Local HT Capabilities
    struct mac_htcapability *ht_cap = &me_env.ht_cap;
    uint16_t ht_capa_info = ht_cap->ht_capa_info & ~MAC_HTCAPA_SMPS_MSK;

    // Tag Number - HT Capabilities
    co_write8p(*frame_addr + MAC_HT_CAPA_ID_OFT, MAC_ELTID_HT_CAPA);
    // Tag Length
    co_write8p(*frame_addr + MAC_HT_CAPA_LEN_OFT, MAC_HT_CAPA_ELMT_LEN);

    ht_capa_info |= MAC_HTCAPA_SMPS_DISABLE;
    co_write16p(*frame_addr + MAC_HT_CAPA_INFO_OFT, co_htows(ht_capa_info));
    co_write8p(*frame_addr + MAC_HT_CAPA_AMPDU_PARAM_OFT, ht_cap->a_mpdu_param);
    co_pack8p(*frame_addr + MAC_HT_CAPA_SUPPORTED_MCS_SET_OFT, ht_cap->mcs_rate,
              MAX_MCS_LEN);
    co_write16p(*frame_addr + MAC_HT_CAPA_EXTENDED_CAPA_OFT,
                co_htows(ht_cap->ht_extended_capa));
    co_pack8p(*frame_addr + MAC_HT_CAPA_TX_BEAM_FORMING_CAPA_OFT,
              (uint8_t *)&ht_cap->tx_beamforming_capa, 4);
    co_write8p(*frame_addr + MAC_HT_CAPA_ASEL_CAPA_OFT, ht_cap->asel_capa);

    *frame_addr += ie_len;

    // And return the written length
    return (ie_len);
}

#if (RW_MESH_EN)
uint32_t me_add_ie_ht_oper(uint32_t *frame_addr, struct softmac_vif_info_tag *vif)
{
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + MAC_HT_OPER_ELMT_LEN;
    // Get Channel Context used by the VIF
    struct chan_ctxt_tag *chan_ctxt = vif->chan_ctxt;
    // HT Information Subset (1 of 3)
    uint8_t ht_info1 = 0x00;

    // Tag Number - HT Information
    co_write8p(*frame_addr + MAC_HT_OPER_ID_OFT, MAC_ELTID_HT_OPERATION);
    // Tag Length
    co_write8p(*frame_addr + MAC_HT_OPER_LEN_OFT, MAC_HT_OPER_ELMT_LEN);

    co_write8p(*frame_addr + MAC_HT_OPER_PRIM_CH_OFT,
               (uint8_t)phy_freq_to_channel(chan_ctxt->channel.band,
                                            chan_ctxt->channel.prim20_freq));

    if (chan_ctxt->channel.type > BW_20MHZ)
    {
        // Allow use of any channel width
        ht_info1 |= 0x04;

        if (chan_ctxt->channel.prim20_freq > chan_ctxt->channel.center1_freq)
        {
            // Secondary channel is below the primary channel
            ht_info1 |= 0x03;
        }
        else
        {
            // Secondary channel is above the primary channel
            ht_info1 |= 0x01;
        }
    }

    co_write8p(*frame_addr + MAC_HT_OPER_INFO_OFT, ht_info1);
    co_write16p(*frame_addr + MAC_HT_OPER_INFO_SUBSET2_OFT, MAC_HT_OPER_OP_MODE_MASK);
    co_write16p(*frame_addr + MAC_HT_OPER_INFO_SUBSET3_OFT, 0);
    co_write8p(*frame_addr + MAC_HT_OPER_BASIC_MSC_SET_OFT, 0xFF);

    *frame_addr += ie_len;

    // And return the written length
    return (ie_len);
}
#endif //(RW_MESH_EN)

#if NX_HE
uint32_t me_add_ie_he_capa(PTR2UINT *frame_addr)
{
    // Element length
    uint32_t ie_len;
    // Get Local VHT Capabilities
    struct mac_hecapability *he_cap = &me_env.he_cap;
    int i;
    uint8_t chan_width_set;
    PTR2UINT copy_addr;

    // JOCHNE: modify mac cap
    he_cap->mac_cap_info[0] |= 0x1; // set +HTC HE Support
    he_cap->mac_cap_info[2] &= ~0x2; // clr All Ack Support

    // Tag Number - Extended element ID
    co_write8p(*frame_addr + MAC_HE_CAPA_ID_OFT, MAC_ELTID_EXT);
    // Extension element - HE capa
    co_write8p(*frame_addr + MAC_HE_CAPA_EXT_ID_OFT, MAC_ELTID_EXT_HE_CAPA);

    // Add MAC and PHY capabilities
    copy_addr = *frame_addr + MAC_HE_MAC_CAPA_INFO_OFT;
    for (i = 0; i < MAC_HE_CAPA_IE_MAC_CAPA_LEN; i++)
    {
        co_write8p(copy_addr++, he_cap->mac_cap_info[i]);
    }
    // BL616 debug test: gain more time for SW to process Trigger frame like BPSR
    he_cap->mac_cap_info[1] |= 0x8;

    // BL616 beamformee on/off control for debug
    #if RW_BFMEE_EN == 0
    he_cap->phy_cap_info[4] &= (~(1 << 0));
    #endif

    // JOCHEN: modify phy cap (clr doppler and triggered bfmee)
    he_cap->phy_cap_info[1] &= ~(1 << 7);
    he_cap->phy_cap_info[2] &= ~(1 << 0);
    he_cap->phy_cap_info[2] &= ~(1 << 5);
    he_cap->phy_cap_info[6] = 0x43;

    for (i = 0; i < MAC_HE_CAPA_IE_PHY_CAPA_LEN; i++)
    {
        co_write8p(copy_addr++, he_cap->phy_cap_info[i]);
    }

    // Add supported MCS
    chan_width_set = HE_PHY_CAPA_VAL_GET(he_cap, CHAN_WIDTH_SET);
    co_write16p(copy_addr, co_htows(he_cap->mcs_supp.rx_mcs_80));
    copy_addr += 2;
    co_write16p(copy_addr, co_htows(he_cap->mcs_supp.tx_mcs_80));
    copy_addr += 2;

    // Add PPE thresholds if present
    if (HE_PHY_CAPA_BIT_IS_SET(he_cap, PPE_THRESHOLD_PRESENT))
    {
        uint8_t local_supp_nss = me_11ac_nss_max(me_env.he_cap.mcs_supp.tx_mcs_80);
        uint8_t length = CO_ALIGNx_HI(7 + 6 * (local_supp_nss + 1) * (me_env.phy_bw_max + 1), 8) / 8;
        
        for (i = 0; i < length; i++)
        {
            co_write8p(copy_addr++, he_cap->ppe_thres[i]);
        }
    }

    ie_len = copy_addr - *frame_addr;
    // Tag Length
    co_write8p(*frame_addr + MAC_HE_CAPA_LEN_OFT, ie_len - 2);
    *frame_addr += ie_len;

    //Clear DCM capability since ACK issue observed on AX9000
    HE_PHY_CAPA_VAL_SET(he_cap, DCM_MAX_CONST_RX, NO_DCM);

    // And return the written length
    return (ie_len);
}
#endif

#if (NX_VHT)
uint32_t me_add_ie_vht_capa(PTR2UINT *frame_addr)
{
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + MAC_VHT_CAPA_ELMT_LEN;
    // Get Local VHT Capabilities
    struct mac_vhtcapability *vht_cap = &me_env.vht_cap;

	vht_cap->vht_capa_info &= ~MAC_VHTCAPA_SU_BEAMFORMEE_CAPABLE;
	vht_cap->vht_capa_info &= ~MAC_VHTCAPA_MU_BEAMFORMEE_CAPABLE;

    // Tag Number - VHT Capabilities
    co_write8p(*frame_addr + MAC_VHT_CAPA_ID_OFT, MAC_ELTID_VHT_CAPA);
    // Tag Length
    co_write8p(*frame_addr + MAC_VHT_CAPA_LEN_OFT, MAC_VHT_CAPA_ELMT_LEN);

    co_write32p(*frame_addr + MAC_VHT_CAPA_INFO_OFT, co_htowl(vht_cap->vht_capa_info));
    co_write16p(*frame_addr + MAC_VHT_RX_MCS_MAP_OFT, co_htows(vht_cap->rx_mcs_map));
    co_write16p(*frame_addr + MAC_VHT_RX_HIGHEST_RATE_OFT, co_htows(vht_cap->rx_highest));
    co_write16p(*frame_addr + MAC_VHT_TX_MCS_MAP_OFT, co_htows(vht_cap->tx_mcs_map));
    co_write16p(*frame_addr + MAC_VHT_TX_HIGHEST_RATE_OFT, co_htows(vht_cap->tx_highest));

    *frame_addr += ie_len;

    // And return the written length
    return (ie_len);
}

#if (RW_MESH_EN)
uint32_t me_add_ie_vht_oper(uint32_t *frame_addr, struct softmac_vif_info_tag *vif)
{
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + MAC_VHT_OPER_ELMT_LEN;
    // Get Channel Context used by the VIF
    struct chan_ctxt_tag *chan_ctxt = vif->chan_ctxt;

    // Tag Number - VHT Operation
    co_write8p(*frame_addr + MAC_VHT_OPER_ID_OFT, MAC_ELTID_VHT_OPERATION);
    // Tag Length
    co_write8p(*frame_addr + MAC_VHT_OPER_LEN_OFT, MAC_VHT_OPER_ELMT_LEN);

    if (chan_ctxt->channel.type == BW_80MHZ)
    {
        co_write8p(*frame_addr + MAC_VHT_CHAN_WIDTH_OFT, 1);
        co_write8p(*frame_addr + MAC_VHT_CENTER_FREQ0_OFT,
                (uint8_t)phy_freq_to_channel(chan_ctxt->channel.band,
                                             chan_ctxt->channel.center1_freq));
    }
    else
    {
        co_write8p(*frame_addr + MAC_VHT_CHAN_WIDTH_OFT, 0);
        co_write8p(*frame_addr + MAC_VHT_CENTER_FREQ0_OFT, 0);
    }

    co_write8p(*frame_addr + MAC_VHT_CENTER_FREQ1_OFT, 0);
    co_write16p(*frame_addr + MAC_VHT_BASIC_MCS_OFT, 0);

    *frame_addr += ie_len;

    // And return the written length
    return (ie_len);
}
#endif //(RW_MESH_EN)

uint32_t me_add_ie_op_mode(PTR2UINT *frame_addr, uint8_t nss, uint8_t bw)
{
    // Element length
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + MAC_OP_MODE_NOTIF_PARAM_LEN;
    uint8_t op_mode = (bw << MAC_OPMODE_BW_OFT) | (nss << MAC_OPMODE_RXNSS_OFT);

    // Tag Number - VHT Capabilities
    co_write8p(*frame_addr + MAC_INFOELT_ID_OFT, MAC_ELTID_OP_MODE_NOTIF);
    // Tag Length
    co_write8p(*frame_addr + MAC_INFOELT_LEN_OFT, MAC_OP_MODE_NOTIF_PARAM_LEN);
    // Parameter
    co_write8p(*frame_addr + MAC_INFOELT_INFO_OFT, op_mode);

    *frame_addr += ie_len;

    // And return the written length
    return (ie_len);
}
#endif //(NX_VHT)

uint32_t me_add_ie_rm_enabled_capa(PTR2UINT *frame_addr)
{
    #if NX_RM
    uint32_t ie_len = MAC_INFOELT_INFO_OFT + MAC_RM_EN_CAPA_LEN;

    co_write8p(*frame_addr + MAC_INFOELT_ID_OFT, MAC_ELTID_RM_ENABLED_CAPA);
    co_write8p(*frame_addr + MAC_INFOELT_LEN_OFT, MAC_RM_EN_CAPA_LEN);
    co_write8p(*frame_addr + MAC_INFOELT_INFO_OFT, (MAC_RM_CAPA_BYTE_0_BEACON_TABLE |
                                                    MAC_RM_CAPA_BYTE_0_BEACON_PASSIVE |
                                                    MAC_RM_CAPA_BYTE_0_BEACON_ACTIVE));
    co_write8p(*frame_addr + MAC_INFOELT_INFO_OFT + 1, 0);
    co_write8p(*frame_addr + MAC_INFOELT_INFO_OFT + 2, 0);
    co_write8p(*frame_addr + MAC_INFOELT_INFO_OFT + 3, 0);
    co_write8p(*frame_addr + MAC_INFOELT_INFO_OFT + 4, 0);

    *frame_addr += ie_len;
    
    return ie_len;
    #else
    return 0;
    #endif // NX_RM
}

uint16_t me_build_authenticate(PTR2UINT frame, uint16_t algo_type,
                                      uint16_t seq_nbr, uint16_t status_code,
                                      uint32_t *challenge_array_ptr)
{

    uint16_t mac_frame_len;

    co_write16p(frame + MAC_AUTH_ALGONBR_OFT, co_htows(algo_type));
    co_write16p(frame + MAC_AUTH_SEQNBR_OFT, co_htows(seq_nbr));
    co_write16p(frame + MAC_AUTH_STATUS_OFT, co_htows(status_code));
    mac_frame_len = MAC_AUTH_CHALLENGE_OFT;

    if (challenge_array_ptr != NULL)
    {
        frame += MAC_AUTH_CHALLENGE_OFT;
        co_write8p(frame + MAC_CHALLENGE_ID_OFT, MAC_ELTID_CHALLENGE);
        co_write8p(frame + MAC_CHALLENGE_LEN_OFT, MAC_AUTH_CHALLENGE_LEN);
        co_copy8p(frame + MAC_CHALLENGE_TEXT_OFT, CPU2HW(challenge_array_ptr),
                  MAC_AUTH_CHALLENGE_LEN);
        mac_frame_len += CHALLENGE_TEXT_SIZE;
    }
    return mac_frame_len;
}

uint16_t me_build_deauthenticate(PTR2UINT frame, uint16_t reason_code)
{
    uint16_t mac_frame_len;

    co_write16p(frame + MAC_DISASSOC_REASON_OFT, co_htows(reason_code));
    mac_frame_len = MAC_DISASSOC_REASON_LEN;

    return(mac_frame_len);
}

uint16_t me_build_associate_req(PTR2UINT frame, struct me_bss_info *bss,
                                        struct mac_addr *old_ap_addr_ptr,
                                        uint8_t vif_idx, PTR2UINT *ie_addr,
                                        uint16_t *ie_len,
                                        struct sm_connect_req const *con_par)
{
    uint16_t mac_frame_len;
    uint16_t capainfo = 0;
    PTR2UINT add_ie_addr = CPU2HW(con_par->ie_buf);
    uint16_t add_ie_len = con_par->ie_len;
    uint16_t listen;
    #if NX_UAPSD
    uint8_t uapsd_info = con_par->uapsd_queues;
    #endif
    //PTR2UINT org_frame = frame;

    dbg("%s\r\n", __func__);

    // Set the listen interval based on the parameter from the host
    listen = con_par->listen_interval?con_par->listen_interval:ME_DEFAULT_LISTEN_INTERVAL;

    // build the capability information
    capainfo = me_build_capability(vif_idx);

    // build the frame body
    co_write16p(frame + MAC_ASSO_REQ_CAPA_OFT, co_htows(capainfo));
    co_write16p(frame + MAC_ASSO_REQ_LISTEN_OFT, co_htows(listen));

    if (old_ap_addr_ptr)
    {
        // Add current AP if this is a re-association request
        MAC_ADDR_CPY(HW2CPU(frame + MAC_REASSO_REQ_AP_ADDR_OFT), old_ap_addr_ptr);

        // update frame length
        mac_frame_len = MAC_REASSO_REQ_SSID_OFT;
    }
    else
    {
        // update frame length
        mac_frame_len = MAC_ASSO_REQ_SSID_OFT;
    }

    frame += mac_frame_len;
    *ie_addr = frame;
    // Update the SSID
    mac_frame_len += me_add_ie_ssid(&frame, bss->ssid.length, 
                                   (uint8_t *)&bss->ssid.array);
    // Add supported rates
    mac_frame_len += me_add_ie_supp_rates(&frame, &bss->rate_set);
    if (bss->rate_set.length > MAC_RATES_ELMT_MAX_LEN)
    {
        mac_frame_len += me_add_ie_ext_supp_rates(&frame, &bss->rate_set);
    }

    if (capainfo & MAC_CAPA_SPECTRUM)
    {
        int8_t min, max;

        // Add Power capability
        co_write8p(frame++, MAC_ELTID_POWER_CAPABILITY);
        co_write8p(frame++, MAC_POWER_CAPABILITY_IE_LEN);
        phy_get_rf_gain_capab(&max, &min);
        if (max > bss->chan.tx_power)
            max = bss->chan.tx_power;

        co_write8p(frame++, (uint8_t)min);
        co_write8p(frame++, (uint8_t)max);
        mac_frame_len += MAC_POWER_CAPABILITY_IE_LEN + 2;
    }

    if (capainfo & MAC_CAPA_SPECTRUM)
    {
        uint8_t start, prev, i, nb, ch_incr, ch_idx, len, cnt = 0;
        struct mac_chan_def *chan = NULL;
        uint32_t tmp;

        // Add Supported Channels
        co_write8p(frame++, MAC_ELTID_SUPPORTED_CHANNELS);
        tmp = frame++; // skip len
        if (bss->chan.band == PHY_BAND_2G4) {
            ch_incr = 1;
            chan = me_env.chan.chan2G4;
            cnt = me_env.chan.chan2G4_cnt;
        }
#if CFG_5G
        else
        {
            ch_incr = 4;
            chan = me_env.chan.chan5G;
            cnt =  me_env.chan.chan5G_cnt;
        }
#endif

        nb = len = start = prev = 0;
        for (i = 0; i < cnt; i++)
        {
            if (chan->flags & CHAN_DISABLED)
                continue;

            ch_idx = phy_freq_to_channel(chan->band, chan->freq);
            if (nb && (ch_idx - prev) != ch_incr)
            {
                co_write8p(frame++, start);
                co_write8p(frame++, nb);
                nb++;
                len += 2;
            }
            else if (!nb)
            {
                start = ch_idx;
            }

            prev = ch_idx;
            nb++;
            chan++;
        }
        co_write8p(frame++, start);
        co_write8p(frame++, nb);
        len += 2;
        co_write8p(tmp, len);
        mac_frame_len += len + 2;
    }

    mac_frame_len += me_add_ie_rm_enabled_capa(&frame);

    // Copy the additional information elements
    co_copy8p(frame, add_ie_addr, add_ie_len);
    mac_frame_len += add_ie_len;
    frame += add_ie_len;

    if (BSS_CAPA(bss, QOS))
    {
        uint8_t wme_ie[MAC_WME_PARAM_LEN] =  MAC_RAW_WME_INFO_ELMT_DEFAULT;

        #if NX_UAPSD
        // adjust the U-APSD flags as requested by the QSTA
        if (ps_uapsd_enabled() &&
            (bss->edca_param.qos_info & MAC_QOS_INFO_AP_UAPSD_ENABLED))
        {
            wme_ie[8] = uapsd_info;
        }
        else
        #endif
        {
            wme_ie[8] = 0;
        }
        co_pack8p(frame, (uint8_t *)&wme_ie,
                  wme_ie[MAC_INFOELT_LEN_OFT] + MAC_INFOELT_INFO_OFT);

        // update the frame pointer
        mac_frame_len += wme_ie[MAC_INFOELT_LEN_OFT] + MAC_INFOELT_INFO_OFT;
        frame += wme_ie[MAC_INFOELT_LEN_OFT] + MAC_INFOELT_INFO_OFT;
    }

    // build the HT capabilities field
    if (BSS_CAPA(bss, HT) && LOCAL_CAPA(HT))
    {
        mac_frame_len += me_add_ie_ht_capa(&frame);
    }

    #if 1 
    // if rsn select psk , adn associate req also added mdie, some AP may confuse whether to choose FT OR psk
    // review 11r code, and fix FT-PSK later
    // add Mobility Domain field
    if ((sm_env.reassoc == 0) && sm_env.connect_param && 
         sm_env.connect_param->auth_type == MAC_AUTH_ALGO_FT)
    {
        struct mobility_domain *mde = &bss->mde;
        if (mde->mdid != 0)
        {
            co_write8p(frame + MAC_INFOELT_MDE_ID_OFT, MAC_ELTID_MDE);
            co_write8p(frame + MAC_INFOELT_MDE_LEN_OFT, MAC_INFOELT_MDE_ELMT_LEN);

            co_write16p(frame + MAC_INFOELT_MDE_MDID_OFT, co_htows(mde->mdid));
            co_write8p(frame + MAC_INFOELT_MDE_FT_CAPA_POL_OFT, mde->ft_capability_policy);

            mac_frame_len += MAC_INFOELT_MDE_LEN;
            frame += MAC_INFOELT_MDE_LEN;
        }
    }
    #endif

    #if NX_VHT
    // build the VHT capabilities field
    if (BSS_CAPA(bss, VHT) && LOCAL_CAPA(VHT))
    {
        mac_frame_len += me_add_ie_vht_capa(&frame);
        mac_frame_len += me_add_ie_op_mode(&frame, me_11ac_nss_max(me_env.vht_cap.tx_mcs_map),
                                           me_phy2mac_bw(me_env.phy_bw_max));
    }
    #endif //(NX_VHT)

    #if NX_HE
    // build the HE capabilities field
    if (BSS_CAPA(bss, HE) && LOCAL_CAPA(HE))
    {
        mac_frame_len += me_add_ie_he_capa(&frame);
    }
    #endif //(NX_HE)

    *ie_len = frame - *ie_addr;

    //bl_dump((uint8_t *)org_frame, mac_frame_len);

    dbg("%s end\r\n", __func__);
    
    return mac_frame_len;
}

void me_extract_rate_set(PTR2UINT buffer, uint16_t buflen,
                               struct mac_rateset* mac_rate_set_ptr)
{
    PTR2UINT addr;
    uint8_t elmt_length, rate_cnt = 0;
    int i;

    do
    {
        // Search the rate set IE in the frame
        addr = mac_ie_rates_find(buffer, buflen, &elmt_length);
        if (addr == 0)
            break;

        // Copy the rate set from the frame
        addr += MAC_RATES_RATES_OFT;
        for (i = 0; i < elmt_length; i++)
        {
            uint8_t rate = co_read8p(addr + i);

            // Discard BSS Membership Selectors if any
            if (((rate & ~MAC_BASIC_RATE) == MAC_BSS_MEMBERSHIP_HT_PHY) ||
                ((rate & ~MAC_BASIC_RATE) == MAC_BSS_MEMBERSHIP_VHT_PHY))
                continue;

            mac_rate_set_ptr->array[rate_cnt++] = rate;
        }

        // Search for extended rates if any
        addr = mac_ie_ext_rates_find(buffer, buflen, &elmt_length);
        if(addr == 0)
            break;

        // Add the extended rates to the rate array
        addr += MAC_RATES_RATES_OFT;
        for (i = 0; i < elmt_length; i++)
        {
            uint8_t rate = co_read8p(addr + i);

            // Discard BSS Membership Selectors if any
            if (((rate & ~MAC_BASIC_RATE) == MAC_BSS_MEMBERSHIP_HT_PHY) ||
                ((rate & ~MAC_BASIC_RATE) == MAC_BSS_MEMBERSHIP_VHT_PHY))
                continue;

            mac_rate_set_ptr->array[rate_cnt++] = rate;

            // Stop looping if our rate array is full
            if (rate_cnt >= MAC_RATESET_LEN)
                break;
        }
    } while(0);

    // Save the rate count
    mac_rate_set_ptr->length = rate_cnt;
}

void me_extract_power_constraint(PTR2UINT buffer, uint16_t buflen,
                                           struct me_bss_info *bss)
{
   PTR2UINT addr = mac_ie_power_constraint_find(buffer, buflen);

   if (addr == 0)
   {
       bss->power_constraint = 0;
       return;
   }

   bss->power_constraint = co_read8p(addr + MAC_INFOELT_POWER_CONSTRAINT_OFT);
}

void me_extract_country_reg(PTR2UINT buffer, uint16_t buflen,
                                     struct me_bss_info *bss)
{
    PTR2UINT addr;
    uint8_t bss_ch_idx, ch_incr, ch_idx, ch_nb, i;
    uint8_t elmt_length, oft;

    addr = mac_ie_country_find(buffer, buflen, &elmt_length);

    if (addr == 0)
        return;

    /* Only update tx power of the bss channel (required to set power
       capability in (re)assoc resp). Other channels will be updated by driver */
#if CFG_5G
    if (bss->chan.band == PHY_BAND_2G4)
        ch_incr = 1;
    else
        ch_incr = 4;
#else
        ch_incr = 1;
#endif

    bss_ch_idx = phy_freq_to_channel(bss->chan.band, bss->chan.prim20_freq);
    elmt_length += MAC_COUNTRY_STRING_OFT;
    oft = MAC_COUNTRY_STRING_OFT + MAC_COUNTRY_STRING_LEN;
    while ((oft + MAC_COUNTRY_TRIPLET_LEN) <= elmt_length)
    {
        ch_idx = co_read8p(addr + oft + MAC_COUNTRY_FIRST_CHAN_OFT);
        ch_nb  = co_read8p(addr + oft + MAC_COUNTRY_NB_CHAN_OFT);

        for (i = 0 ; i < ch_nb; i++)
        {
            if (bss_ch_idx == ch_idx)
            {
                bss->chan.tx_power = (int8_t)co_read8p(addr + oft + MAC_COUNTRY_PWR_LEVEL_OFT);
                return;
            }
            ch_idx += ch_incr;
        }

        oft += MAC_COUNTRY_TRIPLET_LEN;
    }
}

void me_extract_mobility_domain(PTR2UINT buffer, uint16_t buflen, 
                                          struct me_bss_info *bss)
{
    PTR2UINT elmt_ptr;
    uint16_t len;
    
    elmt_ptr = mac_ie_find(buffer, buflen, MAC_ELTID_MDE, &len);

    if (elmt_ptr == 0)
    {
        bss->mde.mdid = 0;
        bss->mde.ft_capability_policy = 0;
        return;
    }

    bss->mde.mdid = co_read16p((void *)(elmt_ptr + MAC_INFOELT_MDE_MDID_OFT));
    bss->mde.ft_capability_policy = co_read8p(elmt_ptr + MAC_INFOELT_MDE_FT_CAPA_POL_OFT);
}

bool me_extract_edca_params(PTR2UINT buffer, uint16_t buflen,
                                      struct mac_edca_param_set *edca_param,
                                      bool *changed)
{
    PTR2UINT addr = mac_ie_wmm_param_find(buffer, buflen);
    uint32_t edca_temp;
    uint8_t qos_info;

    *changed = false;
    if (addr == 0)
    {
        // AP is not WMM compatible - Put default EDCA values (all BE)
        edca_param->ac_param[AC_BK] = NXMAC_EDCA_AC_1_RESET;
        edca_param->ac_param[AC_BE] = NXMAC_EDCA_AC_1_RESET;
        edca_param->ac_param[AC_VI] = NXMAC_EDCA_AC_1_RESET;
        edca_param->ac_param[AC_VO] = NXMAC_EDCA_AC_1_RESET;
        return false;
    }

    // First extract the QoS Info to get the parameter set counter
    qos_info = co_read8p(addr + MAC_WMM_PARAM_QOS_INFO_OFT);

    // Check if the parameters changed since last time we got them
    if ((qos_info & MAC_WMM_PARAM_SET_CNT_MSK) == edca_param->param_set_cnt)
        return true;

    // The parameters are advertised as different by the AP, so read completely the
    // element
    *changed = true;

    edca_param->qos_info = qos_info;
    edca_param->param_set_cnt = qos_info & MAC_WMM_PARAM_SET_CNT_MSK;

    // EDCA Parameter Read from frame is 0xTTTTCCAA
    // TTTT = TXOP
    // CC = Contention Window Parameters
    // AA = AIFSN Parameter.
    // This has to re-arranged as 0xAACCTTTT to as per spec. for EDCA Param
    // So to extract and display in report will be correct
    edca_temp = co_htowl(co_read32p((void *)(addr + MAC_WMM_PARAM_BE_PARAM_OFT)));
    // ACM bit information
    edca_param->acm = ((edca_temp & CO_BIT(4)) != 0) << AC_BE;
    // AIFSN BYTE alignment
    edca_param->ac_param[AC_BE] = edca_temp & 0x0F;
    // CW BYTE alignment
    edca_param->ac_param[AC_BE] |= ((edca_temp >> 8) & 0xFFFFFFF) << 4;

    edca_temp = co_htowl(co_read32p((void *)(addr + MAC_WMM_PARAM_BK_PARAM_OFT)));
    // ACM bit information
    edca_param->acm |= ((edca_temp & CO_BIT(4)) != 0) << AC_BK;
    // AIFSN BYTE alignment
    edca_param->ac_param[AC_BK] = edca_temp & 0x0F;
    // CW BYTE alignment.
    edca_param->ac_param[AC_BK] |= ((edca_temp >> 8) & 0xFFFFFFF) << 4;

    edca_temp = co_htowl(co_read32p((void *)(addr + MAC_WMM_PARAM_VI_PARAM_OFT)));
    // ACM bit information
    edca_param->acm |= ((edca_temp & CO_BIT(4)) != 0) << AC_VI;
    // AIFSN BYTE alignment
    edca_param->ac_param[AC_VI] = edca_temp & 0x0F;
    // CW BYTE alignment
    edca_param->ac_param[AC_VI] |= ((edca_temp >> 8) & 0xFFFFFFF) << 4;

    edca_temp = co_htowl(co_read32p((void *)(addr + MAC_WMM_PARAM_VO_PARAM_OFT)));
    // ACM bit information
    edca_param->acm |= ((edca_temp & CO_BIT(4)) != 0) << AC_VO;
    // AIFSN BYTE alignment
    edca_param->ac_param[AC_VO] = edca_temp & 0x0F;
    // CW BYTE alignment
    edca_param->ac_param[AC_VO] |= ((edca_temp >> 8) & 0xFFFFFFF) << 4;

    return true;
}

bool me_extract_ht_capa(PTR2UINT buffer, uint16_t buflen,
                               struct mac_htcapability *ht_cap)
{
    PTR2UINT addr = mac_ie_ht_capa_find(buffer, buflen);

    if (addr == 0)
        return false;

    ht_cap->ht_capa_info = co_wtohs(co_read16p((void *)(addr + MAC_HT_CAPA_INFO_OFT)));
    ht_cap->a_mpdu_param = co_read8p(addr + MAC_HT_CAPA_AMPDU_PARAM_OFT);
    co_unpack8p(ht_cap->mcs_rate, addr + MAC_HT_CAPA_SUPPORTED_MCS_SET_OFT, MAX_MCS_LEN);
    ht_cap->ht_extended_capa = co_wtohs(co_read16p((void *)(addr + MAC_HT_CAPA_EXTENDED_CAPA_OFT)));
    ht_cap->tx_beamforming_capa =
                   co_wtohs(co_read16p((void *)(addr + MAC_HT_CAPA_TX_BEAM_FORMING_CAPA_OFT)));
    ht_cap->asel_capa = co_read8p(addr + MAC_HT_CAPA_ASEL_CAPA_OFT);

    return true;
}

bool me_extract_vht_capa(PTR2UINT buffer, uint16_t buflen, 
                                 struct mac_vhtcapability *vht_cap)
{
    PTR2UINT addr = mac_ie_vht_capa_find(buffer, buflen);

    if (addr == 0)
        return false;

    vht_cap->vht_capa_info = co_wtohl(co_read32p((void *)(addr + MAC_VHT_CAPA_INFO_OFT)));
    vht_cap->rx_mcs_map = co_wtohs(co_read16p((void *)(addr + MAC_VHT_RX_MCS_MAP_OFT)));
    vht_cap->tx_mcs_map = co_wtohs(co_read16p((void *)(addr + MAC_VHT_TX_MCS_MAP_OFT)));
    vht_cap->rx_highest = co_wtohs(co_read16p((void *)(addr + MAC_VHT_RX_HIGHEST_RATE_OFT)));
    vht_cap->tx_highest = co_wtohs(co_read16p((void *)(addr + MAC_VHT_TX_HIGHEST_RATE_OFT)));

    return true;
}

#if NX_HE
bool me_extract_he_capa(PTR2UINT buffer, uint16_t buflen, 
                                struct mac_hecapability *he_cap)
{
    PTR2UINT copy_addr;
    PTR2UINT end_addr;
    uint8_t elmt_length;
    uint8_t chan_width_set;
    int i;
    PTR2UINT addr = mac_ie_he_capa_find(buffer, buflen, &elmt_length);

    if (addr == 0)
        return false;

    copy_addr = addr + MAC_HE_MAC_CAPA_INFO_OFT;
    end_addr = copy_addr + elmt_length;

    for (i = 0; i < MAC_HE_CAPA_IE_MAC_CAPA_LEN; i++)
    {
        he_cap->mac_cap_info[i] = co_read8p(copy_addr++);
    }
    // BL616 debug test: gain more time for SW to process Trigger frame like BPSR
    he_cap->mac_cap_info[1] |= 0x8;

    for (i = 0; i < MAC_HE_CAPA_IE_PHY_CAPA_LEN; i++)
    {
        he_cap->phy_cap_info[i] = co_read8p(copy_addr++);
    }
    chan_width_set = HE_PHY_CAPA_VAL_GET(he_cap, CHAN_WIDTH_SET);
    he_cap->mcs_supp.rx_mcs_80 = co_wtohs(co_read16p((void *)copy_addr));
    copy_addr += 2;
    he_cap->mcs_supp.tx_mcs_80 = co_wtohs(co_read16p((void *)copy_addr));
    copy_addr += 2;
    if (chan_width_set & HE_PHY_CAPA_CHAN_WIDTH_SET_160MHZ_IN_5G)
    {
        if ((copy_addr + MAC_HE_MCS_INFO_PER_BW_LEN) > end_addr)
            return false;

        he_cap->mcs_supp.rx_mcs_160 = co_wtohs(co_read16p((void *)copy_addr));
        copy_addr += 2;
        he_cap->mcs_supp.tx_mcs_160 = co_wtohs(co_read16p((void *)copy_addr));
        copy_addr += 2;
    }
    if (chan_width_set & HE_PHY_CAPA_CHAN_WIDTH_SET_80PLUS80_MHZ_IN_5G)
    {
        if ((copy_addr + MAC_HE_MCS_INFO_PER_BW_LEN) > end_addr)
            return false;

        he_cap->mcs_supp.rx_mcs_80p80 = co_wtohs(co_read16p((void *)copy_addr));
        copy_addr += 2;
        he_cap->mcs_supp.tx_mcs_80p80 = co_wtohs(co_read16p((void *)copy_addr));
        copy_addr += 2;
    }

    return true;
}

bool me_extract_he_oper(PTR2UINT buffer, uint16_t buflen,
                                struct me_bss_info *bss)
{
    PTR2UINT he_op_addr;
    uint8_t elmt_length;

    // retrieve the HE operation field
    he_op_addr = mac_ie_he_oper_find(buffer, buflen, &elmt_length);
    if (he_op_addr)
        bss->he_oper = co_read32p((void *)(he_op_addr + MAC_HE_OPER_PARAM_OFT));
    else
        bss->he_oper = MAC_HE_OPER_BSS_COLOR_DISABLED_BIT;

    return (he_op_addr != 0);
}

bool me_extract_mu_edca_params(PTR2UINT buffer, uint16_t buflen,
                                   struct mac_mu_edca_param_set *mu_edca_param,
                                   bool *changed)
{
    PTR2UINT addr = mac_ie_mu_edca_find(buffer, buflen);
    uint8_t qos_info;

    *changed = false;

    if (addr == 0)
    {
        return false;
    }

    // First extract the QoS Info to get the parameter set counter
    qos_info = co_read8p(addr + MAC_MU_EDCA_QOS_INFO_OFT);

    // Check if the parameters changed this last time we got them
    if ((qos_info & MAC_MU_EDCA_PARAM_SET_CNT_MSK) == mu_edca_param->param_set_cnt)
        return true;

    // The parameters are advertised as different by the AP, so read completely the
    // element
    *changed = true;

    mu_edca_param->qos_info = qos_info;
    mu_edca_param->param_set_cnt = qos_info & MAC_MU_EDCA_PARAM_SET_CNT_MSK;
    mu_edca_param->ac_param[AC_BE] = co_htowl(co_read24p(addr + MAC_MU_EDCA_AC_BE_OFT));
    mu_edca_param->ac_param[AC_BK] = co_htowl(co_read24p(addr + MAC_MU_EDCA_AC_BK_OFT));
    mu_edca_param->ac_param[AC_VI] = co_htowl(co_read24p(addr + MAC_MU_EDCA_AC_VI_OFT));
    mu_edca_param->ac_param[AC_VO] = co_htowl(co_read24p(addr + MAC_MU_EDCA_AC_VO_OFT));

    return true;
}

bool me_extract_uora_params(PTR2UINT buffer, uint16_t buflen, 
                                      struct mm_set_uora_req *uora_param)
{
    uint8_t ocw_range;
    PTR2UINT addr = mac_ie_uora_find(buffer, buflen);

    if (!addr)
    {
        // Use the default values of OCWmin and OCWmax
        uora_param->eocw_min = MAC_UORA_EOCW_DEFAULT_MIN;
        uora_param->eocw_max = MAC_UORA_EOCW_DEFAULT_MAX;
        return false;
    }
    // Extract OCW Range field
    ocw_range = co_read8p(addr + MAC_UORA_OCW_RANGE);

    uora_param->eocw_min = (ocw_range & MAC_UORA_EOCW_MIN_MASK) >> MAC_UORA_EOCW_MIN_OFT;
    uora_param->eocw_max = (ocw_range & MAC_UORA_EOCW_MAX_MASK) >> MAC_UORA_EOCW_MAX_OFT;

    return true;
}
#endif // NX_HE

uint8_t me_ftm_add_parameter(PTR2UINT frame, uint8_t ftm_per_burst)
{
    co_write8p(frame, MAC_ELTID_FTM_PARAMS);
    co_write8p(frame + 1, MAC_INFOELT_INFO_OFT + MAC_FTM_PARAMS_LEN);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, STATUS_INDICATION, SUCCESSFULL);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, NB_BURSTS_EXP, NO_PREFERENCE);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, BURST_DURATION, NO_PREFERENCE);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, MIN_DELTA_FTM, NO_PREFERENCE);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, PARTIAL_TSF_TIMER, NO_PREFERENCE);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, PARTIAL_TSF_TIMER_PREF, NO);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, ASAP_CAPABLE, NO);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, ASAP, NO);
    FTM_PARAMS_VAL_SET_NUM(frame + MAC_INFOELT_INFO_OFT, FTM_PER_BURST, ftm_per_burst);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, FORMAT_BANDWITH, NON_HT_20MHZ);
    FTM_PARAMS_VAL_SET(frame + MAC_INFOELT_INFO_OFT, FORMAT_BURST_PERIOD, NO_PREFERENCE);

    return (MAC_INFOELT_INFO_OFT + MAC_FTM_PARAMS_LEN);
}
