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
 * @(#) $Header$ (LBL)
 */

#ifndef ns_packet_h
#define ns_packet_h

#include <string.h>
#include <assert.h>

#include "config.h"
#include "scheduler.h"
#include "object.h"
#include "lib/bsd-list.h"
#include "packet-stamp.h"
#include "ns-process.h"

// Used by wireless routing code to attach routing agent
#define RT_PORT		255	/* port that all route msgs are sent to */

#define HDR_CMN(p)      (hdr_cmn::access(p))
#define HDR_ARP(p)      (hdr_arp::access(p))
#define HDR_MAC(p)      (hdr_mac::access(p))
#define HDR_MAC802_11(p) ((hdr_mac802_11 *)hdr_mac::access(p))
#define HDR_MAC_TDMA(p) ((hdr_mac_tdma *)hdr_mac::access(p))
#define HDR_LL(p)       (hdr_ll::access(p))
#define HDR_IP(p)       (hdr_ip::access(p))
#define HDR_RTP(p)      (hdr_rtp::access(p))
#define HDR_TCP(p)      (hdr_tcp::access(p))
#define HDR_SR(p)       (hdr_sr::access(p))
#define HDR_TFRC(p)     (hdr_tfrc::access(p))
#define HDR_TORA(p)     (hdr_tora::access(p))
#define HDR_IMEP(p)     (hdr_imep::access(p))
#define HDR_CDIFF(p)    (hdr_cdiff::access(p))  /* chalermak's diffusion*/
//#define HDR_DIFF(p)     (hdr_diff::access(p))  /* SCADD's diffusion ported into ns */

/* --------------------------------------------------------------------*/

enum packet_t {
	PT_TCP,
	PT_UDP,
	PT_CBR,
	PT_AUDIO,
	PT_VIDEO,
	PT_ACK,
	PT_START,
	PT_STOP,
	PT_PRUNE,
	PT_GRAFT,
	PT_GRAFTACK,
	PT_JOIN,
	PT_ASSERT,
	PT_MESSAGE,
	PT_RTCP,
	PT_RTP,
	PT_RTPROTO_DV,
	PT_CtrMcast_Encap,
	PT_CtrMcast_Decap,
	PT_SRM,
	/* simple signalling messages */
	PT_REQUEST,	
	PT_ACCEPT,	
	PT_CONFIRM,	
	PT_TEARDOWN,	
	PT_LIVE,	// packet from live network
	PT_REJECT,

	PT_TELNET,	// not needed: telnet use TCP
	PT_FTP,
	PT_PARETO,
	PT_EXP,
	PT_INVAL,
	PT_HTTP,

	/* new encapsulator */
	PT_ENCAPSULATED,
	PT_MFTP,

	/* CMU/Monarch's extnsions */
	PT_ARP,
	PT_MAC,
	PT_TORA,
	PT_DSR,
	PT_AODV,
	PT_IMEP,

	// RAP packets
	PT_RAP_DATA,
	PT_RAP_ACK,

	PT_TFRC,
	PT_TFRC_ACK,
	PT_PING,

	// Diffusion packets - Chalermek
	PT_DIFF,

	// LinkState routing update packets
	PT_RTPROTO_LS,

	// MPLS LDP header
	PT_LDP,

	// GAF packet
        PT_GAF,  

	// ReadAudio traffic
	PT_REALAUDIO,

	// Pushback Messages
	PT_PUSHBACK,

#ifdef PGM
	// Pragmatic General Multicast
	PT_PGM,
#endif

	// insert new packet types here

#ifdef AODV_UU
	// AODV packets in AODV-UU
	PT_AODVUU,
#endif /* AODV_UU */

	PT_NTYPE // This MUST be the LAST one
};

class p_info {
public:
	p_info() {
		name_[PT_TCP]= "tcp";
		name_[PT_UDP]= "udp";
		name_[PT_CBR]= "cbr";
		name_[PT_AUDIO]= "audio";
		name_[PT_VIDEO]= "video";
		name_[PT_ACK]= "ack";
		name_[PT_START]= "start";
		name_[PT_STOP]= "stop";
		name_[PT_PRUNE]= "prune";
		name_[PT_GRAFT]= "graft";
		name_[PT_GRAFTACK]= "graftAck";
		name_[PT_JOIN]= "join";
		name_[PT_ASSERT]= "assert";
		name_[PT_MESSAGE]= "message";
		name_[PT_RTCP]= "rtcp";
		name_[PT_RTP]= "rtp";
		name_[PT_RTPROTO_DV]= "rtProtoDV";
		name_[PT_CtrMcast_Encap]= "CtrMcast_Encap";
		name_[PT_CtrMcast_Decap]= "CtrMcast_Decap";
		name_[PT_SRM]= "SRM";

		name_[PT_REQUEST]= "sa_req";	
		name_[PT_ACCEPT]= "sa_accept";
		name_[PT_CONFIRM]= "sa_conf";
		name_[PT_TEARDOWN]= "sa_teardown";
		name_[PT_LIVE]= "live"; 
		name_[PT_REJECT]= "sa_reject";

		name_[PT_TELNET]= "telnet";
		name_[PT_FTP]= "ftp";
		name_[PT_PARETO]= "pareto";
		name_[PT_EXP]= "exp";
		name_[PT_INVAL]= "httpInval";
		name_[PT_HTTP]= "http";
		name_[PT_ENCAPSULATED]= "encap";
		name_[PT_MFTP]= "mftp";
		name_[PT_ARP]= "ARP";
		name_[PT_MAC]= "MAC";
		name_[PT_TORA]= "TORA";
		name_[PT_DSR]= "DSR";
		name_[PT_AODV]= "AODV";
		name_[PT_IMEP]= "IMEP";

		name_[PT_RAP_DATA] = "rap_data";
		name_[PT_RAP_ACK] = "rap_ack";

 		name_[PT_TFRC]= "tcpFriend";
		name_[PT_TFRC_ACK]= "tcpFriendCtl";
		name_[PT_PING]="ping";

	 	/* For diffusion : Chalermek */
 		name_[PT_DIFF] = "diffusion";

		// Link state routing updates
		name_[PT_RTPROTO_LS] = "rtProtoLS";

		// MPLS LDP packets
		name_[PT_LDP] = "LDP";

		// for GAF
                name_[PT_GAF] = "gaf";      

		// RealAudio packets
		name_[PT_REALAUDIO] = "ra";

		//pushback 
		name_[PT_PUSHBACK] = "pushback";

#ifdef PGM
		// for PGM
		name_[PT_PGM] = "PGM";
#endif

#ifdef AODV_UU
		// AODV packets in AODV-UU
		name_[PT_AODVUU] = "AODVUU";
#endif /* AODV_UU */

		name_[PT_NTYPE]= "undefined";
	}
	const char* name(packet_t p) const { 
		if ( p <= PT_NTYPE ) return name_[p];
		return 0;
	}
	static bool data_packet(packet_t type) {
		return ( (type) == PT_TCP || \
			 (type) == PT_TELNET || \
			 (type) == PT_CBR || \
			 (type) == PT_AUDIO || \
			 (type) == PT_VIDEO || \
			 (type) == PT_ACK \
			 );
	}
private:
	static char* name_[PT_NTYPE+1];
};
extern p_info packet_info; /* map PT_* to string name */
//extern char* p_info::name_[];


#define DATA_PACKET(type) ( (type) == PT_TCP || \
                            (type) == PT_TELNET || \
                            (type) == PT_CBR || \
                            (type) == PT_AUDIO || \
                            (type) == PT_VIDEO || \
                            (type) == PT_ACK \
                            )


