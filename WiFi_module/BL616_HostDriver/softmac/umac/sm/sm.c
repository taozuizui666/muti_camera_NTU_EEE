#include "bl_ipc_compat.h"

#include "softmac.h"

#include "co_utils.h"
#include "co_endian.h"

#include "ke_timer.h"

#include "sm.h"
#include "sm_task.h"
#include "scanu.h"
#include "scanu_task.h"
#include "me.h"
#include "me_utils.h"
#include "mm_task.h"
#include "rxu_task.h"
#include "mac_frame.h"
#include "mac_ie.h"
#include "me_mgmtframe.h"
#include "vif_mgmt.h"
#include "sta_mgmt.h"
#include "txl_frame.h"

struct sm_env_tag sm_env;

/// Timeout, in us, for external authentication
#if NX_FULLY_HOSTED
#define SM_EXTERNAL_AUTH_TIMEOUT 10000000
#else
#define SM_EXTERNAL_AUTH_TIMEOUT 1200000
#endif

/// Timeout, in us, for FT element update
#define SM_FT_AUTH_TIMEOUT 100000

/**
 ****************************************************************************************
 * @brief Frame transmission confirmation handler
 * If the frame is not successfully transmitted it is pushed again until the timeout
 * expires.
 *
 * @param[in] env     Pointer to the frame descriptor
 * @param[in] status  Status of the transmission
 ****************************************************************************************
 */
static void sm_frame_tx_cfm_handler(void *env, uint32_t status)
{
    dbg_con("%s, task_sm state:%d, sm_env.tx_frame:0x%p, env:0x%p, status:0x%x, tx_frame_ongoing:%d, tx_frame_failed:%d, ts:%uus\r\n",
          __func__, ke_state_get(TASK_SM), sm_env.tx_frame,
          env, status, sm_env.tx_frame_ongoing, sm_env.tx_frame_failed,
          softmac_time_us());
    
    ASSERT_WARN(sm_env.tx_frame != NULL);

    txl_frame_dump_info(sm_env.tx_frame);
    
    // already connected, directly reset cnt and return.
    if(ke_state_get(TASK_SM) == SM_IDLE) {
        sm_env.tx_frame_ongoing = 0;
        sm_env.tx_frame_failed = 0;
        
        if (sm_env.tx_frame)
            softmac_free_skb(sm_env.tx_frame->skb);
            
        sm_env.tx_frame = NULL;
        
        return;
    }
    
    if ((status & FRAME_SUCCESSFUL_TX_BIT))
    {
        sm_env.tx_frame_failed = 0;
        
        softmac_free_skb(sm_env.tx_frame->skb);
        sm_env.tx_frame = NULL;
        
        if (sm_env.tx_frame_ongoing)
        {
            sm_env.tx_frame_ongoing--;
        }
        else if (ke_timer_active(SM_RSP_TIMEOUT_IND, TASK_SM) == false) 
        {
            dbg_con("%s, SM_RSP_TIMEOUT alreay\r\n", __func__);
            
            sm_connect_ind(MAC_ST_FAILURE);
        }
    }
    else if (sm_env.tx_frame_failed > 10 || (sm_env.tx_frame_ongoing == 0))
    {
        sm_env.tx_frame_failed = 0;
        
        if (sm_env.tx_frame_ongoing)
            sm_env.tx_frame_ongoing--;

        if (ke_timer_active(SM_RSP_TIMEOUT_IND, TASK_SM) == true) {
            ke_timer_clear(SM_RSP_TIMEOUT_IND, TASK_SM);
            
            dbg_con("%s, SM_RSP_TIMEOUT ongoing, clear it\r\n", __func__);
        }
        
        softmac_free_skb(sm_env.tx_frame->skb);
        sm_env.tx_frame = NULL;
        // The flag has been reset upon the response timeout expiry. Simply end the
        // connection procedure with a failure status.
        sm_connect_ind(MAC_ST_FAILURE);
    }
    else
    {
        sm_env.tx_frame_failed++;
        
        // Push again the frame for TX
        txl_frame_push((struct txl_frame_desc_tag *)env);
    }
}

/**
 ****************************************************************************************
 * @brief Initialize the BSS configuration list.
 ****************************************************************************************
 */
static void sm_bss_config_init(void)
{
    dbg_con("%s, cnt sm_env.bss_config queue:%d\r\n", __func__,
          co_list_cnt(&sm_env.bss_config));

    while(co_list_cnt(&sm_env.bss_config)) {
        struct ke_msg *msg = (struct ke_msg *)co_list_pop_front(&sm_env.bss_config);

        // Sanity check - We shall have a message available
        ASSERT_ERR(msg != NULL);

        if (msg) {
            dbg_f("%s, discard msg:%d %d 0x%x\r\n", __func__, 
                  msg->src_id, msg->dest_id, msg->id);

            ke_msg_free(msg);
        } else {
            break;
        }
    }

    // Initialize the BSS configuration list
    co_list_init(&sm_env.bss_config);
}

/**
 ****************************************************************************************
 * @brief Push a BSS configuration message to the list.
 *
 * @param[in] param  Pointer to the message parameters
 ****************************************************************************************
 */
static void sm_bss_config_push(void *param)
{
    struct ke_msg *msg = ke_param2msg(param);

    co_list_push_back(&sm_env.bss_config, &msg->hdr);
}

/**
 ****************************************************************************************
 * @brief Delete the different SW resources that were allocated for this connection.
 *
 * @param[in] vif     Pointer to the VIF
 ****************************************************************************************
 */
