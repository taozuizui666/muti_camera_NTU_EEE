#include "rwnx_config.h"
#if NX_RM
#include "softmac.h"

#include "scanu_task.h"
#include "rm.h"
#include "mac_ie.h"
#include "txl_frame.h"
#include "vif_mgmt.h"
#include "mfp.h"
#include "me_mgmtframe.h"

/// Status of a measure slot
enum rm_beacon_measure_status
{
    /// The measure doesn't contain valid data
    RM_MEASURE_INVALID = 0,
    /// The measure contains valid data
    RM_MEASURE_VALID,
    /// The measure is scheduled for reporting
    RM_MEASURE_SCHEDULED,
    /// The measure has been reported for the current request
    RM_MEASURE_REPORTED,
};

/// RM global context
struct rm_env_tag rm_env;

/// Size of fixed size fields in a beacon frame
#define BCN_FIXED_SIZE_FIELDS_LEN (offsetof_b(struct bcn_frame, variable) - \
                                   offsetof_b(struct bcn_frame, tsf))

static void rm_send_beacon_report(struct rm_beacon_request *request, 
                                           uint8_t mode, bool init_mac_hdr);

/// Maximum element size that can be included in the Reported Frame Body sub element
/// Indeed reported elements are included in a Measurement Report element that is
/// limited to 255 bytes and has other fixed size fields.
#define BCN_MAX_BODY_ELT_LEN 217

uint64_t rm_cur_tsf(void) {
    uint64_t cur_tsf = rm_env.tsf;
    uint64_t cur_jiffies = softmac_time();

    cur_tsf += jiffies_to_usecs((uint32_t)(cur_jiffies- rm_env.jiffies)) + 30;

    dbg_con("%s, jiffies diff:%llu-%llu, tsf diff:%llu-%llu\r\n", 
            __func__, cur_jiffies, rm_env.jiffies, cur_tsf, rm_env.tsf);

    return cur_tsf;
}

/**
 ****************************************************************************************
 * @brief Updates bandwidth from channel_width field of VHT operation Element
 *
 * @p bw is not modified if channel width is '20/40 MHz'
 *
 * @param[in]  ch_width  channel_width field from VHT operation
 * @param[out] bw        Updated bandwidth
 ****************************************************************************************
 */
static void rm_update_vht_bw(uint8_t ch_width, enum mac_chan_bandwidth *bw)
{
    if (ch_width == 0)
        return;

    if (ch_width == 1)
        *bw = PHY_CHNL_BW_80;
    else if (ch_width == 2)
        *bw = PHY_CHNL_BW_160;
    else if (ch_width == 3)
        *bw = PHY_CHNL_BW_80P80;
}

/**
 ****************************************************************************************
 * @brief Gets Global Operating class from channel information
 *
 * It is assumed that parameters are valid, no validity check is done.
 * See ANNEX E in IEEE802.11 for global operating classes definition.
 *
 * @param[in] chan        Channel number
 * @param[in] band        Channel band
 * @param[in] bw          Channel bandwidth
 * @param[in] sec_offset  Secondary channel offset (as found on HT operation element)
 *
 * @return Global Operating Class
 ****************************************************************************************
 */
static uint8_t rm_chan_to_op_class(uint8_t chan, enum mac_chan_band band,
                                 enum mac_chan_bandwidth bw, uint8_t sec_offset)
{
    if (bw == PHY_CHNL_BW_80P80)
        return 130;
    else if (bw == PHY_CHNL_BW_160)
        return 129;
    else if (bw == PHY_CHNL_BW_80)
        return 128;

    if (band == PHY_BAND_2G4)
    {
        if (chan == 14)
            return 82;
        if (bw == PHY_CHNL_BW_20)
            return 81;
        if (sec_offset == 1)
            return 84;
        else
            return 83;
    }
#if CFG_5G
    else
    {
        int class_oft;

        if (chan <= 48)
            class_oft = 0;
        else if (chan <= 64)
            class_oft = 3;
        else if (chan <= 144)
            class_oft = 6;
        else if ((chan >= 165) && (bw == PHY_CHNL_BW_20))
            return 125;
        else
            class_oft = 9;

        if (bw == PHY_CHNL_BW_20)
            return 115 + class_oft;

        if (class_oft == 9)
            class_oft++;

        if (sec_offset == 1)
            return 116 + class_oft;
        else
            return 117 + class_oft;
    }
#endif

    return 81;
}

/**
 ****************************************************************************************
 * @brief Gets the band of a global operating class
 *
 * See ANNEX E in IEEE802.11 for global operating classes definition.
 *
 * @param[in]  op_class  Operating Class
 * @return Band used by the operating class
 ****************************************************************************************
 */
static enum mac_chan_band rm_op_class_to_band(uint8_t op_class)
{
    if (op_class >= 81 && op_class <= 84)
        return PHY_BAND_2G4;
#if CFG_5G
    else
        return PHY_BAND_5G;
#endif

    return PHY_BAND_MAX;
}

/**
 ****************************************************************************************
 * @brief Gets list of channel for a Global Operating Class
 *
 * Since we don't handle bandwidth and always scan on 20MHz treat 40MHz/80MHz operating
 * classes as the corresponding 20MHz one. \\
 * For example op class 116 which defines the two 40+MHz channels 36 and 44
 * and op class 117 that defines the two 40-MHz channels 40 and 48 are treated
 * as op class 115 that defines the four 20MHz channels 36,40,44 and 48
 *
 * See ANNEX E in IEEE802.11 for global operating classes definition.
 *
 * @param[in]  op_class  Operating Class
 * @param[out] list      Table to write the channel list
 * @return Number of channel written in the list
 ****************************************************************************************
 */
static int rm_op_class_to_chan_list(uint8_t op_class, uint8_t *list)
{
    int start, end, oft, cnt = 0, i;

    switch (op_class)
    {
        case 81:
        case 83:
        case 84:
            start = 1;
            end = 14;
            oft = 1;
            break;
        case 82:
            start = 14;
            end = 15;
            oft = 1;
            break;
        case 115:
        case 116:
        case 117:
            start = 36;
            end = 49;
            oft = 4;
            break;
        case 118:
        case 119:
        case 120:
            start = 52;
            end = 65;
            oft = 4;
            break;
        case 121:
        case 122:
        case 123:
            start = 100;
            end = 145;
            oft = 4;
            break;
        case 124:
            start = 149;
            end = 162;
            oft = 4;
            break;
        case 128:
        case 130:
            for (i = 132 ; i < 145 ; i += 4)
                *list++ = i;
            cnt += 4;
            // fallthrough
        case 129:
            for (i = 36 ; i < 65 ; i += 4)
                *list++ = i;
            cnt += 8;
            for (i = 100 ; i < 129 ; i += 4)
                *list++ = i;
            cnt += 8;
            // fallthrough
        case 125:
        case 126:
        case 127:
            start = 149;
            end = 178;
            oft = 4;
            break;
        default:
            return 0;
    }

    for (i = start ; i < end ; i += oft)
    {
        *list++ = i;
        cnt++;
    }

    return cnt;
}

/**
 ****************************************************************************************
 * @brief Include adjacent channel to meet requested bandwidth
 *
 * Since we can't include bandwidth in the scan request, when scan on a single channel
 * is requested (first element of the @p list) add adjacent channels so that the list
 * of selected channels span across the requested bandwidth.
 * See ANNEX E in IEEE802.11 for global operating classes definition.
 *
 * @param[in]     op_class  Operating Class
 * @param[in,out] list      Table to write the channel list
 * @return Number of channel valid in the list
 ****************************************************************************************
 */
