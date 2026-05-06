/**
 ******************************************************************************
 *  bl_utils.c
 *
 *  IPC utility function definitions
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/in.h>

#include "bl_utils.h"

/**
 * is_dhcp_packet - Check if the packet is a DHCP packet
 * @skb: socket buffer containing the packet
 * 
 * Returns: 1 if it's a DHCP packet, 0 otherwise
 */
int is_dhcp_packet(const struct sk_buff *skb)
{
    struct ethhdr *eth;
    struct iphdr *ip;
    struct udphdr *udp;
    
    if (!skb)
        return 0;
    
    /* Check if we have enough headers */
    if (skb->len < (sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr)))
        return 0;
    
    eth = eth_hdr(skb);
    if (!eth)
        return 0;
    
    /* Check if it's an IP packet */
    if (ntohs(eth->h_proto) != ETH_P_IP)
        return 0;
    
    ip = ip_hdr(skb);
    if (!ip)
        return 0;
    
    /* Check if it's UDP */
    if (ip->protocol != IPPROTO_UDP)
        return 0;
    
    /* Get UDP header */
    udp = (struct udphdr *)((__u8 *)ip + (ip->ihl * 4));
    if (!udp)
        return 0;
    
    /* Check if it's DHCP (ports 67/68) */
    if ((ntohs(udp->dest) == DHCP_SERVER_PORT) || 
        (ntohs(udp->source) == DHCP_SERVER_PORT) ||
        (ntohs(udp->dest) == DHCP_CLIENT_PORT) || 
        (ntohs(udp->source) == DHCP_CLIENT_PORT)) {
        return 1;
    }
    
    return 0;
}

/**
 * validate_dhcp_packet - Validate DHCP packet structure
 * @dhcp: pointer to DHCP header
 * 
 * Returns: 0 if valid, -1 if invalid
 */
int validate_dhcp_packet(const struct dhcp_header *dhcp)
{
    __u32 magic;
    
    if (!dhcp)
        return -1;
    
    /* Check magic cookie ( DHCP magic = 99.130.83.99 ) */
    magic = *((__u32 *)dhcp->options);
    if (ntohl(magic) != 0x63825363)
        return -1;
    
    /* Validate hardware type and length */
    if (dhcp->htype != 1)  /* Ethernet */
        return -1;
    
    if (dhcp->hlen != 6)   /* MAC address length */
        return -1;
    
    return 0;
}

/**
 * extract_dhcp_options - Extract and parse DHCP options
 * @options: pointer to options field
 * @options_len: length of options field
 * @info: structure to store parsed information
 * 
 * Returns: 0 on success, -1 on error
 */
