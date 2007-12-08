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
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <net/if.h>

#include "aodv_rreq.h"
#include "aodv_rrep.h"
#include "aodv_rerr.h"
#include "defs.h"
#include "debug.h"
#include "params.h"
#include "timer_queue.h"
#include "routing_table.h"

extern int log_to_file, rt_log_interval;
extern char *progname;
int log_file_fd = -1;
int log_rt_fd = -1;
int log_nmsgs = 0;
int debug = 0;
struct timer rt_timer;

void log_init()
{
    if (log_to_file) {
	if ((log_file_fd =
	     open(AODV_LOG_PATH, O_RDWR | O_CREAT | O_TRUNC,
		  S_IROTH | S_IWUSR | S_IRUSR | S_IRGRP)) < 0) {
	    perror("log_init: open log file failed!");
	    exit(-1);
	}
    }
    if (rt_log_interval) {
	if ((log_rt_fd =
	     open(AODV_RT_LOG_PATH, O_RDWR | O_CREAT | O_TRUNC,
		  S_IROTH | S_IWUSR | S_IRUSR | S_IRGRP)) < 0) {
	    perror("log_init: open rt log file failed!");
	    exit(-1);
	}
    }
}
void log_rt_table_init()
{
    rt_timer.handler = print_rt_table;
    rt_timer.data = NULL;
    timer_add_msec(&rt_timer, rt_log_interval);
}

void log_cleanup()
{
    if (log_to_file && log_file_fd) {
	if (close(log_file_fd) < 0)
	    fprintf(stderr, "log_cleanup: Could not close log_file_fd!\n");
    }
}

void write_to_log_file(char *msg, int len)
{
    if (!log_file_fd) {
	fprintf(stderr, "write_to_log: Could not write to log file\n");
	return;
    }
    if (len <= 0) {
	fprintf(stderr, "write_to_log: len=0\n");
	return;
    }
    if (write(log_file_fd, msg, len) < 0)
	perror("write_to_log: Could not write to log file");
}

char *packet_type(u_int type)
{
    static char temp[50];

    switch (type) {
    case AODV_RREQ:
	return "AODV_RREQ";
    case AODV_RREP:
	return "AODV_RREP";
    case AODV_RERR:
	return "AODV_RERR";
    default:
	sprintf(temp, "Unknown packet type %d", type);
	return temp;
    }
}

void log(int type, int syserr, char *format, ...)
{
    va_list ap;
    static char buffer[256] = "WARNING: ";
    static char log_buf[1024];
    char *msg;
    struct timeval now;
    struct tm *time;
    int len = 0;

    va_start(ap, format);

    if (type == LOG_WARNING)
	msg = &buffer[9];
    else
	msg = buffer;

    vsprintf(msg, format, ap);
    va_end(ap);

    if (!debug && !log_to_file)
	goto syslog;

    gettimeofday(&now, NULL);
    time = localtime(&now.tv_sec);

    if (type <= LOG_NOTICE)
	len += sprintf(log_buf + len, "%s: ", progname);

    len += sprintf(log_buf + len, "%02d:%02d:%02d.%03ld %s", time->tm_hour,
		   time->tm_min, time->tm_sec, now.tv_usec / 1000, msg);

    if (syserr == 0)
	len += sprintf(log_buf + len, "\n");
    else if (syserr < sys_nerr)
	len += sprintf(log_buf + len, ": %s\n", sys_errlist[syserr]);
    else
	len += sprintf(log_buf + len, ": errno %d\n", syserr);

    if (len > 1024) {
	fprintf(stderr, "log(): buffer to small! len = %d\n", len);
	goto syslog;
    }

    /* OK, we are clear to write the buffer to the aodv log file... */
    if (log_to_file)
	write_to_log_file(log_buf, len);

    /* If we have the debug option set, also write to stdout */
    if (debug)
	printf(log_buf);


    /* Syslog all messages that are of severity LOG_WARNING or worse */
  syslog:
    if (type <= LOG_NOTICE) {
	if (syserr != 0) {
	    errno = syserr;
	    syslog(type, "%s: %m", msg);
	} else
	    syslog(type, "%s", msg);
    }
    if (type <= LOG_ERR)
	exit(-1);
}


char *rreq_flags_to_str(RREQ * rreq)
{
    static char buf[4];
    int len = 0;
    char *str;

    if (rreq->j)
	buf[len++] = 'J';
    if (rreq->r)
	buf[len++] = 'R';
    if (rreq->g)
	buf[len++] = 'G';

    buf[len] = '\0';

    str = buf;
    return str;
}

char *rrep_flags_to_str(RREP * rrep)
{
    static char buf[3];
    int len = 0;
    char *str;

    if (rrep->r)
	buf[len++] = 'R';
    if (rrep->a)
	buf[len++] = 'A';

    buf[len] = '\0';

    str = buf;
    return str;
}

