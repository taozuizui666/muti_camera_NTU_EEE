/**
 ******************************************************************************
 *
 *  @file bl_defs.h
 *
 *  @brief Main driver structure declarations for fullmac driver
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


#ifndef _BL_DEFS_H_
#define _BL_DEFS_H_

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <linux/slab.h>
#ifdef CONFIG_WIRELESS_EXT
#include <linux/wireless.h>
#endif
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "bl_mod_params.h"
#ifdef CONFIG_BL_DEBUGFS
#include "bl_debugfs.h"
#endif
#include "bl_tx.h"
#include "bl_rx.h"
#include "bl_radar.h"
#include "bl_utils.h"
#include "bl_ipc.h"
#include "bl_mu_group.h"
#include "bl_platform.h"
#include "bl_cmds.h"

#ifdef BL_BUS_LOOPBACK
#include "bl_loopback.h"
#endif
#include "bl_compat.h"

#define BL_MULTI_HWS

#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
#define BL_HWS_MAX_NUM 5
#endif

#define IWPRIV_IND_LEN_MAX 2000

#define WPI_HDR_LEN    18
#define WPI_PN_LEN     16
#define WPI_PN_OFST     2
#define WPI_MIC_LEN    16
#define WPI_KEY_LEN    32
#define WPI_SUBKEY_LEN 16 // WPI key is actually two 16bytes key

#define LEGACY_PS_ID   0
#define UAPSD_ID       1

#define PS_SP_INTERRUPTED  255

#define BL_SPIN_LOCK(lock, f)        spin_lock(lock)
#define BL_SPIN_UNLOCK(lock, f)      spin_unlock(lock)

#define BL_SPIN_LOCK_IRQ(lock, f)    spin_lock_irq(lock)
#define BL_SPIN_UNLOCK_IRQ(lock, f)  spin_unlock_irq(lock)

#define BL_SPIN_LOCK_IRQSAVE(lock, f)       spin_lock_irqsave(lock, f)
#define BL_SPIN_UNLOCK_IRQRESTORE(lock, f)  spin_unlock_irqrestore(lock, f)

/** DMA alignment value */
#define DMA_ALIGNMENT   64

/** Macros for Data Alignment : address */
#define ALIGN_ADDR(p, a)    PTR_ALIGN(p, a)

#define CHIP_VER_616_A0  0x06160001
#define CHIP_VER_616_A1  0x06160002
#define CHIP_VER_616L    0x0616A001

#define USB_VENDOR_ID_BFL         0x349B
#define USB_DEVICE_ID_BFL_616_B   0x6160
#define USB_DEVICE_ID_BFL_618_B   0x6180
#define USB_DEVICE_ID_BFL_616_F   0x6161
#define USB_DEVICE_ID_BFL_618_F   0x6181

//WIFI interface
#define WIFI_INTERFACE_CLASS      0xFF
#define WIFI_INTERFACE_SUBCLASS   0x00
#define WIFI_INTERFACE_PRTO       0x01

//BLE interface
#define BLE_INTERFACE_CLASS       0xFF
#define BLE_INTERFACE_SUBCLASS    0x00
#define BLE_INTERFACE_PRTO        0x02

/**
 * struct bl_bcn - Information of the beacon in used (AP mode)
 *
 * @head: head portion of beacon (before TIM IE)
 * @tail: tail portion of beacon (after TIM IE)
 * @ies: extra IEs (not used ?)
 * @head_len: length of head data
 * @tail_len: length of tail data
 * @ies_len: length of extra IEs data
 * @tim_len: length of TIM IE
 * @len: Total beacon len (head + tim + tail + extra)
 * @dtim: dtim period
 */
struct bl_bcn {
    u8 *head;
    u8 *tail;
    u8 *ies;
    size_t head_len;
    size_t tail_len;
    size_t ies_len;
    size_t tim_len;
    size_t csa_len;
    size_t len;
    u8 dtim;
};

/**
 * struct bl_key - Key information
 *
 * @hw_idx: Index of the key from hardware point of view
 */
struct bl_key {
    u8 hw_idx;
};

/**
 * struct bl_mesh_path - Mesh Path information
 *
 * @list: List element of bl_vif.mesh_paths
 * @path_idx: Path index
 * @tgt_mac_addr: MAC Address it the path target
 * @nhop_sta: Next Hop STA in the path
 */
struct bl_mesh_path {
    struct list_head list;
    u8 path_idx;
    struct mac_addr tgt_mac_addr;
    struct bl_sta *nhop_sta;
};

