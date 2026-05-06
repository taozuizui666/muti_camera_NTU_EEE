/*
 * INCLUDE FILES
 ****************************************************************************************
 */
// for other MAC definitions
#include "mac.h"
#include "mac_frame.h"
#include "hal_mac_desc.h"

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */
const uint8_t mac_tid2ac[TID_MAX] =
{
    AC_BE,    // TID0
    AC_BK,    // TID1
    AC_BK,    // TID2
    AC_BE,    // TID3
    AC_VI,    // TID4
    AC_VI,    // TID5
    AC_VO,    // TID6
    AC_VO,    // TID7
    AC_VO     // TIDMGT
};

const uint8_t mac_ac2uapsd[AC_MAX] =
{
    [AC_BK] = MAC_QOS_INFO_STA_UAPSD_ENABLED_BK,
    [AC_BE] = MAC_QOS_INFO_STA_UAPSD_ENABLED_BE,
    [AC_VI] = MAC_QOS_INFO_STA_UAPSD_ENABLED_VI,
    [AC_VO] = MAC_QOS_INFO_STA_UAPSD_ENABLED_VO,
};

const uint8_t mac_aci2ac[AC_MAX] =
{
    [0] = AC_BE,
    [1] = AC_BK,
    [2] = AC_VI,
    [3] = AC_VO,
};

const uint8_t mac_ac2aci[AC_MAX] =
{
    [AC_BK] = 1,
    [AC_BE] = 0,
    [AC_VI] = 2,
    [AC_VO] = 3,
};

const uint8_t mac_id2rate[MAC_RATESET_LEN] =
{
    MAC_RATE_1MBPS,
    MAC_RATE_2MBPS,
    MAC_RATE_5_5MBPS,
    MAC_RATE_11MBPS,
    MAC_RATE_6MBPS,
    MAC_RATE_9MBPS,
    MAC_RATE_12MBPS,
    MAC_RATE_18MBPS,
    MAC_RATE_24MBPS,
    MAC_RATE_36MBPS,
    MAC_RATE_48MBPS,
    MAC_RATE_54MBPS
};

/*
 * FUNCTIONS
 ****************************************************************************************
 */
uint32_t mac_paid_gid_sta_compute(struct mac_addr const *mac_addr)
{
    uint32_t paid, gid;
    PTR2UINT mac_ptr = CPU2HW(mac_addr);

    paid = (co_read8p(mac_ptr + 4) >> 7) | (co_read8p(mac_ptr + 5) << 1);
    gid = 0;

    return ((paid << PAID_TX_OFT) | (gid << GID_TX_OFT));
}

uint32_t mac_paid_gid_ap_compute(struct mac_addr const *mac_addr, uint16_t aid)
{
    uint32_t paid, gid;
    PTR2UINT mac_ptr = CPU2HW(mac_addr);

    paid = ((aid & 0x1FF) + (((co_read8p(mac_ptr + 5) & 0x0F) ^
                                 ((co_read8p(mac_ptr + 5) & 0xF0) >> 4)) << 5))
                                                  & 0x1FF;
    gid = 63;

    return ((paid << PAID_TX_OFT) | (gid << GID_TX_OFT));
}

enum mac_akm_suite mac_akm_suite_value(uint32_t akm_suite)
{
    switch (akm_suite)
    {
        case MAC_RSNIE_AKM_8021X:
        case MAC_WPA_AKM_8021X:
            return MAC_AKM_8021X;
        case MAC_RSNIE_AKM_PSK:
        case MAC_WPA_AKM_PSK:
            return MAC_AKM_PSK;
        case MAC_RSNIE_AKM_FT_8021X:
            return MAC_AKM_FT_8021X;
        case MAC_RSNIE_AKM_FT_PSK:
            return MAC_AKM_FT_PSK;
        case MAC_RSNIE_AKM_8021X_SHA256:
            return MAC_AKM_8021X_SHA256;
        case MAC_RSNIE_AKM_PSK_SHA256:
            return MAC_AKM_PSK_SHA256;
        case MAC_RSNIE_AKM_TDLS:
            return MAC_AKM_TDLS;
        case MAC_RSNIE_AKM_SAE:
            return MAC_AKM_SAE;
        case MAC_RSNIE_AKM_FT_OVER_SAE:
            return MAC_AKM_FT_OVER_SAE;
        case MAC_RSNIE_AKM_8021X_SUITE_B:
            return MAC_AKM_8021X_SUITE_B;
        case MAC_RSNIE_AKM_8021X_SUITE_B_192:
            return MAC_AKM_8021X_SUITE_B_192;
        case MAC_RSNIE_AKM_FILS_SHA256:
            return MAC_AKM_FILS_SHA256;
        case MAC_RSNIE_AKM_FILS_SHA384:
            return MAC_AKM_FILS_SHA384;
        case MAC_RSNIE_AKM_FT_FILS_SHA256:
            return MAC_AKM_FT_FILS_SHA256;
        case MAC_RSNIE_AKM_FT_FILS_SHA384:
            return MAC_AKM_FT_FILS_SHA384;
        case MAC_RSNIE_AKM_OWE:
            return MAC_AKM_OWE;
        case MAC_WAPI_AKM_CERT:
            return MAC_AKM_WAPI_CERT;
        case MAC_WAPI_AKM_PSK:
            return MAC_AKM_WAPI_PSK;
        default:
            return -1;
    }
}

enum mac_cipher_suite mac_cipher_suite_value(uint32_t cipher_suite)
{
    switch (cipher_suite)
    {
        case MAC_RSNIE_CIPHER_WEP_40:
            return MAC_CIPHER_WEP40;
        case MAC_RSNIE_CIPHER_TKIP:
        case MAC_WPA_CIPHER_TKIP:
            return MAC_CIPHER_TKIP;
        case MAC_RSNIE_CIPHER_CCMP_128:
        case MAC_WPA_CIPHER_CCMP:
            return MAC_CIPHER_CCMP;
        case MAC_RSNIE_CIPHER_WEP_104:
            return MAC_CIPHER_WEP104;
        case MAC_RSNIE_CIPHER_BIP_CMAC_128:
            return MAC_CIPHER_BIP_CMAC_128;
        case MAC_RSNIE_CIPHER_GCMP_128:
            return MAC_CIPHER_GCMP_128;
        case MAC_RSNIE_CIPHER_GCMP_256:
            return MAC_CIPHER_GCMP_256;
        case MAC_RSNIE_CIPHER_CCMP_256:
            return MAC_CIPHER_CCMP_256;
        case MAC_RSNIE_CIPHER_BIP_GMAC_128:
            return MAC_CIPHER_BIP_GMAC_128;
        case MAC_RSNIE_CIPHER_BIP_GMAC_256:
            return MAC_CIPHER_BIP_GMAC_256;
        case MAC_RSNIE_CIPHER_BIP_CMAC_256:
            return MAC_CIPHER_BIP_CMAC_256;
        case MAC_WAPI_CIPHER_WPI_SMS4:
            return MAC_CIPHER_WPI_SMS4;
        default:
            return -1;
    }
}

uint8_t mac_rcpi_format(int8_t dbm)
{
    if (dbm >= 0)
        return 220;
    else if (dbm <= -110)
        return 0;
    else
        return (dbm + 110) * 2;

}


