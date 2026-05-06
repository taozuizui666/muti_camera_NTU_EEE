/**
 ****************************************************************************************
 *
 *  @file bl_mesh.h
 *
 *  @brief VHT Beamformer function declarations
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


#ifndef _BL_MESH_H_
#define _BL_MESH_H_

/**
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "bl_defs.h"

/**
 * DEFINES
 ****************************************************************************************
 */

/**
 * TYPE DEFINITIONS
 ****************************************************************************************
 */

/**
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief TODO [LT]
 ****************************************************************************************
 */
struct bl_mesh_proxy *bl_get_mesh_proxy_info(struct bl_vif *p_bl_vif, u8 *p_sta_addr, bool local);

#endif /* _BL_MESH_H_ */
