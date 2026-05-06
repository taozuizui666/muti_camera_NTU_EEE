/**
 ******************************************************************************
 *
 *  bl_cmds.c
 *
 *  Handles queueing (push to IPC, ack/cfm from IPC) of commands issued to
 *  LMAC FW
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
#include <linux/list.h>

#include "bl_cmds.h"
#include "bl_defs.h"
#include "bl_strs.h"
#include "bl_ipc_host.h"
#include "bl_irqs.h"
#ifdef CONFIG_BL_USB
#include "bl_usb.h"
#else
#include "bl_sdio.h"
#endif
#include "bl_nl_events.h"
#include "bl_msg_rx.h"

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 17)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

static void dump_info(struct bl_hw *bl_hw)
{
#ifdef CONFIG_BL_SDIO
    struct sdio_mmc_card *card = (struct sdio_mmc_card *)(bl_hw->plat)->priv;
    uint8_t trapDbgData;
    uint8_t trapDataNum = 8, i=0;

    for(i=0; i< trapDataNum; i++){
        bl_read_reg(bl_hw, 0x60 + i, &trapDbgData);
        printk("cmd timeout %d epc/tval 0x%2x \n", i, trapDbgData);
    }
    bl_read_data_sync(bl_hw, card->mp_regs, 64, REG_PORT);
    printk("SDIO reg hostintstatus val:%x \n", card->mp_regs[3]);            
    printk("SDIO reg rdbitmap val:%x \n", card->mp_regs[4]|(card->mp_regs[5]<<8));            
    printk("SDIO reg wrbitmap val:%x \n", card->mp_regs[6]|(card->mp_regs[7]<<8));            
    printk("SDIO reg CardIntStatus val:%x \n", card->mp_regs[0x38]);
    bl_sdio_read_reg_func0(card, 0x04, &trapDbgData);
    printk("sdio fun0 reg0x4 %x\n", trapDbgData);
    bl_sdio_read_reg_func0(card, 0x05, &trapDbgData);
    printk("sdio fun0 reg0x5 %x\n", trapDbgData);
#endif
}

void cmd_dump(const struct bl_cmd *cmd)
{
    printk("tkn[%d]  flags:0x%04x  result:%3d  cmd:0x%x-%-24s - reqcfm(0x%x-%-s)\n",
           cmd->tkn, cmd->flags, cmd->result, cmd->id, BL_ID2STR(cmd->id),
           cmd->reqid, 
           cmd->reqid != (lmac_msg_id_t)-1 ? BL_ID2STR(cmd->reqid) : "none");
}

#if defined(CONFIG_BL_SDIO)
#define DUMP_TO_FILE
void dump_fw_criticals(struct bl_hw *bl_hw) {
    int i = 0;
    uint8_t ind_byte = 3;
    uint32_t c_inv_addr = 0xffffffff;
    uint32_t c_inv_value = 0xffffffff;
    uint32_t wr_mask = 0x80;
    uint32_t rd_mask = 0x7f;
    uint32_t scratch_reg = 0x60;
    const int dump_cnt = 8;
    uint8_t dump_data[8];
    uint8_t dump_buf[50];
    uint32_t value = 0;
    uint32_t addr = 0;
    uint32_t read_cnt_max = 100; //ms
    unsigned long jiffies_end = jiffies;
    uint32_t cnt_print = 0;

    struct file *p_dump_file = NULL;
    unsigned char file_name[128];
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
    mm_segment_t fs;
#endif
    uint32_t len = 0;
    static uint8_t dump_fw_started = 0;

    //multiple cmd timeout, may trigger multiple dump
    if (dump_fw_started) {
        printk(KERN_CRIT"dump fw already started\n");
        return;
    }
    
    dump_fw_started = 1;

    printk(KERN_CRIT"dump fw memory\n");

    msleep_interruptible(500);

    for (i=0; i<dump_cnt; i++) {
        bl_write_reg(bl_hw, scratch_reg+i, 0x00);
    }

    i = 0;
    while (i++ < read_cnt_max) {
        int j = 0;
        
        msleep_interruptible(1);
        
        for (j=0; j<dump_cnt; j++) {
            bl_read_reg(bl_hw, scratch_reg+j, dump_data+j);
        }
        
        BL_DBG("wait starter, i=%d, max:%d, dump data:0x%x, 0x%x\r\n", 
               i, read_cnt_max, *(uint32_t *)(dump_data), 
               *(uint32_t *)(dump_data + 4));
               
        if (*(uint32_t *)dump_data == c_inv_addr &&
            *(uint32_t *)(dump_data + 4) == c_inv_value)
        {
            printk(KERN_CRIT"dump fw start\n");
            
            dump_data[ind_byte] = (dump_data[ind_byte]&rd_mask);
            *(uint32_t *)(dump_data + 4) =  *(uint32_t *)(dump_data);
             
            for (j=0; j<dump_cnt; j++) {
                bl_write_reg(bl_hw, scratch_reg+j, dump_data[j]);
            }
            
            break;
        }
    }
    
    printk("wait starter, i=%d, max:%d end\r\n", i, read_cnt_max);
    
    if (i >= read_cnt_max) {
        dump_fw_started = 0;
        printk(KERN_CRIT"dump fw, wait starter timeout.\n");
        return;
    }

    do {
        if (bl_hw->mod_params->dump_dir != NULL) {
            memset(file_name, 0, sizeof(file_name));
            
            sprintf(file_name, "%s/%s_%ld.txt", bl_hw->mod_params->dump_dir, 
                    "bl_fw_dump", jiffies_end);
            p_dump_file = filp_open(file_name, O_CREAT | O_RDWR, 0644);
            
            if (IS_ERR(p_dump_file)) {
                printk(KERN_CRIT"dump fw, create file %s error, try other folder", file_name);
            } else {
                printk(KERN_CRIT"dump fw data in %s\n", file_name);
                break;
            }
        }

        #ifdef DUMP_TO_FILE
        memset(file_name, 0, sizeof(file_name));
        sprintf(file_name, "%s/%s_%ld.txt", "/data", "bl_fw_dump", jiffies_end);
        p_dump_file = filp_open(file_name, O_CREAT | O_RDWR, 0644);
        
        if (IS_ERR(p_dump_file)) {
            printk(KERN_CRIT"dump fw, create file %s error, next try to create /data/bl_fw_dump.txt", file_name);
        } else {
            printk(KERN_CRIT"dump fw data in %s\n", file_name);
            break;
        }
        
        memset(file_name, 0, sizeof(file_name));
        sprintf(file_name, "%s/%s_%ld.txt", "/var", "bl_fw_dump", jiffies_end);
        p_dump_file = filp_open(file_name, O_CREAT | O_RDWR, 0644);
        
        if (IS_ERR(p_dump_file)) {
            printk(KERN_CRIT"dump fw, create file %s failed, no file to dump, only printk to dmesg\n", file_name);
            
            p_dump_file = NULL;
        } else {
            printk(KERN_CRIT"dump fw data in %s\n", file_name);
            break;
        }
        #endif
    } while(0);

    if (p_dump_file != NULL) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
        fs = get_fs();
        set_fs(KERNEL_DS);
#endif
    }

    memset(dump_data, 0, dump_cnt);
    jiffies_end = jiffies + HZ*read_cnt_max/1000;
    while ((*(uint32_t *)dump_data) != c_inv_addr) {
        //Avoid fw/board is gone, while becomes dead loop
        if (time_after(jiffies, jiffies_end)) {
            printk(KERN_CRIT"dump fw, timeout when dumping.\n");
            break;
        }

        for (i=0; i<dump_cnt; i++) {
            bl_read_reg(bl_hw, scratch_reg+i, dump_data+i);
        }

        //Receive indication flag that fw is done to write.
        if ((dump_data[ind_byte]&wr_mask) > 0) {
            for (i=0; i<dump_cnt; i++) {
                bl_read_reg(bl_hw, scratch_reg+i, dump_data+i);
            }
            
            //Send indiation to fw that it is OK to read
            bl_write_reg(bl_hw, scratch_reg+ind_byte, (dump_data[ind_byte]&rd_mask));
            
            addr = ((*(uint32_t *)dump_data) & 0x7fffffff);
            value = *(uint32_t *)(dump_data + 4);

            if ((cnt_print++ % 1000) == 0)
                printk(KERN_CRIT"dump addr:0x%08x=0x%08x:value\n", addr, value);

            if (p_dump_file != NULL) {
                len = sprintf(dump_buf, "dump addr:0x%08x=0x%08x:value\n", addr, value);
                #if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
                vfs_write(p_dump_file, dump_buf, len, &p_dump_file->f_pos);
                #else
                kernel_write(p_dump_file, dump_buf, len, &p_dump_file->f_pos);
                #endif
            }

            jiffies_end = jiffies + HZ*read_cnt_max/1000;
        }
    }

    if (p_dump_file != NULL) {
        printk(KERN_CRIT"dump fw successfully to file %s\n", file_name);
        
        filp_close(p_dump_file, NULL);
        #if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
        set_fs(fs);
        #endif
    }

    dump_fw_started = 0;
    
    printk(KERN_CRIT"dump fw end\n");

    return;
}
#endif

static void cmd_complete(struct bl_cmd_mgr *cmd_mgr, struct bl_cmd *cmd)
{
    lockdep_assert_held(&cmd_mgr->lock);

    list_del(&cmd->list);
    cmd_mgr->queue_sz--;

    BL_DBG_MSG("%s, cmd id:0x%x, flag:0x%x\n", __func__, cmd->id, cmd->flags);

    cmd->flags |= BL_CMD_FLAG_DONE;
    if (cmd->flags & BL_CMD_FLAG_NONBLOCK) {
        kfree(cmd);
    } else {
        if (BL_CMD_WAIT_COMPLETE(cmd->flags)) {
            BL_DBG("%s cmdid=%d\n", __func__, cmd->id);
            
            cmd->result = 0;
            complete(&cmd->complete);
        }
    }
}

#ifdef CONFIG_MERGE_TRAFFIC_IND_REQ
//make sure "cmd_mgr->lock" is hold when call this function.
//If merge any traffic_ind in queue successfully, then return true, or false
bool cmd_mgr_merge_traffic_ind(struct bl_cmd_mgr *cmd_mgr, struct bl_cmd *cmd)
{
    struct bl_cmd *cur, *tmp;
    u8 skip_cnt = 1;
    int cnt = 0;

    struct me_traffic_ind_req *cmd_req = 
       (struct me_traffic_ind_req *)(((struct lmac_msg *)cmd->a2e_msg)->param);
    struct me_traffic_ind_req *cur_req = NULL;

    list_for_each_entry_safe(cur, tmp, &cmd_mgr->cmds, list) {
        BL_DBG("%s, cur:0x%p, cur->id:%d, tkn:%d, flags:0x%x\n", 
               __func__, cur, cur->id, cur->tkn, cur->flags);

        cnt++;
        
        if (cur->id!=ME_TRAFFIC_IND_REQ || 
            (cur->flags&BL_CMD_FLAG_WAIT_PUSH) == 0 || 
            (cur->flags&BL_CMD_FLAG_DNLD_COMPLETE) ||
            (cur->flags&BL_CMD_FLAG_DNLDING)) 
        {
            continue;
        }

        if (skip_cnt) {
            skip_cnt--;
            continue;
        }

        cur_req = (struct me_traffic_ind_req *)(((struct lmac_msg *)cur->a2e_msg)->param);
        
        BL_DBG(KERN_CRIT"cur_req:0x%p flags:0x%x tkn:%u. cur:%d, %d, %d. cmd_req:%d, %d, %d\r\n", 
               cur, cur->flags, cur->tkn, 
               cur_req->sta_idx, cur_req->tx_avail, cur_req->uapsd, 
               cmd_req->sta_idx, cmd_req->tx_avail, cmd_req->uapsd);

        if (cur->id == cmd->id && cur_req->sta_idx == cmd_req->sta_idx && 
            cur_req->uapsd == cmd_req->uapsd)
        {
            //overwrite queued cmd with incoming new cmd value.
            cur_req->tx_avail = cmd_req->tx_avail;

            BL_DBG("Free one duplicate traffic_ind, cnt:%d\n", cnt);
            
            return true;
        }
    }

    return false;
}
#endif

static int cmd_mgr_queue(struct bl_cmd_mgr *cmd_mgr, struct bl_cmd *cmd)
{
    struct bl_hw *bl_hw = container_of(cmd_mgr, struct bl_hw, cmd_mgr);
    bool defer_push = false;
    u16 flags;
    struct bl_cmd *cur = NULL;

    BL_DBG(BL_FN_ENTRY_STR);
    
    spin_lock_bh(&cmd_mgr->lock);

    if (cmd_mgr->state == BL_CMD_MGR_STATE_CRASHED) {
        printk(KERN_CRIT"cmd queue crashed\n");
        
        cmd->result = -EPIPE;
        kfree(cmd->a2e_msg);
        
        if(cmd->flags & BL_CMD_FLAG_NONBLOCK)
            kfree(cmd);
            
        spin_unlock_bh(&cmd_mgr->lock);
        
        return -EPIPE;
    }

    if (!list_empty(&cmd_mgr->cmds)) {
        struct bl_cmd *last;

        #ifdef CONFIG_MERGE_TRAFFIC_IND_REQ
        if (cmd->id == ME_TRAFFIC_IND_REQ) {
            if (cmd_mgr_merge_traffic_ind(cmd_mgr, cmd)) {
                kfree(cmd->a2e_msg);
                
                if(cmd->flags & BL_CMD_FLAG_NONBLOCK)
                    kfree(cmd);
                    
                spin_unlock_bh(&cmd_mgr->lock);
                
                return 0;
            }
        }
        #endif

        if (cmd_mgr->queue_sz == cmd_mgr->max_queue_sz) {
            //debug print cmds in queue when full.
            printk(KERN_CRIT"Too many cmds (%d) already queued, tkn:%d, flags:0x%04x, result:%3d, cmd id:0x%x\n",
                   cmd_mgr->max_queue_sz, cmd->tkn, cmd->flags, 
                   cmd->result, cmd->id);
                   
            cmd->result = -ENOMEM;
            kfree(cmd->a2e_msg);
            
            if(cmd->flags & BL_CMD_FLAG_NONBLOCK)
                kfree(cmd);

            //debug print cmds in queue when full.
            list_for_each_entry(cur, &cmd_mgr->cmds, list) {
                printk(KERN_CRIT"tkn[%d]  flags:0x%04x  result:%3d  cmd:0x%x-%-24s\r\n",
                       cur->tkn, cur->flags, cur->result, 
                       cur->id, BL_ID2STR(cur->id));
            }
            
            spin_unlock_bh(&cmd_mgr->lock);
            
            return -ENOMEM;
        }
        
        last = list_entry(cmd_mgr->cmds.prev, struct bl_cmd, list);
        
        BL_DBG_MSG("%s, last cmdid:0x%x, flags:0x%x\n", 
                   __func__, last->id, last->flags);
                   
        if (last->flags & (BL_CMD_FLAG_WAIT_ACK | BL_CMD_FLAG_WAIT_PUSH)) {
            cmd->flags |= BL_CMD_FLAG_WAIT_PUSH;
            defer_push = true;
        }
    }

    cmd->flags |= BL_CMD_FLAG_WAIT_ACK;
    if (cmd->flags & BL_CMD_FLAG_REQ_CFM)
        cmd->flags |= BL_CMD_FLAG_WAIT_CFM;

    flags = cmd->flags;
    cmd->tkn = cmd_mgr->next_tkn++;
    cmd->result = -EINTR;

    if (!(cmd->flags & BL_CMD_FLAG_NONBLOCK))
        init_completion(&cmd->complete);

    list_add_tail(&cmd->list, &cmd_mgr->cmds);
    cmd_mgr->queue_sz++;
    spin_unlock_bh(&cmd_mgr->lock);

    BL_DBG_MSG("%s, defer_push:%d, msga2e_hostid:0x%p, cmd_sent:%d\n", 
               __func__, defer_push,
               bl_hw->ipc_env->msga2e_hostid, bl_hw->cmd_sent);

    if (!defer_push) {
        #if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB        
        ASSERT_ERR(!(bl_hw->ipc_env->msga2e_hostid));
        spin_lock_bh(&cmd_mgr->lock);
        bl_hw->ipc_env->msga2e_hostid = (void *)cmd;
        bl_hw->cmd_sent = true;
        spin_unlock_bh(&cmd_mgr->lock);
        
        bl_queue_main_work(bl_hw);
        #endif
    }

    BL_DBG_MSG("%s, cmd:0x%p, cmdid:0x%x, cmd_mgr->queue_sz:%d, cmd->flags:0x%x, msga2e_hostid:0x%p, cmd_sent:%d\n", 
               __func__, cmd, cmd->id, cmd_mgr->queue_sz, cmd->flags,
               bl_hw->ipc_env->msga2e_hostid, bl_hw->cmd_sent);

    if (!(flags & BL_CMD_FLAG_NONBLOCK)) {
        unsigned long tout = msecs_to_jiffies(BL_80211_CMD_TIMEOUT_MS +
                                 BL_80211_CMD_TIMEOUT_MS * ((cmd_mgr->queue_sz > 1)? (cmd_mgr->queue_sz - 1) : 0));
        if (!wait_for_completion_timeout(&cmd->complete, tout)) {
            printk(KERN_CRIT"cmd timed-out, cmd:0x%p, tout:%lu, cmd_mgr->queue_sz:%d\n", 
                   cmd, tout, cmd_mgr->queue_sz);
                   
            printk("%s, tkn[%d]  flags:%04x  result:%3d  cmd:0x%x-%-24s - reqcfm(0x%x-%-s)\n",
                   __func__,
                   cmd->tkn, cmd->flags, cmd->result, cmd->id, BL_ID2STR(cmd->id),
                   cmd->reqid, cmd->reqid != (lmac_msg_id_t)-1 ? BL_ID2STR(cmd->reqid) : "none");
            
            printk("msga2e_hostid:0x%p, msga2e_cnt:%u\n", 
                   bl_hw->ipc_env->msga2e_hostid, bl_hw->ipc_env->msga2e_cnt);
                   
            dump_info(bl_hw);

            #if defined(CONFIG_BL_DUMP_FW) && defined(CONFIG_BL_SDIO)
            if (!bl_hw->mod_params->mp_mode)
                dump_fw_criticals(bl_hw);
            #endif
            
            #ifdef CONFIG_BL_USB
            if (!(cmd->flags & BL_CMD_FLAG_DNLD_COMPLETE)) {
                struct bl_usb_device *device = 
                     (struct bl_usb_device *)(bl_hw->plat)->priv;
                     
                if (device->tx_cmd.urb && device->tx_cmd.a2e_msg)
                    usb_kill_urb(device->tx_cmd.urb);
            }
            #endif
            
            spin_lock_bh(&cmd_mgr->lock);
            if (!(cmd->flags & BL_CMD_FLAG_DONE)) {
                cmd_mgr->state = BL_CMD_MGR_STATE_CRASHED;

                cmd->result = -ETIMEDOUT;
                cmd_complete(cmd_mgr, cmd);
            }
            spin_unlock_bh(&cmd_mgr->lock);   
            
            bl_nl_broadcast_event(bl_hw, BL_EVENT_ID_RESET, NULL, 0);
        } else {
            BL_DBG_MSG("%s, finished, cmd id:0x%x, flag:0x%x\n", 
                       __func__, cmd->id, cmd->flags);
        }
    } else {
        cmd->result = 0;
    } 

    return 0;
}

static int cmd_mgr_llind(struct bl_cmd_mgr *cmd_mgr, struct bl_cmd *cmd)
{
    struct bl_cmd *cur, *tmp, *acked = NULL, *next = NULL;
    struct bl_hw *bl_hw = container_of(cmd_mgr, struct bl_hw, cmd_mgr);

    BL_DBG(BL_FN_ENTRY_STR);
    //cmd_dump(cmd);
    
    spin_lock_bh(&cmd_mgr->lock);
    
    ASSERT_WARN(cmd != NULL);
    BL_DBG_MSG("%s, cmd:0x%p, tkn[%d]  flags:0x%04x  result:%3d  cmd:0x%x-%-24s - reqcfm(0x%x-%-s)\n", 
           __func__, cmd, cmd->tkn, cmd->flags, cmd->result, 
           cmd->id, BL_ID2STR(cmd->id), cmd->reqid, 
           cmd->reqid != (lmac_msg_id_t)-1 ? BL_ID2STR(cmd->reqid) : "none");

    if (cmd) {
        //cmd_dump(cmd);
    } else {
        printk("%s, cmd null\r\n", __func__);
    }
    
    list_for_each_entry_safe(cur, tmp, &cmd_mgr->cmds, list) {
        if (!acked) {
            if (cmd && cur->tkn == cmd->tkn) {
                if (WARN_ON_ONCE(cur != cmd)) {
                    printk("%s, tkn[%d]  flags:%04x  result:%3d  cmd:0x%x-%-24s - reqcfm(0x%x-%-s)\n",
                           __func__,
                           cmd->tkn, cmd->flags, cmd->result, cmd->id, BL_ID2STR(cmd->id),
                           cmd->reqid, cmd->reqid != (lmac_msg_id_t)-1 ? BL_ID2STR(cmd->reqid) : "none");
                }
                
                acked = cur;
                continue;
            }
        }
        
        if (cur->flags & BL_CMD_FLAG_WAIT_PUSH) {
            next = cur;
            break;
        }
    }
    
    if (!acked) {
        if (cmd)
            printk(KERN_CRIT "%s Error: acked cmd(0x%x) not found, next:0x%p, not clear msga2e_hostid:0x%p\n",
                   __func__, cmd->id, next, bl_hw->ipc_env->msga2e_hostid);
        else
            printk(KERN_CRIT "%s Error: acked cmd NULL, next:0x%p, not clear msga2e_hostid:0x%p\n",
                   __func__, next, bl_hw->ipc_env->msga2e_hostid);
    } else {
        BL_DBG_MSG("%s cmdid=0x%x acked, flags:0x%x, next:0x%p, clear cur msga2e_hostid:0x%p\n", 
                   __func__, cmd->id, cmd->flags, next,
                   bl_hw->ipc_env->msga2e_hostid);

        bl_hw->ipc_env->msga2e_hostid = NULL;
                   
        cmd->flags &= ~BL_CMD_FLAG_WAIT_ACK;
        if (BL_CMD_WAIT_COMPLETE(cmd->flags))
            cmd_complete(cmd_mgr, cmd);
    }
    
    if (next) {
        ASSERT_ERR(!(bl_hw->ipc_env->msga2e_hostid));
        bl_hw->ipc_env->msga2e_hostid = (void *)next;
        bl_hw->cmd_sent = true;
        bl_queue_main_work(bl_hw);
    }
    
    BL_DBG_MSG("%s exit, bl_hw->cmd_sent:%d, msga2e_hostid:0x%p, next:0x%p\n", 
               __func__, bl_hw->cmd_sent, 
               bl_hw->ipc_env->msga2e_hostid, next);
    
    spin_unlock_bh(&cmd_mgr->lock);

    return 0;
}

static int cmd_mgr_run_callback(struct bl_hw *bl_hw, struct bl_cmd *cmd,
                                       struct bl_cmd_e2amsg *msg, msg_cb_fct cb)
{
    int res;

    BL_DBG_MSG("%s, msg->id:0x%x, cb:0x%p\n", __func__, msg->id, cb);

    if (!cb) {
        BL_DBG("%s msg%d cb=NULL\n", __func__, msg->id);
        return 0;
    }
    
    spin_lock(&bl_hw->cb_lock);
    res = cb(bl_hw, cmd, msg);
    spin_unlock(&bl_hw->cb_lock);

    return res;
}

static int cmd_mgr_msgind(struct bl_cmd_mgr *cmd_mgr, 
                                  struct bl_cmd_e2amsg *msg, msg_cb_fct cb)
{
    struct bl_hw *bl_hw = container_of(cmd_mgr, struct bl_hw, cmd_mgr);
    struct bl_cmd *cmd, *tmp;
    bool found = false;

    BL_DBG(BL_FN_ENTRY_STR);

    BL_DBG_MSG("%s, id:0x%x, %s (%d - %d)\n", __func__,
               msg->id, BL_ID2STR(msg->id), MSG_T(msg->id), MSG_I(msg->id));

    spin_lock_bh(&cmd_mgr->lock);
    
    list_for_each_entry_safe(cmd, tmp, &cmd_mgr->cmds, list) {
        //cmd_dump(cmd);
        BL_DBG("%s, loop, tkn[%d]  flags:%04x  result:%3d  cmd:0x%x-%-24s - reqcfm(0x%x-%-s)\n",
               __func__, cmd->tkn, cmd->flags, cmd->result, 
               cmd->id, BL_ID2STR(cmd->id), cmd->reqid, 
               cmd->reqid != (lmac_msg_id_t)-1 ? BL_ID2STR(cmd->reqid) : "none");

        if (cmd->reqid == msg->id) {
            if (cmd->flags & BL_CMD_FLAG_WAIT_CFM) {
                if (!cmd_mgr_run_callback(bl_hw, cmd, msg, cb)) {
                    found = true;
                    
                    if(bl_hw->ipc_env->msga2e_hostid == (void *)cmd) {
                        BL_DBG_MSG("clear for ack\n");
                        BL_DBG_MSG("%s, bl_hw->ipc_env->msga2e_hostid:0x%p, id:0x%x, reqid:0x%x, flags:0x%x, bl_hw->bl_processing:%d, bl_hw->more_task_flag:%d\n", 
                               __func__, bl_hw->ipc_env->msga2e_hostid, cmd->id, 
                               cmd->reqid, cmd->flags, bl_hw->bl_processing, 
                               bl_hw->more_task_flag);

                        bl_hw->ipc_env->msga2e_hostid = NULL;
                        spin_unlock_bh(&cmd_mgr->lock);
                        cmd_mgr_llind(cmd_mgr, cmd);
                        spin_lock_bh(&cmd_mgr->lock);
                    }
                    cmd->flags &= ~(BL_CMD_FLAG_WAIT_CFM | BL_CMD_FLAG_WAIT_ACK);

                    if (WARN((msg->param_len > BL_CMD_E2AMSG_LEN_MAX),
                             "Unexpect E2A msg len %d > %d\n", msg->param_len,
                             BL_CMD_E2AMSG_LEN_MAX)) {
                        msg->param_len = BL_CMD_E2AMSG_LEN_MAX;
                    }

                    if (cmd->e2a_msg && msg->param_len)
                        memcpy(cmd->e2a_msg, &msg->param, msg->param_len);

                    BL_DBG_MSG("%s, check flags and call complete \n", __func__);
                    
                    if (BL_CMD_WAIT_COMPLETE(cmd->flags)) {
                        cmd_complete(cmd_mgr, cmd);
                    }

                    break;
                }
            }
            else if ((cmd->flags & BL_CMD_FLAG_DNLD_COMPLETE) && 
                     (cmd->flags & BL_CMD_FLAG_WAIT_ACK))
            {
                BL_DBG_MSG("%s, not wait_cfm and wait_ack, cmd:0x%p, cmd->id:0x%x\n",
                       __func__, cmd, cmd->id);
                       
                if(bl_hw->ipc_env->msga2e_hostid == (void *)cmd) {
                    BL_DBG_MSG("%s, bl_hw->ipc_env->msga2e_hostid:0x%p, id:0x%x, reqid:0x%x, flags:0x%x, bl_hw->bl_processing:%d, bl_hw->more_task_flag:%d\n", 
                           __func__, bl_hw->ipc_env->msga2e_hostid, cmd->id, 
                           cmd->reqid, cmd->flags, bl_hw->bl_processing, 
                           bl_hw->more_task_flag);
                           
                    bl_hw->ipc_env->msga2e_hostid = NULL;
                }
                       
                spin_unlock_bh(&cmd_mgr->lock);
                cmd_mgr_llind(cmd_mgr, cmd);
                spin_lock_bh(&cmd_mgr->lock);
                
                break;
            }
        } else {
            BL_DBG("%s, not match, reqid:0x%x, msgid:0x%x\r\n", __func__, 
                   cmd->reqid, msg->id);
            BL_DBG("%s, not match, tkn[%d]  flags:%04x  result:%3d  cmd:0x%x-%-24s - reqcfm(0x%x-%-s)\n",
                   __func__, cmd->tkn, cmd->flags, cmd->result, 
                   cmd->id, BL_ID2STR(cmd->id), cmd->reqid, 
                   cmd->reqid != (lmac_msg_id_t)-1 ? BL_ID2STR(cmd->reqid) : "none");
        }
    }

#if 0
    list_for_each_entry_safe(cmd, tmp, &cmd_mgr->cmds, list) {
        printk("%s, next queued cmd:0x%p, cmd->id:0x%x, cmd->reqid:0x%x, flags:0x%x, tkn:%u\
               bl_hw->bl_processing:%d, bl_hw->more_task_flag:%d\n", 
               __func__, cmd, cmd->id, cmd->reqid, cmd->flags, cmd->tkn,
               bl_hw->bl_processing, bl_hw->more_task_flag);
        break;
    }
    if (bl_hw->ipc_env->msga2e_hostid) {
        cmd = (struct bl_cmd *)bl_hw->ipc_env->msga2e_hostid;
        printk("%s, msga2e_hostid not null, bl_hw->ipc_env->msga2e_hostid:0x%p, \
               cmd id:0x%x, reqid:0x%x, flags:0x%x, \
               bl_hw->bl_processing:%d, bl_hw->more_task_flag:%d\n", 
               __func__, bl_hw->ipc_env->msga2e_hostid, cmd->id, cmd->reqid, cmd->flags,
               bl_hw->bl_processing, bl_hw->more_task_flag);
    }
#endif

    spin_unlock_bh(&cmd_mgr->lock);

    if (!found) {
        if (bl_hw->vif_started)
            cmd_mgr_run_callback(bl_hw, NULL, msg, cb);
        else
            BL_DBG("vif not started, drop this msg : %s (%d - %d)\n", 
                   BL_ID2STR(msg->id), MSG_T(msg->id), MSG_I(msg->id));
    }

    return 0;
}

static void cmd_mgr_print(struct bl_cmd_mgr *cmd_mgr)
{
    struct bl_cmd *cur;

    spin_lock_bh(&cmd_mgr->lock);
    
    BL_DBG("q_sz/max: %2d / %2d - next tkn: %d\n",
             cmd_mgr->queue_sz, cmd_mgr->max_queue_sz,
             cmd_mgr->next_tkn);
             
    list_for_each_entry(cur, &cmd_mgr->cmds, list) {
        cmd_dump(cur);
    }
    
    spin_unlock_bh(&cmd_mgr->lock);
}

static void cmd_mgr_drain(struct bl_cmd_mgr *cmd_mgr)
{
    struct bl_cmd *cur, *nxt;

    BL_DBG(BL_FN_ENTRY_STR);

    spin_lock_bh(&cmd_mgr->lock);
    
    list_for_each_entry_safe(cur, nxt, &cmd_mgr->cmds, list) {
        list_del(&cur->list);
        cmd_mgr->queue_sz--;
        if (!(cur->flags & BL_CMD_FLAG_NONBLOCK))
            complete(&cur->complete);
    }
    
    spin_unlock_bh(&cmd_mgr->lock);
}

void bl_cmd_mgr_init(struct bl_cmd_mgr *cmd_mgr)
{
    BL_DBG(BL_FN_ENTRY_STR);

    INIT_LIST_HEAD(&cmd_mgr->cmds);
    spin_lock_init(&cmd_mgr->lock);
    cmd_mgr->max_queue_sz = BL_CMD_MAX_QUEUED;
    cmd_mgr->queue  = &cmd_mgr_queue;
    cmd_mgr->print  = &cmd_mgr_print;
    cmd_mgr->drain  = &cmd_mgr_drain;
    cmd_mgr->llind  = &cmd_mgr_llind;
    cmd_mgr->msgind = &cmd_mgr_msgind;
}

void bl_cmd_mgr_deinit(struct bl_cmd_mgr *cmd_mgr)
{
    cmd_mgr->print(cmd_mgr);
    cmd_mgr->drain(cmd_mgr);
    cmd_mgr->print(cmd_mgr);
    memset(cmd_mgr, 0, sizeof(*cmd_mgr));
}
