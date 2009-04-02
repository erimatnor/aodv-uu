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

#ifdef CONFIG_MODVERSIONS
#define MODVERSIONS
#include <linux/modversions.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,18))
#define USE_OLD_ROUTE_ME_HARDER
#endif
#else
#define USE_OLD_ROUTE_ME_HARDER
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/wrapper.h>
#include <linux/string.h>
#include <linux/if_ether.h>
#include <linux/kmod.h>
#include <linux/ctype.h>

#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/ip.h>
#include <net/dst.h>
#include <net/route.h>
#include <linux/udp.h>
#include <net/neighbour.h>

static struct nf_hook_ops nf_hook1, nf_hook2, nf_hook3;

#define AODV_PORT 654
#define MAX_INTERFACES 10

unsigned int ifindices[MAX_INTERFACES];
int nif = 0;

/* This function is taken from the kernel ip_queue.c source file. It
 * seem to have been moved to net/core/netfilter.c in later kernel
 * versions (verified for 2.4.18). There it is called
 * ip_route_me_harder(). Old version is kept here for compatibility.
 */
#ifdef USE_OLD_ROUTE_ME_HARDER
/* With a chainsaw... */
static int route_me_harder(struct sk_buff *skb)
{
    struct iphdr *iph = skb->nh.iph;
    struct rtable *rt;

    struct rt_key key = {
	dst:iph->daddr, src:iph->saddr,
	oif:skb->sk ? skb->sk->bound_dev_if : 0,
	tos:RT_TOS(iph->tos) | RTO_CONN,
#ifdef CONFIG_IP_ROUTE_FWMARK
	fwmark:skb->nfmark
#endif
    };

    if (ip_route_output_key(&rt, &key) != 0)
	return -EINVAL;

    /* Drop old route. */
    dst_release(skb->dst);
    skb->dst = &rt->u.dst;
    return 0;
}
#endif


unsigned int nf_aodv_hook(unsigned int hooknum,
			  struct sk_buff **skb,
			  const struct net_device *in,
			  const struct net_device *out,
			  int (*okfn) (struct sk_buff *))
{
    int i;
    struct udphdr *udph;

    /* We are only interested in IP packets */
    if ((*skb)->nh.iph == NULL)
	goto accept;

    /* We want AODV control messages to go through directly to the
     * AODV socket.... */
    if ((*skb)->nh.iph && (*skb)->nh.iph->protocol == IPPROTO_UDP) {
	
	udph = (struct udphdr *) ((char *) (*skb)->nh.iph + ((*skb)->nh.iph->ihl << 2));
	if (ntohs(udph->dest) == AODV_PORT || 
	    ntohs(udph->source) == AODV_PORT) {
	    goto accept;
	}
    }
    /* Check which hook the packet is on... */
    switch (hooknum) {
    case NF_IP_PRE_ROUTING:
	/* Loop through all AODV enabled interfaces and see if the packet
	 * is bound to any of them. */
	for (i = 0; i < nif; i++)
	    if (ifindices[i] == in->ifindex) {
		(*skb)->nfmark = 3;
		goto queue;
	    }
	break;
    case NF_IP_LOCAL_OUT:
	for (i = 0; i < nif; i++)
	    if (ifindices[i] == out->ifindex) {
		(*skb)->nfmark = 4;
		goto queue;
	    }
	break;
    case NF_IP_POST_ROUTING:
	/* Re-route all packets before sending on interface. This will
	   make sure queued packets are routed on a newly installed
	   route (after a successful RREQ-cycle).  FIXME: Make sure
	   only "buffered" packets are re-routed. But how? */
	if ((*skb)->nfmark == 3 || (*skb)->nfmark == 4) {
#ifdef USE_OLD_ROUTE_ME_HARDER
	    route_me_harder((*skb));
#else
	    ip_route_me_harder(skb);
#endif
	}
	return NF_ACCEPT;
    }

  accept:
    (*skb)->nfmark = 2;
    return NF_ACCEPT;

  queue:
    return NF_QUEUE;
}

/*
 * Called when the module is inserted in the kernel.
 */
char *ifname[MAX_INTERFACES] = { "eth0" };
MODULE_PARM(ifname, "1-" __MODULE_STRING(MAX_INTERFACES) "s");

int init_module()
{
    struct net_device *dev = NULL;
    int i;

    EXPORT_NO_SYMBOLS;

    nf_hook1.list.next = NULL;
    nf_hook1.list.prev = NULL;
    nf_hook1.hook = nf_aodv_hook;
    nf_hook1.pf = PF_INET;
    nf_hook1.hooknum = NF_IP_PRE_ROUTING;
    nf_register_hook(&nf_hook1);

    nf_hook2.list.next = NULL;
    nf_hook2.list.prev = NULL;
    nf_hook2.hook = nf_aodv_hook;
    nf_hook2.pf = PF_INET;
    nf_hook2.hooknum = NF_IP_LOCAL_OUT;
    nf_register_hook(&nf_hook2);

    nf_hook3.list.next = NULL;
    nf_hook3.list.prev = NULL;
    nf_hook3.hook = nf_aodv_hook;
    nf_hook3.pf = PF_INET;
    nf_hook3.hooknum = NF_IP_POST_ROUTING;
    nf_register_hook(&nf_hook3);

    for (i = 0; i < MAX_INTERFACES; i++) {
	if (!ifname[i])
	    break;
	dev = dev_get_by_name(ifname[i]);
	if (!dev) {
	    printk("No device %s available, ignoring!\n", ifname[i]);
	    dev_put(dev);
	    continue;
	}
	ifindices[nif++] = dev->ifindex;
	dev_put(dev);
    }
    return 0;
}

/*
 * Called when removing the module from memory... 
 */
void cleanup_module()
{
    nf_unregister_hook(&nf_hook1);
    nf_unregister_hook(&nf_hook2);
    nf_unregister_hook(&nf_hook3);
}

MODULE_DESCRIPTION("AODV kernel support. © Uppsala University & Ericsson AB");
MODULE_AUTHOR("Erik Nordström");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
