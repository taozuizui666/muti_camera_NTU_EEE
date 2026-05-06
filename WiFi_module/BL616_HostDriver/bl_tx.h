/**
 ******************************************************************************
 *
 *  @file bl_tx.h
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

#ifndef _BL_TX_H_
#define _BL_TX_H_

#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <linux/netdevice.h>
#include "bl_lmac_types.h"
#include "bl_lmac_msg.h"
#include "bl_ipc_shared.h"
#include "bl_txq.h"
#include "bl_hal_desc.h"

#define BL_HWQ_BK                     0
#define BL_HWQ_BE                     1
#define BL_HWQ_VI                     2
#define BL_HWQ_VO                     3
#define BL_HWQ_BCMC                   4
#define BL_HWQ_NB                     NX_TXQ_CNT
#define BL_HWQ_ALL_ACS (BL_HWQ_BK | BL_HWQ_BE | BL_HWQ_VI | BL_HWQ_VO)
#define BL_HWQ_ALL_ACS_BIT ( BIT(BL_HWQ_BK) | BIT(BL_HWQ_BE) |    \
                               BIT(BL_HWQ_VI) | BIT(BL_HWQ_VO) )

#define BL_TX_LIFETIME_MS             100
#define BL_TX_MAX_RATES               NX_TX_MAX_RATES

#define BL_SWTXHDR_ALIGN_SZ           4
#define BL_SWTXHDR_ALIGN_MSK (BL_SWTXHDR_ALIGN_SZ - 1)
#define BL_SWTXHDR_ALIGN_PADS(x) \
                    ((BL_SWTXHDR_ALIGN_SZ - ((x) & BL_SWTXHDR_ALIGN_MSK)) \
                     & BL_SWTXHDR_ALIGN_MSK)

#ifdef CONFIG_BL_SDIO
// tx/rx exec in irq and workqueue
#define BL_TX_LOCK      spin_lock_irqsave
#define BL_TX_UNLOCK    spin_unlock_irqrestore
#define BL_RX_LOCK      spin_lock_irqsave
#define BL_RX_UNLOCK    spin_unlock_irqrestore
#else
#define BL_TX_LOCK(a, b)      spin_lock_bh(a)
#define BL_TX_UNLOCK(a, b)    spin_unlock_bh(a)
#define BL_RX_LOCK(a, b)      spin_lock_bh(a)
#define BL_RX_UNLOCK(a, b)    spin_unlock_bh(a)

#endif

#if BL_SWTXHDR_ALIGN_SZ & BL_SWTXHDR_ALIGN_MSK
#error bad BL_SWTXHDR_ALIGN_SZ
#endif

#define AMSDU_PADDING(x) ((4 - ((x) & 0x3)) & 0x3)

#define TXU_CNTRL_RETRY        BIT(0)
#define TXU_CNTRL_MORE_DATA    BIT(2)
#define TXU_CNTRL_MGMT         BIT(3)
#define TXU_CNTRL_MGMT_NO_CCK  BIT(4)
#define TXU_CNTRL_AMSDU        BIT(6)
#define TXU_CNTRL_MGMT_ROBUST  BIT(7)
#define TXU_CNTRL_USE_4ADDR    BIT(8)
#define TXU_CNTRL_EOSP         BIT(9)
#define TXU_CNTRL_MESH_FWD     BIT(10)
#define TXU_CNTRL_TDLS         BIT(11)

#define TX_MAX_MAC_HEADER_SIZE  76 // mac header(36) +  sec header(18)  + amsdu header(14) + llc header(8)

extern const int bl_tid2hwq[IEEE80211_NUM_TIDS];

#define BL_TCP_ACK_FLAG             (0x10)
#define BL_TCP_ACK_FLAG_OFFSET      (13)

#define PHY_TRPC_NUM_MCS    (10)
#define PHY_11B_RATE_NUM    (4)
#define PHY_11G_RATE_NUM    (8)
#define PHY_11N_RATE_NUM    (8)
#define PHY_11AX_RATE_NUM    (10)

#define BL_RPT_ARP_TABLE_SIZE   (10)

#define BL_DHCP_MAGIC               (0x63825363)
#define BL_DHCP_MAGIC_OFFSET        (236)
#define BL_DHCP_SERVER_PORT         (67)
#define BL_DHCP_CLIENT_PORT         (68)
#define BL_DHCP_BOOTP_BROADCAST     (0x8000)
#define BL_DHCP_BOOTP_FLAG_OFFSET   (10)


/* PHY TX/RX Power Control mode */
enum
{
    PHY_TRPC_11B = 0,
    PHY_TRPC_11G,
    PHY_TRPC_11N_BW20,
    PHY_TRPC_11N_BW40,
    PHY_TRPC_11AX_BW20,
    PHY_TRPC_11AX_BW40,
    PHY_TRPC_MODE_MAX
};

/**
 * struct bl_amsdu_txhdr - Structure added in skb headroom (instead of
 * bl_txhdr) for amsdu subframe buffer (except for the first subframe
 * that has a normal bl_txhdr)
 *
 * @list     List of other amsdu subframe (bl_sw_txhdr.amsdu.hdrs)
 * @map_len  Length to be downloaded for this subframe
 * @dma_addr Buffer address form embedded point of view
 * @skb      skb
 * @pad      padding added before this subframe
 *           (only use when amsdu must be dismantled)
 * @msdu_len Size, in bytes, of the MSDU (without padding nor amsdu header)
 */
struct bl_amsdu_txhdr {
    struct list_head list;
    size_t map_len;
    dma_addr_t dma_addr;
    struct sk_buff *skb;
    u16 pad;
    u16 msdu_len;
};

