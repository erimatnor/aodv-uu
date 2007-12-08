# -*-	Mode:tcl; tcl-indent-level:8; tab-width:8; indent-tabs-mode:t -*-
#
# Copyright (c) 1998-2000 Regents of the University of California.
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
# $Header$
#
# Ported from CMU-Monarch project's mobility extensions -Padma, 10/98.
#

# IT IS NOT ENCOURAGED TO SUBCLASSS MOBILENODE CLASS DEFINED IN THIS FILE

#======================================================================
# The ARPTable class
#======================================================================
ARPTable instproc init args {
	eval $self next $args		;# parent class constructor
}

ARPTable set bandwidth_         0
ARPTable set delay_             5us

#======================================================================
# The Node/MobileNodeNode class
#======================================================================

Node/MobileNode instproc init args {
#  	# I don't care about address classifier; it's not my business
#  	# All I do is to setup port classifier so we can do broadcast, 
#  	# and to set up interface stuff.
#  	$self attach-node $node
#  	$node port-notify $self

	eval $self next $args

	$self instvar nifs_ arptable_ X_ Y_ Z_ nodetype_
	set X_ 0.0
	set Y_ 0.0
	set Z_ 0.0
        set arptable_ ""                ;# no ARP table yet
	set nifs_	0		;# number of network interfaces
	# Mobile IP node processing
        $self makemip-New$nodetype_
}

#----------------------------------------------------------------------

# XXX Following are the last remnant of nodetype_. Need to be completely 
# removed, however, we need a better mechanism to distinguish vanilla 
# mobile node from MIP base station, and MIP mobile host.

Node/MobileNode instproc makemip-NewMobile {} {
}

Node/MobileNode instproc makemip-NewBase {} {
}

Node/MobileNode instproc makemip-New {} {
}

Node/MobileNode instproc makemip-NewMIPBS {} {
	$self instvar regagent_ encap_ decap_ agents_ id_

	set dmux [new Classifier/Port/Reserve]
	$dmux set mask_ 0x7fffffff
	$dmux set shift_ 0
	$self install-demux $dmux
   
	set regagent_ [new Agent/MIPBS $self]
	$self attach $regagent_ [Node/MobileNode set REGAGENT_PORT]
	$self attach-encap 
	$self attach-decap
}

Node/MobileNode instproc attach-encap {} {
	$self instvar encap_ 
	
	set encap_ [new MIPEncapsulator]

	$encap_ set mask_ [AddrParams NodeMask 1]
	$encap_ set shift_ [AddrParams NodeShift 1]
	#set mask 0x7fffffff
	#set shift 0
	set nodeaddr [AddrParams addr2id [$self node-addr]]
	$encap_ set addr_ [expr ( ~([AddrParams NodeMask 1] << \
			[AddrParams NodeShift 1]) & $nodeaddr )]
	$encap_ set port_ 1
	$encap_ target [$self entry]
	$encap_ set node_ $self
}

Node/MobileNode instproc attach-decap {} {
	$self instvar decap_ dmux_ agents_
	set decap_ [new Classifier/Addr/MIPDecapsulator]
	lappend agents_ $decap_
	$decap_ set mask_ [AddrParams NodeMask 1]
	$decap_ set shift_ [AddrParams NodeShift 1]
	$dmux_ install [Node/MobileNode set DECAP_PORT] $decap_
}

Node/MobileNode instproc makemip-NewMIPMH {} {
	$self instvar regagent_
 
	set dmux [new Classifier/Port/Reserve]
	$dmux set mask_ 0x7fffffff
	$dmux set shift_ 0
	$self install-demux $dmux

	set regagent_ [new Agent/MIPMH $self]
	$self attach $regagent_ [Node/MobileNode set REGAGENT_PORT]
	$regagent_ set mask_ [AddrParams NodeMask 1]
	$regagent_ set shift_ [AddrParams NodeShift 1]
 	$regagent_ set dst_addr_ [expr (~0) << [AddrParams NodeShift 1]]
	$regagent_ set dst_port_ 0
	$regagent_ node $self
}

#----------------------------------------------------------------------

