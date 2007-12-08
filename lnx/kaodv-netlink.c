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
 * Author: Erik Nordström, <erik.nordstrom@it.uu.se>
 *
 *****************************************************************************/
#include <linux/if.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/netlink.h>
#ifdef KERNEL26	
#include <linux/security.h>
#endif
#include <net/sock.h>

#include "kaodv-netlink.h"
#include "kaodv-expl.h"
#include "kaodv-queue.h"

static int peer_pid;
static struct sock *kaodvnl;
static DECLARE_MUTEX(kaodvnl_sem);

/* For 2.4 backwards compatibility */
#ifndef KERNEL26
#define sk_receive_queue receive_queue 
#define sk_socket socket
#endif

extern int active_route_timeout, qual_th, is_gateway;

static struct sk_buff *kaodv_netlink_build_msg(int type, void *m, int len)
{
	unsigned char *old_tail;
	size_t size = 0;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct kaodv_rt_msg *rtmsg;
	

	size = NLMSG_SPACE(len);
	
	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
	    goto nlmsg_failure;
	
	old_tail= skb->tail;
	nlh = NLMSG_PUT(skb, 0, 0, type, size - sizeof(*nlh));
	
	rtmsg = NLMSG_DATA(nlh);
	
	memcpy(rtmsg, m, len);
			
	nlh->nlmsg_len = skb->tail - old_tail;
	return skb;

nlmsg_failure:
	if (skb)
		kfree_skb(skb);

	printk(KERN_ERR "kaodv: error creating rt timeout message\n");
	return NULL;
}


void kaodv_netlink_send_rt_msg(int type, __u32 src, __u32 dest)
{
    struct sk_buff *skb = NULL;
    struct kaodv_rt_msg m;
	 
    m.src = src;
    m.dest = dest;
    m.nhop = 0;
    m.time = 0;
    m.flags = 0;

    skb = kaodv_netlink_build_msg(type, &m, sizeof(struct kaodv_rt_msg));
	
    if (skb == NULL) {
	printk("kaodv_netlink: skb=NULL\n");
	return;
    }
    /* status = netlink_unicast(kaodvnl, skb, peer_pid, MSG_DONTWAIT); */
    netlink_broadcast(kaodvnl, skb, 0, AODVGRP_NOTIFY, GFP_USER);
}

void kaodv_netlink_send_rt_update_msg(int type, __u32 src, __u32 dest, 
				      int ifindex)
{
    struct sk_buff *skb = NULL;
    struct kaodv_rt_update_msg m;
	 
    m.type = type;
    m.src = src;
    m.dest = dest;
    m.ifindex = ifindex;

    skb = kaodv_netlink_build_msg(KAODVM_ROUTE_UPDATE, &m, 
				  sizeof(struct kaodv_rt_update_msg));
	
    if (skb == NULL) {
	printk("kaodv_netlink: skb=NULL\n");
	return;
    }
    /* status = netlink_unicast(kaodvnl, skb, peer_pid, MSG_DONTWAIT); */
    netlink_broadcast(kaodvnl, skb, 0, AODVGRP_NOTIFY, GFP_USER);
}
void kaodv_netlink_send_rerr_msg(int type, __u32 src, __u32 dest, 
				 int ifindex)
{
    struct sk_buff *skb = NULL;
    struct kaodv_rt_update_msg m;
	 
    m.type = type;
    m.src = src;
    m.dest = dest;
    m.ifindex = ifindex;

    skb = kaodv_netlink_build_msg(KAODVM_SEND_RERR, &m, 
				  sizeof(struct kaodv_rt_update_msg));
	
    if (skb == NULL) {
	printk("kaodv_netlink: skb=NULL\n");
	return;
    }
    /* status = netlink_unicast(kaodvnl, skb, peer_pid, MSG_DONTWAIT); */
    netlink_broadcast(kaodvnl, skb, 0, AODVGRP_NOTIFY, GFP_USER);
}

static int
kaodv_netlink_receive_peer(unsigned char type, void *msg, unsigned int len)
{
	int status = 0;
	struct kaodv_rt_msg *rtm;
	struct kaodv_conf_msg *cm;
	struct expl_entry e;
	
	if (len < sizeof(*msg))
		return -EINVAL;

	switch (type) {
	case KAODVM_ADDROUTE:
	    rtm = (struct kaodv_rt_msg *)msg;
	    /* printk("Received add route event for %s flags=%d\n", print_ip(rtm->dest), rtm->flags); */
	    
	    if (kaodv_expl_get(rtm->dest, &e))
		kaodv_expl_update(rtm->dest, rtm->nhop, rtm->time, rtm->flags);
	    else
		kaodv_expl_add(rtm->dest, rtm->nhop, rtm->time, rtm->flags);
	    
	    kaodv_queue_set_verdict(KAODV_QUEUE_SEND, rtm->dest);
	    break;
	case KAODVM_DELROUTE:  
	    rtm = (struct kaodv_rt_msg *)msg;
	    //printk("Received del route event\n");
	    kaodv_expl_del(rtm->dest);
	    kaodv_queue_set_verdict(KAODV_QUEUE_DROP, rtm->dest);
	    break;
	case KAODVM_NOROUTE_FOUND:
	    rtm = (struct kaodv_rt_msg *)msg;
	    //printk("Received no route found event\n");
	    kaodv_queue_set_verdict(KAODV_QUEUE_DROP, rtm->dest);
	    break;
	case KAODVM_CONFIG:  
	    cm = (struct kaodv_conf_msg *)msg;
	    //printk("Received no route found event\n");
	    active_route_timeout = cm->active_route_timeout;
	    qual_th = cm->qual_th; 
	    is_gateway = cm->is_gateway;
	    break;
	default:
		status = -EINVAL;
	}
	return status;
}


