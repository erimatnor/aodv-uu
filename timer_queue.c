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
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>

#include "timer_queue.h"
#include "defs.h"
#include "debug.h"

static struct timer *TQ;
static int timer_set_alarm();
//#define DEBUG_TIMER_QUEUE
#ifdef DEBUG_TIMER_QUEUE
static void printTQ();
#endif

void timer_queue_init()
{
    TQ = NULL;
    /* Register signal handler for SIGALRM */
    signal(SIGALRM, timer_timeout);
}

/* Called when a timer should timeout */
void timer_timeout()
{
    struct timer *prev, *expiredTQ, *tmp;
    struct timeval now;

    gettimeofday(&now, NULL);
#ifdef DEBUG_TIMER_QUEUE
    printf("timer_timeout: called!!\n");
    printTQ();
#endif

    expiredTQ = TQ;

    for (prev = NULL; TQ != NULL; prev = TQ, TQ = TQ->next) {
	/* Check if the current timer should not expire yet... */
	if (timeval_diff(&TQ->timeout, &now) > 0) {
	    if (prev == NULL)
		/* No timers have expired yet... */
		goto end;

	    prev->next = NULL;
	    break;
	}
    }

    while (expiredTQ) {
	tmp = expiredTQ;
	expiredTQ->used = 0;
	expiredTQ = expiredTQ->next;
	tmp->next = NULL;
#ifdef DEBUG_TIMER_QUEUE
	printf("timer_timeout: removing timer\n");
#endif
	/* Execute handler function for expired timer... */
	if (tmp->handler)
	    tmp->handler(tmp->data);
    }

    /* Schedule a new timeout... */
  end:
    timer_set_alarm();
}

static void timer_add(struct timer *t)
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

    printf("timer_add: New timer added!\n");
#endif

    /* Base case when queue is empty: */
    if (!TQ) {
	TQ = t;
	t->next = NULL;
	/* Since we insert first in the queue we must reschedule the
	   first timeout */
	timer_set_alarm();
	goto end;
    }

    for (prev = NULL, curr = TQ; curr != NULL;
	 prev = curr, curr = curr->next) {
	if (timeval_diff(&t->timeout, &curr->timeout) < 0) {
	    /* OK, we found the spot to insert the timer */
	    if (prev == NULL) {
		/* We insert first in queue */
		TQ = t;
		t->next = curr;

		/* Since we insert first in the queue we must reschedule the
		   first timeout */
		timer_set_alarm();
		goto end;
	    }
	    t->next = curr;
	    prev->next = t;
	    goto end;
	}
    }
    /* We insert last in queue */
    prev->next = t;
    t->next = NULL;

  end:
#ifdef DEBUG_TIMER_QUEUE
    printTQ();
#endif
    return;
}

void timer_add_msec(struct timer *t, long msec)
{
    gettimeofday(&t->timeout, NULL);

    t->timeout.tv_usec += msec * 1000;
    t->timeout.tv_sec += t->timeout.tv_usec / 1000000;
    t->timeout.tv_usec = t->timeout.tv_usec % 1000000;

    timer_add(t);
}

int timer_timeout_now(struct timer *t)
{

    struct timer *curr, *prev;

    if (!t)
	return -1;

    t->used = 0;

    for (prev = NULL, curr = TQ; curr != NULL;
	 prev = curr, curr = curr->next) {
	if (curr == t) {
	    /* Decouple timer from queue */
	    if (prev == NULL) {
		TQ = curr->next;
		/* Update alarm since this was the first in queue */
		timer_set_alarm();
	    } else
		prev->next = curr->next;

	    /* Call handler function */
	    curr->handler(curr->data);
	    curr->next = NULL;
	    /*  if (curr->data) */
	    /*        free (curr->data); */

	    return 0;
	}
    }
    /* We didn't find the timer... */
    return -1;
}

long timer_left(struct timer *t)
{
    struct timeval now;

    if (!t)
	return -1;

    gettimeofday(&now, NULL);

    return timeval_diff(&now, &t->timeout);
}

int timer_remove(struct timer *t)
{
    struct timer *curr, *prev;

    if (!t)
	return -1;

    if(t->used == 0)
	return 0;
    
    t->used = 0;

    for (prev = NULL, curr = TQ; curr != NULL;
	 prev = curr, curr = curr->next) {
	if (curr == t) {
	    if (prev == NULL) {
		TQ = curr->next;
		/* Update the alarm */
		timer_set_alarm();
	    } else
		prev->next = curr->next;

	    curr->next = NULL;

	    return 0;
	}
    }
    /* We didn't find the timer... */
    return -1;
}

/*
 * This function sets a timer to generate a SIGALRM when the first event
 * in the queue should happen.
 */
int timer_set_alarm()
{
    struct itimerval tv;
    struct timeval now;
    long wait;

    if (!TQ)
	return -1;

    gettimeofday(&now, NULL);

    wait = timeval_diff(&TQ->timeout, &now);

    /* For a very special case... ;-) */
    if (wait <= 0) {
	/*  printf("Wait is %ld!!!\n", wait); */
	wait = 10;
    }
#ifdef DEBUG_TIMER_QUEUE
    printf("timer_set_alarm: Timer set to erupt in %ld usecs\n",
	   timeval_diff(&TQ->timeout, &now));
#endif

    tv.it_value.tv_usec = wait % 1000000;
    tv.it_value.tv_sec = wait / 1000000;
    tv.it_interval.tv_sec = 0;
    tv.it_interval.tv_usec = 0;

    /* Set timer alarm */
    if (setitimer(ITIMER_REAL, &tv, NULL) < 0) {
	fprintf(stderr, "Could not set timer!\n");
	return -1;
    }

    return 0;
}

#ifdef DEBUG_TIMER_QUEUE
void printTQ()
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