#define OFFSET(type, field)	((int) &((type *)0)->field)

class PacketData : public AppData {
public:
	PacketData(int sz) : AppData(PACKET_DATA) {
		datalen_ = sz;
		if (datalen_ > 0)
			data_ = new unsigned char[datalen_];
		else
			data_ = NULL;
	}
	PacketData(PacketData& d) : AppData(d) {
		datalen_ = d.datalen_;
		if (datalen_ > 0) {
			data_ = new unsigned char[datalen_];
			memcpy(data_, d.data_, datalen_);
		} else
			data_ = NULL;
	}
	virtual ~PacketData() { 
		if (data_ != NULL) 
			delete []data_; 
	}
	unsigned char* data() { return data_; }

	virtual int size() const { return datalen_; }
	virtual AppData* copy() { return new PacketData(*this); }
private:
	unsigned char* data_;
	int datalen_;
};

//Monarch ext
typedef void (*FailureCallback)(Packet *,void *);

class Packet : public Event {
private:
	unsigned char* bits_;	// header bits
//	unsigned char* data_;	// variable size buffer for 'data'
//  	unsigned int datalen_;	// length of variable size buffer
	AppData* data_;		// variable size buffer for 'data'
	static void init(Packet*);     // initialize pkt hdr 
	bool fflag_;
protected:
	static Packet* free_;	// packet free list
public:
	Packet* next_;		// for queues and the free list
	static int hdrlen_;