static uint8_t rm_add_adjacent_channels(uint8_t op_class, uint8_t *list)
{
    uint8_t ch = list[0];
    
    switch (op_class)
    {
        case 83:
        case 116:
        case 119:
        case 122:
        case 126:
            // 40+MHz op classes
            list[1] = ch + 4;
            return 2;
        case 84:
        case 117:
        case 120:
        case 123:
        case 127:
            // 40-MHz op classes
            list[1] = ch - 4;
            return 2;
        case 128:
        {
            // 80MHz op classes
            uint8_t start, end;
            if (ch < 36)
                return 1;
            else if (ch <= 48)
            {
                start = 36;
                end = 48;
            }
            else if (ch <= 64)
            {
                start = 52;
                end = 64;
            }
            else if (ch <= 112)
            {
                start = 100;
                end = 112;
            }
            else if (ch <= 128)
            {
                start = 116;
                end = 128;
            }
            else if (ch <= 144)
            {
                start = 132;
                end = 144;
            }
            else
                return 1;

            while (start <= end)
            {
                *list++ = start;
                start += 4;
            }
            return 4;
        }
        case 129:
        {
            // 160MHz op classes
            uint8_t start, end;
            if (ch <= 64)
            {
                start = 36;
                end = 64;
            }
            else
            {
                start = 100;
                end = 128;
            }
            while (start <= end)
            {
                *list++ = start;
                start += 4;
            }
            return 8;
        }
        default:
            // 20MHz op classes, no channel to add
            return 1;
    }
}

/**
 ****************************************************************************************
 * @brief Tests whether element is requested
 *
 * @param[in] elt      Element ID
 * @param[in] elt_ext  Element ID Extension (only used if elt == MAC_ELTID_EXT)
 * @param[in] request  Request to check (May be NULL, in this case accept all elements)
 * @return Whether specified element is one of the requested element
 ****************************************************************************************
 */
static bool rm_requested_elt(uint8_t elt, uint8_t elt_ext,
                                   struct rm_beacon_request *request)
{
    int i;

    if (!request ||
        ((request->req_elt_cnt == 0) && (request->req_ext_elt_cnt == 0)))
        return true;

    if (elt == MAC_ELTID_EXT)
    {
        for (i = 0 ; i < request->req_ext_elt_cnt; i++)
            if (request->req_ext_elt[i] == elt_ext)
                return true;
    }
    else
    {
        for (i = 0 ; i < request->req_elt_cnt; i++)
            if (request->req_elt[i] == elt)
                return true;
    }

    return false;
}

/**
 ****************************************************************************************
 * @brief Tests if Measure is requested
 *
 * @param[in] bssid     BSSID to test
 * @param[in] ssid_elt  HW address of the SSID element to test
 * @param[in] request   Request to check (May be NULL, in this case accept all measures)
 * @return Whether specified bssid is one of the requested BSSID
 ****************************************************************************************
 */
static bool rm_requested_measure(const struct mac_addr *bssid, 
                           PTR2UINT ssid_elt, struct rm_beacon_request *request)
{
    if (!request)
        return true;

    if (!request->wildcard_bssid && !MAC_ADDR_CMP_PACKED(&request->bssid, bssid))
        return false;

    if (request->ssid.length && ssid_elt)
    {
        // use intermediate struct mac_ssid to handle case where CHAR_LEN != 1
        struct mac_ssid ssid;
        
        ssid.length = co_read8p(ssid_elt + MAC_INFOELT_LEN_OFT);
        co_unpack8p(ssid.array, ssid_elt + MAC_SSID_SSID_OFT, ssid.length);

        if (!MAC_SSID_CMP(&request->ssid, &ssid))
            return false;
    }

    return true;
}

/**
 ****************************************************************************************
 * @brief Finds the slot to save a given measure
 *
 * This function first try to find a slot already used for this AP's measure.
 * If not then it tries to find a free slot.
 * If no free slot then it selects, if it exists, the measure already reported with the
 * worst rssi.
 * Lastly it tries to find a valid slot with a rssi worst than this new measure.
 * If still no slot can be found a NULL pointer is returned.
 *
 * @param[in] bssid  BSSID of the new measure
 * @param[in] rssi   RSSI, in dBm, of the new measure
 * @return Pointer to a slot to save the measure (Can be NULL)
 ****************************************************************************************
 */
static struct rm_beacon_measure *rm_get_measure_slot(
                                     const struct mac_addr *bssid, int8_t rssi)
{
    struct rm_beacon_measure *measure, *free, *reported, *least_rssi, *res = NULL;
    int i;

    // TODO add timer to invalid measure after x min ?

    measure = rm_env.measures;
    free = least_rssi = reported = NULL;
    for (i = 0; i < CO_ARRAY_SIZE(rm_env.measures); i++, measure++)
    {
        if (MAC_ADDR_CMP_PACKED(&measure->bssid, bssid))
            return measure;

        switch(measure->status)
        {
            case RM_MEASURE_INVALID:
                if (!free)
                    free = measure;
                break;
            case RM_MEASURE_VALID:
                if (!least_rssi || measure->rssi < least_rssi->rssi)
                    least_rssi = measure;
                break;
            case RM_MEASURE_REPORTED:
                if (!reported || measure->rssi < reported->rssi)
                    reported = measure;
                break;
            default:
                break;
        }
    }

    if (free)
        res = free;
    else if (reported)
        res = reported;
    else if (least_rssi && (least_rssi->rssi < rssi))
        res = least_rssi;

    if (res)
        // this also reset status to INVALID
        memset(res, 0, sizeof(*res));

    return res;
}

/**
 ****************************************************************************************
 * @brief Parses beacon specific part of a Measurement Request Element and initializes
 * request accordingly.
 *
 * @param[in]  req      HW address of the beacon request
 * @param[in]  req_len  Request length, in bytes
 * @param[out] request  Request structure to initialize
 * @return 0 in the request is valid, and the reject reason otherwise
 ****************************************************************************************
 */
static uint8_t rm_parse_beacon_request(PTR2UINT req, uint16_t req_len,
                                              struct rm_beacon_request *request)
{
    uint8_t op_class, chan, ch_rpt_op_class;
    PTR2UINT ap_ch_report = 0, wide_bw_cs = 0;

    if (req_len < MAC_MEAS_REQ_BCN_SUB_ELEM_OFT)
        return MAC_MEAS_REP_MODE_REFUSE_BIT;

    request->measure_mode = co_read8p(req + MAC_MEAS_REQ_BCN_MODE_OFT);

    if (request->measure_mode > MAC_MEAS_REQ_BCN_TABLE)
        return MAC_MEAS_REP_MODE_INCAPABLE_BIT;

    op_class = co_read8p(req + MAC_MEAS_REQ_BCN_OP_CLASS_OFT);
    chan = co_read8p(req + MAC_MEAS_REQ_BCN_CHAN_NUMBER_OFT);
    request->duration = co_read8p(req + MAC_MEAS_REQ_BCN_DURATION_OFT);

    co_unpack8p((uint8_t *)&request->bssid, req + MAC_MEAS_REQ_BCN_BSSID_OFT,
                MAC_ADDR_LEN);

    if ((request->bssid.array[0] == 0xFFFF) &&
        (request->bssid.array[0] == 0xFFFF) &&
        (request->bssid.array[0] == 0xFFFF))
        request->wildcard_bssid = true;

    request->detail = MAC_BCN_REPORT_DETAIL_ALL;

