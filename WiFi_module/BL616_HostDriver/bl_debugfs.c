/**
 ******************************************************************************
 *
 *  @file bl_debugfs.c
 *
 *  @brief Definition of debugfs entries
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



#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/debugfs.h>
#include <linux/string.h>
#include <linux/sort.h>

#ifdef CONFIG_BL_DEBUGFS
#include "bl_debugfs.h"
#endif
#include "bl_msg_tx.h"
#include "bl_radar.h"
#include "bl_tx.h"

#ifdef CONFIG_BL_SDIO
#include "sdio/bl_sdio.h"
#endif

#ifdef CONFIG_BL_USB
#include "usb/bl_usb.h"
#endif

#if defined(BL_BUS_LOOPBACK)
#include "bl_loopback.h"
#endif

__attribute__((unused)) static ssize_t bl_dbgfs_stats_read(struct file *file,
                                     char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char *buf;
    int ret;
    int i, skipped;
#ifdef CONFIG_BL_SPLIT_TX_BUF
    int per;
#endif
    ssize_t read;
    int bufsz = (NX_TXQ_CNT) * 20 + (ARRAY_SIZE(priv->stats.amsdus_rx) + 1) * 40
        + (ARRAY_SIZE(priv->stats.ampdus_tx) * 30);

    if (*ppos)
        return 0;

    buf = kmalloc(bufsz, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    ret = scnprintf(buf, bufsz, "TXQs CFM balances ");
    for (i = 0; i < NX_TXQ_CNT; i++)
        ret += scnprintf(&buf[ret], bufsz - ret,
                         "  [%1d]:%3d", i,
                         priv->stats.cfm_balance[i]);

    ret += scnprintf(&buf[ret], bufsz - ret, "\n");

#ifdef CONFIG_BL_SPLIT_TX_BUF
    ret += scnprintf(&buf[ret], bufsz - ret,
                     "\nAMSDU[len]       done         failed   received\n");
    for (i = skipped = 0; i < NX_TX_PAYLOAD_MAX; i++) {
        if (priv->stats.amsdus[i].done) {
            per = DIV_ROUND_UP((priv->stats.amsdus[i].failed) *
                               100, priv->stats.amsdus[i].done);
        } else if (priv->stats.amsdus_rx[i]) {
            per = 0;
        } else {
            per = 0;
            skipped = 1;
            continue;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
            skipped = 0;
        }

        ret += scnprintf(&buf[ret], bufsz - ret,
                         "   [%2d]    %10d %8d(%3d%%) %10d\n",  i ? i + 1 : i,
                         priv->stats.amsdus[i].done,
                         priv->stats.amsdus[i].failed, per,
                         priv->stats.amsdus_rx[i]);
    }

    for (; i < ARRAY_SIZE(priv->stats.amsdus_rx); i++) {
        if (!priv->stats.amsdus_rx[i]) {
            skipped = 1;
            continue;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
            skipped = 0;
        }

        ret += scnprintf(&buf[ret], bufsz - ret,
                         "   [%2d]                              %10d\n",
                         i + 1, priv->stats.amsdus_rx[i]);
    }
#else
    ret += scnprintf(&buf[ret], bufsz - ret,
                     "\nAMSDU[len]   received\n");
    for (i = skipped = 0; i < ARRAY_SIZE(priv->stats.amsdus_rx); i++) {
        if (!priv->stats.amsdus_rx[i]) {
            skipped = 1;
            continue;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret], bufsz - ret,
                             "   ...\n");
            skipped = 0;
        }

        ret += scnprintf(&buf[ret], bufsz - ret,
                         "   [%2d]    %10d\n",
                         i + 1, priv->stats.amsdus_rx[i]);
    }

#endif /* CONFIG_BL_SPLIT_TX_BUF */

    ret += scnprintf(&buf[ret], bufsz - ret,
                     "\nAMPDU[len]     done  received\n");
    for (i = skipped = 0; i < ARRAY_SIZE(priv->stats.ampdus_tx); i++) {
        if (!priv->stats.ampdus_tx[i] && !priv->stats.ampdus_rx[i]) {
            skipped = 1;
            continue;
        }
        if (skipped) {
            ret += scnprintf(&buf[ret], bufsz - ret,
                             "    ...\n");
            skipped = 0;
        }

        ret += scnprintf(&buf[ret], bufsz - ret,
                         "   [%2d]   %9d %9d\n", i ? i + 1 : i,
                         priv->stats.ampdus_tx[i], priv->stats.ampdus_rx[i]);
    }

    ret += scnprintf(&buf[ret], bufsz - ret,
                     "#mpdu missed        %9d\n",
                     priv->stats.ampdus_rx_miss);
    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    kfree(buf);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_stats_write(struct file *file,
                                      const char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;

    /* Prevent from interrupt preemption as these statistics are updated under
     * interrupt */
    spin_lock_bh(&priv->tx_lock);

    memset(&priv->stats, 0, sizeof(priv->stats));

    spin_unlock_bh(&priv->tx_lock);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(stats);


#ifdef CONFIG_BL_USB
__attribute__((unused)) static ssize_t bl_dbgfs_usb_rw_test_read(struct file *file,
                                     char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    u8 test;

    struct bl_hw *priv = file->private_data;

    bl_read_data_sync(priv->plat, &test, 1, BL_USB_EP_IN, BL_USB_TIMEOUT);

    printk("usb_rw_test read test: %d\n", test);

    return 0;
}

__attribute__((unused)) static ssize_t bl_dbgfs_usb_rw_test_write(struct file *file,
                                      const char __user *user_buf,
                                      size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    u8 buf[1] = {'L'};
	
    //bl_write_data_sync(priv->plat, 'L', 1, BL_USB_EP_OUT, BL_USB_TIMEOUT);
    bl_write_data_sync(priv->plat, buf, 1, BL_USB_EP_OUT, BL_USB_TIMEOUT);

    return 0;
}
DEBUGFS_READ_WRITE_FILE_OPS(usb_rw_test);
#endif


/*******************SDIO loopback test part, bl_debugfs.c***********************/
#if defined(BL_BUS_LOOPBACK) && (defined(CONFIG_BL_SDIO) || defined(CONFIG_BL_USB))
extern void bl_loopback_test(struct bl_hw *bl_hw, u32 exp_data_rate, 
                                 u32 pkt_size, u32 test_dir, u8 *pmsg);
__attribute__((unused)) static ssize_t bl_dbgfs_loopback_test_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;

    if (priv && priv->ploopback && priv->ploopback->msg)
        return simple_read_from_buffer(user_buf, count, ppos, 
                                       priv->ploopback->msg, LBK_MSG_SIZE);

    return 0;
}

__attribute__((unused)) static ssize_t bl_dbgfs_loopback_test_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[LBK_MSG_SIZE];
    u32 test_dir = 0; //0: host to fw, 1: fw to host, 2: both direction
    u32 exp_data_rate = 0;
    u32 pkt_size = 64;	
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    /*
    0,1,4000: drv to fw, try best to run at max rate, 4000 bytes per packet approximately
    1,1,4000: fw to drv, try best to run at max rate, 4000 bytes per packet approximately
    2,1,4000: both direction
    0,0,4000: stop drv to fw
    1,0,4000: stop fw to drv
    2,0,4000: stop both direction
    */
    if (sscanf(buf, "%d,%d,%d", &test_dir, &exp_data_rate, &pkt_size) > 0)
    {
        if (pkt_size == 0 && exp_data_rate != 0) {
        	printk("exp_data_rate=%d or pkt_size=%d is illegal parameter!\n", exp_data_rate, pkt_size);
        } else {
            printk("exp_data_rate=%d, pkt_size=%d\n", exp_data_rate, pkt_size);
            pkt_size = (pkt_size <= MAX_LBK_PKT_SIZE) ? pkt_size : MAX_LBK_PKT_SIZE;	/* the maximum packet pkt_size will be MAX_LBK_PKT_SIZE bytes */

            bl_loopback_test(priv, exp_data_rate, pkt_size, test_dir, buf);
        }
    } else {
        printk("%s, usage: test_dir,exp_data_rate(bps),pkt_size(byte). while input:%s\n", __func__, buf);
    }

    return count;
}
DEBUGFS_READ_WRITE_FILE_OPS(loopback_test);
#endif


#define TXQ_STA_PREF "tid|"
#define TXQ_STA_PREF_FMT "%3d|"

#ifdef CONFIG_BL_FULLMAC
#define TXQ_VIF_PREF "type|"
#define TXQ_VIF_PREF_FMT "%4s|"
#else
#define TXQ_VIF_PREF "AC|"
#define TXQ_VIF_PREF_FMT "%2s|"
#endif /* CONFIG_BL_FULLMAC */

#define TXQ_HDR "idx|  status|credit|ready|retry|pushed"
#define TXQ_HDR_FMT "%3d|%s%s%s%s%s%s%s%s|%6d|%5d|%5d|%6d"

#ifdef CONFIG_BL_AMSDUS_TX
#ifdef CONFIG_BL_FULLMAC
#define TXQ_HDR_SUFF "|amsdu"
#define TXQ_HDR_SUFF_FMT "|%5d"
#else
#define TXQ_HDR_SUFF "|amsdu-ht|amdsu-vht"
#define TXQ_HDR_SUFF_FMT "|%8d|%9d"
#endif /* CONFIG_BL_FULLMAC */
#else
#define TXQ_HDR_SUFF ""
#define TXQ_HDR_SUF_FMT ""
#endif /* CONFIG_BL_AMSDUS_TX */

#define TXQ_HDR_MAX_LEN (sizeof(TXQ_STA_PREF) + sizeof(TXQ_HDR) + sizeof(TXQ_HDR_SUFF) + 1)

#ifdef CONFIG_BL_FULLMAC
#define PS_HDR  "Legacy PS: ready=%d, sp=%d / UAPSD: ready=%d, sp=%d"
#define PS_HDR_LEGACY "Legacy PS: ready=%d, sp=%d"
#define PS_HDR_UAPSD  "UAPSD: ready=%d, sp=%d"
#define PS_HDR_MAX_LEN  sizeof("Legacy PS: ready=xxx, sp=xxx / UAPSD: ready=xxx, sp=xxx\n")
#else
#define PS_HDR ""
#define PS_HDR_MAX_LEN 0
#endif /* CONFIG_BL_FULLMAC */

#define STA_HDR "** STA %d (%pM)\n"
#define STA_HDR_MAX_LEN sizeof("- STA xx (xx:xx:xx:xx:xx:xx)\n") + PS_HDR_MAX_LEN

