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
 * Author: Erik Nordström, <erik.nordstrom@it.uu.se>
 * 
 *****************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "defs.h"
#include "lnx/kaodv-netlink.h"
#include "debug.h"
#include "aodv_rreq.h"
#include "aodv_timeout.h"
#include "routing_table.h"
#include "aodv_hello.h"
#include "params.h"
#include "aodv_socket.h"
#include "aodv_rerr.h"

/* Implements a Netlink socket communication channel to the kernel. Route
 * information and refresh messages are passed. */

static int nlfd;
static struct sockaddr_nl local;
static struct sockaddr_nl peer;

static void nl_callback(int fd);

extern int llfeedback, active_route_timeout, qual_threshold, internet_gw_mode,
    wait_on_reboot;
extern struct timer worb_timer;

#define BUFLEN 256

void nl_init(void)
{
    int status;

    nlfd = socket(PF_NETLINK, SOCK_RAW, NETLINK_AODV);

    if (nlfd < 0) {
	perror("Unable to create netlink socket");
	exit(-1);
    }

    memset(&local, 0, sizeof(struct sockaddr_nl));
    local.nl_family = AF_NETLINK;
    local.nl_pid = getpid();
    local.nl_groups = AODVGRP_NOTIFY;

    memset(&peer, 0, sizeof(struct sockaddr_nl));
    peer.nl_family = AF_NETLINK;
    peer.nl_pid = 0;
    peer.nl_groups = 0;

    status = bind(nlfd, (struct sockaddr *) &local, sizeof(local));

    if (status == -1) {
	perror("Bind failed");
	exit(-1);
    }

    if (attach_callback_func(nlfd, nl_callback) < 0) {
	alog(LOG_ERR, 0, __FUNCTION__, "Could not attach callback.");
    }
}

void nl_cleanup(void)
{
    close(nlfd);
}

static void nl_callback(int fd)
{
    int n;
    int addrlen;
    char buf[BUFLEN];
    struct in_addr dest_addr, src_addr;

    addrlen = sizeof(peer);

    n = recvfrom(nlfd, buf, BUFLEN, 0, (struct sockaddr *) &peer, &addrlen);

    if (n) {
	kaodv_rt_msg_t *m;
	kaodv_rt_update_msg_t *mu;

	rt_table_t *rt, *fwd_rt, *rev_rt = NULL;
	int type;

	type = ((struct nlmsghdr *) buf)->nlmsg_type;

	switch (type) {

	case KAODVM_TIMEOUT:
	    m = NLMSG_DATA((struct nlmsghdr *) (buf));
	    dest_addr.s_addr = m->dest;

	    DEBUG(LOG_DEBUG, 0, "Got TIMEOUT msg from kernel for %s",
		  ip_to_str(dest_addr));

	    rt = rt_table_find(dest_addr);

	    if (rt && rt->state == VALID)
		route_expire_timeout(rt);
	    else
		DEBUG(LOG_DEBUG, 0,
		      "Got rt timeoute event but there is no route");
	    break;
	case KAODVM_NOROUTE:
	    m = NLMSG_DATA((struct nlmsghdr *) (buf));
	    dest_addr.s_addr = m->dest;

	    DEBUG(LOG_DEBUG, 0, "Got NOROUTE msg from kernel for %s",
		  ip_to_str(dest_addr));

	    rreq_route_discovery(dest_addr, 0, NULL);
	    break;
	case KAODVM_REPAIR:
	    m = NLMSG_DATA((struct nlmsghdr *) (buf));
	    dest_addr.s_addr = m->dest;
	    src_addr.s_addr = m->src;

	    DEBUG(LOG_DEBUG, 0, "Got REPAIR msg from kernel for %s",
		  ip_to_str(dest_addr));

	    fwd_rt = rt_table_find(dest_addr);

	    if (fwd_rt)
		rreq_local_repair(fwd_rt, src_addr, NULL);

	    break;
	case KAODVM_ROUTE_UPDATE:
	    mu = NLMSG_DATA((struct nlmsghdr *) (buf));
	    dest_addr.s_addr = mu->dest;
	    src_addr.s_addr = mu->src;
	    /*  DEBUG(LOG_DEBUG, 0, "Got ROUTE_UPDATE msg from kernel for %s",  */
/* 		  ip_to_str(dest_addr)); */

	    if (dest_addr.s_addr == AODV_BROADCAST ||
		dest_addr.s_addr == DEV_IFINDEX(mu->ifindex).broadcast.s_addr)
		return;

	    fwd_rt = rt_table_find(dest_addr);
	    rev_rt = rt_table_find(src_addr);

	    rt_table_update_route_timeouts(fwd_rt, rev_rt);

	    break;
	case KAODVM_SEND_RERR:
	    mu = NLMSG_DATA((struct nlmsghdr *) (buf));
	    dest_addr.s_addr = mu->dest;
	    src_addr.s_addr = mu->src;

	    if (dest_addr.s_addr == AODV_BROADCAST ||
		dest_addr.s_addr == DEV_IFINDEX(mu->ifindex).broadcast.s_addr)
		return;

	    fwd_rt = rt_table_find(dest_addr);
	    rev_rt = rt_table_find(src_addr);

	    do {
		struct in_addr rerr_dest;
		RERR *rerr;

		DEBUG(LOG_DEBUG, 0,
		      "Sending RERR for unsolicited message from %s to dest %s",
		      ip_to_str(src_addr), ip_to_str(dest_addr));

		if (fwd_rt) {
		    rerr = rerr_create(0, fwd_rt->dest_addr,
				       fwd_rt->dest_seqno);

		    rt_table_update_timeout(fwd_rt, DELETE_PERIOD);
		} else
		    rerr = rerr_create(0, dest_addr, 0);

		/* Unicast the RERR to the source of the data transmission
		 * if possible, otherwise we broadcast it. */

		if (rev_rt && rev_rt->state == VALID)
		    rerr_dest = rev_rt->next_hop;
		else
		    rerr_dest.s_addr = AODV_BROADCAST;

		aodv_socket_send((AODV_msg *) rerr, rerr_dest,
				 RERR_CALC_SIZE(rerr), 1,
				 &DEV_IFINDEX(mu->ifindex));

		if (wait_on_reboot) {
		    DEBUG(LOG_DEBUG, 0, "Wait on reboot timer reset.");
		    timer_set_timeout(&worb_timer, DELETE_PERIOD);
		}
	    } while (0);
	    break;
	default:
	    DEBUG(LOG_DEBUG, 0, "Got mesg type=%d\n", type);
	}
    } else {
	printf("no bytes read\n");
    }
}

