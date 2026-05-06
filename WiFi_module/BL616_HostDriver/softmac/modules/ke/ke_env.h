#ifndef _KE_ENV_H_
#define _KE_ENV_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "rwnx_config.h"
#include "ke_config.h"
#include "co_list.h"

#include "softmac.h"

/*
 * DEFINES
 ****************************************************************************************
 */
// forward declaration
struct mblock_free;

/// Event bit field definition
typedef uint32_t evt_field_t;

/// Kernel environment definition
struct ke_env_tag
{
    /// Bit field indicating the events that are currently set
    volatile evt_field_t evt_field;

    /// Queue of sent messages but not yet delivered to receiver
    struct co_list queue_sent;
    /// Queue of messages delivered but not consumed by receiver
    struct co_list queue_saved;
    /// Queue of timers
    struct co_list queue_timer;

    #if KE_MEM_NX
    /// Root pointer = pointer to first element of linked list
    struct mblock_free * mblock_first;

    #if KE_PROFILING
    uint32_t max_heap_used;
    struct co_list heap_used;
    #endif //KE_PROFILING
    #endif //KE_MEM_NX

    struct bl_hw *bl_hw;
};

/// Kernel environment
extern struct ke_env_tag ke_env;

#endif // _KE_ENV_H_
