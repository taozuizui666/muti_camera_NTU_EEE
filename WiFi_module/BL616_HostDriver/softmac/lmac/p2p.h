#ifndef _P2P_H_
#define _P2P_H_

/*
 * DEFINES
 ****************************************************************************************
 */

/**
 * @name P2P IE format
 * See wifi_p2p_technical_specification_v1.1.pdf:
 * - 4.1.1 - P2P IE Format Table 4
 */
#define P2P_IE_ID_OFT       (0)
#define P2P_IE_LEN_OFT      (1)
#define P2P_IE_OUI_OFT      (2)
#define P2P_IE_OUI_TYPE_OFT (5)
#define P2P_IE_ATT_OFT      (6)

/// P2P Element ID (Vendor Specific)
#define P2P_ELMT_ID                 (MAC_ELTID_OUI)
/// OUI Value - Wi-FiAll
#define P2P_OUI_WIFI_ALL_BYTE0      (0x50)
#define P2P_OUI_WIFI_ALL_BYTE1      (0x6F)
#define P2P_OUI_WIFI_ALL_BYTE2      (0x9A)
/// Vendor Specific OUI Type for P2P
#define P2P_OUI_TYPE_P2P            (9)

/**
 * @name P2P Attribute format
 * See wifi_p2p_technical_specification_v1.1.pdf.
 * - 4.1.1 - General format of P2P attribute Table 5
 */
#define P2P_ATT_ID_OFT              (0)
#define P2P_ATT_LEN_OFT             (1)
#define P2P_ATT_BODY_OFT            (3)

/**
 * @name Notice of Absence attribute format
 * See wifi_p2p_technical_specification_v1.1.pdf.
 * - 4.1.14 - Notice of Absence attribute Tables 26-27
 */
#define P2P_NOA_ATT_INDEX_OFT        P2P_ATT_BODY_OFT
#define P2P_NOA_ATT_CTW_OPPPS_OFT    (4)
#define P2P_NOA_ATT_NOA_DESC_OFT     (5)

/**
 * @name Notice of Absence descriptor format
 * See wifi_p2p_technical_specification_v1.1.pdf.
 * - 4.1.14 - Notice of Absence attribute Tables 28
 */
#define P2P_NOA_DESC_COUNT_OFT    (0)
#define P2P_NOA_DESC_DUR_OFT      (1)
#define P2P_NOA_DESC_INTV_OFT     (5)
#define P2P_NOA_DESC_START_OFT    (9)
#define P2P_NOA_DESC_LENGTH       (13)

#define P2P_IE_NOA_NO_NOA_DESC_LENGTH   (P2P_IE_ATT_OFT + P2P_NOA_ATT_NOA_DESC_OFT)
#define P2P_NOA_IE_BUFFER_LEN  ((P2P_IE_NOA_NO_NOA_DESC_LENGTH + (P2P_NOA_NB_MAX * P2P_NOA_DESC_LENGTH) + 1) / 2)
/** @} */

/**
 * If the counter field of NOA attribute is equal to 255, the absence cycles shall repeat
 * until cancel.
 */
#define P2P_NOA_CONTINUOUS_COUNTER  (255)
/// Invalid P2P Information Structure Index
#define P2P_INVALID_IDX             (0xFF)
/// Maximal number of concurrent NoA
#define P2P_NOA_NB_MAX              (2)

/// Margin used to avoid to program NOA timer in the past (in us)
#define P2P_NOA_TIMER_MARGIN        (5000)
/// Minimal absence duration we can consider (in us)
#define P2P_ABSENCE_DUR_MIN         (5000)
/// Beacon RX Timeout Duration (in us)
#define P2P_BCN_RX_TO_DUR           (5000)

/// Flag allowing to read OppPS subfield of CTWindow + OppPS field
#define P2P_OPPPS_MASK              (0x80)
/// Flag allowing to read CTWindow subfield of CTWindow + OppPS field
#define P2P_CTWINDOW_MASK           (0x7F)

