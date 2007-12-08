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
 *
 *****************************************************************************/

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include <netinet/in.h>
#include "aodv_rrep.h"
#include "aodv_neighbor.h"
#include "aodv_hello.h"
#include "routing_table.h"
#include "aodv_timeout.h"
#include "timer_queue.h"
#include "aodv_socket.h"
#include "defs.h"
#include "debug.h"
#include "params.h"

#endif

extern int unidir_hack, optimized_hellos;

RREP *NS_CLASS rrep_create(u_int8_t flags,
			   u_int8_t prefix,
			   u_int8_t hcnt,
			   u_int32_t dest_addr,
			   u_int32_t dest_seqno,
			   u_int32_t orig_addr, 
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

    /* Don't print information about hello messages... */
#ifdef DEBUG_OUTPUT
    if (dest_addr != orig_addr) {
	DEBUG(LOG_DEBUG, 0, "Assembled RREP:");
	log_pkt_fields((AODV_msg *) rrep);
    }
#endif

    return rrep;
}

RREP_ack *NS_CLASS rrep_ack_create()
{
    RREP_ack *rrep_ack;

    rrep_ack = (RREP_ack *) aodv_socket_new_msg();
    rrep_ack->type = AODV_RREP_ACK;

    DEBUG(LOG_DEBUG, 0, "Assembled RREP_ack");

    return rrep_ack;
}

void NS_CLASS rrep_ack_process(RREP_ack * rrep_ack, int rrep_acklen,
			       u_int32_t ip_src, u_int32_t ip_dst)
{
    rt_table_t *rt;

    rt = rt_table_find(ip_src);

    if (rt == NULL) {
	DEBUG(LOG_WARNING, 0, "No RREP_ACK expected for %s", ip_to_str(ip_src));

	return;
    }
    DEBUG(LOG_DEBUG, 0, "Received RREP_ACK from %s", ip_to_str(ip_src));

    /* Remove unexpired timer for this RREP_ACK */
    timer_remove(&rt->ack_timer);
}

void NS_CLASS rrep_send(rt_table_t * rev_rt, rt_table_t * fwd_rt)
{
    RREP *rrep;
    u_int8_t rrep_flags = 0;
    u_int32_t seqno, dest, lifetime;
    int hcnt;
    struct timeval now;

    if (!rev_rt) {
	DEBUG(LOG_WARNING, 0, "Can't send RREP, rev_rt = NULL!");
	return;
    }

    if (fwd_rt == NULL) {
	seqno = this_host.seqno;
	dest = DEV_IFINDEX(rev_rt->ifindex).ipaddr;
	hcnt = 0;
	lifetime = MY_ROUTE_TIMEOUT;
    } else {
	seqno = fwd_rt->dest_seqno;
	dest = fwd_rt->dest_addr;
	hcnt = fwd_rt->hcnt;

	gettimeofday(&now, NULL);
	lifetime = timeval_diff(&fwd_rt->rt_timer.timeout, &now);
    }

    /* Check if we should request a RREP-ACK */
    if ((rev_rt->state == VALID && rev_rt->flags & RT_UNIDIR) ||
	(rev_rt->hcnt == 1 && unidir_hack)) {
	rt_table_t *neighbor = rt_table_find(rev_rt->next_hop);
	
	if (neighbor && neighbor->state == VALID &&
	    !neighbor->ack_timer.used) {
	    /* If the node we received a RREQ for is a neighbor we are
	       probably facing a unidirectional link... Better request a
	       RREP-ack */
	    rrep_flags |= RREP_ACK;
	    neighbor->flags |= RT_UNIDIR;

	    /* Must remove any pending hello timeouts when we set the
	       RT_UNIDIR flag, else the route may expire after we begin to
	       ignore hellos... */
	    timer_remove(&neighbor->hello_timer);
	    neighbor_link_break(neighbor);

	    DEBUG(LOG_DEBUG, 0, "Link to %s is unidirectional!",
		  ip_to_str(neighbor->dest_addr));

	    timer_set_timeout(&neighbor->ack_timer, NEXT_HOP_WAIT);
	}
    }

    DEBUG(LOG_DEBUG, 0, "Sending RREP to %s about %s->%s",
	  ip_to_str(rev_rt->next_hop), ip_to_str(rev_rt->dest_addr),
	  ip_to_str(dest));

    rrep = rrep_create(rrep_flags, 0, hcnt, dest, seqno, rev_rt->dest_addr,
		       lifetime);

    aodv_socket_send((AODV_msg *) rrep, rev_rt->next_hop, RREP_SIZE, MAXTTL,
		     &DEV_IFINDEX(rev_rt->ifindex));

    /* Update precursor lists */
    if (fwd_rt) {
	precursor_add(fwd_rt, rev_rt->next_hop);
	precursor_add(rev_rt, fwd_rt->next_hop);
    }
#ifndef AODVUU_LL_FEEDBACK
	if (optimized_hellos)
	    hello_start();
#endif
}

