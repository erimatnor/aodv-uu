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

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include <linux/if_ether.h>
#include <linux/netfilter.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

#include "defs.h"
#include "debug.h"
#include "routing_table.h"
#include "aodv_hello.h"
#include "aodv_rreq.h"
#include "aodv_rerr.h"
#include "libipq.h"
#include "params.h"
#include "timer_queue.h"
#include "aodv_timeout.h"
#include "aodv_socket.h"
#include "seek_list.h"		/* for struct ip_data */
#include "packet_queue.h"
#include "packet_input.h"

#define BUFSIZE 2048

extern int internet_gw_mode, wait_on_reboot, optimized_hellos, llfeedback;
extern struct timer worb_timer;

struct ipq_handle *h;

static void packet_input(int fd);

#endif				/* NS_PORT */

/* Set this if you want lots of debug printouts: */
/* #define DEBUG_PACKET */

#ifndef NS_PORT
static void die(struct ipq_handle *h)
{
    ipq_destroy_handle(h);
    exit(1);
}
#endif

void NS_CLASS packet_input_cleanup()
{
#ifndef NS_PORT
    ipq_destroy_handle(h);
#endif
    return;
}

void NS_CLASS packet_input_init()
{
#ifndef NS_PORT
    int status;

    h = ipq_create_handle(0);

    if (h == NULL) {
	fprintf(stderr, "Initialization failed!\n");
	exit(1);
    }
    /* Copy 100 bytes of payload: ip header 60 bytes + max 20 bytes
       Transport Layer header (TCP) + 20 extra bytes just to be
       sure.... */
    status = ipq_set_mode(h, IPQ_COPY_PACKET, BUFSIZE);
    if (status < 0) {
	die(h);
    }
    if (attach_callback_func(h->fd, packet_input) < 0) {
	log(LOG_ERR, 0, __FUNCTION__, "Could not attach callback.");
    }
#endif				/* NS_PORT */
}

#ifdef NS_PORT
void NS_CLASS processPacket(Packet * p)
#else
static void packet_input(int fd)
#endif
{
    rt_table_t *fwd_rt, *rev_rt, *next_hop_rt, *expired_rt = NULL;
    u_int32_t dest_addr, src_addr;
    u_int8_t rreq_flags = 0;
    unsigned int ifindex;
    struct ip_data *ipd = NULL;

#ifdef NS_PORT
    ifindex = NS_IFINDEX;	// Always use ns interface
    fwd_rt = NULL;		// In case of broadcast we provide no next hop
    ipd = NULL;			// No ICMP messaging

    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);

    src_addr = ih->saddr();
    dest_addr = ih->daddr();

    /*
       Any packets with our IP address as destination arriving here are
       packets that weren't caught by any agent attached to the node.
       Throw away those.
     */
    if (dest_addr == DEV_IFINDEX(ifindex).ipaddr) {
	DEBUG(LOG_WARNING, 0, "Received orphan packet. Throwing it away.");
	Packet::free(p);
	return;
    }

    /* If this is a TCP packet and we don't have a route, we should
       set the gratuituos flag in the RREQ. */
    if (ch->ptype() == PT_TCP) {
	rreq_flags |= RREQ_GRATUITOUS;
    }
#else
    int status;
    char buf[BUFSIZE], *dev_name;
    ipq_packet_msg_t *pkt;
    struct iphdr *ip;
    struct udphdr *udp;
    struct icmphdr *icmp = NULL;

    ipq_read(h, buf, BUFSIZE, 0);

    status = ipq_message_type(buf);

    if (status == NLMSG_ERROR) {
	fprintf(stderr,
		"ERROR packet_input: Check that the ip_queue.o module is loaded.\n");
	die(h);
    }

    pkt = ipq_get_packet(buf);

#ifdef DEBUG_PACKET
    DEBUG(LOG_DEBUG, 0, "Protocol %u indev=%s outdev=%s\n",
	  pkt->hw_protocol, pkt->indev_name, pkt->outdev_name);