#ifdef CONFIG_BL_FULLMAC
#define VIF_HDR "* VIF [%d] %s\n"
#define VIF_HDR_MAX_LEN sizeof(VIF_HDR) + IFNAMSIZ
#else
#define VIF_HDR "* VIF [%d]\n"
#define VIF_HDR_MAX_LEN sizeof(VIF_HDR)
#endif


#ifdef CONFIG_BL_AMSDUS_TX

#ifdef CONFIG_BL_FULLMAC
#define VIF_SEP "---------------------------------------\n"
#else
#define VIF_SEP "----------------------------------------------------\n"
#endif /* CONFIG_BL_FULLMAC */

#else /* ! CONFIG_BL_AMSDUS_TX */
#define VIF_SEP "---------------------------------\n"
#endif /* CONFIG_BL_AMSDUS_TX*/

#define VIF_SEP_LEN sizeof(VIF_SEP)

#define CAPTION "status: L=in hwq list, F=stop full, P=stop sta PS, V=stop vif PS,\
 C=stop channel, S=stop CSA, M=stop MU, N=Ndev queue stopped"
#define CAPTION_LEN sizeof(CAPTION)

#define STA_TXQ 0
#define VIF_TXQ 1

__attribute__((unused)) static int bl_dbgfs_txq(char *buf, size_t size, 
                             struct bl_txq *txq, int type, int tid, char *name)
{
    int res, idx = 0;
    int i, pushed = 0;

    if (type == STA_TXQ) {
        res = scnprintf(&buf[idx], size, TXQ_STA_PREF_FMT, tid);
        idx += res;
        size -= res;
    } else {
        res = scnprintf(&buf[idx], size, TXQ_VIF_PREF_FMT, name);
        idx += res;
        size -= res;
    }

    for (i = 0; i < CONFIG_USER_MAX; i++) {
        pushed += txq->pkt_pushed[i];
    }

    res = scnprintf(&buf[idx], size, TXQ_HDR_FMT, txq->idx,
                    (txq->status & BL_TXQ_IN_HWQ_LIST) ? "L" : " ",
                    (txq->status & BL_TXQ_STOP_FULL) ? "F" : " ",
                    (txq->status & BL_TXQ_STOP_STA_PS) ? "P" : " ",
                    (txq->status & BL_TXQ_STOP_VIF_PS) ? "V" : " ",
                    (txq->status & BL_TXQ_STOP_CHAN) ? "C" : " ",
                    (txq->status & BL_TXQ_STOP_CSA) ? "S" : " ",
                    (txq->status & BL_TXQ_STOP_MU_POS) ? "M" : " ",
                    (txq->status & BL_TXQ_NDEV_FLOW_CTRL) ? "N" : " ",
                    txq->credits, skb_queue_len(&txq->sk_list),
                    txq->nb_retry, pushed);
    idx += res;
    size -= res;

#ifdef CONFIG_BL_AMSDUS_TX
    if (type == STA_TXQ) {
        res = scnprintf(&buf[idx], size, TXQ_HDR_SUFF_FMT,
#ifdef CONFIG_BL_FULLMAC
                        txq->amsdu_len
#else
                        txq->amsdu_ht_len_cap, txq->amsdu_vht_len_cap
#endif /* CONFIG_BL_FULLMAC */
                        );
        idx += res;
        size -= res;
    }
#endif

    res = scnprintf(&buf[idx], size, "\n");
    idx += res;
    size -= res;

    return idx;
}

__attribute__((unused)) static int bl_dbgfs_txq_sta(char *buf, 
                        size_t size, struct bl_sta *bl_sta, struct bl_hw *bl_hw)
{
    int tid, res, idx = 0;
    struct bl_txq *txq;

    res = scnprintf(&buf[idx], size, "\n" STA_HDR, bl_sta->sta_idx,
                    bl_sta->mac_addr);
    idx += res;
    size -= res;

#ifdef CONFIG_BL_FULLMAC
    if (bl_sta->ps.active) {
        if (bl_sta->uapsd_tids &&
            (bl_sta->uapsd_tids == ((1 << NX_NB_TXQ_PER_STA) - 1)))
            res = scnprintf(&buf[idx], size, PS_HDR_UAPSD "\n",
                            bl_sta->ps.pkt_ready[UAPSD_ID],
                            bl_sta->ps.sp_cnt[UAPSD_ID]);
        else if (bl_sta->uapsd_tids)
            res = scnprintf(&buf[idx], size, PS_HDR "\n",
                            bl_sta->ps.pkt_ready[LEGACY_PS_ID],
                            bl_sta->ps.sp_cnt[LEGACY_PS_ID],
                            bl_sta->ps.pkt_ready[UAPSD_ID],
                            bl_sta->ps.sp_cnt[UAPSD_ID]);
        else
            res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n",
                            bl_sta->ps.pkt_ready[LEGACY_PS_ID],
                            bl_sta->ps.sp_cnt[LEGACY_PS_ID]);
        idx += res;
        size -= res;
    } else {
        res = scnprintf(&buf[idx], size, "\n");
        idx += res;
        size -= res;
    }
#endif /* CONFIG_BL_FULLMAC */


    res = scnprintf(&buf[idx], size, TXQ_STA_PREF TXQ_HDR TXQ_HDR_SUFF "\n");
    idx += res;
    size -= res;


    foreach_sta_txq(bl_sta, txq, tid, bl_hw) {
        res = bl_dbgfs_txq(&buf[idx], size, txq, STA_TXQ, tid, NULL);
        idx += res;
        size -= res;
    }

    return idx;
}

__attribute__((unused)) static int bl_dbgfs_txq_vif(char *buf, size_t size, 
                                     struct bl_vif *bl_vif, struct bl_hw *bl_hw)
{
    int res, idx = 0;
    struct bl_txq *txq;
    struct bl_sta *bl_sta;

#ifdef CONFIG_BL_FULLMAC
    res = scnprintf(&buf[idx], size, VIF_HDR, 
                    bl_vif->vif_index, bl_vif->ndev->name);
    idx += res;
    size -= res;
    if (!bl_vif->up || bl_vif->ndev == NULL)
        return idx;

#else
    int ac;
    char ac_name[2] = {'0', '\0'};

    res = scnprintf(&buf[idx], size, VIF_HDR, bl_vif->vif_index);
    idx += res;
    size -= res;
#endif /* CONFIG_BL_FULLMAC */

#ifdef CONFIG_BL_FULLMAC
    if (BL_VIF_TYPE(bl_vif) ==  NL80211_IFTYPE_AP ||
        BL_VIF_TYPE(bl_vif) ==  NL80211_IFTYPE_P2P_GO ||
        BL_VIF_TYPE(bl_vif) ==  NL80211_IFTYPE_MESH_POINT) {
        res = scnprintf(&buf[idx], size, TXQ_VIF_PREF TXQ_HDR "\n");
        idx += res;
        size -= res;
        txq = bl_txq_vif_get(bl_vif, NX_UNK_TXQ_TYPE);
        res = bl_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "UNK");
        idx += res;
        size -= res;
        txq = bl_txq_vif_get(bl_vif, NX_BCMC_TXQ_TYPE);
        res = bl_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "BCMC");
        idx += res;
        size -= res;
        bl_sta = &bl_hw->sta_table[bl_vif->ap.bcmc_index];
        
        if (bl_sta->ps.active) {
            res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n",
                            bl_sta->ps.sp_cnt[LEGACY_PS_ID],
                            bl_sta->ps.sp_cnt[LEGACY_PS_ID]);
            idx += res;
            size -= res;
        } else {
            res = scnprintf(&buf[idx], size, "\n");
            idx += res;
            size -= res;
        }

        list_for_each_entry(bl_sta, &bl_vif->ap.sta_list, list) {
            res = bl_dbgfs_txq_sta(&buf[idx], size, bl_sta, bl_hw);
            idx += res;
            size -= res;
        }
    } else if (BL_VIF_TYPE(bl_vif) ==  NL80211_IFTYPE_STATION ||
               BL_VIF_TYPE(bl_vif) ==  NL80211_IFTYPE_P2P_CLIENT) {
        if (bl_vif->sta.ap) {
            res = bl_dbgfs_txq_sta(&buf[idx], size, bl_vif->sta.ap, bl_hw);
            idx += res;
            size -= res;
        }
    }

#else
    res = scnprintf(&buf[idx], size, TXQ_VIF_PREF TXQ_HDR "\n");
    idx += res;
    size -= res;

    foreach_vif_txq(bl_vif, txq, ac) {
        ac_name[0]++;
        res = bl_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, ac_name);
        idx += res;
        size -= res;
    }

    list_for_each_entry(bl_sta, &bl_vif->stations, list) {
        res = bl_dbgfs_txq_sta(&buf[idx], size, bl_sta, bl_hw);
        idx += res;
        size -= res;
    }
#endif /* CONFIG_BL_FULLMAC */
    return idx;
}

__attribute__((unused)) static ssize_t bl_dbgfs_txq_read(struct file *file ,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *bl_hw = file->private_data;
    struct bl_vif *vif;
    char *buf;
    int idx, res;
    ssize_t read;
    size_t bufsz = ((NX_VIRT_DEV_MAX * (VIF_HDR_MAX_LEN + 2 * VIF_SEP_LEN)) +
                    (NX_REMOTE_STA_MAX * STA_HDR_MAX_LEN) +
                    ((NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX + NX_NB_TXQ) *
                     TXQ_HDR_MAX_LEN) + CAPTION_LEN);

    /* everything is read in one go */
    if (*ppos)
        return 0;

    bufsz = min_t(size_t, bufsz, count);
    buf = kmalloc(bufsz, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    bufsz--;
    idx = 0;

    res = scnprintf(&buf[idx], bufsz, CAPTION);
    idx += res;
    bufsz -= res;

    //spin_lock_bh(&bl_hw->tx_lock);
    list_for_each_entry(vif, &bl_hw->vifs, list) {
        res = scnprintf(&buf[idx], bufsz, "\n"VIF_SEP);
        idx += res;
        bufsz -= res;
        res = bl_dbgfs_txq_vif(&buf[idx], bufsz, vif, bl_hw);
        idx += res;
        bufsz -= res;
        res = scnprintf(&buf[idx], bufsz, VIF_SEP);
        idx += res;
        bufsz -= res;
    }
    //spin_unlock_bh(&bl_hw->tx_lock);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, idx);
    kfree(buf);

    return read;
}
DEBUGFS_READ_FILE_OPS(txq);

__attribute__((unused)) static ssize_t bl_dbgfs_acsinfo_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    struct wiphy *wiphy = priv->wiphy;
    char buf[(SCAN_CHANNEL_MAX + 1) * 43];
    int survey_cnt = 0;
    int len = 0;
    int band, chan_cnt;

    mutex_lock(&priv->dbgdump_elem.mutex);

    len += scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                     "FREQ    TIME(ms)    BUSY(ms)    NOISE(dBm)\n");

    for (band = NL80211_BAND_2GHZ; band <= NL80211_BAND_5GHZ; band++) {
        for (chan_cnt = 0; chan_cnt < wiphy->bands[band]->n_channels; chan_cnt++) {
            struct bl_survey_info *p_survey_info = &priv->survey[survey_cnt];
            struct ieee80211_channel *p_chan = 
                                        &wiphy->bands[band]->channels[chan_cnt];

            if (p_survey_info->filled) {
                len += scnprintf(&buf[len], 
                                 min_t(size_t, sizeof(buf) - len - 1, count),
                                 "%d    %03d         %03d         %d\n",
                                 p_chan->center_freq,
                                 p_survey_info->chan_time_ms,
                                 p_survey_info->chan_time_busy_ms,
                                 p_survey_info->noise_dbm);
            } else {
                len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) -len -1, count),
                                 "%d    NOT AVAILABLE\n",
                                 p_chan->center_freq);
            }

            survey_cnt++;
        }
    }

    mutex_unlock(&priv->dbgdump_elem.mutex);

    return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

