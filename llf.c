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
#include "defs.h"
#include "debug.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <asm/types.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <net/ethernet.h>	/* struct ether_addr */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <iwlib.h>

#include "aodv_neighbor.h"
#include "routing_table.h"
/* The netlink socket code is taken from the wireless tools by Jean Tourrilhes
 * <jt@hpl.hp.com>, who in turn took code from libnetlink.c RTnetlink service
 * routines, by Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>. All GPL code. */

struct rtnl_handle
{
    int	fd;
    struct sockaddr_nl local;
    struct sockaddr_nl peer;
    u_int32_t seq;
    u_int32_t dump;
};

static inline int llf_rtnl_open(struct rtnl_handle *rth, unsigned subscriptions);
static inline void llf_handle_netlink_events(struct rtnl_handle *rth);

static struct rtnl_handle rth;

static void llf_callback(int fd) {
    
    llf_handle_netlink_events(&rth);
}

void llf_init()
{
    
    if(llf_rtnl_open(&rth, RTMGRP_LINK) < 0) {
	DEBUG(LOG_ERR, 0, "Can't initialize rtnetlink socket");
	return;
    }
    if (attach_callback_func(rth.fd, llf_callback) < 0) {
	alog(LOG_ERR, 0, __FUNCTION__, "Could not attach callback");
	return;
    }
}


void llf_cleanup()
{
    close(rth.fd);
}



static inline int llf_rtnl_open(struct rtnl_handle *rth, 
				 unsigned subscriptions)
{
    int addr_len;

    memset(rth, 0, sizeof(rth));

    rth->fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (rth->fd < 0) {
	perror("Cannot open netlink socket");
	return -1;
    }

    memset(&rth->local, 0, sizeof(rth->local));
    rth->local.nl_family = AF_NETLINK;
    rth->local.nl_groups = subscriptions;

    if (bind(rth->fd, (struct sockaddr*)&rth->local, sizeof(rth->local)) < 0) {
	perror("Cannot bind netlink socket");
	return -1;
    }
    addr_len = sizeof(rth->local);
    if (getsockname(rth->fd, (struct sockaddr*)&rth->local, &addr_len) < 0) {
	perror("Cannot getsockname");
	return -1;
    }
    if (addr_len != sizeof(rth->local)) {
	fprintf(stderr, "Wrong address length %d\n", addr_len);
	return -1;
    }
    if (rth->local.nl_family != AF_NETLINK) {
	fprintf(stderr, "Wrong address family %d\n", rth->local.nl_family);
	return -1;
    }
    rth->seq = time(NULL);
    return 0;
}
/* Ugly, ugly way to get ip from eth address */
int mac_to_ip(struct sockaddr *hwaddr, struct in_addr *ip_addr, char *ifname)
{
    FILE *fp;
    char buffer[128];
    char ip[100];
    char hwa[100];
    char mask[100];
    char line[200];
    char dev[100];
    int type, flags, num;
    
    if ((fp = fopen("/proc/net/arp", "r")) == NULL) {
	perror("/proc/net/arp");
	return (-1);
    }
    /* Bypass header -- read until newline */
    if (fgets(line, sizeof(line), fp) != (char *) NULL) {
	strcpy(mask, "-");
	strcpy(dev, "-");
	/* Read the ARP cache entries. */
	for (; fgets(line, sizeof(line), fp);) {
	    num = sscanf(line, "%s 0x%x 0x%x %100s %100s %100s\n",
			 ip, &type, &flags, hwa, mask, dev);
	    if (num < 4)
		break;
	    
	    //printf("hwa=%s ip=%s\n", hwa, ip);
	    iw_pr_ether(buffer, hwaddr->sa_data);
	    
	    if (memcmp(hwa, buffer, ETH_ALEN) == 0) {
		
		inet_aton(ip, ip_addr);
		
		fclose(fp);
		return 0;
	    }
	}
    }
    fclose(fp);
    return -1;
}