static void sm_delete_resources(struct softmac_vif_info_tag *vif)
{
    struct me_set_ps_disable_req *ps = 
                        KE_MSG_ALLOC(ME_SET_PS_DISABLE_REQ, TASK_ME, TASK_SM,
                                     me_set_ps_disable_req);
    struct me_set_active_req *idle =  
                        KE_MSG_ALLOC(ME_SET_ACTIVE_REQ, TASK_ME,
                                     TASK_SM, me_set_active_req);
    struct me_misc_req *misc_req = 
                       KE_MSG_ALLOC(ME_MISC_REQ, TASK_ME, TASK_SM, me_misc_req);
    struct mm_sta_del_req *sta_del = 
                 KE_MSG_ALLOC(MM_STA_DEL_REQ, TASK_MM, TASK_SM, mm_sta_del_req);

    dbg_con("%s\r\n", __func__);

    // Initialize the BSS configuration list
    sm_bss_config_init();

    misc_req->vif_idx = vif->index;
    sm_bss_config_push(misc_req);

    // Re-allow PS mode in case it was disallowed
    ps->ps_disable = false;
    ps->vif_idx = vif->index;
    sm_bss_config_push(ps);

    // Assoc
    if (vif->active)
    {
        struct mm_set_vif_state_req *assoc = 
                            KE_MSG_ALLOC(MM_SET_VIF_STATE_REQ, TASK_MM,
                                         TASK_SM, mm_set_vif_state_req);
        assoc->active = false;
        assoc->inst_nbr = vif->index;
        
        sm_bss_config_push(assoc);
    }

    sta_del->vif_idx = vif->index;
    sta_del->sta_idx = INVALID_STA_IDX;
    sm_bss_config_push(sta_del);

    idle->active = false;
    idle->vif_idx = vif->index;
    idle->auth_type = 0xff;
    sm_bss_config_push(idle);

    // Send the first BSS disconnection message
    sm_bss_config_send();
}

/**
 ****************************************************************************************
 * @brief Terminates the disconnection procedure
 ****************************************************************************************
 */
static void sm_disconnect_finish(void)
{
    struct softmac_vif_info_tag *vif = sm_env.disconnect.vif;
    uint16_t reason_code;

    struct sm_disconnect_ind *disc =
                          KE_MSG_ALLOC(SM_DISCONNECT_IND, TASK_API, TASK_SM,
                                       sm_disconnect_ind);

    // Locally generated disconnection is reported with reason code 0
    reason_code = sm_env.disconnect.host_initiated?0:sm_env.disconnect.reason_code;

    // Delete the resources that were allocated for this connection
    sm_delete_resources(vif);

    // Fill in the indication parameters
    disc->reason_code = reason_code;
    disc->vif_idx = vif->index;
    disc->reassoc = sm_env.reassoc;

    dbg_con("%s, sm_env.disconnect.ind:0x%p\r\n", __func__, 
          sm_env.disconnect.ind);
    
    sm_env.disconnect.ind = ke_param2msg(disc);
}

void sm_init(void)
{
    // Clear all SM environment
    memset(&sm_env, 0, sizeof(sm_env));

    // Set the SM task state as IDLE by default
    ke_state_set(TASK_SM, SM_IDLE);
}


/**
 ****************************************************************************************
 * @brief DEAUTH frame transmission confirmation handler
 * Once the transmission is confirmed we can continue the disconnection process.
 *
 * @param[in] env     Pointer to the VIF
 * @param[in] status  Status of the transmission
 ****************************************************************************************
 */
void sm_deauth_cfm(void *env, uint32_t status)
{
    dbg_con("%s, call sm_disconnect_finish, status:0x%x\r\n", __func__, status);
    
    sm_disconnect_finish();
}

/**
 ****************************************************************************************
 * @brief Send a deauthentication frame after reception of a @ref SM_DISCONNECT_REQ.
 ****************************************************************************************
 */
static void sm_deauth_send(void)
{
    struct txl_frame_desc_tag *frame;
    struct softmac_vif_info_tag *vif = sm_env.disconnect.vif;
    //struct softmac_sta_info_tag *sta = &sta_info_tab[vif->u.sta.ap_id];
    int txtype = 0;
    uint16_t reason_code = sm_env.disconnect.reason_code;

    frame = txl_frame_get(NX_TXFRAME_LEN + 
                          sizeof(struct txl_frame_snd_deauth_req));
    if (frame != NULL)
    {
        struct txl_frame_snd_deauth_req *req = NULL;
        
        // Get the buffer pointer
        req = (struct txl_frame_snd_deauth_req *)txl_frame_payload_get(frame);
        req->frame_type = MAC_FCTRL_DEAUTHENT;
        req->txtype = txtype;
        req->ac = AC_VO;
        req->vif_idx = vif->index;
        req->sta_idx = vif->u.sta.ap_id;
        req->reason_code = reason_code;

        txl_frame_set_len(frame, sizeof(struct txl_frame_snd_deauth_req));

        // Push the frame for TX
        txl_frame_push(frame);

        softmac_free_skb(frame->skb);

        dbg_f("deauth send\r\n");
    }
    else
    {
        dbg_f("%s, fail get frame, call sm_disconnect_finish\r\n", __func__);
        
        // No frame available, simply consider us as disconnected immediately
        sm_disconnect_finish();
    }
}

