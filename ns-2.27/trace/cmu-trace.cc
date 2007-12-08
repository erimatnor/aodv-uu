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
 *
 * Ported from CMU/Monarch's code, appropriate copyright applies.
 * nov'98 -Padma.
 *
 * $Header$
 */

#include <packet.h>
#include <ip.h>
#include <tcp.h>
#include <sctp.h>
#include <rtp.h>
#include <arp.h>
#include <dsr/hdr_sr.h>	// DSR
#include <mac.h>
#include <mac-802_11.h>
#include <smac.h>
#include <address.h>
#include <tora/tora_packet.h> //TORA
#include <imep/imep_spec.h>         // IMEP
#include <aodv/aodv_packet.h> //AODV
#ifdef AODV_UU
#include <aodv-uu/aodv-uu.h> // AODV-UU
#endif /* AODV_UU */
#include <cmu-trace.h>
#include <mobilenode.h>
#include <simulator.h>

#include "diffusion/diff_header.h" // DIFFUSION -- Chalermek


//#define LOG_POSITION

//extern char* pt_names[];

static class CMUTraceClass : public TclClass {
public:
	CMUTraceClass() : TclClass("CMUTrace") { }
	TclObject* create(int, const char*const* argv) {
		return (new CMUTrace(argv[4], *argv[5]));
	}
} cmutrace_class;


CMUTrace::CMUTrace(const char *s, char t) : Trace(t)
{
	bzero(tracename, sizeof(tracename));
	strncpy(tracename, s, MAX_ID_LEN);

        if(strcmp(tracename, "RTR") == 0) {
                tracetype = TR_ROUTER;
        }
	else if(strcmp(tracename, "TRP") == 0) {
                tracetype = TR_ROUTER;
        }
        else if(strcmp(tracename, "MAC") == 0) {
                tracetype = TR_MAC;
        }
        else if(strcmp(tracename, "IFQ") == 0) {
                tracetype = TR_IFQ;
        }
        else if(strcmp(tracename, "AGT") == 0) {
                tracetype = TR_AGENT;
        }
        else {
                fprintf(stderr, "CMU Trace Initialized with invalid type\n");
                exit(1);
        }
// change wrt Mike's code
//	assert(type_ == DROP || type_ == SEND || type_ == RECV);
	assert(type_ == DROP || type_ == SEND || type_ == RECV
               || ((type_ == EOT) && (tracetype == TR_MAC)));



	newtrace_ = 0;
	for (int i=0 ; i < MAX_NODE ; i++) 
		nodeColor[i] = 3 ;
        node_ = 0;
}

void
CMUTrace::format_mac_common(Packet *p, const char *why, int offset)
{
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_mac802_11 *mh;
	struct hdr_smac *sh;
	char mactype[SMALL_LEN];

	strcpy(mactype, Simulator::instance().macType());
	if (strcmp (mactype, "Mac/SMAC") == 0)
		sh = HDR_SMAC(p);
	else
		mh = HDR_MAC802_11(p);
	
	double x = 0.0, y = 0.0, z = 0.0;
       
	char op = (char) type_;
	Node* thisnode = Node::get_node_by_address(src_);
	double energy = -1;
	if (thisnode) {
	    if (thisnode->energy_model()) {
		    energy = thisnode->energy_model()->energy();
	    }
	}

	// hack the IP address to convert pkt format to hostid format
	// for now until port ids are removed from IP address. -Padma.

	int src = Address::instance().get_nodeaddr(ih->saddr());

	if(tracetype == TR_ROUTER && type_ == SEND) {
		if(src_ != src)
			op = FWRD;
	}

	// use tagged format if appropriate
	if (pt_->tagged()) {
		int next_hop = -1 ;
		Node* nextnode = Node::get_node_by_address(ch->next_hop_);
        	if (nextnode) next_hop = nextnode->nodeid(); 

		node_->getLoc(&x, &y, &z);

		if (op == DROP) op = 'd';
		if (op == SEND) op = '+';
		if (op == FWRD) op = 'h';

		sprintf(pt_->buffer() + offset,
			"%c "TIME_FORMAT" -s %d -d %d -p %s -k %3s -i %d "
			"-N:loc {%.2f %.2f %.2f} -N:en %f ",
			
			op,				// event type
			Scheduler::instance().clock(),	// time
			src_,				// this node
			next_hop,			// next hop
			packet_info.name(ch->ptype()),	// packet type
			tracename,			// trace level
			ch->uid(),			// event id
			x, y, z,			// location
			energy);				// energy

		offset = strlen(pt_->buffer());
		if (strcmp (mactype, "Mac/SMAC") == 0) {
			format_smac(p, offset);
		} else {
			format_mac(p, offset);
		}
		return;
	}


	// Use new ns trace format to replace the old cmu trace format)
	if (newtrace_) {
	    
	    node_->getLoc(&x, &y, &z);
	    // consistence
	    if ( op == DROP ) { op = 'd';}

	        // basic trace infomation + basic exenstion

	    sprintf(pt_->buffer() + offset,
		   "%c -t %.9f -Hs %d -Hd %d -Ni %d -Nx %.2f -Ny %.2f -Nz %.2f -Ne %f -Nl %3s -Nw %s ",
		    op,                       // event type
		    Scheduler::instance().clock(),  // time
		    src_,                           // this node
                    ch->next_hop_,                  // next hop
		    src_,                           // this node
		    x,                              // x coordinate
		    y,                              // y coordinate
		    z,                              // z coordinate
		    energy,                         // energy, -1 = not existing
		    tracename,                      // trace level
                    why);                            // reason

	    // mac layer extension

	    offset = strlen(pt_->buffer());
	    if (strcmp(mactype, "Mac/SMAC") == 0) {
		    format_smac(p, offset);
	    } else {
		    format_mac(p, offset);
	    }
	    return;
	}


#ifdef LOG_POSITION
        double x = 0.0, y = 0.0, z = 0.0;
        node_->getLoc(&x, &y, &z);
#endif

	sprintf(pt_->buffer() + offset,
#ifdef LOG_POSITION
		"%c %.9f %d (%6.2f %6.2f) %3s %4s %d %s %d [%x %x %x %x] ",
#else
		"%c %.9f _%d_ %3s %4s %d %s %d",
#endif
		op,
		Scheduler::instance().clock(),
                src_,                           // this node
#ifdef LOG_POSITION
                x,
                y,
#endif
		tracename,
		why,
		
                ch->uid(),                      // identifier for this event
		
		((ch->ptype() == PT_MAC) ? (
		  (mh->dh_fc.fc_subtype == MAC_Subtype_RTS) ? "RTS"  :
		  (mh->dh_fc.fc_subtype == MAC_Subtype_CTS) ? "CTS"  :
		  (mh->dh_fc.fc_subtype == MAC_Subtype_ACK) ? "ACK"  :
		  "UNKN") :
		 (ch->ptype() == PT_SMAC) ? (
		  (sh->type == RTS_PKT) ? "RTS" :
		  (sh->type == CTS_PKT) ? "CTS" :
		  (sh->type == ACK_PKT) ? "ACK" :
		  (sh->type == SYNC_PKT) ? "SYNC" :
		  "UNKN") : 
		 packet_info.name(ch->ptype())),
		ch->size());
	
	offset = strlen(pt_->buffer());

	if (strncmp (mactype, "Mac/SMAC", 8) == 0) {
		format_smac(p, offset);
	} else {
		format_mac(p, offset);
        }
	
	offset = strlen(pt_->buffer());

	if (thisnode) {
		if (thisnode->energy_model()) {
			sprintf(pt_->buffer() + offset,
				"[energy %f] ",
				thisnode->energy_model()->energy());
		}
        }
}

