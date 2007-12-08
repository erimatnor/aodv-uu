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
 * Authors: Björn Wiberg <bjorn.wiberg@home.se>
 *          Erik Nordström <erik.nordstrom@it.uu.se>
 *
 *****************************************************************************/

#include <string.h>
#include <assert.h>

#include "aodv-uu.h"


/* Method for determining the size of the AODVUU packet header type */
int AODV_msg::size()
{
    return AODV_MSG_MAX_SIZE;
}


/* Tcl hooks for enabling the AODVUU packet header type */
int hdr_aodvuu::offset_;

static class AODVUUHeaderClass:public PacketHeaderClass {

  public:
    AODVUUHeaderClass():PacketHeaderClass("PacketHeader/AODVUU",
					  AODV_MSG_MAX_SIZE) {
	bind_offset(&hdr_aodvuu::offset_);
}} class_rtProtoAODVUU_hdr;


/* Tcl hooks for the AODVUU routing agent */
static class AODVUUclass:public TclClass {

  public:
    AODVUUclass():TclClass("Agent/AODVUU") {
    } TclObject *create(int argc, const char *const *argv) {
	/* argv[4] and up are arguments to the constructor */
	assert(argc == 5);
	return (new AODVUU((nsaddr_t) atoi(argv[4])));
    }
}

class_rtProtoAODVUU;


/* Handler for investigating the queue of timers */
void TimerQueueTimer::expire(Event *e)
{
    struct timeval *timeout;
    timeout = agent_->timer_age_queue();

    if (timeout)
	resched((double) timeout->tv_sec +
		(double) timeout->tv_usec / (double) 1000000);
}


/* Constructor for the AODVUU routing agent */
NS_CLASS AODVUU(nsaddr_t id) : Agent(PT_AODVUU), initialized(0),
			       tqtimer(this), ifqueue(0)
{
    /*
       Enable usage of some of the configuration variables from Tcl.

       Note: Do NOT change the values of these variables in the constructor
       after binding them! The desired default values should be set in
       ~ns/tcl/lib/ns-default.tcl instead.
     */
    bind("unidir_hack_", &unidir_hack);
    bind("rreq_gratuitous_", &rreq_gratuitous);
    bind("expanding_ring_search_", &expanding_ring_search);
    bind("local_repair_", &local_repair);
    bind("receive_n_hellos_", &receive_n_hellos);
    bind("hello_jittering_", &hello_jittering);
    bind("wait_on_reboot_", &wait_on_reboot);
    bind("debug_", &debug);
    bind("rt_log_interval_", &rt_log_interval);	// Note: in milliseconds!
    bind("log_to_file_", &log_to_file);
    bind("optimized_hellos_", &optimized_hellos);
    bind("ratelimit_", &ratelimit);
    bind("llfeedback_", &llfeedback);

    /*
       Force certain settings. These variables should NOT be bound to Tcl
       with bind() since we don't want their values to be changed.
     */
    internet_gw_mode = 0;

    /* Other initializations follow */

    /* From main.c */
    progname = strdup("AODV-UU");

    /* From debug.c */
    /* Note: log_nmsgs was never used anywhere */
    log_nmsgs = 0;
    log_file_fd = -1;
    log_rt_fd = -1;

    /* Set host parameters */
    memset(&this_host, 0, sizeof(struct host_info));
    memset(dev_indices, 0, sizeof(unsigned int) * MAX_NR_INTERFACES);
    this_host.seqno = 0;
    this_host.rreq_id = 0;

    /* Set network interface parameters */
    DEV_NR(NS_DEV_NR).enabled = 1;
    DEV_NR(NS_DEV_NR).sock = -1;
    DEV_NR(NS_DEV_NR).ifindex = NS_IFINDEX;
    dev_indices[NS_DEV_NR] = NS_IFINDEX;

    const char faked_ifname[] = "nsif";
    strncpy(DEV_NR(NS_DEV_NR).ifname, faked_ifname, IFNAMSIZ - 1);
    DEV_NR(NS_DEV_NR).ifname[IFNAMSIZ - 1] = '\0';

    DEV_NR(NS_DEV_NR).ipaddr = (u_int32_t) id;

    // Netmask not used in simulations
    DEV_NR(NS_DEV_NR).netmask = (u_int32_t) 0;

    DEV_NR(NS_DEV_NR).broadcast = AODV_BROADCAST;

    // Internet gateway mode not used in simulations
    this_host.gateway_mode = 0;

    // One enabled network interface (in total)
    this_host.nif = 1;

    /* Set agent parameters */
    addr() = id;
    port() = RT_PORT;
    dport() = RT_PORT;

    INIT_LIST_HEAD(&rreq_records);
    INIT_LIST_HEAD(&rreq_blacklist); 
    INIT_LIST_HEAD(&seekhead);
    INIT_LIST_HEAD(&TQ);

    /* Initialize data structures */
    worb_timer.data = NULL;
    worb_timer.used = 0;
    hello_timer.data = NULL;
    hello_timer.used = 0;
    rt_log_timer.data = NULL;
    rt_log_timer.used = 0;
    aodv_socket_init();
    rt_table_init();
    packet_queue_init();
    packet_input_init();
}


