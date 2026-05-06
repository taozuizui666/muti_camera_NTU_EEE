/**
 ******************************************************************************
 *
 *  @file bl_iwpriv.c
 *
 *  Copyright (C) BouffaloLab 2017-2021
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


#include <linux/version.h>
#include <linux/module.h>
#include <linux/inetdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif
#include <linux/mmc/sdio.h>
#include <linux/timer.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "bl_compat.h"
#include "bl_defs.h"
#include "bl_msg_tx.h"
#include "bl_tx.h"
#include "bl_hal_desc.h"
#include "bl_debugfs.h"
#include "bl_iwpriv.h"
#include "bl_cfgfile.h"
#include "bl_irqs.h"
#include "bl_version.h"
#include "bl_main.h"
#include "bl_nl_events.h"
#if defined(CONFIG_BL_SDIO) 
#include "sdio/bl_sdio.h"
#include "bl_cmds.h"
#endif

uint32_t bl_trace_dyn_level = 0;
uint32_t bl_trace_dyn_module = 0;

uint8_t icmp_discard_thr = 0;
uint8_t icmp_period_thr = 7;
unsigned char cmw_eth_addr[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5};
extern void bl_nl_broadcast_event(struct bl_hw *bl_hw, u32 event_id, u8* payload, u16 len);
extern int btsdio_send_frame(struct hci_dev *hdev, struct sk_buff *skb);
extern int btusb_send_frame(struct hci_dev *hdev, struct sk_buff *skb);

struct cfg_kv_t cfg_kv[] = {
    {
        .key =      "capcode=",
        .id =       CFG_CAPCODE_ID,
    },
    {
        .key =      "power_offset=",
        .id =       CFG_PWR_OFT_ID,
    },
    {
        .key =      "mac=",
        .id =       CFG_MAC_ID,
    },
    {
        .key =      "en_tcal=",
        .id =       CFG_EN_TCAL_ID,
    },
    {
        .key =      "Tchannel_os=",
        .id =       CFG_TCHANNEL_OS_ID,
    },
    {
        .key =      "Tchannel_os_low=",
        .id =       CFG_TCHANNEL_OS_LOW_ID,
    },
    {
        .key =      "linear_or_follow=",
        .id =       CFG_LINEAR_ID,
    },
    {
        .key =      "Tchannels=",
        .id =       CFG_TCHANNELS_ID,
    },
    {
        .key =      "Troom_os=",
        .id =       CFG_TROOM_OS_ID,
    },
    {
        .key =      "xtal_mode=",
        .id =       CFG_XTAL_MODE_ID,
    },
    {
        .key =      "xtal_cap=",
        .id =       CFG_XTAL_CAP_ID,
    },
    {
        .key =      "pwr_mode=",
        .id =       CFG_PWR_MODE_ID,
    },
    {
        .key =      "pwr_table_11b=",
        .id =       CFG_PWR_11B_ID,
    },
    {
        .key =      "pwr_table_11g=",
        .id =       CFG_PWR_11G_ID,
    },
    {
        .key =      "pwr_table_11n_ht20=",
        .id =       CFG_PWR_11N_HT20_ID,
    },
    {
        .key =      "pwr_table_11n_ht40=",
        .id =       CFG_PWR_11N_HT40_ID,
    },
    {
        .key =      "pwr_table_11ax_he20=",
        .id =       CFG_PWR_11AX_HE20_ID,
    },
    {
        .key =      "pwr_table_11ax_he40=",
        .id =       CFG_PWR_11AX_HE40_ID,
    },
    {
        .key =      "pwr_table_11ax_he80=",
        .id =       CFG_PWR_11AX_HE80_ID,
    },
    {
        .key =      "pwr_table_11ax_he160=",
        .id =       CFG_PWR_11AX_HE160_ID,
    },
    {
        .key =      "pwr_table_ble=",
        .id =       CFG_PWR_BLE_ID,
    },
    {
        .key =      "pwr_table_bt=",
        .id =       CFG_PWR_BT_ID,
    },
    {
        .key =      "pwr_oft_bz=",
        .id =       CFG_PWR_OFT_BZ_ID,
    },
    {
        .key =      "country_code=",
        .id =       CFG_COUNTRY_CODE_ID,
    },
};
int cfg_num = sizeof(cfg_kv)/sizeof(cfg_kv[0]);

uint32_t get_args_type(struct net_device *dev, uint16_t cmd) {
    const struct iw_priv_args *descr;
    int i;

    descr = NULL;
    
#ifdef CONFIG_WEXT_PRIV
    for (i = 0; i < dev->wireless_handlers->num_private_args; i++) {
        if (cmd == dev->wireless_handlers->private_args[i].cmd) {
            descr = &dev->wireless_handlers->private_args[i];
            break;
        }
    }
#endif

    if (descr == NULL) {
        printk("%s, unknow cmd, not in iw_priv_args.\r\n", __func__);
        
        return IW_PRIV_TYPE_CHAR;
    } else {
        if (IW_IS_SET(cmd))
            return (descr->set_args & IW_PRIV_TYPE_MASK);
        else
            return (descr->get_args & IW_PRIV_TYPE_MASK);
    }    
}

int read_file(uint8_t *file_name, uint8_t *buf, uint16_t buf_len) {
    int ret = 0;
    struct file *fp;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
    mm_segment_t fs;
#endif
    loff_t pos;

    fp = filp_open(file_name, O_RDONLY, 0644);
    if (IS_ERR(fp)){
        printk("%s, open file error: %s\n", __func__, file_name);
        
        return -EPIPE;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
    fs = get_fs();
    set_fs(KERNEL_DS);
#endif

    printk("%s, open file done: %s\n", __func__, file_name);

    pos =0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
    ret = kernel_read(fp, pos, buf, buf_len);
#else
    ret = kernel_read(fp, buf, buf_len, &pos);
#endif
    filp_close(fp, NULL);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
    set_fs(fs);
#endif

    return ret;
}

int load_cal_data(char *file_path, struct mm_cal_cfg_req *cal_cfg_req) {
    int ret = 0;
    uint8_t *cfg_buf;
    uint16_t cfg_buf_len = 2500;

    printk("enter %s \n",__func__);
    
    cfg_buf = kzalloc(cfg_buf_len, GFP_KERNEL);
    
    if (cfg_buf == NULL) {
        printk("%s, fail to alloc cfg_buf\n", __func__);
        
        return -ENOMEM;
    }
    
    memset(cfg_buf, 0, cfg_buf_len);
    
    ret = read_file(file_path, cfg_buf, cfg_buf_len-1);
    if (ret < 0) {
        printk("%s, read file %s error, ret:%d\n", __func__, file_path, ret);
        
        kfree(cfg_buf);
        return ret;
    }
    
    cfg_buf[ret] = '\0';

    printk("%s, cfg file:%s\n", __func__, cfg_buf);

    //set default value, but not enable by VALID_RF_PARA
    cal_cfg_req->tcal.Tchannels[0] = 2412;
    cal_cfg_req->tcal.Tchannels[1] = 2427;
    cal_cfg_req->tcal.Tchannels[2] = 2442;
    cal_cfg_req->tcal.Tchannels[3] = 2457;
    cal_cfg_req->tcal.Tchannels[4] = 2472;
    cal_cfg_req->tcal.linear_or_follow = 1;
    cal_cfg_req->tcal.Troom_os = 255;

    cal_cfg_req->xtal.xtal_cap[0] = 34;
    cal_cfg_req->xtal.xtal_cap[1] = 34;
    cal_cfg_req->xtal.xtal_cap[2] = 0;
    cal_cfg_req->xtal.xtal_cap[3] = 60;
    cal_cfg_req->xtal.xtal_cap[4] = 60;

    //set default value, enable by VALID_RF_PARA
    cal_cfg_req->pwr_mode_valid = VALID_RF_PARA;
    cal_cfg_req->pwr_mode[0] = 'B';
    cal_cfg_req->pwr_mode[1] = 'F';

    cal_cfg_req->xtal_mode_valid = VALID_RF_PARA;
    cal_cfg_req->xtal.xtal_mode[0] = 'M';
    cal_cfg_req->xtal.xtal_mode[1] = 'F';

    ret = bl_parse_confg_kv(cfg_buf, cfg_buf_len, 
                            cal_cfg_req, cfg_kv, cfg_num);
    if (ret != 0) {
        printk("%s, parsing %s fail\n", __func__, file_path);
    }
    
    kfree(cfg_buf);

    return ret;
}

#ifdef CONFIG_BL_MP
static int bl_iwpriv_common_hdl(struct net_device *dev, uint16_t cmd,
                         union iwreq_data *wrqu, 
                         char *extra, struct iwpriv_cmd *iwp_cmds, uint32_t sc_num, 
                         uint16_t sc_index, uint8_t *in_buf, uint16_t in_len);
//static int parse_str_2_one_bool(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
//static int parse_str_2_one_uint(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_one_bool(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_tx_param(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
//static int parse_one_uint(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_set_duty(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_set_xtal_cap(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
//static int parse_set_misc_param(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_wr_mem(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_rd_mem(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_pkt_len(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
//static int parse_unicast_tx_param(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_11b_rate(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_11g_rate(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_11n_rate(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_11ax_rate(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
//static int parse_set_pkt_freq(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_set_channel(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_set_power(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
//static int parse_sleep_dtim(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_help_args(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_ble_tx(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_ble_rx(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_bt_tx(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_bt_rx(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
//static int parse_ef_wr(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_ef_cap_wr(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_ef_pwr_oft_wr(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_ef_bz_pwr_oft_wr(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_ef_mac_wr(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);
static int parse_pwr_offset_en(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *var_len);

//single
struct iwpriv_sub_cmd iwp_mp_sc_ver[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> version\r\n\
                            example: sudo iwpriv wlan0 version\r\n",
        .mfg_cmd_name =     NULL,
        .mfg_cmd_len =      0,
        .ind_wait_ms =      0,
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_help[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       IS_VAR_LEN|0,
        .sc_var_parser =    parse_help_args,
        .mfg_cmd_name =     NULL,
        .mfg_cmd_len =      0,
        .ind_wait_ms =      0,
    },
};

//ms
#if 0
struct iwpriv_sub_cmd iwp_mp_sc_uc_tx[] = 
{
    {
        .sc_name.iname =    1,          //tx_on
        .sc_len =           4,
        .sc_var_len =       4,          //1 uint, as "<pkt number to be sent>"
        .sc_var_parser =    parse_unicast_tx_param,
        .sc_helper =        "iwpriv <interface> mp_uc_tx <on> <pkt number to be sent>\r\n \
                            on: 1\r\n \
                            pkt number to be sent: > 0\r\n \
                            example: sudo iwpriv wlan0 mp_uc_tx 1 100\r\n",
        .mfg_cmd_name =     "ut1",      //Or "UT1"
        .mfg_cmd_len =      3,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "unicat tx on",
    },
    {
        .sc_name.iname =    0,          //tx_off
        .sc_len =           4,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_uc_tx <off>\r\n \
                            off: 0\r\n \
                            example: sudo iwpriv wlan0 mp_uc_tx 0\r\n",
        .mfg_cmd_name =     "ut0\n",    //Or "UT0\n"
        .mfg_cmd_len =      5,          //include \0
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "unicat tx off",
    },
};
#endif

struct iwpriv_sub_cmd iwp_mp_sc_11b_rate[] = 
{
    {
        .sc_name.iname =    0,          //short preamble
        .sc_len =           4,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, as "<rate idx>"
        .sc_var_parser =    parse_11b_rate,
        .sc_helper =        "iwpriv <interface> mp_11b_rate <preamble> <rate_idx>\r\n \
                            short preamble: 0, long preamble: 1, \r\n \
                            rate_idx: 2.4G 11b rate idx = 0 - 3, 0:1Mbps, 1:2Mbps, 2:5.5Mbps, 3:11Mbps\r\n \
                            example: sudo iwpriv wlan0 mp_11b_rate 1 2\r\n",
        .mfg_cmd_name =     "b",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "tx mode 0",
    },
    {
        .sc_name.iname =    1,          //long preamble
        .sc_len =           4,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, as "<rate>", 2.4G 11b rate idx = 0 - 3, 0:1Mbps, 1:2Mbps, 2:5.5Mbps, 3:11Mbps
        .sc_var_parser =    parse_11b_rate,
        .mfg_cmd_name =     "B",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "tx mode 0",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_11g_rate[] = 
{
#if 0
    {
        .sc_name.iname =    0,          //short preamble
        .sc_len =           4,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, as "<rate idx>"
        .sc_var_parser =    parse_11g_rate,
        .sc_helper =        "iwpriv <interface> mp_11g_rate <short long preamble> <rate idx>\r\n \
                            short preamble: 0, long preamble: 1, \r\n \
                            rate idx: 2.4G 11g rate idx = 0 - 7, 0:6Mbps 1:9Mbps 2:12Mbps 3:18Mbps 4:24Mbps 5:36Mbps 6:48Mbps 7:54Mbps\r\n \
                            example: sudo iwpriv wlan0 mp_11g_rate 1 7\r\n",
        .mfg_cmd_name =     "g",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "tx mode 1",
    },
#endif
    {
        .sc_name.iname =    1,          //long preamble
        .sc_len =           4,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, as "<ate>
        .sc_var_parser =    parse_11g_rate,
        .sc_helper =        "iwpriv <interface> mp_11g_rate <preamble> <rate_idx>\r\n \
                            short preamble: 0(NOT SUPPORT), long preamble: 1, \r\n \
                            rate_idx: 2.4G 11g rate idx = 0 - 7, 0:6Mbps 1:9Mbps 2:12Mbps 3:18Mbps 4:24Mbps 5:36Mbps 6:48Mbps 7:54Mbps\r\n \
                            example: sudo iwpriv wlan0 mp_11g_rate 1 7\r\n",
        .mfg_cmd_name =     "G",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "tx mode 1",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_11n_rate[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       16,         //4 uint32, 4*4 bytes
        .sc_var_parser =    parse_11n_rate,
        .sc_helper =        "iwpriv <interface> mp_11n_rate <GI> <mcs_index> <bw> <ldpc>\r\n \
                            GI is place holder, set to 0. short guard interval: 0, long guard interval: 1\r\n \
                            mcs_index: 0-7\r\n \
                            bw: 2 for 20MHz, 4 for 40MHz(BL616L not support 40M)\r\n \
                            ldpc: 0-1, 0 is BCC coding, 1 is LPDC coding\r\n \
                            example: sudo iwpriv wlan0 mp_11n_rate 0 7 2 0\r\n",
        .mfg_cmd_name =     NULL,
        .mfg_cmd_len =      0,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "tx mode 2",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_11ax_rate[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       16,         //4 uint32, 4*4 bytes
        .sc_var_parser =    parse_11ax_rate,
        .sc_helper =        "iwpriv <interface> mp_11ax_rate <GI> <mcs_index> <bw> <ldpc>\r\n \
                            GI: 0-2, mapping to ['2x HELTF+0.8us GI', '2x HELTF+1.6us GI', '4x HELTF+3.2us GI']\r\n \
                            mcs_index 0-11, SU mcs with HE-DCM=0\r\n \
                            mcs_index 12-15, SU mcs with HE-DCM=1\r\n \
                            mcs_index 16-18, ER mcs with HE-DCM=0\r\n \
                            ldpc: 0-1, 0 is BCC coding, 1 is LPDC coding\r\n \
                            bw: 0,1,2,3. 0 is 20M band, 1 is 40M, 2 is 80M, 3 is 160M(BL616L only support 20M) \r\n \
                            example: sudo iwpriv wlan0 mp_11ax_rate 0 11 1 0\r\n",
        .mfg_cmd_name =     NULL,
        .mfg_cmd_len =      0,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "11ax tx",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_tx[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, as "<on/off>"
        .sc_var_parser =    parse_tx_param,
        .sc_helper =        "iwpriv <interface> mp_tx <on/off/he_tb_on> [RU Size] [RU Index] [mcs_index] [bw]\r\n\
                            off: 0, on: 1, he_tb_on: 2\r\n\
                            only he_tb_on must have all following optional params\r\n\
                            when bw is 20M, RU Size can be 0(26 tone), 1(52), 2(106), 3(242).\r\n\
                                RU Size 26,  then Ru Index: 0~8\r\n\
                                RU Size 52,  then Ru Index: 0~3\r\n\
                                RU Size 106, then Ru Index: 0~1\r\n\
                                RU Size 242, then Ru Index: 0\r\n\
                            when bw is 40M(BL616L not support 40M), RU Size can be 0(26 tone), 1(52), 2(106), 3(242), 4(484).\r\n\
                                RU Size 26,  then Ru Index: 0~17\r\n\
                                RU Size 52,  then Ru Index: 0~7\r\n\
                                RU Size 106, then Ru Index: 0~3\r\n\
                                RU Size 242, then Ru Index: 0~1\r\n\
                                RU Size 484, then Ru Index: 0\r\n\
                            mcs_index: 0-9\r\n\
                            bw: 0 or 1, 0 is 20M band, 1 is 40M band(BL616L not support 40M).\r\n\
                            example: sudo iwpriv wlan0 mp_tx 1\r\n\
                                     sudo iwpriv wlan0 mp_tx 0\r\n\
                                     sudo iwpriv wlan0 mp_tx 2 0 8 9 0\r\n",
        .mfg_cmd_name =     "t",        //Or "T1\n"
        .mfg_cmd_len =      1,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "mfg tx toggle done",
    },
};

#if 0
struct iwpriv_sub_cmd iwp_mp_sc_set_pkt_freq[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, as "<pkt_freq>"
        .sc_var_parser =    parse_set_pkt_freq,
        .sc_helper =        "iwpriv <interface> mp_set_pkt_freq <pkt_freq>\r\n\
                            pkt_freq: pkt to be sent per seconds, {1-1000}\r\n\
                            example: sudo iwpriv wlan0 mp_set_pkt_freq 300\r\n",
        .mfg_cmd_name =     "f",        //Or 'F'
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "tx frenquecy:",
    },
};
#endif

struct iwpriv_sub_cmd iwp_mp_sc_set_pkt_len[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, as "<pkt_len>"
        .sc_var_parser =    parse_pkt_len,
        .sc_helper =        "iwpriv <interface> mp_set_pkt_len <pkt_len>\r\n\
                            pkt_len: length of pkt to be sent\r\n\
                            example: sudo iwpriv wlan0 mp_set_pkt_len 1000\r\n",
        .mfg_cmd_name =     "l",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "tx len ",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_set_ch[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       4,          //1 uint, 4 bytes, as "<channel idx>", which is in [1-14]
        .sc_var_parser =    parse_set_channel,
        .sc_helper =        "iwpriv <interface> mp_set_channel <channel idx>\r\n \
                            channel idx: which is in range {1-14}\r\n \
                            example: sudo iwpriv wlan0 mp_set_channel 14\r\n",
        .mfg_cmd_name =     "c",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_set_cw[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, as "<mode>"
        .sc_var_parser =    parse_one_bool,
        .sc_helper =        "iwpriv <interface> mp_set_cw_mode <mode>\r\n \
                            mode: 0 for none CW mode, 1 for CW mode\r\n \
                            example: sudo iwpriv wlan0 mp_set_cw_mode 0\r\n",
        .mfg_cmd_name =     "M",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "mfgmode ",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_set_pwr[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, as "<power>", which is in 12dbm ~ 23dbm
        .sc_var_parser =    parse_set_power,
        .sc_helper =        "iwpriv <interface> mp_set_power <power>\r\n \
                            power: tx power value, which is in range {0-23}\r\n \
                            example: sudo iwpriv wlan0 mp_set_power 22\r\n",
        .mfg_cmd_name =     "p",        //Or "P"
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "tx power",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_set_tx_duty[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       4,          //1 uint32, 1*4 bytes, <duty value>, [0-100]
        .sc_var_parser =    parse_set_duty,
        .sc_helper =        "iwpriv <interface> mp_set_tx_duty <duty>\r\n \
                            duty: tx duty value to one of [0,10,20,30,40,50,60,70,80,90,100]\r\n \
                            example: sudo iwpriv wlan0 mp_set_tx_duty 50\r\n",
        .mfg_cmd_name =     "d",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "duty:",
    },
};
        
struct iwpriv_sub_cmd iwp_mp_sc_set_pwr_offset_en[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       4,           //1 uint32, 1*4 bytes, 
        .sc_var_parser =    parse_pwr_offset_en,
        .sc_helper =        "iwpriv <interface> mp_en_pwr_oft <0/1/2/3/4>\r\n \
                            0: clear\r\n \
                            1: reload WiFi HP, WiFi LP, BZ, 3 power offset.\r\n \
                            2: reload WiFi HP, BZ, 2 power offset.\r\n \
                            3: reload WiFi LP, BZ, 2 power offset.\r\n \
                            4: reload BZ, 1 power offset.\r\n \
                            example: sudo iwpriv wlan0 mp_en_pwr_oft 1\r\n",
        .mfg_cmd_name =     "V",         //include \0
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_set_xtal_cap[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       4,           //1 uint32, 1*4 bytes, "<N>",  > 0
        .sc_var_parser =    parse_set_xtal_cap,
        .sc_helper =        "iwpriv <interface> mp_set_xtal_cap <cap code>\r\n \
                            cap code: 0-255\r\n \
                            cap code:-1 for applying capcode which is loaded from efuse\r\n \
                            cap code:-2 for applying capcode which is loaded from efuse buffer\r\n \
                            example: sudo iwpriv wlan0 mp_set_xtal_cap 0\r\n",
        .mfg_cmd_name =     "X",         //Or "x"
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};

#if 0
struct iwpriv_sub_cmd iwp_mp_sc_set_misc_param[] = 
{
    {
        .sc_name.iname =    NONE,
        .sc_len =           0,
        .sc_var_len =       12,           //at least, 3 uint32, 3*4 bytes
        .sc_var_parser =    parse_set_misc_param,
        .sc_helper =        "iwpriv <interface> mp_set_misc_param <tcal_debug> <tcal_val_fix> <tcal_val_fix_value>\r\n \
                            example: sudo iwpriv wlan0 mp_set_misc_param 1 0 0\r\n",
        .mfg_cmd_name =     "i",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};
#endif

struct iwpriv_sub_cmd iwp_mp_sc_wr_mem[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       IS_VAR_LEN|8,   //At least 2 uint32
        .sc_var_parser =    parse_wr_mem,
        .sc_helper =        "iwpriv <interface> mp_wr_mem <addr in hex> <value in hex>\r\n \
                            addr should be hex value like 0x21018000, can be reg addr or mem addr, align 4 bytes\r\n \
                            sudo iwpriv wlan0 mp_wr_mem 0x21018000 0x12345678;sudo iwpriv wlan0 mp_ind",
        .mfg_cmd_name =     "o",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_rd_mem[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       IS_VAR_LEN|4,   //At least 4 bytes, addr
        .sc_var_parser =    parse_rd_mem,
        .sc_helper =        "iwpriv <interface> mp_rd_mem <addr in hex> [number of uint32]\r\n \
                            addr should be hex value like 0x21018000, can be reg addr or mem addr, align 4 bytes, The addr must be in valid range of MCU's memory and registers\r\n \
                            if read memory, 2nd param is number of uint32 to read, or omit 2nd param then use default value 1\r\n \
                            sudo iwpriv wlan0 mp_rd_mem 0x21018000 3;sudo iwpriv wlan0 mp_ind\r\n \
                            sudo iwpriv wlan0 mp_rd_mem 0x21018000;sudo iwpriv wlan0 mp_ind\r\n",
        .mfg_cmd_name =     "k",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};
                            
struct iwpriv_sub_cmd iwp_mp_sc_btble_tx[] = 
{
    {
        //ETE 03 25 00 01 0f 置tx_channel=03 test_data_len=25  payload_type=00 tx_rate=01 power=0f
        .sc_name.iname =    1,
        .sc_len =           4,
        .sc_var_len =       20,          //5 uint32, 5*4 bytes
        .sc_var_parser =    parse_ble_tx,
        .sc_helper =        "iwpriv <interface> mp_btble_tx 1 <channel> <rate> <power> <data len> <payload type>\r\n\
                            start BLE tx\r\n\
                            channel: [0-39]\r\n\
                            rate:    [1-4], 1Mbps:1, 2Mbps:2, 125kbps:3, 500kbps:4\r\n\
                            power:   [0-20]. Use power from calibration cfg in ram or BLE pwr in ram TLV if -1\r\n\
                            data len: [1-255] \r\n\
                            payload type: [0-7], PRBS9:0, 11110000:1, 10101010:2, PRBS15:3, 11111111:4, 00000000:5, 00001111:6, 01010101:7\r\n\
                            example: sudo iwpriv wlan0 mp_btble_tx 1 1 2 13 32 0\r\n",
        .mfg_cmd_name =     "ETE",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "mfg ble tx start",
    },
    {
        .sc_name.iname =    0,
        .sc_len =           4,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_btble_tx 0\r\n\
                            stop BLE tx\r\n\
                            example: sudo iwpriv wlan0 mp_btble_tx 0\r\n",
        .mfg_cmd_name =     "EE",
        .mfg_cmd_len =      2,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "mfg ble trx stop",
    },
    {
        //DEV CUBE:EBTB0300010a tx_channel=03 payload_type=00 pkt_type=01 power=0a
        .sc_name.iname =    3,
        .sc_len =           4,
        .sc_var_len =       16,          //4 uint32, 4*4 bytes
        .sc_var_parser =    parse_bt_tx,
        .sc_helper =        "iwpriv <interface> mp_btble_tx 3 <channel> <pkt type> <power> <payload type>\r\n\
                            start BT tx\r\n\
                            channel: [0-78]\r\n\
                            pkt type: DH1:1, DH3:3, DH5:5, 2DH1:6, 3DH1:7, 2DH3:8, 3DH3:9, 2DH5:10, 3DH5:11\r\n\
                            power:   [0-10]. Use power from calibration cfg in ram or BT pwr in ram TLV if -1\r\n\
                            payload type: [0-7], PRBS9:0, 11110000:1, 10101010:2, PRBS15:3, 11111111:4, 00000000:5, 00001111:6, 01010101:7\r\n\
                            example: sudo iwpriv wlan0 mp_btble_tx 3 1 3 10 0\r\n",
        .mfg_cmd_name =     "EBTB",
        .mfg_cmd_len =      4,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "mfg bt tx start",
    },
    {
        .sc_name.iname =    4,
        .sc_len =           4,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_btble_tx 4\r\n\
                            stop BT tx\r\n\
                            example: sudo iwpriv wlan0 mp_btble_tx 4\r\n",
        .mfg_cmd_name =     "EBE",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "mfg bt trx stop",
    },
};

//mg
struct iwpriv_sub_cmd iwp_mp_sc_hello[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_hello\r\n\
                            first msg to check fw running up\r\n\
                            example: sudo iwpriv wlan0 mp_hello\r\n",
        .mfg_cmd_name =     "H\n",       //include \0
        .mfg_cmd_len =      3,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "mfg",
    },
};

#if 0
struct iwpriv_sub_cmd iwp_mp_sc_uc_rx[] = 
{
    {
        .sc_name.cname =    "1",         //rx_on
        .sc_len =           1,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_uc_rx <on/off>\r\n \
                            on: 1, off: 0\r\n \
                            example: sudo iwpriv wlan0 mp_uc_rx 1\r\n \
                                     sudo iwpriv wlan0 mp_uc_rx 0\r\n",
        .mfg_cmd_name =     "ur1\n",     //Or "UR1\n"
        .mfg_cmd_len =      5,           //include \0
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
    {
        .sc_name.cname =    "0",         //rx_off
        .sc_len =           1,
        .sc_var_len =       0,
        .mfg_cmd_name =     "ur0\n",     //Or "UR0\n"
        .mfg_cmd_len =      5,           //include \0
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};
#endif

#if 0
struct iwpriv_sub_cmd iwp_mp_sc_dump_phyrf[] = 
{
    {
        .sc_name.cname =    "dump_rfc",
        .sc_len =           8,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_dump_phyrf dump_rfc\r\n \
                            sudo iwpriv wlan0 mp_dump_phyrf dump_rfc;sudo iwpriv wlan0 mp_ind\r\n",
        .mfg_cmd_name =     "vdump_rfc",
        .mfg_cmd_len =      9,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          NULL,
    },
    {
        .sc_name.cname =    "dump_rf",
        .sc_len =           7,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_dump_phyrf dump_rf\r\n \
                            sudo iwpriv wlan0 mp_dump_phyrf dump_rf;sudo iwpriv wlan0 mp_ind\r\n",
        .mfg_cmd_name =     "vdump_rf",
        .mfg_cmd_len =      8,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          NULL,
    },
    {
        .sc_name.cname =    "dump_txvec",
        .sc_len =           10,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_dump_phyrf dump_txvec\r\n \
                            sudo iwpriv wlan0 mp_dump_phyrf dump_txvec;sudo iwpriv wlan0 mp_ind\r\n",
        .mfg_cmd_name =     "vdump_txvec",
        .mfg_cmd_len =      11,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          NULL,
    },
    {
        .sc_name.cname =    "dump_txgain",
        .sc_len =           11,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_dump_phyrf dump_txgain\r\n \
                            sudo iwpriv wlan0 mp_dump_phyrf dump_txgain;sudo iwpriv wlan0 mp_ind\r\n",
        .mfg_cmd_name =     "vdump_txgain",
        .mfg_cmd_len =      12,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          NULL,
    },
    {
        .sc_name.cname =    "tpc_dump_wlan",
        .sc_len =           13,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_dump_phyrf tpc_dump_wlan\r\n \
                            sudo iwpriv wlan0 mp_dump_phyrf tpc_dump_wlan;sudo iwpriv wlan0 mp_ind\r\n",
        .mfg_cmd_name =     "vtpc_dump_wlan",
        .mfg_cmd_len =      14,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          NULL,
    },
    {
        .sc_name.cname =    "tpc_dump_bz",
        .sc_len =           11,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_dump_phyrf tpc_dump_bz\r\n \
                            sudo iwpriv wlan0 mp_dump_phyrf tpc_dump_bz;sudo iwpriv wlan0 mp_ind\r\n",
        .mfg_cmd_name =     "vtpc_dump_bz",
        .mfg_cmd_len =      12,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          NULL,
    },
};
#endif

struct iwpriv_sub_cmd iwp_mp_sc_rx[] = 
{
    {
        .sc_name.cname =    "2",         //rx on bw 20M
        .sc_len =           1,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_rx <on_off>\r\n \
                            on_off: 2 for 20M rx, 4 for 40M rx(BL616L not support 40M), \r\n \
                                    1 for getting rx results, 0 for stopping rx. \r\n \
                            example: sudo iwpriv wlan0 mp_rx 2\r\n \
                                     sudo iwpriv wlan0 mp_rx 1\r\n",
        .mfg_cmd_name =     "r:s2\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "mfg rx ctrl done",
    },
    {
        .sc_name.cname =    "4",         //rx on bw 40M
        .sc_len =           1,
        .sc_var_len =       0,
        .sc_helper =        NULL,
        .mfg_cmd_name =     "r:s4\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "mfg rx ctrl done",
    },
    {
        .sc_name.cname =    "0",         //rx off 
        .sc_len =           1,
        .sc_var_len =       0,
        .mfg_cmd_name =     "r:p\n",
        .mfg_cmd_len =      5,           //include \0
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "mfg rx ctrl done",
    },
    {
        .sc_name.cname =    "1",         //get status of received pkts
        .sc_len =           1,
        .sc_var_len =       0,
        .mfg_cmd_name =     "r:g\n",
        .mfg_cmd_len =      5,           //include \0
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "mfg rx ctrl done",
    },
};

#if 0
struct iwpriv_sub_cmd iwp_mp_sc_pm[] = 
{
    {
        .sc_name.cname =    "hibernate",
        .sc_len =           9,
        .sc_var_len =       IS_VAR_LEN|1,//At least 1 bytes, as "<number of seconds to hibernate and then wake up by rtc>"
        .sc_var_parser =    parse_str_2_one_uint,
        .mfg_cmd_name =     "hr",
        .mfg_cmd_len =      2,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
    {
        .sc_name.cname =    "sleep_forever",
        .sc_len =           13,
        .sc_var_len =       0,
        .mfg_cmd_name =     "sa\n",
        .mfg_cmd_len =      4,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
    {
        .sc_name.cname =    "sleep_level",
        .sc_len =           11,
        .sc_var_len =       IS_VAR_LEN|1,   //At least 1 bytes, as "<PDS(power down sleep) level>"
        .sc_var_parser =    parse_str_2_one_uint,
        .mfg_cmd_name =     "sl",
        .mfg_cmd_len =      2,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
    {
        .sc_name.cname =    "sleep_dtim",
        .sc_len =           10,
        .sc_var_len =       IS_VAR_LEN|3,   //At least 3 bytes, as "<dtim count>,<exit_count>"
        .sc_var_parser =    parse_sleep_dtim,
        .mfg_cmd_name =     "s:",
        .mfg_cmd_len =      2,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
    {
        .sc_name.cname =    "awakekeep_dtim",
        .sc_len =           14,
        .sc_var_len =       IS_VAR_LEN|1,   //At least 1 bytes, as "<keep rx time>", the keep awake and rx time in ms
        .sc_var_parser =    parse_str_2_one_uint,
        .mfg_cmd_name =     "a:w",          //Or "A:w"
        .mfg_cmd_len =      3,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};
        
struct iwpriv_sub_cmd iwp_mp_sc_reset[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .mfg_cmd_name =     "Reset\n",
        .mfg_cmd_len =      7,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};
#endif

struct iwpriv_sub_cmd iwp_mp_sc_get_fw_ver[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_fw_ver\r\n \
                            get firmware version\r\n \
                            example: sudo iwpriv wlan0 mp_get_fw_ver\r\n",
        .mfg_cmd_name =     "y:v\n",      //Or "Y:v\n"
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_get_build_info[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_buidinfo\r\n \
                            get firmware build info\r\n \
                            example: sudo iwpriv wlan0 mp_get_buidinfo\r\n",
        .mfg_cmd_name =     "y:d\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_get_pwr[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_power\r\n \
                            get tx power\r\n \
                            example: sudo iwpriv wlan0 mp_get_power\r\n",
        .mfg_cmd_name =     "y:p\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_get_freq[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_channel\r\n \
                            get frequency\r\n \
                            example: sudo iwpriv wlan0 mp_get_channel\r\n",
        .mfg_cmd_name =     "y:c\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    }, 
};
        
struct iwpriv_sub_cmd iwp_mp_sc_get_tx_state[] = 
{   
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_tx_state\r\n \
                            get tx onoff status\r\n \
                            example: sudo iwpriv wlan0 mp_get_tx_state\r\n",
        .mfg_cmd_name =     "y:t\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};
#if 0
struct iwpriv_sub_cmd iwp_mp_sc_get_pkt_freq[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_pkt_freq\r\n \
                            get number of pkts sent every second\r\n \
                            example: sudo iwpriv wlan0 mp_get_pkt_freq\r\n",
        .mfg_cmd_name =     "y:f\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};
#endif
struct iwpriv_sub_cmd iwp_mp_sc_get_xtal_cap[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_xtal_cap\r\n \
                            get xtal cap code\r\n \
                            example: sudo iwpriv wlan0 mp_get_xtal_cap\r\n",
        .mfg_cmd_name =     "y:x\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_get_cwmode[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_cwmode\r\n \
                            get CW mode value\r\n \
                            example: sudo iwpriv wlan0 mp_get_cwmode\r\n",
        .mfg_cmd_name =     "y:M\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_get_tx_duty[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_tx_duty\r\n \
                            get tx_duty\r\n \
                            example: sudo iwpriv wlan0 mp_get_tx_duty\r\n",
        .mfg_cmd_name =     "y:i\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      SHORT_WAIT_MS,
        .ind_exp =          "[mfg fw]",
    }, 
};

#if 0
struct iwpriv_sub_cmd iwp_mp_sc_efuse_rd[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       IS_VAR_LEN|1,   //At least 1 byte
        .sc_helper =        "iwpriv <interface> mp_ef_rd <addr in hex>\r\n \
                            addr should be hex value\r\n \
                            read back the value at efuse's specific addr\r\n \
                            example: sudo iwpriv wlan0 mp_ef_rd 0x00000004\r\n",
        .mfg_cmd_name =     "REA",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "Rd ef",        //"[mfg fw] REA",
    },
};
        
struct iwpriv_sub_cmd iwp_mp_sc_efuse_wr[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       IS_VAR_LEN|3,   //At least 3 bytes
        .sc_var_parser =    parse_ef_wr,
        .sc_helper =        "iwpriv <interface> mp_ef_wr <addr in hex>=<value in hex>\r\n \
                            addr and value should be hex value\r\n \
                            want to write value to efuse's specific addr, but only store in specific buffer first\r\n \
                            example: sudo iwpriv wlan0 mp_ef_wr 0x004=0x80000008\r\n",
        .mfg_cmd_name =     "WEA",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "Wr to ef to",
    },
};
#endif

struct iwpriv_sub_cmd iwp_mp_sc_efuse_cap_rd[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_ef_cap_rd\r\n \
                            read back the xtal cap code value at xtal cap code specific addr in efuse\r\n \
                            example: sudo iwpriv wlan0 mp_ef_cap_rd\r\n",
        .mfg_cmd_name =     "REX\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "capcode golden read suss",   //"[mfg fw] REX OK",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_efuse_cap_wr[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       IS_VAR_LEN|1,   //At least 1 bytes, xtal cap code
        .sc_var_parser =    parse_ef_cap_wr,
        .sc_helper =        "iwpriv <interface> mp_ef_cap_wr <xtal cap code>\r\n \
                            value should be decimal value\r\n \
                            want to write xtal cap code to xtal cap code specific addr in efuse, but only store in specific buffer first\r\n \
                            example: sudo iwpriv wlan0 mp_ef_cap_wr 3\r\n",
        .mfg_cmd_name =     "WEX",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "[mfg fw] WEX", //"Cap code2 cmd", "Save cap code2 OK",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_efuse_pwr_oft_rd[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_ef_pwr_oft_rd\r\n \
                            read back the each channel power offset value at power offset specific addr in efuse\r\n \
                            example: sudo iwpriv wlan0 mp_ef_pwr_oft_rd\r\n",
        .mfg_cmd_name =     "REPH\n",
        .mfg_cmd_len =      6,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "hp Power offset:", //"[mfg fw] REP OK",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_efuse_pwr_oft_wr[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       IS_VAR_LEN|14,   //At least 14 bytes, 14 power offset values for 1-14 channels
        .sc_var_parser =    parse_ef_pwr_oft_wr,
        .sc_helper =        "iwpriv <interface> mp_ef_pwr_oft_wr <ch1 power offset>,<ch2 power offset>....<ch14 power offset>\r\n \
                            each channel's power offset value should be decimal value\r\n \
                            want to write each channel's power offset to power offset specific addr in efuse, but only store in specific buffer first\r\n \
                            example: sudo iwpriv wlan0 mp_ef_pwr_oft_wr 1,2,3,3,3,2,1,0,1,2,3,4,1,3\r\n",
        .mfg_cmd_name =     "WEPH",
        .mfg_cmd_len =      4,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "[mfg fw] WEP", //"Pwr offset cmd:",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_efuse_mac_addr_rd[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_ef_mac_rd\r\n \
                            read back the mac addr in efuse\r\n \
                            example: sudo iwpriv wlan0 mp_ef_mac_rd\r\n",
        .mfg_cmd_name =     "REM\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "[mfg fw] REM OK",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_efuse_mac_addr_wr[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       IS_VAR_LEN|12,   //At least 12 bytes, mac addr
        .sc_var_parser =    parse_ef_mac_wr,
        .sc_helper =        "iwpriv <interface> mp_ef_mac_wr <mac addr>\r\n \
                            value should be hex value\r\n \
                            want to write mac addr specific addr in efuse, but only store in specific buffer first\r\n \
                            example: sudo iwpriv wlan0 mp_ef_mac_wr 11:22:33:44:55:66\r\n",
        .mfg_cmd_name =     "WEM",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "[mfg fw] WEM",  //"MAC address cmd:",//"Write slot:",   
    },
};

/*
struct iwpriv_sub_cmd iwp_mp_sc_get_temp[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_get_temp\r\n",
        .mfg_cmd_name =     "e\n",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      3000, //LONG_WAIT_MS,
        .ind_exp =          "average temperature", //"[mfg fw]",
    },
};
*/

