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

#include <endian.h>

#include "defs.h"
#include "seek_list.h"

/* RREQ Flags: */
#define RREQ_JOIN       0x4
#define RREQ_REPAIR     0x2
#define RREQ_GRATUITOUS 0x1

typedef struct {
  u_int8_t  type;
#  if __BYTE_ORDER == __LITTLE_ENDIAN
  u_int8_t res1:5;
  u_int8_t g:1;
  u_int8_t r:1;
  u_int8_t j:1;
#  elif __BYTE_ORDER == __BIG_ENDIAN
  u_int8_t j:1;
  u_int8_t r:1;
  u_int8_t g:1;
  u_int8_t res1:5;
#  else
#   error "Adjust your <bits/endian.h> defines"
#  endif	
  u_int8_t  res2;
  u_int8_t  hcnt;	
  u_int32_t rreq_id;	
  u_int32_t dest_addr;	
  u_int32_t dest_seqno;	
  u_int32_t orig_addr;	
  u_int32_t orig_seqno;	
} RREQ;

#define RREQ_SIZE sizeof(RREQ)

/* A data structure to buffer information about received RREQ's */
struct rreq_record {
  u_int32_t orig_addr; /* Source of the RREQ */
  u_int32_t rreq_id; /* RREQ's broadcast ID */
  u_int32_t timer_id;
  struct rreq_record *next;	
};

struct blacklist {
  u_int32_t dest_addr;
  u_int32_t timer_id;
  struct blacklist *next;
};

RREQ *rreq_create(u_int8_t flags, u_int32_t dest_addr, u_int32_t dest_seqno);
void rreq_process(RREQ *rreq, int rreqlen, u_int32_t ip_src, u_int32_t ip_dst, int ip_ttl);
void rreq_route_discovery(u_int32_t dest_addr, u_int8_t flags, struct ip_data *ipd);
int rreq_record_remove(u_int32_t orig_addr, u_int32_t rreq_id);
struct blacklist *rreq_blacklist_insert(u_int32_t dest_addr);
int rreq_blacklist_remove(u_int32_t dest_addr);
#endif
