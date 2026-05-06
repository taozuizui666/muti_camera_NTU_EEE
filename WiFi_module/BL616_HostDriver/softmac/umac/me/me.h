#ifndef __ME_H__
#define __ME_H__

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "bl_lmac_mac.h"
#include "bl_lmac_msg.h"

#include "rwnx_config.h"
#include "ke_config.h"
#include "mac.h"
#include "me_task.h"

/*
 * DEFINES
 ****************************************************************************************
 */

// field definitions
/// PARTIAL_BSS_COLOR_EN field bit
#define NXMAC_PARTIAL_BSS_COLOR_EN_BIT    ((uint32_t)0x00000200)
/// PARTIAL_BSS_COLOR_EN field position
#define NXMAC_PARTIAL_BSS_COLOR_EN_POS    9
/// BSS_COLOR_EN field bit
#define NXMAC_BSS_COLOR_EN_BIT            ((uint32_t)0x00000100)
/// BSS_COLOR_EN field position
#define NXMAC_BSS_COLOR_EN_POS            8
/// BSS_COLOR field mask
#define NXMAC_BSS_COLOR_MASK              ((uint32_t)0x0000003F)
/// BSS_COLOR field LSB position
#define NXMAC_BSS_COLOR_LSB               0
/// BSS_COLOR field width
#define NXMAC_BSS_COLOR_WIDTH             ((uint32_t)0x00000006)

/// PARTIAL_BSS_COLOR_EN field reset value
#define NXMAC_PARTIAL_BSS_COLOR_EN_RST    0x0
/// BSS_COLOR_EN field reset value
#define NXMAC_BSS_COLOR_EN_RST            0x0
/// BSS_COLOR field reset value
#define NXMAC_BSS_COLOR_RST               0x0


/// Association response timeout (in us)
#define DEFAULT_ASSOCRSP_TIMEOUT        (1000 * TU_DURATION)
/// Authentication response timeout (in us)
#define DEFAULT_AUTHRSP_TIMEOUT         (1000 * TU_DURATION)

/// Power Save mode setting
enum
{
    /// Power-save off
    PS_MODE_OFF,
    /// Power-save on - Normal mode
    PS_MODE_ON,
    /// Power-save on - Dynamic mode
    PS_MODE_ON_DYN,
};

/// Local capability flags
enum
{
    /// Bit indicating that HT is supported by local device
    ME_HT_CAPA = CO_BIT(0),
    /// Bit indicating that VHT is supported by local device
    ME_VHT_CAPA = CO_BIT(1),
    /// Bit indicating that HE is supported by local device
    ME_HE_CAPA = CO_BIT(2),
    /// Bit indicating that HE OFDMA UL is enabled in the local device
    ME_OFDMA_UL_CAPA = CO_BIT(3),
};

/// BSS capability flags
enum
{
    /// BSS is QoS capable
    BSS_QOS_CAPA = CO_BIT(0),
    /// BSS is HT capable
    BSS_HT_CAPA = CO_BIT(1),
    /// BSS is VHT capable
    BSS_VHT_CAPA = CO_BIT(2),
    /// BSS is HE capable
    BSS_HE_CAPA = CO_BIT(3),
    /// BSS is short preamble capable
    BSS_SHORT_PREAMBLE_CAPA = CO_BIT(4),
    /// BSS is privacy capable
    BSS_PRIVA_CAPA = CO_BIT(5),
    /// Information about the BSS are valid
    BSS_VALID_CAPA = CO_BIT(31),
};

/*
 * STRUCTURES/TYPES DEFINITION
 ****************************************************************************************
 */