struct iwpriv_sub_cmd iwp_mp_sc_load_cal_data[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_load_caldata <file path>\r\n\
                            example: sudo iwpriv wlan0 mp_load_caldata /home/rf_para.conf\r\n",
        .mfg_cmd_name =     "D",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      LONG_WAIT_MS*3,
        .ind_exp =          "mp_load_caldata success", //"[mfg fw]",
    },
};

//RTOS
//Used in rtos, in which os/platform does not support filesystem or cfg file rw problem.
#if 0
struct iwpriv_sub_cmd iwp_mp_sc_cfg_cal_data[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_cfg_cal <multi para>\r\n\
                            refer to mp doc\r\n",
        .mfg_cmd_name =     "D",
        .mfg_cmd_len =      1,
        .ind_wait_ms =      LONG_WAIT_MS*3,
        .ind_exp =          "mp_load_caldata success", //"[mfg fw]",
    },
};
#endif

struct iwpriv_sub_cmd iwp_nmp_sc_wmm_cfg[] = 
{
    {
        .sc_name.iname =    0,
        .sc_len =           4,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> wmmcfg <value>\r\n \
                            value should be decimal value, 0 or 1\r\n",
        .sc_var_parser =    NULL,
    },
    {
        .sc_name.iname =    1,
        .sc_len =           4,
        .sc_var_len =       0,
        .sc_helper =        NULL,
        .sc_var_parser =    NULL,
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_efuse_bz_pwr_oft_rd[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       0,
        .sc_helper =        "iwpriv <interface> mp_ef_bz_pof_rd\r\n \
                            read back bz's each channel power offset value at power offset specific addr in efuse\r\n \
                            example: sudo iwpriv wlan0 mp_ef_bz_pof_rd\r\n",
        .mfg_cmd_name =     "REE\n",
        .mfg_cmd_len =      5,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "bz Power offset:", //"[mfg fw] REP OK",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_efuse_bz_pwr_oft_wr[] = 
{
    {
        .sc_name.cname =    NULL,
        .sc_len =           0,
        .sc_var_len =       IS_VAR_LEN|5,   //At least 20 bytes, 5 power offset values for 1-20 channels
        .sc_var_parser =    parse_ef_bz_pwr_oft_wr,
        .sc_helper =        "iwpriv <interface> mp_ef_bz_pof_wr <2402Mhz power offset>,<2426Mhz power offset>,<2442Mhz power offset>,<2458Mhz power offset>,<2474Mhz power offset>\r\n \
                            each channel's power offset value should be decimal value\r\n \
                            want to write each channel's power offset to power offset specific addr in efuse, but only store in specific buffer first\r\n \
                            example: sudo iwpriv wlan0 mp_ef_bz_pof_wr 1,3,0,-4,3\r\n",
        .mfg_cmd_name =     "WEE",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "[mfg fw] WEE", //"Pwr offset cmd:",
    },
};

struct iwpriv_sub_cmd iwp_mp_sc_btble_rx[] = 
{
    {
        /*DEV CUBE:ERE0302 rx_channel=03 rx_rate=02*/
        .sc_name.iname =    1,
        .sc_len =           4,
        .sc_var_len =       8,          //2 uint32, 2*4 bytes
        .sc_var_parser =    parse_ble_rx,
        .sc_helper =        "iwpriv <interface> mp_btble_rx 1 <channel> <rate>\r\n\
                            start BLE rx\r\n\
                            channel: [0-39]\r\n\
                            rate:    [1-4], 1Mbps:1, 2Mbps:2, coded PHY:3\r\n\
                            example: sudo iwpriv wlan0 mp_btble_rx 1 1 1\r\n",
        .mfg_cmd_name =     "ERE",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "mfg ble rx start",
    },
    {
        .sc_name.iname =    0,
        .sc_len =           4,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_btble_rx 0\r\n\
                            stop BLE rx\r\n\
                            example: sudo iwpriv wlan0 mp_btble_rx 0\r\n",
        .mfg_cmd_name =     "EE",
        .mfg_cmd_len =      2,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "mfg ble trx stop",
    },
    {
        /*DEV CUBE:EBRB0303 rx_channel=03 pkt_type=03*/
        .sc_name.iname =    3,
        .sc_len =           4,
        .sc_var_len =       8,          //2 uint32, 2*4 bytes
        .sc_var_parser =    parse_bt_rx,
        .sc_helper =        "iwpriv <interface> mp_btble_rx 3 <channel> <pkt type>\r\n\
                            start BT rx\r\n\
                            channel: [0-78]\r\n\
                            pkt type: DH1:1, DH3:3, DH5:5, 2DH1:6, 3DH1:7, 2DH3:8, 3DH3:9, 2DH5:10, 3DH5:11\r\n\
                            example: sudo iwpriv wlan0 mp_btble_rx 3 3 1\r\n",
        .mfg_cmd_name =     "EBRB",
        .mfg_cmd_len =      4,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "mfg bt rx start",
    },
    {
        .sc_name.iname =    4,
        .sc_len =           4,
        .sc_var_len =       0,
        .sc_var_parser =    NULL,
        .sc_helper =        "iwpriv <interface> mp_btble_rx 4\r\n\
                            stop BT rx\r\n\
                            example: sudo iwpriv wlan0 mp_btble_rx 4\r\n",
        .mfg_cmd_name =     "EBE",
        .mfg_cmd_len =      3,
        .ind_wait_ms =      LONG_WAIT_MS,
        .ind_exp =          "mfg bt trx stop",
    },
};

//iwp cmd group
//for normal fw
struct iwpriv_cmd iwp_nmp_cmds[] = {
    [BL_IOCTL_WMMCFG - SIOCIWFIRSTPRIV] =   {
        .name =         "wmmcfg",
        .sub_cmd =      iwp_nmp_sc_wmm_cfg,
        .sub_cmd_num =  sizeof(iwp_nmp_sc_wmm_cfg)/sizeof(struct iwpriv_sub_cmd),
    },
};

//iwp cmd group
//mp cmd for mfg fw
struct iwpriv_cmd iwp_mp_single[] = {
    [BL_IOCTL_VERSION - SIOCIWFIRSTPRIV] =    {
        .name =         "version",
        .sub_cmd =      iwp_mp_sc_ver,
        .sub_cmd_num =  sizeof(iwp_mp_sc_ver)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_CFG - SIOCIWFIRSTPRIV] = {
        .name =         "mp_load_caldata",
        .sub_cmd =      iwp_mp_sc_load_cal_data,
        .sub_cmd_num =  sizeof(iwp_mp_sc_load_cal_data)/sizeof(struct iwpriv_sub_cmd),
    },
    //RTOS
    //Used in rtos, in which os/platform does not support filesystem or cfg file rw problem.
    #if 0
    [BL_IOCTL_MP_CAL_CFG - SIOCIWFIRSTPRIV] = {
        .name =         "mp_cfg_cal",
        .sub_cmd =      iwp_mp_sc_cfg_cal_data,
        .sub_cmd_num =  sizeof(iwp_mp_sc_cfg_cal_data)/sizeof(struct iwpriv_sub_cmd),
    },
    #endif
    [BL_IOCTL_MP_HELP - SIOCIWFIRSTPRIV] = {
        .name =         "help",
        .sub_cmd =      iwp_mp_sc_help,
        .sub_cmd_num =  sizeof(iwp_mp_sc_help)/sizeof(struct iwpriv_sub_cmd),
    },
};

struct iwpriv_cmd iwp_mp_mg[] = {
    [BL_IOCTL_MP_HELLO] =   {
        .name =         "mp_hello",
        .sub_cmd =      iwp_mp_sc_hello,
        .sub_cmd_num =  sizeof(iwp_mp_sc_hello)/sizeof(struct iwpriv_sub_cmd),
    },
    #if 0
    [BL_IOCTL_MP_UNICAST_RX] = {
        .name =         "mp_uc_rx",
        .sub_cmd =      iwp_mp_sc_uc_rx,
        .sub_cmd_num =  sizeof(iwp_mp_sc_uc_rx)/sizeof(struct iwpriv_sub_cmd),
    },
    #endif
    [BL_IOCTL_MP_RX] = {
        .name =         "mp_rx",
        .sub_cmd =      iwp_mp_sc_rx,
        .sub_cmd_num =  sizeof(iwp_mp_sc_rx)/sizeof(struct iwpriv_sub_cmd),
    },
    #if 0
    [BL_IOCTL_MP_PM] = {
        .name =         "mp_pm",
        .sub_cmd =      iwp_mp_sc_pm,
        .sub_cmd_num =  sizeof(iwp_mp_sc_pm)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_RESET] = {
        .name =         "mp_reset",
        .sub_cmd =      iwp_mp_sc_reset,
        .sub_cmd_num =  sizeof(iwp_mp_sc_reset)/sizeof(struct iwpriv_sub_cmd),
    },
    #endif
    [BL_IOCTL_MP_GET_FW_VER] = {
        .name =         "mp_get_fw_ver",
        .sub_cmd =      iwp_mp_sc_get_fw_ver,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_fw_ver)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_GET_BUILD_INFO] = {
        .name =         "mp_get_buildinf",
        .sub_cmd =      iwp_mp_sc_get_build_info,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_build_info)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_GET_POWER] = {
        .name =         "mp_get_power",
        .sub_cmd =      iwp_mp_sc_get_pwr,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_pwr)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_GET_FREQ] = {
        .name =         "mp_get_channel",
        .sub_cmd =      iwp_mp_sc_get_freq,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_freq)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_GET_TX_STATUS] = {
        .name =         "mp_get_tx_state",
        .sub_cmd =      iwp_mp_sc_get_tx_state,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_tx_state)/sizeof(struct iwpriv_sub_cmd),
    },
    #if 0
    [BL_IOCTL_MP_GET_PKT_FREQ] = {
        .name =         "mp_get_pkt_freq",
        .sub_cmd =      iwp_mp_sc_get_pkt_freq,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_pkt_freq)/sizeof(struct iwpriv_sub_cmd),
    },
    #endif
    [BL_IOCTL_MP_GET_XTAL_CAP] = {
        .name =         "mp_get_xtal_cap",
        .sub_cmd =      iwp_mp_sc_get_xtal_cap,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_xtal_cap)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_GET_CWMODE] = {
        .name =         "mp_get_cw_mode",
        .sub_cmd =      iwp_mp_sc_get_cwmode,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_cwmode)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_GET_TX_DUTY] = {
        .name =         "mp_get_tx_duty",
        .sub_cmd =      iwp_mp_sc_get_tx_duty,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_tx_duty)/sizeof(struct iwpriv_sub_cmd),
    },
    #if 0
    [BL_IOCTL_MP_EFUSE_RD] = {
        .name =         "mp_ef_rd",
        .sub_cmd =      iwp_mp_sc_efuse_rd,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_rd)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_EFUSE_WR] = {
        .name =         "mp_ef_wr",
        .sub_cmd =      iwp_mp_sc_efuse_wr,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_wr)/sizeof(struct iwpriv_sub_cmd),
    },
    #endif
    [BL_IOCTL_MP_EFUSE_CAP_RD] = {
        .name =         "mp_ef_cap_rd",
        .sub_cmd =      iwp_mp_sc_efuse_cap_rd,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_cap_rd)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_EFUSE_CAP_WR] = {
        .name =         "mp_ef_cap_wr",
        .sub_cmd =      iwp_mp_sc_efuse_cap_wr,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_cap_wr)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_EFUSE_PWR_OFT_RD] = {
        .name =         "mp_ef_pwroft_rd",
        .sub_cmd =      iwp_mp_sc_efuse_pwr_oft_rd,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_pwr_oft_rd)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_EFUSE_PWR_OFT_WR] = {
        .name =         "mp_ef_pwroft_wr",
        .sub_cmd =      iwp_mp_sc_efuse_pwr_oft_wr,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_pwr_oft_wr)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_EFUSE_MAC_ADR_RD] = {
        .name =         "mp_ef_mac_rd",
        .sub_cmd =      iwp_mp_sc_efuse_mac_addr_rd,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_mac_addr_rd)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_EFUSE_MAC_ADR_WR] = {
        .name =         "mp_ef_mac_wr",
        .sub_cmd =      iwp_mp_sc_efuse_mac_addr_wr,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_mac_addr_wr)/sizeof(struct iwpriv_sub_cmd),
    },
    #if 0
    [BL_IOCTL_MP_GET_TEMPERATURE] = {
        .name =         "mp_get_temp",
        .sub_cmd =      iwp_mp_sc_get_temp,
        .sub_cmd_num =  sizeof(iwp_mp_sc_get_temp)/sizeof(struct iwpriv_sub_cmd),
    },
    #endif
    #if 0
    [BL_IOCTL_MP_DUMP_PHYRF] = {
        .name =         "mp_dump_phyrf",
        .sub_cmd =      iwp_mp_sc_dump_phyrf,
        .sub_cmd_num =  sizeof(iwp_mp_sc_dump_phyrf)/sizeof(struct iwpriv_sub_cmd),
    },
    #endif
    [BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_RD] = {
        .name =         "mp_ef_bz_pof_rd",
        .sub_cmd =      iwp_mp_sc_efuse_bz_pwr_oft_rd,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_bz_pwr_oft_rd)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_WR] = {
        .name =         "mp_ef_bz_pof_wr",
        .sub_cmd =      iwp_mp_sc_efuse_bz_pwr_oft_wr,
        .sub_cmd_num =  sizeof(iwp_mp_sc_efuse_bz_pwr_oft_wr)/sizeof(struct iwpriv_sub_cmd),
    },
};