/**
 ****************************************************************************************
 * @brief Send a management frame requiring a response within a certain timeout (e.g.
 * Auth or AssocReq).
 *
 * The function calls the TX path and sets the timeout for the response. It also sets the
 * flags used for further retries before the timeout expiry. It finally sets the SM task
 * waiting state.
 *
 * @param[in] frame Pointer to the frame to transmit
 * @param[in] timeout Timeout value (in us)
 * @param[in] state State of the SM task while waiting for the response
 ****************************************************************************************
 */
static void sm_frame_with_rsp_send(struct txl_frame_desc_tag *frame, 
                                             uint32_t timeout, ke_state_t state)
{
    dbg_con("%s, sm_env.tx_frame not null, tx_frm_ongoing:%d, tx_frame_failed:%d\r\n", 
          __func__, sm_env.tx_frame_ongoing, sm_env.tx_frame_failed);
          
    if (sm_env.tx_frame) {
        txl_frame_dump_info(sm_env.tx_frame);

        dbg_con("%s, dump param frame\r\n", __func__);
        txl_frame_dump_info(frame);

        softmac_free_skb(sm_env.tx_frame->skb);
        sm_env.tx_frame = NULL;
        if (sm_env.tx_frame_ongoing > 0)
            sm_env.tx_frame_ongoing--;
    }
    
    sm_env.tx_frame = frame;
    frame->cfm.cfm_func = sm_frame_tx_cfm_handler;
    frame->cfm.env = frame;
    
    // Push the frame for TX
    txl_frame_push(frame);

    // Transmission of the frame is now ongoing
    sm_env.tx_frame_ongoing++;

    sm_env.tx_frame_failed = 0;

    // Start response timeout timer
    ke_timer_set(SM_RSP_TIMEOUT_IND, TASK_SM, timeout);

    // Set the task state for the waiting period
    ke_state_set(TASK_SM, state);
}

void sm_get_bss_params(struct mac_addr const **bssid,
                                struct mac_chan_def const **chan)
{
    struct sm_connect_req const *param = sm_env.connect_param;
    struct mac_scan_result *desired_ap_ptr;

    *bssid = NULL;
    *chan = NULL;

    // In order to launch the join procedure we need the BSSID and the channel,
    // otherwise we will need to get this information by doing a scan
    if (MAC_ADDR_GROUP(&param->bssid))
    {
        desired_ap_ptr = scanu_search_by_ssid(&param->ssid);
        if (desired_ap_ptr)
            *bssid = &desired_ap_ptr->bssid;
    }
    else
    {
        *bssid = &param->bssid;
        desired_ap_ptr = scanu_search_by_bssid(&param->bssid);
    }

    if (desired_ap_ptr)
        *chan = desired_ap_ptr->chan;
    else if (param->chan.freq != ((uint16_t)-1))
        *chan = &param->chan;
}

void sm_scan_bss(struct mac_addr const *bssid,
                       struct mac_chan_def const *chan)
{
    struct sm_connect_req const *param = sm_env.connect_param;
    struct scanu_start_req * req;

    dbg_con("%s\r\n", __func__);

    req = KE_MSG_ALLOC(SCANU_START_REQ, TASK_SCANU, TASK_SM, scanu_start_req);
    req->vif_idx = param->vif_idx;
    req->add_ies = 0;
    req->add_ie_len = 0;
    req->ssid[0] = param->ssid;
    req->ssid_cnt = 1;
    req->bssid = bssid?*bssid:mac_addr_bcst;
    
    if (chan)
    {
        req->chan[0] = *chan;
        req->chan_cnt = 1;
    }
    else
    {
        #if CFG_5G
        int i, j;

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
        int i;
        struct mac_chan_def *chan = me_env.chan.chan2G4;
        uint8_t chan_cnt = me_env.chan.chan2G4_cnt;

        req->chan_cnt = 0;
        for (i = 0; i < chan_cnt; i++)
            if (!(chan[i].flags & CHAN_DISABLED))
                req->chan[req->chan_cnt++] = chan[i];
        #endif
    }

    // Send the message
    ke_msg_send(req);

    // We are now waiting for the scan results
    ke_state_set(TASK_SM, SM_SCANNING);
}


void sm_join_bss(struct mac_addr const *bssid,
                      struct mac_chan_def const *chan, bool passive)
{
    struct sm_connect_req const *param = sm_env.connect_param;
    struct scanu_start_req *join = 
                        KE_MSG_ALLOC(SCANU_JOIN_REQ, TASK_SCANU, TASK_SM,
                                     scanu_start_req);

    dbg_con("%s, passive:%d\r\n", __func__, passive);

    join->chan[0] = *chan;
    join->chan_cnt = 1;
    join->ssid[0] = param->ssid;
    join->ssid_cnt = 1;
    join->add_ie_len = 0;
    join->add_ies = 0;
    join->vif_idx = param->vif_idx;
    join->bssid = *bssid;

    // Check if passive scan is required
    if (passive)
        join->chan[0].flags |= CHAN_NO_IR;

    // Set join scan mode
    sm_env.join_passive = passive;

    // Send the message
    ke_msg_send(join);

    // We are now waiting for the end of the joining procedure
    ke_state_set(TASK_SM, SM_JOINING);
}

