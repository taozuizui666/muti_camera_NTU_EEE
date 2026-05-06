/**
 ******************************************************************************
 *
 *  @file bl_rx.c
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

#include <linux/dma-mapping.h>
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <net/ieee80211_radiotap.h>

#include "bl_defs.h"
#include "bl_rx.h"
#include "bl_tx.h"
#include "bl_ipc_host.h"
#include "bl_compat.h"
#include "bl_irqs.h"

#ifdef CONFIG_BL_SDIO
#include "bl_sdio.h"
#endif
#ifdef CONFIG_BL_USB
#include "bl_usb.h"
#endif

#include "softmac.h"

/**
 * bl_rx_get_vif - Return pointer to the destination vif
 *
 * @bl_hw: main driver data
 * @vif_idx: vif index present in rx descriptor
 *
 * Select the vif that should receive this frame. Returns NULL if the destination
 * vif is not active or vif is not specified in the descriptor.
 */
static inline
struct bl_vif *bl_rx_get_vif(struct bl_hw *bl_hw, int vif_idx)
{
    struct bl_vif *bl_vif = NULL;

    if (vif_idx == BL_INVALID_VIF) {
        list_for_each_entry(bl_vif, &bl_hw->vifs, list) {
            if (bl_vif->up)
                return bl_vif;
        }
        
        return NULL;
    } else if (vif_idx < NX_VIRT_DEV_MAX) {
        bl_vif = bl_hw->vif_table[vif_idx];
        if (!bl_vif || !bl_vif->up)
            return NULL;
    }

    return bl_vif;
}

/**
 * bl_rx_statistic - save some statistics about received frames
 *
 * @bl_hw: main driver data.
 * @hw_rxhdr: Rx Hardware descriptor of the received frame.
 * @sta: STA that sent the frame.
 */
static void bl_rx_statistic(struct bl_hw *bl_hw, struct hw_rxhdr *hw_rxhdr,
                              struct bl_sta *sta)
{
    struct bl_stats *stats = &bl_hw->stats;
#ifdef CONFIG_BL_DEBUGFS
    struct bl_rx_rate_stats *rate_stats = &sta->stats.rx_rate;
    struct rx_vector_1 *rxvect = &hw_rxhdr->hwvect.rx_vect1;
    int mpdu, ampdu, mpdu_prev, rate_idx;

    /* update ampdu rx stats */
    mpdu = hw_rxhdr->hwvect.mpdu_cnt;
    ampdu = hw_rxhdr->hwvect.ampdu_cnt;
    mpdu_prev = stats->ampdus_rx_map[ampdu];

    if (mpdu_prev < mpdu ) {
        stats->ampdus_rx_miss += mpdu - mpdu_prev - 1;
    } else {
        stats->ampdus_rx[mpdu_prev]++;
    }
    stats->ampdus_rx_map[ampdu] = mpdu;

    /* update rx rate statistic */
    if (!rate_stats->size)
        return;

    if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM) {
        int mcs;
        int bw = rxvect->ch_bw;
        int sgi;
        int nss;
        
        switch (rxvect->format_mod) {
            case FORMATMOD_HT_MF:
            case FORMATMOD_HT_GF:
                mcs = rxvect->ht.mcs % 8;
                nss = rxvect->ht.mcs / 8;
                sgi = rxvect->ht.short_gi;
                rate_idx = N_CCK + N_OFDM + nss * 32 + mcs * 4 +  
                             bw * 2 + sgi;
                break;
            case FORMATMOD_VHT:
                mcs = rxvect->vht.mcs;
                nss = rxvect->vht.nss;
                sgi = rxvect->vht.short_gi;
                rate_idx = N_CCK + N_OFDM + N_HT + nss * 80 + 
                             mcs * 8 + bw * 2 + sgi;
                break;
            case FORMATMOD_HE_SU:
                mcs = rxvect->he.mcs;
                nss = rxvect->he.nss;
                sgi = rxvect->he.gi_type;
                rate_idx = N_CCK + N_OFDM + N_HT + N_VHT + 
                             nss * 144 + mcs * 12 + bw * 3 + sgi;
                break;
            default:
                mcs = rxvect->he.mcs;
                nss = rxvect->he.nss;
                sgi = rxvect->he.gi_type;
                rate_idx = N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU
                    + nss * 216 + mcs * 18 + rxvect->he.ru_size * 3 + sgi;
                break;
        }
    } else {
        int idx = legrates_lut[rxvect->leg_rate].idx;
        
        if (idx < 4) {
            rate_idx = idx * 2 + rxvect->pre_type;
        } else {
            rate_idx = N_CCK + idx - 4;
        }
    }
    if (rate_idx < rate_stats->size) {
        if (!rate_stats->table[rate_idx])
            rate_stats->rate_cnt++;
        rate_stats->table[rate_idx]++;
        rate_stats->cpt++;
    } else {
        wiphy_err(bl_hw->wiphy, "RX: Invalid index conversion => %d/%d\n",
                  rate_idx, rate_stats->size);
    }
#endif

    /* Always save complete hwvect */
    sta->stats.last_rx = hw_rxhdr->hwvect;

    sta->stats.rx_pkts ++;
    sta->stats.rx_bytes += hw_rxhdr->hwvect.len;
    sta->stats.last_act = stats->last_rx;
}

/**
 * bl_rx_defer_skb - Defer processing of a SKB
 *
 * @bl_hw: main driver data
 * @bl_vif: vif that received the buffer
 * @skb: buffer to defer
 */
void bl_rx_defer_skb(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                          struct sk_buff *skb)
{
    struct bl_defer_rx_cb *rx_cb = (struct bl_defer_rx_cb *)skb->cb;

    // for now don't support deferring the same buffer on several interfaces
    if (skb_shared(skb))
        return;

    // Increase ref count to avoid freeing the buffer until it is processed
    skb_get(skb);

    rx_cb->vif = bl_vif;
    skb_queue_tail(&bl_hw->defer_rx.sk_list, skb);
    schedule_work(&bl_hw->defer_rx.work);
}

/**
 * bl_rx_data_skb - Process one data frame
 *
 * @bl_hw: main driver data
 * @bl_vif: vif that received the buffer
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 * @return: true if buffer has been forwarded to upper layer
 *
 * If buffer is amsdu , it is first split into a list of skb.
 * Then each skb may be:
 * - forwarded to upper layer
 * - resent on wireless interface
 *
 * When vif is a STA interface, every skb is only forwarded to upper layer.
 * When vif is an AP interface, multicast skb are forwarded and resent, whereas
 * skb for other BSS's STA are only resent.
 */
