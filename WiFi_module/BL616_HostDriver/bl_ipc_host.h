/**
 ******************************************************************************
 *
 *  @file ipc_host.h
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

#ifndef _IPC_HOST_H_
#define _IPC_HOST_H_

/*
 * INCLUDE FILES
 ******************************************************************************
 */
#include "bl_ipc_shared.h"
#ifndef __KERNEL__
#include "arch.h"
#else
#include "bl_ipc_compat.h"
#endif

/*
 * ENUMERATION
 ******************************************************************************
 */


/**
 ******************************************************************************
 * @brief This structure is used to initialize the MAC SW
 *
 * The WLAN device driver provides functions call-back with this structure
 ******************************************************************************
 */
struct ipc_host_cb_tag
{
#if defined CONFIG_BL_SDIO
    int (*send_data_cfm)(void *pthis, void *host_id, void *data1, void **data2);
#elif defined CONFIG_BL_USB
    //TODO:
    int (*send_data_cfm)(void *pthis, void *host_id, void *data1, void **data2);
#endif

    /// WLAN driver call-back function: recv_data_ind
    uint8_t (*recv_data_ind)(void *pthis, void *host_id);

    #ifdef CONFIG_BL_RADAR
    /// WLAN driver call-back function: recv_radar_ind
    uint8_t (*recv_radar_ind)(void *pthis, void *host_id);
    #endif
    
    /// WLAN driver call-back function: recv_msg_ind
    uint8_t (*recv_msg_ind)(void *pthis, void *host_id);

    /// WLAN driver call-back function: recv_msgack_ind
    uint8_t (*recv_msgack_ind)(void *pthis, void *host_id);
};

#ifdef CONFIG_BL_RADAR
/*
 * Struct used to store information about host buffers (DMA Address and local pointer)
 */
struct ipc_hostbuf
{
    void    *hostid;     ///< ptr to hostbuf client (ipc_host client) structure
    uint32_t dma_addr;   ///< ptr to real hostbuf dma address
};
#endif

/// Definition of the IPC Host environment structure.
struct ipc_host_env_tag
{
    /// Structure containing the callback pointers
    struct ipc_host_cb_tag cb;

    /// Pointer to the shared environment
    struct ipc_shared_env_tag *shared;

    #ifdef CONFIG_BL_RADAR
    /// Fields for Radar events handling
    // Global array used to store the hostid and hostbuf addresses
    struct ipc_hostbuf ipc_host_radarbuf_array[IPC_RADARBUF_CNT];
    // Index used for ipc_host_rxbuf_array to point to current buffer
    uint8_t ipc_host_radarbuf_idx;
    #endif

    /// E2A ACKs of A2E MSGs
    uint8_t msga2e_cnt;
    void *msga2e_hostid;

    /// Pointer to the attached object (used in callbacks and register accesses)
    void *pthis;
};

extern const int nx_txdesc_cnt[];
extern const int nx_txuser_cnt[];

/**
 ******************************************************************************
 * @brief Initialize the IPC running on the Application CPU.
 *
 * This function:
 *   - initializes the IPC software environments
 *   - enables the interrupts in the IPC block
 *
 * @param[in]   env   Pointer to the IPC host environment
 *
 * @warning Since this function resets the IPC Shared memory, it must be called
 * before the LMAC FW is launched because LMAC sets some init values in IPC
 * Shared memory at boot.
 *
 ******************************************************************************
 */
void ipc_host_init(struct ipc_host_env_tag *env, struct ipc_host_cb_tag *cb,
                      struct ipc_shared_env_tag *shared_env_ptr, void *pthis);

/**
 ******************************************************************************
 * @brief Retrieve a new free Tx descriptor (host side).
 *
 * This function returns a pointer to the next Tx descriptor available from the
 * queue queue_idx to the host driver. The driver will have to fill it with the
 * appropriate endianness and to send it to the
 * emb side with ipc_host_txdesc_push().
 *
 * This function should only be called once until ipc_host_txdesc_push() is called.
 *
 * This function will return NULL if the queue is full.
 *
 * @param[in]   env   Pointer to the IPC host environment
 * @param[in]   queue_idx   Queue index. The index can be inferred from the
 *                          user priority of the incoming packet.
 * @param[in]   user_pos    User position. If MU-MIMO is not used, this value
 *                          shall be 0.
 * @return                  Pointer to the next Tx descriptor free. This can
 *                          point to the host memory or to shared memory,
 *                          depending on IPC implementation.
 *
 ******************************************************************************
 */
volatile struct txdesc_host *ipc_host_txdesc_get(struct ipc_host_env_tag *env,
                                                        const int queue_idx, 
                                                        const int user_pos);

#if defined CONFIG_BL_SDIO  || defined CONFIG_BL_USB
int ipc_host_txdesc_push(struct ipc_host_env_tag *env, const int queue_idx,
                                 const int user_pos, void *host_id);
#endif

/**
 ******************************************************************************
 * @brief Get and flush a packet from the IPC queue passed as parameter.
 *
 * @param[in]   env        Pointer to the IPC host environment
 * @param[in]   queue_idx  Index of the queue to flush
 * @param[in]   user_pos   User position to flush
 *
 * @return The flushed hostid if there is one, 0 otherwise.
 *
 ******************************************************************************
 */
void *ipc_host_tx_flush(struct ipc_host_env_tag *env, const int queue_idx,
                             const int user_pos);

#ifdef CONFIG_BL_RADAR
/**
 ******************************************************************************
 * @brief Push a pre-allocated radar event buffer descriptor
 *
 * This function is called at Init time to initialize all radar event buffers.
 * Then each time embedded send a radar event, this function is used to push
 * back the same buffer once it has been handled.
 *
 * @param[in]   env         Pointer to the IPC host environment
 * @param[in]   hostid      Address of packet for host
 * @param[in]   hostbuf     Pointer to the start of the buffer payload in the
 *                          host memory. The length of this buffer should be
 *                          predefined between host and emb statically.
 *
 ******************************************************************************
 */
int ipc_host_radarbuf_push(struct ipc_host_env_tag *env, void *hostid,
                                   uint32_t hostbuf);
#endif

#if defined CONFIG_BL_USB || defined CONFIG_BL_SDIO
int ipc_host_txdesc_push(struct ipc_host_env_tag *env, const int queue_idx,
                                 const int user_pos, void *host_id);
#endif

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
void ipc_host_tx_cfm_handler(struct ipc_host_env_tag *env, 
                                     const int queue_idx, const int user_pos,
                                     struct bl_hw_txhdr *hw_hdr, 
                                     struct bl_txq **txq_saved);
#endif

#endif // _IPC_HOST_H_
