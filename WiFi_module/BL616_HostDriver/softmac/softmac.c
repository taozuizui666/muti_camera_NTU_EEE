#include "bl_defs.h"
#include "bl_ipc_host.h"
#include "bl_msg_tx.h"
#include "bl_irqs.h"
#include "bl_lmac_msg.h"

#include "rwnx_config.h"
#include "softmac.h"
#include "ke_event.h"
#include "ke_timer.h"
#include "ke_msg.h"
#include "ke_mem.h"

#include "scanu.h"
#include "scanu_task.h"
#include "rxu_task.h"
#include "me.h"
#include "vif_mgmt.h"
#include "sta_mgmt.h"
#include "sm.h"
#if NX_RM
#include "rm.h"
#endif

struct softmac_sta_info_tag sta_info_tab[STA_MAX] = {0};
struct softmac_vif_info_tag vif_info_tab[NX_VIRT_DEV_MAX] = {0};

int softmac_init(struct bl_hw *bl_hw)
{
    ke_init(bl_hw);

    scanu_init();
    sm_init();

    #if NX_RM
    rm_init();
    #endif
    
    return 0;
}

void softmac_deinit(void)
{
    ke_timer_reset();
    ke_mem_deinit();
}

void softmac_schedule(void)
{
    ke_evt_schedule();
}

void softmac_timer_cb(struct timer_list *t)
{
    struct bl_hw * bl_hw = from_timer(bl_hw, t, ke_timer);
    
    bl_hw->ke_timer_active = false;
    ke_evt_set(KE_EVT_KE_TIMER_BIT);
}

void softmac_timer_set(uint64_t time_jiffies)
{
    if (!ke_env.bl_hw->ke_timer_active) {
        /* Create timer */
        ke_env.bl_hw->ke_timer.expires = (unsigned long)time_jiffies;
        add_timer(&ke_env.bl_hw->ke_timer);
        ke_env.bl_hw->ke_timer_active = true;
    } else {
        /* Rerun the timer */
        mod_timer(&ke_env.bl_hw->ke_timer, (unsigned long)time_jiffies);
    }
}

uint64_t softmac_time(void)
{
    dbg("%s, jiffies:%llu\r\n", __func__, get_jiffies_64());
          
    return get_jiffies_64();
}

uint32_t softmac_time_us(void)
{
    return jiffies_to_usecs(jiffies);
}

bool softmac_time_past(uint64_t time_jiffies)
{
    dbg("%s, time jiffeies:%llu, jiffies:%llu\r\n",
          __func__, time_jiffies, get_jiffies_64());
    return (time_before64(time_jiffies, get_jiffies_64()));
}

void softmac_vif_add(struct mm_add_if_req *req, uint8_t vif_index)
{
    dbg("%s, %d %d %02x%02x%02x%02x%02x%02x, vif_idx:%d, vif_type:%d, ap_id:%d\n", __func__, 
          req->type, vif_index, ((uint8_t *)req->addr.array)[0],
          ((uint8_t *)req->addr.array)[1],
          ((uint8_t *)req->addr.array)[2],
          ((uint8_t *)req->addr.array)[3],
          ((uint8_t *)req->addr.array)[4],
          ((uint8_t *)req->addr.array)[5],
          vif_index, req->type,
          vif_info_tab[vif_index].u.sta.ap_id);
    
    vif_info_tab[vif_index].type = req->type;
    vif_info_tab[vif_index].mac_addr = req->addr;
    vif_info_tab[vif_index].index = vif_index;
    vif_info_tab[vif_index].active = false;
    vif_info_tab[vif_index].chan_ctxt_idx = CHAN_CTXT_UNUSED;
    vif_info_tab[vif_index].p2p = req->p2p;
    
    if (req->type == VIF_STA)
        vif_info_tab[vif_index].u.sta.ap_id = INVALID_STA_IDX;
}

void softmac_vif_remove(uint8_t vif_index)
{
    dbg("%s, %d\n", __func__, vif_index);
    
    memset(&vif_info_tab[vif_index], 0, sizeof(vif_info_tab[vif_index]));
    vif_info_tab[vif_index].chan_ctxt_idx = CHAN_CTXT_UNUSED;
    vif_info_tab[vif_index].u.sta.ap_id = INVALID_STA_IDX;
}

