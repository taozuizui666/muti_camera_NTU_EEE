
/**
 ******************************************************************************
 *  bl_ipc.c
 *
 *  IPC utility function definitions
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

#include "bl_defs.h"
#include "bl_ipc.h"
#include "bl_rx.h"
#include "bl_tx.h"
#include "bl_msg_rx.h"
#ifdef CONFIG_BL_DEBUGFS
#include "bl_debugfs.h"
#endif
#include "bl_ipc_host.h"

/**
 * bl_ipc_elem_var_deallocs() - Free memory allocated for a single ipc buffer
 *
 * @bl_hw: Main driver structure
 * @elem: Element to free
 */
void bl_ipc_elem_var_deallocs(struct bl_hw *bl_hw,
                                      struct bl_ipc_elem_var *elem)
{
    if (!elem->addr)
        return;

    kfree(elem->addr);
    elem->addr = NULL;
}

/**
 * bl_ipc_fw_trace_desc_get() - Return pointer to the start of trace
 * description in IPC environment
 *
 * @bl_hw: Main driver data
 */
void *bl_ipc_fw_trace_desc_get(struct bl_hw *bl_hw)
{
    return (void *)&(bl_hw->ipc_env->shared->trace_pattern);
}

/**
 * bl_ipc_sta_buffer_init - Initialize counter of bufferred data for a given sta
 *
 * @bl_hw: Main driver data
 * @sta_idx: Index of the station to initialize
 */
void bl_ipc_sta_buffer_init(struct bl_hw *bl_hw, int sta_idx)
{
    int i;
    volatile u32_l *buffered;

    if (sta_idx >= NX_REMOTE_STA_MAX)
        return;

    buffered = bl_hw->ipc_env->shared->buffered[sta_idx];

    for (i = 0; i < TID_MAX; i++) {
        *buffered++ = 0;
    }
}

/**
 * bl_ipc_sta_buffer - Update counter of bufferred data for a given sta
 *
 * @bl_hw: Main driver data
 * @sta: Managed station
 * @tid: TID on which data has been added or removed
 * @size: Size of data to add (or remove if < 0) to STA buffer.
 */
void bl_ipc_sta_buffer(struct bl_hw *bl_hw, struct bl_sta *sta, int tid, int size)
{
    u32_l *buffered;

    if (!sta)
        return;

    if ((sta->sta_idx >= NX_REMOTE_STA_MAX) || (tid >= TID_MAX))
        return;

    buffered = &bl_hw->ipc_env->shared->buffered[sta->sta_idx][tid];

    if (size < 0) {
        size = -size;
        if (*buffered < size)
            *buffered = 0;
        else
            *buffered -= size;
    } else {
        // no test on overflow
        *buffered += size;
    }
}

/**
 * bl_msgind() - IRQ handler callback for %IPC_IRQ_E2A_MSG
 *
 * @pthis: Pointer to main driver data
 * @hostid: Pointer to IPC elem from e2amsgs_pool
 */
static u8 bl_msgind(void *pthis, void *hostid)
{
    struct bl_hw *bl_hw = (struct bl_hw *)pthis;
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    struct ipc_e2a_msg *msg = (struct ipc_e2a_msg *)hostid;
#endif
    u8 ret = 0;

    BL_DBG("%s msgid=0x%x\n", __func__, msg->id);

    /* Relay further actions to the msg parser */
    bl_rx_handle_msg(bl_hw, msg);

    return ret;
}

/**
 * bl_msgackind() - IRQ handler callback for %IPC_IRQ_E2A_MSG_ACK
 *
 * @pthis: Pointer to main driver data
 * @hostid: Pointer to command acknoledged
 */
static u8 bl_msgackind(void *pthis, void *hostid)
{
    struct bl_hw *bl_hw = (struct bl_hw *)pthis;
    bl_hw->cmd_mgr.llind(&bl_hw->cmd_mgr, (struct bl_cmd *)hostid);
    return -1;
}

#ifdef CONFIG_BL_RADAR
/**
 * bl_radarind() - IRQ handler callback for %IPC_IRQ_E2A_RADAR
 *
 * @pthis: Pointer to main driver data
 * @hostid: Pointer to IPC elem from e2aradars_pool
 */
static u8 bl_radarind(void *pthis, void *hostid)
{
    struct bl_hw *bl_hw = pthis;
    struct bl_ipc_elem *elem = hostid;
    struct radar_pulse_array_desc *pulses = elem->addr;
    u8 ret = 0;
    int i;

    /* Look for pulse count meaning that this hostbuf contains RADAR pulses */
    if (pulses->cnt == 0) {
        ret = -1;
        goto radar_no_push;
    }

    if (bl_radar_detection_is_enable(&bl_hw->radar, pulses->idx)) {
        /* Save the received pulses only if radar detection is enabled */
        for (i = 0; i < pulses->cnt; i++) {
            struct bl_radar_pulses *p = &bl_hw->radar.pulses[pulses->idx];

            p->buffer[p->index] = pulses->pulse[i];
            p->index = (p->index + 1) % BL_RADAR_PULSE_MAX;
            if (p->count < BL_RADAR_PULSE_MAX)
                p->count++;
        }

        /* Defer pulse processing in separate work */
        if (! work_pending(&bl_hw->radar.detection_work))
            schedule_work(&bl_hw->radar.detection_work);
    }

    /* Reset the radar element and re-use it */
    pulses->cnt = 0;
    wmb();

    /* Push back the buffer to the LMAC */
    ipc_host_radarbuf_push(bl_hw->ipc_env, elem, (PTR2UINT)elem->dma_addr);

radar_no_push:
    return ret;
}
#endif