/* Destructor for the AODV-UU routing agent */
NS_CLASS ~ AODVUU()
{
    rt_table_destroy();
    packet_input_cleanup();
    log_cleanup();
}


/*
  Link layer callback function.
  Used when link layer packet delivery fails.
*/
static void link_layer_callback(Packet *p, void *arg)
{
    ((AODVUU *) arg)->packetFailed(p);
}


/* 
   A small function used as filter to remove packets by destination
   from the interface queue.
*/
int ifqueue_filter_on_dest(Packet *p, void *data)
{
    struct hdr_ip *ih = HDR_IP(p);
    u_int32_t *addr = (u_int32_t *) data;
    u_int32_t dest_addr = (u_int32_t) ih->daddr();
    
    return (dest_addr == *addr);
}


/*
  Moves pending packets with a certain next hop from the interface
  queue to the packet buffer or simply drops it.
*/
void NS_CLASS interfaceQueue(nsaddr_t addr, int action)
{
    Packet *queued_pkt = NULL;
    struct hdr_ip *ih = NULL;
    u_int32_t dest_addr = 0;
    
    /* Check that interface queue reference is valid */
    if (ifqueue == NULL || ifqueue == 0)
	return;
    
    /* Move packets from interface queue to packet buffer */
    switch (action) {
    case IFQ_DROP:
	while ((queued_pkt = ifqueue->filter(addr))) 
	    drop(queued_pkt, DROP_RTR_NO_ROUTE);    
	break;
    case IFQ_DROP_BY_DEST:
	DEBUG(LOG_DEBUG, 0, "Dropping pkts for %s", 
	      ip_to_str((u_int32_t) addr));
	ifqueue->filter(ifqueue_filter_on_dest, (void *) &addr);
	    
	break;
    case IFQ_BUFFER:
	while ((queued_pkt = ifqueue->filter(addr))) {
	    ih = HDR_IP(queued_pkt);
	    dest_addr = (u_int32_t) ih->daddr();
	    packet_queue_add(queued_pkt, dest_addr);
	    DEBUG(LOG_DEBUG, 0, "Rebuffered IFQ packet");
	}
	break;
    default:
	DEBUG(LOG_DEBUG, 0, "Unspecified action. Don't know what to do.");
    }
}


