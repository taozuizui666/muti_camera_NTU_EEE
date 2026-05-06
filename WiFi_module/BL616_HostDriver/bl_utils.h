/**
 ******************************************************************************
 *  bl_utils.h
 *
 *  IPC utility function declarations
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

#ifndef _BL_IPC_UTILS_H_
#define _BL_IPC_UTILS_H_

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include "bl_lmac_types.h"

#define TRACE_MOD_DHCP      (1<<0)


#define TRACE_INFO      (1<<0)
#define TRACE_TX        (1<<1)
#define TRACE_RX        (1<<2)
#define TRACE_SOFTMAC   (1<<3)
#define TRACE_MSG       (1<<4)
#define TRACE_CHAN      (1<<5)
#define TRACE_KE        (1<<6)
#define TRACE_CON       (1<<7)

#ifdef CONFIG_BL_DBG
#define BL_DBG                                  printk
#define BL_DBG_MSG                              printk
#define BL_DBG_DATA                             printk
#define BL_TRACE(level, format, arg...)         printk(format, ##arg)

#define dbg_f                                   printk
#define dbg                                     printk
#define dbg_chan                                printk
#define dbg_ke                                  printk
#define dbg_con                                 printk
#elif defined(CONFIG_BL_DYN_DBG)
extern uint32_t bl_trace_dyn_level;
#define BL_DBG(format, arg...)                          \
        do {                                            \
            if(TRACE_INFO & bl_trace_dyn_level)         \
                printk(format, ##arg);                  \
        } while(0)
#define BL_DBG_MSG(format, arg...)                      \
        do {                                            \
            if(TRACE_MSG & bl_trace_dyn_level)          \
                printk(format, ##arg);                  \
        } while(0)
#define BL_DBG_DATA(format, arg...)                     \
        do {                                            \
            if(TRACE_TX & bl_trace_dyn_level)           \
                printk(format, ##arg);                  \
        } while(0)
#define BL_TRACE(level, format, arg...)                 \
        do {                                            \
            if(level & bl_trace_dyn_level)              \
                printk(format, ##arg);                  \
        } while(0)


#define dbg_f                                   printk
#define dbg(format, arg...)                             \
        do {                                            \
            if(TRACE_SOFTMAC & bl_trace_dyn_level)      \
                printk(format, ##arg);                  \
        } while(0)
#define dbg_chan(format, arg...)                        \
        do {                                            \
            if(TRACE_CHAN & bl_trace_dyn_level)         \
                printk(format, ##arg);                  \
        } while(0)
#define dbg_ke(format, arg...)                          \
        do {                                            \
            if(TRACE_KE & bl_trace_dyn_level)           \
                printk(format, ##arg);                  \
        } while(0)
#define dbg_con(format, arg...)                          \
        do {                                            \
            if(TRACE_CON & bl_trace_dyn_level)           \
                printk(format, ##arg);                  \
        } while(0)
#else
#define BL_DBG(a...)                        do {} while (0)
#define BL_DBG_DATA(a...)                   do {} while (0)
#define BL_DBG_MSG(a...)                    do {} while (0)
#define BL_TRACE(level, format, arg...)     do {} while (0)

#define dbg_f                               printk
#define dbg(fmt, ...)                       do {} while (0)
#define dbg_chan(fmt, ...)                  do {} while (0)
#define dbg_ke(fmt, ...)                    do {} while (0)
#define dbg_con(fmt, ...)                   do {} while (0)
#endif


#define BL_FN_ENTRY_STR ">>> %s()\n", __func__
#define BL_FN_EXIT_STR  "<<< %s()\n", __func__

#ifndef MIN
/** Find minimum value */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif /* MIN */

#ifndef MAX
/** Find maximum value */
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif /* MAX */



/* DHCP Message Types */
#define DHCP_DISCOVER    1
#define DHCP_OFFER       2
#define DHCP_REQUEST     3
#define DHCP_DECLINE     4
#define DHCP_ACK         5
#define DHCP_NAK         6
#define DHCP_RELEASE     7
#define DHCP_INFORM      8

/* DHCP Options */
#define DHCP_OPTION_PAD            0
#define DHCP_OPTION_SUBNET_MASK    1
#define DHCP_OPTION_ROUTER         3
#define DHCP_OPTION_DNS_SERVER     6
#define DHCP_OPTION_REQUESTED_IP   50
#define DHCP_OPTION_LEASE_TIME     51
#define DHCP_OPTION_MESSAGE_TYPE   53
#define DHCP_OPTION_SERVER_ID      54
#define DHCP_OPTION_PARAM_REQUEST  55
#define DHCP_OPTION_END           255

