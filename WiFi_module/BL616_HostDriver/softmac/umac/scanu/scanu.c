/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "bl_ipc_compat.h"

#include "co_endian.h"
#include "co_utils.h"

#include "scanu.h"
#include "scanu_task.h"
#include "mac_frame.h"
#include "me_mgmtframe.h"
#include "me_utils.h"

#include "rxu_task.h"
#include "me.h"
#include "mac_ie.h"
#include "hal_mac_desc.h"
#include "mm_task.h"

#include "softmac.h"

#include "vif_mgmt.h"
#include "sta_mgmt.h"

#if (NX_P2P)
#include "p2p.h"
#endif //(NX_P2P)

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */

/// SCAN module environment definition.
struct scanu_env_tag scanu_env;

struct scanu_add_ie_tag scanu_add_ie;

/*
 * PRIVATE FUNCTIONS DEFINITION
 ****************************************************************************************
 */
static int scanu_scan_frame_handler(struct rxu_mgt_ind const *frame);

/**
 ****************************************************************************************
 * @brief Build the probe request IE.
 * The IE buffer will then be used by the LMAC to build the ProbeReq sent.
 *
 * @return The complete length of the information elements
 ****************************************************************************************
 */
static PTR2UINT scanu_build_ie(uint8_t *probe_ie_buf)
{
    struct scanu_start_req const *param = scanu_env.param;
    struct scanu_add_ie_tag *add_ie = &scanu_add_ie;
    PTR2UINT add_ie_buf = CPU2HW(add_ie->buf);
    PTR2UINT ie_buf = CPU2HW(probe_ie_buf);
    uint8_t sup_rate_len = MAC_RATESET_LEN;
    uint8_t sup_rate_oft = 0;
    uint8_t ie_len;
    uint16_t add_ie_len = param->add_ie_len;

    // Check if the additional IE buffer is valid
    if (add_ie_len > SCANU_MAX_IE_LEN)
        add_ie_len = 0;

    // Compute the number of legacy rates depending on the band we scan
#if CFG_5G
    if ((scanu_env.band == PHY_BAND_5G) || param->no_cck)
#else
    if (param->no_cck)
#endif
    {
        sup_rate_len = MAC_RATES_ELMT_MAX_LEN;
        sup_rate_oft = 4;
    }

    // The supported rates is the first element to put
    co_write8p(ie_buf++, MAC_ELTID_RATES);
    co_write8p(ie_buf++, MAC_RATES_ELMT_MAX_LEN);
    co_pack8p(ie_buf, &mac_id2rate[sup_rate_oft], MAC_RATES_ELMT_MAX_LEN);
    ie_buf += MAC_RATES_ELMT_MAX_LEN;

    // Check if the first element of the IE buffer is the request information
    if (add_ie_len && (co_read8p(add_ie_buf) == MAC_ELTID_REQUEST))
    {
        // Copy the IE
        ie_len = co_read8p(add_ie_buf + 1) + 2;
        co_copy8p(ie_buf, add_ie_buf, ie_len);
        ie_buf += ie_len;
        add_ie_buf += ie_len;
        add_ie_len -= ie_len;
    }

    // Extended supported rates
    if (sup_rate_len > MAC_RATES_ELMT_MAX_LEN)
    {
        ie_len = sup_rate_len - MAC_RATES_ELMT_MAX_LEN;
        co_write8p(ie_buf++, MAC_ELTID_EXT_RATES);
        co_write8p(ie_buf++, ie_len);
        co_pack8p(ie_buf, &mac_id2rate[MAC_RATES_ELMT_MAX_LEN], ie_len);
        ie_buf += ie_len;
    }

    // Then comes the DS parameter
    if (scanu_env.band == PHY_BAND_2G4)
    {
        co_write8p(ie_buf++, MAC_ELTID_DS);
        co_write8p(ie_buf++, 1);
        ie_buf++; // Skip the channel number, as it will be filled by the LMAC
    }

    // Now we may have the Supported Operating Classes in the additional IE
    if (add_ie_len && (co_read8p(add_ie_buf) == MAC_ELTID_SUPP_OPER_CLASS))
    {
        // Copy the IE
        ie_len = co_read8p(add_ie_buf + 1) + 2;
        co_copy8p(ie_buf, add_ie_buf, ie_len);
        ie_buf += ie_len;
        add_ie_buf += ie_len;
        add_ie_len -= ie_len;
    }

    // Now we put the HT capabilities
    if (LOCAL_CAPA(HT))
    {
        me_add_ie_ht_capa(&ie_buf);
    }

    // And we copy the rest of the additional IEs
    if (add_ie_len)
    {
        co_copy8p(ie_buf, add_ie_buf, add_ie_len);
        ie_buf += add_ie_len;
    }

    #if NX_VHT && CFG_5G
    // Add the VHT capability information
    if ((scanu_env.band == PHY_BAND_5G) && LOCAL_CAPA(VHT))
    {
        me_add_ie_vht_capa(&ie_buf);
    }
    #endif

    #if NX_HE
    // Add the HE capability information
    if (LOCAL_CAPA(HE))
    {
        me_add_ie_he_capa(&ie_buf);
    }
    #endif

    // Sanity check - we shall not have overflowed the IE buffer
    ASSERT_ERR((ie_buf - CPU2HW(probe_ie_buf)) <= SCAN_MAX_IE_LEN);

    // Return the complete length
    return (ie_buf - CPU2HW(probe_ie_buf));
}