void sm_set_bss_param(void)
{
    struct sm_connect_req const *param = NULL;
    struct softmac_vif_info_tag *vif = NULL;
    struct me_bss_info *bss = NULL;
    struct me_set_ps_disable_req *ps = NULL;
    struct mm_set_bssid_req *bssid = NULL;
    struct mm_set_basic_rates_req *brates = NULL;
    struct mm_set_beacon_int_req *bint = NULL;
    struct me_set_active_req *active =  NULL;

    dbg_con("%s\r\n", __func__);

    param = sm_env.connect_param;
    vif = &vif_info_tab[param->vif_idx];
    bss = &vif->bss_info;
    ps = KE_MSG_ALLOC(ME_SET_PS_DISABLE_REQ, TASK_ME, TASK_SM, 
                      me_set_ps_disable_req);
    bssid = KE_MSG_ALLOC(MM_SET_BSSID_REQ, TASK_MM, TASK_SM, 
                         mm_set_bssid_req);
    brates = KE_MSG_ALLOC(MM_SET_BASIC_RATES_REQ, TASK_MM, TASK_SM, 
                          mm_set_basic_rates_req);
    bint = KE_MSG_ALLOC(MM_SET_BEACON_INT_REQ, TASK_MM, TASK_SM, 
                        mm_set_beacon_int_req);
    active =  KE_MSG_ALLOC(ME_SET_ACTIVE_REQ, TASK_ME, TASK_SM, 
                           me_set_active_req);

    // Initialize the BSS configuration list
    sm_bss_config_init();

    // Disable PS mode prior to the association procedure
    ps->ps_disable = true;
    ps->vif_idx = param->vif_idx;
    sm_bss_config_push(ps);

    // BSSID
    bssid->bssid = bss->bssid;
    bssid->inst_nbr = param->vif_idx;
    sm_bss_config_push(bssid);

    vif->bssid = bss->bssid;

    // Basic rates
    brates->band = bss->chan.band;
    brates->rates = me_legacy_rate_bitfield_build(&bss->rate_set, true);
    brates->inst_nbr = param->vif_idx;
    sm_bss_config_push(brates);

    // Beacon interval
    bint->beacon_int = bss->beacon_period;
    bint->inst_nbr = param->vif_idx;
    sm_bss_config_push(bint);

    #if NX_HE
    if (BSS_CAPA(bss, HE))
    {
        struct mm_set_bss_color_req *color = 
                            KE_MSG_ALLOC(MM_SET_BSS_COLOR_REQ, TASK_MM,
                                         TASK_SM, mm_set_bss_color_req);
        color->bss_color = me_build_bss_color_reg(bss);
        sm_bss_config_push(color);
    }
    #endif

    // Go back to ACTIVE after setting the BSS parameters
    active->active = true;
    active->vif_idx = param->vif_idx;
    active->auth_type = param->auth_type;
    sm_bss_config_push(active);

    // Send the first BSS configuration message
    sm_bss_config_send();

    // We are now waiting for the channel addition confirmation
    ke_state_set(TASK_SM, SM_BSS_PARAM_SETTING);
}

void sm_bss_config_send(void)
{
    struct ke_msg *msg = (struct ke_msg *)co_list_pop_front(&sm_env.bss_config);

    // Sanity check - We shall have a message available
    if (msg == NULL) {
        dbg_con("%s %u, POP NULL\r\n", __func__, __LINE__);
        
        return;
    }

    // Send the message
    ke_msg_send(ke_msg2param(msg));
}

void sm_disconnect_start(struct softmac_vif_info_tag *vif, 
                                uint16_t reason_code, bool host_initiated)
{
    dbg_con("%s, disconn.reason=%d, host_init %d, vif:0x%p, host_init_org:%d, \
            sm:%d, reason:%d, host_init:%d, vif:0x%p\r\n", 
            __func__, sm_env.disconnect.reason_code, 
            sm_env.disconnect.host_initiated,
            sm_env.disconnect.vif, sm_env.disconnect.host_initiated_org, 
            ke_state_get(TASK_SM), reason_code, host_initiated, vif);

    if (ke_state_get(TASK_SM) == SM_DISCONNECTING) {
        dbg_f("skip %s\r\n", __func__);

        if (host_initiated) {
            ke_msg_send_basic(SM_DISCONNECT_CFM, TASK_API, TASK_SM);
            sm_env.disconnect.host_initiated_org = false;
        }

        return;
    }

    ke_state_set(TASK_SM, SM_DISCONNECTING);

    sm_env.disconnect.host_initiated = host_initiated;
    if (host_initiated)
    {
        sm_env.disconnect.host_initiated_org = host_initiated;
    }
    sm_env.disconnect.vif = vif;
    sm_env.disconnect.reason_code = reason_code;
    
    dbg_f("%s reason=%d from-host %d, vif:0x%p\r\n", 
          __func__, reason_code, host_initiated, vif);

    softmac_vif_update(vif->index, VIF_FIELD_STA_DISCON, NULL, 0);

    sm_disconnect_continue();
}

void sm_disconnect_continue(void)
{
    dbg_con("%s\r\n", __func__);

    if (sm_env.disconnect.host_initiated)
    {
        sm_deauth_send();
    }
    else
    {
        dbg_f("%s, call sm_disconnect_finish\r\n", __func__);
        sm_disconnect_finish();
    }
}

