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
 * Authors: Erik Nordström, <erik.nordstrom@it.uu.se>
 *
 *
 *****************************************************************************/

#ifdef NS_PORT
#include "ns-2/aodv-uu.h"
#else
#include <netinet/in.h>

#include "aodv_rrep.h"
#include "aodv_rreq.h"
#include "aodv_socket.h"
#include "aodv_timeout.h"
#include "debug.h"
#include "defs.h"
#include "params.h"
#include "routing_table.h"
#include "seek_list.h"
#include "timer_queue.h"

#include "locality.h"
#endif

/* Comment this to remove packet field output: */
#define DEBUG_OUTPUT

#ifndef NS_PORT
static LIST(rreq_records);
static LIST(rreq_blacklist);

static struct rreq_record *rreq_record_insert(struct in_addr orig_addr,
                                              u_int32_t rreq_id);
static struct rreq_record *rreq_record_find(struct in_addr orig_addr,
                                            u_int32_t rreq_id);

struct blacklist *rreq_blacklist_find(struct in_addr dest_addr);

extern int rreq_gratuitous, expanding_ring_search;
extern int internet_gw_mode;
#endif

RREQ *NS_CLASS rreq_create(u_int8_t flags, struct in_addr dest_addr,
                           u_int32_t dest_seqno, struct in_addr orig_addr)
{
    RREQ *rreq;

    rreq = (RREQ *)aodv_socket_new_msg();
    rreq->type = AODV_RREQ;
    rreq->res1 = 0;
    rreq->res2 = 0;
    rreq->hcnt = 0;
    rreq->rreq_id = htonl(this_host.rreq_id++);
    rreq->dest_addr = dest_addr.s_addr;
    rreq->dest_seqno = htonl(dest_seqno);
    rreq->orig_addr = orig_addr.s_addr;

    /* Immediately before a node originates a RREQ flood it must
       increment its sequence number... */
    seqno_incr(this_host.seqno);
    rreq->orig_seqno = htonl(this_host.seqno);

    if (flags & RREQ_JOIN)
        rreq->j = 1;
    if (flags & RREQ_REPAIR)
        rreq->r = 1;
    if (flags & RREQ_GRATUITOUS)
        rreq->g = 1;
    if (flags & RREQ_DEST_ONLY)
        rreq->d = 1;

    DEBUG(LOG_DEBUG, 0, "Assembled RREQ %s", ip_to_str(dest_addr));
#ifdef DEBUG_OUTPUT
    log_pkt_fields((AODV_msg *)rreq);
#endif

    return rreq;
}

AODV_ext *rreq_add_ext(RREQ *rreq, int type, unsigned int offset, int len,
                       char *data)
{
    AODV_ext *ext = NULL;

    if (offset < RREQ_SIZE)
        return NULL;

    ext = (AODV_ext *)((char *)rreq + offset);

    ext->type = type;
    ext->length = len;

    memcpy(AODV_EXT_DATA(ext), data, len);

    return ext;
}

void NS_CLASS rreq_send(struct in_addr dest_addr, u_int32_t dest_seqno, int ttl,
                        u_int8_t flags)
{
    RREQ *rreq;
    struct in_addr dest;
    int i;

    dest.s_addr = AODV_BROADCAST;

    /* Check if we should force the gratuitous flag... (-g option). */
    if (rreq_gratuitous)
        flags |= RREQ_GRATUITOUS;

    /* Broadcast on all interfaces */
    for (i = 0; i < MAX_NR_INTERFACES; i++) {
        if (!DEV_NR(i).enabled)
            continue;
        rreq = rreq_create(flags, dest_addr, dest_seqno, DEV_NR(i).ipaddr);
        aodv_socket_send((AODV_msg *)rreq, dest, RREQ_SIZE, ttl, &DEV_NR(i));
    }
}

void NS_CLASS rreq_forward(RREQ *rreq, int size, int ttl)
{
    struct in_addr dest, orig;
    int i;

    dest.s_addr = AODV_BROADCAST;
    orig.s_addr = rreq->orig_addr;

    /* FORWARD the RREQ if the TTL allows it. */
    DEBUG(LOG_INFO, 0, "forwarding RREQ src=%s, rreq_id=%lu", ip_to_str(orig),
          ntohl(rreq->rreq_id));

    /* Queue the received message in the send buffer */
    rreq = (RREQ *)aodv_socket_queue_msg((AODV_msg *)rreq, size);

    rreq->hcnt++; /* Increase hopcount to account for
                   * intermediate route */

    /* Send out on all interfaces */
    for (i = 0; i < MAX_NR_INTERFACES; i++) {
        if (!DEV_NR(i).enabled)
            continue;
        aodv_socket_send((AODV_msg *)rreq, dest, size, ttl, &DEV_NR(i));
    }
}

