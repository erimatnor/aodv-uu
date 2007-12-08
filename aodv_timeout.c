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
#include "packet_input.h"
#include "k_route.h"
#include "seek_list.h"

void route_delete_timeout(void *arg);

void route_discovery_timeout(void *arg) {
  seek_list_t *seek_entry;

  seek_entry = (seek_list_t *)arg;
  
  /* Sanity check... */
  if(seek_entry == NULL)
    return;
  
 
#ifdef DEBUG
  log(LOG_DEBUG, 0, "route_discovery_timeout: %s", 
      ip_to_str(seek_entry->dest));
#endif

  if (seek_entry->reqs < RREQ_RETRIES) {
    RREQ *rreq;

    if (seek_entry->ttl < TTL_THRESHOLD)
      seek_entry->ttl += TTL_INCREMENT;
    else {
      seek_entry->ttl = NET_DIAMETER;
      seek_entry->reqs++;
    }

    rreq = rreq_create(seek_entry->flags, seek_entry->dest, seek_entry->ttl);
    aodv_socket_send((AODV_msg *)rreq, AODV_BROADCAST, RREQ_SIZE, 
		     seek_entry->ttl);
    
    /* Set a new timer for seeking this destination */
    seek_entry->timer_id = timer_new(2*seek_entry->ttl*NODE_TRAVERSAL_TIME,  
				     route_discovery_timeout, seek_entry); 
  } else {

    packet_buff_drop(seek_entry->dest);

#ifdef DEBUG
    log(LOG_DEBUG, 0, "route_discovery_timeout: NO ROUTE FOUND!");
#endif
    seek_entry->timer_id = 0;
    seek_list_remove(seek_entry->dest);
    
    /* Here we should send to the application an ICMP message of type 
       "Destination Unreachable" (see RFC 792).*/
  }
}


void route_expire_timeout(void *arg) {
  rt_table_t *entry;
#ifdef DEBUG
  u_int32_t now = get_currtime();
#endif

  entry = (rt_table_t *)arg;
  
  if(entry == NULL) {
    log(LOG_WARNING, 0, "route_expire_timer: arg was NULL, ignoring timeout!");
    return;
  }
  
#ifdef DEBUG
  log(LOG_DEBUG, 0, "route_expire_timeout: %s curT=%lu exp=%lu",
      ip_to_str(entry->dest), now, entry->expire);
#endif

  /* If hopcount = 1, this is a direct neighbor and a link break has
     occured. Send a RERR with the incremented sequence number 
     (AODV draft v.8, section 8.9). */
  if(entry->hcnt == 1) {
    RERR *rerr;
    rt_table_t *u_entry;
    int i;
      
#ifdef DEBUG     
    log(LOG_DEBUG, 0, "route_expire_timeout: LINK FAILURE for %s, seqno=%d",
	ip_to_str(entry->dest), entry->dest_seqno);
#endif
    /* Create a route error msg */
    rerr = rerr_create(entry);
      
    /* Check the routing table for entries which have the unreachable
       destination (dest) as next hop. These entries (destinations)
       cannot be reached either since dest is down. They should
       therefore also be included in the RERR. */
    for(i = 0; i < RT_TABLESIZE; i++) {
      for (u_entry = routing_table[i]; u_entry != NULL; 
	   u_entry = u_entry->next) {
    
	if ((u_entry->next_hop == entry->dest) && 
	    (u_entry->dest != entry->dest)) {

	  if(u_entry->precursors != NULL) {
	    
	    rerr_add_udest(rerr, u_entry->dest, u_entry->dest_seqno);
#ifdef DEBUG     
	    log(LOG_DEBUG, 0, "route_expire_timeout: Added %s as unreachable, seqno=%d", ip_to_str(u_entry->dest), u_entry->dest_seqno);
#endif
	    rerr->dest_count++;
	  }
	  rt_table_invalidate(u_entry);
	}
      }
    } 
    /* FIXME: Check if we should unicast the RERR. This check does not
       catch all cases when we could unicast (like when several
       unreachable destinations have the same precursor). */
     if(rerr->dest_count == 1 &&
	entry->precursors != NULL &&
	entry->precursors->next == NULL)
       aodv_socket_send((AODV_msg *)rerr, entry->precursors->neighbor, 
			RERR_CALC_SIZE(rerr->dest_count), 1);
     
     else if(rerr->dest_count > 1 && 
	     (entry->precursors != NULL))
       aodv_socket_send((AODV_msg *)rerr, AODV_BROADCAST, 
			RERR_CALC_SIZE(rerr->dest_count), 1);
#ifdef DEBUG     
     else
       log(LOG_DEBUG, 0, "route_expire_timeout: No precursors, dropping RERR");
#endif
       
  } 
  /* Now also invalidate the entry of the link that broke... */
  rt_table_invalidate(entry);
}

void route_delete_timeout(void *arg) {
  rt_table_t *entry;
  
  entry = (rt_table_t *)arg;

  /* Sanity check: */
  if(entry == NULL) 
    return;

#ifdef DEBUG
  log(LOG_DEBUG, 0, "route_delete_timeout: %s", ip_to_str(entry->dest));
#endif
  /* The kernel route entry is already deleted, so we only delete the
     internal AODV routing entry... */
  entry->timer_id = 0;
  rt_table_delete(entry->dest);

}

/* This is called when we stop receiveing hello messages from a
   node. For now this is basically the same as a route timeout. */
void hello_timeout(void *arg) {
  rt_table_t *entry;
  
  entry = (rt_table_t *)arg;
#ifdef DEBUG
  log(LOG_DEBUG, 0, "hello_timeout: %s", ip_to_str(entry->dest));
#endif
 
  entry->hello_timer_id = 0;
  
  if(entry != NULL) {
    timer_remove(entry->timer_id);
    route_expire_timeout(entry);
  }  
}

void rreq_flood_record_timeout(void *arg) {
  struct rreq_record *rreq_pkt;
  
  rreq_pkt = (struct rreq_record *)arg;
  
#ifdef DEBUG
  log(LOG_DEBUG, 0, "rreq_flood_record_timeout: %s flood_id=%lu", 
      ip_to_str(rreq_pkt->src), rreq_pkt->flood_id);
#endif
  
  /* Remove buffered information for this rreq */
  rreq_pkt->timer_id = 0;
  rreq_flood_record_remove(rreq_pkt->src, rreq_pkt->flood_id);
}

void rreq_blacklist_timeout(void *arg) {
  struct blacklist *bl_entry;
  bl_entry = (struct blacklist *)arg;
#ifdef DEBUG
  log(LOG_DEBUG, 0, "rreq_blacklist_timeout: %s", ip_to_str(bl_entry->dest));
#endif
  bl_entry->timer_id = 0;
  rreq_blacklist_remove(bl_entry->dest);
}

void rrep_ack_timeout(void *arg) {
  rt_table_t *entry;
  
  /* We must be really sure here, that this entry really exists at
     this point... (Though it should). */
  entry = (rt_table_t *)arg;

  /* Indicate that the timer is now expired... */
  entry->ack_timer_id = 0;

  /* When a RREP transmission fails (i.e. lack of RREP-ACK), add to
     blacklist set... (section 8.8, draft v.9). */
  rreq_blacklist_insert(entry->dest);
  
#ifdef DEBUG
  log(LOG_DEBUG, 0, "rrep_ack_timeout: %s", ip_to_str(entry->dest));
#endif
}
