#include "bl_ipc_compat.h"

#include "ke_mem.h"
#include "ke_task.h"
#include "ke_msg.h"

#include "softmac.h"

#include "txl_frame.h"

void txl_frame_push(struct txl_frame_desc_tag *frame)
{
    dbg("%s, frame:0x%p\r\n", __func__, frame);
    dbg("%s, frame skb:0x%p\r\n", __func__, frame->skb);
    dbg("%s, frame skb->data:0x%p, skb->len:%u\r\n",
          __func__, frame->skb->data, frame->skb->len);
    
    softmac_txl_frame_req(frame->skb->data, frame->skb->len);
}

struct txl_frame_desc_tag *txl_frame_get(int len)
{
    struct txl_frame_desc_tag *frame = NULL;
    struct sk_buff *skb = NULL;

    skb = softmac_alloc_skb(len*2 + sizeof(struct txl_frame_desc_tag));
    if (skb) 
    {
        frame = (struct txl_frame_desc_tag *)skb->data;
        skb_reserve(skb, sizeof(struct txl_frame_desc_tag)+16);

        frame->skb = skb;
        
        frame->cfm.cfm_func = NULL;
        frame->cfm.env = NULL;

        memset(skb->data, 0, len);
    }
    else
    {
        dbg_f("fail to get txl frame, len:%d\r\n", len);
        ASSERT_ERR(0);
    }

    return frame;
}

void txl_frame_set_len(struct txl_frame_desc_tag *frame, int len)
{
    struct sk_buff *skb = NULL;

    skb = frame->skb;
    skb_put(skb, len);
}

uint8_t *txl_frame_payload_get(struct txl_frame_desc_tag *frame)
{
    return (uint8_t *)frame->skb->data;
}

uint16_t txl_frame_get_frm_type(struct txl_frame_desc_tag *frame)
{
    struct txl_frame_snd_req *req = 
                    (struct txl_frame_snd_req *)txl_frame_payload_get(frame);

    return req->frame_type;
}

struct txl_frame_snd_req * txl_frame_get_req(struct txl_frame_desc_tag *frame)
{
    return (struct txl_frame_snd_req *)txl_frame_payload_get(frame);
}

void txl_frame_dump_info(struct txl_frame_desc_tag *frame)
{
    #if 0
    struct txl_frame_snd_req * req = 
                   (struct txl_frame_snd_req *)txl_frame_payload_get(frame);

    dbg_f("%s, req frame_type:0x%x, vif_idx:%d, sta_idx:%d\r\n",
          __func__, req->frame_type, req->vif_idx, req->sta_idx);
    #endif
}

