/**
 ******************************************************************************
 *
 *  @file bl_main.c
 *
 *  @brief Entry point of the BL driver
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
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <linux/etherdevice.h>
#include <linux/signal.h>
#include <linux/ctype.h>
#include <linux/if_arp.h>

#include "bl_defs.h"
#include "bl_msg_tx.h"
#include "bl_tx.h"
#include "bl_hal_desc.h"
#include "bl_irqs.h"
#ifdef CONFIG_BL_DEBUGFS
#include "bl_debugfs.h"
#endif
#include "bl_cfgfile.h"
#if defined CONFIG_BL_SDIO
#include "bl_sdio.h"
#elif defined CONFIG_BL_USB
#include "bl_usb.h"
#endif
#include "bl_radar.h"
#include "bl_version.h"
#ifdef CONFIG_BL_BFMER
#include "bl_bfmer.h"
#endif //(CONFIG_BL_BFMER)
#include "bl_tdls.h"
#include "bl_compat.h"
#include "bl_iwpriv.h"
#include "bl_nl_events.h"

#include "softmac.h"

#define BL_DRV_DESCRIPTION  "Bouffalolab WiFi driver for Linux cfg80211"
#define BL_DRV_COPYRIGHT    "Copyright(c) 2017-2023 Bouffalolab"
#define BL_DRV_AUTHOR       "Bouffalolab"

#define BL_PRINT_CFM_ERR(req) \
        printk(KERN_CRIT "%s: Status Error(%d)\n", #req, (&req##_cfm)->status)

#define BL_HT_CAPABILITIES                                    \
{                                                               \
    .ht_supported   = true,                                     \
    .cap            = 0,                                        \
    .ampdu_factor   = IEEE80211_HT_MAX_AMPDU_64K,               \
    .ampdu_density  = IEEE80211_HT_MPDU_DENSITY_16,             \
    .mcs        = {                                             \
        .rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },        \
        .rx_highest = cpu_to_le16(65),                          \
        .tx_params = IEEE80211_HT_MCS_TX_DEFINED,               \
    },                                                          \
}

#define BL_VHT_CAPABILITIES                                   \
{                                                               \
    .vht_supported = false,                                     \
    .cap       =                                                \
      (7 << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT),\
    .vht_mcs       = {                                          \
        .rx_mcs_map = cpu_to_le16(                              \
                      IEEE80211_VHT_MCS_SUPPORT_0_9    << 0  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 2  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 4  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 6  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 8  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 10 |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 12 |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 14),  \
        .tx_mcs_map = cpu_to_le16(                              \
                      IEEE80211_VHT_MCS_SUPPORT_0_9    << 0  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 2  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 4  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 6  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 8  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 10 |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 12 |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 14),  \
    }                                                           \
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
#define BL_HE_CAPABILITIES                                    \
{                                                               \
    .has_he = false,                                            \
    .he_cap_elem = {                                            \
        .mac_cap_info[0] = 0,                                   \
        .mac_cap_info[1] = 0,                                   \
        .mac_cap_info[2] = 0,                                   \
        .mac_cap_info[3] = 0,                                   \
        .mac_cap_info[4] = 0,                                   \
        .phy_cap_info[0] = 0,                                   \
        .phy_cap_info[1] = 0,                                   \
        .phy_cap_info[2] = 0,                                   \
        .phy_cap_info[3] = 0,                                   \
        .phy_cap_info[4] = 0,                                   \
        .phy_cap_info[5] = 0,                                   \
        .phy_cap_info[6] = 0,                                   \
        .phy_cap_info[7] = 0,                                   \
        .phy_cap_info[8] = 0,                                   \
    },                                                          \
    .he_mcs_nss_supp = {                                        \
        .rx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .tx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .rx_mcs_160 = cpu_to_le16(0xffff),                      \
        .tx_mcs_160 = cpu_to_le16(0xffff),                      \
        .rx_mcs_80p80 = cpu_to_le16(0xffff),                    \
        .tx_mcs_80p80 = cpu_to_le16(0xffff),                    \
    },                                                          \
    .ppe_thres = {0x08, 0x1c, 0x07},                            \
}
#else
#define BL_HE_CAPABILITIES                                    \
{                                                               \
    .has_he = false,                                            \
    .he_cap_elem = {                                            \
        .mac_cap_info[0] = 0,                                   \
        .mac_cap_info[1] = 0,                                   \
        .mac_cap_info[2] = 0,                                   \
        .mac_cap_info[3] = 0,                                   \
        .mac_cap_info[4] = 0,                                   \
        .mac_cap_info[5] = 0,                                   \
        .phy_cap_info[0] = 0,                                   \
        .phy_cap_info[1] = 0,                                   \
        .phy_cap_info[2] = 0,                                   \
        .phy_cap_info[3] = 0,                                   \
        .phy_cap_info[4] = 0,                                   \
        .phy_cap_info[5] = 0,                                   \
        .phy_cap_info[6] = 0,                                   \
        .phy_cap_info[7] = 0,                                   \
        .phy_cap_info[8] = 0,                                   \
        .phy_cap_info[9] = 0,                                   \
        .phy_cap_info[10] = 0,                                  \
    },                                                          \
    .he_mcs_nss_supp = {                                        \
        .rx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .tx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .rx_mcs_160 = cpu_to_le16(0xffff),                      \
        .tx_mcs_160 = cpu_to_le16(0xffff),                      \
        .rx_mcs_80p80 = cpu_to_le16(0xffff),                    \
        .tx_mcs_80p80 = cpu_to_le16(0xffff),                    \
    },                                                          \
    .ppe_thres = {0x08, 0x1c, 0x07},                            \
}
#endif
#endif

#define RATE(_bitrate, _hw_rate, _flags) {      \
    .bitrate    = (_bitrate),                   \
    .flags      = (_flags),                     \
    .hw_value   = (_hw_rate),                   \
}

#define CHAN(_freq) {                           \
    .center_freq    = (_freq),                  \
    .max_power  = 30, /* FIXME */               \
}

static struct ieee80211_rate bl_ratetable[] = {
    RATE(10,  0x00, 0),
    RATE(20,  0x01, IEEE80211_RATE_SHORT_PREAMBLE),
    RATE(55,  0x02, IEEE80211_RATE_SHORT_PREAMBLE),
    RATE(110, 0x03, IEEE80211_RATE_SHORT_PREAMBLE),
    RATE(60,  0x04, 0),
    RATE(90,  0x05, 0),
    RATE(120, 0x06, 0),
    RATE(180, 0x07, 0),
    RATE(240, 0x08, 0),
    RATE(360, 0x09, 0),
    RATE(480, 0x0A, 0),
    RATE(540, 0x0B, 0),
};

/* The channels indexes here are not used anymore */
static struct ieee80211_channel bl_2ghz_channels[] = {
    CHAN(2412),
    CHAN(2417),
    CHAN(2422),
    CHAN(2427),
    CHAN(2432),
    CHAN(2437),
    CHAN(2442),
    CHAN(2447),
    CHAN(2452),
    CHAN(2457),
    CHAN(2462),
    CHAN(2467),
    CHAN(2472),
    CHAN(2484),
    // Extra channels defined only to be used for PHY measures.
    // Enabled only if custregd and custchan parameters are set
    CHAN(2390),
    CHAN(2400),
    CHAN(2410),
    CHAN(2420),
    CHAN(2430),
    CHAN(2440),
    CHAN(2450),
    CHAN(2460),
    CHAN(2470),
    CHAN(2480),
    CHAN(2490),
    CHAN(2500),
    CHAN(2510),
};

#if 0
static struct ieee80211_channel bl_5ghz_channels[] = {
    CHAN(5180),             // 36 -   20MHz
    CHAN(5200),             // 40 -   20MHz
    CHAN(5220),             // 44 -   20MHz
    CHAN(5240),             // 48 -   20MHz
    CHAN(5260),             // 52 -   20MHz
    CHAN(5280),             // 56 -   20MHz
    CHAN(5300),             // 60 -   20MHz
    CHAN(5320),             // 64 -   20MHz
    CHAN(5500),             // 100 -  20MHz
    CHAN(5520),             // 104 -  20MHz
    CHAN(5540),             // 108 -  20MHz
    CHAN(5560),             // 112 -  20MHz
    CHAN(5580),             // 116 -  20MHz
    CHAN(5600),             // 120 -  20MHz
    CHAN(5620),             // 124 -  20MHz
    CHAN(5640),             // 128 -  20MHz
    CHAN(5660),             // 132 -  20MHz
    CHAN(5680),             // 136 -  20MHz
    CHAN(5700),             // 140 -  20MHz
    CHAN(5720),             // 144 -  20MHz
    CHAN(5745),             // 149 -  20MHz
    CHAN(5765),             // 153 -  20MHz
    CHAN(5785),             // 157 -  20MHz
    CHAN(5805),             // 161 -  20MHz
    CHAN(5825),             // 165 -  20MHz
    // Extra channels defined only to be used for PHY measures.
    // Enabled only if custregd and custchan parameters are set
    CHAN(5190),
    CHAN(5210),
    CHAN(5230),
    CHAN(5250),
    CHAN(5270),
    CHAN(5290),
    CHAN(5310),
    CHAN(5330),
    CHAN(5340),
    CHAN(5350),
    CHAN(5360),
    CHAN(5370),
    CHAN(5380),
    CHAN(5390),
    CHAN(5400),
    CHAN(5410),
    CHAN(5420),
    CHAN(5430),
    CHAN(5440),
    CHAN(5450),
    CHAN(5460),
    CHAN(5470),
    CHAN(5480),
    CHAN(5490),
    CHAN(5510),
    CHAN(5530),
    CHAN(5550),
    CHAN(5570),
    CHAN(5590),
    CHAN(5610),
    CHAN(5630),
    CHAN(5650),
    CHAN(5670),
    CHAN(5690),
    CHAN(5710),
    CHAN(5730),
    CHAN(5750),
    CHAN(5760),
    CHAN(5770),
    CHAN(5780),
    CHAN(5790),
    CHAN(5800),
    CHAN(5810),
    CHAN(5820),
    CHAN(5830),
    CHAN(5840),
    CHAN(5850),
    CHAN(5860),
    CHAN(5870),
    CHAN(5880),
    CHAN(5890),
    CHAN(5900),
    CHAN(5910),
    CHAN(5920),
    CHAN(5930),
    CHAN(5940),
    CHAN(5950),
    CHAN(5960),
    CHAN(5970),
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
static struct ieee80211_sband_iftype_data bl_he_capa = {
    .types_mask = BIT(NL80211_IFTYPE_STATION | NL80211_IFTYPE_AP),
    .he_cap = BL_HE_CAPABILITIES,
};
#endif

static struct ieee80211_supported_band bl_band_2GHz = {
    .channels   = bl_2ghz_channels,
    .n_channels = ARRAY_SIZE(bl_2ghz_channels) - 13, // -13 to exclude extra channels
    .bitrates   = bl_ratetable,
    .n_bitrates = ARRAY_SIZE(bl_ratetable),
    .ht_cap     = BL_HT_CAPABILITIES,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    .iftype_data = &bl_he_capa,
    .n_iftype_data = 1,
#endif
};
#if 0
static struct ieee80211_supported_band bl_band_5GHz = {
    .channels   = bl_5ghz_channels,
    .n_channels = ARRAY_SIZE(bl_5ghz_channels) - 59, // -59 to exclude extra channels
    .bitrates   = &bl_ratetable[4],
    .n_bitrates = ARRAY_SIZE(bl_ratetable) - 4,
    .ht_cap     = BL_HT_CAPABILITIES,
    .vht_cap    = BL_VHT_CAPABILITIES,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    .iftype_data = &bl_he_capa,
    .n_iftype_data = 1,
#endif
};
#endif
static struct ieee80211_iface_limit bl_limits[] = {
    { .max = NX_VIRT_DEV_MAX, .types = BIT(NL80211_IFTYPE_AP) |
                                       BIT(NL80211_IFTYPE_STATION)}
};

static struct ieee80211_iface_limit bl_limits_dfs[] = {
    { .max = NX_VIRT_DEV_MAX, .types = BIT(NL80211_IFTYPE_AP)}
};

static const struct ieee80211_iface_combination bl_combinations[] = {
    {
        .limits                 = bl_limits,
        .n_limits               = ARRAY_SIZE(bl_limits),
        .num_different_channels = NX_CHAN_CTXT_CNT,
        .max_interfaces         = NX_VIRT_DEV_MAX,
    },
    /* Keep this combination as the last one */
    {
        .limits                 = bl_limits_dfs,
        .n_limits               = ARRAY_SIZE(bl_limits_dfs),
        .num_different_channels = 1,
        .max_interfaces         = NX_VIRT_DEV_MAX,
        .radar_detect_widths = (BIT(NL80211_CHAN_WIDTH_20_NOHT) |
                                BIT(NL80211_CHAN_WIDTH_20) |
                                BIT(NL80211_CHAN_WIDTH_40) |
                                BIT(NL80211_CHAN_WIDTH_80)),
    }
};

/* There isn't a lot of sense in it, but you can transmit anything you like */
static struct ieee80211_txrx_stypes
bl_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
    [NL80211_IFTYPE_STATION] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4)),
    },
    [NL80211_IFTYPE_AP] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_AP_VLAN] = {
        /* copy AP */
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_P2P_CLIENT] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
    },
    [NL80211_IFTYPE_P2P_GO] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_P2P_DEVICE] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
    },
    [NL80211_IFTYPE_MESH_POINT] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4)),
    },
};


static u32 cipher_suites[] = {
    WLAN_CIPHER_SUITE_WEP40,
    WLAN_CIPHER_SUITE_WEP104,
    WLAN_CIPHER_SUITE_TKIP,
    WLAN_CIPHER_SUITE_CCMP,
    0, // reserved entries to enable AES-CMAC, GCMP-128/256, CCMP-256, SMS4
    0,
    0,
    0,
    0,
};

#define NB_RESERVED_CIPHER 5;

static const int bl_ac2hwq[1][NL80211_NUM_ACS] = {
    {
        [NL80211_TXQ_Q_VO] = BL_HWQ_VO,
        [NL80211_TXQ_Q_VI] = BL_HWQ_VI,
        [NL80211_TXQ_Q_BE] = BL_HWQ_BE,
        [NL80211_TXQ_Q_BK] = BL_HWQ_BK
    }
};

const int bl_tid2hwq[IEEE80211_NUM_TIDS] = {
    BL_HWQ_BE,
    BL_HWQ_BK,
    BL_HWQ_BK,
    BL_HWQ_BE,
    BL_HWQ_VI,
    BL_HWQ_VI,
    BL_HWQ_VO,
    BL_HWQ_VO,
    /* TID_8 is used for management frames */
    BL_HWQ_VO,
    /* At the moment, all others TID are mapped to BE */
    BL_HWQ_BE,
    BL_HWQ_BE,
    BL_HWQ_BE,
    BL_HWQ_BE,
    BL_HWQ_BE,
    BL_HWQ_BE,
    BL_HWQ_BE,
};

static const int bl_hwq2uapsd[NL80211_NUM_ACS] = {
    [BL_HWQ_VO] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VO,
    [BL_HWQ_VI] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VI,
    [BL_HWQ_BE] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BE,
    [BL_HWQ_BK] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BK,
};


#ifdef CONFIG_BL_DEBUGFS
extern void bl_um_helper_work(struct work_struct *ws);
#endif

/*********************************************************************
 * helper
 *********************************************************************/
struct bl_sta *bl_get_sta(struct bl_hw *bl_hw, const u8 *mac_addr)
{
    int i;

    for (i = 0; i < NX_REMOTE_STA_MAX; i++) {
        struct bl_sta *sta = &bl_hw->sta_table[i];
        if (sta->valid && (memcmp(mac_addr, &sta->mac_addr, 6) == 0))
            return sta;
    }

    return NULL;
}

void bl_enable_wapi(struct bl_hw *bl_hw)
{
    cipher_suites[bl_hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_SMS4;
    bl_hw->wiphy->n_cipher_suites ++;
    bl_hw->wiphy->flags |= WIPHY_FLAG_CONTROL_PORT_PROTOCOL;
}

void bl_enable_mfp(struct bl_hw *bl_hw)
{
    cipher_suites[bl_hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_AES_CMAC;
    bl_hw->wiphy->n_cipher_suites ++;
}

void bl_enable_gcmp(struct bl_hw *bl_hw)
{
    // Assume that HW supports CCMP-256 if it supports GCMP
    cipher_suites[bl_hw->wiphy->n_cipher_suites++] = WLAN_CIPHER_SUITE_CCMP_256;
    cipher_suites[bl_hw->wiphy->n_cipher_suites++] = WLAN_CIPHER_SUITE_GCMP;
    cipher_suites[bl_hw->wiphy->n_cipher_suites++] = WLAN_CIPHER_SUITE_GCMP_256;
}

u8 *bl_build_bcn(struct bl_hw *bl_hw, struct bl_bcn *bcn, 
                     struct cfg80211_beacon_data *new)
{
    u8 *buf, *pos;
    u8 opmode[3] = {0xc7,0x01,0x01};
    
    if (new->head) {
        u8 *head = kmalloc(new->head_len, GFP_KERNEL);

        if (!head)
            return NULL;

        if (bcn->head)
            kfree(bcn->head);

        bcn->head = head;
        bcn->head_len = new->head_len;
        memcpy(bcn->head, new->head, new->head_len);
    }
    if (new->tail) {
        u8 *tail;
        
        if (bl_hw->mod_params->he_on)
            tail = kmalloc(new->tail_len + 3, GFP_KERNEL);
        else
            tail = kmalloc(new->tail_len, GFP_KERNEL);

        if (!tail)
            return NULL;

        if (bcn->tail)
            kfree(bcn->tail);

        bcn->tail = tail;
        bcn->tail_len = new->tail_len;
        memcpy(bcn->tail, new->tail, new->tail_len);
        
        if (bl_hw->mod_params->he_on) {
            memcpy(bcn->tail + new->tail_len, opmode, 3);
            bcn->tail_len +=3;
        }
    }

    if (!bcn->head)
        return NULL;

    bcn->tim_len = 6;
    bcn->len = bcn->head_len + bcn->tail_len + bcn->ies_len + bcn->tim_len;

    if (bl_hw->priv_bcn.bcn_ie_en)
        bcn->len += bl_hw->priv_bcn.bcn_ie_len;

    BL_DBG("%s bcn_Len=%d, head_len=%d, tail_len=%d, ies_len=%d, tim_len=%d priv_bcn_len=%d\n",
           __func__, bcn->len, bcn->head_len,bcn->tail_len, bcn->ies_len, 
           bcn->tim_len, bl_hw->priv_bcn.bcn_ie_len);

    buf = kmalloc(bcn->len, GFP_KERNEL);
    if (!buf)
        return NULL;

    // Build the beacon buffer
    pos = buf;
    memcpy(pos, bcn->head, bcn->head_len);
    pos += bcn->head_len;
    *pos++ = WLAN_EID_TIM;
    *pos++ = 4;
    *pos++ = 0;
    *pos++ = bcn->dtim;
    *pos++ = 0;
    *pos++ = 0;
    if (bcn->tail) {
        memcpy(pos, bcn->tail, bcn->tail_len);
        pos += bcn->tail_len;
    }
    if (bcn->ies) {
        memcpy(pos, bcn->ies, bcn->ies_len);
        pos += bcn->ies_len;
    }

    if (bl_hw->priv_bcn.bcn_ie_en) {
        memcpy(pos, bl_hw->priv_bcn.bcn_ie_buf, bl_hw->priv_bcn.bcn_ie_len);
    }

    return buf;
}


u8 *bl_build_priv_csa_bcn(struct bl_hw *bl_hw, struct bl_bcn *bcn, 
                                 u8 op_class, u16 *csa_oft, u8 channel_number)
{
    u8 *buf, *pos;
    
    if (bcn->head == NULL)
        return NULL;
        
    bcn->len = bcn->head_len + bcn->tail_len + bcn->ies_len + bcn->tim_len;

    if (bl_hw->priv_bcn.bcn_ie_en)
        bcn->len += bl_hw->priv_bcn.bcn_ie_len;

    BL_DBG("%s bcn_Len=%d, head_len=%d, tail_len=%d, ies_len=%d, tim_len=%d priv_bcn_len=%d, csa_len:%d\n",
           __func__, bcn->len, bcn->head_len, bcn->tail_len, bcn->ies_len, 
           bcn->tim_len, bl_hw->priv_bcn.bcn_ie_len, bcn->csa_len);

    buf = kmalloc(bcn->len + bcn->csa_len, GFP_KERNEL);
    if (!buf)
        return NULL;

    // Build the beacon buffer
    pos = buf;
    memcpy(pos, bcn->head, bcn->head_len);
    pos += bcn->head_len;
    
    *pos++ = WLAN_EID_TIM;
    *pos++ = 4;
    *pos++ = 0;
    *pos++ = bcn->dtim;
    *pos++ = 0;
    *pos++ = 0;

    *pos++ = WLAN_EID_CHANNEL_SWITCH;
    *pos++ = 0x03;  //len
    *pos++ = 0x00;  //channel switch mode
    *pos++ = channel_number;  //new channel number
    *pos++ = 0x02;  //channel switch count

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 15, 0)
    *pos++ = WLAN_EID_EXT_CHANSWITCH_ANN;
    *pos++ = 0x04;  //len
    *pos++ = 0x00;  //channel switch mode
    *pos++ = op_class;  //op class
    *pos++ = channel_number;  //new channel number
    *pos++ = 0x02;  //channel switch count
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 15, 0)
    csa_oft[0] = 4 + bcn->head_len + bcn->tim_len;
#else
    csa_oft[0] = 4 + bcn->head_len + bcn->tim_len;
    csa_oft[1] = 10 + bcn->head_len + bcn->tim_len;
#endif
    BL_DBG("%s, csa oft:%d %d, csa counter:%d %d\r\n",
           __func__, csa_oft[0], csa_oft[1], buf[csa_oft[0]], buf[csa_oft[1]]);
        
    if (bcn->tail) {
        memcpy(pos, bcn->tail, bcn->tail_len);
        pos += bcn->tail_len;
    }
    
    if (bcn->ies) {
        memcpy(pos, bcn->ies, bcn->ies_len);
        pos += bcn->ies_len;
    }

    if (bl_hw->priv_bcn.bcn_ie_en) {
        memcpy(pos, bl_hw->priv_bcn.bcn_ie_buf, bl_hw->priv_bcn.bcn_ie_len);
    }

    return buf;
}

u8 *bl_build_priv_after_csa_bcn(struct bl_hw *bl_hw, struct bl_bcn *bcn, 
                                        struct bl_bcn *bcn_before)
{
    u8 *buf, *pos;

    if (bcn_before->head == NULL)
        return NULL;
        
    *bcn = *bcn_before;
    bcn->len = bcn->head_len + bcn->tail_len + bcn->ies_len + bcn->tim_len;
    bcn->head = NULL;
    bcn->tail = NULL;
    bcn->ies = NULL;
    
    if (bl_hw->priv_bcn.bcn_ie_en)
        bcn->len += bl_hw->priv_bcn.bcn_ie_len;

    BL_DBG("%s bcn_Len=%d, head_len=%d, tail_len=%d, ies_len=%d, tim_len=%d priv_bcn_len=%d\n",
            __func__, bcn->len, bcn->head_len, bcn->tail_len, bcn->ies_len, 
            bcn->tim_len, bl_hw->priv_bcn.bcn_ie_len);

    buf = kmalloc(bcn->len, GFP_KERNEL);
    if (!buf)
        return NULL;

    // Build the beacon buffer
    pos = buf;
    memcpy(pos, bcn_before->head, bcn->head_len);
    pos += bcn->head_len;
    
    *pos++ = WLAN_EID_TIM;
    *pos++ = 4;
    *pos++ = 0;
    *pos++ = bcn->dtim;
    *pos++ = 0;
    *pos++ = 0;

    if (bcn_before->tail) {
        memcpy(pos, bcn_before->tail, bcn->tail_len);
        pos += bcn->tail_len;
    }
    if (bcn->ies) {
        memcpy(pos, bcn->ies, bcn->ies_len);
        pos += bcn->ies_len;
    }

    if (bl_hw->priv_bcn.bcn_ie_en) {
        memcpy(pos, bl_hw->priv_bcn.bcn_ie_buf, bl_hw->priv_bcn.bcn_ie_len);
    }

    return buf;
}

static void bl_del_bcn(struct bl_bcn *bcn)
{
    if (bcn->head) {
        kfree(bcn->head);
        bcn->head = NULL;
    }
    bcn->head_len = 0;

    if (bcn->tail) {
        kfree(bcn->tail);
        bcn->tail = NULL;
    }
    bcn->tail_len = 0;

    if (bcn->ies) {
        kfree(bcn->ies);
        bcn->ies = NULL;
    }
    bcn->ies_len = 0;
    bcn->tim_len = 0;
    bcn->dtim = 0;
    bcn->len = 0;
}

/**
 * Link channel ctxt to a vif and thus increments count for this context.
 */
void bl_chanctx_link(struct bl_vif *vif, u8 ch_idx,
                          struct cfg80211_chan_def *chandef)
{
    struct bl_chanctx *ctxt;

    if (ch_idx >= NX_CHAN_CTXT_CNT) {
        WARN(1, "Invalid channel ctxt id %d", ch_idx);
        return;
    }

    vif->ch_index = ch_idx;
    ctxt = &vif->bl_hw->chanctx_table[ch_idx];
    ctxt->count++;

    // For now chandef is NULL for STATION interface
    if (chandef) {
        if (!ctxt->chan_def.chan)
            ctxt->chan_def = *chandef;
        else {
            // TODO. check that chandef is the same as the one already
            // set for this ctxt
        }
    }
}

/**
 * Unlink channel ctxt from a vif and thus decrements count for this context
 */
void bl_chanctx_unlink(struct bl_vif *vif)
{
    struct bl_chanctx *ctxt;

    if (vif->ch_index == BL_CH_NOT_SET)
        return;

    ctxt = &vif->bl_hw->chanctx_table[vif->ch_index];

    if (ctxt->count == 0) {
        WARN(1, "Chan ctxt ref count is already 0");
    } else {
        ctxt->count--;
    }

    if (ctxt->count == 0) {
        #ifdef CONFIG_BL_RADAR
        if (vif->ch_index == vif->bl_hw->cur_chanctx) {
            /* If current chan ctxt is no longer linked to a vif
               disable radar detection (no need to check if it was activated) */
            bl_radar_detection_enable(&vif->bl_hw->radar,
                                        BL_RADAR_DETECT_DISABLE,
                                        BL_RADAR_RIU);
        }
        #endif
        
        /* set chan to null, so that if this ctxt is relinked to a vif that
           don't have channel information, don't use wrong information */
        ctxt->chan_def.chan = NULL;
    }
    vif->ch_index = BL_CH_NOT_SET;
}

