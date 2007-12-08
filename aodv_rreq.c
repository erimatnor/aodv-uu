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

#include "aodv_rreq.h"
#include "aodv_rrep.h"
#include "routing_table.h"
#include "aodv_timeout.h"
#include "k_route.h"
#include "timer_queue.h"
#include "aodv_socket.h"
#include "params.h"
#include "seek_list.h"
#include "defs.h"
#include "debug.h"

static struct rreq_record *rreq_flood_record_head = NULL;
static struct rreq_record *rreq_flood_record_insert(u_int32_t src, u_int32_t flood_id);
static struct rreq_record *rreq_flood_record_find(u_int32_t src, u_int32_t flood_id);

static struct blacklist *rreq_blacklist_head = NULL;
struct blacklist *rreq_blacklist_find(u_int32_t dest);

extern int unidir_hack, rreq_gratuitous;

RREQ *rreq_create(u_int8_t flags, u_int32_t dest, int ttl) {
  RREQ *rreq;
  seek_list_t *seek_entry;
  rt_table_t *entry;
  u_int32_t seqno;
  
  /* Are we already seeking this destination? */
  seek_entry = seek_list_find(dest);
  
  /* Register this destination as being sought for... */
  if(seek_entry == NULL)
    seek_entry = seek_list_insert(dest, ttl, flags);
  
  /* Try to find the last known destination sequence number of the 
     destination we want to reach. If none is found we use zero. 
     (AODV draft v.9 section 8.3). */
  entry = rt_table_find(dest);
  if(entry != NULL)
    seqno = entry->dest_seqno;
  else
    seqno = 0;

  rreq = (RREQ *)aodv_socket_new_msg();
  rreq->type = AODV_RREQ;
  rreq->flags = flags;
  rreq->reserved = 0;
  rreq->hcnt = 0;
  rreq->flood_id = this_host->flood_id++; 
  rreq->dest_addr = htonl(dest);
  rreq->dest_seqno = seqno;
  rreq->src_addr = htonl(this_host->ipaddr);

  /* Immediately before a node originates a RREQ flood it must
     increment its sequence number... (AODV draft v.9, section 8.1). */
  rreq->src_seqno = ++this_host->seqno;
  
  log(LOG_DEBUG, 0, "rreq_create: Assembled RREQ, ttl=%d", ttl);
  log_pkt_fields((AODV_msg *)rreq);
  
  return rreq;
}