/* Called for packets whose delivery fails at the link layer */
void NS_CLASS packetFailed(Packet *p)
{
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);
    rt_table_t *rt_next_hop, *rt;
    u_int32_t dest_addr = (u_int32_t) ih->daddr();
    u_int32_t src_addr = (u_int32_t) ih->saddr();
    u_int32_t next_hop = (u_int32_t) ch->next_hop_;
    packet_t pt = ch->ptype();

    /* We don't care about link failures for broadcast or non-data packets */
    if (!(DATA_PACKET(pt) || pt == PT_PING) ||
	dest_addr == IP_BROADCAST || dest_addr == AODV_BROADCAST) {
	drop(p, DROP_RTR_MAC_CALLBACK);
	goto end;
    }
    DEBUG(LOG_DEBUG, 0, "LINK FAILURE for next_hop=%s dest=%s uid=%d",
	  ip_to_str(next_hop), ip_to_str(dest_addr), ch->uid());
  
    if (seek_list_find(dest_addr)) {
	DEBUG(LOG_DEBUG, 0, "Ongoing route discovery, buffering packet...");
	packet_queue_add(p, dest_addr);
	goto end;
    }
    
    rt_next_hop = rt_table_find(next_hop);
    rt = rt_table_find(dest_addr);
    
    if (!rt_next_hop || rt_next_hop->state == INVALID)
	goto drop;
    
    if (!rt || rt->state == INVALID)
	goto drop;
    
    if (rt->next_hop != next_hop) {
	DEBUG(LOG_DEBUG, 0, "next hop mismatch - DROPPING pkt");
	drop(p, DROP_RTR_MAC_CALLBACK);
	goto end;
    }

    /* Do local repair? */
    if (local_repair && rt->hcnt <= MAX_REPAIR_TTL 
	/* && ch->num_forwards() > rt->hcnt */
	) {
	
        /* Buffer the current packet */
        packet_queue_add(p, dest_addr);

        /* Buffer pending packets from interface queue */
        interfaceQueue((nsaddr_t) next_hop, IFQ_BUFFER);
	
	/* Mark the route to be repaired */
	rt_next_hop->flags |= RT_REPAIR;
	neighbor_link_break(rt_next_hop);
	rreq_local_repair(rt, src_addr, NULL);

    } else {

	/* No local repair - just force timeout of link and drop packets */
	neighbor_link_break(rt_next_hop);
    drop:	
	drop(p, DROP_RTR_MAC_CALLBACK);
	interfaceQueue((nsaddr_t) next_hop, IFQ_DROP);
    }

 end:
    /*
      The link layer event might have changed the timer queue,
      so we'd better reschedule the timer queue timer...
    */
    scheduleNextEvent();
}


/* Entry-level packet reception */
void NS_CLASS recv(Packet *p, Handler *)
{
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);

    /* Routing agent must be started before processing packets */
    assert(initialized);

    /* Detect routing loops */
    if (ih->saddr() == (nsaddr_t) (DEV_NR(NS_DEV_NR).ipaddr) &&
	ch->num_forwards() > 0) {
	drop(p, DROP_RTR_ROUTE_LOOP);
	goto end;
    }

    /* Handle packet depending on type */
    if (ch->ptype() == PT_AODVUU)
	recvAODVUUPacket(p);	// AODV-UU messages (control packets)
    else
	processPacket(p);	// Other packets (data packets)

  end:
    /* Check if any other events are pending and reschedule the timeout */
    scheduleNextEvent();
}


/* Sends a packet using the specified next hop and delay */
void NS_CLASS sendPacket(Packet *p, u_int32_t next_hop, double delay)
{
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);
    double jitter = 0.0;
    u_int32_t dest_addr = ih->daddr();

    /* If TTL = 0, drop packet */
    if (ih->ttl() == 0) {
	DEBUG(LOG_WARNING, 0, "Dropping packet with TTL = 0.");
	drop(p, DROP_RTR_TTL);
	return;
    }

    /* Set packet fields depending on packet type */
    if (dest_addr == AODV_BROADCAST) {
	/* Broadcast packet */
	ch->prev_hop_ = (nsaddr_t) DEV_NR(NS_DEV_NR).ipaddr;
	ch->addr_type() = NS_AF_NONE;
	ch->direction() = hdr_cmn::DOWN;
	jitter = 0.02 * Random::uniform();

	Scheduler::instance().schedule(target_, p, jitter);
    } else {
	/* Unicast packet */
	ch->next_hop_ = next_hop;
	ch->prev_hop_ = (nsaddr_t) DEV_NR(NS_DEV_NR).ipaddr;
	ch->addr_type() = NS_AF_INET;
	ch->direction() = hdr_cmn::DOWN;

	if (llfeedback) {
	    ch->xmit_failure_ = link_layer_callback;
	    ch->xmit_failure_data_ = (void *) this;
	}

	Scheduler::instance().schedule(target_, p, delay);
    }
}


/* Interpreter for commands from Tcl */
int NS_CLASS command(int argc, const char *const *argv)
{

    /* start: starts the routing agent */
    if (strcasecmp(argv[1], "start") == 0) {
	return startAODVUUAgent();
    }

    /* if-queue: allows for access to the interface queue */
    else if (strcasecmp(argv[1], "if-queue") == 0) {
      ifqueue = (PriQueue *) TclObject::lookup(argv[2]);

      if (ifqueue == 0)
	return TCL_ERROR;
      else
	return TCL_OK;
    }

    /* tracetarget: target for trace info specific for the routing agent */
    else if (strcasecmp(argv[1], "tracetarget") == 0) {
	// Note: Currently no such trace info is generated.
	return TCL_OK;
    }

    /* Unknown commands are passed to the Agent base class */
    return Agent::command(argc, argv);
}


