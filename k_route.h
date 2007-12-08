/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University.
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
 *          Henrik Lundgren, <henrikl@docs.uu.se>
 *
 *****************************************************************************/
#ifndef K_ROUTE_H
#define K_ROUTE_H

#include "defs.h"

int k_del_arp(u_int32_t dst);
int k_add_rte(u_int32_t dst, u_int32_t gtwy, u_int32_t netm,
	      short int hcnt, unsigned int ifindex);
int k_chg_rte(u_int32_t dst, u_int32_t gtwy, u_int32_t netm,
	      short int hcnt, unsigned int ifindex);
int k_del_rte(u_int32_t dst, u_int32_t gtwy, u_int32_t netm);

#endif
