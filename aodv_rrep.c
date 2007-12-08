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
#include <netinet/in.h>

#include "aodv_rrep.h"
#include "aodv_hello.h"
#include "routing_table.h"
#include "packet_input.h"
#include "aodv_timeout.h"
#include "timer_queue.h"
#include "aodv_socket.h"
#include "seek_list.h"
#include "defs.h"
#include "debug.h"
#include "params.h"

RREP *rrep_create(u_int8_t flags,
		  u_int8_t prefix,
		  u_int8_t hcnt,
		  u_int32_t dest_addr,
		  u_int32_t dest_seqno, u_int32_t orig_addr,
		  u_int32_t life)
{
    RREP *rrep;

    rrep = (RREP *) aodv_socket_new_msg();
    rrep->type = AODV_RREP;
    rrep->res1 = 0;
    rrep->res2 = 0;
    rrep->prefix = prefix;
    rrep->hcnt = hcnt;
    rrep->dest_addr = htonl(dest_addr);
    rrep->dest_seqno = htonl(dest_seqno);
    rrep->orig_addr = htonl(orig_addr);
    rrep->lifetime = htonl(life);

    if (flags & RREP_REPAIR)
	rrep->r = 1;
    if (flags & RREP_ACK)
	rrep->a = 1;

#ifdef DEBUG
    /* Don't print information about hello messages... */
    if (dest_addr != orig_addr) {
	log(LOG_DEBUG, 0, "Assembled RREP:");
	log_pkt_fields((AODV_msg *) rrep);
    }
#endif

    return rrep;
}

RREP_ack *rrep_ack_create()
{
    RREP_ack *rrep_ack;

    rrep_ack = (RREP_ack *) aodv_socket_new_msg();
    rrep_ack->type = AODV_RREP_ACK;

#ifdef DEBUG
    log(LOG_DEBUG, 0, "Assembled RREP_ack");
#endif
    return rrep_ack;
}

void
rrep_ack_process(RREP_ack * rrep_ack, int rrep_acklen, u_int32_t ip_src,
		 u_int32_t ip_dst)
{
    rt_table_t *rt_entry;

    rt_entry = rt_table_find(ip_src);

    if (rt_entry == NULL) {
#ifdef DEBUG
	log(LOG_WARNING, 0,
	    "rrep_ack_process: No RREP_ACK expected for %s",
	    ip_to_str(ip_src));
#endif
	return;
    }
#ifdef DEBUG
    log(LOG_DEBUG, 0, "rrep_ack_process: Received RREP_ACK from %s",
	ip_to_str(ip_src));
#endif

    /* Remove unexpired timer for this RREP_ACK */
    if (rt_entry->ack_timer.used)
	timer_remove(&rt_entry->ack_timer);
}