/**
 ****************************************************************************************
 * @brief Prepare the download of the additional IEs.
 * If no additional IEs are present, or too long, then this function proceeds directly
 * to the scan.
 ****************************************************************************************
 */
static void scanu_ie_download(void)
{
    struct scanu_start_req const *param = scanu_env.param;

    dbg_chan("%s\n", __func__);

    // Check if we have an IE buffer for this band
    if (param->add_ie_len > SCANU_MAX_IE_LEN)
    {
        // Start the scan procedure immediately
        scanu_scan_next();
    }
    else
    {
        struct scanu_add_ie_tag *ie_desc = &scanu_add_ie;

        #if (FIX_WFA_MBO_5_2_1)
        memcpy(ie_desc->buf, scanu_env.add_ies_buf,scanu_env.add_ie_len);
        #else
        memcpy(ie_desc->buf, (void *)param->add_ies_buf, param->add_ie_len);
        #endif
        scanu_scan_next();
    }
}

/**
 ****************************************************************************************
 * @brief Parse security info from provided Elements
 *
 * @param[in]  ies      Pointer to elements
 * @param[in]  ies_len  Size, in bytes, of the elements
 * @param[in]  capa     Capabilities information in beacon/probe response frame
 * @param[out] res      Scan result to update with security info
 ****************************************************************************************
 */
static void scanu_get_security_info(PTR2UINT ies, uint16_t ies_len, 
                                           uint16_t capa,
                                           struct mac_scan_result *res)
{
    #if !NX_FULLY_HOSTED
    // This information is not used in FullMAC firmware so no need to parse it
    res->akm = 0;
    res->group_cipher = 0;
    res->pairwise_cipher = 0;

    #else
    PTR2UINT sec_ie;
    uint8_t sec_ie_len;
    int cnt, len;

    res->akm = 0;
    res->group_cipher = 0;
    res->pairwise_cipher = 0;

    if (!(capa & MAC_CAPA_PRIVA))
    {
        res->akm = CO_BIT(MAC_AKM_NONE);
        return;
    }

    #define READ_CIPHER_SUITE(type)                                         \
        {                                                                   \
            int val = mac_cipher_suite_value(co_ntohl(co_read32p((void *)sec_ie))); \
            if (val > 0)                                                    \
                res->type |= CO_BIT(val);                                   \
            sec_ie += 4;                                                    \
            len -= 4;                                                       \
         }

    #define READ_AKM_SUITE()                                             \
        {                                                                \
            int val = mac_akm_suite_value(co_ntohl(co_read32p((void *)sec_ie))); \
            if (val > 0)                                                 \
                res->akm |= CO_BIT(val);                                 \
            sec_ie += 4;                                                 \
            len -= 4;                                                    \
         }

    #define READ_CNT()                          \
        cnt = co_wtohs(co_read16p((void *)sec_ie));     \
        sec_ie += 2;                            \
        len -= 2;

    // First look for RSN Element
    sec_ie = mac_ie_rsn_find(ies, ies_len, &sec_ie_len);
    if (sec_ie)
    {
        uint16_t rsn_capa;

        sec_ie += MAC_RSNIE_GROUP_CIPHER_OFT;
        len = sec_ie_len + MAC_INFOELT_INFO_OFT - MAC_RSNIE_GROUP_CIPHER_OFT;

        READ_CIPHER_SUITE(group_cipher);

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_CIPHER_SUITE(pairwise_cipher);
            cnt--;
        }

        if (len < 2)
            return;

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_AKM_SUITE();
            cnt--;
        }

        if (len < 2)
            return;

        rsn_capa = co_wtohs(co_read16p((void *)sec_ie));
        sec_ie += 2;
        len -= 2;

        if (len >= 2)
        {
            READ_CNT();
            sec_ie += MAC_RSNIE_RSN_PMKID_SIZE * cnt;
            len -= MAC_RSNIE_RSN_PMKID_SIZE * cnt;
        }

        if (rsn_capa & (MAC_RSNIE_CAPA_MFPR_BIT | MAC_RSNIE_CAPA_MFPC_BIT))
        {
            if (len >= 4)
            {
                READ_CIPHER_SUITE(group_cipher);
            }
            else
            {
                res->group_cipher |= CO_BIT(MAC_CIPHER_BIP_CMAC_128);
            }
        }

        return;
    }

    // Else look for WPA Element
    sec_ie = mac_ie_wpa_find(ies, ies_len, &sec_ie_len);
    if (sec_ie)
    {
        res->akm = CO_BIT(MAC_AKM_PRE_RSN);

        sec_ie += MAC_WPA_GROUP_CIPHER_OFT;
        len = sec_ie_len + MAC_INFOELT_INFO_OFT -  MAC_WPA_GROUP_CIPHER_OFT;

        READ_CIPHER_SUITE(group_cipher);

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_CIPHER_SUITE(pairwise_cipher);
            cnt--;
        }

        if (len < 2)
            return;

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_AKM_SUITE();
            cnt--;
        }

        return;
    }

    #if RW_WAPI_EN
    // Last try WAPI Element
    sec_ie = mac_ie_wapi_find(ies, ies_len, &sec_ie_len);
    if (sec_ie)
    {

        sec_ie += MAC_WAPI_AKM_SUITE_CNT_OFT;
        len = sec_ie_len + MAC_INFOELT_INFO_OFT - MAC_WAPI_AKM_SUITE_CNT_OFT;

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_AKM_SUITE();
            cnt--;
        }

        if (len < 2)
            return;

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_CIPHER_SUITE(pairwise_cipher);
            cnt--;
        }

        if (len < 4)
            return;

        READ_CIPHER_SUITE(group_cipher);

        return;
    }
    #endif // RW_WAPI_EN

    // No 'Security' Element, assume WEP
    res->akm = CO_BIT(MAC_AKM_PRE_RSN);
    res->group_cipher = CO_BIT(MAC_CIPHER_WEP40);

    #undef READ_CIPHER_SUITE
    #undef READ_AKM_SUITE
    #undef READ_CNT

    #endif // NX_FULLY_HOSTED
}