#if (NX_UMAC_PRESENT)
/// SSID Wildcard for a P2P Device
#define P2P_SSID_WILDCARD           ("DIRECT-")
/// Length of P2P SSID Wildcard
#define P2P_SSID_WILDCARD_LEN       (7)
#endif //(NX_UMAC_PRESENT)

/*
 * ENUMERATIONS
 ****************************************************************************************
 */

/// NOA Timer (see struct p2p_info_tag) status
enum p2p_noa_timer_status
{
    /// Timer not started
    P2P_NOA_TIMER_NOT_STARTED = 0,
    /// Wait for next absence
    P2P_NOA_TIMER_WAIT_NEXT_ABS,
    /// Wait for end of absence
    P2P_NOA_TIMER_WAIT_END_ABS,
};

/// OppPS Timer (see struct p2p_info_tag) status
enum p2p_oppps_timer_status
{
    /// Timer not started
    P2P_OPPPS_TIMER_NOT_STARTED = 0,
    /// Wait for end of CTWindow
    P2P_OPPPS_TIMER_WAIT_END_CTW,
    /// Wait start of CTWindow
    P2P_OPPPS_TIMER_WAIT_START_CTW
};

/**
 * P2P Attribute ID definitions
 * See wifi_p2p_technical_specification_v1.1.pdf
 *     4.1.1 - P2P IE Format Table 6
 */
enum p2p_attribute_id
{
    P2P_ATT_ID_STATUS         = 0,
    P2P_ATT_ID_MINOR_REASON_CODE,
    P2P_ATT_ID_P2P_CAPABILITY,
    P2P_ATT_ID_P2P_DEVICE_ID,
    P2P_ATT_ID_GROUP_OWNER_INTENT,
    P2P_ATT_ID_CONFIG_TIMEOUT,
    P2P_ATT_ID_LISTEN_CHANNEL,
    P2P_ATT_ID_P2P_GROUP_BSSID,
    P2P_ATT_ID_EXT_LISTEN_TIMING,
    P2P_ATT_ID_INTENDED_P2P_IF_ADDR,
    P2P_ATT_ID_P2P_MANAGEABILITY,
    P2P_ATT_ID_CHANNEL_LIST,
    P2P_ATT_ID_NOTICE_OF_ABSENCE,
    P2P_ATT_ID_P2P_DEVICE_INFO,
    P2P_ATT_ID_P2P_GROUP_INFO,
    P2P_ATT_ID_P2P_GROUP_ID,
    P2P_ATT_ID_P2P_INTERFACE,
    P2P_ATT_ID_OPERATING_CHANNEL,
    P2P_ATT_ID_INVITATION_FLAGS,
    /// 19 - 220 -> Reserved
    P2P_ATT_ID_VENDOR_SPEC   = 221,
    /// 222 - 255 -> Reserved
    P2P_ATT_ID_MAX           = 255
};

/// P2P Role
enum p2p_role
{
    /// Client
    P2P_ROLE_CLIENT = 0,
    /// GO
    P2P_ROLE_GO,
};

/// Operation codes to be used in p2p_bcn_update_req structure
enum p2p_bcn_upd_op
{
    /// No Operation
    P2P_BCN_UPD_OP_NONE     = 0,

    /// Add P2P NOA IE
    P2P_BCN_UPD_OP_NOA_ADD,
    /// Remove P2P NOA IE
    P2P_BCN_UPD_OP_NOA_RMV,
    /// Update P2P NOA IE
    P2P_BCN_UPD_OP_NOA_UPD,
};

#if (NX_P2P_GO)
/// Type of NoA
enum p2p_noa_type
{
    /// Concurrent NoA - Triggered by connection as STA on a different channel
    P2P_NOA_TYPE_CONCURRENT,
    /// Normal NoA
    P2P_NOA_TYPE_NORMAL,
};
#endif //(NX_P2P_GO)

#endif

