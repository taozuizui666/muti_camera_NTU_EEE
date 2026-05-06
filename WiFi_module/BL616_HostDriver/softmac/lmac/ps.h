#ifndef _PS_H_
#define _PS_H_

#include "co_bit.h"
#include "ke_env.h"

#if (NX_POWERSAVE)
// Definition of bits preventing from going to sleep (per VIF)
/// Station is waiting for beacon reception
#define PS_VIF_WAITING_BCN         CO_BIT(0)
/// Station is waiting for broadcast/multicast traffic from AP
#define PS_VIF_WAITING_BCMC        CO_BIT(1)
/// Station is waiting for unicast traffic from AP
#define PS_VIF_WAITING_UC          CO_BIT(2)
/// Station is waiting for WMM-PS end of service period
#define PS_VIF_WAITING_EOSP        CO_BIT(3)
/// Station is waiting for the end of the association procedure
#define PS_VIF_ASSOCIATING         CO_BIT(4)
/// P2P GO is supposed to be present
#define PS_VIF_P2P_GO_PRESENT      CO_BIT(5)
/// P2P GO is waiting for TBTT interrupt
#define PS_VIF_P2P_WAIT_TBTT       CO_BIT(6)
/// VIF is configured for monitoring
#define PS_VIF_MONITOR             CO_BIT(7)
/// Waiting for TWT SP to end
#define PS_VIF_TWT_SP_ACTIVE       CO_BIT(8)

// Definition of bits preventing from going to sleep (global)
/// Upload of TX confirmations is ongoing
#define PS_TX_CFM_UPLOADING        CO_BIT(0)
/// A scanning process is ongoing
#define PS_SCAN_ONGOING            CO_BIT(1)
/// A request for going to IDLE is pending
#define PS_IDLE_REQ_PENDING        CO_BIT(2)
/// PSM is paused in order to allow data traffic
#define PS_PSM_PAUSED              CO_BIT(3)
/// A CAC period is active
#define PS_CAC_STARTED             CO_BIT(4)

/*
 * FUNCTION PROTOTYPES
 ****************************************************************************************
 */
#if NX_UAPSD
/**
 ****************************************************************************************
 * @brief Checks UAPSD status
 *
 * @return whether uapsd is enabled or not
 ****************************************************************************************
 */
__INLINE bool ps_uapsd_enabled(void)
{
    return (ke_env.bl_hw->mod_params->uapsd_timeout != 0);
}
#endif //#if NX_UAPSD
#endif //#if (NX_POWERSAVE)

#endif

