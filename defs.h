/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University.
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
#ifndef DEFS_H
#define DEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <sys/time.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>


#define AODV_UU_VERSION "0.2"

#define AODV_LOG_PATH "/var/log/aodvd.log"
#define AODV_RT_LOG_PATH "/var/log/aodvd_rt.log"

#define INFTY 0xff
#define IS_INFTY(x) ((x & INFTY) == INFTY) ? 1 : 0
#define max(A,B) ( (A) > (B) ? (A):(B))

#define MINTTL 1  /* min TTL in the packets sent locally */

struct local_host_info {
  u_int32_t ipaddr;       /* The local IP address */
  u_int32_t netmask;      /* The netmask we use */
  u_int32_t broadcast;  
  u_int32_t seqno;        /* Sequence number */
  u_int64_t bcast_time;   /* The time of the last broadcast msg sent */
  u_int32_t rreq_id;     /* RREQ id */
  u_int32_t wait_on_reboot_timer_id;
  int gateway_mode;
  char ifname[128];
};

/* This will point to a struct containing information about the local
   host */
struct local_host_info *this_host;

 /* Broadcast address according to draft (255.255.255.255) */
#define AODV_BROADCAST 0xFFFFFFFF

#define AODV_PORT 654

/* AODV Message types */
#define AODV_HELLO    0 /* Really never used as a separate type... */
#define AODV_RREQ     1
#define AODV_RREP     2
#define AODV_RERR     3
#define AODV_RREP_ACK 4

/* A generic AODV packet header struct... */
typedef struct {
  u_int8_t type;
} AODV_msg;

/* AODV Extension types */
#define RREQ_EXT 1
#define RREP_EXT 1
#define RREP_HELLO_INTERVAL_EXT 2
#define RREP_HELLO_NEIGHBOR_SET_EXT 3

typedef struct {
  u_int8_t type;
  u_int8_t length;
  /* Type specific data follows here */
} AODV_ext;

/* MACROS to access AODV extensions... */
#define AODV_EXT_HDR_SIZE sizeof(AODV_ext)
#define AODV_EXT_DATA(ext) ((AODV_ext *)((char *)ext + AODV_EXT_HDR_SIZE))
#define AODV_EXT_NEXT(ext) ((AODV_ext *)((char *)ext + AODV_EXT_HDR_SIZE + ext->length))
#define AODV_EXT_SIZE(ext) (AODV_EXT_HDR_SIZE + ext->length)

/* The callback function */
typedef void (*callback_func_t) (int);
extern int attach_callback_func(int fd, callback_func_t func);

#endif


