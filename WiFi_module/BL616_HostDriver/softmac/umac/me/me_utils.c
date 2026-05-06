#include "bl_ipc_compat.h"
#include "bl_lmac_msg.h"

#include "softmac.h"

#include "co_math.h"
#include "hal_mac_desc.h"

#include "me_utils.h"
#include "mac_ie.h"
#include "mac_frame.h"
#include "me.h"
#include "me_mgmtframe.h"


uint8_t me_11ac_mcs_max(uint16_t mcs_map)
{
    uint8_t mcs_max;

    switch (mcs_map & MAC_VHT_MCS_MAP_MSK)
    {
        case MAC_VHT_MCS_MAP_0_7:
            mcs_max = 7;
            break;
        case MAC_VHT_MCS_MAP_0_8:
            mcs_max = 8;
            break;
        case MAC_VHT_MCS_MAP_0_9:
            mcs_max = 9;
            break;
        default:
            mcs_max = 7;
            break;
    }

    return(mcs_max);
}

uint8_t me_11ax_mcs_max(uint16_t mcs_map)
{
    uint8_t mcs_max;

    switch (mcs_map & MAC_HE_MCS_MAP_MSK)
    {
        case MAC_HE_MCS_MAP_0_7:
            mcs_max = 7;
            break;
        case MAC_HE_MCS_MAP_0_9:
            mcs_max = 9;
            break;
        case MAC_HE_MCS_MAP_0_11:
            mcs_max = 11;
            break;
        default:
            mcs_max = 7;
            break;
    }

    return(mcs_max);
}

uint8_t me_11ac_nss_max(uint16_t mcs_map)
{
    uint8_t nss_max;

    // Go through the MCS map to check how many SS are supported
    for (nss_max = 7; nss_max > 0; nss_max--)
    {
        if (((mcs_map >> (2 * nss_max)) & MAC_VHT_MCS_MAP_MSK) != MAC_VHT_MCS_MAP_NONE)
            break;
    }

    return(nss_max);
}

uint8_t me_11n_nss_max(uint8_t *mcs_set)
{
    uint8_t nss_max;

    // Go through the MCS map to check how many SS are supported
    for (nss_max = 3; nss_max > 0; nss_max--)
    {
        if (mcs_set[nss_max] != 0)
            break;
    }

    return(nss_max);
}

void me_get_ampdu_params(struct mac_htcapability const *ht_cap,
                                    struct mac_vhtcapability const *vht_cap,
                                    struct mac_hecapability const *he_cap,
                                    uint16_t *ampdu_size_max_ht,
                                    uint32_t *ampdu_size_max_vht,
                                    uint32_t *ampdu_size_max_he,
                                    uint8_t *ampdu_spacing_min)
{
    *ampdu_size_max_ht = 0;
    *ampdu_size_max_vht = 0;
    *ampdu_size_max_he = 0;

    if (ht_cap)
    {
        #if NX_VHT
        int vht_exp = 0;
        #endif
        int ht_exp = (ht_cap->a_mpdu_param & MAC_AMPDU_LEN_EXP_MSK) >>
                                                               MAC_AMPDU_LEN_EXP_OFT;
        int min_spc = (ht_cap->a_mpdu_param & MAC_AMPDU_MIN_SPACING_MSK) >>
                                                            MAC_AMPDU_MIN_SPACING_OFT;
        *ampdu_spacing_min = (min_spc < 3)?1:(0x01 << (min_spc - 3));
        *ampdu_size_max_ht = (1 << (MAC_HT_MAX_AMPDU_FACTOR + ht_exp)) - 1;

        #if NX_VHT
        if (vht_cap)
        {
            vht_exp = (vht_cap->vht_capa_info & MAC_VHTCAPA_MAX_A_MPDU_LENGTH_EXP_MSK) >>
                                                    MAC_VHTCAPA_MAX_A_MPDU_LENGTH_EXP_OFT;
            *ampdu_size_max_vht = (1 << (MAC_HT_MAX_AMPDU_FACTOR + vht_exp)) - 1;
        }
        #endif

        #if NX_HE
        if (he_cap)
        {
            int he_exp_ext = HE_MAC_CAPA_VAL_GET(he_cap, MAX_A_AMPDU_LEN_EXP);
            if (vht_cap)
            {
                if (vht_exp == 7)
                    *ampdu_size_max_he = (1 << (MAC_HT_MAX_AMPDU_FACTOR
                                                         + 7 + he_exp_ext)) - 1;
                else
                    *ampdu_size_max_he = *ampdu_size_max_vht;
            }
            else
            {
                if (ht_exp == 3)
                    *ampdu_size_max_he = (1 << (MAC_HT_MAX_AMPDU_FACTOR
                                                         + 3 + he_exp_ext)) - 1;
                else
                    *ampdu_size_max_he = *ampdu_size_max_ht;
            }
        }
        #endif
    }
}

