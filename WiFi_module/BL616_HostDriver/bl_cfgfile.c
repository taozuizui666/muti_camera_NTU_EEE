/**
 ****************************************************************************************
 *
 *  @file bl_configparse.c
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

#include <linux/firmware.h>
#include <linux/if_ether.h>
#include <linux/ctype.h>

#include "bl_defs.h"
#include "bl_iwpriv.h"
#include "bl_cfgfile.h"
#include "bl_lmac_types.h"
#include "bl_msg_tx.h"
#include "bl_iwpriv.h"

extern  int8_t bl_txpwr_vs_rate_table[PHY_TRPC_MODE_MAX][PHY_TRPC_NUM_MCS];

static struct region_code_t normal_country_code[] = {
    {"US"}, /* US FCC      */
    {"CA"}, /* IC Canada   */
    {"SG"}, /* Singapore   */
    {"EU"}, /* ETSI        */
    {"AU"}, /* Australia   */
    {"KR"}, /* Republic Of Korea */
    {"JP"}, /* Japan       */
    {"CN"}, /* China       */
    {"BR"}, /* Brazil      */
    {"RU"}, /* Russia      */
    {"IN"}, /* India       */
    {"MY"}, /* Malaysia    */
};

/** Country code for ETSI */
static u8 eu_country_code_table[][COUNTRY_CODE_LEN] = {
    "AL", "AD", "AT", "AU", "BY", "BE", "BA", "BG", "HR", "CY", "CZ", "DK",
    "EE", "FI", "FR", "MK", "DE", "GR", "HU", "IS", "IE", "IT", "KR", "LV",
    "LI", "LT", "LU", "MT", "MD", "MC", "ME", "NL", "NO", "PL", "RO", "RU",
    "SM", "RS", "SI", "SK", "ES", "SE", "CH", "TR", "UA", "UK", "GB"};


//TODO: user set the country code without power limit, use these driver default settings.
static struct country_power_info bl_country_default[12] =
{
    {   "US",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },

    {   "CA",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },

    {   "SG",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },

    {   "EU",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },
    
    {   "AU",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },

    {   "KR",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },    
    {   "JP",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },

    {   "CN",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },

    {   "BR",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },

    {   "RU",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },

    {   "IN",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    },

    {   "MY",
        8, {8, 8, 8}, 
        {22,22,22,22,
        21,20,20,20,20,20,18,18,
        20,20,20,20,20,19,17,17,
        19,19,19,19,19,18,16,16,
        20,20,20,20,20,19,17,17,15,15,
        19,19,19,19,19,18,16,16,14,14}
    }
};

uint8_t tlv_dft_sign[16] = {0x42, 0x4c, 0x52, 0x46, 0x50, 0x41, 0x52, 0x41, 
                        0x4f, 0x36, 0x44, 0x6b, 0x58, 0x62, 0x31, 0x6b};

uint8_t tlv_dft_xtal_mode[RFTLV_MAXLEN_XTAL_MODE+4]={RFTLV_TYPE_XTAL_MODE, 0x00, RFTLV_MAXLEN_XTAL_MODE, 0x00, 
                                                 0x4d, 0x46}; //"MF" , first EFSUE, later ram

uint8_t tlv_dft_xtal_capcode[RFTLV_MAXLEN_XTAL+4] = {RFTLV_TYPE_XTAL, 0x00, RFTLV_MAXLEN_XTAL, 0x00, 
                                                 0x22, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 
                                                 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00};

uint8_t tlv_dft_pwr_mode[RFTLV_MAXLEN_PWR_MODE+4] = {RFTLV_TYPE_PWR_MODE, 0x00, RFTLV_MAXLEN_PWR_MODE, 0x00, 
                                                 0x42, 0x46}; //"BF", first EFSUE, later ram

uint8_t tlv_dft_pwr_11b[RFTLV_MAXLEN_PWR_TABLE_11B+4] = {RFTLV_TYPE_PWR_TABLE_11B, 0x00, RFTLV_MAXLEN_PWR_TABLE_11B, 0x00, 
                                                     0x13, 0x13, 0x13, 0x13};

uint8_t tlv_dft_pwr_11g[RFTLV_MAXLEN_PWR_TABLE_11G+4] = {RFTLV_TYPE_PWR_TABLE_11G, 0x00, RFTLV_MAXLEN_PWR_TABLE_11G, 0x00, 
                                                     0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10};    

uint8_t tlv_dft_pwr_11n[RFTLV_MAXLEN_PWR_TABLE_11N+4] = {RFTLV_TYPE_PWR_TABLE_11N, 0x00, RFTLV_MAXLEN_PWR_TABLE_11N, 0x00, 
                                                     0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};    

uint8_t tlv_dft_pwr_11n_ht40[RFTLV_MAXLEN_PWR_TABLE_11N_HT40+4] = {RFTLV_TYPE_PWR_TABLE_11N_HT40, 0x00, RFTLV_MAXLEN_PWR_TABLE_11N_HT40, 0x00, 
                                                               0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};    

uint8_t tlv_dft_pwr_11ac_vht20[RFTLV_MAXLEN_PWR_TABLE_11AC_VHT20+4] = {RFTLV_TYPE_PWR_TABLE_11AC_VHT20, 0x00, RFTLV_MAXLEN_PWR_TABLE_11AC_VHT20, 0x00, 
                                                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};    

uint8_t tlv_dft_pwr_11ac_vht40[RFTLV_MAXLEN_PWR_TABLE_11AC_VHT40+4] = {RFTLV_TYPE_PWR_TABLE_11AC_VHT40, 0x00, RFTLV_MAXLEN_PWR_TABLE_11AC_VHT40, 0x00, 
                                                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};    

uint8_t tlv_dft_pwr_11ac_vht80[RFTLV_MAXLEN_PWR_TABLE_11AC_VHT80+4] = {RFTLV_TYPE_PWR_TABLE_11AC_VHT80, 0x00, RFTLV_MAXLEN_PWR_TABLE_11AC_VHT80, 0x00, 
                                                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};


uint8_t tlv_dft_pwr_11ax_he20[RFTLV_MAXLEN_PWR_TABLE_11AX_HE20+4] = {RFTLV_TYPE_PWR_TABLE_11AX_HE20, 0x00, RFTLV_MAXLEN_PWR_TABLE_11AX_HE20, 0x00, 
                                                                 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};

uint8_t tlv_dft_pwr_11ax_he40[RFTLV_MAXLEN_PWR_TABLE_11AX_HE40+4] = {RFTLV_TYPE_PWR_TABLE_11AX_HE40, 0x00, RFTLV_MAXLEN_PWR_TABLE_11AX_HE40, 0x00, 
                                                                 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};

uint8_t tlv_dft_pwr_11ax_he80[RFTLV_MAXLEN_PWR_TABLE_11AX_HE80+4] = {RFTLV_TYPE_PWR_TABLE_11AX_HE80, 0x00, RFTLV_MAXLEN_PWR_TABLE_11AX_HE80, 0x00, 
                                                                 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};

uint8_t tlv_dft_pwr_11ax_he160[RFTLV_MAXLEN_PWR_TABLE_11AX_HE160+4] = {RFTLV_TYPE_PWR_TABLE_11AX_HE160, 0x00, RFTLV_MAXLEN_PWR_TABLE_11AX_HE160, 0x00, 
                                                                   0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};

uint8_t tlv_dft_pwr_offset[RFTLV_MAXLEN_PWR_OFFSET+4] = {RFTLV_TYPE_PWR_OFFSET, 0x00, RFTLV_MAXLEN_PWR_OFFSET, 0x00, 
                                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};    