/**
 * struct bl_amsdu - Structure to manage creation of an A-MSDU, updated
 * only In the first subframe of an A-MSDU
 *
 * @hdrs List of subframe of bl_amsdu_txhdr
 * @len  Current size for this A-MDSU (doesn't take padding into account)
 *       0 means that no amsdu is in progress
 * @nb   Number of subframe in the amsdu
 * @pad  Padding to add before adding a new subframe
 */
struct bl_amsdu {
    struct list_head hdrs;
    u16 len;
    u8 nb;
    u8 pad;
};

/**
 * struct bl_sw_txhdr - Software part of tx header
 *
 * @bl_sta sta to which this buffer is addressed
 * @bl_vif vif that send the buffer
 * @txq pointer to TXQ used to send the buffer
 * @hw_queue Index of the HWQ used to push the buffer.
 *           May be different than txq->hwq->id on confirmation.
 * @frame_len Size of the frame (doesn't not include mac header)
 *            (Only used to update stat, can't we use skb->len instead ?)
 * @headroom Headroom added in skb to add bl_txhdr
 *           (Only used to remove it before freeing skb, is it needed ?)
 * @amsdu Description of amsdu whose first subframe is this buffer
 *        (amsdu.nb = 0 means this buffer is not part of amsdu)
 * @skb skb received from transmission
 * @map_len  Length mapped for DMA (only bl_hw_txhdr and data are mapped)
 * @dma_addr DMA address after mapping
 * @desc Buffer description that will be copied in shared mem for FW
 * @jiffies Jiffies when this buffer has been pushed to the driver
 */
struct bl_sw_txhdr {
    struct bl_sta *bl_sta;
    struct bl_vif *bl_vif;
    struct bl_txq *txq;
    u8 hw_queue;
    u16 frame_len;
    u16 headroom;
#ifdef CONFIG_BL_AMSDUS_TX
    struct bl_amsdu amsdu;
#endif
    struct sk_buff *skb;

    size_t map_len;
    dma_addr_t dma_addr;
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    struct inf_hdr hdr;
#endif

    struct txdesc_api desc;
    unsigned long jiffies;
};

/**
 * struct bl_txhdr - Stucture to control transimission of packet
 * (Added in skb headroom)
 *
 * @sw_hdr: Information from driver
 * @cache_guard:
 * @hw_hdr: Information for/from hardware
 */
struct bl_txhdr {
    struct bl_sw_txhdr *sw_hdr;
    char cache_guard[L1_CACHE_BYTES];
    struct bl_hw_txhdr hw_hdr;
};


struct bl_tcp_stream {
    struct list_head    tcp_ack;
    struct bl_hw *      bl_hw;
    struct bl_txq *     txq;
    struct sk_buff *    ack_skb_new;
    struct sk_buff *    ack_skb_priv;
    struct timer_list   tcp_ack_timer;

    /* ip header info */
    u32     saddr;
    u32     daddr;
    /* tcp header info */
    u32     ack_seq;
    u16     source;
    u16     dest;

    u16     tcp_ack_cnt;
    bool    is_timer_set;
    bool    is_timer_to;
};

struct bl_arp_table {
    u8_l  valid;
    u8_l  ar_sha[ETH_ALEN];  /* sender hardware address */
    u32_l ar_sip;            /* sender IP address       */
    ktime_t ts;
};

u16 bl_select_txq(struct bl_vif *bl_vif, struct sk_buff *skb);
int bl_start_xmit(struct sk_buff *skb, struct net_device *dev);
int bl_start_mgmt_xmit(struct bl_vif *vif, struct bl_sta *sta,
                         const u8 *buf, size_t len, bool no_cck,
                         int n_csa_offsets,
                         const u16 *csa_offsets,
                         bool offchan, u64 *cookie);

#if defined CONFIG_BL_SDIO
int bl_txdatacfm(void *pthis, void *host_id, void *data1, void **data2);
#elif defined CONFIG_BL_USB
int bl_txdatacfm(void *pthis, void *host_id, void *data1, void **data2);
#endif

struct bl_hw;
struct bl_sta;
void bl_set_traffic_status(struct bl_hw *bl_hw,
                             struct bl_sta *sta,
                             bool available,
                             u8 ps_id);
void bl_ps_bh_enable(struct bl_hw *bl_hw, struct bl_sta *sta,
                       bool enable);
void bl_ps_bh_traffic_req(struct bl_hw *bl_hw, struct bl_sta *sta,
                            u16 pkt_req, u8 ps_id);

void bl_switch_vif_sta_txq(struct bl_sta *sta, struct bl_vif *old_vif,
                             struct bl_vif *new_vif);

int bl_dbgfs_print_sta(char *buf, size_t size, struct bl_sta *sta,
                         struct bl_hw *bl_hw);
void bl_txq_credit_update(struct bl_hw *bl_hw, int sta_idx, u8 tid,
                            s8 update);
void bl_tx_push(struct bl_hw *bl_hw, struct bl_txhdr *txhdr, int flags);

void bl_tx_multi_pkt_push(struct bl_hw *bl_hw, struct sk_buff_head *sk_list_push);

void bl_tcp_ack_stream_clear(struct bl_hw * bl_hw);
int bl_tcp_ack_process(struct bl_hw * bl_hw, struct sk_buff * skb, struct bl_txq * txq);
void bl_skb_parsing(struct bl_hw *bl_hw,  struct sk_buff *skb, u8 tx_rx, u16 data_offset);
int bl_set_default_tx_pwr(struct bl_hw *bl_hw);

void bl_rpt_arp_table_init(struct bl_hw *bl_hw);
void bl_rpt_eth_mac_change(struct bl_vif * bl_vif,  u8 * eth_hdr, bool is_tx);

#endif /* _BL_TX_H_ */
