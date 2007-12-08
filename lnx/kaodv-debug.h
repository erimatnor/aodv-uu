/*
** kaodv-debug.h
** 
** Made by Erik Nordström
** Login   <erikn@replicator.mine.nu>
** 
** Started on  Sat Dec  3 15:13:48 2005 Erik Nordström
** Last update Sat Dec  3 15:13:48 2005 Erik Nordström
*/

#ifndef   	_KAODV_DEBUG_H
#define   	_KAODV_DEBUG_H

#include <linux/in.h>

#ifdef DEBUG
#undef DEBUG
#define DEBUG(fmt, ...) trace(fmt, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...)
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
