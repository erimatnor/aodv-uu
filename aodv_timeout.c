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

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include <time.h>
#include "defs.h"
#include "aodv_timeout.h"
#include "aodv_socket.h"
#include "aodv_rreq.h"
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
extern int expanding_ring_search;
void route_delete_timeout(void *arg);
#endif

void NS_CLASS route_discovery_timeout(void *arg)
{
    struct timeval now;
    seek_list_t *seek_entry;
    rt_table_t *rt_entry;
    int i;

    seek_entry = (seek_list_t *) arg;

    /* Sanity check... */
    if (!seek_entry)
	return;

    gettimeofday(&now, NULL);

    DEBUG(LOG_DEBUG, 0, "route_discovery_timeout: %s",
	  ip_to_str(seek_entry->dest_addr));

    if (seek_entry->reqs < RREQ_RETRIES) {
	RREQ *rreq;

	if (expanding_ring_search) {

	    if (seek_entry->ttl < TTL_THRESHOLD)
		seek_entry->ttl += TTL_INCREMENT;
	    else {
		seek_entry->ttl = NET_DIAMETER;
		seek_entry->reqs++;
	    }
	    /* Set a new timer for seeking this destination */

	    timer_add_msec(&seek_entry->seek_timer,
			   2 * seek_entry->ttl * NODE_TRAVERSAL_TIME);
	} else {
	    seek_entry->reqs++;
	    timer_add_msec(&seek_entry->seek_timer, NET_TRAVERSAL_TIME);
	}
	DEBUG(LOG_DEBUG, 0,
	      "route_discovery_timeout: Seeking %s ttl=%d wait=%d",
	      ip_to_str(seek_entry->dest_addr),
	      seek_entry->ttl, 2 * seek_entry->ttl * NODE_TRAVERSAL_TIME);

	/* A routing table entry waiting for a RREP should not be expunged
	   before PATH_TRAVERSAL_TIME... (Draft 10 section 6.4) */
	rt_entry = rt_table_find(seek_entry->dest_addr);

	if (rt_entry &&
	    timeval_diff(&rt_entry->rt_timer.timeout, &now) / 1000 <
	    PATH_TRAVERSAL_TIME)
	    rt_table_update_timeout(rt_entry, PATH_TRAVERSAL_TIME);

	for (i = 0; i < MAX_NR_INTERFACES; i++) {
	    if (!DEV_NR(i).enabled)
		continue;
	    rreq = rreq_create(seek_entry->flags, seek_entry->dest_addr,
			       seek_entry->dest_seqno, DEV_NR(i).ipaddr);

	    aodv_socket_send((AODV_msg *) rreq, AODV_BROADCAST, RREQ_SIZE,
			     seek_entry->ttl, &DEV_NR(i));
	}

    } else {
	packet_queue_drop(seek_entry->dest_addr);

	DEBUG(LOG_DEBUG, 0, "route_discovery_timeout: NO ROUTE FOUND!");

#ifndef NS_PORT
	/* Send an ICMP Destination Host Unreachable to the application: */
	if (seek_entry->ipd)
	    icmp_send_host_unreachable(seek_entry->ipd->data,
				       seek_entry->ipd->len);
#endif
	seek_list_remove(seek_entry->dest_addr);
    }
}


