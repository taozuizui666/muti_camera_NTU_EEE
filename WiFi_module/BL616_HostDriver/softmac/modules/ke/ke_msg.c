/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "softmac.h"

#include "rwnx_config.h"

#include "ke_config.h"
#include "ke_queue.h"
#include "ke_msg.h"
#include "ke_task.h"
#include "ke_mem.h"
#include "ke_event.h"
#include "ke_env.h"

#include "bl_lmac_msg.h"

#define MSG_INFO_STR_PRINT 1
#if MSG_INFO_STR_PRINT
#define task_print_index(taskid) ((((int8_t)KE_TYPE_GET(taskid)+1)<=TASK_MAX)?((int8_t)KE_TYPE_GET(taskid)+1):0)

const char* task_name[] =
{
   "NONE",
   "MM",
   "DBG",
   "SCAN",
   "TDLS",
   "SCANU",
   "ME",
   "SM",
   "APM",
   "BAM",
   "MESH",
   "RXU",
   "RM",
   "TWT",
   "FTM",
   "HOST"
};

const char* mm_msg_name[] =
{
    /// RESET Request.
    "MM_RESET_REQ",
    /// RESET Confirmation.
    "MM_RESET_CFM",
    /// START Request.
    "MM_START_REQ",
    /// START Confirmation.
    "MM_START_CFM",
    /// Read Version Request.
    "MM_VERSION_REQ",
    /// Read Version Confirmation.
    "MM_VERSION_CFM",
    /// ADD INTERFACE Request.
    "MM_ADD_IF_REQ",
    /// ADD INTERFACE Confirmation.
    "MM_ADD_IF_CFM",
    /// REMOVE INTERFACE Request.
    "MM_REMOVE_IF_REQ",
    /// REMOVE INTERFACE Confirmation.
    "MM_REMOVE_IF_CFM",
    /// STA ADD Request.
    "MM_STA_ADD_REQ",                  //10
    /// STA ADD Confirm.
    "MM_STA_ADD_CFM",
    /// STA DEL Request.
    "MM_STA_DEL_REQ",
    /// STA DEL Confirm.
    "MM_STA_DEL_CFM",
    /// RX FILTER Configuration Request.
    "MM_SET_FILTER_REQ",
    /// RX FILTER Configuration Confirmation.
    "MM_SET_FILTER_CFM",
    /// CHANNEL Configuration Request.
    "MM_SET_CHANNEL_REQ",
    /// CHANNEL Configuration Confirmation.
    "MM_SET_CHANNEL_CFM",
    /// DTIM PERIOD Configuration Request.
    "MM_SET_DTIM_REQ",
    /// DTIM PERIOD Configuration Confirmation.
    "MM_SET_DTIM_CFM",
    /// BEACON INTERVAL Configuration Request.
    "MM_SET_BEACON_INT_REQ",                //20
    /// BEACON INTERVAL Configuration Confirmation.
    "MM_SET_BEACON_INT_CFM",
    /// BASIC RATES Configuration Request.
    "MM_SET_BASIC_RATES_REQ",
    /// BASIC RATES Configuration Confirmation.
    "MM_SET_BASIC_RATES_CFM",
    /// BSSID Configuration Request.
    "MM_SET_BSSID_REQ",
    /// BSSID Configuration Confirmation.
    "MM_SET_BSSID_CFM",
    /// EDCA PARAMETERS Configuration Request.
    "MM_SET_EDCA_REQ",
    /// EDCA PARAMETERS Configuration Confirmation.
    "MM_SET_EDCA_CFM",
    /// ABGN MODE Configuration Request.
    "MM_SET_MODE_REQ",
    /// ABGN MODE Configuration Confirmation.
    "MM_SET_MODE_CFM",
    /// Request setting the VIF active state (i.e associated or AP started)
    "MM_SET_VIF_STATE_REQ",                         //30
    /// Confirmation of the @ref "MM_SET_VIF_STATE_REQ message.
    "MM_SET_VIF_STATE_CFM",
    /// SLOT TIME PARAMETERS Configuration Request.
    "MM_SET_SLOTTIME_REQ",
    /// SLOT TIME PARAMETERS Configuration Confirmation.
    "MM_SET_SLOTTIME_CFM",
    /// IDLE mode change Request.
    "MM_SET_IDLE_REQ",
    /// IDLE mode change Confirm.
    "MM_SET_IDLE_CFM",
    /// KEY ADD Request.
    "MM_KEY_ADD_REQ",
    /// KEY ADD Confirm.
    "MM_KEY_ADD_CFM",
    /// KEY DEL Request.
    "MM_KEY_DEL_REQ",
    /// KEY DEL Confirm.
    "MM_KEY_DEL_CFM",
    /// Block Ack agreement info addition
    "MM_BA_ADD_REQ",                    //40
    /// Block Ack agreement info addition confirmation
    "MM_BA_ADD_CFM",
    /// Block Ack agreement info deletion
    "MM_BA_DEL_REQ",
    /// Block Ack agreement info deletion confirmation
    "MM_BA_DEL_CFM",
    /// Indication of the primary TBTT to the upper MAC. Upon the reception of this
    /// message the upper MAC has to push the beacon(s) to the beacon transmission queue.
    "MM_PRIMARY_TBTT_IND",
    /// Indication of the secondary TBTT to the upper MAC. Upon the reception of this
    /// message the upper MAC has to push the beacon(s) to the beacon transmission queue.
    "MM_SECONDARY_TBTT_IND",
    /// Request for changing the TX power
    "MM_SET_POWER_REQ",
    /// Confirmation of the TX power change
    "MM_SET_POWER_CFM",
    /// Request to the LMAC to trigger the embedded logic analyzer and forward the debug
    /// dump.
    "MM_DBG_TRIGGER_REQ",
    /// Set Power Save mode
    "MM_SET_PS_MODE_REQ",
    /// Set Power Save mode confirmation
    "MM_SET_PS_MODE_CFM",               //50
    /// Request to add a channel context
    "MM_CHAN_CTXT_ADD_REQ",
    /// Confirmation of the channel context addition
    "MM_CHAN_CTXT_ADD_CFM",
    /// Request to delete a channel context
    "MM_CHAN_CTXT_DEL_REQ",
    /// Confirmation of the channel context deletion
    "MM_CHAN_CTXT_DEL_CFM",
    /// Request to link a channel context to a VIF
    "MM_CHAN_CTXT_LINK_REQ",
    /// Confirmation of the channel context link
    "MM_CHAN_CTXT_LINK_CFM",
    /// Request to unlink a channel context from a VIF
    "MM_CHAN_CTXT_UNLINK_REQ",
    /// Confirmation of the channel context unlink
    "MM_CHAN_CTXT_UNLINK_CFM",
    /// Request to update a channel context
    "MM_CHAN_CTXT_UPDATE_REQ",
    /// Confirmation of the channel context update
    "MM_CHAN_CTXT_UPDATE_CFM",                  //60
    /// Request to schedule a channel context
    "MM_CHAN_CTXT_SCHED_REQ",
    /// Confirmation of the channel context scheduling
    "MM_CHAN_CTXT_SCHED_CFM",
    /// Request to change the beacon template in LMAC
    "MM_BCN_CHANGE_REQ",
    /// Confirmation of the beacon change
    "MM_BCN_CHANGE_CFM",
    /// Request to update the TIM in the beacon (i.e to indicate traffic bufferized at AP)
    "MM_TIM_UPDATE_REQ",
    /// Confirmation of the TIM update
    "MM_TIM_UPDATE_CFM",
    /// Connection loss indication
    "MM_CONNECTION_LOSS_IND",
    /// Channel context switch indication to the upper layers
    "MM_CHANNEL_SWITCH_IND",
    /// Channel context pre-switch indication to the upper layers
    "MM_CHANNEL_PRE_SWITCH_IND",
    /// Request to remain on channel or cancel remain on channel
    "MM_REMAIN_ON_CHANNEL_REQ",             //70
    /// Confirmation of the (cancel) remain on channel request
    "MM_REMAIN_ON_CHANNEL_CFM",
    /// Remain on channel expired indication
    "MM_REMAIN_ON_CHANNEL_EXP_IND",
    /// Indication of a PS state change of a peer device
    "MM_PS_CHANGE_IND",
    /// Indication that some buffered traffic should be sent to the peer device
    "MM_TRAFFIC_REQ_IND",
    /// Request to modify the STA Power-save mode options
    "MM_SET_PS_OPTIONS_REQ",
    /// Confirmation of the PS options setting
    "MM_SET_PS_OPTIONS_CFM",
    /// Indication of PS state change for a P2P VIF
    "MM_P2P_VIF_PS_CHANGE_IND",
    /// Indication that CSA counter has been updated
    "MM_CSA_COUNTER_IND",
    /// Message containing channel information
    "MM_CHANNEL_SURVEY_IND",
    /// Message containing Beamformer information
    "MM_BFMER_ENABLE_REQ",                //80
    /// Request to Start/Stop NOA - GO Only
    "MM_SET_P2P_NOA_REQ",
    /// Request to Start/Stop Opportunistic PS - GO Only
    "MM_SET_P2P_OPPPS_REQ",
    /// Start/Stop NOA Confirmation
    "MM_SET_P2P_NOA_CFM",
    /// Start/Stop Opportunistic PS Confirmation
    "MM_SET_P2P_OPPPS_CFM",
    /// P2P NoA Update Indication - GO Only
    "MM_P2P_NOA_UPD_IND",
    /// Request to set RSSI threshold and RSSI hysteresis
    "MM_CFG_RSSI_REQ",
    /// Indication that RSSI is below or above the threshold
    "MM_RSSI_STATUS_IND",
    /// Indication that CSA is done
    "MM_CSA_FINISH_IND",
    /// Indication that CSA is in prorgess (resp. done) and traffic must be stopped (resp. restarted)
    "MM_CSA_TRAFFIC_IND",
    /// Request to update the group information of a station
    "MM_MU_GROUP_UPDATE_REQ",                    //90
    /// Confirmation of the @ref "MM_MU_GROUP_UPDATE_REQ message
    "MM_MU_GROUP_UPDATE_CFM",
    /// Request to initialize the antenna diversity algorithm
    "MM_ANT_DIV_INIT_REQ",
    /// Request to stop the antenna diversity algorithm
    "MM_ANT_DIV_STOP_REQ",
    /// Request to update the antenna switch status
    "MM_ANT_DIV_UPDATE_REQ",
    /// Request to switch the antenna connected to path_0
    "MM_SWITCH_ANTENNA_REQ",
    /// Indication that a packet loss has occurred
    "MM_PKTLOSS_IND",
    /// MU EDCA PARAMETERS Configuration Request.
    "MM_SET_MU_EDCA_REQ",
    /// MU EDCA PARAMETERS Configuration Confirmation.
    "MM_SET_MU_EDCA_CFM",
    /// UORA PARAMETERS Configuration Request.
    "MM_SET_UORA_REQ",
    /// UORA PARAMETERS Configuration Confirmation.
    "MM_SET_UORA_CFM",                               //100
    /// TXOP RTS THRESHOLD Configuration Request.
    "MM_SET_TXOP_RTS_THRES_REQ",
    /// TXOP RTS THRESHOLD Configuration Confirmation.
    "MM_SET_TXOP_RTS_THRES_CFM",
    /// HE BSS Color Configuration Request.
    "MM_SET_BSS_COLOR_REQ",
    /// HE BSS Color Configuration Confirmation.
    "MM_SET_BSS_COLOR_CFM",
    /// wmm cfg
    "MM_WMM_CFG_REQ",
    "MM_WMM_CFG_CFM",
    // mm cal data save instead write to flash
    "MM_CAL_CFG_REQ",
    "MM_CAL_CFG_CFM",                         

    // read temperature
    "MM_TEMP_READ_REQ",
    "MM_TEMP_READ_CFM",                      //110           
    // read rssi
    "MM_GET_RSSI_REQ",
    "MM_GET_RSSI_CFM",    
    // read efuse
    "MM_READ_EFUSE_REQ",
    "MM_READ_EFUSE_CFM",
    //set phy misc cmd, like cca test cmd, need phylib support, or no effect
    "MM_SET_PHY_MISC_REQ",
    "MM_SET_PHY_MISC_CFM",    
   
    "MM_BBP_START_REQ",
    "MM_BBP_STOP_REQ",


    /*
     * Section of internal MM messages. No MM API messages should be defined below this point
     */
    /// Internal request to force the HW going to IDLE
    "MM_FORCE_IDLE_REQ",
    /// Message indicating that the switch to the scan channel is done
    "MM_SCAN_CHANNEL_START_IND",
    /// Message indicating that the scan on the channel is finished
    "MM_SCAN_CHANNEL_END_IND",
    /// Internal request to move the AP TBTT by an offset
    "MM_TBTT_MOVE_REQ",

#if BL_RA_EN
    /// Timer for link statistics monitor
    "MM_LINK_TIMER_IND",
#endif

    /// Timer id of MM task common scheduler timer.
    "MM_COMMON_SCHED_TIMER",

    /// MAX number of messages
    "MM_MAX",
};