int bl_chanctx_valid(struct bl_hw *bl_hw, u8 ch_idx)
{
    if (ch_idx >= NX_CHAN_CTXT_CNT ||
        bl_hw->chanctx_table[ch_idx].chan_def.chan == NULL) {
        return 0;
    }

    return 1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
static void bl_del_csa(struct bl_vif *vif)
{
    struct bl_hw *bl_hw = vif->bl_hw;
    struct bl_csa *csa = vif->ap.csa;

    BL_DBG_MSG(BL_FN_ENTRY_STR);

    if (!csa)
        return;

    bl_ipc_elem_var_deallocs(bl_hw, &csa->elem);
    bl_del_bcn(&csa->bcn);
    kfree(csa);
    vif->ap.csa = NULL;
}

static void bl_csa_finish(struct work_struct *ws)
{
    struct bl_csa *csa = container_of(ws, struct bl_csa, work);
    struct bl_vif *vif = csa->vif;
    struct bl_hw *bl_hw = vif->bl_hw;
    int error = csa->status;
    const struct element_t *ie_elem = NULL;
    int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);

    BL_DBG(BL_FN_ENTRY_STR);

    BL_DBG_MSG("%s, vif->ap.csa:0x%p, bl_hw->mutex locked:%d\n", 
           __func__, vif->ap.csa, mutex_is_locked(&bl_hw->mutex));

    if (vif->ap.csa == NULL) {
        if (mutex_is_locked(&bl_hw->mutex))
            mutex_unlock(&bl_hw->mutex);

        return;
    }

    if (!error) {
        BL_DBG_MSG("%s, call bcn_change, csa:0x%p\n", __func__, csa);
        
        error = bl_send_bcn_change(bl_hw, vif->vif_index, csa->elem.addr,
                                   csa->bcn.len, csa->bcn.head_len,
                                   csa->bcn.tim_len, NULL);
        csa->bcn.len = 0;
        csa->bcn.tim_len = 0;
        csa->bcn.head_len = 0;
    }
    
    BL_DBG_MSG("%s, vif_idx:%d, csa:0x%p\n", 
               __func__, vif->vif_index, vif->ap.csa);

    if (error) {
        printk("%s change beacon fail, err:0x%x\n", __func__, error);
       
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
        cfg80211_stop_iface(bl_hw->wiphy, &vif->wdev, GFP_KERNEL);
#endif    
        bl_del_csa(vif);
    } else {
        BL_DBG("%s change beacon succ, csa:0x%p, status:%d, ch_idx:%d\n", 
               __func__, csa, csa->status, csa->ch_idx);

        if (vif->ap.bcn.head) {
            for_each_element_t(ie_elem, vif->ap.bcn.head+var_offset, vif->ap.bcn.head_len-var_offset) {
                //bl_print_elem((u8 *)ie_elem, ie_elem->datalen+2);
                if (ie_elem->id == WLAN_EID_DS_PARAMS) {
                    BL_DBG_MSG("%s, change vif bcn channel_number to %d\n", 
                               __func__, csa->channel_number);
                    *((u8 *)ie_elem + 2) = csa->channel_number;
                }
            }
        }

        spin_lock_bh(&bl_hw->cb_lock);
        bl_chanctx_unlink(vif);
        bl_chanctx_link(vif, csa->ch_idx, &csa->chandef);
        
        if (bl_hw->cur_chanctx == csa->ch_idx) {
            #ifdef CONFIG_BL_RADAR
            bl_radar_detection_enable_on_cur_channel(bl_hw);
            #endif
            
            bl_txq_vif_start(vif, BL_TXQ_STOP_CHAN, bl_hw);
        } else {
            bl_txq_vif_stop(vif, BL_TXQ_STOP_CHAN, bl_hw);
        }
        
        spin_unlock_bh(&bl_hw->cb_lock);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
        cfg80211_ch_switch_notify(vif->ndev, &csa->chandef, 0);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(6, 2, 16)
        cfg80211_ch_switch_notify(vif->ndev, &csa->chandef, 0, 0);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(5, 19, 1)
        cfg80211_ch_switch_notify(vif->ndev, &csa->chandef, 0);
#else
        cfg80211_ch_switch_notify(vif->ndev, &csa->chandef);
#endif

        bl_del_csa(vif);
        
        BL_DBG_MSG("vif->csa:0x%p\n", vif->ap.csa);
    }

    if (mutex_is_locked(&bl_hw->mutex))
        mutex_unlock(&bl_hw->mutex);
}
#endif

/**
 * bl_external_auth_enable - Enable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be enabled
 *
 * External authentication requires to start TXQ for unknown STA in
 * order to send auth frame pusehd by user space.
 * Note: It is assumed that fw is on the correct channel.
 */
void bl_external_auth_enable(struct bl_vif *vif)
{
    vif->sta.flags |= BL_STA_EXT_AUTH;
    bl_txq_unk_vif_init(vif);
    bl_txq_start(bl_txq_vif_get(vif, NX_UNK_TXQ_TYPE), 0);
}

/**
 * bl_external_auth_disable - Disable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be disabled
 */
void bl_external_auth_disable(struct bl_vif *vif)
{
    if (!(vif->sta.flags & BL_STA_EXT_AUTH))
        return;

    vif->sta.flags &= ~BL_STA_EXT_AUTH;
    bl_txq_unk_vif_deinit(vif);
}

#ifdef CONFIG_MESH
/**
 * bl_update_mesh_power_mode -
 *
 * @vif: mesh VIF  for which power mode is updated
 *
 * Does nothing if vif is not a mesh point interface.
 * Since firmware doesn't support one power save mode per link select the
 * most "active" power mode among all mesh links.
 * Indeed as soon as we have to be active on one link we might as well be
 * active on all links.
 *
 * If there is no link then the power mode for next peer is used;
 */
void bl_update_mesh_power_mode(struct bl_vif *vif)
{
    enum nl80211_mesh_power_mode mesh_pm;
    struct bl_sta *sta;
    struct mesh_config mesh_conf;
    struct mesh_update_cfm cfm;
    u32 mask;

    if (BL_VIF_TYPE(vif) != NL80211_IFTYPE_MESH_POINT)
        return;

    if (list_empty(&vif->ap.sta_list)) {
        mesh_pm = vif->ap.next_mesh_pm;
    } else {
        mesh_pm = NL80211_MESH_POWER_DEEP_SLEEP;
        list_for_each_entry(sta, &vif->ap.sta_list, list) {
            if (sta->valid && (sta->mesh_pm < mesh_pm)) {
                mesh_pm = sta->mesh_pm;
            }
        }
    }

    if (mesh_pm == vif->ap.mesh_pm)
        return;

    mask = BIT(NL80211_MESHCONF_POWER_MODE - 1);
    mesh_conf.power_mode = mesh_pm;
    if (bl_send_mesh_update_req(vif->bl_hw, vif, mask, &mesh_conf, &cfm) ||
        cfm.status)
        return;

    vif->ap.mesh_pm = mesh_pm;
}
#endif
/**
 * bl_save_assoc_ie_for_ft - Save association request elements if Fast
 * Transition has been configured.
 *
 * @vif: VIF that just connected
 * @sme: Connection info
 */
void bl_save_assoc_info_for_ft(struct bl_vif *vif,
                                      struct cfg80211_connect_params *sme)
{
    int ies_len = sme->ie_len + sme->ssid_len + 2;
    u8 *pos;

    if (!vif->sta.ft_assoc_ies) {
        if (!cfg80211_find_ie(WLAN_EID_MOBILITY_DOMAIN, sme->ie, sme->ie_len))
            return;

        vif->sta.ft_assoc_ies_len = ies_len;
        vif->sta.ft_assoc_ies = kmalloc(ies_len, GFP_KERNEL);
    } else if (vif->sta.ft_assoc_ies_len < ies_len) {
        kfree(vif->sta.ft_assoc_ies);
        vif->sta.ft_assoc_ies = kmalloc(ies_len, GFP_KERNEL);
    }

    if (!vif->sta.ft_assoc_ies)
        return;

    // Also save SSID (as an element) in the buffer
    pos = vif->sta.ft_assoc_ies;
    *pos++ = WLAN_EID_SSID;
    *pos++ = sme->ssid_len;
    memcpy(pos, sme->ssid, sme->ssid_len);
    pos += sme->ssid_len;
    memcpy(pos, sme->ie, sme->ie_len);
    vif->sta.ft_assoc_ies_len = ies_len;
}

/**
 * bl_rsne_to_connect_params - Initialise cfg80211_connect_params from
 * RSN element.
 *
 * @rsne: RSN element
 * @sme: Structure cfg80211_connect_params to initialize
 *
 * The goal is only to initialize enough for bl_send_sm_connect_req
 */
int bl_rsne_to_connect_params(const struct element_t *rsne,
                                        struct cfg80211_connect_params *sme)
{
    int len = rsne->datalen;
    int clen;
    const u8 *pos = rsne->data ;

    if (len < 8)
        return 1;

    sme->crypto.control_port_no_encrypt = false;
    sme->crypto.control_port = true;
    sme->crypto.control_port_ethertype = cpu_to_be16(ETH_P_PAE);

    pos += 2;
    sme->crypto.cipher_group = ntohl(*((u32 *)pos));
    pos += 4;
    clen = le16_to_cpu(*((u16 *)pos)) * 4;
    pos += 2;
    len -= 8;
    if (len < clen + 2)
        return 1;
    // only need one cipher suite
    sme->crypto.n_ciphers_pairwise = 1;
    sme->crypto.ciphers_pairwise[0] = ntohl(*((u32 *)pos));
    pos += clen;
    len -= clen;

    // no need for AKM
    clen = le16_to_cpu(*((u16 *)pos)) * 4;
    pos += 2;
    len -= 2;
    if (len < clen)
        return 1;
    pos += clen;
    len -= clen;

    if (len < 4)
        return 0;

    pos += 2;
    clen = le16_to_cpu(*((u16 *)pos)) * 16;
    len -= 4;
    if (len > clen)
        sme->mfp = NL80211_MFP_REQUIRED;

    return 0;
}

/*********************************************************************
 * netdev callbacks
 ********************************************************************/
/**
 * int (*ndo_open)(struct net_device *dev);
 *     This function is called when network device transistions to the up
 *     state.
 *
 * - Start FW if this is the first interface opened
 * - Add interface at fw level
 */
static int bl_open(struct net_device *dev)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;
    struct mm_add_if_cfm add_if_cfm;
    int error = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    if (bl_mod_params.mp_mode)
        goto next;

    // Check if it is the first opened VIF
    if (bl_hw->vif_started == 0)
    {
        // Start the FW
       if ((error = bl_send_start(bl_hw)))
           return error;

       /* Device is now started */
       set_bit(BL_DEV_STARTED, &bl_hw->flags);
    }

    if (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_AP_VLAN) {
        /* For AP_vlan use same fw and drv indexes. We ensure that this index
           will not be used by fw for another vif by taking index >= NX_VIRT_DEV_MAX */
        add_if_cfm.inst_nbr = bl_vif->drv_vif_index;
        netif_tx_stop_all_queues(dev);
    } else {
        /* Forward the information to the LMAC,
         *     p2p value not used in FMAC configuration, iftype is sufficient */
        if ((error = bl_send_add_if(bl_hw, dev->dev_addr,
                                     BL_VIF_TYPE(bl_vif), false, &add_if_cfm)))
            return error;

        if (add_if_cfm.status != 0) {
            BL_PRINT_CFM_ERR(add_if);
            return -EIO;
        }
    }

next:
    /* Save the index retrieved from LMAC */
    spin_lock_bh(&bl_hw->cb_lock);
    BL_DBG("inst_nbr=%d\n", add_if_cfm.inst_nbr);
    bl_vif->vif_index = add_if_cfm.inst_nbr;
    bl_vif->up = true;
    bl_hw->vif_started++;
    bl_hw->vif_table[add_if_cfm.inst_nbr] = bl_vif;
    spin_unlock_bh(&bl_hw->cb_lock);

    if (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_MONITOR) {
        bl_hw->monitor_vif = bl_vif->vif_index;
        
        if (bl_vif->ch_index != BL_CH_NOT_SET) {
            //Configure the monitor channel
            error = bl_send_config_monitor_req(bl_hw,
                        &bl_hw->chanctx_table[bl_vif->ch_index].chan_def, NULL);
        }
    }

    netif_carrier_off(dev);

    return error;
}

/**
 * int (*ndo_stop)(struct net_device *dev);
 *     This function is called when network device transistions to the down
 *     state.
 *
 * - Remove interface at fw level
 * - Reset FW if this is the last interface opened
 */
int bl_close(struct net_device *dev)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    BL_DBG(BL_FN_ENTRY_STR);

    netdev_info(dev, "CLOSE");

    if (bl_mod_params.mp_mode)
        return 0;

#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    if (!bl_vif->up) {
        BL_DBG("%s, up is false\n", __func__);
        return 0;
    }
#endif

    bl_hw->vif_started--;
    bl_vif->up = false;

    #ifdef CONFIG_BL_RADAR
    bl_radar_cancel_cac(&bl_hw->radar);
    #endif
    
    /* Abort scan request on the vif */
    if (bl_hw->scan_request &&
        bl_hw->scan_request->wdev == &bl_vif->wdev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
        struct cfg80211_scan_info info = {
            .aborted = true,
        };

        cfg80211_scan_done(bl_hw->scan_request, &info);
#else
        cfg80211_scan_done(bl_hw->scan_request, true);
#endif
        bl_hw->scan_request = NULL;
    }

    /* Ensure that we won't process disconnect ind */
    spin_lock_bh(&bl_hw->cb_lock);

    if (netif_carrier_ok(dev)) {
        if (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_STATION ||
            BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_P2P_CLIENT) 
        {
            if (bl_vif->sta.ft_assoc_ies) {
                kfree(bl_vif->sta.ft_assoc_ies);
                bl_vif->sta.ft_assoc_ies = NULL;
                bl_vif->sta.ft_assoc_ies_len = 0;
            }
            
            cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING,
                                  NULL, 0, true, GFP_ATOMIC);
                                  
            if (bl_vif->sta.ap) {
                bl_txq_sta_deinit(bl_hw, bl_vif->sta.ap);
                bl_txq_tdls_vif_deinit(bl_vif);
            }
            
            netif_tx_stop_all_queues(dev);
            netif_carrier_off(dev);
        } else if (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_AP_VLAN) {
            netif_carrier_off(dev);
        } else {
            netdev_warn(dev, "AP not stopped when disabling interface");
        }
    }

    //bl_hw->vif_table[bl_vif->vif_index] = NULL;
    spin_unlock_bh(&bl_hw->cb_lock);

    bl_chanctx_unlink(bl_vif);

    if (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_MONITOR)
        bl_hw->monitor_vif = BL_INVALID_VIF;

    msleep(200);
    bl_send_remove_if(bl_hw, bl_vif->vif_index);

    if (bl_hw->vif_started == 0) {
        /* This also lets both ipc sides remain in sync before resetting */
        bl_ipc_tx_drain(bl_hw);

        bl_send_reset(bl_hw);

        // Set parameters to firmware
        bl_send_me_config_req(bl_hw);

        // Set channel parameters to firmware
        bl_send_me_chan_config_req(bl_hw);

        clear_bit(BL_DEV_STARTED, &bl_hw->flags);
    }

    if (bl_hw->roc && (bl_hw->roc->vif == bl_vif)) {
        kfree(bl_hw->roc);
        bl_hw->roc = NULL;
    }

    return 0;
}

/**
 * struct net_device_stats* (*ndo_get_stats)(struct net_device *dev);
 *    Called when a user wants to get the network device usage
 *    statistics. Drivers must do one of the following:
 *    1. Define @ndo_get_stats64 to fill in a zero-initialised
 *       rtnl_link_stats64 structure passed by the caller.
 *    2. Define @ndo_get_stats to update a net_device_stats structure
 *       (which should normally be dev->stats) and return a pointer to
 *       it. The structure may be changed asynchronously only if each
 *       field is written atomically.
 *    3. Update dev->stats asynchronously and atomically, and define
 *       neither operation.
 */
static struct net_device_stats *bl_get_stats(struct net_device *dev)
{
    struct bl_vif *vif = netdev_priv(dev);

    return &vif->net_stats;
}

/**
 * u16 (*ndo_select_queue)(struct net_device *dev, struct sk_buff *skb,
 *                         struct net_device *sb_dev);
 *    Called to decide which queue to when device supports multiple
 *    transmit queues.
 */
u16 bl_select_queue(struct net_device *dev, struct sk_buff *skb,
                          struct net_device *sb_dev)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    return bl_select_txq(bl_vif, skb);
}

/**
 * int (*ndo_set_mac_address)(struct net_device *dev, void *addr);
 *    This function  is called when the Media Access Control address
 *    needs to be changed. If this interface is not defined, the
 *    mac address can not be changed.
 */
static int bl_set_mac_address(struct net_device *dev, void *addr)
{
    struct sockaddr *sa = addr;
    int ret;

    ret = eth_mac_addr(dev, sa);

    return ret;
}

#ifdef CONFIG_WIRELESS_EXT   
static u8 bl_rssi_to_quality(int8_t rssi, u16_l retry)
{
    u8 quality = 0;
    u8 tx_qual = 0;
    u16 tx_retry = 0;

    /**100ms tx retry number */
    tx_retry = retry / 10; 
    if (tx_retry > 200)
        tx_qual = 35;
    else if (tx_retry > 100)
        tx_qual = 25;
    else if  (tx_retry > 50)
        tx_qual = 20;
    else if (tx_retry > 20)
        tx_qual = 10;
    else if (tx_retry > 10)
        tx_qual = 5;

    
    if (rssi < -95)
        quality = 0;
    else if (rssi < -90)
        quality = 10;
    else if (rssi < -80)
        quality = 20; 
    else if (rssi < -70)
        quality = 30;
    else if (rssi < -65)
        quality = 40;
    else if (rssi < -60)
        quality = 50; 
    else if (rssi < -50)
        quality = 75; 
    else if (rssi < -45)
        quality = 85;
    else if (rssi < -40)
        quality = 90; 
    else if (rssi < -35)
        quality = 95;
    else
        quality = 100; 

    if (quality > 60)
        quality = quality - tx_qual;

    return quality;
}

/**
 *  @brief Get wireless statistics
 *
 *  @param dev          A pointer to net_device structure
 *
 *  @return             A pointer to iw_statistics buf
 */
struct iw_statistics *bl_get_wireless_stats(struct net_device *dev)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;
    struct iw_statistics *stats = &bl_hw->iw_stats;
    struct mm_get_rssi_cfm cfm;
    int ret = 0;
    
    stats->qual.qual = 0;
    stats->qual.level = 0;
    stats->qual.noise = 0;
    stats->miss.beacon = 0;
    stats->discard.retries = 0;
    stats->qual.updated = 7;
    /* STA connected status */
    if(bl_vif->sta.ap) {
        ret = bl_send_rssi_read_req(bl_hw, bl_vif->vif_index, &cfm);
        if (ret) {
            stats->qual.qual = 0;
            stats->qual.level = 0;
            stats->qual.updated |= IW_QUAL_NOISE_INVALID | IW_QUAL_QUAL_INVALID | IW_QUAL_LEVEL_INVALID;
        }
        
        stats->qual.level = cfm.rssi_value;
        stats->qual.qual = bl_rssi_to_quality(cfm.rssi_value, cfm.retry);
        stats->qual.updated |= IW_QUAL_NOISE_INVALID;
    } else {
        /* not connected status  */    
        stats->qual.updated |= IW_QUAL_NOISE_INVALID | IW_QUAL_QUAL_INVALID | IW_QUAL_LEVEL_INVALID;
    }

    return stats;
}
#endif

/* 0x8B00 - 0x8BDF */
static const iw_handler bl_iw_std_hdl_array[] = {
    (iw_handler)NULL,       /* SIOCSIWCOMMIT */
};

/* 0x8BE0 - 0x8BFF */
static const iw_handler bl_iw_priv_hdl_array[] = {
    [BL_IOCTL_VERSION - SIOCIWFIRSTPRIV] =      (iw_handler)bl_iwpriv_ver_hdl,
    [BL_IOCTL_WMMCFG  - SIOCIWFIRSTPRIV] =      (iw_handler)bl_iwpriv_wmmcfg_hdl,
    [BL_IOCTL_TEMP    - SIOCIWFIRSTPRIV] =      (iw_handler)bl_iwpriv_temp_read_hdl,
    [BL_IOCTL_SETRATE - SIOCIWFIRSTPRIV] =      (iw_handler)bl_iwpriv_set_rate_hdl,
    //[BL_IOCTL_SETPROREQPRIVIES-SIOCIWFIRSTPRIV]=(iw_handler)bl_iwpriv_scan_ie_hdl,
    //[BL_IOCTL_PRIVIESON - SIOCIWFIRSTPRIV] =    (iw_handler)bl_iwpriv_set_priv_ies_on_off_hdl,
    [BL_IOCTL_RW_COEX_PARAM-SIOCIWFIRSTPRIV] =  (iw_handler)bl_iwpriv_rw_coex_param_hdl,
    [BL_IOCTL_RW_MEM-SIOCIWFIRSTPRIV] =         (iw_handler)bl_iwpriv_rw_mem_hdl,

#ifdef CONFIG_BL_MP
    [BL_IOCTL_MP_CFG  - SIOCIWFIRSTPRIV] =      (iw_handler)bl_iwpriv_mp_load_caldata_hdl,
    [BL_IOCTL_MP_HELP - SIOCIWFIRSTPRIV] =      (iw_handler)bl_iwpriv_help_hdl,
    [BL_IOCTL_MP_MS   - SIOCIWFIRSTPRIV] =      (iw_handler)bl_iwpriv_mp_ms_hdl,
    [BL_IOCTL_MP_MG   - SIOCIWFIRSTPRIV] =      (iw_handler)bl_iwpriv_mp_mg_hdl,
    [BL_IOCTL_MP_IND  - SIOCIWFIRSTPRIV] =      (iw_handler)bl_iwpriv_mp_ind_hdl,
    //RTOS
    //[BL_IOCTL_MP_CAL_CFG  - SIOCIWFIRSTPRIV] =  (iw_handler)bl_iwpriv_mp_cfg_caldata_hdl,
#endif

    [BL_IOCTL_GET_CMD_TYPE_INT  - SIOCIWFIRSTPRIV] = (iw_handler)bl_iwpriv_cmd_get_type_int,
    [BL_IOCTL_SET_CMD_TYPE_INT  - SIOCIWFIRSTPRIV] = (iw_handler)bl_iwpriv_cmd_set_type_int,
    [BL_IOCTL_SET_CMD  - SIOCIWFIRSTPRIV] =          (iw_handler)bl_iwpriv_cmd_set,
    [BL_IOCTL_GET_CMD  - SIOCIWFIRSTPRIV] =          (iw_handler)bl_iwpriv_cmd_get,
};

