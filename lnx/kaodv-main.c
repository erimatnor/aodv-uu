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
#include <linux/config.h>
#include <linux/version.h>

#ifdef KERNEL26
#include <linux/moduleparam.h>
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/if.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/netfilter_ipv4.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/route.h>

#include "kaodv-expl.h"
#include "kaodv-netlink.h"
#include "kaodv-queue.h"
#include "kaodv-ipenc.h"

#define AODV_PORT 654

#define MAX_INTERFACES 10
#define ACTIVE_ROUTE_TIMEOUT active_route_timeout

struct netdev_info {
	__u32 ip_addr;
	__u32 bc_addr;
	int ifindex;
};

static struct netdev_info netdevs[MAX_INTERFACES];
static int nif = 0;
static int qual = 0;
static unsigned long pkts_dropped = 0;
int qual_th = 0;
int is_gateway = 1;
int active_route_timeout = 3000;
//static unsigned int loindex = 0;

MODULE_DESCRIPTION
    ("AODV-UU kernel support. © Uppsala University & Ericsson AB");
MODULE_AUTHOR("Erik Nordström");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

#define ADDR_HOST 1
#define ADDR_BROADCAST 2

static inline struct netdev_info *netdev_info_from_ifindex(int ifindex)
{
	int i;

	for (i = 0; i < nif; i++)
		if (netdevs[i].ifindex == ifindex)
			return &netdevs[i];
	return NULL;
}

static void
kaodv_update_route_timeouts(int hooknum, const struct net_device *dev,
			    struct iphdr *iph)
{
	struct expl_entry e;
	struct netdev_info *netdi;

	netdi = netdev_info_from_ifindex(dev->ifindex);

	if (!netdi)
		return;

	if (hooknum == NF_IP_PRE_ROUTING)
		kaodv_netlink_send_rt_update_msg(PKT_INBOUND, iph->saddr,
						 iph->daddr, dev->ifindex);
	else if (iph->daddr != INADDR_BROADCAST && iph->daddr != netdi->bc_addr)
		kaodv_netlink_send_rt_update_msg(PKT_OUTBOUND, iph->saddr,
						 iph->daddr, dev->ifindex);

	/* First update forward route and next hop */
	if (kaodv_expl_get(iph->daddr, &e)) {

		kaodv_expl_update(e.daddr, e.nhop, ACTIVE_ROUTE_TIMEOUT,
				  e.flags);

		if (e.nhop != e.daddr && kaodv_expl_get(e.nhop, &e))
			kaodv_expl_update(e.daddr, e.nhop, ACTIVE_ROUTE_TIMEOUT,
					  e.flags);
	}
	/* Update reverse route */
	if (kaodv_expl_get(iph->saddr, &e)) {

		kaodv_expl_update(e.daddr, e.nhop, ACTIVE_ROUTE_TIMEOUT,
				  e.flags);

		if (e.nhop != e.daddr && kaodv_expl_get(e.nhop, &e))
			kaodv_expl_update(e.daddr, e.nhop, ACTIVE_ROUTE_TIMEOUT,
					  e.flags);

	}
}