void rreq_process(RREQ *rreq, int rreqlen, u_int32_t ip_src, u_int32_t ip_dst, int ip_ttl) {
  
  AODV_ext *ext;
  RREP *rrep;
  u_int32_t rreq_src, rreq_dst;
  rt_table_t *rev_rt, *fwd_rt = NULL;
  u_int32_t life, lifetime;
  u_int8_t flags = 0;
  u_int64_t minimal_lifetime;
  
  rreq_src = ntohl(rreq->src_addr);
  rreq_dst = ntohl(rreq->dest_addr);

  /* Ignore RREQ's that originated from this node. Either we do this
     or we buffer our own sent RREQ's as we do with others we
     receive. (Described in section 8.3 of Draft v.9) */
  if(rreq_src == this_host->ipaddr)
    return;
  
  if (rreqlen < RREQ_SIZE) {
    log(LOG_WARNING, 0, "rreq_process: IP data field too short (%u bytes)"\
	"from %s to %s", rreqlen, ip_to_str(ip_src), ip_to_str(ip_dst));
    return;
  }
#ifdef DEBUG
  log(LOG_DEBUG, 0, "RREQ_process: ip_src=%s rreq_src=%s rreq_dst=%s", 
      ip_to_str(ip_src), ip_to_str(rreq_src), ip_to_str(rreq_dst));
#endif
  
  /* Check if the previous hop of the RREQ is in the blacklist set. If
     it is, then ignore the RREQ (AODV draft v.9, section 8.8). */
   if (rreq_blacklist_find(ip_src)) {
#ifdef DEBUG
    log(LOG_DEBUG, 0, "rreq_process: prev hop of RREQ blacklisted, ignoring!");
#endif
    return;
  }
 
  if (rreq_flood_record_find(rreq_src, rreq->flood_id)) {
#ifdef DEBUG
    log(LOG_DEBUG, 0, "rreq_process: RREQ already buffered, ignoring!");
#endif
    return;
  }
  /* Now buffer this RREQ so that we don't process a similar RREQ we
     get within FLOOD_RECORD_TIME. */
  rreq_flood_record_insert(rreq_src, rreq->flood_id);
  
  /*      log(LOG_DEBUG, 0, "rreq_process: RREQ Extenlen = %d", rreqlen - RREQ_SIZE); */

  /* Determine whether there are any RREQ extensions */
  ext = (AODV_ext *)((char *)rreq + RREQ_SIZE);

  while(rreqlen > RREQ_SIZE) {
    switch(ext->type) {
    case RREQ_EXT:
      log(LOG_INFO, 0, "rreq_process: RREQ include EXTENSION");
      /* Do something here */
      break;
    default:
      log(LOG_WARNING, 0, "rreq_process: Unknown extension type %d", 
	  ext->type);
      break;
    }
    rreqlen -= AODV_EXT_SIZE(ext);
    ext = AODV_EXT_NEXT(ext);
    
  }
    

  /*      log_pkt_fields((AODV_msg *)rreq); */

  /* Check J and R flags: */
  if(rreq->flags & RREQ_JOIN) {
    /* We don't have multicasting implemented... */
  }
  if(rreq->flags & RREQ_REPAIR) {
    /* Do something here.. ;-) */
  }
  /* The gratuitous flag is checked below if necessary... */

  /* The node always creates or updates a REVERSE ROUTE entry to the
     source of the RREQ. (AODV draft v.9, section 8.5). */
  rev_rt = rt_table_find(rreq_src);
  
  /* Calculate the extended minimal life time as defined in section
     8.5, draft 9. */
  life = REV_ROUTE_LIFE - rreq->hcnt*NODE_TRAVERSAL_TIME;
    
  /* Increase hopcount to account for this node as an intermediate
     node */
  rreq->hcnt++;

  if(rev_rt == NULL) {
#ifdef DEBUG
    log(LOG_DEBUG, 0, "rreq_process: rev_rt = NULL; route not found");
#endif
    
    /* We didn't have an existing entry, so we insert a new one with
       the values defined in AODV draft v.8, section 8.3.1. */
    rev_rt = rt_table_insert(rreq_src, ip_src, rreq->hcnt, 
			     rreq->src_seqno, life, REV_ROUTE);
    
    rt_table_insert_neighbor(ip_src);

  } else {
    
    /* OK, existing entry found. But we update only if either:
       
       (i) the Source Sequence Number in the RREQ is higher than
       the destination sequence number of the Source IP Address
       in the route table, or
       
       (ii) the sequence numbers are equal, but the hop count as
       specified by the RREQ, plus one, is now smaller than the existing
       hop count in the routing table.
       
       (AODV draft v.9, section 8.5). */
    if(((rreq->src_seqno > rev_rt->dest_seqno) || 
	((rreq->src_seqno == rev_rt->dest_seqno) && 
	 (rreq->hcnt < rev_rt->hcnt)))) {
      
      minimal_lifetime = get_currtime() + life;
      
      /* If the expire time of the route is larger than the minimal
         lifetime, we don't do anything with the expire time,
         i.e. life = 0 */
      if(minimal_lifetime < rev_rt->expire)
	life = 0;
      
      rev_rt = rt_table_update(rev_rt, ip_src, rreq->hcnt, 
			       rreq->src_seqno, life, REV_ROUTE);
      rt_table_insert_neighbor(ip_src);
    } 
  }
  /**** END updating/creating REVERSE route ****/
  
  /* Are we the destination of the RREQ?, if so we should immediately send a
     RREP.. */
  if (memcmp(&rreq_dst, &this_host->ipaddr, sizeof(u_int32_t)) == 0) {
   
    /* WE are the RREQ DESTINATION.  (AODV draft v.9, section 8.6.1).
        Update the nodes own sequence number to the maximum of the
        current seqno and the one in the RREQ. (Also section 8.1) */
    if(this_host->seqno < rreq->dest_seqno)
      this_host->seqno = rreq->dest_seqno; 

    if(rev_rt->flags & UNIDIR ||
       (rreq->hcnt == 1 && unidir_hack)) {
      rt_table_t *neighbor;
      
      /* If the source of the RREQ is not a neigbor we must find the
         neighbor (link) entry which is the next hop towards the RREQ
         source... */
      /*   if(rev_rt->dest != rev_rt->next_hop) */
      /*  	neighbor = rt_table_find(rev_rt->next_hop); */
      /*        else */
      neighbor = rev_rt;

      if(neighbor != NULL && (neighbor->ack_timer_id == 0)) {
	/* If the node we received a RREQ for is a neighbor we are
	   probably facing a unidirectional link... Better request a
	   RREP-ack */
	flags |= RREP_ACK;
	neighbor->flags |= UNIDIR;

	/* Must remove any pending hello timeouts when we set the
           UNIDIR flag, else the route may expire after we begin to
           ignore hellos... */
	timer_remove(neighbor->hello_timer_id);
	
	route_expire_timeout(neighbor);
#ifdef DEBUG
	log(LOG_DEBUG, 0, "rreq_process: Link to %s is unidirectional!",
	    ip_to_str(neighbor->dest));
#endif
	neighbor->ack_timer_id = timer_new(NEXT_HOP_WAIT, rrep_ack_timeout,
					   rev_rt);
      }
    }

    /* The destination node copies the value MY_ROUTE_TIMEOUT into the
       Lifetime field of the RREP. Each node MAY make a separate
       determination about its value MY_ROUTE_TIMEOUT.  */    
    rrep = rrep_create(flags, 0, 0, this_host->ipaddr, this_host->seqno, 
		       rreq_src, MY_ROUTE_TIMEOUT);
    aodv_socket_send((AODV_msg *)rrep, ip_src, RREP_SIZE, MAXTTL);
    return;
  } 
  
  /* We are an INTERMEDIATE node. 
   - check if we have an active route entry */

  fwd_rt = rt_table_find_active(rreq_dst);
  
  if(fwd_rt == NULL) {
    if(ip_ttl > 1)
      goto forward_rreq;
  } else if(fwd_rt->dest_seqno < rreq->dest_seqno)
    goto forward_rreq;
  else
    goto generate_rrep;
  

  /* Ignore the RREQ... */
  return;

 forward_rreq:
 
  /* FORWARD the RREQ (AODV draft v.9, section 8.5).
     
     If the node does not have an active route, it rebroadcasts
     the RREQ from its interface(s) but using its own IP address
     in the IP header of the outgoing RREQ. The Destination
     Sequence Number in the RREQ is updated to the maximum of the
     existing Destination Sequence Number in the RREQ and the
     destination sequence number in the routing table (if an entry
     exists) of the current node.  The TTL or hop limit field in
     the outgoing IP header is decreased by one.  The Hop Count
     field in the broadcast RREQ message is incremented by one, to
     account for the new hop through the intermediate node. */
  
  if(fwd_rt != NULL && (rreq->dest_seqno < fwd_rt->dest_seqno))
    rreq->dest_seqno = fwd_rt->dest_seqno;
  
  
  log(LOG_INFO, 0, "rreq_process: forwarding RREQ src=%s, flood_id=%lu", 
      ip_to_str(rreq_src), rreq->flood_id);
    
  /* Queue the received message in the send buffer */
  rreq = (RREQ *)aodv_socket_queue_msg((AODV_msg *) rreq, rreqlen);
    
  aodv_socket_send((AODV_msg *)rreq, AODV_BROADCAST, RREQ_SIZE, --ip_ttl);
    
  return;

  /* We have an ACTIVE route entry that is fresh enough (i.e. our
     destination sequence number for that route is larger than the one
     in the RREQ). */
  
 generate_rrep:    
  lifetime = fwd_rt->expire - get_currtime();
  
  rrep = rrep_create(flags, 0, fwd_rt->hcnt, rreq_dst, fwd_rt->dest_seqno,
		     rreq_src, lifetime);
    
  aodv_socket_send((AODV_msg *)rrep, ip_src, RREP_SIZE, MAXTTL);
      

  /* Update precursors as described in section 8.6.2 of the AODV
     draft v.9 */
  precursor_add(fwd_rt, ip_src);
  precursor_add(rev_rt, fwd_rt->next_hop);
  
  /* If the gratuitous flag is set, we must also unicast a
     gratuitous RREP to the destination. (AODV draft v.9, section
     8.6.3). */
  if((rreq->flags & RREQ_GRATUITOUS)) {
    /* TODO: Verify that the arguments here are correct... */
    rrep = rrep_create(flags, 0, rev_rt->hcnt, rreq_src, rreq->src_seqno, 
		       rreq_dst, fwd_rt->expire - get_currtime());
    aodv_socket_send((AODV_msg *)rrep, rreq_dst, RREP_SIZE, MAXTTL);

#ifdef DEBUG
    log(LOG_INFO, 0, "rreq_process: Sending G-RREP to %s with rte to %s", 
	ip_to_str(rreq_dst), ip_to_str(rreq_src));
#endif
   
  }
  return;
}

