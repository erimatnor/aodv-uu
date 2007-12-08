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

extern int internet_gw_mode, wait_on_reboot;
extern struct timer worb_timer;

struct ipq_handle *h;

static void packet_input(int fd);

#endif				/* NS_PORT */

/* Set this if you want lots of debug printouts: */
/* #define DEBUG_PACKET */

#ifndef NS_PORT
static void die(struct ipq_handle *h)
{
    packet_queue_destroy();
    ipq_destroy_handle(h);
    exit(1);
}
#endif

void NS_CLASS packet_input_cleanup()
{
    packet_queue_destroy();

#ifndef NS_PORT
    ipq_destroy_handle(h);
#endif
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
	log(LOG_ERR, 0, "packet_input_init:");
    }
#endif				/* NS_PORT */
}

#ifdef NS_PORT
void NS_CLASS processPacket(Packet * p)
#else
static void packet_input(int fd)
#endif
{
    rt_table_t *fwd_rt, *rev_rt, *repair_rt, *next_hop_rt;
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
	DEBUG(LOG_WARNING, 0,
	      "processPacket: Received orphan packet. Throwing it away.");
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
	DEBUG(LOG_INFO, 0, "packet_input: setting G flag for RREQ to %s",
	      ip_to_str(dest_addr));
#endif
	break;
    }

#ifdef DEBUG_PACKET
    DEBUG(LOG_INFO, 0, "packet_input: pkt to %s", ip_to_str(dest_addr));
#endif

    if (dev_name)
	ifindex = if_nametoindex(dev_name);
    else
	ifindex = 0;
#endif				/* NS_PORT */

    /* If the packet is not interesting we just let it go through... */
    if ((dest_addr == AODV_BROADCAST) ||
	(dest_addr == DEV_IFINDEX(ifindex).ipaddr) ||
	(dest_addr == DEV_IFINDEX(ifindex).broadcast) ||
	((internet_gw_mode && this_host.gateway_mode)
	 && ((dest_addr & DEV_IFINDEX(ifindex).netmask) !=
	     DEV_IFINDEX(ifindex).broadcast)))
	goto accept;

    /* Find the entry of the neighboring node and the destination  (if any). */
    rev_rt = rt_table_find_active(src_addr);
    fwd_rt = rt_table_find_active(dest_addr);


    /* If a packet is received on the NF_IP_PRE_ROUTING hook,
       i.e. inbound on the interface and we don't have a route to the
       destination, we should send an RERR to the source and then drop
       the package... */
    /* NF_IP_PRE_ROUTING = 0 */

