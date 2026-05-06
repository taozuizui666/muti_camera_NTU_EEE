#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include <linux/firmware.h>
#if defined(CONFIG_OF)
#include <linux/of_irq.h>
#else
#include <linux/irq.h>
#endif
#include <linux/suspend.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "bl_defs.h"
#include "bl_utils.h"
#include "bl_ipc.h"
#include "bl_version.h"
#include "bl_compat.h"
#include "bl_nl_events.h"
#include "bl_msg_rx.h"

#define BTUSB_1_BULK_EP

#define TRC_MSG_TYPE 6

#define BTUSB_HEADER_LEN 8

#define BTUSB_IGNORE            0x01
#define BTUSB_SNIFFER           0x08
#define BTUSB_BROKEN_ISOC       0x20
#define BTUSB_WRONG_SCO_MTU     0x40
#define BTUSB_BCM_PATCHRAM      0x400
#define BTUSB_SWAVE             0x1000
#define BTUSB_AMP               0x4000
#define BTUSB_BCM_APPLE         0x10000
#define BTUSB_IFNUM_2           0x80000
#define BTUSB_WIDEBAND_SPEECH   0x400000
#define BTUSB_VALID_LE_STATES   0x800000

static struct usb_driver btusb_driver;

extern const struct usb_device_id bl_btusb_table[];

#define BTUSB_MAX_ISOC_FRAMES   10

#define BTUSB_INTR_RUNNING      0
#define BTUSB_BULK_RUNNING      1
#define BTUSB_ISOC_RUNNING      2
#define BTUSB_SUSPENDING        3
#define BTUSB_DID_ISO_RESUME    4
#define BTUSB_BOOTLOADER        5
#define BTUSB_DOWNLOADING       6
#define BTUSB_FIRMWARE_LOADED   7
#define BTUSB_FIRMWARE_FAILED   8
#define BTUSB_BOOTING           9
#define BTUSB_DIAG_RUNNING      10
#define BTUSB_OOB_WAKE_ENABLED  11
#define BTUSB_HW_RESET_ACTIVE   12
#define BTUSB_TX_WAIT_VND_EVT   13
#define BTUSB_WAKEUP_DISABLE    14
#define BTUSB_USE_ALT1_FOR_WBS  15

const struct usb_device_id bl_btusb_table[] = {
    {USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_BFL, USB_DEVICE_ID_BFL_616_F, BLE_INTERFACE_CLASS, BLE_INTERFACE_SUBCLASS, BLE_INTERFACE_PRTO)},
    {USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_BFL, USB_DEVICE_ID_BFL_618_F, BLE_INTERFACE_CLASS, BLE_INTERFACE_SUBCLASS, BLE_INTERFACE_PRTO)},

    { }    /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, bl_btusb_table);

//#define CONFIG_BTBLE_DBG

#ifdef CONFIG_BTBLE_DBG
#define BTBLE_DBG printk
#else
#define BTBLE_DBG(a...)     do {} while (0)
#endif