void log_pkt_fields(AODV_msg * msg)
{

    RREQ *rreq;
    RREP *rrep;
    RERR *rerr;

    switch (msg->type) {
    case AODV_RREQ:
	rreq = (RREQ *) msg;
	log(LOG_DEBUG, 0,
	    "rreq->flags:%s rreq->hopcount=%d rreq->rreq_id=%ld",
	    rreq_flags_to_str(rreq), rreq->hcnt, ntohl(rreq->rreq_id));
	log(LOG_DEBUG, 0, "rreq->dest_addr:%s rreq->dest_seqno=%lu",
	    ip_to_str(ntohl(rreq->dest_addr)), ntohl(rreq->dest_seqno));
	log(LOG_DEBUG, 0, "rreq->source_addr:%s rreq->orig_seqno=%ld",
	    ip_to_str(ntohl(rreq->orig_addr)), ntohl(rreq->orig_seqno));
	break;
    case AODV_RREP:
	rrep = (RREP *) msg;
	log(LOG_DEBUG, 0, "rrep->flags:%s rrep->hcnt=%d",
	    rrep_flags_to_str(rrep), rrep->hcnt);
	log(LOG_DEBUG, 0, "rrep->dest_addr:%s rrep->dest_seqno=%d",
	    ip_to_str(ntohl(rrep->dest_addr)), ntohl(rrep->dest_seqno));
	log(LOG_DEBUG, 0, "rrep->orig_addr:%s rrep->lifetime=%d",
	    ip_to_str(ntohl(rrep->orig_addr)), ntohl(rrep->lifetime));
	break;
    case AODV_RERR:
	rerr = (RERR *) msg;
	log(LOG_DEBUG, 0, "rerr->dest_count:%d", rerr->dest_count);
	break;
    }
}
char *rt_flags_to_str(u_int16_t flags)
{
    static char buf[5];
    int len = 0;
    char *str;

    if (flags & NEIGHBOR)
	buf[len++] = 'N';
    if (flags & FWD_ROUTE)
	buf[len++] = 'F';
    if (flags & REV_ROUTE)
	buf[len++] = 'R';
    if (flags & UNIDIR)
	buf[len++] = 'U';

    buf[len] = '\0';

    str = buf;
    return str;
}
char *devs_ip_to_str() {
    static char buf[MAX_NR_INTERFACES*IFNAMSIZ];
    char *str;
    int i, index = 0;
    
    for (i = 0; i < MAX_NR_INTERFACES; i++) {
	if (!DEV_NR(i).enabled)
	    continue;
	index += sprintf(buf+index,"%s,", ip_to_str(DEV_NR(i).ipaddr));
    }
    str = buf;
    return str;
}
void print_rt_table()
{
    char rt_buf[512], ifname[64];
    int len = 0;
    int i = 0;
    rt_table_t *entry;
    precursor_t *pr;
    struct timeval now;
    struct tm *time;

    gettimeofday(&now, NULL);
    time = localtime(&now.tv_sec);

    len +=
	sprintf(rt_buf, "# Time: %02d:%02d:%02d.%03ld IP: %s seqno: %u\n",
		time->tm_hour, time->tm_min, time->tm_sec,
		now.tv_usec / 1000, devs_ip_to_str(),
		this_host.seqno);
    len +=
	sprintf(rt_buf + len,
		"%-15s %-15s %-3s %-3s %-5s %-6s %-5s %-5s %-15s\n",
		"Destination", "Next hop", "HC", "LHC", "Seqno", "Expire",
		"Flags", "Iface", "Precursors");
    write(log_rt_fd, rt_buf, len);
    len = 0;

    for (i = 0; i < RT_TABLESIZE; i++) {
	entry = routing_table[i];
	while (entry != NULL) {
	    /* Printe routing table entries one by one... */
	    if (entry->precursors == NULL)
		len +=
		    sprintf(rt_buf + len,
			    "%-15s %-15s %-3d %-3d %-5u %-6ld %-5s %-5s\n",
			    ip_to_str(entry->dest_addr),
			    ip_to_str(entry->next_hop), entry->hcnt,
			    entry->last_hcnt, entry->dest_seqno,
			    ((entry->hcnt ==
			      255) ? 0 : timeval_diff(&entry->rt_timer.
						      timeout,
						      &now)) / 1000,
			    rt_flags_to_str(entry->flags), 
			    if_indextoname(entry->ifindex, ifname));
	    else {
		len +=
		    sprintf(rt_buf + len,
			    "%-15s %-15s %-3d %-3d %-5u %-6ld %-5s %-5s %-15s\n",
			    ip_to_str(entry->dest_addr),
			    ip_to_str(entry->next_hop), entry->hcnt,
			    entry->last_hcnt, entry->dest_seqno,
			    timeval_diff(&entry->rt_timer.timeout,
					 &now) / 1000,
			    rt_flags_to_str(entry->flags), 
			    if_indextoname(entry->ifindex, ifname),
			    ip_to_str(entry->precursors->neighbor));

		/* Print all precursors for the current routing entry */
		for (pr = entry->precursors->next; (pr != NULL);
		     pr = pr->next)
		    len +=
			sprintf(rt_buf + len, "%60s %-15s\n", "*",
				ip_to_str(pr->neighbor));
	    }
	    write(log_rt_fd, rt_buf, len);
	    len = 0;

	    entry = entry->next;
	}
    }
    /* Schedule a new printing of routing table... */
    timer_add_msec(&rt_timer, rt_log_interval);
}

/* Converts a ipv4 address in host order to a printable string... At
   most four subsequent calls can be made to this funtion without
   overwriting previous strings... I.e. at most four calls to this
   function can be made in a printf (or log) statement without
   strange behavior. */
char *ip_to_str(u_int32_t addr)
{
    struct in_addr a;
    static char buf[16 * 4];
    static int index = 0;
    char *str;

    a.s_addr = htonl(addr);
    strcpy(&buf[index], inet_ntoa(a));
    str = &buf[index];
    index += 16;
    index %= 64;
    return str;
}
