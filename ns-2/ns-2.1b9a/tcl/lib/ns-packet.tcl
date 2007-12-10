# -*-	Mode:tcl; tcl-indent-level:8; tab-width:8; indent-tabs-mode:t -*-
#
# Time-stamp: <2000-08-31 19:01:26 haoboy>
#
# Copyright (c) 1997 Regents of the University of California.
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
# 	This product includes software developed by the MASH Research
# 	Group at the University of California Berkeley.
# 4. Neither the name of the University nor of the Research Group may be
#    used to endorse or promote products derived from this software without
#    specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# @(#) $Header$
#
# set up the packet format for the simulation
# (initial version)
#

#
# XXX Packet Header Usage Guide
#
# By default, ns includes ALL packet headers of ALL protocols in ns in 
# EVERY packet in your simulation. This is a LOT, and will increase as more
# protocols are added into ns. For "packet-intensive" simulations, this could
# be a huge overhead.
#
# To include only the packet headers that are of interest to you in your 
# specific simulation, follow this pattern (e.g., you want to remove AODV,
# and ARP headers from your simulation):
#
#   remove-packet-header AODV ARP
#   ...
#   set ns [new Simulator]
#
# NOTICE THAT ADD-PACKET-HEADER{} MUST GO BEFORE THE SIMULATOR IS CREATED.
#
# To include only a specific set of headers in your simulation, e.g., AODV
# and ARP, follow this pattern:
#
#   remove-all-packet-headers
#   add-packet-header AODV ARP
#   ... 
#   set ns [new Simulator]
#
# IMPORTANT: You MUST never remove common header from your simulation. 
# As you can see, this is also enforced by these header manipulation procs.
#

PacketHeaderManager set hdrlen_ 0

# XXX Common header should ALWAYS be present
PacketHeaderManager set tab_(Common) 1

proc add-packet-header args {
	foreach cl $args {
		PacketHeaderManager set tab_(PacketHeader/$cl) 1
	}
}

proc add-all-packet-headers {} {
	PacketHeaderManager instvar tab_
	foreach cl [PacketHeader info subclass] {
		if [info exists tab_($cl)] { 
			PacketHeaderManager set tab_($cl) 1
		}
	}
}

proc remove-packet-header args {
	foreach cl $args {
		if { $cl == "Common" } {
			warn "Cannot exclude common packet header."
			continue
		}
		PacketHeaderManager unset tab_(PacketHeader/$cl)
	}
}

proc remove-all-packet-headers {} {
	PacketHeaderManager instvar tab_
	foreach cl [PacketHeader info subclass] {
		if { $cl != "PacketHeader/Common" } {
			if [info exists tab_($cl)] { 
				PacketHeaderManager unset tab_($cl)
			}
		}
	}
}

foreach prot {
	AODV
	ARP
  	aSRM 
	Common 
	CtrMcast 
	Diffusion
	Encap
	Flags
	HttpInval
	IMEP
	IP
        IPinIP 
	IVS
	LDP
	LL
	mcastCtrl
	MFTP
	MPLS
	Mac 
	Message
        MIP 
	Ping
	PGM
	PGM_SPM
	PGM_NAK
	RAP 
	RTP
	Resv 
	rtProtoDV
	rtProtoLS
	SR
	Src_rt
  	SRM 
  	SRMEXT
	Snoop
	TCP
	TCPA
	TFRC
	TFRC_ACK
	TORA
	GAF
	UMP 
	Pushback
	NV
	AODVUU
} {
	add-packet-header $prot
}

proc PktHdr_offset { hdrName {field ""} } {
	set offset [$hdrName offset]
	if { $field != "" } {
		# This requires that fields inside the packet header must
		# be exported via PacketHeaderClass::export_offsets(), which
		# should use PacketHeaderClass::field_offset() to export 
		# field offsets into otcl space.
		incr offset [$hdrName set offset_($field)]
	}
	return $offset
}

