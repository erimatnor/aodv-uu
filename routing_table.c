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
 * Authors: Erik Nordström, <erik.nordstrom@it.uu.se>
 *
 *****************************************************************************/

#include <time.h>

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include "routing_table.h"
#include "aodv_timeout.h"
#include "packet_queue.h"
#include "aodv_rerr.h"
#include "aodv_hello.h"
#include "aodv_socket.h"
#include "k_route.h"
#include "timer_queue.h"
#include "defs.h"
#include "debug.h"
#include "params.h"
#include "seek_list.h"
#endif				/* NS_PORT */

static unsigned int hashing(u_int32_t * addr, hash_value * hash);

#ifndef NS_PORT
static void rt_table_remove_precursor(u_int32_t dest_addr);
#endif

void NS_CLASS rt_table_init()
{
    int i;

    rt_tbl.num_entries = 0;
    rt_tbl.num_active = 0;

    /* We do a for loop here... NS does not like us to use memset() */
    for (i = 0; i < RT_TABLESIZE; i++) {
	INIT_LIST_HEAD(&rt_tbl.tbl[i]);
    }
}

void NS_CLASS rt_table_destroy()
{
    int i;
    list_t *tmp = NULL, *pos = NULL;

    for (i = 0; i < RT_TABLESIZE; i++) {
	list_foreach_safe(pos, tmp, &rt_tbl.tbl[i]) {
	    rt_table_t *rt = (rt_table_t *)pos;
	    /* Destroy and free memory used by precursor list for this
	       entry */
	    
	    list_detach(pos);
	    
	    DEBUG(LOG_INFO, 0, "Clearing bucket %d", i);
	    
	    precursor_list_destroy(rt);

#ifndef NS_PORT
	    if (rt->state == VALID && k_del_rte(rt->dest_addr, 0, 0) < 0)
		log(LOG_WARNING, errno, __FUNCTION__,
		    "Could not delete kernel route!");
#endif
	    /* Free memory used by this route entry */
	    free(rt);
	}
    }
}

/* Calculate a hash value and table index given a key... */
unsigned int hashing(u_int32_t * addr, hash_value * hash)
{
    /*   *hash = (*addr & 0x7fffffff); */
    *hash = *addr;

    return (*hash & RT_TABLEMASK);
}

rt_table_t *NS_CLASS rt_table_insert(u_int32_t dest_addr, u_int32_t next,
				     u_int8_t hops, u_int32_t seqno,
				     u_int32_t life, u_int8_t state,
				     u_int16_t flags, unsigned int ifindex)
{
    hash_value hash;
    unsigned int index;
    list_t *pos;
    rt_table_t *rt;
    
    /* Calculate hash key */
    index = hashing(&dest_addr, &hash);

    /* Check if we already have an entry for dest_addr */
    list_foreach(pos, &rt_tbl.tbl[index]) {
	rt = (rt_table_t *)pos;
	if (memcmp(&rt->dest_addr, &dest_addr, sizeof(u_int32_t)) == 0) {
	    DEBUG(LOG_INFO, 0, "%s already exist in routing table!", 
		  ip_to_str(dest_addr));

	    return NULL;
	}
    }

    if ((rt = (rt_table_t *) malloc(sizeof(rt_table_t))) == NULL) {
	fprintf(stderr, "Malloc failed!\n");
	exit(-1);
    }
    
    memset(rt, 0, sizeof(rt_table_t));

    rt->dest_addr = dest_addr;
    rt->next_hop = next;
    rt->dest_seqno = seqno;
    rt->flags = flags;
    rt->hcnt = hops;
    rt->ifindex = ifindex;
    rt->hash = hash;
    rt->state = state;

    /* Initialize timers */
    rt->rt_timer.handler = &NS_CLASS route_expire_timeout;
    rt->rt_timer.data = rt;

    rt->ack_timer.handler = &NS_CLASS rrep_ack_timeout;
    rt->ack_timer.data = rt;

    rt->hello_timer.handler = &NS_CLASS hello_timeout;
    rt->hello_timer.data = rt;

    rt->last_hello_time.tv_sec = 0;
    rt->last_hello_time.tv_usec = 0;
    rt->hello_cnt = 0;

    rt->nprec = 0;
    INIT_LIST_HEAD(&rt->precursors);

    /* Insert first in bucket... */
    
    rt_tbl.num_entries++;
    

    DEBUG(LOG_INFO, 0, "Inserting %s (bucket %d) next hop %s",
	  ip_to_str(dest_addr), index, ip_to_str(next));

    list_add(&rt_tbl.tbl[index], &rt->l);
    
    if (state == INVALID) {

	if (flags & RT_REPAIR) {
	    rt->rt_timer.handler = &NS_CLASS local_repair_timeout;
	    life = ACTIVE_ROUTE_TIMEOUT;
	} else {
	    rt->rt_timer.handler = &NS_CLASS route_delete_timeout;
	    life = DELETE_PERIOD;
	}

    } else {
	rt_tbl.num_active++;
#ifndef NS_PORT
	/* Add route to kernel routing table ... */
	if (k_add_rte(dest_addr, next, 0, hops, rt->ifindex) < 0)
	    log(LOG_WARNING, errno, __FUNCTION__,
		"Could not add kernel route!");
#endif
    }

    DEBUG(LOG_INFO, 0, "New timer for %s, life=%d",
	  ip_to_str(rt->dest_addr), life);

    if (life != 0)
	timer_set_timeout(&rt->rt_timer, life);

    /* In case there are buffered packets for this destination, we
     * send them on the new route. */
    if (rt->state == VALID && seek_list_remove(seek_list_find(dest_addr)))
	packet_queue_send(dest_addr);

    return rt;
}

