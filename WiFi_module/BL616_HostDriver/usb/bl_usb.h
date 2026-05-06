#ifndef _BL_USB_H_
#define _BL_USB_H_

#include "bl_platform.h"

#define USB_FN_ENTRY_STR ">>> %s()\n", __func__
#define USB_FN_LEAVE_STR "<<< %s()\n", __func__

#ifdef BL_BUS_LOOPBACK
#define BL_RX_CMD_BUF_SIZE (4*1024)
#define BL_RX_DATA_BUF_SIZE (4*1024)
#else
#define BL_RX_CMD_BUF_SIZE (2*1024)
#ifdef BL_USB_RX_BUF_2K
#define BL_RX_DATA_BUF_SIZE (2*1024)
#else
#define BL_RX_DATA_BUF_SIZE (4*1024)
#endif
#endif

#define BL616_FW_DNLD    1
#define BL616_FW_READY   2
#define BL616_FW_DNLD_OK 0x2C
#define BL616_FW_DNLD_FAIL 0x2D

#define BL_TX_DATA_URB 20
#define BL_RX_DATA_URB 40

#define BL_USB_TIMEOUT 100

#if 0
enum bl_usb_ep {
    BL_USB_EP_CMD_EVENT = 1,
    BL_USB_EP_DATA = 2,
};
#endif

enum bl_usb_ep {
    BL_USB_EP_IN  = 1,
    BL_USB_EP_OUT = 2,
    #if defined(CONFIG_CMD_USB_EP)
    BL_USB_EP_CMD_OUT = 4,
    #endif
};

enum bl_usb_ep_type {
    BL_USB_EP_MSG  = 1,
    BL_USB_EP_DATA = 2,
};

struct urb_context {
    struct bl_hw *bl_hw;
    struct sk_buff *skb;
    struct bl_cmd_a2emsg *a2e_msg;
    struct urb *urb;
    u8 ep;
    u8 flag;
    u8 resubmit;
    unsigned int sn;
};

struct bl_usb_device {
    struct usb_device *udev;
    struct usb_interface *intf;

    u8 usb_boot_state;
    u8 block_status;
    int tx_data_idx;

    u8 rx_ep;
    u8 tx_ep;

    #if defined(CONFIG_CMD_USB_EP)
    u8 tx_cmd_ep;
    #endif

    atomic_t tx_cmd_urb_pending;
    atomic_t tx_data_urb_pending;
    atomic_t rx_cmd_urb_pending;
    atomic_t rx_data_urb_pending;

    struct urb_context tx_cmd;
    struct urb_context rx_cmd;
    struct urb_context tx_data_list[BL_TX_DATA_URB];
    struct urb_context rx_data_list[BL_RX_DATA_URB];
};

int bl_device_init(struct usb_interface *inf, struct bl_plat **bl_plat);
void bl_device_deinit(struct bl_plat *bl_plat);
int bl_usb_dnld_fw(struct bl_plat *bl_plat);
int bl_usb_txrx_init(struct bl_hw *hw);
int bl_usb_txrx_deinit(struct bl_hw *bl_hw);
int bl_write_data_sync(struct bl_plat *bl_plat, u8 *pbuf, u32 len, u8 ep, u32 timeout);
int bl_read_data_sync(struct bl_plat *bl_plat, u8 *pbuf, u32 len, u8 ep, u32 timeout);
int bl_usb_host_to_card(struct bl_hw *bl_hw, u8 *data, u32 data_len, u8 ep, u8 flag, struct sk_buff *skb);
int bl_usb_register_drv(void);
void bl_usb_unregister_drv(void);
void bl_rx_urb_resubmit(struct bl_hw *bl_hw);

#endif
