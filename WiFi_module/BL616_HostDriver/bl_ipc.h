/**
 ******************************************************************************
 *  bl_ipc.h
 *
 *  IPC utility function declarations
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

#ifndef _BL_IPC_H_
#define _BL_IPC_H_

#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/skbuff.h>

enum bl_dev_flag {
    BL_DEV_RESTARTING,
    BL_DEV_STACK_RESTARTING,
    BL_DEV_STARTED,
    BL_DEV_ADDING_STA,
};

struct bl_hw;
struct bl_sta;

#ifdef CONFIG_BL_RADAR
/**
 * struct bl_ipc_elem - Generic IPC buffer of fixed size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 */
struct bl_ipc_elem {
    void *addr;
    dma_addr_t dma_addr;
};
#endif

/**
 * struct bl_ipc_elem - Generic IPC buffer of variable size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 * @size: Size, in bytes, of the buffer
 */
struct bl_ipc_elem_var {
    void *addr;
    size_t size;
};

/**
 * struct bl_ipc_dbgdump_elem - IPC buffer for debug dump
 *
 * @mutex: Mutex to protect access to debug dump
 * @buf: IPC buffer
 */
struct bl_ipc_dbgdump_elem {
    struct mutex mutex;
    struct bl_ipc_elem_var buf;
};

#ifdef CONFIG_BL_FULLMAC

/* Maximum number of rx buffer the fw may use at the same time */
#define BL_RXBUFF_MAX (64 * NX_REMOTE_STA_MAX)

#endif /* CONFIG_BL_FULLMAC */

void *bl_ipc_fw_trace_desc_get(struct bl_hw *bl_hw);
int bl_ipc_init(struct bl_hw *bl_hw, u8 *shared_ram);
void bl_ipc_deinit(struct bl_hw *bl_hw);
void bl_ipc_tx_drain(struct bl_hw *bl_hw);

struct ipc_host_env_tag;
void bl_ipc_elem_var_deallocs(struct bl_hw *bl_hw,
                                      struct bl_ipc_elem_var *elem);

void bl_error_ind(struct bl_hw *bl_hw);
void bl_umh_done(struct bl_hw *bl_hw);

void bl_ipc_sta_buffer_init(struct bl_hw *bl_hw, int sta_idx);
void bl_ipc_sta_buffer(struct bl_hw *bl_hw, struct bl_sta *sta, 
                           int tid, int size);

#endif /* _BL_IPC_UTILS_H_ */