#endif

    if (pkt->hook == 0)
	dev_name = pkt->indev_name;
    else if (pkt->hook == 3)
	dev_name = pkt->outdev_name;
    else
	dev_name = NULL;

    /* We know from kaodv.c that this is an IP packet */
    ip = (struct iphdr *) pkt->payload;

    dest_addr = ntohl(ip->daddr);
    src_addr = ntohl(ip->saddr);

    switch (ip->protocol) {
	/* Don't process AODV control packets (UDP on port 654). They
	   are accounted for on the aodv socket */
    case IPPROTO_UDP:
	udp = (struct udphdr *) ((char *) ip + (ip->ihl << 2));
	if (ntohs(udp->dest) == AODV_PORT || ntohs(udp->source) == AODV_PORT)
	    goto accept;
	break;
	/* If this is a TCP packet and we don't have a route, we should
	   set the gratuituos flag in the RREQ. */
    case IPPROTO_TCP:
	rreq_flags |= RREQ_GRATUITOUS;
	break;
	/* We set the gratuitous flag also on ICMP ECHO requests, since
	   the destination will also need a route back for the reply... */
    case IPPROTO_ICMP:
	icmp = (struct icmphdr *) ((char *) ip + (ip->ihl << 2));
	if (icmp->type == ICMP_ECHO)
	    rreq_flags |= RREQ_GRATUITOUS;
#ifdef DEBUG_PACKET
	DEBUG(LOG_INFO, 0, "setting G flag for RREQ to %s",
	      ip_to_str(dest_addr));
#endif

	break;
    }

#ifdef DEBUG_PACKET
    DEBUG(LOG_INFO, 0, "pkt to %s", ip_to_str(dest_addr));
#endif

    if (dev_name) {
	ifindex = name2index(dev_name);
	if (ifindex < 0) {
	    DEBUG(LOG_ERR, 0, "name2index error!");
	    return;
	}
    } else
	ifindex = 0;
