/**
 ******************************************************************************
 *
 *  @file ipc_host.c
 *
 *  @brief IPC module.
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


/*
 * INCLUDE FILES
 ******************************************************************************
 */
#include <linux/spinlock.h>
#include "bl_defs.h"
#include "bl_ipc_host.h"

/*
 * TYPES DEFINITION
 ******************************************************************************
 */

const int nx_txdesc_cnt[] =
{
    NX_TXDESC_CNT0,
    NX_TXDESC_CNT1,
    NX_TXDESC_CNT2,
    NX_TXDESC_CNT3,
    #if NX_TXQ_CNT == 5
    NX_TXDESC_CNT4,
    #endif
};

const int nx_txdesc_cnt_msk[] =
{
    NX_TXDESC_CNT0 - 1,
    NX_TXDESC_CNT1 - 1,
    NX_TXDESC_CNT2 - 1,
    NX_TXDESC_CNT3 - 1,
    #if NX_TXQ_CNT == 5
    NX_TXDESC_CNT4 - 1,
    #endif
};

const int nx_txuser_cnt[] =
{
    CONFIG_USER_MAX,
    CONFIG_USER_MAX,
    CONFIG_USER_MAX,
    CONFIG_USER_MAX,
    #if NX_TXQ_CNT == 5
    1,
    #endif
};


/*
 * FUNCTIONS DEFINITIONS
 ******************************************************************************
 */
 
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
void ipc_host_tx_cfm_handler(struct ipc_host_env_tag *env, 
                         const int queue_idx, const int user_pos, 
                         struct bl_hw_txhdr *hw_hdr, struct bl_txq **txq_saved)
{
    u8_l staid = 0, tid = 0, vif = 0, txq_num = 0;
    struct sk_buff *skb = NULL;
    struct bl_hw * bl_hw = NULL;

    if((bl_hw = (struct bl_hw *)env->pthis) == NULL)
        return;

    staid = hw_hdr->cfm.staid;
    tid = hw_hdr->cfm.tid;

    /* when staid = 10/11/12/13, it is None-STA interface, we use TXQ[90] for TX*/
    if(staid >= NX_REMOTE_STA_MAX)
        tid = 0;

    staid = MIN(staid, NX_REMOTE_STA_MAX);
    tid = MIN(tid, NX_NB_TXQ_PER_STA - 1);
    vif = MIN(vif, NX_VIRT_DEV_MAX - 1);

    txq_num = staid * NX_NB_TXQ_PER_STA + tid + vif;

    skb = skb_dequeue(&bl_hw->transmitted_list[txq_num]);
    if(!skb){
        BL_DBG("%s: host id is null qlen=%u\n", __func__,
               skb_queue_len(&bl_hw->transmitted_list[txq_num]));
        return;
    }

    if (env->cb.send_data_cfm(env->pthis, skb, hw_hdr, (void **)txq_saved) != 0) {
        BL_DBG("send_data_cfm!=0, so break, skb_wait_len=%d\n", 
               skb_queue_len(&bl_hw->transmitted_list[txq_num]));
    }
}
#endif

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
void *ipc_host_tx_flush(struct ipc_host_env_tag *env, const int queue_idx, 
                             const int user_pos)
{
    struct bl_hw * bl_hw = NULL;

    if((bl_hw = (struct bl_hw *)env->pthis) == NULL)
        return NULL;

    return (void *)skb_dequeue(&bl_hw->transmitted_list[queue_idx]);
}
#endif

/**
 ******************************************************************************
 */
void ipc_host_init(struct ipc_host_env_tag *env, struct ipc_host_cb_tag *cb,
                      struct ipc_shared_env_tag *shared_env_ptr, void *pthis)
{
    unsigned int i;
    unsigned int size;
    //unsigned int * dst;
    uint8_t *dst = NULL;

    // Reset the environments
    // Reset the IPC Shared memory
#if 0
    /* check potential platform bug on multiple stores */
    memset(shared_env_ptr, 0, sizeof(struct ipc_shared_env_tag));
#else
    dst = (uint8_t *)shared_env_ptr;
    size = (unsigned int)sizeof(struct ipc_shared_env_tag);
    for (i=0; i < size; i++)
    {
        *dst++ = 0;
    }
#endif
    // Reset the IPC Host environment
    memset(env, 0, sizeof(struct ipc_host_env_tag));

    // Initialize the shared environment pointer
    env->shared = shared_env_ptr;

    // Save the callbacks in our own environment
    env->cb = *cb;

    // Save the pointer to the register base
    env->pthis = pthis;
}

#ifdef CONFIG_BL_RADAR
/**
 ******************************************************************************
 */
int ipc_host_radarbuf_push(struct ipc_host_env_tag *env, void *hostid,
                                   uint32_t hostbuf)
{
    struct ipc_shared_env_tag *shared_env_ptr = env->shared;

    // Save the hostid and the hostbuf in global array
    env->ipc_host_radarbuf_array[env->ipc_host_radarbuf_idx].hostid = hostid;
    env->ipc_host_radarbuf_array[env->ipc_host_radarbuf_idx].dma_addr = hostbuf;

    // Copy the hostbuf (DMA address) in the ipc shared memory
    shared_env_ptr->radarbuf_hostbuf[env->ipc_host_radarbuf_idx] = hostbuf;

    // Increment the array index
    env->ipc_host_radarbuf_idx = (env->ipc_host_radarbuf_idx +1)%IPC_RADARBUF_CNT;

    return (0);
}
#endif

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
int ipc_host_txdesc_push(struct ipc_host_env_tag *env, const int queue_idx,
                                 const int user_pos, void *host_id)
{
    u8_l staid = 0, tid = 0, vif = 0, txq_num = 0;
    struct sk_buff *skb = host_id;
    struct bl_hw * bl_hw = (struct bl_hw *)env->pthis;
    struct bl_txhdr * txhdr = (struct bl_txhdr *)skb->data;

    if(!skb)
        return -1;

    staid = txhdr->sw_hdr->desc.host.staid;
    tid = txhdr->sw_hdr->desc.host.tid;
    /* current TXCFM not include vif, use 0 for all vif */
    vif = 0;

    /* when staid = 10/11/12/13, it is None-STA interface, we use TXQ[90] for TX*/
    if(staid >= NX_REMOTE_STA_MAX)
        tid = 0;

    staid = MIN(staid, NX_REMOTE_STA_MAX);
    tid = MIN(tid, NX_NB_TXQ_PER_STA - 1);
    vif = MIN(vif, NX_VIRT_DEV_MAX - 1);

    txq_num = staid * NX_NB_TXQ_PER_STA + tid + vif;

    skb_queue_tail(&bl_hw->transmitted_list[txq_num], skb);

    return txq_num;
}
#endif