/* Perform route discovery for a unicast destination */

void rreq_route_discovery(u_int32_t dst, u_int8_t flags) {
  RREQ *rreq;
  rt_table_t *entry;
  int ttl;


  /* Immediately before a node originates a RREQ flood, it MUST
     incremented its own sequence number. (Draft 9, section 8.1) */
  this_host->seqno++;
  
  /* Erik: Added this check since routeDiscovery() is called so many
     times by the kernel (when initiated from arp_send()). If we already
     have started a route discovery for this destination, don't start 
     another one...
     Could this be a bad thing if doing RREQ's for other nodes? (i.e. 
     forwarding RREQ's). */
  if(seek_list_find(dst)) {
    log(LOG_INFO, 0, "rreq_route_discovery: Already seeking %s", 
	ip_to_str(dst));
    return;
  }

  /* OK, we are about to send a RREQ... */
  
  /* If we already have a route entry, we must check if it is an
     invalid (hopcnt = INFNTY). */
  entry = rt_table_find(dst);
  
  if (entry == NULL)
    ttl = TTL_START;
  else if(IS_INFTY(entry->hcnt) || (entry->flags & UNIDIR)) {
      /* If a RREP was previously received, the hop count of that
	 packet is remembered as Last Hop Count in the routing table
	 of non active routes.  We use this value (if available) for
	 calculating an intital TTL in the IP header. (AODV draft v.8
	 section 8.2.1). */
      ttl = entry->last_hcnt + TTL_INCREMENT;
  } else
    return; /* Something strange here? */
  
#ifdef DEBUG
  log(LOG_DEBUG, 0, "rreq_route_discovery: route discovery for %s", 
	ip_to_str(dst));
#endif
  /* Check if we should force the gratuitous flag... (-g option). */
  if(rreq_gratuitous)
    flags |= RREQ_GRATUITOUS;
  rreq = rreq_create(flags, dst, ttl);
  aodv_socket_send((AODV_msg *)rreq, AODV_BROADCAST, RREQ_SIZE, ttl);
  return;
}

