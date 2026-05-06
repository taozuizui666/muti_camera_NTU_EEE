/**
 ******************************************************************************
 *
 *  @file bl_platform.c
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


#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/delay.h>

#include "bl_platform.h"
#include "bl_hal_desc.h"
#include "bl_main.h"
#if defined CONFIG_BL_SDIO
#include "bl_sdio.h"
#include "bl_ipc_shared.h"
#elif defined CONFIG_BL_USB
#include "bl_usb.h"
#endif
#include "bl_ipc_host.h"

#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
struct bl_hw * bl_hws[BL_HWS_MAX_NUM];
uint32_t bl_hws_num = 0;
extern int bl_close(struct net_device *dev);
extern int bl_send_sm_disconnect_req(struct bl_hw *bl_hw, 
                                            struct bl_vif *bl_vif, u16 reason);
#endif

#ifdef CONFIG_BL_BTUSB
extern void bl_btusb_register_drv(void);
extern void bl_btusb_unregister_drv(void);
#endif

/**
 * bl_platform_init() - Initialize the platform
 *
 * @bl_plat: platform data (already updated by platform driver)
 * @platform_data: Pointer to store the main driver data pointer (aka bl_hw)
 *                That will be set as driver data for the platform driver
 * Return: 0 on success, < 0 otherwise
 *
 * Called by the platform driver after it has been probed
 */
int bl_platform_init(struct bl_plat *bl_plat, void **platform_data)
{
    int ret = 0;
#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    int i = 0;
#endif

    BL_DBG(BL_FN_ENTRY_STR);

    bl_plat->enabled = false;

    ret = bl_cfg80211_init(bl_plat, platform_data);

#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    if (ret)
        return ret;
    
    for (; i< BL_HWS_MAX_NUM; i++) {
        if (bl_hws[i] == NULL) {
            bl_hws[i] = (struct bl_hw *)(*platform_data);
            bl_hws_num++;
            break;
        }
    }

    if (i >= BL_HWS_MAX_NUM)
        BL_DBG("%s, no place for bl_hw in bl_hws\n", __func__);
#endif

    return ret;
}

/**
 * bl_platform_deinit() - Deinitialize the platform
 *
 * @bl_hw: main driver data
 *
 * Called by the platform driver after it is removed
 */
void bl_platform_deinit(struct bl_hw *bl_hw)
{
#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    int i = 0;
#endif

    BL_DBG(BL_FN_ENTRY_STR);

#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    for (; i< BL_HWS_MAX_NUM; i++) {
        if (bl_hws[i] && bl_hws[i] == bl_hw) {
            BL_DBG("%s, remove bl_hw:%d\n", __func__, i);
            
            bl_hw->wiphy->reg_notifier = NULL;
            bl_hws[i] = NULL;
            bl_hws_num--;
            
            break;
        }
    }
    if (i >= BL_HWS_MAX_NUM) {
        BL_DBG("%s, not find bl_hw in bl_hws\n", __func__);
        
        return;
    }
#endif

#if defined CONFIG_BL_FULLMAC
    bl_cfg80211_deinit(bl_hw);
#endif

#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    //This works well for a complete and easy rmmod
    //BL_DBG("%s, call usb_reset_deivce\n", __func__);
    //usb_reset_device(((struct bl_usb_device *)bl_hw->plat->priv)->udev);
    //BL_DBG("%s, done usb_reset_deivce\n", __func__);
#endif
}

/**
 * bl_platform_register_drv() - Register all possible platform drivers
 */
int bl_platform_register_drv(void)
{
#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    int i = 0;
    for (; i< BL_HWS_MAX_NUM; i++) {
        bl_hws[i] = NULL;
    }
    bl_hws_num = 0;
#endif

    BL_DBG(BL_FN_ENTRY_STR);

#if defined CONFIG_BL_PCIE
    return bl_pci_register_drv();
#elif defined CONFIG_BL_SDIO
    return bl_sdio_register_drv();
#elif defined CONFIG_BL_USB
#ifdef CONFIG_BL_BTUSB
    bl_btusb_register_drv();
#endif    
    
    return bl_usb_register_drv();
#endif
    
}


/**
 * bl_platform_unregister_drv() - Unegister all platform drivers
 */
void bl_platform_unregister_drv(void)
{
#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    int i = 0;
    struct bl_vif *vif = NULL;
    struct net_device *ndev;
#endif

    BL_DBG(BL_FN_ENTRY_STR);

#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    for (; i< BL_HWS_MAX_NUM; i++) {
        struct bl_hw *bl_hw = bl_hws[i];

        if (!bl_hw)
            continue;

        list_for_each_entry(vif, &bl_hw->vifs, list) {
            if (!vif || !vif->up) {
                BL_DBG("%s, up is false for this vif\n", __func__);
                continue;
            }
            
            ndev = vif->ndev;

            if (BL_VIF_TYPE(vif) == NL80211_IFTYPE_STATION ||
                BL_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT) 
            {
                if (vif->sta.ap && vif->sta.ap->valid) {
                    BL_DBG("%s call sm_disconn\n", __func__);
                    
                    bl_send_sm_disconnect_req(bl_hw, vif, 1);
                }
            }
            
            BL_DBG("%s, closing vif:%d, up:%d\n", __func__, vif->vif_index, vif->up);
            
            bl_close(ndev);
        }
    }

    bl_hws_num = 0;
#endif

#if defined CONFIG_BL_SDIO
    return bl_sdio_unregister_drv();
#elif defined CONFIG_BL_USB
#ifdef CONFIG_BL_BTUSB
    bl_btusb_unregister_drv();
#endif

    return bl_usb_unregister_drv();
#endif
}

#ifndef CONFIG_BL_SDM
MODULE_FIRMWARE(BL_AGC_FW_NAME);
MODULE_FIRMWARE(BL_FCU_FW_NAME);
MODULE_FIRMWARE(BL_LDPC_RAM_NAME);
#endif
MODULE_FIRMWARE(BL_MAC_FW_NAME);
#ifndef CONFIG_BL_TL4
MODULE_FIRMWARE(BL_MAC_FW_NAME2);
#endif


