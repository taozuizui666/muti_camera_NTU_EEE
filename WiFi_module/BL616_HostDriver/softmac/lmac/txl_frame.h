#ifndef __TXL_FRAME_H__
#define __TXL_FRAME_H__

/// Type of frame descriptor
enum
{
    /// Internal frame descriptor, i.e part of the generic frame module
    TX_INT,
    /// External frame descriptor
    TX_EXT
};

/// Pointer to confirmation function
typedef void (*cfm_func_ptr)(void *, uint32_t);

/// TX frame confirmation descriptor
struct txl_frame_cfm_tag
{
    /// Function to be called when TX has been completed
    cfm_func_ptr cfm_func;
    /// Void pointer to be passed to the confirmation function after TX completion
    void *env;
};

/// TX frame descriptor
struct txl_frame_desc_tag
{
    struct sk_buff *skb;
    /// Confirmation descriptor used after TX completion to report the status
    struct txl_frame_cfm_tag cfm;
};

struct txl_frame_snd_auth_req {
    uint16_t frame_type;
    uint32_t txtype;
    uint8_t ac;

    uint8_t vif_idx;
    uint8_t sta_idx;
    uint16_t auth_type;
    uint16_t auth_seq;
    uint16_t ie_len;
    uint32_t ie_buf[64];
    uint16_t challenge_len;
    uint8_t challenge[MAC_AUTH_CHALLENGE_LEN];
};

struct txl_frame_snd_req {
    uint16_t frame_type;
    uint32_t txtype;
    uint8_t ac;

    uint8_t vif_idx;
    uint8_t sta_idx;
};

struct txl_frame_snd_deauth_req {
    uint16_t frame_type;
    uint32_t txtype;
    uint8_t ac;

    uint8_t vif_idx;
    uint8_t sta_idx;
    uint16_t reason_code;
};

struct txl_frame_act_sa_query_req {
    uint16_t frame_type;
    uint32_t txtype;
    uint8_t ac;

    uint8_t vif_idx;
    uint8_t sta_idx;
    
    uint8_t act_cat;
    
    uint16_t transaction_id;
};

struct txl_frame_act_rm_bcn_report_req {
    uint16_t frame_type;
    uint32_t txtype;
    uint8_t ac;

    uint8_t vif_idx;
    uint8_t sta_idx;
    
    uint8_t act_cat;

    bool init_mac_hdr;
    uint32_t length;
    uint8_t action[NX_TXFRAME_LEN];
};

void txl_frame_push(struct txl_frame_desc_tag *frame);
struct txl_frame_desc_tag *txl_frame_get(int len);
void txl_frame_set_len(struct txl_frame_desc_tag *frame, int len);
uint8_t *txl_frame_payload_get(struct txl_frame_desc_tag *frame);
uint16_t txl_frame_get_frm_type(struct txl_frame_desc_tag *frame);
struct txl_frame_snd_req * txl_frame_get_req(struct txl_frame_desc_tag *frame);
void txl_frame_dump_info(struct txl_frame_desc_tag *frame);


#endif

