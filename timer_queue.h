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
#ifndef TIMER_QUEUE_H
#define TIMER_QUEUE_H

#include "defs.h"

typedef void (*timeout_func_t) (void *);

/*  #define DEBUG_TIMER_QUEUE */
struct timer {
  u_int64_t expire;  /* Time when timer expires. */
  u_int32_t id;       /* A unique id for this timer... */
  timeout_func_t handler; /* Function to call when the timer expires.. */
  void *data;	/* Data to pass to the handler function. */
  struct timer *next; /* The next timer */
};


u_int64_t get_currtime();
void timer_queue_init();
void timer_block();
void timer_unblock();
void timer_timeout();
u_int32_t timer_new(u_int64_t delay, timeout_func_t handler, void *data);
int timer_timeout_now(u_int32_t timer_id);
u_int64_t timer_left(u_int32_t timer_id);
struct timer *timer_find(u_int32_t timer_id);
int timer_remove(u_int32_t timer_id);
u_int64_t timer_next_timeout();
void timer_interrupt();

#endif