#if 0
#define bt_dev_info(hdev, fmt, ...)     \
    BTBLE_DBG("%s: " fmt, (hdev)->name, ##__VA_ARGS__)
#define bt_dev_dbg(hdev, fmt, ...)      \
    BTBLE_DBG("%s: " fmt, (hdev)->name, ##__VA_ARGS__)
#endif

extern int of_irq_get_byname(struct device_node *dev, const char *name);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0)
static inline void *skb_put_data(struct sk_buff *skb, const void *data,
                                      unsigned int len)
{
    void *tmp = skb_put(skb, len);

    memcpy(tmp, data, len);

    return tmp;
}
#endif

struct btusb_data {
    struct hci_dev       *hdev;
    struct usb_device    *udev;
    struct usb_interface *intf;
    struct usb_interface *isoc;
    struct usb_interface *diag;
    unsigned isoc_ifnum;

    unsigned long flags;

    struct work_struct work;
    struct work_struct waker;

    struct usb_anchor deferred;
    struct usb_anchor tx_anchor;
    int tx_in_flight;
    spinlock_t txlock;

    struct usb_anchor intr_anchor;
    struct usb_anchor bulk_anchor;
    struct usb_anchor isoc_anchor;
    struct usb_anchor diag_anchor;
    struct usb_anchor ctrl_anchor;
    spinlock_t rxlock;

    struct sk_buff *evt_skb;
    struct sk_buff *acl_skb;
    struct sk_buff *sco_skb;

    struct usb_endpoint_descriptor *intr_ep;
    struct usb_endpoint_descriptor *bulk_tx_ep;
    struct usb_endpoint_descriptor *bulk_rx_ep;
    struct usb_endpoint_descriptor *isoc_tx_ep;
    struct usb_endpoint_descriptor *isoc_rx_ep;
    struct usb_endpoint_descriptor *diag_tx_ep;
    struct usb_endpoint_descriptor *diag_rx_ep;

    __u8 cmdreq_type;
    __u8 cmdreq;

    unsigned int sco_num;
    unsigned int air_mode;
    bool usb_alt6_packet_flow;
    int isoc_altsetting;
    int suspend_count;

    int (*setup_on_usb)(struct hci_dev *hdev);

    int oob_wake_irq;   /* irq for out-of-band wake-on-bt */
    unsigned cmd_timeout_cnt;
};

static int btusb_submit_bulk_urb(struct hci_dev *hdev, gfp_t mem_flags);
static void btusb_bulk_complete(struct urb *urb);

bool btusb_get_ble_hci(struct usb_device *udev, struct hci_dev *hdev,
                             struct bl_hw **bl_hw)
{
#if defined(BL_MULTI_HWS) && defined(CONFIG_BL_USB)
    int i = 0;

    *bl_hw = NULL;

    for (; i< BL_HWS_MAX_NUM; i++) {
        if (bl_hws[i] != NULL) {
            struct usb_device *udev_this = 
                 interface_to_usbdev((struct usb_interface *)bl_hws[i]->plat->dev);

            BTBLE_DBG("%s, bl_hw's udev:0x%p, udev:0x%p\n", __func__, udev_this, udev);

            if (udev_this == udev) {
                bl_hws[i]->hdev = hdev;
                break;
            }
        }
    }

    if (i >= BL_HWS_MAX_NUM) {
        printk("%s, not found bl_hw for udev:0x%p, hdev:0x%p\n",
               __func__, udev, hdev);

        return false;
    } else {
        *bl_hw = bl_hws[i];

        return bl_hws[i]->bl_hci_on;
    }
#else
    *bl_hw = NULL;

    return false;
#endif
}

static inline void btusb_free_frags(struct btusb_data *data)
{
    unsigned long flags;

    spin_lock_irqsave(&data->rxlock, flags);

    kfree_skb(data->evt_skb);
    data->evt_skb = NULL;

    kfree_skb(data->acl_skb);
    data->acl_skb = NULL;

    kfree_skb(data->sco_skb);
    data->sco_skb = NULL;

    spin_unlock_irqrestore(&data->rxlock, flags);
}

static int btusb_recv_intr(struct btusb_data *data, void *buffer, int count)
{
    struct sk_buff *skb;
    unsigned long flags;
    int err = 0;
    #ifdef CONFIG_BTBLE_DBG
    int i = 0;
    #endif
    
    BTBLE_DBG("%s\r\n", __func__);

    #ifdef CONFIG_BTBLE_DBG
    for (i=0; i<count; i++) {
        printk("0x%02x ", ((uint8_t *)buffer)[i]);
    }
    #endif
    
    spin_lock_irqsave(&data->rxlock, flags);
    skb = data->evt_skb;

    while (count) {
        int len;

        if (!skb) {
            skb = bt_skb_alloc(HCI_MAX_EVENT_SIZE, GFP_ATOMIC);
            if (!skb) {
                err = -ENOMEM;
                break;
            }

            hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
            hci_skb_expect(skb) = HCI_EVENT_HDR_SIZE;
            
            BTBLE_DBG("%s, expect hdr size=%d, skb:0x%x, count:%d\r\n", 
                      __func__, hci_skb_expect(skb), skb, count);
        }

        len = min_t(uint, hci_skb_expect(skb), count);
        
        BTBLE_DBG("%s, expect size=%d, skb:0x%x, count:%d\r\n", 
                  __func__, hci_skb_expect(skb), skb, count);
                  
        skb_put_data(skb, buffer, len);

        count -= len;
        buffer += len;
        hci_skb_expect(skb) -= len;

        if (skb->len == HCI_EVENT_HDR_SIZE) {
            /* Complete event header */
            hci_skb_expect(skb) = hci_event_hdr(skb)->plen;
            
            BTBLE_DBG("%s, expect pld size=%d, skb:0x%x\r\n", 
                      __func__, hci_skb_expect(skb), skb);

            if (skb_tailroom(skb) < hci_skb_expect(skb)) {
                BTBLE_DBG("%s, skb:0x%x, tail:%d, expect:%d\r\n", 
                         __func__, skb, skb_tailroom(skb), hci_skb_expect(skb));
                
                kfree_skb(skb);
                skb = NULL;

                err = -EILSEQ;
                break;
            }
        }

        if (!hci_skb_expect(skb)) {
            /* Complete frame */
            hci_recv_frame(data->hdev, skb);
            skb = NULL;
        }
    }

    data->evt_skb = skb;
    spin_unlock_irqrestore(&data->rxlock, flags);

    return err;
}

static int btusb_recv_bulk(struct btusb_data *data, void *buffer, int count)
{
    struct sk_buff *skb;
    unsigned long flags;
    int err = 0;

    BTBLE_DBG("%s\r\n", __func__);

    spin_lock_irqsave(&data->rxlock, flags);
    skb = data->acl_skb;

    while (count) {
        int len;

        if (!skb) {
            skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
            if (!skb) {
                err = -ENOMEM;
                break;
            }

            hci_skb_pkt_type(skb) = HCI_ACLDATA_PKT;
            hci_skb_expect(skb) = HCI_ACL_HDR_SIZE;
        }

        len = min_t(uint, hci_skb_expect(skb), count);
        skb_put_data(skb, buffer, len);

        count -= len;
        buffer += len;
        hci_skb_expect(skb) -= len;

        if (skb->len == HCI_ACL_HDR_SIZE) {
            __le16 dlen = hci_acl_hdr(skb)->dlen;

            /* Complete ACL header */
            hci_skb_expect(skb) = __le16_to_cpu(dlen);

            if (skb_tailroom(skb) < hci_skb_expect(skb)) {
                kfree_skb(skb);
                skb = NULL;

                err = -EILSEQ;
                break;
            }
        }

        if (!hci_skb_expect(skb)) {
            /* Complete frame */
            hci_recv_frame(data->hdev, skb);
            skb = NULL;
        }
    }

    data->acl_skb = skb;
    spin_unlock_irqrestore(&data->rxlock, flags);

    return err;
}

static int btusb_recv_isoc(struct btusb_data *data, void *buffer, int count)
{
    struct sk_buff *skb;
    unsigned long flags;
    int err = 0;

    BTBLE_DBG("%s\r\n", __func__);

    spin_lock_irqsave(&data->rxlock, flags);
    skb = data->sco_skb;

    while (count) {
        int len;

        if (!skb) {
            skb = bt_skb_alloc(HCI_MAX_SCO_SIZE, GFP_ATOMIC);
            if (!skb) {
                err = -ENOMEM;
                break;
            }

            hci_skb_pkt_type(skb) = HCI_SCODATA_PKT;
            hci_skb_expect(skb) = HCI_SCO_HDR_SIZE;
        }

        len = min_t(uint, hci_skb_expect(skb), count);
        skb_put_data(skb, buffer, len);

        count -= len;
        buffer += len;
        hci_skb_expect(skb) -= len;

        if (skb->len == HCI_SCO_HDR_SIZE) {
            /* Complete SCO header */
            hci_skb_expect(skb) = hci_sco_hdr(skb)->dlen;

            if (skb_tailroom(skb) < hci_skb_expect(skb)) {
                kfree_skb(skb);
                skb = NULL;

                err = -EILSEQ;
                break;
            }
        }

        if (!hci_skb_expect(skb)) {
            /* Complete frame */
            hci_recv_frame(data->hdev, skb);
            skb = NULL;
        }
    }

    data->sco_skb = skb;
    spin_unlock_irqrestore(&data->rxlock, flags);

    return err;
}

#ifndef BTUSB_1_BULK_EP
static void btusb_intr_complete(struct urb *urb)
{
    struct hci_dev *hdev = urb->context;
    struct btusb_data *data = hci_get_drvdata(hdev);
    int err;

    BTBLE_DBG("%s, %s urb %p status %d count %d", 
              __func__, hdev->name, urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        return;

    if (urb->status == 0 && urb->actual_length > 0 &&
        urb->actual_length <= HCI_MAX_FRAME_SIZE) 
    {
        hdev->stat.byte_rx += urb->actual_length;

        if (btusb_recv_intr(data, urb->transfer_buffer, urb->actual_length) < 0) 
        {
            bt_dev_err(hdev, "corrupted event packet");
            hdev->stat.err_rx++;
        }
    } else if (urb->status == -ENOENT) {
        /* Avoid suspend failed when usb_kill_urb */
        return;
    }

    if (!test_bit(BTUSB_INTR_RUNNING, &data->flags))
        return;

    usb_mark_last_busy(data->udev);
    usb_anchor_urb(urb, &data->intr_anchor);

    err = usb_submit_urb(urb, GFP_ATOMIC);
    if (err < 0) {
        /* -EPERM: urb is being killed;
         * -ENODEV: device got disconnected
         */
        if (err != -EPERM && err != -ENODEV)
            bt_dev_err(hdev, "urb %p failed to resubmit (%d)", urb, -err);
            
        usb_unanchor_urb(urb);
    }
}
#endif

static int btusb_submit_intr_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
    #ifdef BTUSB_1_BULK_EP
    return btusb_submit_bulk_urb(hdev, mem_flags);
    #else
    struct btusb_data *data = hci_get_drvdata(hdev);
    struct urb *urb;
    unsigned char *buf;
    unsigned int pipe;
    int err, size;

    BTBLE_DBG("%s, %s", __func__, hdev->name);

    if (!data->intr_ep)
        return -ENODEV;

    urb = usb_alloc_urb(0, mem_flags);
    if (!urb)
        return -ENOMEM;

    size = le16_to_cpu(data->intr_ep->wMaxPacketSize);

    buf = kmalloc(size, mem_flags);
    if (!buf) {
        usb_free_urb(urb);
        return -ENOMEM;
    }

    pipe = usb_rcvintpipe(data->udev, data->intr_ep->bEndpointAddress);

    usb_fill_int_urb(urb, data->udev, pipe, buf, size,
                     btusb_intr_complete, hdev, data->intr_ep->bInterval);

    urb->transfer_flags |= URB_FREE_BUFFER;

    usb_anchor_urb(urb, &data->intr_anchor);

    err = usb_submit_urb(urb, mem_flags);
    if (err < 0) {
        if (err != -EPERM && err != -ENODEV)
            bt_dev_err(hdev, "urb %p submission failed (%d)", urb, -err);
                   
        usb_unanchor_urb(urb);
    }

    usb_free_urb(urb);

    return err;
    #endif
}

static void btusb_bulk_complete(struct urb *urb)
{
    struct hci_dev *hdev = urb->context;
    struct btusb_data *data = hci_get_drvdata(hdev);
    int err;
    #ifdef BTUSB_1_BULK_EP
    u8 pkt_type;
    #endif
    #ifdef CONFIG_BTBLE_DBG
    int i = 0;
    u8 dbg_buf[700] = {0};
    u32 dbg_len = 0;
    #endif
    
    BTBLE_DBG("%s, %s urb %p rx status %d count %d", 
              __func__, hdev->name, urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags)){
        usb_free_urb(urb);

        return;
    }

    do {
        if (urb->status == 0 && urb->actual_length >= 3 && 
            urb->actual_length <= HCI_MAX_FRAME_SIZE) 
        {
            struct bl_hw *bl_hw = NULL;
            
            #ifdef CONFIG_BTBLE_DBG
            for (i=0; i<urb->actual_length; i++) {
                dbg_len += sprintf(dbg_buf+dbg_len, "%02x", ((u8 *)urb->transfer_buffer)[i]);
                BTBLE_DBG("%s, 0x%x\r\n", __func__, ((u8 *)urb->transfer_buffer)[i]);
            }
            dbg_buf[dbg_len] = '\0';
            BTBLE_DBG("bt recv:%s\n", dbg_buf);
            #endif

            if (btusb_get_ble_hci(data->udev, hdev, &bl_hw)) {
                bl_nl_broadcast_event(bl_hw, BL_EVENT_ID_HCI_MSG, 
                                      (u8_l *)urb->transfer_buffer, 
                                      urb->actual_length);
                
                break;
            }
            
            hdev->stat.byte_rx += urb->actual_length;

            #ifdef BTUSB_1_BULK_EP
            pkt_type = *(u8 *)(urb->transfer_buffer);

            if (pkt_type == HCI_COMMAND_PKT || pkt_type == HCI_ACLDATA_PKT) {
                BTBLE_DBG("%s, call btusb_recv_bulk\r\n", __func__);
                
                if (btusb_recv_bulk(data, urb->transfer_buffer+1,
                                    urb->actual_length-1) < 0) 
                {
                    bt_dev_err(hdev, "corrupted ACL packet");
                    hdev->stat.err_rx++;
                }
            } else if (pkt_type == HCI_EVENT_PKT) {
                BTBLE_DBG("%s, call btusb_recv_intr\r\n", __func__);
                
                if (btusb_recv_intr(data, urb->transfer_buffer+1,
                                    urb->actual_length-1) < 0) 
                {
                    bt_dev_err(hdev, "corrupted event packet");
                    hdev->stat.err_rx++;
                }
            } else if (pkt_type == HCI_SCODATA_PKT) {
                BTBLE_DBG("%s, call btusb_recv_isoc\r\n", __func__);
                
                if (btusb_recv_isoc(data, urb->transfer_buffer+1,
                                    urb->actual_length-1) < 0) 
                {
                    bt_dev_err(hdev, "corrupted SCO packet");
                    
                    hdev->stat.err_rx++;
                }
            } else {
                BTBLE_DBG("%s, unknow type:%d\r\n", __func__, pkt_type);
            }
            #else
            if (btusb_recv_bulk(data, urb->transfer_buffer, urb->actual_length) < 0) 
            {
                bt_dev_err(hdev, "corrupted ACL packet");
                
                hdev->stat.err_rx++;
            }
            #endif
        } else if (urb->status == -ENOENT) {
            /* Avoid suspend failed when usb_kill_urb */
            return;
        } else {
            printk("%s, urb->status:%d, urb->actual_length:%d\n", __func__, 
                   urb->status, urb->actual_length);
        }
    } while (0);
    
    if (!test_bit(BTUSB_BULK_RUNNING, &data->flags)) {
        usb_free_urb(urb);

        return;
    }

    usb_anchor_urb(urb, &data->bulk_anchor);
    usb_mark_last_busy(data->udev);

    err = usb_submit_urb(urb, GFP_ATOMIC);
    if (err < 0) {
        /* -EPERM: urb is being killed;
         * -ENODEV: device got disconnected
         */
        if (err != -EPERM && err != -ENODEV)
            bt_dev_err(hdev, "urb %p failed to resubmit (%d)", urb, -err);
            
        usb_unanchor_urb(urb);
    }
}