void
CMUTrace::format_mac(Packet *p, int offset)
{
	struct hdr_mac802_11 *mh = HDR_MAC802_11(p);
	
	if (pt_->tagged()) {
		sprintf(pt_->buffer() + offset,
			"-M:dur %x -M:s %x -M:d %x -M:t %x ",
			mh->dh_duration,		// MAC: duration
			
			// change wrt Mike's code
			//ETHER_ADDR(mh->dh_da),		// MAC: source
			//ETHER_ADDR(mh->dh_sa),		// MAC: destination
			ETHER_ADDR(mh->dh_ra),          // MAC: source
                       ETHER_ADDR(mh->dh_ta),          // MAC: destination


			GET_ETHER_TYPE(mh->dh_body));	// MAC: type
	} else if (newtrace_) {
		sprintf(pt_->buffer() + offset, 
			"-Ma %x -Md %x -Ms %x -Mt %x ",
			mh->dh_duration,
			
			// change wrt Mike's code
			//ETHER_ADDR(mh->dh_da),
			//ETHER_ADDR(mh->dh_sa),

	   		ETHER_ADDR(mh->dh_ra),
	                   ETHER_ADDR(mh->dh_ta),

			GET_ETHER_TYPE(mh->dh_body));
	} else {
		sprintf(pt_->buffer() + offset,
			" [%x %x %x %x] ",
			//*((u_int16_t*) &mh->dh_fc),
			mh->dh_duration,
			
			// change wrt Mike's code
			//ETHER_ADDR(mh->dh_da),
			//ETHER_ADDR(mh->dh_sa),
			ETHER_ADDR(mh->dh_ra),
                        ETHER_ADDR(mh->dh_ta),


			GET_ETHER_TYPE(mh->dh_body));
	}
}

void
CMUTrace::format_smac(Packet *p, int offset)
{
	struct hdr_smac *sh = HDR_SMAC(p);
	sprintf(pt_->buffer() + offset,
		" [%.2f %d %d] ",
		sh->duration,
		sh->dstAddr,
		sh->srcAddr);
}
	

void
CMUTrace::format_ip(Packet *p, int offset)
{
        struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	
	// hack the IP address to convert pkt format to hostid format
	// for now until port ids are removed from IP address. -Padma.
	int src = Address::instance().get_nodeaddr(ih->saddr());
	int dst = Address::instance().get_nodeaddr(ih->daddr());

	if (pt_->tagged()) {
		sprintf(pt_->buffer() + offset,
			"-IP:s %d -IP:sp %d -IP:d %d -IP:dp %d -p %s -e %d "
			"-c %d -i %d -IP:ttl %d ",
			src,                           // packet src
			ih->sport(),                   // src port
			dst,                           // packet dest
			ih->dport(),                   // dst port
			packet_info.name(ch->ptype()), // packet type
			ch->size(),                    // packet size
			ih->flowid(),                  // flow id
			ch->uid(),                     // unique id
			ih->ttl_                       // ttl
			);
	} else if (newtrace_) {
	    sprintf(pt_->buffer() + offset,
		    "-Is %d.%d -Id %d.%d -It %s -Il %d -If %d -Ii %d -Iv %d ",
		    src,                           // packet src
		    ih->sport(),                   // src port
		    dst,                           // packet dest
		    ih->dport(),                   // dst port
		    packet_info.name(ch->ptype()),  // packet type
		    ch->size(),                     // packet size
		    ih->flowid(),                   // flow id
		    ch->uid(),                      // unique id
		    ih->ttl_);                      // ttl
	} else {
	    sprintf(pt_->buffer() + offset, "------- [%d:%d %d:%d %d %d] ",
		src, ih->sport(),
		dst, ih->dport(),
		ih->ttl_, (ch->next_hop_ < 0) ? 0 : ch->next_hop_);
	}
}

