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
 * Authors: Erik Nordström, <erik.nordstrom@it.uu.se>
 *          
 *
 *****************************************************************************/
#ifndef AODV_RREQ_H
#define AODV_RREQ_H

#ifndef NS_NO_GLOBALS
#include <endian.h>

#include "defs.h"
#include "seek_list.h"
#include "routing_table.h"

/* RREQ Flags: */
#define RREQ_JOIN          0x1
#define RREQ_REPAIR        0x2
#define RREQ_GRATUITOUS    0x4
#define RREQ_DEST_ONLY     0x8
#define RREQ_UNKNOWN_SEQNO 0x10

typedef struct {
    u_int8_t type;
#  if __BYTE_ORDER == __LITTLE_ENDIAN
    u_int8_t res1:3;
    u_int8_t u:1;
    u_int8_t d:1;
    u_int8_t g:1;
    u_int8_t r:1;
    u_int8_t j:1;
#  elif __BYTE_ORDER == __BIG_ENDIAN
    u_int8_t j:1;		/* Join flag (multicast) */
    u_int8_t r:1;		/* Repair flag */
    u_int8_t g:1;		/* Gratuitous RREP flag */
    u_int8_t d:1;		/* Destination only respond */
    u_int8_t u:1;		/* Unknown sequence number */
    u_int8_t res1:3;
#  else
#   error "Adjust your <bits/endian.h> defines"
#  endif
    u_int8_t flags;
    u_int8_t res2;
    u_int8_t hcnt;
    u_int32_t rreq_id;
    u_int32_t dest_addr;
    u_int32_t dest_seqno;
    u_int32_t orig_addr;
    u_int32_t orig_seqno;
} RREQ;

#define RREQ_SIZE sizeof(RREQ)

/* A data structure to buffer information about received RREQ's */
struct rreq_record {
    u_int32_t orig_addr;	/* Source of the RREQ */
    u_int32_t rreq_id;		/* RREQ's broadcast ID */
    struct timer rec_timer;
    struct rreq_record *next;
};

struct blacklist {
    u_int32_t dest_addr;
    struct timer bl_timer;
    struct blacklist *next;
};
#endif				/* NS_NO_GLOBALS */

#ifndef NS_NO_DECLARATIONS
RREQ *rreq_create(u_int8_t flags, u_int32_t dest_addr,
		  u_int32_t dest_seqno, u_int32_t orig_addr);
void rreq_send(u_int32_t dest_addr, u_int32_t dest_seqno, int ttl,
	       u_int8_t flags);
void rreq_forward(RREQ * rreq, int ttl);
void rreq_process(RREQ * rreq, int rreqlen, u_int32_t ip_src,
		  u_int32_t ip_dst, int ip_ttl, unsigned int ifindex);
void rreq_route_discovery(u_int32_t dest_addr, u_int8_t flags,
			  struct ip_data *ipd);
int rreq_record_remove(u_int32_t orig_addr, u_int32_t rreq_id);
struct blacklist *rreq_blacklist_insert(u_int32_t dest_addr);
int rreq_blacklist_remove(u_int32_t dest_addr);
void rreq_local_repair(rt_table_t * rt, u_int32_t src_addr,
		       struct ip_data *ipd);

#ifdef NS_PORT
struct rreq_record *rreq_record_insert(u_int32_t orig_addr, u_int32_t rreq_id);
struct rreq_record *rreq_record_find(u_int32_t orig_addr, u_int32_t rreq_id);
struct blacklist *rreq_blacklist_find(u_int32_t dest_addr);
#endif				/* NS_PORT */

#endif				/* NS_NO_DECLARATIONS */

#endif				/* AODV_RREQ_H */