//The string cmd name should be 15 chars at most, plue '\0', make up to 16 chars'
static const struct iw_priv_args bl_iw_priv_args[] = {
    {BL_IOCTL_VERSION,               IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "version" },
    {BL_IOCTL_WMMCFG,                IW_PRIV_TYPE_INT | 128,                 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,  "wmmcfg" },
    {BL_IOCTL_TEMP,                  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,   IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,  "temp" },
    {BL_IOCTL_SETRATE,               IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_MASK|3,0,                                     "setrate" },
    {BL_IOCTL_RW_COEX_PARAM,         IW_PRIV_TYPE_BYTE | 128,                0,                                     "rw_coex" },
    {BL_IOCTL_RW_MEM,                IW_PRIV_TYPE_INT | 512,                 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,  "rw_mem" },

#ifdef CONFIG_BL_MP
    {BL_IOCTL_MP_CFG,                IW_PRIV_TYPE_CHAR|IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR| IW_PRIV_SIZE_MASK,     "mp_load_caldata" },
    {BL_IOCTL_MP_HELP,               IW_PRIV_TYPE_CHAR|IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR|IW_PRIV_SIZE_MASK,      "help" },
    
    {BL_IOCTL_MP_MS ,                IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "" },
    //{BL_IOCTL_MP_UNICAST_TX,        IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,   "mp_uc_tx" },
    {BL_IOCTL_MP_11b_RATE,           IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_11b_rate" },
    {BL_IOCTL_MP_11g_RATE,           IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_11g_rate" },
    {BL_IOCTL_MP_11n_RATE,           IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_11n_rate" },
    {BL_IOCTL_MP_11ax_RATE,          IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_11ax_rate" },
    {BL_IOCTL_MP_TX,                 IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_tx" }, 
    //{BL_IOCTL_MP_SET_PKT_FREQ,      IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,   "mp_set_pkt_freq" },    
    {BL_IOCTL_MP_SET_CHANNEL,        IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_set_channel" },    
    {BL_IOCTL_MP_SET_PKT_LEN,        IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_set_pkt_len" },     
    {BL_IOCTL_MP_SET_CWMODE,         IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_set_cw_mode" },    
    {BL_IOCTL_MP_SET_PWR,            IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_set_power" },    
    {BL_IOCTL_MP_SET_TXDUTY,         IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_set_tx_duty" },    
    {BL_IOCTL_MP_SET_PWR_OFT_EN,     IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_en_pwr_oft" },    
    {BL_IOCTL_MP_SET_XTAL_CAP,       IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_set_xtal_cap" },
    //{BL_IOCTL_MP_SET_PRIV_PARAM,    IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,   "mp_set_dbg_para" },
    {BL_IOCTL_MP_WR_MEM,             IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_wr_mem" },
    {BL_IOCTL_MP_RD_MEM,             IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_rd_mem" },
    {BL_IOCTL_MP_BTBLE_TX,           IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_btble_tx" }, 
    {BL_IOCTL_MP_BTBLE_RX,           IW_PRIV_TYPE_INT | 512,  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_MASK,                 "mp_btble_rx" },

    {BL_IOCTL_MP_MG,                 IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "" },
    {BL_IOCTL_MP_HELLO,              IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_hello" },
    //{BL_IOCTL_MP_UNICAST_RX,        IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_uc_rx" },
    {BL_IOCTL_MP_RX,                 IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_rx" },
    // {BL_IOCTL_MP_PM,                IW_PRIV_TYPE_CHAR| IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,   "mp_pm" },
    // {BL_IOCTL_MP_RESET,             IW_PRIV_TYPE_CHAR| IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,   "mp_reset" },
    {BL_IOCTL_MP_GET_FW_VER,         IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_fw_ver" },    
    {BL_IOCTL_MP_GET_BUILD_INFO,     IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_buildinf" },    
    {BL_IOCTL_MP_GET_POWER,          IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_power" },    
    {BL_IOCTL_MP_GET_FREQ,           IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_channel" },
    {BL_IOCTL_MP_GET_TX_STATUS,      IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_tx_state" },    
    //{BL_IOCTL_MP_GET_PKT_FREQ,      IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_pkt_freq" },    
    {BL_IOCTL_MP_GET_XTAL_CAP,       IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_xtal_cap" },   
    {BL_IOCTL_MP_GET_CWMODE,         IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_cw_mode" },    
    {BL_IOCTL_MP_GET_TX_DUTY,        IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_tx_duty" },
#if 0
    {BL_IOCTL_MP_EFUSE_RD,           IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_efuse_rd" },
    {BL_IOCTL_MP_EFUSE_WR,           IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_efuse_wr" },
#endif
    {BL_IOCTL_MP_EFUSE_CAP_RD,       IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_ef_cap_rd" },
    {BL_IOCTL_MP_EFUSE_CAP_WR,       IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_ef_cap_wr" },
    {BL_IOCTL_MP_EFUSE_PWR_OFT_RD,   IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_ef_pwroft_rd" },
    {BL_IOCTL_MP_EFUSE_PWR_OFT_WR,   IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_ef_pwroft_wr" },
    {BL_IOCTL_MP_EFUSE_MAC_ADR_RD,   IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_ef_mac_rd" },
    {BL_IOCTL_MP_EFUSE_MAC_ADR_WR,   IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_ef_mac_wr" },
#if 0
    {BL_IOCTL_MP_GET_TEMPERATURE,    IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_get_temp" },
    {BL_IOCTL_MP_DUMP_PHYRF,         IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_dump_phyrf" },
#endif
    {BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_RD,IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR |IW_PRIV_SIZE_MASK,   "mp_ef_bz_pof_rd" },
    {BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_WR,IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR |IW_PRIV_SIZE_MASK,   "mp_ef_bz_pof_wr" },
    {BL_IOCTL_MP_ATCMD,              IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR |IW_PRIV_SIZE_MASK,   "mp_atcmd" },

    {BL_IOCTL_MP_IND,                IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_ind" },

    //RTOS
    //{BL_IOCTL_MP_CAL_CFG,           IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,  "mp_cfg_cal" },
#endif
     // Normal set cmd -- type INT
    {BL_IOCTL_SET_CMD_TYPE_INT,      IW_PRIV_TYPE_INT | 64,    IW_PRIV_TYPE_BYTE | 128,               "" },
    {BL_IOCTL_SUB_EDCA,              IW_PRIV_TYPE_INT | 64,    IW_PRIV_TYPE_BYTE | 128,               "edca" },
    {BL_IOCTL_SUB_TWT_SETUP,         IW_PRIV_TYPE_INT | 64,    IW_PRIV_TYPE_BYTE | 128,               "twt_setup" },
    {BL_IOCTL_SUB_SET_PHY_MISC,      IW_PRIV_TYPE_INT | 64,    IW_PRIV_TYPE_BYTE | 128,               "set_phy_misc" },
    {BL_IOCTL_SUB_KE_STAT_REQ,       IW_PRIV_TYPE_INT | 64,    IW_PRIV_TYPE_BYTE | 128,               "ke_stat" },

    // Normal get cmd -- type INT
    {BL_IOCTL_GET_CMD_TYPE_INT,      IW_PRIV_TYPE_INT | 128,    IW_PRIV_TYPE_CHAR | 128,              "" },
#if defined(CONFIG_BL_SDIO)
    {BL_IOCIL_SUB_READ_SCRATCH,      IW_PRIV_TYPE_INT | 128,    IW_PRIV_TYPE_CHAR | 128,              "read_scratch" },
#endif
#if defined(CONFIG_BL_DYN_DBG)
    {BL_IOCIL_SUB_DBG_LEVEL,         IW_PRIV_TYPE_INT | 128,    IW_PRIV_TYPE_CHAR | 128,              "dbg_level" },
#endif

    // normal set cmd
    {BL_IOCTL_SET_CMD,               IW_PRIV_TYPE_BYTE | 512,  IW_PRIV_TYPE_BYTE | 128,               "" },
    {BL_IOCTL_SUB_PS,                IW_PRIV_TYPE_BYTE | 512,  IW_PRIV_TYPE_BYTE | 128,               "ps" },
    {BL_IOCTL_SUB_SCAN,              IW_PRIV_TYPE_BYTE | 512,  IW_PRIV_TYPE_BYTE | 128,               "scan" },
    {BL_IOCTL_SUB_SCAN_CHAN,         IW_PRIV_TYPE_BYTE | 512,  IW_PRIV_TYPE_BYTE | 128,               "scan_chan" },
    {BL_IOCTL_SUB_SCAN_IE,           IW_PRIV_TYPE_BYTE | 512,  IW_PRIV_TYPE_BYTE | 128,               "scan_ie" },
    {BL_IOCTL_SUB_TWT_TEARDOWN,      IW_PRIV_TYPE_BYTE | 512,  IW_PRIV_TYPE_BYTE | 128,               "twt_teardown" },
    {BL_IOCTL_SUB_CMW_FILTER,        IW_PRIV_TYPE_BYTE | 512,  IW_PRIV_TYPE_BYTE | 128,               "cmw_filter" },

    // normal get cmd
    {BL_IOCTL_GET_CMD,               IW_PRIV_TYPE_BYTE | 128, IW_PRIV_TYPE_BYTE | 512,                "" },
    {BL_IOCTL_SUB_COUNTRY_CODE,      IW_PRIV_TYPE_BYTE | 128, IW_PRIV_TYPE_BYTE | 512,                "country_code" },
    {BL_IOCTL_SUB_MAC_ADDR,          IW_PRIV_TYPE_BYTE | 128, IW_PRIV_TYPE_BYTE | 512,                "mac_addr" },
    {BL_IOCTL_SUB_STATUS,            IW_PRIV_TYPE_BYTE | 128, IW_PRIV_TYPE_BYTE | 512,                "status" },
    {BL_IOCTL_SUB_RSSI,              IW_PRIV_TYPE_BYTE | 128, IW_PRIV_TYPE_BYTE | 512,                "rssi" },
    {BL_IOCTL_SUB_READ_EFUSE,        IW_PRIV_TYPE_BYTE | 128, IW_PRIV_TYPE_BYTE | 512,                "read_efuse" },
};

/* From Kernel 4.12 when CONFIG_WEXT_CORE enable,
   will only process stand/priv ioctl and ignore old driver API (ndo_do_ioctl).
*/
static const struct iw_handler_def bl_iw_handler = {
    standard: (iw_handler *)bl_iw_std_hdl_array,
    num_standard: sizeof(bl_iw_std_hdl_array) / sizeof(iw_handler),
#ifdef CONFIG_WEXT_PRIV
    num_private:  sizeof(bl_iw_priv_hdl_array) / sizeof(iw_handler),
    num_private_args: sizeof(bl_iw_priv_args) / sizeof(struct iw_priv_args),
    private:  (iw_handler *)bl_iw_priv_hdl_array,
    private_args: (struct iw_priv_args *)bl_iw_priv_args,
#endif
#if WIRELESS_EXT > 20
    get_wireless_stats: bl_get_wireless_stats,
#endif
};

//*********************************************************************
//***** private cmd for ioct, demo ioctl app is in driver's app folder. *******
//*********************************************************************
static const struct bl_dev_priv_cmd_node bl_dev_priv_cmd_table[] = {
    {"version",      BL_UTIL_CMD_VERSION,          bl_iwpriv_ver_hdl},
    {"temp",         BL_UTIL_CMD_TEMP,             bl_iwpriv_temp_read_hdl},
    {"scan",         BL_UTIL_CMD_SCAN,             bl_iwpriv_scan_hdl},
    {"scan_chan",    BL_UTIL_CMD_SCAN_CHAN,        bl_iwpriv_scan_chan_hdl},
    {"scan_ie",      BL_UTIL_CMD_SCAN_IE,          bl_iwpriv_scan_ie_hdl},
    {"country_code", BL_UTIL_CMD_GET_COUNTRY_CODE, bl_iwpriv_get_country_code_hdl},
    {"mac_addr",     BL_UTIL_CMD_GET_MAC_ADDR,     bl_iwpriv_get_mac_addr_hdl},
    {"status",       BL_UTIL_CMD_GET_STATUS,       bl_iwpriv_get_intf_status_hdl},
    {"rssi",         BL_UTIL_CMD_GET_RSSI,         bl_iwpriv_get_rssi_hdl},
    {"read_efuse",   BL_UTIL_CMD_READ_EFUSE,       bl_iwpriv_read_efuse_hdl},
    {"atcmd",        BL_UTIL_CMD_ATCMD,            bl_iwpriv_atcmd_hdl},
    {"hci_cmd",      BL_UTIL_CMD_HCI_CMD,          bl_iwpriv_ble_hci_cmd_hdl},
};

int bl_ioctl(struct net_device *dev, struct ifreq *ifreq, int cmd)
{
    struct iwreq *wrq = (struct iwreq *)ifreq;
    struct iw_request_info info;
    const struct bl_dev_priv_cmd_node * priv_cmd = NULL;
    int ret = 0;
    u8  i = 0;
    
#ifdef CONFIG_SUPPORT_WEXT_MODE
    if(bl_mod_params.mp_mode)
        return ret;
#endif

    info.cmd = cmd;
    info.flags = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
    // can still process iwpriv ioctl when dev->wireless_handlers and dev->ieee80211_ptr->wiphy->wext both NULL.
    if (cmd >= SIOCIWFIRSTPRIV && cmd <= SIOCIWLASTPRIV) {
        if (bl_iw_priv_hdl_array[cmd - SIOCIWFIRSTPRIV])
            ret = bl_iw_priv_hdl_array[cmd - SIOCIWFIRSTPRIV](dev, &info, &wrq->u, NULL);
    } else
#endif
    if (cmd == BL_DEV_PRIV_IOCTL_DEFAULT) {
        BL_DBG_MSG("%s, cmd=0x%x, 0x%x\n", __func__, 
                   cmd, *(uint32_t *)wrq->u.data.pointer);

        for (i = 0; i < (u8)ARRAY_SIZE(bl_dev_priv_cmd_table); i++) {
            //int cnt = 0;
            
            priv_cmd = &bl_dev_priv_cmd_table[i];
            
            //User's ioctl app will fill wrq with cmd_id
            if(priv_cmd->hdl && priv_cmd->cmd_id == *(uint32_t *)wrq->u.data.pointer) {
                //for (cnt = 0; cnt<wrq->u.data.length; cnt++)
                //    printk("%d\n", *( ((u8*)(wrq->u.data.pointer)+cnt) ));

                wrq->u.data.pointer += sizeof(priv_cmd->cmd_id);
                wrq->u.data.length -= sizeof(priv_cmd->cmd_id);

                //for (cnt = 0; cnt<wrq->u.data.length; cnt++)
                //    printk("%d\n", *( ((u8*)(wrq->u.data.pointer)+cnt) ));

                ret = priv_cmd->hdl(dev, &info, &wrq->u, NULL);

                wrq->u.data.pointer -= sizeof(priv_cmd->cmd_id);
                wrq->u.data.length += sizeof(priv_cmd->cmd_id);
                
                break;
            }
        }

        if(i == ARRAY_SIZE(bl_dev_priv_cmd_table)) {
            printk("command  handler not found\n");
            ret = -2;
        }
    } else {
        printk("Unknown cmd 0x%x\n", cmd);
        ret = -3;
    }

    return ret;
}

static const struct net_device_ops bl_netdev_ops = {
    .ndo_open               = bl_open,
    .ndo_stop               = bl_close,
    .ndo_do_ioctl           = bl_ioctl,
    .ndo_start_xmit         = bl_start_xmit,
    .ndo_get_stats          = bl_get_stats,
    .ndo_select_queue       = bl_select_queue,
    .ndo_set_mac_address    = bl_set_mac_address
//    .ndo_set_features       = bl_set_features,
//    .ndo_set_rx_mode        = bl_set_multicast_list,
};

static const struct net_device_ops bl_netdev_monitor_ops = {
    .ndo_open               = bl_open,
    .ndo_stop               = bl_close,
    .ndo_get_stats          = bl_get_stats,
    .ndo_set_mac_address    = bl_set_mac_address,
};

static void bl_netdev_setup(struct net_device *dev)
{
    ether_setup(dev);
    
    dev->priv_flags &= ~IFF_TX_SKB_SHARING;
    dev->netdev_ops = &bl_netdev_ops;
#if LINUX_VERSION_CODE <  KERNEL_VERSION(4, 12, 0)
    dev->destructor = free_netdev;
#else
    dev->needs_free_netdev = true;
#endif

#ifdef CONFIG_WIRELESS_EXT
    dev->wireless_handlers = &bl_iw_handler;
#if WIRELESS_EXT < 21
    dev->get_wireless_stats = bl_get_wireless_stats;
#endif

#ifdef CONFIG_CFG80211_WEXT
#ifdef CONFIG_SUPPORT_WEXT_MODE
    //don't register iw handler here if need support wext standard ioctl
    dev->wireless_handlers = NULL;
    printk("don't register wireless handler here \n");
#endif
#endif
#endif

    dev->watchdog_timeo = BL_TX_LIFETIME_MS;

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    dev->needed_headroom = sizeof(struct bl_txhdr) + sizeof(struct txdesc_api) + 
                           sizeof(struct inf_hdr) + TX_MAX_MAC_HEADER_SIZE;
    BL_DBG("%s needed_headroom=%d+%d+%d+76=%d\n", __func__, 
            sizeof(struct bl_txhdr),sizeof(struct txdesc_api),
            sizeof(struct inf_hdr),dev->needed_headroom);
#endif

#ifdef CONFIG_BL_AMSDUS_TX
    dev->needed_headroom = max(dev->needed_headroom,
                               (unsigned short)(sizeof(struct bl_amsdu_txhdr)
                                                + sizeof(struct ethhdr) + 4
                                                + sizeof(rfc1042_header) + 2));
#endif /* CONFIG_BL_AMSDUS_TX */

    dev->hw_features = 0;
}

/*********************************************************************
 * Cfg80211 callbacks (and helper)
 *********************************************************************/
static struct wireless_dev *bl_interface_add(struct bl_hw *bl_hw,
                           const char *name, unsigned char name_assign_type,
                           enum nl80211_iftype type, struct vif_params *params)
{
    int i;
    struct net_device *ndev;
    struct bl_vif *vif;
    int min_idx, max_idx;
    int vif_idx = -1;

    // Look for an available VIF
    if (type == NL80211_IFTYPE_AP_VLAN) {
        min_idx = NX_VIRT_DEV_MAX;
        max_idx = NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX;
    } else {
        min_idx = 0;
        max_idx = NX_VIRT_DEV_MAX;
    }

    for (i = min_idx; i < max_idx; i++) {
        if ((bl_hw->avail_idx_map) & BIT(i)) {
            vif_idx = i;
            break;
        }
    }
    if (vif_idx < 0)
        return NULL;

    #ifndef CONFIG_BL_MON_DATA
    list_for_each_entry(vif, &bl_hw->vifs, list) {
        // Check if monitor interface already exists or type is monitor
        if ((BL_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR) ||
           (type == NL80211_IFTYPE_MONITOR)) {
            wiphy_err(bl_hw->wiphy,
                      "Monitor+Data interface support (MON_DATA) disabled\n");
            return NULL;
        }
    }
    #endif

    ndev = alloc_netdev_mqs(sizeof(*vif), name, name_assign_type,
                            bl_netdev_setup, NX_NB_NDEV_TXQ, 1);
    if (!ndev)
        return NULL;

    vif = netdev_priv(ndev);
    ndev->ieee80211_ptr = &vif->wdev;
    vif->wdev.wiphy = bl_hw->wiphy;
    vif->bl_hw = bl_hw;
    vif->ndev = ndev;
    vif->drv_vif_index = vif_idx;
    SET_NETDEV_DEV(ndev, wiphy_dev(vif->wdev.wiphy));
    vif->wdev.netdev = ndev;
    vif->wdev.iftype = type;
    vif->up = false;
    vif->ch_index = BL_CH_NOT_SET;
    vif->generation = 0;
    memset(&vif->net_stats, 0, sizeof(vif->net_stats));
    memset(&vif->rx_pn, 0, sizeof(vif->rx_pn));

    switch (type) {
        case NL80211_IFTYPE_STATION:
        case NL80211_IFTYPE_P2P_CLIENT:
            vif->sta.flags = 0;
            vif->sta.ap = NULL;
            vif->sta.tdls_sta = NULL;
            vif->sta.ft_assoc_ies = NULL;
            vif->sta.ft_assoc_ies_len = 0;
            break;
        case NL80211_IFTYPE_MESH_POINT:
            INIT_LIST_HEAD(&vif->ap.mpath_list);
            INIT_LIST_HEAD(&vif->ap.proxy_list);
            vif->ap.mesh_pm = NL80211_MESH_POWER_ACTIVE;
            vif->ap.next_mesh_pm = NL80211_MESH_POWER_ACTIVE;
            // no break
        case NL80211_IFTYPE_AP:
        case NL80211_IFTYPE_P2P_GO:
            INIT_LIST_HEAD(&vif->ap.sta_list);
            memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
            vif->ap.flags = 0;
            break;
        case NL80211_IFTYPE_AP_VLAN:
        {
            struct bl_vif *master_vif;
            bool found = false;
            
            list_for_each_entry(master_vif, &bl_hw->vifs, list) {
                if ((BL_VIF_TYPE(master_vif) == NL80211_IFTYPE_AP) &&
                    !(!memcmp(master_vif->ndev->dev_addr, params->macaddr,
                              ETH_ALEN))) 
                {
                     found=true;
                     break;
                }
            }

            if (!found)
                goto err;

             vif->ap_vlan.master = master_vif;
             vif->ap_vlan.sta_4a = NULL;
             break;
        }
        case NL80211_IFTYPE_MONITOR:
            ndev->type = ARPHRD_IEEE80211_RADIOTAP;
            ndev->netdev_ops = &bl_netdev_monitor_ops;
            break;
        default:
            break;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
    if (type == NL80211_IFTYPE_AP_VLAN)
        memcpy(ndev->dev_addr, params->macaddr, ETH_ALEN);
    else {
        u8 temp_addr[ETH_ALEN] = {0};
        
        memcpy(temp_addr, bl_hw->wiphy->perm_addr, ETH_ALEN);
        temp_addr[5] ^= vif_idx;
        memcpy(ndev->dev_addr, temp_addr, ETH_ALEN);
    }
#else
    if (type == NL80211_IFTYPE_AP_VLAN)
        dev_addr_mod(ndev, 0, params->macaddr, ETH_ALEN);
    else {
        u8 temp_addr[ETH_ALEN] = {0};
        memcpy(temp_addr, bl_hw->wiphy->perm_addr, ETH_ALEN);
        temp_addr[5] ^= vif_idx;
        dev_addr_mod(ndev, 0, temp_addr, ETH_ALEN);
    }
#endif

    if (params) {
        vif->use_4addr = params->use_4addr;
        ndev->ieee80211_ptr->use_4addr = params->use_4addr;
    } else
        vif->use_4addr = false;

    if (register_netdevice(ndev))
        goto err;

#ifdef CONFIG_SUPPORT_WEXT_MODE
#ifdef CONFIG_CFG80211_WEXT
#ifdef CONFIG_WIRELESS_EXT
    if(bl_hw->mod_params->mp_mode)
        ndev->wireless_handlers = &bl_iw_handler;
#endif
#endif
#endif

    spin_lock_bh(&bl_hw->cb_lock);
    list_add_tail(&vif->list, &bl_hw->vifs);
    spin_unlock_bh(&bl_hw->cb_lock);
    bl_hw->avail_idx_map &= ~BIT(vif_idx);

    return &vif->wdev;

err:
    free_netdev(ndev);
    
    return NULL;
}

/**
 * @brief Retrieve the bl_sta object allocated for a given MAC address
 * and a given role.
 */
static struct bl_sta *bl_retrieve_sta(struct bl_hw *bl_hw,
                                          struct bl_vif *bl_vif, u8 *addr,
                                          __le16 fc, bool ap)
{
    if (ap) {
        /* only deauth, disassoc and action are bufferable MMPDUs */
        bool bufferable = ieee80211_is_deauth(fc) ||
                          ieee80211_is_disassoc(fc) ||
                          ieee80211_is_action(fc);

        /* Check if the packet is bufferable or not */
        if (bufferable)
        {
            /* Check if address is a broadcast or a multicast address */
            if (is_broadcast_ether_addr(addr) || is_multicast_ether_addr(addr)) {
                /* Returned STA pointer */
                struct bl_sta *bl_sta = &bl_hw->sta_table[bl_vif->ap.bcmc_index];

                if (bl_sta->valid)
                    return bl_sta;
            } else {
                /* Returned STA pointer */
                struct bl_sta *bl_sta;

                /* Go through list of STAs linked with the provided VIF */
                list_for_each_entry(bl_sta, &bl_vif->ap.sta_list, list) {
                    if (bl_sta->valid &&
                        ether_addr_equal(bl_sta->mac_addr, addr)) {
                        /* Return the found STA */
                        
                        return bl_sta;
                    }
                }
            }
        }
    } else {
        return bl_vif->sta.ap;
    }

    return NULL;
}

/**
 * @add_virtual_intf: create a new virtual interface with the given name,
 *    must set the struct wireless_dev's iftype. Beware: You must create
 *    the new netdev in the wiphy's network namespace! Returns the struct
 *    wireless_dev, or an ERR_PTR. For P2P device wdevs, the driver must
 *    also set the address member in the wdev.
 */
static struct wireless_dev *bl_cfg80211_add_iface(struct wiphy *wiphy,
                            const char *name, unsigned char name_assign_type,
                            enum nl80211_iftype type, struct vif_params *params)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct wireless_dev *wdev;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0))
    unsigned char name_assign_type = NET_NAME_UNKNOWN;
#endif

    BL_DBG(BL_FN_ENTRY_STR);

    wdev = bl_interface_add(bl_hw, name, name_assign_type, type, params);

    if (!wdev)
        return ERR_PTR(-EINVAL);

    return wdev;
}

/**
 * @del_virtual_intf: remove the virtual interface
 */
static int bl_cfg80211_del_iface(struct wiphy *wiphy, struct wireless_dev *wdev)
{
    struct net_device *dev = wdev->netdev;
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = wiphy_priv(wiphy);

    BL_DBG(BL_FN_ENTRY_STR);
    
    netdev_info(dev, "Remove Interface");

    if (dev->reg_state == NETREG_REGISTERED) {
        /* Will call bl_close if interface is UP */
        unregister_netdevice(dev);
    }

    spin_lock_bh(&bl_hw->cb_lock);
    list_del(&bl_vif->list);
    spin_unlock_bh(&bl_hw->cb_lock);
    
    bl_hw->avail_idx_map |= BIT(bl_vif->drv_vif_index);
    bl_vif->ndev = NULL;

    /* Clear the priv in adapter */
    dev->ieee80211_ptr = NULL;

    return 0;
}

/**
 * @change_virtual_intf: change type/configuration of virtual interface,
 *    keep the struct wireless_dev's iftype updated.
 */
static int bl_cfg80211_change_iface(struct wiphy *wiphy,
                                             struct net_device *dev,
                                             enum nl80211_iftype type,
                                             struct vif_params *params)
{
#ifndef CONFIG_BL_MON_DATA
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
#endif
    struct bl_vif *vif = netdev_priv(dev);

    BL_DBG(BL_FN_ENTRY_STR);

    if (vif->up)
        return (-EBUSY);

#ifndef CONFIG_BL_MON_DATA
    if ((type == NL80211_IFTYPE_MONITOR) &&
       (BL_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) 
    {
        struct bl_vif *vif_el;
        
        list_for_each_entry(vif_el, &bl_hw->vifs, list) {
            // Check if data interface already exists
            if ((vif_el != vif) &&
               (BL_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) {
                wiphy_err(bl_hw->wiphy,
                        "Monitor+Data interface support (MON_DATA) disabled\n");
                        
                return -EIO;
            }
        }
    }
#endif

    // Reset to default case (i.e. not monitor)
    dev->type = ARPHRD_ETHER;
    dev->netdev_ops = &bl_netdev_ops;

    switch (type) {
        case NL80211_IFTYPE_STATION:
        case NL80211_IFTYPE_P2P_CLIENT:
            vif->sta.flags = 0;
            vif->sta.ap = NULL;
            vif->sta.tdls_sta = NULL;
            vif->sta.ft_assoc_ies = NULL;
            vif->sta.ft_assoc_ies_len = 0;
            break;
        case NL80211_IFTYPE_MESH_POINT:
            INIT_LIST_HEAD(&vif->ap.mpath_list);
            INIT_LIST_HEAD(&vif->ap.proxy_list);
            // no break
        case NL80211_IFTYPE_AP:
        case NL80211_IFTYPE_P2P_GO:
            INIT_LIST_HEAD(&vif->ap.sta_list);
            memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
            vif->ap.flags = 0;
            break;
        case NL80211_IFTYPE_AP_VLAN:
            return -EPERM;
        case NL80211_IFTYPE_MONITOR:
            dev->type = ARPHRD_IEEE80211_RADIOTAP;
            dev->netdev_ops = &bl_netdev_monitor_ops;
            break;
        default:
            break;
    }

    vif->generation = 0;
    vif->wdev.iftype = type;
    if (params->use_4addr != -1)
        vif->use_4addr = params->use_4addr;

    return 0;
}

/**
 * @scan: Request to do a scan. If returning zero, the scan request is given
 *    the driver, and will be valid until passed to cfg80211_scan_done().
 *    For scan results, call cfg80211_inform_bss(); you can call this outside
 *    the scan/scan_done bracket too.
 */
static int bl_cfg80211_scan(struct wiphy *wiphy,
                                  struct cfg80211_scan_request *request)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = container_of(request->wdev, struct bl_vif, wdev);
    int error;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if(bl_hw->scan_request){
        printk("scan already in processing ...\n");
        return -EAGAIN;
    }

    bl_hw->scan_request = request;

    mutex_lock(&bl_hw->mutex);

    if ((error = bl_send_scanu_req(bl_hw, bl_vif, request))) {
        mutex_unlock(&bl_hw->mutex);
        
        bl_hw->scan_request = NULL;
        
        return error;
    }

    mutex_unlock(&bl_hw->mutex);

    return 0;
}

/**
 * @add_key: add a key with the given parameters. @mac_addr will be %NULL
 *    when adding a group key.
 */
static int bl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *netdev,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
                                 int link_id,
#endif
                                 u8 key_index, bool pairwise, const u8 *mac_addr,
                                 struct key_params *params)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *vif = netdev_priv(netdev);
    int i, error = 0;
    struct mm_key_add_cfm key_add_cfm;
    u8_l cipher = 0;
    struct bl_sta *sta = NULL;
    struct bl_key *bl_key;

    BL_DBG(BL_FN_ENTRY_STR);

    if (mac_addr) {
        sta = bl_get_sta(bl_hw, mac_addr);
        
        if (!sta)
            return -EINVAL;
        bl_key = &sta->key;
    }
    else
        bl_key = &vif->key[key_index];

    /* Retrieve the cipher suite selector */
    switch (params->cipher) {
        case WLAN_CIPHER_SUITE_WEP40:
            cipher = MAC_CIPHER_WEP40;
            break;
        case WLAN_CIPHER_SUITE_WEP104:
            cipher = MAC_CIPHER_WEP104;
            break;
        case WLAN_CIPHER_SUITE_TKIP:
            cipher = MAC_CIPHER_TKIP;
            break;
        case WLAN_CIPHER_SUITE_CCMP:
            cipher = MAC_CIPHER_CCMP;
            break;
        case WLAN_CIPHER_SUITE_AES_CMAC:
            cipher = MAC_CIPHER_BIP_CMAC_128;
            break;
        case WLAN_CIPHER_SUITE_SMS4:
        {
            // Need to reverse key order
            u8 tmp, *key = (u8 *)params->key;
            cipher = MAC_CIPHER_WPI_SMS4;
            
            for (i = 0; i < WPI_SUBKEY_LEN/2; i++) {
                tmp = key[i];
                key[i] = key[WPI_SUBKEY_LEN - 1 - i];
                key[WPI_SUBKEY_LEN - 1 - i] = tmp;
            }
            
            for (i = 0; i < WPI_SUBKEY_LEN/2; i++) {
                tmp = key[i + WPI_SUBKEY_LEN];
                key[i + WPI_SUBKEY_LEN] = key[WPI_KEY_LEN - 1 - i];
                key[WPI_KEY_LEN - 1 - i] = tmp;
            }
            break;
        }
        case WLAN_CIPHER_SUITE_GCMP:
            cipher = MAC_CIPHER_GCMP_128;
            break;
        case WLAN_CIPHER_SUITE_GCMP_256:
            cipher = MAC_CIPHER_GCMP_256;
            break;
        case WLAN_CIPHER_SUITE_CCMP_256:
            cipher = MAC_CIPHER_CCMP_256;
            break;
        default:
            return -EINVAL;
    }

    BL_DBG("%s pairwise=%d\n", __func__, pairwise);

    if ((error = bl_send_key_add(bl_hw, vif->vif_index,
                                   (sta ? sta->sta_idx : 0xFF), pairwise,
                                   (u8 *)params->key, params->key_len,
                                   key_index, cipher, &key_add_cfm)))
        return error;

    if (key_add_cfm.status != 0) {
        BL_PRINT_CFM_ERR(key_add);
        
        return -EIO;
    }

    /* Save the index retrieved from LMAC */
    bl_key->hw_idx = key_add_cfm.hw_key_idx;

    return 0;
}

/**
 * @get_key: get information about the key with the given parameters.
 *    @mac_addr will be %NULL when requesting information for a group
 *    key. All pointers given to the @callback function need not be valid
 *    after it returns. This function should return an error if it is
 *    not possible to retrieve the key, -ENOENT if it doesn't exist.
 *
 */
static int bl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *netdev,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
                                 int link_id,
#endif
                                 u8 key_index, bool pairwise, const u8 *mac_addr,
                                 void *cookie,
                                 void (*callback)(void *cookie, struct key_params*))
{
    BL_DBG(BL_FN_ENTRY_STR);

    return -1;
}


/**
 * @del_key: remove a key given the @mac_addr (%NULL for a group key)
 *    and @key_index, return -ENOENT if the key doesn't exist.
 */
static int bl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *netdev,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
                                 int link_id,
#endif
                                 u8 key_index, bool pairwise, const u8 *mac_addr)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *vif = netdev_priv(netdev);
    int error;
    struct bl_sta *sta = NULL;
    struct bl_key *bl_key;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (mac_addr) {
        sta = bl_get_sta(bl_hw, mac_addr);
        
        if (!sta)
            return -EINVAL;
        bl_key = &sta->key;
    } else {
        bl_key = &vif->key[key_index];
    }

    error = bl_send_key_del(bl_hw, bl_key->hw_idx);

    return error;
}

/**
 * @set_default_key: set the default key on an interface
 */
static int bl_cfg80211_set_default_key(struct wiphy *wiphy,
                                         struct net_device *netdev,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
                                         int link_id,
#endif
                                         u8 key_index, bool unicast, bool multicast)
{
    BL_DBG(BL_FN_ENTRY_STR);

    return 0;
}

/**
 * @set_default_mgmt_key: set the default management frame key on an interface
 */
static int bl_cfg80211_set_default_mgmt_key(struct wiphy *wiphy,
                                              struct net_device *netdev,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
                                              int link_id,
#endif
                                              u8 key_index)
{
    return 0;
}

/**
 * @connect: Connect to the ESS with the specified parameters. When connected,
 *    call cfg80211_connect_result() with status code %WLAN_STATUS_SUCCESS.
 *    If the connection fails for some reason, call cfg80211_connect_result()
 *    with the status from the AP.
 *    (invoked with the wireless_dev mutex held)
 */
static int bl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
                                      struct cfg80211_connect_params *sme)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct sm_connect_cfm sm_connect_cfm;
    int error = 0;

    BL_DBG_MSG(BL_FN_ENTRY_STR);

    /* For SHARED-KEY authentication, must install key first */
    if (sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY && sme->key)
    {
        struct key_params key_params;
        
        key_params.key = sme->key;
        key_params.seq = NULL;
        key_params.key_len = sme->key_len;
        key_params.seq_len = 0;
        key_params.cipher = sme->crypto.cipher_group;
        
        bl_cfg80211_add_key(wiphy, dev, 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
                            0,
#endif
                            sme->key_idx, false, NULL, &key_params);
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)  || defined(BL_WPA3_COMPAT)
    else if ((sme->auth_type == NL80211_AUTHTYPE_SAE) &&
             !(sme->flags & CONNECT_REQ_EXTERNAL_AUTH_SUPPORT)) 
    {
        netdev_err(dev, "Doesn't support SAE without external authentication\n");
        return -EINVAL;
    }
#endif

    mutex_lock(&bl_hw->mutex);
    
    BL_DBG_MSG("%s, mutex held\n", __func__);

    /* Forward the information to the LMAC */
    if ((error = bl_send_sm_connect_req(bl_hw, bl_vif, sme, &sm_connect_cfm))) {
        mutex_unlock(&bl_hw->mutex);
        
        printk("%s, mutex release, error:%d\n", __func__, error);

        return error;
    }

    if (sm_connect_cfm.status != CO_OK) {
        printk("%s, mutex release, sm_connect_cfm.status:0x%x\n", 
               __func__, sm_connect_cfm.status);

        mutex_unlock(&bl_hw->mutex);
    }

    // Check the status
    switch (sm_connect_cfm.status)
    {
        case CO_OK:
            bl_save_assoc_info_for_ft(bl_vif, sme);
            error = 0;
            break;
        case CO_BUSY:
            error = -EINPROGRESS;
            break;
        case CO_BAD_PARAM:
            error = -EINVAL;
            break;
        case CO_OP_IN_PROGRESS:
            error = -EALREADY;
            break;
        default:
            error = -EIO;
            break;
    }

    return error;
}

/**
 * @disconnect: Disconnect from the BSS/ESS.
 *    (invoked with the wireless_dev mutex held)
 */
static int bl_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
                                          u16 reason_code)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    int ret = 0;

    BL_DBG_MSG(BL_FN_ENTRY_STR);

#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    if (!bl_vif->up) {
        BL_DBG_MSG("%s, bl_hw:0x%p, vif:0x%p, vif up is false\n",
               __func__, bl_hw, bl_vif);
               
        return 0;
    }
#endif

    mutex_lock(&bl_hw->mutex);
    printk("%s, mutex held\n", __func__);

    ret = bl_send_sm_disconnect_req(bl_hw, bl_vif, reason_code);
    
    printk("%s, mutex release\n", __func__);
    mutex_unlock(&bl_hw->mutex);
    
    return ret;
}

#ifdef CONFIG_PRIV_CH_SWITCH
u8 freq_to_chan_number(u32 freq, u8 op_class)
{
    return (freq - 2407) / 5;
}

int bl_send_priv_channel_switch(struct bl_hw *bl_hw, struct bl_vif *sta_vif, 
                                          struct bl_vif *ap_vif, 
                                          struct cfg80211_chan_def *sta_chan_def,
                                          struct ieee80211_channel *sta_chan) 
{
    struct bl_chanctx *ctxt = NULL;
    int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
    u8 channel_number = 0;
    struct bl_ipc_elem_var elem = {0};
    struct bl_bcn *bcn, *bcn_after;
    struct bl_csa *csa;
    u16 csa_oft[BCN_MAX_CSA_CPT] = {0};
    u8 *buf;
    int error = 0;
    const struct element_t *ie_elem = NULL;
    u8 op_class;
    struct cfg80211_chan_def chan_def;

    mutex_lock(&bl_hw->mutex);

    if (!ap_vif || ap_vif->ap.csa) {
        mutex_unlock(&bl_hw->mutex);

        printk("%s, csa ongoing, busy, vif_idx:%d, csa:0x%p\r\n", __func__,
               ap_vif->vif_index, ap_vif->ap.csa);

        return -EBUSY;
    }

    if (ap_vif->ch_index >= NX_CHAN_CTXT_CNT) {
        mutex_unlock(&bl_hw->mutex);

        printk("%s, Invalid channel ctxt id %d\n", __func__, ap_vif->ch_index);

        return -1;
    }
    
    ctxt = &bl_hw->chanctx_table[ap_vif->ch_index];

    if (ctxt->chan_def.chan == NULL) {
        mutex_unlock(&bl_hw->mutex);

        printk("%s, ap chan NULL\n", __func__);
        return -1;
    }

    BL_DBG_MSG("ap band:%d, bw type:%d, center_freq:%d, center1_freq:%d, center2_freq:%d\r\n", 
            ctxt->chan_def.chan->band, bw2chnl[ctxt->chan_def.width], 
            ctxt->chan_def.chan->center_freq, 
            ctxt->chan_def.center_freq1, ctxt->chan_def.center_freq2);

    chan_def = *sta_chan_def;
    chan_def.chan = sta_chan;
    ieee80211_chandef_to_operating_class(&chan_def, &op_class);

    /* Build the new beacon with CSA IE */
    channel_number = freq_to_chan_number(chan_def.chan->center_freq, op_class);

    bcn = &ap_vif->ap.bcn;
    bcn->csa_len = 11;
    buf = bl_build_priv_csa_bcn(bl_hw, bcn, op_class, csa_oft, channel_number);

    if (!buf) {
        mutex_unlock(&bl_hw->mutex);
        
        printk("%s, no mem for bcn\r\n", __func__);    
       
        return -ENOMEM;
    }

    #if 0
    printk("%s, bl_build_priv_csa_bcn, var_offset:%d\r\n", __func__, var_offset);
    for_each_element_t(ie_elem, buf+var_offset, bcn->len+bcn->csa_len-var_offset) {
        printk("id %d, datalen:%d\r\n", ie_elem->id, ie_elem->datalen);
        bl_dump((u8 *)ie_elem, ie_elem->datalen+2);
    }
    #endif

    bl_change_ch_width_in_opmode(buf+var_offset, 
                                 bcn->len+bcn->csa_len-var_offset);

    elem.addr = buf;
    elem.size = bcn->len+bcn->csa_len;
    
    /* Build the beacon to use after CSA. It will only be sent to fw once
       CSA is over, but do it before sending the beacon as it must be ready
       when CSA is finished. */
    csa = kzalloc(sizeof(struct bl_csa), GFP_KERNEL);
    if (!csa) {
        mutex_unlock(&bl_hw->mutex);
        
        printk("%s, no mem for csa\r\n", __func__);
        error = -ENOMEM;
        
        goto end;
    }

    csa->channel_number = channel_number;
    csa->bcn = ap_vif->ap.bcn;
    bcn_after = &csa->bcn;
    buf = bl_build_priv_after_csa_bcn(bl_hw, bcn_after, bcn);
    if (!buf) {
        kfree(csa);

        mutex_unlock(&bl_hw->mutex);

        printk("%s, no mem for csa bcn\r\n", __func__);
        error = -ENOMEM;        

        goto end;
    }

    BL_DBG_MSG("%s, after bl_build_priv_after_csa_bcn\r\n", __func__);
    
    for_each_element_t(ie_elem, buf+var_offset, bcn_after->len-var_offset) {
        //printk("id %d, datalen:%d\r\n", ie_elem->id, ie_elem->datalen);
        //bl_print_elem((u8 *)ie_elem, ie_elem->datalen+2);
        
        if (ie_elem->id == WLAN_EID_DS_PARAMS) {
            *((u8 *)ie_elem + 2) = csa->channel_number;
        }
    }

    bl_change_ch_width_in_opmode(buf+var_offset, bcn_after->len-var_offset);

    csa->elem.addr = buf;
    csa->elem.size = bcn_after->len;
    ap_vif->ap.csa = csa;
    csa->vif = ap_vif;
    csa->chandef = *sta_chan_def;
    csa->chandef.chan = sta_chan;
    INIT_WORK(&csa->work, bl_csa_finish);

    printk("%s, call bcn_change\n", __func__);
    
    /* Send new Beacon. FW will extract channel and count from the beacon */
    error = bl_send_bcn_change(bl_hw, ap_vif->vif_index, elem.addr,
                               bcn->len+bcn->csa_len, bcn->head_len, 
                               bcn->tim_len, csa_oft);

    if (error) {
        bl_ipc_elem_var_deallocs(bl_hw, &csa->elem);
        kfree(csa);
        ap_vif->ap.csa = NULL;

        mutex_unlock(&bl_hw->mutex);
        
        goto end;
    } else {
        printk("%s, vif_idx:%d, csa:0x%p\n", __func__, ap_vif->vif_index, 
               ap_vif->ap.csa);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
        cfg80211_ch_switch_started_notify(ap_vif->ndev, &csa->chandef, 0, 2, false);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(6, 2, 16)
        cfg80211_ch_switch_started_notify(ap_vif->ndev, &csa->chandef, 0, 2, false, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
        cfg80211_ch_switch_started_notify(ap_vif->ndev, &csa->chandef, 0, 2, false);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 160)
        cfg80211_ch_switch_started_notify(ap_vif->ndev, &csa->chandef, 2, false);
#else
        cfg80211_ch_switch_started_notify(ap_vif->ndev, &csa->chandef, 2);
#endif
    }

end:
    bl_ipc_elem_var_deallocs(bl_hw, &elem);

    return error;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || defined(BL_WPA3_COMPAT)
/**
 * @external_auth: indicates result of offloaded authentication processing from
 *     user space
 */
static int bl_cfg80211_external_auth(struct wiphy *wiphy, struct net_device *dev,
                                   struct cfg80211_external_auth_params *params)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);

    if (!(bl_vif->sta.flags & BL_STA_EXT_AUTH))
        return -EINVAL;

    bl_external_auth_disable(bl_vif);
    return bl_send_sm_external_auth_required_rsp(bl_hw, bl_vif, params->status);
}
#endif

/**
 * @add_station: Add a new station.
 */
static int bl_cfg80211_add_station(struct wiphy *wiphy, struct net_device *dev,
                               const u8 *mac, struct station_parameters *params)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct me_sta_add_cfm me_sta_add_cfm;
    int error = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    WARN_ON(BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_AP_VLAN);

    /* Do not add TDLS station */
    if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
        return 0;

    /* Indicate we are in a STA addition process - This will allow handling
     * potential PS mode change indications correctly
     */
    set_bit(BL_DEV_ADDING_STA, &bl_hw->flags);

    /* Forward the information to the LMAC */
    if ((error = bl_send_me_sta_add(bl_hw, params, mac, bl_vif->vif_index,
                                     &me_sta_add_cfm)))
        return error;

    // Check the status
    switch (me_sta_add_cfm.status)
    {
        case CO_OK:
        {
            struct bl_sta *sta = &bl_hw->sta_table[me_sta_add_cfm.sta_idx];
            int tid;
            sta->aid = params->aid;

            sta->sta_idx = me_sta_add_cfm.sta_idx;
            sta->ch_idx = bl_vif->ch_index;
            sta->vif_idx = bl_vif->vif_index;
            sta->vlan_idx = sta->vif_idx;
            sta->qos = (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME)) != 0;
            sta->ht = STA_PARAM_LINK(params)->ht_capa ? 1 : 0;
            sta->vht = STA_PARAM_LINK(params)->vht_capa ? 1 : 0;
            sta->acm = 0;
            sta->listen_interval = params->listen_interval;
            
#ifdef CONFIG_MESH
            if (params->local_pm != NL80211_MESH_POWER_UNKNOWN)
                sta->mesh_pm = params->local_pm;
            else
                sta->mesh_pm = bl_vif->ap.next_mesh_pm;
                
            bl_update_mesh_power_mode(bl_vif);
#endif

            for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
                int uapsd_bit = bl_hwq2uapsd[bl_tid2hwq[tid]];
                if (params->uapsd_queues & uapsd_bit)
                    sta->uapsd_tids |= 1 << tid;
                else
                    sta->uapsd_tids &= ~(1 << tid);
            }
            
            memcpy(sta->mac_addr, mac, ETH_ALEN);
#ifdef CONFIG_BL_DEBUGFS
            bl_dbgfs_register_sta(bl_hw, sta);
#endif

            /* Ensure that we won't process PS change or channel switch ind*/
            spin_lock_bh(&bl_hw->cb_lock);
            bl_txq_sta_init(bl_hw, sta, bl_txq_vif_get_status(bl_vif));
            list_add_tail(&sta->list, &bl_vif->ap.sta_list);
            bl_vif->generation++;
            sta->valid = true;
            bl_ps_bh_enable(bl_hw, sta, sta->ps.active || me_sta_add_cfm.pm_state);
            spin_unlock_bh(&bl_hw->cb_lock);

            error = 0;

#ifdef CONFIG_BL_BFMER
            if (bl_hw->mod_params->bfmer) {
                bl_send_bfmer_enable(bl_hw, sta, STA_PARAM_LINK(params)->vht_capa);
            }
            
            bl_mu_group_sta_init(sta, params->vht_capa);
#endif /* CONFIG_BL_BFMER */

            #define PRINT_STA_FLAG(f)                               \
                (params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "["#f"]" : "")

            netdev_info(dev, "Add sta %d (%pM) flags=%s%s%s%s%s%s%s",
                        sta->sta_idx, mac,
                        PRINT_STA_FLAG(AUTHORIZED),
                        PRINT_STA_FLAG(SHORT_PREAMBLE),
                        PRINT_STA_FLAG(WME),
                        PRINT_STA_FLAG(MFP),
                        PRINT_STA_FLAG(AUTHENTICATED),
                        PRINT_STA_FLAG(TDLS_PEER),
                        PRINT_STA_FLAG(ASSOCIATED));
            #undef PRINT_STA_FLAG
            break;
        }
        default:
            error = -EBUSY;
            break;
    }

    clear_bit(BL_DEV_ADDING_STA, &bl_hw->flags);

    return error;
}

/**
 * @del_station: Remove a station
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
static int bl_cfg80211_del_station(struct wiphy *wiphy, struct net_device *dev,
                                          struct station_del_parameters *params)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int bl_cfg80211_del_station(struct wiphy *wiphy, struct net_device *dev,
                                   const u8 *mac)
#else
static int bl_cfg80211_del_station(struct wiphy *wiphy, struct net_device *dev,
                                   u8 *mac)
#endif
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_sta *cur, *tmp;
    int error = 0, found = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    const u8 *mac = NULL;
    if (params)
        mac = params->mac;
#endif

    list_for_each_entry_safe(cur, tmp, &bl_vif->ap.sta_list, list) {
        if ((!mac) || (!memcmp(cur->mac_addr, mac, ETH_ALEN))) {
            netdev_info(dev, "Del sta %d (%pM)", cur->sta_idx, cur->mac_addr);
            
            /* Ensure that we won't process PS change ind */
            spin_lock_bh(&bl_hw->cb_lock);
            cur->ps.active = false;
            cur->valid = false;
            spin_unlock_bh(&bl_hw->cb_lock);

            if (cur->vif_idx != cur->vlan_idx) {
                struct bl_vif *vlan_vif;
                
                vlan_vif = bl_hw->vif_table[cur->vlan_idx];
                
                if (vlan_vif->up) {
                    if ((BL_VIF_TYPE(vlan_vif) == NL80211_IFTYPE_AP_VLAN) &&
                        (vlan_vif->use_4addr)) 
                    {
                        vlan_vif->ap_vlan.sta_4a = NULL;
                    } else {
                        WARN(1, "Deleting sta belonging to VLAN other than AP_VLAN 4A");
                    }
                }
            }

            bl_txq_sta_deinit(bl_hw, cur);
            
            error = bl_send_me_sta_del(bl_hw, cur->sta_idx, false);
            if ((error != 0) && (error != -EPIPE) && (error != -EBUSY)) {
#ifdef CONFIG_BL_DEBUGFS
                bl_dbgfs_unregister_sta(bl_hw, cur);
#endif
                return error;
            }

#ifdef CONFIG_BL_BFMER
            // Disable Beamformer if supported
            bl_bfmer_report_del(bl_hw, cur);
            bl_mu_group_sta_del(bl_hw, cur);
#endif /* CONFIG_BL_BFMER */

            list_del(&cur->list);
            bl_vif->generation++;
            
#ifdef CONFIG_BL_DEBUGFS
            bl_dbgfs_unregister_sta(bl_hw, cur);
#endif
            found ++;
            
            break;
        }
    }

    if (!found)
        return -ENOENT;
        
#ifdef CONFIG_MESH
    bl_update_mesh_power_mode(bl_vif);
#endif

    return 0;
}

/**
 * @change_station: Modify a given station. Note that flags changes are not much
 *    validated in cfg80211, in particular the auth/assoc/authorized flags
 *    might come to the driver in invalid combinations -- make sure to check
 *    them, also against the existing state! Drivers must call
 *    cfg80211_check_station_change() to validate the information.
 */
static int bl_cfg80211_change_station(struct wiphy *wiphy, 
                               struct net_device *dev,
                               const u8 *mac, struct station_parameters *params)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_sta *sta;
    int error = 0;

    sta = bl_get_sta(bl_hw, mac);
    if (!sta)
    {
        /* Add the TDLS station */
        if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
        {
            struct bl_vif *bl_vif = netdev_priv(dev);
            struct me_sta_add_cfm me_sta_add_cfm;

            /* Indicate we are in a STA addition process - This will allow handling
             * potential PS mode change indications correctly
             */
            set_bit(BL_DEV_ADDING_STA, &bl_hw->flags);

            /* Forward the information to the LMAC */
            if ((error = bl_send_me_sta_add(bl_hw, params, mac, bl_vif->vif_index,
                                            &me_sta_add_cfm)))
                return error;

            // Check the status
            switch (me_sta_add_cfm.status)
            {
                case CO_OK:
                {
                    int tid;
                    
                    sta = &bl_hw->sta_table[me_sta_add_cfm.sta_idx];
                    sta->aid = params->aid;
                    sta->sta_idx = me_sta_add_cfm.sta_idx;
                    sta->ch_idx = bl_vif->ch_index;
                    sta->vif_idx = bl_vif->vif_index;
                    sta->vlan_idx = sta->vif_idx;
                    sta->qos =
                       (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME)) != 0;
                    sta->ht = STA_PARAM_LINK(params)->ht_capa ? 1 : 0;
                    sta->vht = STA_PARAM_LINK(params)->vht_capa ? 1 : 0;
                    sta->acm = 0;
                    
                    for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
                        int uapsd_bit = bl_hwq2uapsd[bl_tid2hwq[tid]];
                        
                        if (params->uapsd_queues & uapsd_bit)
                            sta->uapsd_tids |= 1 << tid;
                        else
                            sta->uapsd_tids &= ~(1 << tid);
                    }
                    
                    memcpy(sta->mac_addr, mac, ETH_ALEN);
                    
#ifdef CONFIG_BL_DEBUGFS
                    bl_dbgfs_register_sta(bl_hw, sta);
#endif

                    /* Ensure that we won't process PS change or channel switch ind*/
                    spin_lock_bh(&bl_hw->cb_lock);
                    bl_txq_sta_init(bl_hw, sta, bl_txq_vif_get_status(bl_vif));
                    
                    if (bl_vif->tdls_status == TDLS_SETUP_RSP_TX) {
                        bl_vif->tdls_status = TDLS_LINK_ACTIVE;
                        sta->tdls.initiator = true;
                        sta->tdls.active = true;
                    }
                    
                    /* Set TDLS channel switch capability */
                    if ((params->ext_capab[3] & WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) &&
                        !bl_vif->tdls_chsw_prohibited)
                        sta->tdls.chsw_allowed = true;
                        
                    bl_vif->sta.tdls_sta = sta;
                    sta->valid = true;
                    spin_unlock_bh(&bl_hw->cb_lock);
                    
#ifdef CONFIG_BL_BFMER
                    if (bl_hw->mod_params->bfmer)
                        bl_send_bfmer_enable(bl_hw, sta, params->vht_capa);

                    bl_mu_group_sta_init(sta, NULL);
#endif /* CONFIG_BL_BFMER */

                    #define PRINT_STA_FLAG(f)                               \
                        (params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "["#f"]" : "")

                    netdev_info(dev, "Add %s TDLS sta %d (%pM) flags=%s%s%s%s%s%s%s",
                                sta->tdls.initiator ? "initiator" : "responder",
                                sta->sta_idx, mac,
                                PRINT_STA_FLAG(AUTHORIZED),
                                PRINT_STA_FLAG(SHORT_PREAMBLE),
                                PRINT_STA_FLAG(WME),
                                PRINT_STA_FLAG(MFP),
                                PRINT_STA_FLAG(AUTHENTICATED),
                                PRINT_STA_FLAG(TDLS_PEER),
                                PRINT_STA_FLAG(ASSOCIATED));
                    #undef PRINT_STA_FLAG

                    break;
                }
                default:
                    error = -EBUSY;
                    break;
            }

            clear_bit(BL_DEV_ADDING_STA, &bl_hw->flags);
        } else  {
            return -EINVAL;
        }
    }

    if (error == -EBUSY)
        return -EINVAL;

    if (params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
        bl_send_me_set_control_port_req(bl_hw, 
                (params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED)) != 0,
                sta->sta_idx);

#ifdef CONFIG_PRIV_CH_SWITCH
        if (BL_VIF_TYPE(vif) == NL80211_IFTYPE_STATION && 
            (params->sta_flags_set&BIT(NL80211_STA_FLAG_AUTHORIZED)) && 
            vif->sta.ap != NULL)
        {
            struct bl_vif *found_vif = NULL;
            struct cfg80211_chan_def *sta_chan_def = 
                                 &bl_hw->chanctx_table[vif->ch_index].chan_def;
            struct ieee80211_channel *sta_chan = sta_chan_def->chan;

            if (sta_chan == NULL) {
                BL_DBG_MSG("%s, chan NULL\n", __func__);
            } else {
                BL_DBG_MSG("%s, sta port open, vif:0x%p, vif->index:%d, vif type:%d, ch_ctx_indx:%d\r\n", 
                       __func__, vif, vif->vif_index,
                       BL_VIF_TYPE(vif), vif->ch_index);
                BL_DBG_MSG("started sta band:%d, prim20_freq:%d, center1_freq:%d, center2_freq:%d, flags:0x%x\r\n", 
                        sta_chan->band, sta_chan->center_freq, 
                        sta_chan_def->center_freq1, 
                        sta_chan_def->center_freq2, 
                        sta_chan->flags);
                
                list_for_each_entry(found_vif, &bl_hw->vifs, list) {
                    if (BL_VIF_TYPE(found_vif) == NL80211_IFTYPE_AP &&
                        found_vif->ch_index != BL_CH_NOT_SET && found_vif->up)
                    {
                        struct bl_chanctx *ctxt = 
                                   &bl_hw->chanctx_table[found_vif->ch_index];

                        BL_DBG_MSG("%s %u, ap ctxt->chan_def.chan:0x%p\n", __func__, __LINE__,
                               ctxt->chan_def.chan);

                        if (ctxt->chan_def.chan == NULL)
                            continue;
                            
                        BL_DBG_MSG("started ap band:%d, bw type:%d, center_freq:%d, center1_freq:%d, center2_freq:%d\r\n", 
                                ctxt->chan_def.chan->band, 
                                bw2chnl[ctxt->chan_def.width], 
                                ctxt->chan_def.chan->center_freq, 
                                ctxt->chan_def.center_freq1, 
                                ctxt->chan_def.center_freq2);

                        BL_DBG_MSG("%s %u, ap ctxt->chan_def.chan:0x%p\n", __func__, __LINE__,
                               ctxt->chan_def.chan);
                        
                        if (sta_chan->center_freq != 
                            ctxt->chan_def.chan->center_freq)
                        {
                            BL_DBG("%s, sta's freq not same as ap's freq\r\n", 
                                   __func__);
                        } else {
                            BL_DBG("%s, sta's freq same as ap's freq\r\n", __func__);
                            continue;
                        }
                    
                        bl_send_priv_channel_switch(bl_hw, vif, 
                                                    found_vif, sta_chan_def,
                                                    sta_chan);
                        
                        break;
                    }
                }
            }
        }
#endif
    }

#ifdef CONFIG_MESH
    if (BL_VIF_TYPE(vif) == NL80211_IFTYPE_MESH_POINT) {
        if (params->sta_modify_mask & STATION_PARAM_APPLY_PLINK_STATE) {
            if (params->plink_state < NUM_NL80211_PLINK_STATES) {
                bl_send_mesh_peer_update_ntf(bl_hw, vif, sta->sta_idx, 
                                             params->plink_state);
            }
        }
        if (params->local_pm != NL80211_MESH_POWER_UNKNOWN) {
            sta->mesh_pm = params->local_pm;
            bl_update_mesh_power_mode(vif);
        }
    }
#endif

    if (params->vlan) {
        uint8_t vlan_idx;

        vif = netdev_priv(params->vlan);
        vlan_idx = vif->vif_index;

        if (sta->vlan_idx != vlan_idx) {
            struct bl_vif *old_vif;
            
            old_vif = bl_hw->vif_table[sta->vlan_idx];
            bl_txq_sta_switch_vif(sta, old_vif, vif);
            sta->vlan_idx = vlan_idx;

            if ((BL_VIF_TYPE(vif) == NL80211_IFTYPE_AP_VLAN) &&
                (vif->use_4addr)) {
                WARN((vif->ap_vlan.sta_4a),
                     "4A AP_VLAN interface with more than one sta");
                vif->ap_vlan.sta_4a = sta;
            }

            if ((BL_VIF_TYPE(old_vif) == NL80211_IFTYPE_AP_VLAN) &&
                (old_vif->use_4addr)) {
                old_vif->ap_vlan.sta_4a = NULL;
            }
        }
    }

    return 0;
}

#ifdef CONFIG_PRIV_CH_SWITCH
static void ch_switch_work_task(struct work_struct *ws)
{
    struct bl_hw *bl_hw = container_of(ws, struct bl_hw, ch_switch_work.work);
    
    BL_DBG(BL_FN_ENTRY_STR);
    
    if (bl_hw->ch_switch_sta_vif != NULL && bl_hw->ch_switch_sta_vif->up &&
        bl_hw->ch_switch_sta_vif->ch_index != BL_CH_NOT_SET &&
        bl_hw->ch_switch_ap_vif != NULL)
    {
        BL_DBG_MSG("ch_switch_delayed, sta band:%d, prim20_freq:%d, center1_freq:%d, center2_freq:%d, flags:0x%x\r\n", 
                bl_hw->ch_switch_sta_chan->band, 
                bl_hw->ch_switch_sta_chan->center_freq, 
                bl_hw->ch_switch_sta_chan_def.center_freq1, 
                bl_hw->ch_switch_sta_chan_def.center_freq2, 
                bl_hw->ch_switch_sta_chan->flags);

        bl_send_priv_channel_switch(bl_hw, bl_hw->ch_switch_sta_vif, 
                                    bl_hw->ch_switch_ap_vif, &bl_hw->ch_switch_sta_chan_def,
                                    bl_hw->ch_switch_sta_chan);

        bl_hw->ch_switch_sta_vif = NULL;
        bl_hw->ch_switch_ap_vif = NULL;
        bl_hw->ch_switch_sta_chan = NULL;
    }
}
#endif

/**
 * @start_ap: Start acting in AP mode defined by the parameters.
 */
static int bl_cfg80211_start_ap(struct wiphy *wiphy, struct net_device *dev,
                                      struct cfg80211_ap_settings *settings)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *ap_vif = netdev_priv(dev);
    struct apm_start_cfm apm_start_cfm;
    struct bl_ipc_elem_var elem;
    struct bl_sta *sta;
    int error = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Forward the information to the LMAC */
    if ((error = bl_send_apm_start_req(bl_hw, ap_vif, settings, &apm_start_cfm, &elem)))
        goto end;

    // Check the status
    switch (apm_start_cfm.status)
    {
        case CO_OK:
        {
            u8 txq_status = 0;
            
            ap_vif->ap.bcmc_index = apm_start_cfm.bcmc_idx;
            ap_vif->ap.flags = 0;
            ap_vif->ap.bcn_interval = settings->beacon_interval;
            sta = &bl_hw->sta_table[apm_start_cfm.bcmc_idx];
            sta->valid = true;
            sta->aid = 0;
            sta->sta_idx = apm_start_cfm.bcmc_idx;
            sta->ch_idx = apm_start_cfm.ch_idx;
            sta->vif_idx = ap_vif->vif_index;
            sta->qos = false;
            sta->acm = 0;
            sta->ps.active = false;
            sta->listen_interval = 5;
            
            bl_mu_group_sta_init(sta, NULL);
            
            spin_lock_bh(&bl_hw->cb_lock);
            bl_chanctx_link(ap_vif, apm_start_cfm.ch_idx,
                              &settings->chandef);
                              
            if (bl_hw->cur_chanctx != apm_start_cfm.ch_idx) {
                txq_status = BL_TXQ_STOP_CHAN;
            }
            
            bl_txq_vif_init(bl_hw, ap_vif, txq_status);
            spin_unlock_bh(&bl_hw->cb_lock);

            netif_tx_start_all_queues(dev);
            netif_carrier_on(dev);
            error = 0;

            #ifdef CONFIG_BL_RADAR
            /* If the AP channel is already the active, we probably skip radar
               activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
               ctxt). In anycase retest if radar detection must be activated
             */
            if (txq_status == 0) {
                bl_radar_detection_enable_on_cur_channel(bl_hw);
            }
            #endif
            break;
        }
        case CO_BUSY:
            error = -EINPROGRESS;
            break;
        case CO_OP_IN_PROGRESS:
            error = -EALREADY;
            break;
        default:
            error = -EIO;
            break;
    }

    if (error) {
        netdev_info(dev, "Failed to start AP (%d)", error);
    } else {
#ifdef CONFIG_PRIV_CH_SWITCH
        struct bl_vif *found_vif = NULL;
        struct bl_chanctx *ctxt = &bl_hw->chanctx_table[ap_vif->ch_index];
#endif

        netdev_info(dev, "AP started: ch=%d, bcmc_idx=%d",
                    ap_vif->ch_index, ap_vif->ap.bcmc_index);

#ifdef CONFIG_PRIV_CH_SWITCH
        BL_DBG_MSG("%s, ap started, vif:0x%p, vif->index:%d, BL_VIF_TYPE:%d, ch_ctx_indx:%d\r\n", 
                   __func__, ap_vif, ap_vif->vif_index,
                   BL_VIF_TYPE(ap_vif), ap_vif->ch_index);
        BL_DBG_MSG("started ap band:%d, bw type:%d, center_freq:%d, center1_freq:%d, center2_freq:%d\r\n", 
                    ctxt->chan_def.chan->band, bw2chnl[ctxt->chan_def.width], 
                    ctxt->chan_def.chan->center_freq, 
                    ctxt->chan_def.center_freq1, 
                    ctxt->chan_def.center_freq2);
 
        list_for_each_entry(found_vif, &bl_hw->vifs, list) {
            if (BL_VIF_TYPE(found_vif)==NL80211_IFTYPE_STATION && 
                found_vif->ch_index!=BL_CH_NOT_SET && found_vif->up)
            {
                struct cfg80211_chan_def *sta_chan_def = 
                           &bl_hw->chanctx_table[found_vif->ch_index].chan_def;
                struct ieee80211_channel *sta_chan = sta_chan_def->chan;

                if (sta_chan == NULL) {
                    BL_DBG_MSG("%s, sta chan NULL\n", __func__);
                    continue;
                }
                bl_hw->ch_switch_sta_chan_def = *sta_chan_def;

                if (ctxt->chan_def.chan == NULL) {
                    BL_DBG_MSG("%s, ap chan NULL\n", __func__);
                    continue;
                }

                BL_DBG_MSG("started sta band:%d, prim20_freq:%d, center1_freq:%d, center2_freq:%d, flags:0x%x\r\n", 
                        sta_chan->band, sta_chan->center_freq, 
                        sta_chan_def->center_freq1, 
                        sta_chan_def->center_freq2, 
                        sta_chan->flags);

                if (sta_chan->center_freq != ctxt->chan_def.chan->center_freq) {
                    BL_DBG("%s, sta's freq not same as ap's freq\r\n", __func__);
                } else {
                    BL_DBG("%s, sta's freq same as ap's freq\r\n", __func__);
                    continue;
                }

                bl_hw->ch_switch_ap_vif = ap_vif;
                bl_hw->ch_switch_sta_vif = found_vif;
                bl_hw->ch_switch_sta_chan = sta_chan;

                INIT_DELAYED_WORK(&bl_hw->ch_switch_work, ch_switch_work_task);
                schedule_delayed_work(&bl_hw->ch_switch_work, 
                                      msecs_to_jiffies(100));

                break;
            }
        }

        if (bl_hw->ch_switch_ap_vif == NULL) {
            BL_DBG("%s, no sta started before ap\r\n", __func__);
        }
#endif
    }

  end:
    bl_ipc_elem_var_deallocs(bl_hw, &elem);

    return error;
}


/**
 * @change_beacon: Change the beacon parameters for an access point mode
 *    interface. This should reject the call when AP mode wasn't started.
 */
static int bl_cfg80211_change_beacon(struct wiphy *wiphy, 
                                               struct net_device *dev, 
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
                                               struct cfg80211_beacon_data *info)
#else
                                               struct cfg80211_ap_update *params)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
    struct cfg80211_beacon_data *info = &params->beacon;
#endif
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_bcn *bcn = &vif->ap.bcn;
    struct bl_ipc_elem_var elem;
    int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
    u8 *buf;
    int error = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    mutex_lock(&bl_hw->mutex);
  
    // Build the beacon
    buf = bl_build_bcn(bl_hw, bcn, info);
    if (!buf) {
        printk("%s, no mem for bcn\r\n", __func__);        

        mutex_unlock(&bl_hw->mutex);

        return -ENOMEM;
    }

    #if 0
    printk("%s, bcn->len:%d, var_offset:%d\r\n", __func__, bcn->len, var_offset);
    for_each_element_t(ie_elem, buf+var_offset, bcn->len-var_offset) {
        printk("id %d, datalen:%d\r\n", ie_elem->id, ie_elem->datalen);
        bl_dump((u8 *)ie_elem, ie_elem->datalen+2);
    }
    #endif

    //bl_change_mpdu_density(buf+var_offset, bcn->len-var_offset);
    bl_change_ch_width_in_opmode(buf+var_offset, bcn->len-var_offset);

    elem.addr = buf;
    elem.size = bcn->len;

    BL_DBG_MSG("%s, vif->ap.csa:0x%p\n", __func__, vif->ap.csa);
    
    if (vif->ap.csa && vif->ap.csa->bcn.len != 0 && 
        vif->ap.csa->bcn.head_len != 0) 
    {
        u8 * temp_buf = NULL;
        
        BL_DBG_MSG("%s, change_beacon when csa ongoing, replace beacon after csa, csa bcn head:0x%p, tail:0x%p, ies:0x%p\n", 
                   __func__, vif->ap.csa->bcn.head, vif->ap.csa->bcn.tail,
                   vif->ap.csa->bcn.ies);

        temp_buf = bl_build_priv_after_csa_bcn(bl_hw, &vif->ap.csa->bcn, 
                                               &vif->ap.bcn);
        if (temp_buf) {
            const struct element_t *ie_elem = NULL;

            BL_DBG_MSG("update after-csa beacon\r\n");
            
            for_each_element_t(ie_elem, temp_buf+var_offset, vif->ap.csa->bcn.len-var_offset) {
                //printk("id %d, datalen:%d\r\n", ie_elem->id, ie_elem->datalen);
                //bl_print_elem((u8 *)ie_elem, ie_elem->datalen+2);
                
                if (ie_elem->id == WLAN_EID_DS_PARAMS) {
                    *((u8 *)ie_elem + 2) = vif->ap.csa->channel_number;
                }
            }
        
            bl_ipc_elem_var_deallocs(bl_hw, &vif->ap.csa->elem);
            vif->ap.csa->elem.addr = temp_buf;
            vif->ap.csa->elem.size = vif->ap.csa->bcn.len;
        } else {
            printk("%s, fail to alloc mem\n", __func__);
        }
    } else {
        BL_DBG_MSG("%s, call bcn_change\n", __func__);
        
        // Forward the information to the LMAC
        error = bl_send_bcn_change(bl_hw, vif->vif_index, elem.addr,
                                   bcn->len, bcn->head_len, bcn->tim_len, NULL);
    }

    bl_ipc_elem_var_deallocs(bl_hw, &elem);

    mutex_unlock(&bl_hw->mutex);

    return error;
}

/**
 * * @stop_ap: Stop being an AP, including stopping beaconing.
 */
static int bl_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
                                      , unsigned int link_id
#endif
)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_sta *sta;

    BL_DBG(BL_FN_ENTRY_STR);

    mutex_lock(&bl_hw->mutex);

    #ifdef CONFIG_BL_RADAR
    bl_radar_cancel_cac(&bl_hw->radar);
    #endif
    
    bl_send_apm_stop_req(bl_hw, bl_vif);
    spin_lock_bh(&bl_hw->cb_lock);
    bl_chanctx_unlink(bl_vif);
    spin_unlock_bh(&bl_hw->cb_lock);

    /* delete any remaining STA*/
    while (!list_empty(&bl_vif->ap.sta_list)) {
        bl_cfg80211_del_station(wiphy, dev, NULL);
    }

    /* delete BC/MC STA */
    sta = &bl_hw->sta_table[bl_vif->ap.bcmc_index];
    bl_txq_vif_deinit(bl_hw, bl_vif);
    bl_del_bcn(&bl_vif->ap.bcn);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
    bl_del_csa(bl_vif);
#endif

    if (mutex_is_locked(&bl_hw->mutex)) {
        mutex_unlock(&bl_hw->mutex);
    }

    netif_tx_stop_all_queues(dev);
    netif_carrier_off(dev);

    netdev_info(dev, "AP Stopped");

    return 0;
}

/**
 * @set_monitor_channel: Set the monitor mode channel for the device. If other
 *    interfaces are active this callback should reject the configuration.
 *    If no interfaces are active or the device is down, the channel should
 *    be stored for when a monitor interface becomes active.
 *
 * Also called internaly with chandef set to NULL simply to retrieve the channel
 * configured at firmware level.
 */
static int bl_cfg80211_set_monitor_channel(struct wiphy *wiphy,
                                             struct cfg80211_chan_def *chandef)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif;
    struct me_config_monitor_cfm cfm;
    
    BL_DBG(BL_FN_ENTRY_STR);

    if (bl_hw->monitor_vif == BL_INVALID_VIF)
        return -EINVAL;

    bl_vif = bl_hw->vif_table[bl_hw->monitor_vif];

    // Do nothing if monitor interface is already configured with the requested channel
    if (bl_chanctx_valid(bl_hw, bl_vif->ch_index)) {
        struct bl_chanctx *ctxt;
        
        ctxt = &bl_vif->bl_hw->chanctx_table[bl_vif->ch_index];
        if (chandef && cfg80211_chandef_identical(&ctxt->chan_def, chandef))
            return 0;
    }

    // Always send command to firmware. It allows to retrieve channel context index
    // and its configuration.
    if (bl_send_config_monitor_req(bl_hw, chandef, &cfm))
        return -EIO;

    // Always re-set channel context info
    bl_chanctx_unlink(bl_vif);

    // If there is also a STA interface not yet connected then monitor interface
    // will only have a channel context after the connection of the STA interface.
    if (cfm.chan_index != BL_CH_NOT_SET)
    {
        struct cfg80211_chan_def mon_chandef;

        if (bl_hw->vif_started > 1) {
            // In this case we just want to update the channel context index not
            // the channel configuration
            bl_chanctx_link(bl_vif, cfm.chan_index, NULL);
            return -EBUSY;
        }

        mon_chandef.chan = ieee80211_get_channel(wiphy, cfm.chan.prim20_freq);
        mon_chandef.center_freq1 = cfm.chan.center1_freq;
        mon_chandef.center_freq2 = cfm.chan.center2_freq;
        mon_chandef.width =  chnl2bw[cfm.chan.type];
        bl_chanctx_link(bl_vif, cfm.chan_index, &mon_chandef);
    }

    return 0;
}

/**
 * @probe_client: probe an associated client, must return a cookie that it
 *    later passes to cfg80211_probe_status().
 */
int bl_cfg80211_probe_client(struct wiphy *wiphy, struct net_device *dev,
                                     const u8 *peer, u64 *cookie)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_sta *sta = NULL;
    struct apm_probe_client_cfm cfm;

    if ((BL_VIF_TYPE(vif) != NL80211_IFTYPE_AP) &&
        (BL_VIF_TYPE(vif) != NL80211_IFTYPE_AP_VLAN) &&
        (BL_VIF_TYPE(vif) != NL80211_IFTYPE_P2P_GO) &&
        (BL_VIF_TYPE(vif) != NL80211_IFTYPE_MESH_POINT))
        return -EINVAL;

    list_for_each_entry(sta, &vif->ap.sta_list, list) {
        if (sta->valid && ether_addr_equal(sta->mac_addr, peer))
            break;
    }

    if (!sta)
        return -EINVAL;

    bl_send_apm_probe_req(bl_hw, vif, sta, &cfm);

    if (cfm.status != CO_OK)
        return -EINVAL;

    *cookie = (u64)cfm.probe_id;
    
    return 0;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 7, 19)
/**
 * @mgmt_frame_register: Notify driver that a management frame type was
 *    registered. Note that this callback may not sleep, and cannot run
 *    concurrently with itself.
 */
void bl_cfg80211_mgmt_frame_register(struct wiphy *wiphy,
                            struct wireless_dev *wdev, u16 frame_type, bool reg)
{
}
#endif

/**
 * @set_wiphy_params: Notify that wiphy parameters have changed;
 *    @changed bitfield (see &enum wiphy_params_flags) describes which values
 *    have changed. The actual parameter values are available in
 *    struct wiphy. If returning an error, no value should be changed.
 */
static int bl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
    return 0;
}


/**
 * @set_tx_power: set the transmit power according to the parameters,
 *    the power passed is in mBm, to get dBm use MBM_TO_DBM(). The
 *    wdev may be %NULL if power was set for the wiphy, and will
 *    always be %NULL unless the driver supports per-vif TX power
 *    (as advertised by the nl80211 feature flag.)
 */
static int bl_cfg80211_set_tx_power(struct wiphy *wiphy, 
         struct wireless_dev *wdev, enum nl80211_tx_power_setting type, int mbm)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *vif;
    s8 pwr;
    int res = 0;

    if (type == NL80211_TX_POWER_AUTOMATIC) {
        pwr = 0x7f;
    } else {
        pwr = MBM_TO_DBM(mbm);
    }

    if (wdev) {
        vif = container_of(wdev, struct bl_vif, wdev);
        res = bl_send_set_power(bl_hw, vif->vif_index, pwr, NULL);
    } else {
        list_for_each_entry(vif, &bl_hw->vifs, list) {
            res = bl_send_set_power(bl_hw, vif->vif_index, pwr, NULL);
            if (res)
                break;
        }
    }

    return res;
}

/**
 * @set_power_mgmt: set the power save to one of those two modes:
 *  Power-save off
 *  Power-save on - Dynamic mode
 */
static int bl_cfg80211_set_power_mgmt(struct wiphy *wiphy,
                              struct net_device *dev, bool enabled, int timeout)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    u8 ps_mode;

    BL_DBG(BL_FN_ENTRY_STR);
    
    if (timeout >= 0)
        netdev_info(dev, "Ignore timeout value %d", timeout);
 
    if (!(bl_hw->version_cfm.features & BIT(MM_FEAT_PS_BIT)))
        enabled = false;

    BL_DBG("%s enable = %d, to=%d\n", __func__, enabled, timeout);

    if (enabled) {
        /* Switch to Dynamic Power Save */
        ps_mode = MM_PS_MODE_ON_DYN;
    } else {
        /* Exit Power Save */
        ps_mode = MM_PS_MODE_OFF;
        /* debug: directly skip set ps mode */
        
        return 0;
    }

    return bl_send_me_set_ps_mode(bl_hw, ps_mode);
}

/**
 * @set_txq_params: Set TX queue parameters
 */
static int bl_cfg80211_set_txq_params(struct wiphy *wiphy, 
                   struct net_device *dev, struct ieee80211_txq_params *params)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    u8 hw_queue, aifs, cwmin, cwmax;
    u32 param;

    BL_DBG(BL_FN_ENTRY_STR);

    hw_queue = bl_ac2hwq[0][params->ac];

    aifs  = params->aifs;
    cwmin = fls(params->cwmin);
    cwmax = fls(params->cwmax);

    /* Store queue information in general structure */
    param  = (u32) (aifs << 0);
    param |= (u32) (cwmin << 4);
    param |= (u32) (cwmax << 8);
    param |= (u32) (params->txop) << 12;

    /* Send the MM_SET_EDCA_REQ message to the FW */
    return bl_send_set_edca(bl_hw, hw_queue, param, false, bl_vif->vif_index);
}


/**
 * @remain_on_channel: Request the driver to remain awake on the specified
 *    channel for the specified duration to complete an off-channel
 *    operation (e.g., public action frame exchange). When the driver is
 *    ready on the requested channel, it must indicate this with an event
 *    notification by calling cfg80211_ready_on_channel().
 */
static int
bl_cfg80211_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
                                         struct ieee80211_channel *chan,
                                         unsigned int duration, u64 *cookie)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(wdev->netdev);
    struct bl_roc *roc;
    int error;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Check that no other RoC procedure has been launched */
    if (bl_hw->roc)
        return -EBUSY;

    /* Allocate a temporary RoC element */
    roc = kmalloc(sizeof(struct bl_roc), GFP_KERNEL);
    if (!roc)
        return -ENOMEM;

    /* Initialize the RoC information element */
    roc->vif = bl_vif;
    roc->chan = chan;
    roc->duration = duration;
    roc->internal = false;
    roc->on_chan = false;

    /* Initialize the OFFCHAN TX queue to allow off-channel transmissions */
    bl_txq_offchan_init(bl_vif);

    /* Forward the information to the FMAC */
    bl_hw->roc = roc;
    error = bl_send_roc(bl_hw, bl_vif, chan, duration);

    if (error == 0) {
        *cookie = (u64)(bl_hw->roc_cookie);
    } else {
        kfree(roc);
        bl_hw->roc = NULL;
        bl_txq_offchan_deinit(bl_vif);
    }

    return error;
}

/**
 * @cancel_remain_on_channel: Cancel an on-going remain-on-channel operation.
 *    This allows the operation to be terminated prior to timeout based on
 *    the duration value.
 */
static int bl_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
                                          struct wireless_dev *wdev, u64 cookie)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    //struct bl_vif *bl_vif = netdev_priv(wdev->netdev);

    BL_DBG(BL_FN_ENTRY_STR);

    if (!bl_hw->roc)
        return 0;

    /* Forward the information to the FMAC */
    return bl_send_cancel_roc(bl_hw);
}

/**
 * @dump_survey: get site survey information.
 */
static int bl_cfg80211_dump_survey(struct wiphy *wiphy,
                 struct net_device *netdev, int idx, struct survey_info *info)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct ieee80211_supported_band *sband;
    struct bl_survey_info *bl_survey;

    BL_DBG(BL_FN_ENTRY_STR);

    if (idx >= ARRAY_SIZE(bl_hw->survey))
        return -ENOENT;

    bl_survey = &bl_hw->survey[idx];

    // Check if provided index matches with a supported 2.4GHz channel
    sband = wiphy->bands[NL80211_BAND_2GHZ];
    if (sband && idx >= sband->n_channels) {
        idx -= sband->n_channels;
        sband = NULL;
    }

    if (!sband) {
        // Check if provided index matches with a supported 5GHz channel
        sband = wiphy->bands[NL80211_BAND_5GHZ];

        if (!sband || idx >= sband->n_channels)
            return -ENOENT;
    }

    // Fill the survey
    info->channel = &sband->channels[idx];
    info->filled = bl_survey->filled;

    if (bl_survey->filled != 0) {
        SURVEY_TIME(info) = (u64)bl_survey->chan_time_ms;
        SURVEY_TIME_BUSY(info) = (u64)bl_survey->chan_time_busy_ms;
        info->noise = bl_survey->noise_dbm;

        // Set the survey report as not used
        bl_survey->filled = 0;
    }

    return 0;
}

