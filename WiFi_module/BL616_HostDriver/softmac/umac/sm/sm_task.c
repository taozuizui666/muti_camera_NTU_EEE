#include "bl_ipc_compat.h"

#include "co_endian.h"
#include "ke_timer.h"

#include "softmac.h"

#include "sm_task.h"
#include "scanu_task.h"
#include "sm.h"
#include "me_utils.h"
#include "mac_frame.h"
#include "mac.h"
#include "mm_task.h"
#include "me.h"
#include "me_mgmtframe.h"
#include "vif_mgmt.h"
#include "sta_mgmt.h"
#include "rxu_task.h"
#include "txl_frame.h"

/**
 ****************************************************************************************
 * @brief @ref SM_CONNECT_REQ message handler.
 * This message Requests the UMAC to join and associate to a specific AP.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
sm_connect_req_handler(ke_msg_id_t const msgid,
                                struct sm_connect_req const *param,
                                ke_task_id_t const dest_id,
                                ke_task_id_t const src_id)
{
    struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];
    struct sm_connect_cfm *cfm;
    int msg_status = KE_MSG_CONSUMED;

    if (ke_state_get(TASK_SM) == SM_DISCONNECTING)
        return KE_MSG_SAVED;

    dbg_f("%s, {VIF-%d type-%d}, auth_type=%d BSSID=%04x %04x %04x, IE len=%d, task_sm state:%d, vif active:%d\r\n",
          __func__, param->vif_idx, vif->type, param->auth_type, 
          param->bssid.array[0], param->bssid.array[1], param->bssid.array[2], 
          param->ie_len, ke_state_get(TASK_SM), vif->active);

    // Allocate the confirmation message
    cfm = KE_MSG_ALLOC(SM_CONNECT_CFM, src_id, dest_id, sm_connect_cfm);

    if (ke_state_get(TASK_SM) != SM_IDLE)
    {
        cfm->status = CO_BUSY;
        goto send_cfm;
    }

    if ((param->vif_idx >= NX_VIRT_DEV_MAX) || (vif->type != VIF_STA))
    {
        cfm->status = CO_BAD_PARAM;
        goto send_cfm;
    }

    if (vif->active)
    {
        if (param->flags & REASSOCIATION)
        {
            sm_env.reassoc = true;
        }
        else
        {
            cfm->status = CO_OP_IN_PROGRESS;
            goto send_cfm;
        }
    }
    else
    {
        // Sanity check - If we are not associated, we should not have a AP STA index
        // registered neither a channel context
        dbg_con("%s, vif_idx:%d, vif->u.sta.ap_id:%d\n", 
              __func__, param->vif_idx, vif->u.sta.ap_id);

        ASSERT_ERR(vif->u.sta.ap_id == INVALID_STA_IDX);
        sm_env.reassoc = false;
    }

    if ((param->auth_type == MAC_AUTH_ALGO_SAE) &&
        (sm_get_rsnie_pmkid_count(CPU2HW(param->ie_buf), param->ie_len) > 0))
    {
        uint8_t *update_auth_type = (uint8_t *)&param->auth_type;

        // If at least one PMKID is present SAE can be skipped to test PMKID.
        // If PMKID is not accepted by AP, it is assumed that host will try
        // to reconnect without PMKID
        *update_auth_type = MAC_AUTH_ALGO_OPEN;
    }

    // Save the parameters
    sm_env.connect_param = param;

    if (sm_env.connect_ind != NULL) {
        dbg_f("%s, sm_env.connect_ind not null\r\n", __func__);
        
        ke_msg_free(ke_param2msg(sm_env.connect_ind));
    }
    
    // Allocate the kernel message for the connection status forwarding
    sm_env.connect_ind = KE_MSG_ALLOC_VAR(SM_CONNECT_IND, src_id, dest_id, 
                                          sm_connect_ind, 
                                          MACIF_MAX_PARAM_LEN(sm_connect_ind));

    if (sm_env.reassoc)
    {
        // for reassociation, need to clean-up current connection first
        // The reassociation will start once the clean-up is done (in me_set_active_cfm_handler)
        memcpy(&sm_env.prev_bssid, &vif_info_tab[param->vif_idx].bssid, 
               sizeof(sm_env.prev_bssid));
        sm_disconnect_start(&vif_info_tab[param->vif_idx], 0, false);
    }
    else
    {
        struct mac_addr const *bssid = NULL;
        struct mac_chan_def const *chan = NULL;

        // Get the BSSID and Channel from the parameter and/or the scan results
        sm_get_bss_params(&bssid, &chan);

        // Check if we have enough information to launch the join or if we have to scan
        if (bssid && chan) {
            sm_join_bss(bssid, chan, false);
        } else {
            sm_scan_bss(bssid, chan);
        }
    }

    // We will now proceed to the connection, so the status is OK
    cfm->status = CO_OK;
    msg_status = KE_MSG_NO_FREE;

send_cfm:
    ke_msg_send(cfm);
    
    return msg_status;
}

/**
 ****************************************************************************************
 * @brief @ref SM_DISCONNECT_REQ message handler.
 * This message Requests the UMAC to disconnect from a specific AP
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
sm_disconnect_req_handler(ke_msg_id_t const msgid,
                                    struct sm_disconnect_req const *param,
                                    ke_task_id_t const dest_id,
                                    ke_task_id_t const src_id)
{
    struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];

    // Check if we are in an association or scan process
    if (ke_state_get(TASK_SM) != SM_IDLE || ke_state_get(TASK_SCANU) != SCANU_IDLE) 
    {
        dbg_f("%s, sm state:%d, scanu state:%d, saved, from-host:1, reason:%d\r\n",
                 __func__, 
                 ke_state_get(TASK_SM), ke_state_get(TASK_SCANU), param->reason_code);

        return KE_MSG_SAVED;
    }

    dbg_con("{VIF-%d} Disconnection request:\r\n", param->vif_idx);

    if ((vif->type != VIF_STA) || (!vif->active))
    {
        struct sm_disconnect_ind *disc = 
                       KE_MSG_ALLOC(SM_DISCONNECT_IND, TASK_API, TASK_SM,
                                    sm_disconnect_ind);
                                                      
        dbg_f("%s, directly send SM_DISCONNECT_CFM\r\n", __func__);
        
        ke_msg_send_basic(SM_DISCONNECT_CFM, TASK_API, TASK_SM);
        
        // Fill in the indication parameters
        disc->reason_code = MAC_RS_UNSPECIFIED;
        disc->vif_idx = vif->index;
        disc->reassoc = 0;
        
        dbg_f("%s, directly sm_env.disconnect.ind:0x%p\r\n",
                 __func__, sm_env.disconnect.ind);
                 
        ke_msg_send(disc);

        return KE_MSG_CONSUMED;
    }

    // Initialize the disconnection process
    sm_disconnect_start(vif, param->reason_code, true);

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref MM_CONNECTION_LOSS_IND message handler.
 * This message indicates that network connection with the associated AP has been lost.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
mm_connection_loss_ind_handler(ke_msg_id_t const msgid,
                                   struct mm_connection_loss_ind const *param,
                                   ke_task_id_t const dest_id,
                                   ke_task_id_t const src_id)
{
    struct softmac_vif_info_tag *vif = &vif_info_tab[param->inst_nbr];

    dbg_f("%s, sm state:%d, vif active:%d, type:%d\r\n", 
          __func__, ke_state_get(TASK_SM), vif->active, vif->type);

    // Check if we are in a disconnection procedure
    if (ke_state_get(TASK_SM) != SM_IDLE)
        // We are already in a procedure, so save the message
        return KE_MSG_SAVED;

    if ((vif->type != VIF_STA) || (!vif->active))
        return KE_MSG_CONSUMED;

    // Proceed to the disconnection
    sm_disconnect_start(vif, MAC_RS_UNSPECIFIED, false);

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref SM_RSP_TIMEOUT_IND message handler.
 * This message indicates that the programmed AUTH or ASSOC_REQ frame were not successfully
 * transmitted.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
sm_rsp_timeout_ind_handler(ke_msg_id_t const msgid, void const *param,
                                     ke_task_id_t const dest_id,
                                     ke_task_id_t const src_id)

{
    dbg_con("%s, ts:%uus\r\n", __func__, softmac_time_us());
    
    if (sm_env.tx_frame) {
        dbg_con("%s, sm_env.tx_frame not null\r\n", __func__);
        
        txl_frame_dump_info(sm_env.tx_frame);
    }
    
    // Check if we are in a state where such timeout could occur
    if ((ke_state_get(TASK_SM) != SM_AUTHENTICATING) &&
        (ke_state_get(TASK_SM) != SM_EXTERNAL_AUTHENTICATING) &&
        (ke_state_get(TASK_SM) != SM_FT_OVER_AIR) &&
        (ke_state_get(TASK_SM) != SM_ASSOCIATING) )
    {
        dbg_f("%s, sm state:%d\r\n", __func__, ke_state_get(TASK_SM));
        
        return KE_MSG_CONSUMED;
    }

    dbg_con("%s, sm_env.tx_frame_ongoing:%d, sm_env.tx_frame:0x%p, us:%llu\r\n",
          __func__, sm_env.tx_frame_ongoing, sm_env.tx_frame, softmac_time());

    // Check if the transmission of the Auth or AssocReq is still ongoing
    if (sm_env.tx_frame_ongoing)
    {
        // Disable the flag to disallow the retransmission of this frame.
        // The sm_connect_ind() function will be called when the frame transmission is
        // confirmed by the TX path
        sm_env.tx_frame_ongoing--;
    }
    else
    {
        // Status is not successful
        sm_connect_ind(MAC_ST_FAILURE);
    }

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref SCANU_START_CFM message handler.
 * This function handles scan confirmation from the scan module.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
scanu_start_cfm_handler(ke_msg_id_t const msgid, void const *param,
                                ke_task_id_t const dest_id,
                                ke_task_id_t const src_id)
{
    struct mac_addr const *bssid = NULL;
    struct mac_chan_def const *chan = NULL;

    dbg_con("%s\r\n", __func__);

    // Sanity check - This message can be received only in the SCANNING or JOINING states
    ASSERT_ERR(ke_state_get(TASK_SM) == SM_SCANNING);

    sm_get_bss_params(&bssid, &chan);
      
    // Check if we now have enough information to launch the join procedure
    if (bssid && chan)
    {
        sm_join_bss(bssid, chan, false);
    }
    else
    {
        // We did not find our BSS, so terminate the connection procedure
        sm_connect_ind(MAC_ST_FAILURE);
    }

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref SCANU_JOIN_CFM message handler.
 * This function handles joining confirmation from the scan module.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
scanu_join_cfm_handler(ke_msg_id_t const msgid, void const *param,
                               ke_task_id_t const dest_id,
                               ke_task_id_t const src_id)
{
    struct sm_connect_req const *con_par = sm_env.connect_param;
    struct softmac_vif_info_tag *vif;
    struct me_bss_info *bss;

    dbg_con("%s\r\n", __func__);
    
    if (sm_env.connect_param == NULL) {
        dbg_con("%s, null connect_param, return\r\n", __func__);
        
        return KE_MSG_CONSUMED;
    }

    // Sanity check - This message can be received only in the SCANNING or JOINING states
    ASSERT_ERR(ke_state_get(TASK_SM) == SM_JOINING);

    // Get parameters
    vif = &vif_info_tab[con_par->vif_idx];
    bss = &vif->bss_info;

    // Check if the joining procedure was successful
    if (BSS_CAPA(bss, VALID))
    {
        struct mm_sta_add_req *req = KE_MSG_ALLOC(MM_STA_ADD_REQ, TASK_MM,
                                                  TASK_SM, mm_sta_add_req);
        uint32_t paid_gid = 0;

        // Save the VIF flags
        vif->flags = con_par->flags;
        softmac_vif_update(con_par->vif_idx, VIF_FIELD_FLAGS, 
                           (uint8_t *)&vif->flags, sizeof(vif->flags));

        // Check if we need to disable the HT/VHT/HE feature
        if (con_par->flags & DISABLE_HT)
        {
            BSS_CAPA_CLR(bss, HT);
            BSS_CAPA_CLR(bss, VHT);
            BSS_CAPA_CLR(bss, HE);
            softmac_vif_update(con_par->vif_idx, VIF_FIELD_BSS_CAP_FLAGS, 
                               (uint8_t *)&bss->capa_flags, 
                               sizeof(bss->capa_flags));
        }

        req->uapsd_queues = sm_env.connect_param->uapsd_queues;
        req->sm_task_state = SM_STA_ADDING;
        
        // Fill in the MM_STA_ADD_REQ parameters
        // In case of FullMAC the capability flags are not set here. They will be set after
        // the STA is registered
        req->capa_flags = 0;
        req->inst_nbr = con_par->vif_idx;
        req->mac_addr = bss->bssid;
        req->tdls_sta = false;
        req->bssid_index = bss->bssid_index;
        req->max_bssid_ind = bss->max_bssid_ind;
        if (BSS_CAPA(bss, HT))
        {
            struct mac_htcapability *ht_cap = &bss->ht_cap;
            struct mac_vhtcapability *vht_cap = NULL;
            struct mac_hecapability *he_cap = NULL;

            #if NX_VHT
            if (BSS_CAPA(bss, VHT))
            {
                vht_cap = &bss->vht_cap;
                paid_gid = mac_paid_gid_sta_compute(&bss->bssid);
            }
            #endif
            
            #if NX_HE
            if (BSS_CAPA(bss, HE))
            {
                he_cap = &bss->he_cap;
            }
            #endif
            
            me_get_ampdu_params(ht_cap, vht_cap, he_cap,
                          &req->ampdu_size_max_ht, &req->ampdu_size_max_vht, 
                          &req->ampdu_size_max_he, &req->ampdu_spacing_min);
        }
        
        req->paid_gid = paid_gid;

        softmac_vif_update(con_par->vif_idx, VIF_FIELD_BSS_INFO, 
                           (uint8_t *)bss, sizeof(struct me_bss_info));
        softmac_vif_update(con_par->vif_idx, VIF_FIELD_TX_PWR, NULL, 0);

        // Send the message
        ke_msg_send(req);

        // We are now waiting for the station addition confirmation
        ke_state_set(TASK_SM, SM_STA_ADDING);
    }
    else
    {
        // If confirmed joined procedure used an active scan, try again with a passive scan
        if (!sm_env.join_passive)
        {
            struct mac_addr const *bssid = NULL;
            struct mac_chan_def const *chan = NULL;

            sm_get_bss_params(&bssid, &chan);
            
            #if (NX_ANT_DIV)
            // Switch antenna paths and try to join again in case of join not completed successfully
            if (!scanu_env.join_status)
            {
                // Request an antenna switch
                ke_msg_send_basic(MM_SWITCH_ANTENNA_REQ, TASK_MM, TASK_SM);
            }
            #endif //(NX_ANT_DIV)
            
            sm_join_bss(bssid, chan, true);
        }
        else
        {
            // The join was not successful, so terminate the connection procedure
            sm_connect_ind(MAC_ST_FAILURE);
        }
    }

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref MM_STA_ADD_CFM message handler.
 * This function handles the STA addition confirmation message.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int mm_sta_add_cfm_handler(ke_msg_id_t const msgid,
                                             struct mm_sta_add_cfm const *param,
                                             ke_task_id_t const dest_id,
                                             ke_task_id_t const src_id)
{
    // Sanity check - This message can be received only in the STA ADDING state
    ASSERT_ERR(ke_state_get(TASK_SM) == SM_STA_ADDING);
    dbg_con("%s, sm state:%d\r\n", __func__, ke_state_get(TASK_SM));

    if (ke_state_get(TASK_SM) != SM_STA_ADDING) {
        dbg_f("%s, wrong sm state:%d\r\n", __func__, ke_state_get(TASK_SM));
    }

    // Check if the STA was correctly added
    if (param->status == CO_OK && ke_state_get(TASK_SM) == SM_STA_ADDING)
    {
        struct softmac_sta_info_tag *sta = &sta_info_tab[param->sta_idx];
        struct softmac_vif_info_tag *vif = 
                            &vif_info_tab[sm_env.connect_param->vif_idx];

        sta->mac_addr = vif->bss_info.bssid;
        vif->u.sta.ap_id = param->sta_idx;
        vif->chan_ctxt_idx = param->chan_idx;
        
        dbg_con("%s, vif->u.sta.ap_id:%d, vif_idx:%d\r\n",
              __func__, vif->u.sta.ap_id, sm_env.connect_param->vif_idx);
        
        // Set the BSS parameters to LMAC
        sm_set_bss_param();
    }
    else if (ke_state_get(TASK_SM) == SM_DISCONNECTING)
    {
        dbg_f("%s, bypass wrong sm state\r\n", __func__);
    }
    else
    {
        // No station available, terminate the connection procedure
        sm_connect_ind(MAC_ST_FAILURE);
    }
    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref ME_SET_PS_DISABLE_CFM message handler.
 * This function starts the procedure of BSS parameter setting.
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
    struct ke_msg *msg = (struct ke_msg *)co_list_pick(&sm_env.bss_config);

    // Sanity check - This message can be received only in the STA ADDING state
    ASSERT_ERR((ke_state_get(TASK_SM) == SM_BSS_PARAM_SETTING) ||
               (ke_state_get(TASK_SM) == SM_IDLE) ||
               (ke_state_get(TASK_SM) == SM_DISCONNECTING));

    dbg_con("%s, sm:%d\r\n", __func__, ke_state_get(TASK_SM));

    if (msg == NULL)
    {
        dbg_con("%s %u, sm:%d, next msg NULL\r\n", 
                __func__, __LINE__, ke_state_get(TASK_SM));

        return KE_MSG_CONSUMED;
    }
    
    // Check if we were disabling the PS prior to the parameter setting
    if (ke_state_get(TASK_SM) == SM_BSS_PARAM_SETTING &&
        (msg->id == MM_SET_BSSID_REQ))
    {
        // Send the next BSS parameter configuration message
        sm_bss_config_send();
    }
    else if (ke_state_get(TASK_SM) == SM_DISCONNECTING &&
             (msg->id==MM_SET_VIF_STATE_REQ || msg->id==MM_STA_DEL_REQ))
    {
        // Send the next BSS parameter configuration message
        sm_bss_config_send();
    }
    else 
    {
        dbg_con("%s %u, sm:%d, next msg id:0x%x\r\n", 
                __func__, __LINE__, ke_state_get(TASK_SM), msg->id);
    }

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief BSS parameter setting message handler.
 * This function handles all the confirmation of the BSS parameter setting requests sent
 * during the connection process. It pushes the next parameter setting request.
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
    struct ke_msg *msg = (struct ke_msg *)co_list_pick(&sm_env.bss_config);

    dbg_con("%s, sm:%d\r\n", __func__, ke_state_get(TASK_SM));

    if (ke_state_get(TASK_SM) != SM_BSS_PARAM_SETTING)
    {
        dbg_f("%s, sm state is not SM_BSS_PARAM_SETTING\r\n", __func__);
        
        return KE_MSG_CONSUMED;
    }

    if (msg == NULL) 
    {
        dbg_con("%s %u, sm:%d, next msg null\r\n", 
                __func__, __LINE__, ke_state_get(TASK_SM));

        return KE_MSG_CONSUMED;
    } else if (msg->id != MM_SET_BASIC_RATES_REQ && 
               msg->id != MM_SET_BEACON_INT_REQ &&
               msg->id != MM_SET_BSS_COLOR_REQ &&
               msg->id != ME_SET_ACTIVE_REQ) 
    {
        dbg_con("%s %u, sm:%d, next msg id:0x%x\r\n", 
                __func__, __LINE__, ke_state_get(TASK_SM), msg->id);

        return KE_MSG_CONSUMED;
    }

    // Send the next BSS parameter configuration message
    sm_bss_config_send();

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref ME_SET_ACTIVE_CFM message handler.
 * This function handles the confirmation of move to active state that allows starting
 * the AUTH/ASSOC_REQ exchanges.
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
me_set_active_cfm_handler(ke_msg_id_t const msgid, void const *param,
                                    ke_task_id_t const dest_id,
                                    ke_task_id_t const src_id)
{
    dbg_con("%s, sm:%d\r\n", __func__, ke_state_get(TASK_SM));
    
    // Sanity check - This message can be received only in the STA ADDING state
    ASSERT_ERR((ke_state_get(TASK_SM) == SM_BSS_PARAM_SETTING) ||
               (ke_state_get(TASK_SM) == SM_DISCONNECTING));

    // Check if we went to IDLE due to the disconnection procedure
    if (ke_state_get(TASK_SM) == SM_DISCONNECTING)
    {
        struct softmac_vif_info_tag *vif = sm_env.disconnect.vif;

        // Invalidate the BSS information structure
        BSS_CAPA_RESET(&vif->bss_info);
        softmac_vif_update(vif->index, VIF_FIELD_BSS_CAP_FLAGS, 
                           (uint8_t *)vif->bss_info.capa_flags, 
                           sizeof(vif->bss_info.capa_flags));

        dbg_f("%s, from-host:%d, host_initiated_org:%d\r\n", 
                 __func__, sm_env.disconnect.host_initiated, 
                 sm_env.disconnect.host_initiated_org);
             
        if (sm_env.disconnect.host_initiated || 
            sm_env.disconnect.host_initiated_org)
        {
            dbg_f("%s, send SM_DISCONNECT_CFM\r\n", __func__);
            
            ke_msg_send_basic(SM_DISCONNECT_CFM, TASK_API, TASK_SM);
            sm_env.disconnect.host_initiated = false;
            sm_env.disconnect.host_initiated_org = false;
        }

        // Send the end of disconnection procedure indication to the host
        ke_msg_send(ke_msg2param(sm_env.disconnect.ind));
        sm_env.disconnect.ind = NULL;

        if (sm_env.reassoc)
        {
            struct mac_addr const *bssid = NULL;
            struct mac_chan_def const *chan = NULL;

            dbg_f("{VIF-%d} Disconnected, start Re-Association\r\n", vif->index);

            sm_get_bss_params(&bssid, &chan);
            sm_join_bss(bssid, chan, false);
        }
        else
        {
            dbg_f("{VIF-%d} Disconnected\r\n", vif->index);
            
            ke_state_set(TASK_SM, SM_IDLE);
        }
    }
    else
    {
        sm_env.tx_frame_ongoing = 0;
        
        if (sm_env.tx_frame) {
            dbg_f("%s, sm_env.tx_frame not null\r\n", __func__);
            
            txl_frame_dump_info(sm_env.tx_frame);
            ASSERT_WARN(0);
            softmac_free_skb(sm_env.tx_frame->skb);
        }
        sm_env.tx_frame = NULL;

        switch(sm_env.connect_param->auth_type)
        {
            case MAC_AUTH_ALGO_FT:
                if (sm_env.connect_param->flags & FT_OVER_DS)
                {
                    // For FT over DS, authentication has already been done
                    sm_assoc_req_send();
                    break;
                }
                // FT over the AIR continue with authentication
            case MAC_AUTH_ALGO_OPEN:
            case MAC_AUTH_ALGO_SHARED:
                sm_auth_send(MAC_AUTH_FIRST_SEQ, NULL);
                break;
            case MAC_AUTH_ALGO_SAE:
                sm_external_auth_start(MAC_RSNIE_AKM_SAE);
                break;
            default:
                sm_connect_ind(MAC_ST_FAILURE);
        }
    }

    return KE_MSG_CONSUMED;
}


/**
 ****************************************************************************************
 * @brief @ref MM_SET_VIF_STATE_CFM message handler.
 * This function is called once the VIF has been put to active state at the end of the
 * association procedure, or to inactive state upon disconnection.
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
    dbg_con("%s, task_sm state:%d, vif_idx:%d, vif active:%d, call softmac_vif_update_local\r\n",
          __func__, ke_state_get(TASK_SM), param->inst_nbr, param->active);

    softmac_vif_update_local(param->inst_nbr, VIF_FIELD_ACTIVE,
                             (uint8_t *)&param->active, 1);

    // Check if we are in the connection process
    if (ke_state_get(TASK_SM) == SM_ACTIVATING)
    {
        struct sm_connect_req const *con_par = sm_env.connect_param;
        struct softmac_vif_info_tag *vif = &vif_info_tab[con_par->vif_idx];
        //struct softmac_sta_info_tag *sta = &sta_info_tab[vif->u.sta.ap_id];
        #if NX_POWERSAVE
        struct mm_set_ps_options_req *req;
        #endif

        if (param->active == false)
        {
            dbg_con("%s %u, active:%d, sm:%d, cnt:%d\r\n", 
                    __func__, __LINE__, param->active,
                    ke_state_get(TASK_SM), co_list_cnt(&sm_env.bss_config));

            return KE_MSG_CONSUMED;
        }

        #if NX_POWERSAVE
        // Get a pointer to the kernel message
        req = KE_MSG_ALLOC(MM_SET_PS_OPTIONS_REQ, TASK_MM, TASK_SM, 
                           mm_set_ps_options_req);

        // Fill the message parameters
        req->dont_listen_bc_mc = con_par->dont_wait_bcmc;
        req->listen_interval = con_par->listen_interval;
        req->vif_index = con_par->vif_idx;

        // Set the PS options for this VIF
        ke_msg_send(req);
        #endif

        // If no EAP frame has to be sent, reenable the PS mode now that the association is complete
        if (!(vif->flags & CONTROL_PORT_HOST))
        {
            struct me_set_ps_disable_req *ps = 
                        KE_MSG_ALLOC(ME_SET_PS_DISABLE_REQ, TASK_ME, TASK_SM,
                                     me_set_ps_disable_req);

            ps->ps_disable = false;
            ps->vif_idx = con_par->vif_idx;

            ke_msg_send(ps);
        }

        // Association can now be considered as complete
        sm_connect_ind(MAC_ST_SUCCESSFUL);
    }
    // Check if we were setting the VIF state due to a disconnection
    else if (ke_state_get(TASK_SM) == SM_DISCONNECTING)
    {
        struct ke_msg *msg = (struct ke_msg *)co_list_pick(&sm_env.bss_config);

        if (param->active == false && msg && msg->id == MM_STA_DEL_REQ) {
            // Send the next BSS disconnection message
            sm_bss_config_send();
        } else {
            dbg_con("%s %u, active:%d, sm:%d, cnt:%d\r\n", 
                    __func__, __LINE__, param->active,
                    ke_state_get(TASK_SM), co_list_cnt(&sm_env.bss_config));
        }
    }
    else
    {
        ASSERT_WARN(0);
    }

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref MM_STA_DEL_CFM message handler.
 * This function is called once the STA element associated to a VIF has been deleted.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
mm_sta_del_cfm_handler(ke_msg_id_t const msgid,
                                 struct mm_sta_del_cfm const *param,
                                 ke_task_id_t const dest_id, 
                                 ke_task_id_t const src_id)
{
    struct ke_msg *msg = (struct ke_msg *)co_list_pick(&sm_env.bss_config);

    // Sanity check - This message can be received during the disconnection procedure only
    if (ke_state_get(TASK_SM) != SM_DISCONNECTING) {
        dbg_con("%s %u, sm:%d, cnt:%d\r\n", 
                __func__, __LINE__,
                ke_state_get(TASK_SM), co_list_cnt(&sm_env.bss_config));

        return KE_MSG_CONSUMED;
    }

    vif_info_tab[param->vif_idx].u.sta.ap_id = INVALID_STA_IDX;
    vif_info_tab[param->vif_idx].chan_ctxt_idx = CHAN_CTXT_UNUSED;

    dbg_con("%s, sm:%d, vif_idx:%d, clear ap_id to INVALID_STA_IDX\r\n", 
            __func__, ke_state_get(TASK_SM), param->vif_idx);

    if (msg == NULL) {
        dbg_con("%s %u, sm:%d\r\n", 
                __func__, __LINE__, ke_state_get(TASK_SM));
        
        return KE_MSG_CONSUMED;
    }
    else if (msg->id != ME_SET_ACTIVE_REQ) {
        dbg_con("%s %u, sm:%d, next msg id:0x%x, cnt:%d\r\n", 
                __func__, __LINE__,
                ke_state_get(TASK_SM), msg->id, 
                co_list_cnt(&sm_env.bss_config));
        
        return KE_MSG_CONSUMED;
    }
    
    // Send the next BSS disconnection message
    sm_bss_config_send();

    return KE_MSG_CONSUMED;
}

static int
me_misc_cfm_handler(ke_msg_id_t const msgid, void const *param,
                             ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    // Sanity check - This message can be received during the disconnection procedure only
    if(ke_state_get(TASK_SM) != SM_DISCONNECTING)
    {
        dbg_con("%s %u, sm:%d\r\n", __func__, __LINE__, ke_state_get(TASK_SM));

        return KE_MSG_CONSUMED;
    }

    dbg_con("%s, sm:%d\r\n", __func__, ke_state_get(TASK_SM));
    
    // Send the next BSS disconnection message
    sm_bss_config_send();

    return KE_MSG_CONSUMED;
}

static int
sm_fw_act_ind_handler(ke_msg_id_t const msgid, 
                               struct fw_act_ind const *param,
                               ke_task_id_t const dest_id, 
                               ke_task_id_t const src_id)
{
    if (param->act_type == FW_ACT_SEND_TXL_FRAME) {
        struct txl_frame_snd_req *req_local = NULL;
        struct txl_frame_snd_req *req_remote = NULL;

        req_remote = (struct txl_frame_snd_req *)param->data;

        if (req_remote->frame_type == MAC_FCTRL_DEAUTHENT) {
            dbg_f("%s, deauth frame tx act indication\r\n", __func__);

            sm_deauth_cfm(NULL, 0);

            return KE_MSG_CONSUMED;
        }
        
        if (sm_env.tx_frame == NULL) {
            dbg_f("%s, sm_env.tx_frame consumed, ongoing:%d\r\n",
                  __func__, sm_env.tx_frame_ongoing);
                  
            return KE_MSG_CONSUMED;
        }

        req_local = txl_frame_get_req(sm_env.tx_frame);

        dbg_con("%s, req_local frm_type:0x%x, %d %d, req_remote frm_type:0x%x %d %d, cfm_func:0x%p\r\n",
              __func__, req_local->frame_type, req_local->vif_idx, req_local->sta_idx,
              req_remote->frame_type, req_remote->vif_idx, req_remote->sta_idx,
              sm_env.tx_frame->cfm.cfm_func);

        if (req_local->frame_type != req_remote->frame_type) {
            dbg_f("%s, frame_type:0x%x 0x%x, consumed without call cb\n", 
                  __func__, req_local->frame_type, req_remote->frame_type);

            return KE_MSG_CONSUMED;
        }

        if (req_local->frame_type == MAC_FCTRL_ASSOCREQ || req_local->frame_type == MAC_FCTRL_AUTHENT)
            ASSERT_ERR(sm_env.tx_frame == sm_env.tx_frame->cfm.env);
            
        ASSERT_ERR(sm_env.tx_frame->cfm.cfm_func);

        if (sm_env.tx_frame->cfm.cfm_func) {
            cfm_func_ptr cfm_func = sm_env.tx_frame->cfm.cfm_func;
            void *env = sm_env.tx_frame->cfm.env;

            dbg_con("%s, call cfm_func\n", __func__);
            
            cfm_func(env, param->status);
        }
    } else {
        dbg_f("%s, unknow act type:%d\r\n", __func__, param->act_type);
    }
    
    return KE_MSG_CONSUMED;
}


/**
 ****************************************************************************************
 * @brief @ref RXU_MGT_IND message handler.
 * This function handles the reception of the AUTH/(RE)ASSOCRSP/DEAUTH/DISASSOC frames and
 * dispatch them to the correct function.
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
    uint16_t fctl = param->framectrl & MAC_FCTRL_TYPESUBTYPE_MASK;
    int msg_status = KE_MSG_CONSUMED;

    if (sm_external_auth_in_progress())
    {
        return msg_status;
    }

    // Call correct frame handler
    if ((fctl == MAC_FCTRL_AUTHENT) && 
        (ke_state_get(TASK_SM) == SM_AUTHENTICATING))
    {
        sm_auth_handler(param);
    }
    else if ((fctl == MAC_FCTRL_ASSOCRSP) && 
              (ke_state_get(TASK_SM) == SM_ASSOCIATING))
    {
        sm_assoc_rsp_handler(param);
    }
    else if ((fctl == MAC_FCTRL_REASSOCRSP) && 
             (ke_state_get(TASK_SM) == SM_ASSOCIATING))
    {
        sm_assoc_rsp_handler(param);
    }
    else if ((fctl == MAC_FCTRL_DEAUTHENT) || (fctl == MAC_FCTRL_DISASSOC))
    {
        msg_status = sm_deauth_handler(param);
    }
    else if (fctl == MAC_FCTRL_ACTION)
    {
        #if NX_MFP
        PTR2UINT payload = CPU2HW(param->payload);
        uint8_t action = co_read8p(payload + MAC_ACTION_CATEGORY_OFT);

        if (action == MAC_SA_QUERY_ACTION_CATEGORY)
            sm_sa_query_handler(param);
        #endif // NX_MFP
    }

    return msg_status;
}

/**
 ****************************************************************************************
 * @brief @ref SM_EXTERNAL_AUTH_REQUIRED_RSP message handler.
 * This function handles the reception of the SM_EXTERNAL_AUTH_REQUIRED_RSP message sent
 * by the host once the external authentication is completed
 *
 * @param[in] msgid Id of the message received.
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
sm_external_auth_required_rsp_handler(ke_msg_id_t const msgid,
                          struct sm_external_auth_required_rsp const *param,
                          ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    if (ke_state_get(TASK_SM) == SM_EXTERNAL_AUTHENTICATING)
        sm_external_auth_end(param->status);

    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief @ref SM_FT_AUTH_RSP message handler.
 * This function handles the reception of the SM_FT_AUTH_RSP message sent by the host
 * after association element for FT over the air procedure.
 *
 * @param[in] msgid Id of the message received.
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
sm_ft_auth_rsp_handler(ke_msg_id_t const msgid,
                               struct sm_connect_req *param,
                               ke_task_id_t const dest_id,
                               ke_task_id_t const src_id)
{
    // Sanity check - This message can be received during FT over the air
    ASSERT_ERR(ke_state_get(TASK_SM) == SM_FT_OVER_AIR);

    sm_ft_auth_over_air_end(param);

    return KE_MSG_NO_FREE;
}

/**
 ****************************************************************************************
 * @brief @ref ME_DATA_PATH_FLUSHED_IND message handler.
 * This function handles the reception of the ME_DATA_PATH_FLUSHED_IND message sent
 * by the ME once the data path is flushed as part of the disconnection process.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return Whether the message was consumed or not.
 ****************************************************************************************
 */
