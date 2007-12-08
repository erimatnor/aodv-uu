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
 * Authors: Erik Nordström, <erno3431@student.uu.se>
 *          Henrik Lundgren, <henrikl@docs.uu.se>
 *
 *****************************************************************************/
#include <netinet/in.h>

#include "aodv_hello.h"
#include "aodv_timeout.h"
#include "aodv_rrep.h"
#include "aodv_rreq.h"
#include "routing_table.h"
#include "packet_input.h"
#include "timer_queue.h"
#include "params.h"
#include "aodv_socket.h"
#include "defs.h"
#include "debug.h"

/* #define DEBUG_HELLO  */

#define ROUTE_TIMEOUT_SLACK 100
#define JITTER_INTERVAL 100

extern int unidir_hack, receive_n_hellos, hello_jittering;
static struct timer hello_timer;

long hello_jitter() {
    if (hello_jittering)
	return (long)(((float) random() / RAND_MAX - 0.5) * JITTER_INTERVAL);
    else
	return 0;
}

void hello_init()
{
    hello_timer.handler = hello_send;
    hello_timer.data = NULL;
    hello_timer.used = 0;
    /* Set first broadcast time */
    gettimeofday(&this_host.bcast_time, NULL);
    timer_add_msec(&hello_timer, HELLO_INTERVAL + hello_jitter());
}

void hello_send(void *arg)
{
    RREP *rrep;
    AODV_ext *ext = NULL;
    u_int8_t flags = 0;
    long time_diff;
    struct timeval now;
    rt_table_t *entry;
    int msg_size = RREP_SIZE;
    int i;

    gettimeofday(&now, NULL);
    time_diff = timeval_diff(&now, &this_host.bcast_time);

    /* This check will ensure we don't send unnecessary hello msgs, in case
       we have sent other bcast msgs within HELLO_INTERVAL */
    if (time_diff >= HELLO_INTERVAL * 1000) {

	for (i = 0; i < MAX_NR_INTERFACES; i++) {
	    if (!DEV_NR(i).enabled)
		continue;
#ifdef DEBUG_HELLO
	log(LOG_DEBUG, 0, "SEND_BCAST: sending Hello to %s",
	    ip_to_str(AODV_BROADCAST));
#endif
	rrep =
	    rrep_create(flags, 0, 0, DEV_NR(i).ipaddr, 
			this_host.seqno,
			DEV_NR(i).ipaddr,
			ALLOWED_HELLO_LOSS * HELLO_INTERVAL);

	/* Assemble a RREP extension which contain our neighbor set... */
	if (unidir_hack) {
	    u_int32_t neigh;
	    int i;
	    ext = (AODV_ext *) ((char *) rrep + RREP_SIZE);
	    ext->type = RREP_HELLO_NEIGHBOR_SET_EXT;
	    ext->length = 0;
	    for (i = 0; i < RT_TABLESIZE; i++) {
		entry = routing_table[i];
		while (entry != NULL) {
		    /* If an entry has an active hello timer, we assume
		       that we are receiving hello messages from that
		       node... */
		    if (entry->hello_timer.used) {
#ifdef DEBUG_HELLO
			log(LOG_INFO, 0,
			    "hello_send: Adding %s to hello neighbor set ext",
			    ip_to_str(entry->dest_addr));
#endif
			neigh = htonl(entry->dest_addr);
			memcpy(AODV_EXT_NEXT(ext), &neigh, 4);
			ext->length += 4;
		    }
		    entry = entry->next;
		}
	    }
	    if (ext->length)
		msg_size = RREP_SIZE + AODV_EXT_SIZE(ext);
	}

	aodv_socket_send((AODV_msg *) rrep, AODV_BROADCAST, msg_size, 1, 
			 &DEV_NR(i));
	}

	timer_add_msec(&hello_timer, HELLO_INTERVAL + hello_jitter());
    } else
	timer_add_msec(&hello_timer, HELLO_INTERVAL + hello_jitter() - 
		       time_diff / 1000);
}