static unsigned int
kaodv_hook(unsigned int hooknum,
	   struct sk_buff **skb,
	   const struct net_device *in,
	   const struct net_device *out, int (*okfn) (struct sk_buff *))
{
	struct iphdr *iph = (*skb)->nh.iph;
	struct expl_entry e;
	struct netdev_info *netdi;

	/* We are only interested in IP packets */
	if (iph == NULL)
		return NF_ACCEPT;

	/* We want AODV control messages to go through directly to the
	 * AODV socket.... */
	if (iph && iph->protocol == IPPROTO_UDP) {
		struct udphdr *udph;

		udph = (struct udphdr *)((char *)iph + (iph->ihl << 2));

		if (ntohs(udph->dest) == AODV_PORT ||
		    ntohs(udph->source) == AODV_PORT) {

#ifdef CONFIG_QUAL_THRESHOLD
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
			qual = (int)(*skb)->__unused;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
			qual = (*skb)->iwq.qual;
#endif
			if (qual_th && hooknum == NF_IP_PRE_ROUTING) {

				if (qual && qual < qual_th) {
					pkts_dropped++;
					return NF_DROP;
				}
			}
#endif				/* CONFIG_QUAL_THRESHOLD */
			if (hooknum == NF_IP_PRE_ROUTING && in)
				kaodv_update_route_timeouts(hooknum, in, iph);

			return NF_ACCEPT;
		}
	}

	if (hooknum == NF_IP_PRE_ROUTING)
		netdi = netdev_info_from_ifindex(in->ifindex);
	else
		netdi = netdev_info_from_ifindex(out->ifindex);

	if (!netdi)
		return NF_ACCEPT;

	/* Ignore broadcast and multicast packets */
	if (iph->daddr == INADDR_BROADCAST ||
	    IN_MULTICAST(ntohl(iph->daddr)) || iph->daddr == netdi->bc_addr)
		return NF_ACCEPT;

	/* Check which hook the packet is on... */
	switch (hooknum) {
	case NF_IP_PRE_ROUTING:
		/* Loop through all AODV enabled interfaces and see if the packet
		 * is bound to any of them. */

		kaodv_update_route_timeouts(hooknum, in, iph);

		/* If we are a gateway maybe we need to decapsulate? */
		if (is_gateway && iph->protocol == IPPROTO_MIPE &&
		    iph->daddr == netdi->ip_addr) {
			ip_pkt_decapsulate(*skb);
			iph = (*skb)->nh.iph;
		}
		/* Ignore packets generated locally or that are for this
		 * node. */
		else if (iph->saddr == netdi->ip_addr ||
			 iph->daddr == netdi->ip_addr)
			return NF_ACCEPT;

		/* Check for unsolicited data packets */
		else if (!kaodv_expl_get(iph->daddr, &e)) {
			kaodv_netlink_send_rerr_msg(PKT_INBOUND, iph->saddr,
						    iph->daddr, in->ifindex);
			return NF_DROP;

		}
		/* Check if we should repair the route */
		else if (e.flags & KAODV_RT_REPAIR) {

			kaodv_netlink_send_rt_msg(KAODVM_REPAIR, iph->saddr,
						  iph->daddr);
			kaodv_queue_enqueue_packet(*skb, okfn);
			return NF_STOLEN;
		}
		break;
	case NF_IP_LOCAL_OUT:

		if (!kaodv_expl_get(iph->daddr, &e) ||
		    (e.flags & KAODV_RT_REPAIR)) {

			if (!kaodv_queue_find(iph->daddr))
				kaodv_netlink_send_rt_msg(KAODVM_NOROUTE,
							  iph->saddr,
							  iph->daddr);

			kaodv_queue_enqueue_packet(*skb, okfn);
			return NF_STOLEN;

		} else if (e.flags & KAODV_RT_GW_ENCAP) {

			/* Make sure that also the virtual Internet dest entry is
			 * refreshed */
			kaodv_update_route_timeouts(hooknum, out, iph);

			if (!ip_pkt_encapsulate(*skb, e.nhop))
				return NF_STOLEN;

			ip_route_me_harder(skb);
		}
		break;
	case NF_IP_POST_ROUTING:
		kaodv_update_route_timeouts(hooknum, out, iph);
	}
	return NF_ACCEPT;
}

int kaodv_proc_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	len =
	    sprintf(buffer,
		    "qual threshold=%d\npkts dropped=%lu\nlast qual=%d\ngateway_mode=%d\n",
		    qual_th, pkts_dropped, qual, is_gateway);

	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}

/*
 * Called when the module is inserted in the kernel.
 */
static char *ifname[MAX_INTERFACES] = { "eth0" };