DEBUGFS_READ_FILE_OPS(acsinfo);

__attribute__((unused)) static ssize_t bl_dbgfs_fw_dbg_read(struct file *file,
                               char __user *user_buf, size_t count, loff_t *ppos)
{
    char help[]="usage: [MOD:<ALL|KE|DBG|IPC|DMA|MM|TX|RX|PHY>]* "
        "[DBG:<NONE|CRT|ERR|WRN|INF|VRB>]\n";

    return simple_read_from_buffer(user_buf, count, ppos, help, sizeof(help));
}


__attribute__((unused)) static ssize_t bl_dbgfs_fw_dbg_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int idx = 0;
    u32 mod = 0;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';

#define BL_MOD_TOKEN(str, val)                                        \
    if (strncmp(&buf[idx], str, sizeof(str) - 1 ) == 0) {               \
        idx += sizeof(str) - 1;                                         \
        mod |= val;                                                     \
        continue;                                                       \
    }

#define BL_DBG_TOKEN(str, val)                                \
    if (strncmp(&buf[idx], str, sizeof(str) - 1) == 0) {        \
        idx += sizeof(str) - 1;                                 \
        dbg = val;                                              \
        goto dbg_done;                                          \
    }

    while ((idx + 4) < len) {
        if (strncmp(&buf[idx], "MOD:", 4) == 0) {
            idx += 4;
            BL_MOD_TOKEN("ALL", 0xffffffff);
            BL_MOD_TOKEN("KE",  BIT(0));
            BL_MOD_TOKEN("DBG", BIT(1));
            BL_MOD_TOKEN("IPC", BIT(2));
            BL_MOD_TOKEN("DMA", BIT(3));
            BL_MOD_TOKEN("MM",  BIT(4));
            BL_MOD_TOKEN("TX",  BIT(5));
            BL_MOD_TOKEN("RX",  BIT(6));
            BL_MOD_TOKEN("PHY", BIT(7));
            idx++;
        } else if (strncmp(&buf[idx], "DBG:", 4) == 0) {
            u32 dbg = 0;
            idx += 4;
            BL_DBG_TOKEN("NONE", 0);
            BL_DBG_TOKEN("CRT",  1);
            BL_DBG_TOKEN("ERR",  2);
            BL_DBG_TOKEN("WRN",  3);
            BL_DBG_TOKEN("INF",  4);
            BL_DBG_TOKEN("VRB",  5);
            idx++;
            continue;
            
        dbg_done:
            bl_send_dbg_set_sev_filter_req(priv, dbg);
        } else {
            idx++;
        }
    }

    if (mod) {
        bl_send_dbg_set_mod_filter_req(priv, mod);
    }

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(fw_dbg);

__attribute__((unused)) static ssize_t bl_dbgfs_sys_stats_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[3*64];
    int len = 0;
    ssize_t read;
    int error = 0;
    struct dbg_get_sys_stat_cfm cfm;
    u32 sleep_int, sleep_frac, doze_int, doze_frac;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Get the information from the FW */
    if ((error = bl_send_dbg_get_sys_stat_req(priv, &cfm)))
        return error;

    if (cfm.stats_time == 0)
        return 0;

    sleep_int = ((cfm.cpu_sleep_time * 100) / cfm.stats_time);
    sleep_frac = 
         (((cfm.cpu_sleep_time * 100) % cfm.stats_time) * 10) / cfm.stats_time;
    doze_int = ((cfm.doze_time * 100) / cfm.stats_time);
    doze_frac = 
              (((cfm.doze_time * 100) % cfm.stats_time) * 10) / cfm.stats_time;

    len += scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                     "\nSystem statistics:\n");
    len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - 1, count),
                     "  CPU sleep [%%]: %d.%d\n", sleep_int, sleep_frac);
    len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - 1, count),
                     "  Doze      [%%]: %d.%d\n", doze_int, doze_frac);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    return read;
}

DEBUGFS_READ_FILE_OPS(sys_stats);

#ifdef CONFIG_BL_MUMIMO_TX
__attribute__((unused)) static ssize_t bl_dbgfs_mu_group_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *bl_hw = file->private_data;
    struct bl_mu_info *mu = &bl_hw->mu;
    struct bl_mu_group *group;
    size_t bufsz = NX_MU_GROUP_MAX * sizeof("xx = (xx - xx - xx - xx)\n") + 50;
    char *buf;
    int j, res, idx = 0;

    if (*ppos)
        return 0;

    buf = kmalloc(bufsz, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    res = scnprintf(&buf[idx], bufsz, "MU Group list (%d groups, %d users max)\n",
                    NX_MU_GROUP_MAX, CONFIG_USER_MAX);
    idx += res;
    bufsz -= res;

    list_for_each_entry(group, &mu->active_groups, list) {
        if (group->user_cnt) {
            res = scnprintf(&buf[idx], bufsz, "%2d = (", group->group_id);
            idx += res;
            bufsz -= res;
            
            for (j = 0; j < (CONFIG_USER_MAX - 1) ; j++) {
                if (group->users[j])
                    res = scnprintf(&buf[idx], bufsz, "%2d - ",
                                    group->users[j]->sta_idx);
                else
                    res = scnprintf(&buf[idx], bufsz, ".. - ");

                idx += res;
                bufsz -= res;
            }

            if (group->users[j])
                res = scnprintf(&buf[idx], bufsz, "%2d)\n",
                                group->users[j]->sta_idx);
            else
                res = scnprintf(&buf[idx], bufsz, "..)\n");

            idx += res;
            bufsz -= res;
        }
    }

    res = simple_read_from_buffer(user_buf, count, ppos, buf, idx);
    kfree(buf);

    return res;
}

DEBUGFS_READ_FILE_OPS(mu_group);
#endif

#ifdef CONFIG_BL_P2P_DEBUGFS
__attribute__((unused)) static ssize_t bl_dbgfs_oppps_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *rw_hw = file->private_data;
    struct bl_vif *rw_vif;
    char buf[32];
    size_t len = min_t(size_t, count, sizeof(buf) - 1);
    int ctw;

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';

    /* Read the written CT Window (provided in ms) value */
    if (sscanf(buf, "ctw=%d", &ctw) > 0) {
        /* Check if at least one VIF is configured as P2P GO */
        list_for_each_entry(rw_vif, &rw_hw->vifs, list) {
            if (BL_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
                struct mm_set_p2p_oppps_cfm cfm;

                /* Forward request to the embedded and wait for confirmation */
                bl_send_p2p_oppps_req(rw_hw, rw_vif, (u8)ctw, &cfm);

                break;
            }
        }
    }

    return count;
}

DEBUGFS_WRITE_FILE_OPS(oppps);

__attribute__((unused)) static ssize_t bl_dbgfs_noa_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *rw_hw = file->private_data;
    struct bl_vif *rw_vif;
    char buf[64];
    size_t len = min_t(size_t, count, sizeof(buf) - 1);
    int noa_count, interval, duration, dyn_noa;

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';

    /* Read the written NOA information */
    if (sscanf(buf, "count=%d interval=%d duration=%d dyn=%d",
               &noa_count, &interval, &duration, &dyn_noa) > 0) 
    {
        /* Check if at least one VIF is configured as P2P GO */
        list_for_each_entry(rw_vif, &rw_hw->vifs, list) {
            if (BL_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
                struct mm_set_p2p_noa_cfm cfm;

                /* Forward request to the embedded and wait for confirmation */
                bl_send_p2p_noa_req(rw_hw, rw_vif, noa_count, interval,
                                    duration, (dyn_noa > 0),  &cfm);

                break;
            }
        }
    }

    return count;
}

DEBUGFS_WRITE_FILE_OPS(noa);
#endif /* CONFIG_BL_P2P_DEBUGFS */

struct bl_dbgfs_fw_trace {
    struct bl_fw_trace_local_buf lbuf;
    struct bl_fw_trace *trace;
    struct bl_hw *bl_hw;
};

__attribute__((unused)) static int bl_dbgfs_fw_trace_open(
                                       struct inode *inode, struct file *file)
{
    struct bl_dbgfs_fw_trace *ltrace = kmalloc(sizeof(*ltrace), GFP_KERNEL);
    struct bl_hw *priv = inode->i_private;

    if (!ltrace)
        return -ENOMEM;

    if (bl_fw_trace_alloc_local(&ltrace->lbuf, 5120)) {
        kfree(ltrace);
    }

    ltrace->trace = &priv->debugfs.fw_trace;
    ltrace->bl_hw = priv;
    file->private_data = ltrace;
    return 0;
}

__attribute__((unused)) static int bl_dbgfs_fw_trace_release(
                                         struct inode *inode, struct file *file)
{
    struct bl_dbgfs_fw_trace *ltrace = file->private_data;

    if (ltrace) {
        bl_fw_trace_free_local(&ltrace->lbuf);
        kfree(ltrace);
    }

    return 0;
}

__attribute__((unused)) static ssize_t bl_dbgfs_fw_trace_read(
           struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_dbgfs_fw_trace *ltrace = file->private_data;
    bool dont_wait = ((file->f_flags & O_NONBLOCK) ||
                      ltrace->bl_hw->debugfs.unregistering);

    return bl_fw_trace_read(ltrace->trace, &ltrace->lbuf,
                              dont_wait, user_buf, count);
}

__attribute__((unused)) static ssize_t bl_dbgfs_fw_trace_write(
     struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_dbgfs_fw_trace *ltrace = file->private_data;
    int ret;

    ret = _bl_fw_trace_reset(ltrace->trace, true);
    if (ret)
        return ret;

    return count;
}

