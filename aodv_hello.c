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
#include <netinet/in.h>

#include "aodv_hello.h"
#include "aodv_timeout.h"
#include "aodv_rrep.h"
#include "aodv_rreq.h"
#include "routing_table.h"
#include "timer_queue.h"
#include "params.h"
#include "aodv_socket.h"
#include "defs.h"
#include "debug.h"

/* #define DEBUG_HELLO  */

extern int unidir_hack, receive_n_hellos;

void hello_send(void *arg) {
  RREP *rrep;
  AODV_ext *ext = NULL;
  u_int8_t flags = 0;
  u_int64_t time_expired;
  rt_table_t *entry;
  int msg_size = RREP_SIZE;

  time_expired = get_currtime() - this_host->bcast_time;

  /* This check will ensure we don't send unnecessary hello msgs, in case
     we have sent other bcast msgs within HELLO_INTERVAL */
  if (time_expired >= HELLO_INTERVAL) {
    
#ifdef DEBUG_HELLO
    log(LOG_DEBUG, 0, "SEND_BCAST: sending Hello to %s", 
	ip_to_str(AODV_BROADCAST));   
#endif
    rrep = rrep_create(flags, 0, 0, this_host->ipaddr, this_host->seqno, 
		       this_host->ipaddr, ALLOWED_HELLO_LOSS*HELLO_INTERVAL);
    
    /* Assemble a RREP extension which contain our neighbor set... */
    if(unidir_hack) {
      u_int32_t neigh;
      int i;
      
      ext = (AODV_ext *)((char *)rrep + RREP_SIZE);
      ext->type = RREP_HELLO_NEIGHBOR_SET_EXT;
      ext->length = 0;
      for(i = 0; i < RT_TABLESIZE; i++) {
	entry = routing_table[i];
	while(entry != NULL) {
	  /* If an entry has an active hello timer, we asume that we are
	     receiving hello messages from that node... */
	  if(entry->hello_timer_id != 0) {
#ifdef DEBUG_HELLO
	    log(LOG_INFO, 0, "hello_send: Adding %s to hello neighbor set ext",
		ip_to_str(entry->dest_addr));
#endif
	    neigh = htonl(entry->dest_addr);
	    memcpy(AODV_EXT_NEXT(ext), &neigh, 4);
	    ext->length += 4;
	  }
	  entry = entry->next;
	}
      }
      if(ext->length)
	msg_size = RREP_SIZE + AODV_EXT_SIZE(ext);    
    }
    
    aodv_socket_send((AODV_msg *)rrep, AODV_BROADCAST, msg_size, 1);
    
    timer_new(HELLO_INTERVAL, hello_send, NULL);
  }
  else {
    timer_new((HELLO_INTERVAL - time_expired), hello_send, NULL);
  }
}


