#ifndef _MM_TASK_H_
#define _MM_TASK_H_


#define MM_IDX_MAX 1

/// Possible States of the MM STA Task.
enum mm_state_tag
{
    /// MAC IDLE State.
    MM_IDLE,
    /// MAC ACTIVE State.
    MM_ACTIVE,
    /// MAC is going to IDLE
    MM_GOING_TO_IDLE,
    /// IDLE state internally controlled
    MM_HOST_BYPASSED,
    /// IDLE state temporarily disallowed
    MM_NO_IDLE,
    /// MAC Max Number of states.
    MM_STATE_MAX
};

/// List of messages related to the task.
enum softmac_mm_msg_tag
{
    /*
     * Section of internal MM messages. No MM API messages should be defined below this point
     */
    /// Internal request to force the HW going to IDLE
    MM_FORCE_IDLE_REQ = MM_MAX,
    /// Message indicating that the switch to the scan channel is done
    MM_SCAN_CHANNEL_START_IND,
    /// Message indicating that the scan on the channel is finished
    MM_SCAN_CHANNEL_END_IND,
    /// Internal request to move the AP TBTT by an offset
    MM_TBTT_MOVE_REQ,

    #if BL_RA_EN
    /// Timer for link statistics monitor
    MM_LINK_TIMER_IND,
    #endif

    /// Timer id of MM task common scheduler timer.
    MM_COMMON_SCHED_TIMER,

    /// MAX number of messages
    //MM_MAX,
};


#endif