static int btusb_submit_bulk_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    struct urb *urb;
    unsigned char *buf;
    unsigned int pipe;
    int err, size = HCI_MAX_FRAME_SIZE;

    BTBLE_DBG("%s, %s", __func__, hdev->name);

    if (!data->bulk_rx_ep)
        return -ENODEV;

    urb = usb_alloc_urb(0, mem_flags);
    if (!urb)
        return -ENOMEM;

    buf = kmalloc(size, mem_flags);
    if (!buf) {
        usb_free_urb(urb);
        return -ENOMEM;
    }

    pipe = usb_rcvbulkpipe(data->udev, data->bulk_rx_ep->bEndpointAddress);

    usb_fill_bulk_urb(urb, data->udev, pipe, buf, size,
                      btusb_bulk_complete, hdev);

    urb->transfer_flags |= URB_FREE_BUFFER;

    usb_mark_last_busy(data->udev);
    usb_anchor_urb(urb, &data->bulk_anchor);

    err = usb_submit_urb(urb, mem_flags);
    if (err < 0) {
        if (err != -EPERM && err != -ENODEV)
            bt_dev_err(hdev, "urb %p submission failed (%d)", urb, -err);
            
        usb_unanchor_urb(urb);
    }

    usb_free_urb(urb);

    return err;
}