/**
 * @get_channel: Get the current operating channel for the virtual interface.
 *    For monitor interfaces, it should return %NULL unless there's a single
 *    current monitoring channel.
 */
static int bl_cfg80211_get_channel(struct wiphy *wiphy,
                                           struct wireless_dev *wdev,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
                                           unsigned int link_id,
#endif
                                           struct cfg80211_chan_def *chandef) 
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = container_of(wdev, struct bl_vif, wdev);
    struct bl_chanctx *ctxt;

    if (!bl_vif->up) {
        return -ENODATA;
    }

    if (bl_vif->vif_index == bl_hw->monitor_vif)
    {
        //retrieve channel from firmware
        bl_cfg80211_set_monitor_channel(wiphy, NULL);
    }

    //Check if channel context is valid
    if(!bl_chanctx_valid(bl_hw, bl_vif->ch_index)){
        return -ENODATA;
    }

    ctxt = &bl_hw->chanctx_table[bl_vif->ch_index];
    *chandef = ctxt->chan_def;

    return 0;
}

/**
 * @mgmt_tx: Transmit a management frame.
 */
static int bl_cfg80211_mgmt_tx(struct wiphy *wiphy,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
                                        struct net_device *dev,
#else
                                        struct wireless_dev *wdev,
#endif //3.6
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
                                        struct cfg80211_mgmt_tx_params *params,
#else //3.14
                                        struct ieee80211_channel *chan,
                                        bool offchan,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
                                        enum nl80211_channel_type channel_type,
                                        bool channel_type_valid,
#endif
                                        unsigned int wait, const u8 *buf, 
                                        size_t len,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
                                        bool no_cck,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
                                        bool dont_wait_for_ack,
#endif
#endif //3.14
                                        u64 *cookie)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
    struct bl_vif *bl_vif = netdev_priv(dev);
