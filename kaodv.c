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
 
 *****************************************************************************/

#include <linux/config.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,18))
#define USE_OLD_ROUTE_ME_HARDER
#endif

#ifdef CONFIG_MODVERSIONS
#define MODVERSIONS
#ifdef KERNEL26
#include <config/modversions.h>
#else
#include <linux/modversions.h>
#endif
#endif

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
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>

#define AODV_PORT 654
#define MAX_INTERFACES 10

static unsigned int ifidx[MAX_INTERFACES];
static int nif = 0;
static int qual = 0;
static int qual_th = 0;
static unsigned long pkts_dropped = 0;
//static unsigned int loindex = 0;

MODULE_DESCRIPTION("AODV-UU kernel support. © Uppsala University & Ericsson AB");
MODULE_AUTHOR("Erik Nordström");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
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

static unsigned int kaodv_hook(unsigned int hooknum,
			       struct sk_buff **skb,
			       const struct net_device *in,
			       const struct net_device *out,
			       int (*okfn) (struct sk_buff *))
{
    int i;

    /* We are only interested in IP packets */
    if ((*skb)->nh.iph == NULL)
	goto accept;

    /* We want AODV control messages to go through directly to the
     * AODV socket.... */
    if ((*skb)->nh.iph && (*skb)->nh.iph->protocol == IPPROTO_UDP) {
	struct udphdr *udph;
	
	udph = (struct udphdr *) ((char *) (*skb)->nh.iph + ((*skb)->nh.iph->ihl << 2));

	if (ntohs(udph->dest) == AODV_PORT ||
	    ntohs(udph->source) == AODV_PORT) {

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
	    /* (*skb)->__unused does not exist anymore in kernel 2.6. We
	     * probably need to patch the skb for those kernels. */
	    qual = (int)(*skb)->__unused;
	
	    if (qual_th && hooknum == NF_IP_PRE_ROUTING) {
		if (qual && qual < qual_th) {
		    pkts_dropped++;
		    return NF_DROP;
		}
	    }
#endif
	    goto accept;
	}
    }
    /* Check which hook the packet is on... */
    switch (hooknum) {
    case NF_IP_PRE_ROUTING:
	/* Loop through all AODV enabled interfaces and see if the packet
	 * is bound to any of them. */
	for (i = 0; i < nif; i++)
	    if (ifidx[i] == in->ifindex) {
		(*skb)->nfmark = 3;
		goto queue;
	    }
	break;
    case NF_IP_LOCAL_OUT:
	
	for (i = 0; i < nif; i++)
	    if (ifidx[i] == out->ifindex) {
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

int kaodv_proc_info(char *buf, char **start, off_t offset, int len)
{
    int written;

    written = sprintf(buf, "# Qual threshold = %d, pkts dropped = %lu, last qual = %d\n", qual_th, pkts_dropped, qual);
    
    return (written < 0 ? 0 : written); 
}

/*
 * Called when the module is inserted in the kernel.
 */
static char *ifname[MAX_INTERFACES] = { "eth0" };

#ifdef KERNEL26
static int num_parms = 0;
module_param_array(ifname, charp, num_parms, 0);
module_param(qual_th, int, 0);
#else
MODULE_PARM(ifname, "1-" __MODULE_STRING(MAX_INTERFACES) "s");
MODULE_PARM(qual_th, "i");
#endif

static struct nf_hook_ops kaodv_ops[] = {
	{
		.hook		= kaodv_hook,
#ifdef KERNEL26
		.owner		= THIS_MODULE,
#endif
		.pf		= PF_INET,
		.hooknum	= NF_IP_PRE_ROUTING,
		.priority	= NF_IP_PRI_FIRST,
	},
	{
		.hook		= kaodv_hook,
#ifdef KERNEL26
		.owner		= THIS_MODULE,
#endif
		.pf		= PF_INET,
		.hooknum	= NF_IP_LOCAL_OUT,
		.priority	= NF_IP_PRI_FILTER,
	},
	{
		.hook		= kaodv_hook,
#ifdef KERNEL26
		.owner		= THIS_MODULE,
#endif
		.pf		= PF_INET,
		.hooknum	= NF_IP_POST_ROUTING,
		.priority	= NF_IP_PRI_FILTER,
	},
};

static int __init kaodv_init(void)
{
    struct net_device *dev = NULL;
    int i, ret;
    
#ifndef KERNEL26
    EXPORT_NO_SYMBOLS;
#endif

    ret = nf_register_hook(&kaodv_ops[0]);
    
    if (ret < 0)
	return ret;
    
    ret = nf_register_hook(&kaodv_ops[1]);
    
    if (ret < 0)
	goto cleanup_hook0;
    
    ret = nf_register_hook(&kaodv_ops[2]);
    
    if (ret < 0)
	goto cleanup_hook1;
    
    
    for (i = 0; i < MAX_INTERFACES; i++) {
	if (!ifname[i])
	    break;
	dev = dev_get_by_name(ifname[i]);
	if (!dev) {
	    printk("No device %s available, ignoring!\n", ifname[i]);
	    continue;
	}
	ifidx[nif++] = dev->ifindex;
	dev_put(dev);
    }
    
    proc_net_create("kaodv", 0, kaodv_proc_info);
    
    return ret;
    
 cleanup_hook1:
    nf_unregister_hook(&kaodv_ops[1]);
 cleanup_hook0:
    nf_unregister_hook(&kaodv_ops[0]);
    
    return ret;
}

/*
 * Called when removing the module from memory... 
 */
static void __exit kaodv_exit(void)
{
    unsigned int i;
    
    for (i = 0; i < sizeof(kaodv_ops)/sizeof(struct nf_hook_ops); i++)
	nf_unregister_hook(&kaodv_ops[i]);

    proc_net_remove("kaodv");
}

module_init(kaodv_init);
module_exit(kaodv_exit);
