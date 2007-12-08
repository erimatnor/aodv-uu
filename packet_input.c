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
#include "min_ipenc.h"

#ifdef CONFIG_GATEWAY
#define BUFSIZE 10000 /* Should be enough for most datagrams */
#else
#define BUFSIZE 68 /* Max IP header size (20) + options (40) + 8 bytes data */
#endif

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
    DEBUG(LOG_WARNING, 0, "Netfilter ERROR");
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
    int bufsize = 0;
    int optlen = sizeof(int);

    h = ipq_create_handle(0);

    if (h == NULL) {
	fprintf(stderr, "Initialization failed!\n");
	exit(1);
    }

    status = ipq_set_mode(h, IPQ_COPY_PACKET, BUFSIZE);

    if (status < 0) {
	die(h);
    }
    
    bufsize = 65535 *4;
    
    status = setsockopt(h->fd, SOL_SOCKET, SO_RCVBUF,
			(char *) &bufsize, sizeof(int));
    
    if (status != 0) {
	DEBUG(LOG_WARNING, 0, "Could not set netlink buffer size");
    }
    
    status = getsockopt(h->fd, SOL_SOCKET, SO_RCVBUF, 
			(char *) &bufsize, &optlen);
    
    if (status == 0) {
	DEBUG(LOG_DEBUG, 0, "Netlink socket buffer is %d", bufsize);
    }
    
    if (attach_callback_func(h->fd, packet_input) < 0) {
	alog(LOG_ERR, 0, __FUNCTION__, "Could not attach callback.");
    }
#endif				/* NS_PORT */
}

#ifdef NS_PORT
void NS_CLASS processPacket(Packet * p)
#else
static void packet_input(int fd)
#endif
{
    rt_table_t *fwd_rt, *rev_rt, *next_hop_rt = NULL;
    struct in_addr dest_addr, src_addr;
    u_int8_t rreq_flags = 0;
    unsigned int ifindex;
    struct ip_data *ipd = NULL;
    int pkt_flags = 0;

#ifdef NS_PORT
    ifindex = NS_IFINDEX;	/* Always use ns interface */
    fwd_rt = NULL;		/* For broadcast we provide no next hop */
    ipd = NULL;			/* No ICMP messaging */

    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);

    src_addr.s_addr = ih->saddr();
    dest_addr.s_addr = ih->daddr();

    /* If this is a TCP packet and we don't have a route, we should
       set the gratuituos flag in the RREQ. */
    if (ch->ptype() == PT_TCP) {
	rreq_flags |= RREQ_GRATUITOUS;
    }
