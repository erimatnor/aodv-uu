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
 *
 *****************************************************************************/
#ifndef K_ROUTE_H
#define K_ROUTE_H

#include "defs.h"

int k_del_arp(struct in_addr dst);
int k_add_rte(struct in_addr dst, struct in_addr gtwy, struct in_addr  netm,
	      short int hcnt, unsigned int ifindex);
int k_chg_rte(struct in_addr dst, struct in_addr gtwy, struct in_addr  netm,
	      short int hcnt, unsigned int ifindex);
int k_del_rte(struct in_addr dst);

#endif
