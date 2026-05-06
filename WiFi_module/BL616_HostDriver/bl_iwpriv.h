/**
 ******************************************************************************
 *
 *  @file bl_iwpriv.h
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

#ifndef _BL_IWPRIV_H_
#define _BL_IWPRIV_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <linux/wireless.h>
#include <net/iw_handler.h>

#include "bl_defs.h"

/*
 # Enum
 ****************************************************************************************
 */
/* Device private IOCTL */
#define BL_DEV_PRIV_IOCTL_DEFAULT    (SIOCDEVPRIVATE)

/* odd number is get command, even number is set command */
#define BL_IOCTL_VERSION        (SIOCIWFIRSTPRIV + 1)
#define BL_IOCTL_WMMCFG         (SIOCIWFIRSTPRIV + 2)
#define BL_IOCTL_SETRATE        (SIOCIWFIRSTPRIV + 8)

#define BL_IOCTL_SET_CMD_TYPE_INT        (SIOCIWFIRSTPRIV + 10)
#define BL_IOCTL_GET_CMD_TYPE_INT        (SIOCIWFIRSTPRIV + 11)

//#define BL_IOCTL_SETPROREQPRIVIES        (SIOCIWFIRSTPRIV + 10)
//#define BL_IOCTL_PRIVIESON      (SIOCIWFIRSTPRIV + 12)
#define BL_IOCTL_TEMP           (SIOCIWFIRSTPRIV + 13)
#define BL_IOCTL_RW_COEX_PARAM  (SIOCIWFIRSTPRIV + 15)
#define BL_IOCTL_RW_MEM         (SIOCIWFIRSTPRIV + 17)

#define BL_IOCTL_SET_CMD        (SIOCIWFIRSTPRIV + 18)
#define BL_IOCTL_GET_CMD        (SIOCIWFIRSTPRIV + 19)

#define IWPRIV_OUT_BUF_LEN 2048

#define VALID_RF_PARA  0x5a

#define IS_VAR_LEN 0xffff0000U
#define SHORT_WAIT_MS 50
#define LONG_WAIT_MS 200
#define NONE 0
#define DEFAULT_TX_DUTY 50

#define MFG_PWR_OFFSET_MAX (15)
#define MFG_PWR_OFFSET_MIN (-16)


#ifdef CONFIG_BL_MP
#define BL_IOCTL_MP_CFG         (SIOCIWFIRSTPRIV + 3)
#define BL_IOCTL_MP_MS          (SIOCIWFIRSTPRIV + 4)
#define BL_IOCTL_MP_HELP        (SIOCIWFIRSTPRIV + 5)
#define BL_IOCTL_MP_MG          (SIOCIWFIRSTPRIV + 7)
#define BL_IOCTL_MP_IND         (SIOCIWFIRSTPRIV + 9)
//RTOS
//#define BL_IOCTL_MP_CAL_CFG     (SIOCIWFIRSTPRIV + 11)

//ms sub cmds
enum {
    BL_IOCTL_MP_UNICAST_TX,
    BL_IOCTL_MP_11b_RATE,
    BL_IOCTL_MP_11g_RATE,
    BL_IOCTL_MP_11n_RATE,
    BL_IOCTL_MP_11ax_RATE,
    BL_IOCTL_MP_TX,
    BL_IOCTL_MP_SET_PKT_FREQ,
    BL_IOCTL_MP_SET_PKT_LEN,
    BL_IOCTL_MP_SET_CHANNEL,
    BL_IOCTL_MP_SET_CWMODE,
    BL_IOCTL_MP_SET_PWR,
    BL_IOCTL_MP_SET_TXDUTY,
    BL_IOCTL_MP_SET_PWR_OFT_EN,
    BL_IOCTL_MP_SET_XTAL_CAP,
    BL_IOCTL_MP_SET_PRIV_PARAM,
    BL_IOCTL_MP_WR_MEM,
    BL_IOCTL_MP_RD_MEM,
    BL_IOCTL_MP_BTBLE_TX,
    BL_IOCTL_MP_BTBLE_RX,
};

