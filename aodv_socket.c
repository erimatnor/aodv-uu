/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University and Ericsson Telecom AB.
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

/* Uncomment if SOCK_RAW is to be used instead of SOCK_DGRAM: */
/*  #define RAW_SOCKET   */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#ifdef RAW_SOCKET
#include <netinet/udp.h>
#endif
#include "aodv_socket.h"
#include "timer_queue.h"
#include "aodv_rreq.h"
#include "aodv_rerr.h"
#include "aodv_rrep.h"
#include "aodv_hello.h"
#include "debug.h"
#include "defs.h"

/* Set a maximun size for AODV msg's. The RERR is the potentially
   largest message, depending on how many unreachable destinations
   that are included. Lets limit them to 100 */
#define AODV_MSG_MAX_SIZE RERR_SIZE + 100 * RERR_UDEST_SIZE
#define RECV_BUF_SIZE AODV_MSG_MAX_SIZE
#define SEND_BUF_SIZE RECV_BUF_SIZE
#define SO_RECVBUF_SIZE 256*1024
/* static int aodv_socket;	 */	/* The socket we send aodv messages on */
static char recv_buf[RECV_BUF_SIZE];
static char send_buf[SEND_BUF_SIZE];
extern int wait_on_reboot;

static void aodv_socket_read(int fd);

void aodv_socket_init()
{
    struct sockaddr_in aodv_addr;
    struct ifreq ifr;
    int i, retval = 0;
    int on = 1;
    int tos = IPTOS_LOWDELAY;
    int bufsize = SO_RECVBUF_SIZE;
    
    /* Create a UDP socket */

    if (this_host.nif == 0) {
	fprintf(stderr, "aodv_socket_init: No interfaces configured\n");
	exit(-1);
    }
    for (i = 0; i < MAX_NR_INTERFACES; i++) {
	if (!DEV_NR(i).enabled)
	    continue;
#ifdef RAW_SOCKET
	DEV_NR(i).sock = socket(PF_INET, SOCK_RAW, IPPROTO_UDP);
#else
	DEV_NR(i).sock = socket(PF_INET, SOCK_DGRAM, 0);
#endif
    
	if (DEV_NR(i).sock < 0) {
	    perror("aodv_socket_init: ");
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
	    perror("aodv_socket_init: Bind failed ");
	    exit(-1);
	}
	if (setsockopt(DEV_NR(i).sock, SOL_SOCKET, SO_BROADCAST, 
		       &on, sizeof(int)) < 0) {
	    perror("aodv_socket_init: SO_BROADCAST failed ");
	    exit(-1);
	}
	
	memset(&ifr, 0, sizeof(struct ifreq));
	strcpy(ifr.ifr_name, DEV_NR(i).ifname);
	
	if (setsockopt(DEV_NR(i).sock, SOL_SOCKET, SO_BINDTODEVICE, 
		       &ifr, sizeof(ifr)) < 0) {
	    fprintf(stderr, "aodv_socket_init: SO_BINDTODEVICE failed for %s",
		   DEV_NR(i).ifname);
	    perror(" ");
	    exit(-1);
	}
	if (setsockopt(DEV_NR(i).sock, SOL_IP, IP_TOS, 
		       &tos, sizeof(int)) < 0) {
	    perror("aodv_socket_init: Setsockopt SO_PRIORITY failed ");
	    exit(-1);
	}
#ifdef RAW_SOCKET
	/* We provide our own IP header... */

	if (setsockopt(DEV_NR(i).sock, SOL_IP, IP_HDRINCL, 
		       &on, sizeof(int)) < 0) {
	    perror("aodv_socket_init: Setsockopt IP_HDRINCL failed ");
	    exit(-1);
	}
#else

	if (setsockopt(DEV_NR(i).sock, SOL_IP, IP_PKTINFO,
		       &on, sizeof(int)) < 0) {
	    perror("aodv_socket_init: Setsockopt IP_PKTINFO failed ");
	    exit(-1);
	}
	if (setsockopt(DEV_NR(i).sock, SOL_IP, IP_RECVTTL, 
		       &on, sizeof(int)) < 0) {
	    perror("aodv_socket_init: Setsockopt IP_TTL failed ");
	    exit(-1);
	}
#endif
	/* Set max allowable receive buffer size... */
	for (;; bufsize -= 1024) {
	    if (setsockopt(DEV_NR(i).sock, SOL_SOCKET, SO_RCVBUF,
			   (char *) &bufsize, sizeof(bufsize)) == 0) {
		log(LOG_NOTICE, 0,
		    "aodv_socket_init: Receive buffer size set to %d",
		    bufsize);
		break;
	    }
	    if (bufsize < RECV_BUF_SIZE) {
		log(LOG_ERR, 0,
		    "aodv_socket_init: Could not set receive buffer size");
		exit(-1);
	    }
	}

	retval = attach_callback_func(DEV_NR(i).sock, aodv_socket_read);

	if (retval < 0) {
	    perror("aodv_socket_init: register input handler failed ");
	    exit(-1);
	}
    } 
}

