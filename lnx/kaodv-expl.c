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
/* Expire list for aodv route information */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>

#include "kaodv-expl.h"
#include "kaodv-netlink.h"
#include "kaodv-queue.h"
#include "kaodv-debug.h"

#define EXPL_MAX_LEN 1024

static unsigned int expl_len;
static rwlock_t expl_lock = RW_LOCK_UNLOCKED;
static LIST_HEAD(expl_head);

#define list_is_first(e) (&e->l == expl_head.next)

/* Timers and timeouts could potentially be handled in the kernel. However,
 * currently they are not, because it complicates things quite a bit. The code
 * for adding timers is still here though... - Erik */

#ifdef EXPL_TIMER
static struct timer_list expl_timer;

static void kaodv_expl_timeout(unsigned long data);

static inline void __kaodv_expl_set_next_timeout(void)
{
	struct expl_entry *ne;

	if (list_empty(&expl_head))
		return;

	/* Get first entry */
	ne = (struct expl_entry *)expl_head.next;

	if (timer_pending(&expl_timer)) {
		mod_timer(&expl_timer, ne->expires);
	} else {
		expl_timer.function = kaodv_expl_timeout;
		expl_timer.expires = ne->expires;
		expl_timer.data = 0;
		add_timer(&expl_timer);
	}
}

static void kaodv_expl_timeout(unsigned long data)
{
	struct list_head *pos, *tmp;
	int time = jiffies;

	write_lock_bh(&expl_lock);

	list_for_each_safe(pos, tmp, &expl_head) {
		struct expl_entry *e = (struct expl_entry *)pos;

		if (e->expires > time)
			break;

		list_del(&e->l);
		expl_len--;

		/* Flush any queued packets for this dest */
		kaodv_queue_set_verdict(KAODV_QUEUE_DROP, e->daddr);

		/* printk("expl_timeout: sending timeout event!\n"); */
		kaodv_netlink_send_rt_msg(KAODVM_TIMEOUT, e->daddr);
	}
	__kaodv_expl_set_next_timeout();
	write_unlock_bh(&expl_lock);
}
#endif				/* EXPL_TIMER */

static inline void __kaodv_expl_flush(void)
{
	struct list_head *pos, *tmp;

	list_for_each_safe(pos, tmp, &expl_head) {
		struct expl_entry *e = (struct expl_entry *)pos;
		list_del(&e->l);
		expl_len--;
		kfree(e);
	}
}

static inline int __kaodv_expl_add(struct expl_entry *e)
{

	if (expl_len >= EXPL_MAX_LEN) {
		printk(KERN_WARNING "kaodv_expl: Max list len reached\n");
		return -ENOSPC;
	}

	if (list_empty(&expl_head)) {
		list_add(&e->l, &expl_head);
	} else {
		struct list_head *pos;

		list_for_each(pos, &expl_head) {
			struct expl_entry *curr = (struct expl_entry *)pos;

			if (curr->expires > e->expires)
				break;
		}
		list_add(&e->l, pos->prev);
	}
	return 1;
}

static inline struct expl_entry *__kaodv_expl_find(__u32 daddr)
{
	struct list_head *pos;

	list_for_each(pos, &expl_head) {
		struct expl_entry *e = (struct expl_entry *)pos;

		if (e->daddr == daddr)
			return e;
	}
	return NULL;
}

static inline int __kaodv_expl_del(struct expl_entry *e)
{
	if (e == NULL)
		return 0;

	if (list_is_first(e)) {

		list_del(&e->l);
#ifdef EXPL_TIMER
		if (!list_empty(&expl_head)) {
			/* Get the first entry */
			struct expl_entry *f =
			    (struct expl_entry *)expl_head.next;

			/* Update the timer */
			mod_timer(&expl_timer, f->expires);
		}
#endif
	} else
		list_del(&e->l);

	expl_len--;

	return 1;
}

int kaodv_expl_del(__u32 daddr)
{
	int res;
	struct expl_entry *e;

	write_lock_bh(&expl_lock);

	e = __kaodv_expl_find(daddr);

	if (e == NULL) {
		res = 0;
		goto unlock;
	}
	
	res = __kaodv_expl_del(e);

	if (res) {
		kfree(e);
	}
      unlock:
	write_unlock_bh(&expl_lock);

	return res;
}

int kaodv_expl_get(__u32 daddr, struct expl_entry *e_in)
{
	struct expl_entry *e;
	int res = 0;

/*     printk("Checking activeness\n"); */
	read_lock_bh(&expl_lock);
	e = __kaodv_expl_find(daddr);

	if (e) {
		res = 1;
		if (e_in)
			memcpy(e_in, e, sizeof(struct expl_entry));
	}

	read_unlock_bh(&expl_lock);
	return res;
}

