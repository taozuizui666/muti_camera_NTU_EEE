#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/skbuff.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "bl_defs.h"
#include "bl_irqs.h"
#include "bl_compat.h"
#include "bl_nl_events.h"

//#define CONFIG_BTBLE_DBG

#ifdef CONFIG_BTBLE_DBG
#define BTBLE_DBG printk
#else
#define BTBLE_DBG(a...)     do {} while (0)
#endif

#define BTSDIO_MSG_LEN_FIELD 4

extern void bl_nl_broadcast_event(struct bl_hw *bl_hw, u32 event_id,
                                         u8* payload, u16 len);
                                         
int btsdio_rx_packet(struct btsdio_data *data, struct sk_buff *skb, uint32_t len)
{
    uint8_t *hdr = NULL;
    int err;
    int pi = 0;
    u8 dbg_buf[700] = {0};
    u32 dbg_len = 0;
    
    BTBLE_DBG("%s, %s, pld_len:%d, %d\n", __func__, data->hdev->name, skb->len, len);

    while (pi < skb->len) {
        dbg_len += sprintf(dbg_buf+dbg_len, "%02x", skb->data[pi]);
        BTBLE_DBG("0x%02x\n", skb->data[pi]);
        pi++;
    }
    dbg_buf[dbg_len] = '\0';
    BTBLE_DBG("bt recv:%s\n", dbg_buf);
    
    if (skb->len < 3 || skb->len > HCI_MAX_FRAME_SIZE) {
        dev_kfree_skb_any(skb);
        return -EILSEQ;
    }

    if (data->bl_hw && data->bl_hw->bl_hci_on) {
        bl_nl_broadcast_event(data->bl_hw, BL_EVENT_ID_HCI_MSG, 
                              (u8_l *)skb->data, skb->len);
        
        dev_kfree_skb_any(skb);
        
        return 0;
    }

    data->hdev->stat.byte_rx += len;

    hdr = (uint8_t *)skb->data;
    switch (hdr[0]) {
        case HCI_EVENT_PKT:
        case HCI_ACLDATA_PKT:
        case HCI_SCODATA_PKT:
        //case HCI_ISODATA_PKT:
            hci_skb_pkt_type(skb) = hdr[0];
            skb_pull(skb, 1);
            
            BTBLE_DBG("pkt type:%d, pulled 1 skb len:%u\n", hdr[0], skb->len);
            
            err = hci_recv_frame(data->hdev, skb);
            if (err < 0)
                return err;
            break;
            
        default:
            printk("%s, not find type: %d\n", __func__, hdr[0]);
            
            dev_kfree_skb_any(skb);
            return -EINVAL;
    }

    return 0;
}

static int btsdio_open(struct hci_dev *hdev)
{
    //struct btsdio_data *data = hci_get_drvdata(hdev);
    int err = 0;

    BTBLE_DBG("%s, %s", __func__, hdev->name);

    return err;
}

static int btsdio_close(struct hci_dev *hdev)
{
    //struct btsdio_data *data = hci_get_drvdata(hdev);

    BTBLE_DBG("%s, %s", __func__, hdev->name);

    return 0;
}

static int btsdio_flush(struct hci_dev *hdev)
{
    struct btsdio_data *data = hci_get_drvdata(hdev);

    BTBLE_DBG("%s, %s", __func__, hdev->name);

    skb_queue_purge(&data->txq);

    return 0;
}

