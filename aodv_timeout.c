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

#include <time.h>

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include "defs.h"
#include "aodv_timeout.h"
#include "aodv_socket.h"
#include "aodv_neighbor.h"
#include "aodv_rreq.h"
#include "aodv_hello.h"
#include "aodv_rerr.h"
#include "timer_queue.h"
#include "debug.h"
#include "params.h"
#include "routing_table.h"
#include "packet_queue.h"
#include "k_route.h"
#include "seek_list.h"
#include "icmp.h"
#endif

/* These are timeout functions which are called when timers expire... */

#ifndef NS_PORT
extern int expanding_ring_search, local_repair;
void route_delete_timeout(void *arg);
#endif

void NS_CLASS route_discovery_timeout(void *arg)
{
    struct timeval now;
    seek_list_t *seek_entry;
    rt_table_t *rt, *repair_rt;

    seek_entry = (seek_list_t *) arg;

    /* Sanity check... */
    if (!seek_entry)
	return;

    gettimeofday(&now, NULL);

    DEBUG(LOG_DEBUG, 0, "%s", ip_to_str(seek_entry->dest_addr));

    if (seek_entry->reqs < RREQ_RETRIES) {

	if (expanding_ring_search) {

	    if (seek_entry->ttl < TTL_THRESHOLD)
		seek_entry->ttl += TTL_INCREMENT;
	    else {
		seek_entry->ttl = NET_DIAMETER;
		seek_entry->reqs++;
	    }
	    /* Set a new timer for seeking this destination */

	    timer_set_timeout(&seek_entry->seek_timer,
			      2 * seek_entry->ttl * NODE_TRAVERSAL_TIME);
	} else {
	    seek_entry->reqs++;
	    timer_set_timeout(&seek_entry->seek_timer, NET_TRAVERSAL_TIME);
	}
	DEBUG(LOG_DEBUG, 0, "Seeking %s ttl=%d wait=%d",
	      ip_to_str(seek_entry->dest_addr),
	      seek_entry->ttl, 2 * seek_entry->ttl * NODE_TRAVERSAL_TIME);

	/* A routing table entry waiting for a RREP should not be expunged
	   before 2 * NET_TRAVERSAL_TIME... */
	rt = rt_table_find(seek_entry->dest_addr);

	if (rt && timeval_diff(&rt->rt_timer.timeout, &now) <
	    (2 * NET_TRAVERSAL_TIME))
	    rt_table_update_timeout(rt, 2 * NET_TRAVERSAL_TIME);

	rreq_send(seek_entry->dest_addr, seek_entry->dest_seqno,
		  seek_entry->ttl, seek_entry->flags);

    } else {
	packet_queue_drop(seek_entry->dest_addr);

	DEBUG(LOG_DEBUG, 0, "NO ROUTE FOUND!");

#ifndef NS_PORT
	/* Send an ICMP Destination Host Unreachable to the application: */
	if (seek_entry->ipd)
	    icmp_send_host_unreachable(seek_entry->ipd->data,
				       seek_entry->ipd->len);
#endif
	repair_rt = rt_table_find(seek_entry->dest_addr);

	seek_list_remove(seek_entry->dest_addr);

	/* If this route has been in repair, then we should timeout
	   the route at this point. */
	if (repair_rt && (repair_rt->flags & RT_REPAIR)) {
	    DEBUG(LOG_DEBUG, 0, "REPAIR for %s failed!",
		  ip_to_str(repair_rt->dest_addr));
	    local_repair_timeout(repair_rt);
	}
    }
}

void NS_CLASS local_repair_timeout(void *arg)
{
    rt_table_t *rt;
    u_int32_t rerr_dest = AODV_BROADCAST;
    RERR *rerr = NULL;

    rt = (rt_table_t *) arg;

    if (!rt)
	return;

    /* Unset the REPAIR flag */
    rt->flags &= ~RT_REPAIR;

    rt->rt_timer.handler = &NS_CLASS route_delete_timeout;
    timer_set_timeout(&rt->rt_timer, DELETE_PERIOD);

    DEBUG(LOG_DEBUG, 0, "%s removed in %u msecs",
	  ip_to_str(rt->dest_addr), DELETE_PERIOD);

    /* Route should already be invalidated. */

    if (rt->precursors) {

	rerr = rerr_create(0, rt->dest_addr, rt->dest_seqno);

	if (!rt->precursors->next) {
	    rerr_dest = rt->precursors->neighbor;

	    aodv_socket_send((AODV_msg *) rerr, rerr_dest, 
			     RERR_CALC_SIZE(rerr), 1,
			     &DEV_IFINDEX(rt->ifindex));
	} else {
	    int i;

	    for (i = 0; i < MAX_NR_INTERFACES; i++) {
		if (!DEV_NR(i).enabled)
		    continue;
		aodv_socket_send((AODV_msg *) rerr, rerr_dest,
				 RERR_CALC_SIZE(rerr), 1, &DEV_NR(i));
	    }
	}
	DEBUG(LOG_DEBUG, 0, "Sending RERR about %s to %s",
	      ip_to_str(rt->dest_addr), ip_to_str(rerr_dest));
    }
    precursor_list_destroy(rt);
}


