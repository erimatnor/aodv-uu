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
 *****************************************************************************/
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>

#ifdef NS_PORT
#include "aodv-uu.h"
#else
#include "timer_queue.h"
#include "defs.h"
#include "debug.h"

static struct timer *TQ;

/* #define DEBUG_TIMER_QUEUE  */

#ifdef DEBUG_TIMER_QUEUE
static void printTQ();
#endif
#endif				/* NS_PORT */

void NS_CLASS timer_queue_init()
{
    TQ = NULL;
}

/* Called when a timer should timeout */
void NS_CLASS timer_timeout(struct timeval *now)
{
    struct timer *prev, *expiredTQ, *tmp;

#ifdef DEBUG_TIMER_QUEUE
    printf("\n######## timer_timeout: called!!\n");
    printTQ();
#endif

    expiredTQ = TQ;

    for (prev = NULL; TQ; prev = TQ, TQ = TQ->next)
	if (timeval_diff(&TQ->timeout, now) > 0)
	    break;

    /* No expired timers? */
    if (prev == NULL)
	return;

    /* Decouple the expired timers from the rest of the queue. */
    prev->next = NULL;

    while (expiredTQ) {
	tmp = expiredTQ;
	tmp->used = 0;
	expiredTQ = expiredTQ->next;
#ifdef DEBUG_TIMER_QUEUE
	printf("removing timer\n");
#endif
	/* Execute handler function for expired timer... */
	if (tmp->handler) {
#ifdef NS_PORT
	    (*this.*tmp->handler) (tmp->data);
#else
	    tmp->handler(tmp->data);
#endif
	}
    }
}

NS_STATIC void NS_CLASS timer_add(struct timer *t)
{
    struct timer *curr, *prev;
    /* Sanity checks: */

    if (!t) {
	perror("NULL timer!!!\n");
	exit(-1);
    }
    if (!t->handler) {
	perror("NULL handler!!!\n");
	exit(-1);
    }

    /* Make sure we remove unexpired timers before adding a new timeout... */
    if (t->used)
	timer_remove(t);

    t->used = 1;

#ifdef DEBUG_TIMER_QUEUE
    printf("New timer added!\n");
#endif

    /* Base case when queue is empty: */
    if (!TQ) {
	TQ = t;
	t->next = NULL;
	/* Since we insert first in the queue we must reschedule the
	   first timeout */
	goto end;
    }

    for (prev = NULL, curr = TQ; curr; prev = curr, curr = curr->next)
	if (timeval_diff(&t->timeout, &curr->timeout) < 0)
	    break;

    if (curr == TQ) {
	/* We insert first in queue */
	t->next = TQ;
	TQ = t;
    } else {
	t->next = curr;
	prev->next = t;
    }

  end:
#ifdef DEBUG_TIMER_QUEUE
    printTQ();
#endif
    return;
}
int NS_CLASS timer_timeout_now(struct timer *t)
{

    struct timer *curr, *prev;

    if (!t)
	return -1;

    t->used = 0;

    for (prev = NULL, curr = TQ; curr; prev = curr, curr = curr->next) {
	if (curr == t) {
	    /* Decouple timer from queue */
	    if (prev == NULL)
		TQ = t->next;
	    else
		prev->next = t->next;

	    t->next = NULL;

#ifdef NS_PORT
	    (*this.*t->handler) (t->data);
#else
	    t->handler(t->data);
#endif
	    return 0;
	}
    }
    /* We didn't find the timer... */
    return -1;
}

int NS_CLASS timer_remove(struct timer *t)
{
    struct timer *curr, *prev;

    if (!t)
	return -1;

    t->used = 0;

    for (prev = NULL, curr = TQ; curr; prev = curr, curr = curr->next) {
	if (curr == t) {
	    if (prev == NULL)
		TQ = t->next;
	    else
		prev->next = t->next;

	    t->next = NULL;
	    return 0;
	}
    }
    /* We didn't find the timer... */
    return -1;
}

void NS_CLASS timer_set_timeout(struct timer *t, long msec)
{
    gettimeofday(&t->timeout, NULL);

    if (msec < 0)
	DEBUG(LOG_WARNING, 0, "Negative timeout!!!");

    t->timeout.tv_usec += msec * 1000;
    t->timeout.tv_sec += t->timeout.tv_usec / 1000000;
    t->timeout.tv_usec = t->timeout.tv_usec % 1000000;

    timer_add(t);
}

long timer_left(struct timer *t)
{
    struct timeval now;

    if (!t)
	return -1;

    gettimeofday(&now, NULL);

    return timeval_diff(&now, &t->timeout);
}
struct timeval *NS_CLASS timer_age_queue()
{
    struct timeval now;
    static struct timeval remaining;

    gettimeofday(&now, NULL);

    timer_timeout(&now);

    if (!TQ)
	return NULL;

    remaining.tv_usec = (TQ->timeout.tv_usec - now.tv_usec);
    remaining.tv_sec = (TQ->timeout.tv_sec - now.tv_sec);

    if (remaining.tv_usec < 0) {
	remaining.tv_usec += 1000000;
	remaining.tv_sec -= 1;
    }
    return (&remaining);
}


#ifdef DEBUG_TIMER_QUEUE
void NS_CLASS printTQ()
{
    struct timer *t;
    struct timeval now;
    int n = 0;

    gettimeofday(&now, NULL);

    printf("%-12s %-4s\n", "left", "n");
    for (t = TQ; t != NULL; t = t->next) {
	printf("%-12ld %-4d\n", timeval_diff(&t->timeout, &now), n);
	n++;
    }
}
#endif