/**
 ****************************************************************************************
 * @brief Converts rate value present in (Extended) supported rates IE with enum used by HW
 *
 * @param[in] rate Rate to convert
 * @return enum value corresponding to @p rate
 ****************************************************************************************
 */
uint8_t me_rate_translate(uint8_t rate)
{
    uint8_t hwrate = 0;

    rate &= ~MAC_BASIC_RATE;
    switch(rate)
    {
        case MAC_RATE_1MBPS:
            hwrate = HW_RATE_1MBPS;
            break;
        case MAC_RATE_2MBPS:
            hwrate = HW_RATE_2MBPS;
            break;
        case MAC_RATE_5_5MBPS:
            hwrate = HW_RATE_5_5MBPS;
            break;
        case MAC_RATE_11MBPS:
            hwrate = HW_RATE_11MBPS;
            break;
        case MAC_RATE_48MBPS:
            hwrate = HW_RATE_48MBPS;
            break;
        case MAC_RATE_24MBPS:
            hwrate = HW_RATE_24MBPS;
            break;
        case MAC_RATE_12MBPS:
            hwrate = HW_RATE_12MBPS;
            break;
        case MAC_RATE_6MBPS:
            hwrate = HW_RATE_6MBPS;
            break;
        case MAC_RATE_54MBPS:
            hwrate = HW_RATE_54MBPS;
            break;
        case MAC_RATE_36MBPS:
            hwrate = HW_RATE_36MBPS;
            break;
        case MAC_RATE_18MBPS:
            hwrate = HW_RATE_18MBPS;
            break;
        case MAC_RATE_9MBPS:
            hwrate = HW_RATE_9MBPS;
            break;
        default:
            hwrate = 0xFF;
    }
    return(hwrate);
}

uint16_t me_legacy_rate_bitfield_build(struct mac_rateset const *rateset, 
                                                bool basic_only)
{
    int i;
    uint16_t rates = 0;

    // Build the legacy rates bitfield
    for (i = 0; i < rateset->length; i++)
    {
        int bit_pos;
        
        // If the current rate is not basic, then we go to the next one
        if (basic_only && !(rateset->array[i] & MAC_BASIC_RATE))
            continue;

        // Convert the rate into an index
        bit_pos = me_rate_translate(rateset->array[i]);

        // Check if the rate is consistent
        ASSERT_WARN(bit_pos < MAC_RATESET_LEN);

        // Set the corresponding bit in the bitfield
        if (bit_pos < MAC_RATESET_LEN)
        {
            rates |= CO_BIT(bit_pos);
        }
    }

    return (rates);
}

uint16_t me_build_capability(uint8_t vif_idx)
{
    uint16_t capa_info = 0;
    struct softmac_vif_info_tag *vif = &vif_info_tab[vif_idx];

    capa_info |= MAC_CAPA_ESS;
//    if (vif->type == VIF_AP)
//        // add ESS
//        capa_info |= MAC_CAPA_ESS;
//    else if (vif->type == VIF_IBSS)
//        // add the IBSS mode
//        capa_info |= MAC_CAPA_IBSS;

    if (vif->type == VIF_IBSS) {
        // add the IBSS mode
        capa_info &= ~MAC_CAPA_ESS;
        capa_info |= MAC_CAPA_IBSS;
    }

    // add Qos
    //capa_info |= MAC_CAPA_QOS;

    // add privacy
    capa_info &= ~MAC_CAPA_PRIVA;

    // Preamble
    capa_info |= MAC_CAPA_SHORT_PREAMBLE;
    if(vif->type == VIF_STA){
        if(vif->bss_info.capa_flags & BSS_PRIVA_CAPA)
            capa_info |= MAC_CAPA_PRIVA;	
       // if(vif->bss_info.cap_info & MAC_CAPA_SHORT_PREAMBLE) 
         //   capa_info |= MAC_CAPA_SHORT_PREAMBLE;
    }

    // add slot policy
    capa_info |= MAC_CAPA_SHORT_SLOT;
    // add PBCC

    // add channel agility
#if CFG_5G
    if (vif->bss_info.chan.band == PHY_BAND_5G)
        capa_info |= MAC_CAPA_SPECTRUM;
#endif

    // add BA
    // immediate BA supported
    //capa_info |= MAC_CAPA_IMMEDIATE_BA;

    #if NX_RM
    capa_info |= MAC_CAPA_RADIO_MEASUREMENT;
    #endif // NX_RM

    return capa_info;
}

