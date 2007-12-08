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
#ifndef SEEK_LIST_H
#define SEEK_LIST_H

#include "defs.h"
#include "timer_queue.h"

#define IP_DATA_MAX_LEN 60 + 8	/* Max IP header + 64 bits of data */

struct ip_data {
    char data[IP_DATA_MAX_LEN];
    int len;
};

/* This is a list of nodes that route discovery are performed for */
typedef struct seek_list {
    u_int32_t dest_addr;
    u_int32_t dest_seqno;
    struct ip_data *ipd;
    u_int8_t flags;	 /* The flags we are using for resending the RREQ */
    int reqs;
    int ttl;
    struct timer seek_timer;
    struct seek_list *next;
} seek_list_t;

seek_list_t *seek_list_insert(u_int32_t dest_addr, u_int32_t dest_seqno,
			      int ttl, u_int8_t flags,
			      struct ip_data *ipd);
int seek_list_remove(u_int32_t dest_addr);
seek_list_t *seek_list_find(u_int32_t dest_addr);

#endif