void
CMUTrace::format_arp(Packet *p, int offset)
{
	struct hdr_arp *ah = HDR_ARP(p);

	if (pt_->tagged()) {
	    sprintf(pt_->buffer() + offset,
		    "-arp:op %s -arp:ms %d -arp:s %d -arp:md %d -arp:d %d ",
		    ah->arp_op == ARPOP_REQUEST ?  "REQUEST" : "REPLY",
		    ah->arp_sha,
		    ah->arp_spa,
		    ah->arp_tha,
		    ah->arp_tpa);
	} else if (newtrace_) {
	    sprintf(pt_->buffer() + offset,
		    "-P arp -Po %s -Pms %d -Ps %d -Pmd %d -Pd %d ",
		    ah->arp_op == ARPOP_REQUEST ?  "REQUEST" : "REPLY",
		    ah->arp_sha,
		    ah->arp_spa,
		    ah->arp_tha,
		    ah->arp_tpa);
	} else {

	    sprintf(pt_->buffer() + offset,
		"------- [%s %d/%d %d/%d]",
		ah->arp_op == ARPOP_REQUEST ?  "REQUEST" : "REPLY",
		ah->arp_sha,
		ah->arp_spa,
		ah->arp_tha,
		ah->arp_tpa);
	}
}

void
CMUTrace::format_dsr(Packet *p, int offset)
{
	hdr_sr *srh = hdr_sr::access(p);

	if (pt_->tagged()) {
	    sprintf(pt_->buffer() + offset,
		    "-dsr:h %d -dsr:q %d -dsr:s %d -dsr:p %d -dsr:n %d "
		    "-dsr:l %d -dsr:e {%d %d} -dsr:w %d -dsr:m %d -dsr:c %d "
		    "-dsr:b {%d %d} ",
		    srh->num_addrs(),
		    srh->route_request(),
		    srh->rtreq_seq(),
		    srh->route_reply(),
		    srh->rtreq_seq(),
		    srh->route_reply_len(),
		    srh->reply_addrs()[0].addr,
		    srh->reply_addrs()[srh->route_reply_len()-1].addr,
		    srh->route_error(),
		    srh->num_route_errors(),
		    srh->down_links()[srh->num_route_errors() - 1].tell_addr,
		    srh->down_links()[srh->num_route_errors() - 1].from_addr,
		    srh->down_links()[srh->num_route_errors() - 1].to_addr);
	    return;
	} else if (newtrace_) {
	    sprintf(pt_->buffer() + offset, 
		"-P dsr -Ph %d -Pq %d -Ps %d -Pp %d -Pn %d -Pl %d -Pe %d->%d -Pw %d -Pm %d -Pc %d -Pb %d->%d ",
		    srh->num_addrs(),                   // how many nodes travered

		srh->route_request(),
		srh->rtreq_seq(),

		srh->route_reply(),
		srh->rtreq_seq(),
		srh->route_reply_len(),
		// the dest of the src route
		srh->reply_addrs()[0].addr,
		srh->reply_addrs()[srh->route_reply_len()-1].addr,

		srh->route_error(),
		srh->num_route_errors(),
		srh->down_links()[srh->num_route_errors() - 1].tell_addr,
		srh->down_links()[srh->num_route_errors() - 1].from_addr,
		srh->down_links()[srh->num_route_errors() - 1].to_addr);

	   return;
	}
	sprintf(pt_->buffer() + offset, 
		"%d [%d %d] [%d %d %d %d->%d] [%d %d %d %d->%d]",
		srh->num_addrs(),

		srh->route_request(),
		srh->rtreq_seq(),

		srh->route_reply(),
		srh->rtreq_seq(),
		srh->route_reply_len(),
		// the dest of the src route
		srh->reply_addrs()[0].addr,
		srh->reply_addrs()[srh->route_reply_len()-1].addr,

		srh->route_error(),
		srh->num_route_errors(),
		srh->down_links()[srh->num_route_errors() - 1].tell_addr,
		srh->down_links()[srh->num_route_errors() - 1].from_addr,
		srh->down_links()[srh->num_route_errors() - 1].to_addr);
}

void
CMUTrace::format_msg(Packet *, int)
{
}

void
CMUTrace::format_tcp(Packet *p, int offset)
{
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_tcp *th = HDR_TCP(p);
	
	if (pt_->tagged()) {
	    sprintf(pt_->buffer() + offset,
		    "-tcp:s %d -tcp:a %d -tcp:f %d -tcp:o %d ",
		    th->seqno_,
		    th->ackno_,
		    ch->num_forwards(),
		    ch->opt_num_forwards());
	} else if (newtrace_) {
	    sprintf(pt_->buffer() + offset,
		"-Pn tcp -Ps %d -Pa %d -Pf %d -Po %d ",
		th->seqno_,
		th->ackno_,
		ch->num_forwards(),
		ch->opt_num_forwards());

	} else {
	    sprintf(pt_->buffer() + offset,
		"[%d %d] %d %d",
		th->seqno_,
		th->ackno_,
		ch->num_forwards(),
		ch->opt_num_forwards());
	}
}

