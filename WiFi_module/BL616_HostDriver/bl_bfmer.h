/**
 ******************************************************************************
 *
 *  @file bl_bfmer.h
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


#ifndef _BL_BFMER_H_
#define _BL_BFMER_H_

/**
 * INCLUDE FILES
 ******************************************************************************
 */

#include "bl_defs.h"

/**
 * DEFINES
 ******************************************************************************
 */

/// Maximal supported report length (in bytes)
#define BL_BFMER_REPORT_MAX_LEN     2048

/// Size of the allocated report space (twice the maximum report length)
#define BL_BFMER_REPORT_SPACE_SIZE  (BL_BFMER_REPORT_MAX_LEN * 2)

/**
 * TYPE DEFINITIONS
 ******************************************************************************
 */

/*
 * Structure used to store a beamforming report.
 */
struct bl_bfmer_report {
    dma_addr_t dma_addr;    /* Virtual address provided to MAC for
                               DMA transfer of the Beamforming Report */
    unsigned int length;    /* Report Length */
    u8 report[1];           /* Report to be used for VHT TX Beamforming */
};

/**
 * FUNCTION DECLARATIONS
 ******************************************************************************
 */

/**
 ******************************************************************************
 * @brief Allocate memory aiming to contains the Beamforming Report received
 * from a Beamformee capable capable.
 * The providing length shall be large enough to contain the VHT Compressed
 * Beaforming Report and the MU Exclusive part.
 * It also perform a DMA Mapping providing an address to be provided to the HW
 * responsible for the DMA transfer of the report.
 * If successful a struct bl_bfmer_report object is allocated, it's address
 * is stored in bl_sta->bfm_report.
 *
 * @param[in] bl_hw   PHY Information
 * @param[in] bl_sta  Peer STA Information
 * @param[in] length    Memory size to be allocated
 *
 * @return 0 if operation is successful, else -1.
 ******************************************************************************
 */
int bl_bfmer_report_add(struct bl_hw *bl_hw, struct bl_sta *bl_sta,
                               unsigned int length);

/**
 ******************************************************************************
 * @brief Free a previously allocated memory intended to be used for
 * Beamforming Reports.
 *
 * @param[in] bl_hw   PHY Information
 * @param[in] bl_sta  Peer STA Information
 *
 ******************************************************************************
 */
void bl_bfmer_report_del(struct bl_hw *bl_hw, struct bl_sta *bl_sta);

#ifdef CONFIG_BL_FULLMAC
/**
 ******************************************************************************
 * @brief Parse a Rx VHT-MCS map in order to deduce the maximum number of
 * Spatial Streams supported by a beamformee.
 *
 * @param[in] vht_capa  Received VHT Capability field.
 *
 ******************************************************************************
 */
u8 bl_bfmer_get_rx_nss(const struct ieee80211_vht_cap *vht_capa);
#endif /* CONFIG_BL_FULLMAC */

#endif /* _BL_BFMER_H_ */
