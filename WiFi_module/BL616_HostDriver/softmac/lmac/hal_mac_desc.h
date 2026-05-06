
#ifndef _HAL_MAC_DESC_H_
#define _HAL_MAC_DESC_H_


/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "co_math.h"
#include "co_list.h"

#define PHY_MAX_PWR          ((int8_t)22)     // dBm
#define PHY_MIN_PWR          ((int8_t)-10)    // dBm

/// Use BW signaling bit
#define USE_BW_SIG_TX_BIT       CO_BIT(3)
/// Dynamic BW
#define DYN_BW_TX_BIT           CO_BIT(4)
/// Doze allowed by AP during TXOP
#define DOZE_ALLOWED_TX_BIT     CO_BIT(5)
/// Continuous transmit
#define CONT_TX_BIT             CO_BIT(6)
/// User Position field offset
#define USER_POS_OFT            12
/// User Position field mask
#define USER_POS_MASK           (0x3 << USER_POS_OFT)
/// Use RIFS for Transmission (Bit 14)
#define USE_RIFS_TX_BIT         CO_BIT(14)
/// Use MU-MIMO for Transmission (Bit 15)
#define USE_MUMIMO_TX_BIT       CO_BIT(15)
/// GroupId field offset
#define GID_TX_OFT              16
/// GroupId field mask
#define GID_TX_MASK             (0x3F << GID_TX_OFT)
/// Partial AID field offset
#define PAID_TX_OFT             22
/// Partial AID field mask
#define PAID_TX_MASK            (0x1FF << PAID_TX_OFT)

/// Frame successful by TX DMA: Ack received successfully
#define FRAME_SUCCESSFUL_TX_BIT            CO_BIT(23)


/// 20 MHz bandwidth
#define BW_20MHZ                  0
/// 40 MHz bandwidth
#define BW_40MHZ                  1
/// 80 MHz bandwidth
#define BW_80MHZ                  2
/// 160 or 80+80 MHz bandwidth
#define BW_160MHZ                 3

/// legacy RATE definitions
enum
{
    /// 1Mbps
    HW_RATE_1MBPS = 0,
    /// 2Mbps
    HW_RATE_2MBPS,
    /// 5.5Mbps
    HW_RATE_5_5MBPS,
    /// 11Mbps
    HW_RATE_11MBPS,
    /// 6Mbps
    HW_RATE_6MBPS,
    /// 9Mbps
    HW_RATE_9MBPS,
    /// 12Mbps
    HW_RATE_12MBPS,
    /// 18Mbps
    HW_RATE_18MBPS,
    /// 24Mbps
    HW_RATE_24MBPS,
    /// 36Mbps
    HW_RATE_36MBPS,
    /// 48Mbps
    HW_RATE_48MBPS,
    /// 54Mbps
    HW_RATE_54MBPS
};

__INLINE void phy_get_rf_gain_capab(int8_t *max, int8_t *min)
{
    *max = PHY_MAX_PWR; // dBm
    *min = PHY_MIN_PWR; // dBm
}

#endif