uint8_t tlv_dft_pwr_offset_lp[RFTLV_MAXLEN_PWR_OFFSET_LP+4] = {RFTLV_TYPE_PWR_OFFSET_LP, 0x00, RFTLV_MAXLEN_PWR_OFFSET_LP, 0x00,
                                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; 
    
uint8_t tlv_dft_en_tcal[RFTLV_MAXLEN_EN_TCAL+4] = {RFTLV_TYPE_EN_TCAL, 0x00, RFTLV_MAXLEN_EN_TCAL, 0x00, 0x00};

uint8_t tlv_dft_linear_or_follow[RFTLV_MAXLEN_LINEAR_OR_FOLLOW+4] = {RFTLV_TYPE_LINEAR_OR_FOLLOW, 0x00, RFTLV_MAXLEN_LINEAR_OR_FOLLOW, 0x00, 0x01};

uint8_t tlv_dft_tchannel[RFTLV_MAXLEN_TCHANNELS+4] = {RFTLV_TYPE_TCHANNELS, 0x00, RFTLV_MAXLEN_TCHANNELS, 0x00, 
                                                  0x6c, 0x09, 0x7b, 0x09, 0x8a, 0x09, 0x99, 0x09, 0xa8, 0x09};

uint8_t tlv_dft_tchan_os[RFTLV_MAXLEN_TCHANNEL_OS+4] = {RFTLV_TYPE_TCHANNEL_OS, 0x00, RFTLV_MAXLEN_TCHANNEL_OS, 0x00, 
                                                    0xb4, 0x00, 0xa8, 0x00, 0xa3, 0x00, 0xa0, 0x00, 0x9d, 0x00};

uint8_t tlv_dft_tchan_os_low[RFTLV_MAXLEN_TCHANNEL_OS_LOW+4] = {RFTLV_TYPE_TCHANNEL_OS_LOW, 0x00, RFTLV_MAXLEN_TCHANNEL_OS_LOW, 0x00,
                                                            0xc7, 0x00, 0xba, 0x00, 0xaa, 0x00, 0xa5, 0x00, 0xa0, 0x00};
   
uint8_t tlv_dft_troom_os[RFTLV_MAXLEN_TROOM_OS+4] = {RFTLV_TYPE_TROOM_OS, 0x00, RFTLV_MAXLEN_TROOM_OS, 0x00, 0xff, 0x00};
    
uint8_t tlv_dft_pwr_ble[RFTLV_MAXLEN_PWR_TABLE_BLE+4] = {RFTLV_TYPE_PWR_TABLE_BLE, 0x00, RFTLV_MAXLEN_PWR_TABLE_BLE, 0x00, 
                                                     0x0d, 0x00, 0x00, 0x00};
    
uint8_t tlv_dft_pwr_oft_ble[RFTLV_MAXLEN_BLE_PWR_OFFSET+4] = {RFTLV_TYPE_PWR_OFFSET_BLE, 0x00, RFTLV_MAXLEN_BLE_PWR_OFFSET, 0x00, 
                                                          0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t tlv_dft_country_code[RFTLV_MAXLEN_COUNTRY_CODE+4] = {RFTLV_TYPE_COUNTRY_CODE, 0x00, RFTLV_MAXLEN_COUNTRY_CODE, 0x00, 0x00, 0x00};

uint8_t tlv_dft_pwr_bt[RFTLV_MAXLEN_PWR_TABLE_BT+4] = {RFTLV_TYPE_PWR_TABLE_BT, 0x00, RFTLV_MAXLEN_PWR_TABLE_BT, 0x00, 
                                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t tlv_dft_pwr_zb[RFTLV_MAXLEN_PWR_TBALE_ZIGBEE+4] = {RFTLV_TYPE_PWR_TABLE_ZIGBEE, 0x00, RFTLV_MAXLEN_PWR_TBALE_ZIGBEE, 0x00, 
                                                       0x00, 0x00, 0x00, 0x00};

uint8_t tlv_dft_en_tcapcal[RFTLV_MAXLEN_EN_TCAPCAL+4] = {RFTLV_TYPE_EN_TCAPCAL, 0x00, RFTLV_MAXLEN_EN_TCAPCAL, 0x00, 
                                                     0x00};
uint8_t tlv_dft_tcap_tsen[RFTLV_MAXLEN_TCAP_TSEN+4] = {RFTLV_TYPE_TCAP_TSEN, 0x00, RFTLV_MAXLEN_TCAP_TSEN, 0x00, 
                                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t tlv_dft_tcap_capcode[RFTLV_MAXLEN_TCAP_CAPCODE+4] = {RFTLV_TYPE_TCAP_CAPCODE, 0x00, RFTLV_MAXLEN_TCAP_CAPCODE, 0x00, 
                                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t tlv_sign[16];
uint8_t tlv_xtal_mode[RFTLV_MAXLEN_XTAL_MODE+4]; 
uint8_t tlv_xtal_capcode[RFTLV_MAXLEN_XTAL+4]; 
uint8_t tlv_pwr_mode[RFTLV_MAXLEN_PWR_MODE+4];
uint8_t tlv_pwr_11b[RFTLV_MAXLEN_PWR_TABLE_11B+4];
uint8_t tlv_pwr_11g[RFTLV_MAXLEN_PWR_TABLE_11G+4];
uint8_t tlv_pwr_11n[RFTLV_MAXLEN_PWR_TABLE_11N+4];
uint8_t tlv_pwr_11n_ht40[RFTLV_MAXLEN_PWR_TABLE_11N_HT40+4];
uint8_t tlv_pwr_11ac_vht20[RFTLV_MAXLEN_PWR_TABLE_11AC_VHT20+4];
uint8_t tlv_pwr_11ac_vht40[RFTLV_MAXLEN_PWR_TABLE_11AC_VHT40+4];
uint8_t tlv_pwr_11ac_vht80[RFTLV_MAXLEN_PWR_TABLE_11AC_VHT80+4];
uint8_t tlv_pwr_11ax_he20[RFTLV_MAXLEN_PWR_TABLE_11AX_HE20+4];
uint8_t tlv_pwr_11ax_he40[RFTLV_MAXLEN_PWR_TABLE_11AX_HE40+4];
uint8_t tlv_pwr_11ax_he80[RFTLV_MAXLEN_PWR_TABLE_11AX_HE80+4];
uint8_t tlv_pwr_11ax_he160[RFTLV_MAXLEN_PWR_TABLE_11AX_HE160+4];
uint8_t tlv_pwr_offset[RFTLV_MAXLEN_PWR_OFFSET+4];
uint8_t tlv_pwr_offset_lp[RFTLV_MAXLEN_PWR_OFFSET_LP+4];
uint8_t tlv_en_tcal[RFTLV_MAXLEN_EN_TCAL+4];
uint8_t tlv_linear_or_follow[RFTLV_MAXLEN_LINEAR_OR_FOLLOW+4];
uint8_t tlv_tchannel[RFTLV_MAXLEN_TCHANNELS+4];
uint8_t tlv_tchan_os[RFTLV_MAXLEN_TCHANNEL_OS+4];
uint8_t tlv_tchan_os_low[RFTLV_MAXLEN_TCHANNEL_OS_LOW+4];   
uint8_t tlv_troom_os[RFTLV_MAXLEN_TROOM_OS+4];    
uint8_t tlv_pwr_ble[RFTLV_MAXLEN_PWR_TABLE_BLE+4];    
uint8_t tlv_pwr_oft_ble[RFTLV_MAXLEN_BLE_PWR_OFFSET+4];
uint8_t tlv_country_code[RFTLV_MAXLEN_COUNTRY_CODE+4];
uint8_t tlv_pwr_bt[RFTLV_MAXLEN_PWR_TABLE_BT+4];
uint8_t tlv_pwr_zb[RFTLV_MAXLEN_PWR_TBALE_ZIGBEE+4];
uint8_t tlv_en_tcapcal[RFTLV_MAXLEN_EN_TCAPCAL+4];
uint8_t tlv_tcap_tsen[RFTLV_MAXLEN_TCAP_TSEN+4];
uint8_t tlv_tcap_capcode[RFTLV_MAXLEN_TCAP_CAPCODE+4];

/**
 *
 */
const char *bl_find_tag(const u8 *file_data, unsigned int file_size,
                           const char *tag_name, unsigned int tag_len)
{
    unsigned int curr, src,line_start = 0, line_size;
    uint32_t pos = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Walk through all the lines of the configuration file */
    while (line_start < file_size) {
        /* Search the end of the current line (or the end of the file) */
        for (curr = line_start; curr < file_size; curr++)
            if (file_data[curr] == '\n')
                break;

        /* Compute the line size */
        line_size = curr - line_start;
        src = line_start;
        pos = 0;

        while (pos < line_size &&
            file_data[src] != '\x0A' && file_data[src] != '\0') {           
            if (((file_data[src] == '/') && (file_data[src+1] == '/'))  || (file_data[src] == '#')) /* skip */
                goto next_line;
            if (file_data[src] != ' ' && file_data[src] != '\t') /* parse space */
                break;
            else
                src++;
            pos++;
        }
        line_start = src;

            
        /* Check if this line contains the expected tag */
        if ((line_size >= (strlen(tag_name) + tag_len)) &&
            (!strncmp(&file_data[line_start], tag_name, strlen(tag_name))))
            return (&file_data[line_start + strlen(tag_name)]);
next_line:
        /* Move to next line */
        line_start = curr + 1;
    }

    /* Tag not found */
    return NULL;
}

/**
 *  @brief This function finds if given country code is in EU table
 *
 *  @param country_code     Country string
 *
 *  @return                 true or false
 */
u8 is_eu_country(u8 *country_code)
{
    u8 i;

    /* Look for code in EU country code table */
    for (i = 0; i < ARRAY_SIZE(eu_country_code_table); i++) {
        if (!memcmp(eu_country_code_table[i], country_code,
                COUNTRY_CODE_LEN - 1)) {
            return true;
        }
    }

    return false;
}

/**
 *  @brief This function finds if given country code is in default country code table
 *
 *  @param country_code     Country string
 *
 *  @return                 true or false
 */
u8 is_normal_country(u8 *country_code)
{
    u8 i;

    /* Look for code in EU country code table */
    for (i = 0; i < ARRAY_SIZE(normal_country_code); i++) {
        if (!memcmp(normal_country_code[i].region, country_code,
                COUNTRY_CODE_LEN - 1)) {
            return true;
        }
    }

    return false;
}

u8 is_valid_country_code(char *alpha2)
{
    if (!alpha2 || strlen(alpha2) < 2)
        return false;

    if (isalpha(alpha2[0]) && isalpha(alpha2[1]))
        return true;

    return false;
}

/**
 *  @brief This function finds if given country code pwr table in b_country_default table
 *
 *  @param country_code     Country string
 *
 *  @return                 true or false
 */

u8 bl_find_country_pwr_table(struct country_power_info **pwr_table, 
                                     u8 *country_code)
{
   u8 ret = 0;
   u8 code[3] = {0};
   u8 i;
   
    if(is_eu_country(country_code))
        memcpy(code, "EU", COUNTRY_CODE_LEN);
    else if (is_normal_country(country_code))
        memcpy(code, country_code, COUNTRY_CODE_LEN);
    else
        ret = -1;

    code[2] = '\0';
    
    printk("%s, country code %s ret %d\n", __func__, code, ret);

    for (i=0; i < ARRAY_SIZE(bl_country_default); i++)
       {
           if(!memcmp(bl_country_default[i].country_code, code, COUNTRY_CODE_LEN - 1))
           {
               *pwr_table = &bl_country_default[i];
               
               printk("find table %s \n", bl_country_default[i].country_code);
               
               break;
           }    
       }

    return ret;
}

int bl_parse_confg_kv(uint8_t *cfg_buf, uint32_t cfg_buf_len,
                            struct mm_cal_cfg_req *cal_cfg_req,
                            struct cfg_kv_t *kv, int cfg_num)
{
    int ret = 0;
    int i = 0;

    memcpy(tlv_sign, tlv_dft_sign, sizeof(tlv_dft_sign)); 
    memcpy(tlv_xtal_mode, tlv_dft_xtal_mode, sizeof(tlv_dft_xtal_mode)); 
    memcpy(tlv_xtal_capcode, tlv_dft_xtal_capcode, sizeof(tlv_dft_xtal_capcode));
    memcpy(tlv_pwr_mode, tlv_dft_pwr_mode, sizeof(tlv_dft_pwr_mode));
    memcpy(tlv_pwr_11b, tlv_dft_pwr_11b, sizeof(tlv_dft_pwr_11b));
    memcpy(tlv_pwr_11g, tlv_dft_pwr_11g, sizeof(tlv_dft_pwr_11g));
    memcpy(tlv_pwr_11n, tlv_dft_pwr_11n, sizeof(tlv_dft_pwr_11n));
    memcpy(tlv_pwr_11n_ht40, tlv_dft_pwr_11n_ht40, sizeof(tlv_dft_pwr_11n_ht40));
    memcpy(tlv_pwr_11ac_vht20, tlv_dft_pwr_11ac_vht20, sizeof(tlv_dft_pwr_11ac_vht20));
    memcpy(tlv_pwr_11ac_vht40, tlv_dft_pwr_11ac_vht40, sizeof(tlv_dft_pwr_11ac_vht40));
    memcpy(tlv_pwr_11ac_vht80, tlv_dft_pwr_11ac_vht80, sizeof(tlv_dft_pwr_11ac_vht80));
    memcpy(tlv_pwr_11ax_he20, tlv_dft_pwr_11ax_he20, sizeof(tlv_dft_pwr_11ax_he20));
    memcpy(tlv_pwr_11ax_he40, tlv_dft_pwr_11ax_he40, sizeof(tlv_dft_pwr_11ax_he40));
    memcpy(tlv_pwr_11ax_he80, tlv_dft_pwr_11ax_he80, sizeof(tlv_dft_pwr_11ax_he80));
    memcpy(tlv_pwr_11ax_he160, tlv_dft_pwr_11ax_he160, sizeof(tlv_dft_pwr_11ax_he160));
    memcpy(tlv_pwr_offset, tlv_dft_pwr_offset, sizeof(tlv_dft_pwr_offset));
    memcpy(tlv_pwr_offset_lp, tlv_dft_pwr_offset_lp, sizeof(tlv_dft_pwr_offset_lp));
    memcpy(tlv_en_tcal, tlv_dft_en_tcal, sizeof(tlv_dft_en_tcal));
    memcpy(tlv_linear_or_follow, tlv_dft_linear_or_follow, sizeof(tlv_dft_linear_or_follow));
    memcpy(tlv_tchannel, tlv_dft_tchannel, sizeof(tlv_dft_tchannel));
    memcpy(tlv_tchan_os, tlv_dft_tchan_os, sizeof(tlv_dft_tchan_os));
    memcpy(tlv_tchan_os_low, tlv_dft_tchan_os_low, sizeof(tlv_dft_tchan_os_low));
    memcpy(tlv_troom_os, tlv_dft_troom_os, sizeof(tlv_dft_troom_os));
    memcpy(tlv_pwr_ble, tlv_dft_pwr_ble, sizeof(tlv_dft_pwr_ble));
    memcpy(tlv_pwr_oft_ble, tlv_dft_pwr_oft_ble, sizeof(tlv_dft_pwr_oft_ble));
    memcpy(tlv_country_code, tlv_dft_country_code, sizeof(tlv_dft_country_code));
    memcpy(tlv_pwr_bt, tlv_dft_pwr_bt, sizeof(tlv_dft_pwr_bt));
    memcpy(tlv_pwr_zb, tlv_dft_pwr_zb, sizeof(tlv_dft_pwr_zb));
    memcpy(tlv_en_tcapcal, tlv_dft_en_tcapcal, sizeof(tlv_dft_en_tcapcal));
    memcpy(tlv_tcap_tsen, tlv_dft_tcap_tsen, sizeof(tlv_dft_tcap_tsen));
    memcpy(tlv_tcap_capcode, tlv_dft_tcap_capcode, sizeof(tlv_dft_tcap_capcode));
  
    for (i=0; i < cfg_num; i++) {
        uint32_t id = kv[i].id;
        const uint8_t *start_pos = NULL;
        const uint8_t *eq_pos = NULL;
        uint8_t *end_pos = NULL;
        
        start_pos = bl_find_tag(cfg_buf, cfg_buf_len, kv[i].key, 0);
        if (start_pos != NULL) {
            printk("%s, find key in cfg_buf, key:%s\n", __func__, kv[i].key);
            
            eq_pos = start_pos;
            end_pos = strchr(start_pos, '\n');
            
            if (eq_pos != NULL && end_pos != NULL) {
                BL_DBG("%s, find = and end_pos, 0x%p, 0x%p\n", __func__, 
                       eq_pos, end_pos);

                //If comment out
                if ((*(start_pos-1) == '/' && *(start_pos-2) == '/') ||
                    *(start_pos-1) == '#') 
                {
                    printk("%s, key %s is comment out\n", __func__, kv[i].key);
                    continue;
                }
                        
                if (id == CFG_CAPCODE_ID) {
                    int capcode = 0;
                    
                    ret = sscanf(eq_pos, "%d", &capcode);
                    if (ret != 1) {
                        printk("%s, not valid capcode in decimal\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->capcode = (uint8_t)capcode;
                    cal_cfg_req->capcode_valid = VALID_RF_PARA;
                    
                    printk("cap code %d\n", cal_cfg_req->capcode);
                } else if (id == CFG_MAC_ID) {
                    ret = sscanf(eq_pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                                 &cal_cfg_req->mac[0], &cal_cfg_req->mac[1],
                                 &cal_cfg_req->mac[2],
                                 &cal_cfg_req->mac[3], &cal_cfg_req->mac[4], 
                                 &cal_cfg_req->mac[5]);
                    if (ret != 6) {
                        printk("%s, not enough valid mac addr in hex like 11:22:33:44:55:66\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->mac_valid = VALID_RF_PARA;
                    
                    printk("mac:0x%x:0x%x:0x%x:0x%x:0x%x:0x%x\r\n",
                           cal_cfg_req->mac[0], cal_cfg_req->mac[1], 
                           cal_cfg_req->mac[2], cal_cfg_req->mac[3], 
                           cal_cfg_req->mac[4], cal_cfg_req->mac[5]);
                } else if (id == CFG_PWR_OFT_ID) {
                    int p[14];
                    int indx = 0;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
                                 p, p+1, p+2, p+3, p+4, p+5, p+6, p+7, p+8, p+9, 
                                 p+10, p+11, p+12, p+13);
                    if (ret != 14) {
                        printk("%s, not enough valid power offset in decimal like 1,-1,3,5,8,10,1,0,9,0,1,2,3,14\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_oft_wifi_valid = VALID_RF_PARA;

                    for (indx=0; indx<14; indx++) {
                        if (p[indx] > MFG_PWR_OFFSET_MAX) {
                            p[indx] = MFG_PWR_OFFSET_MAX;
                        }
                        if (p[indx] < MFG_PWR_OFFSET_MIN) {
                            p[indx] = MFG_PWR_OFFSET_MIN;
                        }

                        //+16, special for mfg rfparam tlv load which -16
                        tlv_pwr_offset[RFTLV_SIZE_TL+indx] = (int8_t)p[indx] + 16;
                        tlv_pwr_offset_lp[RFTLV_SIZE_TL+indx] = (int8_t)p[indx] + 16;

                        cal_cfg_req->pwrcal.channel_pwrcomp_wlan[indx] = (int8_t)p[indx];
                    }
                    
                    printk("pwr_offset: %d %d %d %d %d %d %d %d %d %d %d %d %d %d\r\n",
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[0],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[1],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[2],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[3],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[4],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[5],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[6],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[7],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[8],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[9],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[10],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[11],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[12],
                           cal_cfg_req->pwrcal.channel_pwrcomp_wlan[13]);
                }else if (id == CFG_EN_TCAL_ID) {
                    int en_tcal = 0;
                    
                    ret = sscanf(eq_pos, "%d", &en_tcal);
                    if (ret != 1) {
                        printk("%s, not valid en_tcal in decimal\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->tcal_valid = (en_tcal > 0)?VALID_RF_PARA:0;
                    cal_cfg_req->tcal.en_tcal = (uint8_t)en_tcal;

                    tlv_en_tcal[RFTLV_SIZE_TL] = (uint8_t)en_tcal;
                    
                    printk("en_tcal 0x%x\n", cal_cfg_req->tcal_valid);
                } else if (id == CFG_TCHANNEL_OS_ID) {
                    int t0, t1, t2, t3, t4;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d", &t0,&t1,&t2,&t3,&t4);
                    if (ret != 5) {
                        cal_cfg_req->tcal_valid = 0;
                        cal_cfg_req->tcal.en_tcal = false;
                        
                        printk("%s, not enough valid Tchannel_os in decimal like 180,168,163,160,157, \
                               tcal disabled\n", __func__);
                               
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->tcal.Tchannel_os[0] = (int16_t)t0;
                    cal_cfg_req->tcal.Tchannel_os[1] = (int16_t)t1;
                    cal_cfg_req->tcal.Tchannel_os[2] = (int16_t)t2;
                    cal_cfg_req->tcal.Tchannel_os[3] = (int16_t)t3;
                    cal_cfg_req->tcal.Tchannel_os[4] = (int16_t)t4;

                    *(int16_t *)&tlv_tchan_os[RFTLV_SIZE_TL] = (int16_t)t0;
                    *(int16_t *)&tlv_tchan_os[RFTLV_SIZE_TL+2] = (int16_t)t1;
                    *(int16_t *)&tlv_tchan_os[RFTLV_SIZE_TL+4] = (int16_t)t2;
                    *(int16_t *)&tlv_tchan_os[RFTLV_SIZE_TL+6] = (int16_t)t3;
                    *(int16_t *)&tlv_tchan_os[RFTLV_SIZE_TL+8] = (int16_t)t4;
                    
                    printk("Tchannel_os: %d %d %d %d %d\n",
                           cal_cfg_req->tcal.Tchannel_os[0], 
                           cal_cfg_req->tcal.Tchannel_os[1], 
                           cal_cfg_req->tcal.Tchannel_os[2], 
                           cal_cfg_req->tcal.Tchannel_os[3],
                           cal_cfg_req->tcal.Tchannel_os[4]);
                } else if (id == CFG_TCHANNEL_OS_LOW_ID) {
                    int t0, t1, t2, t3, t4;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d", &t0,&t1,&t2,&t3,&t4);
                    if (ret != 5) {
                        cal_cfg_req->tcal_valid = 0;
                        cal_cfg_req->tcal.en_tcal = false;
                        
                        printk("%s, not enough valid Tchannel_os_low in decimal like 180,168,163,160,157, \
                               tcal disabled\n", __func__);
                               
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->tcal.Tchannel_os_low[0] = (int16_t)t0;
                    cal_cfg_req->tcal.Tchannel_os_low[1] = (int16_t)t1;
                    cal_cfg_req->tcal.Tchannel_os_low[2] = (int16_t)t2;
                    cal_cfg_req->tcal.Tchannel_os_low[3] = (int16_t)t3;
                    cal_cfg_req->tcal.Tchannel_os_low[4] = (int16_t)t4;

                    *(int16_t *)&tlv_tchan_os_low[RFTLV_SIZE_TL] = (int16_t)t0;
                    *(int16_t *)&tlv_tchan_os_low[RFTLV_SIZE_TL+2] = (int16_t)t1;
                    *(int16_t *)&tlv_tchan_os_low[RFTLV_SIZE_TL+4] = (int16_t)t2;
                    *(int16_t *)&tlv_tchan_os_low[RFTLV_SIZE_TL+6] = (int16_t)t3;
                    *(int16_t *)&tlv_tchan_os_low[RFTLV_SIZE_TL+8] = (int16_t)t4;
                    
                    printk("Tchannel_os_low: %d %d %d %d %d\n", 
                           cal_cfg_req->tcal.Tchannel_os_low[0], 
                           cal_cfg_req->tcal.Tchannel_os_low[1],
                           cal_cfg_req->tcal.Tchannel_os_low[2],
                           cal_cfg_req->tcal.Tchannel_os_low[3], 
                           cal_cfg_req->tcal.Tchannel_os_low[4]);
                } else if (id == CFG_LINEAR_ID) {
                    int linear = 0;
                    
                    ret = sscanf(eq_pos, "%d", &linear);
                    if (ret != 1) {
                        cal_cfg_req->tcal_valid = 0;
                        cal_cfg_req->tcal.en_tcal = false;
                        
                        printk("%s, not valid linear_or_follow in decimal, \
                               linear_or_follow use default\n", __func__);
                               
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->tcal.linear_or_follow = (uint8_t)linear;

                    tlv_linear_or_follow[RFTLV_SIZE_TL] = (uint8_t)linear;
                    
                    printk("linear_or_follow %d\n", cal_cfg_req->tcal.linear_or_follow);
                } else if (id == CFG_TCHANNELS_ID) {
                    int t0, t1, t2, t3, t4;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d", &t0,&t1,&t2,&t3,&t4);
                    if (ret != 5) {
                        cal_cfg_req->tcal_valid = 0;
                        cal_cfg_req->tcal.en_tcal = false;
                        
                        printk("%s, not enough valid Tchannels in decimal like 2412,2427,2442,2457,2472, \
                               use default\n", __func__);
                               
                        ret = -1;
                        break;
                    }
                    cal_cfg_req->tcal.Tchannels[0] = (uint16_t)t0;
                    cal_cfg_req->tcal.Tchannels[1] = (uint16_t)t1;
                    cal_cfg_req->tcal.Tchannels[2] = (uint16_t)t2;
                    cal_cfg_req->tcal.Tchannels[3] = (uint16_t)t3;
                    cal_cfg_req->tcal.Tchannels[4] = (uint16_t)t4;

                    *(uint16_t *)&tlv_tchannel[RFTLV_SIZE_TL] = (uint16_t)t0;
                    *(uint16_t *)&tlv_tchannel[RFTLV_SIZE_TL+2] = (uint16_t)t1;
                    *(uint16_t *)&tlv_tchannel[RFTLV_SIZE_TL+4] = (uint16_t)t2;
                    *(uint16_t *)&tlv_tchannel[RFTLV_SIZE_TL+6] = (uint16_t)t3;
                    *(uint16_t *)&tlv_tchannel[RFTLV_SIZE_TL+8] = (uint16_t)t4;
                    
                    printk("TChannels: %d %d %d %d %d\n", 
                           cal_cfg_req->tcal.Tchannels[0], 
                           cal_cfg_req->tcal.Tchannels[1], 
                           cal_cfg_req->tcal.Tchannels[2], 
                           cal_cfg_req->tcal.Tchannels[3],
                           cal_cfg_req->tcal.Tchannels[4]);
                } else if (id == CFG_TROOM_OS_ID) {
                    int Troom_os = 0;
                    
                    ret = sscanf(eq_pos, "%d", &Troom_os);
                    if (ret != 1) {
                        cal_cfg_req->tcal_valid = 0;
                        cal_cfg_req->tcal.en_tcal = false;
                        
                        printk("%s, not valid Troom_os in decimal, use default\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    cal_cfg_req->tcal.Troom_os = (int16_t)Troom_os;

                    *(int16_t *)&tlv_troom_os[RFTLV_SIZE_TL] = (int16_t)Troom_os;
                    
                    printk("Troom_os %d\n", cal_cfg_req->tcal.Troom_os);
                } else if (id == CFG_XTAL_MODE_ID) {
                    ret = sscanf(eq_pos, "%c%c", cal_cfg_req->xtal.xtal_mode, cal_cfg_req->xtal.xtal_mode+1);
                    if (ret != 2) {
                        printk("%s, not valid xtal_mode, xtal mode disabled\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->xtal_mode_valid = VALID_RF_PARA;

                    tlv_xtal_mode[RFTLV_SIZE_TL] = cal_cfg_req->xtal.xtal_mode[0];
                    tlv_xtal_mode[RFTLV_SIZE_TL+1] = cal_cfg_req->xtal.xtal_mode[1];
                    
                    printk("xtal_mode:%c%c\n", cal_cfg_req->xtal.xtal_mode[0], cal_cfg_req->xtal.xtal_mode[1]);
                } else if (id == CFG_XTAL_CAP_ID) {
                    ret = sscanf(eq_pos, "%d", cal_cfg_req->xtal.xtal_cap);
                    if (ret != 1) {
                        printk("%s, not valid xtal_cap, xtal cap disabled\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->xtal.xtal_cap[1] = cal_cfg_req->xtal.xtal_cap[0];
                    cal_cfg_req->xtal.xtal_cap[2] = cal_cfg_req->xtal.xtal_cap[0];
                    cal_cfg_req->xtal.xtal_cap[3] = cal_cfg_req->xtal.xtal_cap[0];
                    cal_cfg_req->xtal.xtal_cap[4] = cal_cfg_req->xtal.xtal_cap[0];
                    cal_cfg_req->xtal_cap_valid = VALID_RF_PARA;

                    *(uint32_t *)&tlv_xtal_capcode[RFTLV_SIZE_TL] = cal_cfg_req->xtal.xtal_cap[0];
                    *(uint32_t *)&tlv_xtal_capcode[RFTLV_SIZE_TL+4] = cal_cfg_req->xtal.xtal_cap[0];
                    *(uint32_t *)&tlv_xtal_capcode[RFTLV_SIZE_TL+8] = cal_cfg_req->xtal.xtal_cap[0];
                    *(uint32_t *)&tlv_xtal_capcode[RFTLV_SIZE_TL+12] = cal_cfg_req->xtal.xtal_cap[0];
                    *(uint32_t *)&tlv_xtal_capcode[RFTLV_SIZE_TL+16] = cal_cfg_req->xtal.xtal_cap[0];
                    
                    printk("xtal_cap:%d %d %d %d %d\n", cal_cfg_req->xtal.xtal_cap[0],
                           cal_cfg_req->xtal.xtal_cap[1],
                           cal_cfg_req->xtal.xtal_cap[2], cal_cfg_req->xtal.xtal_cap[3],
                           cal_cfg_req->xtal.xtal_cap[4]);
                } else if (id == CFG_PWR_MODE_ID) {
                    ret = sscanf(eq_pos, "%c%c", cal_cfg_req->pwr_mode, cal_cfg_req->pwr_mode+1);
                    if (ret != 2) {
                        printk("%s, not valid pwr_mode, pwr mode disabled\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_mode_valid = VALID_RF_PARA;

                    tlv_pwr_mode[RFTLV_SIZE_TL] = cal_cfg_req->pwr_mode[0];
                    tlv_pwr_mode[RFTLV_SIZE_TL+1] = cal_cfg_req->pwr_mode[1];
                    
                    printk("pwr_mode:%c%c\n", cal_cfg_req->pwr_mode[0], cal_cfg_req->pwr_mode[1]);
                } else if (id == CFG_PWR_11B_ID) {
                    int t0, t1, t2, t3;
                    
                    printk("%s \n", end_pos);
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d", &t0,&t1,&t2,&t3);
                    if (ret != 4) {
                        printk("%s, must be 4 decimal power value for 11b like 20,20,20,20, \
                               11b power target disabled\n", __func__);
                               
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_11b_valid = VALID_RF_PARA;
                    
                    cal_cfg_req->pwrtarget.pwr_11b[0] = (int8_t)t0;
                    cal_cfg_req->pwrtarget.pwr_11b[1] = (int8_t)t1;
                    cal_cfg_req->pwrtarget.pwr_11b[2] = (int8_t)t2;
                    cal_cfg_req->pwrtarget.pwr_11b[3] = (int8_t)t3;
                    memcpy(bl_txpwr_vs_rate_table[PHY_TRPC_11B], 
                          cal_cfg_req->pwrtarget.pwr_11b, PHY_11B_RATE_NUM);

                    tlv_pwr_11b[RFTLV_SIZE_TL] = (int8_t)t0;
                    tlv_pwr_11b[RFTLV_SIZE_TL+1] = (int8_t)t1;
                    tlv_pwr_11b[RFTLV_SIZE_TL+2] = (int8_t)t2;
                    tlv_pwr_11b[RFTLV_SIZE_TL+3] = (int8_t)t3;
                    
                    printk("pwr_11b:%d %d %d %d\n", cal_cfg_req->pwrtarget.pwr_11b[0], 
                           cal_cfg_req->pwrtarget.pwr_11b[1],
                           cal_cfg_req->pwrtarget.pwr_11b[2], 
                           cal_cfg_req->pwrtarget.pwr_11b[3]);
                } else if (id == CFG_PWR_11G_ID) {
                    int t0, t1, t2, t3, t4, t5, t6, t7;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d,%d,%d,%d", 
                                 &t0,&t1,&t2,&t3,&t4,&t5,&t6,&t7);
                    if (ret != 8) {
                        printk("%s, must be 8 decimal power value for 11g like 18,18,18,18,18,18,18,18, \
                               11g power target disabled\n", __func__);
                               
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_11g_valid = VALID_RF_PARA;
                    
                    cal_cfg_req->pwrtarget.pwr_11g[0] = (int8_t)t0;
                    cal_cfg_req->pwrtarget.pwr_11g[1] = (int8_t)t1;
                    cal_cfg_req->pwrtarget.pwr_11g[2] = (int8_t)t2;
                    cal_cfg_req->pwrtarget.pwr_11g[3] = (int8_t)t3;
                    cal_cfg_req->pwrtarget.pwr_11g[4] = (int8_t)t4;
                    cal_cfg_req->pwrtarget.pwr_11g[5] = (int8_t)t5;
                    cal_cfg_req->pwrtarget.pwr_11g[6] = (int8_t)t6;
                    cal_cfg_req->pwrtarget.pwr_11g[7] = (int8_t)t7;
                    memcpy(bl_txpwr_vs_rate_table[PHY_TRPC_11G],
                           cal_cfg_req->pwrtarget.pwr_11g, PHY_11G_RATE_NUM);

                    tlv_pwr_11g[RFTLV_SIZE_TL] = (int8_t)t0;
                    tlv_pwr_11g[RFTLV_SIZE_TL+1] = (int8_t)t1;
                    tlv_pwr_11g[RFTLV_SIZE_TL+2] = (int8_t)t2;
                    tlv_pwr_11g[RFTLV_SIZE_TL+3] = (int8_t)t3;
                    tlv_pwr_11g[RFTLV_SIZE_TL+4] = (int8_t)t4;
                    tlv_pwr_11g[RFTLV_SIZE_TL+5] = (int8_t)t5;
                    tlv_pwr_11g[RFTLV_SIZE_TL+6] = (int8_t)t6;
                    tlv_pwr_11g[RFTLV_SIZE_TL+7] = (int8_t)t7;

                    printk("pwr_11g:%d %d %d %d %d %d %d %d\n",
                           cal_cfg_req->pwrtarget.pwr_11g[0], 
                           cal_cfg_req->pwrtarget.pwr_11g[1], 
                           cal_cfg_req->pwrtarget.pwr_11g[2], 
                           cal_cfg_req->pwrtarget.pwr_11g[3],
                           cal_cfg_req->pwrtarget.pwr_11g[4],
                           cal_cfg_req->pwrtarget.pwr_11g[5],
                           cal_cfg_req->pwrtarget.pwr_11g[6], 
                           cal_cfg_req->pwrtarget.pwr_11g[7]);
                } else if (id == CFG_PWR_11N_HT20_ID) {
                    int t0, t1, t2, t3, t4, t5, t6, t7;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d,%d,%d,%d", &t0,&t1,&t2,&t3,&t4,&t5,&t6,&t7);
                    if (ret != 8) {
                        printk("%s, must be 8 decimal power value for 11n ht20 like 18,18,18,18,18,18,18,18, \
                               11n ht20 power target disabled\n", __func__);
                               
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_11n_ht20_valid = VALID_RF_PARA;
                    
                    cal_cfg_req->pwrtarget.pwr_11n_ht20[0] = (int8_t)t0;
                    cal_cfg_req->pwrtarget.pwr_11n_ht20[1] = (int8_t)t1;
                    cal_cfg_req->pwrtarget.pwr_11n_ht20[2] = (int8_t)t2;
                    cal_cfg_req->pwrtarget.pwr_11n_ht20[3] = (int8_t)t3;
                    cal_cfg_req->pwrtarget.pwr_11n_ht20[4] = (int8_t)t4;
                    cal_cfg_req->pwrtarget.pwr_11n_ht20[5] = (int8_t)t5;
                    cal_cfg_req->pwrtarget.pwr_11n_ht20[6] = (int8_t)t6;
                    cal_cfg_req->pwrtarget.pwr_11n_ht20[7] = (int8_t)t7;                    
                    memcpy(bl_txpwr_vs_rate_table[PHY_TRPC_11N_BW20],
                         cal_cfg_req->pwrtarget.pwr_11n_ht20, PHY_11N_RATE_NUM);
                    
                    tlv_pwr_11n[RFTLV_SIZE_TL] = (int8_t)t0;
                    tlv_pwr_11n[RFTLV_SIZE_TL+1] = (int8_t)t1;
                    tlv_pwr_11n[RFTLV_SIZE_TL+2] = (int8_t)t2;
                    tlv_pwr_11n[RFTLV_SIZE_TL+3] = (int8_t)t3;
                    tlv_pwr_11n[RFTLV_SIZE_TL+4] = (int8_t)t4;
                    tlv_pwr_11n[RFTLV_SIZE_TL+5] = (int8_t)t5;
                    tlv_pwr_11n[RFTLV_SIZE_TL+6] = (int8_t)t6;
                    tlv_pwr_11n[RFTLV_SIZE_TL+7] = (int8_t)t7;
                    
                    printk("pwr_11n_ht20:%d %d %d %d %d %d %d %d\n", 
                           cal_cfg_req->pwrtarget.pwr_11n_ht20[0], 
                           cal_cfg_req->pwrtarget.pwr_11n_ht20[1], 
                           cal_cfg_req->pwrtarget.pwr_11n_ht20[2], 
                           cal_cfg_req->pwrtarget.pwr_11n_ht20[3],
                           cal_cfg_req->pwrtarget.pwr_11n_ht20[4],
                           cal_cfg_req->pwrtarget.pwr_11n_ht20[5],
                           cal_cfg_req->pwrtarget.pwr_11n_ht20[6],
                           cal_cfg_req->pwrtarget.pwr_11n_ht20[7]);
                } else if (id == CFG_PWR_11N_HT40_ID) {
                    int t0, t1, t2, t3, t4, t5, t6, t7;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d,%d,%d,%d",
                                 &t0,&t1,&t2,&t3,&t4,&t5,&t6,&t7);
                    if (ret != 8) {
                        printk("%s, must be 8 decimal power value for 11n ht40 like 18,18,18,18,18,18,18,18\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_11n_ht40_valid = VALID_RF_PARA;

                    cal_cfg_req->pwrtarget.pwr_11n_ht40[0] = (int8_t)t0;
                    cal_cfg_req->pwrtarget.pwr_11n_ht40[1] = (int8_t)t1;
                    cal_cfg_req->pwrtarget.pwr_11n_ht40[2] = (int8_t)t2;
                    cal_cfg_req->pwrtarget.pwr_11n_ht40[3] = (int8_t)t3;
                    cal_cfg_req->pwrtarget.pwr_11n_ht40[4] = (int8_t)t4;
                    cal_cfg_req->pwrtarget.pwr_11n_ht40[5] = (int8_t)t5;
                    cal_cfg_req->pwrtarget.pwr_11n_ht40[6] = (int8_t)t6;
                    cal_cfg_req->pwrtarget.pwr_11n_ht40[7] = (int8_t)t7;
                    memcpy(bl_txpwr_vs_rate_table[PHY_TRPC_11N_BW40], 
                         cal_cfg_req->pwrtarget.pwr_11n_ht20, PHY_11N_RATE_NUM);

                    tlv_pwr_11n_ht40[RFTLV_SIZE_TL] = (int8_t)t0;
                    tlv_pwr_11n_ht40[RFTLV_SIZE_TL+1] = (int8_t)t1;
                    tlv_pwr_11n_ht40[RFTLV_SIZE_TL+2] = (int8_t)t2;
                    tlv_pwr_11n_ht40[RFTLV_SIZE_TL+3] = (int8_t)t3;
                    tlv_pwr_11n_ht40[RFTLV_SIZE_TL+4] = (int8_t)t4;
                    tlv_pwr_11n_ht40[RFTLV_SIZE_TL+5] = (int8_t)t5;
                    tlv_pwr_11n_ht40[RFTLV_SIZE_TL+6] = (int8_t)t6;
                    tlv_pwr_11n_ht40[RFTLV_SIZE_TL+7] = (int8_t)t7;
                    
                    printk("pwr_11n_ht40:%d %d %d %d %d %d %d %d\n", 
                           cal_cfg_req->pwrtarget.pwr_11n_ht40[0], 
                           cal_cfg_req->pwrtarget.pwr_11n_ht40[1], 
                           cal_cfg_req->pwrtarget.pwr_11n_ht40[2], 
                           cal_cfg_req->pwrtarget.pwr_11n_ht40[3], 
                           cal_cfg_req->pwrtarget.pwr_11n_ht40[4],
                           cal_cfg_req->pwrtarget.pwr_11n_ht40[5], 
                           cal_cfg_req->pwrtarget.pwr_11n_ht40[6], 
                           cal_cfg_req->pwrtarget.pwr_11n_ht40[7]);
                } else if (id == CFG_PWR_11AX_HE20_ID) {
                    int t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10=0, t11=0;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
                                 &t0,&t1,&t2,&t3,&t4,&t5,&t6,&t7,&t8,&t9);
                    if (ret != 10) {
                        printk("%s, must be 10 decimal power value for he20 like 18,18,18,18,18,18,18,18,18,18\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_11ax_he20_valid = VALID_RF_PARA;
                    
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[0] = (int8_t)t0;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[1] = (int8_t)t1;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[2] = (int8_t)t2;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[3] = (int8_t)t3;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[4] = (int8_t)t4;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[5] = (int8_t)t5;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[6] = (int8_t)t6;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[7] = (int8_t)t7;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[8] = (int8_t)t8;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[9] = (int8_t)t9;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[10] = (int8_t)t10;
                    cal_cfg_req->pwrtarget.pwr_11ax_he20[11] = (int8_t)t11;
                    memcpy(bl_txpwr_vs_rate_table[PHY_TRPC_11AX_BW20], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he20, PHY_11AX_RATE_NUM);

                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL] = (int8_t)t0;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+1] = (int8_t)t1;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+2] = (int8_t)t2;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+3] = (int8_t)t3;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+4] = (int8_t)t4;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+5] = (int8_t)t5;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+6] = (int8_t)t6;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+7] = (int8_t)t7;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+8] = (int8_t)t8;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+9] = (int8_t)t9;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+10] = (int8_t)t10;
                    tlv_pwr_11ax_he20[RFTLV_SIZE_TL+11] = (int8_t)t11;

                    printk("pwr_11ax_he20:%d %d %d %d %d %d %d %d %d %d\n",
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[0], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[1], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[2], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[3], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[4],
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[5],
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[6],
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[7],
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[8], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he20[9]);
                } else if (id == CFG_PWR_11AX_HE40_ID) {
                    int t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10=0, t11=0;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
                                 &t0,&t1,&t2,&t3,&t4,&t5,&t6,&t7,&t8,&t9);
                    if (ret != 10) {
                        printk("%s, must be 10 decimal power value for he40 like 18,18,18,18,18,18,18,18,18,18\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_11ax_he40_valid = VALID_RF_PARA;
                    
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[0] = (int8_t)t0;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[1] = (int8_t)t1;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[2] = (int8_t)t2;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[3] = (int8_t)t3;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[4] = (int8_t)t4;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[5] = (int8_t)t5;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[6] = (int8_t)t6;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[7] = (int8_t)t7;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[8] = (int8_t)t8;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[9] = (int8_t)t9;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[10] = (int8_t)t10;
                    cal_cfg_req->pwrtarget.pwr_11ax_he40[11] = (int8_t)t11;
                    memcpy(bl_txpwr_vs_rate_table[PHY_TRPC_11AX_BW40], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he40, PHY_11AX_RATE_NUM);

                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL] = (int8_t)t0;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+1] = (int8_t)t1;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+2] = (int8_t)t2;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+3] = (int8_t)t3;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+4] = (int8_t)t4;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+5] = (int8_t)t5;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+6] = (int8_t)t6;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+7] = (int8_t)t7;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+8] = (int8_t)t8;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+9] = (int8_t)t9;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+10] = (int8_t)t10;
                    tlv_pwr_11ax_he40[RFTLV_SIZE_TL+11] = (int8_t)t11;

                    printk("pwr_11ax_he40:%d %d %d %d %d %d %d %d %d %d\n", 
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[0], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[1], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[2], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[3], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[4],
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[5],
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[6],
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[7],
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[8], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he40[9]);
                } else if (id == CFG_PWR_11AX_HE80_ID) {
                    int t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10=0, t11=0;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                                 &t0,&t1,&t2,&t3,&t4,&t5,&t6,&t7,&t8,&t9);
                    if (ret != 10) {
                        printk("%s, must be 10 decimal power value for he80 like 18,18,18,18,18,18,18,18,18,18\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_11ax_he80_valid = VALID_RF_PARA;

                    cal_cfg_req->pwrtarget.pwr_11ax_he80[0] = (int8_t)t0;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[1] = (int8_t)t1;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[2] = (int8_t)t2;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[3] = (int8_t)t3;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[4] = (int8_t)t4;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[5] = (int8_t)t5;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[6] = (int8_t)t6;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[7] = (int8_t)t7;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[8] = (int8_t)t8;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[9] = (int8_t)t9;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[10] = (int8_t)t10;
                    cal_cfg_req->pwrtarget.pwr_11ax_he80[11] = (int8_t)t11;

                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL] = (int8_t)t0;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+1] = (int8_t)t1;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+2] = (int8_t)t2;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+3] = (int8_t)t3;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+4] = (int8_t)t4;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+5] = (int8_t)t5;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+6] = (int8_t)t6;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+7] = (int8_t)t7;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+8] = (int8_t)t8;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+9] = (int8_t)t9;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+10] = (int8_t)t10;
                    tlv_pwr_11ax_he80[RFTLV_SIZE_TL+11] = (int8_t)t11;
                    
                    printk("pwr_11ax_he80:%d %d %d %d %d %d %d %d %d %d\n",
                           cal_cfg_req->pwrtarget.pwr_11ax_he80[0], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he80[1], cal_cfg_req->pwrtarget.pwr_11ax_he80[2], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he80[3], cal_cfg_req->pwrtarget.pwr_11ax_he80[4],
                           cal_cfg_req->pwrtarget.pwr_11ax_he80[5], cal_cfg_req->pwrtarget.pwr_11ax_he80[6],
                           cal_cfg_req->pwrtarget.pwr_11ax_he80[7], cal_cfg_req->pwrtarget.pwr_11ax_he80[8],
                           cal_cfg_req->pwrtarget.pwr_11ax_he80[9]);
                } else if (id == CFG_PWR_11AX_HE160_ID) {
                    int t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10=0, t11=0;
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
                                 &t0,&t1,&t2,&t3,&t4,&t5,&t6,&t7,&t8,&t9);
                    if (ret != 12) {
                        printk("%s, must be 10 decimal power value for he160 like 18,18,18,18,18,18,18,18,18,18\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwr_11ax_he160_valid = VALID_RF_PARA;

                    cal_cfg_req->pwrtarget.pwr_11ax_he160[0] = (int8_t)t0;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[1] = (int8_t)t1;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[2] = (int8_t)t2;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[3] = (int8_t)t3;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[4] = (int8_t)t4;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[5] = (int8_t)t5;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[6] = (int8_t)t6;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[7] = (int8_t)t7;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[8] = (int8_t)t8;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[9] = (int8_t)t9;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[10] = (int8_t)t10;
                    cal_cfg_req->pwrtarget.pwr_11ax_he160[11] = (int8_t)t11;
                    
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL] = (int8_t)t0;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+1] = (int8_t)t1;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+2] = (int8_t)t2;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+3] = (int8_t)t3;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+4] = (int8_t)t4;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+5] = (int8_t)t5;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+6] = (int8_t)t6;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+7] = (int8_t)t7;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+8] = (int8_t)t8;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+9] = (int8_t)t9;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+10] = (int8_t)t10;
                    tlv_pwr_11ax_he160[RFTLV_SIZE_TL+11] = (int8_t)t11;
                    
                    printk("pwr_11ax_he160:%d %d %d %d %d %d %d %d %d %d\n", 
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[0], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[1], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[2], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[3], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[4],
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[5], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[6], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[7], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[8], 
                           cal_cfg_req->pwrtarget.pwr_11ax_he160[9]);
                } else if (id == CFG_PWR_BLE_ID) {
                    int pwr_ble = 0;
                    
                    ret = sscanf(eq_pos, "%d", &pwr_ble);
                    if (ret != 1) {
                        printk("%s, not valid pwr_ble in decimal, pwr_ble disabled\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwrtarget.pwr_ble = (int8_t)pwr_ble;
                    cal_cfg_req->pwr_ble_valid = VALID_RF_PARA;

                    memcpy(&tlv_pwr_ble[RFTLV_SIZE_TL], &pwr_ble, 4);
                    
                    printk("pwr_ble %d\n", cal_cfg_req->pwrtarget.pwr_ble);
                } else if (id == CFG_PWR_BT_ID) {
                    int pwr_bt[3] = {0};
                    
                    ret = sscanf(eq_pos, "%d,%d,%d", pwr_bt, pwr_bt+1, pwr_bt+2);
                    if (ret != 3) {
                        printk("%s, not valid pwr_bt in decimal, pwr_bt disabled\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->pwrtarget.pwr_bt[0] = (int8_t)pwr_bt[0];
                    cal_cfg_req->pwrtarget.pwr_bt[1] = (int8_t)pwr_bt[1];
                    cal_cfg_req->pwrtarget.pwr_bt[2] = (int8_t)pwr_bt[2];
                    cal_cfg_req->pwr_ble_valid = VALID_RF_PARA;

                    memcpy(&tlv_pwr_bt[RFTLV_SIZE_TL], pwr_bt+0, 4);
                    memcpy(&tlv_pwr_bt[RFTLV_SIZE_TL+4], pwr_bt+1, 4);
                    memcpy(&tlv_pwr_bt[RFTLV_SIZE_TL+8], pwr_bt+2, 4);
                    
                    printk("pwr_bt %d %d %d\n", cal_cfg_req->pwrtarget.pwr_bt[0], 
                           cal_cfg_req->pwrtarget.pwr_bt[1], 
                           cal_cfg_req->pwrtarget.pwr_bt[2]);
                } else if (id == CFG_PWR_OFT_BZ_ID) {
                    int indx = 0;
                    int pwr_oft[5] = {0};
                    
                    ret = sscanf(eq_pos, "%d,%d,%d,%d,%d",
                                 pwr_oft, pwr_oft+1, pwr_oft+2, pwr_oft+3, pwr_oft+4);
                    if (ret != 5) {
                        printk("%s, not valid pwr_oft_bz in decimal, pwr_oft_bz disabled\n", __func__);
                        
                        ret = -1;
                        break;
                    }

                    for (indx=0; indx<5; indx++) {
                        /* Use 3 bits as signed value */
                        if (pwr_oft[indx] > MFG_PWR_OFFSET_MAX) {
                            pwr_oft[indx] = MFG_PWR_OFFSET_MAX;
                        }
                        if (pwr_oft[indx] < MFG_PWR_OFFSET_MIN) {
                            pwr_oft[indx] = MFG_PWR_OFFSET_MIN;
                        }

                        //+16, special for mfg rfparam tlv load which -16
                        tlv_pwr_oft_ble[RFTLV_SIZE_TL+indx] = pwr_oft[indx] + 16;

                        cal_cfg_req->pwrcal.channel_pwrcomp_bz[indx] = (int8_t)pwr_oft[indx];
                    }
                    cal_cfg_req->pwr_oft_bz_valid = VALID_RF_PARA;
                    
                    printk("pwr_oft_bz: %d %d %d %d %d\n", 
                           cal_cfg_req->pwrcal.channel_pwrcomp_bz[0], 
                           cal_cfg_req->pwrcal.channel_pwrcomp_bz[1], 
                           cal_cfg_req->pwrcal.channel_pwrcomp_bz[2], 
                           cal_cfg_req->pwrcal.channel_pwrcomp_bz[3], 
                           cal_cfg_req->pwrcal.channel_pwrcomp_bz[4]);
                } else if (id == CFG_COUNTRY_CODE_ID) {
                    char country_code[2];
                    
                    ret = sscanf(eq_pos, "%c%c", country_code, country_code+1);
                    if (ret != 2) {
                        printk("%s, not valid country_code with 2 chars, country_code disabled\n", __func__);
                        
                        ret = -1;
                        break;
                    }
                    
                    cal_cfg_req->country_code = (country_code[0] | (country_code[1]<<8));
                    cal_cfg_req->country_code_valid = VALID_RF_PARA;

                    tlv_country_code[RFTLV_SIZE_TL] = country_code[0];
                    tlv_country_code[RFTLV_SIZE_TL+1] = country_code[1];
                    
                    printk("country_code %c%c\n", country_code[0], country_code[1]);
                } else {
                    printk("%s, cfg id %d is not handled\n", __func__, i);
                    continue;
                }
            } else {
                printk("%s, Not find = and end_pos, %p, %p\n", 
                       __func__, eq_pos, end_pos);
            }
        } else {
            BL_DBG("%s, Not find key in conf file, key:%s\n",
                   __func__, kv[i].key);
        }

        ret = 0;
    }

    if (ret != 0) {
        memset(cal_cfg_req, 0, sizeof(struct mm_cal_cfg_req));
    }

    return ret;
}

