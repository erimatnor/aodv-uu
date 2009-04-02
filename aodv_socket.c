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

#include <sys/types.h>

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/udp.h>
#include "aodv_socket.h"
#include "timer_queue.h"
#include "aodv_rreq.h"
#include "aodv_rerr.h"
#include "aodv_rrep.h"
#include "params.h"
#include "aodv_hello.h"
#include "aodv_neighbor.h"
#include "debug.h"
#include "defs.h"

#ifdef USE_IW_SPY
#include "link_qual.h"
#endif
#endif				/* NS_PORT */

#ifndef NS_PORT
#define SO_RECVBUF_SIZE 256*1024

static char recv_buf[RECV_BUF_SIZE];
static char send_buf[SEND_BUF_SIZE];

extern int wait_on_reboot, hello_qual_threshold, ratelimit;

#ifdef USE_IW_SPY
extern char *spy_addrs;
#endif

static void aodv_socket_read(int fd);

/* Simple function (based on R. Stevens) to calculate UDP header checksum */
static inline u_int16_t in_udp_csum(unsigned long saddr,
				    unsigned long daddr,
				    unsigned short len,
				    unsigned short proto,
				    unsigned short *buf)
{
    unsigned short *w;
    int nleft = ntohs(len);
    int sum = 0;
    struct {
	unsigned short zero:8;
	unsigned short proto:8;
    } proto2 = {0, proto};

    w = (unsigned short *)&saddr;
    sum += *w++;
    sum += *w;

    w = (unsigned short *)&daddr;
    sum += *w++;
    sum += *w;

    sum += *(unsigned short *)&proto2;
    sum += len;

    w = buf;

    while(nleft > 1) {
	sum += *w++;
	nleft -= 2;
    }

    if (nleft == 1)
	sum += *(unsigned char *) w;
                                
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);          
    return ~sum;

}

#endif				/* NS_PORT */


void NS_CLASS aodv_socket_init()
{
#ifndef NS_PORT
    struct sockaddr_in aodv_addr;
    struct ifreq ifr;
    int i, retval = 0;
    int on = 1;
    int tos = IPTOS_LOWDELAY;
    int bufsize = SO_RECVBUF_SIZE;

    /* Create a UDP socket */

    if (this_host.nif == 0) {
	fprintf(stderr, "No interfaces configured\n");
	exit(-1);
    }

    /* Open a socket for every AODV enabled interface */
    for (i = 0; i < MAX_NR_INTERFACES; i++) {
	if (!DEV_NR(i).enabled)
	    continue;
	DEV_NR(i).sock = socket(PF_INET, SOCK_RAW, IPPROTO_UDP);

	if (DEV_NR(i).sock < 0) {
	    perror("");
	    exit(-1);
	}

	/* Bind the socket to the AODV port number */
	memset(&aodv_addr, 0, sizeof(aodv_addr));
	aodv_addr.sin_family = AF_INET;
	aodv_addr.sin_port = htons(AODV_PORT);
	aodv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	retval = bind(DEV_NR(i).sock, (struct sockaddr *) &aodv_addr,
		      sizeof(struct sockaddr));

	if (retval < 0) {
	    perror("Bind failed ");
	    exit(-1);
	}
	if (setsockopt(DEV_NR(i).sock, SOL_SOCKET, SO_BROADCAST,
		       &on, sizeof(int)) < 0) {
	    perror("SO_BROADCAST failed ");
	    exit(-1);
	}

	memset(&ifr, 0, sizeof(struct ifreq));
	strcpy(ifr.ifr_name, DEV_NR(i).ifname);

	if (setsockopt(DEV_NR(i).sock, SOL_SOCKET, SO_BINDTODEVICE,
		       &ifr, sizeof(ifr)) < 0) {
	    fprintf(stderr, "SO_BINDTODEVICE failed for %s", DEV_NR(i).ifname);
	    perror(" ");
	    exit(-1);
	}
	if (setsockopt(DEV_NR(i).sock, SOL_SOCKET, SO_PRIORITY,
		       &tos, sizeof(int)) < 0) {
	    perror("Setsockopt SO_PRIORITY failed ");
	    exit(-1);
	}
	/* We provide our own IP header... */
	if (setsockopt(DEV_NR(i).sock, SOL_IP, IP_HDRINCL,
		       &on, sizeof(int)) < 0) {
	    perror("Setsockopt IP_HDRINCL failed ");
	    exit(-1);
	}

	/* Set max allowable receive buffer size... */
	for (;; bufsize -= 1024) {
	    if (setsockopt(DEV_NR(i).sock, SOL_SOCKET, SO_RCVBUF,
			   (char *) &bufsize, sizeof(bufsize)) == 0) {
		log(LOG_NOTICE, 0, __FUNCTION__,
		    "Receive buffer size set to %d", bufsize);
		break;
	    }
	    if (bufsize < RECV_BUF_SIZE) {
		log(LOG_ERR, 0, __FUNCTION__,
		    "Could not set receive buffer size");
		exit(-1);
	    }
	}

	retval = attach_callback_func(DEV_NR(i).sock, aodv_socket_read);

	if (retval < 0) {
	    perror("register input handler failed ");
	    exit(-1);
	}
    }
#endif				/* NS_PORT */

    num_rreq = 0;
    num_rerr = 0;
}

