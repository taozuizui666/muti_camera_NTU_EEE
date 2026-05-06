/**
 ****************************************************************************************
 *
 *  @file ipc_shared.h
 *
 *  @brief Shared data between both IPC modules.
 *
 *  Copyright (C) BouffaloLab 2017-2023
 *
 *  Licensed under the Apache License, Version 2.0 (the License);
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an ASIS BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************
 */


#ifndef _IPC_SHARED_H_
#define _IPC_SHARED_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "bl_ipc_compat.h"
#include "bl_lmac_mac.h"

/*
 * DEFINES AND MACROS
 ****************************************************************************************
 */
#ifndef CO_BIT
#define CO_BIT(pos) (1U<<(pos))
#endif

#define IPC_TXQUEUE_CNT     NX_TXQ_CNT
#define NX_TXDESC_CNT0      8
#define NX_TXDESC_CNT1      64
#define NX_TXDESC_CNT2      64
#define NX_TXDESC_CNT3      32
#if NX_TXQ_CNT == 5
#define NX_TXDESC_CNT4      8
#else
#define NX_TXDESC_CNT4      0
#endif

/*
 * Number of Host buffers available for Radar events handling (through DMA)
 */
#define IPC_RADARBUF_CNT       16

/*
 * Length used in MSGs structures
 */
#define IPC_A2E_MSG_BUF_SIZE    127 // size in 4-byte words
#define IPC_E2A_MSG_SIZE_BASE   256 // size in 4-byte words

#ifdef CONFIG_BL_TL4
#define IPC_E2A_MSG_PARAM_SIZE  (IPC_E2A_MSG_SIZE_BASE + (IPC_E2A_MSG_SIZE_BASE / 2))
#else
#define IPC_E2A_MSG_PARAM_SIZE  IPC_E2A_MSG_SIZE_BASE
#endif

/*
 * Maximum number of payload addresses and lengths present in the descriptor
 */
#ifdef CONFIG_BL_SPLIT_TX_BUF
#define NX_TX_PAYLOAD_MAX      6
#else
#define NX_TX_PAYLOAD_MAX      1
#endif

/*
 ****************************************************************************************
 */
// c.f LMAC/src/tx/tx_swdesc.h
/// Descriptor filled by the Host
struct hostdesc
{
    /// Unique host identifier sent back in TX confirmation when txdesc has been processed
    u32_l hostid;
    /// Pointers to packet payloads
    u32_l packet_addr[NX_TX_PAYLOAD_MAX];
    /// Sizes of the MPDU/MSDU payloads
    u16_l packet_len[NX_TX_PAYLOAD_MAX];
#ifdef CONFIG_BL_SPLIT_TX_BUF
    /// Number of payloads forming the MPDU
    u8_l packet_cnt;
#endif //(NX_AMSDU_TX)

#ifdef CONFIG_BL_FULLMAC
    /// padding of the status descriptor in host memory (used for confirmation upload)
    u32_l host_tx_padding;
    /// Destination Address
    struct mac_addr eth_dest_addr;
    /// Source Address
    struct mac_addr eth_src_addr;
    /// Ethernet Type
    u16_l ethertype;
    /// Buffer containing the PN to be used for this packet
    u16_l pn[4];
    /// Sequence Number used for transmission of this MPDU
    u16_l sn;
    /// TX flags
    u16_l flags;
    ///FW Sequence number to use for the transmission. Host driver not use it when fw do retry. 
    u16_l dummy_sn_for_retry;
    /// Timestamp of first transmission of this MPDU
    u16_l timestamp;
#else /* ! CONFIG_BL_FULLMAC */
    /// Padding between the buffer control structure and the MPDU in host memory
    u8_l padding;
#endif /* CONFIG_BL_FULLMAC */
    /// Packet TID (0xFF if not a QoS frame)
    u8_l tid;
    /// Interface Id
    u8_l vif_idx;
    /// Station Id (0xFF if station is unknown)
    u8_l staid;
#ifdef CONFIG_BL_MUMIMO_TX
    /// MU-MIMO information (GroupId and User Position in the group) - The GroupId
    /// is located on bits 0-5 and the User Position on bits 6-7. The GroupId value is set
    /// to 63 if MU-MIMO shall not be used
    u8_l mumimo_info;
#endif /* CONFIG_BL_MUMIMO_TX */
};

/// Descriptor filled by the UMAC
struct umacdesc
{
#ifdef CONFIG_BL_AGG_TX
    ///First Sequence Number of the BlockAck window
    u16_l sn_win;
    /// Flags from UMAC (match tx_hd.macctrlinfo2 format)
    u32_l flags;
    /// PHY related flags field - rate, GI type, BW type - filled by driver
    u32_l phy_flags;
#endif //(CONFIG_BL_AGG_TX)
};

struct txdesc_api
{
    /// Information provided by Host
    struct hostdesc host;
};

/// Additional temporary control information passed from the host to the emb regarding
/// the TX descriptor
struct txdesc_ctrl
{
    /// HW queue in which the TX descriptor shall be pushed
    u32_l hwq;
};

/// Descriptor used for Host/Emb TX frame information exchange
struct txdesc_host
{
    /// API of the embedded part
    struct txdesc_api api;

    /// Additional control information about the descriptor
    struct txdesc_ctrl ctrl;

    /// Flag indicating if the TX descriptor is ready (different from 0) or not (equal to 0)
    /// Shall be the last element of the structure, i.e. downloaded at the end
    u32_l ready;
};

// Comes from la.h
/// Length of the configuration data of a logic analyzer
#define LA_CONF_LEN          10