/**
 * struct bl_mesh_path - Mesh Proxy information
 *
 * @list: List element of bl_vif.mesh_proxy
 * @ext_sta_addr: Address of the External STA
 * @proxy_addr: Proxy MAC Address
 * @local: Indicate if interface is a proxy for the device
 */
struct bl_mesh_proxy {
    struct list_head list;
    struct mac_addr ext_sta_addr;
    struct mac_addr proxy_addr;
    bool local;
};

/**
 * struct bl_csa - Information for CSA (Channel Switch Announcement)
 *
 * @vif: Pointer to the vif doing the CSA
 * @bcn: Beacon to use after CSA
 * @elem: IPC buffer to send the new beacon to the fw
 * @chandef: defines the channel to use after the switch
 * @count: Current csa counter
 * @status: Status of the CSA at fw level
 * @ch_idx: Index of the new channel context
 * @work: work scheduled at the end of CSA
 */
struct bl_csa {
    struct bl_vif *vif;
    struct bl_bcn bcn;
    struct bl_ipc_elem_var elem;
    struct cfg80211_chan_def chandef;
    int count;
    int status;
    int ch_idx;
    u8 channel_number;
    struct work_struct work;
};

/**
 * enum tdls_status_tag - States of the TDLS link
 *
 * @TDLS_LINK_IDLE: TDLS link is not active (no TDLS peer connected)
 * @TDLS_SETUP_REQ_TX: TDLS Setup Request transmitted
 * @TDLS_SETUP_RSP_TX: TDLS Setup Response transmitted
 * @TDLS_LINK_ACTIVE: TDLS link is active (TDLS peer connected)
 */
enum tdls_status_tag {
        TDLS_LINK_IDLE,
        TDLS_SETUP_REQ_TX,
        TDLS_SETUP_RSP_TX,
        TDLS_LINK_ACTIVE,
        TDLS_STATE_MAX
};

/**
 * struct bl_tdls - Information relative to the TDLS peer
 *
 * @active: Whether TDLS link is active or not
 * @initiator: Whether TDLS peer is the TDLS initiator or not
 * @chsw_en: Whether channel switch is enabled or not
 * @chsw_allowed: Whether TDLS channel switch is allowed or not
 * @last_tid: TID of the latest MPDU transmitted over the TDLS link
 * @last_sn: Sequence number of the latest MPDU transmitted over the TDLS link
 * @ps_on: Whether power save mode is enabled on the TDLS peer or not
 */
struct bl_tdls {
    bool active;
    bool initiator;
    bool chsw_en;
    u8 last_tid;
    u16 last_sn;
    bool ps_on;
    bool chsw_allowed;
};

/**
 * enum bl_ap_flags - AP flags
 *
 * @BL_AP_ISOLATE: Isolate clients (i.e. Don't bridge packets transmitted by
 * one client to another one
 * @BL_AP_USER_MESH_PM: Mesh Peering Management is done by user space
 * @BL_AP_CREATE_MESH_PATH: A Mesh path is currently being created at fw level
 */
enum bl_ap_flags {
    BL_AP_ISOLATE = BIT(0),
    BL_AP_USER_MESH_PM = BIT(1),
    BL_AP_CREATE_MESH_PATH = BIT(2),
};

/**
 * enum bl_sta_flags - STATION flags
 *
 * @BL_STA_EXT_AUTH: External authentication is in progress
 */
enum bl_sta_flags {
    BL_STA_EXT_AUTH = BIT(0),
    BL_STA_FT_OVER_DS = BIT(1),
    BL_STA_FT_OVER_AIR = BIT(2),
};

enum bl_opmode {
    BL_OPMODE_STA = 0,
    BL_OPMODE_AP,
    BL_OPMODE_COCURRENT,
    BL_OPMODE_REPEATER,
    BL_OPMODE_MAX_NUM
};

