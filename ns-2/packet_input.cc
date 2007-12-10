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
 *****************************************************************************/

#include <common/ip.h>

#include "aodv-uu.h"

extern int internet_gw_mode, wait_on_reboot, optimized_hellos, llfeedback;
extern struct timer worb_timer;
/* Set this if you want lots of debug printouts: */
/* #define DEBUG_PACKET */

void NS_CLASS processPacket(Packet * p)
{
    rt_table_t *fwd_rt, *rev_rt;
    struct in_addr dest_addr, src_addr, prev_hop;
    u_int8_t rreq_flags = 0;
    struct ip_data *ipd = NULL;
    int pkt_flags = 0;
    fwd_rt = NULL;		/* For broadcast we provide no next hop */
    ipd = NULL;			/* No ICMP messaging */

    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);

    src_addr.s_addr = ih->saddr();
    dest_addr.s_addr = ih->daddr();
    prev_hop.s_addr = ch->prev_hop_;

    if (ch->direction() == hdr_cmn::NONE) {
	    DEBUG(LOG_DEBUG, 0,
		  "Packet with src=%s dst=%s has no direction. DROPPING!", 
		  ip_to_str(src_addr), ip_to_str(dest_addr));
	    drop(p);
	    return;
    } 
#if DEBUG_PACKET
    if (ch->direction() == hdr_cmn::DOWN) {
	    DEBUG(LOG_DEBUG, 0,
		  "Packet DOWN src=%s dst=%s prev_hop=%s", 
		  ip_to_str(src_addr), ip_to_str(dest_addr), 
		  ip_to_str(prev_hop));
    } else if (ch->direction() == hdr_cmn::UP) {
	    DEBUG(LOG_DEBUG, 0,
		  "Packet UP src=%s dst=%s prev_hop=%s", 
		  ip_to_str(src_addr), ip_to_str(dest_addr), 
		  ip_to_str(prev_hop));
    }
#endif
    /* If this is a TCP packet and we don't have a route, we should
       set the gratuituos flag in the RREQ. */
    if (ch->ptype() == PT_TCP) {
	rreq_flags |= RREQ_GRATUITOUS;
    }

    /* If the packet is a broadcast packet we just let it go
     * through... */
    if (dest_addr.s_addr == IP_BROADCAST ||
	dest_addr.s_addr == DEV_IFINDEX(ifindex).broadcast.s_addr) {
	    
	    DEBUG(LOG_DEBUG, 0,
		  "Broadcast packet src=%s dst=%s prev_hop=%s", 
		  ip_to_str(src_addr), ip_to_str(dest_addr), 
		  ip_to_str(prev_hop));
	    
	    if (ch->direction() == hdr_cmn::DOWN)
		    sendPacket(p, dest_addr, 0.0);
	    else if (ch->direction() == hdr_cmn::UP)
		    target_->recv(p, (Handler*)0);
	return;
    }
    
    /* Find the entry of the neighboring node and the destination  (if any). */
    rev_rt = rt_table_find(src_addr);
    fwd_rt = rt_table_find(dest_addr);

#ifdef CONFIG_GATEWAY
    /* Check if we have a route and it is an Internet destination (Should be
     * encapsulated and routed through the gateway). */
    if (fwd_rt && (fwd_rt->state == VALID) && 
	(fwd_rt->flags & RT_INET_DEST)) {
	/* The destination should be relayed through the IG */

	rt_table_update_timeout(fwd_rt, ACTIVE_ROUTE_TIMEOUT);

	p = pkt_encapsulate(p, fwd_rt->next_hop);

	if (p == NULL) {
	    DEBUG(LOG_ERR, 0, "IP Encapsulation failed!");
	   return;	    
	}
	/* Update pointers to headers */
	ch = HDR_CMN(p);
	ih = HDR_IP(p);

	dest_addr = fwd_rt->next_hop;
	fwd_rt = rt_table_find(dest_addr);
	pkt_flags |= PKT_ENC;
    }
