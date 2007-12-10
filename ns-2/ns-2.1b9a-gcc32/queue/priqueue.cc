/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- */
/*
 * Copyright (c) 1997 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/* Ported from CMU/Monarch's code, nov'98 -Padma.*/

/* -*- c++ -*-
   priqueue.cc
   
   A simple priority queue with a remove packet function
   $Id: priqueue.cc 822 2002-11-19 14:58:17Z bjwi7937 $
   */

#include <object.h>
#include <queue.h>
#include <drop-tail.h>
#include <packet.h>
//#include <cmu/cmu-trace.h>

#include "priqueue.h"

typedef int (*PacketFilter)(Packet *, void *);

PriQueue_List PriQueue::prhead = { 0 };

static class PriQueueClass : public TclClass {
public:
  PriQueueClass() : TclClass("Queue/DropTail/PriQueue") {}
  TclObject* create(int, const char*const*) {
    return (new PriQueue);
  }
} class_PriQueue;


PriQueue::PriQueue() : DropTail()
{
        bind("Prefer_Routing_Protocols", &Prefer_Routing_Protocols);
	LIST_INSERT_HEAD(&prhead, this, link);
}

int
PriQueue::command(int argc, const char*const* argv)
{
  if (argc == 2 && strcasecmp(argv[1], "reset") == 0)
    {
      Terminate();
      //FALL-THROUGH to give parents a chance to reset
    }
  return DropTail::command(argc, argv);
}

void
PriQueue::recv(Packet *p, Handler *h)
{
        struct hdr_cmn *ch = HDR_CMN(p);

        if(Prefer_Routing_Protocols) {

                switch(ch->ptype()) {
		case PT_DSR:
		case PT_MESSAGE:
                case PT_TORA:
                case PT_AODV:
                        recvHighPriority(p, h);
                        break;

#ifdef AODV_UU
                case PT_AODVUU:
                        recvHighPriority(p, h);
                        break;
#endif /* AODV_UU */

                default:
                        Queue::recv(p, h);
                }
        }
        else {
                Queue::recv(p, h);
        }
}


void 
PriQueue::recvHighPriority(Packet *p, Handler *)
  // insert packet at front of queue
{
	q_->enqueHead(p);
	if (q_->length() >= qlim_)
    {
      Packet *to_drop = q_->lookup(q_->length()-1);
      q_->remove(to_drop);
      drop(to_drop);
    }
  
  if (!blocked_) {
    /*
     * We're not blocked.  Get a packet and send it on.
     * We perform an extra check because the queue
     * might drop the packet even if it was
     * previously empty!  (e.g., RED can do this.)
     */
    p = deque();
    if (p != 0) {
      blocked_ = 1;
      target_->recv(p, &qh_);
    }
  } 
}
 
void 
PriQueue::filter(PacketFilter filter, void * data)
  // apply filter to each packet in queue, 
  // - if filter returns 0 leave packet in queue
  // - if filter returns 1 remove packet from queue
{
  int i = 0;
  while (i < q_->length())
    {
      Packet *p = q_->lookup(i);
      if (filter(p,data))
	{
	  q_->remove(p); // decrements q len
	}
      else i++;
    }
}

Packet*
PriQueue::filter(nsaddr_t id)
{
	Packet *p = 0;
	Packet *pp = 0;
	struct hdr_cmn *ch;

	for(p = q_->head(); p; p = p->next_) {
		ch = HDR_CMN(p);
		if(ch->next_hop() == id)
			break;
		pp = p;
	}

	/*
	 * Deque Packet
	 */
	if(p) {
		if(pp == 0)
			q_->remove(p);
		else
			q_->remove(p, pp);
	}
	return p;
}

/*
 * Called at the end of the simulation to purge the IFQ.
 */
void
PriQueue::Terminate()
{
	Packet *p;
	while((p = deque())) {
		//drop(p, DROP_END_OF_SIMULATION);
		drop(p);
		
	}
}




