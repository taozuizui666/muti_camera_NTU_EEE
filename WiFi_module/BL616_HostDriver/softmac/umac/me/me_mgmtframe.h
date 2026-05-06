#ifndef _ME_MGMTFRAME_H_
#define _ME_MGMTFRAME_H_

#include "co_utils.h"

#include "me.h"
#include "mac.h"
#include "vif_mgmt.h"

#include "bl_lmac_mac.h"

//forward declarations
struct mac_frame;
struct mac_addr;
struct mac_rateset;
struct mac_htcapability;
struct mac_htoprnelmt;
struct me_bss_info;
struct sm_connect_req;
struct mac_chan_op;

#define NXMAC_EDCA_AC_0_RESET  0x00000A47
#define NXMAC_EDCA_AC_1_RESET  0x00000A43
#define NXMAC_EDCA_AC_2_RESET  0x0005E432
#define NXMAC_EDCA_AC_3_RESET  0x0002F322

/// Channel Band
enum phy_mac_chan_band
{
    /// 2.4GHz Band
    E_PHY_BAND_2G4,
    /// 5GHz band
    E_PHY_BAND_5G,
    /// Number of bands
    E_PHY_BAND_MAX,
};
/// 5G lower bound freq
#define PHY_FREQ_5G 5000

/**
 ****************************************************************************************
 * @brief Compute the channel number from its center frequency and its band
 * @param[in] band  RF band (must be @ref E_PHY_BAND_2G4 or @ref E_PHY_BAND_5G)
 * @param[in] freq  Center frequency of the channel
 *
 * @return The channel number, 0 if not found
 ****************************************************************************************
 */
__INLINE int phy_freq_to_channel(uint8_t band, uint16_t freq)
{
    if ((band == E_PHY_BAND_2G4) && (freq >= 2412) && (freq <= 2484))
    {
        if (freq == 2484)
            return 14;
        else
            return (freq - 2407) / 5;
    }
    else if ((band == E_PHY_BAND_5G) && (freq >= 5005) && (freq <= 5885))
    {
        return (freq - PHY_FREQ_5G) / 5;
    }

    return 0;
}

/**
 ****************************************************************************************
 * @brief Compute the channel frequency from its number and band
 * @param[in] band    RF band (must be @ref E_PHY_BAND_2G4 or @ref E_PHY_BAND_5G)
 * @param[in] channel Channel number
 *
 * @return The channel frequency, 0 if not found
 ****************************************************************************************
 */
__INLINE uint16_t phy_channel_to_freq(uint8_t band, int channel)
{
    if ((band == E_PHY_BAND_2G4) && (channel >= 1) && (channel <= 14))
    {
        if (channel == 14)
            return 2484;
        else
            return 2407 + channel * 5;
    }
    else if ((band == E_PHY_BAND_5G) && (channel >= 1) && (channel <= 177))
    {
        return PHY_FREQ_5G + channel * 5;
    }

    return 0;
}