/**
 * bl_ipc_init() - Initialize IPC interface.
 *
 * @bl_hw: Main driver data
 * @shared_ram: Pointer to shared memory that contains IPC shared struct
 *
 * This function initializes IPC interface by registering callbacks, setting
 * shared memory area and calling IPC Init function.
 * It should be called only once during driver's lifetime.
 */
int bl_ipc_init(struct bl_hw *bl_hw, u8 *shared_ram)
{
    struct ipc_host_cb_tag cb;
    int res = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    /* initialize the API interface */
    cb.recv_data_ind   = bl_rxdataind;
    #ifdef CONFIG_BL_RADAR
    cb.recv_radar_ind  = bl_radarind;
    #endif
    cb.recv_msg_ind    = bl_msgind;
    cb.recv_msgack_ind = bl_msgackind;
    cb.send_data_cfm   = bl_txdatacfm;
    
    /* set the IPC environment */
    bl_hw->ipc_env = (struct ipc_host_env_tag *)
                       kzalloc(sizeof(struct ipc_host_env_tag), GFP_KERNEL);

    if (!bl_hw->ipc_env)
        return -ENOMEM;

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    /* call the initialization of the IPC */
    ipc_host_init(bl_hw->ipc_env, &cb,
                  (struct ipc_shared_env_tag *)shared_ram, bl_hw);
#endif

    bl_cmd_mgr_init(&bl_hw->cmd_mgr);

    return res;
}

/**
 * bl_ipc_deinit() - Release IPC interface
 *
 * @bl_hw: Main driver data
 */
void bl_ipc_deinit(struct bl_hw *bl_hw)
{
    BL_DBG(BL_FN_ENTRY_STR);

    bl_ipc_tx_drain(bl_hw);
    bl_cmd_mgr_deinit(&bl_hw->cmd_mgr);

    if (bl_hw->ipc_env) {
        kfree(bl_hw->ipc_env);
        bl_hw->ipc_env = NULL;
    }
}

/**
 * bl_ipc_tx_drain() - Flush IPC TX buffers
 *
 * @bl_hw: Main driver data
 *
 * This assumes LMAC is still (tx wise) and there's no TX race until LMAC is up
 * tx wise.
 * This also lets both IPC sides remain in sync before resetting the LMAC,
 * e.g with bl_send_reset.
 */
void bl_ipc_tx_drain(struct bl_hw *bl_hw)
{
    int i;

    BL_DBG(BL_FN_ENTRY_STR);

    if (!bl_hw->ipc_env) {
        printk(KERN_CRIT "%s: bypassing (restart must have failed)\n", __func__);
        return;
    }

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    for (i = 0; i < NX_NB_TXQ; i++) {
            struct sk_buff *skb;
            while ((skb = (struct sk_buff *)ipc_host_tx_flush(bl_hw->ipc_env, i, 0))) {
                struct bl_sw_txhdr *sw_txhdr =
                    ((struct bl_txhdr *)skb->data)->sw_hdr;

                kmem_cache_free(bl_hw->sw_txhdr_cache, sw_txhdr);
                skb_pull(skb, sw_txhdr->headroom);
                dev_kfree_skb_any(skb);
            }
    }
#endif
}

/**
 * bl_error_ind() - %DBG_ERROR_IND message callback
 *
 * @bl_hw: Main driver data
 *
 * This function triggers the UMH script call that will indicate to the user
 * space the error that occurred and stored the debug dump. Once the UMH script
 * is executed, the bl_umh_done() function has to be called.
 */
void bl_error_ind(struct bl_hw *bl_hw)
{
#ifdef CONFIG_BL_DEBUGFS
    bl_hw->debugfs.trace_prst = true;
    bl_trigger_um_helper(&bl_hw->debugfs);
#endif
}

/**
 * bl_umh_done() - Indicate User Mode helper finished
 *
 * @bl_hw: Main driver data
 *
 */
void bl_umh_done(struct bl_hw *bl_hw)
{
    if (!test_bit(BL_DEV_STARTED, &bl_hw->flags))
        return;

    /* this assumes error_ind won't trigger before ipc_host_dbginfobuf_push
       is called and so does not irq protect (TODO) against error_ind */
#ifdef CONFIG_BL_DEBUGFS
    bl_hw->debugfs.trace_prst = false;
#endif
}


