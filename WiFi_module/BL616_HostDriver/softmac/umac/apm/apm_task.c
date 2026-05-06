#include "rwnx_config.h"
#if NX_BEACONING
#include "ke_timer.h"
#include "vif_mgmt.h"
#include "apm.h"
#include "apm_task.h"
#include "me_utils.h"
#include "me_task.h"
#include "me.h"
#include "ps.h"

#include "softmac.h"

/**
****************************************************************************************
* @brief APM module message handler.
*
* This function handles the message APM_START_REQ from host.
* This message requests the UMAC to start an AP in a BSS network.
*
* @param[in] msgid Id of the message received (probably unused)
* @param[in] param Pointer to the parameters of the message.
* @param[in] dest_id TaskId of the receiving task.
* @param[in] src_id TaskId of the sending task.
* @return Whether the message was consumed or not.
****************************************************************************************
*/
static int
apm_start_req_handler(ke_msg_id_t const msgid,
                             struct apm_start_req *param,
                             ke_task_id_t const dest_id,
                             ke_task_id_t const src_id)
{
    struct apm_start_cfm *cfm;
    uint8_t status;

    do
    {
        struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];

        // Check if the VIF is configured as AP
        if (vif->type != VIF_AP)
        {
            status = CO_BAD_PARAM;
            break;
        }

        // Check if we are busy or not
        if (ke_state_get(TASK_APM) != APM_IDLE)
        {
            status = CO_BUSY;
            break;
        }

        // Check if the AP is not already started
        if (vif->active)
        {
            status = CO_OP_IN_PROGRESS;
            break;
        }

        // Save the parameters
        apm_env.param = param;

        // Set the BSS parameters to LMAC
        apm_set_bss_param();

        // We will now proceed to the AP starting
        return (KE_MSG_NO_FREE);
    } while(0);

    cfm = KE_MSG_ALLOC(APM_START_CFM, src_id, dest_id, apm_start_cfm);
    cfm->status = status;
    cfm->vif_idx = param->vif_idx;

    // Send the message
    ke_msg_send(cfm);

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief APM module message handler.
 *
 * This function handles the message ME_SET_PS_DISABLE_CFM.
 * It is sent by ME Task once request Power Save mode has been configured. Power Save mode
 * is disabled when an AP is started and re-enabled when it is stopped.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