#ifndef BTUSB_1_BULK_EP
static void btusb_isoc_complete(struct urb *urb)
{
    struct hci_dev *hdev = urb->context;
    struct btusb_data *data = hci_get_drvdata(hdev);
    int i, err;

    BTBLE_DBG("%s, %s urb %p status %d count %d", 
              __func__, hdev->name, urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        return;

    if (urb->status == 0) {
        for (i = 0; i < urb->number_of_packets; i++) {
            unsigned int offset = urb->iso_frame_desc[i].offset;
            unsigned int length = urb->iso_frame_desc[i].actual_length;

            if (urb->iso_frame_desc[i].status)
                continue;

            hdev->stat.byte_rx += length;

            if (btusb_recv_isoc(data, urb->transfer_buffer + offset,
                                length) < 0) 
            {
                bt_dev_err(hdev, "corrupted SCO packet");
                
                hdev->stat.err_rx++;
            }
        }
    } else if (urb->status == -ENOENT) {
        /* Avoid suspend failed when usb_kill_urb */
        return;
    }

    if (!test_bit(BTUSB_ISOC_RUNNING, &data->flags))
        return;

    usb_anchor_urb(urb, &data->isoc_anchor);

    err = usb_submit_urb(urb, GFP_ATOMIC);
    if (err < 0) {
        /* -EPERM: urb is being killed;
         * -ENODEV: device got disconnected
         */
        if (err != -EPERM && err != -ENODEV)
            bt_dev_err(hdev, "urb %p failed to resubmit (%d)",
                   urb, -err);
        usb_unanchor_urb(urb);
    }
}

static inline void __fill_isoc_descriptor_msbc(struct urb *urb, int len,
                                              int mtu, struct btusb_data *data)
{
    int i, offset = 0;
    unsigned int interval;

    BTBLE_DBG("%s, len %d mtu %d", __func__, len, mtu);

    /* For mSBC ALT 6 setting the host will send the packet at continuous
     * flow. As per core spec 5, vol 4, part B, table 2.1. For ALT setting
     * 6 the HCI PACKET INTERVAL should be 7.5ms for every usb packets.
     * To maintain the rate we send 63bytes of usb packets alternatively for
     * 7ms and 8ms to maintain the rate as 7.5ms.
     */
    if (data->usb_alt6_packet_flow) {
        interval = 7;
        data->usb_alt6_packet_flow = false;
    } else {
        interval = 6;
        data->usb_alt6_packet_flow = true;
    }

    for (i = 0; i < interval; i++) {
        urb->iso_frame_desc[i].offset = offset;
        urb->iso_frame_desc[i].length = offset;
    }

    if (len && i < BTUSB_MAX_ISOC_FRAMES) {
        urb->iso_frame_desc[i].offset = offset;
        urb->iso_frame_desc[i].length = len;
        i++;
    }

    urb->number_of_packets = i;
}

static inline void __fill_isoc_descriptor(struct urb *urb, int len, int mtu)
{
    int i, offset = 0;

    BTBLE_DBG("%s, len %d mtu %d", __func__, len, mtu);

    for (i = 0; i < BTUSB_MAX_ISOC_FRAMES && len >= mtu;
                    i++, offset += mtu, len -= mtu) {
        urb->iso_frame_desc[i].offset = offset;
        urb->iso_frame_desc[i].length = mtu;
    }

    if (len && i < BTUSB_MAX_ISOC_FRAMES) {
        urb->iso_frame_desc[i].offset = offset;
        urb->iso_frame_desc[i].length = len;
        i++;
    }

    urb->number_of_packets = i;
}
#endif

static int btusb_submit_isoc_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
#ifdef BTUSB_1_BULK_EP
    return btusb_submit_bulk_urb(hdev, mem_flags);
#else
    struct btusb_data *data = hci_get_drvdata(hdev);
    struct urb *urb;
    unsigned char *buf;
    unsigned int pipe;
    int err, size;

    BTBLE_DBG("%s, %s", __func__, hdev->name);

    if (!data->isoc_rx_ep)
        return -ENODEV;

    urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, mem_flags);
    if (!urb)
        return -ENOMEM;

    size = le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize) *
                        BTUSB_MAX_ISOC_FRAMES;

    buf = kmalloc(size, mem_flags);
    if (!buf) {
        usb_free_urb(urb);
        return -ENOMEM;
    }

    pipe = usb_rcvisocpipe(data->udev, data->isoc_rx_ep->bEndpointAddress);

    usb_fill_int_urb(urb, data->udev, pipe, buf, size, btusb_isoc_complete,
                     hdev, data->isoc_rx_ep->bInterval);

    urb->transfer_flags = URB_FREE_BUFFER | URB_ISO_ASAP;

    __fill_isoc_descriptor(urb, size,
                           le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize));

    usb_anchor_urb(urb, &data->isoc_anchor);

    err = usb_submit_urb(urb, mem_flags);
    if (err < 0) {
        if (err != -EPERM && err != -ENODEV)
            bt_dev_err(hdev, "urb %p submission failed (%d)", urb, -err);
            
        usb_unanchor_urb(urb);
    }

    usb_free_urb(urb);

    return err;
#endif
}

static void btusb_tx_complete(struct urb *urb)
{
    struct sk_buff *skb = urb->context;
    struct hci_dev *hdev = (struct hci_dev *)skb->dev;
    struct btusb_data *data = hci_get_drvdata(hdev);
    unsigned long flags;

    BTBLE_DBG("%s, %s urb %p status %d count %d", 
             __func__, hdev->name, urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        goto done;

    if (!urb->status)
        hdev->stat.byte_tx += urb->transfer_buffer_length;
    else
        hdev->stat.err_tx++;

done:
    spin_lock_irqsave(&data->txlock, flags);
    data->tx_in_flight--;
    spin_unlock_irqrestore(&data->txlock, flags);

    kfree(urb->setup_packet);

    kfree_skb(skb);
}

#ifndef BTUSB_1_BULK_EP
static void btusb_isoc_tx_complete(struct urb *urb)
{
    struct sk_buff *skb = urb->context;
    struct hci_dev *hdev = (struct hci_dev *)skb->dev;

    BTBLE_DBG("%s, %s urb %p status %d count %d", 
              __func__, hdev->name, urb, urb->status, urb->actual_length);

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        goto done;

    if (!urb->status)
        hdev->stat.byte_tx += urb->transfer_buffer_length;
    else
        hdev->stat.err_tx++;

done:
    kfree(urb->setup_packet);

    kfree_skb(skb);
}
#endif

static int btusb_open(struct hci_dev *hdev)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    int err;

    BTBLE_DBG("%s, %s", __func__, hdev->name);

    // err = usb_autopm_get_interface(data->intf);
    // printk("%s, usb_autopm_get_interface err:%d, 0x%x\r\n", __func__, err, data->setup_on_usb);
    // if (err < 0)
    //     return err;

    /* Patching USB firmware files prior to starting any URBs of HCI path
     * It is more safe to use USB bulk channel for downloading USB patch
     */
    if (data->setup_on_usb) {
        err = data->setup_on_usb(hdev);
        if (err < 0)
            goto setup_fail;
    }

    data->intf->needs_remote_wakeup = 1;

    /* Disable device remote wakeup when host is suspended
     * For Realtek chips, global suspend without
     * SET_FEATURE (DEVICE_REMOTE_WAKEUP) can save more power in device.
     */
    if (test_bit(BTUSB_WAKEUP_DISABLE, &data->flags))
        device_wakeup_disable(&data->udev->dev);

    if (test_and_set_bit(BTUSB_INTR_RUNNING, &data->flags))
        goto done;

    err = btusb_submit_intr_urb(hdev, GFP_KERNEL);
    //printk("%s, sumbit intr urb err:%d\r\n", __func__, err);
    if (err < 0)
        goto failed;

    err = btusb_submit_bulk_urb(hdev, GFP_KERNEL);
    //printk("%s, sumbit bulk urb err:%d\r\n", __func__, err);
    if (err < 0) {
        usb_kill_anchored_urbs(&data->intr_anchor);
        goto failed;
    }

    set_bit(BTUSB_BULK_RUNNING, &data->flags);
    btusb_submit_bulk_urb(hdev, GFP_KERNEL);

done:
    // usb_autopm_put_interface(data->intf);
    return 0;

failed:
    clear_bit(BTUSB_INTR_RUNNING, &data->flags);
    
setup_fail:
    //usb_autopm_put_interface(data->intf);
    return err;
}

static void btusb_stop_traffic(struct btusb_data *data)
{
    usb_kill_anchored_urbs(&data->intr_anchor);
    usb_kill_anchored_urbs(&data->bulk_anchor);
    usb_kill_anchored_urbs(&data->isoc_anchor);
    usb_kill_anchored_urbs(&data->ctrl_anchor);
}

