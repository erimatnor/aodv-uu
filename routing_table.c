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

static unsigned int hashing(u_int32_t *addr, hash_value *hash);
static void rt_table_remove_precursor(u_int32_t dest);
/*  static void rt_table_remove_entries_with_next_hop(u_int32_t dest); */
int total_entries = 0;

void rt_table_init() {
#ifdef DEBUG
  log(LOG_INFO, 0, "routing_table_init: initializing routing table!");
#endif
  memset(routing_table, 0, RT_TABLESIZE);
}

/* This is a kind of bad hack function for flushing the routing
   cache. What we really want to do is just delete a specific entry,
   not the all of them... */
int flush_rt_cache() {
  int fd = -1;
  char *delay = "-1";
  
  log(LOG_INFO, 0, "FLUSHING routing cache!");
  if ((fd = open("/proc/sys/net/ipv4/route/flush", O_WRONLY)) < 0) 
    return -1;
  
  if (write(fd, &delay, 2*sizeof(char)) < 0)
    return -1;
 
  close(fd);
  
  return 0;
}

void rt_table_destroy() {
  int i = 0;
  rt_table_t *entry, *tmp;

  for(i = 0; i < RT_TABLESIZE; i++) {
    entry = routing_table[i];
     while(entry != NULL) {
       /* Destroy and free memory used by precursor list for this
          entry */
#ifdef DEBUG
       log(LOG_INFO, 0, "routing_table_destroy: Clearing bucket %d", i);
#endif
       precursor_list_destroy(entry);

       if(!IS_INFTY(entry->hcnt)) {
	 k_del_rte(entry->dest, 0, 0);
	/*   k_del_arp(entry->dest); */
#ifdef DEBUG
	 log(LOG_INFO, 0, "routing_table_destroy: Removing kernel route %s", 
	     ip_to_str(entry->dest));
#endif
       }
       tmp = entry;
       entry = entry->next;
       /* Free memory used by this route entry */
       free(tmp);
     }
  }
}

/* Calculate a hash value and table index given a key... */
unsigned int hashing(u_int32_t *addr, hash_value *hash) {

 /*   *hash = (*addr & 0x7fffffff); */
  *hash = *addr;

  return (*hash & RT_TABLEMASK);
}

rt_table_t *rt_table_insert(u_int32_t dest, u_int32_t next, u_int8_t hops, 
		u_int32_t seqno, u_int32_t life, u_int16_t flags) {
  rt_table_t *entry;
  hash_value hash; 
  unsigned int index;

  /* Calculate hash key */
  index = hashing(&dest, &hash);
  
  entry = routing_table[index];

 /*   printf("rt_table_insert(): Adding dest=%s, nex_hop=%s\n", ip_to_str(dest), ip_to_str(next)); */

#ifdef DEBUG
  log(LOG_INFO, 0, "rt_table_insert: Inserting %s into bucket %d", 
      ip_to_str(dest), index);
  
  if(entry != NULL)
    log(LOG_INFO, 0, "rt_table_insert: Collision in bucket %s detected!", 
	index);
#endif

  /* Check if we already have an entry for dest */ 
  while(entry != NULL) {    
    if(memcmp(&entry->dest, &dest, sizeof(u_int32_t)) == 0)
      return NULL;
  }
  
  if((entry = (rt_table_t *)malloc(sizeof(rt_table_t))) == NULL) {
    fprintf(stderr, "insert_rt_table: Malloc failed!\n");
    exit(-1);
  }

  entry->dest = dest;
  entry->next_hop = next;
  entry->dest_seqno = seqno;
  entry->expire = get_currtime() + life;
  entry->flags = flags;
  entry->hcnt = hops;
  entry->last_hcnt = 0;
  entry->last_life = life;
  entry->precursors = NULL;
  entry->hash = hash;
  entry->ack_timer_id = 0;
  entry->hello_timer_id = 0;

  /* Insert first in bucket... */
  entry->next = routing_table[index];
  routing_table[index] = entry;

  total_entries++;
  /* We should also update our own sequence number whenever our
     neighbor set changes.  (AODV draft v.8, section 8.4.1.) */
  if(hops == 1)
    this_host->seqno++;  

  /* Add route to kernel routing table ... */
  if(dest == next)
    k_add_rte(dest, 0, 0, hops);
  else
    k_add_rte(dest, next, 0, hops);
  
  /*  flush_rt_cache(); */
#ifdef DEBUG  
  log(LOG_INFO, 0, "rt_table_insert: New timer for %s, life=%d", 
      ip_to_str(entry->dest), life); 
#endif
  
  entry->timer_id = timer_new(life, route_expire_timeout, entry);
  
  return entry;
}

