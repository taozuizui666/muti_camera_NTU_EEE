#ifndef _RWNX_CONFIG_H_
#define _RWNX_CONFIG_H_

#include "bl_utils.h"

//FW define like this
#define RW_WAPI_EN          0
#define NX_MAC_HE           1
#define NX_RX_RING          1
#define NX_11AX_DRAFT_2_0   0

#define NX_ANT_DIV          0
#define NX_P2P              0
#define NX_P2P_GO           0
#define NX_HE               1
#define NX_UMAC_PRESENT     1
#define NX_BEACONING        1
#define NX_HEAP_SIZE        (2048*10 + 256*2 + 64*4)
#define NX_POWERSAVE        0

#define NX_REMOTE_STA_MAX   4
#define NX_VIRT_DEV_MAX     2
#define NX_FULLY_HOSTED     1
#define NX_VHT              1
#define CFG_MFP
#define NX_BEACONING        1

#define NX_RM               1
#define NX_TDLS             0
#define NX_UAPSD            0

#if NX_UAPSD
#undef NX_POWERSAVE
#define NX_POWERSAVE        1
#endif

#ifdef CFG_FTM_INIT
  #define NX_FTM_INITIATOR  1
#else
  #define NX_FTM_INITIATOR  0
  #undef CFG_FTM_RSP
#endif

#ifdef CFG_FTM_RSP
  #define NX_FAKE_FTM_RSP   1
#else
  #define NX_FAKE_FTM_RSP   0
#endif

#define NX_DEBUG            0

#define CFG_5G              0

#define RW_MESH_EN          0
#define RW_BFMEE_EN         1

#if NX_HE
#undef CFG_MFP
#define CFG_MFP
#endif

#if NX_UMAC_PRESENT
#define NX_VHT              1
#undef CFG_MFP
#define CFG_MFP
#endif

#if NX_UMAC_PRESENT && defined CFG_MFP
#define NX_MFP              1
#else
#define NX_MFP              0
#endif

#define NX_TXFRAME_LEN      384

#define FIX_WFA_MBO_5_2_1   0

#define CFG_BBP_CTRL        1
#define BL_RA_EN            1


#endif