int bl_parse_cal_configfile(struct bl_hw *bl_hw, const char *filename, 
                                  struct mm_cal_cfg_req *cal_cfg_req) 
{
    int ret = 0;
    const struct firmware *config_fw;

    printk("enter %s \n",__func__);

    if ((ret = request_firmware(&config_fw, filename, bl_hw->dev))) {
        printk(KERN_CRIT "%s: failed to load %s, err %d\n", 
               __func__, filename, ret);
        
        return ret;
    }

    //set default value, but not enable by VALID_RF_PARA
    cal_cfg_req->tcal.Tchannels[0] = 2412;
    cal_cfg_req->tcal.Tchannels[1] = 2427;
    cal_cfg_req->tcal.Tchannels[2] = 2442;
    cal_cfg_req->tcal.Tchannels[3] = 2457;
    cal_cfg_req->tcal.Tchannels[4] = 2472;
    cal_cfg_req->tcal.linear_or_follow = 1;
    cal_cfg_req->tcal.Troom_os = 255;

    ret = bl_parse_confg_kv(config_fw->data, config_fw->size, 
                            cal_cfg_req, cfg_kv, cfg_num);
    if (ret != 0) {
        printk("%s, parsing %s fail\n", __func__, filename);
    }
    
    /* Release the configuration file */
    release_firmware(config_fw);

