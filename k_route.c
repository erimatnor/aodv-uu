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
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <net/if_arp.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <linux/unistd.h>
#include <linux/sysctl.h>

#include "defs.h"
#include "debug.h"
#include "k_route.h"
/*
 * Manipulating the kernel routing table through ioctl() operations...
 */

int k_add_rte(u_int32_t dst, u_int32_t gw, u_int32_t nm, short int hcnt) {
  
  int sock, ret;
  struct rtentry rte;
  struct sockaddr_in dest, mask, gtwy;

  memset (&rte, 0, sizeof (struct rtentry));
  
  dest.sin_family = AF_INET;
  dest.sin_addr.s_addr = htonl(dst);

  gtwy.sin_family = AF_INET;
  gtwy.sin_addr.s_addr = htonl(gw);

  mask.sin_family = AF_INET;
  mask.sin_addr.s_addr = htonl(nm);
 
  
  memcpy (&rte.rt_dst, &dest, sizeof (struct sockaddr_in));
  memcpy (&rte.rt_gateway, &gtwy, sizeof (struct sockaddr_in));
  memcpy (&rte.rt_genmask, &mask, sizeof (struct sockaddr_in));
  
  rte.rt_dev = this_host->ifname;

  rte.rt_metric = hcnt;

  /* For neighbors added by hello messages we don't have a gateway... */
  if(gw == 0) 
    rte.rt_flags = RTF_HOST | RTF_UP;
  else
    rte.rt_flags = RTF_HOST | RTF_GATEWAY | RTF_UP;

  sock = socket (AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      fprintf (stderr, "can't make socket\n");
      return -1;
    }

  /* Send message by ioctl(). */
  ret = ioctl (sock, SIOCADDRT, &rte);
  if (ret < 0)
    {
      switch (errno) 
	{
	case EEXIST:
	  close (sock);
	  perror("Sock err:");
	  return errno;
	  break;
	case ENETUNREACH:
	  close (sock);
	  perror("Sock err:");
	  return errno;
	  break;
	case EPERM:
	  close (sock);
	  perror("Sock err:");
	  return errno;
	  break;
	default:
	  close(sock);
	  perror("Sock err:");
	  return errno;
	}
      close (sock);
      return 1;
    }
  close (sock);

  return 0;
}


int k_del_rte(u_int32_t dst, u_int32_t gw, u_int32_t nm) {
  int sock, ret;
  struct rtentry rte;
  struct sockaddr_in dest, mask, gtwy;

  /*  printf("k_del_rte: Removing a kernel route (%s : %s : %s)!\n",  */
/*  	 ip_to_str(dst), ip_to_str(gw), ip_to_str(nm)); */
  memset (&rte, 0, sizeof (struct rtentry));
  
  dest.sin_family = AF_INET;
  dest.sin_addr.s_addr = htonl(dst);

  gtwy.sin_family = AF_INET;
  gtwy.sin_addr.s_addr = htonl(gw);

  mask.sin_family = AF_INET;
  mask.sin_addr.s_addr = htonl(nm);
 
  memcpy (&rte.rt_dst, &dest, sizeof (struct sockaddr_in));
  memcpy (&rte.rt_gateway, &gtwy, sizeof (struct sockaddr_in));
  memcpy (&rte.rt_genmask, &mask, sizeof (struct sockaddr_in));
  
  rte.rt_dev = this_host->ifname;

  rte.rt_flags = RTF_HOST;

  sock = socket (AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      fprintf (stderr, "k_del_rte: can't make socket\n");
      return -1;
    }

  /* Send message by ioctl(). */
  ret = ioctl (sock, SIOCDELRT, &rte);
  if (ret < 0)
    {
      switch (errno) 
	{
	case EEXIST:
	  close (sock);
	  perror("k_del_rte: Sock err:");
	  return errno;
	  break;
	case ENETUNREACH:
	  close (sock);
	  perror("k_del_rte: Sock err:");
	  return errno;
	  break;
	case EPERM:
	  close (sock);
	  perror("k_del_rte: Sock err:");
	  return errno;
	  break;
	}
      close (sock);
      return -1;
    } else {
      close (sock);
    }
  return 0;

}
/* This is an ugly way of updating a route... */
int k_chg_rte(u_int32_t dst, u_int32_t gw, u_int32_t nm, short int hcnt) {
  int ret;
  
  if((ret = k_del_rte(dst, 0, 0)) < 0)
    return ret;
  
  if((ret = k_add_rte(dst, gw, nm, hcnt)) < 0)
    return ret;

  return 0;
}


int k_del_arp(u_int32_t dst) {
  int sock, ret;
  struct arpreq req;
  struct sockaddr_in *dest;
  u_int32_t __dst;
  /*  printf("k_del_rte: Removing a kernel route (%s : %s : %s)!\n",  */
/*  	 ip_to_str(dst), ip_to_str(gw), ip_to_str(nm)); */
 
  memset(&req, 0, sizeof(struct arpreq));
  
  dest = (struct sockaddr_in *)&req.arp_pa;
  dest->sin_family = AF_INET;
    
  __dst = htonl(dst);

  memcpy(&dest->sin_addr, &__dst, sizeof(struct in_addr));

  /*  log(LOG_INFO, 0, "k_del_arp(): Removing %s from arp cache",  */
/*        inet_ntoa(dest->sin_addr)); */
  
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      fprintf (stderr, "k_del_arp: can't make socket\n");
      return -1;
    }

  /* Send message by ioctl(). */
  ret = ioctl (sock, SIOCDARP, &req);
  if (ret < 0)
    {
      switch (errno) 
	{
	case EEXIST:
	  close (sock);
	  perror("k_del_arp: Sock err:");
	  return errno;
	  break;
	case ENETUNREACH:
	  close (sock);
	  perror("k_del_arp: Sock err:");
	  return errno;
	  break;
	case EPERM:
	  close (sock);
	  perror("k_del_arp: Sock err:");
	  return errno;
	  break;
	}
      close (sock);
      return -1;
    } else {
      close (sock);
    }
  return 0;

}
/*
int k_read_route_table() {

  int mib[6] = {CTL_NET, PF_ROUTE, 0, 0, NET_RT_DUMP, 0};
  size_t needed;
  char *buf;
  
  struct rt_msghdr *rtm;

  if(sysctl(mib, 6, NULL, &nedded, NULL, 0) < 0) {
    fprintf(stderr, "Could not get size\n");
    exit(-1);
  }
  if( (buf = malloc(needed)) == NULL) {
    fprintf(stderr, "Malloc failed!\n");
    exit(-1);
  }
  if(sysctl(mib, 6, buf, &nedded, NULL, 0) < 0) {
    fprintf(stderr, "Could not get routing table\n");
    exit(-1);
  }
  
  
}

int main(int argc, char **argv) {
  u_int32_t dst, gtw, nmask;

  dst = (u_int32_t)inet_addr("130.238.8.50");
  gtw = (u_int32_t)inet_addr("130.238.8.51");
  nmask = (u_int32_t)inet_addr("255.255.255.0");
  
  k_add_route(dst, gtw, nmask);
  //k_del_route(dst, gtw, nmask);
  
  return 0;
}
*/