Node/MobileNode instproc reset {} {
	$self instvar arptable_ nifs_ netif_ mac_ ifq_ ll_ imep_
        for {set i 0} {$i < $nifs_} {incr i} {
		$netif_($i) reset
		$mac_($i) reset
		$ll_($i) reset
		$ifq_($i) reset
		if { [info exists opt(imep)] && $opt(imep) == "ON" } { 
			$imep_($i) reset 
		}
	}
	if { $arptable_ != "" } {
		$arptable_ reset 
	}
}

#
# Attach an agent to a node.  Pick a port and
# bind the agent to the port number.
# if portnumber is 255, default target is set to the routing agent
#
Node/MobileNode instproc add-target { agent port } {
	$self instvar dmux_ imep_ toraDebug_ 

	set ns [Simulator instance]
	set newapi [$ns imep-support]

	$agent set sport_ $port

	# special processing for TORA/IMEP node
	set toraonly [string first "TORA" [$agent info class]] 
	if {$toraonly != -1 } {
		$agent if-queue [$self set ifq_(0)]  ;# ifq between LL and MAC
		#
		# XXX: The routing protocol and the IMEP agents needs handles
		# to each other.
		#
		$agent imep-agent [$self set imep_(0)]
		[$self set imep_(0)] rtagent $agent
	}
	
	# Special processing for AODV
	set aodvonly [string first "AODV" [$agent info class]] 
	if {$aodvonly != -1 } {
		$agent if-queue [$self set ifq_(0)]   ;# ifq between LL and MAC
	}
	
	if { $port == [Node set rtagent_port_] } {			
		# Ad hoc routing agent setup needs special handling
		$self add-target-rtagent $agent $port
		return
	}

	# Attaching a normal agent
	set namfp [$ns get-nam-traceall]
	if { [Simulator set AgentTrace_] == "ON" } {
		#
		# Send Target
		#
		if {$newapi != ""} {
			set sndT [$self mobility-trace Send "AGT"]
		} else {
			set sndT [cmu-trace Send AGT $self]
		}
		if { $namfp != "" } {
			$sndT namattach $namfp
		}
		$sndT target [$self entry]
		$agent target $sndT
		#
		# Recv Target
		#
		if {$newapi != ""} {
			set rcvT [$self mobility-trace Recv "AGT"]
		} else {
			set rcvT [cmu-trace Recv AGT $self]
		}
		if { $namfp != "" } {
			$rcvT namattach $namfp
		}
		$rcvT target $agent
		$dmux_ install $port $rcvT
	} else {
		#
		# Send Target
		#
		$agent target [$self entry]
		#
		# Recv Target
		#
		$dmux_ install $port $agent
	}
}

Node/MobileNode instproc add-target-rtagent { agent port } {
	$self instvar imep_ toraDebug_ 

	set ns [Simulator instance]
	set newapi [$ns imep-support]
	set namfp [$ns get-nam-traceall]

	set dmux_ [$self demux]
	set classifier_ [$self entry]

	if { [Simulator set RouterTrace_] == "ON" } {
		#
		# Send Target
		#
		if {$newapi != ""} {
			set sndT [$self mobility-trace Send "RTR"]
		} else {
			set sndT [cmu-trace Send "RTR" $self]
		}
		if { $namfp != "" } {
			$sndT namattach $namfp
		}
		if { $newapi == "ON" } {
			$agent target $imep_(0)
			$imep_(0) sendtarget $sndT
			# second tracer to see the actual
			# types of tora packets before imep packs them
			if { [info exists toraDebug_] && $toraDebug_ == "ON"} {
				set sndT2 [$self mobility-trace Send "TRP"]
				$sndT2 target $imep_(0)
				$agent target $sndT2
			}
		} else {  ;#  no IMEP
			$agent target $sndT
		}
		$sndT target [$self set ll_(0)]
		#
		# Recv Target
		#
		if {$newapi != ""} {
			set rcvT [$self mobility-trace Recv "RTR"]
		} else {
			set rcvT [cmu-trace Recv "RTR" $self]
		}
		if { $namfp != "" } {
			$rcvT namattach $namfp
		}
		if {$newapi == "ON" } {
			[$self set ll_(0)] up-target $imep_(0)
			$classifier_ defaulttarget $agent
			# need a second tracer to see the actual
			# types of tora packets after imep unpacks them
			# no need to support any hier node
			if {[info exists toraDebug_] && $toraDebug_ == "ON" } {
				set rcvT2 [$self mobility-trace Recv "TRP"]
				$rcvT2 target $agent
				$classifier_ defaulttarget $rcvT2
			}
		} else {
			$rcvT target $agent
			$classifier_ defaulttarget $rcvT
			$dmux_ install $port $rcvT
		}
	} else {
		#
		# Send Target
		#
		# if tora is used
		if { $newapi == "ON" } {
			$agent target $imep_(0)
			# second tracer to see the actual
			# types of tora packets before imep packs them
			if { [info exists toraDebug_] && $toraDebug_ == "ON"} {
				set sndT2 [$self mobility-trace Send "TRP"]
				$sndT2 target $imep_(0)
				$agent target $sndT2
			}
			$imep_(0) sendtarget [$self set ll_(0)]
			
		} else {  ;#  no IMEP
			$agent target [$self set ll_(0)]
		}    
		#
		# Recv Target
		#
		if {$newapi == "ON" } {
			[$self set ll_(0)] up-target $imep_(0)
			$classifier_ defaulttarget $agent
			# need a second tracer to see the actual
			# types of tora packets after imep unpacks them
			# no need to support any hier node
			if {[info exists toraDebug_] && $toraDebug_ == "ON" } {
				set rcvT2 [$self mobility-trace Recv "TRP"]
				$rcvT2 target $agent
				[$self set classifier_] defaulttarget $rcvT2
			}
		} else {
			$classifier_ defaulttarget $agent
			$dmux_ install $port $agent
		}
	}
}