rt_table_t* rt_table_update(rt_table_t *entry, u_int32_t next, u_int8_t hops, 
			    u_int32_t seqno, u_int32_t newlife, u_int16_t flags) {
  /* If the next hop for some reason has changed - update kernel
     routing table. */
  if ((entry->next_hop != 0) && 
      (entry->next_hop != next) &&
      !IS_INFTY(entry->hcnt)) {
#ifdef DEBUG
    log(LOG_INFO, 0, "rt_table_update: entry->nxt_addr=%s, next=%s", 
	ip_to_str(entry->next_hop), ip_to_str(next));
#endif 
    if(k_chg_rte(entry->dest, next, 0, hops) < 0)
      fprintf(stderr, "rt_table_update: Could not update kernel routing table!!!\n");
  }

  /* If this previously was an expired route, but will now be active again
     we must add it to the kernel routing table... */
  if(IS_INFTY(entry->hcnt) && !IS_INFTY(hops)) {
    
    /* We should also update our own sequence number whenever our
       neighbor set changes.  (AODV draft v.8, section 8.4.1.) */
    if(hops == 1)
      this_host->seqno++;
#ifdef DEBUG
    log(LOG_INFO, 0, "rt_table_update: Adding KERNEL route for expired %s", 
	ip_to_str(entry->dest));
#endif
    if(entry->dest == next)
      k_add_rte(entry->dest, 0, 0, hops);
    else
      k_add_rte(entry->dest, next, 0, hops);
  }
  entry->dest_seqno = seqno;
  entry->last_life = newlife;
  entry->flags |= flags;
  entry->next_hop = next;
  entry->last_hcnt = entry->hcnt;
  entry->hcnt = hops;


   /* If newlife = 0 the timer and the expire time should not be
     updated... */
  if(newlife != 0) 
    rt_table_update_timeout(entry, newlife);

  return entry;
}
void rt_table_insert_neighbor(u_int32_t neigh) {
  
  if(rt_table_find(neigh) == NULL)
    rt_table_insert(neigh, neigh, 1, 0, ALLOWED_HELLO_LOSS*HELLO_INTERVAL, 
		    NEIGHBOR);
}

rt_table_t *rt_table_update_timeout(rt_table_t *entry, u_int32_t life) {
  entry->expire = get_currtime() + life;
  timer_remove(entry->timer_id);
#ifdef DEBUG  
  /*      log(LOG_INFO, 0, "rt_table_update(): New timer for %s, life=%d",  */
  /*  	ip_to_str(entry->dest), newlife);  */
#endif
  entry->timer_id = timer_new(life, route_expire_timeout, entry);
  return entry;
}

rt_table_t *rt_table_find_active(u_int32_t dest) {
  rt_table_t *entry;
  
  entry = rt_table_find(dest);

  if(entry == NULL || IS_INFTY(entry->hcnt))
    return NULL;

  return entry;
}

rt_table_t *rt_table_find(u_int32_t dest) {
  rt_table_t *entry;
  hash_value hash;
  unsigned int index;

  /* Calculate index */
  index = hashing(&dest, &hash);

  entry = routing_table[index];

 /*   printf("Trying to find %s in table\n", ip_to_str(dest)); */

  /* Handle collisions: */
  while(entry != NULL) {
    
    if(entry->hash != hash) 
      continue;
    
    if(memcmp(&dest, &entry->dest, sizeof(u_int32_t)) == 0)
      return entry;
    
    entry = entry->next;
  }
  return NULL;
}
int rt_table_is_next_hop(u_int32_t dest) {
  rt_table_t *entry;
  int i;
  for(i = 0; i < RT_TABLESIZE; i++) {
    for(entry = routing_table[i]; entry != NULL; entry = entry->next) {
      if(dest != entry->dest && (dest == entry->next_hop))
	return 1;
    }
  }
  return 0;
}

/* Route expiry and Deletion, AODV draft v.9, section 8.13. */
int rt_table_invalidate(rt_table_t *entry) {
  
  if (entry == NULL)
    return -1;
  
  /* Remove hello timer if it exists... */
  if(entry->hello_timer_id)
    timer_remove(entry->hello_timer_id);

  /* Save last hopcount */
  entry->last_hcnt = entry->hcnt;
  /* Set infinity hop count */
  entry->hcnt = INFTY;
  
  /* When the lifetime of a route entry expires, increase the sequence
     number for that entry. (AODV draft v.9, section 8.13.) */
  entry->dest_seqno++;

  entry->flags = 0;

  entry->last_life = 0;
 
  if(entry->hcnt == 1) {
    /* AODV draft 9, section 8.11: When a node invalidates a route to
       a neighboring node, it MUST also delete that neighbor from any
       precursor lists for routes to other nodes.  This prevents
       precursor lists from containing stale entries of neighbors with
       which the node is no longer able to communicate.  The node does
       this by inspecting the precursor list of each destination entry
       in its routing table, and deleting the lost neighbor from any
       list in which it appears. */

    rt_table_remove_precursor(entry->dest); 
    
    /* We should also update our own sequence number whenever our
       neighbor set changes. */
    this_host->seqno++;
  }

  /* We should delete the precursor list for all unreachable
     destinations. */
  precursor_list_destroy(entry);

  /* Delete kernel route and arp entry. Also make sure the routing
     cache does not contain any stale entries that could make IP
     believe we still have contact with this route */
  k_del_rte(entry->dest, 0, 0); 
 /*   k_del_arp(entry->dest); */
   
  /* Schedule a deletion timer, but first make sure a possible existing 
     timer is deleted */
  timer_remove(entry->timer_id);

#ifdef DEBUG
  log(LOG_DEBUG, 0, "rt_table_invalidate: %s removed in %u msecs", 
      ip_to_str(entry->dest), DELETE_PERIOD);
#endif
 
  entry->timer_id = timer_new(DELETE_PERIOD, route_delete_timeout, entry);
  
  return 0;
}