#ifdef KERNEL26
static int num_parms = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10))
module_param_array(ifname, charp, num_parms, 0444);
#else
module_param_array(ifname, charp, &num_parms, 0444);
#endif
module_param(qual_th, int, 0);
#else
MODULE_PARM(ifname, "1-" __MODULE_STRING(MAX_INTERFACES) "s");
MODULE_PARM(qual_th, "i");
#endif

static struct nf_hook_ops kaodv_ops[] = {
	{
	 .hook = kaodv_hook,
#ifdef KERNEL26
	 .owner = THIS_MODULE,
#endif
	 .pf = PF_INET,
	 .hooknum = NF_IP_PRE_ROUTING,
	 .priority = NF_IP_PRI_FIRST,
	 },
	{
	 .hook = kaodv_hook,
#ifdef KERNEL26
	 .owner = THIS_MODULE,
#endif
	 .pf = PF_INET,
	 .hooknum = NF_IP_LOCAL_OUT,
	 .priority = NF_IP_PRI_FILTER,
	 },
	{
	 .hook = kaodv_hook,
#ifdef KERNEL26
	 .owner = THIS_MODULE,
#endif
	 .pf = PF_INET,
	 .hooknum = NF_IP_POST_ROUTING,
	 .priority = NF_IP_PRI_FILTER,
	 },
};

static int __init kaodv_init(void)
{
	struct net_device *dev = NULL;
	struct in_device *indev;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;

	int i, ret = -ENOMEM;

#ifndef KERNEL26
	EXPORT_NO_SYMBOLS;
#endif

	kaodv_expl_init();

	ret = kaodv_queue_init();

	if (ret < 0)
		return ret;

	ret = kaodv_netlink_init();

	if (ret < 0)
		goto cleanup_queue;

	ret = nf_register_hook(&kaodv_ops[0]);

	if (ret < 0)
		goto cleanup_netlink;

	ret = nf_register_hook(&kaodv_ops[1]);

	if (ret < 0)
		goto cleanup_hook0;

	ret = nf_register_hook(&kaodv_ops[2]);

	if (ret < 0)
		goto cleanup_hook1;

	/* Prefetch network device info (ip, broadcast address, ifindex). */
	for (i = 0; i < MAX_INTERFACES; i++) {
		if (!ifname[i])
			break;
		dev = dev_get_by_name(ifname[i]);
		if (!dev) {
			printk("No device %s available, ignoring!\n",
			       ifname[i]);
			continue;
		}
		netdevs[nif].ifindex = dev->ifindex;

//      indev = inetdev_by_index(dev->ifindex);
		indev = in_dev_get(dev);

		if (indev) {
			for (ifap = &indev->ifa_list; (ifa = *ifap) != NULL;
			     ifap = &ifa->ifa_next)
				if (!strcmp(dev->name, ifa->ifa_label))
					break;

			if (ifa) {
				netdevs[nif].ip_addr = ifa->ifa_address;
				netdevs[nif].bc_addr = ifa->ifa_broadcast;

				//printk("dev ip=%s bc=%s\n", print_ip(netdevs[nif].ip_addr), print_ip(netdevs[nif].bc_addr));

			}
			in_dev_put(indev);
		}
		nif++;
		dev_put(dev);
	}

	proc_net_create("kaodv", 0, kaodv_proc_info);

	return ret;

      cleanup_hook1:
	nf_unregister_hook(&kaodv_ops[1]);
      cleanup_hook0:
	nf_unregister_hook(&kaodv_ops[0]);
      cleanup_netlink:
	kaodv_netlink_fini();
      cleanup_queue:
	kaodv_queue_fini();
	return ret;
}

/*
 * Called when removing the module from memory... 
 */
static void __exit kaodv_exit(void)
{
	unsigned int i;

	for (i = 0; i < sizeof(kaodv_ops) / sizeof(struct nf_hook_ops); i++)
		nf_unregister_hook(&kaodv_ops[i]);

	proc_net_remove("kaodv");

	kaodv_queue_fini();
	kaodv_expl_fini();
	kaodv_netlink_fini();
}

module_init(kaodv_init);
module_exit(kaodv_exit);
