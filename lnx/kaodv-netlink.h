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

/* A communications link for passing info between the kernel and AODV daemon */

#include <linux/types.h>

#define NETLINK_AODV     15   /* Type 15 was free last time I checked  */

#define AODVGRP_NOTIFY   1

/* Message types that can be passed between the kernel and user space */
#define KAODVM_BASE	       0x14
#define KAODVM_ADDROUTE        (KAODVM_BASE +1)
#define KAODVM_DELROUTE        (KAODVM_BASE +2)
#define KAODVM_TIMEOUT         (KAODVM_BASE +3)
#define KAODVM_NOROUTE         (KAODVM_BASE +4)
#define KAODVM_REPAIR          (KAODVM_BASE +5)
#define KAODVM_NOROUTE_FOUND   (KAODVM_BASE +6)
#define KAODVM_ROUTE_UPDATE    (KAODVM_BASE +7)
#define KAODVM_SEND_RERR       (KAODVM_BASE +8)
#define KAODVM_CONFIG          (KAODVM_BASE +9)

typedef struct kaodv_rt_msg {
    u_int32_t src;
    u_int32_t dest;
    u_int32_t nhop;
    u_int8_t flags;
    long time;
} kaodv_rt_msg_t;

/* Route information flag */
#define KAODV_RT_GW_ENCAP            0x1
#define KAODV_RT_REPAIR              0x2

/* Two types of route update messages. Packet is coming in our is going out. */
#define PKT_INBOUND  1
#define PKT_OUTBOUND 2

typedef struct kaodv_rt_update_msg {
    u_int8_t type;
    u_int32_t src;
    u_int32_t dest;
    int ifindex;
} kaodv_rt_update_msg_t;

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
void kaodv_netlink_send_rerr_msg(int type, __u32 src, __u32 dest, 
				 int ifindex);
#endif /* __KERNEL__ */

#endif
