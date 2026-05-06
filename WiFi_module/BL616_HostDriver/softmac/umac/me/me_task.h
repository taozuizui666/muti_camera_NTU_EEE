#ifndef _ME_TASK_H_
#define _ME_TASK_H_

#include "ke_task.h"
#include "mac.h"

/// Task max index number.
#define ME_IDX_MAX 1

/// Possible states of the task.
enum
{
    /// Idle State.
    ME_IDLE,
    /// Busy State.
    ME_BUSY,
    /// Max number of states
    ME_STATE_MAX
};

/// Messages that are logically related to the task.
enum
{
    /*
     * Section of internal ME messages. No ME API messages should be defined below this point
     */
    /// Internal request to indicate that a VIF needs to get the HW going to ACTIVE or IDLE
    ME_SET_ACTIVE_REQ = ME_MAX,
    /// Confirmation that the switch to ACTIVE or IDLE has been executed
    ME_SET_ACTIVE_CFM,
    /// Internal request to indicate that a VIF desires to de-activate/activate the Power-Save mode
    ME_SET_PS_DISABLE_REQ,
    /// Confirmation that the PS state de-activate/activate has been executed
    ME_SET_PS_DISABLE_CFM,
    /// Indication that data path is flushed for a given station
    ME_DATA_PATH_FLUSHED_IND,
    ME_MISC_REQ,
    ME_MISC_CFM,
};

/// AMSDU TX values
enum amsdu_tx
{
    /// AMSDU configured as recommended by peer
    AMSDU_TX_ADV,
    /// AMSDU Enabled
    AMSDU_TX_EN,
    /// AMSDU Disabled
    AMSDU_TX_DIS,
};


/// Structure containing the parameters of the @ref ME_SET_ACTIVE_REQ message
struct me_set_active_req
{
    /// Boolean indicating whether the VIF requests the HW to be passed in ACTIVE or IDLE
    bool active;
    /// VIF index
    uint8_t vif_idx;
    uint8_t auth_type;
};

/// Structure containing the parameters of the @ref ME_SET_PS_DISABLE_REQ message
struct me_set_ps_disable_req
{
    /// Boolean indicating whether the VIF requests the PS to be disabled or not
    bool ps_disable;
    /// VIF index
    uint8_t vif_idx;
};

/// Structure containing the parameters of the @ref ME_DATA_PATH_FLUSHED_IND message.
struct me_data_path_flushed_ind
{
    /// Index of the station
    uint8_t sta_idx;
    /// General purpose user data provided at the end of the flushing process
    void *env;
};

struct me_misc_req
{
    uint8_t vif_idx;
};

extern const struct ke_state_handler me_default_handler;

extern ke_state_t me_state[ME_IDX_MAX];


#endif // _ME_TASK_H_



