#ifndef _CO_UTILS_H_
#define _CO_UTILS_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "bl_ipc_compat.h"

#include "ke_config.h"
#include "co_math.h"

/*
 * ENUMERATIONS DEFINITIONS
 ****************************************************************************************
 */

/// Status returned by generic packer-unpacker
enum CO_UTIL_PACK_STATUS
{
    CO_UTIL_PACK_OK,
    CO_UTIL_PACK_IN_BUF_OVFLW,
    CO_UTIL_PACK_OUT_BUF_OVFLW,
    CO_UTIL_PACK_WRONG_FORMAT,
    CO_UTIL_PACK_ERROR,
};

/*
 * MACRO DEFINITIONS
 ****************************************************************************************
 */
/// Pack a structure field
#define __PACKED __attribute__ ((__packed__))
#define __PACKED16 __attribute__ ((__packed__))

/// Common constants - bit field definitions
#define BIT0  0x0001
#define BIT1  0x0002
#define BIT2  0x0004
#define BIT3  0x0008
#define BIT4  0x0010
#define BIT5  0x0020
#define BIT6  0x0040
#define BIT7  0x0080
#define BIT8  0x0100
#define BIT9  0x0200
#define BIT10 0x0400
#define BIT11 0x0800
#define BIT12 0x1000
#define BIT13 0x2000
#define BIT14 0x4000
#define BIT15 0x8000

extern const unsigned char one_bits[];
/// Number of '1' bits in a byte
#define NB_ONE_BITS(byte)   (one_bits[byte & 0x0F] + one_bits[byte >> 4])

/// Get the number of elements within an array, give also number of rows in a 2-D array
#define ARRAY_LEN(array)   (sizeof((array))/sizeof((array)[0]))

/// Get the number of columns within a 2-D array
#define ARRAY_NB_COLUMNS(array)  (sizeof((array[0]))/sizeof((array)[0][0]))


/// Macro for LMP message handler function declaration or definition
#define LMP_MSG_HANDLER(msg_name)   __STATIC int lmp_##msg_name##_handler(struct lmp_##msg_name const *param,  \
                                                                                ke_task_id_t const dest_id)
/// Macro for LMP message handler function declaration or definition
#define LLCP_MSG_HANDLER(msg_name)   __STATIC int llcp_##msg_name##_handler(struct llcp_##msg_name const *param,  \
                                                                                ke_task_id_t const dest_id)

/// Macro for HCI message handler function declaration or definition (for multi-instantiated tasks)
#define HCI_CMD_HANDLER_C(cmd_name, param_struct)   __STATIC int hci_##cmd_name##_cmd_lc_handler(param_struct const *param,  \
                                                                                ke_task_id_t const dest_id,  \
                                                                                uint16_t opcode)

/// Macro for HCI message handler function declaration or definition (with parameters)
#define HCI_CMD_HANDLER(cmd_name, param_struct)   __STATIC int hci_##cmd_name##_cmd_handler(param_struct const *param,  \
                                                                                uint16_t opcode)

/// Macro for HCI message handler function declaration or definition (with parameters)
#define HCI_CMD_HANDLER_TAB(task)   __STATIC const struct task##_hci_cmd_handler task##_hci_command_handler_tab[] =


/// MACRO to build a subversion field from the Minor and Release fields
#define CO_SUBVERSION_BUILD(minor, release)     (((minor) << 8) | (release))


/// Macro to get a structure from one of its structure field
#define CONTAINER_OF(ptr, type, member)    ((type *)( (char *)ptr - offsetof(type,member) ))


/// Increment value and make sure it's never greater or equals max (else wrap to 0)
#define CO_VAL_INC(_val, _max)      \
    (_val) = (_val) + 1;            \
    if((_val) >= (_max)) (_val) = 0


/// Add value and make sure it's never greater or equals max (else wrap)
/// _add must be less that _max
#define CO_VAL_ADD(_val, _add, _max)      \
    (_val) = (_val) + (_add);             \
    if((_val) >= (_max)) (_val) -= (_max)

/// sub value and make sure it's never greater or equals max (else wrap)
/// _sub must be less that _max
#define CO_VAL_SUB(_val, _sub, _max)      \
    if((_val) < (_sub)) (_val) += _max;   \
    (_val) = (_val) - (_sub)