static void bl_rx_data_skb(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                                struct sk_buff *skb,  struct hw_rxhdr *rxhdr)
{
    struct sk_buff_head list;
    struct sk_buff *rx_skb;
    bool amsdu = rxhdr->flags_is_amsdu;
    bool resend = false, forward = true;
    struct ethhdr *icmp_eth;
    int count;

    skb->dev = bl_vif->ndev;

    __skb_queue_head_init(&list);

    BL_DBG("[DBG]pn_check en=%d-%d: stid%d,sn%d,fn%d,tid%d,mf%d,kid%d. pn:%lld~%lld\n", 
            bl_hw->mod_params->pn_check, rxhdr->rxu_hdr.flags_is_pn_check,
            rxhdr->flags_sta_idx, rxhdr->rxu_hdr.sn, rxhdr->rxu_hdr.fn, 
            rxhdr->rxu_hdr.tid,rxhdr->rxu_hdr.flags_mf,rxhdr->rxu_hdr.rx_key_idx,
            rxhdr->rxu_hdr.pn, bl_vif->rx_pn);

    if (bl_hw->mod_params->pn_check && rxhdr->rxu_hdr.flags_is_pn_check) {
        if (bl_vif->rx_pn && (bl_vif->rx_pn + 1 != rxhdr->rxu_hdr.pn)) {
            printk("pn_check miss: stid%d,sn%d,fn%d,tid%d. pn:%lld~%lld, skb:0x%p\n", 
                    rxhdr->flags_sta_idx, rxhdr->rxu_hdr.sn, rxhdr->rxu_hdr.fn,
                    rxhdr->rxu_hdr.tid, rxhdr->rxu_hdr.pn, bl_vif->rx_pn, skb);
                    
            bl_vif->rx_pn = rxhdr->rxu_hdr.pn;
            dev_kfree_skb(skb);
            
            return;
        } else {
            bl_vif->rx_pn = rxhdr->rxu_hdr.pn;
        }
    }


    if (skb->len > 24) {
        static int32_t icmp_cnt = 0;
        extern char cmw_eth_addr[6];
        extern uint8_t icmp_discard_thr;
        extern uint8_t icmp_period_thr;
        
        icmp_eth = (struct ethhdr *)(skb->data);
        
        if (icmp_eth->h_proto == 0x08 && 
             ether_addr_equal(icmp_eth->h_source, cmw_eth_addr) &&
            *((uint8_t *)(&icmp_eth->h_proto)+11) == 0x01) 
        {
            //printk("rx data:prot:0x%x, eth src:%02x:%02x:%02x:%02x:%02x:%02x, eth dst:%02x:%02x:%02x:%02x:%02x:%02x\n", 
            //        icmp_eth->h_proto, 
            //        icmp_eth->h_source[0], icmp_eth->h_source[1], icmp_eth->h_source[2], icmp_eth->h_source[3], icmp_eth->h_source[4], icmp_eth->h_source[5], 
            //        icmp_eth->h_dest[0], icmp_eth->h_dest[1], icmp_eth->h_dest[2], icmp_eth->h_dest[3], icmp_eth->h_dest[4], icmp_eth->h_dest[5]);

            icmp_cnt++;
            if (icmp_cnt < icmp_discard_thr) {
                //printk("rx_data, discard, icmp_cnt:%d\n", icmp_cnt);
                
                dev_kfree_skb(skb);
                
                return;
            } else if (icmp_cnt < icmp_period_thr) {
                //printk("rx_data, pass, icmp_cnt:%d\n", icmp_cnt);
                
                if (icmp_cnt == icmp_period_thr-1) {
                    icmp_cnt = -1;
                }
            } else {
                //printk("rx_data, discard and round back to 1, icmp_cnt:%d\n", icmp_cnt);;
                
                icmp_cnt = -1;
                dev_kfree_skb(skb);
                
                return;
            }
        }
    }

    if (amsdu) {
        struct ethhdr *eth;
        unsigned char rfc1042_header[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

        /* For FFD 5.2.1/5.8.1. mitigate A-MSDU aggregation injection attacks */
        eth = (struct ethhdr *)(skb->data);
        if (ether_addr_equal(eth->h_dest, rfc1042_header)) {
            printk("%s drop amsdu with llc head, skb:0x%p\n", __func__, skb);
            
            dev_kfree_skb(skb);
            
            return;
        }
        
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
        ieee80211_amsdu_to_8023s(skb, &list, bl_vif->ndev->dev_addr,
                                 BL_VIF_TYPE(bl_vif), 0, NULL, NULL, 0);
#else
        ieee80211_amsdu_to_8023s(skb, &list, bl_vif->ndev->dev_addr,
                                 BL_VIF_TYPE(bl_vif), 0, NULL, NULL);
#endif

        count = skb_queue_len(&list);
        if (count > ARRAY_SIZE(bl_hw->stats.amsdus_rx))
            count = ARRAY_SIZE(bl_hw->stats.amsdus_rx);
        if (count >=1 && count <= ARRAY_SIZE(bl_hw->stats.amsdus_rx))
            bl_hw->stats.amsdus_rx[count - 1]++;
        else            
            bl_hw->stats.amsdus_rx[0]++;

        if (count == 0) {
            BL_DBG("%s drop amsdu with no valid msdu, skb len:%u, proto:0x%x, eth src:%02x:%02x:%02x:%02x:%02x:%02x, eth dest:%02x:%02x:%02x:%02x:%02x:%02x\n", 
                   __func__, skb->len, eth->h_proto,
                   eth->h_source[0], eth->h_source[1], eth->h_source[2], 
                   eth->h_source[3], eth->h_source[4], eth->h_source[5],
                   eth->h_dest[0], eth->h_dest[1], eth->h_dest[2], 
                   eth->h_dest[3], eth->h_dest[4], eth->h_dest[5]); 

            //ieee80211_amsdu_to_8023s will free the skb if no valid amsdu found.
            return;
        }
    } else {
        bl_hw->stats.amsdus_rx[0]++;
        __skb_queue_head(&list, skb);
    }

    if (skb_queue_len(&list) == 0) {
        BL_DBG("rx_skb=NULL!\n");
            
        return;
    }

    if (((BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_AP) ||
         (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_AP_VLAN) ||
         (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_P2P_GO)) &&
        !(bl_vif->ap.flags & BL_AP_ISOLATE)) 
    {
        const struct ethhdr *eth;
        rx_skb = skb_peek(&list);
        skb_reset_mac_header(rx_skb);
        eth = eth_hdr(rx_skb);

        if (unlikely(is_multicast_ether_addr(eth->h_dest))) {
            /* broadcast pkt need to be forwared to upper layer and resent
               on wireless interface */
            resend = true;
        } else {
            /* unicast pkt for STA inside the BSS, no need to forward to upper
               layer simply resend on wireless interface */
            if (rxhdr->flags_dst_idx != BL_INVALID_STA)
            {
                struct bl_sta *sta = &bl_hw->sta_table[rxhdr->flags_dst_idx];
                
                if (sta->valid && (sta->vlan_idx == bl_vif->vif_index))
                {
                    forward = false;
                    resend = true;
                }
            }
        }
    } else if (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_MESH_POINT) {
        const struct ethhdr *eth;
        rx_skb = skb_peek(&list);
        skb_reset_mac_header(rx_skb);
        eth = eth_hdr(rx_skb);

        if (!is_multicast_ether_addr(eth->h_dest)) {
            /* unicast pkt for STA inside the BSS, no need to forward to upper
               layer simply resend on wireless interface */
            if (rxhdr->flags_dst_idx != BL_INVALID_STA)
            {
                forward = false;
                resend = true;
            }
        }
    }

    /* Repeater mode, drop Rx broadcast loopback data from STA iface, to avoid confuse to kernel bridge fdb table */
    if ((bl_hw->mod_params->opmode == BL_OPMODE_REPEATER) && 
         (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_STATION)) 
    {
        int i = 0;
        struct ethhdr *eth = NULL;
        struct bl_sta *sta = NULL;
        
        rx_skb = skb_peek(&list);
        if(rx_skb != NULL) {
            skb_reset_mac_header(rx_skb);
            eth = eth_hdr(rx_skb);
            
            if (is_multicast_ether_addr(eth->h_dest) || is_broadcast_ether_addr(eth->h_dest)) {
                for (i = 0; i < NX_REMOTE_STA_MAX; i++) {
                    sta = &bl_hw->sta_table[i];
                    
                    if (sta->valid && 
                        (BL_VIF_TYPE(bl_hw->vif_table[sta->vif_idx]) == NL80211_IFTYPE_AP)
                        && (memcmp(&eth->h_source, &sta->mac_addr, 6) == 0)) 
                    {
                        forward = false;
                        
                        BL_DBG("Repeater mode: STA inface drop data which src_addr is AP connected STA's\n");
                        
                        break;
                    }
                }
            }
        } else {
            printk("rx_skb=NULL!\n");
        }
    }

    /* STA mode, drop frame with SA=self */
    if ((bl_hw->mod_params->opmode == BL_OPMODE_STA) && 
        (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_STATION)) 
    {
        struct ethhdr *eth = NULL;
        u16 eth_type = 0;
        
        rx_skb = skb_peek(&list);
        if(rx_skb != NULL) {
            skb_reset_mac_header(rx_skb);
            eth = eth_hdr(rx_skb);
            eth_type = ntohs(((struct ethhdr *)eth)->h_proto);

            if ((eth_type == ETH_P_ARP || eth_type == ETH_P_IPV6)
                && (memcmp(&eth->h_source, bl_hw->wiphy->perm_addr, 6) == 0)) 
            {
                forward = false;
                
                BL_DBG("%s drop loopback 0x%x\r\n", __func__, eth_type);
            }
        }
    }

    while (!skb_queue_empty(&list)) {
        rx_skb = __skb_dequeue(&list);

        /* resend pkt on wireless interface */
        if (resend) {
            struct sk_buff *skb_copy;
            
            /* always need to copy buffer when forward=0 to get enough headrom for tsdesc */
            skb_copy = skb_copy_expand(rx_skb, sizeof(struct bl_txhdr) +
                                       BL_SWTXHDR_ALIGN_SZ, 0, GFP_ATOMIC);
            if (skb_copy) {
                int res;
                
                skb_copy->protocol = htons(ETH_P_802_3);
                skb_reset_network_header(skb_copy);
                skb_reset_mac_header(skb_copy);

                bl_vif->is_resending = true;
                res = dev_queue_xmit(skb_copy);
                bl_vif->is_resending = false;
                
                /* note: buffer is always consummed by dev_queue_xmit */
                if (res == NET_XMIT_DROP) {
                    bl_vif->net_stats.rx_dropped++;
                    bl_vif->net_stats.tx_dropped++;
                } else if (res != NET_XMIT_SUCCESS) {
                    netdev_err(bl_vif->ndev,
                               "Failed to re-send buffer to driver (res=%d)",
                               res);
                               
                    bl_vif->net_stats.tx_errors++;
                }
            } else {
                netdev_err(bl_vif->ndev, "Failed to copy skb");
            }
        }

        if (bl_trace_dyn_module&TRACE_MOD_DHCP)
            bl_skb_parsing(bl_hw, rx_skb, 0, 0);
        
        if ((bl_hw->mod_params->opmode == BL_OPMODE_REPEATER) && 
            (BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_STATION)) 
        {
            struct ethhdr *eth = NULL;
            
            skb_reset_mac_header(rx_skb);
            eth = eth_hdr(rx_skb);
            bl_rpt_eth_mac_change(bl_vif, (u8 *)eth, false);
        }

        /* Additional DHCP packet detection for RX path */
        if (bl_trace_dyn_module&TRACE_MOD_DHCP)
        {
            struct ethhdr *eth_dhcp = eth_hdr(rx_skb);
            struct dhcp_info dhcp_info_rx;
            
            if (eth_dhcp && eth_dhcp->h_proto == htons(ETH_P_IP)) {
                struct iphdr *ip_dhcp = (struct iphdr *)((u8 *)eth_dhcp + ETH_HLEN);
                
                if (ip_dhcp && ip_dhcp->protocol == IPPROTO_UDP) {
                    struct udphdr *udp_dhcp = (struct udphdr *)((u8 *)ip_dhcp + (ip_dhcp->ihl << 2));
                    
                    if (udp_dhcp) {
                        u16 src_port = ntohs(udp_dhcp->source);
                        u16 dst_port = ntohs(udp_dhcp->dest);
                        
                        /* Check if this is a DHCP packet */
                        if ((src_port == 67 && dst_port == 68) || (src_port == 68 && dst_port == 67)) {
                            printk(KERN_INFO "RX DHCP Packet Detected - src_port:%d, dst_port:%d\n", src_port, dst_port);
                            
                            /* Parse DHCP packet with proper offset for RX path */
                            {
                                struct dhcp_header *dhcp_rx;
                                int dhcp_offset = ETH_HLEN + (ip_dhcp->ihl << 2) + sizeof(struct udphdr);
                                
                                /* Direct access to DHCP header in RX packet */
                                if ((rx_skb->len >= dhcp_offset + sizeof(struct dhcp_header)) &&
                                    (dhcp_rx = (struct dhcp_header *)((u8 *)rx_skb->data + dhcp_offset)) != NULL) {
                                    
                                    /* Basic validation */
                                    __u32 magic = *((__u32 *)dhcp_rx->options);
                                    
                                    if (ntohl(magic) == 0x63825363) {
                                        /* Extract basic DHCP information directly */
                                        dhcp_info_rx.transaction_id = dhcp_rx->xid;
                                        memcpy(dhcp_info_rx.client_mac, dhcp_rx->chaddr, 6);
                                        dhcp_info_rx.client_ip = dhcp_rx->ciaddr;
                                        dhcp_info_rx.your_ip = dhcp_rx->yiaddr;
                                        dhcp_info_rx.server_ip = dhcp_rx->siaddr;
                                        /* Note: dhcp_info doesn't have gateway_ip field, router is used instead */
                                        
                                        /* Try to extract options */
                                        {
                                            int options_len = ntohs(udp_dhcp->len) - sizeof(struct udphdr) - sizeof(struct dhcp_header);
                                            __u8 *options = (__u8 *)dhcp_rx->options;
                                            
                                            printk(KERN_DEBUG "WiFi Driver: DHCP options parsing - options_len:%d, total dhcp_len:%d\n", 
                                                   options_len, ntohs(udp_dhcp->len));
                                            
                                            /* Use the extract_dhcp_options function which handles magic cookie properly */
                                            if (options_len > 0 && options_len <= DHCP_OPTIONS_MAX_LEN) {
                                                if (extract_dhcp_options(options, options_len, &dhcp_info_rx) == 0) {
                                                    /* Print DHCP information for debugging */
                                                    print_dhcp_info(&dhcp_info_rx);
                                                    
                                                    /* Handle specific DHCP messages */
                                                    switch (dhcp_info_rx.message_type) {
                                                    case DHCP_DISCOVER:
                                                        printk(KERN_INFO "WiFi Driver: RX DHCP DISCOVER from %pM\n", 
                                                               dhcp_info_rx.client_mac);
                                                        break;
                                                        
                                                    case DHCP_OFFER:
                                                        printk(KERN_INFO "WiFi Driver: RX DHCP OFFER, IP %pI4 offered to %pM\n", 
                                                               &dhcp_info_rx.your_ip, dhcp_info_rx.client_mac);
                                                        break;
                                                        
                                                    case DHCP_REQUEST:
                                                        printk(KERN_INFO "WiFi Driver: RX DHCP REQUEST for IP %pI4 from %pM\n", 
                                                               &dhcp_info_rx.your_ip, dhcp_info_rx.client_mac);
                                                        break;
                                                        
                                                    case DHCP_DECLINE:
                                                        printk(KERN_INFO "WiFi Driver: RX DHCP DECLINE from %pM\n", 
                                                               dhcp_info_rx.client_mac);
                                                        break;
                                                        
                                                    case DHCP_ACK:
                                                        printk(KERN_INFO "WiFi Driver: RX DHCP ACK, IP %pI4 assigned to %pM\n", 
                                                               &dhcp_info_rx.your_ip, dhcp_info_rx.client_mac);
                                                        break;
                                                        
                                                    case DHCP_NAK:
                                                        printk(KERN_WARNING "WiFi Driver: RX DHCP NAK for client %pM\n", 
                                                                  dhcp_info_rx.client_mac);
                                                        break;
                                                        
                                                    case DHCP_RELEASE:
                                                        printk(KERN_INFO "WiFi Driver: RX DHCP RELEASE from %pM\n", 
                                                               dhcp_info_rx.client_mac);
                                                        break;
                                                        
                                                    case DHCP_INFORM:
                                                        printk(KERN_INFO "WiFi Driver: RX DHCP INFORM from %pM\n", 
                                                               dhcp_info_rx.client_mac);
                                                        break;
                                                        
                                                    default:
                                                        printk(KERN_INFO "WiFi Driver: RX DHCP UNKNOWN type %d from %pM\n", 
                                                               dhcp_info_rx.message_type, dhcp_info_rx.client_mac);
                                                        break;
                                                    }
                                                } else {
                                                    printk(KERN_WARNING "WiFi Driver: RX DHCP packet magic validation failed\n");
                                                }
                                            } else {
                                                printk(KERN_WARNING "WiFi Driver: RX DHCP packet too short or invalid\n");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        /* forward pkt to upper layer */
        if (forward) {
            rx_skb->protocol = eth_type_trans(rx_skb, bl_vif->ndev);
            memset(rx_skb->cb, 0, sizeof(rx_skb->cb));

            /* Update statistics */
            bl_vif->net_stats.rx_packets++;
            bl_vif->net_stats.rx_bytes += rx_skb->len;

            local_bh_disable();
            netif_receive_skb(rx_skb);
            local_bh_enable();
        } else {
            dev_kfree_skb(rx_skb);
        }
    }

    return;
}

/**
 * bl_rx_mgmt - Process one 802.11 management frame
 *
 * @bl_hw: main driver data
 * @bl_vif: vif to upload the buffer to
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 *
 * Forward the management frame to a given interface.
 */
static void bl_rx_mgmt(struct bl_hw *bl_hw, struct bl_vif *bl_vif,
                            struct sk_buff *skb,  struct hw_rxhdr *hw_rxhdr)
{
    struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
    struct rx_vector_1 *rxvect = &hw_rxhdr->hwvect.rx_vect1;

    if (ieee80211_is_deauth(mgmt->frame_control)) {
        printk("%s recv deauth, sa:%02x:%02x:%02x:%02x:%02x:%02x, da:%02x:%02x:%02x:%02x:%02x:%02x\r\n", 
               __func__, 
               mgmt->sa[0], mgmt->sa[1], mgmt->sa[2], mgmt->sa[3],
               mgmt->sa[4], mgmt->sa[5], 
               mgmt->da[0], mgmt->da[1], mgmt->da[2], mgmt->da[3],
               mgmt->da[4], mgmt->da[5]);
    } else if (ieee80211_is_auth(mgmt->frame_control)) {
        printk("%s recv auth, sa:%02x:%02x:%02x:%02x:%02x:%02x, da:%02x:%02x:%02x:%02x:%02x:%02x\r\n", __func__, 
               mgmt->sa[0], mgmt->sa[1], mgmt->sa[2], mgmt->sa[3], 
               mgmt->sa[4], mgmt->sa[5], 
               mgmt->da[0], mgmt->da[1], mgmt->da[2], mgmt->da[3], 
               mgmt->da[4], mgmt->da[5]);
    }

    if (ieee80211_is_beacon(mgmt->frame_control)) {
        if ((BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_MESH_POINT) &&
            hw_rxhdr->flags_new_peer) 
        {
            cfg80211_notify_new_peer_candidate(bl_vif->ndev, mgmt->sa,
                  mgmt->u.beacon.variable,
                  skb->len - offsetof(struct ieee80211_mgmt, u.beacon.variable),
                  rxvect->rssi1, GFP_ATOMIC);
        } else {
            cfg80211_report_obss_beacon(bl_hw->wiphy, skb->data, skb->len,
                                        hw_rxhdr->phy_info.phy_prim20_freq,
                                        rxvect->rssi1);
        }
    } else if ((ieee80211_is_deauth(mgmt->frame_control) ||
                ieee80211_is_disassoc(mgmt->frame_control)) &&
               (mgmt->u.deauth.reason_code == 
                              WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA ||
                mgmt->u.deauth.reason_code == 
                              WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA)) 
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
        cfg80211_rx_unprot_mlme_mgmt(bl_vif->ndev, skb->data, skb->len);
#else
        if (ieee80211_is_deauth(mgmt->frame_control))
            cfg80211_send_unprot_deauth(bl_vif->ndev, skb->data, skb->len);
        else
            cfg80211_send_unprot_disassoc(bl_vif->ndev, skb->data, skb->len);
#endif
    } else if ((BL_VIF_TYPE(bl_vif) == NL80211_IFTYPE_STATION) &&
               (ieee80211_is_action(mgmt->frame_control) &&
                (mgmt->u.action.category == 6))) 
    {
        // Wpa_supplicant will ignore the FT action frame if reported via cfg80211_rx_mgmt
        // and cannot call cfg80211_ft_event from atomic context so defer message processing
        bl_rx_defer_skb(bl_hw, bl_vif, skb);
    } else {
        if (ieee80211_is_probe_req(mgmt->frame_control)) {
            BL_DBG("%s, call cfg80211_rx_mgmt probe req, skb:0x%p, vif_idx:%d\n", 
                   __func__, skb, bl_vif->vif_index);
        }
        
        cfg80211_rx_mgmt(&bl_vif->wdev, hw_rxhdr->phy_info.phy_prim20_freq,
                         rxvect->rssi1, skb->data, skb->len, 0);
    }

    dev_kfree_skb(skb);
}

/**
 * bl_rx_mgmt_any - Process one 802.11 management frame
 *
 * @bl_hw: main driver data
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 *
 * Process the management frame and free the corresponding skb.
 * If vif is not specified in the rx descriptor, the the frame is uploaded
 * on all active vifs.
 */
__attribute__((unused)) static void bl_rx_mgmt_any(struct bl_hw *bl_hw, 
                                 struct sk_buff *skb, struct hw_rxhdr *hw_rxhdr)
{
    struct bl_vif *bl_vif;
    int vif_idx = hw_rxhdr->flags_vif_idx;
    
    if (vif_idx == BL_INVALID_VIF) {
        list_for_each_entry(bl_vif, &bl_hw->vifs, list) {
            if (! bl_vif->up)
                continue;
                
            bl_rx_mgmt(bl_hw, bl_vif, skb, hw_rxhdr);
        }
    } else {
        bl_vif = bl_rx_get_vif(bl_hw, vif_idx);
        if (bl_vif)
            bl_rx_mgmt(bl_hw, bl_vif, skb, hw_rxhdr);
    }

    dev_kfree_skb(skb);
}

#ifdef  BL_RX_DEFRAG

#ifndef DEFRAG_LIST
/* Size of the pool containing Reassembly structure */
#define BL_RX_DEFRAG_POOL_SIZE       (8)
#endif
/* Maximum time we can wait for a fragment (in ms) */
#define BL_DEFRAG_MAX_WAIT        (msecs_to_jiffies(100))

/* Pool of re-defrag structures */
struct rxdefrag_list rx_defrag_pool[BL_RX_DEFRAG_POOL_SIZE];

/**
 * bl_rx_defrag_timeout_cb - callback function of rxdefrag timer
 *
 * @t: Pointer to struct timer_list
 * 
 *
 * This function is callback function of rxdefrag timer
 *
 */
__attribute__((unused)) static void bl_rx_defrag_timeout_cb(struct timer_list *t)
{
    struct bl_hw *bl_hw;
    struct sk_buff *skb;
    struct rxdefrag_list *p_defrag = from_timer(p_defrag, t, timer);

    BL_DBG("%s \n", __func__);

    if (!p_defrag)
        return;

    bl_hw = p_defrag->bl_hw;
    skb = (struct sk_buff *)p_defrag->pkt[0];
    
    /* directly dispatch */
    if (skb)
        bl_rx_packet_dispatch(bl_hw, skb);
}

/**
 * bl_rx_defrag_timer_set - set defrag timer
 *
 * @reorder_list: Pointer to struct rxdefrag_list
 * 
 *
 * This function is to set defrag timer
 *
 */
void bl_rx_defrag_timer_set(struct rxdefrag_list *p_defrag)
{
    BL_DBG("%s \n", __func__);

    if(p_defrag->is_timer_set){
        if(in_irq() || in_atomic() || irqs_disabled())
            del_timer(&p_defrag->timer);
        else    
            del_timer_sync(&p_defrag->timer);
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    timer_setup(&p_defrag->timer, bl_rx_defrag_timeout_cb, 0);
#else
    init_timer(&p_defrag->timer);
    p_defrag->timer.function = bl_rx_defrag_timeout_cb;
    p_defrag->timer.data = (void *)p_defrag->bl_hw;
#endif   

    mod_timer(&p_defrag->timer, jiffies + BL_DEFRAG_MAX_WAIT);

    p_defrag->is_timer_set = true;
}

/**
 * bl_rx_expand_pkt - allocate a large skb to store de-fraged frames
 *
 * @bl_hw: Pointer to struct bl_hw 
 * @p_defrag: Pointer to struct rxdefrag_list
 * 
 *
 * This function is to expand the first fragment skb to a larger one
 *
 */
static void bl_rx_expand_pkt(struct bl_hw *bl_hw, 
                                   struct rxdefrag_list *p_defrag)
{
    u16 alloc_sz;
    struct sk_buff *skb, *new_skb;
    u8  *data_ptr;

    if (!p_defrag)
        return;

    /* get first fragment */
    skb = (struct sk_buff *)p_defrag->pkt[0];

    if (!skb)
        return;

    /* alloc new skb */
    alloc_sz = p_defrag->frame_len;

    //new_skb = dev_alloc_skb(alloc_sz);
    new_skb = __netdev_alloc_skb(NULL, alloc_sz, GFP_KERNEL);
    if (!new_skb) {
        printk("%s: failed to alloc skb", __func__);
        return;
    } else {
        //printk("alloc skb: %p\n", skb);
    }

    /* copy first fragment to new skb */
    data_ptr = (u8 *)skb_put(new_skb, skb->len);
    memcpy(data_ptr, skb->data, skb->len);

    /* free original first fragment */
    if(skb)
        dev_kfree_skb_any(skb); 

    /* attach new skb to defrag list */
    p_defrag->pkt[0] = new_skb; 

}

/**
 * bl_rx_defrag_pkt - de-frag all the fragment frames
 *
 * @bl_hw: Pointer to struct bl_hw 
 * @p_defrag: Pointer to struct rxdefrag_list
 * @payload_offset: the offset of ethernet payload in the skb data buffer 
 *
 * This function is to defrag all fragment frames into the new larger skb buffer
 *
 */
static int bl_rx_defrag_pkt(struct bl_hw *bl_hw, 
                            struct rxdefrag_list *p_defrag, int payload_offset)
{
    struct sk_buff *skb;    
    u8  cur_fn = 0;
    u8  *data_ptr;
    int msdu_offset = payload_offset;   

    if (!p_defrag)
        return -1;

    /* expand first fragment to a large memory */
    bl_rx_expand_pkt(bl_hw, p_defrag);

    /* skip eth hdr len */
    msdu_offset += sizeof(struct ethhdr);

    while (cur_fn < (p_defrag->next_fn-1)) {
        cur_fn++;
        skb = (struct sk_buff *)p_defrag->pkt[cur_fn];

        if (!skb)
            continue;
            
        /* copy the 2nd~n fragment frame's payload to the first fragment */
        skb_pull(skb, msdu_offset); /* skip msdu offset*/

        /* copy to tail of first fragment */
        data_ptr = (u8 *)skb_put((struct sk_buff *)p_defrag->pkt[0], skb->len);

        if (data_ptr == NULL) {
            break;
        }

        memcpy(data_ptr, skb->data, skb->len);

        /* recover original fragment skb and free it */
        skb_push(skb, msdu_offset);     
        if (skb)
            dev_kfree_skb_any(skb); 
    };

    return 0;
}

/**
 * bl_rx_defrag_alloc - allocate a new defrag list header
 *
 * @bl_hw: Pointer to struct bl_hw 
 * @sn: skb sequence number
 *
 * This function is to allocate a new defrag list header according to skb sn
 *
 */
static struct rxdefrag_list *bl_rx_defrag_alloc(struct bl_hw *bl_hw, u16 sn)
{
    /* Get the first element of the list of used Reassembly structures */
    struct rxdefrag_list *p_defrag = NULL;

#ifdef DEFRAG_LIST
    if (!p_defrag)
    {
        /* Get the first element of the list of used Reassembly structures */
        p_defrag = (struct rxdefrag_list *)(&bl_hw->rxu_defrag_used);

    }
#else
    p_defrag = &rx_defrag_pool[sn%8];
#endif

    /* Return the allocated element */
    return (p_defrag);
}

/**
 * bl_rx_defrag_get - get a defrag list header
 *
 * @bl_hw: Pointer to struct bl_hw
 * @sta_idx: sta index 
 * @sn: skb sequence number
 * @tid: traffic index 
 *
 * This function is to get a defrag list header according to sta_idx&sn&tid
 *
 */
static struct rxdefrag_list *bl_rx_defrag_get(struct bl_hw *bl_hw, 
                                                   u8 sta_idx, u16 sn, u8 tid)
{
    /* Get the first element of the list of used Reassembly structures */
    struct rxdefrag_list *p_defrag = NULL;
    u8 cnt = 0;

#ifdef DEFRAG_LIST
    list_for_each_entry(p_defrag, &bl_hw->rxu_defrag_used, list) {
        if (p_defrag)
        {
            /* Compare Station Id and TID */
            if ((p_defrag->sta_idx == sta_idx)
                 && (p_defrag->tid == tid)
                 && (p_defrag->sn == sn))
            {
                /* We found a matching structure, escape from the loop */
                BL_DBG("%s found match!\n", __func__);
                break;
            }

            p_defrag = (struct rxdefrag_list *)p_defrag->list.next;
        }
    }
#else
    while (cnt < BL_RX_DEFRAG_POOL_SIZE) {
            p_defrag = &rx_defrag_pool[cnt];
            /* Compare Station Id and TID */    
            if ((p_defrag->sta_idx == sta_idx)
                    && (p_defrag->tid == tid)
                    && (p_defrag->sn == sn))
            {
                /* We found a matching structure, escape from the loop */
                BL_DBG("%s found match!\n", __func__);
                break;
            }
            cnt++;
    }
    
    //the last item not match, return NULL
    if (cnt == (BL_RX_DEFRAG_POOL_SIZE-1) 
        && ((p_defrag->sn != sn)
        ||(p_defrag->sta_idx != sta_idx)
        ||(p_defrag->tid != tid)))
        p_defrag = NULL;
#endif

    /* Return found element */    
    return (p_defrag);
}

/**
 * @brief Check if the received frame shall be reassembled.
 *
 * @bl_hw: Pointer to bl_hw
 * @skb: pointer to skb
 * @payload_offset: the offset of ethernet payload in the skb data buffer
 *
 * @return Whether the frame shall be defrag or not
 *
 */
struct sk_buff * bl_rx_defrag_check(struct bl_hw *bl_hw, 
                                          struct sk_buff *skb, int payload_offset)
{
    struct hw_rxhdr *hw_rxhdr;
    struct bl_vif *bl_vif;
    struct sk_buff *skb_ret = skb;  
    struct rxdefrag_list *p_defrag;
    int sta_idx = 0, tid = 0;
    u16 sn = 0, mf = 0;
    u8 fn = 0;
    u64 pn = 0;
    u8  key_id = 0;

    hw_rxhdr = (struct hw_rxhdr *)skb->data;
    bl_vif = bl_rx_get_vif(bl_hw, hw_rxhdr->flags_vif_idx);
    if (!bl_vif) {
        BL_DBG("%s, Frame received but no active vif (%d)", __func__,
                hw_rxhdr->flags_vif_idx);
                
        dev_kfree_skb(skb);
        
        return NULL;
    }

    sn = hw_rxhdr->rxu_hdr.sn;
    mf = hw_rxhdr->rxu_hdr.flags_mf;
    fn = hw_rxhdr->rxu_hdr.fn;
    pn = hw_rxhdr->rxu_hdr.pn;
    key_id = hw_rxhdr->rxu_hdr.rx_key_idx;

    do
    {
        if (!mf && !fn)
        {
            /* isn't a fragment frame directly return original skb */
            break;
        }

        /* Check if a reassembly procedure is in progress */
        p_defrag = bl_rx_defrag_get(bl_hw, sta_idx, sn, tid);
        BL_DBG("[DBG]frag tid%d sn%d fn%d pn%lld keyid%d\n", tid,sn,fn,pn,key_id);

        if (!p_defrag)
        {

            if (fn) {
                /* If not first fragment, we can reject the packet */               
                break;
            }
        
            /* Allocate a Reassembly structure */
            p_defrag = bl_rx_defrag_alloc(bl_hw, sn);
        
            /* Fullfil the Reassembly structure */
            p_defrag->bl_hw     = bl_hw;    
            p_defrag->sta_idx     = sta_idx;
            p_defrag->tid         = tid;
            p_defrag->sn          = sn;
            p_defrag->next_fn     = 1;
            p_defrag->next_pn     = pn + 1;
            p_defrag->rx_key_id   = key_id;

            /* Get Fragment Length */
            p_defrag->cpy_len     = skb->len;
            /* Reset total received length */
            p_defrag->frame_len   = skb->len;
            /* Record the frame to defrag list */
            p_defrag->pkt[fn] = skb;

#ifdef DEFRAG_TO            
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
            timer_setup(&p_defrag->timer, bl_rx_defrag_timeout_cb, 0);
#else
            init_timer(&p_defrag->timer);
            p_defrag->timer.function = bl_rx_defrag_timeout_cb;
            p_defrag->timer.data = (void *)bl_hw;
#endif      
#endif

#ifdef DEFRAG_LIST
            /* Push the reassembly structure at the end of the list of used structures */
            list_add_tail(&bl_hw->rxu_defrag_used, &p_defrag->list);
#endif
            /* Indicate to jump dispatch and will be freed in defrag processing */
            skb_ret = NULL;
        }
        else
        {
            /* Check the fragment is the one we are waiting for */          
            if (p_defrag->next_fn != fn)
                /* Packet has already been received */
                break;

            if (p_defrag->next_pn != pn)
                /* Packet PN not consecutive */
                break;

            if (p_defrag->rx_key_id != key_id)
                /* Fragment use different key */
                break;

            /* Get payload length of fragment */
            if (fn == 0) {
                /* Fullfil the Reassembly structure */
                p_defrag->sta_idx     = sta_idx;
                p_defrag->tid         = tid;
                p_defrag->sn          = sn;
                p_defrag->cpy_len     = skb->len;

#ifdef DEFRAG_TO            
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
                timer_setup(&p_defrag->timer, bl_rx_defrag_timeout_cb, 0);
#else
                init_timer(&p_defrag->timer);
                p_defrag->timer.function = bl_rx_defrag_timeout_cb;
                p_defrag->timer.data = (void *)bl_hw;
#endif      
#endif
            }
            else {
                p_defrag->cpy_len = (skb->len - payload_offset- sizeof(struct ethhdr));
            }
            
            p_defrag->frame_len += p_defrag->cpy_len;

            /* Update number of received fragment */
            p_defrag->next_fn++;
            p_defrag->next_pn++;
            p_defrag->pkt[fn] = skb;

            /* Indicate to jump dispatch and will be freed in defrag processing */
            skb_ret = NULL;

            if (!mf)
            {
                /* the last fragment */
                /* Indicate that the packet can now be defraged */
                bl_rx_defrag_pkt(bl_hw, p_defrag, payload_offset);

                skb_ret = (struct sk_buff *)p_defrag->pkt[0];
                
#ifdef DEFRAG_TO
                /* Clear the reassembly timer */
                if(in_irq() || in_atomic() || irqs_disabled())
                    del_timer(&p_defrag->timer);
                else    
                    del_timer_sync(&p_defrag->timer);
#endif
#ifdef DEFRAG_LIST
                /* Free the Reassembly structure */
                list_add_tail(&bl_hw->rxu_defrag_free, &p_defrag->list);
#else
                memset(p_defrag, 0, sizeof(struct rxdefrag_list));
#endif
                /* Forward the total de-fraged frame to upper layer */
            }
        }
        
#ifdef DEFRAG_TO   
        /* Set defrag timer */
        if(mf && !p_defrag->is_timer_set) {
            bl_rx_defrag_timer_set(p_defrag);
        }
#endif
    } while (0);

    return skb_ret;
}
#endif

#ifdef  BL_RX_REORDER 
/**
 * bl_rx_packet_dispatch - dispatch the packet
 *
 * @bl_hw: Pointer to bl_hw
 * @skb: Pointer to the skb
 *
 * This function is called for rx packet dispatch
 *
 */
u8 bl_rx_packet_dispatch(struct bl_hw *bl_hw, struct sk_buff *skb)
{
    struct hw_rxhdr *hw_rxhdr = (struct hw_rxhdr *)skb->data;
    struct bl_vif *bl_vif;

    int msdu_offset = sizeof(struct hw_rxhdr) + hw_rxhdr->rxu_hdr.msdu_offset;

    bl_vif = bl_rx_get_vif(bl_hw, hw_rxhdr->flags_vif_idx);
    if (!bl_vif) {
        BL_DBG("%s, Frame received but no active vif (%d)", __func__,
                hw_rxhdr->flags_vif_idx);
                
        dev_kfree_skb(skb);
        
        goto end;
    }
    
    BL_DBG("enter %s SN %d\n", __func__, hw_rxhdr->rxu_hdr.sn);
    
    /*Now, skb->data pointed to the real payload*/
    skb_pull(skb, msdu_offset);

    if ((hw_rxhdr->flags_sta_idx != 0xff) &&
        (hw_rxhdr->flags_sta_idx < (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX))) 
    {
        struct bl_sta *sta = &bl_hw->sta_table[hw_rxhdr->flags_sta_idx];

        bl_rx_statistic(bl_hw, hw_rxhdr, sta);

        if (sta->vlan_idx != bl_vif->vif_index) {
            bl_vif = bl_hw->vif_table[sta->vlan_idx];
            if (!bl_vif) {
                printk("cannot find the vif, skb:0x%p\n", skb);
                dev_kfree_skb(skb);
                
                goto end;
            }
        }
        
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
        if (hw_rxhdr->flags_is_4addr && !bl_vif->use_4addr) {
            cfg80211_rx_unexpected_4addr_frame(bl_vif->ndev,
                                               sta->mac_addr, GFP_ATOMIC);
        }
#endif
    }

    BL_DBG("original skb->priority=%d\n", skb->priority);
    
    skb->priority = 256 + hw_rxhdr->flags_user_prio;
    
    BL_DBG("hw_rxhdr->flags_user_prio=%d, skb->priority=%d, ndev_idx=%d\n", 
           hw_rxhdr->flags_user_prio, skb->priority, skb_get_queue_mapping(skb));

    bl_rx_data_skb(bl_hw, bl_vif, skb, hw_rxhdr);

end:

    return 0;
}


/**
 * bl_rx_reorder_dispatch_until_start_win - dispatch the reorder list until the start win
 *
 * @bl_hw: Pointer to struct bl_hw
 * @reorder_list: Pointer to the reorder list
 * @start_win: new start win
 *
 * This function is called for rx reorder packet dispatch
 *
 */
 void bl_rx_reorder_dispatch_until_start_win(struct bl_hw *bl_hw, 
                            struct rxreorder_list *reorder_list, u16 start_win)
{
    struct sk_buff *skb;
    u8 index;
    u16 count_to_send;

    if(start_win >= reorder_list->start_win)
        count_to_send = (start_win - reorder_list->start_win);
    else
        count_to_send = (start_win + MAX_SEQ_VALUE - reorder_list->start_win)%MAX_SEQ_VALUE;

    if(count_to_send > RX_WIN_SIZE)
        count_to_send = RX_WIN_SIZE;
        
    for (index = 0; index < count_to_send; index++){
        skb = (struct sk_buff *)reorder_list->reorder_pkt[(index + reorder_list->start_win_index)%RX_WIN_SIZE];
        
        reorder_list->reorder_pkt[(index + reorder_list->start_win_index)%RX_WIN_SIZE] = NULL;

        if(skb)
            bl_rx_packet_dispatch(bl_hw, skb);
    }

    //update start_win and index
    reorder_list->start_win = start_win % MAX_SEQ_VALUE;
    reorder_list->start_win_index =(reorder_list->start_win_index + count_to_send)% RX_WIN_SIZE;
    reorder_list->end_win = (reorder_list->start_win + RX_WIN_SIZE -1) % MAX_SEQ_VALUE;
}

/**
 * bl_rx_reorder_dispatch_until_start_win - dispatch the reorder list until the start win
 *
 * @bl_hw: Pointer to struct bl_hw
 * @reorder_list: Pointer to the reorder list
 *
 * This function is called for rx reorder packet dispatch
 *
 */
 void bl_rx_reorder_scan_and_dispatch(struct bl_hw *bl_hw, 
                                           struct rxreorder_list *reorder_list)
{
    struct sk_buff *skb;
    u8 index;

    BL_DBG("enter %s\n", __func__);

    for (index = 0; index < RX_WIN_SIZE; index++){
        skb = (struct sk_buff *)reorder_list->reorder_pkt[(index + reorder_list->start_win_index)%RX_WIN_SIZE];
        reorder_list->reorder_pkt[(index + reorder_list->start_win_index)%RX_WIN_SIZE] = NULL;
        
        if(skb)
            bl_rx_packet_dispatch(bl_hw, skb);
        else
            break;
    }

    //update start_win and index
    reorder_list->start_win = (reorder_list->start_win + index) % MAX_SEQ_VALUE;
    reorder_list->start_win_index =(reorder_list->start_win_index + index)% RX_WIN_SIZE;
    reorder_list->end_win = (reorder_list->start_win + RX_WIN_SIZE -1) % MAX_SEQ_VALUE;
}

/**
 * bl_rx_flush_all - flush all the packet
 *
 * @bl_hw: Pointer to struct bl_hw
 * @reorder_list: Pointer to the reorder list
 *
 * This function is called for rx reorder packet dispatch
 *
 */
 void bl_rx_flush_all(struct bl_hw *bl_hw, struct rxreorder_list *reorder_list)
 {
    struct sk_buff *skb;
    u8 i;

    /* flush all rx packet */
    for (i=0; i< RX_WIN_SIZE; i++) {
         skb = reorder_list->reorder_pkt[(reorder_list->start_win_index+i)%RX_WIN_SIZE];
         reorder_list->reorder_pkt[(reorder_list->start_win_index+i)%RX_WIN_SIZE] = NULL;
        
         if (skb)
            bl_rx_packet_dispatch(bl_hw, skb);
    }
 }
 
/**
 * bl_rx_reorder_flush - callback function of reorder timer
 *
 * @t: Pointer to struct timer_list
 *
 * This function is callback function of reorder timer
 *
 */
void bl_rx_reorder_flush(struct timer_list *t)
{
    struct rxreorder_list *reorder_list = from_timer(reorder_list, t, timer);

    if(reorder_list->flag)
        reorder_list->flush = true;
        
    reorder_list->is_timer_set = false;
    reorder_list->bl_hw->flush_rx = true;   

    bl_queue_rx_work(reorder_list->bl_hw);
}

/**
 * bl_rx_reorder_timer_set - set reorder timer
 *
 * @reorder_list: Pointer to struct rxreorder_list
 *
 * This function is to set reorder timer
 *
 */
void bl_rx_reorder_timer_set(struct rxreorder_list *reorder_list)
{
    int timeout = reorder_list->bl_hw->mod_params->rx_reorder_to;
    
    if(reorder_list->is_timer_set) {
        if(in_irq() || in_atomic() || irqs_disabled())
            del_timer(&reorder_list->timer);
        else    
            del_timer_sync(&reorder_list->timer);
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    timer_setup(&reorder_list->timer, bl_rx_reorder_flush, 0);
#else
    init_timer(&reorder_list->timer);
    reorder_list->timer.function = bl_rx_reorder_flush;
    reorder_list->timer.data = (void *)reorder_list->bl_hw;
#endif

    mod_timer(&reorder_list->timer, jiffies + msecs_to_jiffies(timeout));

    reorder_list->is_timer_set = true;
}

/**
 * bl_rx_reorder_pkt - reorder the rx packet
 *
 * @bl_hw: Pointer to bl_hw
 * @reorder_list: Pointer to the struct rxreorder_list
 * @sn: packet sequence
 * @is_bar: is BAR packet or not
 * @skb: pointer to skb
 *
 * This function is called for rx packet reorder
 *
 */
 void bl_rx_reorder_pkt(struct bl_hw *bl_hw, 
     struct rxreorder_list *reorder_list, u16 sn, u8 is_bar,struct sk_buff *skb)
{
    u8 offset, index, drop = 0;
    int prev_start_win, start_win, end_win, win_size;

    if (is_bar)
        BL_DBG("%s BAR \n", __func__);

    if (reorder_list->check_start_win) {
        if (sn == reorder_list->start_win) 
        {
            reorder_list->check_start_win = false;
            
            BL_DBG("end start win checking sn = %d \n", sn);
        }
        else
        {
            reorder_list->cnt ++;
            
            if (reorder_list->cnt < RX_WIN_SIZE/2) {
                BL_DBG("checking sn %d \n", sn);
                
                reorder_list->last_seq = sn;
                
                if(!is_bar)
                    bl_rx_packet_dispatch(bl_hw, skb);
                goto end;
            }
            
            reorder_list->check_start_win = false;
            
            if (sn != reorder_list->start_win) {
                end_win = (reorder_list->start_win + RX_WIN_SIZE - 1) & (MAX_SEQ_VALUE -1);
                
                if (((end_win > reorder_list->start_win)  &&
                      (reorder_list->last_seq >= reorder_list->start_win) &&
                      (reorder_list->last_seq < end_win)) ||
                        ((end_win < reorder_list->start_win) &&
                          ((reorder_list->last_seq >= reorder_list->start_win) ||
                          (reorder_list->last_seq < end_win)))) 
                    {
                        BL_DBG("%s Update start_win: last_seq=%d, start_win=%d seq_num=%d\n", 
                               __func__,
                               reorder_list->last_seq, reorder_list->start_win,sn);
                               
                        reorder_list->start_win = reorder_list->last_seq + 1;
                    } 
                    else if ((sn < reorder_list->start_win) && (sn > reorder_list->last_seq)) 
                    {
                        BL_DBG("%s Update start_win: last_seq=%d, start_win=%d seq_num=%d\n", 
                               __func__,
                               reorder_list->last_seq, reorder_list->start_win,sn);

                        reorder_list->start_win = reorder_list->last_seq + 1;
                    }
                }           
        }           
    }

    prev_start_win = start_win = reorder_list->start_win;
    win_size = RX_WIN_SIZE;
    end_win = ((start_win + win_size) - 1) & (MAX_SEQ_VALUE- 1);

    //1. sn  less than start_win, drop it
    //BL_DBG("%s :#1 start win %d end win %d win size %d sn %d \n",__func__,start_win, end_win, win_size, sn);
    if ((start_win + TWOPOW11) > (MAX_SEQ_VALUE - 1)) {
        if (sn >= ((start_win + (TWOPOW11)) & (MAX_SEQ_VALUE - 1)) &&
            (sn < start_win)) 
        {
           drop = 1;
           goto drop;
        }
    } else if ((sn < start_win) || (sn >= (start_win + (TWOPOW11)))) {
        drop = 1;
        goto drop;
    }

    /* BAR: update win start = sn in case #2*/
    if (is_bar)
        sn = ((sn + win_size) - 1) & (MAX_SEQ_VALUE - 1);

    // 2. sn larger than end win, out of buffering, free previous rx packet, and move the start win
    if (((end_win < start_win) && (sn < start_win) && (sn > end_win)) ||
        ((end_win > start_win) && ((sn > end_win) || (sn < start_win)))) 
    {
        BL_DBG("%s :#2(out of win) start win %d end win %d win size %d sn %d \n",
               __func__,start_win, end_win, win_size, sn);  
               
        end_win = sn;
        
        if (((sn  - win_size) + 1) >= 0)
            start_win = (end_win - win_size) + 1;
        else
            start_win = (MAX_SEQ_VALUE - (win_size -sn)) + 1;
            
        //flush util new start win
        bl_rx_reorder_dispatch_until_start_win(bl_hw, reorder_list, start_win);
    }

    //3. now sn in the buffer window
    if(!is_bar) {
        BL_DBG("%s :#3 start win %d end win %d win size %d sn %d \n",
               __func__,start_win, end_win, win_size, sn);  
               
        if (sn >= reorder_list->start_win)
            offset = sn - reorder_list->start_win;
        else // wrap case
            offset = (sn + MAX_SEQ_VALUE - reorder_list->start_win)%MAX_SEQ_VALUE;

        index = (reorder_list->start_win_index + offset) % RX_WIN_SIZE;
        
        if (reorder_list->reorder_pkt[index]) {
            BL_DBG("Drop Duplicate Pkt sn %d\n", sn);
            drop = 1;
            goto drop;
        }
        
        reorder_list->last_seq = sn;    
        reorder_list->reorder_pkt[index] = skb;
    } else{
        // free BAR
        dev_kfree_skb(skb);
    }

    // dispatch packet from start win util one hole is hit ,and update start win
    bl_rx_reorder_scan_and_dispatch(bl_hw, reorder_list);
    if (prev_start_win != reorder_list->start_win || !reorder_list->is_timer_set)
    {
        bl_rx_reorder_timer_set(reorder_list);
    }

    return;
    
drop:
    if (drop) {       
        BL_DBG("%s :#1(drop) start win %d end win %d win size %d sn %d \n",
               __func__,start_win, end_win, win_size, sn);
        dev_kfree_skb(skb);
    }

end:
   return;
}
#endif

void bl_rx_wq_process(struct bl_hw *bl_hw)
{
    struct sk_buff *skb = NULL;
    int i, j, k;
    #ifndef CONFIG_BL_USB
    unsigned long flags;
    #endif

    BL_DBG(BL_FN_ENTRY_STR);
    
    if(bl_hw->surprise_removed)
        goto exit;
        
    BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
    if(bl_hw->rx_processing){
        bl_hw->more_rx_task_flag = true;
        BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
        goto exit;
    } else {
        bl_hw->rx_processing = true;        
        BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
    }

start:
    while (true) {
#ifdef  BL_RX_REORDER
        if(bl_hw->flush_rx) {            
            for(i = 0; i < (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX); i++) {
                for(j = 0; j < NX_NB_TID_PER_STA; j++) {
                    if(bl_hw->rx_reorder[i][j].flush) {
                        struct rxreorder_list *reorder_list = &bl_hw->rx_reorder[i][j];
                        
                        reorder_list->flush = false;
                        reorder_list->last_seq = reorder_list->start_win;
                        
                        for(k = RX_WIN_SIZE -1; k > 0 ; k--) {
                            if(reorder_list->reorder_pkt[(reorder_list->start_win_index + k)%RX_WIN_SIZE]) {
                                reorder_list->last_seq = (reorder_list->start_win + k) % MAX_SEQ_VALUE;
                                break;
                            }
                        }
                        
                        if(reorder_list->last_seq != reorder_list->start_win) {
                            bl_rx_reorder_dispatch_until_start_win(bl_hw, 
                                          reorder_list, reorder_list->last_seq);
                        }
                    }
                    
                    if(bl_hw->rx_reorder[i][j].del_ba) {
                        bl_hw->rx_reorder[i][j].del_ba = false;
                        bl_rx_flush_all(bl_hw, &bl_hw->rx_reorder[i][j]);
                    }
                }
            }
            bl_hw->flush_rx = false;
        }
#endif

        skb = NULL;
        BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
        if(skb_queue_len(&bl_hw->rx_pkt_list)){
            skb = skb_dequeue(&bl_hw->rx_pkt_list);
        }
        BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
      
        if(skb)
            bl_decode_rx_packet(bl_hw, skb);
        else
            break;
    }
    
    BL_RX_LOCK(&bl_hw->rx_process_lock, flags);
    if (bl_hw->more_rx_task_flag) {
        bl_hw->more_rx_task_flag = false;
        BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
        
        goto start;
    }
    bl_hw->rx_processing = false;
    BL_RX_UNLOCK(&bl_hw->rx_process_lock, flags);
    
#ifdef CONFIG_BL_USB
    // resubmit usb urb when pending urb count is 0
    bl_rx_urb_resubmit(bl_hw);
#endif

exit:       
    return;
}


void bl_rx_wq_hdlr(struct work_struct *work)
{
    struct bl_hw *bl_hw = container_of(work, struct bl_hw, rx_work);
    bl_rx_wq_process(bl_hw);
}

#if defined CONFIG_BL_SDIO || defined CONFIG_BL_USB
/*
RX data format:

FW|     sdio header(8)      |      rx_umac_hdr(24)       |                          RXL_HEADER_INFO_LEN(56)           | offset from datastartptr |
---------------------------------------------------------------------------------------------------------------------------------------------------------------
  | sdio_hdr(8) | sd_pad(0) |status(4)|   ......   |pn(4)| hw_vect(40) | phy_channel_info_desc(8) | flags(4) | pat(4) | pal_offset(N)            | MSDU
---------------------------------------------------------------------------------------------------------------------------------------------------------------
DR|     sdio header(8)      |                                        hw_rxhdr(80)                                     | offset from datastartptr |
---------------------------------------------------------------------------------------------------------------------------------------------------------------
*/
u8 bl_rxdataind(void *pthis, void *hostid)
{
    u32 status;
    int sta_idx;
    int tid;
    u16 sn = 0;
    u8 mf = 0;
    u8 fn = 0;
    struct bl_hw *bl_hw = (struct bl_hw *)pthis;
    struct hw_rxhdr *hw_rxhdr;
    struct bl_vif *bl_vif;
    struct sk_buff *skb = (struct sk_buff *)hostid;
    #ifndef  BL_RX_REORDER
    struct bl_agg_reord_pkt *reord_pkt = NULL;
    struct bl_agg_reord_pkt *reord_pkt_inst = NULL;
    #else
    struct rxreorder_list *reorder_list = NULL;
    const struct ethhdr *eth;
    #endif
    int msdu_offset = sizeof(struct hw_rxhdr);
    struct bl_rxu_hdr *rxu_hdr;

    //SIZE:hw_rxhdr=80, hw_vect=40, phyinfo=8, v1=16, v2=8
    BL_DBG("SIZE:hw_rxhdr=%d, hw_vect=%d, phyinfo=%d, v1=%d, v2=%d, size rx_mgmt_info:%u\n",
            sizeof(struct hw_rxhdr), sizeof(struct hw_vect),
            sizeof(struct phy_channel_info_desc), sizeof( struct rx_vector_1), 
            sizeof( struct rx_vector_2), sizeof(struct rx_mgmt_info));

    #if 1
    rxu_hdr = (struct bl_rxu_hdr *)skb->data;
    if (rxu_hdr->flags_sm_scanu) {
        struct rx_mgmt_info rx_mgmt_inf = {0};
        
        //bl_dump(skb->data, skb->len);

        msdu_offset += rxu_hdr->msdu_offset;
        memcpy(&rx_mgmt_inf, (rxu_hdr + 1), sizeof(struct rx_mgmt_info));

        //use var rxu_hdr other than use hw_rxhdr->rxu_hdr
        hw_rxhdr = (struct hw_rxhdr *)(skb->data+sizeof(struct rx_mgmt_info));

        //bl_dump((uint8_t *)hw_rxhdr, sizeof(struct hw_rxhdr));

        BL_DBG("msdu_offset:%u, vif:%d, sta:%d, freq:%u, band:%d\n",
                msdu_offset, hw_rxhdr->flags_vif_idx, hw_rxhdr->flags_sta_idx,
                hw_rxhdr->phy_info.phy_prim20_freq, hw_rxhdr->phy_info.phy_band);

        softmac_handle_sm_scanu_frame_from_fw(bl_hw, skb, &rx_mgmt_inf,
                                              msdu_offset, hw_rxhdr->flags_vif_idx, 
                                              hw_rxhdr->flags_sta_idx,
                                              hw_rxhdr->phy_info.phy_prim20_freq, 
                                              hw_rxhdr->phy_info.phy_band);

        dev_kfree_skb(skb);
        
        return 0;
    }
    #endif

    hw_rxhdr = (struct hw_rxhdr *)skb->data;
    status = hw_rxhdr->rxu_hdr.status;

    /* check that frame is completely uploaded */
    if (!status) {
        printk("receive status error, status=0x%x\n", status);
        
        return -1;
    }

    BL_DBG("status---->:0x%x\n", status);

    if(status & RX_STAT_DELETE) {
        BL_DBG("staus->delete, just free skb\n");
        
        skb_push(skb, sizeof(struct inf_hdr));
        dev_kfree_skb(skb);
        
        goto end;
    }
    
    msdu_offset += hw_rxhdr->rxu_hdr.msdu_offset;
    bl_vif = bl_rx_get_vif(bl_hw, hw_rxhdr->flags_vif_idx);

    if (!bl_vif) {
        BL_DBG("%s, Frame received but no active vif (%d)",
                __func__, hw_rxhdr->flags_vif_idx);
        dev_kfree_skb(skb);
        
        goto end;
    }

    sta_idx = hw_rxhdr->flags_sta_idx;
    sn = hw_rxhdr->rxu_hdr.sn;
    tid = hw_rxhdr->rxu_hdr.tid;
    
    BL_DBG("RX_STATE_FORWARD: stid=%d, vif=%d, sn=%u, tid=%d offset=%d,%d\n",
            hw_rxhdr->flags_sta_idx, hw_rxhdr->flags_vif_idx, sn, tid, 
            hw_rxhdr->rxu_hdr.msdu_offset, msdu_offset);

    skb_pull(skb, msdu_offset);
    /*Now, skb->data pointed to the real payload*/

    if (hw_rxhdr->flags_is_80211_mpdu) {
        struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
        
        #if 0
        uint16_t tt = BL_FC_GET_TYPE(((struct ieee80211_mgmt *)skb->data)->frame_control);
        uint16_t st = BL_FC_GET_STYPE(((struct ieee80211_mgmt *)skb->data)->frame_control);

        if (tt == 0 && st == 4) {
            printk("flags_vif_idx:%d, bl_vif->vif_idx:%d, drv_vif_idx:%d\r\n", hw_rxhdr->flags_vif_idx, bl_vif->vif_index, bl_vif->drv_vif_index);
            printk("%s recv prob req, sa:%02x:%02x:%02x:%02x:%02x:%02x, da:%02x:%02x:%02x:%02x:%02x:%02x\r\n", __func__, 
                   mgmt->sa[0], mgmt->sa[1], mgmt->sa[2], mgmt->sa[3], mgmt->sa[4], mgmt->sa[5], 
                   mgmt->da[0], mgmt->da[1], mgmt->da[2], mgmt->da[3], mgmt->da[4], mgmt->da[5]);
        }
        #endif

        BL_DBG("receive mgmt packet: type=0x%x, subtype=0x%x\n", 
               BL_FC_GET_TYPE(((struct ieee80211_mgmt *)skb->data)->frame_control), 
               BL_FC_GET_STYPE(((struct ieee80211_mgmt *)skb->data)->frame_control));

        if (ieee80211_is_probe_req(mgmt->frame_control)) {
            BL_DBG("stid=%d, vif=%d, receive sn:%d, probe req mgmt packet: type=0x%x, subtype=0x%x, sa:%02x:%02x:%02x:%02x:%02x:%02x, da:%02x:%02x:%02x:%02x:%02x:%02x\n", 
                   hw_rxhdr->flags_sta_idx, hw_rxhdr->flags_vif_idx, sn,
                   BL_FC_GET_TYPE(((struct ieee80211_mgmt *)skb->data)->frame_control), 
                   BL_FC_GET_STYPE(((struct ieee80211_mgmt *)skb->data)->frame_control),
                   mgmt->sa[0], mgmt->sa[1], mgmt->sa[2], mgmt->sa[3],
                   mgmt->sa[4], mgmt->sa[5], mgmt->da[0], mgmt->da[1], 
                   mgmt->da[2], mgmt->da[3], mgmt->da[4], mgmt->da[5]);
        }
            
        if (hw_rxhdr->flags_vif_idx != BL_INVALID_VIF) {
            if (ieee80211_is_probe_req(mgmt->frame_control)) {
                BL_DBG("%s, call bl_rx_mgmt probe req, skb:0x%p, vif_idx:%d, vif_type:%d\n", 
                        __func__, skb, bl_vif->vif_index, BL_VIF_TYPE(bl_vif));
            }
            
            bl_rx_mgmt(bl_hw, bl_vif, skb, hw_rxhdr);
        } else {
            bl_vif = NULL;
            
            list_for_each_entry(bl_vif, &bl_hw->vifs, list) {
                if (bl_vif->up) {
                    struct sk_buff *clone_skb = skb_copy(skb, GFP_ATOMIC);
                    
                    if (clone_skb) {
                        if (ieee80211_is_probe_req(mgmt->frame_control)) {
                            BL_DBG("%s, call bl_rx_mgmt probe req, clone skb:0x%p, vif_idx:%d, vif_type:%d\n", 
                                   __func__, skb, bl_vif->vif_index, 
                                   BL_VIF_TYPE(bl_vif));
                        }
                        bl_rx_mgmt(bl_hw, bl_vif, clone_skb, hw_rxhdr);
                    } else {
                        printk("%s, fail to clone skb for each active vif\r\n", 
                               __func__);
                    }
                }
            }
            
            dev_kfree_skb(skb);
        }
        
        goto end;
    }

    //printk("WARN:%s, sn=%d, mf=%d, fn=%d, tid=%d\n", __func__, sn, mf, fn, tid);
#ifdef BL_RX_DEFRAG
    mf = hw_rxhdr->rxu_hdr.flags_mf;
    fn = hw_rxhdr->rxu_hdr.fn;

    if (mf || fn) {
        /* defrag needs hw_rxhdr info */
        skb_push(skb, msdu_offset);

        skb = bl_rx_defrag_check(bl_hw, skb, msdu_offset);

        if (NULL == skb)
            goto end;
               
        /* skip rx reorder, directly rx_dispatch */
        bl_rx_packet_dispatch(bl_hw, skb);
        
        goto end;
    }
#endif

#ifdef BL_RX_REORDER
    if((tid < 8) && (sta_idx < (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)))
        reorder_list = &bl_hw->rx_reorder[sta_idx][tid];

    eth = (struct ethhdr *)(skb->data);

    skb_push(skb, msdu_offset);

    if(reorder_list && reorder_list->flag && 
       ((!is_multicast_ether_addr(eth->h_dest))||(hw_rxhdr->flags_is_bar)) &&
       (!(eth->h_proto == 0x08 && *((uint8_t *)(&eth->h_proto)+11) == 0x01)))
    {
        bl_rx_reorder_pkt(bl_hw, reorder_list, sn, hw_rxhdr->flags_is_bar, skb);
        
        goto end;
    }
    
    bl_rx_packet_dispatch(bl_hw, skb);
#else
    /* Check if we need to forward the buffer */
    if (status & RX_STAT_FORWARD) {
        BL_DBG("receive data packet: type=0x%x, subtype=0x%x\n", 
                BL_FC_GET_TYPE(*((u8 *)(skb->data))), 
                BL_FC_GET_STYPE(*((u8 *)(skb->data))));
        
        if (hw_rxhdr->flags_sta_idx != 0xff) {
            struct bl_sta *sta= &bl_hw->sta_table[hw_rxhdr->flags_sta_idx];

            bl_rx_statistic(bl_hw, hw_rxhdr, sta);

            BL_DBG("sta->vlan_idx=%d, bl_vif->vif_index=%d\n", 
                   sta->vlan_idx, bl_vif->vif_index);

            if (sta->vlan_idx != bl_vif->vif_index) {
                bl_vif = bl_hw->vif_table[sta->vlan_idx];
                if (!bl_vif) {
                    printk("cannot find the vif\n");
                    dev_kfree_skb(skb);
                    goto end;
                }
            }

            if (hw_rxhdr->flags_is_4addr && !bl_vif->use_4addr) {
                cfg80211_rx_unexpected_4addr_frame(bl_vif->ndev,
                                                   sta->mac_addr, GFP_ATOMIC);
            }
        }

        BL_DBG("original skb->priority=%d\n", skb->priority);
        
        skb->priority = 256 + hw_rxhdr->flags_user_prio;
        
        BL_DBG("hw_rxhdr->flags_user_prio=%d, skb->priority=%d, ndev_idx=%d\n",
               hw_rxhdr->flags_user_prio, skb->priority, skb_get_queue_mapping(skb));

        bl_rx_data_skb(bl_hw, bl_vif, skb, hw_rxhdr);
    }
#endif /*BL_RX_REODER*/

  end:
    return 0;
}
#endif


/**
 * bl_rx_deferred - Work function to defer processing of buffer that cannot be
 * done in bl_rxdataind (that is called in atomic context)
 *
 * @ws: work field within struct bl_defer_rx
 */
void bl_rx_deferred(struct work_struct *ws)
{
    struct bl_defer_rx *rx = container_of(ws, struct bl_defer_rx, work);
    struct sk_buff *skb;

    while ((skb = skb_dequeue(&rx->sk_list)) != NULL) {
        // Currently only management frame can be deferred
        struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
        struct bl_defer_rx_cb *rx_cb = (struct bl_defer_rx_cb *)skb->cb;

        if (ieee80211_is_action(mgmt->frame_control) &&
            (mgmt->u.action.category == 6)) 
        {
            struct cfg80211_ft_event_params ft_event;
            struct bl_vif *vif = rx_cb->vif;
            u8 *action_frame = (u8 *)&mgmt->u.action;
            u8 action_code = action_frame[1];
            u16 status_code = *((u16 *)&action_frame[2 + 2 * ETH_ALEN]);

            if ((action_code == 2) && (status_code == 0)) {
                ft_event.target_ap = action_frame + 2 + ETH_ALEN;
                ft_event.ies = action_frame + 2 + 2 * ETH_ALEN + 2;
                ft_event.ies_len = skb->len - (ft_event.ies - (u8 *)mgmt);
                ft_event.ric_ies = NULL;
                ft_event.ric_ies_len = 0;
                cfg80211_ft_event(rx_cb->vif->ndev, &ft_event);
                vif->sta.flags |= BL_STA_FT_OVER_DS;
                memcpy(vif->sta.ft_target_ap, ft_event.target_ap, ETH_ALEN);
            }
        } else if (ieee80211_is_auth(mgmt->frame_control)) {
            struct cfg80211_ft_event_params ft_event;
            struct bl_vif *vif = rx_cb->vif;
            
            ft_event.target_ap = vif->sta.ft_target_ap;
            ft_event.ies = mgmt->u.auth.variable;
            ft_event.ies_len = (skb->len -
                                offsetof(struct ieee80211_mgmt, u.auth.variable));
            ft_event.ric_ies = NULL;
            ft_event.ric_ies_len = 0;
            cfg80211_ft_event(rx_cb->vif->ndev, &ft_event);
            vif->sta.flags |= BL_STA_FT_OVER_AIR;
        } else {
            netdev_warn(rx_cb->vif->ndev, "Unexpected deferred frame fctl=0x%04x",
                        mgmt->frame_control);
        }

        dev_kfree_skb(skb);
    }
}