/**
 * struct bl_vif - VIF information
 *
 * @list: List element for bl_hw->vifs
 * @bl_hw: Pointer to driver main data
 * @wdev: Wireless device
 * @ndev: Pointer to the associated net device
 * @net_stats: Stats structure for the net device
 * @key: Conversion table between protocol key index and MACHW key index
 * @drv_vif_index: VIF index at driver level (only use to identify active
 * vifs in bl_hw->avail_idx_map)
 * @vif_index: VIF index at fw level (used to index bl_hw->vif_table, and
 * bl_sta->vif_idx)
 * @ch_index: Channel context index (within bl_hw->chanctx_table)
 * @up: Indicate if associated netdev is up (i.e. Interface is created at fw level)
 * @use_4addr: Whether 4address mode should be use or not
 * @is_resending: Whether a frame is being resent on this interface
 * @roc_tdls: Indicate if the ROC has been called by a TDLS station
 * @tdls_status: Status of the TDLS link
 * @tdls_chsw_prohibited: Whether TDLS Channel Switch is prohibited or not
 * @generation: Generation ID. Increased each time a sta is added/removed
 *
 * STA / P2P_CLIENT interfaces
 * @flags: see bl_sta_flags
 * @ap: Pointer to the peer STA entry allocated for the AP
 * @tdls_sta: Pointer to the TDLS station
 * @ft_assoc_ies: Association Request Elements (only allocated for FT connection)
 * @ft_assoc_ies_len: Size, in bytes, of the Association request elements.
 * @ft_target_ap: Target AP for a BSS transition for FT over DS
 *
 * AP/P2P GO/ MESH POINT interfaces
 * @flags: see bl_ap_flags
 * @sta_list: List of station connected to the interface
 * @bcn: Beacon data
 * @bcn_interval: beacon interval in TU
 * @bcmc_index: Index of the BroadCast/MultiCast station
 * @csa: Information about current Channel Switch Announcement (NULL if no CSA)
 * @mpath_list: List of Mesh Paths (MESH Point only)
 * @proxy_list: List of Proxies Information (MESH Point only)
 * @mesh_pm: Mesh power save mode currently set in firmware
 * @next_mesh_pm: Mesh power save mode for next peer
 *
 * AP_VLAN interfaces
 * @mater: Pointer to the master interface
 * @sta_4a: When AP_VLAN interface are used for WDS (i.e. wireless connection
 * between several APs) this is the 'gateway' sta to 'master' AP
 */
struct bl_vif {
    struct list_head list;
    struct bl_hw *bl_hw;
    struct wireless_dev wdev;
    struct net_device *ndev;
    struct net_device_stats net_stats;
    struct bl_key key[6];
    u8 drv_vif_index;
    u8 vif_index;
    u8 ch_index;
    bool up;
    bool use_4addr;
    bool is_resending;
    bool roc_tdls;
    u8 tdls_status;
    bool tdls_chsw_prohibited;
    int generation;
    
    union
    {
        struct
        {
            u32 flags;
            struct bl_sta *ap;
            struct bl_sta *tdls_sta;
            u8 *ft_assoc_ies;
            int ft_assoc_ies_len;
            u8 ft_target_ap[ETH_ALEN];
        } sta;
        
        struct
        {
            u32 flags;
            struct list_head sta_list;
            struct bl_bcn bcn;
            int bcn_interval;
            u8 bcmc_index;
            struct bl_csa *csa;

            struct list_head mpath_list;
            struct list_head proxy_list;
            enum nl80211_mesh_power_mode mesh_pm;
            enum nl80211_mesh_power_mode next_mesh_pm;
        } ap;
        
        struct
        {
            struct bl_vif *master;
            struct bl_sta *sta_4a;
        } ap_vlan;
    };

    u64 rx_pn;
};

#define BL_INVALID_VIF 0xFF
#define BL_VIF_TYPE(vif) (vif->wdev.iftype)

/**
 * Structure used to store information relative to PS mode.
 *
 * @active: True when the sta is in PS mode.
 *          If false, other values should be ignored
 * @pkt_ready: Number of packets buffered for the sta in drv's txq
 *             (1 counter for Legacy PS and 1 for U-APSD)
 * @sp_cnt: Number of packets that remain to be pushed in the service period.
 *          0 means that no service period is in progress
 *          (1 counter for Legacy PS and 1 for U-APSD)
 */
struct bl_sta_ps {
    bool active;
    u16 pkt_ready[2];
    u16 sp_cnt[2];
};

/**
 * struct bl_rx_rate_stats - Store statistics for RX rates
 *
 * @table: Table indicating how many frame has been receive which each
 * rate index. Rate index is the same as the one used by RC algo for TX
 * @size: Size of the table array
 * @cpt: number of frames received
 * @rate_cnt: number of rate for which at least one frame has been received
 */
struct bl_rx_rate_stats {
    int *table;
    int size;
    int cpt;
    int rate_cnt;
};

