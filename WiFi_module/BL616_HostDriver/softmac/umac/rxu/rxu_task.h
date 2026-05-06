#ifndef _RXU_TASK_H_
#define _RXU_TASK_H_

// for mac_addr and other MAC definitions
#include "hal_mac_desc.h"
#include "ke_task.h"
#include "mac.h"

/// Parameters of the @ref RXU_MGT_IND message
struct rxu_mgt_ind
{
    /// Length of the frame
    uint16_t length;
    /// Frame control field of the frame.
    uint16_t framectrl;
    /// Center frequency on which we received the packet
    uint16_t center_freq;
    /// PHY band
    uint8_t band;
    /// Index of the station that sent the frame. 0xFF if unknown.
    uint8_t sta_idx;
    /// Index of the VIF that received the frame. 0xFF if unknown.
    uint8_t inst_nbr;
    /// RSSI of the received frame.
    int8_t rssi;
    /// Rx frame legacy information
    struct rx_leg_info rx_leg_inf;
    uint64_t tsf;
    uint8_t antenna_set;
    /// Frame payload.
    uint32_t payload[];
};


#endif // _RXU_TASK_H_

