/**
 ******************************************************************************
 *
 *  @file bl_msg_tx.c
 *
 *  @brief TX function definitions
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
#include <linux/firmware.h>

#include "softmac.h"

#include "bl_msg_tx.h"
#include "bl_mod_params.h"
#ifdef CONFIG_BL_BFMER
#include "bl_bfmer.h"
#endif //(CONFIG_BL_BFMER)
#include "bl_compat.h"
#include "bl_cfgfile.h"
#include "bl_iwpriv.h"
#include "bl_ipc_host.h"
#ifdef CONFIG_BL_USB
#include "bl_usb.h"
#endif

const struct mac_addr mac_addr_bcst = {{0xFFFF, 0xFFFF, 0xFFFF}};

/* Default MAC Rx filters that can be changed by mac80211
 * (via the configure_filter() callback) */
#define BL_MAC80211_CHANGEABLE        (                                       \
                                         NXMAC_ACCEPT_BA_BIT                  | \
                                         NXMAC_ACCEPT_BAR_BIT                 | \
                                         NXMAC_ACCEPT_OTHER_DATA_FRAMES_BIT   | \
                                         NXMAC_ACCEPT_PROBE_REQ_BIT           | \
                                         NXMAC_ACCEPT_PS_POLL_BIT               \
                                        )

/* Default MAC Rx filters that cannot be changed by mac80211 */
#define BL_MAC80211_NOT_CHANGEABLE    (                                       \
                                         NXMAC_ACCEPT_QO_S_NULL_BIT           | \
                                         NXMAC_ACCEPT_Q_DATA_BIT              | \
                                         NXMAC_ACCEPT_DATA_BIT                | \
                                         NXMAC_ACCEPT_OTHER_MGMT_FRAMES_BIT   | \
                                         NXMAC_ACCEPT_MY_UNICAST_BIT          | \
                                         NXMAC_ACCEPT_BROADCAST_BIT           | \
                                         NXMAC_ACCEPT_BEACON_BIT              | \
                                         NXMAC_ACCEPT_PROBE_RESP_BIT            \
                                        )

/* Default MAC Rx filter */
#define BL_DEFAULT_RX_FILTER  (BL_MAC80211_CHANGEABLE | BL_MAC80211_NOT_CHANGEABLE)

const int bw2chnl[] = {
    [NL80211_CHAN_WIDTH_20_NOHT] = PHY_CHNL_BW_20,
    [NL80211_CHAN_WIDTH_20]      = PHY_CHNL_BW_20,
    [NL80211_CHAN_WIDTH_40]      = PHY_CHNL_BW_40,
    [NL80211_CHAN_WIDTH_80]      = PHY_CHNL_BW_80,
    [NL80211_CHAN_WIDTH_160]     = PHY_CHNL_BW_160,
    [NL80211_CHAN_WIDTH_80P80]   = PHY_CHNL_BW_80P80,
};

const int chnl2bw[] = {
    [PHY_CHNL_BW_20]      = NL80211_CHAN_WIDTH_20,
    [PHY_CHNL_BW_40]      = NL80211_CHAN_WIDTH_40,
    [PHY_CHNL_BW_80]      = NL80211_CHAN_WIDTH_80,
    [PHY_CHNL_BW_160]     = NL80211_CHAN_WIDTH_160,
    [PHY_CHNL_BW_80P80]   = NL80211_CHAN_WIDTH_80P80,
};

const uint32_t channel_freq[] =
{
    2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447, 2452, 2457, 2462, 2467, 2472, 2484
};

/*****************************************************************************/
/*
 * Parse the ampdu density to retrieve the value in usec, according to the
 * values defined in ieee80211.h
 */
static inline u8 bl_ampdudensity2usec(u8 ampdudensity)
{
    switch (ampdudensity) {
    case IEEE80211_HT_MPDU_DENSITY_NONE:
        return 0;
        /* 1 microsecond is our granularity */
    case IEEE80211_HT_MPDU_DENSITY_0_25:
    case IEEE80211_HT_MPDU_DENSITY_0_5:
    case IEEE80211_HT_MPDU_DENSITY_1:
        return 1;
    case IEEE80211_HT_MPDU_DENSITY_2:
        return 2;
    case IEEE80211_HT_MPDU_DENSITY_4:
        return 4;
    case IEEE80211_HT_MPDU_DENSITY_8:
        return 8;
    case IEEE80211_HT_MPDU_DENSITY_16:
        return 16;
    default:
        return 0;
    }
}

static inline bool use_pairwise_key(struct cfg80211_crypto_settings *crypto)
{
    if ((crypto->cipher_group ==  WLAN_CIPHER_SUITE_WEP40) ||
        (crypto->cipher_group ==  WLAN_CIPHER_SUITE_WEP104))
        return false;

    return true;
}

bool is_non_blocking_msg(int id)
{
    return ((id == MM_TIM_UPDATE_REQ) || (id == ME_RC_SET_RATE_REQ) ||
            (id == MM_BFMER_ENABLE_REQ) || (id == ME_TRAFFIC_IND_REQ) ||
            (id == TDLS_PEER_TRAFFIC_IND_REQ) ||
            (id == MESH_PATH_CREATE_REQ) || (id == MESH_PROXY_ADD_REQ) ||
            (id == SM_EXTERNAL_AUTH_REQUIRED_RSP));
}

#ifdef CONFIG_BL_FULLMAC
/**
 * copy_connect_ies -- Copy Association Elements in the the request buffer
 * send to the firmware
 *
 * @vif: Vif that received the connection request
 * @req: Connection request to send to the firmware
 * @ft_over_air: Whether FT over the air should be used
 * @ie: List of association elements provided by user space
 * @ie_len: Lenght of the ie buffer
 *
 * For driver that do not use userspace SME (like this one) the host connection
 * request doesn't explicitly mentions that the connection can use FT over the
 * air. If FT over the air is possible:
 * - auth_type = AUTOMATIC (if already set to FT then it means FT over DS)
 * - already associated to a FT BSS
 * - Target Mobility domain is the same as the curent one
 * Then only send the FT elements (as received in update_ft_ies callback) to
 * the firmware
 *
 * In all other cases simply copy the list povided by the user space in the
 * request buffer
 */
static int copy_connect_ies(struct bl_vif *vif, struct sm_connect_req *req,
                                   struct cfg80211_connect_params *sme)
{
    // Test if this is a possible FT over the air
    if ((req->flags & REASSOCIATION) && vif->sta.ft_assoc_ies &&
        (req->auth_type == WLAN_AUTH_OPEN)) {
        const struct element_t *rsne, *fte, *mde, *mde_req;
        uint8_t *pos = (uint8_t *)req->ie_buf;
        int ft_ie_len = 0;

        mde_req = bl_find_elem(WLAN_EID_MOBILITY_DOMAIN,
                               sme->ie, sme->ie_len);
        mde = bl_find_elem(WLAN_EID_MOBILITY_DOMAIN,
                           vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len);
                           
        if (!mde || !mde_req ||
            memcmp(mde, mde_req, sizeof(struct element_t) + mde->datalen))
            goto default_case;

        ft_ie_len += sizeof(struct element_t) + mde->datalen;

        rsne = bl_find_elem(WLAN_EID_RSN, vif->sta.ft_assoc_ies,
                            vif->sta.ft_assoc_ies_len);
        fte = bl_find_elem(WLAN_EID_FAST_BSS_TRANSITION, vif->sta.ft_assoc_ies,
                           vif->sta.ft_assoc_ies_len);
                           
        if (rsne && fte)
            ft_ie_len += 2 * sizeof(struct element_t) + rsne->datalen + fte->datalen;
        else if (rsne || fte) {
            netdev_warn(vif->ndev,
                        "Missing RSNE or FTE element, skip FT over air");
            goto default_case;
        }

        if (ft_ie_len > sizeof(req->ie_buf)) {
            netdev_warn(vif->ndev,
                        "Not enough space for FTE, skip FT over air");
            goto default_case;
        }

        // We can use FT over the air
        req->auth_type = WLAN_AUTH_FT;
        memcpy(&vif->sta.ft_target_ap, sme->bssid, ETH_ALEN);

        if (rsne) {
            memcpy(pos, rsne, sizeof(struct element_t) + rsne->datalen);
            pos += sizeof(struct element_t) + rsne->datalen;
        }
        
        memcpy(pos, mde, sizeof(struct element_t) + mde->datalen);
        pos += sizeof(struct element_t) + mde->datalen;
        if (fte) {
            memcpy(pos, fte, sizeof(struct element_t) + fte->datalen);
            pos += sizeof(struct element_t) + fte->datalen;
        }

        req->ie_len = pos - (uint8_t *)req->ie_buf;
        return 0;
    }

default_case:
    if (sme->ie_len > sizeof(req->ie_buf)) {
        netdev_warn(vif->ndev, "Not enough space to send all connection Elements");
        return -1;
    }
    memcpy(req->ie_buf, sme->ie, sme->ie_len);
    req->ie_len = sme->ie_len;

    return 0;
}
#endif

static inline u8_l get_chan_flags(uint32_t flags)
{
    u8_l chan_flags = 0;
    
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)	
    if (flags & IEEE80211_CHAN_NO_IR)
        chan_flags |= CHAN_NO_IR;
#endif
    if (flags & IEEE80211_CHAN_RADAR)
        chan_flags |= CHAN_RADAR;
        
    return chan_flags;
}

static inline s8_l chan_to_fw_pwr(int power)
{
    return power > 127 ? 127 : (s8_l)power;
}

static void cfg80211_to_bl_chan(const struct cfg80211_chan_def *chandef,
                                  struct mac_chan_op *chan)
{
    chan->band = chandef->chan->band;
    chan->type = bw2chnl[chandef->width];
    chan->prim20_freq = chandef->chan->center_freq;
    chan->center1_freq = chandef->center_freq1;
    chan->center2_freq = chandef->center_freq2;
    chan->flags = get_chan_flags(chandef->chan->flags);
    chan->tx_power = chan_to_fw_pwr(chandef->chan->max_power);
}

static inline void limit_chan_bw(u8_l *bw, u16_l primary, u16_l *center1)
{
    int oft, new_oft = 10;

    if (*bw <= PHY_CHNL_BW_40)
        return;

    oft = *center1 - primary;
    *bw = PHY_CHNL_BW_40;

    if (oft < 0)
        new_oft = new_oft * -1;
    if (abs(oft) == 10 || abs(oft) == 50)
        new_oft = new_oft * -1;

    *center1 = primary + new_oft;
}

/**
 ******************************************************************************
 * @brief Allocate memory for a message
 *
 * This primitive allocates memory for a message that has to be sent. The memory
 * is allocated dynamically on the heap and the length of the variable parameter
 * structure has to be provided in order to allocate the correct size.
 *
 * Several additional parameters are provided which will be preset in the message
 * and which may be used internally to choose the kind of memory to allocate.
 *
 * The memory allocated will be automatically freed by the kernel, after the
 * pointer has been sent to ke_msg_send(). If the message is not sent, it must
 * be freed explicitly with ke_msg_free().
 *
 * Allocation failure is considered critical and should not happen.
 *
 * @param[in] id        Message identifier
 * @param[in] dest_id   Destination Task Identifier
 * @param[in] src_id    Source Task Identifier
 * @param[in] param_len Size of the message parameters to be allocated
 *
 * @return Pointer to the parameter member of the ke_msg. If the parameter
 *         structure is empty, the pointer will point to the end of the message
 *         and should not be used (except to retrieve the message pointer or to
 *         send the message)
 ******************************************************************************
 */
inline void *bl_msg_zalloc(lmac_msg_id_t const id,
                                lmac_task_id_t const dest_id,
                                lmac_task_id_t const src_id,
                                uint16_t const param_len)
{
    struct lmac_msg *msg;
    gfp_t flags;
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    int len = (sizeof(struct lmac_msg) + param_len + 3)/4*4;
#endif

    if (is_non_blocking_msg(id))
        flags = GFP_ATOMIC;
    else
        flags = GFP_KERNEL;
#if defined CONFIG_BL_PCIE
    msg = (struct lmac_msg *)kzalloc(sizeof(struct lmac_msg) + param_len,
                                     flags);
#elif defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    msg = (struct lmac_msg *)kzalloc(len, flags);
#endif
    if (msg == NULL) {
        printk(KERN_CRIT "%s: msg allocation failed\n", __func__);
        return NULL;
    }

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    msg->inf_hdr.type = BL_TYPE_MSG;
    msg->inf_hdr.len = len;
    msg->inf_hdr.queue_idx = 0xff;
#endif
    msg->id = id;
    msg->dest_id = dest_id;
    msg->src_id = src_id;
    msg->param_len = param_len;

    return msg->param;
}

inline void *bl_msg_ke_zalloc(lmac_msg_id_t const id,
                                    lmac_task_id_t const dest_id,
                                    lmac_task_id_t const src_id,
                                    uint16_t const param_len)
{
    struct lmac_msg *msg;
    gfp_t flags;
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    int len = (sizeof(struct lmac_msg) + param_len + 3)/4*4;
#endif

    flags = GFP_ATOMIC;
#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    msg = (struct lmac_msg *)kzalloc(len, flags);
#endif
    if (msg == NULL) {
        printk(KERN_CRIT "%s: msg allocation failed\n", __func__);
        return NULL;
    }

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
    msg->inf_hdr.type = BL_TYPE_MSG;
    msg->inf_hdr.len = len;
    msg->inf_hdr.queue_idx = 0xff;
#endif
    msg->id = id;
    msg->dest_id = dest_id;
    msg->src_id = src_id;
    msg->param_len = param_len;

    return msg->param;
}

static void bl_msg_free(struct bl_hw *bl_hw, const void *msg_params)
{
    struct lmac_msg *msg = container_of((void *)msg_params,
                                        struct lmac_msg, param);

    BL_DBG(BL_FN_ENTRY_STR);

    /* Free the message */
    kfree(msg);
}