int kaodv_expl_add(__u32 daddr, __u32 nhop, unsigned long time,
		   unsigned short flags, int ifindex)
{
	struct expl_entry *e;
	int status = 0;

	if (kaodv_expl_get(daddr, NULL))
		return 0;

	e = kmalloc(sizeof(struct expl_entry), GFP_ATOMIC);

	if (e == NULL) {
		printk(KERN_ERR "expl: OOM in expl_add\n");
		return -ENOMEM;
	}

	e->daddr = daddr;
	e->nhop = nhop;
	e->flags = flags;
	e->ifindex = ifindex;
	e->expires = jiffies + (time * HZ) / 1000;

	write_lock_bh(&expl_lock);

	status = __kaodv_expl_add(e);

	if (status)
		expl_len++;

#ifdef EXPL_TIMER
	/* If the added element was added first in the list we update the timer */
	if (status && list_is_first(e)) {

		if (timer_pending(&expl_timer))
			mod_timer(&expl_timer, e->expires);
		else {
			expl_timer.function = expl_timeout;
			expl_timer.expires = e->expires;
			expl_timer.data = 0;
			add_timer(&expl_timer);
		}
	}
#endif
	write_unlock_bh(&expl_lock);

	if (status < 0)
		kfree(e);

	return status;
}

static int kaodv_expl_print(char *buf)
{
	struct list_head *pos;
	int len = 0;

	read_lock_bh(&expl_lock);

	len += sprintf(buf, "# Total entries: %u\n", expl_len);
	len += sprintf(buf + len, "# %-15s %-15s %-5s %-5s Expires\n", 
		       "Addr", "Nhop", "Flags", "Iface");

	list_for_each(pos, &expl_head) {
		char addr[16], nhop[16], flags[4];
		struct net_device *dev;
		int num_flags = 0;
		struct expl_entry *e = (struct expl_entry *)pos;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		dev = dev_get_by_index(e->ifindex);
#else
		dev = dev_get_by_index(&init_net, e->ifindex);
#endif

		if (!dev)
			continue;

		sprintf(addr, "%d.%d.%d.%d",
			0x0ff & e->daddr,
			0x0ff & (e->daddr >> 8),
			0x0ff & (e->daddr >> 16), 0x0ff & (e->daddr >> 24));

		sprintf(nhop, "%d.%d.%d.%d",
			0x0ff & e->nhop,
			0x0ff & (e->nhop >> 8),
			0x0ff & (e->nhop >> 16), 0x0ff & (e->nhop >> 24));

		if (e->flags & KAODV_RT_GW_ENCAP)
			flags[num_flags++] = 'E';

		if (e->flags & KAODV_RT_REPAIR)
			flags[num_flags++] = 'R';

		flags[num_flags] = '\0';

		len += sprintf(buf + len, "  %-15s %-15s %-5s %-5s %lu\n",
			       addr, nhop, flags, dev->name,
			       (e->expires - jiffies) * 1000 / HZ);

		dev_put(dev);
	}

	read_unlock_bh(&expl_lock);
	return len;
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
static int
kaodv_expl_proc_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	len = kaodv_expl_print(buffer);

	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}
#else
static int kaodv_expl_proc_info(char *page, char **start, off_t off, int count,
                    int *eof, void *data)
{
	int len;

	len = kaodv_expl_print(page);

	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	else if (len < 0)
		len = 0;
	return len;
}
#endif

int kaodv_expl_update(__u32 daddr, __u32 nhop, unsigned long time,
		      unsigned short flags, int ifindex)
{
	int ret = 0;
	struct expl_entry *e;

	write_lock_bh(&expl_lock);

	e = __kaodv_expl_find(daddr);

	if (e == NULL) {
		/* printk("expl_update: No entry to update!\n"); */
		ret = -1;
		goto unlock;
	}
	e->nhop = nhop;
	e->flags = flags;
	e->ifindex = ifindex;
	/* Update expire time */
	e->expires = jiffies + (time * HZ) / 1000;

	/* Remove from list */
	list_del(&e->l);

	__kaodv_expl_add(e);
#ifdef EXPL_TIMER
	__kaodv_expl_set_next_timeout();
#endif

      unlock:
	write_unlock_bh(&expl_lock);

	return ret;
}

void kaodv_expl_flush(void)
{
#ifdef EXPL_TIMER
	if (timer_pending(&expl_timer))
		del_timer(&expl_timer);
#endif

	write_lock_bh(&expl_lock);

	__kaodv_expl_flush();

	write_unlock_bh(&expl_lock);
}

void kaodv_expl_init(void)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	proc_net_create("kaodv_expl", 0, kaodv_expl_proc_info);
#else
    create_proc_read_entry("kaodv_expl", 0, init_net.proc_net, kaodv_expl_proc_info, NULL);
#endif

	expl_len = 0;
#ifdef EXPL_TIMER
	init_timer(&expl_timer);
#endif
}

void kaodv_expl_fini(void)
{
	kaodv_expl_flush();
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	proc_net_remove("kaodv_expl");
#else
	proc_net_remove(&init_net, "kaodv_expl");
#endif
}