struct iwpriv_cmd iwp_mp_ms[] = {
    #if 0
    [BL_IOCTL_MP_UNICAST_TX] = {
        .name =         "mp_uc_tx",
        .sub_cmd =      iwp_mp_sc_uc_tx,
        .sub_cmd_num =  sizeof(iwp_mp_sc_uc_tx)/sizeof(struct iwpriv_sub_cmd),
    }, 
    #endif
    [BL_IOCTL_MP_11b_RATE] = {
        .name =         "mp_11b_rate",
        .sub_cmd =      iwp_mp_sc_11b_rate,
        .sub_cmd_num =  sizeof(iwp_mp_sc_11b_rate)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_11g_RATE] = {
        .name =         "mp_11g_rate",
        .sub_cmd =      iwp_mp_sc_11g_rate,
        .sub_cmd_num =  sizeof(iwp_mp_sc_11g_rate)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_11n_RATE] = {
        .name =         "mp_11n_rate",
        .sub_cmd =      iwp_mp_sc_11n_rate,
        .sub_cmd_num =  sizeof(iwp_mp_sc_11n_rate)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_11ax_RATE] = {
        .name =         "mp_11ax_rate",
        .sub_cmd =      iwp_mp_sc_11ax_rate,
        .sub_cmd_num =  sizeof(iwp_mp_sc_11ax_rate)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_TX] = {
        .name =         "mp_tx",
        .sub_cmd =      iwp_mp_sc_tx,
        .sub_cmd_num =  sizeof(iwp_mp_sc_tx)/sizeof(struct iwpriv_sub_cmd),
    },
    #if 0
    [BL_IOCTL_MP_SET_PKT_FREQ] = {
        .name =         "mp_set_pkt_freq",
        .sub_cmd =      iwp_mp_sc_set_pkt_freq,
        .sub_cmd_num =  sizeof(iwp_mp_sc_set_pkt_freq)/sizeof(struct iwpriv_sub_cmd),
    },
    #endif
    [BL_IOCTL_MP_SET_PKT_LEN] = {
        .name =         "mp_set_pkt_len",
        .sub_cmd =      iwp_mp_sc_set_pkt_len,
        .sub_cmd_num =  sizeof(iwp_mp_sc_set_pkt_len)/sizeof(struct iwpriv_sub_cmd),
    },
    //Get more msg on console when the followings are in iwp_mp_mg group. only 'GET' iwpriv cmd id can forward msg to user console.
    //If not want the msg on console, we can put the followings to iwp_mp_ms
    [BL_IOCTL_MP_SET_CHANNEL] = {
        .name =         "mp_set_channel",
        .sub_cmd =      iwp_mp_sc_set_ch,
        .sub_cmd_num =  sizeof(iwp_mp_sc_set_ch)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_SET_CWMODE] = {
        .name =         "mp_set_cw_mode",
        .sub_cmd =      iwp_mp_sc_set_cw,
        .sub_cmd_num =  sizeof(iwp_mp_sc_set_cw)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_SET_PWR] = {
        .name =         "mp_set_power",
        .sub_cmd =      iwp_mp_sc_set_pwr,
        .sub_cmd_num =  sizeof(iwp_mp_sc_set_pwr)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_SET_TXDUTY] = {
        .name =         "mp_set_tx_duty",
        .sub_cmd =      iwp_mp_sc_set_tx_duty,
        .sub_cmd_num =  sizeof(iwp_mp_sc_set_tx_duty)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_SET_PWR_OFT_EN] = {
        .name =         "mp_en_pwr_oft",
        .sub_cmd =      iwp_mp_sc_set_pwr_offset_en,
        .sub_cmd_num =  sizeof(iwp_mp_sc_set_pwr_offset_en)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_SET_XTAL_CAP] = {
        .name =         "mp_set_xtal_cap",
        .sub_cmd =      iwp_mp_sc_set_xtal_cap,
        .sub_cmd_num =  sizeof(iwp_mp_sc_set_xtal_cap)/sizeof(struct iwpriv_sub_cmd),
    },
    #if 0
    [BL_IOCTL_MP_SET_PRIV_PARAM] = {
        .name =         "mp_set_dbg_para",
        .sub_cmd =      iwp_mp_sc_set_misc_param,
        .sub_cmd_num =  sizeof(iwp_mp_sc_set_misc_param)/sizeof(struct iwpriv_sub_cmd),
    },
    #endif
    [BL_IOCTL_MP_WR_MEM] = {
        .name =         "mp_wr_mem",
        .sub_cmd =      iwp_mp_sc_wr_mem,
        .sub_cmd_num =  sizeof(iwp_mp_sc_wr_mem)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_RD_MEM] = {
        .name =         "mp_rd_mem",
        .sub_cmd =      iwp_mp_sc_rd_mem,
        .sub_cmd_num =  sizeof(iwp_mp_sc_rd_mem)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_BTBLE_TX] = {
        .name =         "mp_btble_tx",
        .sub_cmd =      iwp_mp_sc_btble_tx,
        .sub_cmd_num =  sizeof(iwp_mp_sc_btble_tx)/sizeof(struct iwpriv_sub_cmd),
    },
    [BL_IOCTL_MP_BTBLE_RX] = {
        .name =         "mp_btble_rx",
        .sub_cmd =      iwp_mp_sc_btble_rx,
        .sub_cmd_num =  sizeof(iwp_mp_sc_btble_rx)/sizeof(struct iwpriv_sub_cmd),
    },    
};

static int parse_help_args(uint8_t *in_buf, uint32_t in_len,
                                uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t m = 0;
    struct iwpriv_cmd *iwp_cmd = NULL;
    char *not_found_str = "Not found\r\n";
    char *more_details_str = "\nPlease use 'iwpriv <interface> help cmd_name' to get more details about specific cmd\r\n";
    int *iwp_cnt = NULL;
    struct iwpriv_cmd **iwp_cs = NULL;
    int total_cnt = 0;

    //struct iwpriv_cmd *iwp_nmp_cs[] = {
    //    iwp_nmp_cmds,
    //};

    //int iwp_nmp_cnt[] = {
    //    sizeof(iwp_nmp_cmds)/sizeof(struct iwpriv_cmd),
    //};
    
    struct iwpriv_cmd *iwp_mp_cs[] = {
        iwp_mp_single,
        iwp_mp_ms,
        iwp_mp_mg
    };

    int iwp_mp_cnt[] = {
        sizeof(iwp_mp_single)/sizeof(struct iwpriv_cmd),
        sizeof(iwp_mp_ms)/sizeof(struct iwpriv_cmd),
        sizeof(iwp_mp_mg)/sizeof(struct iwpriv_cmd)
    };

    if (!bl_mod_params.mp_mode) {
        //iwp_cs = iwp_nmp_cs;
        //iwp_cnt = iwp_nmp_cnt;
        //total_cnt = 1;
        total_cnt = 0;
    } else {
        iwp_cs = iwp_mp_cs;
        iwp_cnt = iwp_mp_cnt;
        total_cnt = 3;
    }

    *var_len = 0;

    for (m=0; m<total_cnt; m++) {
        for (i=0; i<iwp_cnt[m]; i++) {
            iwp_cmd = iwp_cs[m] + i;
            if (iwp_cmd->name == NULL) {
                // printk("iwp cmds null i:%d\n", i);
                continue;
            } else {
                // printk("iwp cmds i:%d\n", i);
            }

            if (strlen(in_buf) > 0) {
                if (strlen(in_buf) >= strlen(iwp_cmd->name) &&
                    strncmp(in_buf, iwp_cmd->name, strlen(iwp_cmd->name)) == 0) 
                {
                // if (strncmp(in_buf, iwp_cmd->name, MIN(strlen(in_buf), strlen(iwp_cmd->name))) == 0) {
                    for (j=0; j<iwp_cmd->sub_cmd_num; j++) {
                        if (iwp_cmd->sub_cmd[j].sc_helper != NULL) {
                            memcpy(out_buf + *var_len, iwp_cmd->sub_cmd[j].sc_helper, 
                                   strlen(iwp_cmd->sub_cmd[j].sc_helper));
                            *var_len += strlen(iwp_cmd->sub_cmd[j].sc_helper);
                            memcpy(out_buf + *var_len, "\r\n", 2);
                            *var_len += 2;
                        }
                    }
                    break;
                }
            } else {
                memcpy(out_buf + *var_len, iwp_cmd->name, strlen(iwp_cmd->name));
                *var_len += strlen(iwp_cmd->name);
                memcpy(out_buf + *var_len, "\r\n", 2);
                *var_len += 2;

                // for (j=0; j<iwp_cmd->sub_cmd_num; j++) {
                //     if (iwp_cmd->sub_cmd[j].sc_helper != NULL) {
                //         memcpy(out_buf + *var_len, iwp_cmd->sub_cmd[j].sc_helper, strlen(iwp_cmd->sub_cmd[j].sc_helper));
                //         *var_len += strlen(iwp_cmd->sub_cmd[j].sc_helper);
                //         memcpy(out_buf + *var_len, "\r\n", 2);
                //         *var_len += 2;
                //     }
                // }
            }
        }
    }

    if (strlen(in_buf) == 0) {
        memcpy(out_buf + *var_len, more_details_str, strlen(more_details_str));
        *var_len += strlen(more_details_str);
        *var_len += 2;
        // printk("%d, %s\n", *var_len, more_details_str);
    }
    
    if (*var_len == 0) {
        memcpy(out_buf + *var_len, not_found_str, strlen(not_found_str));
        *var_len += strlen(not_found_str);
        // printk("%d, %s\n", *var_len, not_found_str);
    }

    // printk("var_len: %d\n", *var_len);

    return 0;
}

#if 0
static int parse_str_2_one_bool(uint8_t *in_buf, uint32_t in_len, 
                                       uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t one_bool = 0;

    if (sscanf(in_buf, "%d", &one_bool) < 1)
        return -EINVAL;

    one_bool = one_bool>0?1:0;

    *var_len = sprintf(out_buf, "%d", one_bool);
    return 0;
}

static int parse_str_2_one_uint(uint8_t *in_buf, uint32_t in_len, 
                                       uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t one_uint = 0;

    if (sscanf(in_buf, "%d", &one_uint) < 1)
        return -EINVAL;

    *var_len = sprintf(out_buf, "%d", one_uint);
    return 0;
}
#endif

static int parse_one_bool(uint8_t *in_buf, uint32_t in_len, 
                                uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t one_uint = *(uint32_t *)in_buf;

    one_uint = one_uint>0?1:0;

    *var_len = sprintf(out_buf, "%d", one_uint);
    return 0;
}

static int parse_tx_param(uint8_t *in_buf, uint32_t in_len,
                                uint8_t *out_buf, uint32_t *var_len)
{
    uint32_t one_uint = *(uint32_t *)in_buf;
    uint32_t ru_size;
    uint32_t ru_index;
    uint32_t mcs;
    uint32_t bw;
    
    if (one_uint!=0 && one_uint!=1 && one_uint!=2)
    {
        printk("Invalid param value, 0, 1, 2 are supported tx mode\n");
        return -EINVAL;
    }
    
    *var_len = 0;
    
    if (one_uint == 2) 
    {
        if (in_len < 20) {
            printk("Invalid param is not long enough, %d >= 20 expected\n", in_len);
            return -EINVAL;
        }
        
        ru_size = *(uint32_t *)(in_buf+4);
        ru_index = *(uint32_t *)(in_buf+8);
        mcs = *(uint32_t *)(in_buf+12);
        bw = *(uint32_t *)(in_buf+16);

        if (mcs > 9) {
            printk("Invalid mcs %d <= 9 expected\n", mcs);
            return -EINVAL;
        }

        if (bw == 0) {
            if (ru_size == 0) {
                if (ru_index > 8) {
                    printk("Invalid ru_index %d, should be 0-8 when RU 26 and bw 20M\n",
                           ru_index);
                    return -EINVAL;
                }
            } else if (ru_size == 1) {
                if (ru_index > 3) {
                    printk("Invalid ru_index %d, should be 0-3 when RU 52 and bw 20M\n", ru_index);
                    return -EINVAL;
                }
            } else if (ru_size == 2) {
                if (ru_index > 1) {
                    printk("Invalid ru_index %d, should be 0-1 when RU 106 and bw 20M\n", ru_index);
                    return -EINVAL;
                }
            } else if (ru_size == 3) {
                if (ru_index > 0) {
                    printk("Invalid ru_index %d, should be 0 when RU 242 and bw 20M\n", ru_index);
                    return -EINVAL;
                }
            } else {
                printk("Invalid ru_size %d, should be 0(RU 26),1(RU 52),2(RU 106),3(RU 242) for 20M\n", ru_size);
                return -EINVAL;
            }
        } else if (bw == 1) {
            if (ru_size == 0) {
                if (ru_index > 17) {
                    printk("Invalid ru_index %d, should be 0-17 when RU 26 and bw 40M\n", ru_index);
                    return -EINVAL;
                }
            } else if (ru_size == 1) {
                if (ru_index > 7) {
                    printk("Invalid ru_index %d, should be 0-7 when RU 52 and bw 40M\n", ru_index);
                    return -EINVAL;
                }
            } else if (ru_size == 2) {
                if (ru_index > 3) {
                    printk("Invalid ru_index %d, should be 0-3 when RU 106 and bw 40M\n", ru_index);
                    return -EINVAL;
                }
            } else if (ru_size == 3) {
                if (ru_index > 1) {
                    printk("Invalid ru_index %d, should be 0-1 when RU 242 and bw 40M\n", ru_index);
                    return -EINVAL;
                }
            } else if (ru_size == 4) {
                if (ru_index > 0) {
                    printk("Invalid ru_index %d, should be 0 when RU 484 and bw 40M\n", ru_index);
                    return -EINVAL;
                }
            } else {
                printk("Invalid ru_size %d, should be 0(RU 26),1(RU 52),2(RU 106),3(RU 242),4(RU 484) for 40M\n", ru_size);
                return -EINVAL;
            }
        } else {
            printk("Invalid bw %d, should be 0 for 20M or 1 for 40M(BL616L not support 40M)\n", bw);
            return -EINVAL;
        }
        
        *var_len += sprintf(out_buf+*var_len, "b");
        *var_len += sprintf(out_buf+*var_len, "%d", ru_size);
        *var_len += sprintf(out_buf+*var_len, "%02d", ru_index);
        *var_len += sprintf(out_buf+*var_len, "%d", mcs);
        *var_len += sprintf(out_buf+*var_len, "%d", bw);
    }
    else 
    {
        *var_len += sprintf(out_buf, "%d", one_uint);
    }
    
    return 0;
}

static int parse_pwr_offset_en(uint8_t *in_buf, uint32_t in_len,
                                        uint8_t *out_buf, uint32_t *var_len)
{
    uint32_t one_uint = *(uint32_t *)in_buf;

    if (one_uint != 0 && one_uint != 1 && one_uint != 3 &&
        one_uint != 4 && one_uint != 2) 
    {
        printk("Invalid param value, 0, 1, 2, 3, 4 are support\n");
        return -EINVAL;
    }
    
    *var_len = sprintf(out_buf, "%d", one_uint);

    return 0;
}

#if 0
static int parse_one_uint(uint8_t *in_buf, uint32_t in_len, 
                               uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t one_uint = *(uint32_t *)in_buf;

    *var_len = sprintf(out_buf, "%d", one_uint);

    return 0;
}
#endif

static int parse_pkt_len(uint8_t *in_buf, uint32_t in_len, 
                              uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t one_uint = *(uint32_t *)in_buf;

    if (one_uint == 0) {
        printk("Invalid param value, pkt_len should be > 0\r\n");
        return -EINVAL;
    }
    
    *var_len = sprintf(out_buf, "%d", one_uint);

    return 0;
}

#if 0
static int parse_unicast_tx_param(uint8_t *in_buf, uint32_t in_len,
                                          uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t pkt_number = 0;

    pkt_number = *(uint32_t *)in_buf;
    if (pkt_number < 1) {
        printk("Invalid param value, pkt_number should > 0\r\n");
        return -EINVAL;
    }
    *var_len = sprintf(out_buf, "%d%d", 1, pkt_number);

    return 0;
}
#endif

static int parse_11b_rate(uint8_t *in_buf, uint32_t in_len, 
                               uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t rate = *(uint32_t *)in_buf;

    if (rate > 3) {
        printk("Invalid rate %d, 2.4G 11b support rate idx = 0 - 3, 0:1Mbps, \
                1:2Mbps, 2:5.5Mbps, 3:11Mbps\n", rate);
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, "%d", rate);

    return 0;
}

static int parse_11g_rate(uint8_t *in_buf, uint32_t in_len, 
                               uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t rate = *(uint32_t *)in_buf;

    if (rate > 7) {
        printk("Invalid rate %d, 2.4G 11g rate idx = 0 - 7, 0:6Mbps 1:9Mbps \
                2:12Mbps 3:18Mbps 4:24Mbps 5:36Mbps 6:48Mbps 7:54Mbps\n", rate);
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, "%d", rate);

    return 0;
}

static int parse_11n_rate(uint8_t *in_buf, uint32_t in_len, 
                               uint8_t *out_buf, uint32_t *var_len) 
{
    //modulation type: 2 for HT-MF, 3 for HT-GF
    uint32_t modulation_type = 2;
    uint32_t guard_interval = *(uint32_t *)(in_buf + 0); 
    uint32_t mcs_index = *(uint32_t *)(in_buf + 4);
    uint32_t bandwidth = *(uint32_t *)(in_buf + 8);
    uint32_t ldpc = *(uint32_t *)(in_buf + 12);

    if (guard_interval != 0 && guard_interval != 1) {
        printk("Invalid gurad interval %d, valid is 0 for short guard interval, 1 for long guard interval\n", 
                guard_interval);
        return -EINVAL;
    }

    if (modulation_type != 2 && modulation_type != 3) {
        printk("Invalid modulation type %d, valid is 2 for HT-MF, 3 for HT-GF.\n",
               modulation_type);
        return -EINVAL;
    }

    if (bandwidth != 2 && bandwidth != 4) {
        printk("Invalid bandwidth %d, valid is 2 for 20MHz, 4 for 40MHz(BL616L not support 40M)\n",
               bandwidth);
        return -EINVAL;
    }

    if (mcs_index > 7) {
        printk("Invalid mcs_index %d, valid is in range 0-7.\n", mcs_index);
        return -EINVAL;
    }

    if (ldpc > 1) {
        printk("Invalid ldpc type %d, valid is 0-1.\n", ldpc);
        return -EINVAL;
    }

    out_buf[0] = 'm';
    out_buf[1] = (guard_interval==0) ? 's' : 'l';
    out_buf[2] = (modulation_type==3) ? 'g' : 'm';
    out_buf[3] = (bandwidth==2) ? '2' : '4';
    sprintf(out_buf+4, "%d", mcs_index);
    out_buf[5] = ldpc + 0x30;
    *var_len = 6;

    return 0;
}