/**
 ****************************************************************************************
 * @brief Clocks addition with 2 operands
 *
 * @param[in]   clock_a   1st operand value (in BT half-slots)
 * @param[in]   clock_b   2nd operand value (in BT half-slots)
 * @return      result    operation result (in BT half-slots)
 ****************************************************************************************
 */
#define CLK_ADD_2(clock_a, clock_b)     ((uint32_t)(((clock_a) + (clock_b)) & RWIP_MAX_CLOCK_TIME))

/**
 ****************************************************************************************
 * @brief Clocks addition with 3 operands
 *
 * @param[in]   clock_a   1st operand value (in BT half-slots)
 * @param[in]   clock_b   2nd operand value (in BT half-slots)
 * @param[in]   clock_c   3rd operand value (in BT half-slots)
 * @return      result    operation result (in BT half-slots)
 ****************************************************************************************
 */
#define CLK_ADD_3(clock_a, clock_b, clock_c)     ((uint32_t)(((clock_a) + (clock_b) + (clock_c)) & RWIP_MAX_CLOCK_TIME))

/**
 ****************************************************************************************
 * @brief Clocks subtraction
 *
 * @param[in]   clock_a   1st operand value (in BT half-slots)
 * @param[in]   clock_b   2nd operand value (in BT half-slots)
 * @return      result    operation result (in BT half-slots)
 ****************************************************************************************
 */
#define CLK_SUB(clock_a, clock_b)     ((uint32_t)(((clock_a) - (clock_b)) & RWIP_MAX_CLOCK_TIME))

/**
 ****************************************************************************************
 * @brief Bluetooth timestamp Clocks subtraction
 *
 * @param[in]   clock_a   1st operand value (in microseconds)
 * @param[in]   clock_b   2nd operand value (in microseconds)
 * @return      result    operation result (in microseconds)
 ****************************************************************************************
 */
#define CLK_BTS_SUB(clock_a, clock_b)     (((int32_t) ((clock_a) - (clock_b))))

/**
 ****************************************************************************************
 * @brief Check if clock_a is lower than or equal to clock_b
 *
 * @param[in]   clock_a   Clock A value (in BT half-slots)
 * @param[in]   clock_b   Clock B value (in BT half-slots)
 * @return      result    True: clock_a lower than or equal to clock_b | False: else
 ****************************************************************************************
 */
#define CLK_BTS_LOWER_EQ(clock_a, clock_b) (((uint32_t)CLK_BTS_SUB(clock_b, clock_a)) < (RWIP_MAX_BTS_TIME >> 1))

/**
 ****************************************************************************************
 * @brief Clocks time difference
 *
 * @param[in]   clock_a   1st operand value (in BT half-slots)
 * @param[in]   clock_b   2nd operand value (in BT half-slots)
 * @return      result    return the time difference from clock A to clock B
 *                           - result < 0  => clock_b is in the past
 *                           - result == 0 => clock_a is equal to clock_b
 *                           - result > 0  => clock_b is in the future
 ****************************************************************************************
 */
#define CLK_DIFF(clock_a, clock_b)     ( (CLK_SUB((clock_b), (clock_a)) > ((RWIP_MAX_CLOCK_TIME+1) >> 1)) ?                      \
                          ((int32_t)((-CLK_SUB((clock_a), (clock_b))))) : ((int32_t)((CLK_SUB((clock_b), (clock_a))))) )



