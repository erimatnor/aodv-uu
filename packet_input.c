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
#include <linux/if_ether.h>
#include <linux/netfilter.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include "defs.h"
#include "debug.h"
#include "routing_table.h"
#include "aodv_rreq.h"
#include "aodv_rerr.h"
#include "libipq.h"
#include "params.h"
#include "timer_queue.h"
#include "aodv_timeout.h"
#include "aodv_hello.h"
#include "aodv_socket.h"

#define BUFSIZE 2048

/* #define DEBUG_PACKET */
static struct ipq_handle *h;

static void packet_input(int fd);

void packet_buff_add(unsigned long id, u_int32_t dest);
void packet_buff_destroy();
int packet_buff_drop(u_int32_t dest);

static void die(struct ipq_handle *h) {
  ipq_destroy_handle(h);
  packet_buff_destroy();
  exit(1);
}

void packet_input_cleanup() {
  die(h);
}

void packet_input_init() {
  int status;

  h = ipq_create_handle(0);

  if(h == NULL) {
    fprintf(stderr, "Initialization failed!\n");
    exit(1);
  }

  status = ipq_set_mode(h, IPQ_COPY_PACKET, 0);
  if (status < 0) {
    die(h);
  }  
  if (attach_callback_func(h->fd, packet_input) < 0) {
    log(LOG_ERR, 0, "packet_input_init:"); 
  }
}

static void packet_input(int fd) {
  int status;
  char buf[BUFSIZE];
  ipq_packet_msg_t *pkt;
  struct iphdr *ip;
  struct udphdr *udp;
  rt_table_t *fwd_rt, *rev_rt;  
  u_int32_t dest, src;
  u_int8_t rreq_flags = 0;
  
  ipq_read(h, buf, BUFSIZE, 0);
  
  status = ipq_message_type(buf);
  
  if(status == NLMSG_ERROR) {
    fprintf(stderr, "ERROR packet_input: Check that the ip_queue_aodv.o module is loaded.\n");
    die(h);
  }
  
  pkt = ipq_get_packet(buf);
  
#ifdef DEBUG_PACKET
    printf("Protocol %u indev=%s outdev=%s\n",
	   pkt->hw_protocol, pkt->indev_name, pkt->outdev_name);  
#endif

  if(ntohs(pkt->hw_protocol) != ETH_P_IP) {
    ipq_set_verdict(h, pkt->packet_id, NF_ACCEPT, 0, NULL);
    return;
  }
  ip = (struct iphdr *)pkt->payload;
  
  dest = ntohl(ip->daddr);
  src = ntohl(ip->saddr);
  
  switch(ip->protocol) {
    /* Don't process AODV control packets (UDP on port 654). They
       are accounted for on the aodv socket */
  case IPPROTO_UDP:
    udp = (struct udphdr *)((char *)ip + (ip->ihl << 2));
    if(ntohs(udp->dest) == AODV_PORT ||
       ntohs(udp->source) == AODV_PORT)
      goto accept;
    break;
    /* If this is a TCP packet and we don't have a route, we should
       set the gratuituos flag in the RREQ. */
  case IPPROTO_TCP:
    rreq_flags |= RREQ_GRATUITOUS;
    break;
  default:
  }
#ifdef DEBUG_PACKET
   log(LOG_INFO, 0, "packet_input: pkt to %s", ip_to_str(dest));  
#endif

  /* Find the entry of the neighboring node and the destination  (if any). */
  rev_rt = rt_table_find_active(src);
  fwd_rt = rt_table_find_active(dest);
  
  /* If a packet is received on the NF_IP_PRE_ROUTING hook,
     i.e. inbound on the interface and we don't have a route to the
     destination, we should send an RERR to the source and then drop
     the package... */
  /* NF_IP_PRE_ROUTING = 0 */
  if(dest != this_host->ipaddr && 
     (fwd_rt == NULL && pkt->hook == 0)) {
    rt_table_t *rt_entry;
    RERR *rerr;

    rerr = (RERR *)aodv_socket_new_msg();
    rerr->type = AODV_RERR;
    rerr->reserved = 0;
    rerr->dest_count = 1;
    rerr->dest_addr = htonl(dest);
    
    /* There is an expired entry in the routing table we wan't to send
       along the seqno in the RERR... */
    if((rt_entry = rt_table_find(dest)) != NULL)
      rerr->dest_seqno = rt_entry->dest_seqno;
    else
      rerr->dest_seqno = 0;
    
#ifdef DEBUG
    log(LOG_DEBUG, 0, "packet_input: Sending RERR for unknown dest %s", 
	ip_to_str(dest));
#endif
    aodv_socket_send((AODV_msg *)rerr, src, 
		     RERR_CALC_SIZE(rerr->dest_count), 1); 

    status = ipq_set_verdict(h, pkt->packet_id, NF_DROP, 0, NULL);
    if (status < 0)
      die(h);
    return;
  }
    
  if(rev_rt != NULL) {
    
    /* If the packet is from a node in our neighbor set, update the
       hello timeout. (AODV draft v.9 section 8.9.) */
    /*  if(rev_rt->hcnt == 1)  */
/*        hello_update_timeout(rev_rt, ALLOWED_HELLO_LOSS*HELLO_INTERVAL); */
    
    /* Always update the route timeout of both forward and reverse
     routes when packet is forwarded... (AODV draft v.9, section
     8.2) */
    rt_table_update_timeout(rev_rt, ACTIVE_ROUTE_TIMEOUT);
  }
  
  if(fwd_rt != NULL && dest != this_host->ipaddr)
    rt_table_update_timeout(fwd_rt, ACTIVE_ROUTE_TIMEOUT);
  

  /* If the packet is not interesting we just let it go through... */
  if((dest == AODV_BROADCAST) || 
     (dest == this_host->ipaddr) ||
     (dest == this_host->broadcast))
    goto accept;

#ifdef DEBUG_PACKET
  log(LOG_INFO, 0, "packet_input: d=%s s=%s id=%lu", 
      ip_to_str(dest), ip_to_str(ntohl(ip->saddr)), pkt->packet_id); 
#endif

  if(fwd_rt == NULL || (fwd_rt->hcnt == 1 && (fwd_rt->flags & UNIDIR))) {
    /* Buffer packets... Packets are queued by the ip_queue_aodv.o module
     already. We only need to save the handle id, and return the proper
     verdict when we know what to do... */
    packet_buff_add(pkt->packet_id, dest);
    rreq_route_discovery(dest, rreq_flags);
    return;
  } 
  
 accept:
  
  status = ipq_set_verdict(h, pkt->packet_id, NF_ACCEPT, 0, NULL);
  if (status < 0)
    die(h);
  
  return;
}
/* Packet buffering: */
struct pkt_buff {
  struct pkt_buff *next;
  unsigned long id;
  u_int32_t dest;
};