/* DHCP Options maximum length */
#define DHCP_OPTIONS_MAX_LEN       312

/* ARP constants */
#define ARP_HW_TYPE_ETHER        1   /* Ethernet hardware type */
#define ARP_PROTO_IP            0x0800  /* IP protocol */
#define ARP_OP_REQUEST          1   /* ARP request */
#define ARP_OP_REPLY            2   /* ARP reply */
#define ARP_OP_RREQUEST         3   /* RARP request */
#define ARP_OP_RREPLY          4   /* RARP reply */

/* ARP Hardware types */
#define ARP_HRD_ETHERNET       0x0001

/* ARP Protocol types */
#define ARP_PRO_IP             0x0800

/* ARP Header Structure */
struct arp_header {
    __be16  ar_hrd;      /* Format of hardware address */
    __be16  ar_pro;      /* Format of protocol address */
    __u8    ar_hln;      /* Length of hardware address */
    __u8    ar_pln;      /* Length of protocol address */
    __be16  ar_op;       /* ARP opcode (command) */
    __u8    ar_sha[6];    /* Sender hardware address */
    __be32  ar_sip;      /* Sender IP address */
    __u8    ar_tha[6];    /* Target hardware address */
    __be32  ar_tip;      /* Target IP address */
} __attribute__((packed));

/* Parsed ARP Information */
struct arp_info {
    __u16    hw_type;      /* Hardware type */
    __u16    proto_type;    /* Protocol type */
    __u8    hw_len;        /* Hardware address length */
    __u8    proto_len;     /* Protocol address length */
    __u16    opcode;        /* ARP operation code */
    __u8    sender_mac[6]; /* Sender MAC address */
    __be32  sender_ip;     /* Sender IP address (network byte order) */
    __u8    target_mac[6]; /* Target MAC address */
    __be32  target_ip;     /* Target IP address (network byte order) */
} __attribute__((packed));

/* DHCP constants for flag modification */
#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC_OFFSET    236
#define DHCP_BOOTP_FLAG_OFFSET 10
#define DHCP_MAGIC           0x63825363
#define DHCP_BOOTP_BROADCAST 0x8000

/* DHCP Header Structure */
struct dhcp_header {
    __u8    op;          /* Message op code / message type */
    __u8    htype;       /* Hardware address type */
    __u8    hlen;        /* Hardware address length */
    __u8    hops;        /* Hops */
    __be32  xid;         /* Transaction ID */
    __be16  secs;        /* Seconds since boot began */
    __be16  flags;       /* Flags */
    __be32  ciaddr;      /* Client IP address */
    __be32  yiaddr;      /* 'your' (client) IP address */
    __be32  siaddr;      /* IP address of next server */
    __be32  giaddr;      /* Relay agent IP address */
    __u8    chaddr[16];  /* Client hardware address */
    __u8    sname[64];   /* Optional server host name */
    __u8    file[128];   /* Boot file name */
    __u8    options[0];  /* Optional parameters field */
} __attribute__((packed));

/* DHCP Option Structure */
struct dhcp_option {
    __u8    code;
    __u8    length;
    __u8    data[0];
} __attribute__((packed));

/* Parsed DHCP Information */
struct dhcp_info {
    __u8    message_type;
    __be32  client_ip;
    __be32  your_ip;
    __be32  server_ip;
    __be32  subnet_mask;
    __be32  router;
    __be32  dns_server;
    __be32  lease_time;
    __u8    client_mac[6];
    __be32  transaction_id;
} __attribute__((packed));

extern uint32_t bl_trace_dyn_module;

/* Function Prototypes */
int parse_dhcp_packet(const struct sk_buff *skb, struct dhcp_info *info);
int extract_dhcp_options(const __u8 *options, int options_len, struct dhcp_info *info);
int validate_dhcp_packet(const struct dhcp_header *dhcp);
void print_dhcp_info(const struct dhcp_info *info);
int is_dhcp_packet(const struct sk_buff *skb);
int is_arp_packet(const struct sk_buff *skb);
int parse_arp_packet(const struct sk_buff *skb, struct arp_info *info);
void print_arp_info(const struct arp_info *info);


void bl_dump(uint8_t *data, uint16_t len);
void bl_dump_char(const void *buf, const uint16_t buf_len);


#endif /* _BL_IPC_UTILS_H_ */
