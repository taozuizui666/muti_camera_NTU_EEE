/**
 ****************************************************************************************
 *
 *  @file bl_mesh.c
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


/**
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "bl_mesh.h"

/**
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */

struct bl_mesh_proxy *bl_get_mesh_proxy_info(struct bl_vif *p_bl_vif, u8 *p_sta_addr, bool local)
{
    struct bl_mesh_proxy *p_mesh_proxy = NULL;
    struct bl_mesh_proxy *p_cur_proxy;

    /* Look for proxied devices with provided address */
    list_for_each_entry(p_cur_proxy, &p_bl_vif->ap.proxy_list, list) {
        if (p_cur_proxy->local != local) {
            continue;
        }

        if (!memcmp(&p_cur_proxy->ext_sta_addr, p_sta_addr, ETH_ALEN)) {
            p_mesh_proxy = p_cur_proxy;
            break;
        }
    }

    /* Return the found information */
    return p_mesh_proxy;
}