/**
 * struct bl_sta_stats - Structure Used to store statistics specific to a STA
 *
 * @rx_pkts: Number of MSDUs and MMPDUs received from this STA
 * @tx_pkts: Number of MSDUs and MMPDUs sent to this STA
 * @rx_bytes: Total received bytes (MPDU length) from this STA
 * @tx_bytes: Total transmitted bytes (MPDU length) to this STA
 * @last_act: Timestamp (jiffies) when the station was last active (i.e. sent a
 frame: data, mgmt or ctrl )
 * @last_rx: Hardware vector of the last received frame
 * @rx_rate: Statistics of the received rates
 */
struct bl_sta_stats {
    u32 rx_pkts;
    u32 tx_pkts;
    u64 rx_bytes;
    u64 tx_bytes;
    unsigned long last_act;
    struct hw_vect last_rx;
#ifdef CONFIG_BL_DEBUGFS
    struct bl_rx_rate_stats rx_rate;
#endif
};

/**
 * struct bl_sta - Managed STATION information
 *
 * @list: List element of bl_vif->ap.sta_list
 * @valid: Flag indicating if the entry is valid
 * @mac_addr:  MAC address of the station
 * @aid: association ID
 * @sta_idx: Firmware identifier of the station
 * @vif_idx: Firmware of the VIF the station belongs to
 * @vlan_idx: Identifier of the VLAN VIF the station belongs to (= vif_idx if
 * no vlan in used)
 * @band: Band (only used by TDLS, can be replaced by channel context)
 * @width: Channel width
 * @center_freq: Center frequency
 * @center_freq1: Center frequency 1
 * @center_freq2: Center frequency 2
 * @ch_idx: Identifier of the channel context linked to the station
 * @qos: Flag indicating if the station supports QoS
 * @acm: Bitfield indicating which queues have AC mandatory
 * @uapsd_tids: Bitfield indicating which tids are subject to UAPSD
 * @key: Information on the pairwise key install for this station
 * @ps: Information when STA is in PS (AP only)
 * @bfm_report: Beamforming report to be used for VHT TX Beamforming
 * @group_info: MU grouping information for the STA
 * @ht: Flag indicating if the station supports HT
 * @vht: Flag indicating if the station supports VHT
 * @ac_param[AC_MAX]: EDCA parameters
 * @tdls: TDLS station information
 * @stats: Station statistics
 * @mesh_pm: link-specific mesh power save mode
 * @listen_interval: listen interval (only for AP client)
 */
struct bl_sta {
    struct list_head list;
    bool valid;
    u8 mac_addr[ETH_ALEN];
    u16 aid;
    u8 sta_idx;
    u8 vif_idx;
    u8 vlan_idx;
    enum nl80211_band band;
    enum nl80211_chan_width width;
    u16 center_freq;
    u32 center_freq1;
    u32 center_freq2;
    u8 ch_idx;
    bool qos;
    u8 acm;
    u16 uapsd_tids;
    struct bl_key key;
    struct bl_sta_ps ps;
#ifdef CONFIG_BL_BFMER
    struct bl_bfmer_report *bfm_report;
#ifdef CONFIG_BL_MUMIMO_TX
    struct bl_sta_group_info group_info;
#endif /* CONFIG_BL_MUMIMO_TX */
#endif /* CONFIG_BL_BFMER */
    bool ht;
    bool vht;
    u32 ac_param[AC_MAX];
    struct bl_tdls tdls;
    struct bl_sta_stats stats;
    enum nl80211_mesh_power_mode mesh_pm;
    int listen_interval;
    struct twt_setup_ind twt_ind; /*TWT Setup indication*/
};

#define BL_INVALID_STA 0xFF

/**
 * bl_sta_addr - Return MAC address of a STA
 *
 * @sta: Station whose address is returned
 */
static inline const u8 *bl_sta_addr(struct bl_sta *sta) {
    return sta->mac_addr;
}

#ifdef CONFIG_BL_SPLIT_TX_BUF
/**
 * struct bl_amsdu_stats - A-MSDU statistics
 *
 * @done: Number of A-MSDU push the firmware
 * @failed: Number of A-MSDU that failed to transit
 */
struct bl_amsdu_stats {
    int done;
    int failed;
};
#endif

