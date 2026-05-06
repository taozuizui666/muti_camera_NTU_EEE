#ifndef _MAC_TYPES_H_
#define _MAC_TYPES_H_

#include "co_bit.h"

#include "bl_lmac_mac.h"

/// Scan result element, parsed from beacon or probe response frames.
struct mac_scan_result
{
    /// Scan result is valid
    bool valid_flag;
    /// Network BSSID.
    struct mac_addr bssid;
    /// Network name.
    struct mac_ssid ssid;
    /// Network type (@ref mac_bss_type).
    uint16_t bsstype;
    /// Network channel.
    struct mac_chan_def *chan;
    /// Supported AKM (bit-field of @ref mac_akm_suite)
    uint32_t akm;
    /// Group cipher (bit-field of @ref mac_cipher_suite)
    uint16_t group_cipher;
    /// Group cipher (bit-field of @ref mac_cipher_suite)
    uint16_t pairwise_cipher;
    /// RSSI of the scanned BSS (in dBm)
    int8_t rssi;
    /// Multi-BSSID index (0 if this is the reference (i.e. transmitted) BSSID)
    uint8_t multi_bssid_index;
    /// Maximum BSSID indicator
    uint8_t max_bssid_indicator;
    /// FTM support
    bool ftm_support;
};

#endif // _MAC_TYPES_H_