rt_table_t *NS_CLASS rt_table_update(rt_table_t * rt, u_int32_t next,
				     u_int8_t hops, u_int32_t seqno,
				     u_int32_t lifetime, u_int8_t state,
				     u_int16_t flags)
{
    if (rt->state == INVALID && state == VALID) {

	/* If this previously was an expired route, but will now be
	   active again we must add it to the kernel routing
	   table... */
	rt_tbl.num_active++;
#ifndef NS_PORT
	if (k_add_rte(rt->dest_addr, next, 0, hops, rt->ifindex) < 0)
	    log(LOG_WARNING, errno, __FUNCTION__,
		"Could not add kernel route!");
	else
	    DEBUG(LOG_INFO, 0,
		  "Added kernel route for expired %s",
		  ip_to_str(rt->dest_addr));
#endif

    } else if (rt->next_hop != 0 && rt->next_hop != next) {
	DEBUG(LOG_INFO, 0,
	      "rt->next_hop=%s, new_next_hop=%s",
	      ip_to_str(rt->next_hop), ip_to_str(next));

#ifndef NS_PORT
	if (k_chg_rte(rt->dest_addr, next, 0, hops, rt->ifindex) < 0)
	    log(LOG_WARNING, errno, __FUNCTION__,
		"Could not update kernel route!");
#endif
    }

    rt->dest_seqno = seqno;
    rt->flags = flags;
    rt->next_hop = next;

    if (hops > 1 && rt->hcnt == 1) {
	rt->last_hello_time.tv_sec = 0;
	rt->last_hello_time.tv_usec = 0;
	rt->hello_cnt = 0;
	timer_remove(&rt->hello_timer);
    }

    rt->hcnt = hops;
    rt->rt_timer.handler = &NS_CLASS route_expire_timeout;

    rt_table_update_timeout(rt, lifetime);
    
    /* Finally, mark as VALID */
    rt->state = state;
    
    /* In case there are buffered packets for this destination, we send
     * them on the new route. */
    if (rt->state == VALID && seek_list_remove(seek_list_find(rt->dest_addr)))
	packet_queue_send(rt->dest_addr);

    return rt;
}

NS_INLINE rt_table_t *NS_CLASS rt_table_update_timeout(rt_table_t * rt,
						       u_int32_t lifetime)
{
    struct timeval new_timeout;

    if (!rt)
	return NULL;

    if (rt->state == VALID) {
	/* Check if the current valid timeout is larger than the new
	   one - in that case keep the old one. */
	gettimeofday(&new_timeout, NULL);
	timeval_add_msec(&new_timeout, lifetime);

	if (timeval_diff(&rt->rt_timer.timeout, &new_timeout) < 0)
	    timer_set_timeout(&rt->rt_timer, lifetime);
    } else
	timer_set_timeout(&rt->rt_timer, lifetime);

    return rt;
}

rt_table_t *NS_CLASS rt_table_find(u_int32_t dest_addr)
{
    hash_value hash;
    unsigned int index;
    list_t *pos;

    if (rt_tbl.num_entries == 0)
	return NULL;

    /* Calculate index */
    index = hashing(&dest_addr, &hash);

    /* Handle collisions: */
    list_foreach(pos, &rt_tbl.tbl[index]) {
	rt_table_t *rt = (rt_table_t *)pos;

	if (rt->hash != hash) 
	    continue;
	
	if (memcmp(&dest_addr, &rt->dest_addr, sizeof(u_int32_t)) == 0)
	    return rt;

    }
    return NULL;
}