#else
    struct bl_vif *bl_vif = netdev_priv(wdev->netdev);
#endif
    struct bl_sta *bl_sta;
    int error = 0;
    bool ap = false;
    bool offchan_i = false;
    int n_csa_offsets = 0;
    const u16 *csa_offsets = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
    struct ieee80211_channel *chan = params->chan;
    const u8 *buf = params->buf;
    size_t len = params->len;
    bool no_cck = params->no_cck;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
    bool no_cck;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
    struct ieee80211_mgmt *mgmt = (void *)params->buf;
#else
    struct ieee80211_mgmt *mgmt = (void *)buf;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    n_csa_offsets = params->n_csa_offsets;
    csa_offsets = params->csa_offsets;
#endif

    /* Check if provided VIF is an AP or a STA one */
    switch (BL_VIF_TYPE(bl_vif)) {
        case NL80211_IFTYPE_AP_VLAN:
            bl_vif = bl_vif->ap_vlan.master;
        /* fall through */
        case NL80211_IFTYPE_AP:
        case NL80211_IFTYPE_P2P_GO:
        case NL80211_IFTYPE_MESH_POINT:
            ap = true;
            break;
        case NL80211_IFTYPE_STATION:
        case NL80211_IFTYPE_P2P_CLIENT:
        default:
            break;
    }

    /* Get STA on which management frame has to be sent */
    bl_sta = bl_retrieve_sta(bl_hw, bl_vif, mgmt->da, mgmt->frame_control, ap);

    if (ap || bl_sta)
        goto send_frame;

    /* Not an AP interface sending frame to unknown STA:
     * This is allowed for external authentication */
    if ((bl_vif->sta.flags & BL_STA_EXT_AUTH) && 
        ieee80211_is_auth(mgmt->frame_control)) 
    {
        printk("%s send auth\r\n", __func__);
        
        goto send_frame;
    }

    /* Otherwise ROC is needed */
    if (!chan)
        return -EINVAL;

    if (bl_hw->roc) {
        /* Check if RoC channel is the same than the required one */
        if ((bl_hw->roc->vif != bl_vif) ||
            (bl_hw->roc->chan->center_freq != chan->center_freq))
            return -EINVAL;

    } else {
        u64 cookie;
        int error;

        /* Start a ROC procedure for 30ms */
        error = bl_cfg80211_remain_on_channel(wiphy, wdev, chan, 30, &cookie);
        if (error)
            return error;

        /* internal RoC, no need to inform user space about it */
        if (bl_hw->roc)
            bl_hw->roc->internal = true;
    }

    offchan_i = true;

