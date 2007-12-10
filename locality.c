/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University & Ericsson AB.
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
 * Authors: Erik Nordström, <erik.nordstrom@it.uu.se>
 *
 *****************************************************************************/
#ifdef NS_PORT
#include "ns-2/aodv-uu.h"
#else
#include <netdb.h>
extern int h_errno;

#include "locality.h"
#include "defs.h"
#include "debug.h"

extern int gw_prefix;
#endif


int NS_CLASS locality(struct in_addr dest, unsigned int ifindex)
{

#ifndef NS_PORT
    if (gw_prefix) {
	if ((dest.s_addr & DEV_IFINDEX(ifindex).netmask.s_addr) ==
	    (DEV_IFINDEX(ifindex).ipaddr.s_addr & DEV_IFINDEX(ifindex).netmask.
	     s_addr))
	    return HOST_ADHOC;
	else
	    return HOST_INET;

    } else {
	struct hostent *hent;

	hent = gethostbyaddr(&dest, sizeof(struct in_addr), AF_INET);

	if (!hent) {
	    switch (h_errno) {
	    case HOST_NOT_FOUND:
		DEBUG(LOG_DEBUG, 0, "RREQ for Non-Internet dest %s",
		      ip_to_str(dest));
		return HOST_UNKNOWN;
	    default:
		DEBUG(LOG_DEBUG, 0, "Unknown DNS error");
		break;

	    }
	} else
	    return HOST_INET;
    }
#else
    char *dstnet = Address::instance().get_subnetaddr(dest.s_addr);
    char *subnet =
	Address::instance().get_subnetaddr(DEV_NR(NS_DEV_NR).ipaddr.s_addr);
    DEBUG(LOG_DEBUG, 0, "myaddr=%d, dest=%d dstnet=%s subnet=%s",
	  DEV_NR(NS_DEV_NR).ipaddr.s_addr, dest.s_addr, dstnet, subnet);
    if (subnet != NULL) {
	if (dstnet != NULL) {
	    if (strcmp(dstnet, subnet) != 0) {
		delete[]dstnet;
		return HOST_INET;
	    }
	    delete[]dstnet;
	}
	delete[]subnet;
    }
    assert(dstnet == NULL);
    return HOST_UNKNOWN;
#endif
    return HOST_UNKNOWN;
}
