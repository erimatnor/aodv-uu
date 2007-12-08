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

RERR *rerr_create(u_int8_t flags, u_int32_t dest_addr, u_int32_t dest_seqno) {

  RERR *rerr;
  
#ifdef DEBUG
  log(LOG_DEBUG, 0, "create_rerr: Assembling RERR %s seqno=%d", 
      ip_to_str(dest_addr), dest_seqno);
#endif

  rerr = (RERR *)aodv_socket_new_msg();
  rerr->type = AODV_RERR;
  rerr->n = 0;
  rerr->res1 = 0;
  rerr->res2 = 0;
  rerr->dest_addr = htonl(dest_addr);
  rerr->dest_seqno = htonl(dest_seqno);
  rerr->dest_count = 1;

  return rerr;
}

void rerr_add_udest(RERR *rerr, u_int32_t udest, u_int32_t udest_seqno) { 
  RERR_udest *ud;

  ud = (RERR_udest *)((char *)rerr + RERR_CALC_SIZE(rerr));
  ud->dest_addr = htonl(udest);
  ud->dest_seqno = htonl(udest_seqno);
  rerr->dest_count++;
}


void rerr_process(RERR *rerr, int rerrlen, u_int32_t ip_src, u_int32_t ip_dst) {
  RERR *new_rerr = NULL;
  rt_table_t *entry;
  u_int32_t rerr_dest, rerr_dest_seqno;
  RERR_udest *udest;

#ifdef DEBUG
  log(LOG_DEBUG, 0, "rerr_process: ip_src=%s", ip_to_str(ip_src));
  log_pkt_fields((AODV_msg *)rerr);
#endif
 
  if (rerrlen < RERR_CALC_SIZE(rerr)) {
    log(LOG_WARNING, 0, "rerr_process: IP data too short (%u bytes) from %s to %s", rerrlen, ip_to_str(ip_src), ip_to_str(ip_dst));
    
    return;
  }

  /* Check which destinations that are unreachable.  */
  udest = RERR_UDEST_FIRST(rerr);
  
  while(rerr->dest_count) {
    
    rerr_dest = ntohl(udest->dest_addr);
    rerr_dest_seqno = ntohl(udest->dest_seqno);
#ifdef DEBUG
    log(LOG_DEBUG, 0, "rerr_process: unreachable dest=%s seqno=%ld", 
	ip_to_str(rerr_dest), rerr_dest_seqno);
#endif
    
    if(((entry = rt_table_find_active(rerr_dest)) != NULL) && 
       (memcmp(&entry->next_hop, &ip_src, sizeof(u_int32_t)) == 0)) {
      
      /* (a) updates the corresponding destination sequence number
	 with the Destination Sequence Number in the packet, and */
      /* UGLY HACK: we decrement the seqno by one here, since it will
	 be incremented upon calling rt_table_invalidate() below... */
      entry->dest_seqno = rerr_dest_seqno - 1;

      /* (d) check precursor list for emptiness. If not empty, include
         the destination as an unreachable destination in the
         RERR... */
      if(entry->precursors != NULL) {
	if(new_rerr == NULL) 
	  new_rerr = rerr_create(0, entry->dest_addr, entry->dest_seqno);
	else
	  rerr_add_udest(new_rerr, entry->dest_addr, entry->dest_seqno);
      }
#ifdef DEBUG
      log(LOG_DEBUG, 0, "rerr_process: removing rte %s - WAS IN RERR!!", 
	  ip_to_str(rerr_dest));
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
		     RERR_CALC_SIZE(rerr), 1); 

}
