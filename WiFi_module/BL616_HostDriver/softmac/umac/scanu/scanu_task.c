#include "bl_ipc_compat.h"

#include "ke_mem.h"
#include "ke_task.h"
#include "ke_msg.h"

#include "softmac.h"

#include "scanu_task.h"
#include "scanu.h"

#include "me.h"
#include "rxu_task.h"
#include "mm_task.h"
#include "vif_mgmt.h"
#include "sta_mgmt.h"
#if NX_RM
#include "rm.h"
#endif

/** @addtogroup TASK_SCANU
 * @{
 */


/**
 ****************************************************************************************
 * @brief SCAN module message handler.
 * This function handles the scan request message from any module.
 * This message Requests the STA to perform active or passive scan.
 * If a scan is requested within two seconds of the last scan, no scan is performed
 * and the last scan results are sent to the requester. The time at which the last
 * scan was performed is stored in the time_stamp element in me_scan_context.
 * If a scan should be performed, the following is done:
 *   - Store the important parameters in the SCAN context.
 *   - Decrease the number of channels to be scanned.
 *   - Send a scan request to the LMAC.
 *   - Move to the state SCANNING to wait the CFM and send the next request.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
int scanu_start_req_handler(ke_msg_id_t const msgid,
                                    struct scanu_start_req *param,
                                    ke_task_id_t const dest_id,
                                    ke_task_id_t const src_id)
{
    // record the SCAN information in the environment
    scanu_env.req_type = SCANU_NO_JOIN;
    scanu_env.param = param;
    scanu_env.src_id = src_id;
    scanu_env.band = PHY_BAND_2G4;
    scanu_env.bssid = param->bssid;

    dbg_chan("%s, req_type:%d, scanu state:%d, param:0x%p, chan_cnt:%d, duration:%d, add_ie_len:%d, bssid0:0x%x\n",
          __func__, scanu_env.req_type, ke_state_get(TASK_SCANU), 
          scanu_env.param, param->chan_cnt, 
          param->duration, param->add_ie_len, param->bssid.array[0]);

    #if (NX_ANT_DIV)
    ke_msg_send_basic(MM_ANT_DIV_STOP_REQ, TASK_MM, TASK_SCANU);
    #endif //(NX_ANT_DIV)

    #if CFG_BBP_CTRL
    ke_msg_send_basic(MM_BBP_STOP_REQ, TASK_MM, TASK_SCANU);
    #endif

    
    #if (FIX_WFA_MBO_5_2_1)
    if(KE_TYPE_GET(src_id)==TASK_API)
    {
        ASSERT_ERR(param->add_ie_len<SCANU_MAX_IE_LEN);
        scanu_env.add_ie_len = param->add_ie_len;
        memcpy(scanu_env.add_ies_buf,param->add_ies_buf,scanu_env.add_ie_len);
    }
    else
    {
        param->add_ie_len=scanu_env.add_ie_len;
    }
    #endif

    // start scanning
    scanu_start();

    return (KE_MSG_NO_FREE);
}

/**
 ****************************************************************************************
 * @brief SCAN module message handler.
 * This function handles the scan request message from any module.
 * This message Requests the STA to perform active or passive scan.
 * If a scan is requested within two seconds of the last scan, no scan is performed
 * and the last scan results are sent to the requester. The time at which the last
 * scan was performed is stored in the time_stamp element in me_scan_context.
 * If a scan should be performed, the following is done:
 *   - Store the important parameters in the SCAN context.
 *   - Decrease the number of channels to be scanned.
 *   - Send a scan request to the LMAC.
 *   - Move to the state SCANNING to wait the CFM and send the next request.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
scanu_join_req_handler(ke_msg_id_t const msgid,
                               struct scanu_start_req *param,
                               ke_task_id_t const dest_id,
                               ke_task_id_t const src_id)
{
    struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];
    struct me_bss_info *bss = &vif->bss_info;
    struct mac_scan_result *scan;

    // Reset the valid flag of the BSS information
    BSS_CAPA_RESET(bss);
    softmac_vif_update(param->vif_idx, VIF_FIELD_BSS_CAP_FLAGS, 
                       (uint8_t *)bss->capa_flags, sizeof(bss->capa_flags));
                               
    // record the SCAN information in the environment
    scanu_env.req_type = SCANU_JOIN;
    scanu_env.param = param;
    scanu_env.src_id = src_id;
    scanu_env.band = PHY_BAND_2G4;
    scanu_env.bssid = param->bssid;
    scanu_env.join_status = 0;
    scan = scanu_find_result(&param->bssid, false);
    if (scan && scan->multi_bssid_index) {
        mac_ref_bssid_get(scan->multi_bssid_index, scan->max_bssid_indicator,
                          &scan->bssid, &scanu_env.ref_bssid);
    } else {
        scanu_env.ref_bssid = param->bssid;

    }
    
    // Sanity checks
    ASSERT_ERR(!MAC_ADDR_GROUP(&param->bssid));
    
    #if (FIX_WFA_MBO_5_2_1)
    param->add_ie_len=scanu_env.add_ie_len;
    #endif

    // start scanning
    scanu_start();

    return (KE_MSG_NO_FREE);
}

/**
 ****************************************************************************************
* @brief SCAN module message handler.
 * This function handles the request to obtain the scan results.
 * The request should be made after finishing the scan process (i.e after reception of
 * scan confirmation message from SCANU) to ensure that the scan results are valid.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
scanu_get_scan_result_req_handler(ke_msg_id_t const msgid,
                                  struct scanu_get_scan_result_req const *param,
                                  ke_task_id_t const dest_id,
                                  ke_task_id_t const src_id)
{
    struct scanu_get_scan_result_cfm *cfm ;

    cfm = KE_MSG_ALLOC(SCANU_GET_SCAN_RESULT_CFM, src_id, TASK_SCANU, 
                       scanu_get_scan_result_cfm);

    if ((!scanu_env.result_cnt) || (param->idx >= scanu_env.result_cnt))
    {
        cfm->scan_result.valid_flag = false;
    }
    else
    {
        cfm->scan_result = scanu_env.scan_result[param->idx];
    }

    ke_msg_send(cfm);

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief SCAN module message handler.
 * This function handles the scan confirmation message from LMAC.
 * This message confirms a scan request command after LMAC is finished with scan procedure
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
scan_start_cfm_handler(ke_msg_id_t const msgid,
                               struct scan_start_cfm const *param,
                               ke_task_id_t const dest_id,
                               ke_task_id_t const src_id)
{
    dbg_chan("%s\n", __func__);
    
    // Check if the scanning procedure was correctly started by LMAC
    if (param->status != CO_OK)
        scanu_confirm(param->status);

    return (KE_MSG_CONSUMED);
}


/**
 ****************************************************************************************
 * @brief SCAN module message handler.
 * This function handles the scan confirmation message from LMAC.
 * This message confirms a scan request command after LMAC is finished with scan procedure
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
scan_done_ind_handler(ke_msg_id_t const msgid,
                              void const *param,
                              ke_task_id_t const dest_id,
                              ke_task_id_t const src_id)
{
    // Update the band
    scanu_env.band++;

    // Scan the next band
    scanu_scan_next();

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief SCAN module message handler.
 *
 * This function handles the reception of a BEACON and PROBE Response
 * It extracts all required information from them.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
rxu_mgt_ind_handler(ke_msg_id_t const msgid,
                            struct rxu_mgt_ind const *param,
                            ke_task_id_t const dest_id,
                            ke_task_id_t const src_id)
{
    int msg_status;

    #if NX_RM
    rm_new_beacon_measure((struct bcn_frame *)param->payload, param->length, 
                          (uint32_t)param->tsf, param->center_freq,
                          param->band, param->rssi, param->antenna_set);
    #endif // NX_RM

    // Call frame handler
    msg_status = scanu_frame_handler(param);

    return (msg_status);
}

/// Specifies the messages handled in idle state.
const struct ke_msg_handler scanu_idle[] =
{
    {SCANU_START_REQ,           (ke_msg_func_t)scanu_start_req_handler},
    {SCANU_JOIN_REQ,            (ke_msg_func_t)scanu_join_req_handler},
    {SCANU_GET_SCAN_RESULT_REQ, (ke_msg_func_t)scanu_get_scan_result_req_handler},
};

/// Specifies the messages handled in scanning state.
const struct ke_msg_handler scanu_scanning[] =
{
    {SCAN_START_CFM, (ke_msg_func_t)scan_start_cfm_handler},
    {SCAN_DONE_IND,  (ke_msg_func_t)scan_done_ind_handler},
    {RXU_MGT_IND,    (ke_msg_func_t)rxu_mgt_ind_handler},
};

/// DEFAULT handler definition.
const struct ke_msg_handler scanu_default_state[] =
{
    // if receiving scan requests while not in IDLE, save message
    {SCANU_START_REQ, ke_msg_save},
    {SCANU_JOIN_REQ, ke_msg_save},
    {SCANU_GET_SCAN_RESULT_REQ, ke_msg_save},
    // if receiving probe responses or beacons out of scan, discard them
    {RXU_MGT_IND, ke_msg_discard},
};


/// Specifies the message handler structure for every input state.
const struct ke_state_handler scanu_state_handler[SCANU_STATE_MAX] =
{
    /// IDLE State message handlers.
    [SCANU_IDLE] = KE_STATE_HANDLER(scanu_idle),
    /// SCANNING State message handlers
    [SCANU_SCANNING] = KE_STATE_HANDLER(scanu_scanning),
};


/// Specifies the message handlers that are common to all states.
const struct ke_state_handler scanu_default_handler =
    KE_STATE_HANDLER(scanu_default_state);


/// Defines the place holder for the states of all the task instances.
ke_state_t scanu_state[SCANU_IDX_MAX];