void NS_CLASS rrep_forward(RREP * rrep, rt_table_t * rev_rt,
			   rt_table_t * fwd_rt, int ttl)
{
    /* Sanity checks... */
    if (!fwd_rt || !rev_rt) {
	DEBUG(LOG_WARNING, 0, "Could not forward RERR because of NULL route!");
	return;
    }

    if (!rrep) {
	DEBUG(LOG_WARNING, 0, "No RREP to forward!");
	return;
    }

    DEBUG(LOG_DEBUG, 0, "Forwarding RREP to %s", ip_to_str(rev_rt->next_hop));

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

	if (neighbor && !neighbor->ack_timer.used) {
	    /* If the node we received a RREQ for is a neighbor we are
	       probably facing a unidirectional link... Better request a
	       RREP-ack */
	    rrep->a = 1;
	    neighbor->flags |= RT_UNIDIR;

	    timer_set_timeout(&neighbor->ack_timer, NEXT_HOP_WAIT);
	}
    }

    rrep = (RREP *) aodv_socket_queue_msg((AODV_msg *) rrep, RREP_SIZE);
    rrep->hcnt = fwd_rt->hcnt;	/* Update the hopcount */

    aodv_socket_send((AODV_msg *) rrep, rev_rt->next_hop, RREP_SIZE, ttl,
		     &DEV_IFINDEX(rev_rt->ifindex));

    precursor_add(fwd_rt, rev_rt->next_hop);
    precursor_add(rev_rt, fwd_rt->next_hop);

    rt_table_update_timeout(rev_rt, ACTIVE_ROUTE_TIMEOUT);
}