static void aodv_socket_read(int fd)
{
    u_int32_t src, dst;
    int len, aodv_len;
    int ttl = 0;
    AODV_msg *aodv_msg;
#ifdef RAW_SOCKET
    int iph_len;
    struct iphdr *iph;
    struct udphdr *udph;

    len = recvfrom(fd, recv_buf, RECV_BUF_SIZE, 0, NULL, NULL);

    if (len < 0 || len < IPHDR_SIZE) {
	log(LOG_WARNING, 0, "aodv_socket_read: receive ERROR!");
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
    aodv_len = ntohs(udph->len) - sizeof(struct udphdr);

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
    int i = 0;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = NULL;
    msg.msg_iovlen = 0;
    msg.msg_control = control_union.control;
    msg.msg_controllen = sizeof(control_union.control);

    /* Get the information control message first */
    if ((len = recvmsg(fd, &msg, MSG_PEEK)) < 0) {
	perror("err: ");
	log(LOG_WARNING, 0, "aodv_socket_read: recvmsg ERROR!");
	return;
    }
    /* Read the data payload (i.e. AODV msg) */
    len = recvfrom(fd, recv_buf, RECV_BUF_SIZE, 0,
		   (struct sockaddr *) &src_addr, &sockaddr_len);

    if (len < 0) {
	log(LOG_WARNING, 0, "aodv_socket_read: receive ERROR!");
	return;
    }
    aodv_msg = (AODV_msg *) (recv_buf);
    aodv_len = len;
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
	} else if (cmsg->cmsg_level == SOL_IP
		   && cmsg->cmsg_type == IP_PKTINFO) {
	    memcpy(&pktinfo, CMSG_DATA(cmsg), sizeof(struct in_pktinfo));
	    cmsg = (void *) cmsg + CMSG_SPACE(sizeof(struct in_pktinfo));
	}
    }

    dst = ntohl(pktinfo.ipi_addr.s_addr);
#endif

#ifdef DEBUG
    /*   log(LOG_DEBUG, 0, "aodv_socket_read: src=%s dst=%s ttl=%d",  */
/*        ip_to_str(src), ip_to_str(dst), ttl); */
#endif

    /* If this was not a HELLO, treat as hello anyway... */
    if (!(aodv_msg->type == AODV_RREP && (ttl == 1)))
	hello_process_non_hello(aodv_msg, src, pktinfo.ipi_ifindex);

    /* Check what type of msg we received and call the corresponding
       function to handle the msg... */
    switch (aodv_msg->type) {
    case AODV_RREQ:
	rreq_process((RREQ *) aodv_msg, aodv_len, src, dst, ttl, 
		     pktinfo.ipi_ifindex);
	break;
    case AODV_RREP:
	rrep_process((RREP *) aodv_msg, aodv_len, src, dst, ttl, 
		     pktinfo.ipi_ifindex);
	break;
    case AODV_RERR:
	rerr_process((RERR *) aodv_msg, aodv_len, src, dst);
	break;
    case AODV_RREP_ACK:
	rrep_ack_process((RREP_ack *) aodv_msg, aodv_len, src, dst);
	break;
    default:
	log(LOG_WARNING, 0,
	    "aodv_socket_read: Unknown msg type %u rcvd from %s to %s",
	    aodv_msg->type, ip_to_str(src), ip_to_str(dst));
    }
}

void
aodv_socket_send(AODV_msg * aodv_msg, u_int32_t dst, int len, u_int8_t ttl, struct dev_info *dev)
{
    struct sockaddr_in dst_addr;
    int retval = 0;
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

    /* If we are in waiting phase after reboot, don't send any RREPs */
    if (wait_on_reboot && aodv_msg->type == AODV_RREP)
	return;

    /* Set the ttl we want to send with */
    if (setsockopt(dev->sock, SOL_IP, IP_TTL, 
		   &ttl, sizeof(ttl)) < 0) {
	log(LOG_WARNING, 0, "aodv_socket_send: Failed to set TTL!!!");
	return;
    }
#endif
    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_addr.s_addr = htonl(dst);
    dst_addr.sin_port = htons(AODV_PORT);
    
    /* If we broadcast this message we update the time of last broadcast
       to prevent unnecessary broadcasts of HELLO msg's */
    if (dst == AODV_BROADCAST) {

	gettimeofday(&this_host.bcast_time, NULL);
	
	retval = sendto(dev->sock, send_buf, len, 0,
			(struct sockaddr *) &dst_addr, sizeof(dst_addr));
	
	if (retval < 0) {
	    log(LOG_WARNING, errno, "aodv_socket_send: Failed send to %s",
		ip_to_str(dst));
	    return;
	    }
   
    } else {
	
	retval = sendto(dev->sock, send_buf, len, 0,
			(struct sockaddr *) &dst_addr, sizeof(dst_addr));
	
	if (retval < 0) {
	    log(LOG_WARNING, errno, "aodv_socket_send: Failed send to %s",
		ip_to_str(dst));
	    return;
	}
    }
#ifdef DEBUG
    /* Do not print hello msgs... */
    if (!(aodv_msg->type == AODV_RREP && (dst == AODV_BROADCAST)))
	log(LOG_INFO, 0,
	    "aodv_socket_send: Sent AODV msg to %s (%d bytes)",
	    ip_to_str(dst), retval);
#endif
}

AODV_msg *aodv_socket_new_msg()
{

#ifdef RAW_SOCKET
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
#endif

}

/* Copy an existing AODV message to the send buffer */
AODV_msg *aodv_socket_queue_msg(AODV_msg * aodv_msg, int size)
{
#ifdef RAW_SOCKET
    memcpy((char *) (send_buf + IPHDR_SIZE), aodv_msg, size);
    return (AODV_msg *) (send_buf + IPHDR_SIZE);
#else
    memcpy((char *) (send_buf), aodv_msg, size);
    return (AODV_msg *) (send_buf);
#endif
}

void aodv_socket_cleanup(void)
{
    int i;

    for (i = 0; i < MAX_NR_INTERFACES; i++) {
	if (!DEV_NR(i).enabled)
	    continue;
	close(DEV_NR(i).sock);
    }
}
