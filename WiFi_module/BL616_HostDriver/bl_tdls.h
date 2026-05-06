/**
 ******************************************************************************
 *
 *  @file bl_tdls.h
 *
 *  @brief TDLS function declarations
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


#ifndef BL_TDLS_H_
#define BL_TDLS_H_

#include "bl_defs.h"

struct ieee_types_header {
    u8 element_id;
    u8 len;
} __packed;

struct ieee_types_bss_co_2040 {
    struct ieee_types_header ieee_hdr;
    u8 bss_2040co;
} __packed;

struct ieee_types_extcap {
    struct ieee_types_header ieee_hdr;
    u8 ext_capab[8];
} __packed;

struct ieee_types_vht_cap {
    struct ieee_types_header ieee_hdr;
    struct ieee80211_vht_cap vhtcap;
} __packed;

struct ieee_types_vht_oper {
    struct ieee_types_header ieee_hdr;
    struct ieee80211_vht_operation vhtoper;
} __packed;

struct ieee_types_aid {
    struct ieee_types_header ieee_hdr;
    u16 aid;
} __packed;

int bl_tdls_send_mgmt_packet_data(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                         const u8 *peer, u8 action_code, u8 dialog_token,
                         u16 status_code, u32 peer_capability, bool initiator,
                         const u8 *extra_ies, size_t extra_ies_len, u8 oper_class,
                         struct cfg80211_chan_def *chandef);

#endif /* BL_TDLS_H_ */