void NS_CLASS rrep_process(RREP * rrep, int rreplen, u_int32_t ip_src,
			   u_int32_t ip_dst, int ip_ttl, unsigned int ifindex)
{
    u_int32_t rrep_dest, rrep_orig, rrep_lifetime, rrep_seqno, rrep_new_hcnt;
    u_int8_t pre_repair_hcnt = 0;
    rt_table_t *fwd_rt, *rev_rt;

    /* Convert to correct byte order on affeected fields: */
    rrep_dest = ntohl(rrep->dest_addr);
    rrep_orig = ntohl(rrep->orig_addr);
    rrep_seqno = ntohl(rrep->dest_seqno);
    rrep_lifetime = ntohl(rrep->lifetime);
    /* Increment RREP hop count to account for intermediate node... */
    rrep_new_hcnt = rrep->hcnt + 1;

    if (rreplen < (int) RREP_SIZE) {
	log(LOG_WARNING, 0, __FUNCTION__,
	    "IP data field too short (%u bytes)"
	    " from %s to %s", rreplen, ip_to_str(ip_src), ip_to_str(ip_dst));
	return;
    }

    /* Ignore messages which aim to a create a route to one self */
    if (rrep_dest == DEV_IFINDEX(ifindex).ipaddr)
	return;

    DEBUG(LOG_DEBUG, 0, "from %s about %s->%s",
	  ip_to_str(ip_src), ip_to_str(rrep_orig), ip_to_str(rrep_dest));
#ifdef DEBUG_OUTPUT
    log_pkt_fields((AODV_msg *) rrep);
#endif
    
    /* ---------- CHECK IF WE SHOULD MAKE A FORWARD ROUTE ------------ */
    
    fwd_rt = rt_table_find(rrep_dest);
    rev_rt = rt_table_find(rrep_orig);
    
    if (!fwd_rt) {
	/* We didn't have an existing entry, so we insert a new one. */
	fwd_rt = rt_table_insert(rrep_dest, ip_src, rrep_new_hcnt, rrep_seqno,
				 rrep_lifetime, VALID, 0, ifindex);
    } else if (fwd_rt->flags & RT_INV_SEQNO ||
	       rrep_seqno > fwd_rt->dest_seqno ||
	       (rrep_seqno == fwd_rt->dest_seqno &&
		(fwd_rt->state == INVALID || fwd_rt->flags & RT_UNIDIR || 
		 rrep_new_hcnt < fwd_rt->hcnt))) {
	pre_repair_hcnt = fwd_rt->hcnt;
	fwd_rt = rt_table_update(fwd_rt, ip_src, rrep_new_hcnt, rrep_seqno,
				 rrep_lifetime, VALID, 0);
    } else {
	if (fwd_rt->hcnt > 1) {
	    DEBUG(LOG_DEBUG, 0, "Dropping RREP, fwd_rt->hcnt=%d fwd_rt->seqno=%ld",
		  fwd_rt->hcnt, fwd_rt->dest_seqno);
	}
	return;
    }
    
    
    /* If the RREP_ACK flag is set we must send a RREP
       acknowledgement to the destination that replied... */
    if (rrep->a) {
	RREP_ack *rrep_ack;

	rrep_ack = rrep_ack_create();
	aodv_socket_send((AODV_msg *) rrep_ack, fwd_rt->next_hop,
			 NEXT_HOP_WAIT, MAXTTL, &DEV_IFINDEX(fwd_rt->ifindex));
	/* Remove RREP_ACK flag... */
	rrep->a = 0;
    }

    /* Check if this RREP was for us (i.e. we previously made a RREQ
       for this host). */
    if (rrep_orig == DEV_IFINDEX(ifindex).ipaddr) {

	/* If the route was previously in repair, a NO DELETE RERR
	   should be sent to the source of the route, so that it may
	   choose to reinitiate route discovery for the
	   destination. */
	if (rev_rt && rev_rt->state == VALID && fwd_rt->flags & RT_REPAIR) {
	    if (fwd_rt->hcnt > pre_repair_hcnt) {
		RERR *rerr;
		u_int8_t rerr_flags = 0;

		rerr_flags |= RERR_NODELETE;
		rerr = rerr_create(rerr_flags, fwd_rt->dest_addr,
				   fwd_rt->dest_seqno);

		aodv_socket_send((AODV_msg *) rerr, rev_rt->next_hop,
				 RERR_CALC_SIZE(rerr), 1,
				 &DEV_IFINDEX(fwd_rt->ifindex));
	    }
	}
    } else {
	/* --- Here we FORWARD the RREP on the REVERSE route --- */
	if (rev_rt && rev_rt->state == VALID) {
	    rrep_forward(rrep, rev_rt, fwd_rt, --ip_ttl);
	} else {
	    DEBUG(LOG_DEBUG, 0, "Could not forward RREP - NO ROUTE!!!");
	}
    }
#ifndef AODVUU_LL_FEEDBACK
	if (optimized_hellos)
	    hello_start();
#endif
}

/************************************************************************/

/* Include a Hello Interval Extension on the RREP and return new offset */

int rrep_add_hello_ext(RREP * rrep, int offset, u_int32_t interval)
{
    AODV_ext *ext;

    ext = (AODV_ext *) ((char *) rrep + RREP_SIZE + offset);
    ext->type = RREP_HELLO_INTERVAL_EXT;
    ext->length = sizeof(interval);
    memcpy(AODV_EXT_DATA(ext), &interval, sizeof(interval));

    return (offset + AODV_EXT_SIZE(ext));
}
