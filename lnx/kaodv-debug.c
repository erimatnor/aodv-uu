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
#include <asm/uaccess.h>
#include <asm/io.h>

#include "kaodv-debug.h"
#include "kaodv-netlink.h"

int trace(const char *fmt, ...)
{
	char buf[512];
	va_list args;
	int len;

	va_start(args, fmt);

	len = vsnprintf(buf, 512, fmt, args);

	va_end(args);
	
	/* Send the message off to user space... */
	kaodv_netlink_send_debug_msg(buf, len + 1);

	return 0;
}