send_frame:
        /* Push the management frame on the TX path */
        error = bl_start_mgmt_xmit(bl_vif, bl_sta, buf, len, no_cck,
                                   n_csa_offsets,csa_offsets, offchan_i, cookie);
    return error;
}


/**
 * @start_radar_detection: Start radar detection in the driver.
 */
static int bl_cfg80211_start_radar_detection(struct wiphy *wiphy,
                                               struct net_device *dev,
                                               struct cfg80211_chan_def *chandef,
                                               u32 cac_time_ms
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
                                               , int link_id
#endif
                                            )
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct apm_start_cac_cfm cfm;

    #ifdef CONFIG_BL_RADAR
    bl_radar_start_cac(&bl_hw->radar, cac_time_ms, bl_vif);
    #endif
    
    bl_send_apm_start_cac_req(bl_hw, bl_vif, chandef, &cfm);

    if (cfm.status == CO_OK) {
        spin_lock_bh(&bl_hw->cb_lock);
        bl_chanctx_link(bl_vif, cfm.ch_idx, chandef);

        #ifdef CONFIG_BL_RADAR
        if (bl_hw->cur_chanctx == bl_vif->ch_index)
            bl_radar_detection_enable(&bl_hw->radar,
                                      BL_RADAR_DETECT_REPORT, BL_RADAR_RIU);
        #endif
        
        spin_unlock_bh(&bl_hw->cb_lock);
    } else {
        return -EIO;
    }

    return 0;
}

/**
 * @update_ft_ies: Provide updated Fast BSS Transition information to the
 *    driver. If the SME is in the driver/firmware, this information can be
 *    used in building Authentication and Reassociation Request frames.
 */
static int bl_cfg80211_update_ft_ies(struct wiphy *wiphy,
                                     struct net_device *dev,
                                     struct cfg80211_update_ft_ies_params *ftie)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *vif = netdev_priv(dev);
    const struct element_t *rsne = NULL, *mde = NULL, *fte = NULL, *elem;
    bool ft_in_non_rsn = false;
    int fties_len = 0;
    u8 *ft_assoc_ies, *pos;

    if ((BL_VIF_TYPE(vif) != NL80211_IFTYPE_STATION) ||
        (vif->sta.ft_assoc_ies == NULL))
        return 0;

    for_each_element_t(elem, ftie->ie, ftie->ie_len) {
        if (elem->id == WLAN_EID_RSN)
            rsne = elem;
        else if (elem->id == WLAN_EID_MOBILITY_DOMAIN)
            mde = elem;
        else if (elem->id == WLAN_EID_FAST_BSS_TRANSITION)
            fte = elem;
        else
            netdev_warn(dev, "Unexpected FT element %d\n", elem->id);
    }
    
    if (!mde) {
        // maybe just test MDE for
        netdev_warn(dev, "Didn't find Mobility_Domain Element\n");
        
        return 0;
    } else if (!rsne && !fte) {
        // not sure this happen in real life ...
        ft_in_non_rsn = true;
    } else if (!rsne || !fte) {
        netdev_warn(dev, "Didn't find RSN or Fast Transition Element\n");
        
        return 0;
    }

    for_each_element_t(elem, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len) {
        if ((elem->id == WLAN_EID_RSN) ||
            (elem->id == WLAN_EID_MOBILITY_DOMAIN) ||
            (elem->id == WLAN_EID_FAST_BSS_TRANSITION))
            fties_len += elem->datalen + sizeof(struct element_t);
    }

    ft_assoc_ies = kmalloc(vif->sta.ft_assoc_ies_len - fties_len + ftie->ie_len,
                           GFP_KERNEL);
    if (!ft_assoc_ies) {
        netdev_warn(dev, "Fail to allocate buffer for association elements");
    }

    // Recopy current Association Elements one at a time and replace FT
    // element with updated version.
    pos = ft_assoc_ies;
    
    for_each_element_t(elem, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len) {
        if (elem->id == WLAN_EID_RSN) {
            if (ft_in_non_rsn) {
                netdev_warn(dev, "Found RSN element in non RSN FT");
                goto abort;
            } else if (!rsne) {
                netdev_warn(dev, "Found several RSN element");
                goto abort;
            } else {
                memcpy(pos, rsne, sizeof(*rsne) + rsne->datalen);
                pos += sizeof(*rsne) + rsne->datalen;
                rsne = NULL;
            }
        } else if (elem->id == WLAN_EID_MOBILITY_DOMAIN) {
            if (!mde) {
                netdev_warn(dev, "Found several Mobility Domain element");
                goto abort;
            } else {
                memcpy(pos, mde, sizeof(*mde) + mde->datalen);
                pos += sizeof(*mde) + mde->datalen;
                mde = NULL;
            }
        }
        else if (elem->id == WLAN_EID_FAST_BSS_TRANSITION) {
            if (ft_in_non_rsn) {
                netdev_warn(dev, "Found Fast Transition element in non RSN FT");
                goto abort;
            } else if (!fte) {
                netdev_warn(dev, "found several Fast Transition element");
                goto abort;
            } else {
                memcpy(pos, fte, sizeof(*fte) + fte->datalen);
                pos += sizeof(*fte) + fte->datalen;
                fte = NULL;
            }
        }
        else {
            // Put FTE after MDE if non present in Association Element
            if (fte && !mde) {
                memcpy(pos, fte, sizeof(*fte) + fte->datalen);
                pos += sizeof(*fte) + fte->datalen;
                fte = NULL;
            }
            
            memcpy(pos, elem, sizeof(*elem) + elem->datalen);
            pos += sizeof(*elem) + elem->datalen;
        }
    }
    
    if (fte) {
        memcpy(pos, fte, sizeof(*fte) + fte->datalen);
        pos += sizeof(*fte) + fte->datalen;
        fte = NULL;
    }

    kfree(vif->sta.ft_assoc_ies);
    vif->sta.ft_assoc_ies = ft_assoc_ies;
    vif->sta.ft_assoc_ies_len = pos - ft_assoc_ies;

    if (vif->sta.flags & BL_STA_FT_OVER_DS) {
        struct sm_connect_cfm sm_connect_cfm;
        struct cfg80211_connect_params sme;

        vif->sta.flags &= ~BL_STA_FT_OVER_DS;

        memset(&sme, 0, sizeof(sme));
        rsne = bl_find_elem(WLAN_EID_RSN, vif->sta.ft_assoc_ies,
                            vif->sta.ft_assoc_ies_len);
        if (rsne && bl_rsne_to_connect_params(rsne, &sme)) 
        {
            netdev_warn(dev, "FT RSN parsing failed\n");
            return 0;
        }

        sme.ssid_len = vif->sta.ft_assoc_ies[1];
        sme.ssid = &vif->sta.ft_assoc_ies[2];
        sme.bssid = vif->sta.ft_target_ap;
        sme.ie = &vif->sta.ft_assoc_ies[2 + sme.ssid_len];
        sme.ie_len = vif->sta.ft_assoc_ies_len - (2 + sme.ssid_len);
        sme.auth_type = NL80211_AUTHTYPE_FT;
        
        bl_send_sm_connect_req(bl_hw, vif, &sme, &sm_connect_cfm);
    } else if (vif->sta.flags & BL_STA_FT_OVER_AIR) {
        uint8_t ssid_len;
        
        vif->sta.flags &= ~BL_STA_FT_OVER_AIR;

        // Skip the first element (SSID)
        ssid_len = vif->sta.ft_assoc_ies[1] + 2;
        
        if (bl_send_sm_ft_auth_rsp(bl_hw, vif, &vif->sta.ft_assoc_ies[ssid_len],
                                     vif->sta.ft_assoc_ies_len - ssid_len))
            netdev_err(dev, "FT Over Air: Failed to send updated assoc elem\n");
    }

    return 0;

abort:
    kfree(ft_assoc_ies);
    
    return 0;
}

/**
 * @set_cqm_rssi_config: Configure connection quality monitor RSSI threshold.
 */
static int bl_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
                                         struct net_device *dev,
                                         int32_t rssi_thold, uint32_t rssi_hyst)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);

    return bl_send_cfg_rssi_req(bl_hw, bl_vif->vif_index, rssi_thold, rssi_hyst);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
/**
 *
 * @channel_switch: initiate channel-switch procedure (with CSA). Driver is
 *    responsible for veryfing if the switch is possible. Since this is
 *    inherently tricky driver may decide to disconnect an interface later
 *    with cfg80211_stop_iface(). This doesn't mean driver can accept
 *    everything. It should do it's best to verify requests and reject them
 *    as soon as possible.
 */
static int bl_cfg80211_channel_switch(struct wiphy *wiphy,
                                        struct net_device *dev,
                                        struct cfg80211_csa_settings *params)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_ipc_elem_var elem;
    struct bl_bcn *bcn, *bcn_after;
    struct bl_csa *csa;
    int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
    u16 csa_oft[BCN_MAX_CSA_CPT];
    u8 *buf;
    int i, error = 0;
    //const struct element *element_t = NULL;

    if (vif->ap.csa)
        return -EBUSY;
        
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    if (params->n_counter_offsets_beacon > BCN_MAX_CSA_CPT)
        return -EINVAL;
#endif

    /* Build the new beacon with CSA IE */
    bcn = &vif->ap.bcn;
    buf = bl_build_bcn(bl_hw, bcn, &params->beacon_csa);
    if (!buf)
        return -ENOMEM;

    #if 0
    printk("%s, var_offset:%d\r\n", __func__, var_offset);
    for_each_element_t(ie_elem, buf+var_offset, bcn->len-var_offset) {
        printk("id %d, datalen:%d\r\n", ie_elem->id, ie_elem->datalen);
        bl_dump((u8 *)ie_elem, ie_elem->datalen+2);
    }
    #endif

    //bl_change_mpdu_density(buf+var_offset, bcn->len-var_offset);
    bl_change_ch_width_in_opmode(buf+var_offset, bcn->len-var_offset);

    memset(csa_oft, 0, sizeof(csa_oft));
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 15, 0)
    csa_oft[0] = params->counter_offset_beacon + bcn->head_len +
                 bcn->tim_len;
#else
    for (i = 0; i < params->n_counter_offsets_beacon; i++)
    {
        csa_oft[i] = params->counter_offsets_beacon[i] + bcn->head_len +
                     bcn->tim_len;
    }
#endif


    /* If count is set to 0 (i.e anytime after this beacon) force it to 2 */
    if (params->count == 0) {
        params->count = 2;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 15, 0)
        buf[csa_oft[0]] = 2;
#else
        for (i = 0; i < params->n_counter_offsets_beacon; i++)
        {
            buf[csa_oft[i]] = 2;
        }
#endif
    }

    elem.addr = buf;
    elem.size = bcn->len;
    /* Build the beacon to use after CSA. It will only be sent to fw once
       CSA is over, but do it before sending the beacon as it must be ready
       when CSA is finished. */
       
    csa = kzalloc(sizeof(struct bl_csa), GFP_KERNEL);
    if (!csa) {
        error = -ENOMEM;
        goto end;
    }

    bcn_after = &csa->bcn;
    buf = bl_build_bcn(bl_hw, bcn_after, &params->beacon_after);
    if (!buf) {
        error = -ENOMEM;
        bl_del_csa(vif);
        goto end;
    }

    #if 0
    for_each_element_t(ie_elem, buf+var_offset, bcn_after->len-var_offset) {
        printk("id %d, datalen:%d\r\n", ie_elem->id, ie_elem->datalen);
        bl_dump((u8 *)ie_elem, ie_elem->datalen+2);        
    }
    #endif
    
    bl_change_ch_width_in_opmode(buf+var_offset, bcn_after->len-var_offset);

    csa->elem.addr = buf;
    csa->elem.size = bcn_after->len;
    vif->ap.csa = csa;
    csa->vif = vif;
    csa->chandef = params->chandef;

    BL_DBG_MSG("%s, call bcn_change\n", __func__);
    
    /* Send new Beacon. FW will extract channel and count from the beacon */
    error = bl_send_bcn_change(bl_hw, vif->vif_index, elem.addr,
                               bcn->len, bcn->head_len, bcn->tim_len, csa_oft);
    if (error) {
        bl_del_csa(vif);
        goto end;
    } else {
        INIT_WORK(&csa->work, bl_csa_finish);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
        cfg80211_ch_switch_started_notify(dev, &csa->chandef, 0, 
                                          params->count, false);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(6, 2, 16)
        cfg80211_ch_switch_started_notify(dev, &csa->chandef, 0, 
                                          params->count, false, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
        cfg80211_ch_switch_started_notify(dev, &csa->chandef, 0, 
                                          params->count, false);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 160)
        cfg80211_ch_switch_started_notify(dev, &csa->chandef, 
                                          params->count, false);
#else
        cfg80211_ch_switch_started_notify(dev, &csa->chandef, params->count);
#endif
    }

  end:
    bl_ipc_elem_var_deallocs(bl_hw, &elem);

    return error;
}
#endif

/**
 * @@tdls_mgmt: Transmit a TDLS management frame.
 */
static int bl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
                                         const u8 *peer, 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
                                         int link_id,
#endif
                                         u8 action_code,  u8 dialog_token,
                                         u16 status_code, u32 peer_capability,
                                         bool initiator, const u8 *buf, size_t len)
{
    #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
    bool initiator = false;
    #endif
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    int ret = 0;

    /* make sure we support TDLS */
    if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
        return -ENOTSUPP;

    /* make sure we are in station mode (and connected) */
    if ((BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_STATION) ||
        (!bl_vif->up) || (!bl_vif->sta.ap))
        return -ENOTSUPP;

    /* only one TDLS link is supported */
    if ((action_code == WLAN_TDLS_SETUP_REQUEST) &&
        (bl_vif->sta.tdls_sta) &&
        (bl_vif->tdls_status == TDLS_LINK_ACTIVE)) {
        BL_DBG("%s: only one TDLS link is supported!\n", __func__);
        
        return -ENOTSUPP;
    }

    if ((action_code == WLAN_TDLS_DISCOVERY_REQUEST) &&
        (bl_hw->mod_params->ps_on)) {
        BL_DBG("%s: discovery request is not supported when "
                "power-save is enabled!\n", __func__);
        return -ENOTSUPP;
    }

    switch (action_code) {
        case WLAN_TDLS_SETUP_RESPONSE:
            /* only one TDLS link is supported */
            if ((status_code == 0) && (bl_vif->sta.tdls_sta) &&
                (bl_vif->tdls_status == TDLS_LINK_ACTIVE))
            {
                BL_DBG("%s: only one TDLS link is supported!\n", __func__);
                
                status_code = WLAN_STATUS_REQUEST_DECLINED;
            }
            /* fall-through */
        case WLAN_TDLS_SETUP_REQUEST:
        case WLAN_TDLS_TEARDOWN:
        case WLAN_TDLS_DISCOVERY_REQUEST:
        case WLAN_TDLS_SETUP_CONFIRM:
        case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
            ret = bl_tdls_send_mgmt_packet_data(bl_hw, bl_vif, peer, action_code,
                                                dialog_token, status_code, 
                                                peer_capability, 
                                                initiator, buf, len, 0, NULL);
            break;

        default:
            printk("%s: Unknown TDLS mgmt/action frame %pM\n", __func__, peer);
            ret = -EOPNOTSUPP;
            break;
    }

    if (action_code == WLAN_TDLS_SETUP_REQUEST) {
        bl_vif->tdls_status = TDLS_SETUP_REQ_TX;
    } else if (action_code == WLAN_TDLS_SETUP_RESPONSE) {
        bl_vif->tdls_status = TDLS_SETUP_RSP_TX;
    } else if ((action_code == WLAN_TDLS_SETUP_CONFIRM) && (ret == CO_OK) &&
               bl_vif->sta.tdls_sta) 
    {
        bl_vif->tdls_status = TDLS_LINK_ACTIVE;
        /* Set TDLS active */
        bl_vif->sta.tdls_sta->tdls.active = true;
    }

    return ret;
}

/**
 * @tdls_oper: Perform a high-level TDLS operation (e.g. TDLS link setup).
 */
static int bl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
                                        const u8 *peer, 
                                        enum nl80211_tdls_operation oper)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    int error;

    if (oper != NL80211_TDLS_DISABLE_LINK)
        return 0;

    if (!bl_vif->sta.tdls_sta) {
        printk("%s: TDLS station %pM does not exist\n", __func__, peer);
        
        return -ENOLINK;
    }

    if (memcmp(bl_vif->sta.tdls_sta->mac_addr, peer, ETH_ALEN) == 0) {
        /* Disable Channel Switch */
        if (!bl_send_tdls_cancel_chan_switch_req(bl_hw, bl_vif,
                                                 bl_vif->sta.tdls_sta, NULL))
        {
            bl_vif->sta.tdls_sta->tdls.chsw_en = false;
        }
        
        netdev_info(dev, "Del TDLS sta %d (%pM)",
                 bl_vif->sta.tdls_sta->sta_idx, bl_vif->sta.tdls_sta->mac_addr);
                 
        /* Ensure that we won't process PS change ind */
        spin_lock_bh(&bl_hw->cb_lock);
        bl_vif->sta.tdls_sta->ps.active = false;
        bl_vif->sta.tdls_sta->valid = false;
        spin_unlock_bh(&bl_hw->cb_lock);
        
        bl_txq_sta_deinit(bl_hw, bl_vif->sta.tdls_sta);
        
        error = bl_send_me_sta_del(bl_hw, bl_vif->sta.tdls_sta->sta_idx, true);
        if ((error != 0) && (error != -EPIPE))
            return error;

#ifdef CONFIG_BL_BFMER
        // Disable Beamformer if supported
        bl_bfmer_report_del(bl_hw, bl_vif->sta.tdls_sta);
        bl_mu_group_sta_del(bl_hw, bl_vif->sta.tdls_sta);
#endif /* CONFIG_BL_BFMER */

        /* Set TDLS not active */
        bl_vif->sta.tdls_sta->tdls.active = false;
#ifdef CONFIG_BL_DEBUGFS
        bl_dbgfs_unregister_sta(bl_hw, bl_vif->sta.tdls_sta);
#endif
        // Remove TDLS station
        bl_vif->tdls_status = TDLS_LINK_IDLE;
        bl_vif->sta.tdls_sta = NULL;
    }

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/**
 *  @tdls_channel_switch: Start channel-switching with a TDLS peer. The driver
 *    is responsible for continually initiating channel-switching operations
 *    and returning to the base channel for communication with the AP.
 */
static int bl_cfg80211_tdls_channel_switch(struct wiphy *wiphy,
                                             struct net_device *dev,
                                             const u8 *addr, u8 oper_class,
                                             struct cfg80211_chan_def *chandef)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_sta *bl_sta = bl_vif->sta.tdls_sta;
    struct tdls_chan_switch_cfm cfm;
    int error;

    if ((!bl_sta) || (memcmp(addr, bl_sta->mac_addr, ETH_ALEN))) {
        printk("%s: TDLS station %pM doesn't exist\n", __func__, addr);
        
        return -ENOLINK;
    }

    if (!bl_sta->tdls.chsw_allowed) {
        printk("%s: TDLS station %pM does not support TDLS channel switch\n",        
               __func__, addr);
               
        return -ENOTSUPP;
    }

    error = bl_send_tdls_chan_switch_req(bl_hw, bl_vif, bl_sta,
                                         bl_sta->tdls.initiator,
                                         oper_class, chandef, &cfm);
    if (error)
        return error;

    if (!cfm.status) {
        bl_sta->tdls.chsw_en = true;
        
        return 0;
    } else {
        printk("%s: TDLS channel switch already enabled and only one is supported\n",
               __func__);
        
        return -EALREADY;
    }
}

/**
 * @tdls_cancel_channel_switch: Stop channel-switching with a TDLS peer. Both
 *    peers must be on the base channel when the call completes.
 */
static void bl_cfg80211_tdls_cancel_channel_switch(struct wiphy *wiphy,
                                         struct net_device *dev, const u8 *addr)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_sta *bl_sta = bl_vif->sta.tdls_sta;
    struct tdls_cancel_chan_switch_cfm cfm;

    if (!bl_sta)
        return;

    if (!bl_send_tdls_cancel_chan_switch_req(bl_hw, bl_vif,
                                               bl_sta, &cfm))
        bl_sta->tdls.chsw_en = false;
}
#endif /* version >= 3.19 */

/**
 * @change_bss: Modify parameters for a given BSS (mainly for AP mode).
 */
static int bl_cfg80211_change_bss(struct wiphy *wiphy, struct net_device *dev,
                                           struct bss_parameters *params)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    int res =  -EOPNOTSUPP;

    if (((BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_AP) ||
          (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_P2P_GO)) &&
        (params->ap_isolate > -1)) 
    {
        if (params->ap_isolate)
            bl_vif->ap.flags |= BL_AP_ISOLATE;
        else
            bl_vif->ap.flags &= ~BL_AP_ISOLATE;

        res = 0;
    }

    return res;
}


static int bl_fill_station_info(struct bl_sta *sta, struct bl_vif *vif,
                                  struct station_info *sinfo)
{
    struct bl_sta_stats *stats = &sta->stats;
    struct rx_vector_1 *rx_vect1 = &stats->last_rx.rx_vect1;

    // Generic info
    sinfo->generation = vif->generation;

    sinfo->inactive_time = jiffies_to_msecs(jiffies - stats->last_act);
    sinfo->rx_bytes = stats->rx_bytes;
    sinfo->tx_bytes = stats->tx_bytes;
    sinfo->tx_packets = stats->tx_pkts;
    sinfo->rx_packets = stats->rx_pkts;
    sinfo->signal = rx_vect1->rssi1;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
    switch (rx_vect1->ch_bw) {
        case PHY_CHNL_BW_20:
            sinfo->rxrate.bw = RATE_INFO_BW_20;
            break;
        case PHY_CHNL_BW_40:
            sinfo->rxrate.bw = RATE_INFO_BW_40;
            break;
        case PHY_CHNL_BW_80:
            sinfo->rxrate.bw = RATE_INFO_BW_80;
            break;
        case PHY_CHNL_BW_160:
            sinfo->rxrate.bw = RATE_INFO_BW_160;
            break;
#if 0
        default:
            sinfo->rxrate.bw = RATE_INFO_BW_HE_RU;
#endif
            break;
    }
#endif

    switch (rx_vect1->format_mod) {
        case FORMATMOD_NON_HT:
        case FORMATMOD_NON_HT_DUP_OFDM:
            sinfo->rxrate.flags = 0;
            sinfo->rxrate.legacy = legrates_lut[rx_vect1->leg_rate].rate;
            break;
        case FORMATMOD_HT_MF:
        case FORMATMOD_HT_GF:
            sinfo->rxrate.flags = RATE_INFO_FLAGS_MCS;
            if (rx_vect1->ht.short_gi)
                sinfo->rxrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
            sinfo->rxrate.mcs = rx_vect1->ht.mcs;
            break;
        case FORMATMOD_VHT:
            sinfo->rxrate.flags = RATE_INFO_FLAGS_VHT_MCS;
            if (rx_vect1->vht.short_gi)
                sinfo->rxrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
            sinfo->rxrate.mcs = rx_vect1->vht.mcs;
            sinfo->rxrate.nss = rx_vect1->vht.nss;
            break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
        case FORMATMOD_HE_MU:
            sinfo->rxrate.he_ru_alloc = rx_vect1->he.ru_size;
        case FORMATMOD_HE_SU:
        case FORMATMOD_HE_ER:
        case FORMATMOD_HE_TB:
            sinfo->rxrate.flags = RATE_INFO_FLAGS_HE_MCS;
            sinfo->rxrate.mcs = rx_vect1->he.mcs;
            sinfo->rxrate.nss = rx_vect1->he.nss;
            sinfo->rxrate.he_gi = rx_vect1->he.gi_type;
            sinfo->rxrate.he_dcm = rx_vect1->he.dcm;
#endif
        default :
            return -EINVAL;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
    sinfo->filled = (STATION_INFO_INACTIVE_TIME |
                     STATION_INFO_RX_BYTES64 |
                     STATION_INFO_TX_BYTES64 |
                     STATION_INFO_RX_PACKETS |
                     STATION_INFO_TX_PACKETS |
                     STATION_INFO_SIGNAL |
                     STATION_INFO_RX_BITRATE);
#else
    sinfo->filled = (BIT(NL80211_STA_INFO_INACTIVE_TIME) |
                     BIT(NL80211_STA_INFO_RX_BYTES64)    |
                     BIT(NL80211_STA_INFO_TX_BYTES64)    |
                     BIT(NL80211_STA_INFO_RX_PACKETS)    |
                     BIT(NL80211_STA_INFO_TX_PACKETS)    |
                     BIT(NL80211_STA_INFO_SIGNAL)        |
                     BIT(NL80211_STA_INFO_RX_BITRATE));
#endif

#ifdef CONFIG_MESH
    // Mesh specific info
    if (BL_VIF_TYPE(vif) == NL80211_IFTYPE_MESH_POINT)
    {
        struct mesh_peer_info_cfm peer_info_cfm;
        
        if (bl_send_mesh_peer_info_req(vif->bl_hw, vif, sta->sta_idx,
                                       &peer_info_cfm))
            return -ENOMEM;

        sinfo->llid = peer_info_cfm.local_link_id;
        sinfo->plid = peer_info_cfm.peer_link_id;
        sinfo->plink_state = peer_info_cfm.link_state;
        sinfo->local_pm = peer_info_cfm.local_ps_mode;
        sinfo->peer_pm = peer_info_cfm.peer_ps_mode;
        sinfo->nonpeer_pm = peer_info_cfm.non_peer_ps_mode;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
        sinfo->filled |= (STATION_INFO_LLID |
                          STATION_INFO_PLID |
                          STATION_INFO_PLINK_STATE |
                          STATION_INFO_LOCAL_PM |
                          STATION_INFO_PEER_PM |
                          STATION_INFO_NONPEER_PM);
#else
        sinfo->filled |= (BIT(NL80211_STA_INFO_LLID) |
                          BIT(NL80211_STA_INFO_PLID) |
                          BIT(NL80211_STA_INFO_PLINK_STATE) |
                          BIT(NL80211_STA_INFO_LOCAL_PM) |
                          BIT(NL80211_STA_INFO_PEER_PM) |
                          BIT(NL80211_STA_INFO_NONPEER_PM));
#endif
    }
#endif

    return 0;
}

/**
 * @get_station: get station information for the station identified by @mac
 */
static int bl_cfg80211_get_station(struct wiphy *wiphy, 
                                          struct net_device *dev,
                                          const u8 *mac, struct station_info *sinfo)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_sta *sta = NULL;