static int btusb_close(struct hci_dev *hdev)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    //int err;

    BTBLE_DBG("%s, %s", __func__, hdev->name);

    cancel_work_sync(&data->work);
    cancel_work_sync(&data->waker);

    clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
    clear_bit(BTUSB_BULK_RUNNING, &data->flags);
    clear_bit(BTUSB_INTR_RUNNING, &data->flags);

    btusb_stop_traffic(data);
    btusb_free_frags(data);

    // err = usb_autopm_get_interface(data->intf);
    // if (err < 0)
    //     goto failed;

    data->intf->needs_remote_wakeup = 0;

    /* Enable remote wake up for auto-suspend */
    if (test_bit(BTUSB_WAKEUP_DISABLE, &data->flags))
        data->intf->needs_remote_wakeup = 1;

    // usb_autopm_put_interface(data->intf);

//failed:
    usb_scuttle_anchored_urbs(&data->deferred);
    return 0;
}

static int btusb_flush(struct hci_dev *hdev)
{
    struct btusb_data *data = hci_get_drvdata(hdev);

    BTBLE_DBG("%s, %s", __func__, hdev->name);

    usb_kill_anchored_urbs(&data->tx_anchor);
    btusb_free_frags(data);

    return 0;
}

#ifndef BTUSB_1_BULK_EP
static struct urb *alloc_ctrl_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    struct usb_ctrlrequest *dr;
    struct urb *urb;
    unsigned int pipe;

    urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!urb)
        return ERR_PTR(-ENOMEM);

    dr = kmalloc(sizeof(*dr), GFP_KERNEL);
    if (!dr) {
        usb_free_urb(urb);
        return ERR_PTR(-ENOMEM);
    }

    dr->bRequestType = data->cmdreq_type;
    dr->bRequest     = data->cmdreq;
    dr->wIndex       = 0;
    dr->wValue       = 0;
    dr->wLength      = __cpu_to_le16(skb->len);

    pipe = usb_sndctrlpipe(data->udev, 0x00);

    usb_fill_control_urb(urb, data->udev, pipe, (void *)dr,
                         skb->data, skb->len, btusb_tx_complete, skb);

    skb->dev = (void *)hdev;

    return urb;
}
#endif

static struct urb *alloc_bulk_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    struct urb *urb;
    unsigned int pipe;

    if (!data->bulk_tx_ep)
        return ERR_PTR(-ENODEV);

    BTBLE_DBG("%s, ep addr:0x%x\r\n", __func__, data->bulk_tx_ep->bEndpointAddress);

    urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!urb)
        return ERR_PTR(-ENOMEM);

    pipe = usb_sndbulkpipe(data->udev, data->bulk_tx_ep->bEndpointAddress);

    usb_fill_bulk_urb(urb, data->udev, pipe,
                      skb->data, skb->len, btusb_tx_complete, skb);

    skb->dev = (void *)hdev;

    BTBLE_DBG("%s done\r\n", __func__);

    return urb;
}

#ifndef BTUSB_1_BULK_EP
static struct urb *alloc_isoc_urb(struct hci_dev *hdev, struct sk_buff *skb)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    struct urb *urb;
    unsigned int pipe;

    if (!data->isoc_tx_ep)
        return ERR_PTR(-ENODEV);

    urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_KERNEL);
    if (!urb)
        return ERR_PTR(-ENOMEM);

    pipe = usb_sndisocpipe(data->udev, data->isoc_tx_ep->bEndpointAddress);

    usb_fill_int_urb(urb, data->udev, pipe,
                     skb->data, skb->len, btusb_isoc_tx_complete,
                     skb, data->isoc_tx_ep->bInterval);

    urb->transfer_flags  = URB_ISO_ASAP;

    if (data->isoc_altsetting == 6)
        __fill_isoc_descriptor_msbc(urb, skb->len,
                        le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize), data);
    else
        __fill_isoc_descriptor(urb, skb->len,
                        le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize));
                        
    skb->dev = (void *)hdev;

    return urb;
}
#endif

static int submit_tx_urb(struct hci_dev *hdev, struct urb *urb)
    {
    struct btusb_data *data = hci_get_drvdata(hdev);
    int err;

    //printk("%s\r\n", __func__);
    usb_anchor_urb(urb, &data->tx_anchor);

    err = usb_submit_urb(urb, GFP_KERNEL);
    if (err < 0) {
        if (err != -EPERM && err != -ENODEV)
            bt_dev_err(hdev, "urb %p submission failed (%d)",
                   urb, -err);
        kfree(urb->setup_packet);
        usb_unanchor_urb(urb);
    } else {
        usb_mark_last_busy(data->udev);
    }

    //printk("%s, ret:%d\r\n", __func__, err);

    usb_free_urb(urb);
    
    return err;
}

static int submit_or_queue_tx_urb(struct hci_dev *hdev, struct urb *urb)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    unsigned long flags;
    bool suspending;

    spin_lock_irqsave(&data->txlock, flags);
    suspending = test_bit(BTUSB_SUSPENDING, &data->flags);
    if (!suspending)
        data->tx_in_flight++;
        
    spin_unlock_irqrestore(&data->txlock, flags);

    //printk("%s, suspending:%d\r\n", __func__, suspending);

    if (!suspending)
        return submit_tx_urb(hdev, urb);

    usb_anchor_urb(urb, &data->deferred);
    schedule_work(&data->waker);

    usb_free_urb(urb);
    
    return 0;
}

int btusb_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
    struct urb *urb;
    int ret = 0;
#ifdef CONFIG_BTBLE_DBG
    int i=0;
    u8 dbg_buf[700] = {0};
    u32 dbg_len = 0;
#endif
    uint32_t pkt_len = skb->len + 1;
    struct sk_buff *new_skb = NULL;
    struct sk_buff *old_skb = NULL;

    BTBLE_DBG("%s, %s, pkt type:%d, skb->len:%d, pkt_len:%d\r\n",
              __func__, hdev->name, hci_skb_pkt_type(skb), skb->len, pkt_len);

#ifdef CONFIG_BTBLE_DBG
    for (i=0; i<skb->len; i++) {
        dbg_len += sprintf(dbg_buf+dbg_len, "%02x", skb->data[i]);
        BTBLE_DBG("0x%02x ", skb->data[i]);
    }
    dbg_buf[dbg_len] = '\0';
    BTBLE_DBG("bt send:%s\n", dbg_buf);
#endif

    if (skb->data-BTUSB_HEADER_LEN < skb->head) {
        new_skb = skb_copy_expand(skb, BTUSB_HEADER_LEN*2, BTUSB_HEADER_LEN*2, GFP_KERNEL);
        
        if (!new_skb) {
            printk("%s, fail to alloc skb to clone skb without enough buf to skb_push\r\n", 
                   __func__);
            return -ENOMEM;
        }
        
        old_skb = skb;
        skb = new_skb;
    }
    
    skb_push(skb, BTUSB_HEADER_LEN);
    memcpy(skb->data, &pkt_len, 4);
    skb->data[4] = 0;
    skb->data[5] = 0;
    skb->data[6] = 0;
    skb->data[7] = hci_skb_pkt_type(skb);

#ifdef CONFIG_BTBLE_DBG
    //printk("%s, new_skb:\r\n", __func__);
    //for (i=0; i<skb->len; i++) {
    //    printk("0x%02x ", skb->data[i]);
    //}