/**
 ****************************************************************************************
 * @brief Add SSID Information Element in a frame.
 *          Byte 0: Element ID
 *          Byte 1: Length
 *          Byte 2-variable: SSID
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 * @param[in] ssid_len        Length of the SSID
 * @param[in] ssid            Pointer to the SSID value in the memory
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_ssid(PTR2UINT *frame_addr, uint8_t ssid_len, uint8_t *ssid);

/**
 ****************************************************************************************
 * @brief Add Supported Rate Information Element in a frame.
 *          Byte 0: Element ID
 *          Byte 1: Length
 *          Byte 2-variable: Rates
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 * @param[in] rateset         Supported Rates. If number of rates is higher than 8, rates from
 *                            position 8 will have to be part of a Extended Supported Rate IE.
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_supp_rates(PTR2UINT *frame_addr, 
                                       struct mac_rateset *rateset);

/**
 ****************************************************************************************
 * @brief Add Extended Supported Rate Information Element in a frame.
 *          Byte 0: Element ID
 *          Byte 1: Length
 *          Byte 2-variable: Rates
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 * @param[in] rateset         Supported Rates. Number of rates shall be higher than 8 when entering
 *                            in this function.
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_ext_supp_rates(PTR2UINT *frame_addr, 
                                             struct mac_rateset *rateset);

#if (RW_MESH_EN)
/**
 ****************************************************************************************
 * @brief Add TIM (Traffic Indication Map) Information Element in a frame.
 *          Byte 0: Element ID
 *          Byte 1: Length
 *          Byte 2: DTIM Count (updated by mm_bcn module before beacon transmission)
 *          Byte 3: DTIM Period (read and used by mm_bcn module)
 *          Byte 4: Bitmap Control (updated by mm_bcn module)
 *          Byte 5: Partial Virtual Bitmap (updated by mm_bcn module)
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 * @param[in] dtim_period     DTIM Period
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_tim(uint32_t *frame_addr, uint8_t dtim_period);

/**
 ****************************************************************************************
 * @brief Add DS Parameter Information Element in a frame.
 *          Byte 0: Element ID
 *          Byte 1: Length
 *          Byte 2: Channel
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 * @param[in] chan            Current Channel
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_dsss_param(uint32_t *frame_addr, uint8_t chan);
#endif //(RW_MESH_EN)

/**
 ****************************************************************************************
 * @brief Add HT Capabilities Information Element in a frame.
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_ht_capa(PTR2UINT *frame_addr);

#if (RW_MESH_EN)
/**
 ****************************************************************************************
 * @brief Add HT Operation Information Element in a frame.
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 * @param[in] vif             Pointer to the structure containing information about the VIF for which
 *                            the Information Element has to be filled.
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
//uint32_t me_add_ie_ht_oper(uint32_t *frame_addr, struct softmac_vif_info_tag *vif);
#endif //(RW_MESH_EN)

#if (NX_VHT)
/**
 ****************************************************************************************
 * @brief Add VHT Capabilities Information Element in a frame.
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_vht_capa(PTR2UINT *frame_addr);

#if (RW_MESH_EN)
/**
 ****************************************************************************************
 * @brief Add VHT Operation Information Element in a frame.
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 * @param[in] vif             Pointer to the structure containing information about the VIF for which
 *                            the Information Element has to be filled.
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
//uint32_t me_add_ie_vht_oper(uint32_t *frame_addr, struct softmac_vif_info_tag *vif);
#endif //(RW_MESH_EN)

/**
 ****************************************************************************************
 * @brief Add Operating Mode Notification Element in a frame.
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 * @param[in] nss             Number of spatial stream we are limited to (should be NSS - 1).
 * @param[in] bw              Bandwidth we are limited to (0: 20M, 1: 40M, 2: 80M)
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_op_mode(PTR2UINT *frame_addr, uint8_t nss, uint8_t bw);
#endif //(NX_VHT)

/**
 ****************************************************************************************
 * @brief Add Radio Measurement Eanbled Capability element in a frame.
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When
 *                            leaving the function, the pointer value matches with the
 *                            new end of the frame.
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_rm_enabled_capa(PTR2UINT *frame_addr);

#if NX_HE
/**
 ****************************************************************************************
 * @brief Add HE Capabilities Information Element in a frame.
 *
 * @param[in,out] frame_addr  Pointer to the address at which IE has to be added. When leaving
 *                            the function, the pointer value matches with the new end of the frame.
 *
 * @return Number of bytes that have been added to the provided frame.
 ****************************************************************************************
 */
uint32_t me_add_ie_he_capa(PTR2UINT *frame_addr);
#endif

/**
 ****************************************************************************************
 * @brief Build an Authentication frame at the AP and the STA
 *
 * @param[in]  frame                    Address of the payload
 * @param[in]  algo_type                indicates a single authentication algorithm.
 * @param[in]  seq_nbr                  indicates the current state of progress through a
 *                                      multi-step transaction
 * @param[in]  status_code              indicate the success or failure of a req operation
 * @param[in]  challenge_array_ptr      pointer to the challenge_array
 *
 * @return     The length of the built frame
 *
 ****************************************************************************************
 */
uint16_t me_build_authenticate(PTR2UINT frame, uint16_t algo_type,
                                      uint16_t seq_nbr, uint16_t status_code,
                                      uint32_t *challenge_array_ptr);

/**
 ****************************************************************************************
 * @brief Build a De authentication frame at the AP and the STA
 *
 * @param[in]  frame              Address of the mac frame
 * @param[in]  reason_code        Used to indicate reason for generating Deauthentication
 *
 * @return     The length of the built frame
 *
 ****************************************************************************************
 */
uint16_t me_build_deauthenticate(PTR2UINT frame, uint16_t reason_code);