#endif /* CONFIG_GATEWAY */

    /* UPDATE TIMERS on active forward and reverse routes...  */

    rt_table_update_route_timeouts(fwd_rt, rev_rt);

    /* OK, the timeouts have been updated. Now see if either: 1. The
       packet is for this node -> ACCEPT. 2. The packet is not for this
       node -> Send RERR (someone want's this node to forward packets
       although there is no route) or Send RREQ. */

    /* If the packet is destined for this node, then just accept it. */
    if (memcmp(&dest_addr, &DEV_IFINDEX(ifindex).ipaddr, 
	       sizeof(struct in_addr)) == 0) {
	
	ch->size() -= IP_HDR_LEN;    // cut off IP header size 4/7/99 -dam
	target_->recv(p, (Handler*)0);
	p = NULL;
	return;

    }
    if (!fwd_rt || fwd_rt->state == INVALID ||
	(fwd_rt->hcnt == 1 && (fwd_rt->flags & RT_UNIDIR))) {

	/* Check if the route is marked for repair or is INVALID. In
	 * that case, do a route discovery. */
	if (fwd_rt && (fwd_rt->flags & RT_REPAIR))
	    goto route_discovery;

	if (ch->direction() == hdr_cmn::UP) {

	    struct in_addr rerr_dest;
	    RERR *rerr;
	    struct in_addr nh;
	    nh.s_addr = ch->prev_hop_;
	    
	    DEBUG(LOG_DEBUG, 0,
		  "No route, src=%s dest=%s prev_hop=%s - DROPPING!",
		  ip_to_str(src_addr), ip_to_str(dest_addr),
		  ip_to_str(nh));

	    if (fwd_rt) {
		rerr = rerr_create(0, fwd_rt->dest_addr,
				   fwd_rt->dest_seqno);

		rt_table_update_timeout(fwd_rt, DELETE_PERIOD);
	    } else
		rerr = rerr_create(0, dest_addr, 0);
	    
	    DEBUG(LOG_DEBUG, 0, "Sending RERR to prev hop %s for unknown dest %s", ip_to_str(src_addr), ip_to_str(dest_addr));
	    
	    /* Unicast the RERR to the source of the data transmission
	     * if possible, otherwise we broadcast it. */
	    
	    if (rev_rt && rev_rt->state == VALID)
		rerr_dest = rev_rt->next_hop;
	    else
		rerr_dest.s_addr = AODV_BROADCAST;

	    aodv_socket_send((AODV_msg *) rerr, rerr_dest,
			     RERR_CALC_SIZE(rerr), 1, &DEV_IFINDEX(ifindex));

	    if (wait_on_reboot) {
		DEBUG(LOG_DEBUG, 0, "Wait on reboot timer reset.");
		timer_set_timeout(&worb_timer, DELETE_PERIOD);
	    }

	    /* DEBUG(LOG_DEBUG, 0, "Dropping pkt uid=%d", ch->uid()); */
	    drop(p, DROP_RTR_NO_ROUTE);

	    return;
	}

      route_discovery:
	/* Buffer packets... Packets are queued by the ip_queue.o
	   module already. We only need to save the handle id, and
	   return the proper verdict when we know what to do... */

	packet_queue_add(p, dest_addr);

	if (fwd_rt && (fwd_rt->flags & RT_REPAIR))
	    rreq_local_repair(fwd_rt, src_addr, ipd);
	else
	    rreq_route_discovery(dest_addr, rreq_flags, ipd);

	return;

    } else {
	/* DEBUG(LOG_DEBUG, 0, "Sending pkt uid=%d", ch->uid()); */
	sendPacket(p, fwd_rt->next_hop, 0.0);

	/* When forwarding data, make sure we are sending HELLO messages */
	gettimeofday(&this_host.fwd_time, NULL);

	if (!llfeedback && optimized_hellos)
	    hello_start();
    }
}

/* EOF */