void rt_table_delete(u_int32_t dest) {
  rt_table_t *entry, *prev;
  hash_value hash;
  unsigned int index;
  
  /* Calculate index */
  index = hashing(&dest, &hash);

  entry = routing_table[index];

  prev = NULL;
  for(; entry != NULL; prev = entry, entry = entry->next) {
    if(entry->hash != hash)
      continue;
    
    if(memcmp(&dest, &entry->dest, sizeof(u_int32_t)) == 0) {

      if(prev == NULL)
	routing_table[index] = entry->next;
      else
	prev = entry->next;
      
      total_entries--;
      
      precursor_list_destroy(entry);
      if(!IS_INFTY(entry->hcnt)) {
	k_del_rte(dest, 0, 0);
	/*  k_del_arp(entry->dest); */
      }
      /* Make sure any timers are removed... */
      if(entry->timer_id)
	timer_remove(entry->timer_id);
      if(entry->hello_timer_id)
	timer_remove(entry->hello_timer_id);
      if(entry->ack_timer_id)
	timer_remove(entry->ack_timer_id);
      free(entry);
      return;
    }
  }
}

static void rt_table_remove_precursor(u_int32_t dest) {
  rt_table_t *entry;
  int i;
  /* Loop through the whole table and remove destination from any
      precursor lists */
  for(i = 0; i < RT_TABLESIZE; i++) {
    for(entry = routing_table[i]; entry != NULL; entry = entry->next)
      precursor_remove(entry, dest);
  }
}

/*  static void rt_table_remove_entries_with_next_hop(u_int32_t dest) { */
/*    rt_table_t *entry; */
/*    int i; */

/*    for(i = 0; i < RT_TABLESIZE; i++) {  */
/*      for(entry = routing_table[i]; entry != NULL; entry = entry->next) { */
/*        if(entry->next_hop == dest) */
/*  	rt_table_invalidate(entry); */
/*      } */
/*    } */
/*  } */
/****************************************************************/

/* Add an neighbor to the active neighbor list. */

void precursor_add(rt_table_t *rt_entry, u_int32_t addr) {
  precursor_t *pr_entry;

  /* Sanity check */
  if(rt_entry == NULL)
    return;

  pr_entry = rt_entry->precursors;

  /* Check if the node is already in the precursors list. */
  for (; pr_entry != NULL; pr_entry = pr_entry->next)
    if (pr_entry->neighbor == addr) 
      return;

  if((pr_entry = (precursor_t *) malloc(sizeof(precursor_t))) == NULL) {
    perror("Could not allocate memory for precursor node!!\n");
    exit(-1);
  }
#ifdef DEBUG
  log(LOG_INFO, 0, "precursor_add: Adding precursor %s to rte %s", 
      ip_to_str(addr), ip_to_str(rt_entry->dest));
#endif
  /* Insert first in precursors list */
  pr_entry->neighbor = addr;
  pr_entry->next = rt_entry->precursors;
  rt_entry->precursors = pr_entry;
}

/****************************************************************/

/* Remove a neighbor from the active neighbor list. */

void precursor_remove(rt_table_t *rt_entry, u_int32_t addr) {
  precursor_t *curr, *prev;
  
  /* Sanity check */
  if(rt_entry == NULL)
    return;

  prev = NULL;
  curr = rt_entry->precursors;
 
  while(curr != NULL) {
    if(curr->neighbor == addr) { /* <----- SEGFAULT HERE */
#ifdef DEBUG
      log(LOG_INFO, 0, "precursor_remove: Removing precursor %s from rte %s", 
	  ip_to_str(addr), ip_to_str(rt_entry->dest));
#endif
      if(prev == NULL)
	/* We are about to remove the first entry.. */
	rt_entry->precursors = curr->next;
      else 
	prev->next = curr->next;

      free(curr);
      return;
    }
    prev = curr;
    curr = curr->next;
  }
}

/****************************************************************/

/* Delete all entries from the active neighbor list. */

void precursor_list_destroy(rt_table_t *rt_entry) {
  precursor_t *pr_entry, *tmp;

  /* Sanity check */
  if(rt_entry == NULL)
    return;

  pr_entry = rt_entry->precursors;
  
  while (pr_entry != NULL) {
    tmp = pr_entry;
    pr_entry = pr_entry->next;
    free(tmp);
  }
  rt_entry->precursors = NULL;
}