void softmac_vif_update_local(uint8_t vif_index, uint8_t field_type, 
                                      uint8_t *field_value, uint32_t field_len)
{
    if (field_type == VIF_FIELD_ACTIVE)
    {
        vif_info_tab[vif_index].active = *field_value;
    }
    else if (field_type == VIF_FIELD_BSS_CHAN)
    {
        vif_info_tab[vif_index].bss_info.chan = 
                             *(struct mac_chan_op *)field_value;
    }
}

void softmac_vif_update(uint8_t vif_index, uint8_t field_type, 
                               uint8_t *field_value, uint32_t field_len)
{
    struct update_fw_vif_req * update_vif;
    
    dbg_con("%s, %d, field_type:%d, value:0x%p, len:0x%x\n", 
         __func__, vif_index, field_type, field_value, field_len);
    
    update_vif = bl_msg_ke_zalloc(DBG_UPDATE_FW_VAR, TASK_DBG, TASK_API,
                                  field_len + sizeof(struct update_fw_vif_req));
    if (!update_vif) {
        dbg_f("%s, no mem\n", __func__);
        
        return;
    }

    update_vif->update_var_type = UPDATE_VAR_VIF;
    update_vif->update_field_type = field_type;
    update_vif->update_len = field_len;
    update_vif->vif_index = vif_index;

    if (field_value) {
        memcpy(update_vif->update_field_val, field_value, field_len);
    }

    bl_send_ke_msg(ke_env.bl_hw, update_vif, DBG_UPDATE_FW_VAR);
}

void softmac_txl_frame_req(uint8_t *data, uint32_t len)
{
    struct fw_act_req *req = NULL;

    dbg_con("%s, len + sizeof(struct fw_act_req):%d\r\n", 
          __func__, len + sizeof(struct fw_act_req));

    req = bl_msg_ke_zalloc(DBG_FW_ACT_REQ, TASK_DBG, TASK_SM,
                           len + sizeof(struct fw_act_req));

    ASSERT_WARN(req != NULL);

    dbg_con("%s, req:0x%p, len:%d\r\n", __func__, req, len);
    
    req->act_type = FW_ACT_SEND_TXL_FRAME;
    req->len = len;
    memcpy(req->data, data, len);

    bl_send_ke_msg(ke_env.bl_hw, req, DBG_FW_ACT_REQ);
}

void softmac_ap_probe_req(uint8_t vif_idx, uint8_t sta_idx)
{
    struct fw_act_req *req = NULL;

    req = bl_msg_ke_zalloc(DBG_FW_ACT_REQ, TASK_DBG, TASK_APM,
                           2 + sizeof(struct fw_act_req));

    ASSERT_WARN(req != NULL);
    
    dbg_con("%s, req:0x%p\r\n", __func__, req);
    
    req->act_type = FW_ACT_APM_PROBE_CLIENT_REQ;
    req->len = 2;
    *((uint8_t *)req->data) = vif_idx;    
    *((uint8_t *)req->data + 1) = sta_idx;

    bl_send_ke_msg(ke_env.bl_hw, req, DBG_FW_ACT_REQ);
}

void softmac_ap_stop_req(uint8_t vif_idx, uint8_t sta_idx)
{
    struct fw_act_req *req = NULL;

    req = bl_msg_ke_zalloc(DBG_FW_ACT_REQ, TASK_DBG, TASK_APM,
                           2 + sizeof(struct fw_act_req));

    ASSERT_WARN(req != NULL);
    
    dbg_con("%s, req:0x%p\r\n", __func__, req);
    
    req->act_type = FW_ACT_APM_STOP_REQ;
    req->len = 2;
    *((uint8_t *)req->data) = vif_idx;    
    *((uint8_t *)req->data + 1) = sta_idx;

    bl_send_ke_msg(ke_env.bl_hw, req, DBG_FW_ACT_REQ);
}