static struct rreq_record *rreq_flood_record_insert(u_int32_t src, u_int32_t flood_id) {
   
  struct rreq_record *rreq_pkt;

  /* First check if this rreq packet is already buffered */
  rreq_pkt = rreq_flood_record_find(src, flood_id);
  
  /* If already buffered, should we update the timer??? Section 8.3 in
     the draft is not clear about this. */
  if(rreq_pkt)
    return rreq_pkt;
  
  if((rreq_pkt = malloc(sizeof(struct rreq_record))) < 0) {
    fprintf(stderr, "rreq_flood_record_insert: Malloc failed!!!\n");
    exit(-1);
  }
  rreq_pkt->src = src;
  rreq_pkt->flood_id = flood_id;
  rreq_pkt->next = rreq_flood_record_head;
  rreq_flood_record_head = rreq_pkt;
#ifdef DEBUG
  log(LOG_INFO, 0, "Buffering RREQ %s flood_id=%lu time=%u", 
      ip_to_str(src), flood_id, FLOOD_RECORD_TIME);
#endif
  rreq_pkt->timer_id = timer_new(FLOOD_RECORD_TIME, rreq_flood_record_timeout, 
				 rreq_pkt);
  return rreq_pkt;
}


static struct rreq_record *rreq_flood_record_find(u_int32_t src, u_int32_t flood_id) {
  struct rreq_record *rreq_pkt;