    return ret;
}
                                            
/**
 * Parse the Config file used at init time
 */
int bl_parse_country_tx_pwr_configfile(struct bl_hw *bl_hw, const char *filename)
{
#ifndef CFG_FILE_LOCATION_USER_DEFINE
    const struct firmware *config_fw;
#endif
    size_t cfg_buf_len = 1500;
    u8 *cfg_buf = NULL;
    int ret = 0, remain_size = 0;
    const u8 *tag_ptr = NULL, * remain_data = NULL;
    struct country_power_info *pwr_table = NULL;
    u8 country_code[3];
    u8  cnt = 0;
    u8 *addr = NULL;

    BL_DBG(BL_FN_ENTRY_STR);

#ifdef CFG_FILE_LOCATION_USER_DEFINE
    cfg_buf = kzalloc(cfg_buf_len, GFP_KERNEL);
    if (cfg_buf == NULL) {
        printk("%s, fail to alloc cfg_buf\n", __func__);
        return -ENOMEM;
    }
    memset(cfg_buf, 0, cfg_buf_len);

    ret = read_file(filename, cfg_buf, cfg_buf_len-1);
    if (ret < 0) {
        printk("%s, read file error, ret:%d\n", __func__, ret);
        kfree(cfg_buf);
        return ret;
    } 
    cfg_buf[ret] = '\0';
    cfg_buf_len = ret;

    BL_DBG("%s, cfg file:%s len=%d\n", __func__, filename, cfg_buf_len);
#else
    if ((ret = request_firmware(&config_fw, filename, bl_hw->dev))) {
        printk(KERN_CRIT "%s: Failed to get %s (%d)\n", __func__, filename, ret);
        return ret;
    }

    cfg_buf = config_fw->data;
    cfg_buf_len = config_fw->size;
#endif

    tag_ptr = bl_find_tag(cfg_buf, cfg_buf_len, "COUNTRY=", strlen("CN"));

    if (tag_ptr != NULL) {
        memcpy(bl_hw->country_code, tag_ptr, 2);
        bl_hw->country_code[2] = '\0';
        
        printk("COUNTRY=%s from conf \n", bl_hw->country_code);
    } 

    remain_data = cfg_buf;
    remain_size = cfg_buf_len;
    do {
        tag_ptr = bl_find_tag(remain_data, remain_size,
                              "RATE_PWR=", CFG_FLIE_WIFI_PWR_VALUE_LEN);
        if (tag_ptr) {
            /*  copy country code */
            memcpy(country_code, tag_ptr, 2);
            country_code[2] = '\0';
            bl_find_country_pwr_table(&pwr_table, country_code);
            if (pwr_table) {
                /* copy rate power value */
                addr = pwr_table->rate_pwr;
                if ((ret = sscanf(tag_ptr + 3,
                       "%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd",
                       addr,     addr + 1, addr + 2, addr + 3, addr + 4, addr + 5, 
                       addr + 6, addr + 7, addr + 8, addr + 9,
                       addr + 10, addr + 11, addr + 12, addr + 13, addr + 14,
                       addr + 15, addr + 16, addr + 17, addr + 18, addr + 19,
                       addr + 20, addr + 21, addr + 22, addr + 23, addr + 24,
                       addr + 25, addr + 26, addr + 27, addr + 28, addr + 29,
                       addr + 30, addr + 33, addr + 32, addr + 33, addr + 34, 
                       addr + 35, addr + 36, addr + 37, addr + 38, addr + 39,
                       addr + 40, addr + 44, addr + 42, addr + 44, addr + 44,
                       addr + 45, addr + 46, addr + 47) != RATE_MAX))
                    {
                        printk("ERR:RATE_PWR %s len %hhd\n", pwr_table->country_code, ret);
                    }

                // re-read line to avoid value invalid
          //      for (cnt = 0; cnt < RATE_MAX; cnt++)
          //          sscanf(tag_ptr + 3 + 3 * cnt, "%d,", addr + cnt);
                
                BL_DBG("Country %s dump RATE_PWR:\n",country_code);
                for (cnt = 0; cnt < RATE_MAX; cnt++) {
                    BL_DBG(":%d",pwr_table->rate_pwr[cnt]);
                }
                BL_DBG("dump RATE_PWR end\n");
            }
        
            remain_data = tag_ptr + CFG_FLIE_WIFI_PWR_VALUE_LEN;
            remain_size = cfg_buf_len - (tag_ptr - cfg_buf);
        }
    } while (tag_ptr && (remain_size > CFG_FLIE_WIFI_PWR_VALUE_LEN));

    /* Check BLE power */
    remain_data = cfg_buf;
    remain_size = cfg_buf_len;
    bl_hw->ble_pwr = 0;
    
    do {
        tag_ptr = bl_find_tag(remain_data, remain_size,
                              "BLE_PWR=", CFG_FLIE_BLE_PWR_VALUE_LEN);

        if (tag_ptr) {
            memcpy(country_code, tag_ptr, 2);
            country_code[2] = '\0';
            
            if (strncmp(country_code, bl_hw->country_code, strlen(bl_hw->country_code))) {
                BL_DBG("%s ble_pwr=%c%c not target country(%s)\n", country_code,
                       *(tag_ptr + 3), *(tag_ptr + 4), bl_hw->country_code);
            } else {
                if((ret = sscanf(tag_ptr + 3,"%hhd", &bl_hw->ble_pwr) != CFG_FLIE_BLE_PWR_CNT)) {
                    printk("ERR:ble_pwr %s len %hhd\n", country_code, ret);
                } else {
                    BL_DBG("country %s ble_pwr=%d\n", country_code, bl_hw->ble_pwr);
                    break;
                }
            }
            
            remain_data = tag_ptr + CFG_FLIE_BLE_PWR_VALUE_LEN;
            remain_size = cfg_buf_len - (tag_ptr - cfg_buf);
        }
    } while (tag_ptr && (remain_size > CFG_FLIE_BLE_PWR_VALUE_LEN));

    /* Check BT power */
    remain_data = cfg_buf;
    remain_size = cfg_buf_len;
    memset(bl_hw->bt_pwr, 0, sizeof(bl_hw->bt_pwr));
    do {
        tag_ptr = bl_find_tag(remain_data, remain_size,
                                "BT_PWR=", CFG_FLIE_BT_PWR_VALUE_LEN);
                                
        if (tag_ptr) {
            memcpy(country_code, tag_ptr, 2);
            country_code[2] = '\0';
            
            if (strncmp(country_code, bl_hw->country_code, strlen(bl_hw->country_code))) {
                BL_DBG("%s bt_pwr=%c%c not target country %s\n", 
                       country_code, *(tag_ptr + 3), *(tag_ptr + 4), 
                       bl_hw->country_code);
            } else {
                if((ret = sscanf(tag_ptr + 3,"%hhd,%hhd,%hhd",
                                  &bl_hw->bt_pwr[0], &bl_hw->bt_pwr[1], 
                                  &bl_hw->bt_pwr[2]) != CFG_FLIE_BT_PWR_CNT)) {
                    printk("ERR:bt_pwr %s len %hhd\n", pwr_table->country_code, ret);
                } else {
                    BL_DBG("country %s bt_pwr=%d,%d,%d\n", country_code, 
                           bl_hw->bt_pwr[0], bl_hw->bt_pwr[1],bl_hw->bt_pwr[2]);
                    break;
                }
            }
            
            remain_data = tag_ptr + CFG_FLIE_BT_PWR_VALUE_LEN;
            remain_size = cfg_buf_len - (tag_ptr - cfg_buf);
        }
    } while (tag_ptr && (remain_size > CFG_FLIE_BT_PWR_VALUE_LEN));

//end:
    /* Release the configuration file */
#ifdef CFG_FILE_LOCATION_USER_DEFINE
    if (cfg_buf)
        kfree(cfg_buf);
#else
    release_firmware(config_fw);
#endif

    return 0;
}

