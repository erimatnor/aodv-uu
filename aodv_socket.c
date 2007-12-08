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

/* Uncomment if SOCK_RAW is to be used instead of SOCK_DGRAM.
   SOCK_RAW code is not finished though... */
/*  #define RAW_SOCKET   */

#include <sys/types.h>

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#ifdef RAW_SOCKET
#include <netinet/udp.h>
#endif				/* RAW_SOCKET */
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

/* For some reason, this struct is not picked up from any header file
 * when compiling against ARM... Therefore it must be defined here. */
#ifdef ARM
struct in_pktinfo {
    int ipi_ifindex;
    struct in_addr ipi_spec_dst;
    struct in_addr ipi_addr;
};
#endif				/* ARM */
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
#ifdef RAW_SOCKET
	DEV_NR(i).sock = socket(PF_INET, SOCK_RAW, IPPROTO_UDP);
#else
	DEV_NR(i).sock = socket(PF_INET, SOCK_DGRAM, 0);
#endif

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
#ifdef RAW_SOCKET
	/* We provide our own IP header... */

	if (setsockopt(DEV_NR(i).sock, SOL_IP, IP_HDRINCL,
		       &on, sizeof(int)) < 0) {
	    perror("Setsockopt IP_HDRINCL failed ");
	    exit(-1);
	}
#else

	if (setsockopt(DEV_NR(i).sock, SOL_IP, IP_PKTINFO,
		       &on, sizeof(int)) < 0) {
	    perror("Setsockopt IP_PKTINFO failed ");
	    exit(-1);
	}
	if (setsockopt(DEV_NR(i).sock, SOL_IP, IP_RECVTTL,
		       &on, sizeof(int)) < 0) {
	    perror("Setsockopt IP_TTL failed ");
	    exit(-1);
	}
#endif
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
    time_last_rreq.tv_sec = 0;
    time_last_rreq.tv_usec = 0;
    time_last_rerr.tv_sec = 0;
    time_last_rerr.tv_usec = 0;
/*      gettimeofday(&time_last_rerr, NULL); */
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
    int i, len, ttl = 0;
    AODV_msg *aodv_msg;

#ifdef USE_IW_SPY
    char ifname[IFNAMSIZ];
#endif				/* USE_IW_SPY */

#ifdef RAW_SOCKET
    int iph_len;
    struct iphdr *iph;
    struct udphdr *udph;

    len = recvfrom(fd, recv_buf, RECV_BUF_SIZE, 0, NULL, NULL);

    if (len < 0 || len < IPHDR_SIZE) {
	log(LOG_WARNING, 0, __FUNCTION__, "receive ERROR!");
	return;
    }
    /* Parse the IP header */
    iph = (struct iphdr *) recv_buf;

    src = ntohl(iph->saddr);
    dst = ntohl(iph->daddr);
    ttl = iph->ttl;
    iph_len = iph->ihl << 2;

    udph = (struct udphdr *) (recv_buf + iph_len);

    if (ntohs(udph->dest) != AODV_PORT && ntohs(udph->source) != AODV_PORT)
	return;

    /* Ignore messages generated locally */
    for (i = 0; i < MAX_NR_INTERFACES; i++)
	if (this_host.devs[i].enabled &&
	    memcmp(&src, &this_host.devs[i].ipaddr, sizeof(u_int32_t)) == 0)
	    return;

    aodv_msg = (AODV_msg *) (recv_buf + iph_len + sizeof(struct udphdr));
    len = ntohs(udph->len) - sizeof(struct udphdr);