static int bl_send_msg(struct bl_hw *bl_hw, const void *msg_params,
                            int reqcfm, lmac_msg_id_t reqid, void *cfm)
{
    struct lmac_msg *msg;
    struct bl_cmd *cmd;
    bool nonblock;
    int ret;

    BL_DBG(BL_FN_ENTRY_STR);

    msg = container_of((void *)msg_params, struct lmac_msg, param);

    if (bl_hw->surprise_removed ||
        (!test_bit(BL_DEV_STARTED, &bl_hw->flags) &&
        reqid != MM_RESET_CFM && reqid != MM_VERSION_CFM &&
        reqid != MM_START_CFM && reqid != MM_SET_IDLE_CFM &&
        reqid != ME_CONFIG_CFM && reqid != MM_SET_PS_MODE_CFM &&
        reqid != ME_CHAN_CONFIG_CFM && reqid != MM_CAL_CFG_CFM &&
        reqid != DBG_BTBLE_UART_BAUD_CFM))
    {
        BL_DBG("%s: bypassing, bl_hw->flags:0x%lx, reqid:0x%x\n",
               __func__, bl_hw->flags, reqid);
               
        kfree(msg);
        
        return -EBUSY;
    } else if (!bl_hw->ipc_env) {
        printk(KERN_CRIT "%s: failed, ipc_env NULL\n", __func__);
        kfree(msg);
        
        return -EBUSY;
    }

    nonblock = is_non_blocking_msg(msg->id);

    cmd = kzalloc(sizeof(struct bl_cmd), nonblock ? GFP_ATOMIC : GFP_KERNEL);
    cmd->result  = -EINTR;
    cmd->id      = msg->id;
    cmd->reqid   = reqid;
    cmd->a2e_msg = msg;
    cmd->e2a_msg = cfm;
    if (nonblock)
        cmd->flags = BL_CMD_FLAG_NONBLOCK;
    if (reqcfm)
        cmd->flags |= BL_CMD_FLAG_REQ_CFM;
        
    ret = bl_hw->cmd_mgr.queue(&bl_hw->cmd_mgr, cmd);
        
    if (!nonblock){
        if (!ret)
            ret = cmd->result;
        kfree(cmd);
    }

    return ret;
}

int bl_send_ke_msg(struct bl_hw *bl_hw, const void *msg_params,
                          u16_l reqid)
{
    struct lmac_msg *msg;
    struct bl_cmd *cmd;
    bool nonblock;
    int ret;
    int reqcfm = 0;
    void *cfm = NULL;

    BL_DBG_MSG(BL_FN_ENTRY_STR);

    BL_DBG_MSG("%s, reqid:0x%x\r\n", __func__, reqid);

    msg = container_of((void *)msg_params, struct lmac_msg, param);

    if (bl_hw->surprise_removed || !test_bit(BL_DEV_STARTED, &bl_hw->flags))
    {
        BL_DBG("%s: bypassing, bl_hw->flags:0x%02x, surprise_removed:%d\n",
               __func__, reqid, bl_hw->surprise_removed);
        kfree(msg);
        
        return -EBUSY;
    } else if (!bl_hw->ipc_env) {
        printk(KERN_CRIT "%s, failed, ipc_env NULL\n", __func__);
        kfree(msg);
        
        return -EBUSY;
    }

    nonblock = true;

    cmd = kzalloc(sizeof(struct bl_cmd), nonblock ? GFP_ATOMIC : GFP_KERNEL);
    cmd->result  = -EINTR;
    cmd->id      = msg->id;
    cmd->reqid   = reqid;
    cmd->a2e_msg = msg;
    cmd->e2a_msg = cfm;
    
    if (nonblock)
        cmd->flags = BL_CMD_FLAG_NONBLOCK;
    if (reqcfm)
        cmd->flags |= BL_CMD_FLAG_REQ_CFM;
        
    ret = bl_hw->cmd_mgr.queue(&bl_hw->cmd_mgr, cmd);
        
    if (!nonblock){
        if (!ret)
            ret = cmd->result;
        kfree(cmd);
    }

    return ret;
}

/******************************************************************************
 *    Control messages handling functions (SOFTMAC and  FULLMAC)
 *****************************************************************************/
int bl_send_reset(struct bl_hw *bl_hw)
{
    void *void_param;

    BL_DBG(BL_FN_ENTRY_STR);

    /* RESET REQ has no parameter */
    void_param = bl_msg_zalloc(MM_RESET_REQ, TASK_MM, DRV_TASK_ID, 0);
    if (!void_param)
        return -ENOMEM;

    return bl_send_msg(bl_hw, void_param, 1, MM_RESET_CFM, NULL);
}

int bl_send_start(struct bl_hw *bl_hw)
{
    struct mm_start_req *start_req_param;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the START REQ message */
    start_req_param = bl_msg_zalloc(MM_START_REQ, TASK_MM, DRV_TASK_ID,
                                      sizeof(struct mm_start_req));
    if (!start_req_param)
        return -ENOMEM;

    /* Set parameters for the START message */
    memcpy(&start_req_param->phy_cfg, &bl_hw->phy.cfg, sizeof(bl_hw->phy.cfg));
    start_req_param->uapsd_timeout = (u32_l)bl_hw->mod_params->uapsd_timeout;
    start_req_param->lp_clk_accuracy = (u16_l)bl_hw->mod_params->lp_clk_ppm;
    start_req_param->tx_timeout[AC_BK] = (u16_l)bl_hw->mod_params->tx_to_bk;
    start_req_param->tx_timeout[AC_BE] = (u16_l)bl_hw->mod_params->tx_to_be;
    start_req_param->tx_timeout[AC_VI] = (u16_l)bl_hw->mod_params->tx_to_vi;
    start_req_param->tx_timeout[AC_VO] = (u16_l)bl_hw->mod_params->tx_to_vo;

    /* Send the START REQ message to LMAC FW */
    return bl_send_msg(bl_hw, start_req_param, 1, MM_START_CFM, NULL);
}

#ifdef CONFIG_BL_MP
int bl_send_mp_test_msg(struct bl_hw *bl_hw, char *mp_cmd, uint32_t cmd_len,
                                 char *mp_test_cfm, bool nonblock)
{
    int ret;
    struct lmac_msg *msg;
    struct dbg_mp_req *mp_msg;
    struct bl_cmd *cmd;

    BL_DBG(BL_FN_ENTRY_STR);

    mp_msg = (struct dbg_mp_req *)bl_msg_zalloc(DBG_MP_REQ, TASK_DBG, 
                       DRV_TASK_ID, cmd_len);
    if (!mp_msg)
        return -ENOMEM;

    memcpy(mp_msg->payload, mp_cmd, cmd_len);
    msg = container_of((void *)mp_msg, struct lmac_msg, param);

    cmd = kzalloc(sizeof(struct bl_cmd), nonblock ? GFP_ATOMIC : GFP_KERNEL);
    cmd->result  = -EINTR;
    cmd->id      = msg->id;
    cmd->reqid   = DBG_MP_CFM;
    cmd->a2e_msg = msg;
    cmd->e2a_msg = mp_test_cfm;
    if (nonblock)
        cmd->flags = BL_CMD_FLAG_NONBLOCK;
    cmd->flags |= BL_CMD_FLAG_REQ_CFM;
    
    //Insmod mp_mode=1, then potential running wpa_supplicant on background may send some cmds to mfg test firmware and get no response,
    //Make the cmd_mgr.state to crashed. Restore it anyway.
    if (bl_hw->cmd_mgr.state == BL_CMD_MGR_STATE_CRASHED) {
        bl_hw->cmd_mgr.state = BL_CMD_MGR_STATE_INITED;
        bl_hw->ipc_env->msga2e_hostid = NULL;
    }
    
    ret = bl_hw->cmd_mgr.queue(&bl_hw->cmd_mgr, cmd);

    if (ret == 0 && cmd->result != 0)
        ret = cmd->result;

    if (cmd->result == -ETIMEDOUT || cmd->result == -EPIPE) {
        printk("bl_send_mp_test_msg restore cmd_mgr's state after timeout\n");
        bl_hw->cmd_mgr.state = BL_CMD_MGR_STATE_INITED;
        bl_hw->ipc_env->msga2e_hostid = NULL;
    }

    if (!nonblock)
        kfree(cmd);

    return ret;
}

int bl_send_mp2_test_msg(struct bl_hw *bl_hw, char *mp_cmd,
                                  char *mp_test_cfm, bool nonblock)
{
    int ret;
    struct lmac_msg *msg;
    struct dbg_mp_req *mp_msg;
    struct bl_cmd *cmd;

    BL_DBG(BL_FN_ENTRY_STR);

    mp_msg = (struct dbg_mp_req *)bl_msg_zalloc(DBG_MP2_REQ, TASK_DBG, 
                                                DRV_TASK_ID, strlen(mp_cmd));
    if (!mp_msg)
        return -ENOMEM;

    memcpy(mp_msg->payload, mp_cmd, strlen(mp_cmd));
    msg = container_of((void *)mp_msg, struct lmac_msg, param);

    cmd = kzalloc(sizeof(struct bl_cmd), nonblock ? GFP_ATOMIC : GFP_KERNEL);
    cmd->result  = -EINTR;
    cmd->id      = msg->id;
    cmd->reqid   = DBG_MP2_CFM;
    cmd->a2e_msg = msg;
    cmd->e2a_msg = mp_test_cfm;
    if (nonblock)
        cmd->flags = BL_CMD_FLAG_NONBLOCK;
    //cmd->flags |= BL_CMD_FLAG_REQ_CFM;
    
    //Insmod mp_mode=1, then potential running wpa_supplicant on background may send some cmds to mfg test firmware and get no response,
    //Make the cmd_mgr.state to crashed. Restore it anyway.
    if (bl_hw->cmd_mgr.state == BL_CMD_MGR_STATE_CRASHED) {
        bl_hw->cmd_mgr.state = BL_CMD_MGR_STATE_INITED;
        bl_hw->ipc_env->msga2e_hostid = NULL;
    }
    
    ret = bl_hw->cmd_mgr.queue(&bl_hw->cmd_mgr, cmd);

    if (ret == 0 && cmd->result != 0)
        ret = cmd->result;

    if (cmd->result == -ETIMEDOUT || cmd->result == -EPIPE) {
        printk("bl_send_mp2_test_msg restore cmd_mgr's state after timeout\n");
        bl_hw->cmd_mgr.state = BL_CMD_MGR_STATE_INITED;
        bl_hw->ipc_env->msga2e_hostid = NULL;
    }

    if (!nonblock)
        kfree(cmd);

    return ret;
}

#endif

#if defined(CONFIG_FW_COMBO) && defined(CONFIG_BL_BTUART) && !defined(CONFIG_BL_BTSDU)
int bl_send_btble_uart_req(struct bl_hw *bl_hw, int baud, int flow,
                                  int rts, int cts)
{
    struct dbg_btble_uart_baud_req *btble_uart_req;

    BL_DBG(BL_FN_ENTRY_STR);

    btble_uart_req = bl_msg_zalloc(DBG_BTBLE_UART_BAUD_REQ, TASK_DBG, DRV_TASK_ID,
                                   sizeof(struct dbg_btble_uart_baud_req));
    if (!btble_uart_req)
        return -ENOMEM;

    btble_uart_req->baud = baud;
    btble_uart_req->flow = flow;
    btble_uart_req->rts = rts;
    btble_uart_req->cts = cts;

    printk("%s, baud %d, flow %d, rts %d, cts %d\n",
           __func__, baud, flow, rts, cts);
    
    return bl_send_msg(bl_hw, btble_uart_req, 1, DBG_BTBLE_UART_BAUD_CFM, NULL);
}
#endif

int bl_send_rw_coex_param(struct bl_hw *bl_hw, 
          struct dbg_coex_rw_param_req *req, struct dbg_coex_rw_param_cfm *cfm)
{
    uint8_t *param = NULL;
    
    BL_DBG(BL_FN_ENTRY_STR);

    param = bl_msg_zalloc(DBG_COEX_RW_PARAM_REQ, TASK_DBG,
                          DRV_TASK_ID, sizeof(struct dbg_coex_rw_param_req));
    if (!param)
        return -ENOMEM;

    memcpy(param, req, sizeof(struct dbg_coex_rw_param_req));

    return bl_send_msg(bl_hw, param, 1, DBG_COEX_RW_PARAM_CFM, cfm);
}

int bl_send_wmmcfg(struct bl_hw *bl_hw, u32 cfg_value)
{
    struct mm_wmm_cfg_req *wmm_cfg_req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_WMM_CFG_REQ message */
    wmm_cfg_req = bl_msg_zalloc(MM_WMM_CFG_REQ, TASK_MM, DRV_TASK_ID,
                                sizeof(struct mm_wmm_cfg_req));
    if (!wmm_cfg_req)
        return -ENOMEM;

    /* Set parameters for the MM_WMM_CFG_REQ message */
    wmm_cfg_req->cfg_value = cfg_value;

    /* Send the MM_WMM_CFG_REQ message */
    return bl_send_msg(bl_hw, wmm_cfg_req, 1, MM_WMM_CFG_CFM, NULL);
}

int bl_send_cal_cfg(struct bl_hw *bl_hw, struct mm_cal_cfg_req *cal_cfg)
{
    struct mm_cal_cfg_req *cal_cfg_req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_CAL_CFG_REQ message */
    cal_cfg_req = bl_msg_zalloc(MM_CAL_CFG_REQ, TASK_MM, DRV_TASK_ID,
                                sizeof(struct mm_cal_cfg_req));
    if (!cal_cfg_req)
        return -ENOMEM;

    memcpy(cal_cfg_req, cal_cfg, sizeof(struct mm_cal_cfg_req));
    
    /* Send the MM_CAL_CFG_REQ message */
    return bl_send_msg(bl_hw, cal_cfg_req, 1, MM_CAL_CFG_CFM, NULL);
}

int bl_send_version_req(struct bl_hw *bl_hw, struct mm_version_cfm *cfm)
{
    void *void_param;

    BL_DBG(BL_FN_ENTRY_STR);

    /* VERSION REQ has no parameter */
    void_param = bl_msg_zalloc(MM_VERSION_REQ, TASK_MM, DRV_TASK_ID, 0);
    if (!void_param)
        return -ENOMEM;

    return bl_send_msg(bl_hw, void_param, 1, MM_VERSION_CFM, cfm);
}