void softmac_ap_start_req(uint8_t vif_idx, uint8_t sta_idx,
                                 uint16_t ctrl_port_ethertype, 
                                 struct mac_rateset *basic_rates,
                                 uint32_t flags)
{
    struct fw_act_req *req = NULL;
    struct ap_start_req * ap_req = NULL;
    
    req = bl_msg_ke_zalloc(DBG_FW_ACT_REQ, TASK_DBG, TASK_APM,
                           sizeof(struct fw_act_req) + 
                           sizeof(struct ap_start_req));

    ASSERT_WARN(req != NULL);
    
    dbg_con("%s, req:0x%p\r\n", __func__, req);
    
    req->act_type = FW_ACT_APM_START_REQ;
    req->len = sizeof(struct ap_start_req);
    
    ap_req = (struct ap_start_req *)req->data;
    ap_req->vif_idx = vif_idx;    
    ap_req->sta_idx = sta_idx;
    memcpy(&ap_req->basic_rates, basic_rates, sizeof(struct mac_rateset));
    ap_req->flags = flags;
    ap_req->ctrl_port_ethertype = ctrl_port_ethertype;

    bl_send_ke_msg(ke_env.bl_hw, req, DBG_FW_ACT_REQ);
}

void softmac_ps_flags_req(uint8_t vif_idx, uint32_t flags, uint8_t is_clear)
{
    struct fw_act_req *req = NULL;
    struct ps_flags_req *ps_req = NULL;
    
    req = bl_msg_ke_zalloc(DBG_FW_ACT_REQ, TASK_DBG, TASK_APM,
                           sizeof(struct ps_flags_req) + 
                           sizeof(struct fw_act_req));

    ASSERT_WARN(req != NULL);
    
    dbg_con("%s, req:0x%p\r\n", __func__, req);
    
    req->act_type = FW_ACT_PS_FLAGS_REQ;
    req->len = sizeof(struct ps_flags_req);

    ps_req = (struct ps_flags_req *)req->data;
    ps_req->vif_idx = vif_idx;
    ps_req->is_clear = is_clear;
    ps_req->flags = flags;

    bl_send_ke_msg(ke_env.bl_hw, req, DBG_FW_ACT_REQ);
}

int softmac_handle_kmsg_from_drv(struct bl_hw *bl_hw,
                                              struct lmac_msg *kmsg_host)
{
    uint8_t *fwd_msg = NULL;

    dbg_con("%s, msg id:0x%x, src_id:0x%x, dest_id:0x%x\n", __func__, 
            kmsg_host->id, kmsg_host->src_id, kmsg_host->dest_id);

    fwd_msg = ke_msg_alloc(kmsg_host->id, kmsg_host->dest_id, kmsg_host->src_id,
                           kmsg_host->param_len);

    memcpy(fwd_msg, kmsg_host->param, kmsg_host->param_len);

    ke_msg_check();
    
    ke_msg_send(fwd_msg);

    return 0;
}

void softmac_handle_sm_scanu_frame_from_fw(struct bl_hw *bl_hw, 
                          struct sk_buff *skb, struct rx_mgmt_info *rx_mgmt_inf,
                          int msdu_offset, uint8_t inst_nbr, uint8_t sta_idx, 
                          uint16_t center_freq, uint8_t band)
{
    struct rxu_mgt_ind *rx = NULL;
    
    dbg_con("%s %u, skb len:%u, %u\r\n",
            __func__, __LINE__, skb->len, msdu_offset);

    rx = KE_MSG_ALLOC_VAR(RXU_MGT_IND, rx_mgmt_inf->dest_id, TASK_RXU, 
                          rxu_mgt_ind, rx_mgmt_inf->length);
    rx->tsf = rx_mgmt_inf->tsf;
    rx->length = rx_mgmt_inf->length;
    rx->inst_nbr = inst_nbr;
    rx->sta_idx = sta_idx;
    rx->framectrl = rx_mgmt_inf->framectrl;
    rx->rssi = rx_mgmt_inf->rssi;
    rx->center_freq = center_freq;
    rx->band = band;
    rx->antenna_set = rx_mgmt_inf->antenna_set;
    memcpy(&rx->rx_leg_inf, &rx_mgmt_inf->rx_leg_inf, sizeof(rx->rx_leg_inf));
    
    BL_DBG("rx len:%u, hdr len:%u, inst_nbr:%u, sta_idx:%u, framectrl:0x%x, rssi:%d, center_freq:%u, band:%u, atn:%u, dest_id:0x%x, rx:0x%p 0x%p\r\n",
           rx_mgmt_inf->length, rx_mgmt_inf->machdr_length, 
           inst_nbr, sta_idx,
           rx_mgmt_inf->framectrl, rx_mgmt_inf->rssi, 
           center_freq, band,
           rx_mgmt_inf->antenna_set, rx_mgmt_inf->dest_id,
           rx, rx->payload);
    
    skb_pull(skb, msdu_offset);
    
    memcpy(rx->payload, skb->data+rx_mgmt_inf->machdr_length, rx_mgmt_inf->length);
    
    ke_msg_check();
    
    ke_msg_send(rx);
}