/**
 * struct bl_stats - Global statistics
 *
 * @cfm_balance: Number of buffer currently pushed to firmware per HW queue
 * @last_rx: Jiffies of the last received frame
 * @last_tx: Jiffies of the last transmitted frame
 * @ampdus_tx: Number of A-MPDU transmitted (indexed by A-MPDU length)
 * @ampdus_rx: Number of A-MPDU received (indexed by A-MPDU length)
 * @ampdus_rx_map: Internal variable to distinguish A-MPDU
 * @ampdus_rx_miss: Number of MPDU non missing while receiving a-MPDU
 * @amsdus: statistics of a-MSDU transmitted
 * @amsdus_rx: Number of A-MSDU received (indexed by A-MSDU length)
 */
struct bl_stats {
    int cfm_balance[NX_TXQ_CNT];
    unsigned long last_rx, last_tx; /* jiffies */
    int ampdus_tx[IEEE80211_MAX_AMPDU_BUF];
    int ampdus_rx[IEEE80211_MAX_AMPDU_BUF];
    int ampdus_rx_map[4];
    int ampdus_rx_miss;
#ifdef CONFIG_BL_SPLIT_TX_BUF
    struct bl_amsdu_stats amsdus[NX_TX_PAYLOAD_MAX];
#endif
    int amsdus_rx[64];
};

/**
 * struct bl_roc - Remain On Channel information
 *
 * @vif: VIF for which RoC is requested
 * @chan: Channel to remain on
 * @duration: Duration in ms
 * @internal: Whether RoC has been started internally by the driver (e.g. to send
 * mgmt frame) or requested by user space
 * @on_chan: Indicate if we have switch on the RoC channel
 */
struct bl_roc {
    struct bl_vif *vif;
    struct ieee80211_channel *chan;
    unsigned int duration;
    bool internal;
    bool on_chan;
};

/**
 * struct bl_survey_info - Channel Survey Information
 *
 * @filled: filled bitfield as per struct survey_info
 * @chan_time_ms: Amount of time in ms the radio spent on the channel
 * @chan_time_busy_ms: Amount of time in ms the primary channel was sensed busy
 * @noise_dbm: Noise in dbm
 */
struct bl_survey_info {
    u32 filled;
    u32 chan_time_ms;
    u32 chan_time_busy_ms;
    s8 noise_dbm;
};

/**
 * bl_chanctx - Channel context information
 *
 * @chan_def: Channel description
 * @count: number of vif using this channel context
 */
struct bl_chanctx {
    struct cfg80211_chan_def chan_def;
    u8 count;
};

#define BL_CH_NOT_SET 0xFF

/**
 * bl_phy_info - Phy information
 *
 * @cnt: Number of phy interface
 * @cfg: Configuration send to firmware
 * @sec_chan: Channel configuration of the second phy interface (if phy_cnt > 1)
 * @limit_bw: Set to true to limit BW on requested channel. Only set to use
 * VHT with old radio that don't support 80MHz (deprecated)
 */
struct bl_phy_info {
    u8 cnt;
    struct phy_cfg_tag cfg;
    struct mac_chan_op sec_chan;
    bool limit_bw;
};

struct bl_agg_reord_pkt {
    struct list_head list;
    struct sk_buff *skb;
    u16 sn;
};

#ifdef BL_TRX_PROFILE
/** Profile TX/RX timing */
enum {
    // start of bl_upload_hdl
    RX_HDL_START = 0,
    // first round of port agg and skb alloced finished
    RX_HDL_ALLOC = 1,
    // second round of port agg and skb alloced finished
    RX_HDL_ALLOC2 = 2,
    // cmd53 read finished
    RX_HDL_READ = 3,
    // queue rx_work finished
    RX_HDL_QUEUE = 4,
    // end of bl_upload_hdl
    RX_HDL_END = 5,

    // time diff between twice alloc finished
    RX_HDL_TWICE_DIFF = 6,
    // total time frome rx_hdl_start to rx_hdl_end
    RX_HDL_DUR_ALL    = 7,
    // time diff between twice hdl_start
    RX_HDL_PERIOD     = 8,
    // time diff frome previous rx_hdl_end to next tx_hdl_start
    RX_HDL_GAP        = 9,
    // time diff between rx irq to rx hdl
    RX_HDL_IRQ_BOTTOM = 10,
    // max number of rec item
    RX_HDL_MAX        = 11,
};

struct bl_trx_profile {
    bool enable;

    u32 rx_irq_cnt;
    u32 rx_port_total;
    u32 rx_port_avg;
    ktime_t rx_irq_prev_ts;
    ktime_t rx_irq_max_interval;
    ktime_t rx_hdl_ts[RX_HDL_MAX];

    u32 tx_irq_cnt;
    ktime_t tx_irq_prev_ts;
    ktime_t tx_irq_max_interval;
};
#endif

