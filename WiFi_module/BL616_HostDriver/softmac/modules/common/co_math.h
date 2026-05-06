#ifndef _CO_MATH_H_
#define _CO_MATH_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "ke_config.h"
#include "co_bit.h"

/*
 * MACROS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Align val on the multiple of size equal or nearest higher.
 * @param[in] val  Value to align.
 * @param[in] size of memory alignment (1, 2 or 4)
 * @return Value aligned.
 ****************************************************************************************
 */
#define CO_ALIGN_HI(val, size) ((((PTR2UINT) val)+((size)- 1))&~((size)- 1))

/**
 ****************************************************************************************
 * @brief Align val on the multiple of 4 equal or nearest higher.
 * @param[in] val Value to align.
 * @return Value aligned.
 ****************************************************************************************
 */
#define CO_ALIGN4_HI(val) (((val)+3)&~3)

/**
 ****************************************************************************************
 * @brief Align val on the multiple of a given number equal or nearest higher.
 *
 * x value should be a power of 2.
 *
 * @param[in] val  Value to align.
 * @param[in] x    Multiple value.
 * @return Value   aligned.
 ****************************************************************************************
 */
#define CO_ALIGNx_HI(val, x) (((val)+((x)-1))&~((x)-1))

/**
 ****************************************************************************************
 * @brief Align val on the multiple of 4 equal or nearest lower.
 * @param[in] val Value to align.
 * @return Value aligned.
 ****************************************************************************************
 */
#define CO_ALIGN4_LO(val) ((val)&~3)

/**
 ****************************************************************************************
 * @brief Align val on the multiple of 2 equal or nearest higher.
 * @param[in] val Value to align.
 * @return Value aligned.
 ****************************************************************************************
 */
#define CO_ALIGN2_HI(val) (((val)+1)&~1)


/**
 ****************************************************************************************
 * @brief Align val on the multiple of 2 equal or nearest lower.
 * @param[in] val Value to align.
 * @return Value aligned.
 ****************************************************************************************
 */
#define CO_ALIGN2_LO(val) ((val)&~1)

/**
 ****************************************************************************************
 * Perform a division and ceil up the result
 *
 * @param[in] val Value to divide
 * @param[in] div Divide value
 * @return ceil(val/div)
 ****************************************************************************************
 */
#define CO_DIVIDE_CEIL(val, div) (((val) + ((div) - 1))/ (div))

/**
 ****************************************************************************************
 * Perform a division and round the result
 *
 * @param[in] val Value to divide
 * @param[in] div Divide value
 * @return round(val/div)
 ****************************************************************************************
 */
#define CO_DIVIDE_ROUND(val, div) (((val) + ((div) >> 1))/ (div))

/*
 * FUNCTION DEFINTIONS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * Perform a modulo operation
 *
 * @param[in] val    Dividend
 * @param[in] div    Divisor
 * @return  val/div)
 ****************************************************************************************
 */
__INLINE uint32_t co_mod(uint32_t val, uint32_t div)
{
   return ((val) % (div));
}
#define CO_MOD(val, div) co_mod(val, div)

/**
 ****************************************************************************************
 * @brief Count leading zeros.
 * @param[in] val Value to count the number of leading zeros on.
 * @return Number of leading zeros when value is written as 32 bits.
 ****************************************************************************************
 */
__INLINE uint32_t co_clz(uint32_t val)
{
    uint32_t tmp;
    uint32_t shift = 0;

    if (val == 0)
    {
        return 32;
    }

    tmp = val >> 16;
    if (tmp)
    {
        shift = 16;
        val = tmp;
    }

    tmp = val >> 8;
    if (tmp)
    {
        shift += 8;
        val = tmp;
    }

    tmp = val >> 4;
    if (tmp)
    {
        shift += 4;
        val = tmp;
    }

    tmp = val >> 2;
    if (tmp)
    {
        shift += 2;
        val = tmp;
    }

    tmp = val >> 1;
    if (tmp)
    {
        shift += 1;
    }

    return (31 - shift);
}

/**
 ****************************************************************************************
 * @brief Count trailing zeros.
 * @param[in] val Value to count the number of trailing zeros on.
 * @return Number of trailing zeros when value is written as 32 bits.
 ****************************************************************************************
 */
__INLINE uint32_t co_ctz(uint32_t val)
{
    uint32_t i;
    for (i = 0; i < 32; i++)
    {
        if (val & CO_BIT(i))
            break;
    }
    return i;
}

 /**
  ****************************************************************************************
  * @brief Find last bit set.
  *
  * @param[in] val Value to count the last bit set.
  *
  * @return The index of the most significant bit set in a 32-bit word, -1 if value is 0
  ****************************************************************************************
  */
__INLINE int co_fls(uint32_t val)
{
    return (31 - co_clz(val));
}

/**
 ****************************************************************************************
 * @brief Function to return the smallest of 2 unsigned 32 bits words.
 * @return The smallest value.
 ****************************************************************************************
 */
__INLINE uint32_t co_min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

/**
 ****************************************************************************************
 * @brief Function to return the smallest of 2 signed 32 bits words.
 * @return The smallest value.
 ****************************************************************************************
 */
__INLINE int32_t co_min_s(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

/**
 ****************************************************************************************
 * @brief Function to return the greatest of 2 unsigned 32 bits words.
 * @return The greatest value.
 ****************************************************************************************
 */
__INLINE uint32_t co_max(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

/**
 ****************************************************************************************
 * @brief Function to return the absolute value of a signed integer.
 * @return The absolute value.
 ****************************************************************************************
 */
__INLINE int co_abs(int val)
{
    return (val < 0) ? (0 - val) : val;
}

/**
 ****************************************************************************************
 * @brief Compute a CRC32 on the buffer passed as parameter. The initial value of the
 * computation is taken from crc parameter, allowing for incremental computation.
 *
 * @param[in] addr   Pointer to the buffer on which the CRC has to be computed
 * @param[in] len    Length of the buffer
 * @param[in] crc    The initial value of the CRC computation
 *
 * @return The CRC computed on the buffer.
 ****************************************************************************************
 */
uint32_t co_crc32(uint32_t addr, uint32_t len, uint32_t crc);


#endif // _CO_MATH_H_
