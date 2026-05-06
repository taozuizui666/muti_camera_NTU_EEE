#ifndef _BL_FWBIN_H_
#define _BL_FWBIN_H_

#ifdef CONFIG_BL_SDIO
extern const unsigned char bl616_sd_wlan_bin[];
extern const unsigned int bl616_sd_wlan_bin_len;

extern const unsigned char bl616_sd_combo_ble_uart_bin[];
extern const unsigned int bl616_sd_combo_ble_uart_bin_len;

extern const unsigned char bl616_sd_combo_ble_sdu_bin[];
extern const unsigned int bl616_sd_combo_ble_sdu_bin_len[];
#endif

#ifdef CONFIG_BL_USB
extern const unsigned char bl616_usb_wlan_bin[];
extern const unsigned int bl616_usb_wlan_bin_len;
extern const unsigned char bl616_usb_combo_bin[];
extern const unsigned int bl616_usb_combo_bin_len;
#endif

#endif /* _BL_FWBIN_H_ */
