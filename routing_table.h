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
 * Authors: Erik Nordström, <erno3431@student.uu.se>
 *          Henrik Lundgren, <henrikl@docs.uu.se>
 *
 *****************************************************************************/
#ifndef ROUTING_TABLE_H
#define ROUTING_TABLE_H

#include "defs.h"

  /* Neighbor struct for active routes in Route Table */

typedef struct precursor {
    u_int32_t neighbor;
    struct precursor *next;
} precursor_t;


typedef u_int32_t hash_value;	/* A hash value */

/* Route Table struct */
typedef struct rt_table {
    u_int32_t dest_addr;	/* IP address of the destination */
    u_int32_t dest_seqno;
    unsigned int ifindex;	/* Network interface index... */
    u_int32_t next_hop;		/* IP address of the next hop to the dest */
    u_int8_t hcnt;		/* Distance (in hops) to the destination */
    u_int8_t last_hcnt;		/* The last distance (in hops) the dest was known at */
    u_int16_t last_life;	/* The last lifetime... */
    u_int16_t flags;		/* Routing flags */
    struct timer rt_timer;	/* The timer associated with this entry */
    struct timer ack_timer;	/* RREP_ack timer for this destination */
    struct timer hello_timer;
    struct timeval last_hello_time;
    u_int8_t hello_cnt;
    hash_value hash;
    precursor_t *precursors;	/* List of neighbors using the route */
    struct rt_table *next;	/* Pointer to next Route Table entry */
} rt_table_t;

/* Route entry flags */

#define REV_ROUTE 0x1
#define FWD_ROUTE 0x2
#define NEIGHBOR  0x4
#define UNIDIR    0x8

/* Route Table  function prototypes */


#define RT_TABLESIZE 64		/* Must be a power of 2 */
#define RT_TABLEMASK (RT_TABLESIZE - 1)

rt_table_t *routing_table[RT_TABLESIZE];

void rt_table_init();
void rt_table_destroy();
rt_table_t *rt_table_insert(u_int32_t dest, u_int32_t next, u_int8_t hops,
			    u_int32_t seqno, u_int32_t life,
			    u_int16_t flags, unsigned int ifindex);
rt_table_t *rt_table_update(rt_table_t * rt_entry, u_int32_t next,
			    u_int8_t hops, u_int32_t seqno,
			    u_int32_t newlife, u_int16_t flags);
inline rt_table_t *rt_table_update_timeout(rt_table_t * rt_entry,
					   long life);
rt_table_t *rt_table_find_active(u_int32_t dest);
rt_table_t *rt_table_find(u_int32_t dest);
int rt_table_invalidate(rt_table_t * rt_entry);
void rt_table_delete(u_int32_t dest);
int rt_table_is_next_hop(u_int32_t dest);

int flush_rt_cache();

void precursor_add(rt_table_t * rt_entry, u_int32_t addr);
void precursor_remove(rt_table_t * rt_entry, u_int32_t addr);
void precursor_list_destroy(rt_table_t * rt_entry);

#endif
