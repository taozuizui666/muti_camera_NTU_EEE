/**
 ******************************************************************************
 *
 *  @file bl_mu_group.h
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

#ifndef _BL_MU_GROUP_H_
#define _BL_MU_GROUP_H_

#include <linux/workqueue.h>
#include <linux/semaphore.h>

struct bl_hw;
struct bl_sta;

#ifdef CONFIG_BL_MUMIMO_TX

/**
 * struct bl_sta_group_info - Group Information for a STA
 *
 * @active: node for @mu->active_sta list
 * @update: node for @mu->update_sta list
 * @cnt: Number of groups the STA belongs to
 * @map: Bitfield of groups the sta belongs to
 * @traffic: Number of buffers sent since previous group selection
 * @group: Id of the group selected by previous group selection
 *         (cf @bl_mu_group_sta_select)
 */
struct bl_sta_group_info {
    struct list_head active;
    struct list_head update;
    u16 last_update;
    int cnt;
    u64 map;
    int traffic;
    u8  group;
};

/**
 * struct mu_group_info - Information about the users of a group
 *
 * @list: node for mu->active_groups
 * @group_id: Group identifier
 * @user_cnt: Number of the users in the group
 * @users: Pointer to the sta, ordered by user position
 */
struct bl_mu_group {
    struct list_head list;
    int group_id;
    int user_cnt;
    struct bl_sta *users[CONFIG_USER_MAX];
};

/**
 * struct bl_mu_info - Information about all MU group
 *
 * @active_groups: List of all possible groups. Ordered from the most recently
 *                 used one to the least one (and possibly never used)
 * @active_sta: List of MU beamformee sta that have been active (since previous
 *              group update). Ordered from the most recently active.
 * @update_sta: List of sta whose group information has changed and need to be
 *              updated at fw level
 * @groups: Table of all groups
 * @group_work: Work item used to schedule group update
 * @update_count: Counter used to identify the last group formation update.
 *                (cf bl_sta_group_info.last_update)
 * @lock: Lock taken during group update. If tx happens lock is taken, then tx
 *        will not used MU.
 * @next_group_assign: Next time the group selection should be run
 *                     (ref @bl_mu_group_sta_select)
 * @group_cnt: Number of group created
 */
struct bl_mu_info {
    struct list_head active_groups;
    struct list_head active_sta;
    struct list_head update_sta;
    struct bl_mu_group groups[NX_MU_GROUP_MAX];
    struct delayed_work group_work;
    u16 update_count;
    struct semaphore lock;
    unsigned long next_group_select;
    u8 group_cnt;
};

#define BL_SU_GROUP BIT_ULL(0)
#define BL_MU_GROUP_MASK 0x7ffffffffffffffeULL
#define BL_MU_GROUP_INTERVAL 200 /* in ms */
#define BL_MU_GROUP_SELECT_INTERVAL 100 /* in ms */
// minimum traffic in a BL_MU_GROUP_SELECT_INTERVAL to consider the sta
#define BL_MU_GROUP_MIN_TRAFFIC 50 /* in number of packet */


#define BL_GET_FIRST_GROUP_ID(map) (fls64(map) - 1)

#define group_sta_for_each(sta, id, map)                                \
    map = sta->group_info.map & BL_MU_GROUP_MASK;                     \
    for (id = (fls64(map) - 1) ; id > 0 ;                               \
         map &= ~(u64)BIT_ULL(id), id = (fls64(map) - 1))

#define group_for_each(id, map)                                         \
    for (id = (fls64(map) - 1) ; id > 0 ;                               \
         map &= ~(u64)BIT_ULL(id), id = (fls64(map) - 1))

#define BL_MUMIMO_INFO_POS_ID(info) (((info) >> 6) & 0x3)
#define BL_MUMIMO_INFO_GROUP_ID(info) ((info) & 0x3f)

static inline
struct bl_mu_group *bl_mu_group_from_id(struct bl_mu_info *mu, int id)
{
    if (id > NX_MU_GROUP_MAX)
        return NULL;

    return &mu->groups[id - 1];
}


void bl_mu_group_sta_init(struct bl_sta *sta,
                            const struct ieee80211_vht_cap *vht_cap);
void bl_mu_group_sta_del(struct bl_hw *bl_hw, struct bl_sta *sta);
u64 bl_mu_group_sta_get_map(struct bl_sta *sta);
int bl_mu_group_sta_get_pos(struct bl_hw *bl_hw, struct bl_sta *sta,
                              int group_id);

void bl_mu_group_init(struct bl_hw *bl_hw);

void bl_mu_set_active_sta(struct bl_hw *bl_hw, struct bl_sta *sta,
                            int traffic);
void bl_mu_set_active_group(struct bl_hw *bl_hw, int group_id);
void bl_mu_group_sta_select(struct bl_hw *bl_hw);


#else /* ! CONFIG_BL_MUMIMO_TX */

static inline
void bl_mu_group_sta_init(struct bl_sta *sta,
                            const struct ieee80211_vht_cap *vht_cap)
{}

static inline
void bl_mu_group_sta_del(struct bl_hw *bl_hw, struct bl_sta *sta)
{}

static inline
u64 bl_mu_group_sta_get_map(struct bl_sta *sta)
{
    return 0;
}

static inline
int bl_mu_group_sta_get_pos(struct bl_hw *bl_hw, struct bl_sta *sta,
                              int group_id)
{
    return 0;
}

static inline
void bl_mu_group_init(struct bl_hw *bl_hw)
{}

static inline
void bl_mu_set_active_sta(struct bl_hw *bl_hw, struct bl_sta *sta,
                            int traffic)
{}

static inline
void bl_mu_set_active_group(struct bl_hw *bl_hw, int group_id)
{}

static inline
void bl_mu_group_sta_select(struct bl_hw *bl_hw)
{}

#endif /* CONFIG_BL_MUMIMO_TX */

#endif /* _BL_MU_GROUP_H_ */