int btsdio_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
    struct btsdio_data *data = hci_get_drvdata(hdev);
    uint32_t pkt_len = skb->len + 1;
    uint32_t reserve_len = sizeof(struct inf_hdr) + BTSDIO_MSG_LEN_FIELD + 1;
    struct sk_buff *new_skb = NULL;
    struct sk_buff *old_skb = NULL;
    #ifdef CONFIG_BTBLE_DBG
    u8 dbg_buf[700] = {0};
    u32 dbg_len = 0;
    int i;
    #endif

    BTBLE_DBG("%s, %s, type:%d, len:%d\r\n", __func__, 
              hdev->name, hci_skb_pkt_type(skb), skb->len);

    #ifdef CONFIG_BTBLE_DBG
    for (i=0; i<skb->len; i++) {
        dbg_len += sprintf(dbg_buf+dbg_len, "%02x", skb->data[i]);
        //BTBLE_DBG("0x%02x ", skb->data[i]);
    }
    dbg_buf[dbg_len] = '\0';
    BTBLE_DBG("bt send:%s\n", dbg_buf);
    #endif

    //reserve space in the front of skb->data: 8 byte sdu_hdr, 1 byte hci_type, 4 byte len
    if (skb->data-reserve_len < skb->head) {
        new_skb = skb_copy_expand(skb, reserve_len, reserve_len, GFP_KERNEL);
        
        if (!new_skb) {
            printk("%s, fail to alloc skb to clone skb without enough buf to skb_push\r\n",
                   __func__);
            
            return -ENOMEM;
        }
        
        old_skb = skb;
        skb = new_skb;
    }
    
    skb_push(skb, BTSDIO_MSG_LEN_FIELD + 1);
    memcpy(skb->data, &pkt_len, BTSDIO_MSG_LEN_FIELD);
    skb->data[BTSDIO_MSG_LEN_FIELD] = hci_skb_pkt_type(skb);

    switch (hci_skb_pkt_type(skb)) {
        case HCI_COMMAND_PKT:
            hdev->stat.cmd_tx++;
            break;

        case HCI_ACLDATA_PKT:
            hdev->stat.acl_tx++;
            break;

        case HCI_SCODATA_PKT:
            hdev->stat.sco_tx++;
            break;

        default:
            if (new_skb) {
                kfree_skb(new_skb);
            }
            
            return -EILSEQ;
    }

    BTBLE_DBG("btsdio txq len:%d, cmd_tx:%d, acl_tx:%d, sco_tx:%d\n", 
           skb_queue_len(&data->txq), hdev->stat.cmd_tx, 
           hdev->stat.acl_tx, hdev->stat.sco_tx);

    spin_lock(&data->hci_cmd_lock);
    skb_queue_tail(&data->txq, skb);
    spin_unlock(&data->hci_cmd_lock);
    
    bl_queue_main_work(data->bl_hw);

    if (new_skb) {
        kfree_skb(old_skb);
    }

    return 0;
}

int btsdio_probe(struct sdio_func *func, const struct sdio_device_id *id,
                     struct bl_hw *bl_hw)
{
    struct btsdio_data *data = &bl_hw->btsdio_data;
    struct hci_dev *hdev;
    struct sdio_func_tuple *tuple = func->tuples;
    int err;

    BTBLE_DBG("%s, func %p class 0x%04x", __func__, func, func->class);

    while (tuple) {
        BTBLE_DBG("code 0x%x size %d", tuple->code, tuple->size);
        tuple = tuple->next;
    }

    data->func = func;

    skb_queue_head_init(&data->txq);

    hdev = hci_alloc_dev();
    if (!hdev)
        return -ENOMEM;

    hdev->bus = HCI_SDIO;
    hci_set_drvdata(hdev, data);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 8, 0)
    //if (id->class == SDIO_CLASS_BT_AMP)
    //    hdev->dev_type = HCI_AMP;
    //else
        hdev->dev_type = HCI_PRIMARY;
#endif

    data->hdev = hdev;
    spin_lock_init(&data->hci_cmd_lock);

    SET_HCIDEV_DEV(hdev, &func->dev);

    hdev->open     = btsdio_open;
    hdev->close    = btsdio_close;
    hdev->flush    = btsdio_flush;
    hdev->send     = btsdio_send_frame;

    err = hci_register_dev(hdev);
    if (err < 0) {
        hci_free_dev(hdev);
        return err;
    }

    bl_hw->hdev = hdev;
    data->bl_hw = (void *)bl_hw;
#if 0
    sdio_set_drvdata(func, data);
#endif

    return 0;
}

void btsdio_remove(struct sdio_func *func, struct bl_hw *bl_hw)
{
    //struct btsdio_data *data = sdio_get_drvdata(func);
    struct btsdio_data *data = &bl_hw->btsdio_data;
    struct hci_dev *hdev;

    BTBLE_DBG("%s, func %p", __func__, func);

    if (!data || data->func==NULL || data->bl_hw==NULL)
        return;

    hdev = data->hdev;
    bl_hw->hdev = NULL;

    //sdio_set_drvdata(func, NULL);

    hci_unregister_dev(hdev);

    hci_free_dev(hdev);
}

