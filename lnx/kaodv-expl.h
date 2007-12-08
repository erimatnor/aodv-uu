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
#ifndef _KAODV_EXPL_H
#define _KAODV_EXPL_H

#ifdef __KERNEL__

#include <linux/list.h>

struct expl_entry {
	struct list_head l;
	unsigned long expires;
	unsigned short flags;
	__u32 daddr;
	__u32 nhop;
	int ifindex;
};

void kaodv_expl_init(void);
void kaodv_expl_flush(void);
int kaodv_expl_get(__u32 daddr, struct expl_entry *e_in);
int kaodv_expl_add(__u32 daddr, __u32 nhop, unsigned long time,
		   unsigned short flags, int ifindex);
int kaodv_expl_update(__u32 daddr, __u32 nhop, unsigned long time,
		      unsigned short flags, int ifindex);

int kaodv_expl_del(__u32 daddr);
void kaodv_expl_fini(void);

#endif				/* __KERNEL__ */

#endif				/* _KAODV_EXPL_H */