//mg sub cmds
enum {
    BL_IOCTL_MP_HELLO,
    BL_IOCTL_MP_UNICAST_RX,
    BL_IOCTL_MP_RX,
    // BL_IOCTL_MP_PM,  //MCU power control like hibernate, sleep, wakeup
    // BL_IOCTL_MP_RESET,
    BL_IOCTL_MP_GET_FW_VER,
    BL_IOCTL_MP_GET_BUILD_INFO,
    BL_IOCTL_MP_GET_POWER,
    BL_IOCTL_MP_GET_FREQ,
    BL_IOCTL_MP_GET_TX_STATUS,
    BL_IOCTL_MP_GET_PKT_FREQ,
    BL_IOCTL_MP_GET_XTAL_CAP,
    BL_IOCTL_MP_GET_CWMODE,
    BL_IOCTL_MP_GET_TX_DUTY,
    BL_IOCTL_MP_EFUSE_RD,
    BL_IOCTL_MP_EFUSE_WR,
    BL_IOCTL_MP_EFUSE_CAP_RD,
    BL_IOCTL_MP_EFUSE_CAP_WR,
    BL_IOCTL_MP_EFUSE_PWR_OFT_RD,
    BL_IOCTL_MP_EFUSE_PWR_OFT_WR,
    BL_IOCTL_MP_EFUSE_MAC_ADR_RD,
    BL_IOCTL_MP_EFUSE_MAC_ADR_WR,
    BL_IOCTL_MP_GET_TEMPERATURE,
    BL_IOCTL_MP_DUMP_PHYRF,
    BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_RD,
    BL_IOCTL_MP_EFUSE_BZ_PWR_OFT_WR,
    BL_IOCTL_MP_ATCMD,
};


// normal sub set cmd (type-byte mode)
enum {
    BL_IOCTL_SUB_PS,
    BL_IOCTL_SUB_SCAN,
    BL_IOCTL_SUB_SCAN_CHAN,
    BL_IOCTL_SUB_SCAN_IE,
    BL_IOCTL_SUB_TWT_TEARDOWN,
    BL_IOCTL_SUB_CMW_FILTER,
};

// normal sub get cmd (type-byte mode)
enum {
    BL_IOCTL_SUB_COUNTRY_CODE,
    BL_IOCTL_SUB_MAC_ADDR,
    BL_IOCTL_SUB_STATUS,
    BL_IOCTL_SUB_RSSI,
    BL_IOCTL_SUB_READ_EFUSE,
};

// normal sub set cmd (type-int mode)
enum {
    BL_IOCTL_SUB_EDCA,
    BL_IOCTL_SUB_TWT_SETUP,
    BL_IOCTL_SUB_SET_PHY_MISC,
    BL_IOCTL_SUB_KE_STAT_REQ,
};

// normal sub get cmd (type-int mode)
enum {
#if defined(CONFIG_BL_SDIO)
    BL_IOCIL_SUB_READ_SCRATCH,
#endif
#if defined(CONFIG_BL_DYN_DBG)
    BL_IOCIL_SUB_DBG_LEVEL,
#endif
    BL_IOCTL_SUB_DUMMY,
};

//Interface to ioctl. Same as bl_util.c
#define BL_UTIL_CMD_VERSION        0x6001
#define BL_UTIL_CMD_TEMP           0x6002
#define BL_UTIL_CMD_SCAN           0x6003
#define BL_UTIL_CMD_SCAN_CHAN      0x6004
#define BL_UTIL_CMD_SCAN_IE        0x6005

#define BL_UTIL_CMD_GET_COUNTRY_CODE     0x6006
#define BL_UTIL_CMD_GET_MAC_ADDR         0x6008
#define BL_UTIL_CMD_GET_STATUS           0x6009
#define BL_UTIL_CMD_GET_RSSI             0x600a
#define BL_UTIL_CMD_READ_EFUSE           0x600b
#define BL_UTIL_CMD_ATCMD                0x600c
#define BL_UTIL_CMD_HCI_CMD              0x600d

typedef int (*parse_func_t)(uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf, uint32_t *out_len);

struct iwpriv_sub_cmd {
    union {
        char    *cname;
        int32_t iname;
    } sc_name;    
    uint32_t    sc_len;
    uint32_t    sc_var_len;
    char        *sc_helper;
    parse_func_t    sc_var_parser;

    char        *mfg_cmd_name;
    uint32_t    mfg_cmd_len;