  rreq_pkt = rreq_flood_record_head;

  while(rreq_pkt != NULL) {
    if(rreq_pkt->src == src && (rreq_pkt->flood_id == flood_id))
      return rreq_pkt;
    
    rreq_pkt = rreq_pkt->next;
  }
  return NULL;
}


int rreq_flood_record_remove(u_int32_t src, u_int32_t flood_id) {
  struct rreq_record *curr, *prev;

  prev = NULL;
  curr = rreq_flood_record_head;
  while(curr != NULL) {
    if(curr->src == src && (curr->flood_id == flood_id)) {
      if(prev == NULL)
	rreq_flood_record_head = curr->next;
      else
	prev->next = curr->next;
      
      /* Also remove the timer if still there */
      if(curr->timer_id)
	timer_remove(curr->timer_id);
      free(curr);
      return 0;
    }
    prev = curr;
    curr = curr->next;
  }
  return -1;
}

struct blacklist *rreq_blacklist_insert(u_int32_t dest) {
   
  struct blacklist *bl_entry;

  /* First check if this rreq packet is already buffered */
  bl_entry = rreq_blacklist_find(dest);
  
  /* If already buffered, should we update the timer??? Section 8.3 in
     the draft is not clear about this. */
  if(bl_entry)
    return bl_entry;
  
  if((bl_entry = malloc(sizeof(struct blacklist))) < 0) {
    fprintf(stderr, "rreq_blacklist_insert: Malloc failed!!!\n");
    exit(-1);
  }
  bl_entry->dest = dest;
  bl_entry->next = rreq_blacklist_head;
  rreq_blacklist_head = bl_entry;
  
  timer_new(BLACKLIST_TIMEOUT, rreq_blacklist_timeout, bl_entry);
  return bl_entry;
}
struct blacklist *rreq_blacklist_find(u_int32_t dest) {
  struct blacklist *bl_entry;

  bl_entry = rreq_blacklist_head;
  
  while(bl_entry != NULL) {
    if(bl_entry->dest == dest)
      return bl_entry;
    bl_entry = bl_entry->next;
  }
  return NULL;
}
int rreq_blacklist_remove(u_int32_t dest) {
  struct blacklist *curr, *prev;

  prev = NULL;
  curr = rreq_blacklist_head;
  while(curr != NULL) {
    if(curr->dest == dest) {
      if(prev == NULL)
	rreq_blacklist_head = curr->next;
      else
	prev->next = curr->next;
     
      if(curr->timer_id)
	timer_remove(curr->timer_id);
      free(curr);
      return 0;
    }
    prev = curr;
    curr = curr->next;
  }
  return -1;
}