DEBUGFS_READ_WRITE_OPEN_RELEASE_FILE_OPS(fw_trace);

__attribute__((unused)) static ssize_t bl_dbgfs_fw_trace_level_read(struct file *file,
                                              char __user *user_buf,
                                              size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    return bl_fw_trace_level_read(&priv->debugfs.fw_trace, user_buf,
                                    count, ppos);
}

__attribute__((unused)) static ssize_t bl_dbgfs_fw_trace_level_write(
     struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    return bl_fw_trace_level_write(&priv->debugfs.fw_trace, user_buf, count);
}
DEBUGFS_READ_WRITE_FILE_OPS(fw_trace_level);


#ifdef CONFIG_BL_RADAR
__attribute__((unused)) static ssize_t bl_dbgfs_pulses_read(
                        struct file *file, char __user *user_buf, size_t count, 
                        loff_t *ppos, int rd_idx)
{
    struct bl_hw *priv = file->private_data;
    char *buf;
    int len = 0;
    int bufsz;
    int i;
    int index;
    struct bl_radar_pulses *p = &priv->radar.pulses[rd_idx];
    ssize_t read;

    if (*ppos != 0)
        return 0;

    /* Prevent from interrupt preemption */
    spin_lock_bh(&priv->radar.lock);
    bufsz = p->count * 34 + 51;
    bufsz += bl_radar_dump_pattern_detector(NULL, 0, &priv->radar, rd_idx);
    buf = kmalloc(bufsz, GFP_ATOMIC);
    if (buf == NULL) {
        spin_unlock_bh(&priv->radar.lock);
        return 0;
    }

    if (p->count) {
        len += scnprintf(&buf[len], bufsz - len,
                         " PRI     WIDTH     FOM     FREQ\n");
        index = p->index;
        for (i = 0; i < p->count; i++) {
            struct radar_pulse *pulse;

            if (index > 0)
                index--;
            else
                index = BL_RADAR_PULSE_MAX - 1;

            pulse = (struct radar_pulse *) &p->buffer[index];

            len += scnprintf(&buf[len], bufsz - len,
                             "%05dus  %03dus     %2d%%    %+3dMHz\n", pulse->rep,
                             2 * pulse->len, 6 * pulse->fom, 2*pulse->freq);
        }
    }

    len += bl_radar_dump_pattern_detector(&buf[len], bufsz - len,
                                          &priv->radar, rd_idx);

    spin_unlock_bh(&priv->radar.lock);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_pulses_prim_read(
           struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    return bl_dbgfs_pulses_read(file, user_buf, count, ppos, 0);
}

DEBUGFS_READ_FILE_OPS(pulses_prim);

__attribute__((unused)) static ssize_t bl_dbgfs_pulses_sec_read(
           struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    return bl_dbgfs_pulses_read(file, user_buf, count, ppos, 1);
}

DEBUGFS_READ_FILE_OPS(pulses_sec);

__attribute__((unused)) static ssize_t bl_dbgfs_detected_read(struct file *file,
                                        char __user *user_buf,
                                        size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char *buf;
    int bufsz,len = 0;
    ssize_t read;

    if (*ppos != 0)
        return 0;

    bufsz = 5; // RIU:\n
    bufsz += bl_radar_dump_radar_detected(NULL, 0, &priv->radar,
                                            BL_RADAR_RIU);

    if (priv->phy.cnt > 1) {
        bufsz += 5; // FCU:\n
        bufsz += bl_radar_dump_radar_detected(NULL, 0, &priv->radar,
                                                BL_RADAR_FCU);
    }

    buf = kmalloc(bufsz, GFP_KERNEL);
    if (buf == NULL) {
        return 0;
    }

    len = scnprintf(&buf[len], bufsz, "RIU:\n");
    len += bl_radar_dump_radar_detected(&buf[len], bufsz - len, &priv->radar,
                                            BL_RADAR_RIU);

    if (priv->phy.cnt > 1) {
        len += scnprintf(&buf[len], bufsz - len, "FCU:\n");
        len += bl_radar_dump_radar_detected(&buf[len], bufsz - len,
                                              &priv->radar, BL_RADAR_FCU);
    }

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);

    return read;
}

DEBUGFS_READ_FILE_OPS(detected);

__attribute__((unused)) static ssize_t bl_dbgfs_enable_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "RIU=%d FCU=%d\n", priv->radar.dpd[BL_RADAR_RIU]->enabled,
                    priv->radar.dpd[BL_RADAR_FCU]->enabled);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_enable_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "RIU=%d", &val) > 0)
        bl_radar_detection_enable(&priv->radar, val, BL_RADAR_RIU);

    if (sscanf(buf, "FCU=%d", &val) > 0)
        bl_radar_detection_enable(&priv->radar, val, BL_RADAR_FCU);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(enable);

__attribute__((unused)) static ssize_t bl_dbgfs_band_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "BAND=%d\n", priv->phy.sec_chan.band);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_band_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if ((sscanf(buf, "%d", &val) > 0) && (val >= 0) && (val <= NL80211_BAND_5GHZ))
        priv->phy.sec_chan.band = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(band);

__attribute__((unused)) static ssize_t bl_dbgfs_type_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "TYPE=%d\n", priv->phy.sec_chan.type);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_type_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if ((sscanf(buf, "%d", &val) > 0) && (val >= PHY_CHNL_BW_20) &&
        (val <= PHY_CHNL_BW_80P80))
        priv->phy.sec_chan.type = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(type);

__attribute__((unused)) static ssize_t bl_dbgfs_prim20_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "PRIM20=%dMHz\n", priv->phy.sec_chan.prim20_freq);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_prim20_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "%d", &val) > 0)
        priv->phy.sec_chan.prim20_freq = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(prim20);

__attribute__((unused)) static ssize_t bl_dbgfs_center1_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "CENTER1=%dMHz\n", priv->phy.sec_chan.center1_freq);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_center1_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "%d", &val) > 0)
        priv->phy.sec_chan.center1_freq = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center1);

__attribute__((unused)) static ssize_t bl_dbgfs_center2_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;

    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "CENTER2=%dMHz\n", priv->phy.sec_chan.center2_freq);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_center2_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int val;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "%d", &val) > 0)
        priv->phy.sec_chan.center2_freq = val;

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center2);


__attribute__((unused)) static ssize_t bl_dbgfs_set_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    return 0;
}

__attribute__((unused)) static ssize_t bl_dbgfs_set_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;

    bl_send_set_channel(priv, 1, NULL);
    bl_radar_detection_enable(&priv->radar, BL_RADAR_DETECT_ENABLE,
                                BL_RADAR_FCU);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(set);
#endif /* CONFIG_BL_RADAR */

#ifdef CONFIG_BL_FULLMAC

#define LINE_MAX_SZ 150

struct st {
    char line[LINE_MAX_SZ + 1];
    unsigned int r_idx;
};

__attribute__((unused)) static int compare_idx(const void *st1, const void *st2)
{
    int index1 = ((struct st *)st1)->r_idx;
    int index2 = ((struct st *)st2)->r_idx;

    if (index1 > index2) return 1;
    if (index1 < index2) return -1;

    return 0;
}

__attribute__((unused)) static const int ru_size[] =
{
    26,
    52,
    106,
    242,
    484,
    996
};

__attribute__((unused)) static int print_rate(char *buf, int size, 
                                      int format, int nss, int mcs, int bw,
                                      int sgi, int pre, int *r_idx)
{
    int res = 0;
    int bitrates_cck[4] = { 10, 20, 55, 110 };
    int bitrates_ofdm[8] = { 6, 9, 12, 18, 24, 36, 48, 54};
    char he_gi[3][4] = {"0.8", "1.6", "3.2"};

    if (format < FORMATMOD_HT_MF) {
        if (mcs < 4) {
            if (r_idx) {
                *r_idx = (mcs * 2) + pre;
                res = scnprintf(buf, size - res, "%3d ", *r_idx);
            }
            
            res += scnprintf(&buf[res], size - res, "L-CCK/%cP      %2u.%1uM    ",
                             pre > 0 ? 'L' : 'S',
                             bitrates_cck[mcs] / 10,
                             bitrates_cck[mcs] % 10);
        } else {
            mcs -= 4;
            if (r_idx) {
                *r_idx = N_CCK + mcs;
                res = scnprintf(buf, size - res, "%3d ", *r_idx);
            }
            
            res += scnprintf(&buf[res], size - res, "L-OFDM        %2u.0M    ",
                             bitrates_ofdm[mcs]);
        }
    } else if (format < FORMATMOD_VHT) {
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + nss * 32 + mcs * 4 + bw * 2 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        
        mcs += nss * 8;
        res += scnprintf(&buf[res], size - res, "HT%d/%cGI       MCS%-2d   ",
                         20 * (1 << bw), sgi ? 'S' : 'L', mcs);
    } else if (format == FORMATMOD_VHT){
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + N_HT + nss * 80 + mcs * 8 + bw * 2 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        
        res += scnprintf(&buf[res], size - res, "VHT%d/%cGI%*cMCS%d/%1d  ",
                         20 * (1 << bw), sgi ? 'S' : 'L', bw > 2 ? 5 : 6, ' ',
                         mcs, nss + 1);
    } else if (format == FORMATMOD_HE_SU){
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + N_HT + N_VHT + nss * 144 +
                     mcs * 12 + bw * 3 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        
        res += scnprintf(&buf[res], size - res, "HE%d/GI%s%*cMCS%d/%1d%*c",
                         20 * (1 << bw), he_gi[sgi], bw > 2 ? 4 : 5, ' ',
                         mcs, nss + 1, mcs > 9 ? 1 : 2, ' ');
    } else {
        if (r_idx) {
            *r_idx = N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + 
                     nss * 216 + mcs * 18 + bw * 3 + sgi;
            res = scnprintf(buf, size - res, "%3d ", *r_idx);
        }
        
        res += scnprintf(&buf[res], size - res, "HEMU-%d/GI%s%*cMCS%d/%1d%*c",
                         ru_size[bw], he_gi[sgi], bw > 1 ? 1 : 2, ' ',
                         mcs, nss + 1, mcs > 9 ? 1 : 2, ' ');

    }

    return res;
}

