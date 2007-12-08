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
#ifndef _KAODV_NETLINK_H
#define _KAODV_NETLINK_H

/* A communication link between the kernel and the AODV daemon */

#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

/* Setting to MAX_LINKS-1 should ensure we use a free NETLINK
 * socket type. */
#define NETLINK_AODV  (MAX_LINKS-1)

enum {
  AODVGRP_NOTIFY = 1,
#define AODVGRP_NOTIFY   AODVGRP_NOTIFY
  __AODVGRP_MAX
};

#define AODVGRP_MAX (__AODVGRP_MAX - 1)      

/* Message types that can be passed between the kernel and user
 * space. I do not really know a good way to set KAODVM_BASE. Just
 * set to 100 and hope there are no conflicts. */
enum {
	KAODVM_BASE = 100,
#define KAODVM_BASE KAODVM_BASE
	KAODVM_ADDROUTE,
#define KAODVM_ADDROUTE KAODVM_ADDROUTE
	KAODVM_DELROUTE,
#define KAODVM_DELROUTE KAODVM_DELROUTE
	KAODVM_TIMEOUT,
#define KAODVM_TIMEOUT KAODVM_TIMEOUT
	KAODVM_ROUTE_REQ,
#define KAODVM_ROUTE_REQ KAODVM_ROUTE_REQ
	KAODVM_REPAIR,
#define KAODVM_REPAIR KAODVM_REPAIR
	KAODVM_NOROUTE_FOUND,
#define KAODVM_NOROUTE_FOUND KAODVM_NOROUTE_FOUND
	KAODVM_ROUTE_UPDATE,
#define KAODVM_ROUTE_UPDATE KAODVM_ROUTE_UPDATE
	KAODVM_SEND_RERR,
#define KAODVM_SEND_RERR KAODVM_SEND_RERR
	KAODVM_CONFIG,
#define KAODVM_CONFIG KAODVM_CONFIG
	KAODVM_DEBUG,
#define KAODVM_DEBUG KAODVM_DEBUG
	__KAODV_MAX,
#define KAODVM_MAX __KAODV_MAX
};

static struct {
	int type;
	char *name;	       
} typenames[KAODVM_MAX] = { 
	{ KAODVM_ADDROUTE, "Add route" }, 
	{ KAODVM_DELROUTE, "Delete route" },
	{ KAODVM_TIMEOUT,  "Timeout" },
	{ KAODVM_ROUTE_REQ, "Route Request" },
	{ KAODVM_REPAIR, "Route repair" },
	{ KAODVM_NOROUTE_FOUND, "No route found" },
	{ KAODVM_ROUTE_UPDATE, "Route update" },
	{ KAODVM_SEND_RERR, "Send route error" },
	{ KAODVM_CONFIG, "Configuration" },
	{ KAODVM_DEBUG, "Debug"},
};

static inline char *kaodv_msg_type_to_str(int type)
{
	int i;

	for (i = 0; i < KAODVM_MAX; i++) {
		if (type == typenames[i].type) {
			return typenames[i].name;
		}
	}
	return "Unknown message type";
}

typedef struct kaodv_rt_msg { 
	u_int8_t type;
	u_int32_t src;
	u_int32_t dst;
	u_int32_t nhop;
	u_int8_t flags;
	int ifindex;
	long time;
} kaodv_rt_msg_t;

/* Route information flag */
#define KAODV_RT_GW_ENCAP            0x1
#define KAODV_RT_REPAIR              0x2

/* Two types of route update messages. Packet is coming in our is going out. */
#define PKT_INBOUND  1
#define PKT_OUTBOUND 2

/* Send configuration paramaters to the kernel. Could be expanded in the
 * future. */
typedef struct kaodv_conf_msg {
	int active_route_timeout;
	int qual_th;
	int is_gateway;
} kaodv_conf_msg_t;

/* Stuff below is not exported to user space */
#ifdef __KERNEL__

int kaodv_netlink_init(void);
void kaodv_netlink_fini(void);

void kaodv_netlink_send_rt_msg(int type, __u32 src, __u32 dest);
void kaodv_netlink_send_rt_update_msg(int type, __u32 src,
				      __u32 dest, int ifindex);
void kaodv_netlink_send_rerr_msg(int type, __u32 src, __u32 dest, int ifindex);
void kaodv_netlink_send_debug_msg(char *buf, int len);

#endif				/* __KERNEL__ */

#endif