#else
    int status;
    char buf[sizeof(struct nlmsghdr)+sizeof(ipq_packet_msg_t)+BUFSIZE];
    char *dev_name;
    ipq_packet_msg_t *pkt;
    struct iphdr *ip;
    struct udphdr *udp;
    struct icmphdr *icmp = NULL;

    status =  ipq_read(h, buf, sizeof(buf), -1);
    
    if (status < 0) {
	DEBUG(LOG_DEBUG, 0, "%s", ipq_errstr());
	ipq_perror(NULL);
	return;
    }

    if (ipq_message_type(buf) == NLMSG_ERROR) {
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

    dest_addr.s_addr = ip->daddr;
    src_addr.s_addr = ip->saddr;

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
#ifdef CONFIG_GATEWAY
    case IPPROTO_MIPE:
	if (internet_gw_mode) {
	    
	    ip = ip_pkt_decapsulate(ip);

	    if (ip == NULL) {
	      DEBUG(LOG_ERR, 0, "Decapsulation failed...");
	      exit(-1);
	    }
	    pkt_flags |= PKT_DEC;
	}
	break;
#endif /* CONFIG_GATEWAY */
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
    if (dest_addr.s_addr == AODV_BROADCAST ||
	dest_addr.s_addr == DEV_IFINDEX(ifindex).broadcast.s_addr) {
#ifdef NS_PORT
	/* Limit Non AODV broadcast packets (Rolf Winter
	 * <winter@pcpool.mi.fu-berlin.de). */
	ih->ttl() = ih->ttl() - 1;                
       
	if(ih->ttl() < 1)        
	    Packet::free(p);                
	else
	    sendPacket(p, dest_addr, 0.0);
	return;
#else
	goto accept;
#endif
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
#ifdef NS_PORT
	p = pkt_encapsulate(p, fwd_rt->next_hop);

	if (p == NULL) {
	    DEBUG(LOG_ERR, 0, "IP Encapsulation failed!");
	   return;	    
	}
	/* Update pointers to headers */
	ch = HDR_CMN(p);
	ih = HDR_IP(p);
#else
	ip = ip_pkt_encapsulate(ip, fwd_rt->next_hop, BUFSIZE);

	if (ip == NULL) {
	    DEBUG(LOG_ERR, 0, "Minimal IP Encapsulation failed!");
	    exit(-1);	    
	}
#endif
	dest_addr = fwd_rt->next_hop;
	fwd_rt = rt_table_find(dest_addr);
	pkt_flags |= PKT_ENC;
    }
#endif /* CONFIG_GATEWAY */

    /* UPDATE TIMERS on active forward and reverse routes...  */

    /* When forwarding a packet, we update the lifetime of the
       destination's routing table entry, as well as the entry for the
       next hop neighbor (if not the same). AODV draft 10, section
       6.2. */
    if (fwd_rt && fwd_rt->state == VALID &&
	dest_addr.s_addr != DEV_IFINDEX(ifindex).ipaddr.s_addr) {

	rt_table_update_timeout(fwd_rt, ACTIVE_ROUTE_TIMEOUT);

	next_hop_rt = rt_table_find(fwd_rt->next_hop);
	
	if (next_hop_rt && next_hop_rt->state == VALID && 
	    next_hop_rt->dest_addr.s_addr != fwd_rt->dest_addr.s_addr)
	    rt_table_update_timeout(next_hop_rt, ACTIVE_ROUTE_TIMEOUT);

    }
    /* Also update the reverse route and reverse next hop along the
       path back, since routes between originators and the destination
       are expected to be symmetric. */
    if (rev_rt && rev_rt->state == VALID) {
	
	rt_table_update_timeout(rev_rt, ACTIVE_ROUTE_TIMEOUT);

	next_hop_rt = rt_table_find(rev_rt->next_hop);

	if (next_hop_rt && next_hop_rt->state == VALID &&
	    rev_rt && next_hop_rt->dest_addr.s_addr != rev_rt->dest_addr.s_addr)
	    rt_table_update_timeout(next_hop_rt, ACTIVE_ROUTE_TIMEOUT);

	/* Update HELLO timer of next hop neighbor if active */   
	if (!llfeedback && next_hop_rt->hello_timer.used) {
	    struct timeval now;
	
	    gettimeofday(&now, NULL);
	    hello_update_timeout(next_hop_rt, &now, 
				 ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
	}
    }

    /* OK, the timeouts have been updated. Now see if either: 1. The
       packet is for this node -> ACCEPT. 2. The packet is not for this
       node -> Send RERR (someone want's this node to forward packets
       although there is no route) or Send RREQ. */

    /* If the packet is destined for this node, then just accept it. */
    if (memcmp(&dest_addr, &DEV_IFINDEX(ifindex).ipaddr, 
	       sizeof(struct in_addr)) == 0) {

#ifdef NS_PORT
	ch->size() -= IP_HDR_LEN;    // cut off IP header size 4/7/99 -dam
	target_->recv(p, (Handler*)0);
	p = 0;
	return;
#else
	goto accept;
#endif
    }
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

	    struct in_addr rerr_dest;
	    RERR *rerr;
#ifdef NS_PORT
	    struct in_addr nh;
	    nh.s_addr = ch->prev_hop_;
	    
	    DEBUG(LOG_DEBUG, 0,
		  "No route, src=%s dest=%s prev_hop=%s - DROPPING!",
		  ip_to_str(src_addr), ip_to_str(dest_addr),
		  ip_to_str(nh));
#endif
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
	packet_queue_add(pkt->packet_id, dest_addr, ip);
	
#ifdef CONFIG_GATEWAY
	/* In gateway mode we handle packets in userspace */
	ipq_set_verdict(h, pkt->packet_id, NF_DROP, 0, NULL);
#endif
	/* Already seeking the destination? Then do not allocate any
	   memory or generate a RREQ. */
	if (seek_list_find(dest_addr))
	    return;

	/* If the request is generated locally by an application, we save
	   the IP header + 64 bits of data for sending an ICMP Destination
	   Host Unreachable in case we don't find a route... */
	if (src_addr.s_addr == DEV_IFINDEX(ifindex).ipaddr.s_addr && ip &&
	    pkt->data_len >= (ip->ihl << 2) + 8) {
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
	    rreq_local_repair(fwd_rt, src_addr, ipd);
	else
	    rreq_route_discovery(dest_addr, rreq_flags, ipd);

	return;

    } else {

#ifdef NS_PORT
	/* DEBUG(LOG_DEBUG, 0, "Sending pkt uid=%d", ch->uid()); */
	sendPacket(p, fwd_rt->next_hop, 0.0);
#else	
      accept:

	if (pkt_flags & PKT_ENC || (pkt_flags & PKT_DEC))
	    status = ipq_set_verdict(h, pkt->packet_id, NF_ACCEPT, 
				     ntohs(ip->tot_len), (unsigned char *)ip);
	else
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

/* EOF */