int bl_country_pwr_update(struct bl_hw * bl_hw, char * country)
{    
    struct country_power_info *pwr_table = NULL;
    struct mm_cal_cfg_req req;
    int cnt, ret = 0;

    memset(&req, 0, sizeof(req));

    if (!bl_find_country_pwr_table(&pwr_table, country) && pwr_table) {
        req.pwr_11b_valid = VALID_RF_PARA;
        req.pwr_11g_valid = VALID_RF_PARA;
        req.pwr_11n_ht20_valid = VALID_RF_PARA;
        req.pwr_11n_ht40_valid = VALID_RF_PARA;
        req.pwr_11ax_he20_valid = VALID_RF_PARA;
        req.pwr_11ax_he40_valid = VALID_RF_PARA;
        
        memcpy(req.pwrtarget.pwr_11b, pwr_table->rate_pwr, 
               PHY_11B_RATE_NUM);
        memcpy(req.pwrtarget.pwr_11g, pwr_table->rate_pwr+PHY_11B_RATE_NUM, 
               PHY_11G_RATE_NUM);
        memcpy(req.pwrtarget.pwr_11n_ht20, 
               pwr_table->rate_pwr+PHY_11B_RATE_NUM + PHY_11G_RATE_NUM, 
               PHY_11N_RATE_NUM);
        memcpy(req.pwrtarget.pwr_11n_ht40, 
               pwr_table->rate_pwr+PHY_11B_RATE_NUM + PHY_11G_RATE_NUM + 
               PHY_11N_RATE_NUM, 
               PHY_11N_RATE_NUM);
        memcpy(req.pwrtarget.pwr_11ax_he20, 
               &pwr_table->rate_pwr[PHY_11B_RATE_NUM + 
               PHY_11G_RATE_NUM + PHY_11N_RATE_NUM * 2],
               PHY_11AX_RATE_NUM);
        memcpy(req.pwrtarget.pwr_11ax_he40, 
               &pwr_table->rate_pwr[PHY_11B_RATE_NUM + 
               PHY_11G_RATE_NUM + PHY_11N_RATE_NUM * 2 + PHY_11AX_RATE_NUM],
               PHY_11AX_RATE_NUM);
        
        BL_DBG("Country %s dump RATE_PWR:\n",country);
        for (cnt = 0; cnt < RATE_MAX; cnt++)
            BL_DBG("%d ",pwr_table->rate_pwr[cnt]);
        BL_DBG("dump RATE_PWR end\n");

        req.pwr_ble_valid = VALID_RF_PARA;
        req.pwrtarget.pwr_ble = pwr_table->ble_pwr;

        req.pwr_bt_valid = VALID_RF_PARA;
        memcpy(req.pwrtarget.pwr_bt, pwr_table->bt_pwr, sizeof(pwr_table->bt_pwr));

        BL_DBG("%s ble_valid=%d, bt_valid=%d, pwr_ble=%d, pwr_bt=%d,%d,%d", __func__,
                req.pwr_ble_valid, req.pwr_bt_valid, req.pwrtarget.pwr_ble,
                req.pwrtarget.pwr_bt[0], req.pwrtarget.pwr_bt[1],
                req.pwrtarget.pwr_bt[2]);

        ret = bl_send_cal_cfg(bl_hw, &req);
        if (ret) 
            printk("%s, error=%d\n", __func__, ret);
    }

    return ret;
}