void sm_connect_ind(uint16_t status)
{
    struct sm_connect_ind *ind = sm_env.connect_ind;
    struct sm_connect_req const *con_par = sm_env.connect_param;
    struct softmac_vif_info_tag *vif = NULL;
    struct me_bss_info *bss = NULL;

    dbg_con("%s, ind:0x%p, con_par:0x%p\r\n", __func__, ind, con_par);

    if (sm_env.connect_param == NULL) {
        dbg_f("skip %s as connect_param null, sm_env.connect_ind:0x%p\r\n", 
              __func__, ind);

        if (ind) {
            ke_msg_free(ke_param2msg(sm_env.connect_ind));
        }
        
        return;
    }

    // To avoid delete resource twice during disconnecting.
    if (status != MAC_ST_SUCCESSFUL && ke_state_get(TASK_SM) == SM_DISCONNECTING) {
        dbg_f("skip %s as disconnecting, sm_env.connect_param:0x%p, sm_env.connect_ind:0x%p\r\n",
              __func__, con_par, ind);
              
        return;
    }

    sm_env.connect_ind = NULL;

    vif = &vif_info_tab[con_par->vif_idx];
    bss = &vif->bss_info;

    // Fill the message parameters
    ind->vif_idx = con_par->vif_idx;
    ind->status_code = status;

    dbg_con("{VIF-%d} Connection status=%d\r\n", con_par->vif_idx, status);

    if (status == MAC_ST_SUCCESSFUL)
    {
        ind->bssid = bss->bssid;
        ind->ap_idx = vif->u.sta.ap_id;
        ind->ch_idx = vif->chan_ctxt_idx;
        ind->chan = vif->bss_info.chan;
        //ind->aid = ??
        ind->qos = BSS_CAPA(bss, QOS);
        ind->acm = ind->qos?bss->edca_param.acm:0;
        // This connection results from a host request except for FT over DS
        ind->roamed = (sm_env.connect_param->flags & FT_OVER_DS);
        #if (NX_TDLS)
        memcpy(ind->ac_param, bss->edca_param.ac_param, sizeof(ind->ac_param));
        #endif
        
        dbg_f("%s, {VIF-%d} Connection status=%d,band=%d type=%d chan=%d(%d-%d)\r\n", 
              __func__, con_par->vif_idx, status, ind->chan.band, 
              ind->chan.type, ind->chan.prim20_freq,ind->chan.center1_freq, 
              ind->chan.center2_freq);

        // Send the message
        ke_msg_send(ind);

        // Set state to back to IDLE
        ke_state_set(TASK_SM, SM_IDLE);
    }
    else
    {
        ke_state_set(TASK_SM, SM_DISCONNECTING);
        
        dbg_con("%s, len:%zd %d %d\r\n", __func__, sizeof(struct sm_connect_ind), 
                 ind->assoc_req_ie_len, ind->assoc_rsp_ie_len);
                 
        sm_env.disconnect.vif = vif;
        sm_env.disconnect.ind = ke_param2msg(ind);

        // Delete the resources that were allocated for this connection
        sm_delete_resources(vif);
    }

    // Free the connection parameters
    ke_msg_free(ke_param2msg(sm_env.connect_param));
    sm_env.connect_param = NULL;
    sm_env.reassoc = false;
}

void sm_auth_send(uint16_t auth_seq, uint32_t *challenge)
{
    struct sm_connect_req const *con_par = sm_env.connect_param;
    struct softmac_vif_info_tag *vif = &vif_info_tab[con_par->vif_idx];
    struct txl_frame_desc_tag *frame;
    int txtype = 0;

    dbg_con("%s\r\n", __func__);

    frame = txl_frame_get(NX_TXFRAME_LEN + sizeof(struct txl_frame_snd_auth_req));
    if (frame != NULL)
    {
        struct txl_frame_snd_auth_req *req = NULL;
        
        // Get the buffer pointer
        req = (struct txl_frame_snd_auth_req *)txl_frame_payload_get(frame);
        req->frame_type = MAC_FCTRL_AUTHENT;
        req->txtype = txtype;
        req->ac = AC_VO;
        req->vif_idx = con_par->vif_idx;
        req->sta_idx = vif->u.sta.ap_id;
        req->auth_seq = auth_seq;
        req->auth_type = (uint16_t)con_par->auth_type;
        req->ie_len = con_par->ie_len;
        memcpy(req->ie_buf, con_par->ie_buf, con_par->ie_len);
        if (challenge) {
            memcpy(req->challenge, challenge, MAC_AUTH_CHALLENGE_LEN);
            req->challenge_len = MAC_AUTH_CHALLENGE_LEN;
        } else {
            req->challenge_len = 0;
        }
        
        dbg_con("%s, ie_len:%d, ts:%uus\r\n", __func__, con_par->ie_len, softmac_time_us());
        
        txl_frame_set_len(frame, sizeof(struct txl_frame_snd_auth_req));
        
        sm_frame_with_rsp_send(frame, DEFAULT_AUTHRSP_TIMEOUT, SM_AUTHENTICATING);
        
        dbg_f("auth send\r\n");
    }
    else
    {
        dbg_f("%s, fail get frame\r\n", __func__);
    }
}