    // Sub elements
    req += MAC_MEAS_REQ_BCN_SUB_ELEM_OFT;
    req_len -= MAC_MEAS_REQ_BCN_SUB_ELEM_OFT;
    while (req_len > 2)
    {
        uint8_t elt = co_read8p(req);
        uint16_t elt_len = mac_ie_len(req);

        if (elt_len > req_len)
            return MAC_MEAS_REP_MODE_REFUSE_BIT;

        switch (elt)
        {
            case MAC_MEAS_REQ_BCN_SUBELTID_SSID:
                if (elt_len > MAC_SSID_MAX_LEN)
                    return MAC_MEAS_REP_MODE_REFUSE_BIT;

                request->ssid.length = elt_len - MAC_INFOELT_INFO_OFT;
                co_unpack8p(request->ssid.array, req + MAC_SSID_SSID_OFT,
                            request->ssid.length);
                break;
            case MAC_MEAS_REQ_BCN_SUBELTID_BCN_REPORTING:
                // should only be present for repetitive measure that we don't support
                break;
            case MAC_MEAS_REQ_BCN_SUBELTID_REPORT_DETAIL:
                request->detail = co_read8p(req + MAC_BCN_REPORT_DETAIL_OFT);
                break;
            case MAC_MEAS_REQ_BCN_SUBELTID_REQUEST:
                if ((elt_len - MAC_INFOELT_INFO_OFT) > CO_ARRAY_SIZE(request->req_elt))
                {
                    dbg_con("Request more elements than supported\r\n");
                    
                    request->req_elt_cnt = CO_ARRAY_SIZE(request->req_elt);
                }
                else
                    request->req_elt_cnt = elt_len - MAC_INFOELT_INFO_OFT;

                co_unpack8p(request->req_elt, req + MAC_REQUEST_ELTID_REQ_OFT,
                            request->req_elt_cnt);
                break;
            case MAC_MEAS_REQ_BCN_SUBELTID_REQUEST_EXT:
            {
                uint8_t req_ext_cnt = elt_len - MAC_INFOELT_INFO_OFT - 1;

                // use -1 everywhere because sub element doesn't contain the Element ID extension
                if (elt_len < (MAC_EXT_REQUEST_ELT_ID_EXT_OFT - 1))
                    return MAC_MEAS_REP_MODE_REFUSE_BIT;

                if (co_read8p(req + MAC_EXT_REQUEST_ELT_ID_OFT - 1) != MAC_ELTID_EXT)
                {
                    dbg_con("Unsupported extension ID\r\n");
                    
                    break;
                }

                if (req_ext_cnt > CO_ARRAY_SIZE(request->req_ext_elt))
                {
                    dbg_con("Request more extended elements than supported\r\n");
                    
                    request->req_ext_elt_cnt = CO_ARRAY_SIZE(request->req_ext_elt);
                }
                else
                    request->req_ext_elt_cnt = req_ext_cnt;

                co_unpack8p(request->req_ext_elt, 
                            req + MAC_EXT_REQUEST_ELT_ID_EXT_OFT - 1,
                            request->req_ext_elt_cnt);
                break;
            }
            case MAC_MEAS_REQ_BCN_SUBELTID_AP_CHAN_REPORT:
                ch_rpt_op_class = co_read8p(req + MAC_AP_CHAN_REPORT_OP_CLASS_OFT);
                //only 2.4G, BL616L
                if (ch_rpt_op_class >= 81 && ch_rpt_op_class <= 84)
                    ap_ch_report = req;
                break;
            case MAC_MEAS_REQ_BCN_SUBELTID_WIDE_BW_CHAN_SWITCH:
                wide_bw_cs = req;
                break;
            case MAC_MEAS_REQ_BCN_SUBELTID_LAST_BCN_REPORT_IND:
                request->last_report_ind =
                           co_read8p(req + MAC_BCN_LAST_BCN_REPORT_IND_REQ_OFT);
            default:
                break;
        }
        req_len -= elt_len;
        req += elt_len;
    }

    if ((request->measure_mode == MAC_MEAS_REQ_BCN_PASSIVE) ||
        (request->measure_mode == MAC_MEAS_REQ_BCN_ACTIVE))
    {
        dbg_f("%s %u, wide_bw_cs:0x%x, ap_ch_rpt:0x%x, op_class:%u, chan:%u\r\n",
              __func__, __LINE__, wide_bw_cs, ap_ch_report, op_class, chan);
              
        if (wide_bw_cs)
        {
            // If Wide Bandwidth Channel Switch present: Op Class and Ch Number fields
            // together specify the primary channel and primary 40 MHz channel
            uint8_t bw;
            uint16_t center1, center2;

            bw = co_read8p(wide_bw_cs + MAC_INFOELT_WIDE_BW_CHAN_SWITCH_NEW_CW_OFT);
            center1 = co_read16p((void *)(wide_bw_cs + MAC_INFOELT_WIDE_BW_CHAN_SWITCH_NEW_CENTER1_OFT));
            center2 = co_read16p((void *)(wide_bw_cs + MAC_INFOELT_WIDE_BW_CHAN_SWITCH_NEW_CENTER2_OFT));

            dbg_f("%s %u, bw:%u, center1:%u %u\r\n",
                  __func__, __LINE__, bw, center1, center2);
                  
#if CFG_5G
            if (center1 > PHY_FREQ_5G)
                request->band_scan = PHY_BAND_5G;
            else
#endif
                request->band_scan = PHY_BAND_2G4;

            if (!phy_channel_to_freq(request->band_scan, op_class))
                return MAC_MEAS_REP_MODE_REFUSE_BIT;

            request->chan_cnt = 1;
            request->chan_list[0] = op_class;

            // recompute pseudo op class for rm_add_adjacent_channels
            if (bw == 0)
            {
                if (chan == op_class)
                    op_class = (request->band_scan = PHY_BAND_2G4) ? 81 : 115;
                else if (chan > op_class)
                    op_class = (request->band_scan = PHY_BAND_2G4) ? 83 : 116;
                else
                    op_class = (request->band_scan = PHY_BAND_2G4) ? 84 : 117;
            }
            else if ((bw == 1) && (center2 == 0))
                op_class = 128;
            else
                op_class = 129;

            dbg_f("%s %u, op_class:%u, band:%u\r\n",
                  __func__, __LINE__, op_class, request->band_scan);

            //only 2.4G, BL616L
            if (op_class < 81 || op_class > 84)
                return MAC_MEAS_REP_MODE_REFUSE_BIT;
        }
        else if (chan == 0xFF)
        {
            uint8_t chan_cnt, idx = 0;
            
            if (!ap_ch_report)
                return MAC_MEAS_REP_MODE_REFUSE_BIT;

            op_class = co_read8p(ap_ch_report + MAC_AP_CHAN_REPORT_OP_CLASS_OFT);

            dbg_f("%s %u, op_class:%d\r\n", __func__, __LINE__, op_class);

            //only 2.4G, BL616L
            if (op_class < 81 || op_class > 84)
                return MAC_MEAS_REP_MODE_REFUSE_BIT;
                
            request->band_scan = rm_op_class_to_band(op_class);

            chan_cnt = mac_ie_len(ap_ch_report) - MAC_AP_CHAN_REPORT_OP_CHAN_OFT;
            if (chan_cnt > CO_ARRAY_SIZE(request->chan_list))
                chan_cnt = CO_ARRAY_SIZE(request->chan_list);

            ap_ch_report += MAC_AP_CHAN_REPORT_OP_CHAN_OFT;
            while (chan_cnt--)
            {
                chan = co_read8p(ap_ch_report++);
                if (phy_channel_to_freq(request->band_scan, chan))
                    request->chan_list[idx++] = chan;
            }
            if (idx == 0)
                return MAC_MEAS_REP_MODE_REFUSE_BIT;
            
            request->chan_cnt = idx;
        }
        else if (chan == 0)
        {
            //only 2.4G, BL616L
            if (op_class < 81 || op_class > 84)
                return MAC_MEAS_REP_MODE_REFUSE_BIT;

            request->band_scan = rm_op_class_to_band(op_class);

            request->chan_cnt = 
                        rm_op_class_to_chan_list(op_class, request->chan_list);
            ASSERT_ERR(request->chan_cnt <= CO_ARRAY_SIZE(request->chan_list));
            
            if (!request->chan_cnt)
                return MAC_MEAS_REP_MODE_REFUSE_BIT;
        }
        else
        {
            //some router send opclass=0 channel =1-14
            if (op_class == 0)
                op_class = 81;
                
            //only 2.4G, BL616L
            if (op_class < 81 || op_class > 84)
                return MAC_MEAS_REP_MODE_REFUSE_BIT;

            request->band_scan = rm_op_class_to_band(op_class);

            if (!phy_channel_to_freq(request->band_scan, chan))
                return MAC_MEAS_REP_MODE_REFUSE_BIT;

            request->chan_cnt = 1;
            request->chan_list[0] = chan;
        }

        // Since we only scan on 20MHz channel, if request include a single channel with
        // wider bandwidth, include adjacent channel to scan the whole requested channel
        if (request->chan_cnt == 1)
            request->chan_cnt = rm_add_adjacent_channels(op_class, 
                                                         request->chan_list);
    }

    dbg_con("Measure mode=%d, detail=%d, last_report_ind=%d, BSSID=%pM\r\n",
         request->measure_mode, request->detail, request->last_report_ind,
         &request->bssid);
             
