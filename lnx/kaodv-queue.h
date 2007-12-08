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
#ifndef _KAODV_QUEUE_H
#define _KAODV_QUEUE_H

#define KAODV_QUEUE_DROP 1
#define KAODV_QUEUE_SEND 2

int kaodv_queue_find(__u32 daddr);
int kaodv_queue_enqueue_packet(struct sk_buff *skb,
			       int (*okfn) (struct sk_buff *));
int kaodv_queue_set_verdict(int verdict, __u32 daddr);
void kaodv_queue_flush(void);
int kaodv_queue_init(void);
void kaodv_queue_fini(void);

#endif