const char* dbg_msg_name[] =
{
    /// Memory read request
    "DBG_MEM_READ_REQ",
    /// Memory read confirm
    "DBG_MEM_READ_CFM",
    /// Memory write request
    "DBG_MEM_WRITE_REQ",
    /// Memory write confirm
    "DBG_MEM_WRITE_CFM",
    /// Module filter request
    "DBG_SET_MOD_FILTER_REQ",
    /// Module filter confirm
    "DBG_SET_MOD_FILTER_CFM",
    /// Severity filter request
    "DBG_SET_SEV_FILTER_REQ",
    /// Severity filter confirm
    "DBG_SET_SEV_FILTER_CFM",
    /// Fatal error indication
    "DBG_ERROR_IND",
    /// Request to get system statistics
    "DBG_GET_SYS_STAT_REQ",
    /// COnfirmation of system statistics
    "DBG_GET_SYS_STAT_CFM",

        
#ifdef BL_BUS_LOOPBACK
    "DBG_LBK_REQ",
    "DBG_LBK_CFM",
#endif
    
    "DBG_MP_REQ",
    "DBG_MP_CFM",
    
    "DBG_COEX_RW_PARAM_REQ",
    "DBG_COEX_RW_PARAM_CFM",
    
    "DBG_BTBLE_UART_BAUD_REQ",
    "DBG_BTBLE_UART_BAUD_CFM",
    
    "DBG_UPDATE_FW_VAR",
    "DBG_FW_ACT_REQ",
    "DBG_FW_ACT_IND",
    
    "DBG_KE_STAT_REQ",
    "DBG_KE_STAT_CFM",


    /*
     * Section of internal DBG messages. No DBG API messages should be defined below this point
     */
    /// Timer allowing resetting the system statistics periodically to avoid
    /// wrap around of timer
    "DBG_SYS_STAT_TIMER",
};

