/**
 ******************************************************************************
 *
 *  @file bl_cfgfile.h
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


#ifndef _BL_CFGFILE_H_
#define _BL_CFGFILE_H_

#include "bl_iwpriv.h"

/*
 * Definitions for RF caldata processing
 */
#define RFTLV_SIZE_TYPE                (2)
#define RFTLV_SIZE_LENGTH              (2)
#define RFTLV_SIZE_TL                  (RFTLV_SIZE_TYPE + RFTLV_SIZE_LENGTH)
 
#define RFTLF_TYPE_MAGIC_FW_HEAD1      (0x4152415046524C42)
#define RFTLF_TYPE_MAGIC_FW_HEAD2      (0x6B3162586B44364F)
#define RFTLV_TYPE_INVALID             0x0000

#define RFTLV_TYPE_XTAL_MODE           0x0001
#define RFTLV_MAXLEN_XTAL_MODE         (2)

#define RFTLV_TYPE_XTAL                0x0002
#define RFTLV_MAXLEN_XTAL              (20)

#define RFTLV_TYPE_PWR_MODE            0x0003
#define RFTLV_MAXLEN_PWR_MODE          (2)

#define RFTLV_TYPE_PWR_TABLE_11B       0x0005
#define RFTLV_MAXLEN_PWR_TABLE_11B     (4)

#define RFTLV_TYPE_PWR_TABLE_11G       0x0006
#define RFTLV_MAXLEN_PWR_TABLE_11G     (8)

#define RFTLV_TYPE_PWR_TABLE_11N       0x0007
#define RFTLV_MAXLEN_PWR_TABLE_11N     (8)

#define RFTLV_TYPE_PWR_TABLE_11N_HT40       0x0009
#define RFTLV_MAXLEN_PWR_TABLE_11N_HT40     (8)

#define RFTLV_TYPE_PWR_TABLE_11AC_VHT20     0x000A
#define RFTLV_MAXLEN_PWR_TABLE_11AC_VHT20   (10)

#define RFTLV_TYPE_PWR_TABLE_11AC_VHT40     0x000B
#define RFTLV_MAXLEN_PWR_TABLE_11AC_VHT40   (10)

#define RFTLV_TYPE_PWR_TABLE_11AC_VHT80     0x000C
#define RFTLV_MAXLEN_PWR_TABLE_11AC_VHT80   (10)

#define RFTLV_TYPE_PWR_TABLE_11AX_HE20      0x000D
#define RFTLV_MAXLEN_PWR_TABLE_11AX_HE20    (12)

#define RFTLV_TYPE_PWR_TABLE_11AX_HE40      0x000E
#define RFTLV_MAXLEN_PWR_TABLE_11AX_HE40    (12)

#define RFTLV_TYPE_PWR_TABLE_11AX_HE80      0x000F
#define RFTLV_MAXLEN_PWR_TABLE_11AX_HE80    (12)

#define RFTLV_TYPE_PWR_TABLE_11AX_HE160     0x0010
#define RFTLV_MAXLEN_PWR_TABLE_11AX_HE160   (12)

#define RFTLV_TYPE_PWR_OFFSET           0x0008
#define RFTLV_MAXLEN_PWR_OFFSET         (14)

#define RFTLV_TYPE_PWR_OFFSET_LP        0x0011
#define RFTLV_MAXLEN_PWR_OFFSET_LP      (14)

#define RFTLV_TYPE_EN_TCAL              0x0020
#define RFTLV_MAXLEN_EN_TCAL            (1)

#define RFTLV_TYPE_LINEAR_OR_FOLLOW     0x0021
#define RFTLV_MAXLEN_LINEAR_OR_FOLLOW   (1)

#define RFTLV_TYPE_TCHANNELS            0x0022
#define RFTLV_MAXLEN_TCHANNELS          (10)

#define RFTLV_TYPE_TCHANNEL_OS          0x0023
#define RFTLV_MAXLEN_TCHANNEL_OS        (10)

#define RFTLV_TYPE_TCHANNEL_OS_LOW      0x0024
#define RFTLV_MAXLEN_TCHANNEL_OS_LOW    (10)

#define RFTLV_TYPE_TROOM_OS             0x0025
#define RFTLV_MAXLEN_TROOM_OS           (2)

#define RFTLV_TYPE_PWR_TABLE_BLE        0x0030
#define RFTLV_MAXLEN_PWR_TABLE_BLE      (4)

#define RFTLV_TYPE_PWR_OFFSET_BLE       0x0031
#define RFTLV_MAXLEN_BLE_PWR_OFFSET     (5)

#define RFTLV_TYPE_COUNTRY_CODE         0x0050
#define RFTLV_MAXLEN_COUNTRY_CODE       (2)

#define RFTLV_TYPE_PWR_TABLE_BT         0x0032
#define RFTLV_MAXLEN_PWR_TABLE_BT       (12)

#define RFTLV_TYPE_PWR_TABLE_ZIGBEE     0x0033
#define RFTLV_MAXLEN_PWR_TBALE_ZIGBEE   (4)

#define RFTLV_TYPE_EN_TCAPCAL           0x0060
#define RFTLV_MAXLEN_EN_TCAPCAL         (1)

#define RFTLV_TYPE_TCAP_TSEN            0x0061
#define RFTLV_MAXLEN_TCAP_TSEN          (10)