    if (request->req_elt_cnt)
    {
        dbg_con("Request elt: %d %d\r\n",
                request->req_elt_cnt, request->req_elt[0]);
    }
    
    if (request->req_ext_elt_cnt)
    {
        dbg_con("Request extended elt: %d %d\r\n",
                request->req_ext_elt_cnt, request->req_ext_elt[0]);
    }
    
    if (request->ssid.length)
    {
        dbg_con("SSID: %d, %s\r\n", request->ssid.length, request->ssid.array);
    }
    
    if (request->chan_cnt)
    {
        dbg_con("Chan: %d %d\r\n", request->chan_cnt, request->chan_list[0]);
    }

    return 0;
}

/**
 ****************************************************************************************
 * @brief Parses one Measurement Request Element and initializes request accordingly.
 *
 * Only Beacon measurement request are supported.
 * If the element contains other types of request, or if the request is invalid or not
 * supported the reject reason to include (in the report mode field) in a measurement
 * report action frame is returned.
 *
 * @note If the element doesn't actually contains a request (i.e. Enable bit is set) the
 * function still returns 0 (as the element is valid). To detect this case the caller
 * must check whether dialog_token in the request has been updated or not.
 *
 * @param[in]  dialog_token  Dialog token of the action frame than contains the request
 * @param[in]  req           HW address of the measurement request element
 * @param[out] request       Request structure to initialize
 * @return 0 in the request is valid, and the reject reason otherwise
 ****************************************************************************************
 */
static uint8_t rm_parse_measurement_request(uint8_t dialog_token, 
                                PTR2UINT req, struct rm_beacon_request *request)
{
    uint8_t mode, reject;
    uint16_t req_len = mac_ie_len(req);

    if (req_len < MAC_MEAS_REQ_REQ_OFT)
        return MAC_MEAS_REP_MODE_REFUSE_BIT;

    memset(request, 0, sizeof(*request));

    // Measurement request generic part
    request->req_token = co_read8p(req + MAC_MEAS_REQ_TOKEN_OFT);
    mode = co_read8p(req + MAC_MEAS_REQ_MODE_OFT);
    request->type = co_read8p(req + MAC_MEAS_REQ_TYPE_OFT);

    if (mode & MAC_MEAS_REQ_MODE_PARALLEL_BIT)
        return MAC_MEAS_REP_MODE_INCAPABLE_BIT;

    if (mode & MAC_MEAS_REQ_MODE_ENABLE_BIT)
        return 0;

    req += MAC_MEAS_REQ_REQ_OFT;
    req_len -= MAC_MEAS_REQ_REQ_OFT;

    if (request->type == MAC_MEAS_REQ_TYPE_BEACON)
        reject = rm_parse_beacon_request(req, req_len, request);
    else
        reject = MAC_MEAS_REP_MODE_INCAPABLE_BIT;

    if (reject)
        return reject;

    request->dialog_token = dialog_token;
    
    return 0;
}

/**
 ****************************************************************************************
 * @brief Reset measures status
 *
 * Reset status of measures after and before a request is processed. This allow to reset
 * @ref RM_MEASURE_SCHEDULED and @ref RM_MEASURE_REPORTED status or simply invalid
 * all measures.
 *
 * @param[in] invalid  Whether status should be forced to invalid
 ****************************************************************************************
 */
void rm_reset_measures(bool invalid)
{
    struct rm_beacon_measure *measure = rm_env.measures;
    int i;

    for (i = 0; i < RM_MAX_MEASURES; i++, measure++)
    {
        if (invalid)
        {
            measure->status = RM_MEASURE_INVALID;
            memset(&measure->bssid, 0, sizeof(measure->bssid));
        }
        else if (measure->status > RM_MEASURE_VALID)
            measure->status = RM_MEASURE_VALID;
    }
}

/**
 ****************************************************************************************
 * @brief Initializes list of measure to report for current request
 ****************************************************************************************
 */
void rm_init_measure_to_report(void)
{
    co_list_init(&rm_env.report_list);
}

/**
 ****************************************************************************************
 * @brief Adds a measure in the list of measure to include in report for current request
 *
 * @param[in] measure  Measure to include
 ****************************************************************************************
 */
static void rm_add_measure_to_report(struct rm_beacon_measure *measure)
{
    measure->reported_len = 0;
    measure->frag_id = 0;
    measure->status = RM_MEASURE_SCHEDULED;
    co_list_push_back(&rm_env.report_list, &measure->report);
}

/**
 ****************************************************************************************
 * @brief Gets next measure to report
 *
 * @return Pointer to the next measure to report
 ****************************************************************************************
 */
static struct rm_beacon_measure *rm_next_measure_to_report(void)
{
    return (struct rm_beacon_measure *)co_list_pick(&rm_env.report_list);
}

/**
 ****************************************************************************************
 * @brief Removes measure that has been fully reported from the report list
 *
 * @param[in] measure  Measure that has been fully reported
 ****************************************************************************************
 */
static void rm_measure_reported(struct rm_beacon_measure *measure)
{
    measure->status = RM_MEASURE_REPORTED;
    co_list_extract(&rm_env.report_list, &measure->report);
}

/**
 ****************************************************************************************
 * @brief Checks if there are still measure to report
 *
 * When the task is in RM_REPORTING_WHILE_MEASURING state it means that there are still
 * measures to be performed but we cannot know if those measures will provide any new
 * result to report. As it is sometimes requested to indicate the last report, in this
 * case one measure to report is always kept in the list. When all measure are completed
 * the last report is always send with he task in the RM_REPORTING state.
 *
 * @return whether there are other measures to report for the current state or not
 ****************************************************************************************
 */
static bool rm_no_more_measure_to_report(void)
{
    if (co_list_is_empty(&rm_env.report_list))
        return true;

    if ((ke_state_get(TASK_RM) == RM_REPORTING_WHILE_MEASURING) &&
        (co_list_next(co_list_pick(&rm_env.report_list)) == NULL))
        return true;

    return false;
}

/**
 ****************************************************************************************
 * @brief Checks if at least a fragment of the next measure can be included in the
 * report buffer
 *
 * @param[in] request  Request structure
 * @param[in] avail    Available length, in bytes, in the report buffer
 * @return True if a fragment of the next measure can be included and false otherwise
 ****************************************************************************************
 */
static bool rm_can_add_bcn_report_fragment(
                            struct rm_beacon_request *request, uint16_t avail)
{
    struct rm_beacon_measure *measure = rm_next_measure_to_report();

    if (!measure)
        // report with no measure
        return true;

    // Fixed size part
    if (avail < MAC_MEAS_REP_BCN_SUB_ELEM_OFT)
        return false;
        
    avail -= MAC_MEAS_REP_BCN_SUB_ELEM_OFT;

    // Sub Elements
    if (request->last_report_ind)
    {
        if (avail < MAC_BCN_LAST_BCN_REPORT_IND_LEN)
            return false;
            
        avail -= MAC_BCN_LAST_BCN_REPORT_IND_LEN;
    }

    if (request->detail == MAC_BCN_REPORT_DETAIL_NONE)
        return true;

    if (avail < MAC_BCN_FRAME_BODY_FRAG_ID_LEN)
        return false;
        
    avail -= MAC_BCN_FRAME_BODY_FRAG_ID_LEN;

    if (measure->reported_len == 0)
    {
        if (avail < (MAC_BCN_FRAME_BODY_OFT + BCN_FIXED_SIZE_FIELDS_LEN))
            return false;
    }
    else
    {
        // If we reach here it means we have at least one more element to report
        // whether the report detail is FIXED or ALL
        PTR2UINT elts = CPU2HW(measure->payload) + measure->reported_len;
        uint32_t elts_len = measure->len - measure->reported_len;

        if (request->detail == MAC_BCN_REPORT_DETAIL_FIXED)
        {
            uint8_t elt = co_read8p(elts);
            uint8_t elt_len = mac_ie_len(elts);
            uint8_t elt_ext = co_read8p(elts + MAC_INFOELT_EXT_ID_OFT);

            while (!rm_requested_elt(elt, elt_ext, request) ||
                   (elt_len > BCN_MAX_BODY_ELT_LEN))
            {
                elts_len -= elt_len;
                elts += elt_len;
                if (!elts_len)
                    break;
                elt = co_read8p(elts);
                elt_len = mac_ie_len(elts);
                co_read8p(elts + MAC_INFOELT_EXT_ID_OFT);
            }
        }

        if (!elts_len)
        {
            ASSERT_WARN(0);
            return true;
        }

        if (avail < (MAC_BCN_FRAME_BODY_OFT + mac_ie_len(elts)))
            return false;
    }