/* Armando L. Caro Jr. <acaro@@cis,udel,edu> 6/5/2002
 * (with help from Florina Almenárez <florina@@it,uc3m,es>)
 */
void
CMUTrace::format_sctp(Packet* p,int offset)
{
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	struct hdr_sctp *sh = HDR_SCTP(p);
	char cChunkType;
  
	for(int i = 0; i < sh->NumChunks(); i++) {
		switch(sh->SctpTrace()[i].eType) {
		case SCTP_CHUNK_INIT:
		case SCTP_CHUNK_INIT_ACK:
		case SCTP_CHUNK_COOKIE_ECHO:
		case SCTP_CHUNK_COOKIE_ACK:
			cChunkType = 'I';       // connection initialization
			break;
      
		case SCTP_CHUNK_DATA:
			cChunkType = 'D';
			break;
      
		case SCTP_CHUNK_SACK:
			cChunkType = 'S';
			break;
      
		case SCTP_CHUNK_FORWARD_TSN:
			cChunkType = 'R';
			break;

		case SCTP_CHUNK_HB:
			cChunkType = 'H';
			break;
			
		case SCTP_CHUNK_HB_ACK:
			cChunkType = 'B';
			break;
		}
    
		if( newtrace_ ) {
			sprintf(pt_->buffer() + offset,
				"-Pn sctp -Pnc %d -Pct %c "
				"-Ptsn %d -Psid %d -Pssn %d "
				"-Pf %d -Po %d ",
				sh->NumChunks(),
				cChunkType,
				sh->SctpTrace()[i].uiTsn,
				sh->SctpTrace()[i].usStreamId,
				sh->SctpTrace()[i].usStreamSeqNum,
				ch->num_forwards(),
				ch->opt_num_forwards());
		}
		else {
			sprintf(pt_->buffer() + offset,
				"[%d %c %d %d %d] %d %d",
				sh->NumChunks(),
				cChunkType,
				sh->SctpTrace()[i].uiTsn,
				sh->SctpTrace()[i].usStreamId,
				sh->SctpTrace()[i].usStreamSeqNum,
				ch->num_forwards(),
				ch->opt_num_forwards());
		}
	}
}

void
CMUTrace::format_rtp(Packet *p, int offset)
{
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_rtp *rh = HDR_RTP(p);
	struct hdr_ip *ih = HDR_IP(p);
        Node* thisnode = Node::get_node_by_address(src_);

	//hacking, needs to change later, 
        int dst = Address::instance().get_nodeaddr(ih->daddr());
	
	if (dst == src_){
		// I just received a cbr data packet
		if (thisnode->energy_model() && 
		    thisnode->energy_model()->powersavingflag()) {
			thisnode->energy_model()->set_node_state(EnergyModel::INROUTE);
		}
        }

	if (pt_->tagged()) {
		sprintf(pt_->buffer() + offset,
			"-cbr:s %d -cbr:f %d -cbr:o %d ",
			rh->seqno_,
			ch->num_forwards(),
			ch->opt_num_forwards());
	} else if (newtrace_) {
		sprintf(pt_->buffer() + offset,
			"-Pn cbr -Pi %d -Pf %d -Po %d ",
			rh->seqno_,
			ch->num_forwards(),
			ch->opt_num_forwards());
	} else {
		sprintf(pt_->buffer() + offset,
			"[%d] %d %d",
			rh->seqno_,
			ch->num_forwards(),
			ch->opt_num_forwards());
	}
}

void
CMUTrace::format_imep(Packet *p, int offset)
{
        struct hdr_imep *im = HDR_IMEP(p);

#define U_INT16_T(x)    *((u_int16_t*) &(x))

	if (pt_->tagged()) {
	    sprintf(pt_->buffer() + offset,
		    "-imep:a %c -imep:h %c -imep:o %c -imep:l %04x ",
		    (im->imep_block_flags & BLOCK_FLAG_ACK) ? 'A' : '-',
                    (im->imep_block_flags & BLOCK_FLAG_HELLO) ? 'H' : '-',
                    (im->imep_block_flags & BLOCK_FLAG_OBJECT) ? 'O' : '-',
                    U_INT16_T(im->imep_length));
	} else if (newtrace_) {
	    sprintf(pt_->buffer() + offset,
                "-P imep -Pa %c -Ph %c -Po %c -Pl 0x%04x ] ",
                (im->imep_block_flags & BLOCK_FLAG_ACK) ? 'A' : '-',
                (im->imep_block_flags & BLOCK_FLAG_HELLO) ? 'H' : '-',
                (im->imep_block_flags & BLOCK_FLAG_OBJECT) ? 'O' : '-',
                U_INT16_T(im->imep_length));
	} else {
            sprintf(pt_->buffer() + offset,
                "[%c %c %c 0x%04x] ",
                (im->imep_block_flags & BLOCK_FLAG_ACK) ? 'A' : '-',
                (im->imep_block_flags & BLOCK_FLAG_HELLO) ? 'H' : '-',
                (im->imep_block_flags & BLOCK_FLAG_OBJECT) ? 'O' : '-',
                U_INT16_T(im->imep_length));
	}
#undef U_INT16_T
}