me_set_ps_disable_cfm_handler(ke_msg_id_t const msgid,
                                          void const *param,
                                          ke_task_id_t const dest_id,
                                          ke_task_id_t const src_id)
{
    // Sanity check
    ASSERT_ERR((ke_state_get(TASK_APM) == APM_BSS_PARAM_SETTING) ||
               (ke_state_get(TASK_APM) == APM_IDLE) ||
               (ke_state_get(TASK_APM) == APM_STOPPING));

    // Check the state
    if ((ke_state_get(TASK_APM) == APM_BSS_PARAM_SETTING) ||
        (ke_state_get(TASK_APM) == APM_STOPPING))
    {
        // Send the next BSS configuration message
        apm_bss_config_send();
    }

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief APM module message handler.
 *
 * This function handles the messages MM_SET_BSSID_CFM and MM_SET_BEACON_INT_CFM.
 * They are sent by MM task once requested configuration has been completed. Configuration
 * is done only when the AP is started.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
mm_bss_param_setting_handler(ke_msg_id_t const msgid,
                                         void const *param,
                                         ke_task_id_t const dest_id,
                                         ke_task_id_t const src_id)
{
    // Sanity check - This message can be received only in the CHAN CTXT ADDING state
    ASSERT_ERR(ke_state_get(TASK_APM) == APM_BSS_PARAM_SETTING);

    // Send the next BSS parameter configuration message
    apm_bss_config_send();

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief APM module message handler.
 *
 * This function handles the message ME_SET_ACTIVE_CFM.
 * It is sent by ME Task once requested state is reached. Vif is activated when the AP is
 * started and deactivated when the AP is stopped.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
me_set_active_cfm_handler(ke_msg_id_t const msgid,
                                    void const *param,
                                    ke_task_id_t const dest_id,
                                    ke_task_id_t const src_id)
{
    // Sanity check - This message can be received only in the STA ADDING state
    ASSERT_ERR((ke_state_get(TASK_APM) == APM_BSS_PARAM_SETTING) ||
               (ke_state_get(TASK_APM) == APM_IDLE) ||
               (ke_state_get(TASK_APM) == APM_STOPPING));

    // Check the state
    if (ke_state_get(TASK_APM) == APM_BSS_PARAM_SETTING)
    {
        // Sanity check - All the BSS configuration parameters shall be set
        ASSERT_ERR(co_list_is_empty(&apm_env.bss_config));

        // Set the beacon information to the LMAC
        apm_bcn_set();
    }
    else if (ke_state_get(TASK_APM) == APM_STOPPING)
    {
        // Send the confirmation
        ke_msg_send_basic(APM_STOP_CFM, TASK_API, TASK_APM);
        ke_state_set(TASK_APM, APM_IDLE);
    }

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief APM module message handler.
 *
 * This function handles the message MM_BCN_CHANGE_CFM.
 * It is sent by MM layer once the beacon has been configured. Beacon is only configured
 * once when the AP is started. It is the last step in the start of the AP.
 * @note If Beacon need to be modified while AP is running, the modification is directly
 * sent to MM task.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
mm_bcn_change_cfm_handler(ke_msg_id_t const msgid,
                                       void const *param,
                                       ke_task_id_t const dest_id,
                                       ke_task_id_t const src_id)
{
    // Sanity check - This message can be received only in the APM_SCHEDULING_CHAN_CTX state
    ASSERT_ERR(ke_state_get(TASK_APM) == APM_BCN_SETTING);

    // Set the beacon information to the LMAC
    apm_start_cfm(CO_OK);

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief @ref MM_CHAN_CTXT_UNLINK_CFM message handler.
 * This function is called once the channel context associated to a VIF has been unlinked.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
mm_chan_ctxt_unlink_cfm_handler(ke_msg_id_t const msgid,
                                             void const *param,
                                             ke_task_id_t const dest_id,
                                             ke_task_id_t const src_id)
{
    // Sanity check - This message can be received during the stopping procedure only
    ASSERT_ERR(ke_state_get(TASK_APM) == APM_STOPPING);

    // Send the next BSS stopping message
    apm_bss_config_send();

    return (KE_MSG_CONSUMED);
}

static int
mm_chan_ctxt_link_cfm_handler(ke_msg_id_t const msgid,
                                          void const *param,
                                          ke_task_id_t const dest_id,
                                          ke_task_id_t const src_id)
{
    if(ke_state_get(TASK_APM) == APM_BSS_PARAM_SETTING) {
        // Send the next BSS stopping message
        apm_bss_config_send();
    }
    
    return (KE_MSG_CONSUMED);
}

static int
mm_chan_ctxt_add_cfm_handler(ke_msg_id_t const msgid,
                                          void const *param,
                                          ke_task_id_t const dest_id,
                                          ke_task_id_t const src_id)
{
    struct mm_chan_ctxt_add_cfm *cfm = (struct mm_chan_ctxt_add_cfm *)param;
    struct mm_chan_ctxt_link_req *ch_link = NULL;
    struct softmac_vif_info_tag *vif = &vif_info_tab[cfm->vif_idx];
    struct me_bss_info *bss = &vif->bss_info;

    ASSERT_WARN(ke_state_get(TASK_APM) == APM_BSS_PARAM_SETTING ||
                ke_state_get(TASK_APM) == APM_IDLE);
    ASSERT_WARN(cfm->status == CO_OK);
    
    if(ke_state_get(TASK_APM) == APM_BSS_PARAM_SETTING) {
        ch_link = KE_MSG_ALLOC(MM_CHAN_CTXT_LINK_REQ,TASK_MM,
                               TASK_APM, mm_chan_ctxt_link_req);
        
        ch_link->vif_index = cfm->vif_idx;
        ch_link->chan_index = cfm->index;
        ch_link->chan_switch = false;

        vif->chan_ctxt_idx = cfm->index;
        
        // Save the BW and channel information in the VIF
        bss->chan = apm_env.param->chan;
        // Initialize TX power
        bss->power_constraint = 0;
       
        // If the AP is started on 2.4GHz band, we need to get the highest 11b rate
        // that might be used for the non-ERP protection
        if (bss->chan.band == PHY_BAND_2G4)
        {
            uint32_t basic_11b_rates;
            
            basic_11b_rates = 
               me_legacy_rate_bitfield_build(&(bss->rate_set), true) & 0x0F;
               
            // Get highest allowed 11b rate
            if (basic_11b_rates)
                // Get highest allowed 11b rate
                bss->high_11b_rate = 31 - co_clz(basic_11b_rates);
            else
                // If no 11b basic rates, set the highest mandatory one
                bss->high_11b_rate = HW_RATE_2MBPS;
        }

        softmac_vif_update(cfm->vif_idx, VIF_FIELD_BSS_INFO, 
                           (uint8_t *)bss, sizeof(struct me_bss_info));
        softmac_vif_update(cfm->vif_idx, VIF_FIELD_TX_PWR, NULL, 0);

        ke_msg_send((void *)ch_link);
    } else if (ke_state_get(TASK_APM) == APM_IDLE) {
        struct apm_start_cac_cfm *cac_cfm;
        
        //START_CAC req ch ctxt add
        ch_link = KE_MSG_ALLOC(MM_CHAN_CTXT_LINK_REQ, TASK_MM,
                               TASK_APM, mm_chan_ctxt_link_req);
        ch_link->vif_index = cfm->vif_idx;
        ch_link->chan_index = cfm->index;
        ch_link->chan_switch = false;
        ke_msg_send((void *)ch_link);

        // Send the confirmation
        cac_cfm = KE_MSG_ALLOC(APM_START_CAC_CFM, 
                               TASK_API, TASK_APM, apm_start_cac_cfm);
        cac_cfm->status = CO_OK;
        cac_cfm->ch_idx = cfm->index;
        // Send the message
        ke_msg_send(cac_cfm);
    }

    return (KE_MSG_CONSUMED);
}

/**
 ****************************************************************************************
 * @brief @ref MM_SET_VIF_STATE_CFM message handler.
 * This function is called once the VIF has been put to active state at the end of the
 * AP creation, or to inactive state upon AP deletion.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
mm_set_vif_state_cfm_handler(ke_msg_id_t const msgid,
                                        struct mm_set_vif_state_cfm const *param,
                                        ke_task_id_t const dest_id,
                                        ke_task_id_t const src_id)
{
    dbg_con("%s, task_apm state:%d, vif_idx:%d, vif active:%d, call softmac_vif_update_local\r\n",
          __func__, ke_state_get(TASK_APM), param->inst_nbr, param->active);
          
    // Sanity check - This message can be received only in the stopping and idle states
    ASSERT_ERR((ke_state_get(TASK_APM) == APM_STOPPING) ||
               (ke_state_get(TASK_APM) == APM_IDLE));

    softmac_vif_update_local(param->inst_nbr, VIF_FIELD_ACTIVE,
                             (uint8_t *)&param->active, 1);

    // Check if we are in the connection process
    if (ke_state_get(TASK_APM) == APM_STOPPING)
    {
        // Send the next BSS stopping message
        apm_bss_config_send();
    }

    return (KE_MSG_CONSUMED);
}

/**
****************************************************************************************
* @brief APM module message handler.
*
* This function handles the message APM_STOP_REQ from host.
* This message requests the UMAC to stop the AP.
*
* @param[in] msgid Id of the message received (probably unused)
* @param[in] param Pointer to the parameters of the message.
* @param[in] dest_id TaskId of the receiving task.
* @param[in] src_id TaskId of the sending task.
* @return Whether the message was consumed or not.
****************************************************************************************
*/
static int
apm_stop_req_handler(ke_msg_id_t const msgid,
                             struct apm_stop_req const *param,
                             ke_task_id_t const dest_id,
                             ke_task_id_t const src_id)
{
    struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];

    dbg_con("%s, vif_idx:%d, type:%d, active:%d\r\n", 
            __func__, vif->index, vif->type, vif->active);

    if ((vif->type != VIF_AP) || (!vif->active))
    {
        // Not an AP (or not started) respond immediately
        ke_msg_send_basic(APM_STOP_CFM, src_id, dest_id);
        return KE_MSG_CONSUMED;
    }
    else if (ke_state_get(TASK_APM) != APM_IDLE)
    {
        // Task is currently busy, postpone this message
        return KE_MSG_SAVED;
    }

    // Stop the AP (confirmation will be sent once stopped)
    apm_stop(vif);
    
    return KE_MSG_CONSUMED;
}

/**
****************************************************************************************
* @brief APM module message handler.
* This function handles the message APM_START_CAC_REQ from SME.
* This message requests the UMAC to start listening on the specified channel
* until APM_AP_STOP_CAC_REQ is received.
* CAC (Channel Availability Check) is done to check for radar before starting
* an AP on a DFS channel
*
* @param[in] msgid Id of the message received (probably unused)
* @param[in] param Pointer to the parameters of the message.
* @param[in] dest_id TaskId of the receiving task.
* @param[in] src_id TaskId of the sending task.
* @return Whether the message was consumed or not.
****************************************************************************************
*/
static int
apm_start_cac_req_handler(ke_msg_id_t const msgid,
                                   struct apm_start_cac_req const *param,
                                   ke_task_id_t const dest_id,
                                   ke_task_id_t const src_id)
{
    struct apm_start_cac_cfm *cfm;
    uint8_t status = CO_OK;
    uint8_t chan_idx = 0;

    do
    {
        struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];
        struct mm_chan_ctxt_add_req *ch_add = NULL;
        
        // Check if the VIF is configured as AP
        if (vif->type != VIF_AP) {
            status = CO_BAD_PARAM;
            break;
        }

        // Check if the AP is not already started
        if (vif->active) {
            status = CO_BUSY;
            break;
        }

        // Check if we are busy or not
        if (ke_state_get(TASK_APM) != APM_IDLE) {
            status = CO_BUSY;
            break;
        }

        ch_add = KE_MSG_ALLOC(MM_CHAN_CTXT_ADD_REQ, TASK_MM,
                              TASK_APM, mm_chan_ctxt_add_req);
        ch_add->chan = param->chan;
        ch_add->vif_idx = param->vif_idx;
        ke_msg_send((void *)ch_add);

        #if (NX_POWERSAVE)
        softmac_ps_flags_req(INVALID_VIF_IDX, PS_CAC_STARTED, false);
        #endif

        return (KE_MSG_CONSUMED);
    } while(0);

    // Send the confirmation
    cfm = KE_MSG_ALLOC(APM_START_CAC_CFM, src_id, dest_id, apm_start_cac_cfm);
    cfm->status = status;
    cfm->ch_idx = chan_idx;

    // Send the message
    ke_msg_send(cfm);

    return (KE_MSG_CONSUMED);
}

/**
****************************************************************************************
* @brief APM module message handler.
* This function handles the message APM_STOP_CAC_REQ from SME.
* This message requests the UMAC to stop listening of the specified channel.
* CAC (Channel Availability Check) ends if a radar is detected or after a defined period
*
* @param[in] msgid Id of the message received (probably unused)
* @param[in] param Pointer to the parameters of the message.
* @param[in] dest_id TaskId of the receiving task.
* @param[in] src_id TaskId of the sending task.
* @return Whether the message was consumed or not.
****************************************************************************************
*/
static int
apm_stop_cac_req_handler(ke_msg_id_t const msgid,
                                   struct apm_stop_cac_req const *param,
                                   ke_task_id_t const dest_id,
                                   ke_task_id_t const src_id)
{
    do
    {
        struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];
        struct mm_chan_ctxt_unlink_req *unlk = NULL;
        
        // Check if the VIF is configured as AP
        if (vif->type != VIF_AP) {
            break;
        }

        // Check if the AP is not already started
        if (vif->active) {
            break;
        }

        // Check if we are busy or not
        if (ke_state_get(TASK_APM) != APM_IDLE) {
            break;
        }

        unlk = KE_MSG_ALLOC(MM_CHAN_CTXT_UNLINK_REQ, TASK_MM,
                            TASK_APM, mm_chan_ctxt_unlink_req);
        unlk->vif_index = param->vif_idx;
        ke_msg_send((void *)unlk);

        #if (NX_POWERSAVE)
        //  Power Save can be used        
        softmac_ps_flags_req(INVALID_VIF_IDX, PS_CAC_STARTED, true);
        #endif
    } while(0);

    // Send the confirmation
    ke_msg_send_basic(APM_STOP_CAC_CFM, src_id, dest_id);

    return (KE_MSG_CONSUMED);
}

/**
****************************************************************************************
* @brief APM module message handler.
*
* This function handles the message APM_PROBE_CLIENT_REQ from SME.
* This message requests the UMAC to send a Data NULL frame to a client, that has been
* inactive for some time, to ensure that it is still in the AP range.
* A response APM_PROBE_CLIENT_CFM is sent to confirm that the request has been received
* and the actual probe status is sent later on in a APM_PROBE_CLIENT_IND message.
*
* @param[in] msgid   Id of the message received (probably unused)
* @param[in] param   Pointer to the parameters of the message.
* @param[in] dest_id TaskId of the receiving task.
* @param[in] src_id  TaskId of the sending task.
* @return Whether the message was consumed or not.
****************************************************************************************
*/
static int
apm_probe_client_req_handler(ke_msg_id_t const msgid,
                                       struct apm_probe_client_req const *param,
                                       ke_task_id_t const dest_id,
                                       ke_task_id_t const src_id)
{
    struct softmac_vif_info_tag *vif;

    if ((param->vif_idx < NX_VIRT_DEV_MAX) && 
        (vif = &vif_info_tab[param->vif_idx]) &&
        (vif->type == VIF_AP) && (vif->active))
    {
        softmac_ap_probe_req(vif->index, param->sta_idx);
    }
 
    return (KE_MSG_CONSUMED);
}

/// DEFAULT handler definition.
const struct ke_msg_handler apm_default_state[] =
{
    {APM_START_REQ, (ke_msg_func_t)apm_start_req_handler},
    {APM_STOP_REQ, (ke_msg_func_t)apm_stop_req_handler},
    {ME_SET_ACTIVE_CFM, (ke_msg_func_t)me_set_active_cfm_handler},
    {MM_BCN_CHANGE_CFM, (ke_msg_func_t)mm_bcn_change_cfm_handler},
    {MM_SET_BSSID_CFM, mm_bss_param_setting_handler},
    {MM_SET_BASIC_RATES_CFM, mm_bss_param_setting_handler},
    {MM_SET_BEACON_INT_CFM, mm_bss_param_setting_handler},
    {ME_SET_PS_DISABLE_CFM, (ke_msg_func_t)me_set_ps_disable_cfm_handler},
    {MM_SET_VIF_STATE_CFM, (ke_msg_func_t)mm_set_vif_state_cfm_handler},
    {MM_CHAN_CTXT_ADD_CFM, (ke_msg_func_t)mm_chan_ctxt_add_cfm_handler},
    {MM_CHAN_CTXT_LINK_CFM, (ke_msg_func_t)mm_chan_ctxt_link_cfm_handler},
    {MM_CHAN_CTXT_UNLINK_CFM, (ke_msg_func_t)mm_chan_ctxt_unlink_cfm_handler},
    {APM_START_CAC_REQ, (ke_msg_func_t)apm_start_cac_req_handler},
    {APM_STOP_CAC_REQ, (ke_msg_func_t)apm_stop_cac_req_handler},
    {APM_PROBE_CLIENT_REQ, (ke_msg_func_t)apm_probe_client_req_handler},
};

/// Specifies the message handlers that are common to all states.
const struct ke_state_handler apm_default_handler =
                                         KE_STATE_HANDLER(apm_default_state);

/// Defines the placeholder for the states of all the task instances.
ke_state_t apm_state[APM_IDX_MAX];

/// @} end of addtogroup

#endif