/**
 ****************************************************************************************
 * @brief Allocate a scan database element and copy the information about the
 * nonTransmitted BSSID.
 *
 * The function first generates the BSSID value based on the reference BSSID and the
 * nonTransmitted BSSID index.
 *
 * @param[in]  mbssid Pointer to the found nonTransmitted BSSID profile information
 * @param[in]  max_bss_ind Maximum BSSID indicator
 * @param[out] res Scan result element linked to the reference BSSID
 *
 * @return true if the new nonTransmitted BSSID was correctly configured, false otherwise
 ****************************************************************************************
 */
static bool scanu_new_bssid_set(struct scanu_mbssid_profile_tag *mbssid,
                                        uint8_t max_bss_ind,
                                        struct mac_scan_result const *res)
{
    struct mac_addr bssid;
    struct mac_scan_result *new_res;
    uint8_t ssid_len = mac_ie_len(mbssid->ssid_ie_addr) - MAC_INFOELT_INFO_OFT;

    // Compute the nonTransmitted BSSID
    if (!mac_nontxed_bssid_get(mbssid->bssid_index, max_bss_ind, &res->bssid, &bssid))
        return false;

    // Check if the element is already present
    new_res = scanu_find_result(&bssid, false);
    if (new_res)
        return true;

    // Allocate a new element for the BSSID
    new_res = scanu_find_result(&bssid, true);
    if (new_res == NULL)
        return false;

    // Copy the info about the new BSSID from the reference one
    *new_res = *res;
    // Copy the new BSSID specific information
    new_res->bssid = bssid;
    new_res->ssid.length = ssid_len;
    co_unpack8p(new_res->ssid.array, mbssid->ssid_ie_addr + MAC_SSID_SSID_OFT, ssid_len);
    new_res->max_bssid_indicator = max_bss_ind;
    new_res->multi_bssid_index = mbssid->bssid_index;
    scanu_env.result_cnt++;
    new_res->valid_flag = true;

    return true;
}

/**
 ****************************************************************************************
 * @brief Store the found nonTransmitted BSSIDs into the scan data base
 *
 * @param[in] res Pointer to the reference BSSID scan result element
 ****************************************************************************************
 */
static void scanu_store_multi_bssid_info(struct mac_scan_result *res)
{
    struct scanu_mbssids_tag *mbssids = &scanu_env.mbssids;
    int i;
    
    // Reference BSSID always has a null BSSID index
    res->multi_bssid_index = 0;
    res->max_bssid_indicator = 0;

    for (i = 0; i < mbssids->mbssid_cnt; i++)
    {
        if (!scanu_new_bssid_set(&mbssids->bssids[i], mbssids->max_bssid_ind, res))
            return;
    }
}

/**
 ****************************************************************************************
 * @brief Parse potential multiple BSSID elements within the Beacon/ProbeRsp
 *
 * This function will store in the @ref scanu_env the information about the nonTransmitted
 * BSSIDs (BSSID index, SSID element address, etc.) for the received beacon/proberesp.
 * The function is doing a quite complex parsing of the frame. The reason for that is the
 * fact the nonTransmitted BSSID profiles can be split across several Multiple BSSID
 * elements. Moreover the profiles are not included directly in the Multiple BSSID
 * elements, but in Nontransmitted BSSID Profile sub-elements.
 *
 * @param[in]  ies      Pointer to elements
 * @param[in]  ies_len  Size, in bytes, of the elements
 ****************************************************************************************
 */
