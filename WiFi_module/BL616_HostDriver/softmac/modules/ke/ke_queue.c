/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "ke_config.h"
#include "ke_queue.h"
#include "ke_mem.h"

/**
 ****************************************************************************************
 * @brief Extracts an element matching a given algorithm.
 *
 * @param[in]  queue    Pointer to the queue.
 * @param[in]  func     Matching function.
 * @param[in]  arg      Match argument.
 *
 * @return              Pointer to the element found and removed (NULL otherwise).
 ****************************************************************************************
 */
struct co_list_hdr *ke_queue_extract(struct co_list * const queue,
                    bool (*func)(struct co_list_hdr const * elmt, uint32_t arg),
                    uint32_t arg)
{
    struct co_list_hdr *prev = NULL;
    struct co_list_hdr *element  = queue->first;

    while (element)
    {
        if (func(element, arg))
        {
            if (prev)
            {
                // second or more
                prev->next = element->next;
            }
            else
            {
                // first message
                queue->first = element->next;
            }

            if (element->next)
            {
                // not the last
                element->next = NULL;
            }
            else
            {
                // last message
                queue->last = prev;
            }

            break;
        }

        prev = element;
        element = element->next;
    }

    #if 0 //KE_PROFILING
    if(element != NULL)
    {
        queue->cnt--;
        if(queue->mincnt > queue->cnt)
        {
            queue->mincnt = queue->cnt;
        }
    }
    #endif //KE_PROFILING

    return element;
}


