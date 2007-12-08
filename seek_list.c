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
#include <stdlib.h>

#include "seek_list.h"
#include "timer_queue.h"
#include "aodv_timeout.h"
#include "defs.h"
#include "params.h"

/* The seek list is a linked list of destinations we are seeking
   (with RREQ's). */
 
static seek_list_t *seek_list_head = NULL;

seek_list_t *seek_list_insert(u_int32_t dest_addr, u_int32_t dest_seqno, 
			      int ttl, u_int8_t flags, struct ip_data *ipd) {
  seek_list_t *entry;

  if((entry = malloc(sizeof(seek_list_t))) < 0) {
    fprintf(stderr, "seek_list_insert: Failed malloc\n");
    exit(-1);
  }
  
  entry->dest_addr = dest_addr;
  entry->dest_seqno = dest_seqno;
  entry->flags = flags;
  entry->reqs = 0;
  entry->ttl = ttl;
  entry->ipd = ipd;
  entry->next = seek_list_head;
  seek_list_head = entry;
  
  return entry;
}

int seek_list_remove(u_int32_t dest_addr) {
  seek_list_t *curr, *prev;
  
  curr = seek_list_head;
  prev = NULL;
  
  while(curr != NULL) {
    if(curr->dest_addr == dest_addr) {
      
      if(prev == NULL)
	seek_list_head = curr->next;
      else 
	prev->next = curr->next;
	
      /* Make sure any timers are removed */
      if(curr->timer_id)
	timer_remove(curr->timer_id);
      if(curr->ipd)
	free(curr->ipd);
      free(curr);
      return 0;
    }
    prev = curr;
    curr = curr->next;
  }
  return -1;
}

seek_list_t *seek_list_find(u_int32_t dest_addr) {
  seek_list_t *entry;

  entry = seek_list_head;
  
  while(entry != NULL) {
    if(entry->dest_addr == dest_addr)
      return entry;
    
    entry = entry->next;
  }
  return NULL;
}
