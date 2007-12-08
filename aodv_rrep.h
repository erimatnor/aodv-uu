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
 *
 *****************************************************************************/
#ifndef AODV_RREP_H
#define AODV_RREP_H

#ifndef NS_NO_GLOBALS
#include <endian.h>

#include "defs.h"

/* RREP Flags: */
#define RREP_REPAIR  0x2
#define RREP_ACK     0x1

typedef struct {
    u_int8_t type;
#  if __BYTE_ORDER == __LITTLE_ENDIAN
    u_int16_t res1:6;
    u_int16_t a:1;
    u_int16_t r:1;
    u_int16_t prefix:5;
    u_int16_t res2:3;
#  elif __BYTE_ORDER == __BIG_ENDIAN
    u_int16_t r:1;
    u_int16_t a:1;
    u_int16_t res1:6;
    u_int16_t res2:3;
    u_int16_t prefix:5;
#  else
#   error "Adjust your <bits/endian.h> defines"
#  endif
    u_int8_t hcnt;
    u_int32_t dest_addr;
    u_int32_t dest_seqno;
    u_int32_t orig_addr;
    u_int32_t lifetime;
} RREP;

#define RREP_SIZE sizeof(RREP)

typedef struct {
    u_int8_t type;
    u_int8_t reserved;
} RREP_ack;

#define RREP_ACK_SIZE sizeof(RREP_ack)
#endif				/* NS_NO_GLOBALS */

#ifndef NS_NO_DECLARATIONS
RREP *rrep_create(u_int8_t flags,
		  u_int8_t prefix,
		  u_int8_t hcnt,
		  u_int32_t dest_addr,
		  u_int32_t dest_seqno, u_int32_t orig_addr, u_int32_t life);

RREP_ack *rrep_ack_create();
void rrep_process(RREP * rrep, int rreplen, u_int32_t ip_src,
		  u_int32_t ip_dst, int ip_ttl, unsigned int ifindex);
void rrep_ack_process(RREP_ack * rrep_ack, int rreplen, u_int32_t ip_src,
		      u_int32_t ip_dst);
#endif				/* NS_NO_DECLARATIONS */

#endif				/* AODV_RREP_H */