	Packet() : bits_(0), data_(0), next_(0) { }
	inline unsigned char* const bits() { return (bits_); }
	inline Packet* copy() const;
	static inline Packet* alloc();
	static inline Packet* alloc(int);
	inline void allocdata(int);
	// dirty hack for diffusion data
	inline void initdata() { data_  = 0;}
	static inline void free(Packet*);
	inline unsigned char* access(int off) const {
		if (off < 0)
			abort();
		return (&bits_[off]);
	}
	// This is used for backward compatibility, i.e., assuming user data
	// is PacketData and return its pointer.
	inline unsigned char* accessdata() const { 
		if (data_ == 0)
			return 0;
		assert(data_->type() == PACKET_DATA);
		return (((PacketData*)data_)->data()); 
	}
	// This is used to access application-specific data, not limited 
	// to PacketData.
	inline AppData* userdata() const {
		return data_;
	}
	inline void setdata(AppData* d) { 
		if (data_ != NULL)
			delete data_;
		data_ = d; 
	}
	inline int datalen() const { return data_ ? data_->size() : 0; }

	// Monarch extn

	static void dump_header(Packet *p, int offset, int length);

	// the pkt stamp carries all info about how/where the pkt
        // was sent needed for a receiver to determine if it correctly
        // receives the pkt
        PacketStamp	txinfo_;  

	/*
         * According to cmu code:
	 * This flag is set by the MAC layer on an incoming packet
         * and is cleared by the link layer.  It is an ugly hack, but
         * there's really no other way because NS always calls
         * the recv() function of an object.
	 * 
         */
        u_int8_t        incoming;

	//monarch extns end;
};

/* 
 * static constant associations between interface special (negative) 
 * values and their c-string representations that are used from tcl
 */
class iface_literal {
public:
	enum iface_constant { 
		UNKN_IFACE= -1, /* 
				 * iface value for locally originated packets 
				 */
		ANY_IFACE= -2   /* 
				 * hashnode with iif == ANY_IFACE_   
				 * matches any pkt iface (imported from TCL);
				 * this value should be different from 
				 * hdr_cmn::UNKN_IFACE (packet.h)
				 */ 
	};
	iface_literal(const iface_constant i, const char * const n) : 
		value_(i), name_(n) {}
	inline int value() const { return value_; }
	inline const char * const name() const { return name_; }
private:
	const iface_constant value_;
	/* strings used in TCL to access those special values */
	const char * const name_; 
};

static const iface_literal UNKN_IFACE(iface_literal::UNKN_IFACE, "?");
static const iface_literal ANY_IFACE(iface_literal::ANY_IFACE, "*");

/*
 * Note that NS_AF_* doesn't necessarily correspond with
 * the constants used in your system (because many
 * systems don't have NONE or ILINK).
 */
enum ns_af_enum { NS_AF_NONE, NS_AF_ILINK, NS_AF_INET };

struct hdr_cmn {
	enum dir_t { DOWN= -1, NONE= 0, UP= 1 };
	packet_t ptype_;	// packet type (see above)
	int	size_;		// simulated packet size
	int	uid_;		// unique id
	int	error_;		// error flag
	int     errbitcnt_;     // # of corrupted bits jahn
	int     fecsize_;
	double	ts_;		// timestamp: for q-delay measurement
	int	iface_;		// receiving interface (label)
	dir_t	direction_;	// direction: 0=none, 1=up, -1=down
	int	ref_count_;	// free the pkt until count to 0
	// source routing 
        char src_rt_valid;

	//Monarch extn begins
	nsaddr_t prev_hop_;     // IP addr of forwarding hop
	nsaddr_t next_hop_;	// next hop for this packet
	int      addr_type_;    // type of next_hop_ addr
	nsaddr_t last_hop_;     // for tracing on multi-user channels

        // called if pkt can't obtain media or isn't ack'd. not called if
        // droped by a queue
        FailureCallback xmit_failure_; 
        void *xmit_failure_data_;

        /*
         * MONARCH wants to know if the MAC layer is passing this back because
         * it could not get the RTS through or because it did not receive
         * an ACK.
         */
        int     xmit_reason_;
#define XMIT_REASON_RTS 0x01
#define XMIT_REASON_ACK 0x02

        // filled in by GOD on first transmission, used for trace analysis
        int num_forwards_;	// how many times this pkt was forwarded
        int opt_num_forwards_;   // optimal #forwards
	// Monarch extn ends;

	// tx time for this packet in sec
	double txtime_;
	inline double& txtime() { return(txtime_); }

	static int offset_;	// offset for this header
	inline static int& offset() { return offset_; }
	inline static hdr_cmn* access(const Packet* p) {
		return (hdr_cmn*) p->access(offset_);
	}
	