int bl_send_add_if(struct bl_hw *bl_hw, const unsigned char *mac,
                        enum nl80211_iftype iftype, bool p2p, 
                        struct mm_add_if_cfm *cfm)
{
    struct mm_add_if_req *add_if_req_param;
    struct mm_add_if_req vif_req_param;
    int ret = 0;
    
    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the ADD_IF_REQ message */
    add_if_req_param = bl_msg_zalloc(MM_ADD_IF_REQ, TASK_MM, DRV_TASK_ID,
                                     sizeof(struct mm_add_if_req));
    if (!add_if_req_param)
        return -ENOMEM;

    BL_DBG_MSG("%s, iftype:%d\r\n", __func__, iftype);

    /* Set parameters for the ADD_IF_REQ message */
    memcpy(&(add_if_req_param->addr.array[0]), mac, ETH_ALEN);
    
    switch (iftype) {
    #ifdef CONFIG_BL_FULLMAC
    case NL80211_IFTYPE_P2P_CLIENT:
        add_if_req_param->p2p = true;
        // no break
    #endif /* CONFIG_BL_FULLMAC */
    case NL80211_IFTYPE_STATION:
        add_if_req_param->type = MM_STA;
        break;

    case NL80211_IFTYPE_ADHOC:
        add_if_req_param->type = MM_IBSS;
        break;

    #ifdef CONFIG_BL_FULLMAC
    case NL80211_IFTYPE_P2P_GO:
        add_if_req_param->p2p = true;
        // no break
    #endif /* CONFIG_BL_FULLMAC */
    case NL80211_IFTYPE_AP:
        add_if_req_param->type = MM_AP;
        break;
    case NL80211_IFTYPE_MESH_POINT:
        add_if_req_param->type = MM_MESH_POINT;
        break;
    case NL80211_IFTYPE_AP_VLAN:
        return -1;
    case NL80211_IFTYPE_MONITOR:
        add_if_req_param->type = MM_MONITOR;
        break;
    default:
        add_if_req_param->type = MM_STA;
        break;
    }

    BL_DBG_MSG("%s, iftype:%d, %d\r\n", __func__, iftype, add_if_req_param->type);
    
    vif_req_param = *add_if_req_param;
    
    /* Send the ADD_IF_REQ message to LMAC FW */
    ret = bl_send_msg(bl_hw, add_if_req_param, 1, MM_ADD_IF_CFM, cfm);

    if (ret == 0 && cfm->status == 0) {
        softmac_vif_add(&vif_req_param, cfm->inst_nbr);
    } else {
        printk("%s, ret:%d, cfm->status:%d, iftype:%d\n",
               __func__, ret, cfm->status, iftype);
    }

    return ret;
}

int bl_send_remove_if(struct bl_hw *bl_hw, u8 vif_index)
{
    struct mm_remove_if_req *remove_if_req;
    int ret = 0;
    
    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_REMOVE_IF_REQ message */
    remove_if_req = bl_msg_zalloc(MM_REMOVE_IF_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_remove_if_req));
    if (!remove_if_req)
        return -ENOMEM;

    /* Set parameters for the MM_REMOVE_IF_REQ message */
    remove_if_req->inst_nbr = vif_index;

    /* Send the MM_REMOVE_IF_REQ message to LMAC FW */
    ret = bl_send_msg(bl_hw, remove_if_req, 1, MM_REMOVE_IF_CFM, NULL);

    softmac_vif_remove(vif_index);
    
    return ret;
}

int bl_send_set_channel(struct bl_hw *bl_hw, int phy_idx,
                               struct mm_set_channel_cfm *cfm)
{
    struct mm_set_channel_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    if (phy_idx >= bl_hw->phy.cnt)
        return -ENOTSUPP;

    req = bl_msg_zalloc(MM_SET_CHANNEL_REQ, TASK_MM, DRV_TASK_ID,
                        sizeof(struct mm_set_channel_req));
    if (!req)
        return -ENOMEM;

    if (phy_idx == 0) {
        /* On FULLMAC only setting channel of secondary chain */
        wiphy_err(bl_hw->wiphy, "Trying to set channel of primary chain");
        return 0;
    } else {
        req->chan = bl_hw->phy.sec_chan;
    }

    req->index = phy_idx;

    if (bl_hw->phy.limit_bw)
        limit_chan_bw(&req->chan.type, req->chan.prim20_freq, &req->chan.center1_freq);

#if 0
    BL_DBG("mac80211:   freq=%d(c1:%d - c2:%d)/width=%d - band=%d\n"
             "   hw(%d): prim20=%d(c1:%d - c2:%d)/ type=%d - band=%d\n",
             center_freq, center_freq1, center_freq2, width, band,
             phy_idx, req->chan.prim20_freq, req->chan.center1_freq,
             req->chan.center2_freq, req->chan.type, req->chan.band);
#endif

    /* Send the MM_SET_CHANNEL_REQ REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, MM_SET_CHANNEL_CFM, cfm);
}


int bl_send_key_add(struct bl_hw *bl_hw, u8 vif_idx, u8 sta_idx, 
                          bool pairwise, u8 *key, u8 key_len, u8 key_idx, 
                          u8 cipher_suite, struct mm_key_add_cfm *cfm)
{
    struct mm_key_add_req *key_add_req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_KEY_ADD_REQ message */
    key_add_req = bl_msg_zalloc(MM_KEY_ADD_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_key_add_req));
    if (!key_add_req)
        return -ENOMEM;

    /* Set parameters for the MM_KEY_ADD_REQ message */
    if (sta_idx != 0xFF) {
        /* Pairwise key */
        key_add_req->sta_idx = sta_idx;
    } else {
        /* Default key */
        key_add_req->sta_idx = sta_idx;
        key_add_req->key_idx = (u8_l)key_idx; /* only useful for default keys */
    }
    
    key_add_req->pairwise = pairwise;
    key_add_req->inst_nbr = vif_idx;
    key_add_req->key.length = key_len;
    memcpy(&(key_add_req->key.array[0]), key, key_len);

    key_add_req->cipher_suite = cipher_suite;

    BL_DBG("%s: sta_idx:%d key_idx:%d inst_nbr:%d cipher:%d key_len:%d\n", __func__,
             key_add_req->sta_idx, key_add_req->key_idx, key_add_req->inst_nbr,
             key_add_req->cipher_suite, key_add_req->key.length);

    //bl_dump( key_add_req->key.array, key_add_req->key.length);
  
    /* Send the MM_KEY_ADD_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, key_add_req, 1, MM_KEY_ADD_CFM, cfm);
}

int bl_send_key_del(struct bl_hw *bl_hw, uint8_t hw_key_idx)
{
    struct mm_key_del_req *key_del_req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_KEY_DEL_REQ message */
    key_del_req = bl_msg_zalloc(MM_KEY_DEL_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_key_del_req));
    if (!key_del_req)
        return -ENOMEM;

    /* Set parameters for the MM_KEY_DEL_REQ message */
    key_del_req->hw_key_idx = hw_key_idx;

    /* Send the MM_KEY_DEL_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, key_del_req, 1, MM_KEY_DEL_CFM, NULL);
}

int bl_send_bcn_change(struct bl_hw *bl_hw, u8 vif_idx, u32 *bcn_addr,
                            u16 bcn_len, u16 tim_oft, u16 tim_len, u16 *csa_oft)
{
    struct mm_bcn_change_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_BCN_CHANGE_REQ message */
    req = bl_msg_zalloc(MM_BCN_CHANGE_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_bcn_change_req) + bcn_len);
    if (!req)
        return -ENOMEM;

#if 0
    do {
        const struct element *ie_elem = NULL;
        int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);

        if (bcn_addr) {
            u8 *tmp_bcn = (u8 *)bcn_addr;
            
            printk("%s, var_offset:%d\r\n", __func__, var_offset);
            
            for_each_element_t(ie_elem, tmp_bcn+var_offset, bcn_len-var_offset) {
                if (ie_elem) {
                    printk("id %d, datalen:%d\r\n", ie_elem->id, ie_elem->datalen);
                    bl_dump((u8 *)ie_elem, ie_elem->datalen+2);
                } else {
                    break;
                }
            }
        }
    } while (0);
