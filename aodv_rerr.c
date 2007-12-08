/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University.
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

#include "aodv_rerr.h"
#include "routing_table.h"
#include "aodv_socket.h"
#include "defs.h"
#include "debug.h"

RERR *rerr_create(rt_table_t *entry) {

  RERR *rerr;
  
#ifdef DEBUG
  log(LOG_DEBUG, 0, "create_rerr: Assembling RERR %s , seqno=%d", 
      ip_to_str(entry->dest), entry->dest_seqno);
#endif

  rerr = (RERR *)aodv_socket_new_msg();
  rerr->type = AODV_RERR;
  rerr->reserved = 0;
  rerr->dest_addr = htonl(entry->dest);
  rerr->dest_seqno = entry->dest_seqno;
  rerr->dest_count = 1;

  return rerr;
}

void rerr_add_udest(RERR *rerr, u_int32_t udest, u_int32_t seqno) { 
  RERR_udest *ud;

  ud = (RERR_udest *)((char *)rerr + RERR_SIZE + rerr->dest_count * RERR_UDEST_SIZE);
  ud->dest_addr = htonl(udest);
  ud->dest_seqno = seqno;
  rerr->dest_count++;
}


void rerr_process(RERR *rerr, int rerrlen, u_int32_t ip_src, u_int32_t ip_dst) {
  RERR *new_rerr = NULL;
  rt_table_t *entry;
  u_int32_t rerr_dst;
  RERR_udest *udest;

#ifdef DEBUG
  log(LOG_DEBUG, 0, "rerr_process: ip_src=%s", ip_to_str(ip_src));
  log_pkt_fields((AODV_msg *)rerr);
#endif
 
  if (rerrlen < RERR_CALC_SIZE(rerr->dest_count)) {
    log(LOG_WARNING, 0, "rerr_process: IP data field too short (%u bytes) "\
	"from %s to %s", rerrlen, ip_to_str(ip_src), ip_to_str(ip_dst));

    return;
  }

  /* Check which destinations that are unreachable.  */
  udest = RERR_UDEST_FIRST(rerr);
  
  /* AODV draft 9, section 8.11: For case (iii), the node instead
   makes the list of affected destinations which use the transmitter
   of the received RERR as the next hop, from among those destinations
   listed in the received RERR message. */
  while(rerr->dest_count) {
    
    rerr_dst = ntohl(udest->dest_addr);
#ifdef DEBUG
    log(LOG_DEBUG, 0, "rerr_process: unreachable dest=%s", 
	ip_to_str(rerr_dst));
#endif
    
    if(((entry = rt_table_find_active(rerr_dst)) != NULL) && 
       (memcmp(&entry->next_hop, &ip_src, sizeof(u_int32_t)) == 0)) {
      
      /* (a) updates the corresponding destination sequence number
	 with the Destination Sequence Number in the packet, and */
      /* UGLY HACK: we decrement the seqno by one here, since it will
	 be incremented upon calling rt_table_invalidate() below... */
      entry->dest_seqno = udest->dest_seqno-1;

      /* (d) check precursor list for emptiness. If not empty, include
         the destination as an unreachable destination in the
         RERR... */
      if(entry->precursors != NULL) {
	if(new_rerr == NULL) 
	  new_rerr = rerr_create(entry);
	else
	  rerr_add_udest(new_rerr, entry->dest, entry->dest_seqno);
      }
#ifdef DEBUG
      log(LOG_DEBUG, 0, "rerr_process: removing rte %s - WAS IN RERR!!", 
	  ip_to_str(rerr_dst));
#endif
      /* Invalidate route: Hop count -> INFTY */
      rt_table_invalidate(entry);
    }
    udest = RERR_UDEST_NEXT(udest);
    rerr->dest_count--;
  } /* End while() */

  /* If a RERR was created, then send it now... */
  /* FIXME: Unicast if possible... */
  if(new_rerr) 
    aodv_socket_send((AODV_msg *)new_rerr, AODV_BROADCAST, 
		     RERR_CALC_SIZE(rerr->dest_count), 1); 

}
