/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University and Ericsson Telecom AB.
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
 * Authors: Erik Nordström, <erno3431@student.uu.se>
 *          Henrik Lundgren, <henrikl@docs.uu.se>
 *
 *****************************************************************************/
#ifndef AODV_RREQ_H
#define AODV_RREQ_H

#include "defs.h"

/* RREQ Flags: */
#define RREQ_JOIN       0x4
#define RREQ_REPAIR     0x2
#define RREQ_GRATUITOUS 0x1


typedef struct {
  u_int8_t  type;
  unsigned int flags:3;
  unsigned int reserved:13;	
  u_int8_t  hcnt;	
  u_int32_t flood_id;	
  u_int32_t dest_addr;	
  u_int32_t dest_seqno;	
  u_int32_t src_addr;	
  u_int32_t src_seqno;	
} RREQ;

#define RREQ_SIZE sizeof(RREQ)


/* A data structure to buffer information about received RREQ's */
struct rreq_record {
  u_int32_t src; /* Source of the RREQ */
  u_int32_t flood_id; /* RREQ's broadcast ID */
  u_int32_t timer_id;
  struct rreq_record *next;	
};

struct blacklist {
  u_int32_t dest;
  u_int32_t timer_id;
  struct blacklist *next;
};

RREQ *rreq_create(u_int8_t flags, u_int32_t dest, int ttl);
void rreq_process(RREQ *rreq, int rreqlen, u_int32_t ip_src, u_int32_t ip_dst, int ip_ttl);
void rreq_route_discovery(u_int32_t dst, u_int8_t flags);
int rreq_flood_record_remove(u_int32_t src, u_int32_t bid);
struct blacklist *rreq_blacklist_insert(u_int32_t dest);
int rreq_blacklist_remove(u_int32_t dest);
#endif