int extract_dhcp_options(const __u8 *options, int options_len, struct dhcp_info *info)
{
    int pos = 4; /* Skip magic cookie */
    __u8 code, length;
    __u32 magic;
    
    if (!options || !info)
        return -1;
    
    /* Validate we have at least the magic cookie */
    if (options_len < 4) {
        printk(KERN_DEBUG "WiFi Driver: DHCP options too short for magic cookie: %d bytes\n", options_len);
        return -1;
    }
    
    /* Validate magic cookie */
    magic = *((__u32 *)options);
    if (ntohl(magic) != 0x63825363) {
        printk(KERN_DEBUG "WiFi Driver: DHCP invalid magic cookie: 0x%08x\n", ntohl(magic));
        return -1;
    }
    
    printk(KERN_DEBUG "WiFi Driver: DHCP options parsing - options_len:%d, magic:0x%08x\n", 
           options_len, ntohl(magic));
    
    while (pos < options_len - 2) {
        code = options[pos++];
        
        if (code == DHCP_OPTION_PAD) {
            continue;
        }
        
        if (code == DHCP_OPTION_END) {
            printk(KERN_DEBUG "WiFi Driver: DHCP option END found at pos:%d\n", pos);
            break;
        }
        
        if (pos >= options_len) {
            printk(KERN_DEBUG "WiFi Driver: DHCP option parsing overflow, pos:%d >= options_len:%d\n", pos, options_len);
            return -1;
        }
        
        length = options[pos++];
        
        if (pos + length > options_len) {
            printk(KERN_DEBUG "WiFi Driver: DHCP option length overflow, pos:%d + length:%d > options_len:%d\n", pos, length, options_len);
            return -1;
        }
        
        printk(KERN_DEBUG "WiFi Driver: DHCP option code:%d (0x%02x), length:%d, pos:%d\n", 
               code, code, length, pos);
        
        switch (code) {
        case DHCP_OPTION_MESSAGE_TYPE:
            if (length == 1)
                info->message_type = options[pos];
            break;
            
        case DHCP_OPTION_SUBNET_MASK:
            if (length == 4)
                memcpy(&info->subnet_mask, &options[pos], 4);
            break;
            
        case DHCP_OPTION_ROUTER:
            if (length >= 4)
                memcpy(&info->router, &options[pos], 4);
            break;
            
        case DHCP_OPTION_DNS_SERVER:
            if (length >= 4)
                memcpy(&info->dns_server, &options[pos], 4);
            break;
            
        case DHCP_OPTION_SERVER_ID:
            if (length == 4)
                memcpy(&info->server_ip, &options[pos], 4);
            break;
            
        case DHCP_OPTION_LEASE_TIME:
            if (length == 4)
                memcpy(&info->lease_time, &options[pos], 4);
            break;
            
        default:
            /* Skip unknown options */
            break;
        }
        
        pos += length;
    }
    
    return 0;
}

/**
 * parse_dhcp_packet - Parse DHCP packet and extract information
 * @skb: socket buffer containing the packet
 * @info: structure to store parsed DHCP information
 * 
 * Returns: 0 on success, -1 on error
 */
int parse_dhcp_packet(const struct sk_buff *skb, struct dhcp_info *info)
{
    struct ethhdr *eth;
    struct iphdr *ip;
    struct udphdr *udp;
    struct dhcp_header *dhcp;
    int dhcp_len;
    int options_len;
    
    if (!skb || !info)
        return -1;
    
    /* Clear the info structure */
    memset(info, 0, sizeof(struct dhcp_info));
    
    /* Get network headers */
    eth = eth_hdr(skb);
    if (!eth)
        return -1;
    
    ip = ip_hdr(skb);
    if (!ip)
        return -1;
    
    udp = (struct udphdr *)((__u8 *)ip + (ip->ihl * 4));
    if (!udp)
        return -1;
    
    /* Get DHCP header and check bounds */
    if (((__u8 *)udp + sizeof(struct udphdr) + sizeof(struct dhcp_header)) > 
        ((__u8 *)skb->data + skb->len))
        return -1;
        
    dhcp = (struct dhcp_header *)((__u8 *)udp + sizeof(struct udphdr));
    if (!dhcp)
        return -1;
    
    /* Calculate DHCP packet length */
    dhcp_len = ntohs(udp->len) - sizeof(struct udphdr);
    if (dhcp_len < sizeof(struct dhcp_header))
        return -1;
    
    /* Validate DHCP packet */
    if (validate_dhcp_packet(dhcp) != 0)
        return -1;
    
    /* Extract basic DHCP information */
    info->transaction_id = dhcp->xid;
    info->client_ip = dhcp->ciaddr;
    info->your_ip = dhcp->yiaddr;
    info->server_ip = dhcp->siaddr;
    
    /* Extract client MAC address */
    memcpy(info->client_mac, dhcp->chaddr, 6);
    
    /* Extract and parse DHCP options */
    options_len = dhcp_len - sizeof(struct dhcp_header);
    if (options_len > 0 && options_len <= DHCP_OPTIONS_MAX_LEN) {
        if (extract_dhcp_options(dhcp->options, options_len, info) != 0)
            return -1;
    }
    
    return 0;
}

/**
 * print_dhcp_info - Print parsed DHCP information
 * @info: structure containing parsed DHCP information
 */