#endif

    switch (hci_skb_pkt_type(skb)) {
        case HCI_COMMAND_PKT:
            #ifdef BTUSB_1_BULK_EP
            urb = alloc_bulk_urb(hdev, skb);
            #else
            urb = alloc_ctrl_urb(hdev, skb);
            #endif
            
            hdev->stat.cmd_tx++;
            break;
            
        case HCI_ACLDATA_PKT:
            #ifdef BTUSB_1_BULK_EP
            urb = alloc_bulk_urb(hdev, skb);
            #else
            urb = alloc_bulk_urb(hdev, skb);
            #endif
            
            hdev->stat.acl_tx++;
            break;

        case HCI_SCODATA_PKT:
            #ifdef BTUSB_1_BULK_EP
            urb = alloc_bulk_urb(hdev, skb);
            #else
            if (hci_conn_num(hdev, SCO_LINK) < 1)
                return -ENODEV;

            urb = alloc_isoc_urb(hdev, skb);
            #endif
            
            hdev->stat.sco_tx++;
            break;

        default:
            if (new_skb) {
                kfree_skb(new_skb);
            }
            
            printk("%s not support pkt type:%d\r\n", 
                   __func__, hci_skb_pkt_type(skb));
            
            return -EILSEQ;
    }

    if (IS_ERR(urb)) {
        if (new_skb) {
            kfree_skb(new_skb);
        }
        
        return PTR_ERR(urb);
    }

    if ((ret = submit_or_queue_tx_urb(hdev, urb))) {
        if (new_skb) {
            kfree_skb(new_skb);
        }
        printk("%s, submit urb fail, ret=%d\r\n", __func__, ret);
        
        return ret;
    } else {
        if (new_skb) {
            kfree_skb(old_skb);
        }
        
        return 0;
    }
}

static void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
    struct btusb_data *data = hci_get_drvdata(hdev);

    BTBLE_DBG("%s, %s evt %d", __func__, hdev->name, evt);

    if (hci_conn_num(hdev, SCO_LINK) != data->sco_num) {
        data->sco_num = hci_conn_num(hdev, SCO_LINK);
        data->air_mode = evt;
        schedule_work(&data->work);
    }
}

static inline int __set_isoc_interface(struct hci_dev *hdev, int altsetting)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    struct usb_interface *intf = data->isoc;
    struct usb_endpoint_descriptor *ep_desc;
    int i, err;

    if (!data->isoc)
        return -ENODEV;

    err = usb_set_interface(data->udev, data->isoc_ifnum, altsetting);
    if (err < 0) {
        bt_dev_err(hdev, "setting interface failed (%d)", -err);
        return err;
    }

    data->isoc_altsetting = altsetting;
    data->isoc_tx_ep = NULL;
    data->isoc_rx_ep = NULL;

    for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
        ep_desc = &intf->cur_altsetting->endpoint[i].desc;

        if (!data->isoc_tx_ep && usb_endpoint_is_isoc_out(ep_desc)) {
            data->isoc_tx_ep = ep_desc;
            continue;
        }

        if (!data->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)) {
            data->isoc_rx_ep = ep_desc;
            continue;
        }
    }

    if (!data->isoc_tx_ep || !data->isoc_rx_ep) {
        bt_dev_err(hdev, "invalid SCO descriptors");
        
        return -ENODEV;
    }

    return 0;
}

static int btusb_switch_alt_setting(struct hci_dev *hdev, int new_alts)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    int err;

    if (data->isoc_altsetting != new_alts) {
        unsigned long flags;

        clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
        usb_kill_anchored_urbs(&data->isoc_anchor);

        /* When isochronous alternate setting needs to be
         * changed, because SCO connection has been added
         * or removed, a packet fragment may be left in the
         * reassembling state. This could lead to wrongly
         * assembled fragments.
         *
         * Clear outstanding fragment when selecting a new
         * alternate setting.
         */
        spin_lock_irqsave(&data->rxlock, flags);
        kfree_skb(data->sco_skb);
        data->sco_skb = NULL;
        spin_unlock_irqrestore(&data->rxlock, flags);

        err = __set_isoc_interface(hdev, new_alts);
        if (err < 0)
            return err;
    }

    if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
        if (btusb_submit_isoc_urb(hdev, GFP_KERNEL) < 0)
            clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
        else
            btusb_submit_isoc_urb(hdev, GFP_KERNEL);
    }

    return 0;
}

__attribute__((unused)) static struct usb_host_interface *btusb_find_altsetting(struct btusb_data *data,
                            int alt)
{
    struct usb_interface *intf = data->isoc;
    int i;

    BTBLE_DBG("Looking for Alt no :%d", alt);

    if (!intf)
        return NULL;

    for (i = 0; i < intf->num_altsetting; i++) {
        if (intf->altsetting[i].desc.bAlternateSetting == alt)
            return &intf->altsetting[i];
    }

    return NULL;
}

