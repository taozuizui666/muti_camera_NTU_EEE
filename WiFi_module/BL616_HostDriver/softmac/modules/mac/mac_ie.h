#ifndef _MAC_IE_H_
#define _MAC_IE_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "co_utils.h"

#include "mac_frame.h"

/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Compute the total length of an information element.
 *
 * @param[in] addr Address of the information element.
 *
 * @return The length of the information element, including the two bytes (ID + Length)
 * header
 ****************************************************************************************
 */
__INLINE uint16_t mac_ie_len(PTR2UINT addr)
{
    return ((uint16_t)co_read8p(addr + MAC_INFOELT_LEN_OFT) + MAC_INFOELT_INFO_OFT);
}

PTR2UINT mac_ie_find(PTR2UINT addr, uint16_t buflen, uint8_t ie_id, uint16_t *len);

/**
 ****************************************************************************************
 * @brief Search for the Supported Rates element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_rates_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the Extended Supported Rates element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_ext_rates_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the SSID element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_ssid_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the Country element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_country_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the RSN element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_rsn_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the WPA element in a frame buffer
 *
 * This is the depercated pre-RSN element
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_wpa_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the WAPI element in a frame buffer
 *
 * Not part of IEEE 802.11.
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_wapi_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the Mesh ID element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_mesh_id_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the Mesh Peering Management element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_mesh_peer_mgmt_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the Mesh Awake Window element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_mesh_awake_win_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the Mesh Configuration element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_mesh_conf_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the TIM element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_tim_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the Channel Switch Announcement element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_csa_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the Extended Channel Switch Announcement element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_ecsa_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the Secondary Channel Offset element in a frame buffer
 * @note For the IEs associated to Channel Switch Announcement, we return the address of
 * the element even if its length is invalid, and we return an additional valid flag for
 * the length. Indeed a channel switch might be announced using several IEs, and all of
 * them need to be valid to ensure we have the complete information about the new channel.
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] valid_len   Indicates whether the length of the element is valid or not
 *
 * @return The address of the element if it is present, 0 otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_sec_chnl_offset_find(PTR2UINT buffer, uint16_t buflen, bool *valid_len);

/**
 ****************************************************************************************
 * @brief Search for the Wide Bandwidth Channel sub-element in a frame buffer
 * @note For the IEs associated to Channel Switch Announcement, we return the address of
 * the element even if its length is invalid, and we return an additional valid flag for
 * the length. Indeed a channel switch might be announced using several IEs, and all of
 * them need to be valid to ensure we have the complete information about the new channel.
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] valid_len   Indicates whether the length of the element is valid or not
 *
 * @return The address of the element if it is present, 0 otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_wide_bw_chnl_find(PTR2UINT buffer, uint16_t buflen, bool *valid_len);

/**
 ****************************************************************************************
 * @brief Search for the DS element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_ds_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the Management MIC element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_mgmt_mic_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the QoS Capability element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_qos_capa_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the ERP element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_erp_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the Operating Mode Notification element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_op_mode_notif_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the Power Constraint element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_power_constraint_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the MDE element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_mde_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the WMM parameter element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_wmm_param_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the Multiple BSSID element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_multi_bssid_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the nonTransmitted BSSID profile sub-element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_sub_non_txed_bssid_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the nonTransmitted BSSID capability element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_non_txed_bssid_capa_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the nonTransmitted BSSID index element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_multi_bssid_index_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the HT capability element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_ht_capa_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the VHT capability element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_vht_capa_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the HE capability element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_he_capa_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the HT operation element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_ht_oper_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the VHT operation element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_vht_oper_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the HE operation element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] ie_len      Length of the information part of the element
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_he_oper_find(PTR2UINT buffer, uint16_t buflen, uint8_t *ie_len);

/**
 ****************************************************************************************
 * @brief Search for the MU EDCA element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_mu_edca_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the UORA element in a frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 *
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_uora_find(PTR2UINT buffer, uint16_t buflen);

/**
 ****************************************************************************************
 * @brief Search for the extended capabilities in the frame buffer
 *
 * @param[in] buffer       Address of the frame buffer
 * @param[in] buflen       Buffer length
 * @param[out] len         Length of the complete element
 * @return The address of the element if it is present and has a valid length, 0
 *         otherwise
 ****************************************************************************************
 */
PTR2UINT mac_ie_ext_cap_find(PTR2UINT buffer, uint16_t buflen, uint16_t *length);

PTR2UINT mac_vsie_find(PTR2UINT addr, uint16_t buflen, uint8_t const *oui,
                            uint8_t ouilen, uint16_t *len);


#endif // _MAC_IE_H_