#define RFTLV_TYPE_TCAP_CAPCODE         0x0062
#define RFTLV_MAXLEN_TCAP_CAPCODE       (11)

#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH1       0x0070
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH2       0x0071
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH3       0x0072
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH4       0x0073
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH5       0x0074
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH6       0x0075
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH7       0x0076
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH8       0x0077
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH9       0x0078
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH10      0x0079
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH11      0x007a
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH12      0x007b
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH13      0x007c
#define RFTLV_TYPE_POWER_LIMIT_2G_EXT_CH14      0x007d
#define RFTLV_MAXLEN_PWR_LIMIT_EXT              (12)



#define CFG_FLIE_PWR_TAG_LEN     (strlen("RATE_PWR="))
#define CFG_FLIE_WIFI_PWR_VALUE_LEN     (strlen("CN") + RATE_MAX * 3)
#define CFG_FLIE_BLE_PWR_CNT            (1)
#define CFG_FLIE_BLE_PWR_VALUE_LEN      (strlen("CN") +  CFG_FLIE_BLE_PWR_CNT * 3)
#define CFG_FLIE_BT_PWR_CNT             (3)
#define CFG_FLIE_BT_PWR_VALUE_LEN       (strlen("CN") +  CFG_FLIE_BT_PWR_CNT * 3)


enum {
    RATE_B_1M = 0,
    RATE_B_2M,
    RATE_B_5M,
    RATE_B_11M,
    RATE_G_6M = 4,
    RATE_G_9M,
    RATE_G_12M,
    RATE_G_18M,
    RATE_G_24M,
    RATE_G_36M,
    RATE_G_48M,
    RATE_G_54M,
    RATE_HT20_MCS0 = 12,
    RATE_HT20_MCS1,
    RATE_HT20_MCS2,
    RATE_HT20_MCS3,
    RATE_HT20_MCS4,
    RATE_HT20_MCS5,
    RATE_HT20_MCS6,
    RATE_HT20_MCS7,
    RATE_HT40_MCS0,
    RATE_HT40_MCS1,
    RATE_HT40_MCS2,
    RATE_HT40_MCS3,
    RATE_HT40_MCS4,
    RATE_HT40_MCS5,
    RATE_HT40_MCS6,
    RATE_HT40_MCS7,
    RATE_HE20_MCS0 = 28,
    RATE_HE20_MCS1,
    RATE_HE20_MCS2,
    RATE_HE20_MCS3,
    RATE_HE20_MCS4,
    RATE_HE20_MCS5,
    RATE_HE20_MCS6,
    RATE_HE20_MCS7,
    RATE_HE20_MCS8,
    RATE_HE20_MCS9,
    RATE_HE40_MCS0,
    RATE_HE40_MCS1,
    RATE_HE40_MCS2,
    RATE_HE40_MCS3,
    RATE_HE40_MCS4,
    RATE_HE40_MCS5,
    RATE_HE40_MCS6,
    RATE_HE40_MCS7,
    RATE_HE40_MCS8,
    RATE_HE40_MCS9 = 47,
    RATE_MAX
};

#define COUNTRY_CODE_LEN                3

struct country_power_info {
    u8      country_code[3];
    int8_t  ble_pwr;
    int8_t  bt_pwr[3];
    int8_t  rate_pwr[RATE_MAX];
};

/** Region code mapping */
struct region_code_t {
	/** Region */
	u8 region[COUNTRY_CODE_LEN];
};

/*
 * Structure used to retrieve information from the Config file used at Initialization time
 */
struct bl_conf_file {
    u8 mac_addr[ETH_ALEN];
    u8 country_code[3];
    u8 pwr_table_num;
    struct country_power_info * pwr_table;
};

/*
 * Structure used to retrieve information from the PHY Config file used at Initialization time
 */
struct bl_phy_conf_file {
    struct phy_trd_cfg_tag trd;
    struct phy_karst_cfg_tag karst;
    struct phy_cataxia_cfg_tag cataxia;
};
const char *bl_find_tag(const u8 *file_data, unsigned int file_size,
                           const char *tag_name, unsigned int tag_len);
u8 is_eu_country(u8 *country_code);
u8 is_valid_country_code(char *alpha2);
int bl_parse_cal_configfile(struct bl_hw *bl_hw, const char *filename,
                                  struct mm_cal_cfg_req *cal_cfg_req);
//int bl_parse_configfile(struct bl_hw *bl_hw, const char *filename,
//                            struct bl_conf_file *config);
int bl_parse_phy_configfile(struct bl_hw *bl_hw, const char *filename,
                                   struct bl_phy_conf_file *config, int path);
int bl_parse_country_tx_pwr_configfile(struct bl_hw *bl_hw, const char *filename);
int bl_find_rftlv(u8_l * buff_addr, u16_l buff_len, u16_l type, u16_l len, u8_l *value);
int bl_country_pwr_update(struct bl_hw * bl_hw, char * country);
int bl_parse_confg_kv(uint8_t *cfg_buf, uint32_t cfg_buf_len,
                            struct mm_cal_cfg_req *cal_cfg_req,
                            struct cfg_kv_t *kv, int cfg_num);
uint32_t bl_convert_cal_to_tlv(uint8_t *hex_buf, uint32_t max_len);

#endif /* _BL_CFGFILE_H_ */