    return true;
}

/**
 ****************************************************************************************
 * @brief Checks if a Measurement Report element for the next measure can be included
 * in the report buffer
 *
 * @note Even if a Measurement Report element can be added, the next measure may still
 * need more Measurement report element to be fully reported.
 *
 * @param[in] request  Request structure
 * @param[in] avail    Available length, in bytes, in the report buffer
 * @return True if a Measurement Report element can be included and false otherwise.
 ****************************************************************************************
 */
static bool rm_can_add_measurement_report(
                           struct rm_beacon_request *request, uint16_t avail)
{
    if (avail < MAC_MEAS_REQ_REQ_OFT)
        return false;

    avail -= MAC_MEAS_REQ_REQ_OFT;

    if (request->type == MAC_MEAS_REQ_TYPE_BEACON)
        return rm_can_add_bcn_report_fragment(request, avail);

    return true;
}

/**
 ****************************************************************************************
 * @brief Builds the Beacon report part of Measurement Report element for the next
 * measure to report
 *
 * Indirect caller must ensure that report will fit in the buffer
 * (@ref rm_add_measurement_report_elt)
 * If the measure is completely reported with this report it will be removed from the
 * list of measure to report. Otherwise its internal status will be updated so that next
 * call to this function will start reporting from where it stopped.
 *
 * @param[in]     report   HW address where to write the Beacon Report
 * @param[in]     avail    Available length, in bytes, for the beacon report
 * @param[in,out] request  Request associated to this report
 * @return Size in bytes of the Measurement report written
 ****************************************************************************************
 */
static uint32_t rm_add_bcn_report(PTR2UINT report, uint16_t avail,
                                         struct rm_beacon_request *request)
{
    struct rm_beacon_measure *measure = rm_next_measure_to_report();
    PTR2UINT pos = report;

    request->report_sent++;

    if (!measure)
    {
        dbg_con("Send report with no measure");
        
        return 0;
    }

    // Fixed part
    co_write8p(pos + MAC_MEAS_REP_BCN_OP_CLASS_OFT, measure->op_class);
    co_write8p(pos + MAC_MEAS_REP_BCN_CHAN_NUMBER_OFT, measure->chan);
    co_write64p(pos + MAC_MEAS_REP_BCN_START_TIME_OFT, request->start_time);
    co_write16p(pos + MAC_MEAS_REP_BCN_DURATION_OFT, request->duration);
    co_write8p(pos + MAC_MEAS_REP_BCN_INFO_OFT, measure->phy_type);
    co_write8p(pos + MAC_MEAS_REP_BCN_RCPI_OFT, mac_rcpi_format(measure->rssi));
    co_write8p(pos + MAC_MEAS_REP_BCN_RSNI_OFT, 0xFF);
    co_pack8p(pos + MAC_MEAS_REP_BCN_BSSID_OFT, (uint8_t *)&measure->bssid, MAC_ADDR_LEN);
    co_write8p(pos + MAC_MEAS_REP_BCN_ANTENNA_OFT, measure->antenna_set);
    co_write32p(pos + MAC_MEAS_REP_BCN_TSF_OFT, measure->tsflo);

    pos += MAC_MEAS_REP_BCN_SUB_ELEM_OFT;
    avail -= MAC_MEAS_REP_BCN_SUB_ELEM_OFT;

    // Sub Elements
    // Last report Indication is written after but must always be present
    if (request->last_report_ind)
        avail -= MAC_BCN_LAST_BCN_REPORT_IND_LEN;

    if (request->detail == MAC_BCN_REPORT_DETAIL_NONE)
    {
        measure->reported_len = measure->len;
    }
    else
    {
        PTR2UINT elts, body = pos;
        uint16_t elts_len, frag_id;

        // Keep space for Body fragment ID sub-element
        avail -= MAC_BCN_FRAME_BODY_FRAG_ID_LEN;

        co_write8p(pos + MAC_INFOELT_ID_OFT, MAC_MEAS_REP_BCN_SUBELTID_FRAME_BODY);
        pos += MAC_BCN_FRAME_BODY_OFT;
        avail -= MAC_BCN_FRAME_BODY_OFT;

        if (measure->reported_len < BCN_FIXED_SIZE_FIELDS_LEN) {
            ASSERT_ERR(avail >= BCN_FIXED_SIZE_FIELDS_LEN);
            co_copy8p(pos, CPU2HW(measure->payload), BCN_FIXED_SIZE_FIELDS_LEN);
            pos += BCN_FIXED_SIZE_FIELDS_LEN;
            avail -= BCN_FIXED_SIZE_FIELDS_LEN;
            measure->reported_len = BCN_FIXED_SIZE_FIELDS_LEN;
        }

        if ((request->detail == MAC_BCN_REPORT_DETAIL_FIXED) &&
            !request->req_elt_cnt && !request->req_ext_elt_cnt)
        {
            measure->reported_len = measure->len;
            goto set_body_len;
        }

        elts = CPU2HW(measure->payload) + measure->reported_len;
        elts_len = measure->len - measure->reported_len;

        while (elts_len) {
            uint8_t elt_id = co_read8p(elts);
            uint8_t elt_len = mac_ie_len(elts);
            uint8_t elt_ext = co_read8p(elts + MAC_INFOELT_EXT_ID_OFT);

            if (rm_requested_elt(elt_id, elt_ext, request) &&
                (elt_len <= BCN_MAX_BODY_ELT_LEN)) // simply ignore element that are too big to report
            {
                if (elt_len > avail)
                    goto set_body_len;
                co_copy8p(pos, elts, elt_len);
                pos += elt_len;
                avail -= elt_len;
            }
            measure->reported_len += elt_len;
            elts_len -= elt_len;
            elts += elt_len;
        }

      set_body_len:
        co_write8p(body + MAC_INFOELT_LEN_OFT, pos - (body + MAC_BCN_FRAME_BODY_OFT));

        // Body fragment ID
        co_write8p(pos + MAC_INFOELT_ID_OFT, MAC_MEAS_REP_BCN_SUBELTID_FRAME_FRAG_ID);
        co_write8p(pos + MAC_INFOELT_LEN_OFT, MAC_BCN_FRAME_BODY_FRAG_ID_ELMT_LEN);
        frag_id = ((request->req_token << MAC_BCN_FRAME_REPORT_ID_OFT) |
                   (measure->frag_id++ << MAC_BCN_FRAME_FRAG_ID_OFT));

        if (measure->reported_len < measure->len)
            frag_id |= MAC_BCN_FRAME_MORE_FRAG_BIT;
            
        co_write16p(pos + MAC_BCN_FRAME_BODY_FRAG_ID_OFT, frag_id);
        pos += MAC_BCN_FRAME_BODY_FRAG_ID_LEN;
    }

    if (measure->reported_len >= measure->len)
        rm_measure_reported(measure);

    if (request->last_report_ind)
    {
        co_write8p(pos + MAC_INFOELT_ID_OFT, MAC_MEAS_REP_BCN_SUBELTID_LAST_BCN_REPORT_IND);
        co_write8p(pos + MAC_INFOELT_LEN_OFT, MAC_BCN_LAST_BCN_REPORT_IND_ELMT_LEN);
        if (rm_no_more_measure_to_report())
            co_write8p(pos + MAC_BCN_LAST_BCN_REPORT_IND_OFT, 1);
        else
            co_write8p(pos + MAC_BCN_LAST_BCN_REPORT_IND_OFT, 0);

        pos += MAC_BCN_LAST_BCN_REPORT_IND_LEN;
    }

    dbg_con("[%d] BSSID=%pM, report len=%d, report part=%d, remaining=%d\r\n",
         CO_GET_INDEX(measure, rm_env.measures), &measure->bssid,
         (pos - report), request->report_sent,
         measure->len - measure->reported_len);

