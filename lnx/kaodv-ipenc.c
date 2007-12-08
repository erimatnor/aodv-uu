/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University & Ericsson AB.
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
 * Authors: Erik Nordström, <erik.nordstrom@it.uu.se>
 *
 *****************************************************************************/
#include <net/ip.h>
#include <linux/skbuff.h>

#include "kaodv-ipenc.h"
#include "kaodv-expl.h"		/* For print_ip() */

/* Simple function (based on R. Stevens) to calculate IP header checksum */
static u_int16_t ip_csum(unsigned short *buf, int nshorts)
{
	u_int32_t sum;

	for (sum = 0; nshorts > 0; nshorts--) {
		sum += *buf++;
	}

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);

	return ~sum;
}

int ip_pkt_encapsulate(struct sk_buff *skb, __u32 dest)
{
	struct min_ipenc_hdr *ipe;
	struct sk_buff *nskb;
	struct iphdr *iph;

	/* Allocate new data space at head */
	nskb = skb_copy_expand(skb, skb_headroom(skb),
			       skb_tailroom(skb) +
			       sizeof(struct min_ipenc_hdr), GFP_ATOMIC);

	if (nskb == NULL) {
		printk("Could not allocate new skb\n");
		kfree_skb(skb);
		return -1;
	}

	/* Set old owner */
	if (skb->sk != NULL)
		skb_set_owner_w(nskb, skb->sk);

	iph = skb->nh.iph;

	skb_put(nskb, sizeof(struct min_ipenc_hdr));

	/* Move the IP header */
	memcpy(nskb->data, skb->data, (iph->ihl << 2));
	/* Move the data */
	memcpy(nskb->data + (iph->ihl << 2) + sizeof(struct min_ipenc_hdr),
	       skb->data + (iph->ihl << 2), skb->len - (iph->ihl << 2));

	kfree_skb(skb);
	skb = nskb;

	/* Update pointers */
	iph = skb->nh.iph = (struct iphdr *)skb->data;

	ipe = (struct min_ipenc_hdr *)(skb->data + (iph->ihl << 2));

	/* Save the old ip header information in the encapsulation header */
	ipe->protocol = iph->protocol;
	ipe->s = 0;		/* No source address field in the encapsulation header */
	ipe->res = 0;
	ipe->check = 0;
	ipe->daddr = iph->daddr;

	/* Update the IP header */
	iph->daddr = dest;
	iph->protocol = IPPROTO_MIPE;
	iph->tot_len =
	    htons(ntohs(iph->tot_len) + sizeof(struct min_ipenc_hdr));

	/* Recalculate checksums */
	ipe->check = ip_csum((unsigned short *)ipe, 4);

	ip_send_check(iph);

	if (skb->nh.iph->id == 0)
		ip_select_ident(skb->nh.iph, skb->dst, NULL);

	return 0;
}

int ip_pkt_decapsulate(struct sk_buff *skb)
{
	struct min_ipenc_hdr *ipe;
	/* skb->nh.iph is probably not set yet */
	struct iphdr *iph = skb->nh.iph;

	ipe = (struct min_ipenc_hdr *)((char *)iph + (iph->ihl << 2));

	iph->protocol = ipe->protocol;
	iph->daddr = ipe->daddr;

	/* Shift the data to the left, overwriting the encap header */
	memmove(skb->data + (iph->ihl << 2),
		skb->data + (iph->ihl << 2) + sizeof(struct min_ipenc_hdr),
		skb->len - (iph->ihl << 2) - sizeof(struct min_ipenc_hdr));

	skb_trim(skb, skb->len - sizeof(struct min_ipenc_hdr));

	skb->nh.iph = iph = (struct iphdr *)skb->data;

	iph->tot_len =
		htons((ntohs(iph->tot_len) - sizeof(struct min_ipenc_hdr)));
	ip_send_check(iph);

	return 0;
}

/* EXPORT_SYMBOL(ip_pkt_encapsulate); */
