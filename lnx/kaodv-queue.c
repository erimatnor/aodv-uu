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
#include <linux/version.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/netfilter_ipv4.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/route.h>
#include <net/icmp.h>

#include "kaodv-queue.h"
#include "kaodv-expl.h"
#include "kaodv-netlink.h"
#include "kaodv-ipenc.h"
#include "kaodv.h"
/*
 * This is basically a shameless rippoff of the linux kernel's ip_queue module.
 */

#define KAODV_QUEUE_QMAX_DEFAULT 1024
#define KAODV_QUEUE_PROC_FS_NAME "kaodv_queue"
#define NET_KAODV_QUEUE_QMAX 2088
#define NET_KAODV_QUEUE_QMAX_NAME "kaodv_queue_maxlen"

struct kaodv_rt_info {
	__u8 tos;
	__u32 daddr;
	__u32 saddr;
};

struct kaodv_queue_entry {
	struct list_head list;
	struct sk_buff *skb;
	int (*okfn) (struct sk_buff *);
	struct kaodv_rt_info rt_info;
};

typedef int (*kaodv_queue_cmpfn) (struct kaodv_queue_entry *, unsigned long);

static unsigned int queue_maxlen = KAODV_QUEUE_QMAX_DEFAULT;
static rwlock_t queue_lock = RW_LOCK_UNLOCKED;
static unsigned int queue_total;
static LIST_HEAD(queue_list);

static inline int __kaodv_queue_enqueue_entry(struct kaodv_queue_entry *entry)
{
	if (queue_total >= queue_maxlen) {
		if (net_ratelimit())
			printk(KERN_WARNING "kaodv-queue: full at %d entries, "
			       "dropping packet(s).\n", queue_total);
		return -ENOSPC;
	}
	list_add(&entry->list, &queue_list);
	queue_total++;
	return 0;
}

/*
 * Find and return a queued entry matched by cmpfn, or return the last
 * entry if cmpfn is NULL.
 */
static inline struct kaodv_queue_entry
*__kaodv_queue_find_entry(kaodv_queue_cmpfn cmpfn, unsigned long data)
{
	struct list_head *p;

	list_for_each_prev(p, &queue_list) {
		struct kaodv_queue_entry *entry = (struct kaodv_queue_entry *)p;

		if (!cmpfn || cmpfn(entry, data))
			return entry;
	}
	return NULL;
}

static inline struct kaodv_queue_entry
*__kaodv_queue_find_dequeue_entry(kaodv_queue_cmpfn cmpfn, unsigned long data)
{
	struct kaodv_queue_entry *entry;

	entry = __kaodv_queue_find_entry(cmpfn, data);
	if (entry == NULL)
		return NULL;

	list_del(&entry->list);
	queue_total--;

	return entry;
}

static inline void __kaodv_queue_flush(void)
{
	struct kaodv_queue_entry *entry;

	while ((entry = __kaodv_queue_find_dequeue_entry(NULL, 0))) {
		kfree_skb(entry->skb);
		kfree(entry);
	}
}

static inline void __kaodv_queue_reset(void)
{
	__kaodv_queue_flush();
}

static struct kaodv_queue_entry
*kaodv_queue_find_dequeue_entry(kaodv_queue_cmpfn cmpfn, unsigned long data)
{
	struct kaodv_queue_entry *entry;

	write_lock_bh(&queue_lock);
	entry = __kaodv_queue_find_dequeue_entry(cmpfn, data);
	write_unlock_bh(&queue_lock);
	return entry;
}

void kaodv_queue_flush(void)
{
	write_lock_bh(&queue_lock);
	__kaodv_queue_flush();
	write_unlock_bh(&queue_lock);
}

int
kaodv_queue_enqueue_packet(struct sk_buff *skb, int (*okfn) (struct sk_buff *))
{
	int status = -EINVAL;
	struct kaodv_queue_entry *entry;
	struct iphdr *iph = SKB_NETWORK_HDR_IPH(skb);

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);

	if (entry == NULL) {
		printk(KERN_ERR
		       "kaodv_queue: OOM in kaodv_queue_enqueue_packet()\n");
		return -ENOMEM;
	}

	/* printk("enquing packet queue_len=%d\n", queue_total); */
	entry->okfn = okfn;
	entry->skb = skb;
	entry->rt_info.tos = iph->tos;
	entry->rt_info.daddr = iph->daddr;
	entry->rt_info.saddr = iph->saddr;

	write_lock_bh(&queue_lock);

	status = __kaodv_queue_enqueue_entry(entry);

	if (status < 0)
		goto err_out_unlock;

	write_unlock_bh(&queue_lock);
	return status;

      err_out_unlock:
	write_unlock_bh(&queue_lock);
	kfree(entry);

	return status;
}

