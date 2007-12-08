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
#ifndef PARAMS_H
#define PARAMS_H

#include "defs.h"

#define K                       5

#ifdef AODVUU_LL_FEEDBACK
#define ACTIVE_ROUTE_TIMEOUT    10000
#define TTL_START               1
#define DELETE_PERIOD           ACTIVE_ROUTE_TIMEOUT
#else
/* HELLO messages are used: */
#define ACTIVE_ROUTE_TIMEOUT    3000
#define DELETE_PERIOD           K * max(ACTIVE_ROUTE_TIMEOUT, ALLOWED_HELLO_LOSS * HELLO_INTERVAL)
#define TTL_START               2
#endif

#define ALLOWED_HELLO_LOSS      2
/* If expanding ring search is used, BLACKLIST_TIMEOUT should be?: */
#define BLACKLIST_TIMEOUT       RREQ_RETRIES * NET_TRAVERSAL_TIME + (TTL_THRESHOLD - TTL_START)/TTL_INCREMENT + 1 + RREQ_RETRIES
#define HELLO_INTERVAL          1000
#define LOCAL_ADD_TTL           2
#define MAX_REPAIR_TTL          3 * NET_DIAMETER / 10
#define MY_ROUTE_TIMEOUT        2 * ACTIVE_ROUTE_TIMEOUT
#define NET_DIAMETER            35
#define NEXT_HOP_WAIT           NODE_TRAVERSAL_TIME + 10
#define NODE_TRAVERSAL_TIME     40
#define NET_TRAVERSAL_TIME      2 * NODE_TRAVERSAL_TIME * NET_DIAMETER
#define PATH_DISCOVERY_TIME     2 * NET_TRAVERSAL_TIME
#define RERR_RATELIMIT          10
#define RREQ_RETRIES            2
#define RREQ_RATELIMIT          10
#define TTL_INCREMENT           2
#define TTL_THRESHOLD           7

#endif