void sm_assoc_req_send(void)
{
    //The local variables
    struct sm_connect_req const *con_par = sm_env.connect_param;
    struct softmac_vif_info_tag *vif = &vif_info_tab[con_par->vif_idx];
    struct me_bss_info *bss = &vif->bss_info;
    struct mac_addr *ap_old_ptr = NULL;
    struct txl_frame_desc_tag *frame;
    struct mac_hdr *buf;
    struct softmac_sta_info_tag *sta = &sta_info_tab[vif->u.sta.ap_id];
    uint32_t length;
    int txtype = 0;

    // Allocate a frame descriptor from the TX path
    frame = txl_frame_get(NX_TXFRAME_LEN + sizeof(struct txl_frame_snd_req));
    if (frame != NULL)
    {
        struct sm_connect_ind *ind = sm_env.connect_ind;
        PTR2UINT ie_addr;
        uint16_t ie_len;
        struct txl_frame_snd_req * req = NULL;

        // Get the buffer pointer
        req = (struct txl_frame_snd_req *)txl_frame_payload_get(frame);
        req->frame_type = MAC_FCTRL_ASSOCREQ;
        req->txtype = txtype;
        req->ac = AC_VO;
        req->vif_idx = con_par->vif_idx;
        req->sta_idx = vif->u.sta.ap_id;
        
        buf = (struct mac_hdr *)((uint8_t *)req + sizeof(struct txl_frame_snd_req));
        
        // Fill-in the frame
        if (sm_env.reassoc)
        {
            buf->fctl = MAC_FCTRL_REASSOCREQ;
            ap_old_ptr = &sm_env.prev_bssid;
        }
        else
        {
            buf->fctl = MAC_FCTRL_ASSOCREQ;
        }
        buf->durid = 0;
        buf->addr1 = sta->mac_addr;
        buf->addr2 = vif->mac_addr;
        buf->addr3 = sta->mac_addr;

        // Build the payload
        length = me_build_associate_req(CPU2HW(buf) + MAC_SHORT_MAC_HDR_LEN, 
                                        bss, ap_old_ptr, vif->index, &ie_addr, 
                                        &ie_len, con_par) + 
                                        MAC_SHORT_MAC_HDR_LEN;

        // Copy the AssocReq IEs into the indication message
        if (ie_len <= MACIF_MAX_PARAM_LEN(sm_connect_ind)-SM_ASSOC_IE_LEN)
        {
            co_copy8p(CPU2HW(ind->assoc_ie_buf), ie_addr, ie_len);
            ind->assoc_req_ie_len = ie_len;
        }
        else
        {
            ASSERT_WARN(0);
            ind->assoc_req_ie_len = 0;
        }

        //bl_dump((uint8_t *)buf, length);

        dbg("%s, length:%d, vif_idx:%d, sta_id:%d, data:%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\r\n",
              __func__, length, con_par->vif_idx, vif->u.sta.ap_id,
              ((uint8_t *)buf)[0], ((uint8_t *)buf)[1], ((uint8_t *)buf)[2], 
              ((uint8_t *)buf)[3], ((uint8_t *)buf)[4], ((uint8_t *)buf)[5], 
              ((uint8_t *)buf)[6], ((uint8_t *)buf)[7], ((uint8_t *)buf)[8], 
              ((uint8_t *)buf)[9], ((uint8_t *)buf)[10], ((uint8_t *)buf)[11], 
              ((uint8_t *)buf)[12], ((uint8_t *)buf)[13], ((uint8_t *)buf)[14], 
              ((uint8_t *)buf)[15]);

        txl_frame_set_len(frame, length + sizeof(struct txl_frame_snd_req));

        // Push the frame for TX and wait for the response
        sm_frame_with_rsp_send(frame, DEFAULT_ASSOCRSP_TIMEOUT, SM_ASSOCIATING);
        
        dbg_f("assoc send\r\n");
    }
    else
    {
        dbg_f("assoc send fail\r\n");
        
        sm_connect_ind(MAC_ST_FAILURE);
    }
}

void sm_assoc_done(uint16_t aid)
{
    //  Send Association done IND to LMAC
    struct mm_set_vif_state_req *req;
    struct sm_connect_req const *con_par = sm_env.connect_param;

    dbg_con("%s\r\n", __func__);

    // Get a pointer to the kernel message
    req = KE_MSG_ALLOC(MM_SET_VIF_STATE_REQ, TASK_MM, TASK_SM,
                       mm_set_vif_state_req);

    // Fill the message parameters
    req->aid = aid;
    req->active = true;
    req->inst_nbr = con_par->vif_idx;
    req->ctrl_port_ethertype = sm_env.connect_param->ctrl_port_ethertype;

    // Send the message to the task
    ke_msg_send(req);

    // Set state to back to IDLE
    ke_state_set(TASK_SM, SM_ACTIVATING);
}

void sm_auth_handler(struct rxu_mgt_ind const *param)
{
    uint16_t status, auth_type, auth_seq;
    PTR2UINT payload = CPU2HW(param->payload);

    // Stop the authentication timeout timer
    ke_timer_clear(SM_RSP_TIMEOUT_IND, TASK_SM);

    // Check authentication response (S,F)
    status = co_read16p((void *)(payload + MAC_AUTH_STATUS_OFT));

    dbg_f("%s, status:0x%x\r\n", __func__, status);

    // Check on the value of the status
    switch (status)
    {
        case MAC_ST_SUCCESSFUL:
            auth_type = co_read16p((void *)(payload + MAC_AUTH_ALGONBR_OFT));

            if (auth_type == MAC_AUTH_ALGO_OPEN)
            {
                // Send AIR_ASSOC_REQ to the AIR
                sm_assoc_req_send();
            }
            else if(auth_type == MAC_AUTH_ALGO_SHARED)
            {
                auth_seq = co_read16p((void *)(payload + MAC_AUTH_SEQNBR_OFT));

                if (auth_seq == MAC_AUTH_FOURTH_SEQ)
                {
                    // Authentication done Send AIR_ASSOC_REQ to the AIR
                    sm_assoc_req_send();
                }
                else if (auth_seq == MAC_AUTH_SECOND_SEQ)
                {
                    // Need to send challenge encrypted
                    sm_auth_send(MAC_AUTH_THIRD_SEQ,
                                 HW2CPU(payload + MAC_AUTH_CHALLENGE_OFT + 
                                 MAC_CHALLENGE_TEXT_OFT));
                }
                else
                {
                    ASSERT_WARN(0);
                    sm_connect_ind(MAC_ST_FAILURE);
                }
            }
            else if (auth_type == MAC_AUTH_ALGO_FT)
            {
                sm_ft_auth_over_air_start(param->inst_nbr, 
                                          payload + MAC_AUTH_FT_IE_OFT,
                                          param->length - MAC_AUTH_FT_IE_OFT);
            }
            break;

        default:
        {
            // No station available, terminate the connection procedure
            sm_connect_ind(status);
            break;
        }
    }
}

