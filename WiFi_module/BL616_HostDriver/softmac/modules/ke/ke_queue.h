#ifndef _KE_QUEUE_H_
#define _KE_QUEUE_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "ke_config.h"
#include "co_list.h"

/*
 * FUNCTION PROTOTYPES
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Push an element into a kernel queue
 *
 * @param[in] queue Pointer to the queue where the element is pushed
 * @param[in] element Pointer to the element to be pushed
 *
 ****************************************************************************************
 */
__INLINE void ke_queue_push(struct co_list *const queue,
                                   struct co_list_hdr *const element)
{
    co_list_push_back(queue, element);
}

/**
 ****************************************************************************************
 * @brief Pop an element from a kernel queue
 *
 * @param[in] queue Pointer to the queue where the element is popped from
 *
 * @return The pointer to the element
 *
 ****************************************************************************************
 */
__INLINE struct co_list_hdr *ke_queue_pop(struct co_list *const queue)
{
    return co_list_pop_front(queue);
}

struct co_list_hdr *ke_queue_extract(struct co_list * const queue,
                 bool (*func)(struct co_list_hdr const * elmt, uint32_t arg),
                 uint32_t arg);



#endif // _KE_QUEUE_H_
