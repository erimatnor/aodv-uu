/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University and Ericsson Telecom AB.
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
#include <time.h>

#include "routing_table.h"
#include "aodv_timeout.h"
#include "aodv_rerr.h"
#include "aodv_socket.h"
#include "k_route.h"
#include "timer_queue.h"
#include "defs.h"
#include "debug.h"
#include "params.h"

static unsigned int hashing(u_int32_t * addr, hash_value * hash);
static void rt_table_remove_precursor(u_int32_t dest_addr);
/*  static void rt_table_remove_entries_with_next_hop(u_int32_t dest_addr); */

void rt_table_init()
{
    memset(routing_table, 0, RT_TABLESIZE);
}

/* This is a kind of bad hack function for flushing the routing
   cache. What we really want to do is just delete a specific entry,
   not the all of them... */
int flush_rt_cache()
{
    int fd = -1;
    char *delay = "-1";

    log(LOG_INFO, 0, "FLUSHING routing cache!");
    if ((fd = open("/proc/sys/net/ipv4/route/flush", O_WRONLY)) < 0)
	return -1;

    if (write(fd, &delay, 2 * sizeof(char)) < 0)
	return -1;

    close(fd);

    return 0;
}

void rt_table_destroy()
{
    int i = 0;
    rt_table_t *rt_entry, *tmp;

    for (i = 0; i < RT_TABLESIZE; i++) {
	rt_entry = routing_table[i];
	while (rt_entry != NULL) {
	    /* Destroy and free memory used by precursor list for this
	       entry */
#ifdef DEBUG
	    log(LOG_INFO, 0, "routing_table_destroy: Clearing bucket %d",
		i);
#endif
	    precursor_list_destroy(rt_entry);

	    if (rt_entry->hcnt != INFTY)
		k_del_rte(rt_entry->dest_addr, 0, 0);

	    tmp = rt_entry;
	    rt_entry = rt_entry->next;
	    /* Free memory used by this route entry */
	    free(tmp);
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

rt_table_t *rt_table_insert(u_int32_t dest_addr, u_int32_t next,
			    u_int8_t hops, u_int32_t seqno, u_int32_t life,
			    u_int16_t flags, unsigned int ifindex)
{
    rt_table_t *rt_entry;
    hash_value hash;
    unsigned int index;

    /* Calculate hash key */
    index = hashing(&dest_addr, &hash);

    rt_entry = routing_table[index];

    /*   printf("rt_table_insert(): Adding dest_addr=%s, nex_hop=%s\n", ip_to_str(dest_addr), ip_to_str(next)); */

#ifdef DEBUG
    log(LOG_INFO, 0, "rt_table_insert: Inserting %s into bucket %d",
	ip_to_str(dest_addr), index);

    if (rt_entry != NULL)
	log(LOG_INFO, 0,
	    "rt_table_insert: Collision in bucket %d detected!", index);
#endif

    /* Check if we already have an entry for dest_addr */
    while (rt_entry != NULL) {
	if (memcmp(&rt_entry->dest_addr, &dest_addr, sizeof(u_int32_t)) ==
	    0) {
#ifdef DEBUG
	    log(LOG_INFO, 0,
		"rt_table_insert: %s already exist in routing table!!!",
		ip_to_str(dest_addr));
#endif
	    return NULL;
	}
	rt_entry = rt_entry->next;
    }

    if ((rt_entry = (rt_table_t *) malloc(sizeof(rt_table_t))) == NULL) {
	fprintf(stderr, "rt_table_insert: Malloc failed!\n");
	exit(-1);
    }

    memset(rt_entry, 0, sizeof(rt_table_t));

    rt_entry->dest_addr = dest_addr;
    rt_entry->next_hop = next;
    rt_entry->dest_seqno = seqno;
    rt_entry->flags = flags;
    rt_entry->hcnt = hops;
    rt_entry->ifindex = ifindex;
    rt_entry->last_hcnt = 0;
    rt_entry->last_life = life;
    rt_entry->precursors = NULL;
    rt_entry->hash = hash;

    /* Initialize timers */
    rt_entry->rt_timer.handler = route_expire_timeout;
    rt_entry->rt_timer.data = rt_entry;

    rt_entry->ack_timer.handler = rrep_ack_timeout;
    rt_entry->ack_timer.data = rt_entry;

    rt_entry->hello_timer.handler = hello_timeout;
    rt_entry->hello_timer.data = rt_entry;
    
    rt_entry->last_hello_time.tv_sec = 0;
    rt_entry->last_hello_time.tv_usec = 0;
    rt_entry->hello_cnt = 0;
    
    /* Insert first in bucket... */
    rt_entry->next = routing_table[index];
    routing_table[index] = rt_entry;

    /* Add route to kernel routing table ... */
    k_add_rte(dest_addr, next, 0, hops, rt_entry->ifindex);

#ifdef DEBUG
    log(LOG_INFO, 0, "rt_table_insert: New timer for %s, life=%d",
	ip_to_str(rt_entry->dest_addr), life);
#endif

    timer_add_msec(&rt_entry->rt_timer, life);
    return rt_entry;
}

rt_table_t *rt_table_update(rt_table_t * rt_entry, u_int32_t next,
			    u_int8_t hops, u_int32_t seqno,
			    u_int32_t newlife, u_int16_t flags)
{

    /* If this previously was an expired route, but will now be active again
       we must add it to the kernel routing table... */
    if (rt_entry->hcnt == INFTY && hops != INFTY) {
	rt_entry->rt_timer.handler = route_expire_timeout;
	k_add_rte(rt_entry->dest_addr, next, 0, hops, rt_entry->ifindex);
#ifdef DEBUG
	log(LOG_INFO, 0,
	    "rt_table_update: Adding kernel route for expired %s",
	    ip_to_str(rt_entry->dest_addr));
#endif
    } else if (rt_entry->next_hop != 0 && rt_entry->next_hop != next) {
#ifdef DEBUG
	log(LOG_INFO, 0, "rt_table_update: rt_entry->nxt_addr=%s, next=%s",
	    ip_to_str(rt_entry->next_hop), ip_to_str(next));
#endif
	precursor_list_destroy(rt_entry);
	k_chg_rte(rt_entry->dest_addr, next, 0, hops, rt_entry->ifindex);
    }

    rt_entry->dest_seqno = seqno;
    rt_entry->last_life = newlife;
    rt_entry->flags |= flags;
    rt_entry->next_hop = next;
    rt_entry->last_hcnt = rt_entry->hcnt;
    rt_entry->hcnt = hops;

    if (hops > 1) {
	rt_entry->last_hello_time.tv_sec = 0;
	rt_entry->last_hello_time.tv_usec = 0;
	rt_entry->hello_cnt = 0;
    }
    /* If newlife = 0 the timer and the expire time should not be
       updated... */
    if (newlife != 0)
	rt_table_update_timeout(rt_entry, newlife);

    return rt_entry;
}

inline rt_table_t *rt_table_update_timeout(rt_table_t * rt_entry,
					   long life)
{
    timer_add_msec(&rt_entry->rt_timer, life);
    return rt_entry;
}

rt_table_t *rt_table_find_active(u_int32_t dest_addr)
{
    rt_table_t *rt_entry;

    rt_entry = rt_table_find(dest_addr);

    if (rt_entry == NULL || rt_entry->hcnt == INFTY)
	return NULL;

    return rt_entry;
}

rt_table_t *rt_table_find(u_int32_t dest_addr)
{
    rt_table_t *rt_entry;
    hash_value hash;
    unsigned int index;

    /* Calculate index */
    index = hashing(&dest_addr, &hash);

    rt_entry = routing_table[index];

    /* Handle collisions: */
    while (rt_entry != NULL) {

	if (rt_entry->hash != hash) {
	    rt_entry = rt_entry->next;
	    continue;
	}
	if (memcmp(&dest_addr, &rt_entry->dest_addr, sizeof(u_int32_t)) ==
	    0)
	    return rt_entry;

	rt_entry = rt_entry->next;
    }
    return NULL;
}

int rt_table_is_next_hop(u_int32_t dest_addr)
{
    rt_table_t *rt_entry;
    int i;
    for (i = 0; i < RT_TABLESIZE; i++) {
	for (rt_entry = routing_table[i];
	     rt_entry != NULL; rt_entry = rt_entry->next) {
	    if (dest_addr != rt_entry->dest_addr &&
		dest_addr == rt_entry->next_hop)
		return 1;
	}
    }
    return 0;
}

/* Route expiry and Deletion, AODV draft v.9, section 8.13. */
int rt_table_invalidate(rt_table_t * rt_entry)
{
#ifdef DEBUG
    struct timeval now;
    gettimeofday(&now, NULL);
#endif
    
    if (rt_entry == NULL)
	return -1;

#ifdef DEBUG
    if(rt_entry->hello_timer.used) 
	log(LOG_DEBUG, 0, "route_expire_timeout: last HELLO: %ld", 
	    timeval_diff(&now, &rt_entry->last_hello_time) / 1000);
#endif
    timer_remove(&rt_entry->rt_timer);
    timer_remove(&rt_entry->hello_timer);
    timer_remove(&rt_entry->ack_timer);

    /* Save last hopcount */
    rt_entry->last_hcnt = rt_entry->hcnt;
    /* Set infinity hop count */
    rt_entry->hcnt = INFTY;

    /* When the lifetime of a route entry expires, increase the sequence
       number for that entry. (AODV draft v.10, section 6.1.) */
    rt_entry->dest_seqno++;
    
    rt_entry->flags = 0;
    
    rt_entry->last_life = 0;
    
    if (rt_entry->last_hcnt == 1)
	rt_table_remove_precursor(rt_entry->dest_addr);
    
    /* We should delete the precursor list for all unreachable
       destinations. */
    precursor_list_destroy(rt_entry);
    
    /* Delete kernel routing table entry. */
    k_del_rte(rt_entry->dest_addr, 0, 0);

    /* Schedule a deletion timer */
    rt_entry->rt_timer.handler = route_delete_timeout;
    timer_add_msec(&rt_entry->rt_timer, DELETE_PERIOD);
#ifdef DEBUG
    log(LOG_DEBUG, 0, "rt_table_invalidate: %s removed in %u msecs",
	ip_to_str(rt_entry->dest_addr), DELETE_PERIOD);
#endif

    return 0;
}

void rt_table_delete(u_int32_t dest_addr)
{
    rt_table_t *rt_entry, *prev;
    hash_value hash;
    unsigned int index;

    /* Calculate index */
    index = hashing(&dest_addr, &hash);

    for (prev = NULL, rt_entry = routing_table[index];
	 rt_entry != NULL; prev = rt_entry, rt_entry = rt_entry->next) {
	if (rt_entry->hash != hash)
	    continue;

	if (memcmp(&dest_addr, &rt_entry->dest_addr, sizeof(u_int32_t)) == 0) {
	    if (prev == NULL)
		routing_table[index] = rt_entry->next;
	    else
		prev->next = rt_entry->next;

	    precursor_list_destroy(rt_entry);

	    if (rt_entry->hcnt != INFTY)
		k_del_rte(dest_addr, 0, 0);

	    /* Make sure any timers are removed... */
	    timer_remove(&rt_entry->rt_timer);
	    timer_remove(&rt_entry->hello_timer);
	    timer_remove(&rt_entry->ack_timer);

	    free(rt_entry);
	    return;
	}
    }
}

static void rt_table_remove_precursor(u_int32_t dest_addr)
{
    rt_table_t *rt_entry;
    int i;
    /* Loop through the whole table and remove destination from any
       precursor lists */
    for (i = 0; i < RT_TABLESIZE; i++) {
	for (rt_entry = routing_table[i];
	     rt_entry != NULL; rt_entry = rt_entry->next)
	    precursor_remove(rt_entry, dest_addr);
    }
}

/****************************************************************/

/* Add an neighbor to the active neighbor list. */

void precursor_add(rt_table_t * rt_entry, u_int32_t addr)
{
    precursor_t *pr_entry;

    /* Sanity check */
    if (rt_entry == NULL)
	return;

    /* Check if the node is already in the precursors list. */
    for (pr_entry = rt_entry->precursors;
	 pr_entry != NULL; pr_entry = pr_entry->next)
	if (pr_entry->neighbor == addr)
	    return;

    if ((pr_entry = (precursor_t *) malloc(sizeof(precursor_t))) == NULL) {
	perror("Could not allocate memory for precursor node!!\n");
	exit(-1);
    }
#ifdef DEBUG
    log(LOG_INFO, 0, "precursor_add: Adding precursor %s to rte %s",
	ip_to_str(addr), ip_to_str(rt_entry->dest_addr));
#endif
    /* Insert first in precursors list */
    pr_entry->neighbor = addr;
    pr_entry->next = rt_entry->precursors;
    rt_entry->precursors = pr_entry;
}

/****************************************************************/

/* Remove a neighbor from the active neighbor list. */

void precursor_remove(rt_table_t * rt_entry, u_int32_t addr)
{
    precursor_t *curr, *prev;

    /* Sanity check */
    if (rt_entry == NULL)
	return;

    for (prev = NULL, curr = rt_entry->precursors;
	 curr != NULL; prev = curr, curr = curr->next) {
	if (curr->neighbor == addr) {
#ifdef DEBUG
	    log(LOG_INFO, 0,
		"precursor_remove: Removing precursor %s from rte %s",
		ip_to_str(addr), ip_to_str(rt_entry->dest_addr));
#endif
	    if (prev == NULL)
		/* We are about to remove the first entry.. */
		rt_entry->precursors = curr->next;
	    else
		prev->next = curr->next;

	    free(curr);
	    return;
	}
    }
}

/****************************************************************/

/* Delete all entries from the active neighbor list. */

void precursor_list_destroy(rt_table_t * rt_entry)
{
    precursor_t *tmp;

    /* Sanity check */
    if (rt_entry == NULL)
	return;

    while (rt_entry->precursors != NULL) {
	tmp = rt_entry->precursors;
	rt_entry->precursors = rt_entry->precursors->next;
	free(tmp);
    }
}
