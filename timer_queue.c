/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University and Ericsson Telecom AB.
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
#include <signal.h>
#include <sys/time.h>

#include "timer_queue.h"
#include "defs.h"
#include "debug.h"

static u_int32_t id; /* This id is incremented for each new event, so
                      that they get a unique id... */
static struct timer *TQ;
int timer_fds[2]; /* File descriptors for pipe we use */
static int timer_set_alarm();

/* A set of signals we want to block with timer_block() */
static sigset_t blockset, oldset;

#ifdef DEBUG_TIMER_QUEUE
static void printTQ();
#endif


/* Some helper functions: */
u_int64_t get_currtime()
{
  struct timeval tv;
  
  if (gettimeofday(&tv, NULL) < 0)
    /* Couldn't get time of day */
    return -1;
  
  return ((u_int64_t)tv.tv_sec) * 1000 + ((u_int64_t)tv.tv_usec) / 1000;
}

u_int64_t timeval_to_msecs(struct timeval tv) {
  return ((u_int64_t)tv.tv_sec) * 1000 + ((u_int64_t)tv.tv_usec) / 1000;
}
void timer_block() {
   /* Initialize the set of signals we want to block with timer_block() */
  sigemptyset(&blockset);
  sigaddset(&blockset, SIGALRM);
  sigprocmask(SIG_BLOCK, &blockset, &oldset);
  /*  log(LOG_INFO, 0, "timer_block() called!"); */
}

void timer_unblock() {
  sigprocmask(SIG_SETMASK, &oldset, NULL);
  /*  log(LOG_INFO, 0, "timer_unblock() called!"); */
}


void timer_queue_init() {
 /*   struct sigaction sa; */
  TQ = NULL;
  id = 1; /* Timer ID starts at 1, 0 means expired timer... */
 
  /* Register signal handler for SIGALRM */
  signal(SIGALRM, timer_timeout);
}


/* Called when a timer should timeout */
void timer_timeout() {
  register struct timer *curr, *prev, *expiredTQ;
  register u_int64_t now;
  
  now = get_currtime();
#ifdef DEBUG_TIMER_QUEUE
  printf("timer_timeout(): called at %lu!!\n",now);
#endif
  expiredTQ = TQ;
  prev = NULL;

  for(; TQ != NULL; TQ = TQ->next) {

#ifdef DEBUG_TIMER_QUEUE
    printf("timer_timeout(): Timer %u expires at %lu (left=%ld)\n", 
	  TQ->id, TQ->expire, TQ->expire - now); 
#endif
    /* Check if the current timer should not expire yet... */
    if(TQ->expire > now) {
      if(prev == NULL) {
	/* No timers have expired yet... */
	goto end;
      }
      prev->next = NULL;
      break;
    }    
    prev = TQ;
  }
  
  while(expiredTQ) {
    curr = expiredTQ;
    /* Execute handler function for expired timer... */
    if (curr->handler) {
      curr->handler(curr->data);
    }
    
    expiredTQ = expiredTQ->next;
   
    /* Free memory occupied by expired timer. */
#ifdef DEBUG_TIMER_QUEUE
    printf("timer_timeout(): removing timer %u\n", curr->id);
#endif
    free(curr);
  }
  
  /* Schedule a new timeout... */
 end:
  timer_set_alarm();
#ifdef DEBUG_TIMER_QUEUE
  printTQ();
#endif
}


u_int32_t timer_new(u_int64_t delay, timeout_func_t handler, void *data) {
  struct timer *new_timer, *curr, *prev;
  u_int64_t now = get_currtime();

  /* Sanity check: */
  if(handler == NULL) {
    perror("Handler function pointer was NULL!!!\n");
    exit(-1);
  }
  
  if((new_timer = malloc(sizeof(struct timer))) < 0) {
    perror("Failed to allocate new timer!!!\n");
    exit(-1);
  }
  
  new_timer->next = NULL;
  new_timer->expire = now + delay;
  new_timer->handler = handler;
  new_timer->data = data;
  new_timer->id = id++;
  
#ifdef DEBUG_TIMER_QUEUE
  printf("timer_new(): New timer with id %u should errupt in %lu msecs\n", 
	 new_timer->id, delay);
#endif

  /* Base case when queue is empty: */
  if(TQ == NULL) {
    TQ = new_timer;
    /* Since we insert first in the queue we must reschedule the
       first timeout */
    timer_set_alarm();
    goto end;
  }
  
  prev = NULL;
  
  for(curr = TQ; curr != NULL; curr = curr->next) {
    if((delay + now) < curr->expire) {
      /* OK, we found the spot to insert the timer */
      if(prev == NULL) {
	/* We insert first in queue */
	TQ = new_timer;
	new_timer->next = curr;
	
	/* Since we insert first in the queue we must reschedule the
           first timeout */
	timer_set_alarm();
	goto end;
      }
      new_timer->next = curr;
      prev->next = new_timer;
      goto end;
    }
    prev = curr;
  }
  /* We insert last in queue */
  prev->next = new_timer;
 
 end:
#ifdef DEBUG_TIMER_QUEUE
  printTQ();
#endif
  return new_timer->id;
}