static int parse_11ax_rate(uint8_t *in_buf, uint32_t in_len, 
                                uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t guard_interval = *(uint32_t *)(in_buf + 0); 
    uint32_t mcs_index = *(uint32_t *)(in_buf + 4);
    uint32_t bandwidth = *(uint32_t *)(in_buf + 8);
    uint32_t ldpc_sufec = *(uint32_t *)(in_buf + 12);

    if (guard_interval != 0 && guard_interval != 1 && guard_interval != 2) {
        printk("Invalid gurad interval %d, valid is 0-2, mapping to ['2x HELTF+0.8us GI', '2x HELTF+1.6us GI', '4x HELTF+3.2us GI']\n", 
                guard_interval);
        return -EINVAL;
    }

    if (ldpc_sufec > 1) {
        printk("Invalid ldpc %d, valid is in range 0-1.\n", ldpc_sufec);
        return -EINVAL;
    }
    
    if (bandwidth > 3) {
        printk("Invalid bandwidth %d, valid is 0 for 20MHz, 1 for 40MHz, 2 for 80MHz, 3 for 160MHz(BL616L only support 20M)\n",
               bandwidth);
        return -EINVAL;
    }

    if (mcs_index > 18) {
        printk("Invalid mcs_index %d, valid mcs_index is 0-18\n", mcs_index);
        return -EINVAL;
    }

    out_buf[0] = 'Q';
    out_buf[1] = ldpc_sufec + 0x30;
    out_buf[2] = guard_interval + 0x30;
    sprintf(out_buf+3, "%02d", mcs_index);
    out_buf[5] = bandwidth + 0x30;
    *var_len = 6;

    return 0;
}

#if 0
static int parse_set_pkt_freq(uint8_t *in_buf, uint32_t in_len, 
                                    uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t pkt_freq = *(uint32_t *)in_buf;

    if (pkt_freq > 1000) {
        printk("Invalid channel %d, valid pkt_freq is 1-1000\n", pkt_freq);
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, "%d", pkt_freq);
    
    return 0;
}
#endif

static int parse_set_channel(uint8_t *in_buf, uint32_t in_len,
                                   uint8_t *out_buf, uint32_t *var_len)
{
    uint32_t channel_idx = *(uint32_t *)in_buf;

    if (channel_idx > 14 || channel_idx == 0) {
        printk("Invalid channel %d, valid channel is 1-14, map to frequency 2412-2484.\n", channel_idx);
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, "%d", channel_idx);
    
    return 0;
}

static int parse_set_power(uint8_t *in_buf, uint32_t in_len,
                                 uint8_t *out_buf, uint32_t *var_len)
{
    int32_t power = *(int32_t *)in_buf;

    //if (power == -1) {
    //    BL_DBG("use fw's default power\n");
    //} else 
    if (power > 23) {
        printk("Valid power is <=23.\n");
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, "%d", power);
    
    return 0;
}

static int parse_set_xtal_cap(uint8_t *in_buf, uint32_t in_len, 
                                    uint8_t *out_buf, uint32_t *var_len) 
{
    int32_t one_int = *(int32_t *)in_buf;

    if (one_int > 255 || one_int < -2) {
        printk("Invalid xtal cap %d, valid is 0-255, or -1 or -2\n", one_int);
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, "%d", one_int);
    
    return 0;
}

#if 0
static int parse_set_misc_param(uint8_t *in_buf, uint32_t in_len,
                                         uint8_t *out_buf, uint32_t *var_len)
{
    uint32_t tcal_debug = *(uint32_t *)in_buf;
    uint32_t tcal_val_fix = *(uint32_t *)(in_buf+4);
    uint32_t tcal_val_fix_value = 0;

    tcal_val_fix = (tcal_val_fix>0)?1:0;
    if (tcal_val_fix > 0) {
        tcal_val_fix_value = *(int32_t *)(in_buf+8);
    }
    
    *var_len = sprintf(out_buf, "t%d,%d,%d", 
                       tcal_debug, tcal_val_fix, tcal_val_fix_value);
                       
    return 0;
}
#endif

static int parse_wr_mem(uint8_t *in_buf, uint32_t in_len, 
                               uint8_t *out_buf, uint32_t *var_len) 
{
    //<addr in hex, 4 bytes align> <value in hex, 32bit>
    uint32_t addr = *(uint32_t *)in_buf;
    uint32_t value = *(uint32_t *)(in_buf+4);
    
    if ((addr & 0x03) > 0) {
        printk("not 4 bytes align, 0x%x\n", addr);
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, " 0x%08x 0x%08x", addr, value);
    
    return 0;
}

static int parse_rd_mem(uint8_t *in_buf, uint32_t in_len, 
                               uint8_t *out_buf, uint32_t *var_len) 
{
    //<addr in hex, 4 bytes align>
    //<addr in hex, 4 bytes align> <number of uint32_t to read, 400 at max>
    uint32_t addr = *(uint32_t *)in_buf;
    uint32_t bit_len = 1;

    if (in_len >= 8) {
        bit_len = *(uint32_t *)(in_buf+4);
        if (bit_len == 0) {
            printk("invalid number of uint32_t to read, should be > 0\n");
            return -EINVAL;
        }
    }
    
    if ((addr & 0x03) > 0) {
        printk("not 4 bytes align, 0x%x\n", addr);
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, " 0x%08x %d", addr, bit_len);
    
    return 0;
}

static int parse_ble_tx(uint8_t *in_buf, uint32_t in_len, 
                            uint8_t *out_buf, uint32_t *var_len)
{
    uint32_t channel = *(uint32_t *)(in_buf + 0); 
    uint32_t rate = *(uint32_t *)(in_buf + 4);
    int32_t power = *(int32_t *)(in_buf + 8);
    uint32_t data_len = *(uint32_t *)(in_buf + 12);
    uint32_t pld_type = *(uint32_t *)(in_buf + 16);

    if (channel > 39) {
        printk("Invalid channel %d, valid range is [0-39]\n", 
                channel);
        return -EINVAL;
    }

    if (rate == 0 || rate > 4) {
        printk("Invalid rate %d, valid is [1-4], 1Mbps:1, 2Mbps:2, 125kbps:3, 500kbps:4.\n", rate);
        return -EINVAL;
    }

    if (power < -1 || power > 20) {
        printk("Invalid power %d, valid power is [0-20]. Use power from calibration cfg in ram or efuse if -1\n", power);
        return -EINVAL;
    }

    if (data_len == 0 || data_len > 255) {
        printk("Invalid dataLen %d, valid is in range 1-255.\n", data_len);
        return -EINVAL;
    }

    if (pld_type > 7) {
        printk("Invalid pldType %d, valid is 0-7.\n", pld_type);
        return -EINVAL;
    }

    sprintf(out_buf, "%02x", channel);
    sprintf(out_buf+2, "%02x", data_len);
    sprintf(out_buf+4, "%02x", pld_type);
    sprintf(out_buf+6, "%02x", rate);
    
    if (power == -1) {
        out_buf[8] = '-';
        out_buf[9] = '1';
    } else {
        sprintf(out_buf+8, "%02x", power);
    }
    
    *var_len = 10;

    return 0;
}

static int parse_ble_rx(uint8_t *in_buf, uint32_t in_len, 
                            uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t channel = *(uint32_t *)(in_buf + 0); 
    uint32_t rate = *(uint32_t *)(in_buf + 4);

    if (channel > 39) {
        printk("Invalid channel %d, valid range is [0-39]\n", 
                channel);
        return -EINVAL;
    }

    if (rate == 0 || rate > 3) {
        printk("Invalid rate %d, valid is [1-4], 1Mbps:1, 2Mbps:2, coded PHY:3.\n", rate);
        return -EINVAL;
    }

    sprintf(out_buf, "%02x", channel);
    sprintf(out_buf+2, "%02x", rate);
    *var_len = 4;

    return 0;
}

static int parse_bt_tx(uint8_t *in_buf, uint32_t in_len, 
                          uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t channel = *(uint32_t *)(in_buf + 0); 
    uint32_t pkt_type = *(uint32_t *)(in_buf + 4);
    int32_t power = *(int32_t *)(in_buf + 8);
    uint32_t pld_type = *(uint32_t *)(in_buf + 12);

    if (channel > 78) {
        printk("Invalid channel %d, valid range is [0-78]\n", 
                channel);
        return -EINVAL;
    }

    if (pkt_type != 1 && pkt_type != 3 && pkt_type != 5 && 
        pkt_type != 6 && pkt_type != 7 && 
        pkt_type != 8 && pkt_type != 9 && pkt_type != 10 && pkt_type != 11) 
    {
        printk("Invalid pkt_type %d, pkt type: DH1:1, DH3:3, DH5:5, 2DH1:6, 3DH1:7, 2DH3:8, 3DH3:9, 2DH5:10, 3DH5:11\n", pkt_type);
        return -EINVAL;
    }

    if (power < -1 || power > 10) {
        printk("Invalid power %d, valid power is [0-10]. Use power from calibration cfg in ram or efuse if -1\n", power);
        return -EINVAL;
    }

    if (pld_type > 7) {
        printk("Invalid pldType %d, valid is 0-7.\n", pld_type);
        return -EINVAL;
    }

    sprintf(out_buf, "%02x", channel);
    sprintf(out_buf+2, "%02x", pld_type);
    sprintf(out_buf+4, "%02x", pkt_type);
    
    if (power == -1) {
        out_buf[6] = '-';
        out_buf[7] = '1';
    } else {
        sprintf(out_buf+6, "%02x", power);
    }
    *var_len = 8;

    return 0;
}

static int parse_bt_rx(uint8_t *in_buf, uint32_t in_len,
                          uint8_t *out_buf, uint32_t *var_len)
{
    uint32_t channel = *(uint32_t *)(in_buf + 0); 
    uint32_t pkt_type = *(uint32_t *)(in_buf + 4);

    if (channel > 78) {
        printk("Invalid channel %d, valid range is [0-78]\n", 
                channel);
        return -EINVAL;
    }

    if (pkt_type != 1 && pkt_type != 3 && pkt_type != 5 &&
        pkt_type != 6 && pkt_type != 7 && 
        pkt_type != 8 && pkt_type != 9 && pkt_type != 10 && pkt_type != 11) 
    {
        printk("Invalid pkt_type %d, pkt type: DH1:1, DH3:3, DH5:5, 2DH1:6, 3DH1:7, 2DH3:8, 3DH3:9, 2DH5:10, 3DH5:11\n", pkt_type);
        return -EINVAL;
    }


    sprintf(out_buf, "%02x", channel);
    sprintf(out_buf+2, "%02x", pkt_type);
    *var_len = 4;

    return 0;
}

#if 0
static int parse_sleep_dtim(uint8_t *in_buf, uint32_t in_len,
                                  uint8_t *out_buf, uint32_t *var_len) {
    uint32_t dtim_count = 0;
    uint32_t dtim_exit_count = 0;
    
    if (sscanf(in_buf, "%d,%d", &dtim_count, &dtim_exit_count) < 2)
        return -EINVAL;

    if (dtim_count > 9 || dtim_exit_count > 9) {
        printk("sleep_dtim only support dtim_count in [0-9] and dtim_exit_count in [0-9].\n");
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, "%d%d", dtim_count, dtim_exit_count);
    return 0;
}
#endif

static int parse_set_duty(uint8_t *in_buf, uint32_t in_len, 
                               uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t duty = *(uint32_t *)in_buf;

    if (duty > 100 || duty%10 != 0) {
        printk("valid duty is one of [0,10,20,30,40,50,60,70,80,90,100]\n");
        return -EINVAL;
    }

    *var_len = sprintf(out_buf, "%d", duty);
    
    return 0;
}

#if 0
static int parse_ef_wr(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf,
                       uint32_t *var_len) 
{
    uint32_t mfg_wr_addr = 0;
    uint32_t mfg_wr_value = 0;

    if(sscanf(in_buf, "%x=%x", &mfg_wr_addr, &mfg_wr_value) < 2) {
        printk("Need (right) efuse write addr in hex and value in hex, addr=value.\n");
        return -EINVAL;
    }

    memcpy(out_buf, in_buf, strlen(in_buf));
    *var_len = strlen(in_buf);
    return 0;
}
#endif

static int parse_ef_cap_wr(uint8_t *in_buf, uint32_t in_len,
                                 uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t mfg_wr_cap_code = 0;

    if(sscanf(in_buf, "%d", &mfg_wr_cap_code) < 1) {
        printk("Need (right) efuse write cap code in decimal.\n");
        return -EINVAL;
    }

    memcpy(out_buf, in_buf, strlen(in_buf));
    *var_len = strlen(in_buf);
    
    return 0;
}

static int parse_ef_pwr_oft_wr(uint8_t *in_buf, uint32_t in_len,
                                      uint8_t *out_buf, uint32_t *var_len)
{
    int32_t mfg_wr_pwr_oft[14];

    if (sscanf(in_buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
               mfg_wr_pwr_oft, mfg_wr_pwr_oft+1, mfg_wr_pwr_oft+2, 
               mfg_wr_pwr_oft+3, mfg_wr_pwr_oft+4, mfg_wr_pwr_oft+5, 
               mfg_wr_pwr_oft+6, mfg_wr_pwr_oft+7, mfg_wr_pwr_oft+8, 
               mfg_wr_pwr_oft+9, mfg_wr_pwr_oft+10, mfg_wr_pwr_oft+11, 
               mfg_wr_pwr_oft+12, mfg_wr_pwr_oft+13) != 14) 
    {
        printk("Need (right) efuse write 14 power offset values in decimal \n\
                like 1,2,3,3,3,2,1,0,1,2,3,4,1,3.\n");
        return -EINVAL;
    }

    memcpy(out_buf, in_buf, strlen(in_buf));
    *var_len = strlen(in_buf);
    
    return 0;
}

static int parse_ef_bz_pwr_oft_wr(uint8_t *in_buf, uint32_t in_len, 
                                          uint8_t *out_buf, uint32_t *var_len) 
{
    int32_t mfg_wr_bz_pwr_oft[5];

    if (sscanf(in_buf, "%d,%d,%d,%d,%d",
               mfg_wr_bz_pwr_oft, mfg_wr_bz_pwr_oft+1, mfg_wr_bz_pwr_oft+2, 
               mfg_wr_bz_pwr_oft+3, mfg_wr_bz_pwr_oft+4) != 5) 
    {
        printk("Need (right) efuse write 20 bz power offset values in decimal like 1,2,5,-1,-2\n");
        return -EINVAL;
    }

    memcpy(out_buf, in_buf, strlen(in_buf));
    *var_len = strlen(in_buf);
    
    return 0;
}

static int parse_ef_mac_wr(uint8_t *in_buf, uint32_t in_len, 
                                  uint8_t *out_buf, uint32_t *var_len) 
{
    uint32_t mac_addr[6];

    if (sscanf(in_buf, "%x:%x:%x:%x:%x:%x", 
               (uint32_t *)mac_addr, (uint32_t *)(mac_addr+1),
               (uint32_t *)(mac_addr+2), 
               (uint32_t *)(mac_addr+3), (uint32_t *)(mac_addr+4),
               (uint32_t *)(mac_addr+5)) != 6) 
    {
        printk("Need (right) efuse write mac addr in hex like 1A:2B:3C:4D:5F:60.\n");
        return -EINVAL;
    }

    memcpy(out_buf, in_buf, strlen(in_buf));
    *var_len = strlen(in_buf);
    
    return 0;
}

static struct iwpriv_sub_cmd *mp_get_sub_cmd(struct iwpriv_sub_cmd *iwp_scs,
         uint32_t sub_cmd_num, uint16_t var_type, char *in_buf, uint32_t in_len) 
{
    int i = 0;
    struct iwpriv_sub_cmd *iwp_sc = NULL;

    for (i=0; i < sub_cmd_num; i++) {
        if (var_type == IW_PRIV_TYPE_CHAR) {
            if (in_len >= iwp_scs[i].sc_len &&
                (iwp_scs[i].sc_len == 0 ||
                strncmp(in_buf, iwp_scs[i].sc_name.cname, iwp_scs[i].sc_len) == 0)) 
            {
                iwp_sc = iwp_scs + i;
                break;
            }
        } else if (var_type == IW_PRIV_TYPE_INT) {
            if (in_len >= iwp_scs[i].sc_len &&
                (iwp_scs[i].sc_len == 0 ||
                *(int32_t *)in_buf == iwp_scs[i].sc_name.iname))
            {
                iwp_sc = iwp_scs + i;
                break;
            }
        }
    }

    return iwp_sc;
}

static uint8_t * mp_find_exp(uint8_t *out_buf, uint16_t out_len, char *ind_exp) {
    uint8_t *cursur_1st = out_buf;
    uint8_t *cursur_2nd = NULL;
    uint8_t *found = NULL;
    
    out_buf[out_len] = '\0';

    ////debug
    //dump_buf(out_buf, out_len);

    while (cursur_1st < out_buf+out_len && *cursur_1st != '\0') {
        uint8_t tmp = 0;

        cursur_2nd = cursur_1st + 1;
        while (cursur_2nd < out_buf+out_len &&
               *cursur_2nd != '\0' && *cursur_2nd != 0x0d && *cursur_2nd != 0x0a) 
        {
            cursur_2nd++;
        }

        tmp = *cursur_2nd;
        *cursur_2nd = '\0';
        
        if (cursur_2nd-cursur_1st >= strlen(ind_exp) && 
            (found=strstr(cursur_1st, ind_exp)) != NULL) 
        {
            BL_DBG_MSG("%s, found expected indication:%s\n", __func__, ind_exp);
            
            *cursur_2nd = tmp;
            return found;
        }
        
        *cursur_2nd = tmp;
        
        cursur_1st = cursur_2nd;
    }

    return NULL;
}

static uint16_t mp_merge_inds(struct bl_hw *bl_hw, uint8_t *out_buf) {
    uint16_t out_len = 0;
    uint16_t ind_len = 0;
    uint8_t *purge_ptr = NULL;

    BL_DBG_MSG("merge, bl_hw->iwp_var.iwpriv_ind_len:%u\n",  bl_hw->iwp_var.iwpriv_ind_len);
    
    purge_ptr = bl_hw->iwp_var.iwpriv_ind;
    ind_len = le16_to_cpu(*(__le16 *)purge_ptr);
    while (ind_len > 0 && purge_ptr < bl_hw->iwp_var.iwpriv_ind + bl_hw->iwp_var.iwpriv_ind_len && 
           ind_len < bl_hw->iwp_var.iwpriv_ind_len - (purge_ptr-bl_hw->iwp_var.iwpriv_ind) && 
           out_len + ind_len < IWPRIV_OUT_BUF_LEN-1)
    {
        BL_DBG_MSG("merge, ind_len:%u\n", ind_len);
        
        //bl_dump(purge_ptr + 2, ind_len);
    
        memcpy(out_buf + out_len, purge_ptr + 2, ind_len - 1);
        out_len = out_len + ind_len - 1;
        purge_ptr = purge_ptr + ind_len + 2;
        ind_len = le16_to_cpu(*(__le16 *)purge_ptr);
    }

    return out_len;
}

static int mp_efuse_write(struct bl_hw *bl_hw, uint16_t sc_index, char *in_buf, 
                                union iwreq_data *wrqu, char *extra)
{
    uint8_t *out_buf = NULL;
    uint16_t out_len = 0;
    int ret = 0;
    uint8_t mp_cmd[100] = {0};
    bool exp_found = true;

    char *mfg_load = "LEA";
    char *mfg_program = "SEA\n";
    char *mfg_load_cap = "LEX\n";
    char *mfg_program_cap = "SEX\n";
    char *mfg_load_pwr_oft = "LEP\n";
    char *mfg_program_pwr_oft = "SEP\n";
    char *mfg_load_mac = "LEM\n";
    char *mfg_program_mac = "SEM\n";
    char *mfg_load_bz_pwr_oft = "LEE\n";
    char *mfg_program_bz_pwr_oft = "SEE\n";
    uint32_t mfg_wr_addr = 0;
    uint32_t mfg_wr_value = 0;
    uint32_t mfg_rd_addr = 0;
    uint32_t mfg_rd_value = 0;
    uint32_t mfg_wr_cap_code = 0;
    uint32_t mfg_rd_cap_code = 0;
    int32_t mfg_wr_pwr_oft[14] = {0};
    // uint8_t *mfg_rd_pwr_offset = NULL;
    int32_t mfg_wr_mac_addr[10] = {0};
    //uint8_t *mfg_rd_mac_addr = NULL;
    int32_t mfg_wr_bz_pwr_oft[5] = {0};

    do {
        out_buf = kzalloc(IWPRIV_OUT_BUF_LEN, GFP_KERNEL);
        if (out_buf == NULL) {
            printk("%s, fail to alloc out_buf\n", __func__);
            ret = -ENOMEM;
            break;
        }
        
        memset(out_buf, 0, IWPRIV_OUT_BUF_LEN);

        //2nd command, load back to check
        memset(mp_cmd, 0, sizeof(mp_cmd));
        memset(bl_hw->iwp_var.iwpriv_ind, 0, IWPRIV_IND_LEN_MAX);
        bl_hw->iwp_var.iwpriv_ind_len = 0;
        exp_found = false;

        if (sc_index == BL_IOCTL_MP_EFUSE_WR) {
            sscanf(in_buf, "%x=%x", &mfg_wr_addr, &mfg_wr_value);
            sprintf(mp_cmd, "%s0x%x\n", mfg_load, mfg_wr_addr);
        } else if (sc_index == BL_IOCTL_MP_EFUSE_CAP_WR) {
            sscanf(in_buf, "%d", &mfg_wr_cap_code);
            sprintf(mp_cmd, "%s", mfg_load_cap);
        } else if (sc_index == BL_IOCTL_MP_EFUSE_PWR_OFT_WR) {
            sscanf(in_buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
                       mfg_wr_pwr_oft, mfg_wr_pwr_oft+1, mfg_wr_pwr_oft+2, 
                       mfg_wr_pwr_oft+3, mfg_wr_pwr_oft+4, mfg_wr_pwr_oft+5, 
                       mfg_wr_pwr_oft+6, mfg_wr_pwr_oft+7, mfg_wr_pwr_oft+8, 
                       mfg_wr_pwr_oft+9, mfg_wr_pwr_oft+10, mfg_wr_pwr_oft+11, 
                       mfg_wr_pwr_oft+12, mfg_wr_pwr_oft+13);
            sprintf(mp_cmd, "%s", mfg_load_pwr_oft);
        } else if (sc_index == BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_WR) {
            sscanf(in_buf, "%d,%d,%d,%d,%d", 
                       mfg_wr_bz_pwr_oft, mfg_wr_bz_pwr_oft+1, mfg_wr_bz_pwr_oft+2, 
                       mfg_wr_bz_pwr_oft+3, mfg_wr_bz_pwr_oft+4);
            sprintf(mp_cmd, "%s", mfg_load_bz_pwr_oft);
        } else if (sc_index == BL_IOCTL_MP_EFUSE_MAC_ADR_WR) {
            sscanf(in_buf, "%x:%x:%x:%x:%x:%x", 
                       (uint32_t *)mfg_wr_mac_addr,
                       (uint32_t *)(mfg_wr_mac_addr+1),
                       (uint32_t *)(mfg_wr_mac_addr+2), 
                       (uint32_t *)(mfg_wr_mac_addr+3), 
                       (uint32_t *)(mfg_wr_mac_addr+4), 
                       (uint32_t *)(mfg_wr_mac_addr+5));
            sprintf(mp_cmd, "%s", mfg_load_mac);
        } else {
            printk("Unknow efuse sub cmd num:%d\n", sc_index);
            ret = -EFAULT;
            break;
        }
        
        printk("mp_efuse write, mp_cmd:%s\n", mp_cmd);
        // dump_buf(mp_cmd, strlen(mp_cmd));

        ret = bl_send_mp_test_msg(bl_hw, mp_cmd, strlen(mp_cmd), out_buf + out_len, false);
        if (ret) {
            printk("%s, bl_send_mp_test_msg:%s, error=%d\n", 
                   __func__, (char *)mp_cmd, ret);
            break;
        }

        msleep_interruptible(SHORT_WAIT_MS);

        out_len += mp_merge_inds(bl_hw, out_buf + out_len);

        ////debug
        //dump_buf_char(out_buf, out_len);
        printk("%s, verify, out_buf:%s\n", __func__, out_buf);
        
        if (sc_index == BL_IOCTL_MP_EFUSE_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len, "Rd ef ");
            
            if (found_ptr) {
                if (sscanf(found_ptr, "Rd ef %x=%x", &mfg_rd_addr, &mfg_rd_value) == 2 &&
                    mfg_rd_addr == mfg_wr_addr && mfg_rd_value == mfg_wr_value)
                {
                    printk("verify step, found rd ef, mfg_rd_addr, mfg_rd_value:0x%x=0x%x\n", 
                            mfg_rd_addr, mfg_rd_value);
                    exp_found = true;
                } else {
                    printk("verify step, found rd ef, but not same as wr addr and value, \n\
                            mfg_rd_addr, mfg_rd_value:0x%x=0x%x\n", 
                            mfg_rd_addr, mfg_rd_value);
                }
            } else {
                printk("verify step, not found rd ef\n");
            }
        } else if (sc_index == BL_IOCTL_MP_EFUSE_CAP_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len,
                                           "capcode golden read suss,capcode ");
            