// Functions used in the SM
/**
 ****************************************************************************************
 * @brief used to build the Associated REQ frame at STA
 *
 * @param[in]  frame              Address of the mac frame
 * @param[in]  bss                Pointer to the BSS information structure
 * @param[in]  old_ap_addr_ptr    Pointer to the old AP address
 * @param[in]  vif_idx            Index to VIF instance
 * @param[out] ie_addr            Pointer to the address of the IEs
 * @param[out] ie_len             Pointer to the length of the IEs
 * @param[in]  con_par            Pointer to the SM connection parameter structure
 *
 * @return     The length of the built frame
 ****************************************************************************************
 */
uint16_t me_build_associate_req(PTR2UINT frame, struct me_bss_info *bss,
                                        struct mac_addr *old_ap_addr_ptr,
                                        uint8_t vif_idx, PTR2UINT *ie_addr,
                                        uint16_t *ie_len,
                                        struct sm_connect_req const *con_par);

/**
 ****************************************************************************************
 * @brief This function is called to extract the rateset from a management frame
 *
 * @param[in] buffer             Pointer to the frame buffer
 * @param[in] buflen             Frame length
 * @param[out] mac_rate_set_ptr  Found rates
 ****************************************************************************************
 */
void me_extract_rate_set(PTR2UINT buffer, uint16_t buflen,
                                struct mac_rateset* mac_rate_set_ptr);


/**
 ****************************************************************************************
 * @brief Extract local power constraint from Power Constraint IE
 *
 * @param[in] buffer    Pointer to the Beacon/ProbeRsp frame buffer
 * @param[in] buflen    Beacon/ProbeRsp frame length
 * @param[out] bss      Pointer to the bss info structure in which power_constraint will
 *                      be updated
 *
 ****************************************************************************************
 */
void me_extract_power_constraint(PTR2UINT buffer, uint16_t buflen,
                                           struct me_bss_info *bss);

/**
 ****************************************************************************************
 * @brief Update tx_power of bss channel from Country IE
 *
 * If country IE is found, update tx power of the bss channel with the tx power set
 * in country IE.
 * Note: Other channel tx power are not updated (assume driver will provide updated
 * channel description after association)
 *
 * @param[in] buffer    Pointer to the Beacon/ProbeRsp frame buffer
 * @param[in] buflen    Beacon/ProbeRsp frame length
 * @param[out] bss      Pointer to the bss info structure
 *
 ****************************************************************************************
 */
void me_extract_country_reg(PTR2UINT buffer, uint16_t buflen,
                                    struct me_bss_info *bss);

/**
 ****************************************************************************************
 * @brief Extract Mobility Domain IE
 *
 * @param[in] buffer    Pointer on beacon/probe response management frame buffer
 * @param[in] buflen    beacon/probe response management frame length
 * @param[out] bss      pointer to the bss info structure
 *
 ****************************************************************************************
 */
void me_extract_mobility_domain(PTR2UINT buffer, uint16_t buflen,
                                          struct me_bss_info *bss);

/**
 ****************************************************************************************
 * @brief Extract Channel Switch Announcement (csa) information from beacon
 *
 * If CSA (or ECSA) IE is detected in the beacon, this function will extract target
 * channel configuration, csa counter and csa mode.
 *
 * @param[in] buffer     Pointer to the Beacon frame buffer
 * @param[in] buflen     Beacon frame length
 * @param[out] mode      Indicates whether transmission must be paused or not until channel switch
 * @param[out] chan_desc Configuration of target channel
 *
 * @return The CSA counter (i.e. number of beacon before actual channel switch).
 *         0 means no CSA detected or CSA elements not valid.
 *
 ****************************************************************************************
 */
//int me_extract_csa(PTR2UINT buffer, uint16_t buflen, uint8_t *mode,
//                         struct mac_chan_op *chan_desc);

/**
 ****************************************************************************************
 * @brief Extract EDCA parameters (i.e. WMMIE) from Beacon or ProbeRsp
 * If no WMMIE is present in the buffer, default values are set to the EDCA parameters.
 *
 * @param[in] buffer       Pointer to the Beacon/ProbeRsp frame buffer
 * @param[in] buflen       Beacon/ProbeRsp frame length
 * @param[in,out] edca_param Current EDCA parameter structure stored
 * @param[out] changed Flag indicating whether the element changed since last time we got
 * it
 *
 * @return true if the WMMIE is present and valid, false otherwise
 ****************************************************************************************
 */