//Don't use cmd field, it may be NULL
int softmac_handle_kmsg_from_fw(struct bl_hw *bl_hw, struct bl_cmd *cmd,
                                             struct ipc_e2a_msg *msg)
{
    uint8_t *fwd_msg;

    fwd_msg = ke_msg_alloc(msg->id, msg->dummy_dest_id, 
                           msg->dummy_src_id, msg->param_len);

    memcpy(fwd_msg, msg->param, msg->param_len);

    dbg_con("%s, msg:0x%p, id:0x%x, src_id:%d, dest_id:%d, param_len:%u, fwd_msg:0x%p\n", 
            __func__, msg, msg->id, msg->dummy_src_id, msg->dummy_dest_id,
            msg->param_len, fwd_msg);

    ke_msg_check();
    
    ke_msg_send(fwd_msg);

    return 0;
}

int softmac_fwd_kmsg_to_fw(struct bl_hw *bl_hw, struct ke_msg *msg)
{
    void *softmac_msg = bl_msg_ke_zalloc(msg->id, msg->dest_id, msg->src_id,
                                         msg->param_len);

    dbg_con("%s, id:0x%x\n", __func__, msg->id);
    
    ASSERT_WARN(softmac_msg != NULL);
    
    memcpy(softmac_msg, msg->param, msg->param_len);

    bl_send_ke_msg(bl_hw, softmac_msg, msg->id);

    dbg("%s done\n", __func__);

    return 0;
}

int softmac_fwd_kmsg_to_drv(struct bl_hw *bl_hw, struct ke_msg *msg)
{
    struct sk_buff *skb;
    uint8_t *data;
    struct inf_hdr *hdr;
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif

    dbg_con("%s, id:0x%x\n", __func__, msg->id);

    skb = alloc_skb(msg->param_len + sizeof(struct ke_msg) + 
                    sizeof(struct inf_hdr) + 500, 
                    GFP_ATOMIC);
    if (skb == NULL) {
        dbg_f("%s, fail to alloc skb, fatal error\n", __func__);
        
        return -1;
    }

    data = skb_put(skb, msg->param_len + sizeof(struct ke_msg) + sizeof(struct inf_hdr));
    hdr = (struct inf_hdr *)data;

    memset(data, 0, msg->param_len + sizeof(struct ke_msg) + sizeof(struct inf_hdr));

    hdr->len = msg->param_len + sizeof(struct ke_msg) - sizeof(msg->hdr);
    hdr->type = BL_TYPE_MSG;
    hdr->reserved = 0;
    hdr->queue_idx = 0;

    memcpy((data + sizeof(struct inf_hdr)), &msg->id, 
           sizeof(struct ke_msg) - sizeof(msg->hdr) + msg->param_len);

    dbg("%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
          data[0], data[1], data[2], data[3], data[4], data[5], data[6], 
          data[7], data[8], data[9], 
          data[10], data[11], data[12], data[13], data[14], data[15]);

    //delay to process in rx_work queue if there is.
    BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
    skb_queue_tail(&bl_hw->rx_pkt_list, skb);
    BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
    
    if (bl_hw->rx_work_flag) {
        bl_queue_rx_work(bl_hw);
    } else {
        bl_queue_main_work(bl_hw);
    }
    
    dbg("%s done\n", __func__);

    return 0;
}

int softmac_send_kmsg_ack_to_drv(struct bl_hw *bl_hw)
{
    struct sk_buff *skb;
    uint8_t *data;
    struct inf_hdr *hdr;
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif

    dbg_con("%s\n", __func__);

    skb = alloc_skb(1 + sizeof(struct inf_hdr) + 500, 
                    GFP_ATOMIC);
    if (skb == NULL) {
        dbg_f("%s, fail to alloc skb, fatal error\n", __func__);
        
        return -1;
    }

    data = skb_put(skb, 1 + sizeof(struct inf_hdr));
    hdr = (struct inf_hdr *)data;

    memset(data, 0, 1 + sizeof(struct inf_hdr));

    hdr->len = 1;
    hdr->type = BL_TYPE_ACK;
    hdr->reserved = 0;
    hdr->queue_idx = 0;

    memcpy((data + sizeof(struct inf_hdr)), &bl_hw->ipc_env->msga2e_cnt, 1);
    
    dbg("%s, skb->len:%d\n", __func__, skb->len);
    
    //delay to process in rx_work queue if there is.
    BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
    skb_queue_tail(&bl_hw->rx_pkt_list, skb);
    BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
    if(bl_hw->rx_work_flag) {
        bl_queue_rx_work(bl_hw);
    } else {
        bl_queue_main_work(bl_hw);
    }

    dbg("%s, queue rx work done\n", __func__);
    
    return 0;
}