#endif

    memcpy(req->bcn_buf, bcn_addr, bcn_len);

    /* Set parameters for the MM_BCN_CHANGE_REQ message */
    //req->bcn_ptr = bcn_addr;
    req->bcn_len = bcn_len;
    req->tim_oft = tim_oft;
    req->tim_len = tim_len;
    req->inst_nbr = vif_idx;

    if (csa_oft)
        BL_DBG("%s, vif_idx:%d, bcn_len=%d, tim_oft=%d, tim_len=%d, csa_oft:%d %d\n",
               __func__, vif_idx, req->bcn_len, req->tim_oft, req->tim_len, 
               csa_oft[0], csa_oft[1]);
    else
        BL_DBG("%s, vif_idx:%d, bcn_len=%d, tim_oft=%d, tim_len=%d\n",
               __func__, vif_idx, req->bcn_len, req->tim_oft, req->tim_len);
               
    if (csa_oft) {
        int i;
        for (i = 0; i < BCN_MAX_CSA_CPT; i++) {
            req->csa_oft[i] = csa_oft[i];
        }
    }

    /* Send the MM_BCN_CHANGE_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, MM_BCN_CHANGE_CFM, NULL);
}

int bl_send_roc(struct bl_hw *bl_hw, struct bl_vif *vif,
                    struct ieee80211_channel *chan, unsigned  int duration)
{
    struct mm_remain_on_channel_req *req;
    struct cfg80211_chan_def chandef;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Create channel definition structure */
    cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);

    /* Build the MM_REMAIN_ON_CHANNEL_REQ message */
    req = bl_msg_zalloc(MM_REMAIN_ON_CHANNEL_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_remain_on_channel_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_REMAIN_ON_CHANNEL_REQ message */
    req->op_code      = MM_ROC_OP_START;
    req->vif_index    = vif->vif_index;
    req->duration_ms  = duration;
    cfg80211_to_bl_chan(&chandef, &req->chan);

    /* Send the MM_REMAIN_ON_CHANNEL_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, MM_REMAIN_ON_CHANNEL_CFM, NULL);
}

int bl_send_cancel_roc(struct bl_hw *bl_hw)
{
    struct mm_remain_on_channel_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_REMAIN_ON_CHANNEL_REQ message */
    req = bl_msg_zalloc(MM_REMAIN_ON_CHANNEL_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_remain_on_channel_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_REMAIN_ON_CHANNEL_REQ message */
    req->op_code = MM_ROC_OP_CANCEL;

    /* Send the MM_REMAIN_ON_CHANNEL_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 0, MM_REMAIN_ON_CHANNEL_CFM, NULL);
}

int bl_send_set_power(struct bl_hw *bl_hw, u8 vif_idx, s8 pwr,
                             struct mm_set_power_cfm *cfm)
{
    struct mm_set_power_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_SET_POWER_REQ message */
    req = bl_msg_zalloc(MM_SET_POWER_REQ, TASK_MM, DRV_TASK_ID,
                          sizeof(struct mm_set_power_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_SET_POWER_REQ message */
    req->inst_nbr = vif_idx;
    req->power = pwr;

    /* Send the MM_SET_POWER_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, MM_SET_POWER_CFM, cfm);
}

int bl_send_set_phy_misc(struct bl_hw *bl_hw, uint32_t type, 
                                  int32_t *misc_value, uint32_t misc_value_len, 
                                  struct mm_set_phy_misc_cfm *phy_misc_cfm)
{
    struct mm_set_phy_misc_req *set_phy_misc_req;

    BL_DBG(BL_FN_ENTRY_STR);

    if (misc_value_len > sizeof(struct mm_set_phy_misc_req)-4) {
        printk("%s, too long misc value for this cmd\r\n", __func__);
        return -ENOMEM;
    }

    set_phy_misc_req = bl_msg_zalloc(MM_SET_PHY_MISC_REQ, TASK_MM, DRV_TASK_ID, 
                                    sizeof(struct mm_set_phy_misc_req));
    if (!set_phy_misc_req)
        return -ENOMEM;

    set_phy_misc_req->misc_type = type;
    memcpy(set_phy_misc_req->misc_value, misc_value, misc_value_len);

    return bl_send_msg(bl_hw, set_phy_misc_req, 1, 
                       MM_SET_PHY_MISC_CFM, phy_misc_cfm);
}

int bl_send_set_edca(struct bl_hw *bl_hw, u8 hw_queue, u32 param,
                            bool uapsd, u8 inst_nbr)
{
    struct mm_set_edca_req *set_edca_req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_SET_EDCA_REQ message */
    set_edca_req = bl_msg_zalloc(MM_SET_EDCA_REQ, TASK_MM, DRV_TASK_ID,
                                   sizeof(struct mm_set_edca_req));
    if (!set_edca_req)
        return -ENOMEM;

    /* Set parameters for the MM_SET_EDCA_REQ message */
    set_edca_req->ac_param = param;
    set_edca_req->uapsd = uapsd;
    set_edca_req->hw_queue = hw_queue;
    set_edca_req->inst_nbr = inst_nbr;

    /* Send the MM_SET_EDCA_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, set_edca_req, 1, MM_SET_EDCA_CFM, NULL);
}

int bl_send_set_ps_mode(struct bl_hw *bl_hw, u8 ps_mode)
{
    struct mm_set_ps_mode_req *set_ps_mode_req_param;

    BL_DBG(BL_FN_ENTRY_STR);

    set_ps_mode_req_param =
            bl_msg_zalloc(MM_SET_PS_MODE_REQ, TASK_MM, DRV_TASK_ID,
                            sizeof(struct mm_set_ps_mode_req));

    if (!set_ps_mode_req_param)
        return -ENOMEM;

    set_ps_mode_req_param->new_state = ps_mode;

    return bl_send_msg(bl_hw, set_ps_mode_req_param, 1, MM_SET_PS_MODE_CFM, NULL);
}

#ifdef CONFIG_BL_P2P_DEBUGFS
int bl_send_p2p_oppps_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                                   u8 ctw, struct mm_set_p2p_oppps_cfm *cfm)
{
    struct mm_set_p2p_oppps_req *p2p_oppps_req;
    int error;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_SET_P2P_OPPPS_REQ message */
    p2p_oppps_req = bl_msg_zalloc(MM_SET_P2P_OPPPS_REQ, TASK_MM, DRV_TASK_ID,
                                    sizeof(struct mm_set_p2p_oppps_req));

    if (!p2p_oppps_req) {
        return -ENOMEM;
    }

    /* Fill the message parameters */
    p2p_oppps_req->vif_index = bl_vif->vif_index;
    p2p_oppps_req->ctwindow = ctw;

    /* Send the MM_P2P_OPPPS_REQ message to LMAC FW */
    error = bl_send_msg(bl_hw, p2p_oppps_req, 1, MM_SET_P2P_OPPPS_CFM, cfm);

    return (error);
}

int bl_send_p2p_noa_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                          int count, int interval, int duration, bool dyn_noa,
                          struct mm_set_p2p_noa_cfm *cfm)
{
    struct mm_set_p2p_noa_req *p2p_noa_req;
    int error;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Param check */
    if (count > 255)
        count = 255;

    if (duration >= interval) {
        dev_err(bl_hw->dev, "Invalid p2p NOA config: interval=%d <= duration=%d\n",
                interval, duration);
        return -EINVAL;
    }

    /* Build the MM_SET_P2P_NOA_REQ message */
    p2p_noa_req = bl_msg_zalloc(MM_SET_P2P_NOA_REQ, TASK_MM, DRV_TASK_ID,
                                  sizeof(struct mm_set_p2p_noa_req));

    if (!p2p_noa_req) {
        return -ENOMEM;
    }

    /* Fill the message parameters */
    p2p_noa_req->vif_index = bl_vif->vif_index;
    p2p_noa_req->noa_inst_nb = 0;
    p2p_noa_req->count = count;

    if (count) {
        p2p_noa_req->duration_us = duration * 1024;
        p2p_noa_req->interval_us = interval * 1024;
        p2p_noa_req->start_offset = (interval - duration - 10) * 1024;
        p2p_noa_req->dyn_noa = dyn_noa;
    }

    /* Send the MM_SET_2P_NOA_REQ message to LMAC FW */
    error = bl_send_msg(bl_hw, p2p_noa_req, 1, MM_SET_P2P_NOA_CFM, cfm);

    return (error);
}
#endif /* CONFIG_BL_P2P_DEBUGFS */

/******************************************************************************
 *    Control messages handling functions (FULLMAC only)
 *****************************************************************************/
#ifdef CONFIG_BL_FULLMAC
int bl_send_me_config_req(struct bl_hw *bl_hw)
{
    struct me_config_req *req;
    struct wiphy *wiphy = bl_hw->wiphy;
    //struct ieee80211_sta_ht_cap *ht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->ht_cap;
    //struct ieee80211_sta_vht_cap *vht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->vht_cap;
    struct ieee80211_sta_ht_cap *ht_cap = &wiphy->bands[NL80211_BAND_2GHZ]->ht_cap; //bl_band_2GHz
    struct ieee80211_sta_vht_cap *vht_cap = NULL;
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
    struct ieee80211_sta_he_cap const *he_cap = NULL;
//#endif
    uint8_t *ht_mcs = (uint8_t *)&ht_cap->mcs;
    int i;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the ME_CONFIG_REQ message */
    req = bl_msg_zalloc(ME_CONFIG_REQ, TASK_ME, DRV_TASK_ID,
                        sizeof(struct me_config_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_CONFIG_REQ message */
    req->ht_supp = ht_cap->ht_supported;
    req->vht_supp = false;
    req->ht_cap.ht_capa_info = cpu_to_le16(ht_cap->cap);
    req->ht_cap.a_mpdu_param = ht_cap->ampdu_factor |
               (ht_cap->ampdu_density << IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);

    for (i = 0; i < sizeof(ht_cap->mcs); i++)
        req->ht_cap.mcs_rate[i] = ht_mcs[i];
    req->ht_cap.ht_extended_capa = 0;
    req->ht_cap.tx_beamforming_capa = 0;
    req->ht_cap.asel_capa = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
    vht_cap = &wiphy->bands[NL80211_BAND_2GHZ]->vht_cap;
#else
    vht_cap = &bl_hw->vht_cap;
#endif

    if (vht_cap && bl_hw->mod_params->vht_on) {
        req->vht_supp = vht_cap->vht_supported;
        req->vht_cap.vht_capa_info = cpu_to_le32(vht_cap->cap);
        req->vht_cap.rx_highest = cpu_to_le16(vht_cap->vht_mcs.rx_highest);
        req->vht_cap.rx_mcs_map = cpu_to_le16(vht_cap->vht_mcs.rx_mcs_map);
        req->vht_cap.tx_highest = cpu_to_le16(vht_cap->vht_mcs.tx_highest);
        req->vht_cap.tx_mcs_map = cpu_to_le16(vht_cap->vht_mcs.tx_mcs_map);
    }
    
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    if (wiphy->bands[NL80211_BAND_2GHZ]->iftype_data != NULL)
        he_cap = &wiphy->bands[NL80211_BAND_2GHZ]->iftype_data->he_cap;
#else
    he_cap = &bl_hw->he_cap;
#endif

//#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
    //if (wiphy->bands[NL80211_BAND_5GHZ]->iftype_data != NULL) {
        //he_cap = &wiphy->bands[NL80211_BAND_5GHZ]->iftype_data->he_cap;
//    if (wiphy->bands[NL80211_BAND_2GHZ]->iftype_data != NULL) {
      if(he_cap && he_cap->has_he) {
        BL_DBG("%s Set 2G HE CAP\n", __func__);
        //he_cap = &wiphy->bands[NL80211_BAND_2GHZ]->iftype_data->he_cap;

        req->he_supp = he_cap->has_he;
        for (i = 0; i < ARRAY_SIZE(he_cap->he_cap_elem.mac_cap_info); i++) {
            req->he_cap.mac_cap_info[i] = he_cap->he_cap_elem.mac_cap_info[i];
        }
        for (i = 0; i < ARRAY_SIZE(he_cap->he_cap_elem.phy_cap_info); i++) {
            req->he_cap.phy_cap_info[i] = he_cap->he_cap_elem.phy_cap_info[i];
        }
        req->he_cap.mcs_supp.rx_mcs_80 = 
                             cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_80);
        req->he_cap.mcs_supp.tx_mcs_80 = 
                             cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_80);
        req->he_cap.mcs_supp.rx_mcs_160 = 
                             cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_160);
        req->he_cap.mcs_supp.tx_mcs_160 = 
                             cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_160);
        req->he_cap.mcs_supp.rx_mcs_80p80 = 
                             cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_80p80);
        req->he_cap.mcs_supp.tx_mcs_80p80 = 
                             cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_80p80);

        for (i = 0; i < MAC_HE_PPE_THRES_MAX_LEN; i++) {
            req->he_cap.ppe_thres[i] = he_cap->ppe_thres[i];
        }
        req->he_ul_on = bl_hw->mod_params->he_ul_on;
    }else {
        BL_DBG("%s NOT Set 2G HE CAP\n", __func__);
    }
//#else
//    req->he_supp = false;
//    req->he_ul_on = false;
//#endif

    req->ps_on = bl_hw->mod_params->ps_on;
    req->dpsm = bl_hw->mod_params->dpsm;
    req->tx_lft = bl_hw->mod_params->tx_lft;
    req->ant_div_on = bl_hw->mod_params->ant_div;
    
    if (bl_hw->mod_params->use_80)
        req->phy_bw_max = PHY_CHNL_BW_80;
    else if (bl_hw->mod_params->use_2040)
        req->phy_bw_max = PHY_CHNL_BW_40;
    else
        req->phy_bw_max = PHY_CHNL_BW_20;

    wiphy_info(wiphy, "HT supp %d, VHT supp %d, HE supp %d\n",
               req->ht_supp, req->vht_supp, req->he_supp);

    softmac_me_config(req);

    /* Send the ME_CONFIG_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, ME_CONFIG_CFM, NULL);
}

int bl_send_me_chan_config_req(struct bl_hw *bl_hw)
{
    struct me_chan_config_req *req;
    struct wiphy *wiphy = bl_hw->wiphy;
    int i;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the ME_CHAN_CONFIG_REQ message */
    req = bl_msg_zalloc(ME_CHAN_CONFIG_REQ, TASK_ME, DRV_TASK_ID,
                        sizeof(struct me_chan_config_req));
    if (!req)
        return -ENOMEM;

    req->chan2G4_cnt=  0;
    if (wiphy->bands[NL80211_BAND_2GHZ] != NULL) {
        struct ieee80211_supported_band *b = wiphy->bands[NL80211_BAND_2GHZ];
        
        for (i = 0; i < b->n_channels; i++) {
            printk("%d, ch flag:0x%x, disabled:%d, no_ir:%d, radar:%d\n", i,
                   b->channels[i].flags, 
                   (b->channels[i].flags&IEEE80211_CHAN_DISABLED), 
                   (b->channels[i].flags&IEEE80211_CHAN_NO_IR), 
                   (b->channels[i].flags&IEEE80211_CHAN_RADAR));
            
            req->chan2G4[req->chan2G4_cnt].flags = 0;
            if (b->channels[i].flags & IEEE80211_CHAN_DISABLED)
                req->chan2G4[req->chan2G4_cnt].flags |= CHAN_DISABLED;
            req->chan2G4[req->chan2G4_cnt].flags |= get_chan_flags(b->channels[i].flags);
            req->chan2G4[req->chan2G4_cnt].band = NL80211_BAND_2GHZ;
            req->chan2G4[req->chan2G4_cnt].freq = b->channels[i].center_freq;
            req->chan2G4[req->chan2G4_cnt].tx_power = 
                                  chan_to_fw_pwr(b->channels[i].max_power);
            req->chan2G4_cnt++;
            if (req->chan2G4_cnt == MAC_DOMAINCHANNEL_24G_MAX)
                break;
        }
    }
    
    printk("req->chan2G4_cnt:%d\n", req->chan2G4_cnt);

#ifdef BL_BAND_5G
    req->chan5G_cnt = 0;
    
    if (wiphy->bands[NL80211_BAND_5GHZ] != NULL) {
        struct ieee80211_supported_band *b = wiphy->bands[NL80211_BAND_5GHZ];
        
        for (i = 0; i < b->n_channels; i++) {
            req->chan5G[req->chan5G_cnt].flags = 0;
            
            if (b->channels[i].flags & IEEE80211_CHAN_DISABLED)
                req->chan5G[req->chan5G_cnt].flags |= CHAN_DISABLED;
            req->chan5G[req->chan5G_cnt].flags |= get_chan_flags(b->channels[i].flags);
            req->chan5G[req->chan5G_cnt].band = NL80211_BAND_5GHZ;
            req->chan5G[req->chan5G_cnt].freq = b->channels[i].center_freq;
            req->chan5G[req->chan5G_cnt].tx_power =
                                   chan_to_fw_pwr(b->channels[i].max_power);
            req->chan5G_cnt++;
            
            if (req->chan5G_cnt == MAC_DOMAINCHANNEL_5G_MAX)
                break;
        }
    }
