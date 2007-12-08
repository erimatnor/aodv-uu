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
#include <netinet/in.h>

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include "aodv_rerr.h"
#include "routing_table.h"
#include "aodv_socket.h"
#include "defs.h"
#include "debug.h"
#endif

RERR *NS_CLASS rerr_create(u_int8_t flags, u_int32_t dest_addr,
			   u_int32_t dest_seqno)
{
    RERR *rerr;

    DEBUG(LOG_DEBUG, 0, "rerr_create: Assembling RERR about %s seqno=%d",
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
    rt_table_t *entry;
    u_int32_t rerr_dest, rerr_dest_seqno, rerr_unicast_dest = 0;
    int i, unicast_rerr = 0;
    RERR_udest *udest;

    DEBUG(LOG_DEBUG, 0, "RERR_process: ip_src=%s", ip_to_str(ip_src));
#ifdef DEBUG_OUTPUT
    log_pkt_fields((AODV_msg *) rerr);
#endif

    if (rerrlen < ((int) RERR_CALC_SIZE(rerr))) {
	log(LOG_WARNING, 0,
	    "rerr_process: IP data too short (%u bytes) from %s to %s. Should be %d bytes.",
	    rerrlen, ip_to_str(ip_src), ip_to_str(ip_dst),
	    RERR_CALC_SIZE(rerr));

	return;
    }

    /* Check which destinations that are unreachable.  */
    udest = RERR_UDEST_FIRST(rerr);

    while (rerr->dest_count) {

	rerr_dest = ntohl(udest->dest_addr);
	rerr_dest_seqno = ntohl(udest->dest_seqno);
	DEBUG(LOG_DEBUG, 0, "rerr_process: unreachable dest=%s seqno=%lu",
	      ip_to_str(rerr_dest), rerr_dest_seqno);


	if ((entry = rt_table_find_active(rerr_dest)) != NULL &&
	    memcmp(&entry->next_hop, &ip_src, sizeof(u_int32_t)) == 0) {


	    DEBUG(LOG_DEBUG, 0,
		  "rerr_process: removing rte %s - WAS IN RERR!!",
		  ip_to_str(rerr_dest));

	    /* Invalidate route: Hop count -> INFTY, dest_seqno++ */
	    if (entry->hcnt != INFTY && !rerr->n)
		rt_table_invalidate(entry);

	    /* (a) updates the corresponding destination sequence number
	       with the Destination Sequence Number in the packet, and */
	    entry->dest_seqno = rerr_dest_seqno;

	    /* (d) check precursor list for emptiness. If not empty, include
	       the destination as an unreachable destination in the
	       RERR... */
	    if (entry->precursors) {
		if (new_rerr == NULL) {
		    u_int8_t flags = 0;

		    if (rerr->n)
			flags |= RERR_NODELETE;

		    new_rerr = rerr_create(flags, entry->dest_addr,
					   entry->dest_seqno);
		    DEBUG(LOG_DEBUG, 0,
			  "rerr_process: Added %s as unreachable, seqno=%lu",
			  ip_to_str(entry->dest_addr), entry->dest_seqno);

		    if (!entry->precursors->next) {
			unicast_rerr = 1;
			rerr_unicast_dest = entry->precursors->neighbor;
		    }
		} else {
		    rerr_add_udest(new_rerr, entry->dest_addr,
				   entry->dest_seqno);
		    DEBUG(LOG_DEBUG, 0,
			  "rerr_process: Added %s as unreachable, seqno=%lu",
			  ip_to_str(entry->dest_addr), entry->dest_seqno);

		    if (!(!entry->precursors->next &&
			  rerr_unicast_dest == entry->precursors->neighbor))
			unicast_rerr = 0;
		}
	    }

	    /* We should delete the precursor list for all unreachable
	       destinations. */
	    if (entry->hcnt == INFTY)
		precursor_list_destroy(entry);
	}
	udest = RERR_UDEST_NEXT(udest);
	rerr->dest_count--;
    }				/* End while() */

    /* If a RERR was created, then send it now... */
    if (new_rerr) {

	entry = rt_table_find(rerr_unicast_dest);

	if (entry && new_rerr->dest_count == 1 && unicast_rerr)
	    aodv_socket_send((AODV_msg *) new_rerr,
			     rerr_unicast_dest,
			     RERR_CALC_SIZE(new_rerr), 1,
			     &DEV_IFINDEX(entry->ifindex));

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