static void scanu_get_multi_bssid_info(PTR2UINT ies, uint16_t ies_len)
{
    PTR2UINT capa_addr = 0;
    PTR2UINT bssid_index_ie_addr = 0;
    PTR2UINT ssid_ie_addr = 0;
    uint8_t ssid_len;
    struct scanu_mbssids_tag *mbssids = &scanu_env.mbssids;
    mbssids->mbssid_cnt = 0;

    while (ies_len)
    {
        PTR2UINT mbssid_ie_addr;
        uint32_t mbssid_ie_len;
        PTR2UINT sub_ie_addr;
        uint32_t sub_ie_len;
        PTR2UINT sub_ies;
        uint16_t subies_len;
        PTR2UINT bssid_ies;
        uint16_t bssid_ies_len;

        // Try to get a Multiple BSSID element
        mbssid_ie_addr = mac_ie_multi_bssid_find(ies, ies_len);
        if (!mbssid_ie_addr)
            return;

        mbssid_ie_len = mac_ie_len(mbssid_ie_addr);
        mbssids->max_bssid_ind = co_read8p(mbssid_ie_addr + MAC_MULTI_BSSID_MAX_INDICATOR_OFT);
        sub_ies = mbssid_ie_addr + MAC_MULTI_BSSID_SUB_IES_OFT;
        subies_len = mbssid_ie_len - MAC_MULTI_BSSID_SUB_IES_OFT;

        while (subies_len)
        {
            // A Multiple BSSID element has been found, search for a nonTransmittedBSSID
            // profile inside it
            sub_ie_addr = mac_ie_sub_non_txed_bssid_find(sub_ies, subies_len);
            if (!sub_ie_addr)
                break;

            sub_ie_len = mac_ie_len(sub_ie_addr);
            bssid_ies = sub_ie_addr + MAC_MBSSID_NON_TXED_PROF_INFO_OFT;
            bssid_ies_len = sub_ie_len - MAC_MBSSID_NON_TXED_PROF_INFO_OFT;
            sub_ies += sub_ie_len;
            subies_len -= sub_ie_len;

            // Check if this is the start of a BSS profile - To do that we check if the
            // nonTransmitted BSSID capability element is the first of the sub-element
            if (mac_ie_non_txed_bssid_capa_find(bssid_ies, MAC_NON_TXED_BSSID_CAPA_LEN))
            {
                // New BSS profile, restart the search for both the BSSID index and SSID
                capa_addr = bssid_ies + MAC_NON_TXED_BSSID_CAPA_OFT;
                bssid_index_ie_addr = 0;
                ssid_ie_addr = 0;
            }
            else if (!capa_addr)
                continue;

            // Now search for the BSSID index and SSID
            if (!bssid_index_ie_addr)
                bssid_index_ie_addr = mac_ie_multi_bssid_index_find(bssid_ies, bssid_ies_len);
            if (!ssid_ie_addr)
                ssid_ie_addr = mac_ie_ssid_find(bssid_ies, bssid_ies_len, &ssid_len);

            // Check if we have all the info related to the nonTransmitted BSSID and
            // configure it
            if (bssid_index_ie_addr && ssid_ie_addr)
            {
                struct scanu_mbssid_profile_tag *mbssid = &mbssids->bssids[mbssids->mbssid_cnt];
                mbssid->bssid_index = co_read8p(bssid_index_ie_addr + MAC_MULTI_BSSID_INDEX_OFT);
                mbssid->ssid_ie_addr = ssid_ie_addr;
                mbssid->capa = co_read16p((void *)capa_addr);
                mbssids->mbssid_cnt++;
                if (mbssids->mbssid_cnt == SCANU_MAX_NONTXED_BSSID_PER_BEACON)
                    return;
                capa_addr = 0;
            }
        }
        
        ies_len -= (mbssid_ie_addr - ies) + mbssid_ie_len;
        ies = mbssid_ie_addr + mbssid_ie_len;
    }
}

/**
 ****************************************************************************************
 * @brief Check if the SSID passed as parameter (retrieved from the beacon or probe
 * response) matches one of the SSIDs we are scanning for.
 *
 * @param[in] ssid Pointer to the SSID present in the beacon/probe response
 *
 * @return true if SSID matches one we searched for, false otherwise
 ****************************************************************************************
 */
static bool scanu_is_scanned_ssid(struct mac_ssid *ssid)
{
    struct scanu_start_req const *param = scanu_env.param;
    int i;

    #if (NX_P2P)
    if (scanu_env.p2p_scan)
    {
        if (!memcmp(&ssid->array[0], P2P_SSID_WILDCARD, P2P_SSID_WILDCARD_LEN))
            return true;
    }
    else
    #endif //(NX_P2P)
    {
        // Check if the SSID is one we are looking for
        if (!param->ssid_cnt)
            // No SSID specifically searched, so consider this one as valid
            return true;

        for (i = 0; i < param->ssid_cnt; i++)
        {
            struct mac_ssid const *scanned_ssid = &param->ssid[i];
            if ((scanned_ssid->length == 0) || (MAC_SSID_CMP(scanned_ssid, ssid)))
                // Received SSID is an expected one
                return true;
        }
    }

    return false;
}

/**
 ****************************************************************************************
 * @brief Go through the SSIDs found in the Beacon/ProbeRsp and check if one is matching
 * the SSIDs we are looking for.
 *
 * The function first checks the "main" SSID (i.e. the one of the reference BSSID), and
 * then goes through the nonTransmitted BSSID elements if any.
 *
 * @param[in] res Pointer to the scan result element
 *
 * @return true if we have a match on one of the SSIDs, false otherwise
 ****************************************************************************************
 */
