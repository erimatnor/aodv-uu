/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University and Ericsson AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Erik Nordström, <erik.nordstrom@it.uu.se>
 * 
 *****************************************************************************/
#ifndef _KAODV_H
#define _KAODV_H

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22))
#define SKB_TAIL_PTR(skb) skb->tail
#define SKB_NETWORK_HDR_RAW(skb) skb->nh.raw
#define SKB_NETWORK_HDR_RIPH(skb) skb->nh.iph
#define SKB_MAC_HDR_RAW(skb) skb->mac.raw
#define SKB_SET_NETWORK_HDR(skb, offset) (skb->nh.raw = (skb->data + (offset)))
#else
#define SKB_TAIL_PTR(skb) skb_tail_pointer(skb)
#define SKB_NETWORK_HDR_RAW(skb) skb_network_header(skb)
#define SKB_NETWORK_HDR_IPH(skb) ((struct iphdr *)skb_network_header(skb))
#define SKB_MAC_HDR_RAW(skb) skb_mac_header(skb)
#define SKB_SET_NETWORK_HDR(skb, offset) skb_set_network_header(skb, offset)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31))
static inline struct dst_entry *skb_dst(const struct sk_buff *skb)
{
	return (struct dst_entry *)skb->dst;
}

static inline void skb_dst_set(struct sk_buff *skb, struct dst_entry *dst)
{
	skb->dst = dst;
}
#endif



#define AODV_PORT 654

#endif /* _KAODV_H */