/// ME environment structure
struct me_env_tag
{
    /// Bitfield containing the ACTIVE state request of the different VIFs
    uint32_t active_vifs;
    /// Bitfield containing the PS state of the different VIFs
    uint32_t ps_disable_vifs;
    /// TaskID of the task requesting the last command
    ke_task_id_t requester_id;
    /// HT capabilities of the device
    struct mac_htcapability ht_cap;
    /// Lifetime of packets sent under a BlockAck agreement (expressed in TUs)
    uint16_t tx_lft;
    #if NX_VHT
    /// VHT capabilities of the device
    struct mac_vhtcapability vht_cap;
    #endif
    #if NX_HE
    /// HE capabilities of the device
    struct mac_hecapability he_cap;
    /// Maximum number of Spatial Streams supported for a HE STBC transmission
    uint8_t he_stbc_nss;
    #endif
    /// List of supported channels
    struct me_chan_config_req chan;
    /// Maximum number of Spatial Streams supported for a HT/VHT STBC transmission
    uint8_t stbc_nss;
    /// Maximum supported BW
    uint8_t phy_bw_max;
    /// Local capability flags
    uint8_t capa_flags;
    #if NX_POWERSAVE
    /// Boolean indicating if PS mode shall be enabled or not
    bool ps_on;
    /// Mode that should be used when PS is enabled (@ref PS_MODE_ON or @ref PS_MODE_ON_DYN)
    uint8_t ps_mode;
    #endif
    /// Indicates whether AMSDU shall be forced or not
    enum amsdu_tx amsdu_tx;
    /// Chip version
    uint8_t chip_version;
};

/// Mobility Domain IE
struct mobility_domain
{
    /// Mobility Domain ID
    uint16_t mdid;
    /// Fast Transition capability and policy
    uint8_t ft_capability_policy;
};

/// Information related to the BSS a VIF is linked to
struct me_bss_info
{
    /// HT capabilities of the BSS
    struct mac_htcapability ht_cap;
    #if NX_VHT
    /// VHT capabilities of the BSS
    struct mac_vhtcapability vht_cap;
    #endif
    #if NX_HE
    /// HE capabilities of the BSS
    struct mac_hecapability he_cap;
    /// MU EDCA parameter set
    struct mac_mu_edca_param_set mu_edca_param;
    #endif
    /// Network BSSID.
    struct mac_addr bssid;
    /// Network name.
    struct mac_ssid ssid;
    /// nonTransmitted BSSID index, in case the STA is connected to a
    /// nonTransmitted BSSID of a Multiple BSSID set. Should be set to 0 otherwise
    uint8_t bssid_index;
    /// Maximum BSSID indicator, in case the STA is connected to a nonTransmitted
    /// BSSID of a Multiple BSSID set.
    uint8_t max_bssid_ind;
    /// Network type (IBSS or ESS).
    uint16_t bsstype;
    /// Operating channel.
    struct mac_chan_op chan;
    /// Network beacon period.
    uint16_t beacon_period;
    /// Legacy rate set
    struct mac_rateset rate_set;
    /// EDCA parameter set
    struct mac_edca_param_set edca_param;
    /// RSSI of the beacons received from this BSS
    int8_t rssi;
    /// Highest 11g rate that can be used
    uint8_t high_11b_rate;
    /// Protection Status (Bit field)
    uint16_t prot_status;
    /// Power Constraint
    uint8_t power_constraint;
    #if NX_HE
    /// HE operation parameters (includes BSS color)
    uint32_t he_oper;
    #endif
    /// Flags indicating which BSS capabilities are valid (HT/VHT/QoS/HE/etc.)
    uint32_t capa_flags;
    /// Mobility Domain element
    struct mobility_domain mde;
};

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */
/// ME module environment variable
extern struct me_env_tag me_env;

/*
 * MACROS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * Test whether the specified capability is supported locally
 * @param[in] type Capability type (HT, VHT or HE)
 * @return true if supported, false otherwise
 ****************************************************************************************
 */
#define LOCAL_CAPA(type) ((me_env.capa_flags & ME_##type##_CAPA) != 0)

/**
 ****************************************************************************************
 * Set the specified local capability
 * @param[in] type Capability type (HT, VHT or HE)
 ****************************************************************************************
 */
#define LOCAL_CAPA_SET(type) (me_env.capa_flags |= ME_##type##_CAPA)

/**
 ****************************************************************************************
 * Clear the specified local capability
 * @param[in] type Capability type (HT, VHT or HE)
 ****************************************************************************************
 */