    return pos - report;
}


/**
 ****************************************************************************************
 * @brief Builds the Measurement Report element with the beacon report for the next
 * measure to report
 *
 * Caller of this function must first check that the report will fit in the buffer by
 * calling @ref rm_can_add_measurement_report.
 *
 * @param[in]     report   HW address where to write the Measurement Report element
 * @param[in]     avail    Available length, in bytes, for the Measurement report element
 * @param[in,out] request  Request associated to this report
 * @return Size in bytes of the Measurement report written
 ****************************************************************************************
 */
static uint32_t rm_add_measurement_report_elt(PTR2UINT report, 
                              uint16_t avail, struct rm_beacon_request *request)
{
    uint32_t report_len = 0;

    // Limit to the maximum size of an Element
    if (avail > MAC_INFOELT_MAX_LEN)
        avail = MAC_INFOELT_MAX_LEN;

    // Generic part
    co_write8p(report, MAC_ELTID_MEASUREMENT_REPORT);
    co_write8p(report + MAC_MEAS_REP_TOKEN_OFT, request->req_token);
    co_write8p(report + MAC_MEAS_REP_MODE_OFT, 0);
    co_write8p(report + MAC_MEAS_REP_TYPE_OFT, request->type);
    report_len = MAC_MEAS_REQ_REQ_OFT;

    avail -= MAC_MEAS_REQ_REQ_OFT;

    if (request->type == MAC_MEAS_REQ_TYPE_BEACON)
        report_len += rm_add_bcn_report(report + MAC_MEAS_REQ_REQ_OFT,
                                        avail, request);

    // update report length
    co_write8p(report + MAC_MEAS_REP_LEN_OFT, 
               (uint8_t)(report_len - MAC_INFOELT_INFO_OFT));

    return report_len;
}

/**
 ****************************************************************************************
 * @brief Builds Measurement Report elements for the request
 *
 * It adds as many as possible Measurement Report elements for the specified request in
 * the provided buffer.
 *
 * @param[in] report       HW address where to write the Measurement Report elements
 * @param[in] buf_len      Size, in bytes, available for Measurement Reports
 * @param[in,out] request  Request associated to this report
 * @return Size in bytes of all the Measurement report elements written
 ****************************************************************************************
 */
static uint32_t rm_add_measurement_report_elts(PTR2UINT report,
                            uint16_t buf_len, struct rm_beacon_request *request)
{
    uint32_t reports_len = 0;

    while (rm_can_add_measurement_report(request, buf_len))
    {
        uint32_t report_len =
                rm_add_measurement_report_elt(report, buf_len, request);

        // Sanity check: ensure that value returned by rm_can_add_measurement_report is
        // aligned with rm_add_measurement_report_elt
        ASSERT_ERR(report_len <= buf_len);

        report += report_len;
        buf_len -= report_len;
        reports_len += report_len;

        if (rm_no_more_measure_to_report())
            break;
    }
    
    dbg_con("%s, bcn reports_len %d\r\n", __func__, reports_len);

    // Sanity check: we should always be able to add at least one report
    ASSERT_WARN(reports_len > 0);

    return reports_len;
}

/**
 ****************************************************************************************
 * @brief Builds Measurement Report elements for the rejected request
 *
 * @param[in] report    HW address where to write the Measurement Report elements
 * @param[in] mode      Report Mode field to include in Measurement Report
 * @param[in] request   Request associated to this report
 * @return Size in bytes of The Measurement Report element written
 ****************************************************************************************
 */
static uint32_t rm_add_reject_measurement_report_elt(PTR2UINT report,
                                uint8_t mode, struct rm_beacon_request *request)
{
    co_write8p(report, MAC_ELTID_MEASUREMENT_REPORT);
    co_write8p(report + MAC_MEAS_REP_LEN_OFT, MAC_MEAS_REP_ELMT_MIN_LEN);
    co_write8p(report + MAC_MEAS_REP_TOKEN_OFT, request->req_token);
    co_write8p(report + MAC_MEAS_REP_MODE_OFT, mode);
    co_write8p(report + MAC_MEAS_REP_TYPE_OFT, request->type);

    dbg_con("Send reject report. type=%d mode=%d\r\n", request->type, mode);
    
    return MAC_MEAS_REP_MIN_LEN;
}

/**
 ****************************************************************************************
 * @brief Builds an Radio Measurement Beacon Report action frame
 *
 * @param[in,out] frame     Frame structure that will contain the action frame
 *                          (vif_idx and staid must be initialized in the txdesc)
 * @param[in] init_mac_hdr  Whether it is needed to initialize the Mac Header
 * @param[in] mode          Report Mode field to include in Measurement Report
 * @param[in] request       Request associated to this report
 ****************************************************************************************
 */
static uint32_t rm_build_beacon_report_action_frame(uint8_t *frame,
                                uint8_t mode, struct rm_beacon_request *request)
{
    uint32_t length = 0;
    PTR2UINT action;
    
    // Radio Measurement Action
    action = CPU2HW(frame) + length;
    co_write8p(action++, MAC_RADIO_MEASURE_ACTION_CATEGORY);
    co_write8p(action++, MAC_RM_ACTION_REPORT);
    co_write8p(action++, request->dialog_token);
    length += MAC_RM_ACTION_REP_MEASURE_REP_OFT;

    // Measurement Report elements
    if (mode)
        length += rm_add_reject_measurement_report_elt(action, mode, request);
    else
        length += rm_add_measurement_report_elts(action,
                                           NX_TXFRAME_LEN - length, request);

    return length;
}

/**
 ****************************************************************************************
 * @brief Measurement Report callback
 *
 * If the report has been completely transmitted then the next request is scheduled.
 * Otherwise the frame it updated to include next part of the report and push back for
 * transmission.
 *
 * @param[in] env     Callback registered parameter: Pointer to frame structure
 * @param[in] status  Transmission status
 ****************************************************************************************
 */
static void rm_send_beacon_report_cfm(void *env, uint32_t status)
{
    struct rm_beacon_request *request = &rm_env.requests[rm_env.active_request];

    if (status == 0)
    {
        // Frame has been dropped. Abort this and any pending request
        ke_state_set(TASK_RM, RM_IDLE);
        rm_env.active_request = -1;
        
        return;
    }

    if (rm_no_more_measure_to_report())
    {
        if (ke_state_get(TASK_RM) == RM_REPORTING_WHILE_MEASURING)
            return rm_continue_active_request();
        else
            return rm_schedule_next_request();
    }

    rm_send_beacon_report(request, 0, false);
}

/**
 ****************************************************************************************
 * @brief Sends one frame Radio Measurement Report action frame containing report for the
 * provided request.
 *
 * A single Radio Measurement request may require several Measurement report split among
 * several Radio Measurement report action frame (especially in our case where the
 * frame size is limited). This function sends a single Radio Measurement report action
 * frame (containing as much Measurement Report as possible) and a callback is
 * associated to this frame (@ref rm_send_beacon_report_cfm) to send next part of the
 * report and so on until the full report is sent.
 *
 * @param[in] request  Request to response to
 * @param[in] mode     Report Mode field to include in the Radio Measurement report
 *                     action frame
 *                     If not 0 it means that report is sent only to reject the request
 ****************************************************************************************
 */