static bool scanu_check_ssid(struct mac_scan_result *res)
{
    struct scanu_mbssids_tag *mbssids = &scanu_env.mbssids;
    int i;
    
    // Check if the SSID matches one we are looking for
    if (scanu_is_scanned_ssid(&res->ssid))
        // SSID matches, exit immediately
        return true;

    // Main SSID does not match, check if there is some nonTransmitted BSSID profiles present
    // that would match a searched SSID
    for (i = 0; i < mbssids->mbssid_cnt; i++)
    {
        struct scanu_mbssid_profile_tag *mbssid = &mbssids->bssids[i];
        struct mac_ssid ssid;

        ssid.length = mac_ie_len(mbssid->ssid_ie_addr) - MAC_INFOELT_INFO_OFT;
        co_unpack8p(ssid.array, mbssid->ssid_ie_addr + MAC_SSID_SSID_OFT, ssid.length);

        // Check if the SSID matches one we are looking for
        if (scanu_is_scanned_ssid(&ssid))
            // SSID matches, exit immediately
            return true;
    }

    return false;

}

/**
 ****************************************************************************************
 * @brief Handle the reception of a beacon or probe response frame when such a frame is
 * received during the joining process.
 *
 * The function copies the information present in the scan database element into the BSS
 * information structure, and then extracts all the remaining information from the frame.
 *
 * @param[in] frame Pointer to the received beacon/probe-response message.
 *
 * @return The message status to be returned to the kernel (@ref KE_MSG_CONSUMED or
 *         @ref KE_MSG_NO_FREE)
 ****************************************************************************************
 */
static int scanu_join_frame_handler(struct rxu_mgt_ind const *frame)
{
    struct mac_scan_result *scan;
    struct scanu_start_req const *param = scanu_env.param;
    PTR2UINT var_part_addr;
    uint32_t var_part_len;
    PTR2UINT ht_op_addr = 0, vht_op_addr = 0;
    struct bcn_frame const *frm = (struct bcn_frame const *)frame->payload;
    struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];
    struct me_bss_info *bss = &vif->bss_info;
    bool changed;

    dbg_chan("%s\n", __func__);

    // We are in the join process, so the received BSSID shall correspond either to the
    // BSSID we join to, or the reference BSSID in case of Multiple BSSID
    if (!MAC_ADDR_CMP(&frm->h.addr3, &scanu_env.bssid) &&
        !MAC_ADDR_CMP(&frm->h.addr3, &scanu_env.ref_bssid))
        return KE_MSG_CONSUMED;

    // find the scan result associated to the BSSID
    scan = scanu_find_result(&scanu_env.bssid, false);
    // reset scan results, and parse the frame again
    if (scan == NULL)
    {
        int i;
        
        // reset the scan results before starting a new scan
        for (i = 0; i < SCANU_MAX_RESULTS; i++)
        {
            scanu_env.scan_result[i].valid_flag = false;
        }
        // reset the valid result counter
        scanu_env.result_cnt = 0;
        scanu_scan_frame_handler(frame);
        
        scan = scanu_find_result(&scanu_env.bssid, false);
        if (scan == NULL)
            return KE_MSG_CONSUMED;
        else {
            dbg_con("%s, scan res, 0x%x 0x%x 0x%x, 0x%x 0x%x 0x%x\n", __func__, 
                  scan->bssid.array[0], scan->bssid.array[1], 
                  scan->bssid.array[2],
                  scanu_env.bssid.array[0], scanu_env.bssid.array[1], 
                  scanu_env.bssid.array[2]);
        }
    }

    // Initialize the variable part address
    var_part_addr = CPU2HW(frm->variable);
    var_part_len = frame->length - MAC_BEACON_VARIABLE_PART_OFT;

    // Initialize EDCA parameter set count to an invalid value to ensure that we
    // will retrieve the EDCA parameters at least once
    #if NX_HE
    bss->mu_edca_param.param_set_cnt = 0xFF;
    #endif
    bss->edca_param.param_set_cnt = 0xFF;

    // Copy some parameters from the scan result
    bss->bsstype = scan->bsstype;
    bss->bssid = scan->bssid;
    bss->beacon_period = frm->bcnint;
    bss->ssid = scan->ssid;
    bss->chan.band = scan->chan->band;
    bss->chan.prim20_freq = scan->chan->freq;
    bss->chan.tx_power = scan->chan->tx_power;
    bss->chan.flags = scan->chan->flags;
    bss->bssid_index = scan->multi_bssid_index;
    bss->max_bssid_ind = scan->max_bssid_indicator;
    // chan bandwidth info is updated by me_bw_check
    BSS_CAPA_RESET(bss);

    // privacy capabilities
    if (frm->capa & MAC_CAPA_PRIVA)
    {
        BSS_CAPA_SET(bss, PRIVA);
    }

    // Short preamble capabilities
    if (frm->capa & MAC_CAPA_SHORT_PREAMBLE)
    {
        BSS_CAPA_SET(bss, SHORT_PREAMBLE);
    }

    // retrieve the rate set (normal + extended)
    me_extract_rate_set(var_part_addr, var_part_len, &(bss->rate_set));
    if (scan->chan->band == PHY_BAND_2G4)
    {
        uint32_t basic_11b_rates;
        basic_11b_rates = me_legacy_rate_bitfield_build(&(bss->rate_set), true) & 0x0F;
        // Get highest allowed 11b rate
        if (basic_11b_rates)
            // Get highest allowed 11b rate
            bss->high_11b_rate = 31 - co_clz(basic_11b_rates);
        else
            // If no 11b basic rates, set the highest mandatory one
            bss->high_11b_rate = HW_RATE_2MBPS;
    }

    // retrieve the EDCA Parameter
    if (me_extract_edca_params(var_part_addr, var_part_len, &bss->edca_param,
                               &changed))
    {
        BSS_CAPA_SET(bss, QOS);
    }

    // retrieve the HT/VHT/HE Capability
    if (LOCAL_CAPA(HT) && BSS_CAPA(bss, QOS) &&
        me_extract_ht_capa(var_part_addr, var_part_len, &bss->ht_cap))
    {
        BSS_CAPA_SET(bss, HT);
        // retrieve the HT operation field
        ht_op_addr = mac_ie_ht_oper_find(var_part_addr, var_part_len);

        #if NX_HE
        if (LOCAL_CAPA(HE) &&
            me_extract_he_capa(var_part_addr, var_part_len, &bss->he_cap))
        {
            BSS_CAPA_SET(bss, HE);

            // retrieve the HE operation field
            me_extract_he_oper(var_part_addr, var_part_len, bss);
        }
        #endif

        #if NX_VHT
        // We get the VHT elements in two cases:
        //  - Either we are VHT capable
        //  - Or we are HE capable and the BSS is a HE one. Indeed in that case
        //    some VHT parameters (BW, A-MDPU max size, MDPU max size, etc.)
        //    apply to the HE as well
        if ((LOCAL_CAPA(VHT) || BSS_CAPA(bss, HE)) &&
            me_extract_vht_capa(var_part_addr, var_part_len, &bss->vht_cap))
        {
            BSS_CAPA_SET(bss, VHT);
            // retrieve the VHT operation field
            vht_op_addr = mac_ie_vht_oper_find(var_part_addr, var_part_len);
        }
        #endif
    }

    // Initialize the BW and channel parameters
    me_bw_check(ht_op_addr, vht_op_addr, bss);

    // Get power constraint
    me_extract_power_constraint(var_part_addr, var_part_len, bss);

    // Get regulatory rules
    me_extract_country_reg(var_part_addr, var_part_len, bss);
    
    // Get Mobility Domain ID and FT capability and policy
    me_extract_mobility_domain(var_part_addr, var_part_len, bss);

    // We consider now the BSS information as valid
    BSS_CAPA_SET(bss, VALID);

    // Set join_status success
    scanu_env.join_status = 1;

    // Always forward scan result to host so that we ensure that the host is aware
    // of the BSS before the SM_CONNECT_IND (needed for connection request without
    // prior scan request, eg ft over ds)
    ke_msg_forward_and_change_id(frame, SCANU_RESULT_IND, TASK_API, TASK_SCANU);
    
    return KE_MSG_NO_FREE;
}