void NS_CLASS route_expire_timeout(void *arg)
{
    rt_table_t *rt;

    rt = (rt_table_t *) arg;

    if (!rt) {
	log(LOG_WARNING, 0, __FUNCTION__, "arg was NULL, ignoring timeout!");
	return;
    }

    DEBUG(LOG_DEBUG, 0, "Route %s DOWN, seqno=%d",
	  ip_to_str(rt->dest_addr), rt->dest_seqno);

    rt_table_invalidate(rt);
    precursor_list_destroy(rt);

    return;
}

void NS_CLASS route_delete_timeout(void *arg)
{
    rt_table_t *rt;

    rt = (rt_table_t *) arg;

    /* Sanity check: */
    if (!rt)
	return;

    DEBUG(LOG_DEBUG, 0, "%s", ip_to_str(rt->dest_addr));

    /* The kernel route entry is already deleted, so we only delete the
       internal AODV routing entry... */
    rt_table_delete(rt->dest_addr);
}

/* This is called when we stop receiveing hello messages from a
   node. For now this is basically the same as a route timeout. */
void NS_CLASS hello_timeout(void *arg)
{
    rt_table_t *rt;
    struct timeval now;

    rt = (rt_table_t *) arg;
    
    if (!rt)
	return;

    gettimeofday(&now, NULL);

    DEBUG(LOG_DEBUG, 0, "LINK/HELLO FAILURE %s last HELLO: %d",
	  ip_to_str(rt->dest_addr), timeval_diff(&now, &rt->last_hello_time));

    if (rt && rt->state == VALID && !(rt->flags & RT_UNIDIR)) {

	/* If the we can repair the route, then mark it to be
	   repaired.. */
	if (local_repair && rt->hcnt <= MAX_REPAIR_TTL) {
	    rt->flags |= RT_REPAIR;
	    DEBUG(LOG_DEBUG, 0, "Marking %s for REPAIR",
		  ip_to_str(rt->dest_addr));
#ifdef NS_PORT
	    /* Buffer pending packets from interface queue */
	    interfaceQueue((nsaddr_t) rt->dest_addr, IFQ_BUFFER);
#endif
	}
	neighbor_link_break(rt);
    }
}

void NS_CLASS rreq_record_timeout(void *arg)
{
    struct rreq_record *rreq_pkt;

    rreq_pkt = (struct rreq_record *) arg;
   
    if (!rreq_pkt)
	return;
    
    DEBUG(LOG_DEBUG, 0, "%s rreq_id=%lu",
	  ip_to_str(rreq_pkt->orig_addr), rreq_pkt->rreq_id);

    /* Remove buffered information for this rreq */
    rreq_record_remove(rreq_pkt->orig_addr, rreq_pkt->rreq_id);
}

void NS_CLASS rreq_blacklist_timeout(void *arg)
{
    struct blacklist *bl_entry;

    bl_entry = (struct blacklist *) arg;

    if (!bl_entry)
	return;

    DEBUG(LOG_DEBUG, 0, "%s", ip_to_str(bl_entry->dest_addr));

    rreq_blacklist_remove(bl_entry->dest_addr);
}

void NS_CLASS rrep_ack_timeout(void *arg)
{
    rt_table_t *rt;

    /* We must be really sure here, that this entry really exists at
       this point... (Though it should). */
    rt = (rt_table_t *) arg;
    
    if (!rt)
	return;
    
    /* When a RREP transmission fails (i.e. lack of RREP-ACK), add to
       blacklist set... */
    rreq_blacklist_insert(rt->dest_addr);

    DEBUG(LOG_DEBUG, 0, "%s", ip_to_str(rt->dest_addr));
}

void NS_CLASS wait_on_reboot_timeout(void *arg)
{
    *((int *) arg) = 0;

    DEBUG(LOG_DEBUG, 0, "Wait on reboot over!!");
}

void NS_CLASS packet_queue_timeout(void *arg)
{
    packet_queue_garbage_collect();
    timer_set_timeout(&PQ.garbage_collect_timer, GARBAGE_COLLECT_TIME);
}