static void rm_send_beacon_report(struct rm_beacon_request *request, 
                                           uint8_t mode, bool init_mac_hdr)
{
    struct txl_frame_desc_tag *frame;
    struct txl_frame_act_rm_bcn_report_req *req;

    if ((rm_env.vif_idx >= NX_VIRT_DEV_MAX) || 
        (rm_env.sta_idx >= NX_REMOTE_STA_MAX))
        return;

    frame = txl_frame_get(NX_TXFRAME_LEN + 
                          sizeof(struct txl_frame_act_rm_bcn_report_req));
    if (!frame) {
        dbg_f("%s, no frame\r\n", __func__);
        
        return;
    }

    req = (struct txl_frame_act_rm_bcn_report_req *)txl_frame_payload_get(frame);
    req->frame_type = MAC_FCTRL_ACTION;
    req->act_cat = MAC_RADIO_MEASURE_ACTION_CATEGORY;
    req->vif_idx = rm_env.vif_idx;
    req->sta_idx = rm_env.sta_idx;
    req->init_mac_hdr = init_mac_hdr;
    req->ac = AC_VO;

    ASSERT_WARN(rm_env.tx_frame == NULL);

    if (!mode)
    {
        // If mode != 0, report is sent to indicate that the request has been rejected
        frame->cfm.cfm_func = rm_send_beacon_report_cfm;
        frame->cfm.env = NULL;
        rm_env.tx_frame = frame;
    }

    req->length = rm_build_beacon_report_action_frame(req->action, mode, request);

    txl_frame_set_len(frame, sizeof(struct txl_frame_act_rm_bcn_report_req));

    dbg_f("Send Radio Measurement Report action frame, type:0x%x, cat:0x%x, len=%d, %d %d\r\n", 
          req->frame_type, req->act_cat, req->length, req->vif_idx, req->sta_idx);

    //bl_dump((uint8_t *)req, 30);

    txl_frame_push(frame);
}

/**
 ****************************************************************************************
 * @brief Starts processing a request for a Beacon request of type Table
 *
 * A 'Beacon table' request consists in sending report with the current available measures.
 * All valid measures, filtered by BSSID/SSID if requested, are included in the report
 * list and the first part of the report is sent.
 *
 * @param[in] request  Request parameters
 ****************************************************************************************
 */
static void rm_start_beacon_table_request(struct rm_beacon_request *request)
{
    struct rm_beacon_measure *measure = rm_env.measures;
    int i;

    ke_state_set(TASK_RM, RM_REPORTING);

    rm_init_measure_to_report();
    for (i = 0; i < CO_ARRAY_SIZE(rm_env.measures); i++, measure++)
    {
        if ((measure->status == RM_MEASURE_VALID) &&
            rm_requested_measure(&measure->bssid,
                                 CPU2HW(measure->payload) + measure->ssid_oft,
                                 request))
            rm_add_measure_to_report(measure);
    }

    rm_send_beacon_report(request, 0, true);
}

/**
 ****************************************************************************************
 * @brief Process a request for a Beacon request of type Passive/Active
 *
 * An Active/Passive request consists in doing a scan and sending the result in the
 * report.
 * In order to avoid loosing measures, because of @ref RM_MAX_MEASURES limit, each scan
 * request only contains one channel and results are reported before processing the
 * next channel to scan.
 * Also to be able to set the last report indication correctly, one measure is always
 * kept until we scan ll requested channels.
 *
 * @param[in] request  Request parameters
 * @param[in] init     Whether we start the request or not
 ****************************************************************************************
 */
static void rm_process_beacon_scan_request(
                                   struct rm_beacon_request *request, bool init)
{
    if (init)
    {
        rm_init_measure_to_report();
        rm_reset_measures(true);
        request->start_time = rm_cur_tsf(); // hal_machw_tsf_read();
        request->chan_idx = 0;
    }

    dbg_f("%s %u, RM state:%d\r\n", __func__, __LINE__, ke_state_get(TASK_RM));
    
    if (ke_state_get(TASK_RM) == RM_MEASURING)
    {
        // Send report between each scan request to avoid loosing measure because table
        // is full
        struct rm_beacon_measure *measure;
        int i;

        measure = rm_env.measures;
        for (i = 0; i < CO_ARRAY_SIZE(rm_env.measures); i++, measure++)
        {
            if ((measure->status == RM_MEASURE_VALID) &&
                rm_requested_measure(&measure->bssid,
                                     CPU2HW(measure->payload) + measure->ssid_oft,
                                     request))
                rm_add_measure_to_report(measure);
        }

        // Check if there are other channel to scan
        request->chan_idx++;
        if (request->chan_idx >= request->chan_cnt)
        {
            // Measures are completed, we should always have at least one pending report
            // unless there is nothing to report for the whole request.
            ASSERT_ERR(!rm_no_more_measure_to_report() || 
                       (request->report_sent == 0));
            
            ke_state_set(TASK_RM, RM_REPORTING);
        }
        else
        {
            ke_state_set(TASK_RM, RM_REPORTING_WHILE_MEASURING);

            // If there are no new measure to report immediately start next scan
            if (rm_no_more_measure_to_report())
                return rm_process_beacon_scan_request(request, false);
        }

        rm_send_beacon_report(request, 0, true);
    }
    else
    {
        struct scanu_start_req *scanu;
        struct mac_chan_def *chan = NULL;

        ke_state_set(TASK_RM, RM_MEASURING);
        while (!chan)
        {
            dbg_f("%s %u, request->chan_idx:%d, band:%d\r\n", 
                  __func__, __LINE__, request->chan_idx, request->band_scan);
            
            chan = me_chan_id_to_chan_def(request->band_scan,
                                          request->chan_list[request->chan_idx]);

            if (!chan || (chan->flags & CHAN_DISABLED))
            {
                chan = NULL;
                request->chan_idx++;
                if (request->chan_idx >= request->chan_cnt)
                    return rm_process_beacon_scan_request(request, false);
            }
        }

        dbg_f("%s %u, band:%d, freq:%u\r\n", 
              __func__, __LINE__, chan->band, chan->freq);

        scanu = KE_MSG_ALLOC(SCANU_START_REQ, TASK_SCANU, TASK_RM, scanu_start_req);
        scanu->chan[0] = *chan;
        
        if (request->measure_mode == MAC_MEAS_REQ_BCN_PASSIVE)
            scanu->chan[0].flags |= CHAN_NO_IR;
            
        scanu->chan_cnt = 1;
        scanu->ssid[0].length = request->ssid.length;
        memcpy(scanu->ssid[0].array, request->ssid.array, request->ssid.length);
        scanu->ssid_cnt = 1;
        memcpy(&scanu->bssid, &request->bssid, sizeof(request->bssid));
        scanu->vif_idx = rm_env.vif_idx;
        scanu->duration = request->duration * TU_DURATION;
        
        ke_msg_send(scanu);
    }
}

/*
 ****************************************************************************************
 * PUBLIC FUNCTIONS
 ****************************************************************************************
 */
void rm_init(void)
{
    ke_state_set(TASK_RM, RM_IDLE);
    memset(&rm_env, 0, sizeof(rm_env));
    rm_env.active_request = -1;
}

void rm_reject_request(uint8_t vif_idx, uint8_t sta_idx, uint8_t dialog_token,
                             uint8_t mode, uint8_t type)
{
    struct rm_beacon_request request;
    uint8_t _sta_idx, _vif_idx;

    dbg_con("%s\r\n", __func__);
    
    request.dialog_token = dialog_token;
    request.type = type;
    _sta_idx = rm_env.sta_idx;
    _vif_idx = rm_env.vif_idx;
    rm_env.sta_idx = sta_idx;
    rm_env.vif_idx = vif_idx;
    rm_send_beacon_report(&request, mode, true);
    rm_env.sta_idx = _sta_idx;
    rm_env.vif_idx = _vif_idx;
}


void rm_initialize_requests(uint8_t vif_idx, uint8_t sta_idx, 
                               uint8_t dialog_token, PTR2UINT *req, int req_cnt)
{
    struct rm_beacon_request *request = rm_env.requests;
    int i;

    rm_env.sta_idx = sta_idx;
    rm_env.vif_idx = vif_idx;
    rm_env.request_cnt = 0;
    rm_env.active_request = -1;

    dbg_con("{VIF-%d}{STA-%d} %d RM request received (dialog_token=%d)",
             vif_idx, sta_idx, req_cnt, dialog_token);
             
    for (i = 0 ; i < req_cnt; i++)
    {
        uint8_t reject = 
               rm_parse_measurement_request(dialog_token, req[i], request);

        if (reject)
        {
            rm_reject_request(vif_idx, sta_idx, dialog_token, reject,
                              co_read8p(req[i] + MAC_MEAS_REQ_TYPE_OFT));
            return;
        }
        else if (request->dialog_token == dialog_token)
        {
            rm_env.request_cnt++;
            request++;
        }
    }
}