const char* scan_msg_name[] =
{
    /// Scanning start Request.
    "SCAN_START_REQ",
    /// Scanning start Confirmation.
    "SCAN_START_CFM",
    /// End of scanning indication.
    "SCAN_DONE_IND",
    /// Cancel scan request
    "SCAN_CANCEL_REQ",
    /// Cancel scan confirmation
    "SCAN_CANCEL_CFM",
    /*
     * Section of internal SCAN messages. No SCAN API messages should be defined below this point
     */
    "SCAN_TIMER",

    /// MAX number of messages
    "SCAN_MAX"
};

const char* tdls_msg_name[] =
{
    /// TDLS channel Switch Request.
    "TDLS_CHAN_SWITCH_REQ",
    /// TDLS channel switch confirmation.
    "TDLS_CHAN_SWITCH_CFM",
    /// TDLS channel switch indication.
    "TDLS_CHAN_SWITCH_IND",
    /// TDLS channel switch to base channel indication.
    "TDLS_CHAN_SWITCH_BASE_IND",
    /// TDLS cancel channel switch request.
    "TDLS_CANCEL_CHAN_SWITCH_REQ",
    /// TDLS cancel channel switch confirmation.
    "TDLS_CANCEL_CHAN_SWITCH_CFM",
    /// TDLS peer power save indication.
    "TDLS_PEER_PS_IND",
    /// TDLS peer traffic indication request.
    "TDLS_PEER_TRAFFIC_IND_REQ",
    /// TDLS peer traffic indication confirmation.
    "TDLS_PEER_TRAFFIC_IND_CFM",
    /// MAX number of messages
    "TDLS_MAX"
};