void NS_CLASS aodv_socket_process_packet(AODV_msg * aodv_msg, int len,
					 u_int32_t src, u_int32_t dst,
					 int ttl, unsigned int ifindex)
{

    /* If this was a HELLO message... Process as HELLO. */
    if ((aodv_msg->type == AODV_RREP && ttl == 1 && dst == AODV_BROADCAST)) {
	hello_process((RREP *) aodv_msg, len, ifindex);
	return;
    }

    /* Make sure we add/update neighbors */
    neighbor_add(aodv_msg, src, ifindex);

    /* Check what type of msg we received and call the corresponding
       function to handle the msg... */
    switch (aodv_msg->type) {

    case AODV_RREQ:
	rreq_process((RREQ *) aodv_msg, len, src, dst, ttl, ifindex);
	break;
    case AODV_RREP:
	DEBUG(LOG_DEBUG, 0, "Received RREP");
	rrep_process((RREP *) aodv_msg, len, src, dst, ttl, ifindex);
	break;
    case AODV_RERR:
	DEBUG(LOG_DEBUG, 0, "Received RERR");
	rerr_process((RERR *) aodv_msg, len, src, dst);
	break;
    case AODV_RREP_ACK:
	DEBUG(LOG_DEBUG, 0, "Received RREP_ACK");
	rrep_ack_process((RREP_ack *) aodv_msg, len, src, dst);
	break;
    default:
	log(LOG_WARNING, 0, __FUNCTION__,
	    "Unknown msg type %u rcvd from %s to %s", aodv_msg->type,
	    ip_to_str(src), ip_to_str(dst));
    }
}

#ifdef NS_PORT
void NS_CLASS recvAODVUUPacket(Packet * p)
{
    int len, i, ttl = 0;
    u_int32_t src, dst;
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);
    hdr_aodvuu *ah = HDR_AODVUU(p);

    src = ih->saddr();
    dst = ih->daddr();
    len = ch->size() - IP_HDR_LEN;
    ttl = ih->ttl();

    AODV_msg *aodv_msg = (AODV_msg *) recv_buf;

    /* Only handle AODVUU packets */
    assert(ch->ptype() == PT_AODVUU);

    /* Only process incoming packets */
    assert(ch->direction() == hdr_cmn::UP);

    /* Copy message to receive buffer */
    memcpy(recv_buf, ah, RECV_BUF_SIZE);

    /* Drop or deallocate packet, depending on the TTL */
    /*   if (ttl == 1) */
/* 	drop(p, DROP_RTR_TTL); */
/*     else */
    Packet::free(p);

    /* Ignore messages generated locally */
    for (i = 0; i < MAX_NR_INTERFACES; i++)
	if (this_host.devs[i].enabled &&
	    memcmp(&src, &this_host.devs[i].ipaddr, sizeof(u_int32_t)) == 0)
	    return;

    aodv_socket_process_packet(aodv_msg, len, src, dst, ttl, NS_IFINDEX);
}
#else
static void aodv_socket_read(int fd)
{
    u_int32_t src, dst;
    int i, len, ttl;
    AODV_msg *aodv_msg;
    int iph_len;
    struct iphdr *ip;
    struct udphdr *udp;
    struct dev_info *dev;
    
    len = recvfrom(fd, recv_buf, RECV_BUF_SIZE, 0, NULL, NULL);

    if (len < 0 || len < IPHDR_SIZE) {
	log(LOG_WARNING, 0, __FUNCTION__, "receive ERROR!");
	return;
    }
    /* Parse the IP header */
    ip = (struct iphdr *) recv_buf;

    src = ntohl(ip->saddr);
    dst = ntohl(ip->daddr);
    ttl = ip->ttl;
    iph_len = ip->ihl << 2;

    udp = (struct udphdr *) (recv_buf + iph_len);

    if (ntohs(udp->dest) != AODV_PORT && ntohs(udp->source) != AODV_PORT)
	return;

    /* Ignore messages generated locally */
    for (i = 0; i < MAX_NR_INTERFACES; i++)
	if (this_host.devs[i].enabled &&
	    memcmp(&src, &this_host.devs[i].ipaddr, sizeof(u_int32_t)) == 0)
	    return;

    aodv_msg = (AODV_msg *) (recv_buf + iph_len + sizeof(struct udphdr));
    len = ntohs(udp->len) - sizeof(struct udphdr);

    dev = devfromsock(fd);
    
    if (!dev) {
	DEBUG(LOG_ERR, 0, "Could not get device info!\n");
	return;
    }
#ifdef USE_IW_SPY
    
    if (spy_addrs && 
	link_qual_get_from_ip(src, dev->ifname) < hello_qual_threshold)
	return;
#endif				/* USE_IW_SPY */
    
    aodv_socket_process_packet(aodv_msg, len, src, dst, ttl, dev->ifindex);
}
#endif				/* NS_PORT */

