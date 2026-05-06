/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "rwnx_config.h"
#if NX_FTM_INITIATOR
#include "ftm.h"
#include "ftm_task.h"
#include "scanu_task.h"
#include "txl_cntrl.h"
#include "txl_frame.h"
#include "vif_mgmt.h"
#include "mac_frame.h"
#include "mfp.h"
#include "tpc.h"
#include "me_mgmtframe.h"

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */
/// FTM module environment definition.
struct ftm_env_tag ftm_env;

void ftm_scan(void)
{
    int i, j;
    // Prepare the scan request
    struct scanu_start_req *req = KE_MSG_ALLOC(SCANU_START_REQ,
                                               TASK_SCANU, TASK_FTM,
                                               scanu_start_req);

    // fill in message
    req->vif_idx = ftm_env.vif_idx;
    req->add_ies = 0;
    req->add_ie_len = 0;
    req->duration = 0;
    req->ssid_cnt = 1;
    // wildcard BSSID
    req->bssid.array[0] = 0xffff;
    req->bssid.array[1] = 0xffff;
    req->bssid.array[2] = 0xffff;

#if CFG_5G
    struct mac_chan_def *chan[PHY_BAND_MAX] = 
                              {me_env.chan.chan2G4, me_env.chan.chan5G};
    uint8_t chan_cnt[PHY_BAND_MAX] = 
                              {me_env.chan.chan2G4_cnt, me_env.chan.chan5G_cnt};

    req->chan_cnt = 0;
    for (i = 0; i < PHY_BAND_MAX; i++)
        for (j = 0; j < chan_cnt[i]; j++)
            if (!(chan[i][j].flags & CHAN_DISABLED))
                req->chan[req->chan_cnt++] = chan[i][j];
#else
    struct mac_chan_def *chan = me_env.chan.chan2G4;
    uint8_t chan_cnt = me_env.chan.chan2G4_cnt;

    req->chan_cnt = 0;
    for (i = 0; i < chan_cnt; i++)
        if (!(chan[i].flags & CHAN_DISABLED))
            req->chan[req->chan_cnt++] = chan[i];
#endif

    // Send the message
    ke_msg_send(req);
}

void ftm_remain_on_channel(uint8_t operation_code)
{
    uint8_t ftm_idx = ftm_env.ftm_current_idx;

    // Start a remain on channel procedure in order to start the negociation
    struct mm_remain_on_channel_req *req = KE_MSG_ALLOC(MM_REMAIN_ON_CHANNEL_REQ,
                                                        TASK_MM, TASK_FTM,
                                                        mm_remain_on_channel_req);

    // Fill request parameters
    req->op_code   = operation_code;
    req->vif_index = ftm_env.vif_idx;
    req->chan.band = ftm_env.ftm_list_sta[ftm_idx].chan.band;
    req->chan.flags = ftm_env.ftm_list_sta[ftm_idx].chan.flags;
    req->chan.prim20_freq= ftm_env.ftm_list_sta[ftm_idx].chan.freq;
    req->chan.tx_power = ftm_env.ftm_list_sta[ftm_idx].chan.tx_power;
    req->chan.center1_freq = ftm_env.ftm_list_sta[ftm_idx].chan.freq;
    req->chan.center2_freq = 0;
    req->duration_ms  = DEFAULT_REMAIN_ON_CHANNEL_TIME;

    if (operation_code == MM_ROC_OP_START)
    {
        TRACE_FTM(INF, "Remain on channel request on frequency %d ",
                  ftm_env.ftm_list_sta[ftm_idx].chan.freq);
    }

    ke_msg_send(req);
}

void ftm_send_start_cfm(uint8_t status, uint8_t vif_idx)
{
    struct ftm_start_cfm *cfm = KE_MSG_ALLOC(FTM_START_CFM,
                                             TASK_API, TASK_FTM,
                                             ftm_start_cfm);

    cfm->status = status;
    cfm->vif_idx = vif_idx;

    ke_msg_send(cfm);
}
void ftm_close_session(void)
{
    // Save the measurement done in this session in the FTM_DONE_IND message
    if (ftm_env.ftm_meas_count)
    {
        struct mac_ftm_results *res = &ftm_env.ind->results;
        int idx = res->nb_ftm_rsp;

        res->nb_ftm_rsp++;
        res->meas[idx].addr = 
                    ftm_env.ftm_list_sta[ftm_env.ftm_current_idx].bssid;
        res->meas[idx].rtt = 
                    (uint32_t) (ftm_env.ftm_sum_meas / ftm_env.ftm_meas_count);

        TRACE_FTM(INF, "Store measurement %d %pM %d, on %d received", 
                  res->nb_ftm_rsp, TR_MAC(&res->meas[idx].addr.array), 
                  res->meas[idx].rtt, ftm_env.ftm_meas_count);
    }

    // Close the session
    ke_timer_set(FTM_CLOSE_SESSION_REQ, TASK_FTM, 1000);
    ke_state_set(TASK_FTM, FTM_CLOSING_SESSION);
}