#else
    struct sockaddr_in src_addr;
    struct msghdr msg;
    union {
	struct cmsghdr cm;
	char control[CMSG_SPACE(sizeof(int)) +
		     CMSG_SPACE(sizeof(struct in_pktinfo))];
    } control_union;
    struct cmsghdr *cmsg;
    struct in_pktinfo pktinfo;
    int sockaddr_len = sizeof(struct sockaddr_in);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = NULL;
    msg.msg_iovlen = 0;
    msg.msg_control = control_union.control;
    msg.msg_controllen = sizeof(control_union.control);

    /* Get the information control message first */
    if ((len = recvmsg(fd, &msg, MSG_PEEK)) < 0) {
	log(LOG_WARNING, 0, __FUNCTION__, "recvmsg ERROR!");
	return;
    }
    /* Read the data payload (i.e. AODV msg) */
    len = recvfrom(fd, recv_buf, RECV_BUF_SIZE, 0,
		   (struct sockaddr *) &src_addr, &sockaddr_len);

    if (len < 0) {
	log(LOG_WARNING, 0, __FUNCTION__, "receive ERROR!");
	return;
    }
    aodv_msg = (AODV_msg *) (recv_buf);
    src = ntohl(src_addr.sin_addr.s_addr);

    /* Ignore messages generated locally */
    for (i = 0; i < MAX_NR_INTERFACES; i++)
	if (this_host.devs[i].enabled &&
	    memcmp(&src, &this_host.devs[i].ipaddr, sizeof(u_int32_t)) == 0)
	    return;

    /* Get the TTL and pktinfo struct (destination address) from the
       control messages... For some reason the correct use of the
       CMSG(3) macros using CMSG_NXTHDR does not work across different
       Red Hat versions (6.2 vs 7.2), but this code seem to work: */
    cmsg = CMSG_FIRSTHDR(&msg);
    for (i = 0; i < 2; i++) {
	if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_TTL) {
	    memcpy(&ttl, CMSG_DATA(cmsg), sizeof(int));
	    cmsg = (void *) cmsg + CMSG_SPACE(sizeof(int));
	} else if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_PKTINFO) {
	    memcpy(&pktinfo, CMSG_DATA(cmsg), sizeof(struct in_pktinfo));
	    cmsg = (void *) cmsg + CMSG_SPACE(sizeof(struct in_pktinfo));
	}
    }

    dst = ntohl(pktinfo.ipi_addr.s_addr);
#endif				/* RAW_SOCKET */

#ifdef USE_IW_SPY
    if (spy_addrs &&
	link_qual_get_from_ip(src, if_indextoname(pktinfo.ipi_ifindex,
						  ifname)) <
	hello_qual_threshold)
	return;
#endif				/* USE_IW_SPY */

    aodv_socket_process_packet(aodv_msg, len, src, dst, ttl,
			       pktinfo.ipi_ifindex);
}
#endif				/* NS_PORT */

void NS_CLASS aodv_socket_send(AODV_msg * aodv_msg, u_int32_t dst, int len,
			       u_int8_t ttl, struct dev_info *dev)
{
    int retval = 0;
    struct timeval now;
    /* Rate limit stuff: */
  
#ifndef NS_PORT
    struct sockaddr_in dst_addr;

#ifdef RAW_SOCKET
    struct iphdr *iph;
    struct udphdr *udph;

    if (wait_on_reboot && aodv_msg->type == AODV_RREP)
	return;

    /* Create a IP header around the packet... The AODV msg is already
       located in the send buffer and referenced by the in parameter
       "aodv_msg". */
    iph = (struct iphdr *) send_buf;
    iph->tot_len = htons(IPHDR_SIZE + sizeof(struct udphdr) + len);
    iph->saddr = htonl(dev->ipaddr);
    iph->daddr = htonl(dst);
    iph->ttl = ttl;

    udph = (struct udphdr *) (send_buf + IPHDR_SIZE);

    udph->len = htons(len + sizeof(struct udphdr));

#else

    /* If we are in waiting phase after reboot, don't send any control
       messages */
    if (wait_on_reboot)
	return;

    /* Set the ttl we want to send with */
    if (setsockopt(dev->sock, SOL_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
	log(LOG_WARNING, 0, __FUNCTION__, "Failed to set TTL!!!");
	return;
    }
#endif				/* RAW_SOCKET */

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
	    if (timeval_diff(&now, &time_last_rreq) < 1000 / RREQ_RATELIMIT) {
		DEBUG(LOG_DEBUG, 0, "Dropping RREQ due to RATELIMIT %ld ms",
		      timeval_diff(&now, &time_last_rreq));
		return;
	    }
	    memcpy(&time_last_rreq, &now, sizeof(struct timeval));
	    break;
	case AODV_RERR:
	    if (timeval_diff(&now, &time_last_rerr) < 1000 / RERR_RATELIMIT) {

		DEBUG(LOG_DEBUG, 0, "Dropping RERR due to RATELIMIT %ld ms",
		      timeval_diff(&now, &time_last_rerr));
		return;
	    }
	    memcpy(&time_last_rerr, &now, sizeof(struct timeval));
	    break;
	}
    }
    

/* Alternative RATE LIMIT ALGORITHM: This algorithm will start to
 * count time (forward 1 second) from the first packet sent. If more
 * than RATELIMIT allowed packets are sent during that period, the
 * last packet is dropped and the packet counter and time are
 * reset. The time and packet count are also reset if 1 second lasts
 * without the limit being reached... The problem is that if there are
 * many packets at the end of an "interval" and at the start of the
 * next, although the limit is not reached in either interval, there
 * can be more than RATELIMIT packets sent if the second half of the
 * first interval is joined with the first of the following
 * interval... */
    
   /*  if (ratelimit) { */