void NS_CLASS aodv_socket_send(AODV_msg * aodv_msg, u_int32_t dst, int len,
			       u_int8_t ttl, struct dev_info *dev)
{
    int retval = 0;
    struct timeval now;
    /* Rate limit stuff: */
    
#ifndef NS_PORT
    int totlen = 0;
    struct sockaddr_in dst_addr;

    struct iphdr *ip;
    struct udphdr *udp;

    if (wait_on_reboot && aodv_msg->type == AODV_RREP)
	return;

    /* Create a IP header around the packet... The AODV msg is already
       located in the send buffer and referenced by the in parameter
       "aodv_msg". */

    totlen = sizeof(struct iphdr) + sizeof(struct udphdr) + len;

    ip = (struct iphdr *) send_buf;
    ip->version = IPVERSION;
    ip->ihl = sizeof(struct iphdr) >> 2;
    ip->tos |= IPTOS_PREC_NETCONTROL | IPTOS_LOWDELAY; /* Highest priority */
    ip->tot_len = htons(totlen);
    ip->id = 0; /* Let IP set */
    ip->frag_off = htons(0x4000); /* Set Do Not Fragment bit */
    ip->ttl = ttl;
    ip->protocol = IPPROTO_UDP;
    ip->saddr = htonl(dev->ipaddr);
    ip->daddr = htonl(dst);
    
    ip->check = 0; /* Let kernel calculate */
    
    udp = (struct udphdr *) (send_buf + sizeof(struct iphdr));
    
    udp->source = htons(AODV_PORT);
    udp->dest = htons(AODV_PORT);
    udp->len = htons(len + sizeof(struct udphdr));
    
    udp->check = in_udp_csum(ip->saddr, ip->daddr, udp->len, 
			     ip->protocol, (unsigned short *)udp);

    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_addr.s_addr = htonl(dst);
    dst_addr.sin_port = htons(AODV_PORT);

#else

    /*
       NS_PORT: Sending of AODV_msg messages to other AODV-UU routing agents
       by encapsulating them in a Packet.

       Note: This method is _only_ for sending AODV packets to other routing
       agents, _not_ for forwarding "regular" IP packets!
     */

    /* If we are in waiting phase after reboot, don't send any RREPs */
    if (wait_on_reboot && aodv_msg->type == AODV_RREP)
	return;

    /*
       NS_PORT: Don't allocate packet until now. Otherwise packet uid
       (unique ID) space is unnecessarily exhausted at the beginning of
       the simulation, resulting in uid:s starting at values greater than 0.
     */
    Packet *p = allocpkt();
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);
    hdr_aodvuu *ah = HDR_AODVUU(p);

    // Clear AODVUU part of packet
    memset(ah, '\0', ah->size());

    // Copy message contents into packet
    memcpy(ah, aodv_msg, len);

    // Set common header fields
    ch->ptype() = PT_AODVUU;
    ch->direction() = hdr_cmn::DOWN;
    ch->size() = IP_HDR_LEN + len;
    ch->iface() = -2;
    ch->error() = 0;
    ch->prev_hop_ = (nsaddr_t) dev->ipaddr;

    // Set IP header fields
    ih->saddr() = (nsaddr_t) dev->ipaddr;
    ih->daddr() = (nsaddr_t) dst;
    ih->ttl() = ttl;

    // Note: Port number for routing agents, not AODV port number!
    ih->sport() = RT_PORT;
    ih->dport() = RT_PORT;

    // Fake success
    retval = len;
