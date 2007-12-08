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
#include <netinet/ip_icmp.h>

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
#include "seek_list.h" /* for struct ip_data */

#define BUFSIZE 2048

extern int internet_gw_mode;

/* #define DEBUG_PACKET */
static struct ipq_handle *h;

static void packet_input(int fd);

void packet_buff_add(unsigned long id, u_int32_t dest_addr);
void packet_buff_destroy();
int packet_buff_drop(u_int32_t dest_addr);

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
  struct ip_data *ipd;
  ipq_packet_msg_t *pkt;
  struct iphdr *ip;
  struct udphdr *udp;
  struct icmphdr *icmp = NULL;
  rt_table_t *fwd_rt, *rev_rt;  
  u_int32_t dest_addr, src_addr;
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
  
  dest_addr = ntohl(ip->daddr);
  src_addr = ntohl(ip->saddr);
  
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
    /* We set the gratuituos flag also on ICMP ECHO requests, since
       the destination will also need a route back for the reply... */
  case IPPROTO_ICMP:
    icmp = (struct icmphdr *)((char *)ip + (ip->ihl << 2));
    if(icmp->type == ICMP_ECHO)
      rreq_flags |= RREQ_GRATUITOUS;
#ifdef DEBUG_PACKET
   log(LOG_INFO, 0, "packet_input: setting G flag for RREQ to %s", 
       ip_to_str(dest_addr));  
#endif
   
    break;
  default:
  }
#ifdef DEBUG_PACKET
   log(LOG_INFO, 0, "packet_input: pkt to %s", ip_to_str(dest_addr));  
#endif

  /* Find the entry of the neighboring node and the destination  (if any). */
  rev_rt = rt_table_find_active(src_addr);
  fwd_rt = rt_table_find_active(dest_addr);
  
  /* If a packet is received on the NF_IP_PRE_ROUTING hook,
     i.e. inbound on the interface and we don't have a route to the
     destination, we should send an RERR to the source and then drop
     the package... */
  /* NF_IP_PRE_ROUTING = 0 */
  if(dest_addr != this_host->ipaddr && 
     (fwd_rt == NULL && pkt->hook == 0)) {
    rt_table_t *rt_entry;
    RERR *rerr;

    if(internet_gw_mode && this_host->gateway_mode)
      goto update_timers;
    
    /* There is an expired entry in the routing table we wan't to send
       along the seqno in the RERR... */
    if((rt_entry = rt_table_find(dest_addr)) != NULL)
      rerr = rerr_create(0, rt_entry->dest_addr, rt_entry->dest_seqno);
    else
      rerr = rerr_create(0, dest_addr, 0);

#ifdef DEBUG
    log(LOG_DEBUG, 0, "packet_input: Sending RERR for unknown dest %s", 
	ip_to_str(dest_addr));
#endif
    aodv_socket_send((AODV_msg *)rerr, src_addr, 
		     RERR_CALC_SIZE(rerr), 1); 
    
    if(this_host->wait_on_reboot_timer_id) {
      timer_remove(this_host->wait_on_reboot_timer_id);
      this_host->wait_on_reboot_timer_id = 
	timer_new(DELETE_PERIOD, wait_on_reboot_timeout, this_host);
    }
    
    status = ipq_set_verdict(h, pkt->packet_id, NF_DROP, 0, NULL);
    if (status < 0)
      die(h);
    return;
  }

 update_timers: 
  /* When forwarding a packet, we update the lifetime of the
     destination's routing table entry, as well as the entry for its
     next hop (if not the same). AODV draft 10, section 6.2. */
  if(fwd_rt != NULL && dest_addr != this_host->ipaddr) {
    rt_table_t *next_hop_rt;
    
    next_hop_rt = rt_table_find(fwd_rt->next_hop);

    if(next_hop_rt != NULL && 
       next_hop_rt->dest_addr != fwd_rt->dest_addr)
      rt_table_update_timeout(next_hop_rt, ACTIVE_ROUTE_TIMEOUT);
    
    rt_table_update_timeout(fwd_rt, ACTIVE_ROUTE_TIMEOUT);
  }
  /* Also update the reverse route along the path back, since routes
     between originators and the destination are expected to be
     symmetric. */
  if(rev_rt != NULL)
    rt_table_update_timeout(rev_rt, ACTIVE_ROUTE_TIMEOUT);
  
  /* If the packet is not interesting we just let it go through... */
  if((dest_addr == AODV_BROADCAST) || 
     (dest_addr == this_host->ipaddr) ||
     (dest_addr == this_host->broadcast) ||
     (internet_gw_mode && this_host->gateway_mode))
    goto accept;

#ifdef DEBUG_PACKET
  log(LOG_INFO, 0, "packet_input: d=%s s=%s id=%lu", 
      ip_to_str(dest_addr), ip_to_str(ntohl(ip->saddr)), pkt->packet_id); 
#endif

  if(fwd_rt == NULL || (fwd_rt->hcnt == 1 && (fwd_rt->flags & UNIDIR))) {
    /* Buffer packets... Packets are queued by the ip_queue_aodv.o module
     already. We only need to save the handle id, and return the proper
     verdict when we know what to do... */
    packet_buff_add(pkt->packet_id, dest_addr);

    /* If the request is generated locally by an application, we save
       the IP header + 64 bits of data for sending an ICMP Destination
       Host Unreachable in case we don't find a route... */
    if(src_addr == this_host->ipaddr) {
      ipd = (struct ip_data *)malloc(sizeof(struct ip_data));
      ipd->len = (ip->ihl << 2) + 8;
      memcpy(ipd->data, ip, ipd->len);
    } else
      ipd = NULL;

    rreq_route_discovery(dest_addr, rreq_flags, ipd);
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
  u_int32_t dest_addr;
};

struct pkt_buff *pkt_buff_head = NULL;
struct pkt_buff *pkt_buff_tail = NULL;

/* Buffer a packet in a FIFO queue. Implemented as a linked list,
   where we add elements at the end and remove at the beginning.... */
void packet_buff_add(unsigned long id, u_int32_t dest_addr) {
  struct pkt_buff *new;

  if((new = (struct pkt_buff *)malloc(sizeof(struct pkt_buff))) == NULL) {
    fprintf(stderr, "packet_buff_add: Malloc failed!\n");
    exit(-1);
  }
#ifdef DEBUG
  log(LOG_INFO, 0, "packet_buff_add: %s buffering pkt (id=%lu)", 
      ip_to_str(dest_addr), id);
#endif
  if(pkt_buff_head == NULL)
    pkt_buff_head = new;
  
  new->id = id;
  new->dest_addr = dest_addr;
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

int packet_buff_drop(u_int32_t dest_addr) {
  struct pkt_buff *curr, *tmp, *prev;
  int count = 0;  
  
  curr = pkt_buff_head;
  prev = NULL;
  while(curr != NULL) {
        
    if(curr->dest_addr == dest_addr) {
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
      ip_to_str(dest_addr), count);
#endif

  return count;
}

int packet_buff_send(u_int32_t dest_addr) {
  struct pkt_buff *curr, *tmp, *prev;
  int count = 0;  
 
  curr = pkt_buff_head;
  prev = NULL;
  while(curr != NULL) {
    if(curr->dest_addr == dest_addr) {
       log(LOG_INFO, 0, "pkt_buff_send: %s - SENDING pkt id=%lu!", 
	   ip_to_str(dest_addr), curr->id);
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
     ip_to_str(dest_addr), count);
#endif
 return count;
}
