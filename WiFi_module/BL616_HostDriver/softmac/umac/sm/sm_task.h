#ifndef _SM_TASK_H_
#define _SM_TASK_H_

#include "sm.h"
#include "ke_task.h"

/// Task max index number.
#define SM_IDX_MAX 1

/// SM task possible states.
enum sm_state_tag
{
    /// IDLE state
    SM_IDLE,
    /// SCANNING state
    SM_SCANNING,
    /// JOIN state
    SM_JOINING,
    /// Addition of station
    SM_STA_ADDING,
    /// Configuration of BSS parameters
    SM_BSS_PARAM_SETTING,
    /// AUTHENTICATE state
    SM_AUTHENTICATING,
    /// EXTERNAL AUTHENTICATE state
    SM_EXTERNAL_AUTHENTICATING,
    /// Transitional state between SM_AUTHENTICATING and SM_ASSOCIATING during FT over air
    SM_FT_OVER_AIR,
    /// ASSOCIATE state
    SM_ASSOCIATING,
    /// ACTIVATE state
    SM_ACTIVATING,
    /// DISCONNECTING state
    SM_DISCONNECTING,
    /// Number of states
    SM_STATE_MAX
};


#define SM_RSP_TIMEOUT_IND   SM_MAX


/// Default state handler of the SM task.
extern const struct ke_state_handler sm_default_handler;

/// Table including the state of each instance of the SM task.
extern ke_state_t sm_state[SM_IDX_MAX];


#endif // _SM_TASK_H_