        /* per-field member functions */
	inline packet_t& ptype() { return (ptype_); }
	inline int& size() { return (size_); }
	inline int& uid() { return (uid_); }
	inline int& error() { return error_; }
	inline int& errbitcnt() {return errbitcnt_; }
	inline int& fecsize() {return fecsize_; }
	inline double& timestamp() { return (ts_); }
	inline int& iface() { return (iface_); }
	inline dir_t& direction() { return (direction_); }
	inline int& ref_count() { return (ref_count_); }
	// monarch_begin
	inline nsaddr_t& next_hop() { return (next_hop_); }
	inline int& addr_type() { return (addr_type_); }
	inline int& num_forwards() { return (num_forwards_); }
	inline int& opt_num_forwards() { return (opt_num_forwards_); }
        //monarch_end
};


class PacketHeaderClass : public TclClass {
protected:
	PacketHeaderClass(const char* classname, int hdrsize);
	virtual int method(int argc, const char*const* argv);
	void field_offset(const char* fieldname, int offset);
	inline void bind_offset(int* off) { offset_ = off; }
	inline void offset(int* off) {offset_= off;}
	int hdrlen_;		// # of bytes for this header
	int* offset_;		// offset for this header
public:
	virtual void bind();
	virtual void export_offsets();
	TclObject* create(int argc, const char*const* argv);
};


inline void Packet::init(Packet* p)
{
	bzero(p->bits_, hdrlen_);
}

inline Packet* Packet::alloc()
{
	Packet* p = free_;
	if (p != 0) {
		assert(p->fflag_ == FALSE);
		free_ = p->next_;
		assert(p->data_ == 0);
		p->uid_ = 0;
		p->time_ = 0;
	} else {
		p = new Packet;
		p->bits_ = new unsigned char[hdrlen_];
		if (p == 0 || p->bits_ == 0)
			abort();
	}
	init(p); // Initialize bits_[]
	(HDR_CMN(p))->next_hop_ = -2; // -1 reserved for IP_BROADCAST
	(HDR_CMN(p))->last_hop_ = -2; // -1 reserved for IP_BROADCAST
	p->fflag_ = TRUE;
	(HDR_CMN(p))->direction() = hdr_cmn::DOWN;
	/* setting all direction of pkts to be downward as default; 
	   until channel changes it to +1 (upward) */
	p->next_ = 0;
	return (p);
}

/* 
 * Allocate an n byte data buffer to an existing packet 
 * 
 * To set application-specific AppData, use Packet::setdata()
 */
inline void Packet::allocdata(int n)
{
	assert(data_ == 0);
	data_ = new PacketData(n);
	if (data_ == 0)
		abort();
}

/* allocate a packet with an n byte data buffer */
inline Packet* Packet::alloc(int n)
{
	Packet* p = alloc();
	if (n > 0) 
		p->allocdata(n);
	return (p);
}


inline void Packet::free(Packet* p)
{
	hdr_cmn* ch = hdr_cmn::access(p);
	if (p->fflag_) {
		if (ch->ref_count() == 0) {
			/*
			 * A packet's uid may be < 0 (out of a event queue), or
			 * == 0 (newed but never gets into the event queue.
			 */
			assert(p->uid_ <= 0);
			// Delete user data because we won't need it any more.
			if (p->data_ != 0) {
				delete p->data_;
				p->data_ = 0;
			}
			init(p);
			p->next_ = free_;
			free_ = p;
			p->fflag_ = FALSE;
		} else {
			ch->ref_count() = ch->ref_count() - 1;
		}
	}
}

inline Packet* Packet::copy() const
{
	
	Packet* p = alloc();
	memcpy(p->bits(), bits_, hdrlen_);
	if (data_) 
		p->data_ = data_->copy();
	p->txinfo_.init(&txinfo_);
 
	return (p);
}

inline void
Packet::dump_header(Packet *p, int offset, int length)
{
        assert(offset + length <= p->hdrlen_);
        struct hdr_cmn *ch = HDR_CMN(p);

        fprintf(stderr, "\nPacket ID: %d\n", ch->uid());

        for(int i = 0; i < length ; i+=16) {
                fprintf(stderr, "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                        p->bits_[offset + i],     p->bits_[offset + i + 1],
                        p->bits_[offset + i + 2], p->bits_[offset + i + 3],
                        p->bits_[offset + i + 4], p->bits_[offset + i + 5],
                        p->bits_[offset + i + 6], p->bits_[offset + i + 7],
                        p->bits_[offset + i + 8], p->bits_[offset + i + 9],
                        p->bits_[offset + i + 10], p->bits_[offset + i + 11],
                        p->bits_[offset + i + 12], p->bits_[offset + i + 13],
                        p->bits_[offset + i + 14], p->bits_[offset + i + 15]);
        }
}

#endif
