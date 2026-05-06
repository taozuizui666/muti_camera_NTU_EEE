#ifndef __SOFTMAC_H__
#define __SOFTMAC_H__

#include "rwnx_config.h"

#include "bl_defs.h"

#include "co_list.h"

#include "ke_msg.h"

#include "mac_types.h"
#include "me.h"
#include "sta_mgmt.h"
#include "vif_mgmt.h"

enum {
    UPDATE_VAR_VIF,
    UPDATE_VAR_STA,
};

enum {
    VIF_FIELD_BSS_INFO,
    VIF_FIELD_TX_PWR,
    VIF_FIELD_ACTIVE,
    VIF_FIELD_FLAGS,
    VIF_FIELD_BSS_CAP_FLAGS,
    VIF_FIELD_BSS_CHAN,
    VIF_FIELD_STA_DISCON,
};

enum {
    FW_ACT_SEND_TXL_FRAME,
    FW_ACT_APM_PROBE_CLIENT_REQ,
    FW_ACT_APM_STOP_REQ,
    FW_ACT_APM_START_REQ,
    FW_ACT_PS_FLAGS_REQ,
};

struct update_fw_vif_req {
    uint16_t update_var_type;
    uint16_t update_field_type;
    uint16_t update_len;
    uint32_t vif_index;
    uint32_t update_field_val[];
};

struct fw_act_req {
    uint16_t act_type;
    uint32_t len;
    uint32_t data[];
};

struct fw_act_ind {
    uint16_t act_type;
    uint32_t len;
    uint32_t status;
    uint32_t data[];
};

struct ap_start_req
{
    uint8_t vif_idx;
    //BCMC sta_idx
    uint8_t sta_idx;
    struct mac_rateset basic_rates;
    uint32_t flags;
    uint16_t ctrl_port_ethertype;
};

struct ps_flags_req
{
    uint8_t vif_idx;
    uint8_t is_clear;
    uint32_t flags;
};


#if 0
#define GLOBAL_INT_DISABLE()                        \
    do {                                            \
        unsigned long flags;                        \
        local_irq_save(flags);

#define GLOBAL_INT_RESTORE()                            \
        local_irq_restore(flags);                       \
    } while(0);
#else
#define GLOBAL_INT_DISABLE()
#define GLOBAL_INT_RESTORE()
#endif


int softmac_init(struct bl_hw *bl_hw);
void softmac_deinit(void);
void softmac_schedule(void);
void softmac_timer_set(uint64_t time);
uint64_t softmac_time(void);
uint32_t softmac_time_us(void);
void softmac_timer_cb(struct timer_list *t);
bool softmac_time_past(uint64_t time);
void softmac_vif_add(struct mm_add_if_req *req, uint8_t vif_index);
void softmac_vif_remove(uint8_t vif_index);
void softmac_vif_update(uint8_t vif_index, uint8_t field_type, 
                               uint8_t *field_value, uint32_t field_len);
void softmac_vif_update_local(uint8_t vif_index, uint8_t field_type, 
                              uint8_t *field_value, uint32_t field_len);
int softmac_scan_done_ind(struct bl_hw *bl_hw, struct bl_cmd *cmd,
                                   struct ipc_e2a_msg *msg);
int softmac_fwd_kmsg_to_fw(struct bl_hw *bl_hw, struct ke_msg *msg);
int softmac_fwd_kmsg_to_drv(struct bl_hw *bl_hw, struct ke_msg *msg);
int softmac_send_kmsg_ack_to_drv(struct bl_hw *bl_hw);
int softmac_handle_kmsg_from_fw(struct bl_hw *bl_hw, struct bl_cmd *cmd,
                                             struct ipc_e2a_msg *msg);
int softmac_handle_kmsg_from_drv(struct bl_hw *bl_hw,
                                              struct lmac_msg *msg);
void softmac_me_config(struct me_config_req const *param) ;
void softmac_me_chan_config(struct me_chan_config_req const *param);
void softmac_txl_frame_req(uint8_t *data, uint32_t len);
void softmac_ap_probe_req(uint8_t vif_idx, uint8_t sta_idx);
void softmac_ap_stop_req(uint8_t vif_idx, uint8_t sta_idx);
void softmac_ap_start_req(uint8_t vif_idx, uint8_t sta_idx,
                                 uint16_t ctrl_port_ethertype, 
                                 struct mac_rateset *basic_rates,
                                 uint32_t flags);
void softmac_ps_flags_req(uint8_t vif_idx, uint32_t flags, uint8_t is_clear);

struct sk_buff * softmac_alloc_skb(uint32_t len);
void softmac_free_skb(struct sk_buff *skb);
void softmac_handle_sm_scanu_frame_from_fw(struct bl_hw *bl_hw, 
                        struct sk_buff *skb, struct rx_mgmt_info *rx_mgmt_inf,
                        int msdu_offset, uint8_t inst_nbr, uint8_t sta_idx, 
                        uint16_t center_freq, uint8_t band);


#endif