static inline int dest_cmp(struct kaodv_queue_entry *e, unsigned long daddr)
{
	return (daddr == e->rt_info.daddr);
}

int kaodv_queue_find(__u32 daddr)
{
	struct kaodv_queue_entry *entry;
	int res = 0;

	read_lock_bh(&queue_lock);
	entry = __kaodv_queue_find_entry(dest_cmp, daddr);
	if (entry != NULL)
		res = 1;

	read_unlock_bh(&queue_lock);
	return res;
}

int kaodv_queue_set_verdict(int verdict, __u32 daddr)
{
	struct kaodv_queue_entry *entry;
	int pkts = 0;

	if (verdict == KAODV_QUEUE_DROP) {

		while (1) {
			entry = kaodv_queue_find_dequeue_entry(dest_cmp, daddr);

			if (entry == NULL)
				return pkts;

			/* Send an ICMP message informing the application that the
			 * destination was unreachable. */
			if (pkts == 0)
				icmp_send(entry->skb, ICMP_DEST_UNREACH,
					  ICMP_HOST_UNREACH, 0);

			kfree_skb(entry->skb);
			kfree(entry);
			pkts++;
		}
	} else if (verdict == KAODV_QUEUE_SEND) {
		struct expl_entry e;

		while (1) {
			entry = kaodv_queue_find_dequeue_entry(dest_cmp, daddr);

			if (entry == NULL)
				return pkts;

			if (!kaodv_expl_get(daddr, &e)) {
				kfree_skb(entry->skb);
				goto next;
			}
			if (e.flags & KAODV_RT_GW_ENCAP) {

				entry->skb = ip_pkt_encapsulate(entry->skb, e.nhop);
				if (!entry->skb)
					goto next;
			}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
			ip_route_me_harder(&entry->skb);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
			ip_route_me_harder(&entry->skb, RTN_LOCAL);
#else
			ip_route_me_harder(entry->skb, RTN_LOCAL);
#endif
			pkts++;

			/* Inject packet */
			entry->okfn(entry->skb);
		next:
			kfree(entry);
		}
	}
	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
static int kaodv_queue_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	read_lock_bh(&queue_lock);

	len = sprintf(buffer,
		      "Queue length      : %u\n"
		      "Queue max. length : %u\n", queue_total, queue_maxlen);

	read_unlock_bh(&queue_lock);

	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}
#else
static int kaodv_queue_get_info(char *page, char **start, off_t off, int count,
                    int *eof, void *data)
{
	int len;

	read_lock_bh(&queue_lock);

	len = sprintf(page,
		      "Queue length      : %u\n"
		      "Queue max. length : %u\n", queue_total, queue_maxlen);

	read_unlock_bh(&queue_lock);

	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	else if (len < 0)
		len = 0;
	return len;
}
#endif

static int init_or_cleanup(int init)
{
	int status = -ENOMEM;
	struct proc_dir_entry *proc;

	if (!init)
		goto cleanup;

	queue_total = 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	proc = proc_net_create(KAODV_QUEUE_PROC_FS_NAME, 0, kaodv_queue_get_info);
#else
	proc = create_proc_read_entry(KAODV_QUEUE_PROC_FS_NAME, 0, init_net.proc_net, kaodv_queue_get_info, NULL);
#endif
	if (!proc) {
	  printk(KERN_ERR "kaodv_queue: failed to create proc entry\n");
	  return -1;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30))
	proc->owner = THIS_MODULE;
#endif

	return 1;
	
 cleanup:
#ifdef KERNEL26
	synchronize_net();
#endif
	kaodv_queue_flush();

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	proc_net_remove(KAODV_QUEUE_PROC_FS_NAME);
#else
	proc_net_remove(&init_net, KAODV_QUEUE_PROC_FS_NAME);
#endif
	return status;
}

int kaodv_queue_init(void)
{

	return init_or_cleanup(1);
}

void kaodv_queue_fini(void)
{
	init_or_cleanup(0);
}
