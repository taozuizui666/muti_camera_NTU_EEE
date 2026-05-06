/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "rwnx_config.h"
#include "ftm_task.h"
//#include "scanu_task.h"
//#include "scanu.h"
#include "mac.h"
#include "rxu_task.h"
#include "vif_mgmt.h"

#if NX_FTM_INITIATOR
/*
 * API MESSAGES HANDLERS
 ****************************************************************************************
 */
/*
 * PUBLIC VARIABLES DECLARATION
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief Handle reception of the @ref FTM_START_REQ message.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 *
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int ftm_start_req_handler(ke_msg_id_t const msgid,
                                       struct ftm_start_req *param,
                                       ke_task_id_t const dest_id,
                                       ke_task_id_t const src_id)
{

    int status = CO_OK;

    #if NX_FAKE_FTM_RSP
    struct softmac_vif_info_tag *vif = &vif_info_tab[ftm_env.vif_idx];
    if (vif->type == VIF_AP)
    {
        ftm_env.vif_idx = param->vif_idx;
        goto send_cfm;
    }
    #endif

    if (ke_state_get(TASK_FTM) != FTM_IDLE)
    {
        status = CO_FAIL;
        goto send_cfm;
    }

    ftm_env.vif_idx = param->vif_idx;
    ftm_env.ind = KE_MSG_ALLOC(FTM_DONE_IND, TASK_API, TASK_FTM, ftm_done_ind);
    ftm_env.ind->vif_idx = param->vif_idx;
    ftm_env.ind->results.nb_ftm_rsp = 0;
    ftm_env.ftm_per_burst = param->ftm_per_burst;
    ftm_env.nb_ftm_rsp = co_min(param->nb_ftm_rsp, FTM_RSP_MAX);

    // Scan to find all active AP
    ftm_scan();

    // Waiting for the scan results
    ke_state_set(TASK_FTM, FTM_SCANNING);

  send_cfm:
    ftm_send_start_cfm(status, param->vif_idx);
    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref SCANU_START_CFM message handler.
 *
 * This function handles scan confirmation from the scan module.
 * @param[in] msgid Id of the message received.
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 *
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int scanu_start_cfm_handler(ke_msg_id_t const msgid,
                                           struct scanu_start_cfm const *param,
                                           ke_task_id_t const dest_id,
                                           ke_task_id_t const src_id)
{
    int index_result, index_bssid = 0;
    struct mac_scan_result *list_scan_ftm_support[SCANU_MAX_RESULTS];

    // Sanity check - This message can be received only in the SCANNING states
    ASSERT_ERR(ke_state_get(TASK_FTM) == FTM_SCANNING);

    for (index_result = 0 ;index_result < param->result_cnt; index_result++)
    {
        struct mac_scan_result *result_ptr;
        result_ptr = scanu_get_result_from_idx(index_result);

        if (result_ptr->ftm_support)
        {
            list_scan_ftm_support[index_bssid++] = result_ptr;
            TRACE_FTM(INF, "New AP found on %pM index_bssid = %d freq= %d rssi = %d",
                            TR_MAC(&result_ptr->bssid.array), index_bssid-1,
                            result_ptr->chan->freq, result_ptr->rssi)
        }
    }

    // Check we have the number of responders we expected, and correct it if necessary
    if (index_bssid < ftm_env.nb_ftm_rsp)
        ftm_env.nb_ftm_rsp = index_bssid;

    sort_bssid_by_rssi(index_bssid, list_scan_ftm_support);

    // Start Measurement scheduling
    ftm_env.ftm_current_idx = -1;
    ftm_measurement_scheduling();

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref MM_REMAIN_ON_CHANNEL_CFM message handler.
 *
 * This function handles remain on channel confirmation.
 * @param[in] msgid Id of the message received.
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 *
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int mm_remain_on_channel_cfm_handler(ke_msg_id_t const msgid,
                                    struct mm_remain_on_channel_cfm const *param,
                                    ke_task_id_t const dest_id,
                                    ke_task_id_t const src_id)
{
    if (param->op_code == MM_ROC_OP_START)
    {
        // Sanity check - This message can be received only when starting RoC
        ASSERT_ERR(ke_state_get(TASK_FTM) == FTM_WAITING_ROC_START);

        // It failed for some reason, proceed to next FTM session
        if (param->status != CO_OK)
            ftm_measurement_scheduling();
    }
    else
    {
        // Sanity check - This message can be received during session closing
        ASSERT_ERR(ke_state_get(TASK_FTM) == FTM_CLOSING_SESSION);

        // Proceed to next FTM session
        ftm_measurement_scheduling();
    }

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref MM_CHANNEL_SWITCH_IND message handler.
 *
 * This function handles remain on channel confirmation.
 * @param[in] msgid Id of the message received.
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 *
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int mm_channel_switch_ind_handler(ke_msg_id_t const msgid,
                                         struct mm_channel_switch_ind const *param,
                                         ke_task_id_t const dest_id,
                                         ke_task_id_t const src_id)
{
    if (ftm_send_action_frame(FTM_INITIAL_FRAME_REQ,
                                  &ftm_env.ftm_list_sta[ftm_env.ftm_current_idx].bssid))
        ke_state_set(TASK_FTM, FTM_ESTABLISHING_SESSION);
    else
    {
        TRACE_FTM(ERR, "Action Frame has not been sent");
    }

    // Start action frame timeout timer
    ke_timer_set(FTM_PEER_RSP_TIMEOUT_IND, TASK_FTM, DEFAULT_ACTION_FRAME_TIMEOUT);

    return KE_MSG_CONSUMED;
}
/**
 ****************************************************************************************
 * @brief @ref RXU_MGT_IND message handler.
 *
 * This function handles the different frames that can be received for the FTM measurement
 *
 * @param[in] msgid Id of the message received.
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 *
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int rxu_mgt_ind_handler(ke_msg_id_t const msgid,
                                      struct rxu_mgt_ind const *param,
                                      ke_task_id_t const dest_id,
                                      ke_task_id_t const src_id)
{
    uint32_t frame = CPU2HW(param->payload);
    uint8_t action = co_read8p(frame + sizeof(struct mac_hdr) + MAC_ACTION_ACTION_OFT);
    struct mac_addr addr2;
    struct mac_hdr *mac_hdr = (struct mac_hdr *)(HW2CPU(param->payload));
    
    MAC_ADDR_EXTRACT(&addr2, &mac_hdr->addr2 );

    #if NX_FAKE_FTM_RSP
    struct softmac_vif_info_tag *vif = &vif_info_tab[ftm_env.vif_idx];
    if (vif->type == VIF_AP)
    {
        if (action == FTM_MEAS_REQ)
        {
            ke_state_t state = ke_state_get(TASK_FTM);
            switch (state)
            {
                case FTM_IDLE:
                {
                    TRACE_FTM(INF, "FTM AP -->  INITIAL FTM_MEAS_REQ");
                    uint32_t meas_frame = frame + sizeof(struct mac_hdr);

                    if (co_read8p(CPU2HW(meas_frame) + FTM_REQ_PARAMS) == MAC_ELTID_FTM_PARAMS)
                        ftm_env.ftm_per_burst = 
                           FTM_PARAMS_VAL_GET(meas_frame + FTM_REQ_PARAMS + 2,FTM_PER_BURST);

                    ke_state_set(TASK_FTM, FTM_ESTABLISHING_SESSION);
                    if (!ftm_send_action_frame(FTM_MEAS_RSP, &addr2))
                        TRACE_FTM(INF, "Action Frame has not been sent");
                    break;
                }
                case FTM_ESTABLISHING_SESSION:
                {
                    TRACE_FTM(INF,"FTM AP --> FTM_MEAS_REQ");
                    MAC_ADDR_CPY(&ftm_env.ap_mac_addr,&addr2);
                    ke_state_set(TASK_FTM, FTM_MEAS_INITIAL_EXCHANGE);
                    
                    if (!ftm_send_action_frame(FTM_MEAS_RSP, &addr2))
                        TRACE_FTM(INF, "Action Frame has not been sent");
                        
                    ke_timer_set(FTM_AP_MEASUREMENT, TASK_FTM, DEFAULT_MEAS_FRAME_SENT_TIMEOUT);
                    ftm_env.ftm_per_burst--;
                    break;
                }
                default:
                    break;
            }
        }
    }
    else
    #endif
    {
        if(!MAC_ADDR_CMP(&ftm_env.ftm_list_sta[ftm_env.ftm_current_idx].bssid,&addr2))
        {
            TRACE_FTM(ERR,"Action Frame received from incorrect AP %pM",
                      TR_MAC(&ftm_env.ftm_list_sta[ftm_env.ftm_current_idx].bssid));
            return KE_MSG_CONSUMED;
        }

        ke_timer_clear(FTM_PEER_RSP_TIMEOUT_IND, TASK_FTM);

        switch (action)
        {
            case FTM_MEAS_REQ:
            {
                TRACE_FTM(ERR,"FTM Measurement request is not supported");
                break;
            }
            case FTM_MEAS:
            {
                ke_state_t state = ke_state_get(TASK_FTM);
                switch (state)
                {
                    case FTM_ESTABLISHING_SESSION:
                    {
                        TRACE_FTM(INF,"Initial Request Reply received");

                        // Check status indication
                        uint8_t status_indication;
                        uint32_t meas_frame = 
                             ftm_get_ftm_params(frame + sizeof(struct mac_hdr) + FTM_MEAS_PARAMS);

                        if (meas_frame != 0)
                        {
                            status_indication = 
                               FTM_PARAMS_VAL_GET(meas_frame,STATUS_INDICATION);
                               
                            if (status_indication != 1)
                            {
                                TRACE_FTM(ERR,"Status Indication = %d",status_indication);
                                // Close the current session
                                ftm_close_session();
                                break;
                            }
                        }
                        else
                        {
                            TRACE_FTM(ERR,"No FTM parameter present in the frame");
                            // Close the current session
                            ftm_close_session();
                            break;
                        }

                        ke_state_set(TASK_FTM, FTM_MEAS_INITIAL_EXCHANGE);
                        // Start action frame timeout timer
                        ke_timer_set(FTM_MEASUREMENT_REQ, TASK_FTM,
                                     DEFAULT_MEAS_REQUEST_TIME);
                        break;
                    }
                    case FTM_MEAS_INITIAL_EXCHANGE:
                    {
                        TRACE_FTM(INF,"Initial Measurement received");
                        ke_state_set(TASK_FTM, FTM_MEAS_EXCHANGE);
                        ftm_env.current_ftm_per_burst --;
                        ke_timer_set(FTM_PEER_RSP_TIMEOUT_IND, TASK_FTM, 
                                     DEFAULT_MEAS_FRAME_TIMEOUT);
                        break;
                    }
                    case FTM_MEAS_EXCHANGE:
                    {
                        uint64_t tod, toa;
                        uint64_t duration_of_frame;

                        // Compute the local time at first bit of timestamp
                        duration_of_frame = ftm_frame_duration_ps(&param->rx_leg_inf);

                        uint32_t meas_frame = frame + sizeof(struct mac_hdr);

                        co_copy8p(CPU2HW(&toa),CPU2HW(meas_frame) + FTM_MEAS_TOA,6);
                        co_copy8p(CPU2HW(&tod),CPU2HW(meas_frame) + FTM_MEAS_TOD,6);

                        TRACE_FTM(INF,"Measurement received");

                        ftm_env.ftm_sum_meas += (uint32_t)((toa - tod) - 
                                   duration_of_frame - FTM_SIFS_DURATION_IN_PS);
                        ftm_env.ftm_meas_count++;

                        ftm_env.current_ftm_per_burst--;

                        // Check if we got all the needed measurements
                        if (ftm_env.current_ftm_per_burst)
                        {
                            // Additional measurement expected - Just wait for the next
                            // FTM packet
                            // Give time to send the ack (and receive again a measurement in case
                            // ftm_per_burst is not null
                            ke_timer_set(FTM_PEER_RSP_TIMEOUT_IND, TASK_FTM,
                                         DEFAULT_MEAS_FRAME_TIMEOUT);
                        }
                        else
                        {
                            TRACE_FTM(INF,"Last expected measurement received");

                            // Close the session
                            ftm_close_session();
                        }
                        break;
                    }
                    default:
                    {
                        TRACE_FTM(ERR,"This point should never be reached")
                        break;
                    }
                }
                break;
            }
            default:
            {
                TRACE_FTM(ERR,"This point should never be reached")
                break;
            }
        }
    }
    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref FTM_PEER_RSP_TIMEOUT_IND message handler.
 *
 * This message indicates we did not get the expected message from the peer after a
 * certain. Close the session.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 *
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int ftm_peer_rsp_timeout_ind_handler(ke_msg_id_t const msgid,
                                                        void const *param,
                                                        ke_task_id_t const dest_id,
                                                        ke_task_id_t const src_id)
{
    // Check if we are in a state where such timeout could occur
    ASSERT_ERR ((ke_state_get(TASK_FTM) == FTM_ESTABLISHING_SESSION) ||
                (ke_state_get(TASK_FTM) == FTM_MEAS_INITIAL_EXCHANGE) ||
                (ke_state_get(TASK_FTM) == FTM_MEAS_EXCHANGE));

    // Close the session
    ftm_close_session();

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref FTM_MEAS_REQ message handler.
 *
 * This message is used to schedule a FTM request measurement
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 *
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int ftm_meas_req_handler(ke_msg_id_t const msgid,
                                        void const *param,
                                        ke_task_id_t const dest_id,
                                        ke_task_id_t const src_id)
{
    TRACE_FTM(INF,"Request a new measurement")

    // Start action frame timeout timer
    ke_timer_set(FTM_PEER_RSP_TIMEOUT_IND, TASK_FTM, DEFAULT_ACTION_FRAME_TIMEOUT);

    // Check if we are in a state where such timeout could occur
    if (ke_state_get(TASK_FTM) != FTM_MEAS_INITIAL_EXCHANGE)
        return KE_MSG_CONSUMED;

    if (!ftm_send_action_frame(FTM_START_FRAME_REQ,
                          &ftm_env.ftm_list_sta[ftm_env.ftm_current_idx].bssid))
    {
       TRACE_FTM(INF, "Action Frame has not been sent");
    }

    return KE_MSG_CONSUMED;
}

#if NX_FAKE_FTM_RSP
/**
 ****************************************************************************************
 * @brief @ref FTM_AP_MEASUREMENT message handler.
 *
 * This message is used to to send the second measurement
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 *
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int ftm_ap_measurement(ke_msg_id_t const msgid,
                                        void const *param,
                                        ke_task_id_t const dest_id,
                                        ke_task_id_t const src_id)
{
    TRACE_FTM(INF,"Send measurement to the STA")

    // Check if we are in a state where such timeout could occur
    if (ke_state_get(TASK_FTM) != FTM_MEAS_INITIAL_EXCHANGE)
        return KE_MSG_CONSUMED;

    if(ftm_env.ftm_per_burst != 0)
    {
        if (!ftm_send_action_frame(FTM_MEAS_RSP, &ftm_env.ap_mac_addr))
            TRACE_FTM(INF, "Action Frame has not been sent");
            
        ke_timer_set(FTM_AP_MEASUREMENT, TASK_FTM, 
                     DEFAULT_MEAS_FRAME_SENT_TIMEOUT);
        ftm_env.ftm_per_burst--;
    }
    else
    {
        ke_state_set(TASK_FTM, FTM_IDLE);
    }

    return KE_MSG_CONSUMED;
}
#endif

/**
 ****************************************************************************************
 * @brief Process FTM_CLOSE_SESSION_REQ message
 *
 * Simply cancel the remain on channel procedure
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 *
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int ftm_close_session_req_handler(ke_msg_id_t const msgid,
                                                    void const *param,
                                                    ke_task_id_t const dest_id,
                                                    ke_task_id_t const src_id)
{
    ASSERT_ERR(ke_state_get(TASK_FTM) == FTM_CLOSING_SESSION);

    ftm_remain_on_channel(MM_ROC_OP_CANCEL);

    return KE_MSG_CONSUMED;
}

/// Default handler definition.
const struct ke_msg_handler ftm_default_state[] =
{
    {FTM_START_REQ,                (ke_msg_func_t)ftm_start_req_handler},
    {SCANU_START_CFM,              (ke_msg_func_t)scanu_start_cfm_handler},
    {MM_REMAIN_ON_CHANNEL_CFM,     (ke_msg_func_t)mm_remain_on_channel_cfm_handler},
    {MM_CHANNEL_SWITCH_IND,        (ke_msg_func_t)mm_channel_switch_ind_handler},
    {RXU_MGT_IND,                  (ke_msg_func_t)rxu_mgt_ind_handler},
    {FTM_MEASUREMENT_REQ,          (ke_msg_func_t)ftm_meas_req_handler},
    {FTM_PEER_RSP_TIMEOUT_IND,     (ke_msg_func_t)ftm_peer_rsp_timeout_ind_handler},
    {MM_REMAIN_ON_CHANNEL_EXP_IND, (ke_msg_func_t)ke_msg_discard},
    {FTM_CLOSE_SESSION_REQ,        (ke_msg_func_t)ftm_close_session_req_handler},
    #if NX_FAKE_FTM_RSP
    {FTM_AP_MEASUREMENT,           (ke_msg_func_t)ftm_ap_measurement},
    #endif

};

/// Message handlers that are common to all states.
const struct ke_state_handler ftm_default_handler = 
                                         KE_STATE_HANDLER(ftm_default_state);

/// Place holder for the states of the FTM task.
ke_state_t ftm_state[FTM_IDX_MAX];

#endif // NX_FTM_INITIATOR