/// macro to extract a field from a value containing several fields
/// @param[in] __r bit field value
/// @param[in] __f field name
/// @return the value of the register masked and shifted
#define GETF(__r, __f)                                                           \
    (( (__r) & (__f##_MASK) ) >> (__f##_LSB))

/// macro to set a field value into a value  containing several fields.
/// @param[in] __r bit field value
/// @param[in] __f field name
/// @param[in] __v value to put in field
#define SETF(__r, __f, __v)                                                      \
    do {                                                                         \
        ASSERT_INFO( ( ( ( (__v) << (__f##_LSB) ) & ( ~(__f##_MASK) ) ) ) == 0 ,(__f##_MASK), (__v)); \
        __r = (((__r) & ~(__f##_MASK)) | (__v) << (__f##_LSB));                  \
    } while (0)



/// macro to extract a bit field from a value containing several fields
/// @param[in] __r bit field value
/// @param[in] __b bit field name
/// @return the value of the register masked and shifted
#define GETB(__r, __b)                                                           \
    (( (__r) & (__b##_BIT) ) >> (__b##_POS))

/// macro to set a bit field value into a value containing several fields.
/// @param[in] __r bit field value
/// @param[in] __b bit field name
/// @param[in] __v value to put in field
#define SETB(__r, __b, __v)                                                      \
    do {                                                                         \
        ASSERT_ERR( ( ( ( (__v ? 1 : 0) << (__b##_POS) ) & ( ~(__b##_BIT) ) ) ) == 0 ); \
        __r = (((__r) & ~(__b##_BIT)) | (__v ? 1 : 0) << (__b##_POS));                  \
    } while (0)

/// macro to toggle a bit into a value containing several bits.
/// @param[in] __r bit field value
/// @param[in] __b bit field name
#define TOGB(__r, __b)                                                           \
    do {                                                                         \
        __r = ((__r) ^ (__b##_BIT));                                             \
    } while (0)

/**
 ****************************************************************************************
 * @brief Check if clock_a is equal to clock_b
 *
 * @param[in]   clock_a   Clock A value (in BT half-slots)
 * @param[in]   clock_b   Clock B value (in BT half-slots)
 * @return      result    True: clock_a lower than or equal to clock_b | False: else
 ****************************************************************************************
 */
#define CLK_EQ(clock_a, clock_b)      (clock_b == clock_a)

/**
 ****************************************************************************************
 * @brief Check if clock_a is lower than or equal to clock_b
 *
 * @param[in]   clock_a   Clock A value (in BT half-slots)
 * @param[in]   clock_b   Clock B value (in BT half-slots)
 * @return      result    True: clock_a lower than or equal to clock_b | False: else
 ****************************************************************************************
 */
#define CLK_LOWER_EQ(clock_a, clock_b)      (CLK_SUB(clock_b, clock_a) < (RWIP_MAX_CLOCK_TIME >> 1))

/**
 ****************************************************************************************
 * @brief Check if clock A is lower than or equal to clock B (with half-us precision)
 *
 * @param[in]   int_a     Integer part of clock A (in BT half-slots)
 * @param[in]   fract_a   Fractional part of clock A (in half-us) (range: 0 to 624)
 * @param[in]   int_b     Integer part of clock B (in BT half-slots)
 * @param[in]   fract_b   Fractional part of clock B (in half-us) (range: 0 to 624)
 * @return      result    True: clock A lower than or equal to clock B | False: else
 ****************************************************************************************
 */
#define CLK_LOWER_EQ_HUS(int_a, fract_a, int_b, fract_b)      (  CLK_GREATER_THAN(int_b, int_a)  \
                                                                 || (   CLK_EQ(int_a, int_b)     \
                                                                     && (fract_a <= fract_b) ) ) \

/**
 ****************************************************************************************
 * @brief Check if clock_a is greater than clock_b
 *
 * @param[in]   clock_a   Clock A value (in BT half-slots)
 * @param[in]   clock_b   Clock B value (in BT half-slots)
 * @return      result    True: clock_a is greater than clock_b | False: else
 ****************************************************************************************
 */
#define CLK_GREATER_THAN(clock_a, clock_b)    !(CLK_LOWER_EQ(clock_a, clock_b))

/**
 ****************************************************************************************
 * @brief Check if clock A is greater than clock B (with half-us precision)
 *
 * @param[in]   int_a     Integer part of clock A (in BT half-slots)
 * @param[in]   fract_a   Fractional part of clock A (in half-us) (range: 0 to 624)
 * @param[in]   int_b     Integer part of clock B (in BT half-slots)
 * @param[in]   fract_b   Fractional part of clock B (in half-us) (range: 0 to 624)
 * @return      result    True: clock A greater than clock B | False: else
 ****************************************************************************************
 */
#define CLK_GREATER_THAN_HUS(int_a, fract_a, int_b, fract_b)      (  CLK_GREATER_THAN(int_a, int_b)  \
                                                                     || (   CLK_EQ(int_a, int_b)     \
                                                                         && (fract_a > fract_b) ) )  \

/**
 ****************************************************************************************
 * @brief Converts a STA index in an Association Index.
 * @param[in] sta_idx The station index.
 * @return The association index
 ****************************************************************************************
 */
#define CO_STAIDX_TO_AID(sta_idx) ((sta_idx) + 1)

/**
 ****************************************************************************************
 * @brief Get the index of an element in an array.
 * @param[in] __element_ptr Pointer to the element
 * @param[in] __array_ptr Pointer to the array
 * @return The index of the element
 ****************************************************************************************
 */
#define CO_GET_INDEX(__element_ptr, __array_ptr) ((__element_ptr) - (__array_ptr))

/**
 ****************************************************************************************
 * @brief Get the number of element in an array.
 * @param[in] a  Pointer to the array
 * @return The number of the element
 ****************************************************************************************
 */
#define CO_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/// Length of a char in bytes
#define CHAR_LEN    1

/**
 ****************************************************************************************
 * @brief Converts a CPU pointer into a HW address
 * This macro is used to convert a SW pointer into the corresponding HW address. With CPUs
 * having native byte support, the value returned will be the same as the pointer passed.
 * With TL4, the value returned is the pointer multiplied by 2.
 * @param[in] ptr Pointer to be converted
 * @return The corresponding HW address
 ****************************************************************************************
 */
#define CPU2HW(ptr) (((PTR2UINT)(ptr)) * CHAR_LEN)

/**
 ****************************************************************************************
 * @brief Converts a HW address into a CPU pointer
 * This macro is doing the reverse operation as @ref CPU2HW.
 * @param[in] ptr Address to be converted
 * @return The corresponding CPU pointer
 ****************************************************************************************
 */
#define HW2CPU(ptr) ((void *)(((PTR2UINT)(ptr)) / CHAR_LEN))

/**
 ****************************************************************************************
 * @brief Return the size of a variable or type in bytes
 * @param[in] a Variable for which the size is computed
 * @return The size of the variable in bytes
 ****************************************************************************************
 */
#define sizeof_b(a) (sizeof(a) * CHAR_LEN)

/**
 ****************************************************************************************
 * @brief Return the offset (in bytes) of a structure element
 * @param[in] a Structure type
 * @param[in] b Field name
 * @return The of the field in bytes
 ****************************************************************************************
 */
#define offsetof_b(a, b)  (offsetof(a, b) * CHAR_LEN)

/*
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Read an aligned 16 bits word.
 * @param[in] ptr16 The address of the first byte of the 16 bits word.
 * @return The 16 bits value.
 ****************************************************************************************
 */
__INLINE uint16_t co_read16(void const *ptr16)
{
    return *((uint16_t*)ptr16);
}

/**
 ****************************************************************************************
 * @brief Write an aligned 16 bits word.
 * @param[in] ptr16 The address of the first byte of the 16 bits word.
 * @param[in] value The value to write.
 ****************************************************************************************
 */
__INLINE void co_write16(void const *ptr16, uint32_t value)
{
    *(uint16_t*)ptr16 = value;
}

/**
 ****************************************************************************************
 * @brief Read an aligned 32 bit word.
 * @param[in] ptr32 The address of the first byte of the 32 bit word.
 * @return The 32 bit value.
 ****************************************************************************************
 */
__INLINE uint32_t co_read32(void const *ptr32)
{
    return *((uint32_t*)ptr32);
}

/**
 ****************************************************************************************
 * @brief Write an aligned 32 bits word.
 * @param[in] ptr32 The address of the first byte of the 32 bits word.
 * @param[in] value The value to write.
 ****************************************************************************************
 */
__INLINE void co_write32(void const *ptr32, uint32_t value)
{
    *(uint32_t*)ptr32 = value;
}

/**
 ****************************************************************************************
 * @brief Copy a 32-bit aligned buffer into another one
 * The length in bytes is converted to the corresponding number of 32-bit words. If the
 * byte length is not a multiple of 4, then additional bytes will be copied at the end
 * of the buffer. It is the responsibility of the caller to ensure that these extra-byte
 * copy won't corrupt the memory at the end of the destination buffer.
 *
 * @param[in] dst Pointer to the destination buffer
 * @param[in] src Pointer to the source buffer
 * @param[in] len Length to be copied (in bytes)
 ****************************************************************************************
 */
__INLINE void co_copy32(uint32_t *dst, uint32_t *src, uint32_t len)
{
    len = CO_ALIGN4_HI(len)/4;
    while (len--)
    {
        *dst++ = *src++;
    }
}

/**
 ****************************************************************************************
 * @brief Read a 8 bits word.
 * @param[in] addr The address of the first byte of the 8 bits word.
 * @return The read value
 ****************************************************************************************
 */
__INLINE uint8_t co_read8p(PTR2UINT addr)
{
    #ifdef CFG_RWTL
    int shift = (addr & 0x1) * 8;
    uint16_t *ptr = (uint16_t *)(addr / 2);
    return ((uint8_t)((*ptr >> shift) & 0xFF));
    #else
    return (*(uint8_t *)addr);
    #endif
}

/**
 ****************************************************************************************
 * @brief Write a 8 bits word.
 * @param[in] addr The address of the first byte of the 8 bits word.
 * @param[in] value The value to write.
 ****************************************************************************************
 */
__INLINE void co_write8p(PTR2UINT addr, uint8_t value)
{
    #ifdef CFG_RWTL
    int shift = (addr & 0x1) * 8;
    uint16_t *ptr = (uint16_t *)(addr / 2);
    *ptr =  (*ptr & ~(0xFF << shift)) | ((value & 0xFF) << shift);
    #else
    *(uint8_t *)addr = value;
    #endif
}

/**
 ****************************************************************************************
 * @brief Read a packed 16 bits word.
 * @param[in] ptr16 The address of the first byte of the 16 bits word.
 * @return The 16 bits value.
 ****************************************************************************************
 */
__INLINE uint16_t co_read16p(void const *ptr16)
{
    uint16_t value = ((uint8_t *)ptr16)[0] | ((uint8_t *)ptr16)[1] << 8;
    return value;
}

/**
 ****************************************************************************************
 * @brief Write a packed 16 bits word.
 * @param[in] addr The address of the first byte of the 16 bits word.
 * @param[in] value The value to write.
 ****************************************************************************************
 */
__INLINE void co_write16p(PTR2UINT addr, uint32_t value)
{
    #ifdef CFG_RWTL
    co_write8p(addr, value & 0xFF);
    co_write8p(addr + 1, ((value >> 8) & 0xFF));
    #else
    struct co_read16_struct
    {
        uint16_t val __PACKED16;
    } *ptr = (struct co_read16_struct*) addr;

    ptr->val = value;
    #endif
}

/**
 ****************************************************************************************
 * @brief Read a packed 24 bits word.
 * @param[in] addr The address of the first byte of the 24 bits word.
 * @return The 24 bits value, on a 32-bit variable.
 ****************************************************************************************
 */
__INLINE uint32_t co_read24p(PTR2UINT addr)
{
    return ((((uint32_t)co_read16p((void *)(addr + 1))) << 8) | co_read8p(addr));
}

/**
 ****************************************************************************************
 * @brief Write a packed 24 bits word.
 * @param[in] addr The address of the first byte of the 24 bits word.
 * @param[in] value The value to write.
 ****************************************************************************************
 */
__INLINE void co_write24p(PTR2UINT addr, uint32_t value)
{
    co_write8p(addr, value & 0xFF);
    co_write16p(addr + 1, ((value >> 8) & 0xFFFF));
}

/**
 ****************************************************************************************
 * @brief Read a packed 32 bits word.
 * @param[in] ptr32 The address of the first byte of the 32 bits word.
 * @return The 32 bits value.
 ****************************************************************************************
 */
__INLINE uint32_t co_read32p(void const *ptr32)
{
    uint16_t addr_l, addr_h;
    addr_l = co_read16p(ptr32);
    addr_h = co_read16p((uint8_t *)ptr32 + 2);
    return ((uint32_t)addr_l | (uint32_t)addr_h << 16);
}

/**
 ****************************************************************************************
 * @brief Write a packed 32 bits word.
 * @param[in] addr The address of the first byte of the 32 bits word.
 * @param[in] value The value to write.
 ****************************************************************************************
 */
__INLINE void co_write32p(PTR2UINT addr, uint32_t value)
{
    #ifdef CFG_RWTL
    co_write16p(addr, value & 0xFFFF);
    co_write16p(addr + 2, ((value >> 16) & 0xFFFF));
    #else
    struct co_read32_struct
    {
        uint32_t val __PACKED;
    } *ptr = (struct co_read32_struct*) addr;
    ptr->val = value;
    #endif
}

/**
 ****************************************************************************************
 * @brief Read a packed 64 bits word.
 * @param[in] addr The address of the first byte of the 64 bits word.
 * @return The 64 bits value.
 ****************************************************************************************
 */
__INLINE uint64_t co_read64p(PTR2UINT addr)
{
    #ifdef CFG_RWTL
    return ((((uint64_t)co_read32p(addr + 4)) << 32) | co_read32p(addr));
    #else
    struct co_read64_struct
    {
        uint64_t val __PACKED;
    } *ptr = (struct co_read64_struct*) addr;
    return ptr->val;
    #endif
}

/**
 ****************************************************************************************
 * @brief Write a packed 64 bits word.
 * @param[in] addr The address of the first byte of the 32 bits word.
 * @param[in] value The value to write.
 ****************************************************************************************
 */
__INLINE void co_write64p(PTR2UINT addr, uint64_t value)
{
    #ifdef CFG_RWTL
    co_write32p(addr, value & 0xFFFFFFFF);
    co_write16p(addr + 4, ((value >> 32) & 0xFFFFFFFF));
    #else
    struct co_read64_struct
    {
        uint64_t val __PACKED;
    } *ptr = (struct co_read64_struct*) addr;
    ptr->val = value;
    #endif
}

/**
 ****************************************************************************************
 * @brief Compare a packed byte buffer with a CPU byte array
 * @param[in] pkd The address of the packed buffer.
 * @param[in] ptr Pointer to the CPU byte array
 * @param[in] len Length to be compared
 *
 * @return true if the buffers are equal, false otherwise
 ****************************************************************************************
 */
__INLINE bool co_cmp8p(PTR2UINT pkd, uint8_t const *ptr, uint32_t len)
{
    while (len--)
    {
        if (co_read8p(pkd++) != (*ptr++ & 0xFF))
            return false;
    }
    return true;
}

/**
 ****************************************************************************************
 * @brief Copy and pack a byte array to another one
 * @param[in] dst The address of the first byte of the packed buffer in which the data has
 *                to be copied.
 * @param[in] src Pointer to the source buffer
 * @param[in] len Length to be copied
 ****************************************************************************************
 */
__INLINE void co_pack8p(PTR2UINT dst, uint8_t const *src, uint32_t len)
{
    while (len--)
    {
        co_write8p(dst++, *src++);
    }
}

/**
 ****************************************************************************************
 * @brief Copy and unpack a byte array to another one
 * @param[in] dst Pointer to the first byte of the unpacked buffer in which the data has
 *                to be copied.
 * @param[in] src Address of the packed source buffer
 * @param[in] len Length to be copied
 ****************************************************************************************
 */
__INLINE void co_unpack8p(uint8_t *dst, PTR2UINT src, uint32_t len)
{
    while (len--)
    {
        *dst++ = co_read8p(src++);
    }
}

/**
 ****************************************************************************************
 * @brief Copy a packed byte array to another packed byte array
 * @param[in] dst The address of the first byte of the packed buffer in which the data has
 *                to be copied.
 * @param[in] src Address of the source buffer
 * @param[in] len Length to be copied
 ****************************************************************************************
 */
__INLINE void co_copy8p(PTR2UINT dst, PTR2UINT src, uint32_t len)
{
    while (len--)
    {
        co_write8p(dst++, co_read8p(src++));
    }
}

/**
 ****************************************************************************************
 * @brief This function returns the value of bit field inside an array of bits,
 * represented as an array of bytes.
 * @param[in] array Array of bits
 * @param[in] lsb Position of the LSB of the field inside the array of bits
 * @param[in] width Width of the field
 * @return true if the specified bit is set, false otherwise
 ****************************************************************************************
 */
__INLINE uint8_t co_val_get(uint8_t const array[], int lsb, int width)
{
    int msb = lsb + width - 1;
    int l_byte_idx = lsb/8;
    int m_byte_idx = msb/8;
    uint8_t val;

    if (m_byte_idx == l_byte_idx)
    {
        uint8_t mask = CO_BIT(width) - 1;
        int shift = lsb % 8;
        val = (array[l_byte_idx] >> shift) & mask;
    }
    else
    {
        uint8_t l_bits_cnt = m_byte_idx * 8 - lsb;
        uint8_t l_mask = CO_BIT(l_bits_cnt) - 1;
        uint8_t m_mask = CO_BIT(width - l_bits_cnt) - 1;
        int l_shift = lsb % 8;
        val = (array[l_byte_idx] >> l_shift) & l_mask;
        val |= (array[m_byte_idx] & m_mask) << l_bits_cnt;
    }
    return (val);
}

/**
 ****************************************************************************************
 * @brief This function sets a value of a bit field inside an array of bits,
 * represented as an array of bytes.
 * @param[in] array Array of bits
 * @param[in] lsb Position of the LSB of the field inside the array of bits
 * @param[in] width Width of the field
 * @param[in] val Value to be set
 ****************************************************************************************
 */
__INLINE void co_val_set(uint8_t array[], int lsb, int width, uint8_t val)
{
    int msb = lsb + width - 1;
    int l_byte_idx = lsb/8;
    int m_byte_idx = msb/8;

    if (m_byte_idx == l_byte_idx)
    {
        uint8_t mask = CO_BIT(width) - 1;
        int shift = lsb % 8;
        array[l_byte_idx] &= ~(mask << shift);
        array[l_byte_idx] |= (val & mask) << shift;
    }
    else
    {
        uint8_t l_bits_cnt = m_byte_idx * 8 - lsb;
        uint8_t l_mask = CO_BIT(l_bits_cnt) - 1;
        uint8_t m_mask = CO_BIT(width - l_bits_cnt) - 1;
        int l_shift = lsb % 8;
        array[l_byte_idx] &= ~(l_mask << l_shift);
        array[m_byte_idx] &= ~m_mask;
        array[l_byte_idx] |= (val & l_mask) << l_shift;
        array[m_byte_idx] |= (val >> l_bits_cnt) & m_mask;
    }
}

/**
 ****************************************************************************************
 * @brief This function returns the status of a specific bit position inside an array of
 *        bits, represented as an array of bytes.
 * @param[in] array Array of bits to be checked
 * @param[in] pos Bit position to be checked
 * @return true if the specified bit is set, false otherwise
 ****************************************************************************************
 */
__INLINE bool co_bit_is_set(uint8_t const array[], int pos)
{
    return ((array[pos / 8] & CO_BIT(pos % 8)) != 0);
}

/**
 ****************************************************************************************
 * @brief This function returns the status of a specific bit position inside an array of
 *        bits, represented as an array of bytes.
 * @param[in] array Array of bits to be checked
 * @param[in] pos Bit position to be checked
 * @param[in] length Length of the array
 * @return true if the specified bit is set, false otherwise and false is the bit is
 * outside the range of the array
 ****************************************************************************************
 */
__INLINE bool co_bit_is_set_var(uint8_t* array, int pos, int length)
{
    if ((pos / 8) > (length - 1))
        return false;
    return co_bit_is_set(array, pos);
}

/**
 ****************************************************************************************
 * @brief This function sets a specific bit position inside an array of bits, represented
 * as an array of bytes.
 * @param[in] array Array of bits
 * @param[in] pos Bit position to be set
 ****************************************************************************************
 */
__INLINE void co_bit_set(uint8_t array[], uint8_t pos)
{
    array[pos / 8] |= CO_BIT(pos % 8);
}

/**
 ****************************************************************************************
 * @brief This function clears a specific bit position inside an array of bits,
 * represented as an array of bytes.
 * @param[in] array Array of bits
 * @param[in] pos Bit position to be cleared
 ****************************************************************************************
 */
__INLINE void co_bit_clr(uint8_t array[], uint8_t pos)
{
    array[pos / 8] &= ~CO_BIT(pos % 8);
}

/**
 ****************************************************************************************
 * Count number of bit set to 1 in a value with variable length
 *
 * @param[in] p_val Pointer to value
 * @param[in] size  Number of Bytes
 * @return Number of bit counted
 ****************************************************************************************
 */
__INLINE uint8_t co_bit_cnt(const uint8_t* p_val, uint8_t size)
{
    uint8_t nb_bit = 0;
    while(size-- > 0)
    {
        nb_bit += NB_ONE_BITS(*p_val);
        p_val++;
    }
    return (nb_bit);
}


#endif // _CO_UTILS_H_