    uint32_t    ind_wait_ms;
    char        *ind_exp;
};

struct iwpriv_cmd {
    char     *name;
    struct   iwpriv_sub_cmd *sub_cmd;
    uint32_t sub_cmd_num;
};

int bl_iwpriv_mp_ms_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_mp_mg_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_help_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_mp_ind_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_mp_load_caldata_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);

#endif

enum {
    CFG_CAPCODE_ID,
    CFG_PWR_OFT_ID,
    CFG_MAC_ID,
    CFG_EN_TCAL_ID,
    CFG_TCHANNEL_OS_ID,
    CFG_TCHANNEL_OS_LOW_ID,
    CFG_LINEAR_ID,
    CFG_TCHANNELS_ID,
    CFG_TROOM_OS_ID,

    CFG_XTAL_MODE_ID,
    CFG_XTAL_CAP_ID,
    CFG_PWR_MODE_ID,
    
    CFG_PWR_11B_ID,
    CFG_PWR_11G_ID,
    CFG_PWR_11N_HT20_ID,
    CFG_PWR_11N_HT40_ID,
    CFG_PWR_11AX_HE20_ID,
    CFG_PWR_11AX_HE40_ID,
    CFG_PWR_11AX_HE80_ID,
    CFG_PWR_11AX_HE160_ID,
    
    CFG_PWR_BLE_ID,
    CFG_PWR_BT_ID,
    CFG_PWR_OFT_BZ_ID,

    CFG_COUNTRY_CODE_ID
};

struct cfg_kv_t {
    char        *key;
    uint32_t    id;
};

struct bl_dev_priv_cmd_node {
    char * name;
    uint32_t cmd_id;
    int (*hdl) (struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
};

extern int cfg_num;
extern struct cfg_kv_t cfg_kv[];

int read_file(uint8_t *file_name, uint8_t *buf, uint16_t buf_len);

int bl_iwpriv_rw_mem_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_rw_coex_param_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_wmmcfg_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_ver_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_atcmd_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_temp_read_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_set_rate_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_mp_cfg_caldata_hdl(struct net_device *dev, struct iw_request_info *info, 
                                           union iwreq_data *wrqu, char *extra);

int bl_iwpriv_cmd_set_type_int(struct net_device *dev, struct iw_request_info *info, 
                         union iwreq_data *wrqu, char *extra);
int bl_iwpriv_cmd_get_type_int(struct net_device *dev, struct iw_request_info *info, 
                         union iwreq_data *wrqu, char *extra);

int bl_iwpriv_cmd_set(struct net_device *dev, struct iw_request_info *info, 
                         union iwreq_data *wrqu, char *extra);
int bl_iwpriv_cmd_get(struct net_device *dev, struct iw_request_info *info, 
                         union iwreq_data *wrqu, char *extra);

int bl_caldata_cfg_file_handle(struct bl_hw *bl_hw, char *file_path);
int bl_country_pwr_file_handle(struct bl_hw *bl_hw, char *file_path);
int bl_iwpriv_scan_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_scan_ie_hdl(struct net_device *dev, struct iw_request_info *info, union iwreq_data *wrqu, char *extra);
int bl_iwpriv_scan_chan_hdl(struct net_device *dev, struct iw_request_info *info,union iwreq_data *wrqu, char *extra);

// get cmd
int bl_iwpriv_get_country_code_hdl(struct net_device *dev, struct iw_request_info *info,union iwreq_data *wrqu, char *extra);

int bl_iwpriv_get_mac_addr_hdl(struct net_device *dev, struct iw_request_info *info,union iwreq_data *wrqu, char *extra);

int bl_iwpriv_get_intf_status_hdl(struct net_device *dev, struct iw_request_info *info,union iwreq_data *wrqu, char *extra);

int bl_iwpriv_get_rssi_hdl(struct net_device *dev, struct iw_request_info *info,union iwreq_data *wrqu, char *extra);

int bl_iwpriv_read_efuse_hdl(struct net_device *dev, struct iw_request_info *info,union iwreq_data *wrqu, char *extra);
int bl_iwpriv_ble_hci_cmd_hdl(struct net_device *dev, struct iw_request_info *info,
                                       union iwreq_data *wrqu, char *extra);


#endif