/* Route expiry and Deletion. */
int NS_CLASS rt_table_invalidate(rt_table_t * rt)
{
    struct timeval now;

    gettimeofday(&now, NULL);

    if (rt == NULL)
	return -1;

    if (rt->hello_timer.used)
	DEBUG(LOG_DEBUG, 0, "last HELLO: %ld",
	      timeval_diff(&now, &rt->last_hello_time));

    /* Remove any pending, but now obsolete timers. */
    timer_remove(&rt->rt_timer);
    timer_remove(&rt->hello_timer);
    timer_remove(&rt->ack_timer);

    /* If the route is already invalidated, do nothing... */
    if (rt->state == INVALID) {
	DEBUG(LOG_DEBUG, 0, "Route %s already invalidated!!!",
	      ip_to_str(rt->dest_addr));
	return -1;
    }

    /* Mark the route as invalid */
    rt->state = INVALID;
    rt_tbl.num_active--;

    rt->hello_cnt = 0;

    /* When the lifetime of a route entry expires, increase the sequence
       number for that entry. */
    rt->dest_seqno++;

    rt->last_hello_time.tv_sec = 0;
    rt->last_hello_time.tv_usec = 0;

#ifndef NS_PORT
    /* Delete kernel routing table entry. */
    if (k_del_rte(rt->dest_addr, 0, 0) < 0)
	log(LOG_WARNING, errno, __FUNCTION__, "Could not delete kernel route!");
#endif

    if (rt->flags & RT_REPAIR) {
	/* Set a timeout for the repair */

	rt->rt_timer.handler = &NS_CLASS local_repair_timeout;
	timer_set_timeout(&rt->rt_timer, ACTIVE_ROUTE_TIMEOUT);

	DEBUG(LOG_DEBUG, 0, "%s kept for repairs during %u msecs",
	      ip_to_str(rt->dest_addr), ACTIVE_ROUTE_TIMEOUT);
    } else {
	/* Schedule a deletion timer */
	rt->rt_timer.handler = &NS_CLASS route_delete_timeout;
	timer_set_timeout(&rt->rt_timer, DELETE_PERIOD);

	DEBUG(LOG_DEBUG, 0, "%s removed in %u msecs",
	      ip_to_str(rt->dest_addr), DELETE_PERIOD);
    }

    return 0;
}

void NS_CLASS rt_table_delete(u_int32_t dest_addr)
{
    rt_table_t *rt;

   
    rt = rt_table_find(dest_addr);
    
    if(!rt) {
	DEBUG(LOG_ERR, 0, "No route entry to delete");
	return;
    }
    
    list_detach(&rt->l);
    
    precursor_list_destroy(rt);
    
    if (rt->state == VALID) {
	
#ifndef NS_PORT
	if (k_del_rte(dest_addr, 0, 0) < 0)
	    log(LOG_WARNING, errno, __FUNCTION__,
		"Could not delete kernel route!");
	
#endif
	rt_tbl.num_active--;
    }
    /* Make sure any timers are removed... */
    timer_remove(&rt->rt_timer);
    timer_remove(&rt->hello_timer);
    timer_remove(&rt->ack_timer);
    
    rt_tbl.num_entries--;
    
    free(rt);
    return;
}

NS_STATIC void NS_CLASS rt_table_remove_precursor(u_int32_t dest_addr)
{
    int i;
    list_t *pos;
    /* Loop through the whole table and remove destination from any
       precursor lists */
    for (i = 0; i < RT_TABLESIZE; i++)
	list_foreach(pos, &rt_tbl.tbl[i]) 
	    precursor_remove((rt_table_t *)pos, dest_addr);
}

/****************************************************************/

/* Add an neighbor to the active neighbor list. */

void NS_CLASS precursor_add(rt_table_t * rt, u_int32_t addr)
{
    precursor_t *pr;
    list_t *pos;

    /* Sanity check */
    if (!rt)
	return;

    /* Check if the node is already in the precursors list. */
    list_foreach(pos, &rt->precursors) {
	pr = (precursor_t *)pos;
	    
	if (pr->neighbor == addr)
	    return;
    }
    
    if ((pr = (precursor_t *) malloc(sizeof(precursor_t))) == NULL) {
	perror("Could not allocate memory for precursor node!!\n");
	exit(-1);
    }

    DEBUG(LOG_INFO, 0, "Adding precursor %s to rte %s",
	  ip_to_str(addr), ip_to_str(rt->dest_addr));
    
    pr->neighbor = addr;
    
    /* Insert in precursors list */
    
    list_add(&rt->precursors, &pr->l);
    rt->nprec++;

    return;
}

/****************************************************************/

/* Remove a neighbor from the active neighbor list. */

void NS_CLASS precursor_remove(rt_table_t * rt, u_int32_t addr)
{
    list_t *pos;

    /* Sanity check */
    if (!rt)
	return;

    list_foreach(pos, &rt->precursors) {
	precursor_t *pr = (precursor_t *)pos;
	if (pr->neighbor == addr) {
	    DEBUG(LOG_INFO, 0, "Removing precursor %s from rte %s",
		  ip_to_str(addr), ip_to_str(rt->dest_addr));

	    list_detach(pos);
	    rt->nprec--;
	    free(pr);
	    return;
	}
    }
}

/****************************************************************/

/* Delete all entries from the active neighbor list. */

void precursor_list_destroy(rt_table_t * rt)
{
    list_t *pos, *tmp;
    
    /* Sanity check */
    if (!rt)
	return;

    list_foreach_safe(pos, tmp, &rt->precursors) {
	precursor_t *pr = (precursor_t *)pos;
	list_detach(pos);
	rt->nprec--;
	free(pr);
    }
}