void sm_assoc_rsp_handler(struct rxu_mgt_ind const *param)
{
    uint16_t status;
    PTR2UINT payload = CPU2HW(param->payload);
    struct sm_connect_ind *ind = sm_env.connect_ind;

    // stop the assoc timeout timer
    ke_timer_clear(SM_RSP_TIMEOUT_IND, TASK_SM);

    // get association response status code
    status = co_read16p((void *)(payload + MAC_ASSO_RSP_STATUS_OFT));

    dbg_f("%s, status:%u\r\n", __func__, status);

    // Copy the AssocRsp IEs into the indication message
    if (param->length >= MAC_ASSO_RSP_RATES_OFT)
    {
         PTR2UINT ie_addr = payload + MAC_ASSO_RSP_RATES_OFT;
         uint16_t ie_len = param->length - MAC_ASSO_RSP_RATES_OFT;
         
         co_copy8p(CPU2HW(ind->assoc_ie_buf) + ind->assoc_req_ie_len, 
                   ie_addr, ie_len);
         ind->assoc_rsp_ie_len = ie_len;
    }
    else
    {
        ind->assoc_rsp_ie_len = 0;
    }

    dbg_con("%s, status:%d, paramlen=%d, req_ie_len=%d, rsp_ie_len=%d\r\n", 
          __func__, 
          status, param->length, ind->assoc_req_ie_len, ind->assoc_rsp_ie_len);

    switch (status)
    {
        case MAC_ST_SUCCESSFUL:
            // send MM_SET_ASSOCIATED_REQ to the LMAC
            sm_assoc_done(co_wtohs(co_read16p((void *)(payload + MAC_ASSO_RSP_AID_OFT))) & MAC_AID_MSK);
            break;

        default:
            // status is not successful
            sm_connect_ind(status);
            break;
    }
}

int sm_deauth_handler(struct rxu_mgt_ind const *param)
{
    struct softmac_vif_info_tag *vif = &vif_info_tab[param->inst_nbr];

    dbg_f("%s, sm state:%d, vif active:%d, sm_env.connect_ind:0x%p\r\n", 
          __func__, ke_state_get(TASK_SM), vif->active, sm_env.connect_ind);

    // Check if we are in a disconnection procedure
    if (ke_state_get(TASK_SM) == SM_DISCONNECTING)
        // We are already in a disconnection procedure, so save the message
        return KE_MSG_CONSUMED;

    // Check if we are in a connection procedure
    if (ke_state_get(TASK_SM) != SM_IDLE)
    {
        struct sm_connect_req const *con_par = sm_env.connect_param;

        // Check on which VIF we are currently in a connection procedure
        if (con_par->vif_idx != param->inst_nbr)
            // We are in a connection procedure on another VIF, so save the message
            return KE_MSG_SAVED;

        // status is not successful
        sm_connect_ind(MAC_ST_FAILURE);
    }
    else if (vif->active)
    {
        PTR2UINT payload = CPU2HW(param->payload);
        uint16_t reason = co_read16p((void *)(payload + MAC_DEAUTH_REASON_OFT));

        // Proceed to the disconnection
        sm_disconnect_start(vif, reason, false);
    }

    return KE_MSG_CONSUMED;
}

#if NX_MFP
void sm_sa_query_handler(struct rxu_mgt_ind const *param)
{
    struct softmac_vif_info_tag *vif = &vif_info_tab[param->inst_nbr];
    struct softmac_sta_info_tag *ap;
    struct txl_frame_desc_tag *frame;
    PTR2UINT payload = CPU2HW(param->payload);
    uint8_t  action;
    uint16_t transaction_id;
    int txtype = 0;

    // only process sa_query on STA interfaces
    if (!vif->active || vif->type != VIF_STA)
        return;

    // Ensure we recieved the frame form the AP
    if ((param->sta_idx == INVALID_STA_IDX) || (vif->u.sta.ap_id != param->sta_idx))
        return;

    // Check that connection with AP is finished
    ap = &sta_info_tab[param->sta_idx];
    if (vif->flags & CONTROL_PORT_HOST)
    //if (ap->ctrl_port_state != PORT_OPEN)
        return;

    // Chose the right rate according to the band
    #if CFG_5G
    txtype = (param->band == PHY_BAND_2G4) ? TX_DEFAULT_24G : TX_DEFAULT_5G;
    #else
    txtype = TX_DEFAULT_24G;
    #endif

    action = co_read8p(payload + MAC_SA_QUERY_ACTION_OFT);
    transaction_id = co_read16p((void *)(payload + MAC_SA_QUERY_TR_ID_OFT));

    if (action != MAC_SA_QUERY_REQUEST)
        return;

    // Allocate a frame descriptor from the TX path
    frame = txl_frame_get(NX_TXFRAME_LEN + sizeof(struct txl_frame_act_sa_query_req));
    if (frame != NULL)
    {
        struct txl_frame_act_sa_query_req * req = NULL;

        // Get the buffer pointer
        req = (struct txl_frame_act_sa_query_req *)txl_frame_payload_get(frame);
        req->frame_type = MAC_FCTRL_ACTION;
        req->txtype = txtype;
        req->ac = AC_VO;
        req->vif_idx = vif->index;
        req->sta_idx = vif->u.sta.ap_id;
        req->transaction_id = transaction_id;
        req->act_cat = MAC_SA_QUERY_ACTION_CATEGORY;
        
        txl_frame_set_len(frame, sizeof(struct txl_frame_snd_req));

        frame->cfm.cfm_func = NULL;
        frame->cfm.env = NULL;
        
        // Push the frame for TX
        txl_frame_push(frame);

        softmac_free_skb(frame->skb);
        
        dbg_f("sa_query rsp send\r\n");
    }
}
#endif // NX_MFP

