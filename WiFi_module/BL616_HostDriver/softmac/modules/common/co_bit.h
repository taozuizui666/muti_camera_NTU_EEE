#ifndef _CO_BIT_H_
#define _CO_BIT_H_


/**
 ****************************************************************************************
 * @brief Return value with one bit set.
 *
 * @param[in] pos Position of the bit to set.
 *
 * @return Value with one bit set.
 ****************************************************************************************
 */
#ifndef CO_BIT
#define CO_BIT(pos) (1UL<<(pos))
#endif

/**
 ****************************************************************************************
 * @brief Return value bit into a bit field.
 *
 * @param[in] bf  Bit Field
 * @param[in] pos Position of the bit
 *
 * @return value of a bit into a bit field
 ****************************************************************************************
 */
#define CO_BIT_GET(bf, pos) (((((uint8_t*)bf)[((pos) >> 3)])>>((pos) & 0x7)) & 0x1)

/**
 ****************************************************************************************
 * @brief Update value bit into a bit field.
 *
 * @param[in] bf  Bit Field
 * @param[in] pos Position of the bit
 * @param[in] val New value of the bit (0 or 1)
 ****************************************************************************************
 */
#define CO_BIT_SET(bf, pos, val) (((uint8_t*)bf)[((pos) >> 3)]) = ((((uint8_t*)bf)[((pos) >> 3)]) & ~CO_BIT(((pos) & 0x7))) \
                                                                | (((val) & 0x1) << ((pos) & 0x7))


/// count number of bit into a long field
#define CO_BIT_CNT(val) (co_bit_cnt((uint8_t*) &(val), sizeof(val)))


#endif // _CO_BIT_H_