static int
kaodv_netlink_rcv_nl_event(struct notifier_block *this,
		   unsigned long event, void *ptr)
{
	struct netlink_notify *n = ptr;

	if (event == NETLINK_URELEASE &&
	    n->protocol == NETLINK_AODV && n->pid) {
	    if (n->pid == peer_pid) {
		peer_pid = 0;
		kaodv_expl_flush();
		kaodv_queue_flush();
	    }
	    return NOTIFY_DONE;
	}
	return NOTIFY_DONE;
}

static struct notifier_block kaodv_nl_notifier = {
	.notifier_call	= kaodv_netlink_rcv_nl_event,
};

#define RCV_SKB_FAIL(err) do { netlink_ack(skb, nlh, (err)); return; } while (0)
static inline void
kaodv_netlink_rcv_skb(struct sk_buff *skb)
{
	int status, type, pid, flags, nlmsglen, skblen;
	struct nlmsghdr *nlh;

	skblen = skb->len;
	if (skblen < sizeof(*nlh)) {
	    printk("skblen to small\n");
	    return;
	}
	nlh = (struct nlmsghdr *)skb->data;
	nlmsglen = nlh->nlmsg_len;
	if (nlmsglen < sizeof(*nlh) || skblen < nlmsglen) {
	    printk("nlsmsg=%d skblen=%d to small\n", nlmsglen, skblen);
	    return;
	}
	pid = nlh->nlmsg_pid;
	flags = nlh->nlmsg_flags;
	
	if(pid <= 0 || !(flags & NLM_F_REQUEST) || flags & NLM_F_MULTI)
		RCV_SKB_FAIL(-EINVAL);
		
	if (flags & MSG_TRUNC)
		RCV_SKB_FAIL(-ECOMM);
		
	type = nlh->nlmsg_type;
	
/* 	printk("kaodv_netlink: type=%d\n", type); */
	/* if (type < NLMSG_NOOP || type >= IPQM_MAX) */
/* 		RCV_SKB_FAIL(-EINVAL); */
		
/* 	if (type <= IPQM_BASE) */
/* 		return; */
#ifdef KERNEL26		
	if (security_netlink_recv(skb))
		RCV_SKB_FAIL(-EPERM);
#endif	
	//write_lock_bh(&queue_lock);
	
	if (peer_pid) {
		if (peer_pid != pid) {
		    //write_unlock_bh(&queue_lock);
			RCV_SKB_FAIL(-EBUSY);
		}
	}
	else
	    peer_pid = pid;
		
	//write_unlock_bh(&queue_lock);
	
	status = kaodv_netlink_receive_peer(type, NLMSG_DATA(nlh),
				    skblen - NLMSG_LENGTH(0));
	if (status < 0)
		RCV_SKB_FAIL(status);
		
	if (flags & NLM_F_ACK)
		netlink_ack(skb, nlh, 0);
        return;
}

static void
kaodv_netlink_rcv_sk(struct sock *sk, int len)
{
	do {
		struct sk_buff *skb;

		if (down_trylock(&kaodvnl_sem))
			return;

		while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
			kaodv_netlink_rcv_skb(skb);
			kfree_skb(skb);
		}
		
		up(&kaodvnl_sem);

	} while (kaodvnl && kaodvnl->sk_receive_queue.qlen);

    return;
}

int kaodv_netlink_init(void)
{

    netlink_register_notifier(&kaodv_nl_notifier);
    kaodvnl = netlink_kernel_create(NETLINK_AODV, kaodv_netlink_rcv_sk);
    if (kaodvnl == NULL) {
	printk(KERN_ERR "ip_queue: failed to create netlink socket\n");
	netlink_unregister_notifier(&kaodv_nl_notifier);
	return -1;
    }
    return 1;   
}

void kaodv_netlink_fini(void)
{
    sock_release(kaodvnl->sk_socket);
    down(&kaodvnl_sem);
    up(&kaodvnl_sem);
    
    netlink_unregister_notifier(&kaodv_nl_notifier);
}