const char* scanu_msg_name[] =
{
    /// Scan request from host.
    "SCANU_START_REQ",
    /// Scanning start Confirmation.
    "SCANU_START_CFM",
    /// Join request
    "SCANU_JOIN_REQ",
    /// Join confirmation.
    "SCANU_JOIN_CFM",
    /// Scan result indication.
    "SCANU_RESULT_IND",
    /// Fast scan request from any other module.
    "SCANU_FAST_REQ",
    /// Confirmation of fast scan request.
    "SCANU_FAST_CFM",
};

const char* me_msg_name[] =
{
    /// Configuration request from host.
    "ME_CONFIG_REQ = KE_FIRST_MSG(TASK_ME)",
    /// Configuration confirmation.
    "ME_CONFIG_CFM",
    /// Configuration request from host.
    "ME_CHAN_CONFIG_REQ",
    /// Configuration confirmation.
    "ME_CHAN_CONFIG_CFM",
    /// Set control port state for a station.
    "ME_SET_CONTROL_PORT_REQ",
    /// Control port setting confirmation.
    "ME_SET_CONTROL_PORT_CFM",
    /// TKIP MIC failure indication.
    "ME_TKIP_MIC_FAILURE_IND",
    /// Add a station to the FW (AP mode)
    "ME_STA_ADD_REQ",
    /// Confirmation of the STA addition
    "ME_STA_ADD_CFM",
    /// Delete a station from the FW (AP mode)
    "ME_STA_DEL_REQ",
    /// Confirmation of the STA deletion
    "ME_STA_DEL_CFM",
    /// Indication of a TX RA/TID queue credit update
    "ME_TX_CREDITS_UPDATE_IND",
    /// Request indicating to the FW that there is traffic buffered on host
    "ME_TRAFFIC_IND_REQ",
    /// Confirmation that the @ref "ME_TRAFFIC_IND_REQ has been executed
    "ME_TRAFFIC_IND_CFM",
    /// Request RC statistics to a station
    "ME_RC_STATS_REQ",
    /// RC statistics confirmation
    "ME_RC_STATS_CFM",
    /// Request RC fixed rate
    "ME_RC_SET_RATE_REQ",
    /// Configure monitor interface
    "ME_CONFIG_MONITOR_REQ",
    /// Configure monitor interface response
    "ME_CONFIG_MONITOR_CFM",
    /// Setting Power Save mode request from host
    "ME_SET_PS_MODE_REQ",
    /// Set Power Save mode confirmation
    "ME_SET_PS_MODE_CFM",

    /*
     * Section of internal ME messages. No ME API messages should be defined below this point
     */
    /// Internal request to indicate that a VIF needs to get the HW going to ACTIVE or IDLE
    "ME_SET_ACTIVE_REQ",
    /// Confirmation that the switch to ACTIVE or IDLE has been executed
    "ME_SET_ACTIVE_CFM",
    /// Internal request to indicate that a VIF desires to de-activate/activate the Power-Save mode
    "ME_SET_PS_DISABLE_REQ",
    /// Confirmation that the PS state de-activate/activate has been executed
    "ME_SET_PS_DISABLE_CFM",
    /// Indication that data path is flushed for a given station
    "ME_DATA_PATH_FLUSHED_IND",
};