int timer_timeout_now(u_int32_t timer_id) {
  
  struct timer *curr, *prev;

  if (!timer_id)
    return -1;

  curr = TQ;
  prev = NULL;
  for(curr = TQ; curr != NULL; curr = curr->next) {
    
    if (curr->id == timer_id) {
      /* Decouple timer from queue */
      if(prev == NULL) {
	TQ = curr->next;
	/* Update alarm since this was the first in queue */
	timer_set_alarm();
      }
      else
	prev->next = curr->next;
    
      /* Call handler function */
      curr->handler(curr->data);
      
      if(curr->data)
	free(curr->data);
      free(curr);
      return 0;
    }
    prev = curr;
  }
  /* We didn't find the timer... */
  return -1;
}

/* finds the indicated timer and returns the data contained */
struct timer *timer_find(u_int32_t timer_id) {
  struct timer *t;
  
  if (!timer_id)
    return NULL;

  t = TQ;
  
  while (t) {
    if (t->id == timer_id) {
      return t;
    }
    t = t->next;
  }
  return NULL;
}


u_int64_t timer_left(u_int32_t timer_id) {
  struct timer *t;
  
  if (!timer_id)
    return -1;

  t = TQ;
  
  while (t) {
    if (t->id == timer_id) {
      return (t->expire - get_currtime());
    }
    t = t->next;
  }
  return -1;
}

int timer_remove(u_int32_t timer_id) {
  struct timer *curr, *prev;

  if (!timer_id)
    return -1;

  curr = TQ;
  prev = NULL;
  for(curr = TQ; curr != NULL; curr = curr->next) {
    
    if (curr->id == timer_id) {
      if(prev == NULL) {
	TQ = curr->next;
	/* Update the alarm */
	timer_set_alarm();
      } else
	prev->next = curr->next;
      
      /*  if(curr->data) */
      /*  	free(curr->data); */
      free(curr);
      return 0;
    }
    prev = curr;
  }
  /* We didn't find the timer... */
  return -1;
}

u_int64_t timer_next_timeout() {

  if(TQ) 
    return TQ->expire;
  
  return 0;
}
/*
 * This function sets a timer to generate a SIGALRM when the first event
 * in the queue should happen.
 */
int timer_set_alarm() {
  struct itimerval tv;
  register u_int64_t expire; /* Remaining time to next event in msecs */
  register long remaining;
  /* Get the time until the first event in the queue should happen. */
  expire = timer_next_timeout();
  
  /* Check that the queue really held an event... */
  if(expire == 0) {
    printf("No timer in queue!\n");
    return 0;
  } 
  
  remaining = expire - get_currtime();
  
  /* For a very special case... ;-) */
  if(remaining <= 0) {
    timer_timeout();
    return 0;
  }
    
  /* Convert the time in milliseconds to timeval format... */
  tv.it_value.tv_sec = remaining / 1000;
  tv.it_value.tv_usec = (remaining % 1000) * 1000;
  tv.it_interval.tv_sec = 0;
  tv.it_interval.tv_usec = 0;

#ifdef DEBUG_TIMER_QUEUE
  printf("timer_set_alarm(): Timer set to erupt in %ld millisecond(s)\n", 
	 remaining);
#endif
  
  /* Set timer alarm */
  if (setitimer(ITIMER_REAL, &tv, NULL) < 0) {
   fprintf(stderr, "Could not set timer!\n");
   return -1;
  } 
  
  return 0;
}

#ifdef DEBUG_TIMER_QUEUE
void printTQ() {
  struct timer *t;
  int n = 0;

  printf("%-4s %-12s %-12s %-4s\n", "id", "expires", "left", "n");
  for(t = TQ; t != NULL; t = t->next) {
    printf("%-4u %-12lu %-12ld %-4d\n", 
	   t->id, t->expire, (t->expire - get_currtime()), n);
    n++;
  }
}
#endif