void print_dhcp_info(const struct dhcp_info *info)
{
    if (!info)
        return;
    
    printk(KERN_INFO "=== DHCP Packet Information ===\n");
    printk(KERN_INFO "Message Type: %d\n", info->message_type);
    printk(KERN_INFO "Transaction ID: 0x%x\n", ntohl(info->transaction_id));
    printk(KERN_INFO "Client IP: %pI4\n", &info->client_ip);
    printk(KERN_INFO "Your IP: %pI4\n", &info->your_ip);
    printk(KERN_INFO "Server IP: %pI4\n", &info->server_ip);
    printk(KERN_INFO "Subnet Mask: %pI4\n", &info->subnet_mask);
    printk(KERN_INFO "Router: %pI4\n", &info->router);
    printk(KERN_INFO "DNS Server: %pI4\n", &info->dns_server);
    printk(KERN_INFO "Lease Time: %u seconds\n", ntohl(info->lease_time));
    printk(KERN_INFO "Client MAC: %pM\n", info->client_mac);
    
    /* Print message type description */
    switch (info->message_type) {
    case DHCP_DISCOVER:
        printk(KERN_INFO "Message: DHCP DISCOVER\n");
        break;
    case DHCP_OFFER:
        printk(KERN_INFO "Message: DHCP OFFER\n");
        break;
    case DHCP_REQUEST:
        printk(KERN_INFO "Message: DHCP REQUEST\n");
        break;
    case DHCP_DECLINE:
        printk(KERN_INFO "Message: DHCP DECLINE\n");
        break;
    case DHCP_ACK:
        printk(KERN_INFO "Message: DHCP ACK\n");
        break;
    case DHCP_NAK:
        printk(KERN_INFO "Message: DHCP NAK\n");
        break;
    case DHCP_RELEASE:
        printk(KERN_INFO "Message: DHCP RELEASE\n");
        break;
    case DHCP_INFORM:
        printk(KERN_INFO "Message: DHCP INFORM\n");
        break;
    default:
        printk(KERN_INFO "Message: UNKNOWN (%d)\n", info->message_type);
        break;
    }
    printk(KERN_INFO "===============================\n");
}

void bl_dump(uint8_t *data, uint16_t len)
{
    u8 dbg_dump_buf[1000] = {0};
    int pi = 0;
    u32 dbg_len = 0;

    while (pi < len) {
        dbg_len = 0;
        
        while (pi < len && dbg_len < sizeof(dbg_dump_buf)-4) {
            dbg_len += sprintf(dbg_dump_buf+dbg_len, "0x%02x, ", data[pi]);
            pi++;

            //if (pi%4 == 0)
            //    dbg_len += sprintf(dbg_dump_buf+dbg_len, " ");
                
            if (pi%16 == 0)
                break;
        }
        
        dbg_dump_buf[dbg_len] = '\0';
        printk("%s\n", dbg_dump_buf);
    }
}

void bl_dump_char(const void *buf, const uint16_t buf_len)
{    
    unsigned long i = 0;

    for (; i < buf_len; i++) {
        printk("%c", ((uint8_t *)buf)[i]);
    }
    printk("\n");
}

/**
 * is_arp_packet - Check if the packet is an ARP packet
 * @skb: socket buffer containing the packet
 * 
 * Returns: 1 if it's an ARP packet, 0 otherwise
 */
int is_arp_packet(const struct sk_buff *skb)
{
    struct ethhdr *eth;
    
    if (!skb)
        return 0;
    
    /* Check if we have enough headers */
    if (skb->len < (sizeof(struct ethhdr) + sizeof(struct arp_header)))
        return 0;
    
    /* Use direct data access for consistency */
    eth = (struct ethhdr *)skb->data;
    if (!eth)
        return 0;
    
    /* Check if it's an ARP packet */
    if (ntohs(eth->h_proto) == ETH_P_ARP)
        return 1;
    
    return 0;
}