#
# The following setups up link layer, mac layer, network interface
# and physical layer structures for the mobile node.
#
Node/MobileNode instproc add-interface { channel pmodel lltype mactype \
		qtype qlen iftype anttype inerrproc outerrproc fecproc} {
	$self instvar arptable_ nifs_ netif_ mac_ ifq_ ll_ imep_ inerr_ outerr_ fec_
	
	set ns [Simulator instance]
	set imepflag [$ns imep-support]
	set t $nifs_
	incr nifs_

	set netif_($t)	[new $iftype]		;# interface
	set mac_($t)	[new $mactype]		;# mac layer
	set ifq_($t)	[new $qtype]		;# interface queue
	set ll_($t)	[new $lltype]		;# link layer
        set ant_($t)    [new $anttype]

	set inerr_($t) ""
	if {$inerrproc != ""} {
		set inerr_($t) [$inerrproc]
	}
	set outerr_($t) ""
	if {$outerrproc != ""} {
		set outerr_($t) [$outerrproc]
	}
	set fec_($t) ""
	if {$fecproc != ""} {
		set fec_($t) [$fecproc]
	}

	set namfp [$ns get-nam-traceall]
        if {$imepflag == "ON" } {              
		# IMEP layer
		set imep_($t) [new Agent/IMEP [$self id]]
		set imep $imep_($t)
		set drpT [$self mobility-trace Drop "RTR"]
		if { $namfp != "" } {
			$drpT namattach $namfp
		}
		$imep drop-target $drpT
		$ns at 0.[$self id] "$imep_($t) start"   ;# start beacon timer
        }
	#
	# Local Variables
	#
	set nullAgent_ [$ns set nullAgent_]
	set netif $netif_($t)
	set mac $mac_($t)
	set ifq $ifq_($t)
	set ll $ll_($t)

	set inerr $inerr_($t)
	set outerr $outerr_($t)
	set fec $fec_($t)

	#
	# Initialize ARP table only once.
	#
	if { $arptable_ == "" } {
		set arptable_ [new ARPTable $self $mac]
		# FOR backward compatibility sake, hack only
		if {$imepflag != ""} {
			set drpT [$self mobility-trace Drop "IFQ"]
		} else {
			set drpT [cmu-trace Drop "IFQ" $self]
		}
		$arptable_ drop-target $drpT
		if { $namfp != "" } {
			$drpT namattach $namfp
		}
        }
	#
	# Link Layer
	#
	$ll arptable $arptable_
	$ll mac $mac
	$ll down-target $ifq

	if {$imepflag == "ON" } {
		$imep recvtarget [$self entry]
		$imep sendtarget $ll
		$ll up-target $imep
        } else {
		$ll up-target [$self entry]
	}
	#
	# Interface Queue
	#
	$ifq target $mac
	$ifq set limit_ $qlen
	if {$imepflag != ""} {
		set drpT [$self mobility-trace Drop "IFQ"]
	} else {
		set drpT [cmu-trace Drop "IFQ" $self]
        }
	$ifq drop-target $drpT
	if { $namfp != "" } {
		$drpT namattach $namfp
	}
	#
	# Mac Layer
	#
	$mac netif $netif
	$mac up-target $ll

	if {$outerr == "" && $fec == ""} {
		$mac down-target $netif
	} elseif {$outerr != "" && $fec == ""} {
		$mac down-target $outerr
		$outerr target $netif
	} elseif {$outerr == "" && $fec != ""} {
		$mac down-target $fec
		$fec down-target $netif
	} else {
		$mac down-target $fec
		$fec down-target $outerr
		$err target $netif
	}

	set god_ [God instance]
        if {$mactype == "Mac/802_11"} {
		$mac nodes [$god_ num_nodes]
	}
	#
	# Network Interface
	#
	#if {$fec == ""} {
        #		$netif up-target $mac
	#} else {
        #		$netif up-target $fec
	#	$fec up-target $mac
	#}

	$netif channel $channel
	if {$inerr == "" && $fec == ""} {
		$netif up-target $mac
	} elseif {$inerr != "" && $fec == ""} {
		$netif up-target $inerr
		$inerr target $mac
	} elseif {$err == "" && $fec != ""} {
		$netif up-target $fec
		$fec up-target $mac
	} else {
		$netif up-target $inerr
		$inerr target $fec
		$fec up-target $mac
	}

	$netif propagation $pmodel	;# Propagation Model
	$netif node $self		;# Bind node <---> interface
	$netif antenna $ant_($t)
	#
	# Physical Channel
	#
	$channel addif $netif

	# ============================================================

	if { [Simulator set MacTrace_] == "ON" } {
		#
		# Trace RTS/CTS/ACK Packets
		#
		if {$imepflag != ""} {
			set rcvT [$self mobility-trace Recv "MAC"]
		} else {
			set rcvT [cmu-trace Recv "MAC" $self]
		}
		$mac log-target $rcvT
		if { $namfp != "" } {
			$rcvT namattach $namfp
		}
		#
		# Trace Sent Packets
		#
		if {$imepflag != ""} {
			set sndT [$self mobility-trace Send "MAC"]
		} else {
			set sndT [cmu-trace Send "MAC" $self]
		}
		$sndT target [$mac down-target]
		$mac down-target $sndT
		if { $namfp != "" } {
			$sndT namattach $namfp
		}
		#
		# Trace Received Packets
		#
		if {$imepflag != ""} {
			set rcvT [$self mobility-trace Recv "MAC"]
		} else {
			set rcvT [cmu-trace Recv "MAC" $self]
		}
		$rcvT target [$mac up-target]
		$mac up-target $rcvT
		if { $namfp != "" } {
			$rcvT namattach $namfp
		}
		#
		# Trace Dropped Packets
		#
		if {$imepflag != ""} {
			set drpT [$self mobility-trace Drop "MAC"]
		} else {
			set drpT [cmu-trace Drop "MAC" $self]`
		}
		$mac drop-target $drpT
		if { $namfp != "" } {
			$drpT namattach $namfp
		}
	} else {
		$mac log-target [$ns set nullAgent_]
		$mac drop-target [$ns set nullAgent_]
	}

	# ============================================================

	$self addif $netif
}

# set transmission power
Node/MobileNode instproc setPt { val } {
	$self instvar netif_
	$netif_(0) setTxPower $val
}

# set receiving power
Node/MobileNode instproc setPr { val } {
	$self instvar netif_
	$netif_(0) setRxPower $val
}

# set idle power -- Chalermek
Node/MobileNode instproc setPidle { val } {
	$self instvar netif_
	$netif_(0) setIdlePower $val
}

Node/MobileNode instproc mobility-trace { ttype atype } {
	set ns [Simulator instance]
        set tracefd [$ns get-ns-traceall]
        if { $tracefd == "" } {
	        puts "Warning: You have not defined you tracefile yet!"
	        puts "Please use trace-all command to define it."
		return ""
	}
	set T [new CMUTrace/$ttype $atype]
	$T newtrace [Simulator set WirelessNewTrace_]
	$T tagged [Simulator set TaggedTrace_]
	$T target [$ns nullagent]
	$T attach $tracefd
        $T set src_ [$self id]
        $T node $self
	return $T
}

Node/MobileNode instproc nodetrace { tracefd } {
	#
	# This Trace Target is used to log changes in direction
	# and velocity for the mobile node.
	#
	set T [new Trace/Generic]
	$T target [[Simulator instance] set nullAgent_]
	$T attach $tracefd
	$T set src_ [$self id]
	$self log-target $T    
}

Node/MobileNode instproc agenttrace {tracefd} {
	set ns [Simulator instance]
	set ragent [$self set ragent_]
	#
	# Drop Target (always on regardless of other tracing)
	#
	set drpT [$self mobility-trace Drop "RTR"]
	set namfp [$ns get-nam-traceall]
	if { $namfp != ""} {
		$drpT namattach $namfp
	}
	$ragent drop-target $drpT
	#
	# Log Target
	#
	set T [new Trace/Generic]
	$T target [$ns set nullAgent_]
	$T attach $tracefd
	$T set src_ [$self id]
	$ragent tracetarget $T
	#
	# XXX: let the IMEP agent use the same log target.
	#
	set imepflag [$ns imep-support]
	if {$imepflag == "ON"} {
		[$self set imep_(0)] log-target $T
	}
}

Node/MobileNode instproc mip-call {ragent} {
	$self instvar regagent_
	if [info exists regagent_] {
		$regagent_ ragent $ragent
	}
}

Node/MobileNode instproc attach-gafpartner {} {

        $self instvar gafpartner_ address_ ll_ 

        set gafpartner_ [new GAFPartner]

	$gafpartner_ set mask_ [AddrParams NodeMask 1]
	$gafpartner_ set shift_ [AddrParams NodeShift 1]
	set nodeaddr [AddrParams addr2id [$self node-addr]]
	
	#$gafpartner_ set addr_ [expr ( ~([AddrParams NodeMask 1] << \
	#		[AddrParams NodeShift 1]) & $nodeaddr )]

	
	$gafpartner_ set addr_ $nodeaddr
	$gafpartner_ set port_ 254

	#puts [$gafpartner_ set addr_]

        $gafpartner_ target [$self entry]
	$ll_(0) up-target $gafpartner_
}

Node/MobileNode instproc unset-gafpartner {} {
	$self instvar gafpartner_
	
	$gafpartner_ set-gafagent 0

}


Class SRNodeNew -superclass Node/MobileNode

SRNodeNew instproc init args {
	$self instvar dsr_agent_ dmux_ entry_point_ address_

        set ns [Simulator instance]

	eval $self next $args	;# parent class constructor

	if {$dmux_ == "" } {
		# Use the default mash and shift
		set dmux_ [new Classifier/Port]
	}
	set dsr_agent_ [new Agent/DSRAgent]

	# setup address (supports hier-address) for dsragent
	$dsr_agent_ addr $address_
	$dsr_agent_ node $self
	if [Simulator set mobile_ip_] {
		$dsr_agent_ port-dmux [$self set dmux_]
	}
	# set up IP address
	$self addr $address_
	
	if { [Simulator set RouterTrace_] == "ON" } {
		# Recv Target
		set rcvT [$self mobility-trace Recv "RTR"]
		set namfp [$ns get-nam-traceall]
		if {  $namfp != "" } {
			$rcvT namattach $namfp
		}
		$rcvT target $dsr_agent_
		set entry_point_ $rcvT	
	} else {
		# Recv Target
		set entry_point_ $dsr_agent_
	}

	$self set ragent_ $dsr_agent_
	$dsr_agent_ target $dmux_

	# packets to the DSR port should be dropped, since we've
	# already handled them in the DSRAgent at the entry.
	set nullAgent_ [$ns set nullAgent_]
	$dmux_ install [Node set rtagent_port_] $nullAgent_

	# SRNodes don't use the IP addr classifier.  The DSRAgent should
	# be the entry point
	$self instvar classifier_
	set classifier_ "srnode made illegal use of classifier_"

	return $self
}

SRNodeNew instproc start-dsr {} {
	$self instvar dsr_agent_
	$dsr_agent_ startdsr
}

SRNodeNew instproc entry {} {
        $self instvar entry_point_
        return $entry_point_
}

SRNodeNew instproc add-interface args {
	eval $self next $args

	$self instvar dsr_agent_ ll_ mac_ ifq_

	set ns [Simulator instance]
	$dsr_agent_ mac-addr [$mac_(0) id]

	if { [Simulator set RouterTrace_] == "ON" } {
		# Send Target
		set sndT [$self mobility-trace Send "RTR"]
		set namfp [$ns get-nam-traceall]
		if {$namfp != "" } {
			$sndT namattach $namfp
		}
		$sndT target $ll_(0)
		$dsr_agent_ add-ll $sndT $ifq_(0)
	} else {
		# Send Target
		$dsr_agent_ add-ll $ll_(0) $ifq_(0)
	}
	# setup promiscuous tap into mac layer
	$dsr_agent_ install-tap $mac_(0)
}

SRNodeNew instproc reset args {
	$self instvar dsr_agent_
	eval $self next $args
	$dsr_agent_ reset
}
##############################################################################
# A MobileNode Class for AODV which is modeled after the SRNode Class
# but with modifications.
##############################################################################
Class Node/MobileNode/AODVNode -superclass Node/MobileNode

Node/MobileNode/AODVNode instproc init args {
	$self instvar ragent_ dmux_ classifier_ entry_point_ address_

        set ns [Simulator instance]

	eval $self next $args	;# parent class constructor

	if {$dmux_ == "" } {
		# Use the default mash and shift
		set dmux_ [new Classifier/Port]
	}
	set ragent_ [new Agent/AODVUU [$self id ]]

	# setup address (supports hier-address) for AODV agent
	$self addr $address_
	$ragent_ addr $address_
	$ragent_ node $self

	# Add the node's own address to the port demuxer
	$self add-route $address_ $dmux_

	if { [Simulator set RouterTrace_] == "ON" } {
		# Recv Target
		set rcvT [$self mobility-trace Recv "RTR"]
		set namfp [$ns get-nam-traceall]
		if {  $namfp != "" } {
			$rcvT namattach $namfp
		}
		$rcvT target $ragent_
		set entry_point_ $rcvT	
	} else {
		# Recv Target
		set entry_point_ $ragent_
	}

	$self set ragent_ $ragent_

	# The target of the routing agent is the address classifier
	$ragent_ target $classifier_

	set nullAgent_ [$ns set nullAgent_]
	
	# The default target in the classifier is set to the
	# nullAgent, since the routing agent already handled whatever
	# needs to be handled
	$classifier_ defaulttarget $nullAgent_
	
	# Packets to the routing agent and default port should be
	# dropped, since we've already handled them in the routing
	# agent at the entry.
	$dmux_ install [Node set rtagent_port_] $nullAgent_
	$dmux_ defaulttarget $nullAgent_

	return $self
}

Node/MobileNode/AODVNode instproc start-aodv {} {
	$self instvar ragent_
	$ragent_ start
}

Node/MobileNode/AODVNode instproc entry {} {
        $self instvar entry_point_
        return $entry_point_
}

Node/MobileNode/AODVNode instproc add-interface args {
	eval $self next $args

	$self instvar ragent_ ll_ mac_ ifq_

	set ns [Simulator instance]

	if { [Simulator set RouterTrace_] == "ON" } {
		# Send Target
		set sndT [$self mobility-trace Send "RTR"]
		set namfp [$ns get-nam-traceall]
		if {$namfp != "" } {
			$sndT namattach $namfp
		}
		$sndT target $ll_(0)
		$ragent_ add-ll $sndT 
	} else {
		# Send Target
		$ragent_ add-ll $ll_(0) 
	}

	$ragent_ if-queue $ifq_(0)
}

Node/MobileNode/AODVNode instproc reset args {
	$self instvar ragent_
	eval $self next $args
	$ragent_ reset
}