#endif				/* NS_PORT */

    /* If the packet is not interesting we just let it go through... */
    if (dest_addr == AODV_BROADCAST ||
	dest_addr == DEV_IFINDEX(ifindex).broadcast ||
	((internet_gw_mode && this_host.gateway_mode)
	 && ((dest_addr & DEV_IFINDEX(ifindex).netmask) !=
	     DEV_IFINDEX(ifindex).broadcast))) {
#ifdef NS_PORT
	sendPacket(p, 0, 0.0);
#else
	goto accept;
#endif
    }

    /* Find the entry of the neighboring node and the destination  (if any). */
    rev_rt = rt_table_find(src_addr);
    fwd_rt = rt_table_find(dest_addr);

    /* UPDATE TIMERS on active forward and reverse routes...  */

    /* When forwarding a packet, we update the lifetime of the
       destination's routing table entry, as well as the entry for the
       next hop neighbor (if not the same). AODV draft 10, section
       6.2. */
    if (fwd_rt && fwd_rt->state == VALID &&
	dest_addr != DEV_IFINDEX(ifindex).ipaddr) {
	rt_table_update_timeout(fwd_rt, ACTIVE_ROUTE_TIMEOUT);

	next_hop_rt = rt_table_find(fwd_rt->next_hop);
	
	if (next_hop_rt && next_hop_rt->state == VALID && 
	    next_hop_rt->dest_addr != fwd_rt->dest_addr)
	    rt_table_update_timeout(next_hop_rt, ACTIVE_ROUTE_TIMEOUT);

    }
    /* Also update the reverse route and reverse next hop along the
       path back, since routes between originators and the destination
       are expected to be symmetric. */
    if (rev_rt && rev_rt->state == VALID) {
	rt_table_update_timeout(rev_rt, ACTIVE_ROUTE_TIMEOUT);

	next_hop_rt = rt_table_find(rev_rt->next_hop);

	if (next_hop_rt && next_hop_rt->state == VALID &&
	    rev_rt && next_hop_rt->dest_addr != rev_rt->dest_addr)
	    rt_table_update_timeout(next_hop_rt, ACTIVE_ROUTE_TIMEOUT);
    }

    /* OK, the timeouts have been updated. Now see if either: 1. The
       packet is for this node -> ACCEPT. 2. The packet is not for this
       node -> Send RERR (someone want's this node to forward packets
       although there is no route) or Send RREQ. */

    /* If the packet is destined for this node, then just accept it. */
    if (dest_addr == DEV_IFINDEX(ifindex).ipaddr)
	goto accept;

    if (!fwd_rt || fwd_rt->state == INVALID ||
	(fwd_rt->hcnt == 1 && (fwd_rt->flags & RT_UNIDIR))) {

	/* Check if the route is marked for repair or is INVALID. In
	 * that case, do a route discovery. */
	if (fwd_rt && (fwd_rt->flags & RT_REPAIR))
	    goto route_discovery;

	/* If a packet is received on the NF_IP_PRE_ROUTING hook,
	   i.e. inbound on the interface and we don't have a route to
	   the destination, we should send an RERR to the source and
	   then drop the package... */
	/* NF_IP_PRE_ROUTING = 0 */
#ifdef NS_PORT
#define PACKET_IS_INBOUND ch->direction() == hdr_cmn::UP
#else
#define PACKET_IS_INBOUND pkt->hook == 0
#endif
	if (PACKET_IS_INBOUND) {

	    u_int32_t rerr_dest;
	    RERR *rerr;
#ifdef NS_PORT
	    DEBUG(LOG_DEBUG, 0,
		  "No route, src=%s dest=%s prev_hop=%s - DROPPING!",
		  ip_to_str(src_addr), ip_to_str(dest_addr),
		  ip_to_str(ch->prev_hop_));
#endif
	    if (fwd_rt) {
		rerr = rerr_create(0, fwd_rt->dest_addr,
				   fwd_rt->dest_seqno);

		rt_table_update_timeout(fwd_rt, DELETE_PERIOD);
	    } else
		rerr = rerr_create(0, dest_addr, 0);
	    
	    DEBUG(LOG_DEBUG, 0, "Sending RERR for unknown dest %s",
		  ip_to_str(dest_addr));
	    
	    /* Unicast the RERR to the source of the data transmission
	     * if possible, otherwise we broadcast it. */
	    
	    if (rev_rt && rev_rt->state == VALID)
		rerr_dest = rev_rt->next_hop;
	    else
		rerr_dest = AODV_BROADCAST;

	    aodv_socket_send((AODV_msg *) rerr, rerr_dest,
			     RERR_CALC_SIZE(rerr), 1, &DEV_IFINDEX(ifindex));

	    if (wait_on_reboot) {
		DEBUG(LOG_DEBUG, 0, "Wait on reboot timer reset.");
		timer_set_timeout(&worb_timer, DELETE_PERIOD);
	    }
#ifdef NS_PORT
	    /* DEBUG(LOG_DEBUG, 0, "Dropping pkt uid=%d", ch->uid()); */
	    drop(p, DROP_RTR_NO_ROUTE);
#else
	    status = ipq_set_verdict(h, pkt->packet_id, NF_DROP, 0, NULL);
	    if (status < 0)
		die(h);
#endif
	    return;
	}

      route_discovery:
	/* Buffer packets... Packets are queued by the ip_queue.o
	   module already. We only need to save the handle id, and
	   return the proper verdict when we know what to do... */

#ifdef NS_PORT
	packet_queue_add(p, dest_addr);
#else
	packet_queue_add(pkt->packet_id, dest_addr);

	/* Already seeking the destination? Then do not allocate any
	   memory or generate a RREQ. */
	if (seek_list_find(dest_addr))
	    return;

	/* If the request is generated locally by an application, we save
	   the IP header + 64 bits of data for sending an ICMP Destination
	   Host Unreachable in case we don't find a route... */
	if (src_addr == DEV_IFINDEX(ifindex).ipaddr) {
	    ipd = (struct ip_data *) malloc(sizeof(struct ip_data));
	    if (ipd == NULL) {
		perror("Malloc for IP data failed!");
		exit(-1);
	    }
	    /* IP header + 64 bits data (8 bytes) */
	    ipd->len = (ip->ihl << 2) + 8;
	    memcpy(ipd->data, ip, ipd->len);
	} else
	    ipd = NULL;
#endif
	if (fwd_rt && (fwd_rt->flags & RT_REPAIR))
	    rreq_local_repair(expired_rt, src_addr, ipd);
	else
	    rreq_route_discovery(dest_addr, rreq_flags, ipd);

	return;

    } else {

      accept:

#ifdef NS_PORT
	/* DEBUG(LOG_DEBUG, 0, "Sending pkt uid=%d", ch->uid()); */
	sendPacket(p, fwd_rt->next_hop, 0.0);
#else
	status = ipq_set_verdict(h, pkt->packet_id, NF_ACCEPT, 0, NULL);
	if (status < 0)
	    die(h);

#endif
	/* When forwarding data, make sure we are sending HELLO messages */
	gettimeofday(&this_host.fwd_time, NULL);

	if (!llfeedback && optimized_hellos)
	    hello_start();
    }
}
