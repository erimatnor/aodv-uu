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
#ifndef AODV_SOCKET_H
#define AODV_SOCKET_H

#include <netinet/ip.h>

#include "defs.h"

#define IPHDR_SIZE sizeof(struct iphdr)

void aodv_socket_init();
void aodv_socket_send(AODV_msg *aodv_msg, u_int32_t dst, int len, u_int8_t ttl);
AODV_msg *aodv_socket_new_msg();
AODV_msg *aodv_socket_queue_msg(AODV_msg *aodv_msg, int size);
void aodv_socket_cleanup(void);

#endif
