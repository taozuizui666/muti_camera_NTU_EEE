/**
 ******************************************************************************
 *
 *  @file bl_bfmer.c
 *
 *  @brief VHT Beamformer function definitions
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
 ******************************************************************************
 */

#include <linux/slab.h>
#include "bl_bfmer.h"

/**
 * FUNCTION DEFINITIONS
 ******************************************************************************
 */

int bl_bfmer_report_add(struct bl_hw *bl_hw, struct bl_sta *bl_sta,
                          unsigned int length)
{
    gfp_t flags;
    struct bl_bfmer_report *bfm_report ;

    if (in_softirq())
        flags = GFP_ATOMIC;
    else
        flags = GFP_KERNEL;

    /* Allocate a structure that will contain the beamforming report */
    bfm_report = kmalloc(sizeof(*bfm_report) + length, flags);


    /* Check report allocation */
    if (!bfm_report) {
        /* Do not use beamforming */
        return -1;
    }

    /* Store report length */
    bfm_report->length = length;

    /*
     * Need to provide a Virtual Address to the MAC so that it can
     * upload the received Beamforming Report in driver memory
     */
    bfm_report->dma_addr = dma_map_single(bl_hw->dev, &bfm_report->report[0],
                                          length, DMA_FROM_DEVICE);

    /* Check DMA mapping result */
    if (dma_mapping_error(bl_hw->dev, bfm_report->dma_addr)) {
        /* Free allocated report */
        kfree(bfm_report);
        /* And leave */
        return -1;
    }

    /* Store report structure */
    bl_sta->bfm_report = bfm_report;

    return 0;
}

void bl_bfmer_report_del(struct bl_hw *bl_hw, struct bl_sta *bl_sta)
{
    /* Verify if a report has been allocated */
    if (bl_sta->bfm_report) {
        struct bl_bfmer_report *bfm_report = bl_sta->bfm_report;

        /* Unmap DMA region */
        dma_unmap_single(bl_hw->dev, bfm_report->dma_addr,
                         bfm_report->length, DMA_BIDIRECTIONAL);

        /* Free allocated report structure and clean the pointer */
        kfree(bfm_report);
        bl_sta->bfm_report = NULL;
    }
}

#ifdef CONFIG_BL_FULLMAC
u8 bl_bfmer_get_rx_nss(const struct ieee80211_vht_cap *vht_capa)
{
    int i;
    u8 rx_nss = 0;
    u16 rx_mcs_map = le16_to_cpu(vht_capa->supp_mcs.rx_mcs_map);

    for (i = 7; i >= 0; i--) {
        u8 mcs = (rx_mcs_map >> (2 * i)) & 3;

        if (mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
            rx_nss = i + 1;
            break;
        }
    }

    return rx_nss;
}
#endif /* CONFIG_BL_FULLMAC */