__attribute__((unused)) static int print_rate_from_cfg(char *buf, 
                            int size, u32 rate_config, int *r_idx, int ru_size)
{
    union bl_rate_ctrl_info *r_cfg = (union bl_rate_ctrl_info *)&rate_config;
    union bl_mcs_index *mcs_index = (union bl_mcs_index *)&rate_config;
    unsigned int ft, pre, gi, bw, nss, mcs, len;

    ft = r_cfg->formatModTx;
    pre = r_cfg->giAndPreTypeTx >> 1;
    gi = r_cfg->giAndPreTypeTx;
    bw = r_cfg->bwTx;
    if (ft == FORMATMOD_HE_MU) {
        mcs = mcs_index->he.mcs;
        nss = mcs_index->he.nss;
        bw = ru_size;
    } else if (ft == FORMATMOD_HE_SU) {
        mcs = mcs_index->he.mcs;
        nss = mcs_index->he.nss;
    } else if (ft == FORMATMOD_VHT) {
        mcs = mcs_index->vht.mcs;
        nss = mcs_index->vht.nss;
    } else if (ft >= FORMATMOD_HT_MF) {
        mcs = mcs_index->ht.mcs;
        nss = mcs_index->ht.nss;
    } else {
        mcs = mcs_index->legacy;
        nss = 0;
    }

    len = print_rate(buf, size, ft, nss, mcs, bw, gi, pre, r_idx);
    return len;
}

__attribute__((unused)) static void idx_to_rate_cfg(int idx,
                                  union bl_rate_ctrl_info *r_cfg, int *ru_size)
{
    r_cfg->value = 0;

    if (idx >= N_HE_DCM_RATE_IDX_BASE) {
        r_cfg->dcmTx = 1;
        idx -= N_HE_DCM_RATE_IDX_BASE;
        printk("Add DCM bit to original rate idx %d\n", idx);
    }
        
    if (idx < N_CCK)
    {
        r_cfg->formatModTx = FORMATMOD_NON_HT;
        r_cfg->giAndPreTypeTx = (idx & 1) << 1;
        r_cfg->mcsIndexTx = idx / 2;
    }
    else if (idx < (N_CCK + N_OFDM))
    {
        r_cfg->formatModTx = FORMATMOD_NON_HT;
        r_cfg->mcsIndexTx =  idx - N_CCK + 4;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT))
    {
        union bl_mcs_index *r = (union bl_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM);
        r_cfg->formatModTx = FORMATMOD_HT_MF;
        r->ht.nss = idx / (8*2*2);
        r->ht.mcs = (idx % (8*2*2)) / (2*2);
        r_cfg->bwTx = ((idx % (8*2*2)) % (2*2)) / 2;
        r_cfg->giAndPreTypeTx = idx & 1;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT))
    {
        union bl_mcs_index *r = (union bl_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM + N_HT);
        r_cfg->formatModTx = FORMATMOD_VHT;
        r->vht.nss = idx / (10*4*2);
        r->vht.mcs = (idx % (10*4*2)) / (4*2);
        r_cfg->bwTx = ((idx % (10*4*2)) % (4*2)) / 2;
        r_cfg->giAndPreTypeTx = idx & 1;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU))
    {
        union bl_mcs_index *r = (union bl_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM + N_HT + N_VHT);
        r_cfg->formatModTx = FORMATMOD_HE_SU;
        r->vht.nss = idx / (12*4*3);
        r->vht.mcs = (idx % (12*4*3)) / (4*3);
        r_cfg->bwTx = ((idx % (12*4*3)) % (4*3)) / 3;
        r_cfg->giAndPreTypeTx = idx % 3;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + N_HE_MU))
    {
        union bl_mcs_index *r = (union bl_mcs_index *)r_cfg;

        BUG_ON(ru_size == NULL);

        idx -= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU);
        r_cfg->formatModTx = FORMATMOD_HE_MU;
        r->vht.nss = idx / (12*6*3);
        r->vht.mcs = (idx % (12*6*3)) / (6*3);
        *ru_size = ((idx % (12*6*3)) % (6*3)) / 3;
        r_cfg->giAndPreTypeTx = idx % 3;
        r_cfg->bwTx = 0;
    }
    else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + N_HE_MU + N_HE_ER))
    {
        union bl_mcs_index *r = (union bl_mcs_index *)r_cfg;

        idx -= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + N_HE_MU);
        r_cfg->formatModTx = FORMATMOD_HE_ER;
        r_cfg->bwTx = idx / 9;
        if (ru_size)
            *ru_size = idx / 9;
        r_cfg->giAndPreTypeTx = idx % 3;
        r->vht.mcs = (idx % 9) / 3;
        r->vht.nss = 0;
    } else {
        printk("WARN known rate idx=%d\n", idx);
    }
}

__attribute__((unused)) static struct bl_sta* bl_dbgfs_get_sta(
                                            struct bl_hw *bl_hw, char* mac_addr)
{
    u8 mac[6];

    if (sscanf(mac_addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6)
        return NULL;
    return bl_get_sta(bl_hw, mac);
}

__attribute__((unused)) static ssize_t bl_dbgfs_twt_request_read(
           struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    char buf[750];
    ssize_t read;
    struct bl_hw *priv = file->private_data;
    struct bl_sta *sta = NULL;
    int len;

    /* Get the station index from MAC address */
    sta = bl_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;
        
    if (sta->twt_ind.sta_idx != BL_INVALID_STA)
    {
        struct twt_conf_tag *conf = &sta->twt_ind.conf;
        if (sta->twt_ind.resp_type == MAC_TWT_SETUP_ACCEPT)
            len = scnprintf(buf, sizeof(buf) - 1, 
                            "Accepted configuration");
        else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_ALTERNATE)
            len = scnprintf(buf, sizeof(buf) - 1, 
                            "Alternate configuration proposed by AP");
        else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_DICTATE)
            len = scnprintf(buf, sizeof(buf) - 1,
                            "AP dictates the following configuration");
        else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_REJECT)
            len = scnprintf(buf, sizeof(buf) - 1,
                            "AP rejects the following configuration");
        else
        {
            len = scnprintf(buf, sizeof(buf) - 1,
                            "Invalid response from the peer");
            goto end;
        }
        
        len += scnprintf(&buf[len], sizeof(buf) - 1 - len,":\n"
                         "flow_type = %d\n"
                         "wake interval mantissa = %d\n"
                         "wake interval exponent = %d\n"
                         "wake interval = %d us\n"
                         "nominal minimum wake duration = %d us\n",
                         conf->flow_type, conf->wake_int_mantissa,
                         conf->wake_int_exp,
                         conf->wake_int_mantissa << conf->wake_int_exp,
                         conf->wake_dur_unit ?
                         conf->min_twt_wake_dur * 1024:
                         conf->min_twt_wake_dur * 256);
    }
    else
    {
        len = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                        "setup_command = <0: request, 1: suggest, 2: demand>,"
                        "flow_type = <0: announced, 1: unannounced>,"
                        "wake_interval_mantissa = <0 if setup request and no constraints>,"
                        "wake_interval_exp = <0 if setup request and no constraints>,"
                        "nominal_min_wake_dur = <0 if setup request and no constraints>,"
                        "wake_dur_unit = <0: 256us, 1: tu>");
    }
  end:
    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_twt_request_write(
                                 struct file *file, const char __user *user_buf,
                                 size_t count, loff_t *ppos)
{
    char *accepted_params[] = {"setup_command",
                               "flow_type",
                               "wake_interval_mantissa",
                               "wake_interval_exp",
                               "nominal_min_wake_dur",
                               "wake_dur_unit",
                               0};
    struct twt_conf_tag twt_conf;
    struct twt_setup_cfm twt_setup_cfm;
    struct bl_sta *sta = NULL;
    struct bl_hw *priv = file->private_data;
    char buf[512], param[30];
    char *line;
    int error = 1, i, val, setup_command = -1;
    bool_l found;
    size_t len = sizeof(buf) - 1;

    BL_DBG(BL_FN_ENTRY_STR);
    /* Get the station index from MAC address */
    sta = bl_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    /* Get the content of the file */
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';
    memset(&twt_conf, 0, sizeof(twt_conf));

    line = buf;
    /* Get the content of the file */
    while (line != NULL)
    {
        if (sscanf(line, "%s = %d", param, &val) == 2)
        {
            i = 0;
            found = false;
            // Check if parameter is valid
            while(accepted_params[i])
            {
                if (strcmp(accepted_params[i], param) == 0)
                {
                    found = true;
                    break;
                }
                i++;
            }

            if (!found)
            {
                dev_err(priv->dev, "%s: parameter %s is not valid\n",
                        __func__, param);
                return -EINVAL;
            }

            if (!strcmp(param, "setup_command"))
            {
                setup_command = val;
            }
            else if (!strcmp(param, "flow_type"))
            {
                twt_conf.flow_type = val;
            }
            else if (!strcmp(param, "wake_interval_mantissa"))
            {
                twt_conf.wake_int_mantissa = val;
            }
            else if (!strcmp(param, "wake_interval_exp"))
            {
                twt_conf.wake_int_exp = val;
            }
            else if (!strcmp(param, "nominal_min_wake_dur"))
            {
                twt_conf.min_twt_wake_dur = val;
            }
            else if (!strcmp(param, "wake_dur_unit"))
            {
                twt_conf.wake_dur_unit = val;
            }
        }
        else
        {
            dev_err(priv->dev, 
                 "%s: Impossible to read TWT configuration option\n", __func__);
            return -EFAULT;
        }
        line = strchr(line, ',');
        if(line == NULL)
            break;
        line++;
    }

    if (setup_command == -1)
    {
        dev_err(priv->dev, "%s: TWT missing setup command\n", __func__);
        return -EFAULT;
    }
    printk("%s set=%d flow_type=%d int_mantissa=%d int_exp=%d twt_dur=%d dur_unit=%d",
          __func__, setup_command, twt_conf.flow_type,
          twt_conf.wake_int_mantissa, 
          twt_conf.wake_int_exp, twt_conf.min_twt_wake_dur, 
          twt_conf.wake_dur_unit);

    // Forward the request to the LMAC
    if ((error = bl_send_twt_request(priv, setup_command, sta->vif_idx,
                                       &twt_conf, &twt_setup_cfm)) != 0)
        return error;

    // Check the status
    if (twt_setup_cfm.status != CO_OK)
        return -EIO;

    return count;
}
DEBUGFS_READ_WRITE_FILE_OPS(twt_request);