/**
 ****************************************************************************************
 * @brief Handle the reception of a beacon or probe response frame when such a frame is
 * received during the scanning process.
 *
 * The function allocates a free element from the scan database and then extracts some
 * part of the information present in the frame (BSSID, SSID, RSSI, etc.).
 *
 * @param[in] frame Pointer to the received beacon/probe-response message.
 *
 * @return The message status to be returned to the kernel (@ref KE_MSG_CONSUMED or
 *         @ref KE_MSG_NO_FREE)
 ****************************************************************************************
 */
static int scanu_scan_frame_handler(struct rxu_mgt_ind const *frame)
{
    struct mac_scan_result scan, *res;
    PTR2UINT elmt_addr, var_part_addr;
    uint32_t var_part_len;
    struct bcn_frame const *frm = (struct bcn_frame const *)frame->payload;
    uint8_t elmt_length;
    uint16_t elmt_length_16;
    struct mac_addr bssid;

    dbg_chan("%s\n", __func__);

    MAC_ADDR_EXTRACT(&bssid, &frm->h.addr3);
    // check if the scan frame matches the SCAN requested BSSID
    if (!MAC_ADDR_GROUP(&scanu_env.bssid) &&
        !MAC_ADDR_CMP(&bssid, &scanu_env.bssid))
        return KE_MSG_CONSUMED;

    // copy the BSSID
    MAC_ADDR_CPY(&scan.bssid, &bssid);

    // ESS or IBSS
    if ((frm->capa & MAC_CAPA_ESS) == MAC_CAPA_ESS)
        scan.bsstype = INFRASTRUCTURE_MODE;
    else
        scan.bsstype = INDEPENDENT_BSS_MODE;

    // Initialize the variable part address
    var_part_addr = CPU2HW(frm->variable);
    var_part_len = frame->length - MAC_BEACON_VARIABLE_PART_OFT;

    // retrieve the SSID if broadcasted
    elmt_addr = mac_ie_ssid_find(var_part_addr, var_part_len, &elmt_length);
    if (elmt_addr != 0)
    {
        scan.ssid.length = elmt_length;
        // copy the SSID length
        co_unpack8p(scan.ssid.array, elmt_addr + MAC_SSID_SSID_OFT, elmt_length);
    }
    else
    {
        // SSID is not broadcasted
        scan.ssid.length = 0;
    }

    // Get potential information about Multi-BSSID, in order to retrieve the SSIDs
    scanu_get_multi_bssid_info(var_part_addr, var_part_len);

    // Retrieve extended capabilities
    elmt_addr = mac_ie_ext_cap_find(var_part_addr, var_part_len, &elmt_length_16);
    if (elmt_addr != 0) {
        scan.ftm_support = 
                  EXT_CAPA_BIT_IS_SET(HW2CPU(elmt_addr + MAC_INFOELT_INFO_OFT),
                                      FTM_RESPONDER,
                                      elmt_length_16 - MAC_INFOELT_INFO_OFT);
    } else {
        scan.ftm_support = false;
    }

    // Check if the found SSID matches the searched ones
    if (!scanu_check_ssid(&scan))
        return KE_MSG_CONSUMED;

    // retrieve the channel
    elmt_addr = mac_ie_ds_find(var_part_addr, var_part_len);
    if (elmt_addr != 0)
    {
        uint8_t ch_nbr = co_read8p(elmt_addr + MAC_DS_CHANNEL_OFT);
        scan.chan = me_freq_to_chan_def(frame->band,
                                      phy_channel_to_freq(frame->band, ch_nbr));
    }
    else
    {
        scan.chan = me_freq_to_chan_def(frame->band, frame->center_freq);
    }

    scanu_get_security_info(var_part_addr, var_part_len, frm->capa, &scan);

    dbg_chan("scaned bssid:0x%x 0x%x 0x%x\n", 
             scan.bssid.array[0], scan.bssid.array[1], scan.bssid.array[2]);

    // Store scan result if slot is available (or there is already one for this BSSID)
    res = scanu_find_result(&bssid, true);
    if (res)
    {
        int8_t rssi = frame->rssi;
        struct mac_chan_def *chan = scan.chan;
        bool valid = res->valid_flag;

        if (valid && (res->rssi > rssi))
        {
            // If result already exist keep the best rssi and the chan for the best
            // rssi (if chan is not included in DS element)
            rssi = res->rssi;
            chan = res->chan;
        }

        // copy scan result
        *res = scan;
        res->rssi = rssi;
        res->chan = chan;
        res->valid_flag = valid;

        if (!res->valid_flag)
        {
            scanu_env.result_cnt++;
            res->valid_flag = true;
        }

        // We have to do that after validating the scan element because this function
        // may allocate new scan elements
        scanu_store_multi_bssid_info(res);
    }

    // Always upload result, even if scan result cannot be saved in fw
    // add check condition if call this func in scanu_join_frame_handler, don't upload 
    if (scanu_env.req_type != SCANU_JOIN) {
        ke_msg_forward_and_change_id(frame, SCANU_RESULT_IND, TASK_API, TASK_SCANU);
    }

    return KE_MSG_NO_FREE;
}