void
CMUTrace::format_tora(Packet *p, int offset)
{
        struct hdr_tora *th = HDR_TORA(p);
        struct hdr_tora_qry *qh = HDR_TORA_QRY(p);
        struct hdr_tora_upd *uh = HDR_TORA_UPD(p);
        struct hdr_tora_clr *ch = HDR_TORA_CLR(p);

        switch(th->th_type) {

        case TORATYPE_QRY:

		if (pt_->tagged()) {
		    sprintf(pt_->buffer() + offset,
			    "-tora:t %x -tora:d %d -tora:c QUERY",
			    qh->tq_type, qh->tq_dst);
		} else if (newtrace_) {
		    sprintf(pt_->buffer() + offset,
			"-P tora -Pt 0x%x -Pd %d -Pc QUERY ",
                        qh->tq_type, qh->tq_dst);
			
                } else {

                    sprintf(pt_->buffer() + offset, "[0x%x %d] (QUERY)",
                        qh->tq_type, qh->tq_dst);
		}
                break;

        case TORATYPE_UPD:

		if (pt_->tagged()) {
		    sprintf(pt_->buffer() + offset,
			    "-tora:t %x -tora:d %d -tora:a %f -tora:o %d "
			    "-tora:r %d -tora:e %d -tora:i %d -tora:c UPDATE",
			    uh->tu_type,
                            uh->tu_dst,
                            uh->tu_tau,
                            uh->tu_oid,
                            uh->tu_r,
                            uh->tu_delta,
                            uh->tu_id);
		} else if (newtrace_) {
		    sprintf(pt_->buffer() + offset,
                        "-P tora -Pt 0x%x -Pd %d (%f %d %d %d %d) -Pc UPDATE ",
                        uh->tu_type,
                        uh->tu_dst,
                        uh->tu_tau,
                        uh->tu_oid,
                        uh->tu_r,
                        uh->tu_delta,
                        uh->tu_id);
		} else {
                    sprintf(pt_->buffer() + offset,
                        "-Pt 0x%x -Pd %d -Pa %f -Po %d -Pr %d -Pe %d -Pi %d -Pc UPDATE ",
                        uh->tu_type,
                        uh->tu_dst,
                        uh->tu_tau,
                        uh->tu_oid,
                        uh->tu_r,
                        uh->tu_delta,
                        uh->tu_id);
		}
                break;

        case TORATYPE_CLR:
		if (pt_->tagged()) {
		    sprintf(pt_->buffer() + offset,
			    "-tora:t %x -tora:d %d -tora:a %f -tora:o %d "
			    "-tora:c CLEAR ",
			    ch->tc_type,
                            ch->tc_dst,
                            ch->tc_tau,
                            ch->tc_oid);
		} else if (newtrace_) {
		    sprintf(pt_->buffer() + offset, 
			"-P tora -Pt 0x%x -Pd %d -Pa %f -Po %d -Pc CLEAR ",
                        ch->tc_type,
                        ch->tc_dst,
                        ch->tc_tau,
                        ch->tc_oid);
		} else {
                    sprintf(pt_->buffer() + offset, "[0x%x %d %f %d] (CLEAR)",
                        ch->tc_type,
                        ch->tc_dst,
                        ch->tc_tau,
                        ch->tc_oid);
		}
                break;
        }
}

