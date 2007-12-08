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
 *****************************************************************************/

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include "aodv_neighbor.h"
#include "aodv_rerr.h"
#include "aodv_hello.h"
#include "aodv_socket.h"
#include "routing_table.h"
#include "params.h"
#include "defs.h"
#include "debug.h"
#endif				/* NS_PORT */


/* Add/Update neighbor from a non HELLO AODV control message... */
void NS_CLASS neighbor_add(AODV_msg * aodv_msg, u_int32_t source,
			   unsigned int ifindex)
{
    rt_table_t *rt = NULL;
    u_int32_t seqno = 0;
    int flags = 0;

    /* According to the draft, the seqno should always be set to
     * invalid when adding neighbors... */
    flags |= RT_INV_SEQNO;

    rt = rt_table_find(source);

    if (!rt) {
	DEBUG(LOG_DEBUG, 0, "%s new NEIGHBOR!", ip_to_str(source));
	rt = rt_table_insert(source, source, 1, seqno,
			     ACTIVE_ROUTE_TIMEOUT, VALID, flags, ifindex);
    } else {
	/* Don't update anything if this is a uni-directional link... */
	if (rt->flags & RT_UNIDIR)
	    return;

	/* If we previously knew a valid sequence number then we
	 * should use it. */
	if (!(rt->flags & RT_INV_SEQNO)) {
	    seqno = rt->dest_seqno;
	    flags = 0;
	}

	rt_table_update(rt, source, 1, seqno, ACTIVE_ROUTE_TIMEOUT,
			VALID, flags);
    }

#ifndef AODVUU_LL_FEEDBACK
    hello_update_timeout(rt, ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
#endif
    return;
}

void NS_CLASS neighbor_link_break(rt_table_t * rt)
{
    /* If hopcount = 1, this is a direct neighbor and a link break has
       occured. Send a RERR with the incremented sequence number */
    RERR *rerr = NULL;
    rt_table_t *rt_u;
    u_int32_t rerr_unicast_dest = 0;
    int i, unicast_rerr = 0;

    if (!rt)
	return;

    if (rt->hcnt != 1) {
	DEBUG(LOG_DEBUG, 0, "%s is not a neighbor, hcnt=%d!!!",
	      ip_to_str(rt->dest_addr), rt->hcnt);
	return;
    }

    DEBUG(LOG_DEBUG, 0, "Link to %s broke!", ip_to_str(rt->dest_addr));
     
    /* Invalidate the entry of the route that broke or timed out... */
    rt_table_invalidate(rt);

    /* Create a route error msg, unless the route is to be repaired */
    if (rt->precursors && !(rt->flags & RT_REPAIR)) {
	rerr = rerr_create(0, rt->dest_addr, rt->dest_seqno);
	DEBUG(LOG_DEBUG, 0, "Added %s as unreachable, seqno=%lu",
	      ip_to_str(rt->dest_addr), rt->dest_seqno);

	if (!rt->precursors->next) {
	    unicast_rerr = 1;
	    rerr_unicast_dest = rt->precursors->neighbor;
	}
    }
   
    /* Purge precursor list: */
    if (!(rt->flags & RT_REPAIR))
	precursor_list_destroy(rt);

    /* Check the routing table for entries which have the unreachable
       destination (dest) as next hop. These entries (destinations)
       cannot be reached either since dest is down. They should
       therefore also be included in the RERR. */
    for (i = 0; i < RT_TABLESIZE; i++) {
	for (rt_u = rt_tbl.tbl[i]; rt_u; rt_u = rt_u->next) {

	    if (rt_u->state == VALID &&
		rt_u->next_hop == rt->dest_addr &&
		rt_u->dest_addr != rt->dest_addr) {

		/* If the link that broke are marked for repair,
		   then do the same for all additional unreachable
		   destinations. */
		if ((rt->flags & RT_REPAIR) && rt_u->hcnt <= MAX_REPAIR_TTL) {

		    rt_u->flags |= RT_REPAIR;
		    DEBUG(LOG_DEBUG, 0, "Marking %s for REPAIR",
			  ip_to_str(rt_u->dest_addr));

		    rt_table_invalidate(rt_u);
		    continue;
		}
		
		rt_table_invalidate(rt_u);

		if (rt_u->precursors) {

		    if (!rerr) {
			rerr = rerr_create(0, rt_u->dest_addr,
					   rt_u->dest_seqno);
			if (!rt_u->precursors->next) {
			    unicast_rerr = 1;
			    rerr_unicast_dest = rt_u->precursors->neighbor;
			}
			DEBUG(LOG_DEBUG, 0,
			      "Added %s as unreachable, seqno=%lu",
			      ip_to_str(rt_u->dest_addr),
			      rt_u->dest_seqno);
		    } else {
			rerr_add_udest(rerr, rt_u->dest_addr,
				       rt_u->dest_seqno);
			if (!(!rt_u->precursors->next &&
			      rerr_unicast_dest ==
			      rt_u->precursors->neighbor))
			    unicast_rerr = 0;
			DEBUG(LOG_DEBUG, 0,
			      "Added %s as unreachable, seqno=%lu",
			      ip_to_str(rt_u->dest_addr),
			      rt_u->dest_seqno);
		    }
		}
		precursor_list_destroy(rt_u);
	    }
	}
    }

    if (rerr) {
	DEBUG(LOG_DEBUG, 0, "RERR created, %d bytes.", RERR_CALC_SIZE(rerr));

	rt_u = rt_table_find(rerr_unicast_dest);

	if (rt_u && rerr->dest_count == 1 && unicast_rerr)
	    aodv_socket_send((AODV_msg *) rerr,
			     rerr_unicast_dest,
			     RERR_CALC_SIZE(rerr), 1,
			     &DEV_IFINDEX(rt_u->ifindex));

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
}