/**
 * parse_arp_packet - Parse ARP packet and extract information
 * @skb: socket buffer containing the packet
 * @info: structure to store parsed ARP information
 * 
 * Returns: 0 on success, -1 on error
 */
int parse_arp_packet(const struct sk_buff *skb, struct arp_info *info)
{
    struct ethhdr *eth;
    struct arp_header *arp;
    
    if (!skb || !info)
        return -1;
    
    /* Clear info structure */
    memset(info, 0, sizeof(struct arp_info));
    
    /* Get Ethernet header - use direct data access for consistency with bl_skb_parsing */
    eth = (struct ethhdr *)skb->data;
    if (!eth)
        return -1;
    
    /* Get ARP header and check bounds */
    if (((__u8 *)eth + sizeof(struct ethhdr) + sizeof(struct arp_header)) > 
        ((__u8 *)skb->data + skb->len))
        return -1;
        
    arp = (struct arp_header *)((__u8 *)eth + sizeof(struct ethhdr));
    if (!arp)
        return -1;
    
    /* Extract ARP information */
    info->hw_type = ntohs(arp->ar_hrd);
    info->proto_type = ntohs(arp->ar_pro);
    info->hw_len = arp->ar_hln;
    info->proto_len = arp->ar_pln;
    info->opcode = ntohs(arp->ar_op);
    
    /* Extract MAC addresses */
    memcpy(info->sender_mac, arp->ar_sha, 6);
    memcpy(info->target_mac, arp->ar_tha, 6);
    
    /* Extract IP addresses - keep in network byte order for %pI4 format */
    info->sender_ip = arp->ar_sip;
    info->target_ip = arp->ar_tip;
    
    /* Add debug information */
    printk(KERN_DEBUG "WiFi Driver: ARP Parse - hw_type:%d, proto_type:0x%04x, hw_len:%d, proto_len:%d, opcode:%d\n",
           info->hw_type, info->proto_type, info->hw_len, info->proto_len, info->opcode);
    printk(KERN_DEBUG "WiFi Driver: ARP Parse - skb_len:%d, eth_proto:0x%04x\n",
           skb->len, ntohs(eth->h_proto));
    
    return 0;
}

/**
 * print_arp_info - Print parsed ARP information
 * @info: structure containing parsed ARP information
 */
void print_arp_info(const struct arp_info *info)
{
    if (!info)
        return;
    
    printk(KERN_INFO "=== ARP/RARP Packet Information ===\n");
    printk(KERN_INFO "Hardware Type: %d", info->hw_type);
    if (info->hw_type == ARP_HRD_ETHERNET)
        printk(" (Ethernet)");
    printk("\n");
    
    printk(KERN_INFO "Protocol Type: 0x%04x", info->proto_type);
    if (info->proto_type == ARP_PRO_IP)
        printk(" (IP)");
    printk("\n");
    
    printk(KERN_INFO "Hardware Addr Len: %d\n", info->hw_len);
    printk(KERN_INFO "Protocol Addr Len: %d\n", info->proto_len);
    
    printk(KERN_INFO "Operation: %d", info->opcode);
    switch (info->opcode) {
    case ARP_OP_REQUEST:
        printk(" (ARP Request)");
        break;
    case ARP_OP_REPLY:
        printk(" (ARP Reply)");
        break;
    case ARP_OP_RREQUEST:
        printk(" (RARP Request)");
        break;
    case ARP_OP_RREPLY:
        printk(" (RARP Reply)");
        break;
    default:
        printk(" (Unknown)");
        break;
    }
    printk("\n");
    
    printk(KERN_INFO "Sender MAC: %pM\n", info->sender_mac);
    printk(KERN_INFO "Sender IP: %pI4\n", &info->sender_ip);
    printk(KERN_INFO "Target MAC: %pM\n", info->target_mac);
    printk(KERN_INFO "Target IP: %pI4\n", &info->target_ip);
    printk(KERN_INFO "================================\n");
}
