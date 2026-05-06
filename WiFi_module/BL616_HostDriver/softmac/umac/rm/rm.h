#ifndef _RM_H_
#define _RM_H_

#if NX_RM
#include "mac_types.h"
#include "mac_frame.h"
#include "rm_task.h"
#include "rxu_task.h"
#include "txl_frame.h"

/// Maximum number of measures that can be saved
#define RM_MAX_MEASURES 12

/// Size allocated for each measure to save beacon/probe response payload
#define RM_MEASURE_PAYLOAD_SIZE 464

/// Maximum number of requests that can be processed at once
#define RM_MAX_REQUEST 1


/// Radio Measurement Beacon measure
struct rm_beacon_measure
{
    /// List element for rm_env.report_list
    struct co_list_hdr report;
    /// Measure status
    uint16_t status;
    /// BSSID requested
    struct mac_addr bssid;
    /// Lower part of TSF when measure has been received
    uint32_t tsflo;
    /// Channel number
    uint8_t chan;
    /// Antenna set used to receive the measure
    uint8_t antenna_set;
    /// Phy type of the AP that send the beacon/probe resp
    uint8_t phy_type;
    /// Global Operating class of the AP that send the beacon/probe resp
    uint8_t op_class;
    /// RSSI of the measure
    int8_t rssi;
    /// offset for SSID element within payload (0 means not present)
    uint8_t ssid_oft;
    /// offset for DS element within payload (0 means not present)
    uint8_t ds_oft;
    /// Next body Fragment ID
    uint8_t frag_id;
    /// Size of the measure payload already reported
    uint16_t reported_len;
    /// length in bytes of the payload
    uint16_t len;
    /// Beacon/probe response payload
    uint32_t payload[RM_MEASURE_PAYLOAD_SIZE/4];
};

/// Radio Measurement Beacon request
struct rm_beacon_request
{
    /// Token from the Radio Measure Action frame
    uint8_t dialog_token;
    /// Token from the Measurement Request element
    uint8_t req_token;
    /// Type of request
    uint8_t type;
    /// Beacon measure mode (@ref mac_measure_req_bcn_mode)
    uint8_t measure_mode;
    /// Beacon report requested detail
    uint8_t detail;
    /// Number of valid elements in req_elt table
    int req_elt_cnt;
    /// Table of requested element ID
    uint8_t req_elt[8];
    /// Number of valid elements in req_ext_elt table
    int req_ext_elt_cnt;
    /// Table of requested extended element ID
    uint8_t req_ext_elt[8];
    /// Whether report should include Last Report indication sub element
    bool last_report_ind;
    /// Local TSF when measure start
    uint64_t start_time;
    /// Measure duration in TU
    uint16_t duration;
    /// Whether bssid is wildcard
    bool wildcard_bssid;
    /// BSSID  requested
    struct mac_addr bssid;
    /// SSID requested (length==0 means no SSID specified)
    struct mac_ssid ssid;
    /// Number of reports already sent for this request
    int report_sent;
    /// Current chan being processed
    int chan_idx;
    /// Band for channel list (active/passive mode only)
    uint8_t band_scan;
    /// Number of channel valid in chan_list
    uint8_t chan_cnt;
    /// List of channel to scan (active/passive mode only)
#if CFG_5G
    uint8_t chan_list[MAC_DOMAINCHANNEL_5G_MAX];
#else
    uint8_t chan_list[MAC_DOMAINCHANNEL_24G_MAX];
#endif
};

/// Radio Measurement environment
struct rm_env_tag
{
    /// Table of measures
    struct rm_beacon_measure measures[RM_MAX_MEASURES];
    /// Table of requests
    struct rm_beacon_request requests[RM_MAX_REQUEST];
    /// List of measures to include in reports for current request
    struct co_list report_list;
    /// Id of the requests currently being processed (-1 if no request is being processed)
    int active_request;
    /// Number of valid requests in requests table
    int request_cnt;
    /// STA idx for the current requests
    uint8_t sta_idx;
    /// VIF idx for the current requests
    uint8_t vif_idx;
    // us in unit
    uint64_t tsf;
    uint64_t jiffies;
    struct txl_frame_desc_tag * tx_frame;
};

extern struct rm_env_tag rm_env;

/**
 ****************************************************************************************
 * @brief Initialize RM module
 ****************************************************************************************
 */
void rm_init(void);

/**
 ****************************************************************************************
 * @brief Sends an Measurement Report action frame to reject the request
 *
 * @param[in] vif_idx       Index of the VIF to send the report from
 * @param[in] sta_idx       Index of the STA to send the report to
 * @param[in] dialog_token  Dialog token of action frame
 * @param[in] mode          Reject reason to include in the report
 * @param[in] type          Type of the rejected request
 ****************************************************************************************
 */
void rm_reject_request(uint8_t vif_idx, uint8_t sta_idx, 
                             uint8_t dialog_token, uint8_t mode, uint8_t type);

/**
 ****************************************************************************************
 * @brief Initializes RM request from Measurement request elements
 *
 * Initializes internal request structures from list of Measurement Request elements
 * extracted form Radio Measurement action frame.
 * If invalid data is found in one of the request then the action is rejected.
 *
 * @param[in] vif_idx       Index of the vif that received the request
 * @param[in] sta_idx       Index of the STA that send the request
 * @param[in] dialog_token  Dialog token of action frame that contains the requests
 * @param[in] req           Table of Measurement element address (HW address)
 * @param[in] req_cnt       Number of element in @p req
 ****************************************************************************************
 */
void rm_initialize_requests(uint8_t vif_idx, uint8_t sta_idx,
                              uint8_t dialog_token, PTR2UINT *req, int req_cnt);

/**
 ****************************************************************************************
 * @brief Schedules processing of next RM request.
 *
 * If this function is called when all request have been processed then it simply puts
 * the RM task in @ref RM_IDLE state and exit.
 * Otherwise it sends the message @ref RM_PROCESS_NEXT_REQUEST_IND to the RM task.
 * This allows to asynchronously start process of request from a function.
 ****************************************************************************************
 */
void rm_schedule_next_request(void);

/**
 ****************************************************************************************
 * @brief Starts processing the current active request.
 ****************************************************************************************
 */
void rm_start_active_request(void);

/**
 ****************************************************************************************
 * @brief Continues processing of the current active request.
 ****************************************************************************************
 */
void rm_continue_active_request(void);

/**
 ****************************************************************************************
 * @brief Saves new beacon/probe response info to include in beacon Radio Measurement
 * report
 *
 * @param[in] bcn          Beacon (or Probe resp) frame (CPU address)
 * @param[in] bcn_length   Length, in bytes, of the frame
 * @param[in] tsflo        Lower part of the TSF at frame reception
 * @param[in] freq         PHY primary frequency during reception
 * @param[in] band         PHY Band during reception
 * @param[in] rssi         RSSI of the frame
 * @param[in] antenna_set  Antenna set used to receive the frame
 ****************************************************************************************
 */
void rm_new_beacon_measure(struct bcn_frame const *bcn, 
                    uint32_t bcn_length, uint32_t tsflo, uint16_t freq,
                    enum mac_chan_band band, int8_t rssi, uint8_t antenna_set);

void rm_reset_measures(bool invalid);
void rm_init_measure_to_report(void);

#endif

#endif // _RM_H_