const char* sm_msg_name[] =
{
    /// Request to connect to an AP
    "SM_CONNECT_REQ",
    /// Confirmation of connection
    "SM_CONNECT_CFM",
    /// Indicates that the SM associated to the AP
    "SM_CONNECT_IND",
    /// Request to disconnect
    "SM_DISCONNECT_REQ",
    /// Confirmation of disconnection
    "SM_DISCONNECT_CFM",
    /// Indicates that the SM disassociated the AP
    "SM_DISCONNECT_IND",
    /// Request to start external authentication
    "SM_EXTERNAL_AUTH_REQUIRED_IND",
    /// Response to external authentication request
    "SM_EXTERNAL_AUTH_REQUIRED_RSP",
    /// Request to update assoc elements after FT over the air authentication
    "SM_FT_AUTH_IND",
    /// Response to FT authentication with updated assoc elements
    "SM_FT_AUTH_RSP",

    // Section of internal SM messages. No SM API messages should be defined below this point
    /// Timeout message for procedures requiring a response from peer
    "SM_RSP_TIMEOUT_IND",
};

const char* apm_msg_name[] =
{
    /// Request to start the AP.
    "APM_START_REQ",
    /// Confirmation of the AP start.
    "APM_START_CFM",
    /// Request to stop the AP.
    "APM_STOP_REQ",
    /// Confirmation of the AP stop.
    "APM_STOP_CFM",
    /// Request to start CAC
    "APM_START_CAC_REQ",
    /// Confirmation of the CAC start
    "APM_START_CAC_CFM",
    /// Request to stop CAC
    "APM_STOP_CAC_REQ",
    /// Confirmation of the CAC stop
    "APM_STOP_CAC_CFM",
    /// Request to Probe Client
    "APM_PROBE_CLIENT_REQ",
    /// Confirmation of Probe Client
    "APM_PROBE_CLIENT_CFM",
    /// Indication of Probe Client status
    "APM_PROBE_CLIENT_IND,"
};