static int
me_data_path_flushed_ind_handler(ke_msg_id_t const msgid,
                                 struct me_data_path_flushed_ind const *param,
                                 ke_task_id_t const dest_id,
                                 ke_task_id_t const src_id)
{
    return KE_MSG_CONSUMED;
}

/// DEFAULT handler definition.
const struct ke_msg_handler sm_default_state[] =
{
    {SM_CONNECT_REQ,            (ke_msg_func_t)sm_connect_req_handler},
    {SM_DISCONNECT_REQ,         (ke_msg_func_t)sm_disconnect_req_handler},
    {SCANU_START_CFM,           (ke_msg_func_t)scanu_start_cfm_handler},
    {SCANU_JOIN_CFM,            (ke_msg_func_t)scanu_join_cfm_handler},
    {SM_RSP_TIMEOUT_IND,        (ke_msg_func_t)sm_rsp_timeout_ind_handler},
    {MM_CONNECTION_LOSS_IND,    (ke_msg_func_t)mm_connection_loss_ind_handler},
    {MM_STA_ADD_CFM,            (ke_msg_func_t)mm_sta_add_cfm_handler},
    {ME_SET_ACTIVE_CFM,         (ke_msg_func_t)me_set_active_cfm_handler},
    {MM_SET_BSSID_CFM,          mm_bss_param_setting_handler},
    {MM_SET_BASIC_RATES_CFM,    mm_bss_param_setting_handler},
    {MM_SET_BEACON_INT_CFM,     mm_bss_param_setting_handler},
    {MM_SET_BSS_COLOR_CFM,      mm_bss_param_setting_handler},
    //{MM_SET_EDCA_CFM, mm_bss_param_setting_handler},
    {MM_SET_VIF_STATE_CFM,      (ke_msg_func_t)mm_set_vif_state_cfm_handler},
    {ME_SET_PS_DISABLE_CFM,     (ke_msg_func_t)me_set_ps_disable_cfm_handler},
    #if NX_POWERSAVE
    {MM_SET_PS_OPTIONS_CFM,     ke_msg_discard},
    #endif
    {MM_STA_DEL_CFM,            (ke_msg_func_t)mm_sta_del_cfm_handler},
    //{MM_CHAN_CTXT_UNLINK_CFM, (ke_msg_func_t)mm_chan_ctxt_unlink_cfm_handler},
    {RXU_MGT_IND,               (ke_msg_func_t)rxu_mgt_ind_handler},
    {SM_EXTERNAL_AUTH_REQUIRED_RSP, (ke_msg_func_t)sm_external_auth_required_rsp_handler},
    {SM_FT_AUTH_RSP,            (ke_msg_func_t)sm_ft_auth_rsp_handler},
    {ME_DATA_PATH_FLUSHED_IND,  (ke_msg_func_t)me_data_path_flushed_ind_handler},
    {ME_MISC_CFM,               (ke_msg_func_t)me_misc_cfm_handler},
    {DBG_FW_ACT_IND,            (ke_msg_func_t)sm_fw_act_ind_handler}
};

/// Specifies the message handlers that are common to all states.
const struct ke_state_handler sm_default_handler =
                                    KE_STATE_HANDLER(sm_default_state);

/// Defines the placeholder for the states of all the task instances.
ke_state_t sm_state[SM_IDX_MAX];


