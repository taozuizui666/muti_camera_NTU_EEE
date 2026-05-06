#ifndef _KE_TIMER_H_
#define _KE_TIMER_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "ke_queue.h"
#include "ke_msg.h"

/*
 * DEFINES
 ****************************************************************************************
 */
/// Timers can be set up to 300s in the future
#define KE_TIMER_DELAY_MAX      300000000

/// Standard Multiplier
#define MILLI2MICRO             1000

/*
 * TYPE DEFINITIONS
 ****************************************************************************************
 */

/// Timer Object
struct ke_timer
{
    /// Pointer to the next timer element in the list
    struct ke_timer *next;
    /// Message identifier of the timer
    ke_msg_id_t     id;
    /// Identifier of the task that programmed the timer
    ke_task_id_t    task;
    /// Expiration time of the timer, abs time, jiffies64
    uint64_t        time;
    //us, delta
    uint32_t        delay;
};

/*
 * FUNCTION PROTOTYPES
 ****************************************************************************************
 */

#ifdef CFG_RWTL
extern uint32_t tl_diff;
#endif

void ke_timer_set(ke_msg_id_t const timer_id, ke_task_id_t const task,
                      uint32_t const delay);
void ke_timer_clear(ke_msg_id_t const timerid, ke_task_id_t const task);
void ke_timer_schedule(int dummy);
bool ke_timer_active(ke_msg_id_t const timer_id, ke_task_id_t const task_id);
void ke_timer_reset(void);


#endif // _KE_TIMER_H_