void
rrep_process(RREP * rrep, int rreplen, u_int32_t ip_src, u_int32_t ip_dst,
	     int ip_ttl, unsigned int ifindex)
{
    u_int32_t rrep_dest, rrep_orig, rrep_lifetime, rrep_seqno;
    rt_table_t *fwd_rt, *rev_rt;

    /* Convert to correct byte order on affeected fields: */
    rrep_dest = ntohl(rrep->dest_addr);
    rrep_orig = ntohl(rrep->orig_addr);
    rrep_seqno = ntohl(rrep->dest_seqno);
    rrep_lifetime = ntohl(rrep->lifetime);

    if (rreplen < RREP_SIZE) {
	log(LOG_WARNING, 0,
	    "rrep_process: IP data field too short (%u bytes)"
	    " from %s to %s", rreplen, ip_to_str(ip_src),
	    ip_to_str(ip_dst));
	return;
    }

    /* Ignore messages which aim to a create a route to one self */
    if (memcmp(&rrep_dest, &DEV_IFINDEX(ifindex).ipaddr, sizeof(u_int32_t)) == 0 &&
	memcmp(&rrep_orig, &DEV_IFINDEX(ifindex).ipaddr, sizeof(u_int32_t)) == 0)
	return;

    /* Check if this was a hello message... */
    if (ip_ttl == 1) {
	if (memcmp(&ip_src, &DEV_IFINDEX(ifindex).ipaddr, sizeof(u_int32_t)) != 0)
	    
	    hello_process(rrep, rreplen, ifindex);
	return;
    }
#ifdef DEBUG
    log(LOG_DEBUG, 0, "RREP_process: from=%s about %s->%s",
	ip_to_str(ip_src), ip_to_str(rrep_orig), ip_to_str(rrep_dest));
    log_pkt_fields((AODV_msg *) rrep);
#endif

    /* When a node receives a RREP, it first increments the hop count in
       the RREP, to account for the extra hop */
    rrep->hcnt++;

    /* ---------- CHECK IF WE SHOULD MAKE A FORWARD ROUTE ------------
       We update or insert a forward route only if:

       (i) the Destination Sequence Number in the RREP is greater than
       the node's copy of the destination sequence number, or

       (ii) the sequence numbers are the same, but the route is no
       longer active or the Hop Count in RREP is smaller than the hop
       count in route table entry. */

    fwd_rt = rt_table_find(rrep_dest);

    if (fwd_rt == NULL) {
	/* We didn't have an existing entry, so we insert a new one. */
	fwd_rt = rt_table_insert(rrep_dest, ip_src, rrep->hcnt,
				 rrep_seqno, rrep_lifetime, FWD_ROUTE, ifindex);
    } else if ((rrep_seqno > fwd_rt->dest_seqno) ||
	       (((rrep_seqno == fwd_rt->dest_seqno) &&
		 IS_INFTY(fwd_rt->hcnt)) ||
		(fwd_rt->flags & UNIDIR) || (rrep->hcnt < fwd_rt->hcnt))) {
	fwd_rt = rt_table_update(fwd_rt, ip_src, rrep->hcnt,
				 rrep_seqno, rrep_lifetime, FWD_ROUTE);
    } else
	return;


    /* Check if this RREP was for us (i.e. we previously made a RREQ
       for this host). */
    if (memcmp(&rrep_orig, &DEV_IFINDEX(ifindex).ipaddr, sizeof(u_int32_t)) == 0) {

	/* Remove destination from seeking list since a route has been
	   found. */
	if (seek_list_remove(rrep_dest))
	    packet_buff_send(rrep_dest);

	/* If the RREP_ACK flag is set we must send a RREP
	   acknowledgement to the destination that replied... */
	if (rrep->a) {
	    RREP_ack *rrep_ack;

	    rrep_ack = rrep_ack_create();
	    aodv_socket_send((AODV_msg *) rrep_ack, fwd_rt->next_hop,
			     NEXT_HOP_WAIT, MAXTTL, 
			     &DEV_IFINDEX(fwd_rt->ifindex));
	    /* Remove RREP_ACK flag... */
	    rrep->a = 0;
	}

    } else {
	/* --- Here we FORWARD the RREP on the REVERSE route --- */
	/* If the current node is not the source node as indicated by the
	   Source IP Address in the RREP message AND a forward route has
	   been created or updated as described before, the node consults
	   its route table entry for the source node to determine the next
	   hop for the RREP packet, and then forwards the RREP towards the
	   source with its Hop Count incremented by one. */
	if ((rev_rt = rt_table_find_active(rrep_orig)) != NULL) {

	    /* Here we should do a check if we should request a RREP_ACK,
	       i.e we suspect a unidirectional link.. But how? */
	    if (0) {
		rt_table_t *neighbor;

		/* If the source of the RREP is not a neighbor we must find the
		   neighbor (link) entry which is the next hop towards the RREP
		   source... */
		if (rev_rt->dest_addr != rev_rt->next_hop)
		    neighbor = rt_table_find(rev_rt->next_hop);
		else
		    neighbor = rev_rt;

		if (neighbor != NULL && !neighbor->ack_timer.used) {
		    /* If the node we received a RREQ for is a neighbor we are
		       probably facing a unidirectional link... Better request a
		       RREP-ack */
		    rrep->a = 1;
		    neighbor->flags |= UNIDIR;

		    timer_add_msec(&neighbor->ack_timer, NEXT_HOP_WAIT);
		}
	    }
	    rrep = (RREP *) aodv_socket_queue_msg((AODV_msg *) rrep, rreplen);
	    aodv_socket_send((AODV_msg *) rrep, rev_rt->next_hop,
			     RREP_SIZE, --ip_ttl, &DEV_IFINDEX(rev_rt->ifindex));

	    /* The neighboring nodes to which a RREP was forwarded should be 
	       added as a precursor node. */
	    if (fwd_rt != NULL)
		precursor_add(fwd_rt, rev_rt->next_hop);

	    /* At each node the (reverse) route used to forward the RREP
	       has its lifetime changed to current time plus
	       ACTIVE_ROUTE_TIMEOUT. */
	    rt_table_update_timeout(rev_rt, ACTIVE_ROUTE_TIMEOUT);
	}
    }

}

/************************************************************************/

/* Include a Hello Interval Extension on the RREP return new offset */

int rrep_add_hello_ext(RREP * rrep, int offset, u_int32_t interval)
{
    AODV_ext *ext;

    ext = (AODV_ext *) ((char *) rrep + RREP_SIZE + offset);
    ext->type = RREP_HELLO_INTERVAL_EXT;
    ext->length = sizeof(interval);
    memcpy(AODV_EXT_DATA(ext), &interval, sizeof(interval));

    return (offset + AODV_EXT_SIZE(ext));
}