uint32_t bl_convert_cal_to_tlv(uint8_t *hex_buf, uint32_t max_len)
{
    uint8_t *pos = NULL;

    //copy data to buf
    pos = hex_buf;
    
    //tlv_sign header
    memcpy(pos, tlv_sign, sizeof(tlv_sign));
    pos += sizeof(tlv_sign);
    //xtal mode
    memcpy(pos, tlv_xtal_mode, sizeof(tlv_xtal_mode));
    pos += sizeof(tlv_xtal_mode);
    //xtal capcode
    memcpy(pos, tlv_xtal_capcode, sizeof(tlv_xtal_capcode));
    pos += sizeof(tlv_xtal_capcode);        
    //power mode
    memcpy(pos, tlv_pwr_mode, sizeof(tlv_pwr_mode));
    pos += sizeof(tlv_pwr_mode);
    //11b power
    memcpy(pos, tlv_pwr_11b, sizeof(tlv_pwr_11b));
    pos += sizeof(tlv_pwr_11b);
    //11g power
    memcpy(pos, tlv_pwr_11g, sizeof(tlv_pwr_11g));
    pos += sizeof(tlv_pwr_11g);
    //11n power
    memcpy(pos, tlv_pwr_11n, sizeof(tlv_pwr_11n));
    pos += sizeof(tlv_pwr_11n);
    //11n ht40 power
    memcpy(pos, tlv_pwr_11n_ht40, sizeof(tlv_pwr_11n_ht40));
    pos += sizeof(tlv_pwr_11n_ht40);
    //11ac vht20 power
    memcpy(pos, tlv_pwr_11ac_vht20, sizeof(tlv_pwr_11ac_vht20));
    pos += sizeof(tlv_pwr_11ac_vht20);
    //11ac vht40 power
    memcpy(pos, tlv_pwr_11ac_vht40, sizeof(tlv_pwr_11ac_vht40));
    pos += sizeof(tlv_pwr_11ac_vht40);
    //11ac vht80 power
    memcpy(pos, tlv_pwr_11ac_vht80, sizeof(tlv_pwr_11ac_vht80));
    pos += sizeof(tlv_pwr_11ac_vht80);
    //11ax he20 power
    memcpy(pos, tlv_pwr_11ax_he20, sizeof(tlv_pwr_11ax_he20));
    pos += sizeof(tlv_pwr_11ax_he20);
    //11ax he40 power
    memcpy(pos, tlv_pwr_11ax_he40, sizeof(tlv_pwr_11ax_he40));
    pos += sizeof(tlv_pwr_11ax_he40);
    //11ax he80 power
    memcpy(pos, tlv_pwr_11ax_he80, sizeof(tlv_pwr_11ax_he80));
    pos += sizeof(tlv_pwr_11ax_he80);
    //11ax he160 power
    memcpy(pos, tlv_pwr_11ax_he160, sizeof(tlv_pwr_11ax_he160));
    pos += sizeof(tlv_pwr_11ax_he160);
    //powr offset
    memcpy(pos, tlv_pwr_offset, sizeof(tlv_pwr_offset));
    pos += sizeof(tlv_pwr_offset);
    //powr offset lp
    memcpy(pos, tlv_pwr_offset_lp, sizeof(tlv_pwr_offset_lp));
    pos += sizeof(tlv_pwr_offset_lp);        
    //tlv_en_tcal
    memcpy(pos, tlv_en_tcal, sizeof(tlv_en_tcal));
    pos += sizeof(tlv_en_tcal);
    //tlv_linear_or_follow
    memcpy(pos, tlv_linear_or_follow, sizeof(tlv_linear_or_follow));
    pos += sizeof(tlv_linear_or_follow);
    //Tchannel
    memcpy(pos, tlv_tchannel, sizeof(tlv_tchannel));
    pos += sizeof(tlv_tchannel);
    //Tchannel_os
    memcpy(pos, tlv_tchan_os, sizeof(tlv_tchan_os));
    pos += sizeof(tlv_tchan_os);			
    //Tchannel_os_low
    memcpy(pos, tlv_tchan_os_low, sizeof(tlv_tchan_os_low));
    pos += sizeof(tlv_tchan_os_low);
    //temperature, tlv_troom_os
    memcpy(pos, tlv_troom_os, sizeof(tlv_troom_os));
    pos += sizeof(tlv_troom_os);
    //tlv_pwr_ble
    memcpy(pos, tlv_pwr_ble, sizeof(tlv_pwr_ble));
    pos += sizeof(tlv_pwr_ble);
    //tlv_pwr_oft_ble
    memcpy(pos, tlv_pwr_oft_ble, sizeof(tlv_pwr_oft_ble));
    pos += sizeof(tlv_pwr_oft_ble);
    //tlv_country_code
    memcpy(pos, tlv_country_code, sizeof(tlv_country_code));
    pos += sizeof(tlv_country_code);
    //tlv_pwr_bt
    memcpy(pos, tlv_pwr_bt, sizeof(tlv_pwr_bt));
    pos += sizeof(tlv_pwr_bt);
    //tlv_pwr_zb
    memcpy(pos, tlv_pwr_zb, sizeof(tlv_pwr_zb));
    pos += sizeof(tlv_pwr_zb);
    //tlv_en_tcapcal
    memcpy(pos, tlv_en_tcapcal, sizeof(tlv_en_tcapcal));
    pos += sizeof(tlv_en_tcapcal);
    //tlv_tcap_tsen
    memcpy(pos, tlv_tcap_tsen, sizeof(tlv_tcap_tsen));
    pos += sizeof(tlv_tcap_tsen);
    //tlv_tcap_capcode
    memcpy(pos, tlv_tcap_capcode, sizeof(tlv_tcap_capcode));
    pos += sizeof(tlv_tcap_capcode);

    if (pos - hex_buf > max_len)
        printk("%s, overflow\n", __func__);

    return (uint32_t)(pos-hex_buf);
}

