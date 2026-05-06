#ifndef _RM_TASK_H_
#define _RM_TASK_H_
#if NX_RM

#include "ke_task.h"

/// RM Task max index number.
#define RM_IDX_MAX 1

/// Possible states of the task.
enum rm_state_tag
{
    /// Idle State.
    RM_IDLE,
    /// One Radio measurement is in progress
    RM_MEASURING,
    /// Reports are being transmitted
    RM_REPORTING,
    /// Reports are being transmitted and other measures are pending
    RM_REPORTING_WHILE_MEASURING,
    /// Ready to process next request
    RM_READY_PROCESS,
    /// Max number of states
    RM_STATE_MAX
};

/// Messages that are logically related to the task.
enum rm_msg_tag
{
    /// Indication that RM task is ready to process next RM request
    /// Send by RM task to itself
    RM_PROCESS_NEXT_REQUEST_IND = KE_FIRST_MSG(TASK_RM),
};

extern const struct ke_state_handler rm_default_handler;
extern ke_state_t rm_state[RM_IDX_MAX];

#endif

#endif // _RM_TASK_H_


