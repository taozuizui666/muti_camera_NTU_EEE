/**
 *****************************************************************************
 *
 *  @file hal_desc.c
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


#include <linux/string.h>
#include "bl_hal_desc.h"

const struct bl_legrate legrates_lut[] = {
    [0]  = { .idx = 0,  .rate = 10 },
    [1]  = { .idx = 1,  .rate = 20 },
    [2]  = { .idx = 2,  .rate = 55 },
    [3]  = { .idx = 3,  .rate = 110 },
    [4]  = { .idx = -1, .rate = 0 },
    [5]  = { .idx = -1, .rate = 0 },
    [6]  = { .idx = -1, .rate = 0 },
    [7]  = { .idx = -1, .rate = 0 },
    [8]  = { .idx = 10, .rate = 480 },
    [9]  = { .idx = 8,  .rate = 240 },
    [10] = { .idx = 6,  .rate = 120 },
    [11] = { .idx = 4,  .rate = 60 },
    [12] = { .idx = 11, .rate = 540 },
    [13] = { .idx = 9,  .rate = 360 },
    [14] = { .idx = 7,  .rate = 180 },
    [15] = { .idx = 5,  .rate = 90 },
};

/**
 * bl_machw_type - Return type (NX or HE) MAC HW is used
 *
 */
int bl_machw_type(uint32_t machw_version_2)
{
    uint32_t machw_um_ver_maj = (machw_version_2 >> 4) & 0x7;

    if (machw_um_ver_maj >= 4)
        return BL_MACHW_HE;
    else
        return BL_MACHW_NX;
}