void scanu_confirm(uint8_t status)
{
    struct scanu_start_cfm* cfm;

    dbg_f("==>scanu confirm, status:%d\r\n", status);

    ke_state_set(TASK_SCANU, SCANU_IDLE);
    // allocate message
    if (scanu_env.req_type == SCANU_JOIN)
    {
        cfm = KE_MSG_ALLOC(SCANU_JOIN_CFM, scanu_env.src_id, TASK_SCANU, 
                           scanu_start_cfm);
    }
    else
    {
        #if (NX_ANT_DIV)
        ke_msg_send_basic(MM_ANT_DIV_UPDATE_REQ, TASK_MM, TASK_SCANU);
        #endif //(NX_ANT_DIV)

        #if CFG_BBP_CTRL
        ke_msg_send_basic(MM_BBP_START_REQ, TASK_MM, TASK_SCANU);
        #endif
        
        cfm = KE_MSG_ALLOC(SCANU_START_CFM, scanu_env.src_id, TASK_SCANU, 
                           scanu_start_cfm);
    }
    // fill in the message parameters
    cfm->vif_idx = scanu_env.param->vif_idx;
    cfm->status = status;
    cfm->result_cnt = scanu_env.result_cnt;

    // Free the scan parameters structure
    ke_msg_free(ke_param2msg(scanu_env.param));
    scanu_env.param = NULL;

    // send the message to the sender
    ke_msg_send(cfm);
    
    dbg_f("<==scanu confirm\r\n");
}

void scanu_init(void)
{
    // set the state of the task to default
    ke_state_set(TASK_SCANU, SCANU_IDLE);

    dbg("%s\n", __func__);

    // reset the SCAN environment
    memset(&scanu_env, 0, sizeof(scanu_env));
}

int scanu_frame_handler(struct rxu_mgt_ind const *frame)
{
    // Check if we are in a scanning process
    if (ke_state_get(TASK_SCANU) != SCANU_SCANNING)
        return KE_MSG_CONSUMED;

    dbg_chan("%s, req_type:%d, len %d fctl 0x%x chan %d band %d rssi %d, sizeof(rxu_mgt_ind):%d\r\n", 
             __func__, scanu_env.req_type, frame->length, 
             frame->framectrl, frame->center_freq, frame->band, frame->rssi,
             sizeof(struct rxu_mgt_ind));

    if (scanu_env.req_type == SCANU_JOIN)
        return scanu_join_frame_handler(frame);
    else
        return scanu_scan_frame_handler(frame);
}

