/*
 * INCLUDE FILES
 ****************************************************************************************
 */
// minimum default inclusion directive
#include "mac_ie.h"
#include "bl_ipc_compat.h"

/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Find an information element in the variable part of a management frame body.
 *
 * @param[in] addr Address of the variable part of the management frame body to look
 * for the IE into.
 * @param[in] buflen Length of the frame body variable part.
 * @param[in] ie_id  Identifier of the information element to look for.
 * @param[out] len   Length of the complete element
 *
 * @return 0 if the IE is not found otherwise the address of the first IE ID location
 * in the frame passed as a parameter.
 * @warning To find a vendor specific information element, see @ref mac_vsie_find. To
 * find an extended information element, see @ref mac_ext_ie_find
 ****************************************************************************************
 */
PTR2UINT mac_ie_find(PTR2UINT addr, uint16_t buflen, uint8_t ie_id,
                         uint16_t *len)
{
    PTR2UINT end = addr + buflen;
    // loop as long as we do not go beyond the frame size
    while ((addr + MAC_INFOELT_LEN_OFT) < end)
    {
        uint16_t ie_len = mac_ie_len(addr);
        PTR2UINT ie_end = addr + ie_len;

        // Check if the current IE is the one we look for
        if (ie_id == co_read8p(addr))
        {
            // Check if the IE length complies with the remaining length in the buffer
            if (ie_end > end)
                return 0;

            *len = ie_len;

            // The IE is valid
            return addr;
        }
        // move on to the next IE
        addr = ie_end;
    }

    return 0;
}

/**
 ****************************************************************************************
 * @brief Find an extended information element in the variable part of a management frame
 * body.
 *
 * @param[in] addr Address of the variable part of the management frame body to look
 * for the IE into.
 * @param[in] buflen Length of the frame body variable part.
 * @param[in] ext_ie_id  Identifier of the extended information element to look for.
 * @param[out] len   Length of the complete element
 *
 * @return 0 if the IE is not found otherwise the address of the first IE ID location
 * in the frame passed as a parameter.
 ****************************************************************************************
 */
static PTR2UINT mac_ext_ie_find(PTR2UINT addr,  uint16_t buflen,
                                      uint8_t ext_ie_id, uint16_t *len)
{
    PTR2UINT end = addr + buflen;
    // loop as long as we do not go beyond the frame size
    while (addr < end)
    {
        // First of all we need to find the extension element ID
        addr = mac_ie_find(addr, buflen, MAC_ELTID_EXT, len);

        // Check if we found the extension ID, and that we have at least one byte
        // available after the length for the extension field
        if ((addr == 0) || ((addr + MAC_INFOELT_EXT_INFO_OFT) > end))
            return 0;

        // Check if the extension field is the one we look for
        if (ext_ie_id == co_read8p(addr + MAC_INFOELT_EXT_ID_OFT))
        {
            // the extension field matches, return the pointer to this IE
            return addr;
        }
        // move on to the next extended IE
        addr += *len;
        buflen -= *len;
    }

    // sanity check: the offset can not be greater than the length
    ASSERT_ERR(addr == end);

    return 0;
}

/**
 ****************************************************************************************
 * @brief Find a vendor specific information element in the variable part of a management
 * frame body.
 *
 * @param[in] addr Address of the variable part of the management frame body to look
 * for the IE into.
 * @param[in] buflen Length of the frame body variable part.
 * @param[in] oui Pointer to the OUI identifier to look for.
 * @param[in] ouilen Length of the OUI identifier.
 * @param[out] len   Length of the complete element
 *
 * @return NULL if the VSIE is not found otherwise the pointer to the first VSIE ID
 * location in the frame passed as a parameter.
 ****************************************************************************************
 */
