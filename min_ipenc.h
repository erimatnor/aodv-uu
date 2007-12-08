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
/* Definitions for Minimal IP Encapsulation (RFC 2004) */

#ifndef MIN_IPENC_H
#define MIN_IPENC_H


#ifndef NS_NO_GLOBALS
#include "defs.h"

#define IPPROTO_MIPE 55

struct min_ipenc_hdr {
    
    u_int8_t protocol;
#if __BYTE_ORDER == __LITTLE_ENDIAN
    u_int8_t res:7;
    u_int8_t s:1;
#elif __BYTE_ORDER == __BIG_ENDIAN
    u_int8_t s:1;
    u_int8_t res:7;
#endif
    u_int16_t check;
    u_int32_t daddr;
 /*    u_int32_t saddr; */
};

#endif				/* NS_NO_GLOBALS */

#ifndef NS_NO_DECLARATIONS

struct iphdr *ip_pkt_encapsulate(struct iphdr *ip, struct in_addr dest, int buflen);
struct iphdr *ip_pkt_decapsulate(struct iphdr *ip);

#endif				/* NS_NO_DECLARATIONS */

#endif				/* MIN_IPENC_H */
