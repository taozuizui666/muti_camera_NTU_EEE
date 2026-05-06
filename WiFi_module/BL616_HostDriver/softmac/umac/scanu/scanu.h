#ifndef _SCANU_H_
#define _SCANU_H_

#include "rwnx_config.h"

#include "hal_mac_desc.h"

#include "mac.h"
#include "ke_msg.h"

#include "bl_lmac_msg.h"

/// Maximum length of the additional ProbeReq IEs
//#define SCANU_MAX_IE_LEN  600//200

///Maximum number of scan results that can be stored.
#define SCANU_MAX_RESULTS 64//32

///Maximum number of nonTxed BSSID information we can store for one received beacon
#define SCANU_MAX_NONTXED_BSSID_PER_BEACON 16

struct rxu_mgt_ind;

/// nonTransmitted BSSID information structure
struct scanu_mbssid_profile_tag
{
    /// Address of the SSID element in the Beacon/ProbeRsp frame
    PTR2UINT ssid_ie_addr;
    /// Capability information
    uint16_t capa;
    /// nonTransmitted BSSID index
    uint8_t bssid_index;
};

/// nonTransmitted BSSID database
struct scanu_mbssids_tag
{
    /// Information about each nonTransmitted BSSID
    struct scanu_mbssid_profile_tag bssids[SCANU_MAX_NONTXED_BSSID_PER_BEACON];
    /// Maximum BSSID indicator
    uint8_t max_bssid_ind;
    /// Number of nonTransmitted BSSIDs stored
    uint8_t mbssid_cnt;
};

/// Type of SCANU request
enum scanu_request_type
{
    /// Normal scan request
    SCANU_NO_JOIN,
    /// Internal Scan request prior to connection
    SCANU_JOIN,
};

/** The SCAN environment.
 * This environment can be used by any element that requires the SCAN.
 */
struct scanu_env_tag
{
    /// Parameters of the Scan request
    struct scanu_start_req const *param;
    /// The number of scan results obtained from LMAC
    uint16_t result_cnt;
    /// Array containing the results of the scan
    struct mac_scan_result scan_result[SCANU_MAX_RESULTS];
    /// Task ID of the sender of the scan request.
    ke_task_id_t src_id;
    /// Type of scan request
    enum scanu_request_type req_type;
    /// Band currently scanned
    uint8_t band;
    /// BSSID looked for.
    struct mac_addr bssid;
    /// SSID looked for.
    struct mac_ssid ssid;
    /// Reference BSSID looked for.
    struct mac_addr ref_bssid;
    #if (NX_P2P)
    /// P2P Scan (ssid we are looking for is "DIRECT-")
    bool p2p_scan;
    #endif //(NX_P2P)
    /// Join procedure status (1: success, 0: failure)
    bool join_status;
    /// nonTransmitted BSSID database
    struct scanu_mbssids_tag mbssids;
    #if (FIX_WFA_MBO_5_2_1)
    uint16_t add_ie_len;
    uint8_t add_ies_buf[SCANU_MAX_IE_LEN];
    #endif
};

/// Definition of an additional IE buffer
struct scanu_add_ie_tag
{
    /// Buffer space for IEs
    uint32_t buf[SCANU_MAX_IE_LEN/4];
};

/// Scan time to enable or disable the scan (2sec).
#define SCAN_ENABLE_TIME 2000000

/// SCAN module environment declaration.
extern struct scanu_env_tag scanu_env;

/**
 ****************************************************************************************
 * @brief Initialize the SCAN environment and task.
 *
 ****************************************************************************************
 */
void scanu_init(void);

/**
 ****************************************************************************************
 * @brief Handle the reception of a beacon or probe response frame.
 *
 * It extracts all required information from them and save them in scan result array.
 *
 * @param[in] frame Pointer to the received beacon/probe-response message.
 *
 * @return The message status to be returned to the kernel (@ref KE_MSG_CONSUMED or
 *         @ref KE_MSG_NO_FREE)
 ****************************************************************************************
 */
int scanu_frame_handler(struct rxu_mgt_ind const *frame);

/**
 ****************************************************************************************
 * @brief Look for a given BSS in the scan results and returns its parameters.
 *
 * @param[in] bssid_ptr Pointer to the BSSID looked for.
 * @param[in] allocate Set to true only a result location is required for allocation.
 *
 * @return If the allocation parameter was set, return the pointer to this BSS parameters
 * if the BSS exists or the pointer to the first free BSS entry.  In this case, if return
 * NULL, then no space was found.  If the allocation parameter was not set, return NULL
 * if the BSS was not found.
 ****************************************************************************************
 */
struct mac_scan_result* scanu_find_result(struct mac_addr const *bssid_ptr,
                                               bool allocate);

/**
 ****************************************************************************************
 * @brief Look for a given BSS from its BSSID.
 *
 * @param[in] bssid Pointer to the BSSID looked for.
 *
 * @return If found, return the pointer to the BSS parameters, and NULL otherwise
 ****************************************************************************************
 */
struct mac_scan_result *scanu_search_by_bssid(struct mac_addr const *bssid);

/**
 ****************************************************************************************
 * @brief Look for a given BSS from its SSID.
 * If several entries in the database have the same SSID, then the function returns the
 * one having the highest RSSI.
 *
 * @param[in] ssid Pointer to the SSID looked for.
 *
 * @return If found, return the pointer to the BSS parameters, and NULL otherwise
 ****************************************************************************************
 */
struct mac_scan_result *scanu_search_by_ssid(struct mac_ssid const *ssid);

/**
 ****************************************************************************************
 * @brief Look for a result from its result index.
 *
 * @param[in] result_idx Result index to the scan result looked for.
 *
 * @return If found, return the pointer to the BSS parameters, and NULL otherwise
 ****************************************************************************************
 */
struct mac_scan_result *scanu_get_result_from_idx(uint8_t result_idx);

/**
 ****************************************************************************************
 * @brief Start scanning process.
 ****************************************************************************************
 */
void scanu_start(void);

/**
 ****************************************************************************************
 * @brief Send a scan request to LMAC for the next band to scan
 ****************************************************************************************
 */
void scanu_scan_next(void);

/**
 ****************************************************************************************
 * @brief Send the appropriate confirmation to the source task.
 *
 * @param[in] status  Status to be sent to the source task
 ****************************************************************************************
 */
void scanu_confirm(uint8_t status);

#endif // _SCANU_H_