void softmac_me_config(struct me_config_req const *param) {
    dbg_con("%s\n", __func__);

    me_env.capa_flags = 0;

    // Copy the HT, VHT and HE capabilities
    if (param->ht_supp)
    {
        LOCAL_CAPA_SET(HT);
        me_env.ht_cap = param->ht_cap;
    }
#if NX_VHT
    if (param->vht_supp)
    {
        LOCAL_CAPA_SET(VHT);
        me_env.vht_cap = param->vht_cap;
    }
#endif
#if NX_HE
    if (param->he_supp)
    {
        LOCAL_CAPA_SET(HE);
        me_env.he_cap = param->he_cap;

        // 20220519 filter invalid mcs for BL616 by Jochen
        if ((me_env.he_cap.mcs_supp.rx_mcs_80 & 0b11) == 0b10) {
            me_env.he_cap.mcs_supp.rx_mcs_80 &= ~((uint16_t)0b11);
            me_env.he_cap.mcs_supp.rx_mcs_80 |= 0b01;
        }

        if ((me_env.he_cap.mcs_supp.tx_mcs_80 & 0b11) == 0b10) {
            me_env.he_cap.mcs_supp.tx_mcs_80 &= ~((uint16_t)0b11);
            me_env.he_cap.mcs_supp.tx_mcs_80 |= 0b01;
        }
    }

    if (param->he_ul_on)
    {
        LOCAL_CAPA_SET(OFDMA_UL);
    }
#endif

    // Set maximum supported bandwidth
    me_env.phy_bw_max = param->phy_bw_max;

    // Perform additional checks depending on HT/VHT/HE support
    if (LOCAL_CAPA(HT))
    {
        // Set the maximum number of NSS supported when using STBC for TX
        me_env.stbc_nss = (1+1)/2; // (phy_get_nss()+1)/2;

        // Get the maximum BW supported
        #if NX_HE
        if (LOCAL_CAPA(HE))
        {
            bool stbc_tx_under_80 = HE_PHY_CAPA_BIT_IS_SET(&me_env.he_cap, STBC_TX_UNDER_80MHZ);
            bool stbc_tx_above_80 = HE_PHY_CAPA_BIT_IS_SET(&me_env.he_cap, STBC_TX_ABOVE_80MHZ);

            if (me_env.phy_bw_max > PHY_CHNL_BW_80)
                me_env.he_stbc_nss = stbc_tx_under_80 & stbc_tx_above_80 ? 1 : 0;
            else
                me_env.he_stbc_nss = stbc_tx_under_80 ? 1 : 0;
        }
        #endif
    }
    else
    {
        me_env.stbc_nss = 0;
    }

    // Set the lifetime of packets sent under BA agreement
    me_env.tx_lft = param->tx_lft;

    // Set the amsdu force
    me_env.amsdu_tx = param->amsdu_tx;

    #if NX_POWERSAVE
    // Save PS mode flags
    me_env.ps_on = param->ps_on;
    me_env.ps_mode = param->dpsm ? PS_MODE_ON_DYN : PS_MODE_ON;
    #endif
}

void softmac_me_chan_config(struct me_chan_config_req const *param) 
{
    dbg_con("%s\n", __func__);
    
    me_env.chan = *param;
}

struct sk_buff * softmac_alloc_skb(uint32_t len)
{
    struct sk_buff *skb;

    skb = dev_alloc_skb(len);
    dbg("%s, skb:0x%p\r\n", __func__, skb);

    return skb;
}

void softmac_free_skb(struct sk_buff *skb)
{
    dbg("%s, skb:0x%p\r\n", __func__, skb);
    
    dev_kfree_skb_any(skb);
}

