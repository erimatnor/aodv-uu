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

static struct rreq_record *rreq_record_head = NULL;
static struct rreq_record *rreq_record_insert(u_int32_t orig_addr,
					      u_int32_t rreq_id);
static struct rreq_record *rreq_record_find(u_int32_t orig_addr,
					    u_int32_t rreq_id);

static struct blacklist *rreq_blacklist_head = NULL;
struct blacklist *rreq_blacklist_find(u_int32_t dest_addr);

extern int unidir_hack, rreq_gratuitous, expanding_ring_search;
extern int internet_gw_mode;

RREQ *rreq_create(u_int8_t flags, u_int32_t dest_addr,
		  u_int32_t dest_seqno, u_int32_t orig_addr)
{
    RREQ *rreq;

    rreq = (RREQ *) aodv_socket_new_msg();
    rreq->type = AODV_RREQ;
    rreq->res1 = 0;
    rreq->res2 = 0;
    rreq->hcnt = 0;
    rreq->rreq_id = htonl(this_host.rreq_id++);
    rreq->dest_addr = htonl(dest_addr);
    rreq->dest_seqno = htonl(dest_seqno);
    rreq->orig_addr = htonl(orig_addr);

    /* Immediately before a node originates a RREQ flood it must
       increment its sequence number... (AODV draft v.10, section 6.1). */
    rreq->orig_seqno = htonl(++this_host.seqno);

    if (flags & RREQ_JOIN)
	rreq->j = 1;
    if (flags & RREQ_REPAIR)
	rreq->r = 1;
    if (flags & RREQ_GRATUITOUS)
	rreq->g = 1;
#ifdef DEBUG
    log(LOG_DEBUG, 0, "rreq_create: Assembled RREQ %s",
	ip_to_str(dest_addr));
    log_pkt_fields((AODV_msg *) rreq);
#endif

    return rreq;
}