            if (found_ptr) {
                if (sscanf(found_ptr, "capcode golden read suss,capcode %d", &mfg_rd_cap_code) == 1 &&
                    mfg_rd_cap_code == mfg_wr_cap_code)
                {
                    printk("verify step, found Cap code read and write value:%d, %d\n", 
                            mfg_rd_cap_code, mfg_wr_cap_code);
                    exp_found = true;
                } else {
                    printk("verify step, found Cap code, but not same as wr cap code, read and write value:%d, %d\n", 
                            mfg_rd_cap_code, mfg_wr_cap_code);
                }
            } else {
                printk("verify step, not found Cap code2:\n");
            }
        } else if (sc_index == BL_IOCTL_MP_EFUSE_PWR_OFT_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len, "hp Power offset:");
            
            if (found_ptr != NULL) 
            {
                int32_t pwr_oft[14];
                
                if (sscanf(found_ptr + strlen("hp Power offset:"), 
                           "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
                           pwr_oft, pwr_oft+1, pwr_oft+2, 
                           pwr_oft+3, pwr_oft+4, pwr_oft+5, 
                           pwr_oft+6, pwr_oft+7, pwr_oft+8, 
                           pwr_oft+9, pwr_oft+10, pwr_oft+11, 
                           pwr_oft+12, pwr_oft+13) == 14 &&
                           pwr_oft[0] == mfg_wr_pwr_oft[0] && 
                           pwr_oft[6] == mfg_wr_pwr_oft[6] &&
                           pwr_oft[12] == mfg_wr_pwr_oft[12])
                {
                    printk("verify step, found power offset read and write value\n");
                    exp_found = true;
                } else {
                    printk("verify step, found power offset, but not match\n");
                }
            } else {
                printk("verify step, not found power offset\n");
            }
        } else if (sc_index == BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len, "bz Power offset:");
            
            if (found_ptr != NULL) 
            {
                int32_t pwr_oft[20];
                
                //old phy, return 20 elements
                if (sscanf(found_ptr + strlen("bz Power offset:"), 
                           "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
                           pwr_oft, pwr_oft+1, pwr_oft+2, pwr_oft+3, 
                           pwr_oft+4, pwr_oft+5, pwr_oft+6, pwr_oft+7, 
                           pwr_oft+8, pwr_oft+9, pwr_oft+10, pwr_oft+11, 
                           pwr_oft+12, pwr_oft+13, pwr_oft+14, pwr_oft+15,
                           pwr_oft+16, pwr_oft+17, pwr_oft+18, pwr_oft+19) == 20 &&
                    pwr_oft[0] == mfg_wr_bz_pwr_oft[0] &&
                    pwr_oft[6] == mfg_wr_bz_pwr_oft[1] &&
                    pwr_oft[10] == mfg_wr_bz_pwr_oft[2] &&
                    pwr_oft[14] == mfg_wr_bz_pwr_oft[3] &&
                    pwr_oft[18] == mfg_wr_bz_pwr_oft[4])
                {
                    printk("verify step, found BZ power offset read and write value\n");
                    exp_found = true;
                }
                //dbg phy or release phy, return 5 elements
                else if (sscanf(found_ptr + strlen("bz Power offset:"),  "%d,%d,%d,%d,%d", 
                                 pwr_oft, pwr_oft+1, pwr_oft+2, pwr_oft+3, pwr_oft+4) == 5 &&
                         pwr_oft[0] == mfg_wr_bz_pwr_oft[0] &&
                         pwr_oft[1] == mfg_wr_bz_pwr_oft[1] &&
                         pwr_oft[2] == mfg_wr_bz_pwr_oft[2] && 
                         pwr_oft[3] == mfg_wr_bz_pwr_oft[3] &&
                         pwr_oft[4] == mfg_wr_bz_pwr_oft[4])
                {
                    printk("verify step, found BZ power offset read and write value\n");
                    exp_found = true;
                } 
                else 
                {
                    printk("verify step, found BZ power offset, but not match\n");
                }
            } else {
                printk("verify step, not found BZ power offset\n");
            }
        } else if (sc_index == BL_IOCTL_MP_EFUSE_MAC_ADR_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len, "MAC: ");
            int32_t ef_mac[6] = {0};
            
            if (found_ptr != NULL)
            {
                if (sscanf(found_ptr + strlen("MAC: "), "%x:%x:%x:%x:%x:%x",
                           ef_mac, ef_mac+1, ef_mac+2, ef_mac+3, 
                           ef_mac+4, ef_mac+5) == 6 &&
                    ef_mac[0] == mfg_wr_mac_addr[0] && 
                    ef_mac[1] == mfg_wr_mac_addr[1] &&
                    ef_mac[2] == mfg_wr_mac_addr[2] &&
                    ef_mac[3] == mfg_wr_mac_addr[3] &&
                    ef_mac[4] == mfg_wr_mac_addr[4] &&
                    ef_mac[5] == mfg_wr_mac_addr[5])
                {
                    printk("verify step, found mac addr and match\n");
                    exp_found = true;
                } else {
                    printk("verify step, found mac addr but not match\n");
                }
            } else {
                printk("verify step, not found mac addr\n");
            }
        }
        
        if (exp_found == false) {
            printk("Fail when load back the wrote value and check, out_buf:%s\n", out_buf);
            ret = -EFAULT;
            break;
        }

        //3rd command, program efuse
        memset(mp_cmd, 0, sizeof(mp_cmd));
        memset(bl_hw->iwp_var.iwpriv_ind, 0, IWPRIV_IND_LEN_MAX);
        bl_hw->iwp_var.iwpriv_ind_len = 0;
        exp_found = false;

        if (sc_index == BL_IOCTL_MP_EFUSE_WR) {
            sprintf(mp_cmd, "%s", mfg_program);
        } else if (sc_index == BL_IOCTL_MP_EFUSE_CAP_WR) {
            sprintf(mp_cmd, "%s", mfg_program_cap);
        } else if (sc_index == BL_IOCTL_MP_EFUSE_PWR_OFT_WR) {
            sprintf(mp_cmd, "%s", mfg_program_pwr_oft);
        } else if (sc_index == BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_WR) {
            sprintf(mp_cmd, "%s", mfg_program_bz_pwr_oft);
        } else if (sc_index == BL_IOCTL_MP_EFUSE_MAC_ADR_WR) {
            sprintf(mp_cmd, "%s", mfg_program_mac);
        }

        printk("mp_efuse program, mp_cmd:%s\n", mp_cmd);
        //dump_buf(mp_cmd, strlen(mp_cmd));

        ret = bl_send_mp_test_msg(bl_hw, mp_cmd, strlen(mp_cmd), out_buf + out_len, false);
        if (ret) {
            printk("bl_send_mp_test_msg:%s, error=%d\n", (char *)mp_cmd, ret);
            break;
        }

        msleep_interruptible(SHORT_WAIT_MS);

        out_len += mp_merge_inds(bl_hw, out_buf + out_len);

        ////debug
        //dump_buf_char(out_buf, *out_len);
        printk("%s, program, out_buf:%s\n", __func__, out_buf);
        
        if (sc_index == BL_IOCTL_MP_EFUSE_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len, "Save efuse OK");
            
            if (found_ptr) {
                printk("program step, found Save efuse OK\n");
                exp_found = true;
            } else {
                printk("program step, not found Save efuse OK\n");
            }
        } else if (sc_index == BL_IOCTL_MP_EFUSE_CAP_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len, "Save cap code OK");
            
            if (found_ptr) {
                printk("program step, found Save cap code2 OK\n");
                exp_found = true;
            } else {
                printk("program step, not found Save cap code2 OK\n");
            }
        } else if (sc_index == BL_IOCTL_MP_EFUSE_PWR_OFT_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len, "Save hp power offset OK");
            
            if (found_ptr) {
                printk("program step, found Save power offset OK\n");
                exp_found = true;
            } else {
                printk("program step, not found Save power offset OK\n");
            }
        } else if (sc_index == BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len, 
                                             "Save bz power offset ok");
            if (found_ptr) {
                printk("program step, found Save BZ power offset OK\n");
                exp_found = true;
            } else {
                printk("program step, not found Save BZ power offset OK\n");
            }
        } else if (sc_index == BL_IOCTL_MP_EFUSE_MAC_ADR_WR) {
            uint8_t *found_ptr = mp_find_exp(out_buf, out_len, "Save MAC address OK");
            
            if (found_ptr) {
                printk("program step, found Save MAC address OK\n");
                exp_found = true;
            } else {
                printk("program step, not found Save MAC address OK\n");
            }
        }

        if (exp_found == false) {
            printk("out_buf:%s\n", out_buf);
            ret = -EFAULT;
            break;
        }
    } while (0);

    if (ret == 0) {
        if(extra) {
            memcpy(extra + wrqu->data.length, out_buf, out_len);
            wrqu->data.length += out_len;
        } else {
            if (copy_to_user(wrqu->data.pointer + wrqu->data.length, out_buf, out_len)) {
                ret = -EFAULT;
            } else {
                wrqu->data.length += out_len;
            }
        }
    }

    if (out_buf != NULL)
        kfree(out_buf);

    return ret;
}

#if 0
static int mp_pre_rate_hdl(struct net_device *dev,
                                 struct iw_request_info *info, 
                                 union iwreq_data *wrqu, char *extra, 
                                 uint16_t sc_index, uint8_t *in_buf, uint16_t in_len) 
{
    int ret = 0;

    uint32_t rate_11b_pkt_lens[] = {101, 202, 556, 1111};
    //uint32_t rate_11b_pkt_lens[] = {1024, 1024, 1024, 1024};
    //uint32_t rate_11b_pkt_freqs[] = {500, 500, 500, 500};
    uint32_t rate_11g_pkt_lens[] = {360, 540, 720, 1080, 1440, 2160, 2880, 3240};
    //uint32_t rate_11g_pkt_lens[] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
    //uint32_t rate_11g_pkt_freqs[] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
    uint32_t rate_11n_pkt_lens[] = {384, 767, 1151, 1534, 2301, 3068, 3452, 3835};
    //uint32_t rate_11n_pkt_lens[] = {4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096};
    //uint32_t rate_11n_pkt_freqs[] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
    uint32_t rate_11ax_pkt_lens[] = {4096, 4096, 4096, 4096, 4096, 4096, 4096, 
        4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096};
    uint32_t rate_idx = 0;
    uint8_t extra_in_buf[20];
    uint16_t extra_in_len = 0;
    uint16_t extra_sc_index = BL_IOCTL_MP_SET_PKT_FREQ; //BL_IOCTL_MP_SET_PKT_LEN;
    uint32_t *pkt_lens = NULL;
    //uint32_t *pkt_freqs = NULL;
    
    if (sc_index == BL_IOCTL_MP_11b_RATE) {
        uint32_t preamble = *(uint32_t *)in_buf;
        
        if (preamble > 1) {
            printk("Invalid preamble %d, 2.4G 11b support preamble 0 or 1\n", preamble);
            return -EINVAL;
        }
        
        rate_idx = *(uint32_t *)(in_buf+4);
        
        if (rate_idx > 3) {
            printk("Invalid rate %d, 2.4G 11b support rate idx = 0 - 3, 0:1Mbps, \
                    1:2Mbps, 2:5.5Mbps, 3:11Mbps\n", rate_idx);
            return -EINVAL;
        }
        
        pkt_lens = rate_11b_pkt_lens;
        //pkt_freqs = rate_11b_pkt_freqs;
    } else if (sc_index == BL_IOCTL_MP_11g_RATE) {
        rate_idx = *(uint32_t *)in_buf;
        
        if (rate_idx > 7) {
            printk("Invalid rate %d, 2.4G 11g rate idx = 0 - 7, 0:6Mbps 1:9Mbps \
                    2:12Mbps 3:18Mbps 4:24Mbps 5:36Mbps 6:48Mbps 7:54Mbps\n", rate_idx);
            return -EINVAL;
        }
        
        pkt_lens = rate_11g_pkt_lens;
        //pkt_freqs = rate_11g_pkt_freqs;
    } else if (sc_index == BL_IOCTL_MP_11n_RATE) {
        uint32_t guard_interval = *(uint32_t *)(in_buf + 0); 
        uint32_t mcs_index = *(uint32_t *)(in_buf + 4);
        //modulation type: 2 for HT-MF, 3 for HT-GF
        uint32_t modulation_type = 2;
        uint32_t bandwidth = *(uint32_t *)(in_buf + 8);
        uint32_t ldpc = *(uint32_t *)(in_buf + 12);
        
        if (guard_interval != 0 && guard_interval != 1) {
            printk("Invalid gurad interval %d, valid is 0 for short guard interval, 1 for long guard interval\n", 
                    guard_interval);
            return -EINVAL;
        }
        
        if (modulation_type != 2 && modulation_type != 3) {
            printk("Invalid modulation type %d, valid is 2 for HT-MF, 3 for HT-GF.\n", 
                    modulation_type);
            return -EINVAL;
        }
        
        if (bandwidth != 2 && bandwidth != 4) {
            printk("Invalid bandwidth %d, valid is 2 for 20MHz, 4 for 40MHz\n",
                    bandwidth);
            return -EINVAL;
        }
        
        if (mcs_index > 7) {
            printk("Invalid mcs_index %d, valid is in range 0-7.\n", mcs_index);
            return -EINVAL;
        }
        
        if (ldpc > 1) {
            printk("Invalid ldpc type %d, valid is 0-1.\n", ldpc);
            return -EINVAL;
        }

        rate_idx = mcs_index;
        pkt_lens = rate_11n_pkt_lens;
        //pkt_freqs = rate_11n_pkt_freqs;
    } else if (sc_index == BL_IOCTL_MP_11ax_RATE) {
        uint32_t guard_interval = *(uint32_t *)(in_buf + 0); 
        uint32_t mcs_index = *(uint32_t *)(in_buf + 4);
        uint32_t bandwidth = *(uint32_t *)(in_buf + 8);
        uint32_t ldpc_sufec = *(uint32_t *)(in_buf + 12);
        uint32_t dcm = 0;
        
        if (in_len >= 20) {
            dcm = *(uint32_t *)(in_buf + 16);
        }
        
        if (guard_interval != 0 && guard_interval != 1 && guard_interval != 2) {
            printk("Invalid gurad interval %d, valid is 0-2, mapping to ['2x HELTF+0.8us GI', '2x HELTF+1.6us GI', '4x HELTF+3.2us GI']\n", 
                    guard_interval);
            return -EINVAL;
        }
        
        if (ldpc_sufec > 1) {
            printk("Invalid ldpc %d, valid is in range 0-1.\n", ldpc_sufec);
            return -EINVAL;
        }
        
        if (bandwidth != 2 && bandwidth != 4) {
            printk("Invalid bandwidth %d, valid is 2 for 20MHz, 4 for 40MHz\n", bandwidth);
            return -EINVAL;
        }
        
        if (dcm == 0) {
            if (ldpc_sufec == 1) {
                if (mcs_index > 11) {
                    printk("Invalid mcs_index %d, valid mcs_index with LDPC coding is range 0-11\n",
                          mcs_index);
                    return -EINVAL;
                }
            } else if (ldpc_sufec == 0) {
                if (mcs_index > 9) {
                    printk("Invalid mcs_index %d, valid mcs_index with BCC coding is range 0-9\n",
                           mcs_index);
                    return -EINVAL;
                }
            } else {
                printk("Invalid mcs_index %d and coding %d, valid mcs_index BCC coding is range 0-9, for LDPC coding is 0-11\n",
                       mcs_index, ldpc_sufec);
                return -EINVAL;
            }
        } else if (dcm == 1) {
            if (mcs_index > 4) {
                printk("Invalid mcs_index %d when bandwidth is 40M and DCM enabled, valid mcs_index for 40M with DCM enabled is range 0-3\n",
                       mcs_index);
                return -EINVAL;
            }
        } else if (dcm == 2) {
            if (bandwidth == 4) {
                printk("Invalid mcs_index %d, not support ER in bandwidth 40M.\n",
                       mcs_index);
                return -EINVAL;
            } else {
                if (mcs_index > 2) {
                    printk("Invalid mcs_index %d, valid mcs_index for 20M with ER enabled is in range 0-2.\n",
                           mcs_index);
                    return -EINVAL;
                }
            }
        } else {
            printk("Invalid dcm %d 40M\n", dcm);
            return -EINVAL;
        }

        rate_idx = mcs_index;
        pkt_lens = rate_11ax_pkt_lens;
    } else {
        return -EINVAL;
    }

#if 0
    memset(extra_in_buf, 0, sizeof(extra_in_buf));
    extra_sc_index = BL_IOCTL_MP_SET_TXDUTY;
    extra_in_len = 4;
    *(uint32_t *)extra_in_buf = DEFAULT_TX_DUTY;
    ret = bl_iwpriv_common_hdl(dev, info->cmd, wrqu, extra, iwp_mp_ms,
                               sizeof(iwp_mp_ms)/sizeof(struct iwpriv_cmd), extra_sc_index,
                               extra_in_buf, extra_in_len);
    if (ret != 0) {
        printk("%s, set tx duty 50 failed\n", __func__);
        return ret;
    }

    memset(extra_in_buf, 0, sizeof(extra_in_buf));
    extra_sc_index = BL_IOCTL_MP_SET_PKT_FREQ;
    extra_in_len = 4;
    memcpy(extra_in_buf, pkt_freqs+rate_idx, 4);
    ret = bl_iwpriv_common_hdl(dev, info->cmd, wrqu, extra, iwp_mp_ms,
                               sizeof(iwp_mp_ms)/sizeof(struct iwpriv_cmd), extra_sc_index,
                               extra_in_buf, extra_in_len);
    if (ret != 0) {
        printk("%s, set pkt freq failed\n", __func__);
        return ret;
    }
#endif

    memset(extra_in_buf, 0, sizeof(extra_in_buf));
    extra_sc_index = BL_IOCTL_MP_SET_PKT_LEN;
    extra_in_len = 4;
    memcpy(extra_in_buf, pkt_lens+rate_idx, 4);
    
    ret = bl_iwpriv_common_hdl(dev, info->cmd, wrqu, extra, iwp_mp_ms,
                               sizeof(iwp_mp_ms)/sizeof(struct iwpriv_cmd),
                               extra_sc_index,
                               extra_in_buf, extra_in_len);
    if (ret != 0) {
        printk("%s, set pkt len failed\n", __func__);
    }

    return ret;
}
#endif

static int bl_iwpriv_common_hdl(struct net_device *dev, 
                          uint16_t cmd, union iwreq_data *wrqu, char *extra, 
                          struct iwpriv_cmd *iwp_cmds, uint32_t sc_num, 
                          uint16_t sc_index, uint8_t *in_buf, uint16_t in_len)
{
    int ret = 0;
    uint8_t *out_buf = NULL;
    uint16_t out_len = 0;
    uint32_t var_len = 0;
    uint16_t var_type = 0;
    uint8_t mp_cmd[100] = {0};
    bool exp_found = true;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    struct iwpriv_sub_cmd *iwp_sc = NULL;

    BL_DBG_MSG("%s, info->cmd:0x%x, sc_index:%d, sc:%s, in_len:%d\n", 
           __func__, cmd, sc_index, in_buf, in_len);

    if (sc_index > sc_num){
        printk("iwpriv cmd: 0x%x, but unknow sub cmd index: 0x%x\n", cmd, sc_index);
        
        bl_dump(in_buf, in_len);
        return -EINVAL;
    }

    out_buf = kzalloc(IWPRIV_OUT_BUF_LEN, GFP_KERNEL);
    if (out_buf == NULL) {
        printk("%s, fail to alloc out_buf\n", __func__);
        return -ENOMEM;
    }
    
    memset(out_buf, 0, IWPRIV_OUT_BUF_LEN);

    var_type = get_args_type(dev, cmd);

    ////debug
    //BL_DBG("dump in_buf\n");
    //dump_buf(in_buf, in_len);

    do {
        iwp_sc = mp_get_sub_cmd(iwp_cmds[sc_index].sub_cmd, 
                                iwp_cmds[sc_index].sub_cmd_num, 
                                var_type, in_buf, in_len);
        
        if (iwp_sc == NULL) {
            printk("%s, right iwpriv cmd: %s, but unknow sub cmd\n", 
                   __func__, iwp_cmds[sc_index].name);
                   
            bl_dump(in_buf, in_len);
            ret = -EINVAL;
            break;
        }

        if (in_len < (iwp_sc->sc_var_len&(~IS_VAR_LEN)) + iwp_sc->sc_len) {
            printk("%s, right iwpriv cmd: %s, wrong len of iwpriv sub cmd and var\n", 
                   __func__, iwp_cmds[sc_index].name);
                   
            bl_dump(in_buf, in_len);
            ret = -EINVAL;
            break;
        }

        if (iwp_sc->ind_exp) {
            exp_found = false;    
        }

        memcpy(mp_cmd, iwp_sc->mfg_cmd_name, iwp_sc->mfg_cmd_len);

        in_len -= iwp_sc->sc_len;
        if (iwp_sc->sc_var_parser != NULL) {
            ret = iwp_sc->sc_var_parser((uint8_t *)in_buf + iwp_sc->sc_len, 
                                        in_len, mp_cmd + iwp_sc->mfg_cmd_len, &var_len);
            if (ret != 0) {
                printk("%s, right iwpriv cmd: %s, wrong iwpriv sub cmd var\n", 
                       __func__, iwp_cmds[sc_index].name);

                bl_dump(in_buf, in_len);
                ret = -EINVAL;
                break;
            }
            
            mp_cmd[iwp_sc->mfg_cmd_len + var_len] = '\n';
            mp_cmd[iwp_sc->mfg_cmd_len + var_len + 1] = '\0';
        } else {
            if ((iwp_sc->sc_var_len&IS_VAR_LEN) > 0) {
                var_len = strlen(in_buf) - iwp_sc->sc_len;
            } else if (iwp_sc->sc_var_len > 0) {
                var_len = iwp_sc->sc_var_len;
            }
            
            memcpy(mp_cmd + iwp_sc->mfg_cmd_len, in_buf + iwp_sc->sc_len, var_len);
            mp_cmd[iwp_sc->mfg_cmd_len + var_len] = '\n';
            mp_cmd[iwp_sc->mfg_cmd_len + var_len + 1] = '\0';
        }

        memset(bl_hw->iwp_var.iwpriv_ind, 0, IWPRIV_IND_LEN_MAX+1);
        bl_hw->iwp_var.iwpriv_ind_len = 0;

        ////debug
        BL_DBG_MSG("%s, mp_cmd:%s\n", __func__, mp_cmd);
        //dump_buf(mp_cmd, strlen(mp_cmd));
        
        ret = bl_send_mp_test_msg(bl_hw, mp_cmd, strlen(mp_cmd), out_buf + out_len, false);
        if (ret) {
            printk("%s, bl_send_mp_test_msg:%s, error=%d\n", 
                   __func__, (char *)mp_cmd, ret);
            break;
        }

        if (iwp_sc->ind_wait_ms == 0) {
            uint16_t ind_len = le16_to_cpu(*(__le16 *)(out_buf + out_len));
            memcpy(out_buf + out_len, out_buf + out_len + 2, ind_len);
            out_len += ind_len;
        } else {
            BL_DBG_MSG("%s, wait ind and check\n", __func__);
            msleep_interruptible(iwp_sc->ind_wait_ms);
            
            out_len += mp_merge_inds(bl_hw, out_buf + out_len);
            printk("%s\n", out_buf);

            if (iwp_sc->ind_exp) {
                ////debug
                //dump_buf(out_buf, out_len);
                
                if (mp_find_exp(out_buf, out_len, iwp_sc->ind_exp)) {
                    exp_found = true;
                    break;
                }
            }

            BL_DBG_MSG("%s, finish to wait ind and check\n", __func__);
        }
    } while (0);

    if (ret == 0 && iwp_sc->ind_exp && exp_found == false) {
        ret = 0; //-EPIPE;
    }

    if (wrqu->data.length + out_len > IW_PRIV_SIZE_MASK - 10)
        out_len = IW_PRIV_SIZE_MASK - 10 - wrqu->data.length;

    if (ret == 0) {
        if(extra) {
            memcpy(extra + wrqu->data.length, out_buf, out_len);
            wrqu->data.length += out_len;
        } else {
            if (copy_to_user(wrqu->data.pointer + wrqu->data.length, out_buf, out_len)) {
                ret = -EFAULT;
            } else {
                wrqu->data.length += out_len;
            }
        }
    }

    if (out_buf != NULL)
        kfree(out_buf);

    return ret;
}