#if 0


/**
 * Parse the Config file used at init time
 */
int bl_parse_configfile(struct bl_hw *bl_hw, const char *filename,
                             struct bl_conf_file *config)
{
    const struct firmware *config_fw;
    static const u8 bfl_oui[3] = {0x7C, 0xB9, 0x4C};

    int ret;
    const u8 *tag_ptr;

    BL_DBG(BL_FN_ENTRY_STR);

    if ((ret = request_firmware(&config_fw, filename, bl_hw->dev))) {
        printk("%s: no %s, (%d)\n", __func__, filename, ret);
        memcpy(config->mac_addr, bfl_oui, 3);
        get_random_bytes(config->mac_addr + 3, 3);
        return ret;
    }

    /* Get MAC Address */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                          "MAC_ADDR=", strlen("00:00:00:00:00:00"));
    if (tag_ptr != NULL) {
        u8 *addr = config->mac_addr;
        
        if (sscanf(tag_ptr,
                   "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   addr + 0, addr + 1, addr + 2,
                   addr + 3, addr + 4, addr + 5) != ETH_ALEN) {
            memcpy(config->mac_addr, bfl_oui, 3);
            get_random_bytes(config->mac_addr + 3, 3);
        }
    } else {
        memcpy(config->mac_addr, bfl_oui, 3);
        get_random_bytes(config->mac_addr + 3, 3);
    }

    printk("MAC Address is:\n%pM\n", config->mac_addr);

    /* Release the configuration file */
    release_firmware(config_fw);

    return 0;
}
#endif