bool me_extract_edca_params(PTR2UINT buffer, uint16_t buflen,
                            struct mac_edca_param_set *edca_param,
                            bool *changed);

/**
 ****************************************************************************************
 * @brief Extract HT capability element from Beacon or ProbeRsp
 *
 * @param[in] buffer       Pointer to the Beacon/ProbeRsp frame buffer
 * @param[in] buflen       Beacon/ProbeRsp frame length
 * @param[out] ht_cap      HT capability structure
 *
 * @return true if the HT capability element is present and valid, false otherwise
 ****************************************************************************************
 */
bool me_extract_ht_capa(PTR2UINT PTR2UINT, uint16_t buflen,
                        struct mac_htcapability *ht_cap);

/**
 ****************************************************************************************
 * @brief Extract VHT capability element from Beacon or ProbeRsp
 *
 * @param[in] buffer       Pointer to the Beacon/ProbeRsp frame buffer
 * @param[in] buflen       Beacon/ProbeRsp frame length
 * @param[out] vht_cap     VHT capability structure
 *
 * @return true if the VHT capability element is present and valid, false otherwise
 ****************************************************************************************
 */
bool me_extract_vht_capa(PTR2UINT buffer, uint16_t buflen,
                                 struct mac_vhtcapability *vht_cap);

#if NX_HE
/**
 ****************************************************************************************
 * @brief Extract HE capability element from Beacon or ProbeRsp
 *
 * @param[in] buffer       Pointer to the Beacon/ProbeRsp frame buffer
 * @param[in] buflen       Beacon/ProbeRsp frame length
 * @param[out] he_cap      HE capability structure
 *
 * @return true if the HE capability element is present and valid, false otherwise
 ****************************************************************************************
 */
bool me_extract_he_capa(PTR2UINT buffer, uint16_t buflen,
                                struct mac_hecapability *he_cap);

/**
 ****************************************************************************************
 * @brief Extract HE operation element from Beacon or ProbeRsp
 *
 * If the element is found, the BSS structure is updated with some fields (BSS color,
 * RTS threshold, etc.) retrieved from the element.
 *
 * @param[in] buffer    Pointer to the Beacon/ProbeRsp frame buffer
 * @param[in] buflen    Beacon/ProbeRsp frame length
 * @param[out] bss      Pointer to the bss info structure
 *
 * @return true if the HE operation element is present and valid, false otherwise
 ****************************************************************************************
 */
bool me_extract_he_oper(PTR2UINT buffer, uint16_t buflen,
                                struct me_bss_info *bss);

/**
 ****************************************************************************************
 * @brief Extract MU EDCA element from Beacon or ProbeRsp
 *
 * @param[in] buffer             Pointer to the Beacon/ProbeRsp frame buffer
 * @param[in] buflen             Beacon/ProbeRsp frame length
 * @param[in,out] mu_edca_param  Current MU EDCA parameter structure stored
 * @param[out] changed           Flag indicating whether the element changed since last
 *                               time we got it
 *
 * @return true if the MU EDCA element is present and valid, false otherwise
 ****************************************************************************************
 */
bool me_extract_mu_edca_params(PTR2UINT buffer, uint16_t buflen,
                               struct mac_mu_edca_param_set *mu_edca_param,
                               bool *changed);

/**
 ****************************************************************************************
 * @brief Extract UORA element from Beacon or ProbeRsp
 *
 * @param[in] buffer         Pointer to the Beacon/ProbeRsp frame buffer
 * @param[in] buflen         Beacon/ProbeRsp frame length
 * @param[in,out] uora_param Current UORA parameter structure stored
 *
 * @return true if the MU EDCA element is present and valid, false otherwise
 ****************************************************************************************
 */
bool me_extract_uora_params(PTR2UINT buffer, uint16_t buflen,
                                      struct mm_set_uora_req *uora_param);
#endif // NX_HE

/**
 ****************************************************************************************
 * @brief Complete the frame with the FTM parameters
 *
 * @param[in]  frame              Address of the mac frame
 * @param[in]  ftm_per_burst      Number of FTMs per burst
 *
 * @return     The length of the built frame.
 ****************************************************************************************
 */
uint8_t me_ftm_add_parameter(PTR2UINT frame,  uint8_t ftm_per_burst);

#endif  // _ME_MGMTFRAME_H_
