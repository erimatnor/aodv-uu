/*****************************************************************************
 *
 * Copyright (C) 2002 Uppsala University.
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
 * Authors: Björn Wiberg <bjwi7937@student.uu.se>
 *
 *****************************************************************************/

#ifndef AODV_UU_H
#define AODV_UU_H

/* This is a C++ port of AODV-UU for ns-2 */
#ifndef NS_PORT
#error "To compile the ported version, NS_PORT must be defined!"
#endif				/* NS_PORT */

/* Header files from ns-2 */
#include <common/packet.h>
#include <common/timer-handler.h>
#include <tools/random.h>
#include <trace/cmu-trace.h>

/* Forward declaration needed to be able to reference the class */
class AODVUU;

/* Global definitions */
#include "params.h"
#include "defs.h"

/* Extract global data types, defines and global declarations */
#undef NS_NO_GLOBALS
#define NS_NO_DECLARATIONS

#include "timer_queue.h"
#include "aodv_hello.h"
#include "aodv_rerr.h"
#include "aodv_rrep.h"
#include "aodv_rreq.h"
#include "aodv_socket.h"
#include "aodv_timeout.h"
#include "debug.h"
#include "packet_input.h"
#include "routing_table.h"
#include "seek_list.h"
#include "packet_queue.h"

#undef NS_NO_DECLARATIONS


/* Timer class for managing the queue of timers */
class TimerQueueTimer:public TimerHandler {

  public:
    TimerQueueTimer(AODVUU * a):TimerHandler() {
	agent_ = a;
    } virtual void expire(Event * e);

  protected:
    AODVUU * agent_;
};


/* The AODV-UU routing agent class */
class AODVUU:public Agent {

    friend class TimerQueueTimer;

  public:
     AODVUU(nsaddr_t id);
    ~AODVUU();
    void recv(Packet * p, Handler *);
    int command(int argc, const char *const *argv);
    void packetFailed(Packet * p);

  protected:
    void sendPacket(Packet * p, u_int32_t next_hop, double delay);
    int startAODVUUAgent();
    int gettimeofday(struct timeval *tv, struct timezone *tz);
    char *if_indextoname(int ifindex, char *ifname);

    void scheduleNextEvent() {
	struct timeval *timeout;
	 timeout = timer_age_queue();

	if (timeout)
	     tqtimer.resched((double) timeout->tv_sec +
			     (double) timeout->tv_usec / (double) 1000000);
    } int initialized;
    TimerQueueTimer tqtimer;

    /*
       Extract method declarations (and occasionally, variables)
       from header files
     */
#define NS_NO_GLOBALS
#undef NS_NO_DECLARATIONS

#undef AODV_HELLO_H
#include "aodv_hello.h"

#undef AODV_RERR_H
#include "aodv_rerr.h"

#undef AODV_RREP_H
#include "aodv_rrep.h"

#undef AODV_RREQ_H
#include "aodv_rreq.h"

#undef AODV_SOCKET_H
#include "aodv_socket.h"

#undef AODV_TIMEOUT_H
#include "aodv_timeout.h"

#undef DEBUG_H
#include "debug.h"

#undef PACKET_INPUT_H
#include "packet_input.h"

#undef PACKET_QUEUE_H
#include "packet_queue.h"

#undef ROUTING_TABLE_H
#include "routing_table.h"

#undef SEEK_LIST_H
#include "seek_list.h"

#undef TIMER_QUEUE_H
#include "timer_queue.h"

#undef NS_NO_GLOBALS

    /* (Previously global) variables from main.c */
    int log_to_file;
    int rt_log_interval;
    int unidir_hack;
    int rreq_gratuitous;
    int expanding_ring_search;
    int internet_gw_mode;
    int local_repair;
    int receive_n_hellos;
    int hello_jittering;
    char *progname;
    char versionstring[100];
    int wait_on_reboot;
    struct timer worb_timer;

    /* From aodv_hello.c */
    struct timer hello_timer;

    /* From aodv_rreq.c */
    struct rreq_record *rreq_record_head;
    struct blacklist *rreq_blacklist_head;

    /* From aodv_socket.c */
    char recv_buf[RECV_BUF_SIZE];
    char send_buf[SEND_BUF_SIZE];

    /* From debug.c */
    int log_file_fd;
    int log_rt_fd;
    int log_nmsgs;
    int debug;
    struct timer rt_log_timer;

    /* From defs.h */
    struct host_info this_host;
    unsigned int dev_indices[MAX_NR_INTERFACES];
    inline int ifindex2devindex(unsigned int ifindex);

    /* From seek_list.c */
    seek_list_t *seek_list_head;

    /* From timer_queue.c */
    struct timer *TQ;
};


/* From defs.h (needs the AODVUU class declaration) */
inline int NS_CLASS ifindex2devindex(unsigned int ifindex)
{
    int i;

    for (i = 0; i < this_host.nif; i++)
	if (dev_indices[i] == ifindex)
	    return i;

    return -1;
}

#endif				/* AODV_UU_H */
