/**
 ******************************************************************************
 *
 *  @file bl_debugfs.h
 *
 *  @brief Miscellaneous utility function definitions
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



#ifndef _BL_DEBUGFS_H_
#define _BL_DEBUGFS_H_

#include <linux/workqueue.h>
#include <linux/if_ether.h>
#include <linux/version.h>
#include "bl_fw_trace.h"

struct bl_hw;
struct bl_sta;

/* some macros taken from iwlwifi */
/* TODO: replace with generic read and fill read buffer in open to avoid double
 * reads */
#define DEBUGFS_ADD_FILE(name, parent, mode) do {               \
    if (!debugfs_create_file(#name, mode, parent, bl_hw,      \
                &bl_dbgfs_##name##_ops))                      \
    goto err;                                                   \
} while (0)

#define DEBUGFS_ADD_BOOL(name, parent, ptr) do {                \
    struct dentry *__tmp;                                       \
    __tmp = debugfs_create_bool(#name, S_IWUSR | S_IRUSR,       \
            parent, ptr);                                       \
    if (IS_ERR(__tmp) || !__tmp)                                \
    goto err;                                                   \
} while (0)

#define DEBUGFS_ADD_X64(name, parent, ptr) do {                 \
    struct dentry *__tmp;                                       \
    __tmp = debugfs_create_x64(#name, S_IWUSR | S_IRUSR,        \
            parent, ptr);                                       \
    if (IS_ERR(__tmp) || !__tmp)                                \
    goto err;                                                   \
} while (0)

#define DEBUGFS_ADD_U64(name, parent, ptr, mode) do {           \
    struct dentry *__tmp;                                       \
    __tmp = debugfs_create_u64(#name, mode,                     \
            parent, ptr);                                       \
    if (IS_ERR(__tmp) || !__tmp)                                \
    goto err;                                                   \
} while (0)

#define DEBUGFS_ADD_X32(name, parent, ptr) do {                 \
    struct dentry *__tmp;                                       \
    __tmp = debugfs_create_x32(#name, S_IWUSR | S_IRUSR,        \
            parent, ptr);                                       \
    if (IS_ERR(__tmp) || !__tmp)                                \
    goto err;                                                   \
} while (0)

//TODO: same with DEBUGFS_ADD_X32/U64......
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 4, 149)
#define DEBUGFS_ADD_U32(name, parent, ptr, mode) do {                 \
    struct dentry *__tmp;                                       \
    __tmp = debugfs_create_u32(#name, S_IWUSR | S_IRUSR,        \
            parent, ptr);                                       \
    if (IS_ERR(__tmp) || !__tmp)                                \
    goto err;                                                   \
} while (0)
#else
#define DEBUGFS_ADD_U32(name, parent, ptr, mode) do {           \
    debugfs_create_u32(#name, mode,                             \
            parent, ptr);                                       \
} while (0)
#endif


/* file operation */
#define DEBUGFS_READ_FUNC(name)                                         \
    static ssize_t bl_dbgfs_##name##_read(struct file *file,          \
                                            char __user *user_buf,      \
                                            size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                         \
    static ssize_t bl_dbgfs_##name##_write(struct file *file,          \
                                             const char __user *user_buf,\
                                             size_t count, loff_t *ppos);

#define DEBUGFS_OPEN_FUNC(name)                              \
    static int bl_dbgfs_##name##_open(struct inode *inode, \
                                        struct file *file);

#define DEBUGFS_RELEASE_FUNC(name)                              \
    static int bl_dbgfs_##name##_release(struct inode *inode, \
                                           struct file *file);

#define DEBUGFS_READ_FILE_OPS(name)                             \
    DEBUGFS_READ_FUNC(name);                                    \
static const struct file_operations bl_dbgfs_##name##_ops = { \
    .read   = bl_dbgfs_##name##_read,                         \
    .open   = simple_open,                                      \
    .llseek = generic_file_llseek,                              \
};

#define DEBUGFS_WRITE_FILE_OPS(name)                            \
    DEBUGFS_WRITE_FUNC(name);                                   \
static const struct file_operations bl_dbgfs_##name##_ops = { \
    .write  = bl_dbgfs_##name##_write,                        \
    .open   = simple_open,                                      \
    .llseek = generic_file_llseek,                              \
};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)                       \
    DEBUGFS_READ_FUNC(name);                                    \
    DEBUGFS_WRITE_FUNC(name);                                   \