/** 2K buf size */
#define BL_TX_DATA_BUF_SIZE_16K        16*1024
#define BL_RX_DATA_BUF_SIZE_16K        32*1024

#define BL_SDIO_MP_AGGR_PKT_LIMIT_MAX  8
#define BL_SDIO_MPA_ADDR_BASE                  0x1000

typedef struct _sdio_mpa_tx {
               u8 *buf_ptr;
               u8 *buf;
               u32 buf_len;
               u32 pkt_cnt;
               u32 ports;
               u16 start_port;
               u16 mp_wr_info[BL_SDIO_MP_AGGR_PKT_LIMIT_MAX];
} sdio_mpa_tx;

typedef struct _sdio_mpa_rx {
               u8 *buf_ptr;
               u8 *buf;
               u32 buf_len;
               u32 pkt_cnt;
               u32 ports;
               u16 start_port;
               struct sk_buff *buf_arr[BL_SDIO_MP_AGGR_PKT_LIMIT_MAX];
               u32 len_arr[BL_SDIO_MP_AGGR_PKT_LIMIT_MAX];
               u16 mp_rd_info[BL_SDIO_MP_AGGR_PKT_LIMIT_MAX];
               u16 mp_rd_port[BL_SDIO_MP_AGGR_PKT_LIMIT_MAX];
} sdio_mpa_rx;

#if defined  BL_RX_REORDER  && (defined CONFIG_BL_USB || defined CONFIG_BL_SDIO)
/** MAX sequence value 2^12 = 4096 */
#define MAX_SEQ_VALUE   (1 << 12)
/** 2^11 = 2048 */
#define TWOPOW11 (1 << 11)
#endif

struct iwpriv_var {
    uint8_t *iwpriv_ind;
    uint32_t iwpriv_ind_len;
    uint32_t tx_duty;
};

struct bl_iface_tbl {
    bool    valid;
    u8      type;
    char    name[8];
    void *  param;
};

// max private scan req/rsp ie len
#define MAX_PRI_IE_LEN 600
// max private bcn ie len
#define MAX_PRI_BCN_IE_LEN 255

struct bl_priv_scan {
    bool   prob_req_en;
    u8   ie_type;
    u16  ie_len;
    u8   chan_cnt;
    u8   chan_list[14];
    u8   ie_buf[MAX_PRI_IE_LEN];
};

struct bl_priv_bcn_ie {
    bool bcn_ie_en;
    u16  bcn_ie_len;
    u8   bcn_ie_buf[MAX_PRI_BCN_IE_LEN];
};

#ifdef CONFIG_BL_BTSDU
struct btsdio_data {
    struct hci_dev   *hdev;
    struct sdio_func *func;

    struct work_struct work;

    struct sk_buff_head txq;

    spinlock_t hci_cmd_lock;

    struct bl_hw *bl_hw;
};
#endif

