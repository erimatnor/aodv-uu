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
 *
 *****************************************************************************/
#include <linux/config.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/version.h>
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

/*  #define DEBUG_OUTPUT */

static struct nf_hook_ops nf_hook1, nf_hook2;

#ifdef DEBUG_OUTPUT
static struct nf_hook_ops nf_hook3;
#endif

#define MAX_INTERFACES 10

unsigned int ifindices[MAX_INTERFACES];
int nif = 0;

unsigned int
netfilter_hook(unsigned int hooknum,
	       struct sk_buff **skb,
	       const struct net_device *in,
	       const struct net_device *out,
	       int (*okfn) (struct sk_buff *))
{
    int i;
    
    if ((*skb)->protocol != htons(ETH_P_IP))
	return NF_ACCEPT;
    
    /* We want AODV control messages to go through directly.... */
    if ((*skb)->nh.iph && (*skb)->nh.iph->protocol == IPPROTO_UDP)
	if ((*skb)->sk) 
	    if ((*skb)->sk->dport == htons(654) ||
		(*skb)->sk->sport == htons(654)) 
		return NF_ACCEPT;
    
    /* Check which hook the packet is on... */
    switch(hooknum) {
    case NF_IP_PRE_ROUTING:
	/* Loop through all AODV enabled interfaces and see if the packet
	 * is bound to any of them. */ 
	for (i = 0; i < nif; i++)
	    if (ifindices[i] == in->ifindex)
		return NF_QUEUE;
	
	break;
    case NF_IP_LOCAL_OUT:
	/* Loop through all AODV enabled interfaces and see if the packet
	 * is bound to any of them. */ 
	for (i = 0; i < nif; i++)
	    if (ifindices[i] == out->ifindex)
		return NF_QUEUE;
	break;
#ifdef DEBUG_OUTPUT
    case NF_IP_POST_ROUTING:
	struct rtable *rt = (struct rtable *) (*skb)->dst;
	
	if ((*skb)->nh.iph)
	    printk("kaodv: IP s=%u.%u.%u.%u d=%u.%u.%u.%u rt=%u.%u.%u.%u\n",
		   NIPQUAD((*skb)->nh.iph->saddr),
		   NIPQUAD((*skb)->nh.iph->daddr), 
		   NIPQUAD(rt->rt_gateway));
#endif
    default:
	return NF_ACCEPT;
	
    }
    return NF_ACCEPT;
}

/*
 * Called when the module is inserted in the kernel.
 */
char *ifname[MAX_INTERFACES] = {"eth0"};
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0))
MODULE_PARM(ifname, "1-" __MODULE_STRING(MAX_INTERFACES) "s");
#endif

int init_module()
{
    struct net_device *dev = NULL;
    int i;

    EXPORT_NO_SYMBOLS;

    nf_hook1.list.next = NULL;
    nf_hook1.list.prev = NULL;
    nf_hook1.hook = netfilter_hook;
    nf_hook1.pf = PF_INET;
    nf_hook1.hooknum = NF_IP_PRE_ROUTING;
    nf_register_hook(&nf_hook1);

    nf_hook2.list.next = NULL;
    nf_hook2.list.prev = NULL;
    nf_hook2.hook = netfilter_hook;
    nf_hook2.pf = PF_INET;
    nf_hook2.hooknum = NF_IP_LOCAL_OUT;
    nf_register_hook(&nf_hook2);

#ifdef DEBUG_OUTPUT
    nf_hook3.list.next = NULL;
    nf_hook3.list.prev = NULL;
    nf_hook3.hook = netfilter_hook;
    nf_hook3.pf = PF_INET;
    nf_hook3.hooknum = NF_IP_POST_ROUTING;
    nf_register_hook(&nf_hook3);
#endif

    for(i = 0; i < MAX_INTERFACES; i++) {
	if(!ifname[i])
	    break;
	dev = dev_get_by_name(ifname[i]);
	if(!dev) { 
	    printk("kaodv: No device %s available, ignoring!\n", ifname[i]);
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
#ifdef DEBUG_OUTPUT
    nf_unregister_hook(&nf_hook3);
#endif
}

MODULE_DESCRIPTION
    ("AODV kernel support. © Uppsala University & Ericsson Telecom AB");
MODULE_AUTHOR("Erik Nordström");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