void
CMUTrace::format_aodv(Packet *p, int offset)
{
        struct hdr_aodv *ah = HDR_AODV(p);
        struct hdr_aodv_request *rq = HDR_AODV_REQUEST(p);
        struct hdr_aodv_reply *rp = HDR_AODV_REPLY(p);


        switch(ah->ah_type) {
        case AODVTYPE_RREQ:

		if (pt_->tagged()) {
		    sprintf(pt_->buffer() + offset,
			    "-aodv:t %x -aodv:h %d -aodv:b %d -aodv:d %d "
			    "-aodv:ds %d -aodv:s %d -aodv:ss %d "
			    "-aodv:c REQUEST ",
			    rq->rq_type,
                            rq->rq_hop_count,
                            rq->rq_bcast_id,
                            rq->rq_dst,
                            rq->rq_dst_seqno,
                            rq->rq_src,
                            rq->rq_src_seqno);
		} else if (newtrace_) {

		    sprintf(pt_->buffer() + offset,
			"-P aodv -Pt 0x%x -Ph %d -Pb %d -Pd %d -Pds %d -Ps %d -Pss %d -Pc REQUEST ",
			rq->rq_type,
                        rq->rq_hop_count,
                        rq->rq_bcast_id,
                        rq->rq_dst,
                        rq->rq_dst_seqno,
                        rq->rq_src,
                        rq->rq_src_seqno);


		} else {

		    sprintf(pt_->buffer() + offset,
			"[0x%x %d %d [%d %d] [%d %d]] (REQUEST)",
			rq->rq_type,
                        rq->rq_hop_count,
                        rq->rq_bcast_id,
                        rq->rq_dst,
                        rq->rq_dst_seqno,
                        rq->rq_src,
                        rq->rq_src_seqno);
		}
                break;

        case AODVTYPE_RREP:
        case AODVTYPE_HELLO:
	case AODVTYPE_RERR:
		
		if (pt_->tagged()) {
		    sprintf(pt_->buffer() + offset,
			    "-aodv:t %x -aodv:h %d -aodv:d %d -adov:ds %d "
			    "-aodv:l %f -aodv:c %s ",
			    rp->rp_type,
			    rp->rp_hop_count,
			    rp->rp_dst,
			    rp->rp_dst_seqno,
			    rp->rp_lifetime,
			    rp->rp_type == AODVTYPE_RREP ? "REPLY" :
			    (rp->rp_type == AODVTYPE_RERR ? "ERROR" :
			     "HELLO"));
		} else if (newtrace_) {
			
			sprintf(pt_->buffer() + offset,
			    "-P aodv -Pt 0x%x -Ph %d -Pd %d -Pds %d -Pl %f -Pc %s ",
				rp->rp_type,
				rp->rp_hop_count,
				rp->rp_dst,
				rp->rp_dst_seqno,
				rp->rp_lifetime,
				rp->rp_type == AODVTYPE_RREP ? "REPLY" :
				(rp->rp_type == AODVTYPE_RERR ? "ERROR" :
				 "HELLO"));
	        } else {
			
			sprintf(pt_->buffer() + offset,
				"[0x%x %d [%d %d] %f] (%s)",
				rp->rp_type,
				rp->rp_hop_count,
				rp->rp_dst,
				rp->rp_dst_seqno,
				rp->rp_lifetime,
				rp->rp_type == AODVTYPE_RREP ? "REPLY" :
				(rp->rp_type == AODVTYPE_RERR ? "ERROR" :
				 "HELLO"));
		}
                break;
		
        default:
#ifdef WIN32
                fprintf(stderr,
		        "CMUTrace::format_aodv: invalid AODV packet type\n");
#else
		fprintf(stderr,
		        "%s: invalid AODV packet type\n", __FUNCTION__);
#endif
                abort();
        }
}
#ifdef AODV_UU
void CMUTrace::format_aodvuu(Packet *p, int offset) {

        struct hdr_ip *ih = HDR_IP(p);
        hdr_aodvuu *ah = HDR_AODVUU(p);
        AODV_msg *aodv_msg = (AODV_msg *) ah;

        RREQ *aodv_rreq = (RREQ *) aodv_msg;
        RREP *aodv_rrep = (RREP *) aodv_msg;
        RREP_ack *aodv_rrep_ack = (RREP_ack *) aodv_msg;
        RERR *aodv_rerr = (RERR *) aodv_msg;

        switch (aodv_msg->type) {

        case AODV_RREQ:

                if (pt_->tagged()) {
                        // Tagged format currently not supported
                } else if (newtrace_) {

                        sprintf(pt_->buffer() + offset,
                                "-P aodvuu -Pt 0x%x -Ph %d -Pb %d -Pd %d -Pds %d -Ps %d -Pss %d -Pc REQUEST ",
                                aodv_rreq->type,
                                aodv_rreq->hcnt,
                                aodv_rreq->rreq_id,
                                (nsaddr_t) aodv_rreq->dest_addr,
                                aodv_rreq->dest_seqno,
                                (nsaddr_t) aodv_rreq->orig_addr,
                                aodv_rreq->orig_seqno);

                } else {

                        sprintf(pt_->buffer() + offset,
                                "[0x%x %d %d [%d %d] [%d %d]] (REQUEST)",
                                aodv_rreq->type,
                                aodv_rreq->hcnt,
                                ntohl(aodv_rreq->rreq_id),
                                (nsaddr_t) aodv_rreq->dest_addr,
                                aodv_rreq->dest_seqno,
                                (nsaddr_t) aodv_rreq->orig_addr,
                                aodv_rreq->orig_seqno);
                }

                break;

        case AODV_HELLO:

                /* FALLS THROUGH (HELLO:s are sent as RREP:s) */

        case AODV_RREP:

                if (pt_->tagged()) {
                        // Tagged format currently not supported
                } else if (newtrace_) {

                        sprintf(pt_->buffer() + offset,
                                "-P aodvuu -Pt 0x%x -Ph %d -Pd %d -Pds %d -Ps %d -Pl %f -Pc %s ",
                                aodv_rrep->type,
                                aodv_rrep->hcnt,
                                (nsaddr_t) aodv_rrep->dest_addr,
                                aodv_rrep->dest_seqno,
				(nsaddr_t) aodv_rrep->orig_addr,
                                (double) aodv_rrep->lifetime,
                                (ih->daddr() == (nsaddr_t) AODV_BROADCAST &&
                                 ih->ttl() == 1) ? "HELLO" : "REPLY");
                } else {

                        sprintf(pt_->buffer() + offset,
                                "[0x%x %d [%d %d] [%d] %f] (%s)",
                                aodv_rrep->type,
                                aodv_rrep->hcnt,
                                (nsaddr_t) aodv_rrep->dest_addr,
                                aodv_rrep->dest_seqno,
				(nsaddr_t) aodv_rrep->orig_addr,
                                (double) aodv_rrep->lifetime,
                                (ih->daddr() == (nsaddr_t) AODV_BROADCAST &&
                                 ih->ttl() == 1) ? "HELLO" : "REPLY");
                }

                break;

        case AODV_RERR:

                /*
                  Note 1:

                  The "hop count" (-Ph and its corresponding field in
                  the old trace format) is actually the DestCount.

                  This is a reminiscence from the AODV trace format,
                  where RREP:s, RERR:s and HELLO:s are treated equally
                  in terms of logging.

                  Note 2:

                  Lifetime field does not exist for RERR:s.
                  Again a reminiscence from the AODV trace format
                  (where that field isn't even initialized!).
                  Therefore lifetime is set to 0.0 all the time for RERR:s.
		*/

                if (pt_->tagged()) {
                        // Tagged format currently not supported
                } else if (newtrace_) {

                        sprintf(pt_->buffer() + offset,
                                "-P aodvuu -Pt 0x%x -Ph %d -Pd %d -Pds %d -Pl %f -Pc ERROR ",
                                aodv_rerr->type,
                                aodv_rerr->dest_count,
                                (nsaddr_t) aodv_rerr->dest_addr,
                                aodv_rerr->dest_seqno,
                                0.0);
                } else {

                        sprintf(pt_->buffer() + offset,
                                "[0x%x %d [%d %d] %f] (ERROR)",
                                aodv_rerr->type,
                                aodv_rerr->dest_count,
                                (nsaddr_t) aodv_rerr->dest_addr,
                                aodv_rerr->dest_seqno,
                                0.0);
                }

                break;

        case AODV_RREP_ACK:

                /*
                  Note 3:

                  RREP-ACK logging didn't exist in the AODV trace format.
		*/

                if (pt_->tagged()) {
                        // Tagged format currently not supported
                } else if (newtrace_) {

                        sprintf(pt_->buffer() + offset,
                                "-P aodvuu -Pt 0x%x RREP-ACK ",
                                aodv_rrep_ack->type);
                } else {

                        sprintf(pt_->buffer() + offset,
                                "[%d] (RREP-ACK)",
                                aodv_rrep_ack->type);
                }

                break;

        default:

#ifdef WIN32
                fprintf(stderr,
                        "CMUTrace::format_aodvuu: invalid AODVUU packet type\n");
#else
                fprintf(stderr,
                        "%s: invalid AODVUU packet type\n", __FUNCTION__);
#endif
                abort();

                break;
        }
}
#endif /* AODV_UU */