#define LOCAL_CAPA_CLR(type) (me_env.capa_flags &= ~ME_##type##_CAPA)

/**
 ****************************************************************************************
 * Test whether the specified capability is supported by the BSS
 * @param[in] bss Pointer to the BSS information structure
 * @param[in] type Capability type (QOS, HT, VHT or HE)
 * @return true if supported, false otherwise
 ****************************************************************************************
 */
#define BSS_CAPA(bss, type) (((bss)->capa_flags & BSS_##type##_CAPA) != 0)

/**
 ****************************************************************************************
 * Set the specified BSS capability
 * @param[out] bss Pointer to the BSS information structure
 * @param[in] type Capability type (QOS, HT, VHT or HE)
 ****************************************************************************************
 */
#define BSS_CAPA_SET(bss, type) ((bss)->capa_flags |= BSS_##type##_CAPA)

/**
 ****************************************************************************************
 * Clear the specified BSS capability
 * @param[out] bss Pointer to the BSS information structure
 * @param[in] type Capability type (QOS, HT, VHT or HE)
 ****************************************************************************************
 */
#define BSS_CAPA_CLR(bss, type) ((bss)->capa_flags &= ~BSS_##type##_CAPA)

/**
 ****************************************************************************************
 * Reset (i.e. put all 0) the BSS capabilities
 * @param[out] bss Pointer to the BSS information structure
 ****************************************************************************************
 */
#define BSS_CAPA_RESET(bss) ((bss)->capa_flags = 0)

/*
 * FUNCTION PROTOTYPES
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Send the ME_DATA_PATH_FLUSHED_IND message to the appropriate
 * task depending on the type of the VIF the given STA is attached to.
 *
 * @param[in] sta_idx  Index of the STA
 ****************************************************************************************
 */
//void me_send_data_flushed_ind(uint8_t sta_idx);

/**
 ****************************************************************************************
 * @brief This function returns the TX lifetime of packets sent under BlockAck agreement
 *
 * @return The lifetime that was configured by the upper layers
 ****************************************************************************************
 */
__INLINE uint16_t me_tx_lft_get(void)
{
    return me_env.tx_lft;
}

/**
 ****************************************************************************************
 * @brief This function updates the host about the number of credits allocated/deallocated
 * to a peer STA/TID pair.
 *
 * @param[in] sta_idx  Index of the peer device whose credits are updated
 * @param[in] tid      TID
 * @param[in] credits  Number of credits allocated/deallocated
 ****************************************************************************************
 */
__INLINE void me_credits_update_ind(uint8_t sta_idx, uint8_t tid, int8_t credits)
{
    struct me_tx_credits_update_ind *ind = 
                                KE_MSG_ALLOC(ME_TX_CREDITS_UPDATE_IND, TASK_API,
                                             TASK_ME, me_tx_credits_update_ind);

    // Fill-in the parameters
    ind->sta_idx = sta_idx;
    ind->tid = tid;
    ind->credits = credits;

    // Send the message
    ke_msg_send(ind);
}

/**
 ****************************************************************************************
 * @brief Search the channel structure corresponding to the parameters, and returns a
 * a pointer to this structure.
 *
 * @param[in] band PHY band (@ref PHY_BAND_2G4 or @ref PHY_BAND_5G)
 * @param[in] freq Frequency of the channel (in MHz)
 *
 * @return The pointer to the channel structure found if available, NULL otherwise
 ****************************************************************************************
 */
struct mac_chan_def *me_freq_to_chan_def(uint8_t band, uint16_t freq);

/**
 ****************************************************************************************
 * @brief Search the channel structure corresponding to the parameters, and returns a
 * a pointer to this structure.
 *
 * @param[in] band     PHY band (@ref PHY_BAND_2G4 or @ref PHY_BAND_5G)
 * @param[in] chan_id  Channel index within the band
 *
 * @return The pointer to the channel structure found if available, NULL otherwise
 ****************************************************************************************
 */
struct mac_chan_def *me_chan_id_to_chan_def(uint8_t band, uint8_t chan_id);

#endif // _ME_INIT_H_