#endif

    softmac_me_chan_config(req);
    
    /* Send the ME_CHAN_CONFIG_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, ME_CHAN_CONFIG_CFM, NULL);
}

int bl_send_me_set_control_port_req(struct bl_hw *bl_hw, 
                                                bool opened, u8 sta_idx)
{
    struct me_set_control_port_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the ME_SET_CONTROL_PORT_REQ message */
    req = bl_msg_zalloc(ME_SET_CONTROL_PORT_REQ, TASK_ME, DRV_TASK_ID,
                        sizeof(struct me_set_control_port_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_SET_CONTROL_PORT_REQ message */
    req->sta_idx = sta_idx;
    req->control_port_open = opened;

    /* Send the ME_SET_CONTROL_PORT_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, ME_SET_CONTROL_PORT_CFM, NULL);
}

int bl_send_me_sta_add(struct bl_hw *bl_hw, 
                               struct station_parameters *params,
                               const u8 *mac, u8 inst_nbr, 
                               struct me_sta_add_cfm *cfm)
{
    struct me_sta_add_req *req;
    u8 *ht_mcs = (u8 *)&STA_PARAM_LINK(params)->ht_capa->mcs;
    int i;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_STA_ADD_REQ message */
    req = bl_msg_zalloc(ME_STA_ADD_REQ, TASK_ME, DRV_TASK_ID,
                        sizeof(struct me_sta_add_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_STA_ADD_REQ message */
    memcpy(&(req->mac_addr.array[0]), mac, ETH_ALEN);

    req->rate_set.length = STA_PARAM_LINK(params)->supported_rates_len;
    for (i = 0; i < STA_PARAM_LINK(params)->supported_rates_len; i++)
        req->rate_set.array[i] = STA_PARAM_LINK(params)->supported_rates[i];

    req->flags = 0;
    if (STA_PARAM_LINK(params)->ht_capa) {
        const struct ieee80211_ht_cap *ht_capa = STA_PARAM_LINK(params)->ht_capa;

        req->flags |= STA_HT_CAPA;
        req->ht_cap.ht_capa_info = cpu_to_le16(ht_capa->cap_info);
        req->ht_cap.a_mpdu_param = ht_capa->ampdu_params_info;
        for (i = 0; i < sizeof(ht_capa->mcs); i++)
            req->ht_cap.mcs_rate[i] = ht_mcs[i];
        req->ht_cap.ht_extended_capa = cpu_to_le16(ht_capa->extended_ht_cap_info);
        req->ht_cap.tx_beamforming_capa = cpu_to_le32(ht_capa->tx_BF_cap_info);
        req->ht_cap.asel_capa = ht_capa->antenna_selection_info;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
    if (STA_PARAM_LINK(params)->vht_capa) {
        const struct ieee80211_vht_cap *vht_capa = 
                              STA_PARAM_LINK(params)->vht_capa;

        req->flags |= STA_VHT_CAPA;
        req->vht_cap.vht_capa_info = cpu_to_le32(vht_capa->vht_cap_info);
        req->vht_cap.rx_highest = cpu_to_le16(vht_capa->supp_mcs.rx_highest);
        req->vht_cap.rx_mcs_map = cpu_to_le16(vht_capa->supp_mcs.rx_mcs_map);
        req->vht_cap.tx_highest = cpu_to_le16(vht_capa->supp_mcs.tx_highest);
        req->vht_cap.tx_mcs_map = cpu_to_le16(vht_capa->supp_mcs.tx_mcs_map);
    }
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    if (STA_PARAM_LINK(params)->he_capa) {
        const struct ieee80211_he_cap_elem *he_capa = 
                              STA_PARAM_LINK(params)->he_capa;
        struct ieee80211_he_mcs_nss_supp *mcs_nss_supp =
                              (struct ieee80211_he_mcs_nss_supp *)(he_capa + 1);

        req->flags |= STA_HE_CAPA;

        // he_cap supported from hostapd 2.9/wpa_supplicant2.9
        // all sizeof(ieee80211_he_cap_elem) = 17.
        for (i = 0; i < MAC_HE_MAC_CAPA_LEN; i++) {
            req->he_cap.mac_cap_info[i] = he_capa->mac_cap_info[i];
        }
        
        for (i = 0; i < MAC_HE_PHY_CAPA_LEN; i++) {
            req->he_cap.phy_cap_info[i] = he_capa->phy_cap_info[i];
        }

        // Kernel 4.19 he_capa size differ with hostapd/wpa_supplicant
        if (sizeof(struct ieee80211_he_cap_elem) != 
                      (MAC_HE_MAC_CAPA_LEN + MAC_HE_PHY_CAPA_LEN)) 
        {
            printk("WARN:%s he_cap_size:mac=%zu,phy=%zu.\n", __func__, 
                    sizeof(he_capa->mac_cap_info), 
                    sizeof(he_capa->phy_cap_info));
            mcs_nss_supp = (struct ieee80211_he_mcs_nss_supp *)
                            ((u8_l *)he_capa + MAC_HE_MAC_CAPA_LEN + 
                            MAC_HE_PHY_CAPA_LEN);
        }

        req->he_cap.mcs_supp.rx_mcs_80 = mcs_nss_supp->rx_mcs_80;
        req->he_cap.mcs_supp.tx_mcs_80 = mcs_nss_supp->tx_mcs_80;
        req->he_cap.mcs_supp.rx_mcs_160 = mcs_nss_supp->rx_mcs_160;
        req->he_cap.mcs_supp.tx_mcs_160 = mcs_nss_supp->tx_mcs_160;
        req->he_cap.mcs_supp.rx_mcs_80p80 = mcs_nss_supp->rx_mcs_80p80;
        req->he_cap.mcs_supp.tx_mcs_80p80 = mcs_nss_supp->tx_mcs_80p80;

        BL_DBG("%s rxmcs80=0x%x, txmcs80=0x%x \n", __func__, 
               mcs_nss_supp->rx_mcs_80, mcs_nss_supp->tx_mcs_80);
    }
#else
    //TODO: construct he_cap for peer without kernel info.
#endif

    if (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME))
        req->flags |= STA_QOS_CAPA;

    if (params->sta_flags_set & BIT(NL80211_STA_FLAG_MFP))
        req->flags |= STA_MFP_CAPA;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)	
    if (STA_PARAM_LINK(params)->opmode_notif_used) {
        req->flags |= STA_OPMOD_NOTIF;
        req->opmode = STA_PARAM_LINK(params)->opmode_notif;
    }
#endif
    req->aid = cpu_to_le16(params->aid);
    req->uapsd_queues = params->uapsd_queues;
    req->max_sp_len = params->max_sp * 2;
    req->vif_idx = inst_nbr;

    if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)) {
        struct bl_vif *bl_vif = bl_hw->vif_table[inst_nbr];
        req->tdls_sta = true;
        if ((params->ext_capab[3] & WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) &&
            !bl_vif->tdls_chsw_prohibited)
            req->tdls_chsw_allowed = true;
        if (bl_vif->tdls_status == TDLS_SETUP_RSP_TX)
            req->tdls_sta_initiator = true;
    }

    /* Send the ME_STA_ADD_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, ME_STA_ADD_CFM, cfm);
}

int bl_send_me_sta_del(struct bl_hw *bl_hw, u8 sta_idx, bool tdls_sta)
{
    struct me_sta_del_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_STA_DEL_REQ message */
    req = bl_msg_zalloc(ME_STA_DEL_REQ, TASK_ME, DRV_TASK_ID,
                        sizeof(struct me_sta_del_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_STA_DEL_REQ message */
    req->sta_idx = sta_idx;
    req->tdls_sta = tdls_sta;

    /* Send the ME_STA_DEL_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, ME_STA_DEL_CFM, NULL);
}

int bl_send_me_traffic_ind(struct bl_hw *bl_hw, u8 sta_idx, 
                                  bool uapsd, u8 tx_status)
{
    struct me_traffic_ind_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the ME_UTRAFFIC_IND_REQ message */
    req = bl_msg_zalloc(ME_TRAFFIC_IND_REQ, TASK_ME, DRV_TASK_ID,
                          sizeof(struct me_traffic_ind_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_TRAFFIC_IND_REQ message */
    req->sta_idx = sta_idx;
    req->tx_avail = tx_status;
    req->uapsd = uapsd;

    /* Send the ME_TRAFFIC_IND_REQ to UMAC FW */
    return bl_send_msg(bl_hw, req, 1, ME_TRAFFIC_IND_CFM, NULL);
}

int bl_send_twt_request(struct bl_hw *bl_hw,
                              u8 setup_type, u8 vif_idx,
                              struct twt_conf_tag *conf,
                              struct twt_setup_cfm *cfm)
{
    struct twt_setup_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the TWT_SETUP_REQ message */
    req = bl_msg_zalloc(TWT_SETUP_REQ, TASK_TWT, DRV_TASK_ID,
                          sizeof(struct twt_setup_req));
    if (!req)
        return -ENOMEM;

    memcpy(&req->conf, conf, sizeof(req->conf));
    req->setup_type = setup_type;
    req->vif_idx = vif_idx;

    /* Send the TWT_SETUP_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, TWT_SETUP_CFM, cfm);
}

int bl_send_twt_teardown(struct bl_hw *bl_hw,
                                 struct twt_teardown_req *twt_teardown,
                                 struct twt_teardown_cfm *cfm)
{
    struct twt_teardown_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the TWT_TEARDOWN_REQ message */
    req = bl_msg_zalloc(TWT_TEARDOWN_REQ, TASK_TWT, DRV_TASK_ID,
                          sizeof(struct twt_teardown_req));
    if (!req)
        return -ENOMEM;

    memcpy(req, twt_teardown, sizeof(struct twt_teardown_req));

    /* Send the TWT_TEARDOWN_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, TWT_TEARDOWN_CFM, cfm);
}

int bl_send_me_rc_stats(struct bl_hw *bl_hw,
                               u8 sta_idx, struct me_rc_stats_cfm *cfm)
{
    struct me_rc_stats_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the ME_RC_STATS_REQ message */
    req = bl_msg_zalloc(ME_RC_STATS_REQ, TASK_ME, DRV_TASK_ID,
                        sizeof(struct me_rc_stats_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_RC_STATS_REQ message */
    req->sta_idx = sta_idx;

    /* Send the ME_RC_STATS_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, ME_RC_STATS_CFM, cfm);
}

int bl_send_me_rc_set_rate(struct bl_hw *bl_hw,
                                   u8 sta_idx, u16 rate_cfg)
{
    struct me_rc_set_rate_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the ME_RC_SET_RATE_REQ message */
    req = bl_msg_zalloc(ME_RC_SET_RATE_REQ, TASK_ME, DRV_TASK_ID,
                          sizeof(struct me_rc_set_rate_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_RC_SET_RATE_REQ message */
    req->sta_idx = sta_idx;
    req->fixed_rate_cfg = rate_cfg;

    /* Send the ME_RC_SET_RATE_REQ message to FW */
    return bl_send_msg(bl_hw, req, 0, 0, NULL);
}

int bl_send_me_set_ps_mode(struct bl_hw *bl_hw, u8 ps_mode)
{
    struct me_set_ps_mode_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the ME_SET_PS_MODE_REQ message */
    req = bl_msg_zalloc(ME_SET_PS_MODE_REQ, TASK_ME, DRV_TASK_ID,
                          sizeof(struct me_set_ps_mode_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the ME_SET_PS_MODE_REQ message */
    req->ps_state = ps_mode;

    /* Send the ME_SET_PS_MODE_REQ message to FW */
    return bl_send_msg(bl_hw, req, 1, ME_SET_PS_MODE_CFM, NULL);
}

int bl_send_sm_connect_req(struct bl_hw *bl_hw,
                                     struct bl_vif *bl_vif,
                                     struct cfg80211_connect_params *sme,
                                     struct sm_connect_cfm *cfm)
{
    struct sm_connect_req *req;
    int i;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the SM_CONNECT_REQ message */
    req = bl_msg_zalloc(SM_CONNECT_REQ, TASK_SM, DRV_TASK_ID,
                        sizeof(struct sm_connect_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the SM_CONNECT_REQ message */
    if (sme->crypto.n_ciphers_pairwise &&
        ((sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP40) ||
         (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_TKIP) ||
         (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP104)))
        req->flags |= DISABLE_HT;

    if (sme->crypto.control_port)
        req->flags |= CONTROL_PORT_HOST;

    if (sme->crypto.control_port_no_encrypt)
        req->flags |= CONTROL_PORT_NO_ENC;

    if (use_pairwise_key(&sme->crypto))
        req->flags |= WPA_WPA2_IN_USE;

    if (sme->mfp == NL80211_MFP_REQUIRED)
        req->flags |= MFP_IN_USE;

    req->ctrl_port_ethertype = sme->crypto.control_port_ethertype;

    if (sme->bssid)
        memcpy(&req->bssid, sme->bssid, ETH_ALEN);
    else
        req->bssid = mac_addr_bcst;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
    if (sme->prev_bssid)
        req->flags |= REASSOCIATION;
#else
    if (bl_vif->sta.ap)
        req->flags |= REASSOCIATION;
#endif

    if (sme->auth_type == NL80211_AUTHTYPE_FT)
        req->flags |= (REASSOCIATION | FT_OVER_DS);

    req->vif_idx = bl_vif->vif_index;
    
    if (sme->channel) {
        req->chan.band = sme->channel->band;
        req->chan.freq = sme->channel->center_freq;
        req->chan.flags = get_chan_flags(sme->channel->flags);
    } else {
        req->chan.freq = (u16_l)-1;
    }
    
    for (i = 0; i < sme->ssid_len; i++)
        req->ssid.array[i] = sme->ssid[i];
        
    req->ssid.length = sme->ssid_len;

    req->listen_interval = bl_mod_params.listen_itv;
    req->dont_wait_bcmc = !bl_mod_params.listen_bcmc;

    /* Set auth_type */
    if (sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC)
        req->auth_type = WLAN_AUTH_OPEN;
    else if (sme->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM)
        req->auth_type = WLAN_AUTH_OPEN;
    else if (sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY)
        req->auth_type = WLAN_AUTH_SHARED_KEY;
    else if (sme->auth_type == NL80211_AUTHTYPE_FT)
        req->auth_type = WLAN_AUTH_FT;
    else if (sme->auth_type == NL80211_AUTHTYPE_SAE)
        req->auth_type = WLAN_AUTH_SAE;
    else
        goto invalid_param;

    if (copy_connect_ies(bl_vif, req, sme))
        goto invalid_param;

    /* Set UAPSD queues */
    req->uapsd_queues = bl_mod_params.uapsd_queues;
    printk("%s, vif=%d req_flag=0x%x, band=%d, freq=%d, auth_type=%d, listen=%d, ie_len=%d\n",
            __func__, req->vif_idx, req->flags, req->chan.band, req->chan.freq, 
            req->auth_type, req->listen_interval, req->ie_len);

    /* Send the SM_CONNECT_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, SM_CONNECT_CFM, cfm);

invalid_param:
    bl_msg_free(bl_hw, req);
    
    return -EINVAL;
}

int bl_send_sm_disconnect_req(struct bl_hw *bl_hw,
                                         struct bl_vif *bl_vif, u16 reason)
{
    struct sm_disconnect_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the SM_DISCONNECT_REQ message */
    req = bl_msg_zalloc(SM_DISCONNECT_REQ, TASK_SM, DRV_TASK_ID,
                        sizeof(struct sm_disconnect_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the SM_DISCONNECT_REQ message */
    req->reason_code = reason;
    req->vif_idx = bl_vif->vif_index;

    printk("==>send disconnect req \n");
	
    /* Send the SM_DISCONNECT_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, SM_DISCONNECT_CFM, NULL);
}

int bl_send_sm_external_auth_required_rsp(struct bl_hw *bl_hw,
                                              struct bl_vif *bl_vif, u16 status)
{
    struct sm_external_auth_required_rsp *rsp;

    /* Build the SM_EXTERNAL_AUTH_CFM message */
    rsp = bl_msg_zalloc(SM_EXTERNAL_AUTH_REQUIRED_RSP, TASK_SM, DRV_TASK_ID,
                          sizeof(struct sm_external_auth_required_rsp));
    if (!rsp)
        return -ENOMEM;

    rsp->status = status;
    rsp->vif_idx = bl_vif->vif_index;

    /* send the SM_EXTERNAL_AUTH_REQUIRED_RSP message UMAC FW */
    return bl_send_msg(bl_hw, rsp, 0, 0, NULL);
}

int bl_send_sm_ft_auth_rsp(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                                    uint8_t *ie, int ie_len)
{
    struct sm_ft_auth_rsp *rsp;

    if (sizeof(rsp->ie_buf) < ie_len)
        return -EINVAL;

    rsp = bl_msg_zalloc(SM_FT_AUTH_RSP, TASK_SM, DRV_TASK_ID,
                          sizeof(struct sm_ft_auth_rsp));
    if (!rsp)
        return -ENOMEM;

    rsp->vif_idx = bl_vif->vif_index;
    rsp->ie_len = ie_len;
    memcpy(rsp->ie_buf, ie, rsp->ie_len);

    return bl_send_msg(bl_hw, rsp, 0, 0, NULL);
}
int bl_change_mpdu_density(const u8 *var_pos, int len)
{
    u8 *ht_capa_ie;

    ht_capa_ie = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, var_pos, len);
    if(ht_capa_ie) {
        BL_DBG("ht_capa_ie[0]: %02x %02x %02x %02x %02x\n",
                ht_capa_ie[0], ht_capa_ie[1], ht_capa_ie[2], 
                ht_capa_ie[3], ht_capa_ie[4]);
        ht_capa_ie[4] &= 0x1F; //mpdu density=7
        //ht_capa_ie[4] &= 0x17; //mpdu density=5
        //ht_capa_ie[4] &= 0x03;   //mpdu density=0
    }

    return 0;
}
int bl_change_ch_width_in_opmode(const u8 *var_pos, int len)
{
    const u8 *he_capa_ie;
    const u8 *opmode_notif_ie;

    he_capa_ie = cfg80211_find_ie(WLAN_EID_EXTENSION, var_pos, len);
    if(he_capa_ie) {
        opmode_notif_ie = cfg80211_find_ie(WLAN_EID_OPMODE_NOTIF, var_pos, len);
        if(opmode_notif_ie) {
            u8 *ch_width_in_he_capa = he_capa_ie + 9;
            u8 *ch_width_in_opmode = opmode_notif_ie +2;
            if(((*ch_width_in_he_capa) & 0x02) == 0)
                *ch_width_in_opmode &= 0xFC;
            else
                *ch_width_in_opmode |= 0x01;
        BL_DBG("ch_width_in_opmode = 0x%02x\n", *ch_width_in_opmode);
        }
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    he_capa_ie = cfg80211_find_ie(WLAN_EID_EXT_HE_CAPABILITY, var_pos, len);
    if(he_capa_ie) {
        printk("WLAN_EID_EXT_HE_CAPABILITY\n");
        //TODO: cannot find this EID
    }
#endif

    return 0;
}

int bl_send_apm_start_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                  struct cfg80211_ap_settings *settings,
                                  struct apm_start_cfm *cfm,
                                  struct bl_ipc_elem_var *elem)
{
    struct apm_start_req *req;
    struct bl_bcn *bcn = &vif->ap.bcn;
    u8 *buf;
    u32 flags = 0;
    const u8 *rate_ie;
    u8 rate_len = 0;
    int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
    const u8 *var_pos;
    int len, i;
    //const struct element *ie_elem = NULL;

    BL_DBG(BL_FN_ENTRY_STR);

    // Build the beacon
    bcn->dtim = (u8)settings->dtim_period;
    buf = bl_build_bcn(bl_hw, bcn, &settings->beacon);
    if (!buf) {
        printk("%s no MEM for building bcn\n",__func__);
        return -ENOMEM;
    }

    #if 0
    for_each_element_t(ie_elem, buf+var_offset, bcn->len-var_offset) {
        printk("id %d, datalen:%d\r\n", ie_elem->id, ie_elem->datalen);
        print_elem((u8 *)ie_elem, ie_elem->datalen+2);        
    }
    #endif

    /* Build the APM_START_REQ message */
    req = bl_msg_zalloc(APM_START_REQ, TASK_APM, DRV_TASK_ID,
                        sizeof(struct apm_start_req) + bcn->len);
    if (!req)
        return -ENOMEM;

    // Retrieve the basic rate set from the beacon buffer
    len = bcn->len - var_offset;
    var_pos = buf + var_offset;

// Assume that rate higher that 54 Mbps are BSS membership
#define IS_BASIC_RATE(r) (r & 0x80) && ((r & ~0x80) <= (54 * 2))
    //bl_change_mpdu_density(var_pos, len);
    bl_change_ch_width_in_opmode(var_pos, len);

    rate_ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, var_pos, len);
    if (rate_ie) {
        const u8 *rates = rate_ie + 2;
        for (i = 0; (i < rate_ie[1]) && (rate_len < MAC_RATESET_LEN); i++) {
            if (IS_BASIC_RATE(rates[i]))
                req->basic_rates.array[rate_len++] = rates[i];
        }
    }
    rate_ie = cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES, var_pos, len);
    if (rate_ie) {
        const u8 *rates = rate_ie + 2;
        for (i = 0; (i < rate_ie[1]) && (rate_len < MAC_RATESET_LEN); i++) {
            if (IS_BASIC_RATE(rates[i]))
                req->basic_rates.array[rate_len++] = rates[i];
        }
    }
    req->basic_rates.length = rate_len;
#undef IS_BASIC_RATE

#if 0
    // Sync buffer for FW
    if ((error = bl_ipc_elem_var_allocs(bl_hw, elem, bcn->len,
                                          DMA_TO_DEVICE, buf, NULL, NULL))) {
        return error;
    }
#else
    // Fill in the DMA structure
    elem->addr = buf;
    elem->size = bcn->len;
    memcpy(req->bcn_buf, elem->addr, elem->size);
#endif
    /* Set parameters for the APM_START_REQ message */
    req->vif_idx = vif->vif_index;
    //req->bcn_addr = elem->dma_addr;
    req->bcn_len = bcn->len;
    req->tim_oft = bcn->head_len;
    req->tim_len = bcn->tim_len;
    cfg80211_to_bl_chan(&settings->chandef, &req->chan);
    req->bcn_int = settings->beacon_interval;
    
    if (settings->crypto.control_port)
        flags |= CONTROL_PORT_HOST;

    if (settings->crypto.control_port_no_encrypt)
        flags |= CONTROL_PORT_NO_ENC;

    if (use_pairwise_key(&settings->crypto))
        flags |= WPA_WPA2_IN_USE;

    if (settings->crypto.control_port_ethertype)
        req->ctrl_port_ethertype = settings->crypto.control_port_ethertype;
    else
        req->ctrl_port_ethertype = ETH_P_PAE;
        
    req->flags = flags;

    /* Send the APM_START_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, APM_START_CFM, cfm);
}

int bl_send_apm_stop_req(struct bl_hw *bl_hw, struct bl_vif *vif)
{
    struct apm_stop_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the APM_STOP_REQ message */
    req = bl_msg_zalloc(APM_STOP_REQ, TASK_APM, DRV_TASK_ID,
                          sizeof(struct apm_stop_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the APM_STOP_REQ message */
    req->vif_idx = vif->vif_index;

    /* Send the APM_STOP_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, APM_STOP_CFM, NULL);
}

int bl_send_apm_probe_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                    struct bl_sta *sta, 
                                    struct apm_probe_client_cfm *cfm)
{
    struct apm_probe_client_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    req = bl_msg_zalloc(APM_PROBE_CLIENT_REQ, TASK_APM, DRV_TASK_ID,
                          sizeof(struct apm_probe_client_req));
    if (!req)
        return -ENOMEM;

    req->vif_idx = vif->vif_index;
    req->sta_idx = sta->sta_idx;

    /* Send the APM_PROBE_CLIENT_REQ message to UMAC FW */
    return bl_send_msg(bl_hw, req, 1, APM_PROBE_CLIENT_CFM, cfm);
}

int bl_send_user_scanu_req(struct bl_hw *bl_hw, 
                                    struct bl_vif *bl_vif, u8 enable)
{
    struct scanu_start_req *req;
    int i = 0;

    if (enable) {
        bl_hw->priv_scan.prob_req_en = true;
        /* Build the SCANU_START_REQ message */
        req = bl_msg_zalloc(SCANU_START_REQ, TASK_SCANU, DRV_TASK_ID,
                            sizeof(struct scanu_start_req) + 
                            bl_hw->priv_scan.ie_len);
        if (!req)
            return -ENOMEM;
        
        printk("%s, vif_index:%d, vif_type:%d\r\n", 
              __func__, bl_vif->vif_index, BL_VIF_TYPE(bl_vif));

        /* Set parameters */
        req->vif_idx = bl_vif->vif_index;
        req->chan_cnt = bl_hw->priv_scan.chan_cnt;
        req->ssid_cnt = 1;
        req->ssid[0].length = 0;
        req->bssid = mac_addr_bcst;
        req->no_cck = 0;
        req->duration = 0;
        
        if (bl_hw->priv_scan.ie_len) {
            memcpy(req->add_ies_buf, bl_hw->priv_scan.ie_buf, 
                   bl_hw->priv_scan.ie_len);
            req->add_ie_len = bl_hw->priv_scan.ie_len;
        } else {
            req->add_ie_len = 0;
            req->add_ies = 0;
        }

        for (i = 0; i < req->chan_cnt; i++) {        
            req->chan[i].band = 0; // 2.4G is band 0
            req->chan[i].freq = channel_freq[bl_hw->priv_scan.chan_list[i] - 1];
            req->chan[i].flags = 0;
            req->chan[i].tx_power = 20;
        }

        return bl_send_msg(bl_hw, req, 1, SCANU_START_CFM, NULL);
    } else {
        printk("%s, stop scanu, vif_index:%d, vif_type:%d\r\n",
               __func__, bl_vif->vif_index, BL_VIF_TYPE(bl_vif));

        bl_hw->priv_scan.prob_req_en = false;
        memset(&bl_hw->priv_scan, 0, sizeof(struct bl_priv_scan));
    }
    
    return 0;
}

int bl_send_scanu_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                             struct cfg80211_scan_request *param)
{
    struct scanu_start_req *req;
    int i;
    uint8_t chan_flags = 0;
    uint16_t len = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    len = (param->ie == NULL) ? 0: param->ie_len;

#if 0
    if(bl_hw->priv_ies)
        len += bl_hw->priv_ies_len;
#endif

    printk("bl_send_scanu_req ==>start scan \n");
    
    /* Build the SCANU_START_REQ message */
    req = bl_msg_zalloc(SCANU_START_REQ, TASK_SCANU, DRV_TASK_ID,
                        sizeof(struct scanu_start_req) + len);
    if (!req)
        return -ENOMEM;

    BL_DBG_MSG("%s, vif_index:%d, vif_type:%d\r\n", 
               __func__, bl_vif->vif_index, BL_VIF_TYPE(bl_vif));
    
    /* Set parameters */
    req->vif_idx = bl_vif->vif_index;
    req->chan_cnt = (u8)min_t(int, SCAN_CHANNEL_MAX, param->n_channels);
    req->ssid_cnt = (u8)min_t(int, SCAN_SSID_MAX, param->n_ssids);
    req->bssid = mac_addr_bcst;
    req->no_cck = param->no_cck;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
    if (param->duration_mandatory)
        req->duration = ieee80211_tu_to_usec(param->duration);
#endif

    if (req->ssid_cnt == 0)
        chan_flags |= CHAN_NO_IR;
        
    for (i = 0; i < req->ssid_cnt; i++) {
        int j;
        
        for (j = 0; j < param->ssids[i].ssid_len; j++)
            req->ssid[i].array[j] = param->ssids[i].ssid[j];
        req->ssid[i].length = param->ssids[i].ssid_len;
    }

    if (param->ie) {
#if 0
        if (bl_ipc_elem_var_allocs(bl_hw, &bl_hw->scan_ie,
                                     param->ie_len, DMA_TO_DEVICE,
                                     NULL, param->ie, NULL))
            goto error;

        req->add_ie_len = param->ie_len;
        req->add_ies = bl_hw->scan_ie.dma_addr;
#else
        memcpy(req->add_ies_buf, param->ie, param->ie_len);
        req->add_ie_len = param->ie_len;
#endif
    } else {
        req->add_ie_len = 0;
        req->add_ies = 0;
    }

#if 0
    if(bl_hw->priv_ies) {
        printk("priv ies, %02x\n", bl_hw->priv_ies_buf[0]);
        printk("priv ies len, %d, param->ie_len=%d\n", bl_hw->priv_ies_len, param->ie_len);
        memcpy(req->add_ies_buf+param->ie_len, bl_hw->priv_ies_buf, bl_hw->priv_ies_len);
        req->add_ie_len += bl_hw->priv_ies_len;
        printk("priv ies, %02x\n", req->add_ies_buf[param->ie_len]);
    }
#endif

    for (i = 0; i < req->chan_cnt; i++) {
        struct ieee80211_channel *chan = param->channels[i];

        req->chan[i].band = chan->band;
        req->chan[i].freq = chan->center_freq;
        req->chan[i].flags = chan_flags | get_chan_flags(chan->flags);
        req->chan[i].tx_power = chan_to_fw_pwr(chan->max_reg_power);
        
        BL_DBG("band=%d, freq=%d, flags=0x%x,0x%x,0x%x, tx_power=%d\n", 
               req->chan[i].band,req->chan[i].freq,
               req->chan[i].flags, chan_flags, get_chan_flags(chan->flags),
               chan->max_reg_power);
    }

    BL_DBG("req no_cck=%d dura=%d", req->no_cck, req->duration);
    
    /* Send the SCANU_START_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, SCANU_START_CFM, NULL);
}

int bl_send_apm_start_cac_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                        struct cfg80211_chan_def *chandef,
                                        struct apm_start_cac_cfm *cfm)
{
    struct apm_start_cac_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the APM_START_CAC_REQ message */
    req = bl_msg_zalloc(APM_START_CAC_REQ, TASK_APM, DRV_TASK_ID,
                        sizeof(struct apm_start_cac_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the APM_START_CAC_REQ message */
    req->vif_idx = vif->vif_index;
    cfg80211_to_bl_chan(chandef, &req->chan);

    /* Send the APM_START_CAC_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, APM_START_CAC_CFM, cfm);
}

int bl_send_apm_stop_cac_req(struct bl_hw *bl_hw, struct bl_vif *vif)
{
    struct apm_stop_cac_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the APM_STOP_CAC_REQ message */
    req = bl_msg_zalloc(APM_STOP_CAC_REQ, TASK_APM, DRV_TASK_ID,
                        sizeof(struct apm_stop_cac_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the APM_STOP_CAC_REQ message */
    req->vif_idx = vif->vif_index;

    /* Send the APM_STOP_CAC_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, APM_STOP_CAC_CFM, NULL);
}

#ifdef CONFIG_MESH
int bl_send_mesh_start_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                    const struct mesh_config *conf, 
                                    const struct mesh_setup *setup,
                                    struct mesh_start_cfm *cfm)
{
    // Message to send
    struct mesh_start_req *req;
    // Supported basic rates
    struct ieee80211_supported_band *band_2GHz = 
                                   bl_hw->wiphy->bands[NL80211_BAND_2GHZ];
    /* Counter */
    int i;
    /* Return status */
    int status;
    /* DMA Address to be unmapped after confirmation reception */
    u32 dma_addr = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MESH_START_REQ message */
    req = bl_msg_zalloc(MESH_START_REQ, TASK_MESH, DRV_TASK_ID,
                        sizeof(struct mesh_start_req));
    if (!req) {
        return -ENOMEM;
    }

    req->vif_index = vif->vif_index;
    req->bcn_int = setup->beacon_interval;
    req->dtim_period = setup->dtim_period;
    req->mesh_id_len = setup->mesh_id_len;

    for (i = 0; i < setup->mesh_id_len; i++) {
        req->mesh_id[i] = *(setup->mesh_id + i);
    }

    req->user_mpm = setup->user_mpm;
    req->is_auth = setup->is_authenticated;
    req->auth_id = setup->auth_id;
    req->ie_len = setup->ie_len;

    if (setup->ie_len) {
        /*
         * Need to provide a Virtual Address to the MAC so that it can download the
         * additional information elements.
         */
        req->ie_addr = dma_map_single(bl_hw->dev, (void *)setup->ie,
                                      setup->ie_len, DMA_FROM_DEVICE);

        /* Check DMA mapping result */
        if (dma_mapping_error(bl_hw->dev, req->ie_addr)) {
            printk(KERN_CRIT "%s - DMA Mapping error on additional IEs\n", __func__);

            /* Consider there is no Additional IEs */
            req->ie_len = 0;
        } else {
            /* Store DMA Address so that we can unmap the memory section once MESH_START_CFM is received */
            dma_addr = req->ie_addr;
        }
    }

    /* Provide rate information */
    req->basic_rates.length = 0;
    for (i = 0; i < band_2GHz->n_bitrates; i++) {
        u16 rate = band_2GHz->bitrates[i].bitrate;

        /* Read value is in in units of 100 Kbps, provided value is in units
         * of 1Mbps, and multiplied by 2 so that 5.5 becomes 11 */
        rate = (rate << 1) / 10;

        if (setup->basic_rates & CO_BIT(i)) {
            rate |= 0x80;
        }

        req->basic_rates.array[i] = (u8)rate;
        req->basic_rates.length++;
    }

    /* Provide channel information */
    cfg80211_to_bl_chan(&setup->chandef, &req->chan);

    /* Send the MESH_START_REQ message to UMAC FW */
    status = bl_send_msg(bl_hw, req, 1, MESH_START_CFM, cfm);

    /* Unmap DMA area */
    if (setup->ie_len) {
        dma_unmap_single(bl_hw->dev, dma_addr, setup->ie_len, DMA_TO_DEVICE);
    }

    /* Return the status */
    return (status);
}

int bl_send_mesh_stop_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                    struct mesh_stop_cfm *cfm)
{
    // Message to send
    struct mesh_stop_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MESH_STOP_REQ message */
    req = bl_msg_zalloc(MESH_STOP_REQ, TASK_MESH, DRV_TASK_ID,
                        sizeof(struct mesh_stop_req));
    if (!req) {
        return -ENOMEM;
    }

    req->vif_idx = vif->vif_index;

    /* Send the MESH_STOP_REQ message to UMAC FW */
    return bl_send_msg(bl_hw, req, 1, MESH_STOP_CFM, cfm);
}

int bl_send_mesh_update_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                      u32 mask, const struct mesh_config *p_mconf,
                                      struct mesh_update_cfm *cfm)
{
    // Message to send
    struct mesh_update_req *req;
    // Keep only bit for fields which can be updated
    u32 supp_mask = (mask << 1) & (CO_BIT(NL80211_MESHCONF_GATE_ANNOUNCEMENTS)
                                   | CO_BIT(NL80211_MESHCONF_HWMP_ROOTMODE)
                                   | CO_BIT(NL80211_MESHCONF_FORWARDING)
                                   | CO_BIT(NL80211_MESHCONF_POWER_MODE));

    BL_DBG(BL_FN_ENTRY_STR);

    if (!supp_mask) {
        return -ENOENT;
    }

    /* Build the MESH_UPDATE_REQ message */
    req = bl_msg_zalloc(MESH_UPDATE_REQ, TASK_MESH, DRV_TASK_ID,
                        sizeof(struct mesh_update_req));

    if (!req) {
        return -ENOMEM;
    }

    req->vif_idx = vif->vif_index;

    if (supp_mask & CO_BIT(NL80211_MESHCONF_GATE_ANNOUNCEMENTS))
    {
        req->flags |= CO_BIT(MESH_UPDATE_FLAGS_GATE_MODE_BIT);
        req->gate_announ = p_mconf->dot11MeshGateAnnouncementProtocol;
    }

    if (supp_mask & CO_BIT(NL80211_MESHCONF_HWMP_ROOTMODE))
    {
        req->flags |= CO_BIT(MESH_UPDATE_FLAGS_ROOT_MODE_BIT);
        req->root_mode = p_mconf->dot11MeshHWMPRootMode;
    }

    if (supp_mask & CO_BIT(NL80211_MESHCONF_FORWARDING))
    {
        req->flags |= CO_BIT(MESH_UPDATE_FLAGS_MESH_FWD_BIT);
        req->mesh_forward = p_mconf->dot11MeshForwarding;
    }

    if (supp_mask & CO_BIT(NL80211_MESHCONF_POWER_MODE))
    {
        req->flags |= CO_BIT(MESH_UPDATE_FLAGS_LOCAL_PSM_BIT);
        req->local_ps_mode = p_mconf->power_mode;
    }

    /* Send the MESH_UPDATE_REQ message to UMAC FW */
    return bl_send_msg(bl_hw, req, 1, MESH_UPDATE_CFM, cfm);
}

int bl_send_mesh_peer_info_req(struct bl_hw *bl_hw, struct bl_vif *vif,
                                     u8 sta_idx, struct mesh_peer_info_cfm *cfm)
{
    // Message to send
    struct mesh_peer_info_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MESH_PEER_INFO_REQ message */
    req = bl_msg_zalloc(MESH_PEER_INFO_REQ, TASK_MESH, DRV_TASK_ID,
                        sizeof(struct mesh_peer_info_req));
    if (!req) {
        return -ENOMEM;
    }

    req->sta_idx = sta_idx;

    /* Send the MESH_PEER_INFO_REQ message to UMAC FW */
    return bl_send_msg(bl_hw, req, 1, MESH_PEER_INFO_CFM, cfm);
}

void bl_send_mesh_peer_update_ntf(struct bl_hw *bl_hw, struct bl_vif *vif,
                                              u8 sta_idx, u8 mlink_state)
{
    // Message to send
    struct mesh_peer_update_ntf *ntf;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MESH_PEER_UPDATE_NTF message */
    ntf = bl_msg_zalloc(MESH_PEER_UPDATE_NTF, TASK_MESH, DRV_TASK_ID,
                        sizeof(struct mesh_peer_update_ntf));

    if (ntf) {
        ntf->vif_idx = vif->vif_index;
        ntf->sta_idx = sta_idx;
        ntf->state = mlink_state;

        /* Send the MESH_PEER_INFO_REQ message to UMAC FW */
        bl_send_msg(bl_hw, ntf, 0, 0, NULL);
    }
}

void bl_send_mesh_path_create_req(struct bl_hw *bl_hw, 
                                              struct bl_vif *vif, u8 *tgt_addr)
{
    struct mesh_path_create_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Check if we are already waiting for a confirmation */
    if (vif->ap.flags & BL_AP_CREATE_MESH_PATH)
        return;

    /* Build the MESH_PATH_CREATE_REQ message */
    req = bl_msg_zalloc(MESH_PATH_CREATE_REQ, TASK_MESH, DRV_TASK_ID,
                        sizeof(struct mesh_path_create_req));
    if (!req)
        return;

    req->vif_idx = vif->vif_index;
    memcpy(&req->tgt_mac_addr, tgt_addr, ETH_ALEN);

    vif->ap.flags |= BL_AP_CREATE_MESH_PATH;

    /* Send the MESH_PATH_CREATE_REQ message to UMAC FW */
    bl_send_msg(bl_hw, req, 0, MESH_PATH_CREATE_CFM, NULL);
}

int bl_send_mesh_path_update_req(struct bl_hw *bl_hw, 
                                       struct bl_vif *vif, const u8 *tgt_addr,
                                       const u8 *p_nhop_addr, 
                                       struct mesh_path_update_cfm *cfm)
{
    // Message to send
    struct mesh_path_update_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MESH_PATH_UPDATE_REQ message */
    req = bl_msg_zalloc(MESH_PATH_UPDATE_REQ, TASK_MESH, DRV_TASK_ID,
                        sizeof(struct mesh_path_update_req));
    if (!req) {
        return -ENOMEM;
    }

    req->delete = (p_nhop_addr == NULL);
    req->vif_idx = vif->vif_index;
    memcpy(&req->tgt_mac_addr, tgt_addr, ETH_ALEN);

    if (p_nhop_addr) {
        memcpy(&req->nhop_mac_addr, p_nhop_addr, ETH_ALEN);
    }

    /* Send the MESH_PATH_UPDATE_REQ message to UMAC FW */
    return bl_send_msg(bl_hw, req, 1, MESH_PATH_UPDATE_CFM, cfm);
}

void bl_send_mesh_proxy_add_req(struct bl_hw *bl_hw, 
                                            struct bl_vif *vif, u8 *ext_addr)
{
    // Message to send
    struct mesh_proxy_add_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MESH_PROXY_ADD_REQ message */
    req = bl_msg_zalloc(MESH_PROXY_ADD_REQ, TASK_MESH, DRV_TASK_ID,
                        sizeof(struct mesh_proxy_add_req));

    if (req) {
        req->vif_idx = vif->vif_index;
        memcpy(&req->ext_sta_addr, ext_addr, ETH_ALEN);

        /* Send the MESH_PROXY_ADD_REQ message to UMAC FW */
        bl_send_msg(bl_hw, req, 0, 0, NULL);
    }
}
#endif

int bl_send_tdls_peer_traffic_ind_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif)
{
    struct tdls_peer_traffic_ind_req *tdls_peer_traffic_ind_req;

    if (!bl_vif->sta.tdls_sta)
        return -ENOLINK;

    /* Build the TDLS_PEER_TRAFFIC_IND_REQ message */
    tdls_peer_traffic_ind_req = 
                bl_msg_zalloc(TDLS_PEER_TRAFFIC_IND_REQ, TASK_TDLS, DRV_TASK_ID,
                              sizeof(struct tdls_peer_traffic_ind_req));

    if (!tdls_peer_traffic_ind_req)
        return -ENOMEM;

    /* Set parameters for the TDLS_PEER_TRAFFIC_IND_REQ message */
    tdls_peer_traffic_ind_req->vif_index = bl_vif->vif_index;
    tdls_peer_traffic_ind_req->sta_idx = bl_vif->sta.tdls_sta->sta_idx;
    memcpy(&(tdls_peer_traffic_ind_req->peer_mac_addr.array[0]),
           bl_vif->sta.tdls_sta->mac_addr, ETH_ALEN);
    tdls_peer_traffic_ind_req->dialog_token = 0; // check dialog token value
    tdls_peer_traffic_ind_req->last_tid = bl_vif->sta.tdls_sta->tdls.last_tid;
    tdls_peer_traffic_ind_req->last_sn = bl_vif->sta.tdls_sta->tdls.last_sn;

    /* Send the TDLS_PEER_TRAFFIC_IND_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, tdls_peer_traffic_ind_req, 0, 
                       TDLS_PEER_TRAFFIC_IND_CFM, NULL);
}

int bl_send_config_monitor_req(struct bl_hw *bl_hw,
                                        struct cfg80211_chan_def *chandef,
                                        struct me_config_monitor_cfm *cfm)
{
    struct me_config_monitor_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the ME_CONFIG_MONITOR_REQ message */
    req = bl_msg_zalloc(ME_CONFIG_MONITOR_REQ, TASK_ME, DRV_TASK_ID,
                        sizeof(struct me_config_monitor_req));
    if (!req)
        return -ENOMEM;

    if (chandef) {
        req->chan_set = true;
        cfg80211_to_bl_chan(chandef, &req->chan);

        if (bl_hw->phy.limit_bw)
            limit_chan_bw(&req->chan.type, req->chan.prim20_freq, 
                          &req->chan.center1_freq);
    } else {
         req->chan_set = false;
    }

    req->uf = bl_hw->mod_params->uf;

    /* Send the ME_CONFIG_MONITOR_REQ message to FW */
    return bl_send_msg(bl_hw, req, 1, ME_CONFIG_MONITOR_CFM, cfm);
}
#endif /* CONFIG_BL_FULLMAC */

int bl_send_tdls_chan_switch_req(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                                   struct bl_sta *bl_sta, bool sta_initiator,
                                   u8 oper_class, struct cfg80211_chan_def *chandef,
                                   struct tdls_chan_switch_cfm *cfm)
{
    struct tdls_chan_switch_req *tdls_chan_switch_req;

    /* Build the TDLS_CHAN_SWITCH_REQ message */
    tdls_chan_switch_req =
                bl_msg_zalloc(TDLS_CHAN_SWITCH_REQ, TASK_TDLS, DRV_TASK_ID,
                              sizeof(struct tdls_chan_switch_req));

    if (!tdls_chan_switch_req)
        return -ENOMEM;

    /* Set parameters for the TDLS_CHAN_SWITCH_REQ message */
    tdls_chan_switch_req->vif_index = bl_vif->vif_index;
    tdls_chan_switch_req->sta_idx = bl_sta->sta_idx;
    memcpy(&(tdls_chan_switch_req->peer_mac_addr.array[0]),
           bl_sta_addr(bl_sta), ETH_ALEN);
    tdls_chan_switch_req->initiator = sta_initiator;
    cfg80211_to_bl_chan(chandef, &tdls_chan_switch_req->chan);
    tdls_chan_switch_req->op_class = oper_class;

    /* Send the TDLS_CHAN_SWITCH_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, tdls_chan_switch_req, 1, TDLS_CHAN_SWITCH_CFM, cfm);
}

int bl_send_tdls_cancel_chan_switch_req(struct bl_hw *bl_hw,
                                          struct bl_vif *bl_vif,
                                          struct bl_sta *bl_sta,
                                          struct tdls_cancel_chan_switch_cfm *cfm)
{
    struct tdls_cancel_chan_switch_req *tdls_cancel_chan_switch_req;

    /* Build the TDLS_CHAN_SWITCH_REQ message */
    tdls_cancel_chan_switch_req = 
              bl_msg_zalloc(TDLS_CANCEL_CHAN_SWITCH_REQ, TASK_TDLS, DRV_TASK_ID,
                            sizeof(struct tdls_cancel_chan_switch_req));

    if (!tdls_cancel_chan_switch_req)
        return -ENOMEM;

    /* Set parameters for the TDLS_CHAN_SWITCH_REQ message */
    tdls_cancel_chan_switch_req->vif_index = bl_vif->vif_index;
    tdls_cancel_chan_switch_req->sta_idx = bl_sta->sta_idx;
    memcpy(&(tdls_cancel_chan_switch_req->peer_mac_addr.array[0]),
           bl_sta_addr(bl_sta), ETH_ALEN);

    /* Send the TDLS_CHAN_SWITCH_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, tdls_cancel_chan_switch_req, 1, 
                       TDLS_CANCEL_CHAN_SWITCH_CFM, cfm);
}

#ifdef CONFIG_BL_BFMER
void bl_send_bfmer_enable(struct bl_hw *bl_hw, struct bl_sta *bl_sta,
                                   const struct ieee80211_vht_cap *vht_cap)
{
    struct mm_bfmer_enable_req *bfmer_en_req;
    __le32 vht_capability;
    u8 rx_nss = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    if (!vht_cap) {
        goto end;
    }

    vht_capability = vht_cap->vht_cap_info;

    if (!(vht_capability & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE)) {
        goto end;
    }

#ifdef CONFIG_BL_FULLMAC
    rx_nss = bl_bfmer_get_rx_nss(vht_cap);
#endif /* CONFIG_BL_FULLMAC */

    /* Allocate a structure that will contain the beamforming report */
    if (bl_bfmer_report_add(bl_hw, bl_sta, BL_BFMER_REPORT_SPACE_SIZE))
    {
        goto end;
    }

    /* Build the MM_BFMER_ENABLE_REQ message */
    bfmer_en_req = bl_msg_zalloc(MM_BFMER_ENABLE_REQ, TASK_MM, DRV_TASK_ID,
                                 sizeof(struct mm_bfmer_enable_req));

    /* Check message allocation */
    if (!bfmer_en_req) {
        /* Free memory allocated for the report */
        bl_bfmer_report_del(bl_hw, bl_sta);

        /* Do not use beamforming */
        goto end;
    }

    /* Provide DMA address to the MAC */
    bfmer_en_req->host_bfr_addr = bl_sta->bfm_report->dma_addr;
    bfmer_en_req->host_bfr_size = BL_BFMER_REPORT_SPACE_SIZE;
    bfmer_en_req->sta_idx = bl_sta->sta_idx;
    bfmer_en_req->aid = bl_sta->aid;
    bfmer_en_req->rx_nss = rx_nss;

    if (vht_capability & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) {
        bfmer_en_req->vht_mu_bfmee = true;
    } else {
        bfmer_en_req->vht_mu_bfmee = false;
    }

    /* Send the MM_BFMER_EN_REQ message to LMAC FW */
    bl_send_msg(bl_hw, bfmer_en_req, 0, 0, NULL);

end:
    return;
}

#ifdef CONFIG_BL_MUMIMO_TX
int bl_send_mu_group_update_req(struct bl_hw *bl_hw, struct bl_sta *bl_sta)
{
    struct mm_mu_group_update_req *req;
    int group_id, i = 0;
    u64 map;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_MU_GROUP_UPDATE_REQ message */
    req = bl_msg_zalloc(MM_MU_GROUP_UPDATE_REQ, TASK_MM, DRV_TASK_ID,
                        sizeof(struct mm_mu_group_update_req) +
                        bl_sta->group_info.cnt * sizeof(req->groups[0]));

    /* Check message allocation */
    if (!req)
        return -ENOMEM;

    /* Go through the groups the STA belongs to */
    group_sta_for_each(bl_sta, group_id, map) {
        int user_pos = bl_mu_group_sta_get_pos(bl_hw, bl_sta, group_id);

        if (WARN((i >= bl_sta->group_info.cnt),
                 "STA%d: Too much group (%d)\n",
                 bl_sta->sta_idx, i + 1))
            break;

        req->groups[i].group_id = group_id;
        req->groups[i].user_pos = user_pos;

        i++;
    }

    req->group_cnt = bl_sta->group_info.cnt;
    req->sta_idx = bl_sta->sta_idx;

    /* Send the MM_MU_GROUP_UPDATE_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, MM_MU_GROUP_UPDATE_CFM, NULL);
}
#endif /* CONFIG_BL_MUMIMO_TX */
#endif /* CONFIG_BL_BFMER */

#ifdef CONFIG_BL_DEBUGFS
/**********************************************************************
 *    Debug Messages
 *********************************************************************/
int bl_send_dbg_trigger_req(struct bl_hw *bl_hw, char *msg)
{
    struct mm_dbg_trigger_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_DBG_TRIGGER_REQ message */
    req = bl_msg_zalloc(MM_DBG_TRIGGER_REQ, TASK_MM, DRV_TASK_ID,
                        sizeof(struct mm_dbg_trigger_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_DBG_TRIGGER_REQ message */
    strncpy(req->error, msg, sizeof(req->error));

    /* Send the MM_DBG_TRIGGER_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 0, 0, NULL);
}
#endif

int bl_send_dbg_mem_read_req(struct bl_hw *bl_hw, u32 mem_addr,
                                         u32 rd_len, struct dbg_mem_read_cfm *cfm)
{
    struct dbg_mem_read_req *mem_read_req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the DBG_MEM_READ_REQ message */
    mem_read_req = bl_msg_zalloc(DBG_MEM_READ_REQ, TASK_DBG, DRV_TASK_ID,
                                 sizeof(struct dbg_mem_read_req));
    if (!mem_read_req)
        return -ENOMEM;

    /* Set parameters for the DBG_MEM_READ_REQ message */
    mem_read_req->memaddr = mem_addr;
    mem_read_req->len = rd_len;

    /* Send the DBG_MEM_READ_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, mem_read_req, 1, DBG_MEM_READ_CFM, cfm);
}

int bl_send_dbg_mem_write_req(struct bl_hw *bl_hw, u32 mem_addr,
                                          u32 mem_data)
{
    struct dbg_mem_write_req *mem_write_req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the DBG_MEM_WRITE_REQ message */
    mem_write_req = bl_msg_zalloc(DBG_MEM_WRITE_REQ, TASK_DBG, DRV_TASK_ID,
                                  sizeof(struct dbg_mem_write_req));
    if (!mem_write_req)
        return -ENOMEM;

    /* Set parameters for the DBG_MEM_WRITE_REQ message */
    mem_write_req->memaddr = mem_addr;
    mem_write_req->memdata = mem_data;

    /* Send the DBG_MEM_WRITE_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, mem_write_req, 1, DBG_MEM_WRITE_CFM, NULL);
}

int bl_send_dbg_ke_stat_req(struct bl_hw *bl_hw, u32 clear_on_read,
                                     u32 period_rpt, u32 period_print)
{
    struct dbg_ke_stat_req *ke_stat_req;
    //struct dbg_ke_stat_cfm dbg_ke_stat_cfm = {0};
    int ret = 0;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the DBG_MEM_WRITE_REQ message */
    ke_stat_req = bl_msg_zalloc(DBG_KE_STAT_REQ, TASK_DBG, DRV_TASK_ID,
                                sizeof(struct dbg_ke_stat_req));
    if (!ke_stat_req)
        return -ENOMEM;

    ke_stat_req->clear_on_read = clear_on_read;
    ke_stat_req->period_rpt = period_rpt;
    ke_stat_req->period_print = period_print;

    /* Send the DBG_MEM_WRITE_REQ message to LMAC FW */
    ret = bl_send_msg(bl_hw, ke_stat_req, 1, DBG_KE_STAT_CFM, NULL);
    if (ret == 0) {
        
    } else {
        printk("%s, failed, %d\n", __func__, ret);
    }

    return 0;
}

#ifdef CONFIG_BL_DEBUGFS
int bl_send_dbg_set_mod_filter_req(struct bl_hw *bl_hw, u32 filter)
{
    struct dbg_set_mod_filter_req *set_mod_filter_req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the DBG_SET_MOD_FILTER_REQ message */
    set_mod_filter_req =
        bl_msg_zalloc(DBG_SET_MOD_FILTER_REQ, TASK_DBG, DRV_TASK_ID,
                      sizeof(struct dbg_set_mod_filter_req));
    if (!set_mod_filter_req)
        return -ENOMEM;

    /* Set parameters for the DBG_SET_MOD_FILTER_REQ message */
    set_mod_filter_req->mod_filter = filter;

    /* Send the DBG_SET_MOD_FILTER_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, set_mod_filter_req, 1, DBG_SET_MOD_FILTER_CFM, NULL);
}

int bl_send_dbg_set_sev_filter_req(struct bl_hw *bl_hw, u32 filter)
{
    struct dbg_set_sev_filter_req *set_sev_filter_req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the DBG_SET_SEV_FILTER_REQ message */
    set_sev_filter_req =
        bl_msg_zalloc(DBG_SET_SEV_FILTER_REQ, TASK_DBG, DRV_TASK_ID,
                      sizeof(struct dbg_set_sev_filter_req));
    if (!set_sev_filter_req)
        return -ENOMEM;

    /* Set parameters for the DBG_SET_SEV_FILTER_REQ message */
    set_sev_filter_req->sev_filter = filter;

    /* Send the DBG_SET_SEV_FILTER_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, set_sev_filter_req, 1, DBG_SET_SEV_FILTER_CFM, NULL);
}

int bl_send_dbg_get_sys_stat_req(struct bl_hw *bl_hw,
                                            struct dbg_get_sys_stat_cfm *cfm)
{
    void *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Allocate the message */
    req = bl_msg_zalloc(DBG_GET_SYS_STAT_REQ, TASK_DBG, DRV_TASK_ID, 0);
    if (!req)
        return -ENOMEM;

    /* Send the DBG_MEM_READ_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, DBG_GET_SYS_STAT_CFM, cfm);
}
#endif

int bl_send_cfg_rssi_req(struct bl_hw *bl_hw, u8 vif_index,
                                int rssi_thold, u32 rssi_hyst)
{
    struct mm_cfg_rssi_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Build the MM_CFG_RSSI_REQ message */
    req = bl_msg_zalloc(MM_CFG_RSSI_REQ, TASK_MM, DRV_TASK_ID,
                        sizeof(struct mm_cfg_rssi_req));
    if (!req)
        return -ENOMEM;

    /* Set parameters for the MM_CFG_RSSI_REQ message */
    req->vif_index = vif_index;
    req->rssi_thold = (s8)rssi_thold;
    req->rssi_hyst = (u8)rssi_hyst;

    /* Send the MM_CFG_RSSI_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 0, 0, NULL);
}

int bl_send_temp_read_req(struct bl_hw *bl_hw, int32_t * temp)
{
    void *void_param;

    if (! temp)
        return -1;

    BL_DBG(BL_FN_ENTRY_STR);

    void_param = bl_msg_zalloc(MM_TEMP_READ_REQ, TASK_MM, DRV_TASK_ID, 0);
    if (!void_param)
        return -ENOMEM;

    return bl_send_msg(bl_hw, void_param, 1, MM_TEMP_READ_CFM, temp);
}

int bl_send_rssi_read_req(struct bl_hw *bl_hw, u8 vif_index, 
                                 struct mm_get_rssi_cfm *cfm)
{
    struct mm_get_rssi_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    req = bl_msg_zalloc(MM_GET_RSSI_REQ, TASK_MM, DRV_TASK_ID,
                        sizeof(struct mm_get_rssi_req));
    if (!req)
        return -ENOMEM;

    req->inst_nbr = vif_index;

    return bl_send_msg(bl_hw, req, 1, MM_GET_RSSI_CFM, cfm);
}

// read xtal/power offset from efuse
int bl_send_efuse_read_req(struct bl_hw *bl_hw, 
                                    struct mm_read_efuse_cfm *cfm)
{
    void *req;

    BL_DBG(BL_FN_ENTRY_STR);

    req = bl_msg_zalloc(MM_READ_EFUSE_REQ, TASK_MM, DRV_TASK_ID, 0);
    if (!req)
        return -ENOMEM;

    return bl_send_msg(bl_hw, req, 1, MM_READ_EFUSE_CFM, cfm);
}

#if defined(BL_BUS_LOOPBACK)
int bl_send_dbg_lbk_req(struct bl_hw *bl_hw, u32 exp_data_rate, 
                                u32 pkt_size, struct dbg_lbk_cfm *cfm)
{
    struct dbg_lbk_req *req;

    BL_DBG(BL_FN_ENTRY_STR);

    /* Allocate the message */
    req = bl_msg_zalloc(DBG_LBK_REQ, TASK_DBG, DRV_TASK_ID,
                        sizeof(struct dbg_lbk_req));
    if (!req)
        return -ENOMEM;

    //if (bl_hw->cmd_mgr.state == BL_CMD_MGR_STATE_CRASHED) {
    //    bl_hw->cmd_mgr.state = BL_CMD_MGR_STATE_INITED;
    //    bl_hw->ipc_env->msga2e_hostid = NULL;
    //}

    req->exp_data_rate = exp_data_rate;
    req->pkt_size = pkt_size;
    
    /* Send the DBG_LBK_REQ message to LMAC FW */
    return bl_send_msg(bl_hw, req, 1, DBG_LBK_CFM, cfm);
}
#endif