void rm_schedule_next_request(void)
{
    rm_env.active_request++;
    rm_reset_measures(false);
    if (rm_env.active_request >= rm_env.request_cnt)
    {
        ke_state_set(TASK_RM, RM_IDLE);
        rm_env.active_request = -1;
        return;
    }

    ke_state_set(TASK_RM, RM_READY_PROCESS);
    ke_msg_send_basic(RM_PROCESS_NEXT_REQUEST_IND, TASK_RM, TASK_RM);
}

void rm_start_active_request(void)
{
    struct rm_beacon_request *request = &rm_env.requests[rm_env.active_request];

    dbg_con("Start processing request %d", rm_env.active_request);
    ASSERT_ERR(request->type == MAC_MEAS_REQ_TYPE_BEACON);

    switch (request->measure_mode)
    {
        case MAC_MEAS_REQ_BCN_PASSIVE:
        case MAC_MEAS_REQ_BCN_ACTIVE:
            rm_process_beacon_scan_request(request, true);
            break;
        case MAC_MEAS_REQ_BCN_TABLE:
            rm_start_beacon_table_request(request);
            break;
        default:
            ASSERT_ERR(0);
            break;
    }
}

void rm_continue_active_request(void)
{
    struct rm_beacon_request *request = &rm_env.requests[rm_env.active_request];
    ASSERT_ERR(rm_env.active_request >= 0);
    ASSERT_ERR(request->type == MAC_MEAS_REQ_TYPE_BEACON);

    switch (request->measure_mode)
    {
        case MAC_MEAS_REQ_BCN_PASSIVE:
        case MAC_MEAS_REQ_BCN_ACTIVE:
            rm_process_beacon_scan_request(request, false);
            break;
        default:
            ASSERT_ERR(0);
            break;
    }
}

void rm_new_beacon_measure(struct bcn_frame const *bcn, 
                      uint32_t bcn_length, uint32_t tsflo, uint16_t freq, 
                      enum mac_chan_band band, int8_t rssi, uint8_t antenna_set)
{
    struct rm_beacon_measure *measure;
    struct rm_beacon_request *request = NULL;
    PTR2UINT bcn_elt, data;
    int remain_data, remain_bcn;
    enum mac_chan_bandwidth bw = PHY_CHNL_BW_20;
    uint8_t sec_offset = 0;
    struct mac_addr addr3;

    MAC_ADDR_EXTRACT(&addr3, &bcn->h.addr3);

    if (rm_env.active_request >= 0)
        request = &rm_env.requests[rm_env.active_request];

    if (!rm_requested_measure(&addr3, 0, request))
        return;

    measure = rm_get_measure_slot(&addr3, rssi);
    if (!measure)
        return;

    if (measure->status == RM_MEASURE_REPORTED)
        return;

    //MAC_ADDR_CPY(&measure->bssid, &addr3);
    memcpy(&measure->bssid, &addr3, sizeof(measure->bssid));
    measure->antenna_set = antenna_set;
    measure->tsflo = tsflo;
    measure->phy_type = 
              (band == PHY_BAND_2G4) ? DOT11PHY_HRDSSS : DOT11PHY_OFDM;

    // Save frame payload
    data = CPU2HW(measure->payload);
    bcn_elt = CPU2HW(bcn->variable);

    // First fixed size fields
    co_copy8p(data, (PTR2UINT)&bcn->tsf, BCN_FIXED_SIZE_FIELDS_LEN);
    data += BCN_FIXED_SIZE_FIELDS_LEN;

    remain_bcn = bcn_length - offsetof_b(struct bcn_frame, variable);
    remain_data = sizeof_b(measure->payload) - BCN_FIXED_SIZE_FIELDS_LEN;

    // Then save each Element one by one until measure buffer is full
    // The frame elements are still read till the end to get phy/bandwidth info
    while (remain_bcn > 2)
    {
        uint8_t elt_id = co_read8p(bcn_elt);
        uint16_t elt_len = mac_ie_len(bcn_elt);
        uint8_t elt_ext = co_read8p(bcn_elt + MAC_INFOELT_EXT_ID_OFT);

        if (elt_id == MAC_ELTID_SSID)
        {
            measure->ssid_oft = (uint8_t)(data - CPU2HW(measure->payload));
            if (!rm_requested_measure(&measure->bssid, bcn_elt, request))
            {
                measure->status = RM_MEASURE_INVALID;
                return;
            }
        }
        else if (elt_id == MAC_ELTID_DS)
        {
            measure->ds_oft = (uint8_t)(data - CPU2HW(measure->payload));
            measure->chan = co_read8p(bcn_elt + MAC_DS_CHANNEL_OFT);
        }
        else if ((measure->phy_type < DOT11PHY_ERP) && (elt_id == MAC_ELTID_ERP))
        {
            measure->phy_type = DOT11PHY_ERP;
        }
        else if ((measure->phy_type < DOT11PHY_HT) &&
                 (elt_id == MAC_ELTID_HT_OPERATION) && 
                 (elt_len == MAC_HT_OPER_LEN))
        {
            measure->phy_type = DOT11PHY_HT;
            sec_offset = co_read8p(bcn_elt + MAC_HT_OPER_INFO_OFT) &
                MAC_HT_OPER_OP_MODE_MASK;
            if (sec_offset)
                bw = PHY_CHNL_BW_40;
        }
        else if ((measure->phy_type < DOT11PHY_VHT) &&
                 (elt_id == MAC_ELTID_VHT_OPERATION) && 
                 (elt_len == MAC_VHT_OPER_LEN))
        {
            measure->phy_type = DOT11PHY_VHT;
            rm_update_vht_bw(co_read8p(bcn_elt + MAC_VHT_CHAN_WIDTH_OFT), &bw);
        }
        else if ((measure->phy_type < DOT11PHY_HE) && (elt_id == MAC_ELTID_EXT) &&
                 (elt_len >= MAC_HE_OPER_MIN_LEN) &&
                 (elt_ext == MAC_ELTID_EXT_HE_OPERATION))
        {
            if ((measure->phy_type != DOT11PHY_VHT) &&
                (elt_len >= MAC_HE_OPER_MIN_LEN + MAC_VHT_OPER_INFO_LEN))
            {
                uint32_t he_oper = 
                    co_read32p((void *)(bcn_elt + MAC_HE_OPER_PARAM_OFT));
                
                if (he_oper & MAC_HE_OPER_VHT_OPER_PRESENT_BIT)
                    rm_update_vht_bw(co_read8p(bcn_elt +
                                     MAC_HE_OPER_VHT_OPER_INFO_OFT), &bw);
            }
            
            measure->phy_type = DOT11PHY_HE;
        }
        
        if ((remain_data >= elt_len) && 
             rm_requested_elt(elt_id, elt_ext, request))
        {
            co_copy8p(data, bcn_elt, elt_len);
            data += elt_len;
            remain_data -= elt_len;
        }

        bcn_elt += elt_len;
        remain_bcn -= elt_len;
    }

    measure->len = sizeof_b(measure->payload) - remain_data;

    if (measure->ds_oft)
    {
        measure->rssi = rssi;
    }
    else if ((measure->status == RM_MEASURE_INVALID) || (rssi > measure->rssi))
    {
        measure->rssi = rssi;
        measure->chan = phy_freq_to_channel(band, freq);
    }

    measure->op_class = rm_chan_to_op_class(measure->chan, band, bw, sec_offset);
    measure->status = RM_MEASURE_VALID;

    dbg_con("[%d] BSSID=%pM rssi=%d op_class=%d chan=%d phy_type=%d tsf=%d len=%d\r\n",
             CO_GET_INDEX(measure, rm_env.measures), &measure->bssid,
             measure->rssi, measure->op_class, measure->chan, 
             measure->phy_type, measure->tsflo, measure->len);

    if (measure->ssid_oft)
    {
        #if 0
        PTR2UINT ssid_elt = CPU2HW(measure->payload) + measure->ssid_oft;
                       
        dbg("SSID: %pBs\r\n", co_read8p(ssid_elt + MAC_INFOELT_LEN_OFT),
             HW2CPU(ssid_elt + MAC_SSID_SSID_OFT));
        #endif
    }
}

#endif // NX_RM

