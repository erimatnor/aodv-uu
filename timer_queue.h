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
 *
 *****************************************************************************/
#ifndef TIMER_QUEUE_H
#define TIMER_QUEUE_H

#include <sys/time.h>

#include "defs.h"

typedef void (*timeout_func_t) (void *);
/* A set of signals we want to block with timer_block() */
static sigset_t blockset, oldset;


struct timer {
    struct timeval timeout;
    timeout_func_t handler;
    void *data;
    int used;
    struct timer *next;
};

static inline long timeval_diff(struct timeval *t1, struct timeval *t2)
{
    if (!t1 || !t2)
	return -1;
    else
	return (1000000 * (t1->tv_sec - t2->tv_sec) + t1->tv_usec -
		t2->tv_usec);
}

static inline int timeval_add_msec(struct timeval *t, long msec)
{
    if (!t)
	return -1;

    t->tv_usec += msec * 1000;
    t->tv_sec += t->tv_usec / 1000000;
    t->tv_usec = t->tv_usec % 1000000;
    return 0;
}

static inline void timer_block()
{
    /* Initialize the set of signals we want to block with timer_block() */
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGALRM);
    sigprocmask(SIG_BLOCK, &blockset, &oldset);
}
static inline void timer_unblock()
{
    sigprocmask(SIG_SETMASK, &oldset, NULL);
}

void timer_queue_init();
void timer_timeout();
void timer_add_msec(struct timer *t, long msec);
int timer_remove(struct timer *t);

#endif