static const struct file_operations bl_dbgfs_##name##_ops = { \
    .write  = bl_dbgfs_##name##_write,                        \
    .read   = bl_dbgfs_##name##_read,                         \
    .open   = simple_open,                                      \
    .llseek = generic_file_llseek,                              \
};

#define DEBUGFS_READ_WRITE_OPEN_RELEASE_FILE_OPS(name)              \
    DEBUGFS_READ_FUNC(name);                                        \
    DEBUGFS_WRITE_FUNC(name);                                       \
    DEBUGFS_OPEN_FUNC(name);                                        \
    DEBUGFS_RELEASE_FUNC(name);                                     \
static const struct file_operations bl_dbgfs_##name##_ops = {     \
    .write   = bl_dbgfs_##name##_write,                           \
    .read    = bl_dbgfs_##name##_read,                            \
    .open    = bl_dbgfs_##name##_open,                            \
    .release = bl_dbgfs_##name##_release,                         \
    .llseek  = generic_file_llseek,                                 \
};


#ifdef CONFIG_BL_DEBUGFS
struct bl_debugfs {
    unsigned long long rateidx;
    struct dentry *dir;
    struct dentry *dir_stas;
    bool trace_prst;

    char helper_cmd[64];
    struct work_struct helper_work;
    bool helper_scheduled;
    spinlock_t umh_lock;
    bool unregistering;

    struct bl_fw_trace fw_trace;

#ifdef CONFIG_BL_FULLMAC
    struct work_struct sta_work;
    struct dentry *dir_sta[NX_REMOTE_STA_MAX];
    uint8_t sta_idx;
    struct dentry *dir_rc;
    struct dentry *dir_rc_sta[NX_REMOTE_STA_MAX];
    int rc_config[NX_REMOTE_STA_MAX];
    struct list_head rc_config_save;
    struct dentry *dir_twt;
    struct dentry *dir_twt_sta[NX_REMOTE_STA_MAX];
#endif
};

#ifdef CONFIG_BL_FULLMAC

// Max duration in msecs to save rate config for a sta after disconnection
#define RC_CONFIG_DUR 600000

struct bl_rc_config_save {
    struct list_head list;
    unsigned long timestamp;
    int rate;
    u8 mac_addr[ETH_ALEN];
};
#endif

int bl_dbgfs_register(struct bl_hw *bl_hw, const char *name);
void bl_dbgfs_unregister(struct bl_hw *bl_hw);
int bl_um_helper(struct bl_debugfs *bl_debugfs, const char *cmd);
int bl_trigger_um_helper(struct bl_debugfs *bl_debugfs);
void bl_wait_um_helper(struct bl_hw *bl_hw);
#ifdef CONFIG_BL_FULLMAC
void bl_dbgfs_register_sta(struct bl_hw *bl_hw, struct bl_sta *sta);
void bl_dbgfs_unregister_sta(struct bl_hw *bl_hw, struct bl_sta *sta);
#endif

int bl_dbgfs_register_fw_dump(struct bl_hw *bl_hw,
                                struct dentry *dir_drv,
                                struct dentry *dir_diags);
void bl_dbgfs_trigger_fw_dump(struct bl_hw *bl_hw, char *reason);

void bl_fw_trace_dump(struct bl_hw *bl_hw);
void bl_fw_trace_reset(struct bl_hw *bl_hw);

#else

struct bl_debugfs {
};

static inline int bl_dbgfs_register(struct bl_hw *bl_hw, const char *name) { return 0; }
static inline void bl_dbgfs_unregister(struct bl_hw *bl_hw) {}
static inline int bl_um_helper(struct bl_debugfs *bl_debugfs, const char *cmd) { return 0; }
static inline int bl_trigger_um_helper(struct bl_debugfs *bl_debugfs) {return 0;}
static inline void bl_wait_um_helper(struct bl_hw *bl_hw) {}
#ifdef CONFIG_BL_FULLMAC
static inline void bl_dbgfs_register_sta(struct bl_hw *bl_hw, struct bl_sta *sta)  {}
static inline void bl_dbgfs_unregister_sta(struct bl_hw *bl_hw, struct bl_sta *sta)  {}
#endif

void bl_fw_trace_dump(struct bl_hw *bl_hw) {}
void bl_fw_trace_reset(struct bl_hw *bl_hw) {}

#endif /* CONFIG_BL_DEBUGFS */


#endif /* _BL_DEBUGFS_H_ */
