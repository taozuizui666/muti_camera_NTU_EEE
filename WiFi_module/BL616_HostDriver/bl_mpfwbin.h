#ifndef _BL_MPFWBIN_H_
#define _BL_MPFWBIN_H_

#ifdef CONFIG_BL_SDIO
extern const unsigned char bl616_sd_mp_bin[];
extern const unsigned int bl616_sd_mp_bin_len;
#endif

#ifdef CONFIG_BL_USB
extern const unsigned char bl616_usb_mp_bin[];
extern const unsigned int bl616_usb_mp_bin_len;
#endif

#endif /* _BL_FWBIN_H_ */
