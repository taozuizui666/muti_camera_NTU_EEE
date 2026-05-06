#ifndef _APM_TASK_H_
#define _APM_TASK_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "rwnx_config.h"
#if NX_BEACONING
#include "ke_task.h"
#include "apm.h"

/// Task max index number.
#define APM_IDX_MAX 1

/// Possible states of the task.
enum apm_state_tag
{
    /// IDLE State.
    APM_IDLE,
    /// Waiting for BSS parameter setting
    APM_BSS_PARAM_SETTING,
    /// Waiting for the beacon to be set to LMAC
    APM_BCN_SETTING,
    /// Waiting for the AP to be stopped
    APM_STOPPING,
    /// Number of states.
    APM_STATE_MAX
};

extern const struct ke_state_handler apm_default_handler;

extern ke_state_t apm_state[APM_IDX_MAX];

#endif

#endif // _APM_TASK_H_