void NS_CLASS rreq_process(RREQ *rreq, int rreqlen, struct in_addr ip_src,
                           struct in_addr ip_dst, int ip_ttl,
                           unsigned int ifindex)
{

    AODV_ext *ext;
    RREP *rrep = NULL;
    int rrep_size = RREP_SIZE;
    rt_table_t *rev_rt, *fwd_rt = NULL;
    u_int32_t rreq_orig_seqno, rreq_dest_seqno;
    u_int32_t rreq_id, rreq_new_hcnt, life;
    unsigned int extlen = 0;
    struct in_addr rreq_dest, rreq_orig;

    rreq_dest.s_addr = rreq->dest_addr;
    rreq_orig.s_addr = rreq->orig_addr;
    rreq_id = ntohl(rreq->rreq_id);
    rreq_dest_seqno = ntohl(rreq->dest_seqno);
    rreq_orig_seqno = ntohl(rreq->orig_seqno);
    rreq_new_hcnt = rreq->hcnt + 1;

    /* Ignore RREQ's that originated from this node. Either we do this
       or we buffer our own sent RREQ's as we do with others we
       receive. */
    if (rreq_orig.s_addr == DEV_IFINDEX(ifindex).ipaddr.s_addr)
        return;

    DEBUG(LOG_DEBUG, 0, "ip_src=%s rreq_orig=%s rreq_dest=%s ttl=%d",
          ip_to_str(ip_src), ip_to_str(rreq_orig), ip_to_str(rreq_dest),
          ip_ttl);

    if (rreqlen < (int)RREQ_SIZE) {
        alog(LOG_WARNING, 0, __FUNCTION__,
             "IP data field too short (%u bytes)"
             "from %s to %s",
             rreqlen, ip_to_str(ip_src), ip_to_str(ip_dst));
        return;
    }

    /* Check if the previous hop of the RREQ is in the blacklist set. If
       it is, then ignore the RREQ. */
    if (rreq_blacklist_find(ip_src)) {
        DEBUG(LOG_DEBUG, 0, "prev hop of RREQ blacklisted, ignoring!");
        return;
    }

    /* Ignore already processed RREQs. */
    if (rreq_record_find(rreq_orig, rreq_id))
        return;

    /* Now buffer this RREQ so that we don't process a similar RREQ we
       get within PATH_DISCOVERY_TIME. */
    rreq_record_insert(rreq_orig, rreq_id);

    /* Determine whether there are any RREQ extensions */
    ext = (AODV_ext *)((char *)rreq + RREQ_SIZE);

    while ((rreqlen - extlen) > RREQ_SIZE) {
        switch (ext->type) {
        case RREQ_EXT:
            DEBUG(LOG_INFO, 0, "RREQ include EXTENSION");
            /* Do something here */
            break;
        default:
            alog(LOG_WARNING, 0, __FUNCTION__, "Unknown extension type %d",
                 ext->type);
            break;
        }
        extlen += AODV_EXT_SIZE(ext);
        ext = AODV_EXT_NEXT(ext);
    }
#ifdef DEBUG_OUTPUT
    log_pkt_fields((AODV_msg *)rreq);
#endif

    /* The node always creates or updates a REVERSE ROUTE entry to the
       source of the RREQ. */
    rev_rt = rt_table_find(rreq_orig);

    /* Calculate the extended minimal life time. */
    life = PATH_DISCOVERY_TIME - 2 * rreq_new_hcnt * NODE_TRAVERSAL_TIME;

    if (rev_rt == NULL) {
        DEBUG(LOG_DEBUG, 0, "Creating REVERSE route entry, RREQ orig: %s",
              ip_to_str(rreq_orig));

        rev_rt = rt_table_insert(rreq_orig, ip_src, rreq_new_hcnt,
                                 rreq_orig_seqno, life, VALID, 0, ifindex);
    } else {
        if (rev_rt->dest_seqno == 0 ||
            (int32_t)rreq_orig_seqno > (int32_t)rev_rt->dest_seqno ||
            (rreq_orig_seqno == rev_rt->dest_seqno &&
             (rev_rt->state == INVALID || rreq_new_hcnt < rev_rt->hcnt))) {
            rev_rt =
                rt_table_update(rev_rt, ip_src, rreq_new_hcnt, rreq_orig_seqno,
                                life, VALID, rev_rt->flags);
        }
#ifdef DISABLED
        /* This is a out of draft modification of AODV-UU to prevent
           nodes from creating routing entries to themselves during
           the RREP phase. We simple drop the RREQ if there is a
           missmatch between the reverse path on the node and the one
           suggested by the RREQ. */

        else if (rev_rt->next_hop.s_addr != ip_src.s_addr) {
            DEBUG(LOG_DEBUG, 0, "Dropping RREQ due to reverse route mismatch!");
            return;
        }
#endif
    }
    /**** END updating/creating REVERSE route ****/

#ifdef CONFIG_GATEWAY
    /* This is a gateway */
    if (internet_gw_mode) {
        /* Subnet locality decision */
        switch (locality(rreq_dest, ifindex)) {
        case HOST_ADHOC:
            break;
        case HOST_INET:
            /* We must increase the gw's sequence number before sending a RREP,
             * otherwise intermediate nodes will not forward the RREP. */
            seqno_incr(this_host.seqno);
            rrep = rrep_create(0, 0, 0, DEV_IFINDEX(rev_rt->ifindex).ipaddr,
                               this_host.seqno, rev_rt->dest_addr,
                               ACTIVE_ROUTE_TIMEOUT);

            ext = rrep_add_ext(rrep, RREP_INET_DEST_EXT, rrep_size,
                               sizeof(struct in_addr), (char *)&rreq_dest);

            rrep_size += AODV_EXT_SIZE(ext);

            DEBUG(LOG_DEBUG, 0, "Responding for INTERNET dest: %s rrep_size=%d",
                  ip_to_str(rreq_dest), rrep_size);

            rrep_send(rrep, rev_rt, NULL, rrep_size);

            return;

        case HOST_UNKNOWN:
        default:
            DEBUG(LOG_DEBUG, 0, "GW: Destination unkown");
        }
    }
#endif
    /* Are we the destination of the RREQ?, if so we should immediately send a
       RREP.. */
    if (rreq_dest.s_addr == DEV_IFINDEX(ifindex).ipaddr.s_addr) {

        /* WE are the RREQ DESTINATION. Update the node's own
           sequence number to the maximum of the current seqno and the
           one in the RREQ. */
        if (rreq_dest_seqno != 0) {
            if ((int32_t)this_host.seqno < (int32_t)rreq_dest_seqno)
                this_host.seqno = rreq_dest_seqno;
            else if (this_host.seqno == rreq_dest_seqno)
                seqno_incr(this_host.seqno);
        }
        rrep =
            rrep_create(0, 0, 0, DEV_IFINDEX(rev_rt->ifindex).ipaddr,
                        this_host.seqno, rev_rt->dest_addr, MY_ROUTE_TIMEOUT);

        rrep_send(rrep, rev_rt, NULL, RREP_SIZE);

    } else {
        /* We are an INTERMEDIATE node. - check if we have an active
         * route entry */

        fwd_rt = rt_table_find(rreq_dest);

        if (fwd_rt && fwd_rt->state == VALID && !rreq->d) {
            struct timeval now;
            u_int32_t lifetime;

            /* GENERATE RREP, i.e we have an ACTIVE route entry that is fresh
               enough (our destination sequence number for that route is
               larger than the one in the RREQ). */

            gettimeofday(&now, NULL);
#ifdef CONFIG_GATEWAY_DISABLED
            if (fwd_rt->flags & RT_INET_DEST) {
                rt_table_t *gw_rt;
                /* This node knows that this is a rreq for an Internet
                 * destination and it has a valid route to the gateway */

                goto forward; // DISABLED

                gw_rt = rt_table_find(fwd_rt->next_hop);

                if (!gw_rt || gw_rt->state == INVALID)
                    goto forward;

                lifetime = timeval_diff(&gw_rt->rt_timer.timeout, &now);

                rrep =
                    rrep_create(0, 0, gw_rt->hcnt, gw_rt->dest_addr,
                                gw_rt->dest_seqno, rev_rt->dest_addr, lifetime);

                ext = rrep_add_ext(rrep, RREP_INET_DEST_EXT, rrep_size,
                                   sizeof(struct in_addr), (char *)&rreq_dest);

                rrep_size += AODV_EXT_SIZE(ext);

                DEBUG(LOG_DEBUG, 0,
                      "Intermediate node response for INTERNET dest: %s "
                      "rrep_size=%d",
                      ip_to_str(rreq_dest), rrep_size);

                rrep_send(rrep, rev_rt, gw_rt, rrep_size);
                return;
            }
#endif /* CONFIG_GATEWAY_DISABLED */

            /* Respond only if the sequence number is fresh enough... */
            if (fwd_rt->dest_seqno != 0 &&
                (int32_t)fwd_rt->dest_seqno >= (int32_t)rreq_dest_seqno) {
                lifetime = timeval_diff(&fwd_rt->rt_timer.timeout, &now);
                rrep = rrep_create(0, 0, fwd_rt->hcnt, fwd_rt->dest_addr,
                                   fwd_rt->dest_seqno, rev_rt->dest_addr,
                                   lifetime);
                rrep_send(rrep, rev_rt, fwd_rt, rrep_size);
            } else {
                goto forward;
            }
            /* If the GRATUITOUS flag is set, we must also unicast a
               gratuitous RREP to the destination. */
            if (rreq->g) {
                rrep = rrep_create(0, 0, rev_rt->hcnt, rev_rt->dest_addr,
                                   rev_rt->dest_seqno, fwd_rt->dest_addr,
                                   lifetime);

                rrep_send(rrep, fwd_rt, rev_rt, RREP_SIZE);

                DEBUG(LOG_INFO, 0, "Sending G-RREP to %s with rte to %s",
                      ip_to_str(rreq_dest), ip_to_str(rreq_orig));
            }
            return;
        }
    forward:
        if (ip_ttl > 1) {
            /* Update the sequence number in case the maintained one is
             * larger */
            if (fwd_rt && !(fwd_rt->flags & RT_INET_DEST) &&
                (int32_t)fwd_rt->dest_seqno > (int32_t)rreq_dest_seqno)
                rreq->dest_seqno = htonl(fwd_rt->dest_seqno);

            rreq_forward(rreq, rreqlen, --ip_ttl);

        } else {
            DEBUG(LOG_DEBUG, 0, "RREQ not forwarded - ttl=0");
        }
    }
}

