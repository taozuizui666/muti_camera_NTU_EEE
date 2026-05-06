/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <linux/spinlock.h>
#include <linux/completion.h>

#include "rwnx_config.h"

#include "ke_event.h"
#include "ke_env.h"
#include "ke_mem.h"
//#include "mm_timer.h"

#include "softmac.h"

/*
 * TABLE OF EVENT HANDLERS
 ****************************************************************************************
 */
/// Format of an event handler function
typedef void (*evt_ptr_t)(int);

/// Structure defining an event callback
struct ke_evt_tag
{
    /// Pointer to the event function to call
    evt_ptr_t func;
    /// Parameter to pass to the event function
    int       param;
};

#include "ke_task.h"
#include "ke_timer.h"

/// Table of event handlers
static const struct ke_evt_tag ke_evt_hdlr[32] =
{
    [KE_EVT_KE_TIMER      ]     = {&ke_timer_schedule, 0},
    [KE_EVT_KE_MESSAGE    ]     = {&ke_task_schedule, 0},
};

/**
 ****************************************************************************************
 * @brief Set events
 *
 * This primitive sets one or more events in the event field variable. It will trigger
 * the call to the corresponding event handlers in the next scheduling call.
 *
 * @param[in]  event       Events that have to be set (bit field).
 *
 ****************************************************************************************
 */
void ke_evt_set(evt_field_t const event)
{
    unsigned long flags;
    
    spin_lock_irqsave(&ke_env.bl_hw->ke_queue_lock, flags);
    ke_env.evt_field |= event;
    spin_unlock_irqrestore(&ke_env.bl_hw->ke_queue_lock, flags);

    #ifndef CONFIG_KE_TASKLET
    complete(&ke_env.bl_hw->ke_wait);
    #else
    tasklet_schedule(&ke_env.bl_hw->ke_tasklet);
    #endif
}

void ke_evt_only_set(evt_field_t const event)
{
    ke_env.evt_field |= event;
}

/**
 ****************************************************************************************
 * @brief Clear events
 *
 * This primitive clears one or more events in the event field variable.
 *
 * @param[in]  event       Events that have to be cleared (bit field).
 *
 ****************************************************************************************
 */
void ke_evt_clear(evt_field_t const event)
{
    unsigned long flags;

    spin_lock_irqsave(&ke_env.bl_hw->ke_queue_lock, flags);
    ke_env.evt_field &= ~event;
    spin_unlock_irqrestore(&ke_env.bl_hw->ke_queue_lock, flags);
}

void ke_evt_only_clear(evt_field_t const event)
{
    ke_env.evt_field &= ~event;
}

/**
 ****************************************************************************************
 * @brief Event scheduler entry point.
 *
 * This primitive has to be called in the background loop in order to execute the event
 * handlers for the event that are set.
 *
 ****************************************************************************************
 */
void ke_evt_schedule(void)
{
    // Get the volatile value
    uint32_t field = ke_env.evt_field;

    while (field) // Compiler is assumed to optimize with loop inversion
    {
        // Find highest priority event set
        uint32_t event = co_clz(field);

        // Sanity check
        ASSERT_ERR((event < KE_EVT_MAX) && ke_evt_hdlr[event].func);

        // Execute corresponding handler
        (ke_evt_hdlr[event].func)(ke_evt_hdlr[event].param);

        // Update the volatile value
        field = ke_env.evt_field;
    }
}

/**
 ****************************************************************************************
 * @brief This function performs all the initializations of the kernel.
 *
 * It initializes first the heap, then the message queues and the events. Then if required
 * it initializes the trace.
 *
 ****************************************************************************************
 */
void ke_init(struct bl_hw *bl_hw)
{
    ke_env.bl_hw = bl_hw;    
    
    // ke_mem_init MUST be called first to be able to allocate memory right from start
    #if KE_MEM_NX
    ke_env.mblock_first = ke_mem_init();

    #if KE_PROFILING
    ke_env.max_heap_used = 0;
    ke_env.heap_used.first = NULL;
    ke_env.heap_used.last = NULL;
    #endif //KE_PROFILING
    #endif //KE_MEM_RW

    // initialize the kernel message queue, mandatory before any message can be transmitted
    ke_env.queue_saved.first = NULL;
    ke_env.queue_saved.last = NULL;
    ke_env.queue_sent.first = NULL;
    ke_env.queue_sent.last = NULL;
    ke_env.queue_timer.first = NULL;
    ke_env.queue_timer.last = NULL;
    
    // clears all possible pending events
    ke_evt_clear(0xFFFFFFFF);
}

/**
 ****************************************************************************************
 * @brief This function flushes all messages, timers and events currently pending in the
 * kernel.
 *
 ****************************************************************************************
 */
void ke_flush(void)
{
    // free all pending message(s)
    while(1)
    {
        struct ke_msg *msg = 
                (struct ke_msg*) ke_queue_pop(&ke_env.queue_sent);
                
        if(msg == NULL)
            break;
            
        ke_msg_free(msg);
    }
    // free all saved message(s)
    while(1)
    {
        struct ke_msg *msg = 
               (struct ke_msg*) ke_queue_pop(&ke_env.queue_saved);
               
        if(msg == NULL)
            break;
            
        ke_msg_free(msg);
    }
    // free all timers
    while(1)
    {
        struct ke_timer *timer = 
               (struct ke_timer*) ke_queue_pop(&ke_env.queue_timer);
               
        if(timer == NULL)
            break;
            
        ke_free(timer);
    }

    // clears all possible pending events
    ke_evt_clear(0xFFFFFFFF);
}