PTR2UINT mac_vsie_find(PTR2UINT addr, uint16_t buflen, uint8_t const *oui,
                            uint8_t ouilen, uint16_t *len)
{
    PTR2UINT end = addr + buflen;

    // loop as long as we do not go beyond the frame size
    while (addr < end)
    {
        // First of all we need to find the OUI ID
        addr = mac_ie_find(addr, buflen, MAC_ELTID_OUI, len);

        // Check if we found the OUI ID, and that we have enough bytes
        // available after the length for the OUI length
        if ((addr == 0) || ((addr + MAC_INFOELT_INFO_OFT + ouilen) > end))
            return 0;

        // check if the OUI matches the one we are looking for
        if (co_cmp8p(addr + MAC_INFOELT_INFO_OFT, oui, ouilen))
        {
            // the OUI matches, return the pointer to this IE
            return addr;
        }

        // Move on to the next OUI ID
        addr += *len;
        buflen -= *len;
    }

    // sanity check: the offset can not be greater than the length
    ASSERT_ERR(addr == end);

    return 0;
}

PTR2UINT mac_ie_rates_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_RATES, &len);

    if ((addr == 0) || (len > MAC_RATES_MAX_LEN) ||
        (len < MAC_RATES_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_ext_rates_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_EXT_RATES, &len);

    if ((addr == 0) || (len < MAC_RATES_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_ssid_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_SSID, &len);

    if ((addr == 0) || (len > MAC_SSID_MAX_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_country_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_COUNTRY, &len);

    if ((addr == 0) || (len < MAC_COUNTRY_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_rsn_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_RSN_IEEE, &len);

    if ((addr == 0) || (len < MAC_RSNIE_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_wpa_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_vsie_find(buffer, buflen,
                                  (uint8_t const *)"\x00\x50\xF2\x01\x01", 5, &len);

    if ((addr == 0) ||  (len < MAC_WPA_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_wapi_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_WAPI, &len);

    if ((addr == 0) || (len < MAC_WAPI_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_mesh_id_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_MESH_ID, &len);

    if ((addr == 0) || (len > MAC_MESHID_MAX_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_mesh_peer_mgmt_find(PTR2UINT buffer, 
                                              uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_MESH_PEER_MGMT, &len);

    if ((addr == 0) || (len > MAC_MPM_MAX_LEN) ||
        (len < MAC_MPM_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_mesh_awake_win_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_MESH_AWAKE_WINDOW, &len);

    if ((addr == 0) || (len != MAC_MAW_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_mesh_conf_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_MESH_CONF, &len);

    if ((addr == 0) || (len != MAC_MCFG_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_tim_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_TIM, &len);

    if ((addr == 0) || (len > MAC_TIM_MAX_LEN) ||
        (len < MAC_TIM_MIN_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_csa_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_CHANNEL_SWITCH, &len);

    if ((addr == 0) || (len != MAC_CHANNEL_SWITCH_ELT_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_ecsa_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_EXT_CHANNEL_SWITCH, &len);

    if ((addr == 0) || (len != MAC_EXT_CHANNEL_SWITCH_ELT_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_sec_chnl_offset_find(PTR2UINT buffer, 
                                              uint16_t buflen, bool *valid_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_SEC_CH_OFFSET, &len);

    if (addr == 0)
        return 0;

    *valid_len = (len == MAC_INFOELT_SEC_CH_OFFSET_ELT_LEN);

    return addr;
}

PTR2UINT mac_ie_wide_bw_chnl_find(PTR2UINT buffer, 
                                            uint16_t buflen, bool *valid_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_CHAN_SWITCH_WRP, &len);

    if (addr == 0)
        return 0;

    addr = mac_ie_find(addr + MAC_INFOELT_INFO_OFT, len - MAC_INFOELT_INFO_OFT,
                       MAC_ELTID_WIDE_BANDWIDTH_CHAN_SWITCH, &len);

    if (addr == 0)
        return 0;

    *valid_len = (len == MAC_INFOELT_WIDE_BW_CHAN_SWITCH_ELT_LEN);

    return addr;
}

PTR2UINT mac_ie_ds_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_DS, &len);

    if ((addr == 0) || (len != MAC_DS_PARAM_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_mgmt_mic_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_MGMT_MIC, &len);

    if ((addr == 0) || (len != MAC_MGMT_MIC_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_qos_capa_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_QOS_CAPA, &len);

    if ((addr == 0) || (len != MAC_QOS_CAPA_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_erp_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_ERP, &len);

    if ((addr == 0) || (len != MAC_ERP_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_op_mode_notif_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_OP_MODE_NOTIF, &len);

    if ((addr == 0) || (len != MAC_OP_MODE_NOTIF_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_power_constraint_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_POWER_CONSTRAINT, &len);

    if ((addr == 0) || (len != MAC_POWER_CONSTRAINT_ELT_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_mde_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_MDE, &len);

    if ((addr == 0) || (len != MAC_INFOELT_MDE_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_wmm_param_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_vsie_find(buffer, buflen,
                                  (uint8_t const *)"\x00\x50\xF2\x02\x01", 5, &len);

    if ((addr == 0) || (len != MAC_WMM_PARAM_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_ht_capa_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_HT_CAPA, &len);

    if ((addr == 0) || (len != MAC_HT_CAPA_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_multi_bssid_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_MULTIPLE_BSSID, &len);

    if ((addr == 0) || (len < MAC_MULTI_BSSID_MIN_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_sub_non_txed_bssid_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_SUBID_MBSSID_NON_TXED_PROF, &len);

    if ((addr == 0) || (len < MAC_MBSSID_NON_TXED_PROF_MIN_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_non_txed_bssid_capa_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_NON_TXED_BSSID_CAPA, &len);

    if ((addr == 0) || (len != MAC_NON_TXED_BSSID_CAPA_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_multi_bssid_index_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_MULTI_BSSID_INDEX, &len);

    if ((addr == 0) || (len < MAC_MULTI_BSSID_INDEX_MIN_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_vht_capa_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_VHT_CAPA, &len);

    if ((addr == 0) || (len != MAC_VHT_CAPA_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_he_capa_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ext_ie_find(buffer, buflen, MAC_ELTID_EXT_HE_CAPA, &len);

    if ((addr == 0) || (len > MAC_HE_CAPA_MAX_LEN) ||
        (len < MAC_HE_CAPA_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_EXT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_ht_oper_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_HT_OPERATION, &len);

    if ((addr == 0) || (len != MAC_HT_OPER_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_vht_oper_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_VHT_OPERATION, &len);

    if ((addr == 0) || (len != MAC_VHT_OPER_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_he_oper_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    PTR2UINT addr = mac_ext_ie_find(buffer, buflen, MAC_ELTID_EXT_HE_OPERATION, &len);

    if ((addr == 0) || (len > MAC_HE_OPER_MAX_LEN) ||
        (len < MAC_HE_OPER_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_EXT_INFO_OFT;

    return addr;
}

PTR2UINT mac_ie_mu_edca_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ext_ie_find(buffer, buflen, MAC_ELTID_EXT_MU_EDCA, &len);

    if ((addr == 0) || (len != MAC_MU_EDCA_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_uora_find(PTR2UINT buffer, uint16_t buflen)
{
    uint16_t len;
    PTR2UINT addr = mac_ext_ie_find(buffer, buflen, MAC_ELTID_EXT_UORA, &len);

    if ((addr == 0) || (len != MAC_UORA_LEN))
        return 0;

    return addr;
}

PTR2UINT mac_ie_ext_cap_find(PTR2UINT buffer, uint16_t buflen, uint16_t* length)
{
    PTR2UINT addr = mac_ie_find(buffer, buflen, MAC_ELTID_EXT_CAPA, length);

    if ((addr == 0) || (*length < MAC_EXT_CAPA_MIN_LEN))
        return 0;

    return addr;
}


