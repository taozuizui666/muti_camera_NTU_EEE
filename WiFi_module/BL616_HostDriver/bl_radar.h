/**
 ******************************************************************************
 *
 *  @file bl_radar.h
 *
 *  @brief Functions to handle radar detection
 *
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

#ifndef _BL_RADAR_H_
#define _BL_RADAR_H_

#include <linux/nl80211.h>

struct bl_vif;
struct bl_hw;

enum bl_radar_chain {
    BL_RADAR_RIU = 0,
    BL_RADAR_FCU,
    BL_RADAR_LAST
};

enum bl_radar_detector {
    BL_RADAR_DETECT_DISABLE = 0, /* Ignore radar pulses */
    BL_RADAR_DETECT_ENABLE  = 1, /* Process pattern detection but do not
                                      report radar to upper layer (for test) */
    BL_RADAR_DETECT_REPORT  = 2  /* Process pattern detection and report
                                      radar to upper layer. */
};

#ifdef CONFIG_BL_RADAR
#include <linux/workqueue.h>
#include <linux/spinlock.h>

#define BL_RADAR_PULSE_MAX  32

/**
 * struct bl_radar_pulses - List of pulses reported by HW
 * @index: write index
 * @count: number of valid pulses
 * @buffer: buffer of pulses
 */
struct bl_radar_pulses {
    /* Last radar pulses received */
    int index;
    int count;
    u32 buffer[BL_RADAR_PULSE_MAX];
};

/**
 * struct dfs_pattern_detector - DFS pattern detector
 * @region: active DFS region, NL80211_DFS_UNSET until set
 * @num_radar_types: number of different radar types
 * @last_pulse_ts: time stamp of last valid pulse in usecs
 * @prev_jiffies:
 * @radar_detector_specs: array of radar detection specs
 * @channel_detectors: list connecting channel_detector elements
 */
struct dfs_pattern_detector {
    u8 enabled;
    enum nl80211_dfs_regions region;
    u8 num_radar_types;
    u64 last_pulse_ts;
    u32 prev_jiffies;
    const struct radar_detector_specs *radar_spec;
    struct list_head detectors[];
};

#define NX_NB_RADAR_DETECTED 4

/**
 * struct bl_radar_detected - List of radar detected
 */
struct bl_radar_detected {
    u16 index;
    u16 count;
    s64 time[NX_NB_RADAR_DETECTED];
    s16 freq[NX_NB_RADAR_DETECTED];
};


struct bl_radar {
    struct bl_radar_pulses pulses[BL_RADAR_LAST];
    struct dfs_pattern_detector *dpd[BL_RADAR_LAST];
    struct bl_radar_detected detected[BL_RADAR_LAST];
    struct work_struct detection_work;  /* Work used to process radar pulses */
    spinlock_t lock;                    /* lock for pulses processing */

    /* In softmac cac is handled by mac80211 */
#ifdef CONFIG_BL_FULLMAC
    struct delayed_work cac_work;       /* Work used to handle CAC */
    struct bl_vif *cac_vif;           /* vif on which we started CAC */
#endif
};

bool bl_radar_detection_init(struct bl_radar *radar);
void bl_radar_detection_deinit(struct bl_radar *radar);
bool bl_radar_set_domain(struct bl_radar *radar,
                           enum nl80211_dfs_regions region);
void bl_radar_detection_enable(struct bl_radar *radar, u8 enable, u8 chain);
bool bl_radar_detection_is_enable(struct bl_radar *radar, u8 chain);
void bl_radar_start_cac(struct bl_radar *radar, u32 cac_time_ms,
                          struct bl_vif *vif);
void bl_radar_cancel_cac(struct bl_radar *radar);
void bl_radar_detection_enable_on_cur_channel(struct bl_hw *bl_hw);
int  bl_radar_dump_pattern_detector(char *buf, size_t len,
                                      struct bl_radar *radar, u8 chain);
int  bl_radar_dump_radar_detected(char *buf, size_t len,
                                    struct bl_radar *radar, u8 chain);

#else

struct bl_radar {
};

static inline bool bl_radar_detection_init(struct bl_radar *radar)
{return true;}

static inline void bl_radar_detection_deinit(struct bl_radar *radar)
{}

static inline bool bl_radar_set_domain(struct bl_radar *radar,
                                         enum nl80211_dfs_regions region)
{return true;}

static inline void bl_radar_detection_enable(struct bl_radar *radar,
                                               u8 enable, u8 chain)
{}

static inline bool bl_radar_detection_is_enable(struct bl_radar *radar,
                                                 u8 chain)
{return false;}

static inline void bl_radar_start_cac(struct bl_radar *radar,
                                        u32 cac_time_ms, struct bl_vif *vif)
{}

static inline void bl_radar_cancel_cac(struct bl_radar *radar)
{}

static inline void bl_radar_detection_enable_on_cur_channel(struct bl_hw *bl_hw)
{}

static inline int bl_radar_dump_pattern_detector(char *buf, size_t len,
                                                   struct bl_radar *radar,
                                                   u8 chain)
{return 0;}

static inline int bl_radar_dump_radar_detected(char *buf, size_t len,
                                                 struct bl_radar *radar,
                                                 u8 chain)
{return 0;}

#endif /* CONFIG_BL_RADAR */

#endif // _BL_RADAR_H_