bool ftm_send_action_frame(uint8_t action, struct mac_addr *dest_addr)
{
    struct txl_frame_desc_tag *frame;
    struct mac_hdr *buf;
    struct tx_hd *thd;
    struct softmac_vif_info_tag *vif = &vif_info_tab[ftm_env.vif_idx];
    uint32_t length = 0;

    // Allocate a frame descriptor from the TX path
    frame = txl_frame_get(TX_DEFAULT_5G,NX_TXFRAME_LEN);

    if (!frame)
        return 0;

    // update Tx power in policy table
    tpc_update_frame_tx_power(vif, frame);
    // Get the buffer pointer
    buf = txl_buffer_payload_get(&frame->txdesc);
    // Fill-in the frame
    buf->fctl = MAC_FCTRL_ACTION;
    // Set Destination Address
    MAC_ADDR_CPY(&buf->addr1, dest_addr);
    // Set Source Address
    MAC_ADDR_CPY(&buf->addr2, &(vif->mac_addr));
    // Set BSSID
    MAC_ADDR_CPY(&buf->addr3, &(vif->mac_addr));
    // Update the sequence number in the frame
    buf->seq = txl_get_seq_ctrl();
    // Set VIF and STA indexes
    frame->txdesc.host.vif_idx = ftm_env.vif_idx;
    frame->txdesc.host.staid   = 0xFF;

    length = MAC_SHORT_MAC_HDR_LEN;

    #if NX_MFP
    frame->txdesc.umac.head_len = 0;
    frame->txdesc.umac.tail_len = 0;
    if (MFP_UNICAST_PROT == mfp_protect_mgmt_frame(&frame->txdesc, buf->fctl,
                                                   MAC_PUBLIC_ACTION_CATEGORY))
    {
        txu_cntrl_protect_mgmt_frame(&frame->txdesc, buf, MAC_SHORT_MAC_HDR_LEN);
        length += frame->txdesc.umac.head_len;
    }
    #endif

    co_write8p(CPU2HW(buf)+ length, MAC_PUBLIC_ACTION_CATEGORY);

    switch (action)
    {
        case (FTM_INITIAL_FRAME_REQ):
            TRACE_FTM(INF, "Send Initial Action Frame to bssid = %pM ", TR_MAC(&buf->addr1));
            co_write8p(CPU2HW(buf)+ length + MAC_ACTION_ACTION_OFT, FTM_MEAS_REQ);
            co_write8p(CPU2HW(buf) + length + FTM_REQ_TRIGGER, 1);
            length += 3;
            length += me_ftm_add_parameter(CPU2HW(buf) + length, ftm_env.ftm_per_burst);
            break;
        case (FTM_START_FRAME_REQ):
            TRACE_FTM(INF, "Send Measurement Request Action Frame to bssid = %pM ",
                      TR_MAC(&buf->addr1));
            co_write8p(CPU2HW(buf)+ length + MAC_ACTION_ACTION_OFT, FTM_MEAS_REQ);
            co_write8p(CPU2HW(buf) + length + FTM_REQ_TRIGGER, 1);
            length += 3;
            break;
        #if NX_FAKE_FTM_RSP
        case (FTM_MEAS_RSP):
        {
            uint64_t tod = (hal_machw_time() * 1000000) & 0xFFFFFFFF;
            uint32_t toa_addr;
            struct rx_leg_info leg;
            uint64_t frame_dur_plus_sifs;

            TRACE_FTM(INF, "Send Measurement Reply Action Frame to bssid = %pM ",
                      TR_MAC(&buf->addr1));

            co_write8p(CPU2HW(buf) + length + MAC_ACTION_ACTION_OFT, FTM_MEAS);
            co_write8p(CPU2HW(buf) + length + FTM_MEAS_DIAG_TOKEN, 1);
            co_write8p(CPU2HW(buf) + length + FTM_MEAS_FLW_DIAG_TOKEN, 0);
            co_copy8p(CPU2HW(buf) + length + FTM_MEAS_TOD, CPU2HW(&tod), 6);
            toa_addr = CPU2HW(buf) + length + FTM_MEAS_TOA;
            co_write8p(CPU2HW(buf) + length + FTM_MEAS_TOD_ERR,6);
            co_write8p(CPU2HW(buf) + length + FTM_MEAS_TOA_ERR,8);
            length += 20;
            length += me_ftm_add_parameter(CPU2HW(buf) + length, ftm_env.ftm_per_burst);

            leg.ch_bw = 0;
            leg.format_mod = FORMATMOD_NON_HT;
            leg.leg_length = length + MAC_FCS_LEN;
            leg.leg_rate = mac2rxvrate[HW_RATE_6MBPS];
            leg.pre_type = 0;
            frame_dur_plus_sifs = ftm_frame_duration_ps(&leg) + FTM_SIFS_DURATION_IN_PS;
            // Add a fixed value to the delay, that we should find as is on the initiator
            tod += frame_dur_plus_sifs + 9999;
            co_copy8p(toa_addr, CPU2HW(&tod), 6);
            break;
        }
        #endif
        case (FTM_STOP_FRAME_REQ):
            TRACE_FTM(INF, "Send Action Frame to stop FTM to bssid = %pM ", 
                      TR_MAC(&buf->addr1));
            co_write8p(CPU2HW(buf)+ length + MAC_ACTION_ACTION_OFT, FTM_MEAS_REQ);
            co_write8p(CPU2HW(buf) + length + FTM_REQ_TRIGGER, 0);
            length += 3;
            break;
        default:
            break;
    }

    #if NX_MFP
    length += frame->txdesc.umac.tail_len;
    #endif
    thd             = tx_desc_thd(&frame->txdesc);
    thd->dataendptr = (uint32_t)thd->datastartptr + length - 1;
    thd->frmlen     = length + MAC_FCS_LEN;

    txl_frame_set_len(frame, NX_TXFRAME_LEN);

    // Push the frame for TX
    txl_frame_push(frame, AC_VO);

    softmac_free_skb(frame->skb);

    return 1;
}