static void btusb_work(struct work_struct *work)
{
    struct btusb_data *data = container_of(work, struct btusb_data, work);
    struct hci_dev *hdev = data->hdev;
    int new_alts = 0;
    //int err;

    BTBLE_DBG("%s\r\n", __func__);

    if (data->sco_num > 0) {
        if (!test_bit(BTUSB_DID_ISO_RESUME, &data->flags)) {
            // err = usb_autopm_get_interface(data->isoc ? data->isoc : data->intf);
            // if (err < 0) {
            //     clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
            //     usb_kill_anchored_urbs(&data->isoc_anchor);
            //     return;
            // }

            set_bit(BTUSB_DID_ISO_RESUME, &data->flags);
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
        if (data->air_mode == HCI_NOTIFY_ENABLE_SCO_CVSD) {
            if (hdev->voice_setting & 0x0020) {
                static const int alts[3] = { 2, 4, 5 };

                new_alts = alts[data->sco_num - 1];
            } else {
                new_alts = data->sco_num;
            }
        } else if (data->air_mode == HCI_NOTIFY_ENABLE_SCO_TRANSP) {
            /* Check if Alt 6 is supported for Transparent audio */
            if (btusb_find_altsetting(data, 6)) {
                data->usb_alt6_packet_flow = true;
                new_alts = 6;
            } else if (test_bit(BTUSB_USE_ALT1_FOR_WBS, &data->flags)) {
                new_alts = 1;
            } else {
                bt_dev_err(hdev, "Device does not support ALT setting 6");
            }
        }
#endif

        if (btusb_switch_alt_setting(hdev, new_alts) < 0)
            bt_dev_err(hdev, "set USB alt:(%d) failed!", new_alts);
    } else {
        clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
        usb_kill_anchored_urbs(&data->isoc_anchor);

        __set_isoc_interface(hdev, 0);
        // if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &data->flags))
        //     usb_autopm_put_interface(data->isoc ? data->isoc : data->intf);
    }
}

static void btusb_waker(struct work_struct *work)
{
    //struct btusb_data *data = container_of(work, struct btusb_data, waker);
    //int err;

    // err = usb_autopm_get_interface(data->intf);
    // if (err < 0)
    //     return;

    // usb_autopm_put_interface(data->intf);
}

#if 0 //def CONFIG_PM
static irqreturn_t btusb_oob_wake_handler(int irq, void *priv)
{
    struct btusb_data *data = priv;

    BTBLE_DBG("%s\r\n", __func__);
    
    pm_wakeup_event(&data->udev->dev, 0);
    pm_system_wakeup();

    /* Disable only if not already disabled (keep it balanced) */
    if (test_and_clear_bit(BTUSB_OOB_WAKE_ENABLED, &data->flags)) {
        disable_irq_nosync(irq);
        disable_irq_wake(irq);
    }
    return IRQ_HANDLED;
}

/* Use an oob wakeup pin? */
__attribute__((unused)) static int btusb_config_oob_wake(struct hci_dev *hdev)
{
    struct btusb_data *data = hci_get_drvdata(hdev);
    struct device *dev = &data->udev->dev;
    int irq, ret;

    BTBLE_DBG("%s\r\n", __func__);
    
    clear_bit(BTUSB_OOB_WAKE_ENABLED, &data->flags);

    /* Move on if no IRQ specified */
    irq = of_irq_get_byname(dev->of_node, "wakeup");
    if (irq <= 0) {
        bt_dev_dbg(hdev, "%s: no OOB Wakeup IRQ in DT", __func__);
        return 0;
    }

    irq_set_status_flags(irq, IRQ_NOAUTOEN);
    ret = devm_request_irq(&hdev->dev, irq, btusb_oob_wake_handler,
                   0, "OOB Wake-on-BT", data);
    if (ret) {
        bt_dev_err(hdev, "%s: IRQ request failed", __func__);
        return ret;
    }

    ret = device_init_wakeup(dev, true);
    if (ret) {
        bt_dev_err(hdev, "%s: failed to init_wakeup", __func__);
        return ret;
    }

    data->oob_wake_irq = irq;
    bt_dev_info(hdev, "OOB Wake-on-BT configured at IRQ %u", irq);
    return 0;
}
#endif

__attribute__((unused)) static bool btusb_prevent_wake(struct hci_dev *hdev)
{
    struct btusb_data *data = hci_get_drvdata(hdev);

    BTBLE_DBG("%s\r\n", __func__);
    
    if (test_bit(BTUSB_WAKEUP_DISABLE, &data->flags))
        return true;

    return !device_may_wakeup(&data->udev->dev);
    }

static int btusb_probe(struct usb_interface *intf,
                            const struct usb_device_id *id)
{
    struct usb_endpoint_descriptor *ep_desc;
    struct btusb_data *data;
    struct hci_dev *hdev;
    unsigned ifnum_base;
    int i, err;
    struct usb_device *udev = interface_to_usbdev(intf);
    struct usb_host_interface *iface_desc = intf->cur_altsetting;
    u16 id_vendor, id_product, bcd_device;

    BTBLE_DBG("%s, intf %p id %p", __func__, intf, id);

    id_vendor = le16_to_cpu(udev->descriptor.idVendor);
    id_product = le16_to_cpu(udev->descriptor.idProduct);
    bcd_device = le16_to_cpu(udev->descriptor.bcdDevice);
    
    printk("btusb_probe info: VID/PID = %X/%X, Boot2 version = %X\n",
           id_vendor, id_product, bcd_device);
    printk("info: infClass=%#x infSubClass=%#x infProtocol=%#x\n",
           iface_desc->desc.bInterfaceClass, iface_desc->desc.bInterfaceSubClass,
           iface_desc->desc.bInterfaceProtocol);
    printk("info: bcdUSB=%#x Device Class=%#x SubClass=%#x Protocol=%#x\n",
           udev->descriptor.bcdUSB, udev->descriptor.bDeviceClass,
           udev->descriptor.bDeviceSubClass,
           udev->descriptor.bDeviceProtocol);

    printk("btusb_probe intf->cur_altsetting->desc.bInterfaceNumber %d, num_epts:%d, num_altsts:%d\r\n",
           intf->cur_altsetting->desc.bInterfaceNumber, 
           intf->cur_altsetting->desc.bNumEndpoints,
           intf->num_altsetting);

    ifnum_base = intf->cur_altsetting->desc.bInterfaceNumber;

    data = devm_kzalloc(&intf->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
        ep_desc = &intf->cur_altsetting->endpoint[i].desc;

        // printk("ep %d, ep addr:%d, ep type:%d\r\n", i, ep_desc->bEndpointAddress, ep_desc->bDescriptorType);

        if (!data->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
            data->bulk_tx_ep = ep_desc;
            continue;
        }

        if (!data->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
            data->bulk_rx_ep = ep_desc;
            continue;
        }
    }

    // if (!data->intr_ep || !data->bulk_tx_ep || !data->bulk_rx_ep)
    if (!data->bulk_tx_ep || !data->bulk_rx_ep)
        return -ENODEV;

    if (id->driver_info & BTUSB_AMP) {
        data->cmdreq_type = USB_TYPE_CLASS | 0x01;
        data->cmdreq = 0x2b;
    } else {
        data->cmdreq_type = USB_TYPE_CLASS;
        data->cmdreq = 0x00;
    }

    data->udev = interface_to_usbdev(intf);
    data->intf = intf;

    INIT_WORK(&data->work, btusb_work);
    INIT_WORK(&data->waker, btusb_waker);
    init_usb_anchor(&data->deferred);
    init_usb_anchor(&data->tx_anchor);
    spin_lock_init(&data->txlock);

    init_usb_anchor(&data->intr_anchor);
    init_usb_anchor(&data->bulk_anchor);
    init_usb_anchor(&data->isoc_anchor);
    init_usb_anchor(&data->ctrl_anchor);
    spin_lock_init(&data->rxlock);

    hdev = hci_alloc_dev();
    if (!hdev)
        return -ENOMEM;

    hdev->bus = HCI_USB;
    hci_set_drvdata(hdev, data);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 8, 0)
    //if (id->driver_info & BTUSB_AMP)
    //    hdev->dev_type = HCI_AMP;
    //else
        hdev->dev_type = HCI_PRIMARY;
#endif

    data->hdev = hdev;

    SET_HCIDEV_DEV(hdev, &intf->dev);

    hdev->open   = btusb_open;
    hdev->close  = btusb_close;
    hdev->flush  = btusb_flush;
    hdev->send   = btusb_send_frame;
    hdev->notify = btusb_notify;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 9)
    hdev->prevent_wake = btusb_prevent_wake;
#endif
#endif

#if 0//def CONFIG_PM
    err = btusb_config_oob_wake(hdev);
    if (err)
        goto out_free_dev;
#endif

    if (id->driver_info & BTUSB_AMP) {
        /* AMP controllers do not support SCO packets */
        data->isoc = NULL;
    } else {
        /* Interface orders are hardcoded in the specification */
        data->isoc = usb_ifnum_to_if(data->udev, ifnum_base + 1);
        data->isoc_ifnum = ifnum_base + 1;
    }

    if (id->driver_info & BTUSB_BROKEN_ISOC)
        data->isoc = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
    if (id->driver_info & BTUSB_WIDEBAND_SPEECH)
        set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED, &hdev->quirks);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 8, 0)
    if (id->driver_info & BTUSB_VALID_LE_STATES)
        set_bit(HCI_QUIRK_VALID_LE_STATES, &hdev->quirks);