const char* bam_msg_name[] =
{
    /// BAM addition response timeout
    "BAM_ADD_BA_RSP_TIMEOUT_IND",
    /// BAM deletion of a BA idx
    "BAM_DEL_BA_IDX_REQ",
    /// BAM Inactivity timeout
    "BAM_INACTIVITY_TIMEOUT_IND",
};

const char* mesh_msg_name[] =
{
    /// Request to start the Mesh Point
    "MESH_START_REQ",
    /// Confirmation of the MP start.
    "MESH_START_CFM",

    /// Request to stop the MP.
    "MESH_STOP_REQ",
    /// Confirmation of the MP stop.
    "MESH_STOP_CFM",

    /// Request to update the MP
    "MESH_UPDATE_REQ",
    /// Confirmation of the MP update
    "MESH_UPDATE_CFM",

    /// Request information about a given link
    "MESH_PEER_INFO_REQ",
    /// Response to the "MESH_PEER_INFO_REQ message
    "MESH_PEER_INFO_CFM",

    /// Request establishment of a path with a given mesh STA
    "MESH_PATH_CREATE_REQ",
    /// Confirmation to the "MESH_PATH_CREATE_REQ message
    "MESH_PATH_CREATE_CFM",

    /// Request a path update (delete path", modify next hop mesh STA)
    "MESH_PATH_UPDATE_REQ",
    /// Confirmation to the "MESH_PATH_UPDATE_REQ message
    "MESH_PATH_UPDATE_CFM",

    /// Indication from Host that the indicated Mesh Interface is a proxy for an external STA
    "MESH_PROXY_ADD_REQ",

    /// Indicate that a connection has been established or lost
    "MESH_PEER_UPDATE_IND",
    /// Notification that a connection has been established or lost (when MPM handled by userspace)
    "MESH_PEER_UPDATE_NTF",

    /// Indicate that a path is now active or inactive
    "MESH_PATH_UPDATE_IND",
    /// Indicate that proxy information have been updated
    "MESH_PROXY_UPDATE_IND",
};

const char* rxu_msg_name[] =
{
    /// Management frame reception indication
    "RXU_MGT_IND",
    /// NULL frame reception indication
    "RXU_NULL_DATA",
};

const char* rm_msg_name[] =
{
    /// Indication that RM task is ready to process next RM request
    /// Send by RM task to itself
    "RM_PROCESS_NEXT_REQUEST_IND",
};

const char* twt_msg_name[] =
{
    /// Request Individual TWT Establishment
    "TWT_SETUP_REQ",
    /// Confirm Individual TWT Establishment
    "TWT_SETUP_CFM",
    /// Indicate TWT Setup response from peer
    "TWT_SETUP_IND",
    /// Request to destroy a TWT Establishment or all of them
    "TWT_TEARDOWN_REQ",
    /// Confirm to destroy a TWT Establishment or all of them
    "TWT_TEARDOWN_CFM",
};

const char* ftm_msg_name[] =
{
    /// Setup a FTM measurement
    "FTM_START_REQ",
    /// Confirmation of the MP start.
    "FTM_START_CFM",
    /// FTM measurement available
    "FTM_DONE_IND",
    /// MAX number of messages
    "FTM_MAX",
};

const char* host_msg_name[] =
{
    /// Management frame reception indication
    "Host_MGT",
};

const char** msg_name_array[] =
{
   mm_msg_name,
   dbg_msg_name,
   scan_msg_name,
   tdls_msg_name,
   scanu_msg_name,
   me_msg_name,
   sm_msg_name,
   apm_msg_name,
   bam_msg_name,
   mesh_msg_name,
   rxu_msg_name,
   rm_msg_name,
   twt_msg_name,
   ftm_msg_name,
   host_msg_name
};
#endif

