#ifndef _KAODV_MOD_H
#define _KAODV_MOD_H

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/list.h>
#include <linux/spinlock.h>

/* Interface information */
struct if_info {
	struct list_head l;
	struct in_addr if_addr;
	struct in_addr bc_addr;
	struct net_device *dev;
};

static LIST_HEAD(ifihead);
static rwlock_t ifilock = RW_LOCK_UNLOCKED;
/* extern struct list_head ifihead; */
/* extern rwlock_t ifilock; */

static inline int if_info_add(struct net_device *dev)
{
	struct if_info *ifi;
	struct in_device *indev;

	ifi = (struct if_info *)kmalloc(sizeof(struct if_info), GFP_ATOMIC);

	if (!ifi)
		return -1;

	ifi->dev = dev;

	dev_hold(dev);

	indev = in_dev_get(dev);

	if (indev) {
		struct in_ifaddr **ifap;
		struct in_ifaddr *ifa;

		for (ifap = &indev->ifa_list; (ifa = *ifap) != NULL;
		     ifap = &ifa->ifa_next)
			if (!strcmp(dev->name, ifa->ifa_label))
				break;

		if (ifa) {
			ifi->if_addr.s_addr = ifa->ifa_address;
			ifi->bc_addr.s_addr = ifa->ifa_broadcast;
		}
		in_dev_put(indev);
	}

	write_lock(&ifilock);
	list_add(&ifi->l, &ifihead);
	write_unlock(&ifilock);

	return 0;
}

static inline void if_info_purge(void)
{
	struct list_head *pos, *n;

	write_lock(&ifilock);
	list_for_each_safe(pos, n, &ifihead) {
		struct if_info *ifi = (struct if_info *)pos;
		list_del(&ifi->l);
		dev_put(ifi->dev);
		kfree(ifi);
	}
	write_unlock(&ifilock);
}

static inline int if_info_from_ifindex(struct in_addr *ifa, struct in_addr *bc,
				   int ifindex)
{
	struct list_head *pos;
	int res = -1;

	read_lock(&ifilock);
	list_for_each(pos, &ifihead) {
		struct if_info *ifi = (struct if_info *)pos;
		if (ifi->dev->ifindex == ifindex) {
			if (ifa)
				*ifa = ifi->if_addr;

			if (bc)
				*bc = ifi->bc_addr;
			res = 0;
			break;
		}
	}
	read_unlock(&ifilock);

	return res;
}

void kaodv_update_route_timeouts(int hooknum, const struct net_device *dev,
				 struct iphdr *iph);
#endif