void NS_CLASS route_expire_timeout(void *arg)
{
    rt_table_t *rt_entry;
    struct timeval now;

    gettimeofday(&now, NULL);

    rt_entry = (rt_table_t *) arg;

    if (!rt_entry) {
	log(LOG_WARNING, 0,
	    "route_expire_timer: arg was NULL, ignoring timeout!");
	return;
    }

    DEBUG(LOG_DEBUG, 0, "route_expire_timeout: Route %s DOWN, seqno=%d",
	  ip_to_str(rt_entry->dest_addr), rt_entry->dest_seqno);

    /* If hopcount = 1, this is a direct neighbor and a link break has
       occured. Send a RERR with the incremented sequence number */
    if (rt_entry->hcnt == 1) {
	RERR *rerr = NULL;
	rt_table_t *u_entry;
	u_int32_t rerr_unicast_dest = 0;
	int i, unicast_rerr = 0;

	/* Invalidate the entry of the route that broke or timed out... */
	/* Hop count -> INFTY, dest_seqno++ */
	rt_table_invalidate(rt_entry);

	/* Create a route error msg */
	if (rt_entry->precursors) {
	    rerr = rerr_create(0, rt_entry->dest_addr, rt_entry->dest_seqno);
	    DEBUG(LOG_DEBUG, 0,
		  "route_expire_timeout: Added %s as unreachable, seqno=%lu",
		  ip_to_str(rt_entry->dest_addr), rt_entry->dest_seqno);

	    if (!rt_entry->precursors->next) {
		unicast_rerr = 1;
		rerr_unicast_dest = rt_entry->precursors->neighbor;
	    }
	}
	/* Purge precursor list: */
	precursor_list_destroy(rt_entry);

	/* Check the routing table for entries which have the unreachable
	   destination (dest) as next hop. These entries (destinations)
	   cannot be reached either since dest is down. They should
	   therefore also be included in the RERR. */
	for (i = 0; i < RT_TABLESIZE; i++) {
	    for (u_entry = routing_table[i]; u_entry; u_entry = u_entry->next) {

		if ((u_entry->next_hop == rt_entry->dest_addr) &&
		    (u_entry->dest_addr != rt_entry->dest_addr)) {

		    if (u_entry->hcnt != INFTY)
			rt_table_invalidate(u_entry);

		    if (u_entry->precursors) {
			if (!rerr) {
			    rerr = rerr_create(0, u_entry->dest_addr,
					       u_entry->dest_seqno);
			    if (!u_entry->precursors->next) {
				unicast_rerr = 1;
				rerr_unicast_dest =
				    u_entry->precursors->neighbor;
			    }
			    DEBUG(LOG_DEBUG, 0,
				  "route_expire_timeout: Added %s as unreachable, seqno=%lu",
				  ip_to_str(u_entry->dest_addr),
				  u_entry->dest_seqno);
			} else {
			    rerr_add_udest(rerr, u_entry->dest_addr,
					   u_entry->dest_seqno);
			    if (!(!u_entry->precursors->next &&
				  rerr_unicast_dest ==
				  u_entry->precursors->neighbor))
				unicast_rerr = 0;
			    DEBUG(LOG_DEBUG, 0,
				  "route_expire_timeout: Added %s as unreachable, seqno=%lu",
				  ip_to_str(u_entry->dest_addr),
				  u_entry->dest_seqno);
			}
		    }
		    precursor_list_destroy(u_entry);
		}
	    }
	}

	if (rt_entry->flags & LREPAIR) {
	    rreq_local_repair(rt_entry);
	} else if (rerr) {
	    DEBUG(LOG_DEBUG, 0,
		  "route_expire_timeout: RERR created, %d bytes.",
		  RERR_CALC_SIZE(rerr));

	    u_entry = rt_table_find(rerr_unicast_dest);

	    if (u_entry && rerr->dest_count == 1 && unicast_rerr)
		aodv_socket_send((AODV_msg *) rerr,
				 rerr_unicast_dest,
				 RERR_CALC_SIZE(rerr), 1,
				 &DEV_IFINDEX(u_entry->ifindex));

	    else if (rerr->dest_count > 0) {
		/* FIXME: Should only transmit RERR on those interfaces
		 * which have precursor nodes for the broken route */
		for (i = 0; i < MAX_NR_INTERFACES; i++) {
		    if (!DEV_NR(i).enabled)
			continue;
		    aodv_socket_send((AODV_msg *) rerr, AODV_BROADCAST,
				     RERR_CALC_SIZE(rerr), 1, &DEV_NR(i));
		}
	    }
	}
    } else {
	rt_table_invalidate(rt_entry);
	precursor_list_destroy(rt_entry);
    }

    return;
}

void NS_CLASS route_delete_timeout(void *arg)
{
    rt_table_t *rt_entry;

    rt_entry = (rt_table_t *) arg;

    /* Sanity check: */
    if (rt_entry == NULL)
	return;

    DEBUG(LOG_DEBUG, 0, "route_delete_timeout: %s",
	  ip_to_str(rt_entry->dest_addr));

    /* The kernel route entry is already deleted, so we only delete the
       internal AODV routing entry... */
    rt_table_delete(rt_entry->dest_addr);
}

/* This is called when we stop receiveing hello messages from a
   node. For now this is basically the same as a route timeout. */
void NS_CLASS hello_timeout(void *arg)
{
    rt_table_t *rt_entry;
    struct timeval now;

    rt_entry = (rt_table_t *) arg;

    gettimeofday(&now, NULL);

    DEBUG(LOG_DEBUG, 0, "hello_timeout: LINK FAILURE %s last HELLO: %d",
	  ip_to_str(rt_entry->dest_addr),
	  timeval_diff(&now, &rt_entry->last_hello_time) / 1000);

    if (rt_entry && rt_entry->hcnt != INFTY && !(rt_entry->flags & UNIDIR))
	timer_timeout_now(&rt_entry->rt_timer);
}

void NS_CLASS rreq_record_timeout(void *arg)
{
    struct rreq_record *rreq_pkt;

    rreq_pkt = (struct rreq_record *) arg;

    DEBUG(LOG_DEBUG, 0, "rreq_record_timeout: %s rreq_id=%lu",
	  ip_to_str(rreq_pkt->orig_addr), rreq_pkt->rreq_id);

    /* Remove buffered information for this rreq */
    rreq_record_remove(rreq_pkt->orig_addr, rreq_pkt->rreq_id);
}

void NS_CLASS rreq_blacklist_timeout(void *arg)
{
    struct blacklist *bl_entry;

    bl_entry = (struct blacklist *) arg;

    DEBUG(LOG_DEBUG, 0, "rreq_blacklist_timeout: %s",
	  ip_to_str(bl_entry->dest_addr));

    rreq_blacklist_remove(bl_entry->dest_addr);
}

void NS_CLASS rrep_ack_timeout(void *arg)
{
    rt_table_t *rt_entry;

    /* We must be really sure here, that this entry really exists at
       this point... (Though it should). */
    rt_entry = (rt_table_t *) arg;

    /* When a RREP transmission fails (i.e. lack of RREP-ACK), add to
       blacklist set... */
    rreq_blacklist_insert(rt_entry->dest_addr);

    DEBUG(LOG_DEBUG, 0, "rrep_ack_timeout: %s", ip_to_str(rt_entry->dest_addr));
}

void NS_CLASS wait_on_reboot_timeout(void *arg)
{
    *((int *) arg) = 0;

    DEBUG(LOG_DEBUG, 0, "wait_on_reboot_timeout: Wait on reboot over!!");
}
