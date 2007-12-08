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
 *
 *****************************************************************************/
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <net/if_arp.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <linux/unistd.h>
#include <linux/sysctl.h>

#include "defs.h"
#include "debug.h"
#include "k_route.h"
/*
 * Manipulating the kernel routing table through ioctl() operations...
 */

int k_add_rte(struct in_addr dst, struct in_addr gw, struct in_addr nm,
	      short int hcnt, unsigned int ifindex)
{

    int sock, ret;
    struct rtentry rte;
    struct sockaddr_in dest, mask, gtwy;

    if (dst.s_addr == gw.s_addr)
	gw.s_addr = 0;

    memset(&rte, 0, sizeof(struct rtentry));

    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = dst.s_addr;

    gtwy.sin_family = AF_INET;
    gtwy.sin_addr.s_addr = gw.s_addr;

    mask.sin_family = AF_INET;
    mask.sin_addr.s_addr = nm.s_addr;


    memcpy(&rte.rt_dst, &dest, sizeof(struct sockaddr_in));
    memcpy(&rte.rt_gateway, &gtwy, sizeof(struct sockaddr_in));
    memcpy(&rte.rt_genmask, &mask, sizeof(struct sockaddr_in));

    rte.rt_dev = DEV_IFINDEX(ifindex).ifname;

    rte.rt_metric = hcnt + 1;

    if (gw.s_addr == 0)
	rte.rt_flags = RTF_HOST | RTF_UP | RTF_DYNAMIC;
    else
	rte.rt_flags = RTF_HOST | RTF_GATEWAY | RTF_UP | RTF_DYNAMIC;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
	fprintf(stderr, "can't make socket\n");

	return -1;
    }

    /* Send message by ioctl(). */
    ret = ioctl(sock, SIOCADDRT, &rte);
    if (ret < 0) {
	switch (errno) {
	case EEXIST:
	case ENETUNREACH:
	case EPERM:
	default:
	    close(sock);
	    return ret;
	}
	close(sock);
	return 1;
    }
    close(sock);

    return 0;
}


int k_del_rte(struct in_addr dst)
{
    int sock, ret;
    struct rtentry rte;
    struct sockaddr_in dest, mask, gtwy;

    /*  printf("Removing a kernel route (%s : %s : %s)!\n",  */
/*  	 ip_to_str(dst), ip_to_str(gw), ip_to_str(nm)); */
    memset(&rte, 0, sizeof(struct rtentry));

    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = dst.s_addr;

    gtwy.sin_family = AF_INET;
    gtwy.sin_addr.s_addr = 0;

    mask.sin_family = AF_INET;
    mask.sin_addr.s_addr = 0;

    memcpy(&rte.rt_dst, &dest, sizeof(struct sockaddr_in));
    memcpy(&rte.rt_gateway, &gtwy, sizeof(struct sockaddr_in));
    memcpy(&rte.rt_genmask, &mask, sizeof(struct sockaddr_in));

    rte.rt_flags = RTF_HOST;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
	fprintf(stderr, "can't make socket\n");

	return -1;
    }

    /* Send message by ioctl(). */
    ret = ioctl(sock, SIOCDELRT, &rte);
    if (ret < 0) {
	switch (errno) {
	case EEXIST:
	case ENETUNREACH:
	case EPERM:
	default:
	    close(sock);
	    return ret;
	}
	close(sock);
	return -1;
    } else {
	close(sock);
    }
    return 0;

}

/* This is an ugly way of updating a route... */
int k_chg_rte(struct in_addr dst, struct in_addr gw, struct in_addr nm,
	      short int hcnt, unsigned int ifindex)
{
    int ret;

    if ((ret = k_del_rte(dst)) < 0)
	return ret;

    if ((ret = k_add_rte(dst, gw, nm, hcnt, ifindex)) < 0)
	return ret;

    return 0;
}


int k_del_arp(struct in_addr dst)
{
    int sock, ret;
    struct arpreq req;
    struct sockaddr_in *dest;

    memset(&req, 0, sizeof(struct arpreq));

    dest = (struct sockaddr_in *) &req.arp_pa;
    dest->sin_family = AF_INET;

    memcpy(&dest->sin_addr, &dst, sizeof(struct in_addr));

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
	fprintf(stderr, "can't make socket\n");

	return -1;
    }

    /* Send message by ioctl(). */
    ret = ioctl(sock, SIOCDARP, &req);
    if (ret < 0) {
	switch (errno) {
	case EEXIST:
	case ENETUNREACH:
	case EPERM:
	default:
	    close(sock);
	    perror("Sock err:");
	    return ret;
	}
	close(sock);
	return -1;
    } else {
	close(sock);
    }
    return 0;

}
