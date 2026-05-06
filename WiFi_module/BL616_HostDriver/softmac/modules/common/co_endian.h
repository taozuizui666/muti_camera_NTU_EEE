#ifndef _CO_ENDIAN_H_
#define _CO_ENDIAN_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "ke_config.h"

/**
 ****************************************************************************************
 * @brief Swap bytes of a 32 bits value.
 * The swap is done in every case. Should not be called directly.
 * @param[in] val32 The 32 bits value to swap.
 * @return The 32 bit swapped value.
 ****************************************************************************************
 */
__INLINE uint32_t co_bswap32(uint32_t val32)
{
    return (val32<<24) | ((val32<<8)&0xFF0000) | ((val32>>8)&0xFF00) | ((val32>>24)&0xFF);
}

/**
 ****************************************************************************************
 * @brief Swap bytes of a 16 bits value.
 * The swap is done in every case. Should not be called directly.
 * @param[in] val16 The 16 bit value to swap.
 * @return The 16 bit swapped value.
 ****************************************************************************************
 */
__INLINE uint16_t co_bswap16(uint16_t val16)
{
    return ((val16<<8)&0xFF00) | ((val16>>8)&0xFF);
}

/**
 ****************************************************************************************
 * @brief Convert host to network long word.
 *
 * @param[in] hostlong Long word value to convert.
 *
 * @return The converted long word.
 ****************************************************************************************
 */
__INLINE uint32_t co_htonl(uint32_t hostlong)
{
    #if (!CPU_LE)
        return hostlong;
    #else
        return co_bswap32(hostlong);
    #endif // CPU_LE
}

/**
 ****************************************************************************************
 * @brief Convert host to network short word.
 *
 * @param[in] hostshort Short word value to convert.
 *
 * @return The converted short word.
 ****************************************************************************************
 */
__INLINE uint16_t co_htons(uint16_t hostshort)
{
    #if (!CPU_LE)
        return hostshort;
    #else
        return co_bswap16(hostshort);
    #endif // CPU_LE
}

/**
 ****************************************************************************************
 * @brief Convert network to host long word.
 *
 * @param[in] netlong Long word value to convert.
 *
 * @return The converted long word.
 ****************************************************************************************
 */
__INLINE uint32_t co_ntohl(uint32_t netlong)
{
    return co_htonl(netlong);
}

/**
 ****************************************************************************************
 * @brief Convert network to host short word.
 *
 * @param[in] netshort Short word value to convert.
 *
 * @return The converted short word.
 ****************************************************************************************
 */
__INLINE uint16_t co_ntohs(uint16_t netshort)
{
    return co_htons(netshort);
}

/**
 ****************************************************************************************
 * @brief Convert host to wlan long word.
 *
 * @param[in] hostlong Long word value to convert.
 *
 * @return The converted long word.
 ****************************************************************************************
 */
__INLINE uint32_t co_htowl(uint32_t hostlong)
{
    #if (CPU_LE)
        return hostlong;
    #else
        return co_bswap32(hostlong);
    #endif // CPU_LE
}

/**
 ****************************************************************************************
 * @brief Convert host to wlan short word.
 *
 * @param[in] hostshort Short word value to convert.
 *
 * @return The converted short word.
 ****************************************************************************************
 */
__INLINE uint16_t co_htows(uint16_t hostshort)
{
    #if (CPU_LE)
        return hostshort;
    #else
        return co_bswap16(hostshort);
    #endif // CPU_LE
}


/**
 ****************************************************************************************
 * @brief Convert wlan to host long word.
 *
 * @param[in] wlanlong Long word value to convert.
 * @return The converted long word.
 ****************************************************************************************
 */
__INLINE uint32_t co_wtohl(uint32_t wlanlong)
{
    return co_htowl(wlanlong);
}


/**
 ****************************************************************************************
 * @brief Convert wlan to host short word.
 *
 * @param[in] wlanshort Short word value to convert.
 * @return The converted short word.
 ****************************************************************************************
 */
__INLINE uint16_t co_wtohs(uint16_t wlanshort)
{
    return co_htows(wlanshort);
}

#endif // _CO_ENDIAN_H_