Simulator instproc create_packetformat { } {
	PacketHeaderManager instvar tab_
	set pm [new PacketHeaderManager]
	foreach cl [PacketHeader info subclass] {
		if [info exists tab_($cl)] {
			set off [$pm allochdr $cl]
			$cl offset $off
		}
	}
	$self set packetManager_ $pm
}

PacketHeaderManager instproc allochdr cl {
	set size [$cl set hdrlen_]

	$self instvar hdrlen_
	set NS_ALIGN 8
	# round up to nearest NS_ALIGN bytes
	# (needed on sparc/solaris)
	set incr [expr ($size + ($NS_ALIGN-1)) & ~($NS_ALIGN-1)]
	set base $hdrlen_
	incr hdrlen_ $incr

	return $base
}

# XXX Old code. Do NOT delete for now. - Aug 30, 2000

# Initialization
#  foreach cl [PacketHeader info subclass] {
#  	PacketHeaderManager set vartab_($cl) ""
#  }

# So that not all packet headers should be initialized here.
# E.g., the link state routing header is initialized using this proc in 
# ns-rtProtoLS.tcl; because link state may be turned off when STL is not 
# available, this saves us a ns-packet.tcl.in
#  proc create-packet-header { cl var } {
#  	PacketHeaderManager set vartab_(PacketHeader/$cl) $var
#  }

# If you need to save some memory, you can disable unneeded packet headers
# by commenting them out from the list below
#  foreach pair {
#  	{ Common off_cmn_ }
#  	{ Mac off_mac_ }
#  	{ LL off_ll_ }
#  	{ ARP off_arp_ }
#  	{ Snoop off_snoop_ }
#  	{ SR off_SR_ }
#  	{ IP off_ip_ }
#  	{ TCP off_tcp_ }
#  	{ TCPA off_tcpasym_ }
#  	{ Flags off_flags_ }
#  	{ TORA off_TORA_ }
#  	{ AODV off_AODV_ }
#  	{ IMEP off_IMEP_ }
#  	{ RTP off_rtp_ } 
#  	{ Message off_msg_ }
#  	{ IVS off_ivs_ }
#  	{ rtProtoDV off_DV_ }
#  	{ CtrMcast off_CtrMcast_ }
#  	{ mcastCtrl off_mcast_ctrl_ }
#    	{ aSRM off_asrm_ }
#    	{ SRM off_srm_ }
#    	{ SRMEXT off_srm_ext_}
#  	{ Resv off_resv_}
#  	{ HttpInval off_inv_}
#          { IPinIP off_ipinip_} 
#          { MIP off_mip_}
#  	{ MFTP off_mftp_ }
#  	{ Encap off_encap_ }
#  	{ RAP off_rap_ }
#  	{ UMP off_ump_  }
#  	{ TFRC off_tfrm_ }
#  	{ Ping off_ping_ }
#  	{ rtProtoLS off_LS_ }
#  	{ MPLS off_mpls_ }
#	{ GAF off_gaf_ } 
#  	{ LDP off_ldp_ }
#  } {
#  	create-packet-header [lindex $pair 0] [lindex $pair 1]
#  }

#  proc PktHdr_offset {hdrName {field ""}} {
#  	set var [PacketHeaderManager set vartab_($hdrName)]
#  	set offset [TclObject set $var]
#  	if {$field != ""} {
#  		incr offset [$hdrName set offset_($field)]
#  	}
#  	return $offset
#  }

#  Simulator instproc create_packetformat { } {
#  	PacketHeaderManager instvar vartab_
#  	set pm [new PacketHeaderManager]
#  	foreach cl [PacketHeader info subclass] {
#  		if {[info exists vartab_($cl)] && $vartab_($cl) != ""} {
#  			set off [$pm allochdr [lindex [split $cl /] 1]]
#  			set var [PacketHeaderManager set vartab_($cl)]
#  			TclObject set $var $off
#  			$cl offset $off
#  		}
#  	}
#  	$self set packetManager_ $pm
#  }