#endif
#endif

    if (id->driver_info & BTUSB_SNIFFER) {
        struct usb_device *udev = data->udev;

        /* New sniffer firmware has crippled HCI interface */
        if (le16_to_cpu(udev->descriptor.bcdDevice) > 0x997)
            set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);
    }

    if (data->isoc) {
        err = usb_driver_claim_interface(&btusb_driver, data->isoc, data);
        if (err < 0)
            goto out_free_dev;
    }

    data->diag = NULL;

    //if (enable_autosuspend)
    //    usb_enable_autosuspend(data->udev);

    err = hci_register_dev(hdev);
    if (err < 0)
        goto out_free_dev;

    usb_set_intfdata(intf, data);

    BTBLE_DBG("%s, done\r\n", __func__);

    return 0;

out_free_dev:
    hci_free_dev(hdev);
    
    return err;
}

static void btusb_disconnect(struct usb_interface *intf)
{
    struct btusb_data *data = usb_get_intfdata(intf);
    struct hci_dev *hdev;

    BTBLE_DBG("%s, intf %p", __func__, intf);

    if (!data)
        return;

    hdev = data->hdev;
    usb_set_intfdata(data->intf, NULL);

    if (data->isoc)
        usb_set_intfdata(data->isoc, NULL);

    hci_unregister_dev(hdev);

    if (intf == data->intf) {
        if (data->isoc)
            usb_driver_release_interface(&btusb_driver, data->isoc);
    } else if (intf == data->isoc) {
        usb_driver_release_interface(&btusb_driver, data->intf);
    }

    if (data->oob_wake_irq)
        device_init_wakeup(&data->udev->dev, false);

    hci_free_dev(hdev);

    BTBLE_DBG("%s, done\r\n", __func__);
}

#ifdef CONFIG_PM
static int btusb_suspend(struct usb_interface *intf, pm_message_t message)
{
    struct btusb_data *data = usb_get_intfdata(intf);
    unsigned long flags;

    BTBLE_DBG("%s, intf %p", __func__, intf);

    if (data->suspend_count++)
        return 0;

    spin_lock_irqsave(&data->txlock, flags);
    if (!(PMSG_IS_AUTO(message) && data->tx_in_flight)) {
        set_bit(BTUSB_SUSPENDING, &data->flags);
        spin_unlock_irqrestore(&data->txlock, flags);
    } else {
        spin_unlock_irqrestore(&data->txlock, flags);
        data->suspend_count--;
        
        return -EBUSY;
    }

    cancel_work_sync(&data->work);

    btusb_stop_traffic(data);
    usb_kill_anchored_urbs(&data->tx_anchor);

    if (data->oob_wake_irq && device_may_wakeup(&data->udev->dev)) {
        set_bit(BTUSB_OOB_WAKE_ENABLED, &data->flags);
        enable_irq_wake(data->oob_wake_irq);
        enable_irq(data->oob_wake_irq);
    }

    /* For global suspend, Realtek devices lose the loaded fw
     * in them. But for autosuspend, firmware should remain.
     * Actually, it depends on whether the usb host sends
     * set feature (enable wakeup) or not.
     */
    if (test_bit(BTUSB_WAKEUP_DISABLE, &data->flags)) {
        if (PMSG_IS_AUTO(message) &&
            device_can_wakeup(&data->udev->dev))
            data->udev->do_remote_wakeup = 1;
        else if (!PMSG_IS_AUTO(message))
            data->udev->reset_resume = 1;
    }

    return 0;
}

static void play_deferred(struct btusb_data *data)
{
    struct urb *urb;
    int err;

    while ((urb = usb_get_from_anchor(&data->deferred))) {
        usb_anchor_urb(urb, &data->tx_anchor);

        err = usb_submit_urb(urb, GFP_ATOMIC);
        if (err < 0) {
            if (err != -EPERM && err != -ENODEV)
                BT_ERR("%s urb %p submission failed (%d)",
                       data->hdev->name, urb, -err);
                       
            kfree(urb->setup_packet);
            usb_unanchor_urb(urb);
            usb_free_urb(urb);
            break;
        }

        data->tx_in_flight++;
        usb_free_urb(urb);
    }

    /* Cleanup the rest deferred urbs. */
    while ((urb = usb_get_from_anchor(&data->deferred))) {
        kfree(urb->setup_packet);
        usb_free_urb(urb);
    }
}

static int btusb_resume(struct usb_interface *intf)
{
    struct btusb_data *data = usb_get_intfdata(intf);
    struct hci_dev *hdev = data->hdev;
    unsigned long flags;
    int err = 0;

    BTBLE_DBG("%s, intf %p", __func__, intf);

    if (--data->suspend_count)
        return 0;

    /* Disable only if not already disabled (keep it balanced) */
    if (test_and_clear_bit(BTUSB_OOB_WAKE_ENABLED, &data->flags)) {
        disable_irq(data->oob_wake_irq);
        disable_irq_wake(data->oob_wake_irq);
    }

    if (!test_bit(HCI_RUNNING, &hdev->flags))
        goto done;

    if (test_bit(BTUSB_INTR_RUNNING, &data->flags)) {
        err = btusb_submit_intr_urb(hdev, GFP_NOIO);
        if (err < 0) {
            clear_bit(BTUSB_INTR_RUNNING, &data->flags);
            goto failed;
        }
    }

    if (test_bit(BTUSB_BULK_RUNNING, &data->flags)) {
        err = btusb_submit_bulk_urb(hdev, GFP_NOIO);
        if (err < 0) {
            clear_bit(BTUSB_BULK_RUNNING, &data->flags);
            goto failed;
        }

        btusb_submit_bulk_urb(hdev, GFP_NOIO);
    }

    if (test_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
        if (btusb_submit_isoc_urb(hdev, GFP_NOIO) < 0)
            clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
        else
            btusb_submit_isoc_urb(hdev, GFP_NOIO);
    }

    spin_lock_irqsave(&data->txlock, flags);
    play_deferred(data);
    clear_bit(BTUSB_SUSPENDING, &data->flags);
    spin_unlock_irqrestore(&data->txlock, flags);
    schedule_work(&data->work);

    return 0;

failed:
    usb_scuttle_anchored_urbs(&data->deferred);
done:
    spin_lock_irqsave(&data->txlock, flags);
    clear_bit(BTUSB_SUSPENDING, &data->flags);
    spin_unlock_irqrestore(&data->txlock, flags);

    return err;
}
#endif

static struct usb_driver btusb_driver = {
    .name          = "bl_btusb",
    .probe         = btusb_probe,
    .disconnect    = btusb_disconnect,
#ifdef CONFIG_PM
    .suspend       = btusb_suspend,
    .resume        = btusb_resume,
#endif
    .id_table      = bl_btusb_table,
    .supports_autosuspend = 0,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
    .disable_hub_initiated_lpm = 1,
#endif
};

int bl_btusb_register_drv(void)
{
    return usb_register(&btusb_driver);
}

void bl_btusb_unregister_drv(void)
{
    usb_deregister(&btusb_driver);
}

#ifndef CONFIG_BL_BTUSB
static int __init bl_btusb_mod_init(void)
{
    BTBLE_DBG(BL_FN_ENTRY_STR);
    bl_print_version();

    return bl_btusb_register_drv();
}

static void __exit bl_btusb_mod_exit(void)
{
    BTBLE_DBG(BL_FN_ENTRY_STR);

    bl_btusb_unregister_drv();
}

module_init(bl_btusb_mod_init);
module_exit(bl_btusb_mod_exit);

MODULE_DESCRIPTION("Bouffalolab BT USB driver for Linux");
MODULE_VERSION(RELEASE_VERSION);
MODULE_AUTHOR("Copyright(c) 2017-2023 Bouffalolab");
MODULE_LICENSE("GPL");
#endif