/* Starts the AODV-UU routing agent */
int NS_CLASS startAODVUUAgent()
{
    if (initialized == 0) {

	log_init();

	/* Set up the wait-on-reboot timer */
	if (wait_on_reboot) {
	    worb_timer.handler = &NS_CLASS wait_on_reboot_timeout;
	    worb_timer.data = &wait_on_reboot;
	    worb_timer.used = 0;
	    timer_set_timeout(&worb_timer, DELETE_PERIOD);
	    DEBUG(LOG_NOTICE, 0, "In wait on reboot for %d milliseconds.",
		  DELETE_PERIOD);
	}
	/* Schedule the first HELLO */
	if (!llfeedback && !optimized_hellos)
	    hello_start();

	/* Initialize routing table logging */
	if (rt_log_interval)
	    log_rt_table_init();

	/* Initialization complete */
	initialized = 1;

	DEBUG(LOG_DEBUG, 0, "Routing agent with IP = %s started.",
	      ip_to_str(DEV_NR(NS_DEV_NR).ipaddr));

	DEBUG(LOG_DEBUG, 0, "Settings:");
	DEBUG(LOG_DEBUG, 0, "unidir_hack %s", unidir_hack ? "ON" : "OFF");
	DEBUG(LOG_DEBUG, 0, "rreq_gratuitous %s", rreq_gratuitous ? "ON" : "OFF");
	DEBUG(LOG_DEBUG, 0, "expanding_ring_search %s", expanding_ring_search ? "ON" : "OFF");
	DEBUG(LOG_DEBUG, 0, "local_repair %s", local_repair ? "ON" : "OFF");
	DEBUG(LOG_DEBUG, 0, "receive_n_hellos %s", receive_n_hellos ? "ON" : "OFF");
	DEBUG(LOG_DEBUG, 0, "hello_jittering %s", hello_jittering ? "ON" : "OFF");
	DEBUG(LOG_DEBUG, 0, "wait_on_reboot %s", wait_on_reboot ? "ON" : "OFF");
	DEBUG(LOG_DEBUG, 0, "optimized_hellos %s", optimized_hellos ? "ON" : "OFF");
	DEBUG(LOG_DEBUG, 0, "ratelimit %s", ratelimit ? "ON" : "OFF");
	DEBUG(LOG_DEBUG, 0, "llfeedback %s", llfeedback ? "ON" : "OFF");


	/* Schedule the first timeout */
	scheduleNextEvent();

	return TCL_OK;

    } else {

	/* AODV-UU routing agent already started */
	return TCL_ERROR;
    }
}


/*
  Reschedules the timer queue timer to go off at the time of the
  earliest event (so that the timer queue will be investigated then).
  Should be called whenever something might have changed the timer queue.
*/
void NS_CLASS scheduleNextEvent()
{
    struct timeval *timeout;
    timeout = timer_age_queue();

    if (timeout)
	tqtimer.resched((double) timeout->tv_sec +
			(double) timeout->tv_usec / (double) 1000000);
}


/*
  Replacement for gettimeofday(), used for timers.
  The timeval should only be interpreted as number of seconds and
  fractions of seconds since the start of the simulation.
*/
int NS_CLASS gettimeofday(struct timeval *tv, struct timezone *tz)
{

    /* Timeval is required, timezone is ignored */
    if (!tv)
	return -1;

    double current_time = Scheduler::instance().clock();

    tv->tv_sec = 0;
    tv->tv_usec = 0;

    timeval_add_msec(tv, (long) (1000.0 * current_time));

    return 0;
}


/*
  Replacement for if_indextoname(), used in routing table logging.
*/
char *NS_CLASS if_indextoname(int ifindex, char *ifname)
{
    assert(ifindex >= 0);
    strncpy(ifname, DEV_IFINDEX(ifindex).ifname, IFNAMSIZ - 1);
    DEV_IFINDEX(ifindex).ifname[IFNAMSIZ - 1] = '\0';
    return ifname;
}