/* Perform route discovery for a unicast destination */

void NS_CLASS rreq_route_discovery(struct in_addr dest_addr, u_int8_t flags,
                                   struct ip_data *ipd)
{
    struct timeval now;
    rt_table_t *rt;
    seek_list_t *seek_entry;
    u_int32_t dest_seqno;
    int ttl;
#define TTL_VALUE ttl

    gettimeofday(&now, NULL);

    if (seek_list_find(dest_addr))
        return;

    /* If we already have a route entry, we use information from it. */
    rt = rt_table_find(dest_addr);

    ttl = NET_DIAMETER; /* This is the TTL if we don't use expanding
                           ring search */
    if (!rt) {
        dest_seqno = 0;

        if (expanding_ring_search)
            ttl = TTL_START;

    } else {
        dest_seqno = rt->dest_seqno;

        if (expanding_ring_search) {
            ttl = rt->hcnt + TTL_INCREMENT;
        }

        /* 	if (rt->flags & RT_INET_DEST) */
        /* 	    flags |= RREQ_DEST_ONLY; */

        /* A routing table entry waiting for a RREP should not be expunged
           before 2 * NET_TRAVERSAL_TIME... */
        if (timeval_diff(&rt->rt_timer.timeout, &now) <
            (2 * NET_TRAVERSAL_TIME))
            rt_table_update_timeout(rt, 2 * NET_TRAVERSAL_TIME);
    }