void ke_msg_check(void)
{
    struct ke_msg *msg;
    struct co_list_hdr *ptr;
    bool wrong = false;
    unsigned long flags;

    dbg("%s\n", __func__);

    spin_lock_irqsave(&ke_env.bl_hw->ke_queue_lock, flags);

    ptr = ke_env.queue_sent.first;
    while(ptr) {
        msg = (struct ke_msg*) ptr;
        
        if (softmac_task_ids_check(msg->dest_id) == false)
        {
            wrong = true;
            break;
        }
        
        ptr = ptr->next;
    }

    if (wrong) {
        dbg_f("%s, something wrong\n", __func__);
        
        dump_stack();
        
        ptr = ke_env.queue_sent.first;
        
        while(ptr) {
            msg = (struct ke_msg*) ptr;

            dbg_f("msg:0x%p, id:0x%x, src_id:0x%x, dest_id:0x%x\n",
                  msg, msg->id, msg->src_id, msg->dest_id);

            ptr = ptr->next;
        }
    }

    spin_unlock_irqrestore(&ke_env.bl_hw->ke_queue_lock, flags);
}

/**
 ****************************************************************************************
 * @brief Allocate memory for a message
 *
 * This primitive allocates memory for a message that has to be sent. The memory
 * is allocated dynamically on the heap and the length of the variable parameter
 * structure has to be provided in order to allocate the correct size.
 *
 * Several additional parameters are provided which will be preset in the message
 * and which may be used internally to choose the kind of memory to allocate.
 *
 * The memory allocated will be automatically freed by the kernel, after the
 * pointer has been sent to ke_msg_send(). If the message is not sent, it must
 * be freed explicitly with ke_msg_free().
 *
 * Allocation failure is considered critical and should not happen.
 *
 * @param[in] id        Message identifier
 * @param[in] dest_id   Destination Task Identifier
 * @param[in] src_id    Source Task Identifier
 * @param[in] param_len Size of the message parameters to be allocated
 *
 * @return Pointer to the parameter member of the ke_msg. If the parameter
 *         structure is empty, the pointer will point to the end of the message
 *         and should not be used (except to retrieve the message pointer or to
 *         send the message)
 ****************************************************************************************
 */
void *ke_msg_alloc(ke_msg_id_t const id, ke_task_id_t const dest_id,
                        ke_task_id_t const src_id, uint16_t const param_len)
{
    struct ke_msg *msg = (struct ke_msg*) ke_malloc(sizeof(struct ke_msg) +
                                                    param_len - sizeof (uint32_t));
    void *param_ptr = NULL;

    if (!msg) {
        dbg_f("ke_msg_alloc null %d\r\n", param_len);
        
        return NULL;
    }
    
    ASSERT_ERR(msg != NULL);
    msg->hdr.next = NULL;
    msg->id = id;
    msg->dest_id = dest_id;
    msg->src_id = src_id;
    msg->param_len = param_len;

    param_ptr = ke_msg2param(msg);

    memset(param_ptr, 0, param_len);

    return param_ptr;
}


/**
 ****************************************************************************************
 * @brief Message sending.
 *
 * Send a message previously allocated with any ke_msg_alloc()-like functions.
 *
 * The kernel will take care of freeing the message memory.
 *
 * Once the function have been called, it is not possible to access its data
 * anymore as the kernel may have copied the message and freed the original
 * memory.
 *
 * @param[in] param_ptr  Pointer to the parameter member of the message that
 *                       should be sent.
 ****************************************************************************************
 */