/// Structure containing the configuration data of a logic analyzer
struct la_conf_tag
{
    u32_l conf[LA_CONF_LEN];
    u32_l trace_len;
    u32_l diag_conf;
};

/// Size of a logic analyzer memory
#define LA_MEM_LEN       (1024 * 1024)

/// Maximum length of the SW diag trace
#define DBG_SW_DIAG_MAX_LEN   1024

/// Maximum length of the error trace
#define DBG_ERROR_TRACE_SIZE  256

/// Number of MAC diagnostic port banks
#define DBG_DIAGS_MAC_MAX     48

/// Number of PHY diagnostic port banks
#define DBG_DIAGS_PHY_MAX     32

/// Maximum size of the RX header descriptor information in the debug dump
#define DBG_RHD_MEM_LEN      (5 * 1024)

/// Maximum size of the RX buffer descriptor information in the debug dump
#define DBG_RBD_MEM_LEN      (5 * 1024)

/// Maximum size of the TX header descriptor information in the debug dump
#define DBG_THD_MEM_LEN      (10 * 1024)

/// Structure containing the information about the PHY channel that is used
struct phy_channel_info
{
    /// PHY channel information 1
    u32_l info1;
    /// PHY channel information 2
    u32_l info2;
};

/// Debug information forwarded to host when an error occurs
struct dbg_debug_info_tag
{
    /// Type of error (0: recoverable, 1: fatal)
    u32_l error_type;
    /// Pointer to the first RX Header Descriptor chained to the MAC HW
    u32_l rhd;
    /// Size of the RX header descriptor buffer
    u32_l rhd_len;
    /// Pointer to the first RX Buffer Descriptor chained to the MAC HW
    u32_l rbd;
    /// Size of the RX buffer descriptor buffer
    u32_l rbd_len;
    /// Pointer to the first TX Header Descriptors chained to the MAC HW
    u32_l thd[NX_TXQ_CNT];
    /// Size of the TX header descriptor buffer
    u32_l thd_len[NX_TXQ_CNT];
    /// MAC HW diag configuration
    u32_l hw_diag;
    /// Error message
    u32_l error[DBG_ERROR_TRACE_SIZE/4];
    /// SW diag configuration length
    u32_l sw_diag_len;
    /// SW diag configuration
    u32_l sw_diag[DBG_SW_DIAG_MAX_LEN/4];
    /// PHY channel information
    struct phy_channel_info chan_info;
    /// Embedded LA configuration
    struct la_conf_tag la_conf;
    /// MAC diagnostic port state
    u16_l diags_mac[DBG_DIAGS_MAC_MAX];
    /// PHY diagnostic port state
    u16_l diags_phy[DBG_DIAGS_PHY_MAX];
    /// MAC HW RX Header descriptor pointer
    u32_l rhd_hw_ptr;
    /// MAC HW RX Buffer descriptor pointer
    u32_l rbd_hw_ptr;
};

/// Full debug dump that is forwarded to host in case of error
struct dbg_debug_dump_tag
{
    /// Debug information
    struct dbg_debug_info_tag dbg_info;

    /// RX header descriptor memory
    u32_l rhd_mem[DBG_RHD_MEM_LEN/4];

    /// RX buffer descriptor memory
    u32_l rbd_mem[DBG_RBD_MEM_LEN/4];

    /// TX header descriptor memory
    u32_l thd_mem[NX_TXQ_CNT][DBG_THD_MEM_LEN/4];

    /// Logic analyzer memory
    u32_l la_mem[LA_MEM_LEN/4];
};

#ifdef CONFIG_BL_RADAR
/// Number of pulses in a radar event structure
#define RADAR_PULSE_MAX   4

/// Definition of an array of radar pulses
struct radar_pulse_array_desc
{
    /// Buffer containing the radar pulses
    u32_l pulse[RADAR_PULSE_MAX];
    /// Index of the radar detection chain that detected those pulses
    u32_l idx;
    /// Number of valid pulses in the buffer
    u32_l cnt;
};

/// Bit mapping inside a radar pulse element
struct radar_pulse {
    s32_l freq:6; /** Freq (resolution is 2Mhz range is [-Fadc/4 .. Fadc/4]) */
    u32_l fom:4;  /** Figure of Merit */
    u32_l len:6;  /** Length of the current radar pulse (resolution is 2us) */
    u32_l rep:16; /** Time interval between the previous radar event
                      and the current one (in us) */
};
#endif

/// Message structure for MSGs from Emb to App
struct ipc_e2a_msg
{
    u16_l id;                ///< Message id.
    u16_l dummy_dest_id;
    u16_l dummy_src_id;
    u16_l param_len;         ///< Parameter embedded struct length.
    u32_l param[IPC_E2A_MSG_PARAM_SIZE];  ///< Parameter embedded struct. Must be word-aligned.
};


/*
 * TYPE and STRUCT DEFINITIONS
 ****************************************************************************************
 */
// Indexes are defined in the MIB shared structure
struct ipc_shared_env_tag
{
    #ifdef CONFIG_BL_RADAR
    volatile u32_l  radarbuf_hostbuf [IPC_RADARBUF_CNT]; // buffers @ for Radar Events
    #endif
    
    u32_l buffered[NX_REMOTE_STA_MAX][TID_MAX];

    volatile uint16_t trace_pattern;
};

extern struct ipc_shared_env_tag ipc_shared_env;


/*
 * TYPE and STRUCT DEFINITIONS
 ****************************************************************************************
 */


#endif // _IPC_SHARED_H_

