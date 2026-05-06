#ifndef _KE_MEM_H_
#define _KE_MEM_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "ke_config.h"

#if KE_MEM_NX

// forward declarations
struct mblock_free;

/**
 ****************************************************************************************
 * @brief Heap initialization.
 *
 * This function performs the following operations:
 * - sanity checks
 * - check memory allocated is at least large enough to hold two block descriptors to hold
 * start and end
 * - initialize the first and last descriptors
 * - save the pointer to the first free descriptor
 *
 * @return The pointer to the first free block.
 *
 ****************************************************************************************
 */
struct mblock_free *ke_mem_init(void);
void ke_mem_deinit(void);

/**
 ****************************************************************************************
 * @brief Allocation of a block of memory.
 *
 * Allocates a memory block whose size is size; if no memory is available return
 * NULL
 *
 * @param[in] size Size of the memory area that need to be allocated.
 *
 * @return A pointer to the allocated memory area.
 *
 ****************************************************************************************
 */
void *ke_malloc(uint32_t size);

void *ke_malloc_check(uint32_t size);

/**
 ****************************************************************************************
 * @brief Freeing of a block of memory.
 *
 * Free the memory area pointed by mem_ptr : mark the block as free and insert it in
 * the pool of free block.
 *
 * @param[in] mem_ptr Pointer to the memory area that need to be freed.
 *
 ****************************************************************************************
 */
void ke_free(void *mem_ptr);


#elif KE_MEM_LINUX
// Wrappers to Linux mem functions here

#include <linux/slab.h>

__INLINE void *ke_malloc(uint32_t size)
{
    return kmalloc(size, GFP_KERNEL);
}

__INLINE void ke_free (void * mem_ptr)
{
    kfree(mem_ptr);
}



#elif KE_MEM_LIBC
// Wrapper to lib C mem functions here
//#include <stdlib.h>

__INLINE void *ke_malloc(uint32_t size) { return malloc(size); }

__INLINE void ke_free(void * mem_ptr) { free(mem_ptr); }

#endif

#endif // _KE_MEM_H_