    if (BL_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR)
        return -EINVAL;
    else if ((BL_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) ||
             (BL_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT)) {
        if (vif->sta.ap && ether_addr_equal(vif->sta.ap->mac_addr, mac))
            sta = vif->sta.ap;
    }
    else
    {
        struct bl_sta *sta_iter;
        
        list_for_each_entry(sta_iter, &vif->ap.sta_list, list) {
            if (sta_iter->valid && ether_addr_equal(sta_iter->mac_addr, mac)) {
                sta = sta_iter;
                break;
            }
        }
    }

    if (sta)
        return bl_fill_station_info(sta, vif, sinfo);

    return -EINVAL;
}

/**
 * @dump_station: dump station callback -- resume dump at index @idx
 */
static int bl_cfg80211_dump_station(struct wiphy *wiphy, 
                                             struct net_device *dev, int idx, 
                                             u8 *mac, struct station_info *sinfo)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_sta *sta = NULL;

    if (BL_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR)
        return -EINVAL;
    else if ((BL_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) ||
             (BL_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT)) {
        if ((idx == 0) && vif->sta.ap && vif->sta.ap->valid)
            sta = vif->sta.ap;
    } else {
        struct bl_sta *sta_iter;
        int i = 0;
        
        list_for_each_entry(sta_iter, &vif->ap.sta_list, list) {
            if (i == idx) {
                sta = sta_iter;
                break;
            }
            i++;
        }
    }

    if (sta == NULL)
        return -ENOENT;

    /* Copy peer MAC address */
    memcpy(mac, &sta->mac_addr, ETH_ALEN);

    return bl_fill_station_info(sta, vif, sinfo);
}

#ifdef CONFIG_MESH
/**
 * @add_mpath: add a fixed mesh path
 */
static int bl_cfg80211_add_mpath(struct wiphy *wiphy, struct net_device *dev,
                                          const u8 *dst, const u8 *next_hop)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct mesh_path_update_cfm cfm;

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return bl_send_mesh_path_update_req(bl_hw, bl_vif, dst, next_hop, &cfm);
}

/**
 * @del_mpath: delete a given mesh path
 */
static int bl_cfg80211_del_mpath(struct wiphy *wiphy, struct net_device *dev,
                                          const u8 *dst)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct mesh_path_update_cfm cfm;

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return bl_send_mesh_path_update_req(bl_hw, bl_vif, dst, NULL, &cfm);
}

/**
 * @change_mpath: change a given mesh path
 */
static int bl_cfg80211_change_mpath(struct wiphy *wiphy, struct net_device *dev,
                                               const u8 *dst, const u8 *next_hop)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct mesh_path_update_cfm cfm;

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return bl_send_mesh_path_update_req(bl_hw, bl_vif, dst, next_hop, &cfm);
}

/**
 * @get_mpath: get a mesh path for the given parameters
 */
static int bl_cfg80211_get_mpath(struct wiphy *wiphy, struct net_device *dev,
                                         u8 *dst, u8 *next_hop, struct mpath_info *pinfo)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_mesh_path *mesh_path = NULL;
    struct bl_mesh_path *cur;

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &bl_vif->ap.mpath_list, list) {
        /* Compare the path target address and the provided destination address */
        if (memcmp(dst, &cur->tgt_mac_addr, ETH_ALEN)) {
            continue;
        }

        mesh_path = cur;
        break;
    }

    if (mesh_path == NULL)
        return -ENOENT;

    /* Copy next HOP MAC address */
    if (mesh_path->nhop_sta)
        memcpy(next_hop, &mesh_path->nhop_sta->mac_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = bl_vif->generation;

    return 0;
}

/**
 * @dump_mpath: dump mesh path callback -- resume dump at index @idx
 */
static int bl_cfg80211_dump_mpath(struct wiphy *wiphy, struct net_device *dev,
                                            int idx, u8 *dst, u8 *next_hop, 
                                            struct mpath_info *pinfo)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_mesh_path *mesh_path = NULL;
    struct bl_mesh_path *cur;
    int i = 0;

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &bl_vif->ap.mpath_list, list) {
        if (i < idx) {
            i++;
            continue;
        }

        mesh_path = cur;
        break;
    }

    if (mesh_path == NULL)
        return -ENOENT;

    /* Copy target and next hop MAC address */
    memcpy(dst, &mesh_path->tgt_mac_addr, ETH_ALEN);
    
    if (mesh_path->nhop_sta)
        memcpy(next_hop, &mesh_path->nhop_sta->mac_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = bl_vif->generation;

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/**
 * @get_mpp: get a mesh proxy path for the given parameters
 */
static int bl_cfg80211_get_mpp(struct wiphy *wiphy, struct net_device *dev,
                                       u8 *dst, u8 *mpp, struct mpath_info *pinfo)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_mesh_proxy *mesh_proxy = NULL;
    struct bl_mesh_proxy *cur;

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &bl_vif->ap.proxy_list, list) {
        if (cur->local) {
            continue;
        }

        /* Compare the path target address and the provided destination address */
        if (memcmp(dst, &cur->ext_sta_addr, ETH_ALEN)) {
            continue;
        }

        mesh_proxy = cur;
        break;
    }

    if (mesh_proxy == NULL)
        return -ENOENT;

    memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = bl_vif->generation;

    return 0;
}

/**
 * @dump_mpp: dump mesh proxy path callback -- resume dump at index @idx
 */
static int bl_cfg80211_dump_mpp(struct wiphy *wiphy, 
                                          struct net_device *dev, int idx, 
                                          u8 *dst, u8 *mpp, struct mpath_info *pinfo)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_mesh_proxy *mesh_proxy = NULL;
    struct bl_mesh_proxy *cur;
    int i = 0;

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &bl_vif->ap.proxy_list, list) {
        if (cur->local) {
            continue;
        }

        if (i < idx) {
            i++;
            continue;
        }

        mesh_proxy = cur;
        break;
    }

    if (mesh_proxy == NULL)
        return -ENOENT;

    /* Copy target MAC address */
    memcpy(dst, &mesh_proxy->ext_sta_addr, ETH_ALEN);
    memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = bl_vif->generation;

    return 0;
}
#endif /* version >= 3.19 */

/**
 * @get_mesh_config: Get the current mesh configuration
 */
static int bl_cfg80211_get_mesh_config(struct wiphy *wiphy, 
                                                  struct net_device *dev, 
                                                  struct mesh_config *conf)
{
    struct bl_vif *bl_vif = netdev_priv(dev);

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return 0;
}

/**
 * @update_mesh_config: Update mesh parameters on a running mesh.
 */
static int bl_cfg80211_update_mesh_config(struct wiphy *wiphy, 
                                                      struct net_device *dev, 
                                                      u32 mask, 
                                                      const struct mesh_config *nconf)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct mesh_update_cfm cfm;
    int status;

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    if (mask & CO_BIT(NL80211_MESHCONF_POWER_MODE - 1)) {
        bl_vif->ap.next_mesh_pm = nconf->power_mode;

        if (!list_empty(&bl_vif->ap.sta_list)) {
            // If there are mesh links we don't want to update the power mode
            // It will be updated with bl_update_mesh_power_mode() when the
            // ps mode of a link is updated or when a new link is added/removed
            mask &= ~BIT(NL80211_MESHCONF_POWER_MODE - 1);

            if (!mask)
                return 0;
        }
    }

    status = bl_send_mesh_update_req(bl_hw, bl_vif, mask, nconf, &cfm);

    if (!status && (cfm.status != 0))
        status = -EINVAL;

    return status;
}

/**
 * @join_mesh: join the mesh network with the specified parameters
 * (invoked with the wireless_dev mutex held)
 */
static int bl_cfg80211_join_mesh(struct wiphy *wiphy, struct net_device *dev,
                                         const struct mesh_config *conf, 
                                         const struct mesh_setup *setup)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct mesh_start_cfm mesh_start_cfm;
    int error = 0;
    u8 txq_status = 0;
    /* STA for BC/MC traffic */
    struct bl_sta *sta;

    BL_DBG(BL_FN_ENTRY_STR);

    if (BL_VIF_TYPE(bl_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    /* Forward the information to the UMAC */
    if ((error = bl_send_mesh_start_req(bl_hw, bl_vif,
         conf, setup, &mesh_start_cfm))) 
    {
        return error;
    }

    /* Check the status */
    switch (mesh_start_cfm.status) {
        case CO_OK:
            bl_vif->ap.bcmc_index = mesh_start_cfm.bcmc_idx;
            bl_vif->ap.flags = 0;
            bl_vif->ap.bcn_interval = setup->beacon_interval;
            bl_vif->use_4addr = true;
            if (setup->user_mpm)
                bl_vif->ap.flags |= BL_AP_USER_MESH_PM;

            sta = &bl_hw->sta_table[mesh_start_cfm.bcmc_idx];
            sta->valid = true;
            sta->aid = 0;
            sta->sta_idx = mesh_start_cfm.bcmc_idx;
            sta->ch_idx = mesh_start_cfm.ch_idx;
            sta->vif_idx = bl_vif->vif_index;
            sta->qos = true;
            sta->acm = 0;
            sta->ps.active = false;
            sta->listen_interval = 5;
            bl_mu_group_sta_init(sta, NULL);

            spin_lock_bh(&bl_hw->cb_lock);
            bl_chanctx_link(bl_vif, mesh_start_cfm.ch_idx,
                              (struct cfg80211_chan_def *)(&setup->chandef));

            if (bl_hw->cur_chanctx != mesh_start_cfm.ch_idx) {
                txq_status = BL_TXQ_STOP_CHAN;
            }
            
            bl_txq_vif_init(bl_hw, bl_vif, txq_status);
            spin_unlock_bh(&bl_hw->cb_lock);

            netif_tx_start_all_queues(dev);
            netif_carrier_on(dev);

            #ifdef CONFIG_BL_RADAR
            /* If the AP channel is already the active, we probably skip radar
               activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
               ctxt). In anycase retest if radar detection must be activated
             */
            if (bl_hw->cur_chanctx == mesh_start_cfm.ch_idx) {
                bl_radar_detection_enable_on_cur_channel(bl_hw);
            }
            #endif
            
            break;

        case CO_BUSY:
            error = -EINPROGRESS;
            break;

        default:
            error = -EIO;
            break;
    }

    /* Print information about the operation */
    if (error) {
        netdev_info(dev, "Failed to start MP (%d)", error);
    } else {
        netdev_info(dev, "MP started: ch=%d, bcmc_idx=%d",
                    bl_vif->ch_index, bl_vif->ap.bcmc_index);
    }

    return error;
}

/**
 * @leave_mesh: leave the current mesh network
 * (invoked with the wireless_dev mutex held)
 */
static int bl_cfg80211_leave_mesh(struct wiphy *wiphy,  
                                           struct net_device *dev)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct mesh_stop_cfm mesh_stop_cfm;
    int error = 0;

    error = bl_send_mesh_stop_req(bl_hw, bl_vif, &mesh_stop_cfm);

    if (error == 0) {
        /* Check the status */
        switch (mesh_stop_cfm.status) {
            case CO_OK:
                spin_lock_bh(&bl_hw->cb_lock);
                bl_chanctx_unlink(bl_vif);
                
                #ifdef CONFIG_BL_RADAR
                bl_radar_cancel_cac(&bl_hw->radar);
                #endif
                
                spin_unlock_bh(&bl_hw->cb_lock);
                /* delete BC/MC STA */
                bl_txq_vif_deinit(bl_hw, bl_vif);
                bl_del_bcn(&bl_vif->ap.bcn);

                netif_tx_stop_all_queues(dev);
                netif_carrier_off(dev);

                break;

            default:
                error = -EIO;
                break;
        }
    }

    if (error) {
        netdev_info(dev, "Failed to stop MP");
    } else {
        netdev_info(dev, "MP Stopped");
    }

    return 0;
}
#endif

static struct cfg80211_ops bl_cfg80211_ops = {
    .add_virtual_intf = bl_cfg80211_add_iface,
    .del_virtual_intf = bl_cfg80211_del_iface,
    .change_virtual_intf = bl_cfg80211_change_iface,
    .scan = bl_cfg80211_scan,
    .connect = bl_cfg80211_connect,
    .disconnect = bl_cfg80211_disconnect,
    .add_key = bl_cfg80211_add_key,
    .get_key = bl_cfg80211_get_key,
    .del_key = bl_cfg80211_del_key,
    .set_default_key = bl_cfg80211_set_default_key,
    .set_default_mgmt_key = bl_cfg80211_set_default_mgmt_key,
    .add_station = bl_cfg80211_add_station,
    .del_station = bl_cfg80211_del_station,
    .change_station = bl_cfg80211_change_station,
    .mgmt_tx = bl_cfg80211_mgmt_tx,
    .start_ap = bl_cfg80211_start_ap,
    .change_beacon = bl_cfg80211_change_beacon,
    .stop_ap = bl_cfg80211_stop_ap,
    .set_monitor_channel = bl_cfg80211_set_monitor_channel,
    .probe_client = bl_cfg80211_probe_client,
    #if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 7, 19)
    .mgmt_frame_register = bl_cfg80211_mgmt_frame_register,
    #endif
    .set_wiphy_params = bl_cfg80211_set_wiphy_params,
    .set_txq_params = bl_cfg80211_set_txq_params,
    .set_tx_power = bl_cfg80211_set_tx_power,
//    .get_tx_power = bl_cfg80211_get_tx_power,
    .set_power_mgmt = bl_cfg80211_set_power_mgmt,
    .get_station = bl_cfg80211_get_station,
    .dump_station = bl_cfg80211_dump_station,
    .remain_on_channel = bl_cfg80211_remain_on_channel,
    .cancel_remain_on_channel = bl_cfg80211_cancel_remain_on_channel,
    .dump_survey = bl_cfg80211_dump_survey,
    .get_channel = bl_cfg80211_get_channel,
    .start_radar_detection = bl_cfg80211_start_radar_detection,
    .update_ft_ies = bl_cfg80211_update_ft_ies,
    .set_cqm_rssi_config = bl_cfg80211_set_cqm_rssi_config,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
    .channel_switch = bl_cfg80211_channel_switch,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    .tdls_channel_switch = bl_cfg80211_tdls_channel_switch,
    .tdls_cancel_channel_switch = bl_cfg80211_tdls_cancel_channel_switch,
#endif
    .tdls_mgmt = bl_cfg80211_tdls_mgmt,
    .tdls_oper = bl_cfg80211_tdls_oper,
    .change_bss = bl_cfg80211_change_bss,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || defined(BL_WPA3_COMPAT)
    .external_auth = bl_cfg80211_external_auth,
#endif
};


/*********************************************************************
 * Init/Exit functions
 *********************************************************************/
static void bl_wdev_unregister(struct bl_hw *bl_hw)
{
    struct bl_vif *bl_vif, *tmp;

    rtnl_lock();
    list_for_each_entry_safe(bl_vif, tmp, &bl_hw->vifs, list) {
        BL_DBG("%s, call bl_cfg80211_del_iface\n", __func__);
        bl_cfg80211_del_iface(bl_hw->wiphy, &bl_vif->wdev);
    }
    rtnl_unlock();
}

static void bl_set_vers(struct bl_hw *bl_hw)
{
    u32 vers = bl_hw->version_cfm.version_lmac;

    BL_DBG(BL_FN_ENTRY_STR);

    snprintf(bl_hw->wiphy->fw_version,
             sizeof(bl_hw->wiphy->fw_version), "%d.%d.%d.%d",
             (vers & (0xff << 24)) >> 24, (vers & (0xff << 16)) >> 16,
             (vers & (0xff <<  8)) >>  8, (vers & (0xff <<  0)) >>  0);

    bl_hw->machw_type = bl_machw_type(bl_hw->version_cfm.version_machw_2);
}

/**
 *  @brief This function check special region code.
 *
 *  @param region_string         Region string
 *
 *  @return     true/false
 */
static u8 is_special_region_code(u8 *region_string)
{
    u8 i;
    struct region_code_t cfg80211_special_region_code[] = {
        {"00 "}, {"99 "}, {"98 "}, {"97 "}};

    for (i = 0; i < COUNTRY_CODE_LEN && region_string[i]; i++)
        region_string[i] = toupper(region_string[i]);

    for (i = 0; i < ARRAY_SIZE(cfg80211_special_region_code); i++) {
        if (!memcmp(region_string,
                    cfg80211_special_region_code[i].region,
                    COUNTRY_CODE_LEN)) 
        {
            BL_DBG("special region code=%s\n",region_string);
            return true;
        }
    }
    return false;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
static int 
#else
static void 
#endif
bl_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
    struct bl_hw *bl_hw = wiphy_priv(wiphy);
    u8 region[3];

    if (bl_mod_params.mp_mode) 
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
        return 0;
#else
        return;
#endif

    switch (request->initiator) {
    case NL80211_REGDOM_SET_BY_DRIVER:
        printk("Regulatory domain BY_DRIVER\n");
        break;
    case NL80211_REGDOM_SET_BY_CORE:
        printk("Regulatory domain BY_CORE\n");
        break;
    case NL80211_REGDOM_SET_BY_USER:
        printk("Regulatory domain BY_USER\n");
        break;
    case NL80211_REGDOM_SET_BY_COUNTRY_IE:
        printk("Regulatory domain BY_COUNTRY_IE\n");
        break;
    }

    if (!is_valid_country_code(request->alpha2)) {
        printk("%s invalid country code\n", __func__);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
        return 0;
#else
        return;
#endif
    }

    memset(region, 0, sizeof(region));
    memcpy(region, request->alpha2, sizeof(request->alpha2));
    region[2] = ' ';
    
    if ((bl_hw->country_code[0] != request->alpha2[0]) ||
        (bl_hw->country_code[1] != request->alpha2[1])) 
    {
        u8 country_code[COUNTRY_CODE_LEN];
        
        bl_hw->country_code[0] = request->alpha2[0];
        bl_hw->country_code[1] = request->alpha2[1];
        bl_hw->country_code[2] = ' ';
        memset(country_code, 0, sizeof(country_code));
        
        if (is_special_region_code(region)) {
            country_code[0] = 'W';
            country_code[1] = 'W';
        } else {
            country_code[0] = request->alpha2[0];
            country_code[1] = request->alpha2[1];
        }
        
        printk("%s code  %s\n",__func__, country_code);
        
        // 1. first update pwr from country code table
        // 2. if tx_pwr_cfg used, force power update from tx_pwr_cfg
        if (is_valid_country_code(bl_hw->country_code))
            bl_country_pwr_update(bl_hw, (char *)bl_hw->country_code);    

        if(bl_hw->mod_params->tx_pwr_cfg) {
            printk("tx pwr file : %s\n",bl_hw->mod_params->tx_pwr_cfg);
            bl_caldata_cfg_file_handle(bl_hw, bl_hw->mod_params->tx_pwr_cfg);
        } 
    }

    #ifdef CONFIG_BL_RADAR
    // For now trust all initiator
    bl_radar_set_domain(&bl_hw->radar, request->dfs_region);
    #endif
    
    BL_DBG("%s, call send_me_chan_config\n", __func__);
    
    bl_send_me_chan_config_req(bl_hw);
    
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
    return 0;
#else
    return;
#endif    
}