    rreq_send(dest_addr, dest_seqno, ttl, flags);

    /* Remember that we are seeking this destination */
    seek_entry = seek_list_insert(dest_addr, dest_seqno, ttl, flags, ipd);

    /* Set a timer for this RREQ */
    if (expanding_ring_search)
        timer_set_timeout(&seek_entry->seek_timer, RING_TRAVERSAL_TIME);
    else
        timer_set_timeout(&seek_entry->seek_timer, NET_TRAVERSAL_TIME);

    DEBUG(LOG_DEBUG, 0, "Seeking %s ttl=%d", ip_to_str(dest_addr), ttl);

    return;
}

/* Local repair is very similar to route discovery... */
void NS_CLASS rreq_local_repair(rt_table_t *rt, struct in_addr src_addr,
                                struct ip_data *ipd)
{
    struct timeval now;
    seek_list_t *seek_entry;
    rt_table_t *src_entry;
    int ttl;
    u_int8_t flags = 0;

    if (!rt)
        return;

    if (seek_list_find(rt->dest_addr))
        return;

    if (!(rt->flags & RT_REPAIR))
        return;

    gettimeofday(&now, NULL);

    DEBUG(LOG_DEBUG, 0, "REPAIRING route to %s", ip_to_str(rt->dest_addr));

    /* Caclulate the initial ttl to use for the RREQ. MIN_REPAIR_TTL
       mentioned in the draft is the last known hop count to the
       destination. */

    src_entry = rt_table_find(src_addr);

