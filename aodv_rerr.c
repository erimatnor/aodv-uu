/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University & Ericsson AB.
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
#include "aodv_rerr.h"
#include "aodv_socket.h"
#include "aodv_timeout.h"
#include "debug.h"
#include "defs.h"
#include "params.h"
#include "routing_table.h"
#include <netinet/in.h>

#endif

RERR *NS_CLASS rerr_create(u_int8_t flags, struct in_addr dest_addr,
                           u_int32_t dest_seqno)
{
    RERR *rerr;

    DEBUG(LOG_DEBUG, 0, "Assembling RERR about %s seqno=%d",
          ip_to_str(dest_addr), dest_seqno);

    rerr = (RERR *)aodv_socket_new_msg();
    rerr->type = AODV_RERR;
    rerr->n = (flags & RERR_NODELETE ? 1 : 0);
    rerr->res1 = 0;
    rerr->res2 = 0;
    rerr->dest_addr = dest_addr.s_addr;
    rerr->dest_seqno = htonl(dest_seqno);
    rerr->dest_count = 1;

    return rerr;
}

void NS_CLASS rerr_add_udest(RERR *rerr, struct in_addr udest,
                             u_int32_t udest_seqno)
{
    RERR_udest *ud;

    ud = (RERR_udest *)((char *)rerr + RERR_CALC_SIZE(rerr));
    ud->dest_addr = udest.s_addr;
    ud->dest_seqno = htonl(udest_seqno);
    rerr->dest_count++;
}

void NS_CLASS rerr_process(RERR *rerr, int rerrlen, struct in_addr ip_src,
                           struct in_addr ip_dst)
{
    RERR *new_rerr = NULL;
    RERR_udest *udest;
    rt_table_t *rt;
    u_int32_t rerr_dest_seqno;
    struct in_addr udest_addr, rerr_unicast_dest;
    int i;

    rerr_unicast_dest.s_addr = 0;

    DEBUG(LOG_DEBUG, 0, "ip_src=%s", ip_to_str(ip_src));

    log_pkt_fields((AODV_msg *)rerr);

    if (rerrlen < ((int)RERR_CALC_SIZE(rerr))) {
        alog(LOG_WARNING, 0, __FUNCTION__,
             "IP data too short (%u bytes) from %s to %s. Should be %d bytes.",
             rerrlen, ip_to_str(ip_src), ip_to_str(ip_dst),
             RERR_CALC_SIZE(rerr));

        return;
    }

    /* Check which destinations that are unreachable.  */
    udest = RERR_UDEST_FIRST(rerr);

    while (rerr->dest_count) {

        udest_addr.s_addr = udest->dest_addr;
        rerr_dest_seqno = ntohl(udest->dest_seqno);
        DEBUG(LOG_DEBUG, 0, "unreachable dest=%s seqno=%lu",
              ip_to_str(udest_addr), rerr_dest_seqno);

        rt = rt_table_find(udest_addr);

        if (rt && rt->state == VALID && rt->next_hop.s_addr == ip_src.s_addr) {

            /* Checking sequence numbers here is an out of draft
             * addition to AODV-UU. It is here because it makes a lot
             * of sense... */
            if (0 && (int32_t)rt->dest_seqno > (int32_t)rerr_dest_seqno) {
                DEBUG(LOG_DEBUG, 0, "Udest ignored because of seqno");
                udest = RERR_UDEST_NEXT(udest);
                rerr->dest_count--;
                continue;
            }
            DEBUG(LOG_DEBUG, 0, "removing rte %s - WAS IN RERR!!",
                  ip_to_str(udest_addr));

#ifdef NS_PORT
            interfaceQueue((nsaddr_t)udest_addr.s_addr, IFQ_DROP_BY_DEST);
#endif
            /* Invalidate route: */
            if (!rerr->n) {
                rt_table_invalidate(rt);
            }
            /* (a) updates the corresponding destination sequence number
               with the Destination Sequence Number in the packet, and */
            rt->dest_seqno = rerr_dest_seqno;

            /* (d) check precursor list for emptiness. If not empty, include
               the destination as an unreachable destination in the
               RERR... */
            if (rt->nprec && !(rt->flags & RT_REPAIR)) {

                if (!new_rerr) {
                    u_int8_t flags = 0;

                    if (rerr->n)
                        flags |= RERR_NODELETE;

                    new_rerr =
                        rerr_create(flags, rt->dest_addr, rt->dest_seqno);
                    DEBUG(LOG_DEBUG, 0, "Added %s as unreachable, seqno=%lu",
                          ip_to_str(rt->dest_addr), rt->dest_seqno);

                    if (rt->nprec == 1)
                        rerr_unicast_dest =
                            FIRST_PREC(rt->precursors)->neighbor;

                } else {
                    /* Decide whether new precursors make this a non unicast
                     * RERR */
                    rerr_add_udest(new_rerr, rt->dest_addr, rt->dest_seqno);

                    DEBUG(LOG_DEBUG, 0, "Added %s as unreachable, seqno=%lu",
                          ip_to_str(rt->dest_addr), rt->dest_seqno);

                    if (rerr_unicast_dest.s_addr) {
                        list_t *pos2;
                        list_foreach(pos2, &rt->precursors)
                        {
                            precursor_t *pr = (precursor_t *)pos2;
                            if (pr->neighbor.s_addr !=
                                rerr_unicast_dest.s_addr) {
                                rerr_unicast_dest.s_addr = 0;
                                break;
                            }
                        }
                    }
                }
            } else {
                DEBUG(LOG_DEBUG, 0,
                      "Not sending RERR, no precursors or route in RT_REPAIR");
            }
            /* We should delete the precursor list for all unreachable
               destinations. */
            if (rt->state == INVALID)
                precursor_list_destroy(rt);
        } else {
            DEBUG(LOG_DEBUG, 0, "Ignoring UDEST %s", ip_to_str(udest_addr));
        }
        udest = RERR_UDEST_NEXT(udest);
        rerr->dest_count--;
    } /* End while() */

    /* If a RERR was created, then send it now... */
    if (new_rerr) {

        rt = rt_table_find(rerr_unicast_dest);

        if (rt && new_rerr->dest_count == 1 && rerr_unicast_dest.s_addr)
            aodv_socket_send((AODV_msg *)new_rerr, rerr_unicast_dest,
                             RERR_CALC_SIZE(new_rerr), 1,
                             &DEV_IFINDEX(rt->ifindex));

        else if (new_rerr->dest_count > 0) {
            /* FIXME: Should only transmit RERR on those interfaces
             * which have precursor nodes for the broken route */
            for (i = 0; i < MAX_NR_INTERFACES; i++) {
                struct in_addr dest;

                if (!DEV_NR(i).enabled)
                    continue;
                dest.s_addr = AODV_BROADCAST;
                aodv_socket_send((AODV_msg *)new_rerr, dest,
                                 RERR_CALC_SIZE(new_rerr), 1, &DEV_NR(i));
            }
        }
    }
}