/* Process a hello message */
void hello_process(RREP *hello, int rreplen) {
  u_int32_t hello_dst, hello_seqno;
  u_int32_t hello_interval = HELLO_INTERVAL;
  u_int32_t ext_neighbor;
  rt_table_t *rt_entry;
  AODV_ext *ext = NULL;
  int i, unidir_link = 1;
  u_int64_t now = get_currtime();

  hello_dst = ntohl(hello->dest_addr);
  hello_seqno = ntohl(hello->dest_seqno);
  /* Check for hello interval extension: */
  ext = (AODV_ext *)((char *)hello + RREP_SIZE);
  
  while(rreplen > RREP_SIZE) {
    switch(ext->type) {
    case RREP_HELLO_INTERVAL_EXT:
      if(ext->length == 4) {
	memcpy(&hello_interval, AODV_EXT_DATA(ext), 4);
	hello_interval = ntohl(hello_interval);
#ifdef DEBUG_HELLO
	log(LOG_INFO, 0, "HELLO_process: Hello extension interval=%lu!", 
	    hello_interval);
#endif
      } else
	log(LOG_WARNING, 0, "hello_process: Bad hello interval extension!");
      break;
    case RREP_HELLO_NEIGHBOR_SET_EXT:
#ifdef DEBUG_HELLO
      log(LOG_INFO, 0, "HELLO_process: RREP_HELLO_NEIGHBOR_SET_EXT");
#endif
      for(i = 0; i < ext->length; i = i + 4) {
	ext_neighbor = ntohl(*(u_int32_t *)((char *)AODV_EXT_DATA(ext) + i));

	if(memcmp(&ext_neighbor, &this_host->ipaddr, 4) == 0)
	  unidir_link = 0;
      }
      break;
    default:
      log(LOG_WARNING, 0, "hello_process: Bad extension!! type=%d, length=%d", 
	  ext->type, ext->length);
      ext = NULL;
      break;
    }
    if(ext == NULL)
      break;

    rreplen -= AODV_EXT_SIZE(ext);
    ext = AODV_EXT_NEXT(ext);
    
  }
  
#ifdef DEBUG_HELLO
  log(LOG_DEBUG, 0, "hello_process: rcvd HELLO from %s, seqno %lu", 
      ip_to_str(hello_dst), hello_seqno);
#endif

  if((rt_entry = rt_table_find(hello_dst)) == NULL) {
    /* No active or expired route in the routing table. So we add a
       new entry... */
    if(unidir_hack && unidir_link) {
      rt_entry = rt_table_insert(hello_dst, hello_dst, 1, hello_seqno,  
			      ACTIVE_ROUTE_TIMEOUT, NEIGHBOR | UNIDIR);  
#ifdef DEBUG
      log(LOG_INFO, 0, "hello_process: %s is UNI-DIR!!!", 
	  ip_to_str(rt_entry->dest_addr));
#endif
    } else {
      rt_entry = rt_table_insert(hello_dst, hello_dst, 1, hello_seqno,  
			    ACTIVE_ROUTE_TIMEOUT, NEIGHBOR);  
#ifdef DEBUG
        log(LOG_INFO, 0, "hello_process: %s is BI-DIR!!!", 
	  ip_to_str(rt_entry->dest_addr));
#endif
    }
    
    /* This neighbor should only be valid after receiving 3
       consecutive hello messages... */
    if(receive_n_hellos)
      rt_entry->hcnt = INFTY;
    
  } else {

    if(receive_n_hellos && rt_entry->hello_cnt < receive_n_hellos) {
      if((now - rt_entry->last_hello_time) < ALLOWED_HELLO_LOSS*hello_interval)
	rt_entry->hello_cnt++;
      else
	rt_entry->hello_cnt = 0;
      
      rt_entry->last_hello_time = now;
      return;
    }

    if(unidir_hack && unidir_link) {
#ifdef DEBUG
      if(!(rt_entry->flags & UNIDIR))
	log(LOG_INFO, 0, "hello_process: %s is UNI-DIR!!!", 
	    ip_to_str(rt_entry->dest_addr));
#endif
      rt_entry->flags |= UNIDIR;
    } else if(rt_entry->flags & UNIDIR) {
      rt_entry->flags &= ~UNIDIR;
#ifdef DEBUG
      log(LOG_INFO, 0, "hello_process: %s is BI-DIR!!!", 
	  ip_to_str(rt_entry->dest_addr));
#endif
    }
    /* Update sequence numbers if the hello contained new info... */
   /*  if(hello_seqno < rt_entry->dest_seqno) */
    /*       hello_seqno = rt_entry->dest_seqno; */
   
    if(rt_entry->flags & UNIDIR)
      rt_entry->dest_seqno = hello_seqno;
    else {
      rt_table_update(rt_entry, hello_dst, 1, hello_seqno,
		      ACTIVE_ROUTE_TIMEOUT, NEIGHBOR);
    } 
  }
  /* Only update hello timeout for routes which are not unidir */
  if(!(rt_entry->flags & UNIDIR)) 
    hello_update_timeout(rt_entry, ALLOWED_HELLO_LOSS*hello_interval);
  
  if(receive_n_hellos)
    rt_entry->last_hello_time = now;
  return;
}
/* Used to update neighbor when non-hello AODV message is received... */
void hello_process_non_hello(AODV_msg *aodv_msg, u_int32_t source) {
  rt_table_t *rt_entry = NULL;
  u_int32_t seqno = 0;
  u_int32_t rt_lifetime = ALLOWED_HELLO_LOSS * HELLO_INTERVAL;
  
  switch(aodv_msg->type) {
  case AODV_RREQ:
    if(((RREQ *)aodv_msg)->orig_addr == source)
      seqno = ntohl(((RREQ *)aodv_msg)->orig_seqno);
    break;
  case AODV_RREP:
     rt_lifetime = ((RREP *)aodv_msg)->lifetime;
    break;
  case AODV_RERR:
    break;
  default:
    break;
  }
  rt_entry = rt_table_find(source);
  
  /* Check message type, and if we can retrieve a sequence number */
  if(rt_entry == NULL)
    rt_entry = rt_table_insert(source, source, 1, seqno,  
			       rt_lifetime, NEIGHBOR);  
  else {
    /* Don't update anything if this is a uni-directional link... */
    if(rt_entry->flags & UNIDIR)
      return;
    
    if(seqno > rt_entry->dest_seqno)
      rt_table_update(rt_entry, source, 1, seqno,
		      rt_lifetime, NEIGHBOR);
    else
      rt_table_update_timeout(rt_entry, rt_lifetime);
  }
  hello_update_timeout(rt_entry, ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
}

#define HELLO_DELAY 0 /* The extra time we should allow an hello
			 message to take (due to processing) before
			 assuming lost . */

void hello_update_timeout(rt_table_t *rt_entry, u_int32_t time) {
  timer_remove(rt_entry->hello_timer_id);
  rt_entry->hello_timer_id = timer_new(time + HELLO_DELAY, hello_timeout, 
				       rt_entry);
}