int bl_iwpriv_mp_ms_hdl(struct net_device *dev, struct iw_request_info *info, 
                                 union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    uint16_t sc_index = wrqu->data.flags;
    uint8_t *in_buf = NULL;
    uint16_t in_len = wrqu->data.length;  
    uint16_t var_type = 0;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length, 
           wrqu->data.pointer, extra);
           
    if (!bl_mod_params.mp_mode) {
        printk("bl driver not in mp_mode\n");
        return -EINVAL;
    }

    var_type = get_args_type(dev, info->cmd);
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);

    in_buf = kzalloc(in_len + 1, GFP_KERNEL);
    if (in_buf == NULL) {
        printk("%s, fail to alloc buf for wrqu in data\n", __func__);
        return -ENOMEM;
    }

    if (in_len > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, in_len)) {
            printk("%s, copy from user failed\n", __func__);
            kfree(in_buf);
            return -EFAULT;
        }
    }
    in_buf[in_len] = '\0';
    wrqu->data.length = 0;


#if 0
    if (sc_index == BL_IOCTL_MP_11b_RATE || sc_index == BL_IOCTL_MP_11g_RATE || sc_index == BL_IOCTL_MP_11n_RATE || 
        sc_index == BL_IOCTL_MP_11ax_RATE) 
    {
        ret = mp_pre_rate_hdl(dev, info, wrqu, extra, sc_index, in_buf, in_len);
    }
#endif

    if (ret == 0) {
        ret = bl_iwpriv_common_hdl(dev, info->cmd, wrqu, extra, iwp_mp_ms,
                                   sizeof(iwp_mp_ms)/sizeof(struct iwpriv_cmd),
                                   sc_index, in_buf, in_len);
    }
    
    if (in_buf != NULL)
        kfree(in_buf);

    return ret;
}

int bl_iwpriv_mp_mg_hdl(struct net_device *dev, 
              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    uint16_t sc_index = wrqu->data.flags;
    uint8_t *in_buf = NULL;
    uint16_t in_len = wrqu->data.length;  
    uint16_t var_type = 0;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length, 
           wrqu->data.pointer, extra);

    if (!bl_mod_params.mp_mode) {
        printk("bl driver not in mp_mode\n");
        return -EINVAL;
    }

    if (sc_index == BL_IOCTL_MP_ATCMD) {
        return bl_iwpriv_atcmd_hdl(dev, info, wrqu, extra);
    }

    var_type = get_args_type(dev, info->cmd);
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);

    in_buf = kzalloc(in_len + 1, GFP_KERNEL);
    if (in_buf == NULL) {
        printk("%s, fail to alloc buf for wrqu in data\n", __func__);
        return -ENOMEM;
    }

    if (in_len > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, in_len)) {
            printk("%s, copy from user failed\n", __func__);
            kfree(in_buf);
            
            return -EFAULT;
        }
    }
    in_buf[in_len] = '\0';

    wrqu->data.length = 0;

    ret = bl_iwpriv_common_hdl(dev, info->cmd, wrqu, extra, iwp_mp_mg, 
                               sizeof(iwp_mp_mg)/sizeof(struct iwpriv_cmd), 
                               sc_index, in_buf, in_len);

    if (ret == 0 && 
        (sc_index == BL_IOCTL_MP_EFUSE_WR || 
         sc_index == BL_IOCTL_MP_EFUSE_CAP_WR ||
         sc_index == BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_WR ||
         sc_index == BL_IOCTL_MP_EFUSE_PWR_OFT_WR || 
         sc_index == BL_IOCTL_MP_EFUSE_MAC_ADR_WR)) 
    {
        printk("%s, in_buf:%s\n", __func__, in_buf);

        ret = mp_efuse_write(bl_hw, sc_index, in_buf, wrqu, extra);
    }

    if (in_buf != NULL)
        kfree(in_buf);

    return ret;
}

int bl_iwpriv_mp_ind_hdl(struct net_device *dev, struct iw_request_info *info, 
                                 union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    uint16_t out_len = 0;
    uint8_t *out_buf;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length, 
           wrqu->data.pointer,extra);

    if (!bl_mod_params.mp_mode) {
        printk("bl driver not in mp_mode\n");
        return -EINVAL;
    }

    out_buf = kzalloc(IWPRIV_OUT_BUF_LEN, GFP_KERNEL);
    if (out_buf == NULL) {
        printk("%s, fail to alloc out_buf\n", __func__);
        return -ENOMEM;
    }

    wrqu->data.length = 0;

    out_len += mp_merge_inds(bl_hw, out_buf + out_len);

    memset(bl_hw->iwp_var.iwpriv_ind, 0, IWPRIV_IND_LEN_MAX+1);
    bl_hw->iwp_var.iwpriv_ind_len = 0;

    if (out_len > IW_PRIV_SIZE_MASK - 10)
        out_len = IW_PRIV_SIZE_MASK - 10;
    
    out_buf[out_len] = '\0';
    out_len += 1;

    if(extra) {
        memcpy(extra, out_buf, out_len);
        wrqu->data.length += out_len;
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, out_len))
            ret = -EFAULT;
        else
            wrqu->data.length += out_len;
    }
  
    if (out_buf != NULL)
        kfree(out_buf);

    return ret;
}

int bl_iwpriv_help_hdl(struct net_device *dev, struct iw_request_info *info, 
                             union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    char * in_buf = NULL;
    uint32_t in_len = 0;
    uint16_t sc_index = 0;
    uint8_t *out_buf;
    uint32_t out_len = 0;
    uint32_t var_len = 0;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    in_len = wrqu->data.length;
    in_buf = kzalloc(wrqu->data.length + 1, GFP_KERNEL);
    if (in_buf == NULL) {
        printk("%s, fail to alloc buf for wrqu in data\n", __func__);
        
        return -ENOMEM;
    }

    out_buf = kzalloc(IWPRIV_OUT_BUF_LEN, GFP_KERNEL);
    if (out_buf == NULL) {
        printk("%s, fail to alloc out_buf\n", __func__);
        kfree(in_buf);
        
        return -ENOMEM;
    }

    if (wrqu->data.length > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, wrqu->data.length)) {
            printk("%s, copy from user failed\n", __func__);
            kfree(in_buf);
            kfree(out_buf);
            
            return -EFAULT;
        }
    }
    in_buf[wrqu->data.length] = '\0';
    wrqu->data.length = 0;

    if (in_len == 0) {
        out_len += sprintf(out_buf, 
            "\nBL Wlan Driver Version is %s\n\nUsage:\n", RELEASE_VERSION);
    } else {        
        out_len += sprintf(out_buf, 
            "\nBL Wlan Driver Version is %s\n\nAll mp commands:\n", RELEASE_VERSION);
    }

    sc_index = info->cmd - SIOCIWFIRSTPRIV;
    iwp_mp_single[sc_index].sub_cmd[0].sc_var_parser(in_buf, in_len, out_buf + out_len, &var_len);
    out_len += var_len;

    //BL_DBG("%s, out_buf:%s\n", __func__, out_buf);
    
    if (out_len > IW_PRIV_SIZE_MASK - 10)
        out_len = IW_PRIV_SIZE_MASK - 10;

    if(extra) {
        memcpy(extra, out_buf, out_len);
        wrqu->data.length += out_len;
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, out_len))
            ret = -EFAULT;
        else
            wrqu->data.length += out_len;
    }
  
    if (out_buf != NULL)
        kfree(out_buf);

    if (in_buf != NULL)
        kfree(in_buf);

    return ret;
}

int bl_iwpriv_mp_load_caldata_hdl(struct net_device *dev, 
             struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    uint16_t in_len = 0;
    char * in_buf = NULL;
    uint16_t out_len = 0;
    uint16_t short_out_len = 0;
    uint8_t *out_buf;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    bool exp_found = true;
    struct iwpriv_sub_cmd *iwp_sc = &iwp_mp_sc_load_cal_data[0];
    uint8_t *cfg_buf;
    uint16_t cfg_buf_max_len = 800;
    uint16_t cfg_buf_len = 0;
    struct mm_cal_cfg_req cal_cfg_req;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    if (!bl_mod_params.mp_mode) {
        printk("bl driver in not mp_mode\n");
        
        return -EINVAL;
    }

    memset(&cal_cfg_req, 0, sizeof(cal_cfg_req));

    in_len = wrqu->data.length + 1;
    wrqu->data.length = 0;
    in_buf = kzalloc(in_len, GFP_KERNEL);
    if (in_buf == NULL) {
        printk("%s, fail to alloc buf for wrqu in data\n", __func__);
        
        return -ENOMEM;
    }

    out_buf = kzalloc(IWPRIV_OUT_BUF_LEN, GFP_KERNEL);
    if (out_buf == NULL) {
        printk("%s, fail to alloc out_buf\n", __func__);
        kfree(in_buf);
        
        return -ENOMEM;
    }
    memset(out_buf, 0, IWPRIV_OUT_BUF_LEN);

    if (in_len > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, in_len)) {
            printk("%s, copy from user failed\n", __func__);
            kfree(in_buf);
            kfree(out_buf);
            
            return -EFAULT;
        }
    }
    in_buf[in_len] = '\0';

    cfg_buf = kzalloc(cfg_buf_max_len, GFP_KERNEL);
    if (cfg_buf == NULL) {
        printk("%s, fail to alloc cfg_buf\n", __func__);
        kfree(in_buf);
        kfree(out_buf);
        
        return -ENOMEM;
    }
    
    memset(cfg_buf, 0, cfg_buf_max_len);

    //BL_DBG("%s, in_buf:%s\n", __func__, in_buf);

    memset(bl_hw->iwp_var.iwpriv_ind, 0, IWPRIV_IND_LEN_MAX+1);
    bl_hw->iwp_var.iwpriv_ind_len = 0;

    do {
        ret = load_cal_data(in_buf, &cal_cfg_req);
        if (ret < 0) {
            printk("%s, read file %s error or parse fail, ret:%d\n",
                   __func__, in_buf, ret);
                   
            break;
        }

        //mfg cmd
        memcpy(cfg_buf, iwp_sc->mfg_cmd_name, iwp_sc->mfg_cmd_len);
        cfg_buf_len = iwp_sc->mfg_cmd_len;

        if (bl_hw->plat->chip_ver != CHIP_VER_616L) {
            int i = 0;
            
            for (i = 0; i < sizeof(struct mm_cal_cfg_req); i++) {
                cfg_buf_len += sprintf(cfg_buf + cfg_buf_len, "%02x",
                                       ((uint8_t *)&cal_cfg_req)[i]);
            }
            
            cfg_buf[cfg_buf_len] = '\n';
            cfg_buf[cfg_buf_len + 1] = '\0';
            cfg_buf_len += 1;
            printk("%s, cal cfg struct len:%u\n", __func__, cfg_buf_len-iwp_sc->mfg_cmd_len);
        } else {
            cfg_buf_len += bl_convert_cal_to_tlv(cfg_buf + cfg_buf_len, cfg_buf_max_len-cfg_buf_len-4);
            printk("%s, tlv len:%u\n", __func__, cfg_buf_len-iwp_sc->mfg_cmd_len);
            bl_dump(cfg_buf+iwp_sc->mfg_cmd_len, cfg_buf_len-iwp_sc->mfg_cmd_len);
            
            cfg_buf[cfg_buf_len] = RFTLV_TYPE_INVALID;
            cfg_buf_len++;
            //cfg_buf[cfg_buf_len] = 0xfe;
            //cfg_buf_len++;
            //cfg_buf[cfg_buf_len] = 0xfe;
            //cfg_buf_len++;
        }

        printk("%s, cfg_buf_len:%d, cfg_buf:%s\n", __func__, cfg_buf_len, cfg_buf);

        if (iwp_sc->ind_exp) {
            exp_found = false;    
        }
        
        memset(bl_hw->iwp_var.iwpriv_ind, 0, IWPRIV_IND_LEN_MAX+1);
        bl_hw->iwp_var.iwpriv_ind_len = 0;
        
        ret = bl_send_mp_test_msg(bl_hw, cfg_buf, cfg_buf_len, out_buf + out_len, false);
    
        if (ret) {
            printk("%s, bl_send_mp_test_msg, error=%d\n", __func__, ret);
            break;
        } else {
            out_len += sprintf(out_buf, 
                         "\n%s, success to recv cfm from firmware\n", __func__);
        }
        short_out_len = out_len;
        
        if (iwp_sc->ind_wait_ms == 0) {
            uint16_t ind_len = le16_to_cpu(*(__le16 *)(out_buf + out_len));
            memcpy(out_buf + out_len, out_buf + out_len + 2, ind_len);
            out_len += ind_len;
        } else {
            BL_DBG("%s, wait ind and check\n", __func__);
            msleep_interruptible(iwp_sc->ind_wait_ms);
            
            out_len += mp_merge_inds(bl_hw, out_buf + out_len);
    
            if (iwp_sc->ind_exp) {
                //dump_buf(out_buf, out_len);
                if (mp_find_exp(out_buf, out_len, iwp_sc->ind_exp)) {
                    exp_found = true;
                    break;
                }
            }
    
            BL_DBG("%s, finish to wait ind and check\n", __func__);
        }
    } while (0);

    out_buf[short_out_len] = '\0';
    short_out_len += 1;
    
    if (ret == 0 && iwp_sc->ind_exp && exp_found == false) {
        ret = -EPIPE;
    }
    
    if(extra) {
        memcpy(extra + wrqu->data.length, out_buf, short_out_len);
        wrqu->data.length += short_out_len;
    } else {
        if (copy_to_user(wrqu->data.pointer + wrqu->data.length, out_buf, short_out_len))
            ret = -EFAULT;
        else
            wrqu->data.length += short_out_len;
    }
        
    if (in_buf != NULL)
        kfree(in_buf);
    
    if (out_buf != NULL)
        kfree(out_buf);
    
    if (cfg_buf != NULL)
        kfree(cfg_buf);
    
    return ret;
}

//RTOS
//Used in rtos, in which os/platform does not support filesystem or cfg file rw problem.
#if 0
int bl_iwpriv_mp_cfg_caldata_hdl(struct net_device *dev, struct iw_request_info *info, 
                                 union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    uint32_t i = 0;
    uint16_t in_len = 0;
    char * in_buf = NULL;
    uint16_t out_len = 0;
    uint16_t short_out_len = 0;
    uint8_t *out_buf;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    bool exp_found = true;
    struct iwpriv_sub_cmd *iwp_sc = &iwp_mp_sc_cfg_cal_data[0];
    uint8_t *cfg_buf;
    uint16_t cfg_buf_len = 512;
    struct mm_cal_cfg_req cal_cfg_req;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length, wrqu->data.pointer, extra);

    if (!bl_mod_params.mp_mode) {
        printk("bl driver in not mp_mode\n");
        return -EINVAL;
    }

    memset(&cal_cfg_req, 0, sizeof(cal_cfg_req));

    in_len = wrqu->data.length + 1;
    wrqu->data.length = 0;
    in_buf = kzalloc(in_len, GFP_KERNEL);
    if (in_buf == NULL) {
        printk("%s, fail to alloc buf for wrqu in data\n", __func__);
        return -ENOMEM;
    }

    out_buf = kzalloc(IWPRIV_OUT_BUF_LEN, GFP_KERNEL);
    if (out_buf == NULL) {
        printk("%s, fail to alloc out_buf\n", __func__);
        kfree(in_buf);
        return -ENOMEM;
    }
    memset(out_buf, 0, IWPRIV_OUT_BUF_LEN);

    if (in_len > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, in_len)) {
            printk("%s, copy from user failed\n", __func__);
            kfree(in_buf);
            kfree(out_buf);
            return -EFAULT;
        }
    }
    in_buf[in_len] = '\0';
    printk("%s, in_buf:%s\n", __func__, in_buf);

    cfg_buf = kzalloc(cfg_buf_len, GFP_KERNEL);
    if (cfg_buf == NULL) {
        printk("%s, fail to alloc cfg_buf\n", __func__);
        kfree(in_buf);
        kfree(out_buf);
        return -ENOMEM;
    }
    memset(cfg_buf, 0, cfg_buf_len);

    //debug
    //BL_DBG("%s, in_buf:%s\n", __func__, in_buf);

    memset(bl_hw->iwp_var.iwpriv_ind, 0, IWPRIV_IND_LEN_MAX+1);
    bl_hw->iwp_var.iwpriv_ind_len = 0;

    do {
        ret = parse_cal_data(in_buf, &cal_cfg_req);
        if (ret < 0) {
            printk("%s, read file error, ret:%d\n", __func__, ret);
            break;
        }

        //mfg cmd
        memcpy(cfg_buf, iwp_sc->mfg_cmd_name, iwp_sc->mfg_cmd_len);
        cfg_buf_len = iwp_sc->mfg_cmd_len;
        for (i = 0; i < sizeof(struct mm_cal_cfg_req); i++) {
            cfg_buf_len += sprintf(cfg_buf + cfg_buf_len, "%02x", ((uint8_t *)&cal_cfg_req)[i]);
        }
        cfg_buf[cfg_buf_len] = '\n';
        cfg_buf[cfg_buf_len + 1] = '\0';
        cfg_buf_len += 1;
        
        printk("%s, cfg_buf_len:%d, cfg_buf:%s\n", __func__, cfg_buf_len, cfg_buf);

        if (iwp_sc->ind_exp) {
            exp_found = false;    
        }
        
        memset(bl_hw->iwp_var.iwpriv_ind, 0, IWPRIV_IND_LEN_MAX+1);
        bl_hw->iwp_var.iwpriv_ind_len = 0;
        
        ret = bl_send_mp_test_msg(bl_hw, cfg_buf, out_buf + out_len, false);
    
        if (ret) {
            printk("%s, bl_send_mp_test_msg, error=%d\n", __func__, ret);
            break;
        } else {
            out_len += sprintf(out_buf, "\n%s, success to recv cfm from firmware\n", __func__);
        }
        short_out_len = out_len;
    
        if (iwp_sc->ind_wait_ms == 0) {
            uint16_t ind_len = le16_to_cpu(*(__le16 *)(out_buf + out_len));
            memcpy(out_buf + out_len, out_buf + out_len + 2, ind_len);
            out_len += ind_len;
        } else {
            BL_DBG("%s, wait ind and check\n", __func__);
            msleep_interruptible(iwp_sc->ind_wait_ms);
            
            out_len += mp_merge_inds(bl_hw, out_buf + out_len);
    
            if (iwp_sc->ind_exp) {
                ////debug
                //dump_buf(out_buf, out_len);
                if (mp_find_exp(out_buf, out_len, iwp_sc->ind_exp)) {
                    exp_found = true;
                    break;
                }
            }
    
            BL_DBG("%s, finish to wait ind and check\n", __func__);
        }
    } while (0);

    out_buf[short_out_len] = '\0';
    short_out_len += 1;
    
    if (ret == 0 && iwp_sc->ind_exp && exp_found == false) {
        ret = -EPIPE;
    }
    
    if(extra) {
        memcpy(extra + wrqu->data.length, out_buf, short_out_len);
        wrqu->data.length += short_out_len;
    } else {
        if (copy_to_user(wrqu->data.pointer + wrqu->data.length, out_buf, short_out_len))
            ret = -EFAULT;
        else
            wrqu->data.length += short_out_len;
    }
    
    if (in_buf != NULL)
        kfree(in_buf);
    
    if (out_buf != NULL)
        kfree(out_buf);
    
    if (cfg_buf != NULL)
        kfree(cfg_buf);
    
    return ret;
}
#endif
#endif //end of CONFIG_BL_MP

int bl_iwpriv_wmmcfg_hdl(struct net_device *dev, struct iw_request_info *info, 
                                  union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    char * in_buf = NULL;
    uint32_t in_len = 0;
    uint8_t *out_buf = NULL;
    uint8_t out_buf_len = 200;
    uint32_t var_value = 0;
    uint16_t var_type = 0;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length, 
           wrqu->data.pointer, extra);

    if (bl_mod_params.mp_mode) {
        printk("bl driver in mp_mode\n");
        
        return -EINVAL;
    }

    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);

    in_buf = kzalloc(in_len + 1, GFP_KERNEL);
    if (in_buf == NULL)
    
        return -ENOMEM;

    out_buf = kzalloc(out_buf_len, GFP_KERNEL);
    if (out_buf == NULL) {
        kfree(in_buf);
        
        return -ENOMEM;
    }

    if (in_len > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, in_len)) {
            kfree(in_buf);
            kfree(out_buf);
            
            return -EFAULT;
        }
    } else {
        kfree(in_buf);
        kfree(out_buf);

        return -EINVAL;
    }
    wrqu->data.length = 0;

    // dump_buf(in_buf, in_len);

    do {
        var_value = *(uint32_t *)in_buf;
        
        ret = bl_send_wmmcfg(bl_hw, var_value);
        if (ret) {
            printk("bl_send_wmmcfg, error=%d\n", ret);
            
            if (ret == -ETIMEDOUT)
                wrqu->data.length += sprintf(out_buf,
                    "\nFail, timeout when wait for wmmcfg response from firmware\n");
            else
                wrqu->data.length += sprintf(out_buf, "\nFail to wmmcfg, %d\n", ret);
            break;
        }

        out_buf[wrqu->data.length] = '\0';
        wrqu->data.length += 1;
    } while (0);

    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length)) {
            ret = -EFAULT;
        }
    }

    if (out_buf != NULL)
        kfree(out_buf);

    if (in_buf != NULL)
        kfree(in_buf);

    return ret;
}

int bl_caldata_cfg_file_handle(struct bl_hw *bl_hw, char *file_path)
{
    int ret = 0;
    struct mm_cal_cfg_req cal_cfg_req;

    if (bl_mod_params.mp_mode) {
        printk("bl driver in mp_mode\n");
        
        return -EINVAL;
    }

    memset(&cal_cfg_req, 0, sizeof(cal_cfg_req));
    
    //BL_DBG("%s, in_buf:%s\n", __func__, in_buf);

    do {
#ifdef CFG_FILE_LOCATION_USER_DEFINE
        ret = load_cal_data(file_path, &cal_cfg_req);
#else
        ret = bl_parse_cal_configfile(bl_hw, file_path, &cal_cfg_req);
#endif

        if (ret < 0) {
            printk("%s, read file %s or parse error, ret:%d\n",
                   __func__, file_path, ret);
            break;
        }
        
        //update mac address
        if(cal_cfg_req.mac_valid)
            memcpy(bl_hw->version_cfm.mac, cal_cfg_req.mac, ETH_ALEN);

        ret = bl_send_cal_cfg(bl_hw, &cal_cfg_req);
        if (ret) {
            printk("%s, bl_send_cal_cfg, error=%d\n", __func__, ret);
        } 
    } while(0);

    return ret;
}

int bl_country_pwr_file_handle(struct bl_hw *bl_hw, char *file_path)
{
    int ret = 0;

    if (bl_mod_params.mp_mode) {
        printk("bl driver in mp_mode\n");
        return -EINVAL;
    }

    ret = bl_parse_country_tx_pwr_configfile(bl_hw, file_path);

    return ret;
}

