#ifndef _KE_CONFIG_H_
#define _KE_CONFIG_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <linux/kernel.h>
#include <linux/string.h>

/*
 * CONSTANT DEFINITIONS
 ****************************************************************************************
 */

/// ARM is little endian
#define CPU_LE          1

/** @name Kernel configuration
 * @{
 * Defines if Kernel heap management is compiled or not
 */
#if (__SIZEOF_POINTER__ == 4)
#define CPU_WORD_SIZE   4
#elif (__SIZEOF_POINTER__ == 8)
#define CPU_WORD_SIZE   8
#else
#define CPU_WORD_SIZE   4
#endif


#define KE_MEM_NX       1
#define KE_MEM_LINUX    0
#define KE_MEM_LIBC     0

#define KE_PROFILING    0

/**
 * @brief Null Type definition
 */
#ifndef NULL
#define NULL 0
#endif

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
typedef signed char         int8_t;
typedef signed char         s8;
typedef short               s16;
typedef short               int16_t;
typedef int                 s32;
typedef int                 int32_t;
typedef long long           s64;

#ifndef __INLINE
#define __INLINE static __attribute__((__always_inline__)) inline
#endif


#endif // _KE_CONFIG_H_