/**
 * struct bl_hw - BL driver main data
 *
 * @dev: Device structure
 *
 * @plat: Underlying BL platform information
 * @phy: PHY Configuration
 * @version_cfm: MACSW/HW versions (as obtained via MM_VERSION_REQ)
 * @machw_type: Type of MACHW (see BL_MACHW_xxx)
 *
 * @mod_params: Driver parameters
 * @flags: Global flags, see enum bl_dev_flag
 * @task: Tasklet to execute firmware IRQ bottom half
 * @wiphy: Wiphy structure
 * @ext_capa: extended capabilities supported
 *
 * @vifs: List of VIFs currently created
 * @vif_table: Table of all possible VIFs, indexed with fw id.
 * (NX_REMOTE_STA_MAX extra elements for AP_VLAN interface)
 * @vif_started: Number of vif created at firmware level
 * @avail_idx_map: Bitfield of created interface (indexed by bl_vif.drv_vif_index)
 * @monitor_vif:  FW id of the monitor interface (BL_INVALID_VIF if no monitor vif)
 *
 * @sta_table: Table of all possible Stations
 * (NX_VIRT_DEV_MAX] extra elements for BroadCast/MultiCast stations)
 *
 * @chanctx_table: Table of all possible Channel contexts
 * @cur_chanctx: Currently active Channel context
 * @survey: Table of channel surveys
 * @roc: Information of current Remain on Channel request (NULL if Roc requested)
 * @roc_cookie: Counter used to identify RoC request
 * @scan_request: Information of current scan request
 * @radar: Radar detection information
 *
 * @tx_lock: Spinlock to protect TX path
 * @txq: Table of STA/TID TX queues
 * @hwq: Table of MACHW queues
 * @txq_cleanup: Timer list to drop packet queued too long in TXQs
 * @sw_txhdr_cache: Cache to allocate
 * @tcp_pacing_shift: TCP buffering configuration (buffering is ~ 2^(10-tps) ms)
 * @mu: Information for Multiuser TX groups
 *
 * @defer_rx: Work to defer RX processing out of IRQ tasklet. Only use for specific mgmt frames
 *
 * @ipc_env: IPC environment
 * @cmd_mgr: FW command manager
 * @cb_lock: Spinlock to protect code from FW confirmation/indication message
 *
 * @e2amsgs_pool: Pool of shared buffers to retrieve FW messages
 * @dbgmsgs_pool: Pool of shared buffers to retrieve FW dbg messages
 * @e2aradars_pool: Pool of shared buffers to retrieve FW radar events
 * @pattern_elem: Shared buffer for the FW to download end of TX buf pattern from
 * @dbgdump_elem: Shared buffer to retrieve FW debug dump
 * @e2arxdesc_pool: Pool of shared buffers to retrieve RX descriptors
 * @rxbuf_elems: Table of shared buffer s to retrieve RX data
 * @e2aunsuprxvec_elems: Table of shared buffers to retrieve FW unsupported frames
 * @scan_ie: Shared buffer, allocated to push probe request elements to the FW
 *
 * @debugfs: Debug FS entries
 * @stats: global statistics
 */
struct bl_hw {
    struct device *dev;
    u8 surprise_removed;

    // Hardware info
    struct bl_plat *plat;
    struct bl_phy_info phy;
    struct mm_version_cfm version_cfm;
    int machw_type;

    // Global wifi config
    struct bl_mod_params *mod_params;
    unsigned long flags;
    struct wiphy *wiphy;
    u8 ext_capa[10];

    // VIFs
    struct list_head vifs;
    struct bl_vif *vif_table[NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX];
    u8 vif_started;
    u8 avail_idx_map;
    u8 monitor_vif;

    // Stations
    struct bl_sta sta_table[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX];

    // Channels
    struct bl_chanctx chanctx_table[NX_CHAN_CTXT_CNT];
    u8 cur_chanctx;
    struct bl_survey_info survey[SCAN_CHANNEL_MAX];
    struct bl_roc *roc;
    u32 roc_cookie;
    struct cfg80211_scan_request *scan_request;
    
    #ifdef CONFIG_BL_RADAR
    struct bl_radar radar;
    #endif

    // TX path
    spinlock_t tx_lock;
    spinlock_t main_proc_lock;
    spinlock_t int_lock;
//    spinlock_t cmd_lock;
//    spinlock_t resend_lock;
//    spinlock_t txq_lock;
//    spinlock_t rd_int_lock;
    struct mutex mutex;                         /* per-device perimeter lock */
    struct semaphore sem;
    u8 bl_processing;
    u8 more_task_flag;
    u8 lost_int_flag;
    //for sdio watchdog update wr_bitmap control
    u8 data_sent;
    //for sdio watchdog update rd_bitmap control
    u8 data_recv;
    u32 cmd_sent;
    //for fw recovery flag
    u8 recovery_flag;

    struct iwpriv_var iwp_var;

    spinlock_t rx_process_lock;
    struct work_struct rx_work;
    struct work_struct rx_flush_work;
    struct workqueue_struct *rx_workqueue;
    u32  rx_processing;
    u32  more_rx_task_flag;
    struct sk_buff_head rx_pkt_list;
    u32  rx_pkt_cnt;
    u8 flush_rx;
    u8 rx_work_flag;

    // SDIO IRQ watchdog
#ifdef CONFIG_BL_SDIO
    struct timer_list timer;
    struct completion watchdog_wait;
    struct task_struct *watchdog_task;
    bool watchdog_active;
    u32  int_cnt;
    u32  last_int_cnt;
#endif

#ifdef CONFIG_PRIV_CH_SWITCH
    struct delayed_work ch_switch_work;
    struct bl_vif *ch_switch_ap_vif;
    struct bl_vif *ch_switch_sta_vif;
    struct ieee80211_channel *ch_switch_sta_chan;
    struct cfg80211_chan_def ch_switch_sta_chan_def;
#endif
    