static inline int index2name(int index, char *name)
{
  int		skfd = -1;	/* generic raw socket desc.	*/
  struct ifreq	irq;
  int		ret = 0;

  memset(name, 0, IFNAMSIZ + 1);

  /* Create a channel to the NET kernel., */
  if((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
      perror("socket");
      exit(-1);
    }

  /* Get interface name */
  irq.ifr_ifindex = index;
  if(ioctl(skfd, SIOCGIFNAME, &irq) < 0)
    ret = -1;
  else
    strncpy(name, irq.ifr_name, IFNAMSIZ);

  close(skfd);
  return(ret);
}
static inline int llf_print_event(struct iw_event *event, 
				   struct iw_range *iwrange, int has_iwrange)
{
    char buffer[128];
    struct in_addr ip;
    rt_table_t *rt;
    
    /* Now, let's decode the event */
    switch(event->cmd) {
	
    case IWEVTXDROP:
	DEBUG(LOG_DEBUG, 0, "Tx packet dropped:%s",
	      iw_pr_ether(buffer, event->u.addr.sa_data));
	
	
	if (mac_to_ip(&event->u.addr, &ip, this_host.devs[0].ifname) < 0) {
	    DEBUG(LOG_DEBUG, 0, "failed mac_to_ip");
	    return 0;
	}
	//printf("IP=%s\n", ip_to_str(ip));
	
	rt = rt_table_find(ip);
	
	if (rt) 
	    neighbor_link_break(rt);
	else
	    DEBUG(LOG_DEBUG, 0, "no route for ip=%s", ip_to_str(ip));
	break;
	
    default: 
	DEBUG(LOG_DEBUG, 0, "(Unknown Wireless event 0x%04X)", event->cmd);
    }
    
    return 0;
}

static inline void llf_handle_netlink_events(struct rtnl_handle *rth)
{
    while(1)
    {
	struct sockaddr_nl sanl;
	socklen_t sanllen;
	struct nlmsghdr *h;
	struct ifinfomsg* ifi;
	char ifname[IFNAMSIZ + 1];
	int amt;
	char buf[8192];

	amt = recvfrom(rth->fd, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr*)&sanl, &sanllen);
	if(amt < 0)
	{
	    if(errno != EINTR && errno != EAGAIN)
	    {
		fprintf(stderr, "%s: error reading netlink: %s.\n",
			__PRETTY_FUNCTION__, strerror(errno));
	    }
	    return;
	}

	if(amt == 0)
	{
	    fprintf(stderr, "%s: EOF on netlink??\n", __PRETTY_FUNCTION__);
	    return;
	}

	h = (struct nlmsghdr*)buf;
	while(amt >= (int)sizeof(*h))
	{
	    int len = h->nlmsg_len;
	    int l = len - sizeof(*h);

	    if(l < 0 || len > amt)
	    {
		fprintf(stderr, "%s: malformed netlink message: len=%d\n", __PRETTY_FUNCTION__, len);
		break;
	    }

	    switch(h->nlmsg_type)
	    {
	    case RTM_NEWLINK:
// 		LinkCatcher(h);
		if(h->nlmsg_type != RTM_NEWLINK)
		    return;
		
		ifi = NLMSG_DATA(h);
		
		/* Get a name... */
		index2name(ifi->ifi_index, ifname);
		
		/* Check for attributes */
		if (h->nlmsg_len > NLMSG_ALIGN(sizeof(struct ifinfomsg))) {
		    int attrlen = h->nlmsg_len - NLMSG_ALIGN(sizeof(struct ifinfomsg));
		    struct rtattr *attr = (void*)ifi + NLMSG_ALIGN(sizeof(struct ifinfomsg));
		    
		    while (RTA_OK(attr, attrlen)) {
			/* Check if the Wireless kind */
			if(attr->rta_type == IFLA_WIRELESS) {
			    struct iw_event iwe;
			    struct stream_descr	stream;
			    int ret;
			    /* Go to display it */
			    
			    iw_init_event_stream(&stream, (void *)attr + RTA_ALIGN(sizeof(struct rtattr)), attr->rta_len - RTA_ALIGN(sizeof(struct rtattr)));
			    do {
				/* Extract an event and print it */
				ret = iw_extract_event_stream(&stream, &iwe);
				if(ret != 0) {
				    if(ret > 0)
					llf_print_event(&iwe, NULL, 0);
				    else
					DEBUG(LOG_WARNING, 0, "Invalid iw event");
				}
			    } while(ret > 0);
			    
			}
			attr = RTA_NEXT(attr, attrlen);
		    }
		}
		
		break;
	    default:
#if 0
		fprintf(stderr, "%s: got nlmsg of type %#x.\n", __PRETTY_FUNCTION__, h->nlmsg_type);
#endif
		break;
	    }

	    len = NLMSG_ALIGN(len);
	    amt -= len;
	    h = (struct nlmsghdr*)((char*)h + len);
	}

	if(amt > 0)
	    fprintf(stderr, "%s: remnant of size %d on netlink\n", __PRETTY_FUNCTION__, amt);
    }
}