static int nl_create_and_send_msg(int type, struct in_addr dest,
				  struct in_addr nhop, u_int32_t lifetime,
				  int flags)
{
    int status;
    struct {
	struct nlmsghdr nlh;
	kaodv_rt_msg_t rtm;
    } req;

    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(req));
    req.nlh.nlmsg_flags = NLM_F_REQUEST;
    req.nlh.nlmsg_type = type;
    req.nlh.nlmsg_pid = local.nl_pid;
    req.rtm.dest = dest.s_addr;
    req.rtm.nhop = nhop.s_addr;
/*     req.rtm.gw = gw_addr.s_addr; */
    req.rtm.flags = flags;
    req.rtm.time = lifetime;
    status = sendto(nlfd, &req, req.nlh.nlmsg_len, 0,
		    (struct sockaddr *) &peer, sizeof(peer));

    return status;
}

void nl_send_add_route_msg(struct in_addr dest, struct in_addr next_hop,
			   u_int32_t lifetime, int rt_flags)
{
    int flags = 0;


    DEBUG(LOG_DEBUG, 0, "Send ADD/UPDATE ROUTE to kernel: %s:%s",
	  ip_to_str(dest), ip_to_str(next_hop));

    if (rt_flags & RT_INET_DEST)
	flags |= KAODV_RT_GW_ENCAP;

    if (rt_flags & RT_REPAIR)
	flags |= KAODV_RT_REPAIR;

    nl_create_and_send_msg(KAODVM_ADDROUTE, dest, next_hop, lifetime, flags);
}

void nl_send_no_route_found_msg(struct in_addr dest)
{
    struct in_addr tmp;
    tmp.s_addr = 0;

    DEBUG(LOG_DEBUG, 0, "Send NOROUTE_FOUND to kernel: %s", ip_to_str(dest));
    nl_create_and_send_msg(KAODVM_NOROUTE_FOUND, dest, tmp, 0, 0);
}

void nl_send_del_route_msg(struct in_addr dest)
{
    struct in_addr tmp;
    tmp.s_addr = 0;

    DEBUG(LOG_DEBUG, 0, "Send DELROUTE to kernel: %s", ip_to_str(dest));
    nl_create_and_send_msg(KAODVM_DELROUTE, dest, tmp, 0, 0);
}

int nl_send_conf_msg(void)
{
    int status;
    struct {
	struct nlmsghdr nlh;
	kaodv_conf_msg_t cm;
    } req;

    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(req));
    req.nlh.nlmsg_flags = NLM_F_REQUEST;
    req.nlh.nlmsg_type = KAODVM_CONFIG;
    req.nlh.nlmsg_pid = local.nl_pid;
    req.cm.qual_th = qual_threshold;
    req.cm.active_route_timeout = active_route_timeout;
    req.cm.is_gateway = internet_gw_mode;

    status = sendto(nlfd, &req, req.nlh.nlmsg_len, 0,
		    (struct sockaddr *) &peer, sizeof(peer));

    return status;
}