void rreq_process(RREQ * rreq, int rreqlen, u_int32_t ip_src, u_int32_t ip_dst,
		  int ip_ttl, unsigned int ifindex)
{

    AODV_ext *ext;
    RREP *rrep;
    rt_table_t *rev_rt, *fwd_rt = NULL;
    u_int32_t rreq_orig, rreq_dest, rreq_orig_seqno, rreq_dest_seqno;
    u_int32_t rreq_id, life, lifetime;
    u_int8_t flags = 0;
    struct timeval minimal_lifetime, now;

    rreq_id = ntohl(rreq->rreq_id);
    rreq_dest = ntohl(rreq->dest_addr);
    rreq_dest_seqno = ntohl(rreq->dest_seqno);
    rreq_orig = ntohl(rreq->orig_addr);
    rreq_orig_seqno = ntohl(rreq->orig_seqno);

    /* Ignore RREQ's that originated from this node. Either we do this
       or we buffer our own sent RREQ's as we do with others we
       receive. */
    if (rreq_orig == DEV_IFINDEX(ifindex).ipaddr)
	return;

    if (rreqlen < RREQ_SIZE) {
	log(LOG_WARNING, 0,
	    "rreq_process: IP data field too short (%u bytes)"
	    "from %s to %s", rreqlen, ip_to_str(ip_src),
	    ip_to_str(ip_dst));
	return;
    }
#ifdef DEBUG
    log(LOG_DEBUG, 0, "RREQ_process: ip_src=%s rreq_orig=%s rreq_dest=%s",
	ip_to_str(ip_src), ip_to_str(rreq_orig), ip_to_str(rreq_dest));
#endif

    /* Check if the previous hop of the RREQ is in the blacklist set. If
       it is, then ignore the RREQ (AODV draft v.10, section 6.8). */
    if (rreq_blacklist_find(ip_src)) {
#ifdef DEBUG
	log(LOG_DEBUG, 0,
	    "rreq_process: prev hop of RREQ blacklisted, ignoring!");
#endif
	return;
    }

    if (rreq_record_find(rreq_orig, rreq_id)) {
#ifdef DEBUG
	log(LOG_DEBUG, 0,
	    "rreq_process: RREQ already buffered, ignoring!");
#endif
	return;
    }
    /* Now buffer this RREQ so that we don't process a similar RREQ we
       get within PATH_TRAVERSAL_TIME. */
    rreq_record_insert(rreq_orig, rreq_id);

    /*      log(LOG_DEBUG, 0, "rreq_process: RREQ Extenlen = %d", rreqlen - RREQ_SIZE); */

    /* Determine whether there are any RREQ extensions */
    ext = (AODV_ext *) ((char *) rreq + RREQ_SIZE);

    while (rreqlen > RREQ_SIZE) {
	switch (ext->type) {
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
    if (rreq->j) {
	/* We don't have multicasting implemented... */
    }

    /* The node always creates or updates a REVERSE ROUTE entry to the
       source of the RREQ. (AODV draft v.10, section 6.5). */
    rev_rt = rt_table_find(rreq_orig);

    /* Calculate the extended minimal life time as defined in section
       6.5, draft 10. */
    life = PATH_TRAVERSAL_TIME - 2 * rreq->hcnt * NODE_TRAVERSAL_TIME;

    /* Increase hopcount to account for this node as an intermediate
       node */
    rreq->hcnt++;

    if (rev_rt == NULL) {
#ifdef DEBUG
	log(LOG_DEBUG, 0, "rreq_process: rev_rt = NULL; route not found");
#endif
	rev_rt = rt_table_insert(rreq_orig, ip_src, rreq->hcnt,
				 rreq_orig_seqno, life, REV_ROUTE, ifindex);

    } else {

	/* OK, existing entry found. But we update only if either:

	   (i) the Originator Sequence Number in the RREQ is higher than
	   the destination sequence number of the Originator IP Address
	   in the route table, or

	   (ii) the sequence numbers are equal, but the hop count as
	   specified by the RREQ, plus one, is now smaller than the existing
	   hop count in the routing table.
	 */
	if (((rreq_orig_seqno > rev_rt->dest_seqno) ||
	     (rreq_orig_seqno == rev_rt->dest_seqno &&
	      rreq->hcnt < rev_rt->hcnt))) {

	    gettimeofday(&minimal_lifetime, NULL);
	    timeval_add_msec(&minimal_lifetime, life);

	    /* If the expire time of the route is larger than the minimal
	       lifetime, we don't do anything with the expire time,
	       i.e. life = 0 */
	    if (timeval_diff(&rev_rt->rt_timer.timeout, &minimal_lifetime) > 0)
		life = 0;

	    rev_rt = rt_table_update(rev_rt, ip_src, rreq->hcnt,
				     rreq_orig_seqno, life, REV_ROUTE);
	}
    }
  /**** END updating/creating REVERSE route ****/

    /* BAD HACK for Internet gateway support: */
    if (internet_gw_mode &&
	this_host.gateway_mode &&
	(rreq_dest & DEV_IFINDEX(ifindex).netmask) !=
	(DEV_IFINDEX(ifindex).ipaddr & DEV_IFINDEX(ifindex).netmask)) {
	rrep = rrep_create(flags, 0, 0, rreq_dest, this_host.seqno,
			   rreq_orig, MY_ROUTE_TIMEOUT);
	aodv_socket_send((AODV_msg *) rrep, ip_src, RREP_SIZE, MAXTTL, 
			 &DEV_IFINDEX(ifindex));
	return;

    }
    /* Are we the destination of the RREQ?, if so we should immediately send a
       RREP.. */
    if (memcmp(&rreq_dest, &DEV_IFINDEX(ifindex).ipaddr, sizeof(u_int32_t)) == 0) {

	/* WE are the RREQ DESTINATION.  (AODV draft v.10, section 6.6.1).
	   Update the node's own sequence number to the maximum of the
	   current seqno and the one in the RREQ. (Also section 6.1) */
	if (this_host.seqno < rreq_dest_seqno)
	    this_host.seqno = rreq_dest_seqno;

	if ((rreq->hcnt != INFTY && rev_rt->flags & UNIDIR) ||
	    (rreq->hcnt == 1 && unidir_hack)) {
	    rt_table_t *neighbor = rev_rt;

	    /* If the source of the RREQ is not a neighbor we must find the
	       neighbor (link) entry which is the next hop towards the RREQ
	       source... */

	    if (neighbor != NULL && !neighbor->ack_timer.used) {
		/* If the node we received a RREQ for is a neighbor we are
		   probably facing a unidirectional link... Better request a
		   RREP-ack */
		flags |= RREP_ACK;
		neighbor->flags |= UNIDIR;

		/* Must remove any pending hello timeouts when we set the
		   UNIDIR flag, else the route may expire after we begin to
		   ignore hellos... */
		timer_remove(&neighbor->hello_timer);
		route_expire_timeout(neighbor);
#ifdef DEBUG
		log(LOG_DEBUG, 0,
		    "rreq_process: Link to %s is unidirectional!",
		    ip_to_str(neighbor->dest_addr));
#endif
		timer_add_msec(&neighbor->ack_timer, NEXT_HOP_WAIT);
	    }
	}

	/* The destination node copies the value MY_ROUTE_TIMEOUT into the
	   Lifetime field of the RREP. Each node MAY make a separate
	   determination about its value MY_ROUTE_TIMEOUT.  */
	rrep = rrep_create(flags, 0, 0, DEV_IFINDEX(ifindex).ipaddr, 
			   this_host.seqno, rreq_orig, MY_ROUTE_TIMEOUT);
	aodv_socket_send((AODV_msg *) rrep, ip_src, RREP_SIZE, MAXTTL, 
			 &DEV_IFINDEX(rev_rt->ifindex));
	return;
    }

    /* We are an INTERMEDIATE node. 
       - check if we have an active route entry */

    fwd_rt = rt_table_find_active(rreq_dest);

    if (fwd_rt && fwd_rt->dest_seqno >= rreq_dest_seqno) {

	/* GENERATE RREP, i.e we have an ACTIVE route entry that is fresh
	   enough (our destination sequence number for that route is
	   larger than the one in the RREQ). */
	gettimeofday(&now, NULL);
	lifetime = timeval_diff(&fwd_rt->rt_timer.timeout, &now) / 1000;

	rrep =
	    rrep_create(flags, 0, fwd_rt->hcnt, rreq_dest,
			fwd_rt->dest_seqno, rreq_orig, lifetime);

	aodv_socket_send((AODV_msg *) rrep, ip_src, RREP_SIZE, MAXTTL, 
			 &DEV_IFINDEX(ifindex));

	/* Update precursor lists */
	precursor_add(fwd_rt, ip_src);
	precursor_add(rev_rt, fwd_rt->next_hop);

	/* If the GRATUITOUS flag is set, we must also unicast a
	   gratuitous RREP to the destination. */
	if (rreq->g) {
	    rrep =
		rrep_create(flags, 0, rev_rt->hcnt, rreq_orig,
			    rreq_orig_seqno, rreq_dest, lifetime);
	    aodv_socket_send((AODV_msg *) rrep, rreq_dest, RREP_SIZE,
			     MAXTTL, &DEV_IFINDEX(fwd_rt->ifindex));

#ifdef DEBUG
	    log(LOG_INFO, 0,
		"rreq_process: Sending G-RREP to %s with rte to %s",
		ip_to_str(rreq_dest), ip_to_str(rreq_orig));
#endif
	}
    } else if (ip_ttl > 1) {
	int i;
	/* FORWARD the RREQ if the TTL allows it. */
#ifdef DEBUG
	log(LOG_INFO, 0,
	    "rreq_process: forwarding RREQ src=%s, rreq_id=%lu",
	    ip_to_str(rreq_orig), rreq_id);
#endif
	/* Queue the received message in the send buffer */
	rreq = (RREQ *) aodv_socket_queue_msg((AODV_msg *) rreq, rreqlen);
	
	/* Send out on all interfaces */
	for (i = 0; i < MAX_NR_INTERFACES; i++) {
	    if (!DEV_NR(i).enabled)
		continue;
	    aodv_socket_send((AODV_msg *) rreq, AODV_BROADCAST, RREQ_SIZE,
			 --ip_ttl, &DEV_NR(i));
	}
    }
    return;
}

/* Perform route discovery for a unicast destination */

void rreq_route_discovery(u_int32_t dest_addr, u_int8_t flags,
			  struct ip_data *ipd)
{
    RREQ *rreq;
    rt_table_t *rt_entry;
    seek_list_t *seek_entry;
    u_int32_t dest_seqno;
    int ttl, i;

    if (seek_list_find(dest_addr)) {
#ifdef DEBUG
	log(LOG_INFO, 0, "rreq_route_discovery: Already seeking %s",
	    ip_to_str(dest_addr));
#endif
	return;
    }

    if (expanding_ring_search)
	ttl = TTL_START;
    else
	ttl = NET_DIAMETER;	/* This is the TTL if we don't use expanding
				   ring search */

    /* If we already have a route entry, we must check if it is an
       invalid (hopcnt = INFNTY). */
    rt_entry = rt_table_find(dest_addr);

    if (rt_entry == NULL)
	dest_seqno = 0;
    else if (IS_INFTY(rt_entry->hcnt) || (rt_entry->flags & UNIDIR)) {
	/* If a RREP was previously received, the hop count of that
	   packet is remembered as Last Hop Count in the routing table
	   of non active routes.  We use this value (if available) for
	   calculating an intital TTL in the IP header. We also use the
	   last known sequence number... */
	dest_seqno = rt_entry->dest_seqno;

	if (expanding_ring_search)
	    ttl = rt_entry->last_hcnt + TTL_INCREMENT;

	/* A routing table entry waiting for a RREP should not be expunged
	   before PATH_TRAVERSAL_TIME... (Draft 10 section 6.4) */
	rt_table_update_timeout(rt_entry, PATH_TRAVERSAL_TIME);
    } else
	return;			/* Something strange here? */

    /* Remember that we are seeking this destination */
    seek_entry = seek_list_insert(dest_addr, dest_seqno, ttl, flags, ipd);

    /* Set a timer for this RREQ */
    if (expanding_ring_search)
	timer_add_msec(&seek_entry->seek_timer,
		       2 * ttl * NODE_TRAVERSAL_TIME);
    else
	timer_add_msec(&seek_entry->seek_timer, NET_TRAVERSAL_TIME);
#ifdef DEBUG
    log(LOG_DEBUG, 0,
	"rreq_route_discovery: route discovery for %s, ttl=%d",
	ip_to_str(dest_addr), ttl);
#endif
    /* Check if we should force the gratuitous flag... (-g option). */
    if (rreq_gratuitous)
	flags |= RREQ_GRATUITOUS;
    
    /* Broadcast on all interfaces */
    for (i = 0; i < MAX_NR_INTERFACES; i++) {
	if (!DEV_NR(i).enabled)
	    continue;
	rreq = rreq_create(flags, dest_addr, dest_seqno, DEV_NR(i).ipaddr);
	aodv_socket_send((AODV_msg *) rreq, AODV_BROADCAST, RREQ_SIZE, ttl, 
			 &DEV_NR(i));
    }
    return;
}

/* Local repair is not yet completely implemented... */
void rreq_local_repair(rt_table_t * rt_entry)
{
    RREQ *rreq;
    seek_list_t *seek_entry;
    int ttl, i;
    u_int8_t flags = 0;

    if (rt_entry == NULL)
	return;

    if (seek_list_find(rt_entry->dest_addr)) {
	log(LOG_INFO, 0, "rreq_route_discovery: Already seeking %s",
	    ip_to_str(rt_entry->dest_addr));
	return;
    }

    ttl = max(rt_entry->last_hcnt, 0.5 * 1) + LOCAL_ADD_TTL;

    rt_entry->dest_seqno++;

    /* Remember that we are seeking this destination */
    seek_entry =
	seek_list_insert(rt_entry->dest_addr, rt_entry->dest_seqno, ttl,
			 flags, NULL);

    timer_add_msec(&seek_entry->seek_timer, NET_TRAVERSAL_TIME);

#ifdef DEBUG
    log(LOG_DEBUG, 0, "rreq_local_repair: route discovery for %s",
	ip_to_str(rt_entry->dest_addr));
#endif
    /* Check if we should force the gratuitous flag... (-g option). */
    if (rreq_gratuitous)
	flags |= RREQ_GRATUITOUS;

    /* Broadcast on all interfaces */
    for (i = 0; i < MAX_NR_INTERFACES; i++) {
	if (!DEV_NR(i).enabled)
	    continue;
	rreq = rreq_create(flags, rt_entry->dest_addr, rt_entry->dest_seqno, 
			   DEV_NR(i).ipaddr);
	aodv_socket_send((AODV_msg *) rreq, AODV_BROADCAST, RREQ_SIZE, ttl, &DEV_NR(i));
    }
    return;
}

static struct rreq_record *rreq_record_insert(u_int32_t orig_addr,
					      u_int32_t rreq_id)
{

    struct rreq_record *rreq_pkt;

    /* First check if this rreq packet is already buffered */
    rreq_pkt = rreq_record_find(orig_addr, rreq_id);

    /* If already buffered, should we update the timer??? Section 8.3 in
       the draft is not clear about this. */
    if (rreq_pkt)
	return rreq_pkt;

    if ((rreq_pkt = malloc(sizeof(struct rreq_record))) < 0) {
	fprintf(stderr, "rreq_record_insert: Malloc failed!!!\n");
	exit(-1);
    }
    rreq_pkt->orig_addr = orig_addr;
    rreq_pkt->rreq_id = rreq_id;
    rreq_pkt->rec_timer.handler = rreq_record_timeout;
    rreq_pkt->rec_timer.data = rreq_pkt;
    rreq_pkt->next = rreq_record_head;
    rreq_record_head = rreq_pkt;
#ifdef DEBUG
    log(LOG_INFO, 0, "Buffering RREQ %s rreq_id=%lu time=%u",
	ip_to_str(orig_addr), rreq_id, PATH_TRAVERSAL_TIME);
#endif
    timer_add_msec(&rreq_pkt->rec_timer, PATH_TRAVERSAL_TIME);
    return rreq_pkt;
}


static struct rreq_record *rreq_record_find(u_int32_t orig_addr,
					    u_int32_t rreq_id)
{
    struct rreq_record *rreq_pkt;

    rreq_pkt = rreq_record_head;

    while (rreq_pkt != NULL) {
	if (rreq_pkt->orig_addr == orig_addr
	    && (rreq_pkt->rreq_id == rreq_id))
	    return rreq_pkt;

	rreq_pkt = rreq_pkt->next;
    }
    return NULL;
}


int rreq_record_remove(u_int32_t orig_addr, u_int32_t rreq_id)
{
    struct rreq_record *curr, *prev;

    prev = NULL;
    curr = rreq_record_head;
    while (curr != NULL) {
	if (curr->orig_addr == orig_addr && (curr->rreq_id == rreq_id)) {
	    if (prev == NULL)
		rreq_record_head = curr->next;
	    else
		prev->next = curr->next;

	    free(curr);
	    return 0;
	}
	prev = curr;
	curr = curr->next;
    }
    return -1;
}

struct blacklist *rreq_blacklist_insert(u_int32_t dest_addr)
{

    struct blacklist *bl_entry;

    /* First check if this rreq packet is already buffered */
    bl_entry = rreq_blacklist_find(dest_addr);

    /* If already buffered, should we update the timer??? Section 8.3 in
       the draft is not clear about this. */
    if (bl_entry)
	return bl_entry;

    if ((bl_entry = malloc(sizeof(struct blacklist))) < 0) {
	fprintf(stderr, "rreq_blacklist_insert: Malloc failed!!!\n");
	exit(-1);
    }
    bl_entry->dest_addr = dest_addr;
    bl_entry->bl_timer.handler = rreq_blacklist_timeout;
    bl_entry->bl_timer.data = bl_entry;
    bl_entry->next = rreq_blacklist_head;
    rreq_blacklist_head = bl_entry;

    timer_add_msec(&bl_entry->bl_timer, BLACKLIST_TIMEOUT);
    return bl_entry;
}
struct blacklist *rreq_blacklist_find(u_int32_t dest_addr)
{
    struct blacklist *bl_entry;

    bl_entry = rreq_blacklist_head;

    while (bl_entry != NULL) {
	if (bl_entry->dest_addr == dest_addr)
	    return bl_entry;
	bl_entry = bl_entry->next;
    }
    return NULL;
}

int rreq_blacklist_remove(u_int32_t dest_addr)
{
    struct blacklist *curr, *prev;

    prev = NULL;
    curr = rreq_blacklist_head;
    while (curr != NULL) {
	if (curr->dest_addr == dest_addr) {
	    if (prev == NULL)
		rreq_blacklist_head = curr->next;
	    else
		prev->next = curr->next;

	    free(curr);
	    return 0;
	}
	prev = curr;
	curr = curr->next;
    }
    return -1;
}