struct mac_scan_result *scanu_find_result(struct mac_addr const *bssid_ptr,
                                               bool allocate)
{
    int i;
    struct mac_scan_result *scan;

    // search in the scan list using the MAC address
    for (i = 0, scan = scanu_env.scan_result; i < SCANU_MAX_RESULTS; i++, scan++)
    {
        // if it is a valid BSS.
        if (scan->valid_flag)
        {
            if (MAC_ADDR_CMP(&scan->bssid, bssid_ptr))
                return scan;
        }
        else if (allocate)
        {
            // empty entry: if allocation was requested, then return this pointer
            scan->rssi = -128;
            return scan;
        }
    }
    return NULL;
}

struct mac_scan_result *scanu_search_by_bssid(struct mac_addr const *bssid)
{
    return (scanu_find_result(bssid, false));
}

struct mac_scan_result *scanu_search_by_ssid(struct mac_ssid const *ssid)
{
    int i;
    int8_t rssi = -128;
    struct mac_scan_result *scan_rslt = NULL;

    // Check if the SSID is valid
    if (!ssid->length)
        return NULL;

    // Search in the scan list using the SSID. If several BSSes share the same SSID,
    // the one with the highest RSSI will be returned.
    for (i = 0; i < SCANU_MAX_RESULTS; i++)
    {
        struct mac_scan_result *scan = &scanu_env.scan_result[i];

        // scan results are always ordered so stop on first non valid result
        if (!scan->valid_flag)
            break;

        if ((scan->rssi > rssi) && (MAC_SSID_CMP(&scan->ssid, ssid)))
        {
            scan_rslt = scan;
            rssi = scan->rssi;
        }
    }

    return (scan_rslt);
}

struct mac_scan_result *scanu_get_result_from_idx(uint8_t result_idx)
{
    // Check if the SSID is valid
    if (result_idx >= scanu_env.result_cnt)
        return NULL;

    return(&scanu_env.scan_result[result_idx]);
}

void scanu_start(void)
{
    if (scanu_env.req_type == SCANU_NO_JOIN)
    {
        int i;

        // reset the scan results before starting a new scan
        for (i = 0; i < SCANU_MAX_RESULTS; i++)
        {
            scanu_env.scan_result[i].valid_flag = false;
        }

        // reset the valid result counter
        scanu_env.result_cnt = 0;
    }

    #if (NX_P2P)
    if ((scanu_env.param->ssid_cnt == 1) && 
         (scanu_env.param->ssid[0].length == P2P_SSID_WILDCARD_LEN) &&
         !memcmp(&scanu_env.param->ssid[0].array[0], 
                 P2P_SSID_WILDCARD, P2P_SSID_WILDCARD_LEN))
    {
        scanu_env.p2p_scan = true;
    }
    else
    {
        scanu_env.p2p_scan = false;
    }
    #endif //(NX_P2P)

    // move to SCANNING state (done before scanu_next because it could reset the state)
    ke_state_set(TASK_SCANU, SCANU_SCANNING);

    dbg_f("%s\r\n", __func__);

    // Download the additional IE if available
    scanu_ie_download();
}

void scanu_scan_next(void)
{
    int i;
    struct scanu_start_req const *param = scanu_env.param;
    struct scan_start_req *req;

    if (scanu_env.param == NULL) {
        dbg_f("WARN:%s scanu prarm null\r\n", __func__);
        return;
    }

    dbg_chan("%s, scanu_env.band:%d, param->chan_cnt:%d\n", 
          __func__, scanu_env.band, param->chan_cnt);

    do
    {
        // Check if we scanned all the requested bands
        #if CFG_5G
        if (scanu_env.band > PHY_BAND_5G)
        #else
        if (scanu_env.band > PHY_BAND_2G4)
         #endif
            break;

        // Go through the list of channels to find the first channel to be scanned
        for (i = 0; i < param->chan_cnt; i++)
        {
            if (param->chan[i].band == scanu_env.band)
                break;
        }

        // Check if we have at least one channel to scan
        if (i == param->chan_cnt)
        {
            scanu_env.band++;
            continue;
        }

        dbg("%s, call alloc msg for scan_start_req\r\n", __func__);

        // Allocate the scan request for the LMAC
        req = KE_MSG_ALLOC(SCAN_START_REQ, TASK_SCAN, TASK_SCANU, scan_start_req);
        if (req == NULL){
            dbg_f("%s alloc req null\r\n", __func__);
            
            break;
        }
        // Fill-in the request
        req->vif_idx = param->vif_idx;
        req->bssid = param->bssid;
        req->ssid_cnt = param->ssid_cnt;
        req->no_cck = param->no_cck;
        req->duration = param->duration;

        // Prepare the channel list
        for (; i < param->chan_cnt; i++)
        {
            // Check if the current channel is on the band we have to scan
            if (param->chan[i].band == scanu_env.band)
            {
                req->chan[req->chan_cnt] = param->chan[i];
                req->chan_cnt++;
            }
        }
        
        // Prepare the SSIDs
        for (i = 0; i < param->ssid_cnt; i++)
        {
            req->ssid[i] = param->ssid[i];
        }

        // Prepare the IE buffer
        req->add_ie_len = scanu_build_ie(req->add_ies);

        dbg_chan("%s, chan_cnt:%d\r\n", __func__, req->chan_cnt);

        // send a scan start request
        ke_msg_send(req);

        return;
    } while(1);

    dbg_chan("%s call scanu_confirm\n", __func__);
    
    // Scan is done
    scanu_confirm(CO_OK);
}