int bl_iwpriv_atcmd_hdl(struct net_device *dev, struct iw_request_info *info, 
                               union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    int ret = 0;
    char * in_buf = NULL;
    u8 out_buf[128];

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer,extra);

    in_buf = kzalloc(wrqu->data.length + 5, GFP_KERNEL);
    
    if (in_buf == NULL)
        return -ENOMEM;

    if (wrqu->data.length > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, wrqu->data.length)) {
            kfree(in_buf);
            
            return -EFAULT;
        }
    } else {
        kfree(in_buf);
        
        return -EINVAL;
    }

    //dump_buf(in_buf, wrqu->data.length);
    
    in_buf[wrqu->data.length-1] = '\r';
    in_buf[wrqu->data.length] = '\0';
    printk("%s, in_buf:%s, len:%d\r\n", __func__, in_buf, strlen(in_buf));
    wrqu->data.length = 0;

    ret = bl_send_mp2_test_msg(bl_hw, in_buf, NULL, false);
    if (ret) {
        printk("%s, bl_send_mp2_test_msg:%s, error=%d\n", 
               __func__, (char *)in_buf, ret);
               
        if (in_buf != NULL)
            kfree(in_buf);
            
        return -EFAULT;
    }
    
    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }
  
    if (in_buf != NULL)
        kfree(in_buf);

    return ret;
}

int bl_iwpriv_ver_hdl(struct net_device *dev, struct iw_request_info *info, 
                           union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    int ret = 0;
    char * in_buf = NULL;
    u8 out_buf[128];

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer,extra);

    in_buf = kzalloc(wrqu->data.length + 1, GFP_KERNEL);
    
    if (in_buf == NULL)
        return -ENOMEM;

    if (wrqu->data.length > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, wrqu->data.length)) {
            kfree(in_buf);
            
            return -EFAULT;
        }
    }
    
    in_buf[wrqu->data.length] = '\0';
    wrqu->data.length = 0;

    if (strcmp(in_buf, "Signed") == 0) {
        wrqu->data.length += sprintf(out_buf, "\nBL Driver version: %s, Signed: %s \nBL FW version: %s-%d, Signed: %s\n", 
                                     RELEASE_VERSION, RELEASE_SIGNED, 
                                     bl_hw->wiphy->fw_version, 
                                     bl_hw->version_cfm.sub_version, 
                                     bl_hw->version_cfm.version_signed);
    } else {
        wrqu->data.length += sprintf(out_buf, 
                                     "\nBL Driver version: %s \nBL FW version: %s-%d\n", 
                                     RELEASE_VERSION, bl_hw->wiphy->fw_version, 
                                     bl_hw->version_cfm.sub_version);
    }
    
    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }
  
    if (in_buf != NULL)
        kfree(in_buf);

    return ret;
}

int bl_iwpriv_temp_read_hdl(struct net_device *dev,
              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    int ret = 0;
    int temp = 0;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer,extra);

    wrqu->data.length = 0;

    if(bl_send_temp_read_req(bl_hw, &temp))
        return -1;

    wrqu->data.length += sizeof(temp);

    if(extra) {
        memcpy(extra, &temp, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, &temp, wrqu->data.length))
            ret = -EFAULT;
    }

    return ret;
}

#if 0
int bl_iwpriv_set_priv_ies_on_off_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    u8 in_buf;

    if (copy_from_user(&in_buf, wrqu->data.pointer, sizeof(in_buf)))
        return -EFAULT;

    bl_hw->priv_ies = in_buf;

    printk("priv_ies=%d\n", bl_hw->priv_ies);
    
    return 0;
}
#endif

/*formatModTx, mcs, nss, bwTx, giAndPreTypeTx*/
int bl_iwpriv_set_rate_hdl(struct net_device *dev,
              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    union bl_rate_ctrl_info rate_config;
    union bl_mcs_index *r = (union bl_mcs_index *)&rate_config;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    u8 sta_idx = 0xff;
    s8 info_buf[6] = {0};

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    if (bl_mod_params.mp_mode) {
        printk("bl driver in mp_mode\n");
        
        return -EINVAL;
    }

    if (vif->wdev.iftype == NL80211_IFTYPE_AP) {
        struct bl_sta *sta_iter;
        
        list_for_each_entry(sta_iter, &vif->ap.sta_list, list) {
            if (sta_iter->valid) {
                sta_idx = sta_iter->sta_idx;
                break;
            }
        }
        
        if (sta_idx == 0xff) {
            printk("no valid sta under this ap\r\n");
            return 0;
        }
    } else {
        if(vif->sta.ap == NULL) {
            printk("connect ap first\n");
            
            return 0;
        }
        
        sta_idx = vif->sta.ap->sta_idx;
    }

    rate_config.value = 0;

    if((wrqu->data.length < 1) || (wrqu->data.length > 6) ) {
        printk("cmd usage: more info see README:\n");
        printk("cancel fixrate: iwpriv wlanx setrate -1\n");
        printk("setrate: iwpriv wlanx setrate [format_mode][mcs] \n");
        printk("setrate: iwpriv wlanx setrate [format_mode][mcs][giAndpreType] \n");    
        printk("setrate: iwpriv wlanx setrate [format_mode][mcs][giAndpreType][nss][bw]  \n");
        printk("setrate: iwpriv wlanx setrate [format_mode][mcs][giAndpreType][nss][bw][dcm]  \n");
        
        return 0;
    }

    printk("vif->sta_idx=%d, valid=%d\n", vif->sta.ap->sta_idx, vif->sta.ap->valid);

    if (copy_from_user(info_buf, wrqu->data.pointer, sizeof(info_buf)))
        return -EFAULT;

    if(info_buf[0] == -1) {
        rate_config.value = (u32)-1;
    } else {
        rate_config.formatModTx = info_buf[0];
        
        if(rate_config.formatModTx == FORMATMOD_NON_HT) {
            rate_config.mcsIndexTx = info_buf[1];
        } else if(rate_config.formatModTx == FORMATMOD_HT_MF) {
            r->ht.mcs = info_buf[1];

            if(wrqu->data.length > 3)
                r->ht.nss = info_buf[3];
        }else { 
            r->vht.mcs = info_buf[1];
            
            if(wrqu->data.length > 3)
                r->vht.nss = info_buf[3];

            if(wrqu->data.length == 6)
                rate_config.dcmTx = info_buf[5];
        }

        if(wrqu->data.length > 2) {
            if(rate_config.formatModTx == FORMATMOD_NON_HT) {
                rate_config.bmodePreamble = info_buf[2];
            } else {
                rate_config.giAndPreTypeTx = info_buf[2];
                rate_config.bmodePreamble = (info_buf[2]>>1);
            }
        }
     
        if (wrqu->data.length > 4)
            rate_config.bwTx = info_buf[4];
    }

    if(info_buf[0] != -1)
        printk("info_buf: %d %d %d %d %d\n", info_buf[0], info_buf[1],
               info_buf[2], info_buf[3], info_buf[4]);
    else
        printk("info_buf: %d\n", info_buf[0]);

    printk("rate_config.value=%0x\n", rate_config.value);

     // Forward the request to the LMAC
     if ((ret = bl_send_me_rc_set_rate(bl_hw, sta_idx, (u16)rate_config.value)) != 0)
     {
         printk("%s failed, ret=%d\n", __func__, ret);
         
         return ret;
     }

    return ret;
}

int bl_iwpriv_rw_coex_param_hdl(struct net_device *dev, 
              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    char * in_buf = NULL;
    uint32_t in_len = 0;
    uint8_t *out_buf = NULL;
    uint8_t out_buf_len = 200;
    uint16_t var_type = 0;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    struct dbg_coex_rw_param_req rw_req;
    struct dbg_coex_rw_param_cfm rw_cfm;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);

    in_buf = kzalloc(in_len + 1, GFP_KERNEL);
    
    if (in_buf == NULL)
        return -ENOMEM;

    out_buf = kzalloc(out_buf_len, GFP_KERNEL);
    
    if (out_buf == NULL) {
        kfree(in_buf);
        
        return -ENOMEM;
    }

    if (in_len > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, in_len)) {
            kfree(in_buf);
            kfree(out_buf);
            
            return -EFAULT;
        }
    } else {
        printk("%s, wrong input param len %d, at least 1 byte as rw cmd\n",
               __func__, in_len);
               
        kfree(in_buf);
        kfree(out_buf);

        return -EINVAL;
    }
    
    wrqu->data.length = 0;

    do {
        memset(&rw_cfm, 0, sizeof(rw_cfm));

        rw_req.rw_cmd = *(uint8_t *)in_buf;
        
        if (rw_req.rw_cmd != 0) //write
        {
            if (in_len != sizeof(struct dbg_coex_rw_param_req)) {
                printk("%s, wrong input param len %d, should be %zu=sizeof(struct dbg_coex_rw_param_req)\n",
                       __func__, in_len, sizeof(struct dbg_coex_rw_param_req));
                ret = -EINVAL;
                break;
            }

            memcpy(&rw_req.cp, (uint8_t *)in_buf+1, sizeof(struct coex_param));
        }

        ret = bl_send_rw_coex_param(bl_hw, &rw_req, &rw_cfm);
        
        if (ret) {
            printk("bl_send_rw_coex_param, error=%d\n", ret);
            
            if (ret == -ETIMEDOUT)
                wrqu->data.length += sprintf(out_buf, 
                    "\nFail, timeout when wait for rw_coex_param response from firmware\n");
            else
                wrqu->data.length += sprintf(out_buf,
                                          "\nFail to rw_coex_param, %d\n", ret);
                
            break;
        }

        if (rw_req.rw_cmd != 0) {
            #if 0
            if (rw_cfm.cp.coex_cfg_split_num == 1) {
                printk("%s, fail to write coex_cfg_split_num with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_split_num);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_inq_wifi_con_trx == 1) {
                printk("%s, fail to write coex_cfg_split_num with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_split_num);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_inq_wifi_con == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_inq_wifi_con with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_inq_wifi_con);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_sco_trx_wifi_con_trx == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_sco_trx_wifi_con_trx with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_sco_trx_wifi_con_trx);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_sco_trx_wifi_con == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_sco_trx_wifi_con with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_sco_trx_wifi_con);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_trx_wifi_con_trx == 1) {
                printk("%s, fail to write coex_cfg_split_num with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_split_num);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_trx_wifi_con == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_acl_trx_wifi_con with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_acl_trx_wifi_con);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_wifi_con_trx == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_acl_wifi_con_trx with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_acl_wifi_con_trx);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_wifi_con == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_acl_wifi_con with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_acl_wifi_con);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_pi_scan_wifi_con_trx == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_pi_scan_wifi_con_trx with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_pi_scan_wifi_con_trx);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_other_wifi_con_trx == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_other_wifi_con_trx with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_other_wifi_con_trx);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_pi_scan_wifi_con == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_pi_scan_wifi_con with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_pi_scan_wifi_con);
            }
            if (rw_cfm.cp.coex_cfg_wifi_pct_bt_other_wifi_con == 1) {
                printk("%s, fail to write coex_cfg_wifi_pct_bt_other_wifi_con with new value %d\n", 
                       __func__, rw_req.cp.coex_cfg_wifi_pct_bt_other_wifi_con);
            }
            #endif
            printk("%s, write ret status:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                     __func__,
                     rw_cfm.cp.coex_cfg_split_num,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_inq_wifi_con_trx,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_inq_wifi_con,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_sco_trx_wifi_con_trx,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_sco_trx_wifi_con,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_trx_wifi_con_trx,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_trx_wifi_con,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_wifi_con_trx,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_wifi_con,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_pi_scan_wifi_con_trx,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_pi_scan_wifi_con,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_other_wifi_con_trx,
                     rw_cfm.cp.coex_cfg_wifi_pct_bt_other_wifi_con);
        } 
        else 
        {
            printk("%s, read coex params:\n \
                   cur_wifi_pct=%d\n \
                   coex_cfg_split_num = %d\n \
                   coex_cfg_wifi_pct_bt_inq_wifi_con_trx = %d\n \
                   coex_cfg_wifi_pct_bt_inq_wifi_con = %d\n \
                   coex_cfg_wifi_pct_bt_sco_trx_wifi_con_trx = %d\n \
                   coex_cfg_wifi_pct_bt_sco_trx_wifi_con = %d\n \
                   coex_cfg_wifi_pct_bt_acl_trx_wifi_con_trx = %d\n \
                   coex_cfg_wifi_pct_bt_acl_trx_wifi_con = %d\n \
                   coex_cfg_wifi_pct_bt_acl_wifi_con_trx = %d\n \
                   coex_cfg_wifi_pct_bt_acl_wifi_con = %d\n \
                   coex_cfg_wifi_pct_bt_pi_scan_wifi_con_trx = %d\n \
                   coex_cfg_wifi_pct_bt_pi_scan_wifi_con = %d\n \
                   coex_cfg_wifi_pct_bt_other_wifi_con_trx = %d\n \
                   coex_cfg_wifi_pct_bt_other_wifi_con = %d\n", 
                   __func__, 
                   rw_cfm.cur_wifi_pct,
                   rw_cfm.cp.coex_cfg_split_num,
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_inq_wifi_con_trx,
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_inq_wifi_con,                    
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_sco_trx_wifi_con_trx,
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_sco_trx_wifi_con,                    
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_trx_wifi_con_trx,
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_trx_wifi_con,                    
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_wifi_con_trx,
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_acl_wifi_con,                    
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_pi_scan_wifi_con_trx,
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_pi_scan_wifi_con,
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_other_wifi_con_trx,                    
                   rw_cfm.cp.coex_cfg_wifi_pct_bt_other_wifi_con
            );
        }

        out_buf[wrqu->data.length] = '\0';
        wrqu->data.length += 1;
    } while (0);

    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length)) {
            ret = -EFAULT;
        }
    }

    if (out_buf != NULL)
        kfree(out_buf);

    if (in_buf != NULL)
        kfree(in_buf);

    return ret;
}

int bl_iwpriv_rw_mem_hdl(struct net_device *dev,
            struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    char * in_buf = NULL;
    uint32_t in_len = 0;
    uint8_t *out_buf = NULL;
    uint8_t out_buf_len = 200;
    uint16_t var_type = 0;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    struct dbg_mem_read_cfm cfm;
    uint32_t mem_addr = 0;
    uint32_t mem_data = 0;
    uint32_t rw_cmd = 0;
    uint32_t rd_len = 0;

    /** Read addr: iwpriv wlan0 rw_mem 0 addr num*/
    /** write addr: iwpriv wlan0 rw_mem 1 addr value */
    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);

    in_buf = kzalloc(in_len + 1, GFP_KERNEL);
    
    if (in_buf == NULL)
        return -ENOMEM;

    out_buf = kzalloc(out_buf_len, GFP_KERNEL);
    
    if (out_buf == NULL) {
        kfree(in_buf);
        
        return -ENOMEM;
    }

    if (in_len > 0) {
        if (copy_from_user(in_buf, wrqu->data.pointer, in_len)) {
            kfree(in_buf);
            kfree(out_buf);
            
            return -EFAULT;
        }
    } else {
        printk("%s, wrong input param len %d, at least 2 uint32_t as rw cmd and param\n",
               __func__, in_len);
               
        kfree(in_buf);
        kfree(out_buf);
        
        return -EINVAL;
    }
    
    wrqu->data.length = 0;

    bl_hw->cmd_mgr.state = BL_CMD_MGR_STATE_INITED;

    do {
        memset(&cfm, 0, sizeof(cfm));

        if (in_len < 2*sizeof(uint32_t)) {
            printk("%s, wrong input param len %d, at least 2 uint32_t as rw cmd and param\n",
                   __func__, in_len);
                   
            ret = -EINVAL;
            break;
        }

        rw_cmd = *(uint32_t *)in_buf;
        mem_addr = *(uint32_t *)(in_buf + 4);
        if (rw_cmd==0 && in_len > 2*sizeof(uint32_t)) {
            rd_len = *(uint32_t *)(in_buf + 8);
        }
        
        if (mem_addr&0x3) {
            printk("%s, addr 0x%x is not 4 bytes align\r\n", __func__, mem_addr);
            
            ret = -EINVAL;
            break;
        }
        
        if (rw_cmd != 0) //write
        {
            if (in_len != 3*sizeof(uint32_t)) {
                printk("%s, wrong input param len %d for write mem cmd, should be %zu=3*sizeof(uint32_t)\n",
                       __func__, in_len, 3*sizeof(uint32_t));
                       
                ret = -EINVAL;
                break;
            }
            
            mem_data = *(uint32_t *)(in_buf + 8);
            ret = bl_send_dbg_mem_write_req(bl_hw, mem_addr, mem_data);
        }
        else
        {
            ret = bl_send_dbg_mem_read_req(bl_hw, mem_addr, rd_len, &cfm);
        }

        if (ret) {
            printk("bl_send_dbg_mem_write/read_req, error=%d\n", ret);
            
            if (ret == -ETIMEDOUT)
                wrqu->data.length += sprintf(out_buf, 
                    "\nFail, timeout when wait for rw mem response from firmware\n");
            else
                wrqu->data.length += sprintf(out_buf, "\nFail to rw mem, %d\n", ret);
                
            break;
        }

        if (rw_cmd != 0) {
            printk("%s, success to write addr 0x%x with value 0x%x\n",
                   __func__, mem_addr, mem_data);
        } 
        else 
        {
            printk("%s, success to read addr 0x%x with value 0x%x\n",
                   __func__, cfm.memaddr, cfm.memdata);
        }

        out_buf[wrqu->data.length] = '\0';
        wrqu->data.length += 1;
    } while (0);

    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length)) {
            ret = -EFAULT;
        }
    }

    if (out_buf != NULL)
        kfree(out_buf);

    if (in_buf != NULL)
        kfree(in_buf);

    return ret;

}

//normal private sub cmd follow below example, don't use new iotcl

//********************************************************************************
//********private cmd parameter in INT mode *************************************
//********************************************************************************

int bl_iwpriv_set_phy_misc_hdl(struct net_device *dev, 
              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    //uint16_t sub_oid = wrqu->data.flags;
    int ret = 0;
    uint32_t in_len = 0;
    uint16_t var_type = 0;
    int32_t misc_value[sizeof(struct mm_set_phy_misc_req)/4];    
    struct mm_set_phy_misc_cfm phy_misc_cfm = {0};
    uint8_t *out_buf = NULL;
    uint32_t out_buf_len = 500;
    int i = 0;
    
    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length, 
           wrqu->data.pointer, extra);

    if(wrqu->data.length < 1) {
        printk("help: iwpriv wlan0 set_phy_misc misc_type [decimal array, int32_t]\n");
        printk("misc_type = 1, set cca with cca_rise and cca_fall param, cmd: iwpriv wlan0 set_phy_misc 1 62 65\n");

        ret = -EINVAL;
        
        return ret;
    }
    
    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);

    if (in_len > sizeof(struct mm_set_phy_misc_req)) {
        printk("%s, too long param to set phy misc\r\n", __func__);
        
        return -EFAULT;
    }

    if (in_len > 0) {
        if (copy_from_user((uint8_t *)misc_value, wrqu->data.pointer, in_len)) 
        {
            return -EFAULT;
        }
    }

    out_buf = kzalloc(out_buf_len, GFP_KERNEL);
    if (out_buf == NULL) {
    
        return -ENOMEM;
    }
    
    wrqu->data.length = 0;

    ret = bl_send_set_phy_misc(bl_hw, misc_value[0], misc_value+1, in_len-4, &phy_misc_cfm);

    if (ret) {
        printk("set_phy_misc failed, ret:%d\n", ret);
        
        kfree(out_buf);
        ret = -EFAULT;
        
        return ret;
    }

    wrqu->data.length += sprintf(out_buf + wrqu->data.length, "set_phy_misc_cfm: ");

    BL_DBG("phy_misc_cfm, print_type:%d, len:%d\r\n",
           phy_misc_cfm.print_type, phy_misc_cfm.len);
    
    if (phy_misc_cfm.print_type == 0) {
        for (i=0; i<phy_misc_cfm.len; i++) {
            wrqu->data.length += sprintf(out_buf + wrqu->data.length, 
                                         "0x%04x ", phy_misc_cfm.resp[i]);
        }
    } else {
        wrqu->data.length += sprintf(out_buf + wrqu->data.length,
                                     "%s", (char *)phy_misc_cfm.resp);
    }
    
    printk("%s, %s\r\n", __func__, out_buf);
    
    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }
  
    if (out_buf != NULL)
        kfree(out_buf);
    
    return ret;
}

int bl_iwpriv_edca_hdl(struct net_device *dev, struct iw_request_info *info, 
                             union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    int ret = 0;
    uint32_t in_len = 0;
    uint16_t var_type = 0;
    int data[2];    

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p \n", 
           __func__, info->cmd, info->flags, wrqu->data.length, 
           wrqu->data.pointer, extra);

    if(wrqu->data.length > 2) {
        printk("help: iwpriv wlan0 edca [ac_param] [txq]\n");        
        printk("[ac_param]:txop|cwmax|cwmin|aifsn\n");
        printk("[txq]:BK-0,BE-1,VO-2,VI-3\n");
        ret = -EINVAL;
        
        return ret;
    }
    
    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);


    if (in_len > 0) {
        if (copy_from_user((uint8_t *)data, wrqu->data.pointer, in_len)) {
            return -EFAULT;
        }
    }
    
    wrqu->data.length = 0;

    ret = bl_send_set_edca(bl_hw, data[1], data[0], false, vif->vif_index);

    return ret;
}


int bl_iwpriv_send_twt_request_hdl(struct net_device *dev, 
              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    int ret = 0;
    uint32_t in_len = 0;
    uint16_t var_type = 0;
    int data[6];
    struct twt_conf_tag twt_conf;
    struct twt_setup_cfm twt_setup_cfm;
    int error = 1, setup_command = -1;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p \n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    if(wrqu->data.length != 6) {
        printk("help: iwpriv wlan0 twt_setup [setup_command] [flow_type][wake_interval_exp][wake_dur_unit][nominal_min_wake_dur][wake_interval_mantissa]\n");        
        ret = -EINVAL;
        
        return ret;
    }
    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);


    if (in_len > 0) {
        if (copy_from_user((uint8_t *)data, wrqu->data.pointer, in_len)) {
            return -EFAULT;
        }
    }
    wrqu->data.length = 0;

    memset(&twt_conf, 0, sizeof(twt_conf));

    setup_command = data[0];
    twt_conf.flow_type = data[1];    
    twt_conf.wake_int_exp = data[2];    
    twt_conf.wake_dur_unit = data[3];
    twt_conf.min_twt_wake_dur = data[4];
    twt_conf.wake_int_mantissa = data[5];

    if (setup_command == -1)
    {
        printk("%s: TWT missing setup command\n", __func__);
        
        return -EFAULT;
    }
    
    printk("%s set=%d flow_type=%d int_mantissa=%d int_exp=%d twt_dur=%d dur_unit=%d",
          __func__, setup_command, 
          twt_conf.flow_type, twt_conf.wake_int_mantissa, twt_conf.wake_int_exp, 
          twt_conf.min_twt_wake_dur, twt_conf.wake_dur_unit);

    // Forward the request to the LMAC
    if ((error = bl_send_twt_request(bl_hw, setup_command, vif->vif_index,
                                      &twt_conf, &twt_setup_cfm)) != 0)
        return error;

    // Check the status
    if (twt_setup_cfm.status != CO_OK)
        return -EIO;

    return 0;
}

int bl_iwpriv_ke_stat_req_hdl(struct net_device *dev, struct iw_request_info *info, 
                                      union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    bool clear_on_read = false;
    uint32_t period_rpt = 0;
    uint32_t period_print = 0;
    uint32_t in_len = 0;
    uint16_t var_type = 0;
    int data[3];

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p \n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    if (wrqu->data.length != 3) {
        printk("usage: iwpriv wlan0 ke_stat [0 or 1, clear on read] [report period in us] [uart print period in us]\n\
                please input:iwpriv wlan0 ke_stat 1 1000000 1000000\n");
        
        return -1;
    }

    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);

    if (in_len > 0) {
        if (copy_from_user((uint8_t *)data, wrqu->data.pointer, in_len)) {
            return -EFAULT;
        }
    }

    clear_on_read = data[0]>0;
    period_rpt = data[1];
    period_print = data[2];
    
    printk("%s clear_on_read=%d, period_rpt:%u, period_print:%u\n",
           __func__, clear_on_read, period_rpt, period_print);

    bl_send_dbg_ke_stat_req(bl_hw, clear_on_read, period_rpt, period_print);

    return 0;
}


