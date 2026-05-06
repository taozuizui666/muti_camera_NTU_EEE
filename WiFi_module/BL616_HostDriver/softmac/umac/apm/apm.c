#include "rwnx_config.h"
#if NX_BEACONING
#include "vif_mgmt.h"
#include "apm.h"
#include "apm_task.h"
#include "mac_frame.h"
#include "me_utils.h"
#include "me_task.h"

#include "softmac.h"

/// Definition of the global environment.
struct apm apm_env;

/*
 * FUNCTION IMPLEMENTATIONS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief Initialize the BSS configuration list.
 ****************************************************************************************
 */
static void apm_bss_config_init(void)
{
    // Sanity check - No BSS config should be ongoing
    ASSERT_ERR(co_list_is_empty(&apm_env.bss_config));

    // Initialize the BSS configuration list
    co_list_init(&apm_env.bss_config);
}

/**
 ****************************************************************************************
 * @brief Push a BSS configuration message to the list.
 *
 * @param[in] param  Pointer to the message parameters
 ****************************************************************************************
 */
static void apm_bss_config_push(void *param)
{
    struct ke_msg *msg = ke_param2msg(param);

    co_list_push_back(&apm_env.bss_config, &msg->hdr);
}

void apm_init(void)
{
    // Reset the environment
    memset(&apm_env, 0, sizeof(apm_env));
    
    // Set state as Idle
    ke_state_set(TASK_APM, APM_IDLE);
}

void apm_start_cfm(uint8_t status)
{
    struct apm_start_req const *param = apm_env.param;
    struct apm_start_cfm *cfm = 
                KE_MSG_ALLOC(APM_START_CFM, TASK_API, TASK_APM, apm_start_cfm);

    dbg_con("%s, status:%d\r\n", __func__, status);

    // Check if the status is OK
    if (status == CO_OK)
    {
        struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];
        struct softmac_sta_info_tag *sta = 
                               &sta_info_tab[VIF_TO_BCMC_IDX(param->vif_idx)];
        struct mm_set_vif_state_req *req = 
                               KE_MSG_ALLOC(MM_SET_VIF_STATE_REQ, TASK_MM,
                                            TASK_APM, mm_set_vif_state_req);

        // Fill the message parameters
        req->active = true;
        req->inst_nbr = vif->index;

        // Send the message to the task
        ke_msg_send(req);

        vif->flags = param->flags;
        cfm->ch_idx = vif->chan_ctxt_idx;
        cfm->bcmc_idx = VIF_TO_BCMC_IDX(param->vif_idx);
        sta->mac_addr.array[0] = 0x1;

        softmac_ap_start_req(vif->index, VIF_TO_BCMC_IDX(param->vif_idx),
                             param->ctrl_port_ethertype, &param->basic_rates,
                             param->flags);
    }

    // Fill-in the message parameters
    cfm->status = status;
    cfm->vif_idx = param->vif_idx;

    // Send the message
    ke_msg_send(cfm);

    // Free the parameters
    ke_msg_free(ke_param2msg(param));
    apm_env.param = NULL;

    // We are now waiting for the channel addition confirmation
    ke_state_set(TASK_APM, APM_IDLE);
}

void apm_set_bss_param(void)
{
    struct apm_start_req const *param = apm_env.param;
    struct softmac_vif_info_tag *vif = &vif_info_tab[param->vif_idx];
    struct me_set_ps_disable_req *ps = 
                       KE_MSG_ALLOC(ME_SET_PS_DISABLE_REQ, TASK_ME, TASK_APM,
                                    me_set_ps_disable_req);
    struct mm_set_bssid_req *bssid = 
                       KE_MSG_ALLOC(MM_SET_BSSID_REQ, TASK_MM, TASK_APM,
                                    mm_set_bssid_req);
    struct mm_set_basic_rates_req *brates = 
                       KE_MSG_ALLOC(MM_SET_BASIC_RATES_REQ, TASK_MM,
                                   TASK_APM, mm_set_basic_rates_req);
    struct mm_set_beacon_int_req *bint = 
                       KE_MSG_ALLOC(MM_SET_BEACON_INT_REQ, TASK_MM,
                                    TASK_APM, mm_set_beacon_int_req);
    struct me_set_active_req *active =  
                       KE_MSG_ALLOC(ME_SET_ACTIVE_REQ, TASK_ME,
                                   TASK_APM, me_set_active_req);
    struct mm_chan_ctxt_add_req *ch_add = 
                       KE_MSG_ALLOC(MM_CHAN_CTXT_ADD_REQ,TASK_MM,
                                   TASK_APM, mm_chan_ctxt_add_req);

    dbg_con("%s\r\n", __func__);
    
    // Initialize the BSS configuration list
    apm_bss_config_init();

    ch_add->chan = param->chan;
    ch_add->vif_idx = param->vif_idx;
    apm_bss_config_push(ch_add);

    // Disable PS mode when an AP is started
    #if (NX_P2P_GO)
    if (!vif->p2p)
    #endif
    {
        ps->ps_disable = true;
        ps->vif_idx = vif->index;
        apm_bss_config_push(ps);
    }

    // BSSID
    bssid->bssid = vif->mac_addr;
    bssid->inst_nbr = param->vif_idx;
    apm_bss_config_push(bssid);

    // Basic rates
    brates->band = param->chan.band;
    brates->rates = me_legacy_rate_bitfield_build(&param->basic_rates, true);
    brates->inst_nbr = param->vif_idx;
    apm_bss_config_push(brates);

    // Beacon interval
    bint->beacon_int = param->bcn_int;
    bint->inst_nbr = param->vif_idx;
    apm_bss_config_push(bint);

    // Go back to ACTIVE after setting the BSS parameters
    active->active = true;
    active->vif_idx = param->vif_idx;
    active->auth_type = 0xff;
    apm_bss_config_push(active);

    // Send the first BSS configuration message
    apm_bss_config_send();

    // We are now waiting for the channel addition confirmation
    ke_state_set(TASK_APM, APM_BSS_PARAM_SETTING);
}