/* 	gettimeofday(&now, NULL); */

/* 	switch (aodv_msg->type) { */
/* 	case AODV_RREQ: */
/* 	    if (timeval_diff(&now, &time_last_rreq) > 1000) { */
/* 		num_rreq = 1; */
/* 		memcpy(&time_last_rreq, &now, sizeof(struct timeval)); */
/* 	    } else { */
/* 		num_rreq++; */
/* 		DEBUG(LOG_DEBUG, 0, "RATELIMIT RREQ: time=%ld ms num=%d", */
/* 		      timeval_diff(&now, &time_last_rreq), num_rreq); */
/* 		if (num_rreq > RREQ_RATELIMIT) { */
/* 		    num_rreq = 0; */
/* 		    DEBUG(LOG_DEBUG, 0, "Dropping RREQ due to RATELIMIT"); */
/* 		    return; */
/* 		} */
/* 	    } */
/* 	    break; */
/* 	case AODV_RERR: */
/* 	    if (timeval_diff(&now, &time_last_rerr) > 1000) { */
/* 		num_rerr = 1; */
/* 		memcpy(&time_last_rerr, &now, sizeof(struct timeval)); */
/* 	    } else { */
/* 		num_rerr++; */
/* 		DEBUG(LOG_DEBUG, 0, "RATELIMIT RERR: time=%ld ms num=%d", */
/* 		      timeval_diff(&now, &time_last_rerr), num_rerr); */
/* 		if (num_rerr > RERR_RATELIMIT) { */
/* 		    num_rerr = 0; */
/* 		    DEBUG(LOG_DEBUG, 0, "Dropping RERR due to RATELIMIT"); */
/* 		    return; */
/* 		} */
/* 	    } */
/* 	    break; */
/* 	} */
/*      } */

    /* If we broadcast this message we update the time of last broadcast
       to prevent unnecessary broadcasts of HELLO msg's */
    if (dst == AODV_BROADCAST) {

	gettimeofday(&this_host.bcast_time, NULL);

#ifdef NS_PORT
	ch->addr_type() = NS_AF_NONE;

	sendPacket(p, 0, 0.0);
#else
	retval = sendto(dev->sock, send_buf, len, 0,
			(struct sockaddr *) &dst_addr, sizeof(dst_addr));

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
	retval = sendto(dev->sock, send_buf, len, 0,
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
	DEBUG(LOG_INFO, 0, "AODV msg to %s ttl=%d (%d bytes)",
	      ip_to_str(dst), ttl, retval);

    return;
}

AODV_msg *NS_CLASS aodv_socket_new_msg(void)
{
#if defined(RAW_SOCKET) && !defined(NS_PORT)
    struct iphdr *iph;
    struct udphdr *udph;
    /* Initialize IP header, kernel fills in zero:ed values... */
    memset(send_buf, '\0', SEND_BUF_SIZE);
    iph = (struct iphdr *) send_buf;
    iph->version = IPVERSION;
    iph->ihl = (IPHDR_SIZE >> 2);
    iph->tos = 0;
    iph->id = 0;
    iph->frag_off = 0;
    iph->protocol = IPPROTO_UDP;
    iph->check = 0;

    udph = (struct udphdr *) (send_buf + IPHDR_SIZE);
    udph->source = htons(AODV_PORT);
    udph->dest = htons(AODV_PORT);
    udph->check = 0;
    return (AODV_msg *) (send_buf + IPHDR_SIZE + sizeof(struct udphdr));
#else
    memset(send_buf, '\0', SEND_BUF_SIZE);
    return (AODV_msg *) (send_buf);
#endif				/* RAW_SOCKET */
}

/* Copy an existing AODV message to the send buffer */
AODV_msg *NS_CLASS aodv_socket_queue_msg(AODV_msg * aodv_msg, int size)
{
#if defined(RAW_SOCKET) && !defined(NS_PORT)
    memcpy((char *) (send_buf + IPHDR_SIZE), aodv_msg, size);
    return (AODV_msg *) (send_buf + IPHDR_SIZE);
#else
    memcpy((char *) (send_buf), aodv_msg, size);
    return (AODV_msg *) (send_buf);
#endif				/* RAW_SOCKET */
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