void
CMUTrace::nam_format(Packet *p, int offset)
{
	Node* srcnode = 0 ;
	Node* dstnode = 0 ;
	Node* nextnode = 0 ;
        struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_ip *ih = HDR_IP(p);
	char op = (char) type_;
	char colors[32];
	int next_hop = -1 ;

// change wrt Mike's code
	assert(type_ != EOT);



        int dst = Address::instance().get_nodeaddr(ih->daddr());

	nextnode = Node::get_node_by_address(ch->next_hop_);
        if (nextnode) next_hop = nextnode->nodeid(); 

	srcnode = Node::get_node_by_address(src_);
	dstnode = Node::get_node_by_address(ch->next_hop_);

	double distance = 0;

        if ((srcnode) && (dstnode)) {
	   MobileNode* tmnode = (MobileNode*)srcnode;
	   MobileNode* rmnode = (MobileNode*)dstnode;

	   distance = tmnode->propdelay(rmnode) * 300000000 ;
	}

	double energy = -1;
	double initenergy = -1;

	//default value for changing node color with respect to energy depletion
	double l1 = 0.5; 
	double l2 = 0.2;
	
	if (srcnode) {
	    if (srcnode->energy_model()) {
		    energy = srcnode->energy_model()->energy();
		    initenergy = srcnode->energy_model()->initialenergy();
		    l1 = srcnode->energy_model()->level1();
		    l2 = srcnode->energy_model()->level2();
	    }
	}

        int energyLevel = 0 ;
        double energyLeft = (double)(energy/initenergy) ;

        if ((energyLeft <= 1 ) && (energyLeft >= l1 )) energyLevel = 3;	
        if ((energyLeft >= l2 ) && (energyLeft < l1 )) energyLevel = 2;	
        if ((energyLeft > 0 ) && (energyLeft < l2 )) energyLevel = 1;	

	if (energyLevel == 0) 
		strcpy(colors,"-c black -o red");
        else if (energyLevel == 1) 
		strcpy(colors,"-c red -o yellow");
        else if (energyLevel == 2) 
		strcpy(colors,"-c yellow -o green");
        else if (energyLevel == 3) 
		strcpy(colors,"-c green -o black");

	// A simple hack for scadds demo (fernandez's visit) -- Chalermek
	int pkt_color = 0;
	if (ch->ptype()==PT_DIFF) {
		hdr_cdiff *dfh= HDR_CDIFF(p);
		if (dfh->mess_type != DATA) {
			pkt_color = 1;
		}
	}
	// convert to nam format 
	if (op == 's') op = 'h' ;
	if (op == 'D') op = 'd' ;
	if (op == 'h') {
	   sprintf(pt_->nbuffer(),
		"+ -t %.9f -s %d -d %d -p %s -e %d -c 2 -a %d -i %d -k %3s ",
		Scheduler::instance().clock(),
		src_,                           // this node
		next_hop,
		packet_info.name(ch->ptype()),
		ch->size(),
		pkt_color,   
		ch->uid(),
		tracename);

	   offset = strlen(pt_->nbuffer());
	   pt_->namdump();
	   sprintf(pt_->nbuffer() ,
		"- -t %.9f -s %d -d %d -p %s -e %d -c 2 -a %d -i %d -k %3s",
		Scheduler::instance().clock(),
		src_,                           // this node
		next_hop,
		packet_info.name(ch->ptype()),
		ch->size(),
		pkt_color,
		ch->uid(),
		tracename);

	   offset = strlen(pt_->nbuffer());
           pt_->namdump();
	}

        // if nodes are too far from each other
	// nam won't dump SEND event 'cuz it's
	// gonna be dropped later anyway
	// this value 250 is pre-calculated by using 
	// two-ray ground refelction model with fixed
	// transmission power 3.652e-10
//	if ((type_ == SEND)  && (distance > 250 )) return ;

	if(tracetype == TR_ROUTER && type_ == RECV && dst != -1 ) return ;
	if(type_ == RECV && dst == -1 )dst = src_ ; //broadcasting event

        if (energy != -1) { //energy model being turned on
	   if (src_ >= MAX_NODE) {
	       fprintf (stderr, "%: node id must be < %d\n",
		       __PRETTY_FUNCTION__, MAX_NODE);
	       exit(0);
	   }
	   if (nodeColor[src_] != energyLevel ) { //only dump it when node  
	       sprintf(pt_->nbuffer() ,                    //color change
	          "n -t %.9f -s %d -S COLOR %s",
	           Scheduler::instance().clock(),
	           src_,                           // this node
	           colors);
               offset = strlen(pt_->nbuffer());
               pt_->namdump();
	       nodeColor[src_] = energyLevel ;
	    }   
        }

	sprintf(pt_->nbuffer() ,
		"%c -t %.9f -s %d -d %d -p %s -e %d -c 2 -a %d -i %d -k %3s",
		op,
		Scheduler::instance().clock(),
		src_,                           // this node
		next_hop,
		packet_info.name(ch->ptype()),
		ch->size(),
		pkt_color,
		ch->uid(),
		tracename);

	offset = strlen(pt_->nbuffer());
	pt_->namdump();
}

