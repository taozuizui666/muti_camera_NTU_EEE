#ifndef _SCANU_TASK_H_
#define _SCANU_TASK_H_

#include "ke_config.h"
#include "ke_task.h"
#include "mac.h"

#include "bl_lmac_mac.h"
#include "bl_lmac_msg.h"

/// Task max index number.
#define SCANU_IDX_MAX 1

/// Possible states of the task.
enum
{
    /// Idle State.
    SCANU_IDLE,
    /// SCANNING State.
    SCANU_SCANNING,
    /// Max number of states
    SCANU_STATE_MAX
};

/// Messages that are logically related to the task.
enum
{
    /// Get Scan result request.
    SCANU_GET_SCAN_RESULT_REQ = SCANU_MAX,
    /// Scan result confirmation.
    SCANU_GET_SCAN_RESULT_CFM,
};

/// Structure containing the parameters of the @ref SCANU_GET_SCAN_RESULT_REQ message
struct scanu_get_scan_result_req
{
    /// index of the scan element
    uint8_t idx;
};

/// Structure containing the parameters of the @ref SCANU_GET_SCAN_RESULT_CFM message
struct scanu_get_scan_result_cfm
{
    /// Structure for scan result element
    struct mac_scan_result scan_result;
};

extern const struct ke_state_handler scanu_state_handler[SCANU_STATE_MAX];

extern const struct ke_state_handler scanu_default_handler;

extern ke_state_t scanu_state[SCANU_IDX_MAX];

#endif // _SCANU_TASK_H_