/**
 * Parse the Config file used at init time
 */
int bl_parse_phy_configfile(struct bl_hw *bl_hw, const char *filename,
                                   struct bl_phy_conf_file *config, int path)
{
    const struct firmware *config_fw;
    int ret;
    const u8 *tag_ptr;

    BL_DBG(BL_FN_ENTRY_STR);

    if ((ret = request_firmware(&config_fw, filename, bl_hw->dev))) {
        printk(KERN_CRIT "%s: Failed to get %s (%d)\n", __func__, filename, ret);
        return ret;
    }

    /* Get Trident path mapping */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                          "TRD_PATH_MAPPING=", strlen("00"));

    if (tag_ptr != NULL) {
        u8 val;
        
        if (sscanf(tag_ptr, "%hhx", &val) == 1)
            config->trd.path_mapping = val;
        else
            config->trd.path_mapping = path;
    } else
        config->trd.path_mapping = path;

    BL_DBG("Trident path mapping is: %d\n", config->trd.path_mapping);

    /* Get DC offset compensation */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "TX_DC_OFF_COMP=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->trd.tx_dc_off_comp) != 1)
            config->trd.tx_dc_off_comp = 0;
    } else
        config->trd.tx_dc_off_comp = 0;

    BL_DBG("TX DC offset compensation is: %08X\n", config->trd.tx_dc_off_comp);

    /* Get Karst TX IQ compensation value for path0 on 2.4GHz */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "KARST_TX_IQ_COMP_2_4G_PATH_0=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_2_4G[0]) != 1)
            config->karst.tx_iq_comp_2_4G[0] = 0x01000000;
    } else
        config->karst.tx_iq_comp_2_4G[0] = 0x01000000;

    BL_DBG("Karst TX IQ compensation for path 0 on 2.4GHz is: %08X\n", 
            config->karst.tx_iq_comp_2_4G[0]);

    /* Get Karst TX IQ compensation value for path1 on 2.4GHz */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "KARST_TX_IQ_COMP_2_4G_PATH_1=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_2_4G[1]) != 1)
            config->karst.tx_iq_comp_2_4G[1] = 0x01000000;
    } else
        config->karst.tx_iq_comp_2_4G[1] = 0x01000000;

    BL_DBG("Karst TX IQ compensation for path 1 on 2.4GHz is: %08X\n",
           config->karst.tx_iq_comp_2_4G[1]);

    /* Get Karst RX IQ compensation value for path0 on 2.4GHz */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "KARST_RX_IQ_COMP_2_4G_PATH_0=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_2_4G[0]) != 1)
            config->karst.rx_iq_comp_2_4G[0] = 0x01000000;
    } else
        config->karst.rx_iq_comp_2_4G[0] = 0x01000000;

    BL_DBG("Karst RX IQ compensation for path 0 on 2.4GHz is: %08X\n",
           config->karst.rx_iq_comp_2_4G[0]);

    /* Get Karst RX IQ compensation value for path1 on 2.4GHz */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "KARST_RX_IQ_COMP_2_4G_PATH_1=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_2_4G[1]) != 1)
            config->karst.rx_iq_comp_2_4G[1] = 0x01000000;
    } else
        config->karst.rx_iq_comp_2_4G[1] = 0x01000000;

    BL_DBG("Karst RX IQ compensation for path 1 on 2.4GHz is: %08X\n",
           config->karst.rx_iq_comp_2_4G[1]);

    /* Get Karst TX IQ compensation value for path0 on 5GHz */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "KARST_TX_IQ_COMP_5G_PATH_0=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_5G[0]) != 1)
            config->karst.tx_iq_comp_5G[0] = 0x01000000;
    } else
        config->karst.tx_iq_comp_5G[0] = 0x01000000;

    BL_DBG("Karst TX IQ compensation for path 0 on 5GHz is: %08X\n",
            config->karst.tx_iq_comp_5G[0]);

    /* Get Karst TX IQ compensation value for path1 on 5GHz */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "KARST_TX_IQ_COMP_5G_PATH_1=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_5G[1]) != 1)
            config->karst.tx_iq_comp_5G[1] = 0x01000000;
    } else
        config->karst.tx_iq_comp_5G[1] = 0x01000000;

    BL_DBG("Karst TX IQ compensation for path 1 on 5GHz is: %08X\n",
           config->karst.tx_iq_comp_5G[1]);

    /* Get Karst RX IQ compensation value for path0 on 5GHz */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "KARST_RX_IQ_COMP_5G_PATH_0=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_5G[0]) != 1)
            config->karst.rx_iq_comp_5G[0] = 0x01000000;
    } else
        config->karst.rx_iq_comp_5G[0] = 0x01000000;

    BL_DBG("Karst RX IQ compensation for path 0 on 5GHz is: %08X\n", 
           config->karst.rx_iq_comp_5G[0]);

    /* Get Karst RX IQ compensation value for path1 on 5GHz */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "KARST_RX_IQ_COMP_5G_PATH_1=", strlen("00000000"));
    if (tag_ptr != NULL) {
        if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_5G[1]) != 1)
            config->karst.rx_iq_comp_5G[1] = 0x01000000;
    } else
        config->karst.rx_iq_comp_5G[1] = 0x01000000;

    BL_DBG("Karst RX IQ compensation for path 1 on 5GHz is: %08X\n",
           config->karst.rx_iq_comp_5G[1]);

    /* Get Karst default path */
    tag_ptr = bl_find_tag(config_fw->data, config_fw->size,
                            "KARST_DEFAULT_PATH=", strlen("00"));
    if (tag_ptr != NULL) {
        u8 val;
        
        if (sscanf(tag_ptr, "%hhx", &val) == 1)
            config->karst.path_used = val;
        else
            config->karst.path_used = path;
    } else
        config->karst.path_used = path;

    BL_DBG("Karst default path is: %d\n", config->karst.path_used);

    /* Release the configuration file */
    release_firmware(config_fw);

    return 0;
}