void CMUTrace::format(Packet* p, const char *why)
{
	hdr_cmn *ch = HDR_CMN(p);
	int offset = 0;

	/*
	 * Log the MAC Header
	 */
	format_mac_common(p, why, offset);

	if (pt_->namchannel()) 
		nam_format(p, offset);
	offset = strlen(pt_->buffer());
	switch(ch->ptype()) {
	case PT_MAC:
	case PT_SMAC:
		break;
	case PT_ARP:
		format_arp(p, offset);
		break;
	default:
		format_ip(p, offset);
		offset = strlen(pt_->buffer());
		switch(ch->ptype()) {
		case PT_AODV:
			format_aodv(p, offset);
			break;
		case PT_TORA:
                        format_tora(p, offset);
                        break;
                case PT_IMEP:
                        format_imep(p, offset);
                        break;
		case PT_DSR:
			format_dsr(p, offset);
			break;
		case PT_MESSAGE:
		case PT_UDP:
			format_msg(p, offset);
			break;
		case PT_TCP:
		case PT_ACK:
			format_tcp(p, offset);
			break;
		case PT_SCTP:
			/* Armando L. Caro Jr. <acaro@@cis,udel,edu> 6/5/2002
			 */
			format_sctp(p, offset);
			break;
		case PT_CBR:
			format_rtp(p, offset);
			break;
	        case PT_DIFF:
			break;
		case PT_GAF:
		case PT_PING:
			break;
#ifdef AODV_UU
		case PT_ENCAPSULATED:
			break;
                case PT_AODVUU:
                        format_aodvuu(p, offset);
                        break;
#endif /* AODV_UU */
		default:
			fprintf(stderr, "%s - invalid packet type (%s).\n",
				__PRETTY_FUNCTION__, packet_info.name(ch->ptype()));
			exit(1);
		}
	}
}

int
CMUTrace::command(int argc, const char*const* argv)
{
	
        if(argc == 3) {
                if(strcmp(argv[1], "node") == 0) {
                        node_ = (MobileNode*) TclObject::lookup(argv[2]);
                        if(node_ == 0)
                                return TCL_ERROR;
                        return TCL_OK;
                }
		if (strcmp(argv[1], "newtrace") == 0) {
			newtrace_ = atoi(argv[2]);
		        return TCL_OK;
		}
        }
	return Trace::command(argc, argv);
}

/*ARGSUSED*/
void
CMUTrace::recv(Packet *p, Handler *h)
{
	if (!node_energy()) {
		Packet::free(p);
		return;
	}
        assert(initialized());
        /*
         * Agent Trace "stamp" the packet with the optimal route on
         * sending.
         */
        if (tracetype == TR_AGENT && type_ == SEND) {
                God::instance()->stampPacket(p);
        }
#if 0
        /*
         * When the originator of a packet drops the packet, it may or may
         * not have been stamped by GOD.  Stamp it before logging the
         * information.
         */
        if(src_ == src && type_ == DROP) {
                God::instance()->stampPacket(p);
        }
#endif
	format(p, "---");
	pt_->dump();
	//namdump();
	if(target_ == 0)
		Packet::free(p);
	else
		send(p, h);
}

void
CMUTrace::recv(Packet *p, const char* why)
{
        assert(initialized() && type_ == DROP);
	if (!node_energy()) {
		Packet::free(p);
		return;
	}
#if 0
        /*
         * When the originator of a packet drops the packet, it may or may
         * not have been stamped by GOD.  Stamp it before logging the
         * information.
         */
        if(src_ == ih->saddr()) {
                God::instance()->stampPacket(p);
        }
#endif
	format(p, why);
	pt_->dump();
	//namdump();
	Packet::free(p);
}

int CMUTrace::node_energy()
{
	Node* thisnode = Node::get_node_by_address(src_);
	double energy = 1;
	if (thisnode) {
		if (thisnode->energy_model()) {
			energy = thisnode->energy_model()->energy();
		}
	} 
	if (energy > 0) return 1;
	return 0;
}