#ifdef NS_PORT
#define PACKET_IS_INBOUND ch->direction() == hdr_cmn::UP
#else
#define PACKET_IS_INBOUND pkt->hook == 0
#endif

    if ((dest_addr != DEV_IFINDEX(ifindex).ipaddr) &&
	(!fwd_rt && PACKET_IS_INBOUND)) {
	rt_table_t *rt_entry;
	u_int32_t rerr_dest;
	RERR *rerr;

	DEBUG(LOG_DEBUG, 0, "packet_input: Sending RERR for unknown dest %s",
	      ip_to_str(dest_addr));

	/* There is an expired entry in the routing table we want to send
	   along the seqno in the RERR... */
	rt_entry = rt_table_find(dest_addr);

	if (rt_entry) {
	    rerr = rerr_create(0, rt_entry->dest_addr, rt_entry->dest_seqno);
	    rt_table_update_timeout(rt_entry, DELETE_PERIOD);
	} else
	    rerr = rerr_create(0, dest_addr, 0);

	/* Unicast the RERR to the source of the data transmission if
	 * possible, otherwise we broadcast it. */
	if (rev_rt)
	    rerr_dest = rev_rt->next_hop;
	else
	    rerr_dest = AODV_BROADCAST;

	aodv_socket_send((AODV_msg *) rerr, rerr_dest,
			 RERR_CALC_SIZE(rerr), 1, &DEV_IFINDEX(ifindex));

	if (wait_on_reboot) {
	    DEBUG(LOG_DEBUG, 0, "packet_input: Wait on reboot timer reset.");
	    timer_add_msec(&worb_timer, DELETE_PERIOD);
	}
#ifdef NS_PORT
	drop(p, DROP_RTR_NO_ROUTE);
#else
	status = ipq_set_verdict(h, pkt->packet_id, NF_DROP, 0, NULL);
	if (status < 0)
	    die(h);
#endif
	return;
    }
    /* Check if the route is currently in repair. In that case just
       buffer the packet */
    repair_rt = rt_table_find(dest_addr);

    if (repair_rt && (repair_rt->flags & LREPAIR)) {
#ifdef NS_PORT
	packet_queue_add(p, dest_addr);
#else
	packet_queue_add(pkt->packet_id, dest_addr);
#endif
	return;
    }
    /*  update_timers:  */
    /* When forwarding a packet, we update the lifetime of the
       destination's routing table entry, as well as the entry for the
       next hop neighbor (if not the same). AODV draft 10, section
       6.2. */
    if (fwd_rt && dest_addr != DEV_IFINDEX(ifindex).ipaddr) {
	rt_table_update_timeout(fwd_rt, ACTIVE_ROUTE_TIMEOUT);

	next_hop_rt = rt_table_find_active(fwd_rt->next_hop);

	if (next_hop_rt && next_hop_rt->dest_addr != fwd_rt->dest_addr)
	    rt_table_update_timeout(next_hop_rt, ACTIVE_ROUTE_TIMEOUT);

    }
    /* Also update the reverse route and reverse next hop along the
       path back, since routes between originators and the destination
       are expected to be symmetric. */
    if (rev_rt) {
	rt_table_update_timeout(rev_rt, ACTIVE_ROUTE_TIMEOUT);

	next_hop_rt = rt_table_find_active(rev_rt->next_hop);

	if (next_hop_rt && fwd_rt && 
	    next_hop_rt->dest_addr != fwd_rt->dest_addr)
	    rt_table_update_timeout(next_hop_rt, ACTIVE_ROUTE_TIMEOUT);
    }
#ifdef DEBUG_PACKET
    DEBUG(LOG_INFO, 0, "packet_input: d=%s s=%s",
	  ip_to_str(dest_addr), ip_to_str(src_addr));
#endif				/* DEBUG_PACKET */

    if (!fwd_rt || (fwd_rt->hcnt == 1 && (fwd_rt->flags & UNIDIR))) {
	/* Buffer packets... Packets are queued by the ip_queue_aodv.o module
	   already. We only need to save the handle id, and return the proper
	   verdict when we know what to do... */
#ifdef NS_PORT
	packet_queue_add(p, dest_addr);
#else
	packet_queue_add(pkt->packet_id, dest_addr);

	/* If the request is generated locally by an application, we save
	   the IP header + 64 bits of data for sending an ICMP Destination
	   Host Unreachable in case we don't find a route... */
	if (src_addr == DEV_IFINDEX(ifindex).ipaddr) {
	    ipd = (struct ip_data *) malloc(sizeof(struct ip_data));
	    if (ipd < 0) {
		perror("Malloc for IP data failed!");
		exit(-1);
	    }
	    ipd->len = (ip->ihl << 2) + 8;	/* IP header + 64 bits data (8 bytes) */
	    memcpy(ipd->data, ip, ipd->len);
	} else
	    ipd = NULL;
#endif
	rreq_route_discovery(dest_addr, rreq_flags, ipd);
	return;
    }

  accept:

#ifdef NS_PORT
    if (fwd_rt)
	sendPacket(p, fwd_rt->next_hop, 0.0);
    else
	drop(p, DROP_RTR_NO_ROUTE);
#else
    status = ipq_set_verdict(h, pkt->packet_id, NF_ACCEPT, 0, NULL);
    if (status < 0)
	die(h);
#endif
    return;
}