/* Process a hello message */
void hello_process(RREP * hello, int rreplen, unsigned int ifindex)
{
    u_int32_t hello_dst, hello_seqno;
    u_int32_t hello_interval = HELLO_INTERVAL;
    u_int32_t ext_neighbor;
    rt_table_t *rt_entry;
    AODV_ext *ext = NULL;
    int i, hcnt, unidir_link = 1;
    struct timeval now;

    gettimeofday(&now, NULL);

    hello_dst = ntohl(hello->dest_addr);
    hello_seqno = ntohl(hello->dest_seqno);

    /* Check for hello interval extension: */
    ext = (AODV_ext *) ((char *) hello + RREP_SIZE);

    while (rreplen > RREP_SIZE) {
	switch (ext->type) {
 	case RREP_HELLO_INTERVAL_EXT:
	    if (ext->length == 4) {
		memcpy(&hello_interval, AODV_EXT_DATA(ext), 4);
		hello_interval = ntohl(hello_interval);
#ifdef DEBUG_HELLO
		log(LOG_INFO, 0,
		    "HELLO_process: Hello extension interval=%lu!",
		    hello_interval);
#endif
	    } else
		log(LOG_WARNING, 0,
		    "hello_process: Bad hello interval extension!");
	    break;
	case RREP_HELLO_NEIGHBOR_SET_EXT:
#ifdef DEBUG_HELLO
	    log(LOG_INFO, 0, "HELLO_process: RREP_HELLO_NEIGHBOR_SET_EXT");
#endif
	    for (i = 0; i < ext->length; i = i + 4) {
		ext_neighbor = ntohl(*(u_int32_t *)((char *) AODV_EXT_DATA(ext) + i));

		if (memcmp(&ext_neighbor, &DEV_IFINDEX(ifindex).ipaddr, 4) 
		    == 0)
		    unidir_link = 0;
	    }
	    break;
	default:
	    log(LOG_WARNING, 0,
		"hello_process: Bad extension!! type=%d, length=%d",
		ext->type, ext->length);
	    ext = NULL;
	    break;
	}
	if (ext == NULL)
	    break;

	rreplen -= AODV_EXT_SIZE(ext);
	ext = AODV_EXT_NEXT(ext);
    }

#ifdef DEBUG_HELLO
    log(LOG_DEBUG, 0, "hello_process: rcvd HELLO from %s, seqno %lu",
	ip_to_str(hello_dst), hello_seqno);
#endif
    /* This neighbor should only be valid after receiving 3
       consecutive hello messages... */
    if (receive_n_hellos)
	hcnt = INFTY;
    else
	hcnt = 1;
    
    if ((rt_entry = rt_table_find(hello_dst)) == NULL) {
	/* No active or expired route in the routing table. So we add a
	   new entry... */
	if (unidir_hack && unidir_link) {
	    rt_entry =
		rt_table_insert(hello_dst, hello_dst, INFTY, hello_seqno,
				ALLOWED_HELLO_LOSS * HELLO_INTERVAL + 
				ROUTE_TIMEOUT_SLACK,
				UNIDIR, ifindex);
#ifdef DEBUG_UNIDIR
	    log(LOG_INFO, 0, "hello_process: %s is UNI-DIR!!!",
		ip_to_str(rt_entry->dest_addr));
#endif
	} else {
	    rt_entry =
		rt_table_insert(hello_dst, hello_dst, hcnt, hello_seqno,
				ALLOWED_HELLO_LOSS * HELLO_INTERVAL + 
				ROUTE_TIMEOUT_SLACK,
				NEIGHBOR, ifindex);
#ifdef DEBUG_UNIDIR
	    log(LOG_INFO, 0, "hello_process: %s is BI-DIR!!!",
		ip_to_str(rt_entry->dest_addr));
#endif
	}

	rt_entry->hello_cnt = 1;
	
    } else {

	if(unidir_hack && 
	   unidir_link &&
	   rt_entry->hcnt != INFTY && 
	   rt_entry->hcnt > 1 &&
	   !(rt_entry->flags & NEIGHBOR)) {
	    goto hello_update;
	}	

	if (receive_n_hellos && rt_entry->hello_cnt < (receive_n_hellos - 1)) {
	    if (timeval_diff(&now, &rt_entry->last_hello_time) / 1000 <
		(hello_interval + hello_interval / 2))
		rt_entry->hello_cnt++;
	    else
		rt_entry->hello_cnt = 1;

	    rt_entry->last_hello_time = now;
	    return;
	}

	/* Update sequence numbers if the hello contained new info... */
	if (unidir_hack && unidir_link)
	    rt_table_update(rt_entry, hello_dst, 1, hello_seqno,
			    ALLOWED_HELLO_LOSS * HELLO_INTERVAL + 
			    ROUTE_TIMEOUT_SLACK, UNIDIR);
        else
	    rt_table_update(rt_entry, hello_dst, 1, hello_seqno,
			    ALLOWED_HELLO_LOSS * HELLO_INTERVAL + 
			    ROUTE_TIMEOUT_SLACK, NEIGHBOR);
	
	
    }
    /* In case we were performing a route discovery, stop it now
       and send any puffered packets... */
    if (seek_list_remove(hello_dst))
	packet_buff_send(hello_dst);
    
 hello_update:
    hello_update_timeout(rt_entry, ALLOWED_HELLO_LOSS * hello_interval);
    rt_entry->last_hello_time = now;
    return;
}

/* Used to update neighbor when non-hello AODV message is received... */
void hello_process_non_hello(AODV_msg * aodv_msg, u_int32_t source, unsigned int ifindex)
{
    rt_table_t *rt_entry = NULL;
    u_int32_t seqno = 0;
    
    switch (aodv_msg->type) {
    case AODV_RREQ:
	if (((RREQ *) aodv_msg)->orig_addr == source)
	    seqno = ntohl(((RREQ *) aodv_msg)->orig_seqno);
	break;
    case AODV_RREP:
	break;
    case AODV_RERR:
	break;
    default:
	break;
    }
    rt_entry = rt_table_find(source);
   
   
    /* Check message type, and if we can retrieve a sequence number */
    if (rt_entry == NULL)
	rt_entry = rt_table_insert(source, source, 1, seqno,
				   ALLOWED_HELLO_LOSS * HELLO_INTERVAL + 
				   ROUTE_TIMEOUT_SLACK, NEIGHBOR, ifindex);
    else {
	/* Don't update anything if this is a uni-directional link... */
	if (rt_entry->flags & UNIDIR)
	    return;

	if (seqno > rt_entry->dest_seqno)
	    rt_table_update(rt_entry, source, 1, seqno, 
			    ALLOWED_HELLO_LOSS * HELLO_INTERVAL + 
			    ROUTE_TIMEOUT_SLACK, NEIGHBOR);
	else
	    rt_table_update_timeout(rt_entry, ALLOWED_HELLO_LOSS * 
				    HELLO_INTERVAL + ROUTE_TIMEOUT_SLACK);
    }
    /* Remove destination from seeking list if a route has been
       found. */
    if (seek_list_remove(source))
	packet_buff_send(source);
	
    hello_update_timeout(rt_entry, ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
}

#define HELLO_DELAY 50		/* The extra time we should allow an hello
				   message to take (due to processing) before
				   assuming lost . */

inline void hello_update_timeout(rt_table_t * rt_entry, long time)
{
    timer_add_msec(&rt_entry->hello_timer, time + HELLO_DELAY);
}
