#ifndef _BL_NL_EVENTS_H_
#define _BL_NL_EVENTS_H_


/*****************************************************************************
 * Define for netlink event broadcast
 ****************************************************************************/

#define BL_NL_BCAST_GROUP_ID   1
#define BL_NL_SOCKET_NUM       31
#define BL_NL_BUF_MAX_LEN      1024

enum bl_event_id {
    BL_EVENT_ID_NONE           = 0x1000,
    BL_EVENT_ID_PROBE_RESPONSE = 0x1001,
    BL_EVENT_ID_SCAN_DONE      = 0x1002,
    
    BL_EVENT_ID_RESET          = 0x1003,    
    BL_EVENT_ID_OUT_MEM        = 0x1004,
    
    BL_EVENT_ID_HCI_MSG        = 0x1005,
};

struct bl_nl_event {
    u32 event_id;
    u32 payload_len;
    u8  payload[];
};

#endif
