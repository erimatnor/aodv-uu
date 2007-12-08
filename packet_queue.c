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
#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include <stdlib.h>
#include <linux/netfilter.h>

#include "packet_queue.h"
#include "debug.h"
#include "routing_table.h"
#include "libipq.h"
#include "params.h"

extern struct ipq_handle *h;

#endif

void NS_CLASS packet_queue_init()
{
    PQ.head = NULL;
    PQ.tail = NULL;
    PQ.len = 0;
}


void NS_CLASS packet_queue_destroy()
{
    int count = 0;
    struct q_pkt *curr, *tmp;

    curr = PQ.head;

    while (curr != NULL) {
	tmp = curr;
	curr = curr->next;
#ifdef NS_PORT
	drop(tmp->p, DROP_END_OF_SIMULATION);
#else
	ipq_set_verdict(h, tmp->id, NF_DROP, 0, NULL);
#endif				/* NS_PORT */
	free(tmp);
	count++;
    }

    DEBUG(LOG_INFO, 0, "packet_queue_destroy: Destroyed %d buffered packets!",
	  count);
}

/* Buffer a packet in a FIFO queue. Implemented as a linked list,
   where we add elements at the end and remove at the beginning.... */

#ifdef NS_PORT
void NS_CLASS packet_queue_add(Packet * p, u_int32_t dest_addr)
#else
void packet_queue_add(unsigned long id, u_int32_t dest_addr)
#endif
{
    struct q_pkt *qp;
#ifdef NS_PORT
    struct hdr_ip *ih;
    ih = HDR_IP(p);
    assert(((u_int32_t) ih->daddr()) == dest_addr);
#endif

    if ((qp = (struct q_pkt *) malloc(sizeof(struct q_pkt))) == NULL) {
	fprintf(stderr, "packet_queue_add: Malloc failed!\n");
	exit(-1);
    }

    if (PQ.head == NULL)
	PQ.head = qp;
#ifdef NS_PORT
    qp->p = p;
#else
    qp->id = id;
#endif
    qp->dest_addr = dest_addr;
    qp->next = NULL;

    if (PQ.tail != NULL)
	PQ.tail->next = qp;

    PQ.tail = qp;

    PQ.len++;

    DEBUG(LOG_INFO, 0, "packet_queue_add: buffered pkt to %s qlen=%u",
	  ip_to_str(dest_addr), PQ.len);

}

int NS_CLASS packet_queue_drop(u_int32_t dest_addr)
{
    int count = 0;
    struct q_pkt *curr, *tmp, *prev;

    curr = PQ.head;
    prev = NULL;
    while (curr != NULL) {

	if (curr->dest_addr == dest_addr) {
#ifdef NS_PORT
	    drop(curr->p, DROP_RTR_NO_ROUTE);
#else
	    ipq_set_verdict(h, curr->id, NF_DROP, 0, NULL);
#endif
	    if (prev == NULL)
		PQ.head = curr->next;
	    else
		prev->next = curr->next;

	    /* If we remove the last element in the queue we must update the
	       tail pointer */
	    if (curr->next == NULL)
		PQ.tail = prev;

	    tmp = curr;
	    curr = curr->next;
	    free(tmp);
	    count++;
	    PQ.len--;
	    continue;
	}
	curr = curr->next;
    }

    DEBUG(LOG_INFO, 0, "packet_queue_drop: DROPPED %d packets for %s!",
	  count, ip_to_str(dest_addr));

    return count;
}

int NS_CLASS packet_queue_send(u_int32_t dest_addr)
{
    int count = 0;
    rt_table_t *route, *next_hop_rt;
    struct q_pkt *curr, *tmp, *prev;
#ifdef NS_PORT
    double delay = 0;
#define ARP_DELAY 0.01
#endif

    route = rt_table_find_active(dest_addr);

    if (!route)
	return -1;

    curr = PQ.head;
    prev = NULL;

    while (curr != NULL) {
	if (curr->dest_addr == dest_addr) {
#ifdef NS_PORT
	    if (curr->p) {
		/* Apparently, the link layer implementation can't handle
		 * a burst of packets. So to keep ARP happy, buffered
		 * packets are sent with ARP_DELAY seconds between
		 * sends. */
		sendPacket(curr->p, route->next_hop, delay);
		delay += ARP_DELAY;
	    }
#else
	    ipq_set_verdict(h, curr->id, NF_ACCEPT, 0, NULL);
#endif				/* NS_PORT */
	    if (prev == NULL)
		PQ.head = curr->next;
	    else
		prev->next = curr->next;

	    /* If we remove the last element in the queue we must update the
	       tail pointer */
	    if (curr->next == NULL)
		PQ.tail = prev;

	    tmp = curr;
	    curr = curr->next;
	    free(tmp);
	    count++;
	    PQ.len--;
	    continue;
	}
	curr = curr->next;
    }

    /* Update route timeouts */
    if (dest_addr != DEV_IFINDEX(route->ifindex).ipaddr) {
	rt_table_update_timeout(route, ACTIVE_ROUTE_TIMEOUT);

	next_hop_rt = rt_table_find_active(route->next_hop);

	if (next_hop_rt && next_hop_rt->dest_addr != route->dest_addr)
	    rt_table_update_timeout(next_hop_rt, ACTIVE_ROUTE_TIMEOUT);
    }

    DEBUG(LOG_INFO, 0, "packet_queue_send: SENT %d packets to %s qlen=%u",
	  count, ip_to_str(dest_addr), PQ.len);

    return count;
}