void apm_bss_config_send(void)
{
    struct ke_msg *msg = (struct ke_msg *)co_list_pop_front(&apm_env.bss_config);

    dbg_con("%s\r\n", __func__);

    // Sanity check - We shall have a message available
    ASSERT_ERR(msg != NULL);

    // Send the message
    ke_msg_send(ke_msg2param(msg));
}

void apm_bcn_set(void)
{
    struct apm_start_req const *param = apm_env.param;
    struct mm_bcn_change_req *bcn =  
                    KE_MSG_ALLOC_VAR(MM_BCN_CHANGE_REQ, TASK_MM,
                                   TASK_APM, mm_bcn_change_req, param->bcn_len);

    // Fill-in the parameters
    bcn->bcn_ptr = param->bcn_addr;
    bcn->bcn_len = param->bcn_len;
    bcn->tim_oft = param->tim_oft;
    bcn->tim_len = param->tim_len;
    bcn->inst_nbr = param->vif_idx;

    dbg_con("%s bcn_len=%d time_len=%d tim_oft=%d\r\n",
        __func__,param->bcn_len, param->tim_len,param->tim_oft);
        
    // Copy Beacon buffer
    memcpy(bcn->bcn_buf, param->bcn_buf,param->bcn_len);

    // Send the beacon information to the LMAC
    ke_msg_send(bcn);

    ke_state_set(TASK_APM, APM_BCN_SETTING);
}

void apm_stop(struct softmac_vif_info_tag *vif)
{
    struct me_set_ps_disable_req *ps = 
                     KE_MSG_ALLOC(ME_SET_PS_DISABLE_REQ, TASK_ME, TASK_APM,
                                  me_set_ps_disable_req);
    struct me_set_active_req *idle =  
                     KE_MSG_ALLOC(ME_SET_ACTIVE_REQ, TASK_ME,
                                 TASK_APM, me_set_active_req);
    struct mm_chan_ctxt_unlink_req *unlk;

    softmac_ap_stop_req(vif->index, VIF_TO_BCMC_IDX(vif->index));

    // Initialize the BSS configuration list
    apm_bss_config_init();

    // Re-allow PS mode in case it was disallowed
    ps->ps_disable = false;
    ps->vif_idx = vif->index;
    apm_bss_config_push(ps);

    // VIF state
    if (vif->active)
    {
        struct mm_set_vif_state_req *state = 
                            KE_MSG_ALLOC(MM_SET_VIF_STATE_REQ, TASK_MM,
                                        TASK_APM, mm_set_vif_state_req);

        state->active = false;
        state->inst_nbr = vif->index;
        apm_bss_config_push(state);
    }

    // Unlink the VIF from the channel context
    unlk = KE_MSG_ALLOC(MM_CHAN_CTXT_UNLINK_REQ, TASK_MM,
                        TASK_APM, mm_chan_ctxt_unlink_req);

    unlk->vif_index = vif->index;
    apm_bss_config_push(unlk);

    idle->active = false;
    idle->vif_idx = vif->index;
    idle->auth_type = 0xff;
    apm_bss_config_push(idle);

    // Send the first BSS configuration message
    apm_bss_config_send();

    ke_state_set(TASK_APM, APM_STOPPING);
}

#endif /* NX_BEACONING */