__attribute__((unused)) static ssize_t bl_dbgfs_twt_teardown_read(
                                     struct file *file, char __user *user_buf,
                                     size_t count, loff_t *ppos)
{
    char buf[512];
    int ret;
    ssize_t read;


    ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
                    "TWT teardown format:\n\n"
                    "flow_id = <ID>\n");
    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_twt_teardown_write(
                               struct file *file, const char __user *user_buf,
                               size_t count, loff_t *ppos)
{
    struct twt_teardown_req twt_teardown;
    struct twt_teardown_cfm twt_teardown_cfm;
    struct bl_sta *sta = NULL;
    struct bl_hw *priv = file->private_data;
    char buf[256];
    char *line;
    int error = 1;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    BL_DBG(BL_FN_ENTRY_STR);
    /* Get the station index from MAC address */
    sta = bl_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    /* Get the content of the file */
    if (copy_from_user(buf, user_buf, len))
        return -EINVAL;

    buf[len] = '\0';
    memset(&twt_teardown, 0, sizeof(twt_teardown));

    /* Get the content of the file */
    line = buf;

    if (sscanf(line, "flow_id = %d", (int *) &twt_teardown.id) != 1)
    {
        dev_err(priv->dev, "%s: Invalid TWT configuration\n", __func__);
        return -EINVAL;
    }

    twt_teardown.neg_type = 0;
    twt_teardown.all_twt = 0;
    twt_teardown.vif_idx = sta->vif_idx;

    // Forward the request to the LMAC
    if ((error = bl_send_twt_teardown(priv, &twt_teardown, &twt_teardown_cfm)) != 0)
        return error;

    // Check the status
    if (twt_teardown_cfm.status != CO_OK)
        return -EIO;

    return count;
}
DEBUGFS_READ_WRITE_FILE_OPS(twt_teardown);

__attribute__((unused)) static ssize_t bl_dbgfs_rc_stats_read(
                                    struct file *file, char __user *user_buf,
                                    size_t count, loff_t *ppos)
{
    struct bl_sta *sta = NULL;
    struct bl_hw *priv = file->private_data;
    char *buf;
    int bufsz, len = 0;
    ssize_t read;
    int i = 0;
    int error = 0;
    struct me_rc_stats_cfm me_rc_stats_cfm;
    unsigned int no_samples;
    struct st *st;

    BL_DBG(BL_FN_ENTRY_STR);

    /* everything should fit in one call */
    if (*ppos)
        return 0;

    /* Get the station index from MAC address */
    sta = bl_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    /* Forward the information to the LMAC */
    if ((error = bl_send_me_rc_stats(priv, sta->sta_idx, &me_rc_stats_cfm)))
        return error;

    no_samples = me_rc_stats_cfm.no_samples;
    if (no_samples == 0)
        return 0;

    bufsz = no_samples * LINE_MAX_SZ + 500;

    buf = kmalloc(bufsz + 1, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    st = kmalloc(sizeof(struct st) * no_samples, GFP_ATOMIC);
    if (st == NULL)
    {
        kfree(buf);
        return 0;
    }

    for (i = 0; i < no_samples; i++)
    {
        unsigned int tp, eprob;
        len = print_rate_from_cfg(st[i].line, LINE_MAX_SZ,
                                  me_rc_stats_cfm.rate_stats[i].rate_config,
                                  &st[i].r_idx, 0);

        if (me_rc_stats_cfm.sw_retry_step != 0)
        {
            len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len,  "%c",
                    me_rc_stats_cfm.retry_step_idx[me_rc_stats_cfm.sw_retry_step] == i ? '*' : ' ');
        }
        else
        {
            len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, " ");
        }
        len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c",
                me_rc_stats_cfm.retry_step_idx[0] == i ? 'T' : ' ');
        len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c",
                me_rc_stats_cfm.retry_step_idx[1] == i ? 't' : ' ');
        len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c ",
                me_rc_stats_cfm.retry_step_idx[2] == i ? 'P' : ' ');

        tp = me_rc_stats_cfm.tp[i] / 10;
        len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, " %4u.%1u",
                         tp / 10, tp % 10);

        eprob = ((me_rc_stats_cfm.rate_stats[i].probability * 1000) >> 16) + 1;
        len += scnprintf(&st[i].line[len],LINE_MAX_SZ - len,
                         "  %4u.%1u %5u(%6u)  %6u",
                         eprob / 10, eprob % 10,
                         me_rc_stats_cfm.rate_stats[i].success,
                         me_rc_stats_cfm.rate_stats[i].attempts,
                         me_rc_stats_cfm.rate_stats[i].sample_skipped);
    }
    len = scnprintf(buf, bufsz ,
                     "\nTX rate info for %02X:%02X:%02X:%02X:%02X:%02X:\n",
                     sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2],
                     sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5]);

    len += scnprintf(&buf[len], bufsz - len,
            " #  type           rate             tpt   eprob    ok(   tot)   skipped\n");

    // add sorted statistics to the buffer
    sort(st, no_samples, sizeof(st[0]), compare_idx, NULL);
    for (i = 0; i < no_samples; i++)
    {
        len += scnprintf(&buf[len], bufsz - len, "%s\n", st[i].line);
    }

    // display HE TB statistics if any
    if (me_rc_stats_cfm.rate_stats[RC_HE_STATS_IDX].rate_config != 0) {
        unsigned int tp, eprob;
        struct rc_rate_stats *rate_stats = &me_rc_stats_cfm.rate_stats[RC_HE_STATS_IDX];
        int ru_index = rate_stats->ru_and_length & 0x07;
        int ul_length = rate_stats->ru_and_length >> 3;

        len += scnprintf(&buf[len], bufsz - len,
                         "\nHE TB rate info:\n");

        len += scnprintf(&buf[len], bufsz - len,
                "    type           rate             tpt   eprob    ok(   tot)   ul_length\n    ");
        len += print_rate_from_cfg(&buf[len], bufsz - len, rate_stats->rate_config,
                                   NULL, ru_index);

        tp = me_rc_stats_cfm.tp[RC_HE_STATS_IDX] / 10;
        len += scnprintf(&buf[len], bufsz - len, "      %4u.%1u",
                         tp / 10, tp % 10);

        eprob = ((rate_stats->probability * 1000) >> 16) + 1;
        len += scnprintf(&buf[len],bufsz - len,
                         "  %4u.%1u %5u(%6u)  %6u\n",
                         eprob / 10, eprob % 10,
                         rate_stats->success,
                         rate_stats->attempts,
                         ul_length);
    }

    len += scnprintf(&buf[len], bufsz - len, "\n MPDUs AMPDUs AvLen trialP");
    len += scnprintf(&buf[len], bufsz - len, "\n%6u %6u %3d.%1d %6u\n",
                     me_rc_stats_cfm.ampdu_len,
                     me_rc_stats_cfm.ampdu_packets,
                     me_rc_stats_cfm.avg_ampdu_len >> 16,
                     ((me_rc_stats_cfm.avg_ampdu_len * 10) >> 16) % 10,
                     me_rc_stats_cfm.sample_wait);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);
    kfree(st);

    return read;
}

DEBUGFS_READ_FILE_OPS(rc_stats);

__attribute__((unused)) static ssize_t bl_dbgfs_rc_fixed_rate_idx_write(
                              struct file *file, const char __user *user_buf,
                              size_t count, loff_t *ppos)
{
    struct bl_sta *sta = NULL;
    struct bl_hw *priv = file->private_data;
    char buf[10];
    int fixed_rate_idx = -1;
    union bl_rate_ctrl_info rate_config;
    int error = 0;
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    BL_DBG(BL_FN_ENTRY_STR);

    /* Get the station index from MAC address */
    sta = bl_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    /* Get the content of the file */
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;
    buf[len] = '\0';
    sscanf(buf, "%i\n", &fixed_rate_idx);

    /* Convert rate index into rate configuration */
    if ((fixed_rate_idx < 0) || (fixed_rate_idx < N_HE_DCM_RATE_IDX_BASE && 
         fixed_rate_idx >= 
               (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + N_HE_MU + N_HE_ER)))
    {
        // disable fixed rate
        rate_config.value = (u32)-1;
    }
    else
    {
        idx_to_rate_cfg(fixed_rate_idx, &rate_config, NULL);
    }

    // Forward the request to the LMAC
    if ((error = bl_send_me_rc_set_rate(priv, sta->sta_idx,
                                          (u16)rate_config.value)) != 0)
    {
        return error;
    }

    priv->debugfs.rc_config[sta->sta_idx] = (int)rate_config.value;
    return len;
}

DEBUGFS_WRITE_FILE_OPS(rc_fixed_rate_idx);