void sort_bssid_by_rssi(uint8_t nb_bssid, struct mac_scan_result *list_result[])
{
    for (int i = 0;i < FTM_RSP_MAX && i < nb_bssid; i++)
    {
        int current_bssid = 0;

        for (int j = 0; j < nb_bssid; j++)
        {
            if(list_result[j]->rssi > list_result[current_bssid]->rssi)
            {
                current_bssid = j;
            }
        }
        
        ftm_env.ftm_list_sta[i].bssid = list_result[current_bssid]->bssid;
        ftm_env.ftm_list_sta[i].rssi = list_result[current_bssid]->rssi;
        ftm_env.ftm_list_sta[i].chan = *list_result[current_bssid]->chan;
        TRACE_FTM(INF, "Ordering AP: BSSID=%pM RSSI = %d FREQ = %d",
                  TR_MAC(&list_result[current_bssid]->bssid.array),
                  list_result[current_bssid]->rssi,
                  ftm_env.ftm_list_sta[i].chan.freq)
                  
        list_result[current_bssid]->rssi = -128;
    }
}

void ftm_measurement_scheduling(void)
{
    ftm_env.ftm_current_idx++;

    // Check if new sessions shall be started
    if (ftm_env.ftm_current_idx > (ftm_env.nb_ftm_rsp-1))
    {
        // No sessions, to start, end the FTM procedure
        ke_msg_send(ftm_env.ind);
        ke_state_set(TASK_FTM, FTM_IDLE);
        TRACE_FTM(INF, "-------------- End of FTM request --------------")
        return;
    }

    TRACE_FTM(INF, "New FTM session started on %pM",
              TR_MAC(&ftm_env.ftm_list_sta[ftm_env.ftm_current_idx].bssid));

    ftm_env.current_ftm_per_burst = ftm_env.ftm_per_burst;
    ftm_env.ftm_meas_count = 0;
    ftm_env.ftm_sum_meas = 0;

    ftm_remain_on_channel(MM_ROC_OP_START);
    ke_state_set(TASK_FTM, FTM_WAITING_ROC_START);
}

uint32_t ftm_get_ftm_params(uint32_t frame)
{
    uint8_t elemt_id;
    while(1)
    {
        elemt_id = co_read8p(CPU2HW(frame));
        switch (elemt_id)
        {
            case (MAC_ELTID_FTM_PARAMS):
                return (frame + 2);
                break;
            case (MAC_ELTID_MEASUREMENT_REQUEST):
                frame = frame + co_read8p(CPU2HW(frame + 1));
                break;
            default:
                return 0;
                break;
        }
    }
}

uint64_t ftm_frame_duration_ps(const struct rx_leg_info *rx_leg_inf)
{
    // Ensure that received frame is of type NON_HT
    if (rx_leg_inf->format_mod != 0)
        return 500;

    // Fill-in the TimeOnAir parameter registers
    nxmac_ppdu_mcs_index_setf(rxv2macrate[rx_leg_inf->leg_rate]);
    #if (NX_MAC_VER >= 20)
    nxmac_time_on_air_param_1_pack(0, 0, 0, 0, 
                                   rx_leg_inf->pre_type, rx_leg_inf->leg_length);
    #else
    nxmac_time_on_air_param_1_pack(0, 0, 0, rx_leg_inf->pre_type,
                                   0, rx_leg_inf->leg_length);
    #endif // (NX_MAC_VER >= 20)

    // Compute the duration
    nxmac_compute_duration_setf(1);
    while ((nxmac_time_on_air_value_get() & (NXMAC_COMPUTE_DURATION_BIT |
                                             NXMAC_TIME_ON_AIR_VALID_BIT))
            != NXMAC_TIME_ON_AIR_VALID_BIT);
    ASSERT_REC_VAL(nxmac_time_on_air_valid_getf() != 0, 500);

    // Retrieve the duration
    return ((uint64_t)nxmac_time_on_air_getf()) * 1000000;
}

#endif // NX_FTM_INITIATOR
