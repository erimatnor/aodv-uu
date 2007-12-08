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
#include "aodv_rerr.h"
#include "routing_table.h"
#include "aodv_socket.h"
#include "aodv_timeout.h"
#include "defs.h"
#include "debug.h"
#include "params.h"

#endif

RERR *NS_CLASS rerr_create(u_int8_t flags, u_int32_t dest_addr,
			   u_int32_t dest_seqno)
{
    RERR *rerr;

    DEBUG(LOG_DEBUG, 0, "Assembling RERR about %s seqno=%d",
	  ip_to_str(dest_addr), dest_seqno);

    rerr = (RERR *) aodv_socket_new_msg();
    rerr->type = AODV_RERR;
    rerr->n = (flags & RERR_NODELETE ? 1 : 0);
    rerr->res1 = 0;
    rerr->res2 = 0;
    rerr->dest_addr = htonl(dest_addr);
    rerr->dest_seqno = htonl(dest_seqno);
    rerr->dest_count = 1;

    return rerr;
}

void NS_CLASS rerr_add_udest(RERR * rerr,
			     u_int32_t udest, u_int32_t udest_seqno)
{
    RERR_udest *ud;

    ud = (RERR_udest *) ((char *) rerr + RERR_CALC_SIZE(rerr));
    ud->dest_addr = htonl(udest);
    ud->dest_seqno = htonl(udest_seqno);
    rerr->dest_count++;
}


void NS_CLASS rerr_process(RERR * rerr, int rerrlen, u_int32_t ip_src,
			   u_int32_t ip_dst)
{
    RERR *new_rerr = NULL;
    rt_table_t *rt;
    u_int32_t rerr_dest, rerr_dest_seqno, rerr_unicast_dest = 0;
    int i;
    RERR_udest *udest;

    DEBUG(LOG_DEBUG, 0, "ip_src=%s", ip_to_str(ip_src));

    log_pkt_fields((AODV_msg *) rerr);

    if (rerrlen < ((int) RERR_CALC_SIZE(rerr))) {
	log(LOG_WARNING, 0, __FUNCTION__,
	    "IP data too short (%u bytes) from %s to %s. Should be %d bytes.",
	    rerrlen, ip_to_str(ip_src), ip_to_str(ip_dst),
	    RERR_CALC_SIZE(rerr));

	return;
    }

    /* Check which destinations that are unreachable.  */
    udest = RERR_UDEST_FIRST(rerr);

    while (rerr->dest_count) {

	rerr_dest = ntohl(udest->dest_addr);
	rerr_dest_seqno = ntohl(udest->dest_seqno);
	DEBUG(LOG_DEBUG, 0, "unreachable dest=%s seqno=%lu",
	      ip_to_str(rerr_dest), rerr_dest_seqno);

	rt = rt_table_find(rerr_dest);
	
	if (rt && rt->state == VALID && rt->next_hop == ip_src) {
	    
	    /* Checking sequence numbers here is an out of draft
	     * addition to AODV-UU. It is here because it makes a lot
	     * of sense... */
	    if (rt->dest_seqno > rerr_dest_seqno) {
		DEBUG(LOG_DEBUG, 0, "Udest ignored because of seqno");
		udest = RERR_UDEST_NEXT(udest);
		rerr->dest_count--;
		continue;
	    }
	    DEBUG(LOG_DEBUG, 0, "removing rte %s - WAS IN RERR!!",
		  ip_to_str(rerr_dest));

#ifdef NS_PORT
	    interfaceQueue(rerr_dest, IFQ_DROP_BY_DEST);
#endif
	    /* Invalidate route: */
	    if (!rerr->n)
		rt_table_invalidate(rt);
	    
	    /* (a) updates the corresponding destination sequence number
	       with the Destination Sequence Number in the packet, and */
	    rt->dest_seqno = rerr_dest_seqno;

	    /* (d) check precursor list for emptiness. If not empty, include
	       the destination as an unreachable destination in the
	       RERR... */
	    if (rt->nprec && !(rt->flags & RT_REPAIR)) {
		u_int32_t rerr_dest = FIRST_PREC(rt->precursors)->neighbor;
		if (!new_rerr) {
		    u_int8_t flags = 0;

		    if (rerr->n)
			flags |= RERR_NODELETE;

		    new_rerr = rerr_create(flags, rt->dest_addr,
					   rt->dest_seqno);
		    DEBUG(LOG_DEBUG, 0, "Added %s as unreachable, seqno=%lu",
			  ip_to_str(rt->dest_addr), rt->dest_seqno);

		    if (rt->nprec == 1) 
			rerr_unicast_dest = rerr_dest;
		   
		} else {/* Decide whether new precursors make this a non unicast
			   RERR */
		    rerr_add_udest(new_rerr, rt->dest_addr, rt->dest_seqno);
		    
		    DEBUG(LOG_DEBUG, 0, "Added %s as unreachable, seqno=%lu",
			  ip_to_str(rt->dest_addr), rt->dest_seqno);
		    
		    if (rerr_unicast_dest) {
			    list_t *pos2;
			    list_foreach(pos2, &rt->precursors) {
				precursor_t *pr = (precursor_t *)pos2;
				if (pr->neighbor != rerr_unicast_dest) {
				    rerr_unicast_dest = 0;
				    break;
				}
			    }
			}
		}
	    } else {
		DEBUG(LOG_DEBUG, 0,
		      "Not sending RERR, no precursors or route in RT_REPAIR");
	    }
	    /* We should delete the precursor list for all unreachable
	       destinations. */
	    if (rt->state == INVALID)
		precursor_list_destroy(rt);
	}
	udest = RERR_UDEST_NEXT(udest);
	rerr->dest_count--;
    }				/* End while() */

    /* If a RERR was created, then send it now... */
    if (new_rerr) {

	rt = rt_table_find(rerr_unicast_dest);

	if (rt && new_rerr->dest_count == 1 && rerr_unicast_dest)
	    aodv_socket_send((AODV_msg *) new_rerr,
			     rerr_unicast_dest,
			     RERR_CALC_SIZE(new_rerr), 1,
			     &DEV_IFINDEX(rt->ifindex));

	else if (new_rerr->dest_count > 0) {
	    /* FIXME: Should only transmit RERR on those interfaces
	     * which have precursor nodes for the broken route */
	    for (i = 0; i < MAX_NR_INTERFACES; i++) {
		if (!DEV_NR(i).enabled)
		    continue;
		aodv_socket_send((AODV_msg *) new_rerr, AODV_BROADCAST,
				 RERR_CALC_SIZE(new_rerr), 1, &DEV_NR(i));
	    }
	}
    }
}
