/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University.
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
#ifndef AODV_RERR_H
#define AODV_RERR_H

#include "defs.h"
#include "routing_table.h"

/* RERR Flags: */
#define RERR_NODELETE        0x1

typedef struct {
  u_int8_t  type;
  unsigned int flags:1;
  unsigned int reserved:15;
  u_int8_t  dest_count;
  u_int32_t dest_addr;
  u_int32_t dest_seqno;
} RERR;

#define RERR_SIZE sizeof(RERR)

/* Extra unreachable destinations... */
typedef struct {
  u_int32_t      dest_addr;
  u_int32_t      dest_seqno;
} RERR_udest;

#define RERR_UDEST_SIZE sizeof(RERR_udest)

/* Given the total number of unreachable destination this macro
   returns the RERR size */
#define RERR_CALC_SIZE(x) (RERR_SIZE + (x - 1)*RERR_UDEST_SIZE)
#define RERR_UDEST_FIRST(rerr) ((RERR_udest *)&rerr->dest_addr)
#define RERR_UDEST_NEXT(udest) ((RERR_udest *)((char *)udest + RERR_UDEST_SIZE))

RERR *rerr_create(rt_table_t *entry);
void rerr_add_udest(RERR *rerr, u_int32_t udest, u_int32_t seqno);
void rerr_process(RERR *rerr, int rerrlen, u_int32_t ip_src, u_int32_t ip_dst);

#endif
