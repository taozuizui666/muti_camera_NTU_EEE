#include "rwnx_config.h"
#if NX_RM
#include "softmac.h"

#include "rm_task.h"
#include "rxu_task.h"
#include "scanu_task.h"
#include "rm.h"
#include "mac_frame.h"
#include "mac_ie.h"
#include "txl_frame.h"

/**
 ****************************************************************************************
 * @brief RM module message handler for management frame.
 *
 * This function handles the reception of Measurement request.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int rxu_mgt_ind_handler(ke_msg_id_t const msgid,
                                       struct rxu_mgt_ind const *param,
                                       ke_task_id_t const dest_id,
                                       ke_task_id_t const src_id)
{
    PTR2UINT frame = CPU2HW(param->payload);
    int frame_len = param->length;
    PTR2UINT rm_req[RM_MAX_REQUEST];
    int rm_req_cnt = 0;
    uint8_t category, action, token, nb_repetition, type;

    rm_env.tsf = param->tsf;
    rm_env.jiffies = softmac_time();

    category = co_read8p(frame + MAC_ACTION_CATEGORY_OFT);
    action = co_read8p(frame + MAC_ACTION_ACTION_OFT);

    // Ignore invalid or not Measure Request frame
    if ((category != MAC_RADIO_MEASURE_ACTION_CATEGORY) ||
        (action != MAC_RM_ACTION_REQUEST) ||
        (frame_len < MAC_RM_ACTION_REQ_MEASURE_REQ_OFT + MAC_MEAS_REQ_REQ_OFT))
    {
        dbg_f("%s, frame_len:%d too short?\r\n", __func__, frame_len);
        
        return KE_MSG_CONSUMED;
    }

    token = co_read8p(frame + MAC_ACTION_TOKEN_OFT);
    type = co_read8p(frame + MAC_RM_ACTION_REQ_MEASURE_REQ_OFT + MAC_MEAS_REQ_TYPE_OFT);
    
    if (ke_state_get(TASK_RM) != RM_IDLE)
    {
        dbg_f("%s, radio_measure not idle, %d\r\n", __func__, ke_state_get(TASK_RM));
        
        // Don't accept new request until previous one is finished
        rm_reject_request(param->inst_nbr, param->sta_idx, token,
                          MAC_MEAS_REP_MODE_REFUSE_BIT, type);
                          
        return KE_MSG_CONSUMED;
    } 
    else if (ke_state_get(TASK_SCANU) == SCANU_SCANNING)
    {
        dbg_f("%s, reject because already scanning, %d %d\r\n", 
              __func__, ke_state_get(TASK_RM), ke_state_get(TASK_SCANU));
        
        // Don't accept new request until previous one is finished
        rm_reject_request(param->inst_nbr, param->sta_idx, token,
                          MAC_MEAS_REP_MODE_REFUSE_BIT, type);
                          
        return KE_MSG_CONSUMED;
    }

    nb_repetition = co_read8p(frame + MAC_RM_ACTION_REQ_NUMBER_REPEAT_OFT);
    if (nb_repetition)
    {
        dbg_f("%s, not support nb_repetition\r\n", __func__);
        
        // Don't support repetitive measure
        rm_reject_request(param->inst_nbr, param->sta_idx, token,
                          MAC_MEAS_REP_MODE_INCAPABLE_BIT, type);
                          
        return KE_MSG_CONSUMED;
    }

    frame += MAC_RM_ACTION_REQ_MEASURE_REQ_OFT;
    frame_len -= MAC_RM_ACTION_REQ_MEASURE_REQ_OFT;
    while (frame_len > 2)
    {
        uint8_t elt = co_read8p(frame);
        uint16_t elt_len = mac_ie_len(frame);

        if ((elt != MAC_ELTID_MEASUREMENT_REQUEST) ||
            (elt_len > frame_len))
        {
            dbg_f("%s, not measure request or elt_len(%d) > frame_len(%d)\r\n",
                  __func__, elt_len, frame_len);
                  
            // unexpected/invalid element in Request action frame
            rm_reject_request(param->inst_nbr, param->sta_idx, token,
                              MAC_MEAS_REP_MODE_REFUSE_BIT, type);
                              
            return KE_MSG_CONSUMED;
        }

        if (rm_req_cnt < RM_MAX_REQUEST)
        {
            rm_req[rm_req_cnt++] = frame;
        }
        else
        {
            dbg("Ignore extra measure request");
        }

        frame += elt_len;
        frame_len -= elt_len;
    }

    rm_initialize_requests(param->inst_nbr, param->sta_idx, token,
                           rm_req, rm_req_cnt);
    rm_schedule_next_request();
    
    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief RM module message handler for request processing indication.
 *
 * This indication is sent by RM task to itself in order to avoid starting new request
 * process directly from frame callback function.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int rm_process_next_req_handler(ke_msg_id_t const msgid,
                                   void const *param, ke_task_id_t const dest_id,
                                   ke_task_id_t const src_id)
{
    if (ke_state_get(TASK_RM) != RM_READY_PROCESS)
    {
        ASSERT_WARN(0);
        return KE_MSG_CONSUMED;
    }

    rm_start_active_request();
    
    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief RM module message handler for end of scan confirmation.
 *
 * Sent by SCANU task once the requested scan has been completed.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int scanu_start_cfm_handler(ke_msg_id_t const msgid,
                                           struct scanu_start_cfm const *param,
                                           ke_task_id_t const dest_id,
                                           ke_task_id_t const src_id)
{
    if (ke_state_get(TASK_RM) != RM_MEASURING)
    {
        ASSERT_WARN(0);
        return KE_MSG_CONSUMED;
    }

    if (param->status != CO_OK)
    {
        dbg_f("%s, reject because scanning ongoing\r\n", __func__);
        
        // Don't accept new request until previous scan is finished
        rm_reject_request(rm_env.vif_idx, rm_env.sta_idx, 
                          rm_env.requests[0].dialog_token,
                          MAC_MEAS_REP_MODE_REFUSE_BIT,
                          rm_env.requests[0].type);

        rm_init_measure_to_report();
        rm_reset_measures(true);
        
        ke_state_set(TASK_RM, RM_IDLE);
        rm_env.active_request = -1;
    }
    else
    {
        rm_continue_active_request();
    }
    
    return KE_MSG_CONSUMED;
}

static int
rm_fw_act_ind_handler(ke_msg_id_t const msgid, 
                               struct fw_act_ind const *param,
                               ke_task_id_t const dest_id, 
                               ke_task_id_t const src_id)
{
    if (param->act_type == FW_ACT_SEND_TXL_FRAME) {
        if (rm_env.tx_frame) {
            cfm_func_ptr cfm_func = rm_env.tx_frame->cfm.cfm_func;

            softmac_free_skb(rm_env.tx_frame->skb);
            rm_env.tx_frame = NULL;

            if (cfm_func) {
                cfm_func(NULL, param->status);
            }
        } else {
            dbg_f("%s, rm_env.tx_frame null, not cb\r\n", __func__);
        }
    } else {
        dbg_f("%s, unknow act type:%d\r\n", __func__, param->act_type);
    }
    
    return KE_MSG_CONSUMED;
}

/// DEFAULT handler definition.
const struct ke_msg_handler rm_default_state[] =
{
    {RXU_MGT_IND,                 (ke_msg_func_t)rxu_mgt_ind_handler},
    {RM_PROCESS_NEXT_REQUEST_IND, (ke_msg_func_t)rm_process_next_req_handler},
    {SCANU_START_CFM,             (ke_msg_func_t)scanu_start_cfm_handler},
    {DBG_FW_ACT_IND,              (ke_msg_func_t)rm_fw_act_ind_handler},
};

/// Messages handlers table
const struct ke_state_handler rm_default_handler =
    KE_STATE_HANDLER(rm_default_state);

/// Defines the place holder for the states of all the task instances.
ke_state_t rm_state[RM_IDX_MAX];

#endif // NX_RM