void sm_external_auth_start(uint32_t akm)
{
    struct sm_connect_req const *con_par = sm_env.connect_param;
    struct softmac_vif_info_tag *vif = &vif_info_tab[con_par->vif_idx];
    struct me_bss_info *bss = &vif->bss_info;
    struct sm_external_auth_required_ind *ind;

    ASSERT_ERR(BSS_CAPA(bss, VALID));

    ind = KE_MSG_ALLOC(SM_EXTERNAL_AUTH_REQUIRED_IND, TASK_API, TASK_SM,
                       sm_external_auth_required_ind);

    ind->vif_idx = con_par->vif_idx;
    ind->ssid = bss->ssid;
    ind->bssid = bss->bssid;
    ind->akm = co_htonl(akm);

    // Wait until host complete the external authentication procedure
    ke_state_set(TASK_SM, SM_EXTERNAL_AUTHENTICATING);

    // Start timeout timer
    ke_timer_set(SM_RSP_TIMEOUT_IND, TASK_SM, SM_EXTERNAL_AUTH_TIMEOUT);

    ke_msg_send(ind);
}

void sm_external_auth_end(uint16_t status)
{
    ke_timer_clear(SM_RSP_TIMEOUT_IND, TASK_SM);

    if (status != MAC_ST_SUCCESSFUL)
    {
        sm_connect_ind(status);
        return;
    }

    // Start association if external authentication succeed
    sm_assoc_req_send();
}

bool sm_external_auth_in_progress(void)
{
    return (ke_state_get(TASK_SM) == SM_EXTERNAL_AUTHENTICATING);
}

void sm_ft_auth_over_air_start(uint8_t vif_idx, PTR2UINT ft_ie,
                                      uint16_t ft_ie_len)
{
    struct sm_ft_auth_ind *ind = 
                       KE_MSG_ALLOC_VAR(SM_FT_AUTH_IND, TASK_API, TASK_SM, 
                                        sm_ft_auth_ind, ft_ie_len);

    ind->vif_idx = vif_idx;
    ind->ft_ie_len = ft_ie_len;
    ASSERT_ERR(ind->ft_ie_len < MACIF_MAX_PARAM_LEN(sm_ft_auth_ind));
    co_copy8p(CPU2HW(ind->ft_ie_buf), ft_ie, ind->ft_ie_len);

    // Wait until host update the association elements
    ke_state_set(TASK_SM, SM_FT_OVER_AIR);

    // Start timeout timer
    ke_timer_set(SM_RSP_TIMEOUT_IND, TASK_SM, SM_FT_AUTH_TIMEOUT);

    ke_msg_send(ind);
}

void sm_ft_auth_over_air_end(struct sm_connect_req *param)
{
    ke_timer_clear(SM_RSP_TIMEOUT_IND, TASK_SM);

    memcpy(param, sm_env.connect_param, offsetof(struct sm_connect_req, ie_len));

    // Free the old connection parameters
    ke_msg_free(ke_param2msg(sm_env.connect_param));

    // Save the parameters
    sm_env.connect_param = param;
    sm_assoc_req_send();
}

int sm_get_rsnie_pmkid_count(PTR2UINT ies, uint16_t ies_len)
{
    PTR2UINT rsn_ie, rsn_ie_ptr;
    uint16_t cipher_len, akm_len = 0;
    uint8_t rsn_ie_len;
    rsn_ie = mac_ie_rsn_find(ies, ies_len, &rsn_ie_len);

    if (!rsn_ie || (rsn_ie_len < 30))
        return 0;

    rsn_ie_ptr = rsn_ie + MAC_RSNIE_PAIRWISE_CIPHER_SUITE_CNT_OFT;
    rsn_ie_len -= MAC_RSNIE_PAIRWISE_CIPHER_SUITE_CNT_OFT;

    cipher_len =
      co_wtohs(co_read16p((void *)rsn_ie_ptr)) * MAC_RSNIE_PAIRWISE_CIPHER_SIZE;
    rsn_ie_ptr += 2 + cipher_len;
    rsn_ie_len -= 2 + cipher_len;

    if (rsn_ie_len < 22)
        return 0;

    akm_len = 
       co_wtohs(co_read16p((void *)rsn_ie_ptr)) * MAC_RSNIE_KEY_MANAGEMENT_SIZE;
    rsn_ie_ptr += 2 + akm_len;
    rsn_ie_len -= 2 + akm_len;

    if (rsn_ie_len < 20)
        return 0;

    rsn_ie_ptr += 2;
    
    return co_wtohs(co_read16p((void *)rsn_ie_ptr));
}