#if NX_HE
uint32_t me_build_bss_color_reg(struct me_bss_info *bss)
{
    uint32_t bss_color = bss->he_oper & MAC_HE_OPER_BSS_COLOR_MASK;

    // Format as per BSS COLOR register format
    bss_color = bss->he_oper & MAC_HE_OPER_BSS_COLOR_MASK;
    bss_color >>= MAC_HE_OPER_BSS_COLOR_OFT;
    bss_color <<= NXMAC_BSS_COLOR_LSB;
    bss_color |= NXMAC_BSS_COLOR_EN_BIT;
    if (!(bss->he_oper & MAC_HE_OPER_BSS_COLOR_DISABLED_BIT))
        bss_color |= NXMAC_BSS_COLOR_EN_BIT;
    if (bss->he_oper & MAC_HE_OPER_BSS_COLOR_PARTIAL_BIT)
        bss_color |= NXMAC_PARTIAL_BSS_COLOR_EN_BIT;

    return bss_color;
}
#endif

void me_bw_check(PTR2UINT ht_op_addr, PTR2UINT vht_op_addr, 
                        struct me_bss_info *bss)
{
    struct mac_chan_op *chan = &bss->chan;

    // By default we consider that the BW will be 20MHz
    chan->type = PHY_CHNL_BW_20;
    chan->center1_freq = bss->chan.prim20_freq;
    chan->center2_freq = 0;

    // Check if there is a HT operation element
    if ((ht_op_addr != 0) && (me_env.phy_bw_max >= PHY_CHNL_BW_40))
    {
        uint8_t sec_ch_oft = co_read8p(ht_op_addr + MAC_HT_OPER_PRIM_CH_OFT + 1) & 3;
        if (sec_ch_oft != 0)
        {
            // Compute the secondary channel frequency offset
            int8_t freq_offset = (sec_ch_oft == 1) ? 10 : -10;
            chan->center1_freq += freq_offset;
            chan->type = PHY_CHNL_BW_40;
        }
    }

    // Check if there is a VHT operation element
    #if NX_VHT
    if ((vht_op_addr != 0) && (me_env.phy_bw_max >= PHY_CHNL_BW_80))
    {
        uint8_t chan_width = co_read8p(vht_op_addr + MAC_VHT_CHAN_WIDTH_OFT) & 3;
        uint8_t center0 = co_read8p(vht_op_addr + MAC_VHT_CENTER_FREQ0_OFT);
        uint8_t center1 = co_read8p(vht_op_addr + MAC_VHT_CENTER_FREQ1_OFT);
        me_vht_bandwidth_parse(chan_width, center0, center1, chan);
    }
    #endif
}

void me_vht_bandwidth_parse(uint8_t width, uint8_t center0, 
                                      uint8_t center1, struct mac_chan_op *chan)
{
    int delta = 0;

    if ((width == 0) || (width >= 4))
        return;

    chan->center1_freq = phy_channel_to_freq(chan->band, center0);
    chan->center2_freq = phy_channel_to_freq(chan->band, center1);

    switch (width)
    {
        case 1:
            // 80MHz, 160MHz or 80+80MHz channel
            // center0: center of the 80MHz channel that contains the primary
            // center1: 0 (for 80MHz)
            //          center of the 160MHz channel (for 160MHz)
            //          center of the secondary 80MHz channel (for 80+80MHz)
            if (chan->center2_freq)
            {
                if (chan->center1_freq > chan->center2_freq)
                    delta = chan->center1_freq - chan->center2_freq;
                else
                    delta = chan->center2_freq - chan->center1_freq;
            }

            if ((delta == 40) && (me_env.phy_bw_max >= PHY_CHNL_BW_160))
            {
                chan->type = PHY_CHNL_BW_160;
                chan->center1_freq = chan->center2_freq;
                chan->center2_freq = 0;
            }
            else if ((delta > 40) && (me_env.phy_bw_max == PHY_CHNL_BW_80P80))
            {
                chan->type = PHY_CHNL_BW_80P80;
            }
            else
            {
                chan->type = PHY_CHNL_BW_80;
                chan->center2_freq = 0;
            }
            break;
        case 2:
            // 160MHZ channel
            // center0: center of the 160MHz channel
            // center1: 0
            if (me_env.phy_bw_max >= PHY_CHNL_BW_160)
            {
                chan->type = PHY_CHNL_BW_160;
            }
            else
            {
                // 160MHz not supported, only use the 80MHz that contains the primary channel
                chan->type = PHY_CHNL_BW_80;
                if (chan->center1_freq < chan->prim20_freq)
                    chan->center1_freq += 40;
                else
                    chan->center1_freq -= 40;
            }
            break;
        case 3:
            // 80+80Mhz channel
            // center0: center of the primary 80MHz channel
            // center1: center of the secondary 80MHz channel
            if (me_env.phy_bw_max == PHY_CHNL_BW_80P80)
            {
                chan->type = PHY_CHNL_BW_80P80;
            }
            else
            {
                // 80+80MHZ not supported, only used priumary 80MHz channel
                chan->type = PHY_CHNL_BW_80;
                chan->center2_freq = 0;
            }
            break;
        default:
            break;
    }
}


