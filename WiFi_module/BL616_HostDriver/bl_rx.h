/**
 ******************************************************************************
 *
 *  @file bl_rx.h
 *
 *  Copyright (C) BouffaloLab 2017-2023
 *
 *  Licensed under the Apache License, Version 2.0 (the License);
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an ASIS BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************
 */

#ifndef _BL_RX_H_
#define _BL_RX_H_

#include <linux/workqueue.h>
#include "bl_hal_desc.h"

enum rx_status_bits
{
    /// The buffer can be forwarded to the networking stack
    RX_STAT_FORWARD = 1 << 0,
    /// A new buffer has to be allocated
    RX_STAT_ALLOC = 1 << 1,
    /// The buffer has to be deleted
    RX_STAT_DELETE = 1 << 2,
    /// The length of the buffer has to be updated
    RX_STAT_LEN_UPDATE = 1 << 3,
    /// The length in the Ethernet header has to be updated
    RX_STAT_ETH_LEN_UPDATE = 1 << 4,
    /// Simple copy
    RX_STAT_COPY = 1 << 5,
    /// Spurious frame (inform upper layer and discard)
    RX_STAT_SPURIOUS = 1 << 6,
    /// packet for monitor interface
    RX_STAT_MONITOR = 1 << 7,
};

#ifdef CONFIG_BL_MON_DATA
#define RX_MACHDR_BACKUP_LEN    64

/// MAC header backup descriptor
struct mon_machdrdesc
{
    /// Length of the buffer
    u32 buf_len;
    /// Buffer containing mac header, LLC and SNAP
    u8 buffer[RX_MACHDR_BACKUP_LEN];
};
#endif

struct bl_rxu_hdr {
    u32  status;
    u16  sn;
    u16  msdu_offset;            //offset from datastartptr to real msdu start.
    u8   tid;
    u8   fn;                     // frag number
    u8   flags_mf;               // more frag
    u8   flags_is_pn_check;      // need driver check pn
    u8   flags_sm_scanu;
    u8   flags_rsvd[2];          // for alined
    u8   rx_key_idx;             // rx key idx
    u64  pn;                     // rx pn
};

struct hw_rxhdr {
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    struct bl_rxu_hdr rxu_hdr;
#endif
    /** RX vector */
    struct hw_vect hwvect;

    /** PHY channel information */
    struct phy_channel_info_desc phy_info;

    /** RX flags */
    u32    flags_is_amsdu     : 1;
    u32    flags_is_80211_mpdu: 1;
    u32    flags_is_4addr     : 1;
    u32    flags_new_peer     : 1;
    u32    flags_user_prio    : 3;
    //u32    flags_rsvd0        : 1;
    u32    flags_is_bar       : 1;    // used for driver reorder
    u32    flags_vif_idx      : 8;    // 0xFF if invalid VIF index
    u32    flags_sta_idx      : 8;    // 0xFF if invalid STA index
    u32    flags_dst_idx      : 8;    // 0xFF if unknown destination STA
#ifdef CONFIG_BL_MON_DATA
    /// MAC header backup descriptor (used only for MSDU when there is a monitor and a data interface)
    struct mon_machdrdesc mac_hdr_backup;
#endif
    /** Pattern indicating if the buffer is available for the driver */
    u32    pattern;
};

/**
 * struct bl_defer_rx - Defer rx buffer processing
 *
 * @skb: List of deferred buffers
 * @work: work to defer processing of this buffer
 */
struct bl_defer_rx {
    struct sk_buff_head sk_list;
    struct work_struct work;
};

/**
 * struct bl_defer_rx_cb - Control buffer for deferred buffers
 *
 * @vif: VIF that received the buffer
 */
struct bl_defer_rx_cb {
    struct bl_vif *vif;
};

struct bl_agg_reodr_msg {
    u16 sn;
    u8 sta_idx;
    u8 tid;
    u8 status;
    u8 num;
}__packed;

#define RX_WIN_SIZE 64
#if defined  BL_RX_REORDER  && (defined CONFIG_BL_USB || defined CONFIG_BL_SDIO)
struct rxreorder_list {
    struct bl_hw *bl_hw;
    u8 flag;
    u8 check_start_win;
    u8 is_timer_set;
    u8 flush;
    u8 del_ba;
    u16 record_start_win;
    spinlock_t rx_lock;
    struct timer_list timer;
    u16 start_win;
    u16 end_win;
    u16 last_seq;
    u16 win_size;
    u16 start_win_index;
    u16 cnt;
    void *reorder_pkt[RX_WIN_SIZE];
};
#endif

#ifdef BL_RX_DEFRAG
#define DEFRAG_SKB_CNT       (16)
struct rxdefrag_list {
    struct bl_hw *bl_hw;
    /** Pointer to the next element in the queue */
    struct list_head list;
    /** Station Index */
    u8 sta_idx;
    /** Traffic Index */
    u8 tid;
    /** Next Expected FN */
    u8 next_fn;
    /** Sequence Number */
    u16 sn;
    /** Length to be copied */
    u16 cpy_len;    
    /** Total Frame Length */
    u16 frame_len;
    /** fragment skb buffer    pointer array */
    void *pkt[DEFRAG_SKB_CNT];
    /** lock */    
    spinlock_t rx_lock;
    /** timer used for flushing the fragments if the packet is not complete */
    bool is_timer_set;    
    struct timer_list timer;
    u64 next_pn;
    u8  rx_key_id;
};
#endif

#define BL_FC_GET_TYPE(fc)    (((fc) & 0x000c) >> 2)
#define BL_FC_GET_STYPE(fc)    (((fc) & 0x00f0) >> 4)

#define AGG_REORD_MSG_MAX_NUM 4096

#ifdef BL_RX_REORDER 
u8 bl_rx_packet_dispatch(struct bl_hw *bl_hw, struct sk_buff *skb);
void bl_rx_reorder_flush(struct timer_list *t);
#endif
void bl_rx_wq_hdlr(struct work_struct *work);
void bl_rx_wq_process(struct bl_hw *bl_hw);

u8 bl_unsup_rx_vec_ind(void *pthis, void *hostid);
u8 bl_rxdataind(void *pthis, void *hostid);
void bl_rx_deferred(struct work_struct *ws);
void bl_rx_defer_skb(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                       struct sk_buff *skb);
void bl_rx_data_flush(struct bl_hw *bl_hw);
int bl_rx_process(struct bl_hw *bl_hw);

#endif /* _BL_RX_H_ */
