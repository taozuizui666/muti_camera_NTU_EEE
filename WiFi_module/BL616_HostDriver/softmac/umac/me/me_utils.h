#ifndef _ME_UTILS_H_
#define _ME_UTILS_H_

#include "hal_mac_desc.h"

#include "ke_config.h"
#include "bl_lmac_mac.h"
#include "me.h"

/**
 ****************************************************************************************
 * @brief Convert a PHY channel type to a MAC HW BW definition
 *
 * @param[in]  phy_bw  PHY channel type
 *
 * @return The corresponding MAC HW BW value
 *
 ****************************************************************************************
 */
__INLINE uint8_t me_phy2mac_bw(uint8_t phy_bw)
{
    return((phy_bw == PHY_CHNL_BW_80P80) ? BW_160MHZ : phy_bw);
}

/**
 ****************************************************************************************
 * @brief Gets the maximum MCS supported (11ac station)
 * @param[in]  mcs_map Bitmap of supported MCSs
 * @return The maximum MCS
 ****************************************************************************************
 */
uint8_t me_11ac_mcs_max(uint16_t mcs_map);

/**
 ****************************************************************************************
 * @brief Gets the maximum MCS supported (11ax station)
 * @param[in]  mcs_map Bitmap of supported MCSs
 * @return The maximum MCS
 ****************************************************************************************
 */
uint8_t me_11ax_mcs_max(uint16_t mcs_map);

/**
 ****************************************************************************************
 * @brief Gets the maximum number of spatial streams supported (11ac station)
 * @param[in]  mcs_map Bitmap of supported MCSs
 * @return The maximum number of spatial streams
 ****************************************************************************************
 */
uint8_t me_11ac_nss_max(uint16_t mcs_map);

/**
 ****************************************************************************************
 * @brief Gets the maximum number of spatial streams supported (11n station)
 * @param[in]  mcs_set Pointer to the Supported MCS Set array
 * @return The RX maximum number of spatial streams
 ****************************************************************************************
 */
uint8_t me_11n_nss_max(uint8_t *mcs_set);

uint32_t me_build_bss_color_reg(struct me_bss_info *bss);

uint16_t me_build_capability(uint8_t vif_idx);

/**
 ****************************************************************************************
 * @brief Compute the A-MPDU parameters of a peer STA (maximum A-MPDU length, minimum
 * spacing), based on the HT, VHT and HE capabilities.
 * @param[in] ht_cap Pointer to the HT capabilities (NULL if not supported)
 * @param[in] vht_cap Pointer to the VHT capabilities (NULL if not supported)
 * @param[in] he_cap Pointer to the HE capabilities (NULL if not supported)
 * @param[out] ampdu_size_max_ht Maximum A-MPDU size in HT mode
 * @param[out] ampdu_size_max_vht Maximum A-MPDU size in VHT mode
 * @param[out] ampdu_size_max_he Maximum A-MPDU size in HE mode
 * @param[out] ampdu_spacing_min Minimum MPDU start spacing
 ****************************************************************************************
 */
void me_get_ampdu_params(struct mac_htcapability const *ht_cap,
                                    struct mac_vhtcapability const *vht_cap,
                                    struct mac_hecapability const *he_cap,
                                    uint16_t *ampdu_size_max_ht,
                                    uint32_t *ampdu_size_max_vht,
                                    uint32_t *ampdu_size_max_he,
                                    uint8_t *ampdu_spacing_min);

/**
 ****************************************************************************************
 * @brief Build the legacy rate bitfield according to the rate set passed as parameter.
 * Include only basic rates or not depending on the corresponding parameter.
 *
 * @param[in]  rateset     The rate set containing the legacy rates
 * @param[in]  basic_only  If this parameter is true, then only basic rates are extracted
 *                         from the rate set
 *
 * @return The legacy rate bitfield
 *
 ****************************************************************************************
 */
uint16_t me_legacy_rate_bitfield_build(struct mac_rateset const *rateset,
                                               bool basic_only);

/**
 ****************************************************************************************
 * @brief Check content of the HT and VHT operation element passed as parameters and
 * update the bandwidth and channel information of the BSS accordingly.
 *
 * @param[in] ht_op_addr Address of the HT operation element to check (0 if not present)
 * @param[in] vht_op_addr Address of the VHT operation element to check (0 if not present)
 * @param[in] bss Pointer to the BSS information structure
 *
 ****************************************************************************************
 */
void me_bw_check(PTR2UINT ht_op_addr, PTR2UINT vht_op_addr, 
                        struct me_bss_info *bss);

/**
 ****************************************************************************************
 * @brief Update chan bandwidth and center freqs from channel width, channel center
 * frequency segment0 and channel center frequency segment1
 *
 * This is the information found on VHT operation and Wide Bandwidth Channel Switch
 * elements.
 * If the new bandwidth is not supported by the PHY (e.g. 160Mhz or 80+80MHz) then the
 * bandwidth is limited to the 'primary' 80MHZ channel.
 *
 * @param[in]     width    Channel Width
 * @param[in]     center0  Channel Center Frequency Segment 0
 * @param[in]     center1  Channel Center Frequency Segment 1
 * @param[in,out] chan     Channel description to update. 'band' and 'prim20_freq' fields
 *                         must be valid when calling this function
 ****************************************************************************************
 */
void me_vht_bandwidth_parse(uint8_t width, uint8_t center0, uint8_t center1,
                                      struct mac_chan_op *chan);
                            

#endif  // _ME_UTILS_H_

