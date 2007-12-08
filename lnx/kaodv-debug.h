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
#ifndef _KAODV_DEBUG_H
#define _KAODV_DEBUG_H

#include <linux/in.h>

#ifdef DEBUG
//#undef DEBUG
#define KAODV_DEBUG(fmt, ...) trace(fmt, ##__VA_ARGS__)
#else
#define KAODV_DEBUG(fmt, ...)
#endif


static inline char *print_ip(__u32 addr)
{
	static char buf[16 * 4];
	static int index = 0;
	char *str;

	sprintf(&buf[index], "%d.%d.%d.%d",
		0x0ff & addr,
		0x0ff & (addr >> 8),
		0x0ff & (addr >> 16), 0x0ff & (addr >> 24));

	str = &buf[index];
	index += 16;
	index %= 64;

	return str;
}

static inline char *print_eth(char *addr)
{
	static char buf[30];

	sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
		(unsigned char)addr[0], (unsigned char)addr[1],
		(unsigned char)addr[2], (unsigned char)addr[3],
		(unsigned char)addr[4], (unsigned char)addr[5]);

	return buf;
}

int trace(const char *fmt, ...);

#endif				/* !KAODV-DEBUG_H_ */
