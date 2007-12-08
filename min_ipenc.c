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
#include <string.h>

#include "defs.h"
#include "debug.h"
#include "min_ipenc.h"

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

struct iphdr *ip_pkt_encapsulate(struct iphdr *ip, struct in_addr dest, int buflen)
{
    struct min_ipenc_hdr *ipe;    

    /* Check if buffer is large enough to store the encapsulated packet */
    if (!ip || (buflen < (ntohs(ip->tot_len) + sizeof(struct min_ipenc_hdr)))) {
	DEBUG(LOG_DEBUG, 0, "Buffer too small for encapsulation...");
	return NULL;
    }
    
    ipe = (struct min_ipenc_hdr *)((char *)ip + (ip->ihl << 2));
    
    /* Move data: */
    memmove((char *)ipe + sizeof(struct min_ipenc_hdr), 
	    (char *)ipe, ntohs(ip->tot_len) - (ip->ihl << 2));
    
    /* Save the old ip header information in the encapsulation header */
    ipe->protocol = ip->protocol;
    ipe->s = 0; /* No source address field in the encapsulation header */
    ipe->res = 0;
    ipe->check = 0;
    ipe->daddr = ip->daddr;

    /* Update the IP header */
    ip->daddr = dest.s_addr;
    ip->protocol = IPPROTO_MIPE;
    ip->tot_len = htons(ntohs(ip->tot_len) + sizeof(struct min_ipenc_hdr));
    
    /* Recalculate checksums */
    ipe->check = ip_csum((unsigned short *)ipe, 4);

    ip->check = 0;
    ip->check = ip_csum((unsigned short *)ip, ip->ihl * 2);

    return ip;
}

struct iphdr *ip_pkt_decapsulate(struct iphdr *ip)
{
    struct min_ipenc_hdr *ipe;

    if (!ip)
	return NULL;

    ipe = (struct min_ipenc_hdr *)((char *)ip + (ip->ihl << 2));

    ip->protocol = ipe->protocol;
    ip->daddr = ipe->daddr;
    ip->tot_len = htons((ntohs(ip->tot_len) - sizeof(struct min_ipenc_hdr))); 
    ip->check = 0;

    memmove((char *)ipe, (char *)ipe + sizeof(struct min_ipenc_hdr), 
	    ntohs(ip->tot_len) - (ip->ihl << 2));

    ip->check = ip_csum((unsigned short *) ip, ip->ihl * 2);
	    
    return ip;
}