#endif				/* NS_PORT */

    /* If rate limiting is enabled, check if we are sending either a
       RREQ or a RERR. In that case, drop the outgoing control packet
       if the time since last transmit of that type of packet is less
       than the allowed RATE LIMIT time... */

    if (ratelimit) {
	
	gettimeofday(&now, NULL);
	
	switch (aodv_msg->type) { 
	case AODV_RREQ:	   
	    if (num_rreq == (RREQ_RATELIMIT - 1)) {
		if (timeval_diff(&now, &rreq_ratel[0]) < 1000) {
		    DEBUG(LOG_DEBUG, 0, "RATELIMIT: Dropping RREQ %ld ms", 
			  timeval_diff(&now, &rreq_ratel[0])); 
		    return; 
		} else {
		    memmove(rreq_ratel, &rreq_ratel[1], 
			    sizeof(struct timeval) * (num_rreq - 1));
		    memcpy(&rreq_ratel[num_rreq - 1], &now, 
			   sizeof(struct timeval));
		}
	    } else {
		memcpy(&rreq_ratel[num_rreq], &now, sizeof(struct timeval));
		num_rreq++;
	    }
	    break;
	case AODV_RERR:
	    if (num_rerr == (RERR_RATELIMIT - 1)) {
		if (timeval_diff(&now, &rerr_ratel[0]) < 1000) {
		    DEBUG(LOG_DEBUG, 0, "RATELIMIT: Dropping RERR %ld ms", 
			  timeval_diff(&now, &rerr_ratel[0])); 
		    return; 
		} else {
		    memmove(rerr_ratel, &rerr_ratel[1], 
			    sizeof(struct timeval) * (num_rerr - 1));
		    memcpy(&rerr_ratel[num_rerr - 1], &now, 
			   sizeof(struct timeval));
		}
	    } else {
		memcpy(&rerr_ratel[num_rerr], &now, sizeof(struct timeval));
		num_rerr++;
	    }
	    break;
	}
    }

    /* If we broadcast this message we update the time of last broadcast
       to prevent unnecessary broadcasts of HELLO msg's */
    if (dst == AODV_BROADCAST) {

	gettimeofday(&this_host.bcast_time, NULL);

#ifdef NS_PORT
	ch->addr_type() = NS_AF_NONE;

	sendPacket(p, 0, 0.0);
#else
	retval = sendto(dev->sock, send_buf, totlen, 0,
			(struct sockaddr *) &dst_addr, sizeof(dst_addr));

/* 	retval = send(dev->sock, send_buf, len, 0); */

	if (retval < 0) {
	
	    log(LOG_WARNING, errno, __FUNCTION__, "Failed send to %s",
		ip_to_str(dst));
	    return;
	}
#endif

    } else {

#ifdef NS_PORT
	ch->addr_type() = NS_AF_INET;
	/* We trust the decision of next hop for all AODV messages... */
	/* Add jitter, even for unicast control messages. */
	sendPacket(p, dst, 0.03 * Random::uniform());
#else
	retval = sendto(dev->sock, send_buf, totlen, 0,
			(struct sockaddr *) &dst_addr, sizeof(dst_addr));

	if (retval < 0) {
	    log(LOG_WARNING, errno, __FUNCTION__, "Failed send to %s",
		ip_to_str(dst));
	    return;
	}
#endif
    }

    /* Do not print hello msgs... */
    if (!(aodv_msg->type == AODV_RREP && (dst == AODV_BROADCAST)))
	DEBUG(LOG_INFO, 0, "AODV msg to %s ttl=%d",
	      ip_to_str(dst), ttl, retval);

    return;
}

AODV_msg *NS_CLASS aodv_socket_new_msg(void)
{
#ifndef NS_PORT
    /* Initialize IP header, kernel fills in zero:ed values... */
    memset(send_buf, '\0', SEND_BUF_SIZE);
    
    return (AODV_msg *) (send_buf + IPHDR_SIZE + sizeof(struct udphdr));
#else
    memset(send_buf, '\0', SEND_BUF_SIZE);
    return (AODV_msg *) (send_buf);
#endif			
}

/* Copy an existing AODV message to the send buffer */
AODV_msg *NS_CLASS aodv_socket_queue_msg(AODV_msg * aodv_msg, int size)
{
#ifndef NS_PORT
    memcpy((char *) (send_buf + IPHDR_SIZE + sizeof(struct udphdr)), 
	   aodv_msg, size);
    return (AODV_msg *) (send_buf + IPHDR_SIZE + sizeof(struct udphdr));
#else
    memcpy((char *) send_buf, aodv_msg, size);
    return (AODV_msg *) send_buf;
#endif			
}

void aodv_socket_cleanup(void)
{
#ifndef NS_PORT
    int i;

    for (i = 0; i < MAX_NR_INTERFACES; i++) {
	if (!DEV_NR(i).enabled)
	    continue;
	close(DEV_NR(i).sock);
    }
#endif				/* NS_PORT */
}