__attribute__((unused)) static ssize_t bl_dbgfs_last_rx_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_sta *sta = NULL;
    struct bl_hw *priv = file->private_data;
    struct bl_rx_rate_stats *rate_stats;
    char *buf;
    int bufsz, i, len = 0;
    ssize_t read;
    unsigned int fmt, pre, bw, nss, mcs, gi;
    struct rx_vector_1 *last_rx;
    char hist[] = "##################################################";
    int hist_len = sizeof(hist) - 1;
    u8 nrx;

    BL_DBG(BL_FN_ENTRY_STR);

    /* everything should fit in one call */
    if (*ppos)
        return 0;

    /* Get the station index from MAC address */
    sta = bl_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    rate_stats = &sta->stats.rx_rate;
    bufsz = (rate_stats->rate_cnt * ( 50 + hist_len) + 200);
    buf = kmalloc(bufsz + 1, GFP_ATOMIC);
    if (buf == NULL)
        return 0;

    // Get number of RX paths
    nrx = (priv->version_cfm.version_phy_1 & MDM_NRX_MASK) >> MDM_NRX_LSB;

    len += scnprintf(buf, bufsz,
                     "\nRX rate info for %02X:%02X:%02X:%02X:%02X:%02X:\n",
                     sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2],
                     sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5]);

    // Display Statistics
    for (i = 0 ; i < rate_stats->size ; i++ )
    {
        if (rate_stats->table[i]) {
            union bl_rate_ctrl_info rate_config;
            /*to avoid build error on raspberry [ERROR: "__aeabi_uldivmod" undefined!] */
            //int percent = (((u64)rate_stats->table[i]) * 1000) / rate_stats->cpt;
            int percent = (((rate_stats->table[i]) * 10) / rate_stats->cpt) * 100;
            int p;
            int ru_size;

            idx_to_rate_cfg(i, &rate_config, &ru_size);
            len += print_rate_from_cfg(&buf[len], bufsz - len,
                                       rate_config.value, NULL, ru_size);
            p = (percent * hist_len) / 1000;
            len += scnprintf(&buf[len], bufsz - len, ": %9d(%2d.%1d%%)%.*s\n",
                             rate_stats->table[i],
                             percent / 10, percent % 10, p, hist);
        }
    }

    // Display detailed info of the last received rate
    last_rx = &sta->stats.last_rx.rx_vect1;

    len += scnprintf(&buf[len], bufsz - len,"\nLast received rate\n"
                     "  type         rate    LDPC STBC BEAMFM DCM DOPPLER %s\n",
                     (nrx > 1) ? "rssi1(dBm) rssi2(dBm)" : "rssi(dBm)");

    fmt = last_rx->format_mod;
    bw = last_rx->ch_bw;
    pre = last_rx->pre_type;
    if (fmt >= FORMATMOD_HE_SU) {
        mcs = last_rx->he.mcs;
        nss = last_rx->he.nss;
        gi = last_rx->he.gi_type;
        if (fmt == FORMATMOD_HE_MU)
            bw = last_rx->he.ru_size;
    } else if (fmt == FORMATMOD_VHT) {
        mcs = last_rx->vht.mcs;
        nss = last_rx->vht.nss;
        gi = last_rx->vht.short_gi;
    } else if (fmt >= FORMATMOD_HT_MF) {
        mcs = last_rx->ht.mcs % 8;
        nss = last_rx->ht.mcs / 8;;
        gi = last_rx->ht.short_gi;
    } else {
        BUG_ON((mcs = legrates_lut[last_rx->leg_rate].idx) == -1);
        nss = 0;
        gi = 0;
    }

    len += print_rate(&buf[len], bufsz - len, fmt, nss, mcs, bw, gi, pre, NULL);

    /* flags for HT/VHT/HE */
    if (fmt >= FORMATMOD_HE_SU) {
        len += scnprintf(&buf[len], bufsz - len, "  %c    %c     %c    %c     %c",
                         last_rx->he.fec ? 'L' : ' ',
                         last_rx->he.stbc ? 'S' : ' ',
                         last_rx->he.beamformed ? 'B' : ' ',
                         last_rx->he.dcm ? 'D' : ' ',
                         last_rx->he.doppler ? 'D' : ' ');
    } else if (fmt == FORMATMOD_VHT) {
        len += scnprintf(&buf[len], bufsz - len, "  %c    %c     %c           ",
                         last_rx->vht.fec ? 'L' : ' ',
                         last_rx->vht.stbc ? 'S' : ' ',
                         last_rx->vht.beamformed ? 'B' : ' ');
    } else if (fmt >= FORMATMOD_HT_MF) {
        len += scnprintf(&buf[len], bufsz - len, "  %c    %c                  ",
                         last_rx->ht.fec ? 'L' : ' ',
                         last_rx->ht.stbc ? 'S' : ' ');
    } else {
        len += scnprintf(&buf[len], bufsz - len, "                         ");
    }
    if (nrx > 1) {
        len += scnprintf(&buf[len], bufsz - len, "       %-4d       %d\n",
                         last_rx->rssi1, last_rx->rssi1);
    } else {
        len += scnprintf(&buf[len], bufsz - len, "      %d\n", last_rx->rssi1);
    }

    read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

    kfree(buf);
    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_last_rx_write(struct file *file,
                         const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_sta *sta = NULL;
    struct bl_hw *priv = file->private_data;

    /* Get the station index from MAC address */
    sta = bl_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
    if (sta == NULL)
        return -EINVAL;

    /* Prevent from interrupt preemption as these statistics are updated under
     * interrupt */
    spin_lock_bh(&priv->tx_lock);
    memset(sta->stats.rx_rate.table, 0,
           sta->stats.rx_rate.size * sizeof(sta->stats.rx_rate.table[0]));
    sta->stats.rx_rate.cpt = 0;
    sta->stats.rx_rate.rate_cnt = 0;
    spin_unlock_bh(&priv->tx_lock);

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(last_rx);

#endif /* CONFIG_BL_FULLMAC */

/*
 * trace helper
 */
void bl_fw_trace_dump(struct bl_hw *bl_hw)
{
    BL_DBG(BL_FN_ENTRY_STR);
    /* may be called before bl_dbgfs_register */
    if (bl_hw->plat->enabled && !bl_hw->debugfs.fw_trace.buf.data) {
        bl_fw_trace_buf_init(&bl_hw->debugfs.fw_trace.buf,
                               bl_ipc_fw_trace_desc_get(bl_hw));
    }

    if (!bl_hw->debugfs.fw_trace.buf.data)
        return;

    _bl_fw_trace_dump(&bl_hw->debugfs.fw_trace.buf);
}

void bl_fw_trace_reset(struct bl_hw *bl_hw)
{
    _bl_fw_trace_reset(&bl_hw->debugfs.fw_trace, true);
}

void bl_dbgfs_trigger_fw_dump(struct bl_hw *bl_hw, char *reason)
{
    bl_send_dbg_trigger_req(bl_hw, reason);
}

#ifdef CONFIG_BL_FULLMAC
__attribute__((unused)) static void _bl_dbgfs_register_sta(
                             struct bl_debugfs *bl_debugfs, struct bl_sta *sta)
{
    struct bl_hw *bl_hw = container_of(bl_debugfs, struct bl_hw, debugfs);
    struct dentry *dir_sta;
    char sta_name[18];
    struct dentry *dir_rc;
    struct dentry *file;
    struct bl_rx_rate_stats *rate_stats = &sta->stats.rx_rate;
    int nb_rx_rate = N_CCK + N_OFDM;
    struct bl_rc_config_save *rc_cfg, *next;

    if (sta->sta_idx >= NX_REMOTE_STA_MAX) {
        scnprintf(sta_name, sizeof(sta_name), "bc_mc");
    } else {
        scnprintf(sta_name, sizeof(sta_name), "%pM", sta->mac_addr);
    }

    if (!(dir_sta = debugfs_create_dir(sta_name, bl_debugfs->dir_stas)))
        goto error;
    bl_debugfs->dir_sta[sta->sta_idx] = dir_sta;

    if (!(dir_rc = debugfs_create_dir("rc", bl_debugfs->dir_sta[sta->sta_idx])))
        goto error_after_dir;

    bl_debugfs->dir_rc_sta[sta->sta_idx] = dir_rc;

    file = debugfs_create_file("stats", S_IRUSR, dir_rc, bl_hw,
                               &bl_dbgfs_rc_stats_ops);
    if (IS_ERR_OR_NULL(file))
        goto error_after_dir;

    file = debugfs_create_file("fixed_rate_idx", S_IWUSR , dir_rc, bl_hw,
                               &bl_dbgfs_rc_fixed_rate_idx_ops);
    if (IS_ERR_OR_NULL(file))
        goto error_after_dir;

    file = debugfs_create_file("rx_rate", S_IRUSR | S_IWUSR, dir_rc, bl_hw,
                               &bl_dbgfs_last_rx_ops);
    if (IS_ERR_OR_NULL(file))
        goto error_after_dir;

    if (bl_hw->mod_params->ht_on)
        nb_rx_rate += N_HT;

    if (bl_hw->mod_params->vht_on)
        nb_rx_rate += N_VHT;

    if (bl_hw->mod_params->he_on)
        nb_rx_rate += N_HE_SU + N_HE_MU;

    rate_stats->table = kzalloc(nb_rx_rate * sizeof(rate_stats->table[0]),
                                GFP_KERNEL);
    if (!rate_stats->table)
        goto error_after_dir;

    rate_stats->size = nb_rx_rate;
    rate_stats->cpt = 0;
    rate_stats->rate_cnt = 0;

    /* By default enable rate contoller */
    bl_debugfs->rc_config[sta->sta_idx] = -1;

    /* Unless we already fix the rate for this station */
    list_for_each_entry_safe(rc_cfg, next, &bl_debugfs->rc_config_save, list) {
        if (jiffies_to_msecs(jiffies - rc_cfg->timestamp) > RC_CONFIG_DUR) {
            list_del(&rc_cfg->list);
            kfree(rc_cfg);
        } else if (!memcmp(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN)) {
            bl_debugfs->rc_config[sta->sta_idx] = rc_cfg->rate;
            list_del(&rc_cfg->list);
            kfree(rc_cfg);
            break;
        }
    }

    if ((bl_debugfs->rc_config[sta->sta_idx] >= 0) &&
        bl_send_me_rc_set_rate(bl_hw, sta->sta_idx,
                                 (u16)bl_debugfs->rc_config[sta->sta_idx]))
        bl_debugfs->rc_config[sta->sta_idx] = -1;

    if (BL_VIF_TYPE(bl_hw->vif_table[sta->vif_idx]) == NL80211_IFTYPE_STATION)
    {
        /* register the sta */
        struct dentry *dir_twt;
        struct dentry *file;

        if (!(dir_twt = debugfs_create_dir("twt", 
                                      bl_debugfs->dir_sta[sta->sta_idx])))
            goto error_after_dir;

        bl_debugfs->dir_twt_sta[sta->sta_idx] = dir_twt;

        file = debugfs_create_file("request", S_IRUSR | S_IWUSR, dir_twt, bl_hw,
                                   &bl_dbgfs_twt_request_ops);
        if (IS_ERR_OR_NULL(file))
            goto error_after_dir;

        file = debugfs_create_file("teardown", S_IRUSR | S_IWUSR, dir_twt, bl_hw,
                                   &bl_dbgfs_twt_teardown_ops);
        if (IS_ERR_OR_NULL(file))
            goto error_after_dir;

        sta->twt_ind.sta_idx = BL_INVALID_STA;
    }
    return;

    error_after_dir:
      debugfs_remove_recursive(bl_debugfs->dir_sta[sta->sta_idx]);
      bl_debugfs->dir_sta[sta->sta_idx] = NULL;
      bl_debugfs->dir_rc_sta[sta->sta_idx] = NULL;
      bl_debugfs->dir_twt_sta[sta->sta_idx] = NULL;
    error:
      dev_err(bl_hw->dev,
              "Error while registering debug entry for sta %d\n", sta->sta_idx);
}

__attribute__((unused)) static void _bl_dbgfs_unregister_sta(
                              struct bl_debugfs *bl_debugfs, struct bl_sta *sta)
{
    debugfs_remove_recursive(bl_debugfs->dir_sta[sta->sta_idx]);
    /* unregister the sta */
    if (sta->stats.rx_rate.table) {
        kfree(sta->stats.rx_rate.table);
        sta->stats.rx_rate.table = NULL;
    }
    sta->stats.rx_rate.size = 0;
    sta->stats.rx_rate.cpt  = 0;
    sta->stats.rx_rate.rate_cnt = 0;

    /* If fix rate was set for this station, save the configuration in case
       we reconnect to this station within RC_CONFIG_DUR msec */
    if (bl_debugfs->rc_config[sta->sta_idx] >= 0) {
        struct bl_rc_config_save *rc_cfg;
        rc_cfg = kmalloc(sizeof(*rc_cfg), GFP_KERNEL);
        if (rc_cfg) {
            rc_cfg->rate = bl_debugfs->rc_config[sta->sta_idx];
            rc_cfg->timestamp = jiffies;
            memcpy(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN);
            list_add_tail(&rc_cfg->list, &bl_debugfs->rc_config_save);
        }
    }

    bl_debugfs->dir_sta[sta->sta_idx] = NULL;
    bl_debugfs->dir_rc_sta[sta->sta_idx] = NULL;
    bl_debugfs->dir_twt_sta[sta->sta_idx] = NULL;
    sta->twt_ind.sta_idx = BL_INVALID_STA;
}

__attribute__((unused)) static void bl_sta_work(struct work_struct *ws)
{
    struct bl_debugfs *bl_debugfs = container_of(ws, struct bl_debugfs, sta_work);
    struct bl_hw *bl_hw = container_of(bl_debugfs, struct bl_hw, debugfs);
    struct bl_sta *sta;
    uint8_t sta_idx;

    sta_idx = bl_debugfs->sta_idx;
    if (sta_idx > (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
        WARN(1, "Invalid sta index %d", sta_idx);
        return;
    }

    bl_debugfs->sta_idx = BL_INVALID_STA;
    sta = &bl_hw->sta_table[sta_idx];
    if (!sta) {
        WARN(1, "Invalid sta %d", sta_idx);
        return;
    }

    if (bl_debugfs->dir_sta[sta_idx] == NULL)
        _bl_dbgfs_register_sta(bl_debugfs, sta);
    else
        _bl_dbgfs_unregister_sta(bl_debugfs, sta);

    return;
}

void _bl_dbgfs_sta_write(struct bl_debugfs *bl_debugfs, uint8_t sta_idx)
{
    if (bl_debugfs->unregistering)
        return;

    bl_debugfs->sta_idx = sta_idx;
    schedule_work(&bl_debugfs->sta_work);
}

void bl_dbgfs_unregister_sta(struct bl_hw *bl_hw,
                               struct bl_sta *sta)
{
    _bl_dbgfs_sta_write(&bl_hw->debugfs, sta->sta_idx);
}

void bl_dbgfs_register_sta(struct bl_hw *bl_hw,
                             struct bl_sta *sta)
{
    _bl_dbgfs_sta_write(&bl_hw->debugfs, sta->sta_idx);
}
#endif /* CONFIG_BL_FULLMAC */

#ifdef CONFIG_BL_SDIO
__attribute__((unused)) static ssize_t bl_dbgfs_run_fw_read(struct file *file,
                              char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    char buf[32];
    int ret;
    ssize_t read;
	u8 data;

	bl_read_reg(priv, 0x60, &data);

	printk("data=0x%x\n", data);

    ret = scnprintf(buf, 4, "0x%x", data);

    read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

    return read;
}

__attribute__((unused)) static ssize_t bl_dbgfs_run_fw_write(struct file *file,
                        const char __user *user_buf, size_t count, loff_t *ppos)
{
    struct bl_hw *priv = file->private_data;
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(priv->plat)->priv;
    int  status = 0;    
    u8   val = 0;
    char buf[32];
    size_t len = min_t(size_t, count, sizeof(buf) - 1);

    BL_DBG(BL_FN_ENTRY_STR);

    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';

    if (sscanf(buf, "%d", &val) > 0)
    {
        printk("val=0x%x-%c\n", val, val);
        bl_write_reg(priv, CARD_FW_STATUS0_REG, val);
    }

    /* echo 13 > /sys/kernel/debug/ieee80211/phy?/bl/run_fw */
    if (val == 0xD) {
        bl_sdio_reset(priv);
    }

    /* echo 14 > /sys/kernel/debug/ieee80211/phy?/bl/run_fw */
    if (val == 0xE) {
        status = bl_sdio_read_reg_func0(card, CARD_BUS_CTRL, &val);
        printk("Read F0 addr7 val=0x%x status=0x%x\r\n", val, status);
        val |= CARD_SDIO_IRQ;
        status = 0;
        status = bl_sdio_write_reg_func0(card, CARD_BUS_CTRL, val);
        printk("Trigger INT7 write val=0x%x status0x%x\r\n", val, status);

        mdelay(100);

        status = bl_sdio_read_reg_func0(card, CARD_BUS_CTRL, &val);
        printk("Read F0 addr7 val=0x%x status=0x%x\r\n", val, status);
        val &= ~CARD_SDIO_IRQ;
        status = 0;
        status = bl_sdio_write_reg_func0(card, CARD_BUS_CTRL, val);
        printk("Trigger INT7 write val=0x%x status0x%x\r\n", val, status);
    }

    return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(run_fw);

#endif  /* CONFIG_BL_SDIO */


int bl_dbgfs_register(struct bl_hw *bl_hw, const char *name)
{
    struct dentry *phyd = bl_hw->wiphy->debugfsdir;
    struct bl_debugfs *bl_debugfs = &bl_hw->debugfs;
    struct dentry *dir_drv, *dir_diags, *dir_stas;

    if (!(dir_drv = debugfs_create_dir(name, phyd)))
        return -ENOMEM;

    bl_debugfs->dir = dir_drv;

    if (!(dir_stas = debugfs_create_dir("stations", dir_drv)))
        return -ENOMEM;

    bl_debugfs->dir_stas = dir_stas;
    bl_debugfs->unregistering = false;

    if (!(dir_diags = debugfs_create_dir("diags", dir_drv)))
        goto err;

#ifdef CONFIG_BL_FULLMAC
    INIT_WORK(&bl_debugfs->sta_work, bl_sta_work);
    INIT_LIST_HEAD(&bl_debugfs->rc_config_save);
    bl_debugfs->sta_idx = BL_INVALID_STA;
#endif

    DEBUGFS_ADD_U32(tcp_pacing_shift, dir_drv, &bl_hw->tcp_pacing_shift,
                    S_IWUSR | S_IRUSR);
    DEBUGFS_ADD_FILE(stats, dir_drv, S_IWUSR | S_IRUSR);
#ifdef CONFIG_BL_USB
    DEBUGFS_ADD_FILE(usb_rw_test, dir_drv, S_IWUSR | S_IRUSR);
#endif
#if defined(BL_BUS_LOOPBACK) &&  (defined(CONFIG_BL_SDIO) || defined(CONFIG_BL_USB))
    DEBUGFS_ADD_FILE(loopback_test, dir_drv, S_IWUSR | S_IRUSR);
#endif

    DEBUGFS_ADD_FILE(sys_stats, dir_drv,  S_IRUSR);
    DEBUGFS_ADD_FILE(txq, dir_drv, S_IRUSR);
    DEBUGFS_ADD_FILE(acsinfo, dir_drv, S_IRUSR);
#ifdef CONFIG_BL_SDIO
    DEBUGFS_ADD_FILE(run_fw, dir_drv, S_IWUSR | S_IRUSR);
#endif

#ifdef CONFIG_BL_MUMIMO_TX
    DEBUGFS_ADD_FILE(mu_group, dir_drv, S_IRUSR);
#endif

#ifdef CONFIG_BL_P2P_DEBUGFS
    {
        /* Create a p2p directory */
        struct dentry *dir_p2p;
        if (!(dir_p2p = debugfs_create_dir("p2p", dir_drv)))
            goto err;

        /* Add file allowing to control Opportunistic PS */
        DEBUGFS_ADD_FILE(oppps, dir_p2p, S_IRUSR);
        /* Add file allowing to control Notice of Absence */
        DEBUGFS_ADD_FILE(noa, dir_p2p, S_IRUSR);
    }
#endif /* CONFIG_BL_P2P_DEBUGFS */

    if (bl_dbgfs_register_fw_dump(bl_hw, dir_drv, dir_diags))
        goto err;
        
    DEBUGFS_ADD_FILE(fw_dbg, dir_diags, S_IWUSR | S_IRUSR);

    if (!bl_fw_trace_init(&bl_hw->debugfs.fw_trace,
                            bl_ipc_fw_trace_desc_get(bl_hw))) {
        DEBUGFS_ADD_FILE(fw_trace, dir_diags, S_IWUSR | S_IRUSR);
        
        if (bl_hw->debugfs.fw_trace.buf.nb_compo)
            DEBUGFS_ADD_FILE(fw_trace_level, dir_diags, S_IWUSR | S_IRUSR);
    } else {
        printk("%s fw_trace init fail, closing=%d\n",
               __func__, bl_hw->debugfs.fw_trace.closing);
        bl_debugfs->fw_trace.buf.data = NULL;
    }

#ifdef CONFIG_BL_RADAR
    {
        struct dentry *dir_radar, *dir_sec;
        if (!(dir_radar = debugfs_create_dir("radar", dir_drv)))
            goto err;

        DEBUGFS_ADD_FILE(pulses_prim, dir_radar, S_IRUSR);
        DEBUGFS_ADD_FILE(detected,    dir_radar, S_IRUSR);
        DEBUGFS_ADD_FILE(enable,      dir_radar, S_IRUSR);

        if (bl_hw->phy.cnt == 2) {
            DEBUGFS_ADD_FILE(pulses_sec, dir_radar, S_IRUSR);

            if (!(dir_sec = debugfs_create_dir("sec", dir_radar)))
                goto err;

            DEBUGFS_ADD_FILE(band,    dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(type,    dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(prim20,  dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(center1, dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(center2, dir_sec, S_IWUSR | S_IRUSR);
            DEBUGFS_ADD_FILE(set,     dir_sec, S_IWUSR | S_IRUSR);
        }
    }
#endif /* CONFIG_BL_RADAR */
    return 0;

err:
    bl_dbgfs_unregister(bl_hw);
    return -ENOMEM;
}

void bl_dbgfs_unregister(struct bl_hw *bl_hw)
{
    struct bl_debugfs *bl_debugfs = &bl_hw->debugfs;

#ifdef CONFIG_BL_FULLMAC
    struct bl_rc_config_save *cfg, *next;
    list_for_each_entry_safe(cfg, next, &bl_debugfs->rc_config_save, list) {
        list_del(&cfg->list);
        kfree(cfg);
    }
#endif /* CONFIG_BL_FULLMAC */

    if (!bl_hw->debugfs.dir)
        return;

#ifdef CONFIG_BL_SDIO
    spin_lock_irq(&bl_debugfs->umh_lock);
#else
    spin_lock_bh(&bl_debugfs->umh_lock);
#endif
    bl_debugfs->unregistering = true;
#ifdef CONFIG_BL_SDIO
    spin_unlock_irq(&bl_debugfs->umh_lock);
#else
    spin_unlock_bh(&bl_debugfs->umh_lock);
#endif
    bl_wait_um_helper(bl_hw);
    bl_fw_trace_deinit(&bl_hw->debugfs.fw_trace);
#ifdef CONFIG_BL_FULLMAC
    flush_work(&bl_debugfs->sta_work);
#endif
    debugfs_remove_recursive(bl_hw->debugfs.dir);
    bl_hw->debugfs.dir = NULL;
}

