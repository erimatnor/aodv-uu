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
#ifndef PARAMS_H
#define PARAMS_H

#define max(A,B) ( (A) > (B) ? (A):(B))

#define K                       5

/* These are according to draft v8: */
#define ACTIVE_ROUTE_TIMEOUT    3000
#define ALLOWED_HELLO_LOSS      2
/* If expanding ring search is used, BLACKLIST_TIMEOUT should be?: */
#define BLACKLIST_TIMEOUT       RREQ_RETRIES*NET_TRAVERSAL_TIME + (TTL_THRESHOLD - TTL_START)/TTL_INCREMENT + 1 + RREQ_RETRIES
#define FLOOD_RECORD_TIME       2 * NET_TRAVERSAL_TIME
#define DELETE_PERIOD           K * max(ACTIVE_ROUTE_TIMEOUT, ALLOWED_HELLO_LOSS * HELLO_INTERVAL)
#define HELLO_INTERVAL          1000
#define LOCAL_ADD_TTL           2
#define MAX_REPAIR_TTL          0.3 * NET_DIAMETER
#define MY_ROUTE_TIMEOUT        2 * ACTIVE_ROUTE_TIMEOUT
#define NET_DIAMETER            35
#define NEXT_HOP_WAIT           NODE_TRAVERSAL_TIME + 10
#define NODE_TRAVERSAL_TIME     40
#define REV_ROUTE_LIFE          NET_TRAVERSAL_TIME
#define NET_TRAVERSAL_TIME      3 * NODE_TRAVERSAL_TIME * NET_DIAMETER / 2
#define RREQ_RETRIES            2
/* TTL_START defaults to 1, but if hello msg's are used it should be
   at least 2. (AODV draft v.8, section 12). */
#define TTL_START               2
#define TTL_INCREMENT           2
#define TTL_THRESHOLD           7

#endif