struct pkt_buff *pkt_buff_head = NULL;
struct pkt_buff *pkt_buff_tail = NULL;

/* Buffer a packet in a FIFO queue. Implemented as a linked list,
   where we add elements at the end and remove at the beginning.... */
void packet_buff_add(unsigned long id, u_int32_t dest) {
  struct pkt_buff *new;

  if((new = (struct pkt_buff *)malloc(sizeof(struct pkt_buff))) == NULL) {
    fprintf(stderr, "packet_buff_add: Malloc failed!\n");
    exit(-1);
  }
#ifdef DEBUG
  log(LOG_INFO, 0, "packet_buff_add: %s buffering pkt (id=%lu)", 
      ip_to_str(dest), id);
#endif
  if(pkt_buff_head == NULL)
    pkt_buff_head = new;
  
  new->id = id;
  new->dest = dest;
  new->next = NULL;

  if(pkt_buff_tail != NULL)
    pkt_buff_tail->next = new;
  
  pkt_buff_tail = new;
}

void packet_buff_destroy() {
  struct pkt_buff *curr, *tmp;
  int count = 0;
  
  curr = pkt_buff_head;
  while(curr != NULL) {
    tmp = curr;
    curr = curr->next;
    ipq_set_verdict(h, tmp->id, NF_DROP, 0, NULL);
    free(tmp);
    count++;
  }
#ifdef DEBUG
  log(LOG_INFO, 0, "packet_buff_destroy: Destroyed %d buffered packets!", 
      count);
#endif
}

int packet_buff_drop(u_int32_t dest) {
  struct pkt_buff *curr, *tmp, *prev;
  int count = 0;  
  
  curr = pkt_buff_head;
  prev = NULL;
  while(curr != NULL) {
        
    if(curr->dest == dest) {
      ipq_set_verdict(h, curr->id, NF_DROP, 0, NULL);
      if(prev == NULL) 
	pkt_buff_head = curr->next;
      else
	prev->next = curr->next;
      
      /* If we remove the last element in the queue we must update the
         tail pointer */
      if(curr->next == NULL)
	pkt_buff_tail = prev;

      tmp = curr;
      curr = curr->next;
      free(tmp);
      count++;
      continue;
    }
    curr = curr->next;
  }
#ifdef DEBUG
  log(LOG_INFO, 0, "pkt_buff_drop: %s - DROPPED %d packets!", 
      ip_to_str(dest), count);
#endif

  return count;
}

int packet_buff_send(u_int32_t dest) {
  struct pkt_buff *curr, *tmp, *prev;
  int count = 0;  
 
  curr = pkt_buff_head;
  prev = NULL;
  while(curr != NULL) {
    if(curr->dest == dest) {
       log(LOG_INFO, 0, "pkt_buff_send: %s - SENDING pkt id=%lu!", 
	   ip_to_str(dest), curr->id);
      ipq_set_verdict(h, curr->id, NF_ACCEPT, 0, NULL);
      
      if(prev == NULL) 
	pkt_buff_head = curr->next;
      else
	prev->next = curr->next;
      
      /* If we remove the last element in the queue we must update the
	 tail pointer */
      if(curr->next == NULL)
	pkt_buff_tail = prev;
      
      tmp = curr;
      curr = curr->next;
      free(tmp);
      count++;
      continue;
    }
    curr = curr->next;
  }
#ifdef DEBUG
 log(LOG_INFO, 0, "pkt_buff_send: %s - SENT %d packets!", 
     ip_to_str(dest), count);
#endif
 return count;
}
