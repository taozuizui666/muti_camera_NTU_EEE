#ifndef _KE_EVENT_H_
#define _KE_EVENT_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
// for ke environment
#include "ke_env.h"
// for CO_BIT
#include "co_math.h"

/*
 * CONSTANTS
 ****************************************************************************************
 */
/** Offset of the different events in the event bit field.  0 is the highest priority and
 *  31 is the lowest.
 *  THESE ENUMS MUST NOT BE USED WITH ke_evt_xxx()! USE xxx_BIT BLOW.
 */
enum
{
    KE_EVT_RESET = 0,
    KE_EVT_MM_TIMER,
    KE_EVT_KE_TIMER,
    KE_EVT_MACIF_MSG,
    KE_EVT_KE_MESSAGE,
    KE_EVT_HW_IDLE,
    #if NX_BEACONING
    KE_EVT_PRIMARY_TBTT,
    KE_EVT_SECONDARY_TBTT,
    #endif
    KE_EVT_RXUPLOADED,
    #if NX_UMAC_PRESENT
    KE_EVT_RXUREADY,
    #endif
    KE_EVT_RXREADY,
    KE_EVT_TXFRAME_CFM,
    KE_EVT_MACIF_DATA_BUF_READY,
    KE_EVT_MACIF_MSG_SEND_DONE,
    #if NX_BEACONING
    KE_EVT_TXCFM_BCN,
    #endif
    KE_EVT_TXCFM_AC3,
    KE_EVT_TXCFM_AC2,
    KE_EVT_TXCFM_AC1,
    KE_EVT_TXCFM_AC0,
    KE_EVT_DELAYED_TXDESC,
    KE_EVT_MACIF_TXDESC,
    KE_EVT_MAX
};

#define KE_EVT_RESET_BIT          CO_BIT(31 - KE_EVT_RESET)          ///< Reset event
#define KE_EVT_MM_TIMER_BIT       CO_BIT(31 - KE_EVT_MM_TIMER)       ///< MM timer event
#define KE_EVT_KE_TIMER_BIT       CO_BIT(31 - KE_EVT_KE_TIMER)       ///< Kernel timer event
#define KE_EVT_KE_MESSAGE_BIT     CO_BIT(31 - KE_EVT_KE_MESSAGE)     ///< Kernel message event
#define KE_EVT_HW_IDLE_BIT        CO_BIT(31 - KE_EVT_HW_IDLE)        ///< IDLE state event
#if NX_BEACONING
#define KE_EVT_PRIMARY_TBTT_BIT   CO_BIT(31 - KE_EVT_PRIMARY_TBTT)   ///< Primary TBTT event
#define KE_EVT_SECONDARY_TBTT_BIT CO_BIT(31 - KE_EVT_SECONDARY_TBTT) ///< Secondary TBTT event
#else
#define KE_EVT_PRIMARY_TBTT_BIT   0
#endif
#if NX_UMAC_PRESENT
#define KE_EVT_RXUREADY_BIT       CO_BIT(31 - KE_EVT_RXUREADY)       ///< RXU ready event
#endif
#define KE_EVT_RXREADY_BIT        CO_BIT(31 - KE_EVT_RXREADY)        ///< RX ready event
#define KE_EVT_RXUPLOADED_BIT     CO_BIT(31 - KE_EVT_RXUPLOADED)     ///< RX DMA event
#define KE_EVT_TXFRAME_CFM_BIT    CO_BIT(31 - KE_EVT_TXFRAME_CFM)    ///< Internal frame confirmation event
#define KE_EVT_DELAYED_TXDESC_BIT CO_BIT(31 - KE_EVT_DELAYED_TXDESC) ///< Delayed tx data event
#define KE_EVT_MACIF_TXDESC_BIT   CO_BIT(31 - KE_EVT_MACIF_TXDESC)   ///< IPC tx data event
#define KE_EVT_MACIF_MSG_BIT      CO_BIT(31 - KE_EVT_MACIF_MSG)      ///< Application message event
#define KE_EVT_MACIF_DATA_BUF_READY_BIT    CO_BIT(31 - KE_EVT_MACIF_DATA_BUF_READY)   ///< Data buffer is ready for host downloading
#define KE_EVT_MACIF_MSG_SEND_DONE_BIT     CO_BIT(31 - KE_EVT_MACIF_MSG_SEND_DONE)    ///< Message has been sent to host event
#if NX_BEACONING
#define KE_EVT_TXCFM_BCN_BIT      CO_BIT(31 - KE_EVT_TXCFM_BCN)      ///< Tx confirmation event
#endif
#define KE_EVT_TXCFM_AC0_BIT      CO_BIT(31 - KE_EVT_TXCFM_AC0)      ///< Tx confirmation event
#define KE_EVT_TXCFM_AC1_BIT      CO_BIT(31 - KE_EVT_TXCFM_AC1)      ///< Tx confirmation event
#define KE_EVT_TXCFM_AC2_BIT      CO_BIT(31 - KE_EVT_TXCFM_AC2)      ///< Tx confirmation event
#define KE_EVT_TXCFM_AC3_BIT      CO_BIT(31 - KE_EVT_TXCFM_AC3)      ///< Tx confirmation event


/// Mask of the TX confirmation events
#if NX_BEACONING
#define KE_EVT_TXCFM_MASK   ( KE_EVT_TXCFM_AC0_BIT  \
                            | KE_EVT_TXCFM_AC1_BIT  \
                            | KE_EVT_TXCFM_AC2_BIT  \
                            | KE_EVT_TXCFM_AC3_BIT  \
                            | KE_EVT_TXCFM_BCN_BIT)
#else
#define KE_EVT_TXCFM_MASK   ( KE_EVT_TXCFM_AC0_BIT  \
                            | KE_EVT_TXCFM_AC1_BIT  \
                            | KE_EVT_TXCFM_AC2_BIT  \
                            | KE_EVT_TXCFM_AC3_BIT)
#endif

/*
 * MACROS
 ****************************************************************************************
 */
/// Retrieves the pending events bit field
__INLINE evt_field_t ke_evt_get(void)
{
    return ke_env.evt_field;
}

/*
 * FUNCTION PROTOTYPES
 ****************************************************************************************
 */
void ke_evt_only_set(evt_field_t const);
void ke_evt_only_clear(evt_field_t const);
void ke_evt_set(evt_field_t const);
void ke_evt_clear(evt_field_t const);
void ke_evt_schedule(void);
void ke_init(struct bl_hw *bl_hw);
void ke_flush(void);



#endif //_KE_EVENT_H_