    struct bl_txq txq[NX_NB_TXQ];
    struct bl_hwq hwq[NX_TXQ_CNT];
    struct timer_list txq_cleanup;
    struct kmem_cache *sw_txhdr_cache;
    struct kmem_cache      *agg_reodr_pkt_cache;
    sdio_mpa_tx mpa_tx_data;
    sdio_mpa_rx mpa_rx_data;
    u32 tcp_pacing_shift;
#ifdef CONFIG_BL_MUMIMO_TX
    struct bl_mu_info mu;
#endif

    // RX path
    struct bl_defer_rx defer_rx;

    // IRQ
    struct tasklet_struct task;

    struct work_struct main_work;
    struct workqueue_struct *workqueue;
    u32 int_status;

    // IPC
    struct ipc_host_env_tag *ipc_env;
    struct bl_cmd_mgr cmd_mgr;
    spinlock_t cb_lock;

    // Shared buffers
    struct bl_ipc_dbgdump_elem dbgdump_elem;
    struct bl_ipc_elem_var scan_ie;

    // Debug FS and stats
#ifdef CONFIG_BL_DEBUGFS
    struct bl_debugfs debugfs;
#endif
    struct bl_stats stats;
#ifdef BL_RX_REORDER
    struct rxreorder_list  rx_reorder[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX][NX_NB_TID_PER_STA];
#else
    struct list_head reorder_list[NX_REMOTE_STA_MAX*NX_NB_TID_PER_STA];
#endif

#ifdef BL_BUS_LOOPBACK
    ploopbackdata ploopback;
#endif
    struct sk_buff_head transmitted_list[NX_NB_TXQ];
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
    struct ieee80211_sta_he_cap he_cap;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
    struct ieee80211_sta_vht_cap vht_cap;
#endif

    struct list_head tcp_stream_list;
    spinlock_t tcp_ack_lock;
#ifdef BL_TRX_PROFILE
    struct bl_trx_profile trx_profile;
#endif
    struct bl_priv_scan priv_scan;
    struct bl_priv_bcn_ie priv_bcn;

    struct bl_arp_table rpt_arp_table[BL_RPT_ARP_TABLE_SIZE];
    /* nl socket for event bcst */
    struct sock * netlink_sock;
    u32  netlink_sock_num;

#ifdef CONFIG_WIRELESS_EXT    
    /** IW statistics */
    struct iw_statistics iw_stats;
#endif    
    u8 drv_ready;

    /* country code */    
    u8 country_code[3];
    /* ble power */
    u8 ble_pwr;
    /* bt power */
    u8 bt_pwr[3];

    #ifdef CONFIG_BL_BTSDU
    struct btsdio_data btsdio_data;
    #endif

    struct hci_dev *hdev;
    uint8_t bl_hci_on;

#ifdef CONFIG_KE_TASKLET
    struct tasklet_struct ke_tasklet;
#else
    struct task_struct *ke_task;
    struct completion ke_wait;
#endif
    struct timer_list ke_timer;
    bool ke_timer_active;
    spinlock_t ke_queue_lock;
    spinlock_t ke_mem_lock;
};

#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
extern struct bl_hw * bl_hws[];
extern u32 bl_hws_num;
#endif

u8 *bl_build_bcn(struct bl_hw *bl_hw, struct bl_bcn *bcn, struct cfg80211_beacon_data *new);

void bl_chanctx_link(struct bl_vif *vif, u8 idx,
                        struct cfg80211_chan_def *chandef);
void bl_chanctx_unlink(struct bl_vif *vif);
int  bl_chanctx_valid(struct bl_hw *bl_hw, u8 idx);

static inline bool is_multicast_sta(int sta_idx)
{
    return (sta_idx >= NX_REMOTE_STA_MAX);
}
struct bl_sta *bl_get_sta(struct bl_hw *bl_hw, const u8 *mac_addr);

static inline uint8_t master_vif_idx(struct bl_vif *vif)
{
    if (unlikely(vif->wdev.iftype == NL80211_IFTYPE_AP_VLAN)) {
        return vif->ap_vlan.master->vif_index;
    } else {
        return vif->vif_index;
    }
}
#ifdef CONFIG_BL_DEBUGFS
static inline void *bl_get_shared_trace_buf(struct bl_hw *bl_hw)
{
    return (void *)&(bl_hw->debugfs.fw_trace.buf);
}
#endif
void bl_external_auth_enable(struct bl_vif *vif);
void bl_external_auth_disable(struct bl_vif *vif);

#endif /* _BL_DEFS_H_ */