void ke_msg_send(void const *param_ptr)
{
    struct ke_msg * msg = ke_param2msg(param_ptr);
    int type = KE_TYPE_GET(msg->dest_id);
    int idx = KE_IDX_GET(msg->dest_id);

    dbg_ke("ke_msg_send, msg:0x%p, msg->id:0x%x, dest:0x%x, src:0x%x, msg_len:0x%x\r\n",
          msg, msg->id, msg->dest_id, msg->src_id, msg->param_len); 

    ke_msg_check();
       
    ASSERT_ERR((type < TASK_MAX || type == DRV_TASK_ID));
    if (ke_task_local(type)) {
        ASSERT_ERR(idx < TASK_DESC[type].idx_max);
    }

    #if MSG_INFO_STR_PRINT
    #if 0
    dbg("ke_msg_send:%s(0x%x) to %s from %s\r\n",
        msg_name_array[(msg->id)/1024][(msg->id)%1024], msg->id,
        task_name[task_print_index(msg->dest_id)], 
        task_name[task_print_index(msg->src_id)]);
    #endif
    #endif

    if (ke_task_local(type))
    {
        unsigned long flags;
        
        spin_lock_irqsave(&ke_env.bl_hw->ke_queue_lock, flags);
        ke_queue_push(&ke_env.queue_sent, &msg->hdr);
        ke_evt_only_set(KE_EVT_KE_MESSAGE_BIT);
        spin_unlock_irqrestore(&ke_env.bl_hw->ke_queue_lock, flags);

        dbg_ke("%s, queued, msg:0x%p, id:0x%x, src_id:0x%x, dest_id:0x%x, %p\n",
              __func__, msg, msg->id, msg->src_id, msg->dest_id,
              __builtin_frame_address(0));
        
        #ifndef CONFIG_KE_TASKLET
        complete(&ke_env.bl_hw->ke_wait);
        #else
        tasklet_schedule(&ke_env.bl_hw->ke_tasklet);
        #endif
    }
    else if (ke_task_drv(type))
    {
        dbg_ke("%s, to drv, msg:0x%p, id:0x%x, src_id:0x%x, dest_id:0x%x\n",
              __func__, msg, msg->id, msg->src_id, msg->dest_id);
        
        softmac_fwd_kmsg_to_drv(ke_env.bl_hw, msg);
        ke_msg_free(msg);
    }
    else
    {
        dbg_ke("%s, to fw, msg:0x%p\n", __func__, msg);
        
        // forward the message to fw
        softmac_fwd_kmsg_to_fw(ke_env.bl_hw, msg);
        ke_msg_free(msg);
    }

    ke_msg_check();
}

/**
 ****************************************************************************************
 * @brief Basic message sending.
 *
 * Send a message that has a zero length parameter member. No allocation is
 * required as it will be done internally.
 *
 * @param[in] id        Message identifier
 * @param[in] dest_id   Destination Identifier
 * @param[in] src_id    Source Identifier
 ****************************************************************************************
 */
void ke_msg_send_basic(ke_msg_id_t const id, ke_task_id_t const dest_id,
                               ke_task_id_t const src_id)
{
    void *no_param = KE_MSG_ALLOC_BASIC(id, dest_id, src_id, 0);
    ke_msg_send(no_param);
}

/**
 ****************************************************************************************
 * @brief Message forwarding.
 *
 * Forward a message to another task by changing its destination and source tasks IDs.
 *
 * @param[in] param_ptr  Pointer to the parameter member of the message that
 *                       should be sent.
 * @param[in] dest_id New destination task of the message.
 * @param[in] src_id New source task of the message.
 ****************************************************************************************
 */
void ke_msg_forward(void const *param_ptr, ke_task_id_t const dest_id,
                           ke_task_id_t const src_id)

{
    struct ke_msg * msg = ke_param2msg(param_ptr);

    // update the source and destination of the message
    msg->dest_id = dest_id;
    msg->src_id = src_id;

    // send the message
    ke_msg_send(param_ptr);
}

/**
 ****************************************************************************************
 * @brief Message forwarding.
 *
 * Forward a message to another task by changing its message, destination and source tasks IDs.
 *
 * @param[in] param_ptr  Pointer to the parameter member of the message that
 *                       should be sent.
 * @param[in] msg_id New Id of the message.
 * @param[in] dest_id New destination task of the message.
 * @param[in] src_id New source task of the message.
 ****************************************************************************************
 */
void ke_msg_forward_and_change_id(void const *param_ptr,
                                              ke_msg_id_t const msg_id,
                                              ke_task_id_t const dest_id,
                                              ke_task_id_t const src_id)
{
    struct ke_msg * msg = ke_param2msg(param_ptr);

    // update the source and destination of the message
    msg->id = msg_id;
    msg->dest_id = dest_id;
    msg->src_id = src_id;

    ke_msg_check();

    // send the message
    ke_msg_send(param_ptr);
}

/**
 ****************************************************************************************
 * @brief Free allocated message
 *
 * @param[in] msg   Pointer to the message to be freed (not the parameter member!)
 ****************************************************************************************
 */
void ke_msg_free(struct ke_msg const *msg)
{
    ke_free((void*)msg);

    ke_msg_check();
}

