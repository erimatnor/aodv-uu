/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University and Ericsson Telecom AB.
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
 * Author: Erik Nordström, <erno3431@student.uu.se>
 *
 *****************************************************************************/
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip.h>

#include "defs.h"
#include "debug.h"

#define ICMP_BUFSIZE sizeof(struct icmphdr) + 60 + 20

char icmp_send_buf[ICMP_BUFSIZE];
int icmp_socket;

static unsigned short cksum(unsigned short *w, int len)
{
    int sum = 0;
    unsigned short answer = 0;

    while (len > 1) {
	sum += *w++;
	len -= 2;
    }

    if (len == 1) {
	*(unsigned char *) (&answer) = *(unsigned char *) w;
	sum += answer;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;

    return (answer);
}

/* Data = IP header + 64 bits of data */
int icmp_send_host_unreachable(char *data, int len)
{
    struct icmphdr *icmp;
    struct sockaddr_in dst_addr;
    int ret, icmp_socket;
    char tos = IPTOS_PREC_INTERNETCONTROL;
    int i;

    icmp_socket = socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (icmp_socket < 0)
	return -1;

    setsockopt(icmp_socket, SOL_IP, IP_TOS, &tos, sizeof(char));

    memset(icmp_send_buf, 0, ICMP_BUFSIZE);

    icmp = (struct icmphdr *) icmp_send_buf;

    icmp->type = ICMP_DEST_UNREACH;
    icmp->code = ICMP_HOST_UNREACH;

    memcpy(icmp_send_buf + sizeof(struct icmphdr), data, len);

    icmp->checksum = cksum((u_short *) icmp, len + sizeof(struct icmphdr));

    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(INADDR_ANY);

    /* Send ICMP message on all AODV enabled interfaces */
    for(i = 0; i < MAX_NR_INTERFACES; i++) {
	if(!DEV_NR(i).enabled)
	    continue;
#ifdef DEBUG
	log(LOG_DEBUG, 0, "icmp_send: Sending HOST_UNREACHABLE to %s, len=%d",
	    ip_to_str(DEV_NR(i).ipaddr), len);
#endif
	dst_addr.sin_addr.s_addr = htonl(DEV_NR(i).ipaddr);
	
	ret = sendto(icmp_socket, icmp_send_buf, len + sizeof(struct icmphdr), 
		     0, (struct sockaddr *) &dst_addr, sizeof(dst_addr));
	if(ret < 0)
	    log(LOG_WARNING, errno, "icmp: Could not sent ICMP msg to %s",
		ip_to_str(DEV_NR(i).ipaddr));
    }
    close(icmp_socket);

    return 0;
}
