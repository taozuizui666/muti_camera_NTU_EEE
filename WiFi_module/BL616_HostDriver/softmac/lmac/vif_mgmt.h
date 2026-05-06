#ifndef __VIF_MGMT_H__
#define __VIF_MGMT_H__

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "co_list.h"

#include "mac.h"
#include "me.h"

#define INVALID_VIF_IDX 0xFF

struct me_bss_info;

/// VIF Info Table
struct softmac_vif_info_tag
{
    /// linked list header
    struct co_list_hdr list_hdr;
    /// Bitfield indicating if this VIF currently allows sleep or not

    struct mac_addr mac_addr;
    /// Type of the interface (@ref VIF_STA, @ref VIF_IBSS, @ref VIF_MESH_POINT or @ref VIF_AP)
    uint8_t type;
    /// Index of the interface
    uint8_t index;
    /// Flag indicating if the VIF is active or not
    bool active;
    uint32_t flags;

    bool p2p;

    union
    {
        /// STA specific parameter structure
        struct
        {
            /// Index of the station being the peer AP
            uint8_t ap_id;
        } sta;
        /// AP specific parameter structure
        struct
        {
        } ap;
    } u;    ///< Union of AP/STA specific parameter structures

    uint8_t chan_ctxt_idx;
    /// Network BSSID.
    struct mac_addr bssid;
    
    #if NX_UMAC_PRESENT
    /// Information about the BSS linked to this VIF
    struct me_bss_info bss_info;
    #endif
};

#define CHAN_CTXT_UNUSED       (0xFF)

extern struct softmac_vif_info_tag vif_info_tab[NX_VIRT_DEV_MAX];

#endif