    if (src_entry)
        ttl = (int)(max(rt->hcnt, 0.5 * src_entry->hcnt) + LOCAL_ADD_TTL);
    else
        ttl = rt->hcnt + LOCAL_ADD_TTL;

    DEBUG(LOG_DEBUG, 0, "%s, rreq ttl=%d, dest_hcnt=%d",
          ip_to_str(rt->dest_addr), ttl, rt->hcnt);

    /* Reset the timeout handler, was probably previously
       local_repair_timeout */
    rt->rt_timer.handler = &NS_CLASS route_expire_timeout;

    if (timeval_diff(&rt->rt_timer.timeout, &now) < (2 * NET_TRAVERSAL_TIME))
        rt_table_update_timeout(rt, 2 * NET_TRAVERSAL_TIME);

    rreq_send(rt->dest_addr, rt->dest_seqno, ttl, flags);

    /* Remember that we are seeking this destination and setup the
       timers */
    seek_entry =
        seek_list_insert(rt->dest_addr, rt->dest_seqno, ttl, flags, ipd);

    if (expanding_ring_search)
        timer_set_timeout(&seek_entry->seek_timer,
                          2 * ttl * NODE_TRAVERSAL_TIME);
    else
        timer_set_timeout(&seek_entry->seek_timer, NET_TRAVERSAL_TIME);

    DEBUG(LOG_DEBUG, 0, "Seeking %s ttl=%d", ip_to_str(rt->dest_addr), ttl);

    return;
}

NS_STATIC struct rreq_record *NS_CLASS
rreq_record_insert(struct in_addr orig_addr, u_int32_t rreq_id)
{
    struct rreq_record *rec;

    /* First check if this rreq packet is already buffered */
    rec = rreq_record_find(orig_addr, rreq_id);

    /* If already buffered, should we update the timer???  */
    if (rec)
        return rec;

    if ((rec = (struct rreq_record *)malloc(sizeof(struct rreq_record))) ==
        NULL) {
        fprintf(stderr, "Malloc failed!!!\n");
        exit(-1);
    }
    rec->orig_addr = orig_addr;
    rec->rreq_id = rreq_id;

    timer_init(&rec->rec_timer, &NS_CLASS rreq_record_timeout, rec);

    list_add(&rreq_records, &rec->l);

    DEBUG(LOG_INFO, 0, "Buffering RREQ %s rreq_id=%lu time=%u",
          ip_to_str(orig_addr), rreq_id, PATH_DISCOVERY_TIME);

    timer_set_timeout(&rec->rec_timer, PATH_DISCOVERY_TIME);
    return rec;
}

NS_STATIC struct rreq_record *NS_CLASS
rreq_record_find(struct in_addr orig_addr, u_int32_t rreq_id)
{
    list_t *pos;

    list_foreach(pos, &rreq_records)
    {
        struct rreq_record *rec = (struct rreq_record *)pos;
        if (rec->orig_addr.s_addr == orig_addr.s_addr &&
            (rec->rreq_id == rreq_id))
            return rec;
    }
    return NULL;
}

void NS_CLASS rreq_record_timeout(void *arg)
{
    struct rreq_record *rec = (struct rreq_record *)arg;

    list_detach(&rec->l);
    free(rec);
}

struct blacklist *NS_CLASS rreq_blacklist_insert(struct in_addr dest_addr)
{

    struct blacklist *bl;

    /* First check if this rreq packet is already buffered */
    bl = rreq_blacklist_find(dest_addr);

    /* If already buffered, should we update the timer??? */
    if (bl)
        return bl;

    if ((bl = (struct blacklist *)malloc(sizeof(struct blacklist))) == NULL) {
        fprintf(stderr, "Malloc failed!!!\n");
        exit(-1);
    }
    bl->dest_addr.s_addr = dest_addr.s_addr;

    timer_init(&bl->bl_timer, &NS_CLASS rreq_blacklist_timeout, bl);

    list_add(&rreq_blacklist, &bl->l);

    timer_set_timeout(&bl->bl_timer, BLACKLIST_TIMEOUT);
    return bl;
}

struct blacklist *NS_CLASS rreq_blacklist_find(struct in_addr dest_addr)
{
    list_t *pos;

    list_foreach(pos, &rreq_blacklist)
    {
        struct blacklist *bl = (struct blacklist *)pos;

        if (bl->dest_addr.s_addr == dest_addr.s_addr)
            return bl;
    }
    return NULL;
}

void NS_CLASS rreq_blacklist_timeout(void *arg)
{

    struct blacklist *bl = (struct blacklist *)arg;

    list_detach(&bl->l);
    free(bl);
}
