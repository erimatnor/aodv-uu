/*
** kaodv-debug.c
** 
** Made by (Erik Nordström)
** Login   <erikn@replicator.mine.nu>
** 
** Started on  Sat Dec  3 15:13:39 2005 Erik Nordström
** Last update Sun May 12 01:17:25 2002 Speed Blue
*/

#include <asm/uaccess.h>
#include <asm/io.h>

#include "kaodv-debug.h"
#include "kaodv-netlink.h"

int trace(const char *fmt, ...)
{
	char buf[1024];
	va_list args;
	int len;

	va_start(args, fmt);

	len = vsnprintf(buf, 1024, fmt, args);

	va_end(args);

	/* Send the message off to user space... */
	kaodv_netlink_send_debug_msg(buf, len + 1);

	return 0;
}
