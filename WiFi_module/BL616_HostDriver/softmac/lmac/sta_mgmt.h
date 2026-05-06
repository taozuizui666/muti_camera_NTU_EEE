#ifndef __STA_MGMT_H__
#define __STA_MGMT_H__


/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "rwnx_config.h"

// for linked list definitions
#include "co_list.h"
// for mac_addr and other structure definitions
#include "mac.h"

#define VIF_TO_BCMC_IDX(idx) (BROADCAST_STA_IDX_MIN + (idx))

struct softmac_sta_info_tag {
    struct mac_addr mac_addr;
    uint8_t inst_nbr;
};

/// Station indexes.
enum
{
    #if NX_UMAC_PRESENT
    /// BROADCAST/GROUP DATA TX STA Index for first virtual AP.
    BROADCAST_STA_IDX_MIN = NX_REMOTE_STA_MAX,
    /// BROADCAST/GROUP DATA TX STA Index for last virtual AP.
    BROADCAST_STA_IDX_MAX = NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX - 1,
    /// Maximum number of supported STA
    STA_MAX,
    #else
    STA_MAX = NX_REMOTE_STA_MAX,
    #endif
    /// Invalid STA Index used for error checking.
    INVALID_STA_IDX = 0xFF
};


/// Station capability information
struct sta_capa_info
{
    /// Legacy rate set supported by the STA
    struct mac_rateset rate_set;
    /// HT capabilities
    struct mac_htcapability ht_cap;
    #if NX_VHT
    /// VHT capabilities
    struct mac_vhtcapability vht_cap;
    #endif
    #if NX_HE
    /// HE capabilities
    struct mac_hecapability he_cap;
    #endif
    /// Maximum PHY channel bandwidth supported by the STA
    uint8_t phy_bw_max;
    /// Current channel bandwidth for the STA
    uint8_t bw_cur;
    /// Bit field indicating which queues have to be delivered upon U-APSD trigger
    uint8_t uapsd_queues;
    /// Maximum size, in frames, of a APSD service period
    uint8_t max_sp_len;
    /// Maximum number of spatial streams supported for STBC reception
    uint8_t stbc_nss;
};


/**
 ****************************************************************************************
 * Test whether the specified capability is supported by the STA
 * @param[in] sta Pointer to the STA information structure
 * @param[in] type Capability type (QOS, MFP, HT, VHT or HE)
 * @return true if supported, false otherwise
 ****************************************************************************************
 */
#define STA_CAPA(sta, type) (((sta)->capa_flags & STA_##type##_CAPA) != 0)

/**
 ****************************************************************************************
 * Set the specified STA capability
 * @param[out] sta Pointer to the STA information structure
 * @param[in] type Capability type (QOS, HT, VHT or HE)
 ****************************************************************************************
 */
#define STA_CAPA_SET(sta, type) ((sta)->capa_flags |= STA_##type##_CAPA)

/**
 ****************************************************************************************
 * Clear the specified STA capability
 * @param[out] sta Pointer to the STA information structure
 * @param[in] type Capability type (QOS, HT, VHT or HE)
 ****************************************************************************************
 */
#define STA_CAPA_CLR(sta, type) ((sta)->capa_flags &= ~STA_##type##_CAPA)


/// logical port state
enum
{
    /// no data traffic could be exchanged with this station
    PORT_CLOSED = 0,
    /// encryption key is not yet available, only EAP frames could be sent
    PORT_CONTROLED,
    /// any data types could be sent
    PORT_OPEN
};


extern struct softmac_sta_info_tag sta_info_tab[STA_MAX];

#endif