//**************************************************************************
//****** set cmd table for iwpriv set, parameter in INT type *****
//**************************************************************************
static const struct bl_dev_priv_cmd_node bl_set_sub_int_cmd_table[] = {
    {"edca",         0,       bl_iwpriv_edca_hdl},  
    {"twt_setup",    0,       bl_iwpriv_send_twt_request_hdl},
    {"set_phy_misc", 0,       bl_iwpriv_set_phy_misc_hdl},
    {"ke_stat",      0,       bl_iwpriv_ke_stat_req_hdl},
};

int bl_iwpriv_cmd_set_type_int(struct net_device *dev, 
                                        struct iw_request_info *info, 
                                        union iwreq_data *wrqu, char *extra)
{
    int ret = 0;    
    uint16_t sub_oid = wrqu->data.flags;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    if (bl_mod_params.mp_mode) {
        printk("bl driver in mp_mode\n");
        ret = -EINVAL;
        
        return ret;
    }

    if (sub_oid < sizeof(bl_set_sub_int_cmd_table)/sizeof(struct bl_dev_priv_cmd_node))
        ret = bl_set_sub_int_cmd_table[sub_oid].hdl(dev, info, wrqu, extra);

    return ret;
}

#if defined(CONFIG_BL_SDIO)
int bl_iwpriv_cmd_read_scratch_hdl(struct net_device *dev, 
                                              struct iw_request_info *info, 
                                              union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;
    int ret = 0;
    u32 rd_addr;
    u8 out_buf[128];
    uint32_t scratch_reg = 0x60;
    uint32_t mask = 0x80000000;
    uint32_t read_cnt_max = 1000;
    uint32_t read_cnt = 0;
    const int dump_cnt = 8;
    uint8_t dump_data[8];
    char in_buf[128];
    uint32_t in_len = 0;
    uint16_t out_len = 0;
    int i = 0;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    in_len = wrqu->data.length*4;
    if (copy_from_user(in_buf, wrqu->data.pointer, in_len)) {
        kfree(in_buf);
        
        return -EFAULT;
    }

    wrqu->data.length = 0;

    rd_addr = *(uint32_t *)in_buf;
    if (rd_addr != 0xffffffff && rd_addr != 0x7fffffff && (rd_addr&0x3) > 0)
    {
        printk("%s not 4 bytes align addr:0x%x\r\n", __func__, rd_addr);
        return -EFAULT;
    }
    
    rd_addr = rd_addr|mask;
    for(i=0; i<dump_cnt/2; i++) {
        bl_write_reg(bl_hw, scratch_reg + i, *((uint8_t *)&rd_addr + i));
    }

    //In case some plat, int param is converted.
    if (rd_addr == 0xffffffff || rd_addr == 0x7fffffff) {
        out_len += sprintf(out_buf+out_len, "dumping\r\n");
        dump_fw_criticals(bl_hw);
        out_len += sprintf(out_buf+out_len, "dump done\r\n");
    } else {
        rd_addr = rd_addr&~mask;
        
        while (read_cnt++ < read_cnt_max) {
            msleep_interruptible(1);

            for (i=0; i<dump_cnt/2; i++) {
                bl_read_reg(bl_hw, scratch_reg+i, dump_data+i);
                //printk("%d: 0x%x\r\n", i, dump_data[i]);
            }

            //printk("read_cnt:%d\r\n", read_cnt);
            
            if (*(uint32_t *)dump_data == rd_addr) {
                for (i=0; i<dump_cnt; i++) {
                    bl_read_reg(bl_hw, scratch_reg+i, dump_data+i);
                    //printk("%d: 0x%x\r\n", i, dump_data[i]);
                }

                //printk(KERN_CRIT"read scratch: 0x%x\n", *(uint32_t *)(dump_data+4));
                out_len += sprintf(out_buf+out_len, "0x%08x:0x%08x\r\n",
                                   rd_addr, *(uint32_t *)(dump_data+4));
                break;
            }
        }

        if (read_cnt >= read_cnt_max)
            out_len += sprintf(out_buf+out_len, "failed\r\n");
    }
    
    wrqu->data.length += out_len;
    out_buf[wrqu->data.length] = '\0';
    wrqu->data.length += 1;

    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }
    
    return ret;
}
#endif

#if defined(CONFIG_BL_DYN_DBG)
int bl_iwpriv_dbg_level_hdl(struct net_device *dev, 
                                    struct iw_request_info *info, 
                                    union iwreq_data *wrqu, char *extra)
{
    int ret = 0;
    u8 out_buf[128];
    char in_buf[128];
    uint32_t in_len = 0;
    uint16_t out_len = 0;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    in_len = wrqu->data.length*4;

    if (in_len) {
        if (copy_from_user(in_buf, wrqu->data.pointer, in_len)) {
            kfree(in_buf);
            
            return -EFAULT;
        }

        bl_trace_dyn_level = *(uint32_t *)in_buf;

        if (wrqu->data.length > 1) {
            bl_trace_dyn_module = *((uint32_t *)in_buf + 1);
        }
    }

    wrqu->data.length = 0;
    
    out_len += sprintf(out_buf+out_len, 
                       "bl_trace_dyn_level=0x%x, bl_trace_dyn_module=0x%x\r\n", 
                       bl_trace_dyn_level, bl_trace_dyn_module);

    wrqu->data.length += out_len;
    out_buf[wrqu->data.length] = '\0';
    wrqu->data.length += 1;
    
    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }
    
    return ret;
}
#endif

//**************************************************************************
//****** get cmd table for iwpriv set, parameter in INT type *****
//**************************************************************************
static const struct bl_dev_priv_cmd_node bl_get_sub_int_cmd_table[] = {
#if defined(CONFIG_BL_SDIO)
    {"read_scratch",      0,              bl_iwpriv_cmd_read_scratch_hdl},
#endif
#if defined(CONFIG_BL_DYN_DBG)
    {"dbg_level",         0,              bl_iwpriv_dbg_level_hdl},
#endif
};

int bl_iwpriv_cmd_get_type_int(struct net_device *dev,
                                        struct iw_request_info *info, 
                                        union iwreq_data *wrqu, char *extra)
{
    int ret = 0;    
    uint16_t sub_oid = wrqu->data.flags;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    if (bl_mod_params.mp_mode) {
        printk("bl driver in mp_mode\n");
        ret = -EINVAL;
        
        return ret;
    }

    if (ret < sizeof(bl_get_sub_int_cmd_table)/sizeof(struct bl_dev_priv_cmd_node))
        ret = bl_get_sub_int_cmd_table[sub_oid].hdl(dev, info, wrqu, extra);
   
    return ret;
}


//********************************************************************************
//********private cmd parameter in byte mode *************************************
//********************************************************************************

int bl_iwpriv_ps_hdl(struct net_device *dev, struct iw_request_info *info, 
                          union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    uint16_t sub_oid = wrqu->data.flags;
    int ret = 0;
    uint32_t in_len = 0;
    uint16_t var_type = 0;
    int data = 0;    
    uint8_t ps_mode;

    printk("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p  sub_oid %d\n", 
           __func__, info->cmd, info->flags, wrqu->data.length, 
           wrqu->data.pointer, extra, sub_oid);
           
    if (wrqu->data.length < 1) {
        printk("help: iwpriv wlan0 ps 0/1 \n");
        printk("0-OFF, 1-ON \n");
        ret = -EINVAL;
        
        return ret;
    }

    if (!(bl_hw->version_cfm.features & BIT(MM_FEAT_PS_BIT))) {
        printk("ps was not supported, skip\n");
        
        return -EFAULT;
    }

    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);


    if (in_len > 0) {
        if (copy_from_user((uint8_t *)&data, wrqu->data.pointer, in_len)) {
            return -EFAULT;
        }
    } else {
        return -EINVAL;
    }
    wrqu->data.length = 0;

    ps_mode = (uint8_t)data;

    ret = bl_send_me_set_ps_mode(bl_hw, ps_mode);

    return ret;
}


int bl_iwpriv_scan_chan_hdl(struct net_device *dev, 
                                    struct iw_request_info *info,
                                    union iwreq_data *wrqu, char *extra)
{
    int i;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    uint32_t in_len = 0;
    uint8_t chan_list[14] = {0};

    in_len = wrqu->data.length;

    printk("%s:data->len=%d\n", __func__, wrqu->data.length);

    if (extra) {
        memcpy(chan_list, extra, in_len);
    } else {
        memcpy(chan_list, wrqu->data.pointer, in_len);
    }
    
    for(i=0; i< in_len; i++)
        printk("%02x ", chan_list[i]);
    printk("\n");

    if (in_len > 14) {
        printk("please input: iwpriv wlan0 scan_chan 1 2 3 4 ... 14  at most 14 channel");
    }

    if (in_len) {
        bl_hw->priv_scan.chan_cnt = in_len;
        memcpy(bl_hw->priv_scan.chan_list, chan_list, in_len);
    } else {
        printk("no chan input, use 2.4G full channel list\n");
        
        //use 2.4G full channel list, TODO: ch14
        for (i = 1; i < 14; i++)
            bl_hw->priv_scan.chan_list[i - 1] = i;
        bl_hw->priv_scan.chan_cnt = 13;
    }

    printk("chan list\n");
    for(i=0; i< bl_hw->priv_scan.chan_cnt; i++)
        printk("%02x ", bl_hw->priv_scan.chan_list[i]);
    printk("\n");

    return 0;

}

int bl_iwpriv_scan_ie_hdl(struct net_device *dev, struct iw_request_info *info,
                                union iwreq_data *wrqu, char *extra)
{
    int i;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    uint32_t in_len = 0;
    //TODO: malloc
    u8 scan_ie[256] = {0};

    in_len = wrqu->data.length;

    printk("%s:data->len=%d %d\n", __func__, wrqu->data.length, in_len);

    if (extra) {
        memcpy(scan_ie, extra, in_len);
    } else {
        memcpy(scan_ie, wrqu->data.pointer, in_len);
    }

    for(i=0; i < wrqu->data.length; i++)
        printk("%02x ", scan_ie[i]);
    printk("\n");

    bl_hw->priv_scan.ie_type = 0;

    bl_hw->priv_scan.ie_len = in_len;

    memcpy(bl_hw->priv_scan.ie_buf, scan_ie, in_len);

    printk("priv ie buf\n");
    for(i=0; i< wrqu->data.length; i++)
        printk("%02x ", bl_hw->priv_scan.ie_buf[i]);
    printk("\n");

    return 0;
}

int bl_iwpriv_scan_hdl(struct net_device *dev, struct iw_request_info *info,
                             union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    u8 enable =  0;
    uint32_t in_len = 0;

    in_len = wrqu->data.length;

    printk("%s:data->len=%d %d\n", __func__, wrqu->data.length, in_len);

    if (wrqu->data.length != 1) {
        printk("please input:iwpriv wlan0 scan 0/1  0 is deinit scan settings, 1 is trigger STA scan");
        
        return -1;
    }

    if (extra)
        enable = extra[0];
    else
        enable = *(u8 *)(wrqu->data.pointer);
    printk("%s enable = %d\n",__func__, enable);

    bl_send_user_scanu_req(bl_hw, vif, enable);

    return 0;
}

int bl_iwpriv_cmw_filter_hdl(struct net_device *dev, struct iw_request_info *info, 
                                    union iwreq_data *wrqu, char *extra)
{
    uint16_t sub_oid = wrqu->data.flags;
    int ret = 0;
    uint32_t in_len = 0;
    uint16_t var_type = 0;
    uint8_t data[8];    

    printk("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p  sub_oid %d\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra, sub_oid);
           
    if(wrqu->data.length != 8) {
        printk("help: iwpriv wlan0 cmw_filter <discard_cnt> <max_cnt> <eth addr 0> <eth addr 1> <eth addr 2> <eth addr 3> <eth addr 4> <eth addr 5> \
                example: iwpriv wlan0 cmw_filter 4 5 0x00 0x01 0x02 0x03 0x04 0x05\n");
        ret = -EINVAL;
        
        return ret;
    }
    
    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);

    if (in_len > 0) {
        if (copy_from_user((uint8_t *)&data, wrqu->data.pointer, in_len)) {
            return -EFAULT;
        }
    }
    
    wrqu->data.length = 0;

    icmp_discard_thr = (uint8_t)data[0];
    icmp_period_thr = (uint8_t)data[1];
    cmw_eth_addr[0] = (uint8_t)data[2];
    cmw_eth_addr[1] = (uint8_t)data[3];
    cmw_eth_addr[2] = (uint8_t)data[4];
    cmw_eth_addr[3] = (uint8_t)data[5];
    cmw_eth_addr[4] = (uint8_t)data[6];
    cmw_eth_addr[5] = (uint8_t)data[7];

    if (icmp_discard_thr > icmp_period_thr)
        icmp_discard_thr = icmp_period_thr;

    printk("%s, icmp_discard_thr:%d, icmp_period_thr:%d, eth:%02x:%02x:%02x:%02x:%02x:%02x\n",
           __func__,
           icmp_discard_thr, icmp_period_thr, cmw_eth_addr[0], cmw_eth_addr[1], 
           cmw_eth_addr[2], cmw_eth_addr[3], cmw_eth_addr[4], cmw_eth_addr[5]);
           
    return 0;
}


int bl_iwpriv_twt_teardown_hdl(struct net_device *dev,
                                        struct iw_request_info *info, 
                                        union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    uint16_t sub_oid = wrqu->data.flags;
    int ret = 0;
    uint32_t in_len = 0;
    uint16_t var_type = 0;
    int data = 0, error = 1;    
    struct twt_teardown_req twt_teardown;
    struct twt_teardown_cfm twt_teardown_cfm;

    printk("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p  sub_oid %d\n", 
           __func__, info->cmd, info->flags, wrqu->data.length, 
           wrqu->data.pointer, extra, sub_oid);
           
    if(wrqu->data.length != 1) {
        printk("help: iwpriv wlan0 twt_teardown [flow_id] \n");
        ret = -EINVAL;
        
        return ret;
    }
    
    var_type = get_args_type(dev, info->cmd);
    in_len = wrqu->data.length;
    
    if (var_type == IW_PRIV_TYPE_INT)
        in_len *= sizeof(int32_t);


    if (in_len > 0) {
        if (copy_from_user((uint8_t *)&data, wrqu->data.pointer, in_len)) {
        
            return -EFAULT;
        }
    } else {
        return -EINVAL;
    }
    wrqu->data.length = 0;

    memset(&twt_teardown, 0, sizeof(twt_teardown));

    twt_teardown.neg_type = 0;
    twt_teardown.all_twt = 0;
    twt_teardown.vif_idx = vif->vif_index;
    twt_teardown.id = (uint8_t)data;

    // Forward the request to the LMAC
    if ((error = bl_send_twt_teardown(bl_hw, &twt_teardown, &twt_teardown_cfm)) != 0)
        return error;

    // Check the status
//    if (twt_teardown_cfm.status != CO_OK)
//        return -EIO;

    return 0;
}


//**************************************************************************
//****** set cmd table for iwpriv set *****
//**************************************************************************
static const struct bl_dev_priv_cmd_node bl_set_sub_cmd_table[] = {
    {"ps",           0,      bl_iwpriv_ps_hdl},
    {"scan",         0,      bl_iwpriv_scan_hdl},
    {"scan_chan",    0,      bl_iwpriv_scan_chan_hdl},
    {"scan_ie",      0,      bl_iwpriv_scan_ie_hdl},
    {"twt_teardown", 0,      bl_iwpriv_twt_teardown_hdl},
    {"cmw_filter",   0,      bl_iwpriv_cmw_filter_hdl},
};

int bl_iwpriv_cmd_set(struct net_device *dev, struct iw_request_info *info, 
                             union iwreq_data *wrqu, char *extra)
{
    int ret = 0;    
    uint16_t sub_oid = wrqu->data.flags;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, 
           wrqu->data.length, wrqu->data.pointer, extra);

    if (bl_mod_params.mp_mode) {
        printk("bl driver in mp_mode\n");
        ret = -EINVAL;
        
        return ret;
    }

    if (sub_oid < sizeof(bl_set_sub_cmd_table)/sizeof(struct bl_dev_priv_cmd_node))
        ret = bl_set_sub_cmd_table[sub_oid].hdl(dev, info, wrqu, extra);

    return ret;
}

//*************************************************************************************
//********** get cmd ****************
//*************************************************************************************

int bl_iwpriv_get_country_code_hdl(struct net_device *dev,
              struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    int ret = 0;
    u8 out_buf[128];

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer,extra);

    wrqu->data.length = 0;

    wrqu->data.length += sprintf(out_buf, 
                                 "country code : %s\n", bl_hw->country_code);
    
    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }

    return ret;
}


int bl_iwpriv_get_mac_addr_hdl(struct net_device *dev, 
                                         struct iw_request_info *info,
                                         union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    int ret = 0;
    u8 out_buf[128];

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer,extra);

    wrqu->data.length = 0;
    memcpy(out_buf, bl_hw->wiphy->perm_addr, 6);

    wrqu->data.length += 6;
    
    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }

    return ret;
}

int bl_iwpriv_get_intf_status_hdl(struct net_device *dev, 
                                         struct iw_request_info *info,
                                         union iwreq_data *wrqu, char *extra)
{
    //struct bl_vif *bl_vif = netdev_priv(dev);

    int ret = 0;
    char * in_buf = NULL;
    u8 out_buf[128];

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer,extra);

    wrqu->data.length = 0;

    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }
  
    if (in_buf != NULL)
        kfree(in_buf);

    return ret;
}

int bl_iwpriv_get_rssi_hdl(struct net_device *dev, struct iw_request_info *info,
                                 union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    int ret = 0;
    u8 out_buf[128];

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer,extra);
           
    wrqu->data.length = 0;

    if (bl_vif->sta.ap == NULL) {
        printk("WARN:STA doesn't in connected status.\n");
        
        return -EFAULT;
    }

    ret = bl_send_rssi_read_req(bl_hw, bl_vif->vif_index,
                                (struct mm_get_rssi_cfm *)out_buf);
    if (ret) {
        ret = -EFAULT;
        
        return ret;
    }

    printk("rssi:%d, tx_retry:%u, tx_total:%u\n", 
            ((struct mm_get_rssi_cfm *)out_buf)->rssi_value,
            ((struct mm_get_rssi_cfm *)out_buf)->retry,
            ((struct mm_get_rssi_cfm *)out_buf)->total);
    
    wrqu->data.length += 1;//sizeof(struct mm_get_rssi_cfm);

    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }

    return ret;
}


int bl_iwpriv_read_efuse_hdl(struct net_device *dev, 
                                     struct iw_request_info *info,
                                     union iwreq_data *wrqu, char *extra)
{
    struct bl_vif *bl_vif = netdev_priv(dev);
    struct bl_hw *bl_hw = bl_vif->bl_hw;

    int ret = 0;
    u8 out_buf[128];

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer,extra);

    wrqu->data.length = 0;
    
    ret = bl_send_efuse_read_req(bl_hw, (struct mm_read_efuse_cfm *)out_buf);
    
    if (ret) {
        ret = -EFAULT;
        
        return ret;
    }
    wrqu->data.length += sizeof(struct mm_read_efuse_cfm);

    if(extra) {
        memcpy(extra, out_buf, wrqu->data.length);
    } else {
        if (copy_to_user(wrqu->data.pointer, out_buf, wrqu->data.length))
            ret = -EFAULT;
    }
  
    return ret;
}

//**************************************************************************
//****** get cmd table for iwpriv get *****
//**************************************************************************
static const struct bl_dev_priv_cmd_node bl_get_sub_cmd_table[] = {
    {"country_code",      0,          bl_iwpriv_get_country_code_hdl},
    {"mac_addr",          0,          bl_iwpriv_get_mac_addr_hdl},
    {"status",            0,          bl_iwpriv_get_intf_status_hdl},
    {"rssi",              0,          bl_iwpriv_get_rssi_hdl},
    {"read_efuse",        0,          bl_iwpriv_read_efuse_hdl},
};


int bl_iwpriv_cmd_get(struct net_device *dev, struct iw_request_info *info, 
                             union iwreq_data *wrqu, char *extra)
{
    int ret = 0;    
    uint16_t sub_oid = wrqu->data.flags;

    BL_DBG("%s, cmd=0x%x, flags=0x%x, wrqu->len=%d, buf=%p, extra=%p\n", 
           __func__, info->cmd, info->flags, wrqu->data.length,
           wrqu->data.pointer, extra);

    if (bl_mod_params.mp_mode) {
        printk("bl driver in mp_mode\n");
        ret = -EINVAL;
        
        return ret;
    }

    if (sub_oid < sizeof(bl_get_sub_cmd_table)/sizeof(struct bl_dev_priv_cmd_node))
        ret = bl_get_sub_cmd_table[sub_oid].hdl(dev, info, wrqu, extra);

    return ret;
}

int bl_iwpriv_ble_hci_cmd_hdl(struct net_device *dev, struct iw_request_info *info,
                                       union iwreq_data *wrqu, char *extra)
{
    int i;
    struct bl_vif *vif = netdev_priv(dev);
    struct bl_hw *bl_hw = vif->bl_hw;
    uint32_t in_len = 0;
    #define HCI_MAX_LEN  512
    u8 hci_cmd[HCI_MAX_LEN] = {0};

    in_len = wrqu->data.length;

    BL_DBG("%s:data->len=%d %d\n", __func__, wrqu->data.length, in_len);

    if (bl_hw->mod_params->mp_mode) {
        printk("%s, not support BLE hci in mp_mode\n", __func__);
        
        return -EINVAL;
    }

    #if !defined(CONFIG_BL_BTSDU) && !defined(CONFIG_BL_BTUSB)
    printk("%s, not support BLE hci on WiFi only driver\n", __func__);
    
    return -EINVAL;
    #endif


    if (in_len == 0) {
        printk("%s, exiting hci cmd mode\n", __func__);

        bl_hw->bl_hci_on = 0;

        return 0;
    }
    else if (in_len >= HCI_MAX_LEN)
    {
        printk("%s, too long in_len:%d\r\n", __func__, in_len);

        return -EINVAL;
    }
    else 
    {
        bl_hw->bl_hci_on = 1;
    }
    
    if (extra) {
        memcpy(hci_cmd, extra, in_len);
    } else {
        memcpy(hci_cmd, wrqu->data.pointer, in_len);
    }

    BL_DBG("iwpriv dump:\n");
    for(i=0; i < wrqu->data.length; i++)
        BL_DBG("%02x ", hci_cmd[i]);
    BL_DBG(".\n");

    if (bl_hw->hdev) {
        #if defined(CONFIG_BL_BTSDU) || defined(CONFIG_BL_BTUSB)
        struct sk_buff *skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_KERNEL);

        if (!skb) {
            printk("%s, no mem\r\n", __func__);
            
            return -ENOMEM;
        }

        BL_DBG("%s %u, in_len:%u\r\n", __func__, __LINE__, in_len);
        
        hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;
        //remove 1st byte in hci_cmd, which is HCI_COMMAND_PKT, will be added in 
        //send_frame.
        memcpy(skb->data, &hci_cmd[1], in_len-1);
        skb_put(skb, in_len-1);
        
        #if defined CONFIG_BL_BTSDU
        if (btsdio_send_frame(bl_hw->hdev, skb) < 0) {
            printk("%s, fail send hci frame\n", __func__);
            
            dev_kfree_skb_any(skb);

            return -EINVAL;
        }
        #elif defined CONFIG_BL_BTUSB
        if (btusb_send_frame(bl_hw->hdev, skb) < 0) {
            printk("%s, fail send hci frame\n", __func__);
            
            dev_kfree_skb_any(skb);

            return -EINVAL;
        }
        #endif
        #endif
    }
    else 
    {
        bl_nl_broadcast_event(bl_hw, BL_EVENT_ID_HCI_MSG, 
                              (u8_l *)hci_cmd, in_len);
    }
    
    return 0;
}