#ifdef CONFIG_MESH
static void bl_enable_mesh(struct bl_hw *bl_hw)
{
    struct wiphy *wiphy = bl_hw->wiphy;

    if (!bl_mod_params.mesh)
        return;

    bl_cfg80211_ops.add_mpath = bl_cfg80211_add_mpath;
    bl_cfg80211_ops.del_mpath = bl_cfg80211_del_mpath;
    bl_cfg80211_ops.change_mpath = bl_cfg80211_change_mpath;
    bl_cfg80211_ops.get_mpath = bl_cfg80211_get_mpath;
    bl_cfg80211_ops.dump_mpath = bl_cfg80211_dump_mpath;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    bl_cfg80211_ops.get_mpp = bl_cfg80211_get_mpp;
    bl_cfg80211_ops.dump_mpp = bl_cfg80211_dump_mpp;
#endif
    bl_cfg80211_ops.get_mesh_config = bl_cfg80211_get_mesh_config;
    bl_cfg80211_ops.update_mesh_config = bl_cfg80211_update_mesh_config;
    bl_cfg80211_ops.join_mesh = bl_cfg80211_join_mesh;
    bl_cfg80211_ops.leave_mesh = bl_cfg80211_leave_mesh;

    wiphy->flags |= (WIPHY_FLAG_MESH_AUTH | WIPHY_FLAG_IBSS_RSN);
    wiphy->features |= NL80211_FEATURE_USERSPACE_MPM;
    wiphy->interface_modes |= BIT(NL80211_IFTYPE_MESH_POINT);

    bl_limits[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
    bl_limits_dfs[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
}
#endif

#ifdef CONFIG_BL_SDIO
#define  BL_WD_PEROID   msecs_to_jiffies(50)

static void bl_watchdog_timer(struct bl_hw *bl_hw, bool active)
{
    /* Totally stop the timer */
    if (!active && bl_hw->watchdog_active) {
        del_timer_sync(&bl_hw->timer);
        bl_hw->watchdog_active = false;
        return;
    }

    if (active) {
        if (!bl_hw->watchdog_active) {
            /* Create timer */
            bl_hw->timer.expires = jiffies + BL_WD_PEROID;
            add_timer(&bl_hw->timer);
            bl_hw->watchdog_active = true;
        } else {
            /* Rerun the timer */
            mod_timer(&bl_hw->timer, jiffies + BL_WD_PEROID);
        }
    }
}

static int bl_watchdog_thread(void *data)
{
    struct bl_hw *bl_hw = (struct bl_hw *)data;
    int wait = 0; 
#ifdef BL_TRX_PROFILE    
    static int i = 0;
    static int rx_int_cnt_prev = 0;
#endif

    allow_signal(SIGTERM);
    while (1) {
        if (kthread_should_stop())
            break;
            
        //BL_DBG("watch dog thread\n");
        wait = wait_for_completion_interruptible(&bl_hw->watchdog_wait);

        if (kthread_should_stop())
            break;

        if(bl_hw->surprise_removed)
            break;
            
#ifdef BL_TRX_PROFILE
        // rx statistic for one second
        i++;
        if(i == 20){
            i = 0;
            
            if (bl_hw->trx_profile.rx_irq_cnt) {
                BL_DBG("one second rx:irq_cnt=%d,%d irq_dur_max=%d\n", 
                       bl_hw->trx_profile.rx_irq_cnt - rx_int_cnt_prev, 
                       bl_hw->trx_profile.rx_irq_cnt, 
                       ktime_to_us(bl_hw->trx_profile.rx_irq_max_interval));
                       
                rx_int_cnt_prev = bl_hw->trx_profile.rx_irq_cnt;
                bl_hw->trx_profile.rx_irq_max_interval = 0;
            }
        }
#endif

        // TODO: consider sleep later
        if (!wait) {
#ifdef BL_INT_POLLING            
            if(bl_hw->last_int_cnt == bl_hw->int_cnt){
                bl_get_interrupt_status(bl_hw, 1);
                
                if(bl_hw->int_status) {
                    bl_queue_main_work(bl_hw);
                }
            }
            bl_hw->last_int_cnt = bl_hw->int_cnt;
#endif            

            reinit_completion(&bl_hw->watchdog_wait);
        } else {
            break;
        }
    }
    
    return 0;
}

static void bl_watchdog(struct timer_list *t)
{
    struct bl_hw *bl_hw = from_timer(bl_hw, t, timer);

    if (bl_hw->watchdog_task) {
        complete(&bl_hw->watchdog_wait);
        /* Reschedule the watchdog */
        if (bl_hw->watchdog_active && !bl_hw->surprise_removed)
            mod_timer(&bl_hw->timer,
                  jiffies + BL_WD_PEROID);
    }
}
#endif

#ifdef CONFIG_KE_TASKLET
static void ke_tasklet(unsigned long data)
{
    struct bl_hw *bl_hw = (struct bl_hw *)data;

    if(bl_hw->surprise_removed) {
        BL_DBG_MSG("%s, removed, exit\n", __func__);
        
        return;
    }

    BL_DBG_MSG("%s, run one loop\n", __func__);
    
    softmac_schedule();

    BL_DBG_MSG("%s, exit\n", __func__);

    return;
}
#else
static int ke_thread(void *data)
{
    struct bl_hw *bl_hw = (struct bl_hw *)data;
    int wait = 0; 

    allow_signal(SIGTERM);
    while (1) {
        if (kthread_should_stop())
            break;
            
        wait = wait_for_completion_interruptible(&bl_hw->ke_wait);

        if(bl_hw->surprise_removed)
            break;

        if (kthread_should_stop())
            break;

        BL_DBG_MSG("%s, run one loop\n", __func__);
        
        softmac_schedule();

        if (!wait) {
              reinit_completion(&bl_hw->ke_wait);
        } else {
            break;
        }
    }

    BL_DBG_MSG("%s, exit\n", __func__);

    softmac_deinit();
    
    return 0;
}
#endif

int bl_cfg80211_init(struct bl_plat *bl_plat, void **platform_data)
{
    struct bl_hw *bl_hw;
    int ret = 0;
    struct wiphy *wiphy;
    struct wireless_dev *wdev;
    int i, j;
    u8_l zero_addr[6] =  { 0, 0, 0, 0, 0, 0 };
#ifdef CONFIG_BL_DEBUGFS
    struct bl_debugfs *bl_debugfs = NULL;
#endif
    struct vif_params vif_params = {.use_4addr = 1};
    struct bl_iface_tbl nl_iface_table[BL_OPMODE_MAX_NUM] = {
                            {1, NL80211_IFTYPE_STATION, "wlan%d",  NULL},
                            {0, NL80211_IFTYPE_AP,      "ap%d",    NULL} };
    u8_l nl_iface_num = 1;
    struct netlink_kernel_cfg nl_cfg = {
        .groups = BL_NL_BCAST_GROUP_ID,
    };


    BL_DBG(BL_FN_ENTRY_STR);

    /* create a new wiphy for use with cfg80211 */
    wiphy = wiphy_new(&bl_cfg80211_ops, sizeof(struct bl_hw));

    if (!wiphy) {
        dev_err(bl_platform_get_dev(bl_plat), "Failed to create new wiphy\n");
        ret = -ENOMEM;
        goto err_out;
    }

    bl_hw = wiphy_priv(wiphy);
    bl_hw->wiphy = wiphy;
    bl_hw->plat = bl_plat;
    bl_hw->dev = bl_platform_get_dev(bl_plat);
    bl_hw->mod_params = &bl_mod_params;
    bl_hw->tcp_pacing_shift = 7;
#ifdef CONFIG_BL_DEBUGFS
    bl_debugfs = &bl_hw->debugfs;
#endif

    if (bl_hw->mod_params->opmode == BL_OPMODE_COCURRENT) {
        nl_iface_num++;
        nl_iface_table[BL_OPMODE_AP].valid = 1;
    } else if (bl_hw->mod_params->opmode == BL_OPMODE_REPEATER) {
        nl_iface_num++;
        nl_iface_table[BL_OPMODE_AP].valid = 1;
        nl_iface_table[BL_OPMODE_STA].param = &vif_params;
        bl_hw->mod_params->tcp_ack_filter = false;
        bl_rpt_arp_table_init(bl_hw);
    }

    /* set device pointer for wiphy */
    set_wiphy_dev(wiphy, bl_hw->dev);

    /* Create cache to allocate sw_txhdr */
    bl_hw->sw_txhdr_cache = KMEM_CACHE(bl_sw_txhdr, 0);
    if (!bl_hw->sw_txhdr_cache) {
        wiphy_err(wiphy, "Cannot allocate cache for sw TX header\n");
        ret = -ENOMEM;
        goto err_cache;
    }
    
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    bl_hw->agg_reodr_pkt_cache= kmem_cache_create("bl_agg_reodr_pkt_cache",
                                                sizeof(struct bl_agg_reord_pkt),
                                                0, 0, NULL);
    if (!bl_hw->agg_reodr_pkt_cache) {
        wiphy_err(wiphy, "Cannot allocate cache for agg reorder packet\n");
        ret = -ENOMEM;
        goto err_reodr;
    }
#endif

    bl_hw->iwp_var.iwpriv_ind = NULL;
    
#ifdef CONFIG_BL_MP
    if (bl_mod_params.mp_mode) {
        bl_hw->iwp_var.tx_duty = DEFAULT_TX_DUTY;
        bl_hw->iwp_var.iwpriv_ind = kzalloc(IWPRIV_IND_LEN_MAX+1, GFP_KERNEL);
        bl_hw->iwp_var.iwpriv_ind_len = 0;
        
        if (!bl_hw->iwp_var.iwpriv_ind) {
            BL_DBG("allocate iwpriv_ind fail \n");
            ret = -ENOMEM;
            
            goto err_alloc_iwpriv_ind;
        }
    }
#endif

    bl_hw->vif_started = 0;
    bl_hw->monitor_vif = BL_INVALID_VIF;

    bl_hw->scan_ie.addr = NULL;

    for (i = 0; i < NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX; i++)
        bl_hw->avail_idx_map |= BIT(i);

    bl_hwq_init(bl_hw);
    bl_txq_prepare(bl_hw);

    bl_mu_group_init(bl_hw);

    bl_hw->roc = NULL;
    bl_hw->roc_cookie = 1;
     
    wiphy->mgmt_stypes = bl_default_mgmt_stypes;
    wiphy->bands[NL80211_BAND_2GHZ] = &bl_band_2GHz;
#ifdef BL_BAND_5G
    wiphy->bands[NL80211_BAND_5GHZ] = &bl_band_5GHz;
#else
    wiphy->bands[NL80211_BAND_5GHZ] = NULL;
#endif
    wiphy->interface_modes =
        BIT(NL80211_IFTYPE_STATION)     |
        BIT(NL80211_IFTYPE_AP)          |
        BIT(NL80211_IFTYPE_AP_VLAN)     |
        BIT(NL80211_IFTYPE_P2P_CLIENT)  |
        BIT(NL80211_IFTYPE_P2P_GO)      |
        BIT(NL80211_IFTYPE_MONITOR);
    wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
        WIPHY_FLAG_4ADDR_STATION |
        WIPHY_FLAG_4ADDR_AP;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
    wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    wiphy->max_num_csa_counters = BCN_MAX_CSA_CPT;
#endif
    wiphy->max_remain_on_channel_duration = bl_hw->mod_params->roc_dur_max;
    wiphy->features |= NL80211_FEATURE_NEED_OBSS_SCAN |
                       NL80211_FEATURE_SK_TX_STATUS |
                       NL80211_FEATURE_VIF_TXPOWER;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || defined(BL_WPA3_COMPAT)
    wiphy->features |= NL80211_FEATURE_SAE;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    wiphy->features |= NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
    wiphy->features |= NL80211_FEATURE_ACTIVE_MONITOR;
#endif

    if (bl_mod_params.tdls)
        /* TDLS support */
        wiphy->features |= NL80211_FEATURE_TDLS_CHANNEL_SWITCH;

    wiphy->iface_combinations   = bl_combinations;
    /* -1 not to include combination with radar detection, will be re-added in
       bl_handle_dynparams if supported */
    wiphy->n_iface_combinations = ARRAY_SIZE(bl_combinations) - 1;
    wiphy->reg_notifier = bl_reg_notifier;
    wiphy->regulatory_flags |= REGULATORY_DISABLE_BEACON_HINTS |
                               REGULATORY_COUNTRY_IE_IGNORE;
    wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
    wiphy->cipher_suites = cipher_suites;
    wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites) - NB_RESERVED_CIPHER;

    bl_hw->ext_capa[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING;
    bl_hw->ext_capa[2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT;
    bl_hw->ext_capa[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF;

    wiphy->extended_capabilities = bl_hw->ext_capa;
    wiphy->extended_capabilities_mask = bl_hw->ext_capa;
    wiphy->extended_capabilities_len = ARRAY_SIZE(bl_hw->ext_capa);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
    //MBO feature
    wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_ACCEPT_BCAST_PROBE_RESP);
    wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_FILS_MAX_CHANNEL_TIME);
    wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_OCE_PROBE_REQ_HIGH_TX_RATE);
    wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_OCE_PROBE_REQ_DEFERRAL_SUPPRESSION);
#endif

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    bl_hw->workqueue = alloc_workqueue("BL_WORK_QUEUE",
                                   WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
                                   
    if (!bl_hw->workqueue) {    
        printk("creat workqueue failed\n");

        ret = -EFAULT;
        goto err_alloc_bl_work_queue;
    }
    
    INIT_WORK(&bl_hw->main_work, bl_main_wq_hdlr);

#ifdef BL_RX_REORDER
    bl_hw->rx_workqueue = alloc_workqueue("BL_RXWORK_QUEUE", 
                                   WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
    if (!bl_hw->rx_workqueue) {
        printk("creat rx workqueue failed\n");

        ret = -EFAULT;
        goto err_alloc_bl_rx_work_queue;
    }
    
    INIT_WORK(&bl_hw->rx_work, bl_rx_wq_hdlr);
    bl_hw->rx_work_flag = true;

    bl_hw->rx_work_flag = bl_hw->mod_params->rx_work;
    
    if (!bl_hw->mod_params->rx_work) {
        if(num_possible_cpus() > 1)
            bl_hw->rx_work_flag = true;
        else
            bl_hw->rx_work_flag = false;
    }
    
    BL_DBG_MSG("rx work flag %d parameter - rx work %d cpu %d\n",
               bl_hw->rx_work_flag,
               bl_hw->mod_params->rx_work,
               num_possible_cpus());
#endif
#endif

    INIT_LIST_HEAD(&bl_hw->vifs);
    if (bl_hw->mod_params->tcp_ack_filter) {
        spin_lock_init(&bl_hw->tcp_ack_lock);
        INIT_LIST_HEAD(&bl_hw->tcp_stream_list);
    }

    mutex_init(&bl_hw->mutex);
    mutex_init(&bl_hw->dbgdump_elem.mutex);
    spin_lock_init(&bl_hw->tx_lock);
    spin_lock_init(&bl_hw->cb_lock);
    
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    spin_lock_init(&bl_hw->main_proc_lock);
    spin_lock_init(&bl_hw->int_lock);
    //spin_lock_init(&bl_hw->cmd_lock);
    spin_lock_init(&bl_hw->rx_process_lock);
    
    bl_hw->more_task_flag = false;
    bl_hw->cmd_sent = false;
    bl_hw->recovery_flag = false;
    bl_hw->bl_processing = false;
    bl_hw->rx_processing = false;
    bl_hw->data_sent = false;
    bl_hw->data_recv = false;
    bl_hw->surprise_removed = false;
    bl_hw->priv_scan.prob_req_en = false;

#ifdef BL_RX_REORDER
    for(i = 0; i < (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX); i++) {
        for(j = 0; j < NX_NB_TID_PER_STA; j++) {
            memset(&bl_hw->rx_reorder[i][j], 0, sizeof(struct rxreorder_list));
            bl_hw->rx_reorder[i][j].bl_hw = bl_hw;
        }
    }
#else
    for(i = 0; i < NX_REMOTE_STA_MAX*NX_NB_TID_PER_STA; i++)
        INIT_LIST_HEAD(&bl_hw->reorder_list[i]);
#endif

    for(i = 0; i < NX_NB_TXQ; i++)
        skb_queue_head_init(&bl_hw->transmitted_list[i]);
#endif //defined CONFIG_BL_SDIO || defined CONFIG_BL_USB

    skb_queue_head_init(&bl_hw->rx_pkt_list);

#ifdef CONFIG_BL_DEBUGFS
    BUILD_BUG_ON(sizeof(CONFIG_BL_UM_HELPER_DFLT) >=
                 sizeof(bl_debugfs->helper_cmd));
    strncpy(bl_debugfs->helper_cmd,
            CONFIG_BL_UM_HELPER_DFLT, sizeof(bl_debugfs->helper_cmd));
    INIT_WORK(&bl_debugfs->helper_work, bl_um_helper_work);

    bl_debugfs->trace_prst = bl_debugfs->helper_scheduled = false;
    spin_lock_init(&bl_debugfs->umh_lock);
#endif

    if ((ret = bl_hw->plat->ops.platform_on(bl_hw, NULL)))
        goto err_platon;

    *platform_data = bl_hw;

#ifdef CONFIG_BL_USB
    bl_usb_txrx_init(bl_hw);
#endif

    // don't issuse any cmd before drv_ready flag set
    bl_hw->drv_ready = true;


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    timer_setup(&bl_hw->ke_timer, softmac_timer_cb, 0);
#else
    init_timer(&bl_hw->ke_timer);
    bl_hw->ke_timer.function = softmac_timer_cb;
    bl_hw->ke_timer.data = (void *)bl_hw;
#endif

    spin_lock_init(&bl_hw->ke_queue_lock);
    spin_lock_init(&bl_hw->ke_mem_lock);
    bl_hw->ke_timer_active = false;
    softmac_init(bl_hw);

    #ifndef CONFIG_KE_TASKLET
    init_completion(&bl_hw->ke_wait);
    bl_hw->ke_task = kthread_run(ke_thread, bl_hw, "bl_ke/%s",
                                 dev_name(bl_hw->dev));
    if (IS_ERR(bl_hw->ke_task)) {
        pr_warn("ke thread failed to start\n");
        bl_hw->ke_task = NULL;
        ret = -ENOMEM;
        
        goto err_ke_task;
    }
    #else
    tasklet_init(&bl_hw->ke_tasklet, ke_tasklet, (unsigned long)bl_hw);
    #endif

    /**
     * set wifi mac address
     * 1. get mac addr from efuse by version_req and cfm.
     * 2. get mac addr from rf_para.conf if mac addr was not write to Efuse, by update version_cfm
     * 3. mod parameter: wifi_mac (in bl_handle_dynparams)
     **/

    memset(&bl_hw->version_cfm, 0, sizeof(bl_hw->version_cfm));
    if (!bl_mod_params.mp_mode)
    {
        /* Reset FW */
        if ((ret = bl_send_reset(bl_hw)))
            goto err_lmac_reqs;

        if ((ret = bl_send_version_req(bl_hw, &bl_hw->version_cfm)))
            goto err_lmac_reqs;


        if (bl_hw->plat->chip_ver == 0)
            bl_hw->plat->chip_ver = bl_hw->version_cfm.chip_ver;

        if (bl_hw->plat->chip_ver == CHIP_VER_616L) {
            printk("bl616l\n");
            
            bl_hw->mod_params->ldpc_on = false;
            bl_hw->mod_params->use_2040 = false;
        } else {
            printk("bl616\n");
        }
        
        #if defined(CONFIG_FW_COMBO) && defined(CONFIG_BL_BTUART) && !defined(CONFIG_BL_BTSDU)
        if (bl_mod_params.btble_uart_baud > 0)
            bl_send_btble_uart_req(bl_hw, bl_mod_params.btble_uart_baud, 
                                   bl_mod_params.btble_uart_flow, 
                                   bl_mod_params.btble_uart_rts, 
                                   bl_mod_params.btble_uart_cts);
        #endif
        
        if(bl_hw->mod_params->country_pwr_cfg) {
            printk("country pwr file : %s\n", bl_hw->mod_params->country_pwr_cfg);
            bl_country_pwr_file_handle(bl_hw, bl_hw->mod_params->country_pwr_cfg);
        } else {
            printk("no country code use tx_pwr_cfg or default tx_pwr");
        }
        
        /* country code could update in country_tx_pwr.conf and also in module parameter
         * or follow kernel default country code
         * country code in moudle parameter would overwrite country code from country_tx_pwr.conf */
        if(bl_hw->mod_params->country_code)
            memcpy(bl_hw->country_code, bl_hw->mod_params->country_code, 3);

        printk("%s, coutry code %s \n", __func__, bl_hw->country_code);
        
        /* 1. first update pwr from country code table
         * 2. if tx_pwr_cfg used, force power update from tx_pwr_cfg */
        if (is_valid_country_code(bl_hw->country_code)) {
            bl_country_pwr_update(bl_hw, (char *)bl_hw->country_code);
        } else {
            //if no valid country code, we use CN to init cal_cfg to fw, avoid null value in wl_cfg in fw.
            //later, wpa_supplicant or hostapd may update country code by bl_reg_notifier.
            printk("no valid country code in driver param, use default first to init tx pwr.\r\n");
            
            bl_country_pwr_update(bl_hw, "CN");
        }
        
        if(bl_hw->mod_params->tx_pwr_cfg) {
            printk("tx pwr file : %s\n",bl_hw->mod_params->tx_pwr_cfg);
            bl_caldata_cfg_file_handle(bl_hw, bl_hw->mod_params->tx_pwr_cfg);
        }

        // if cal data also set mac address , save mac to bl_hw->version_cfm.mac
        if(bl_hw->mod_params->cal_data_cfg) {
            printk("caldata file : %s\n",bl_hw->mod_params->cal_data_cfg);
            
            bl_caldata_cfg_file_handle(bl_hw, bl_hw->mod_params->cal_data_cfg);
        }

        if(bl_hw->version_cfm.mac_slot!=-1 && 
           memcmp(bl_hw->version_cfm.mac, zero_addr, ETH_ALEN))
        {
            memcpy(wiphy->perm_addr, bl_hw->version_cfm.mac, ETH_ALEN);
        }
    }else {
        if ((ret = bl_send_version_req(bl_hw, &bl_hw->version_cfm))) 
            goto err_lmac_reqs;

        if (bl_hw->plat->chip_ver == 0)
            bl_hw->plat->chip_ver = bl_hw->version_cfm.chip_ver;

        if (bl_hw->plat->chip_ver == CHIP_VER_616L) {
            printk("bl616l\n");

            bl_hw->mod_params->ldpc_on = false;
            bl_hw->mod_params->use_2040 = false;
        } else {
            printk("bl616\n");
        }

        if(bl_hw->version_cfm.mac_slot!=-1 && bl_hw->version_cfm.mac_slot<3)
            memcpy(wiphy->perm_addr, bl_hw->version_cfm.mac, ETH_ALEN);
    }

    bl_set_vers(bl_hw);

    if ((ret = bl_handle_dynparams(bl_hw, bl_hw->wiphy)))
        goto err_lmac_reqs;
        
    if (!bl_mod_params.mp_mode)
    {
#ifdef CONFIG_MESH
        bl_enable_mesh(bl_hw);
#endif
#ifdef CONFIG_BL_RADAR
        bl_radar_detection_init(&bl_hw->radar);
#endif

        /* Set parameters to firmware */
        bl_send_me_config_req(bl_hw);
    }
    
    /* Only monitor mode supported when custom channels are enabled */
    if (bl_mod_params.custchan) {
        bl_limits[0].types = BIT(NL80211_IFTYPE_MONITOR);
        bl_limits_dfs[0].types = BIT(NL80211_IFTYPE_MONITOR);
    }

    printk("BL FW version: %s-%d, Signed: %s\n", bl_hw->wiphy->fw_version, 
           bl_hw->version_cfm.sub_version, bl_hw->version_cfm.version_signed);

    printk("MAC Address is:\n%pM\n", wiphy->perm_addr);

    if ((ret = wiphy_register(wiphy))) {
        wiphy_err(wiphy, "Could not register wiphy device\n");
        
        goto err_register_wiphy;
    }

    /* Work to defer processing of rx buffer */
    INIT_WORK(&bl_hw->defer_rx.work, bl_rx_deferred);
    skb_queue_head_init(&bl_hw->defer_rx.sk_list);

    /* update country code to kernel */
    if (is_valid_country_code(bl_hw->country_code)) {
        printk("%s, driver has vaild country, call regulatory hint, country:%s\n", 
               __func__, bl_hw->country_code);

        regulatory_hint(wiphy, bl_hw->country_code);
    }
    
    /* Update regulatory (if needed) and set channel parameters to firmware
       (must be done after WiPHY registration) */
    bl_custregd(bl_hw, wiphy);
    
    if (!bl_mod_params.mp_mode)
    {
        bl_send_me_chan_config_req(bl_hw);
    }

    *platform_data = bl_hw;

#ifdef CONFIG_BL_DEBUGFS
    if ((ret = bl_dbgfs_register(bl_hw, "bl"))) {
        wiphy_err(wiphy, "Failed to register debugfs entries");
        
        goto err_debugfs;
    }
#endif

    for (i = 0; i < nl_iface_num; i++) {
        printk("iface_idx=%d, valid=%d, name=%s, type=%d, param=%p\n", 
               i, nl_iface_table[i].valid, nl_iface_table[i].name, 
               nl_iface_table[i].type, nl_iface_table[i].param);

        rtnl_lock();

        /* Add an initial interface */
        wdev = bl_interface_add(bl_hw, nl_iface_table[i].name, NET_NAME_UNKNOWN, 
                    bl_mod_params.custchan ? NL80211_IFTYPE_MONITOR : nl_iface_table[i].type, 
                    nl_iface_table[i].param);

        rtnl_unlock();

        if (!wdev) {
            wiphy_err(wiphy, "Failed to instantiate a network device\n");
            ret = -ENOMEM;
            
            goto err_add_interface;
        }

        wiphy_info(wiphy, "New interface create %s", wdev->netdev->name);
    }
    
#ifdef CONFIG_BL_SDIO
    /* create watch dog thread and timer */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    timer_setup(&bl_hw->timer, bl_watchdog, 0);
#else
    init_timer(&bl_hw->timer);
    bl_hw->timer.function = bl_watchdog;
    bl_hw->timer.data = (void *)bl_hw;
#endif
    
    init_completion(&bl_hw->watchdog_wait);
    bl_hw->watchdog_task = kthread_run(bl_watchdog_thread,
                                       bl_hw, "bl_wdog/%s",
                                       dev_name(bl_hw->dev));
    if (IS_ERR(bl_hw->watchdog_task)) {
        pr_warn("bl watch thread failed to start\n");
        bl_hw->watchdog_task = NULL;
        ret = -ENOMEM;
        
        goto err_wd_task;
    }

    bl_watchdog_timer(bl_hw, true);
#endif

    bl_hw->netlink_sock_num = BL_NL_SOCKET_NUM;
    bl_hw->netlink_sock = netlink_kernel_create(&init_net, 
                                         bl_hw->netlink_sock_num, &nl_cfg);
    if (bl_hw->netlink_sock) {
        printk("creat netlink socket num = %d\n", bl_hw->netlink_sock_num);
    }

    return 0;

#ifdef CONFIG_BL_SDIO
err_wd_task:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    del_timer_sync(&bl_hw->timer);
#else
    del_timer_sync(&bl_hw->timer);
#endif
#endif

err_add_interface:
#ifdef CONFIG_BL_DEBUGFS
err_debugfs:
#endif
    wiphy_unregister(bl_hw->wiphy);
    
err_register_wiphy:
err_lmac_reqs:
#ifdef CONFIG_BL_DEBUGFS
    bl_fw_trace_dump(bl_hw);
#endif

    #ifndef CONFIG_KE_TASKLET
    if (bl_hw->ke_task) {
        complete(&bl_hw->ke_wait);
        kthread_stop(bl_hw->ke_task);
    }
    #else
    tasklet_kill(&bl_hw->ke_tasklet);
    #endif

#ifndef CONFIG_KE_TASKLET
err_ke_task:
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    del_timer_sync(&bl_hw->ke_timer);
#else
    del_timer_sync(&bl_hw->ke_timer);
#endif

    bl_hw->plat->ops.platform_off(bl_hw, NULL);
    
err_platon:
#if defined CONFIG_BL_USB || defined CONFIG_BL_SDIO
#ifdef BL_RX_REORDER
    if (bl_hw->rx_workqueue) {
        flush_workqueue(bl_hw->rx_workqueue);
        destroy_workqueue(bl_hw->rx_workqueue);
        bl_hw->rx_workqueue = NULL;
    }
#endif
#endif

err_alloc_bl_rx_work_queue:
    if(bl_hw->workqueue) {
        flush_workqueue(bl_hw->workqueue);
        destroy_workqueue(bl_hw->workqueue);
        bl_hw->workqueue = NULL;
    }
    
err_alloc_bl_work_queue:
    if (bl_mod_params.mp_mode) {
        if (bl_hw->iwp_var.iwpriv_ind) {
            kfree(bl_hw->iwp_var.iwpriv_ind);
        }
    }
    
err_alloc_iwpriv_ind:
    kmem_cache_destroy(bl_hw->agg_reodr_pkt_cache);
err_reodr:
    kmem_cache_destroy(bl_hw->sw_txhdr_cache);
err_cache:
    wiphy_free(wiphy);
err_out:

    return ret;
}

void bl_cfg80211_deinit(struct bl_hw *bl_hw)
{
    int i, j, k;
    
    BL_DBG_MSG(BL_FN_ENTRY_STR);

    #ifdef BL_RX_REORDER
    for (i = 0; i < (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX); i++) {
        for (j = 0; j < NX_NB_TID_PER_STA; j++) {
            struct rxreorder_list *reorder_list;
            struct sk_buff *skb;
            
            reorder_list = &bl_hw->rx_reorder[i][j];
            
            if (reorder_list->flag == true)
            {
                reorder_list->flag = false;
                reorder_list->start_win = 0;
                reorder_list->end_win = 0;
                reorder_list->last_seq = 0;
                reorder_list->start_win_index = 0;
                reorder_list->flush = false;
                
                if (reorder_list->is_timer_set) {
                    BL_DBG("%s, clear reorder timer, sta:%d, tid:%d\n", 
                           __func__, i, j);
                           
                    if (in_irq() || in_atomic() || irqs_disabled())
                        del_timer(&reorder_list->timer);
                    else    
                        del_timer_sync(&reorder_list->timer);
                }
                
                reorder_list->is_timer_set = false;

                for (k=0; k< RX_WIN_SIZE; k++) {
                     skb = reorder_list->reorder_pkt[k];
                     reorder_list->reorder_pkt[k] = NULL;
                    
                     if(skb) {
                         BL_DBG("%s, free reorder skb, sta:%d, tid:%d\n", 
                                __func__, i, j);

                         dev_kfree_skb(skb);
                     }
                }
            }
        }
    }
    #endif

    if (bl_mod_params.mp_mode && bl_hw->iwp_var.iwpriv_ind) 
        kfree(bl_hw->iwp_var.iwpriv_ind);
    
    if(bl_hw->mod_params->tcp_ack_filter)
        bl_tcp_ack_stream_clear(bl_hw);

    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    del_timer_sync(&bl_hw->ke_timer);
    #else
    del_timer_sync(&bl_hw->ke_timer);
    #endif

    #ifndef CONFIG_KE_TASKLET
    if (bl_hw->ke_task) {
        complete(&bl_hw->ke_wait);
        kthread_stop(bl_hw->ke_task);
    }
    #else
    tasklet_kill(&bl_hw->ke_tasklet);
    #endif

    softmac_deinit();

    #ifdef CONFIG_BL_SDIO
    if (bl_hw->watchdog_task) {
        complete(&bl_hw->watchdog_wait);
        kthread_stop(bl_hw->watchdog_task);
    }

    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    del_timer_sync(&bl_hw->timer);
    #else
    del_timer_sync(&bl_hw->timer);
    #endif
    #endif

    #ifdef CONFIG_BL_DEBUGFS
    bl_dbgfs_unregister(bl_hw);
    #endif

    if (mutex_is_locked(&bl_hw->mutex)) {
        mutex_unlock(&bl_hw->mutex);
    }

    del_timer_sync(&bl_hw->txq_cleanup);
    bl_wdev_unregister(bl_hw);
    wiphy_unregister(bl_hw->wiphy);
    
    #ifdef CONFIG_BL_RADAR
    bl_radar_detection_deinit(&bl_hw->radar);
    #endif
    
    bl_hw->plat->ops.platform_off(bl_hw, NULL);
    
    kmem_cache_destroy(bl_hw->sw_txhdr_cache);
    #if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    kmem_cache_destroy(bl_hw->agg_reodr_pkt_cache);
    #endif
    
    wiphy_free(bl_hw->wiphy);
    netlink_kernel_release(bl_hw->netlink_sock);
    bl_hw->netlink_sock_num = 0;
}

/**
 *
 */
static int __init bl_mod_init(void)
{
    BL_DBG(BL_FN_ENTRY_STR);
    
    bl_print_version();

    return bl_platform_register_drv();
}

/**
 *
 */
static void __exit bl_mod_exit(void)
{
    BL_DBG(BL_FN_ENTRY_STR);

    bl_platform_unregister_drv();
}

module_init(bl_mod_init);
module_exit(bl_mod_exit);

MODULE_DESCRIPTION(BL_DRV_DESCRIPTION);
MODULE_VERSION(RELEASE_VERSION);
MODULE_AUTHOR(BL_DRV_COPYRIGHT " " BL_DRV_AUTHOR);
MODULE_LICENSE("GPL");

