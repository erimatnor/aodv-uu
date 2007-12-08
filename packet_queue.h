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
#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#ifndef NS_NO_GLOBALS
#include "defs.h"

#define MAX_QUEUE_LENGTH 100
#define MAX_QUEUE_TIME 10000 /* Maximum time packets can be queued (ms) */
#define GARBAGE_COLLECT_TIME 1000 /* Interval between running the
				   * garbage collector (ms) */

struct q_pkt {
    u_int32_t dest_addr;
    struct timeval q_time;
#ifdef NS_PORT
    Packet *p;
#else
    unsigned long id;
#endif
    struct q_pkt *next;
};

struct packet_queue {
    struct q_pkt *head;
    struct q_pkt *tail;
    unsigned int len;
    struct timer garbage_collect_timer;
};

#endif				/* NS_NO_GLOBALS */

#ifndef NS_NO_DECLARATIONS
struct packet_queue PQ;

#ifdef NS_PORT
void packet_queue_add(Packet * p, u_int32_t dest_addr);
#else
void packet_queue_add(unsigned long id, u_int32_t dest_addr);
#endif
void packet_queue_init();
void packet_queue_destroy();
int packet_queue_drop(u_int32_t dest_addr);
int packet_queue_send(u_int32_t dest_addr);
int packet_queue_garbage_collect(void);
#endif				/* NS_NO_DECLARATIONS */

#endif
