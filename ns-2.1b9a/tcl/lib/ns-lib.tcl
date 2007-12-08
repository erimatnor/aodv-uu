# -*-	Mode:tcl; tcl-indent-level:8; tab-width:8; indent-tabs-mode:t -*-
#
# Copyright (c) 1996 Regents of the University of California.
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
# Word of warning to developers:
# this code (and all it sources) is compiled into the
# ns executable.  You need to rebuild ns or explicitly
# source this code to see changes take effect.
#



proc warn {msg} {
	global warned_
	if {![info exists warned_($msg)]} {
		puts stderr "warning: $msg"
		set warned_($msg) 1
	}
}

if {[info commands debug] == ""} {
	proc debug args {
		warn {Script debugging disabled.  Reconfigure with --with-tcldebug, and recompile.}
	}
}

proc assert args {
        if [catch "expr $args" ret] {
                set ret [eval expr $args]
        }
        if {! $ret} {
                error "assertion failed: $args"
        }
}

proc find-max list {
	set max 0
	foreach val $list {
		if {$val > $max} {
			set max $val
		}
	}
	return $max
}

proc bw_parse { bspec } {
	if { [scan $bspec "%f%s" b unit] == 1 } {
		set unit bps
	}
	regsub {[/p]s(ec)?$} $unit {} unit
	if [string match {*B} $unit] {
		set b [expr $b*8]
		set unit "[string trimright $unit B]b"
	}
	switch $unit {
		b { return $b }
		kb { return [expr $b*1000] }
		Mb { return [expr $b*1000000] }
		Gb { return [expr $b*1000000000] }
		default { 
			puts "error: bw_parse: unknown unit `$unit'" 
			exit 1
		}
	}
}

proc time_parse { spec } {
	if { [scan $spec "%f%s" t unit] == 1 } {
		set unit s
	}
	regsub {sec$} $unit {s} unit
	switch $unit {
		s { return $t }
		ms { return [expr $t*1e-3] }
		us { return [expr $t*1e-6] }
		ns { return [expr $t*1e-9] }
		ps { return [expr $t*1e-12] }
		default { 
			puts "error: time_parse: unknown unit `$unit'" 
			exit 1
		}
	}
}

proc delay_parse { spec } {
	return [time_parse $spec]
}

#
# Create the core OTcl class called "Simulator".
# This is the principal interface to the simulation engine.
#
#Class Simulator

#
# XXX Whenever you modify the source list below, please also change the
# OTcl script dependency list in Makefile.in
#
source ns-autoconf.tcl
source ns-address.tcl
source ns-node.tcl
source ns-rtmodule.tcl
source ns-hiernode.tcl
source ns-mobilenode.tcl
source ns-bsnode.tcl
source ns-link.tcl
source ns-source.tcl
source ns-compat.tcl
source ns-packet.tcl
source ns-queue.tcl
source ns-trace.tcl
source ns-random.tcl
source ns-agent.tcl
source ns-route.tcl
source ns-errmodel.tcl
source ns-intserv.tcl
source ns-cmutrace.tcl
source ns-mip.tcl
source ns-sat.tcl
#source ns-nix.tcl
source ns-diffusion.tcl
source ../rtp/session-rtp.tcl
source ../interface/ns-iface.tcl
source ../lan/ns-mac.tcl
source ../lan/ns-ll.tcl
source ../lan/vlan.tcl
source ../lan/abslan.tcl
source ../mcast/timer.tcl
source ../mcast/ns-mcast.tcl
source ns-srcrt.tcl
source ../mcast/McastProto.tcl
source ../mcast/DM.tcl
source ../ctr-mcast/CtrMcast.tcl
source ../ctr-mcast/CtrMcastComp.tcl
source ../ctr-mcast/CtrRPComp.tcl
source ../mcast/BST.tcl
source ../mcast/srm.tcl
source ../mcast/srm-ssm.tcl
source ../mcast/mftp_snd.tcl
source ../mcast/mftp_rcv.tcl
source ../mcast/mftp_rcv_stat.tcl
source ../mcast/McastMonitor.tcl
source ../rlm/rlm.tcl
source ../rlm/rlm-ns.tcl
source ../session/session.tcl
source ../webcache/http-server.tcl
source ../webcache/http-cache.tcl
source ../webcache/http-agent.tcl
source ../webcache/http-mcache.tcl
source ../webcache/webtraf.tcl
source ../webcache/empweb.tcl
source ns-namsupp.tcl
source ../mobility/dsdv.tcl
source ../mobility/dsr.tcl
source ../mobility/com.tcl

source ../plm/plm.tcl
source ../plm/plm-ns.tcl
source ../plm/plm-topo.tcl

# MPLS
source ../mpls/ns-mpls-simulator.tcl
source ../mpls/ns-mpls-node.tcl
source ../mpls/ns-mpls-ldpagent.tcl
source ../mpls/ns-mpls-classifier.tcl

source ns-default.tcl
source ../emulate/ns-emulate.tcl

#pushback
source ns-pushback.tcl

# PGM
#source ../pgm/ns-pgm.tcl

# STL dependent modules get included
# ONLY when STL is found

if {[ns-hasSTL] == 1} {
source ns-nix.tcl
source ../pgm/ns-pgm.tcl
source ../rtglib/ns-rtProtoLS.tcl
}

# Obsolete modules
#source ns-wireless-mip.tcl
#source ns-nam.tcl

Simulator instproc init args {

	# Debojyoti added this for asim 

	$self instvar useasim_
	$self instvar slinks_
	$self instvar nconn_
	$self instvar sflows_
	$self instvar nsflows_

	set slinks_(0:0) 0
	set nconn_ 0
	set conn_ ""
	# for short flows stuff
	set sflows_ "" 
	set nsflows_ 0
	set useasim_ 0

	$self create_packetformat
	$self use-scheduler Calendar
	#$self use-scheduler List
	$self set nullAgent_ [new Agent/Null]
	$self set-address-format def
	if {[lindex $args 0] == "-multicast"} {
		$self multicast $args
	}
	eval $self next $args
}

Simulator instproc nullagent {} {
	$self instvar nullAgent_
	return $nullAgent_
}

Simulator instproc use-scheduler type {
	$self instvar scheduler_
	if [info exists scheduler_] {
		if { [$scheduler_ info class] == "Scheduler/$type" } {
			return
		} else {
			delete $scheduler_
		}
	}
	set scheduler_ [new Scheduler/$type]
	$scheduler_ now
}

Simulator instproc delay_parse { spec } {
	return [time_parse $spec]
}

Simulator instproc bw_parse { spec } {
	return [bw_parse $spec]
}

#
# A simple method to wrap any object around
# a trace object that dumps to stdout
#
Simulator instproc dumper obj {
	set t [$self alloc-trace hop stdout]
	$t target $obj
	return $t
}

# New node structure
#
# Add APT to support multi-interface: user can specified multiple channels
# when config nod. Still need modifications in routing agents to make
# multi-interfaces really work.   -chen xuan  07/21/00
#
# Define global node configuration
# $ns_ node-config -addressType flat/hierarchical
#                  -adhocRouting   DSDV/DSR/TORA
#                  -llType
#                  -macType
#                  -propType
#                  -ifqType
#                  -ifqLen
#                  -phyType
#                  -antType
#		   -channel
#                  -channelType
#                  -topologyInstance
#                  -wiredRouting   ON/OFF
#                  -mobileIP       ON/OFF
#                  -energyModel    "EnergyModel"
#                  -initialEnergy  (in Joules)
#                  -rxPower        (in W)
#                  -txPower        (in W)
#                  -idlePower      (in W)
#                  -agentTrace  ON
#                  -routerTrace ON 
#                  -macTrace OFF 
#                  -toraDebug OFF                
#                  -movementTrace OFF

Simulator instproc addressType  {val} { $self set addressType_  $val }
Simulator instproc adhocRouting  {val} { $self set routingAgent_  $val }
Simulator instproc llType  {val} { $self set llType_  $val }
Simulator instproc macType  {val} { $self set macType_  $val }
Simulator instproc propType  {val} { $self set propType_  $val }
Simulator instproc propInstance  {val} { $self set propInstance_  $val }
Simulator instproc ifqType  {val} { $self set ifqType_  $val }
Simulator instproc ifqLen  {val} { $self set ifqlen_  $val }
Simulator instproc phyType  {val} { $self set phyType_  $val }
Simulator instproc antType  {val} { $self set antType_  $val }
Simulator instproc channel {val} {$self set channel_ $val}
Simulator instproc channelType {val} {$self set channelType_ $val}
Simulator instproc topoInstance {val} {$self set topoInstance_ $val}
Simulator instproc wiredRouting {val} {$self set wiredRouting_ $val}
Simulator instproc mobileIP {val} {$self set mobileIP_ $val}
Simulator instproc energyModel  {val} { $self set energyModel_  $val }
Simulator instproc initialEnergy  {val} { $self set initialEnergy_  $val }
Simulator instproc txPower  {val} { $self set txPower_  $val }
Simulator instproc rxPower  {val} { $self set rxPower_  $val }
Simulator instproc idlePower  {val} { $self set idlePower_  $val }
Simulator instproc IncomingErrProc  {val} { $self set inerrProc_  $val }
Simulator instproc OutgoingErrProc  {val} { $self set outerrProc_  $val }
Simulator instproc FECProc  {val} { $self set FECProc_  $val }
Simulator instproc agentTrace  {val} { $self set agentTrace_  $val }
Simulator instproc routerTrace  {val} { $self set routerTrace_  $val }
Simulator instproc macTrace  {val} { $self set macTrace_  $val }
Simulator instproc movementTrace  {val} { $self set movementTrace_  $val }
Simulator instproc toraDebug {val} {$self set toraDebug_ $val }
Simulator instproc satNodeType {val} {$self set satNodeType_ $val}
Simulator instproc downlinkBW {val} {$self set downlinkBW_ $val}
Simulator instproc MPLS { val } { 
	if { $val == "ON" } {
		Node enable-module "MPLS"
	} else {
		Node disable-module "MPLS"
	}
}

Simulator instproc PGM { val } { 
        if { $val == "ON" } {
                Node enable-module "PGM"
        } else {
                Node disable-module "PGM"
        }
}

Simulator instproc get-nodetype {} {
	$self instvar addressType_ routingAgent_ wiredRouting_ 
	set val ""

	if { [info exists addressType_] && $addressType_ == "hierarchical" } {
		set val Hier
	}
	if { [info exists routingAgent_] && $routingAgent_ != "" } {
		set val Mobile
	}
	if { [info exists wiredRouting_] && $wiredRouting_ == "ON" } {
		set val Base
	}
	if { [info exists wiredRouting_] && $wiredRouting_ == "OFF"} {
		set val Base
	}
	if { [Simulator set mobile_ip_] } {
		if { $val == "Base" && $wiredRouting_ == "ON" } {
			set val MIPBS
		}
		if { $val == "Base" && $wiredRouting_ == "OFF" } {
			set val MIPMH
		}
	}
	return $val
}

Simulator instproc node-config args {
        # Object::init-vars{} is defined in ~tclcl/tcl-object.tcl.
        # It initializes all default variables in the following way:
        #  1.  Look for pairs of {-cmd val} in args
        #  2.  If "$self $cmd $val" is not valid then put it in a list of 
        #      arguments to be returned to the caller.
        # 
        # Since we do not handle undefined {-cmd val} pairs, we ignore 
        # return value from init-vars{}.
        set args [eval $self init-vars $args]

        $self instvar addressType_  routingAgent_ propType_  macTrace_ \
		routerTrace_ agentTrace_ movementTrace_ channelType_ channel_ \
		chan topoInstance_ propInstance_ mobileIP_ rxPower_ \
		txPower_ idlePower_ satNodeType_

        if [info exists macTrace_] {
		Simulator set MacTrace_ $macTrace_
	}
        if [info exists routerTrace_] {
		Simulator set RouterTrace_ $routerTrace_
	}
        if [info exists agentTrace_] {
		Simulator set AgentTrace_ $agentTrace_
	}
        if [info exists movementTrace_] {
		Simulator set MovementTrace_ $movementTrace_
	}
        # hacking for matching old cmu add-interface
        # not good style, for back-compability ONLY
	#
	# Only create 1 instance of prop
	if {[info exists propInstance_]} {
		if {[info exists propType_] && [Simulator set propInstCreated_] == 0} {
			warn "Both propType and propInstance are set. propType is ignored."
		}
	} else {
		if {[info exists propType_]} {
			set propInstance_ [new $propType_]
			Simulator set propInstCreated_ 1
		}
	}
	
	# Add multi-interface support:
 	# User can only specify either channelType_ (single_interface as 
	# before) or channel_ (multi_interface)
 	# If both variables are specified, error!
 	if {[info exists channelType_] && [info exists channel_]} { 
		error "Can't specify both channel and channelType, error!"
	} elseif {[info exists channelType_] && ![info exists satNodeType_]} {
		# Single channel, single interface
		warn "Please use -channel as shown in tcl/ex/wireless-mitf.tcl"
		if {![info exists chan]} {
			set chan [new $channelType_]
		}
 	} elseif {[info exists channel_]} {
		# Multiple channel, multiple interfaces
		set chan $channel_
 	}
	if [info exists topoInstance_] {
		$propInstance_  topography $topoInstance_
	}
	# set address type, hierarchical or expanded
	if {[string compare $addressType_ ""] != 0} {
		$self set-address-format $addressType_ 
	}
	# set mobileIP flag
	if { [info exists mobileIP_] && $mobileIP_ == "ON"} {
		Simulator set mobile_ip_  1
	} else {
		if { [info exists mobileIP_] } {
			Simulator set mobile_ip_ 0
		}
	}
}

# Default behavior is changed: consider nam as not initialized if 
# no shape OR color parameter is given
Simulator instproc node args {
	$self instvar Node_ routingAgent_ wiredRouting_ satNodeType_
        if { [Simulator info vars EnableMcast_] != "" } {
                warn "Flag variable Simulator::EnableMcast_ discontinued.\n\t\
                      Use multicast methods as:\n\t\t\
                        % set ns \[new Simulator -multicast on]\n\t\t\
                        % \$ns multicast"
                $self multicast
                Simulator unset EnableMcast_
        }
        if { [Simulator info vars NumberInterfaces_] != "" } {
                warn "Flag variable Simulator::NumberInterfaces_ discontinued.\n\t\
                      Setting this variable will not affect simulations."
                Simulator unset NumberInterfaces_
        }
	
	# wireless-ready node
	if { [info exists routingAgent_] && ($routingAgent_ != "") } {
		set node [eval $self create-wireless-node $args]
		# for base node
		if {[info exists wiredRouting_] && $wiredRouting_ == "ON"} {
			set Node_([$node id]) $node
			#simulator's nodelist in C++ space
			$self add-node $node [$node id] 
		}
		return $node
	}
	# Satellite node
	if { [info exists satNodeType_] } {
		set node [eval $self create-satnode]
		#simulator's nodelist in C++ space
		if {[info exists wiredRouting_] && $wiredRouting_ == "ON"} {
			# add node to simulator's nodelist in C++ space
			$self add-node $node [$node id]
			# Want to keep global state of wiredRouting info
			SatRouteObject set wiredRouting_ true
		}
		return $node
	}

	# Enable-mcast is now done automatically inside Node::init{}
	# 
	# XXX node_factory_ is deprecated, HOWEVER, since it's still used by
	# mobile IP, algorithmic routing, manual routing, and backward 
	# compability tests of hierarchical routing, we should keep it around
	# before all related code are wiped out.
	set node [eval new [Simulator set node_factory_] $args]
	set Node_([$node id]) $node
	
	#add to simulator's nodelist in C++ space
	$self add-node $node [$node id] 

	#set the nodeid in c++ Node - ratul
	$node nodeid [$node id]

	$node set ns_ $self
	$self check-node-num
	return $node
}

# XXX This is stupid hack. When old code (not using node-config) is used, 
# create-wireless-node{} will not be called, and IMEPFlag_ will remain empty
# (as set in ns-default.tcl), then Node/MobileNode will use global proc 
# cmu-trace to create trace objects; otherwise mobility-trace{} will be 
# triggered.
Simulator instproc imep-support {} {
	return [Simulator set IMEPFlag_]
}

# XXX This should be moved into the node initialization procedure instead 
# of standing here in ns-lib.tcl.
Simulator instproc create-wireless-node args {
        $self instvar routingAgent_ wiredRouting_ propInstance_ llType_ \
		macType_ ifqType_ ifqlen_ phyType_ chan antType_ energyModel_ \
		initialEnergy_ txPower_ rxPower_ idlePower_ \
		topoInstance_ level1_ level2_ inerrProc_ outerrProc_ FECProc_

	Simulator set IMEPFlag_ OFF

        # create node instance
        set node [eval $self create-node-instance $args]
        
        # basestation address setting
        if { [info exist wiredRouting_] && $wiredRouting_ == "ON" } {
		$node base-station [AddrParams addr2id [$node node-addr]]
    	}
        switch -exact $routingAgent_ {
	    DSDV {
		    set ragent [$self create-dsdv-agent $node]
	    }
	    DSR {
		    $self at 0.0 "$node start-dsr"
	    }
	    AODV {
		    set ragent [$self create-aodv-agent $node]
	    }

	    # AODV-UU routing agent
	    AODVUU {
		    set ragent [$self create-aodvuu-agent $node]
	    }

	    TORA {
		    Simulator set IMEPFlag_ ON
		    set ragent [$self create-tora-agent $node]
	    }
	    DIFFUSION/RATE {
		    eval $node addr $args
		    set ragent [$self create-diffusion-rate-agent $node]
	    }
	    DIFFUSION/PROB {
		    eval $node addr $args
		    set ragent [$self create-diffusion-probability-agent $node]
	    }
	    Directed_Diffusion {
		    eval $node addr $args
		    set ragent [$self create-core-diffusion-rtg-agent $node]
	    }
	    FLOODING {
		    eval $node addr $args
		    set ragent [$self create-flooding-agent $node]
	    }
	    OMNIMCAST {
		    eval $node addr $args
		    set ragent [$self create-omnimcast-agent $node]
	    }
	    default {
		    puts "Wrong node routing agent!"
		    exit
	    }
	}

	# errProc_ and FECProc_ are an option unlike other 
        # parameters for node interface
	if ![info exist inerrProc_] {
		set inerrProc_ ""
	}
	if ![info exist outerrProc_] {
		set outerrProc_ ""
	}
	if ![info exist FECProc_] {
		set FECProc_ ""
	}

	# Add main node interface
	$node add-interface $chan $propInstance_ $llType_ $macType_ \
			$ifqType_ $ifqlen_ $phyType_ $antType_ \
			$inerrProc_ $outerrProc_ $FECProc_
	# Attach agent
	if {$routingAgent_ != "DSR"} {
		$node attach $ragent [Node set rtagent_port_]
	}
	if {$routingAgent_ == "DIFFUSION/RATE" ||
            $routingAgent_ == "DIFFUSION/PROB" ||
            $routingAgent_ == "FLOODING" ||
            $routingAgent_ == "OMNIMCAST" ||
	    $routingAgent_ == "Directed_Diffusion" } {
		$ragent port-dmux [$node demux]
		$node instvar ll_
		$ragent add-ll $ll_(0)
	}

	# Bind routing agent and mip agent if existing basestation 
	# address setting
        if { [info exist wiredRouting_] && $wiredRouting_ == "ON" } {
		if { $routingAgent_ != "DSR" } {
			$node mip-call $ragent
		}
	}
	#
        # This Trace Target is used to log changes in direction
        # and velocity for the mobile node.
        #
	set tracefd [$self get-ns-traceall]
        if {$tracefd != "" } {
		$node nodetrace $tracefd
		$node agenttrace $tracefd
	}
	set namtracefd [$self get-nam-traceall]
	if {$namtracefd != "" } {
		$node namattach $namtracefd
	}
	if [info exists energyModel_] {
		if  [info exists level1_] {
			set l1 $level1_
		} else {
			set l1 0.5
		}
		if  [info exists level2_] {
			set l2 $level2_
		} else {
			set l2 0.2
		}
		$node addenergymodel [new $energyModel_ $node \
				$initialEnergy_ $l1 $l2]
        }
        if [info exists txPower_] {
		$node setPt $txPower_
        }
        if [info exists rxPower_] {
		$node setPr $rxPower_
        }
        if [info exists idlePower_] {
		$node setPidle $idlePower_
        }
	$node topography $topoInstance_
	return $node
}

Simulator instproc create-node-instance args {
	$self instvar routingAgent_
	# DSR is a special case
	if {$routingAgent_ == "DSR"} {
		set nodeclass [$self set-dsr-nodetype]
	} else {
		set nodeclass Node/MobileNode
	}
	return [eval new $nodeclass $args]
}

Simulator instproc set-dsr-nodetype {} {
	$self instvar wiredRouting_ 
	set nodetype SRNodeNew
	# MIP mobilenode
	if [Simulator set mobile_ip_] {
		set nodetype SRNodeNew/MIPMH
	} 
	# basestation dsr node
	if { [info exists wiredRouting_] && $wiredRouting_ == "ON"} {
		set nodetype Node/MobileNode/BaseStationNode
	}
	return $nodetype
}

Simulator instproc create-tora-agent { node } {
        set ragent [new Agent/TORA [$node id]]
	$node set ragent_ $ragent
	return $ragent
}

Simulator instproc create-dsdv-agent { node } {
	# Create a dsdv routing agent for this node
	set ragent [new Agent/DSDV]
	# Setup address (supports hier-addr) for dsdv agent
	# and mobilenode
	set addr [$node node-addr]
	$ragent addr $addr
	$ragent node $node
	if [Simulator set mobile_ip_] {
		$ragent port-dmux [$node demux]
	}
	$node addr $addr
	$node set ragent_ $ragent
	$self at 0.0 "$ragent start-dsdv"    ;# start updates
	return $ragent
}

Simulator instproc create-aodv-agent { node } {
        #  Create AODV routing agent
        set ragent [new Agent/AODV [$node id]]
        $self at 0.0 "$ragent start"     ;# start BEACON/HELLO Messages
        $node set ragent_ $ragent
        return $ragent
}

# AODV-UU routing agent
Simulator instproc create-aodvuu-agent { node } {
	# Create AODV-UU routing agent
	set ragent [new Agent/AODVUU [$node id]]
	$self at 0.0 "$ragent start"     ;# start BEACON/HELLO messages
	$node set ragent_ $ragent
	return $ragent
}

Simulator instproc use-newtrace {} {
	Simulator set WirelessNewTrace_ 1
} 

Simulator instproc use-taggedtrace { {tag ON} } {
	Simulator set TaggedTrace_ $tag
}

Simulator instproc hier-node haddr {
 	error "hier-nodes should be created with [$ns_ node $haddr]"
}

Simulator instproc now {} {
	$self instvar scheduler_
	return [$scheduler_ now]
}

Simulator instproc at args {
	$self instvar scheduler_
	return [eval $scheduler_ at $args]
}

Simulator instproc at-now args {
	$self instvar scheduler_
	return [eval $scheduler_ at-now $args]
}

Simulator instproc cancel args {
	$self instvar scheduler_
	return [eval $scheduler_ cancel $args]
}

Simulator instproc after {ival args} {
        eval $self at [expr [$self now] + $ival] $args
}

#
# check if total num of nodes exceed 2 to the power n 
# where <n=node field size in address>
#
Simulator instproc check-node-num {} {
	if {[Node set nn_] > [expr pow(2, [AddrParams nodebits])]} {
		error "Number of nodes exceeds node-field-size of [AddrParams nodebits] bits"
	}
}

#
# Check if number of items at each hier level (num of nodes, or clusters or
# domains) exceed size of that hier level field size (in bits). should be 
# modified to support n-level of hierarchies
#
Simulator instproc chk-hier-field-lengths {} {
	AddrParams instvar domain_num_ cluster_num_ nodes_num_
	if [info exists domain_num_] {
		if {[expr $domain_num_ - 1]> [AddrParams NodeMask 1]} {
			error "\# of domains exceed dom-field-size "
		}
	} 
	if [info exists cluster_num_] {
		set maxval [expr [find-max $cluster_num_] - 1] 
		if {$maxval > [expr pow(2, [AddrParams NodeMask 2])]} {
			error "\# of clusters exceed clus-field-size "
		}
	}
	if [info exists nodes_num_] {
		set maxval [expr [find-max $nodes_num_] -1]
		if {$maxval > [expr pow(2, [AddrParams NodeMask 3])]} {
			error "\# of nodess exceed node-field-size"
		}
	}
}

Simulator instproc run {} {
	# NIXVECTOR?
	# global runstart
	# set runstart [clock seconds]
	$self check-node-num
	$self rtmodel-configure			;# in case there are any
	[$self get-routelogic] configure
	$self instvar scheduler_ Node_ link_ started_ 
	
	set started_ 1
	
	#
	# Reset every node, which resets every agent.
	#

	foreach nn [array names Node_] {
		$Node_($nn) reset
		# GFR Additions for NixVector Routing
		if { [Simulator set nix-routing] } {
			$Node_($nn) populate-objects
		}
	}

	#
	# Also reset every queue
	#

	foreach qn [array names link_] {
		set q [$link_($qn) queue]
		$q reset
	}

	# Do all nam-related initialization here
	$self init-nam

	# NIXVECTOR xxx?
	# global simstart
	# set simstart [clock seconds]
	return [$scheduler_ run]
}

# johnh xxx?
Simulator instproc log-simstart { } {
        # GFR Modification to log actual start
        global simstart
        puts "Starting Actual Simulation"
        set simstart [clock seconds]
}

Simulator instproc halt {} {
	$self instvar scheduler_
	$scheduler_ halt
}

Simulator instproc dumpq {} {
	$self instvar scheduler_
	$scheduler_ dumpq
}

Simulator instproc is-started {} {
	$self instvar started_
	return [info exists started_]
}

Simulator instproc clearMemTrace {} {
	$self instvar scheduler_
	$scheduler_ clearMemTrace
}

Simulator instproc simplex-link { n1 n2 bw delay qtype args } {
	$self instvar link_ queueMap_ nullAgent_ useasim_
	set sid [$n1 id]
	set did [$n2 id]

	# Debo
	if { $useasim_ == 1 } {
		set slink_($sid:$did) $self
	}

	if [info exists queueMap_($qtype)] {
		set qtype $queueMap_($qtype)
	}
	# construct the queue
	set qtypeOrig $qtype
	switch -exact $qtype {
	    ErrorModule {
		if { [llength $args] > 0 } {
		    set q [eval new $qtype $args]
		} else {
		    set q [new $qtype Fid]
		}
	    }
	    intserv {
		set qtype [lindex $args 0]
		set q [new Queue/$qtype]
	    }
	    default {
		if { [llength $args] == 0} {
		    set q [new Queue/$qtype]
		} else {
		    set q [new Queue/$qtype $args]
		}
	    }
	}
	# Now create the link
	switch -exact $qtypeOrig {
		RTM {
                        set c [lindex $args 1]
                        set link_($sid:$did) [new CBQLink       \
                                        $n1 $n2 $bw $delay $q $c]
                }
                CBQ -
                CBQ/WRR {
                        # assume we have a string of form "linktype linkarg"
                        if {[llength $args] == 0} {
                                # default classifier for cbq is just Fid type
                                set c [new Classifier/Hash/Fid 33]
                        } else {
                                set c [lindex $args 0]
                        }
                        set link_($sid:$did) [new CBQLink       \
                                        $n1 $n2 $bw $delay $q $c]
                }
		FQ      {
			set link_($sid:$did) [new FQLink $n1 $n2 $bw $delay $q]
		}
                intserv {
                        #XX need to clean this up
                        set link_($sid:$did) [new IntServLink   \
                                        $n1 $n2 $bw $delay $q	\
						[concat $qtypeOrig $args]]
                }
                default {
                        set link_($sid:$did) [new SimpleLink    \
                                        $n1 $n2 $bw $delay $q]
                }
        }
	if {$qtype == "RED/Pushback"} {
		set pushback 1
	} else {
		set pushback 0
	}
	$n1 add-neighbor $n2 $pushback
	
	#XXX yuck
	if {[string first "RED" $qtype] != -1 || 
		[string first "PI" $qtype] != -1 || 
		[string first "Vq" $qtype] != -1 ||
		[string first "REM" $qtype] != -1 ||  
		[string first "GK" $qtype] != -1 ||  
		[string first "RIO" $qtype] != -1} {
		$q link [$link_($sid:$did) set link_]
	}

	set trace [$self get-ns-traceall]
	if {$trace != ""} {
		$self trace-queue $n1 $n2 $trace
	}
	set trace [$self get-nam-traceall]
	if {$trace != ""} {
		$self namtrace-queue $n1 $n2 $trace
	}
	
	# Register this simplex link in nam link list. Treat it as 
	# a duplex link in nam
	$self register-nam-linkconfig $link_($sid:$did)
}

#
# This is used by Link::orient to register/update the order in which links 
# should created in nam. This is important because different creation order
# may result in different layout.
#
# A poor hack. :( Any better ideas?
#
Simulator instproc register-nam-linkconfig link {
	$self instvar linkConfigList_ link_
	if [info exists linkConfigList_] {
		# Check whether the reverse simplex link is registered,
		# if so, don't register this link again.
		# We should have a separate object for duplex link.
		set i1 [[$link src] id]
		set i2 [[$link dst] id]
		if [info exists link_($i2:$i1)] {
			set pos [lsearch $linkConfigList_ $link_($i2:$i1)]
			if {$pos >= 0} {
				set a1 [$link_($i2:$i1) get-attribute \
						"ORIENTATION"]
				set a2 [$link get-attribute "ORIENTATION"]
				if {$a1 == "" && $a2 != ""} {
					# If this duplex link has not been 
					# assigned an orientation, do it.
					set linkConfigList_ [lreplace \
						$linkConfigList_ $pos $pos]
				} else {
					return
				}
			}
		}
		# Remove $link from list if it's already there
		set pos [lsearch $linkConfigList_ $link]
		if {$pos >= 0} {
			set linkConfigList_ \
					[lreplace $linkConfigList_ $pos $pos]
		}
	}
	lappend linkConfigList_ $link
}

#
# GT-ITM may occasionally generate duplicate links, so we need this check
# to ensure duplicated links do not appear in nam trace files.
#
Simulator instproc remove-nam-linkconfig {i1 i2} {
	$self instvar linkConfigList_ link_
	if ![info exists linkConfigList_] {
		return
	}
	set pos [lsearch $linkConfigList_ $link_($i1:$i2)]
	if {$pos >= 0} {
		set linkConfigList_ [lreplace $linkConfigList_ $pos $pos]
		return
	}
	set pos [lsearch $linkConfigList_ $link_($i2:$i1)]
	if {$pos >= 0} {
		set linkConfigList_ [lreplace $linkConfigList_ $pos $pos]
	}
}

Simulator instproc duplex-link { n1 n2 bw delay type args } {
	$self instvar link_
	set i1 [$n1 id]
	set i2 [$n2 id]
	if [info exists link_($i1:$i2)] {
		$self remove-nam-linkconfig $i1 $i2
	}
	eval $self simplex-link $n1 $n2 $bw $delay $type $args
	eval $self simplex-link $n2 $n1 $bw $delay $type $args
	# Modified by GFR for nix-vector routing
	if { [Simulator set nix-routing] } {
		# Inform nodes of neighbors
		$n1 set-neighbor [$n2 id]
		$n2 set-neighbor [$n1 id]
	}
}

Simulator instproc duplex-intserv-link { n1 n2 bw pd sched signal adc args } {
	eval $self duplex-link $n1 $n2 $bw $pd intserv $sched $signal $adc $args
}

Simulator instproc simplex-link-op { n1 n2 op args } {
	$self instvar link_
	eval $link_([$n1 id]:[$n2 id]) $op $args
}

Simulator instproc duplex-link-op { n1 n2 op args } {
	$self instvar link_
	eval $link_([$n1 id]:[$n2 id]) $op $args
	eval $link_([$n2 id]:[$n1 id]) $op $args
}

Simulator instproc flush-trace {} {
	$self instvar alltrace_
	if [info exists alltrace_] {
		foreach trace $alltrace_ {
			$trace flush
		}
	}
}

Simulator instproc namtrace-all file   {
	$self instvar namtraceAllFile_
	if {$file != ""} {
		set namtraceAllFile_ $file
	} else {
		unset namtraceAllFile_
	}
}

Simulator instproc energy-color-change {level1 level2} {
	$self instvar level1_ level2_
 	set level1_ $level1
 	set level2_ $level2
}

Simulator instproc namtrace-all-wireless {file optx opty} {
        $self instvar namtraceAllFile_
        if {$file != ""} {
                set namtraceAllFile_ $file
        } else {
                unset namtraceAllFile_
        }
	if { $optx != "" && $opty != "" } {
		$self puts-nam-config "W -t * -x $optx -y $opty"
	}
}

Simulator instproc nam-end-wireless {stoptime} {
        $self instvar namtraceAllFile_

        if {$namtraceAllFile_ != ""} {
		$self puts-nam-config "W -t $stoptime"
        }
}

Simulator instproc namtrace-some file {
	$self instvar namtraceSomeFile_
	set namtraceSomeFile_ $file
}

Simulator instproc namtrace-all-wireless {file optx opty} {
        $self instvar namtraceAllFile_  
 
        if {$file != ""} { 
                set namtraceAllFile_ $file
        } else {
                unset namtraceAllFile_
        }       
        $self puts-nam-config "W -t * -x $optx -y $opty"
}

# Support for event-tracing
        
Simulator instproc eventtrace-all {{file ""}} {
	$self instvar eventTraceAll_ eventtraceAllFile_ traceAllFile_
	set eventTraceAll_ 1
	if {$file != ""} {
		set eventtraceAllFile_ $file
	} else {
		set eventtraceAllFile_ $traceAllFile_
	}
	
}



Simulator instproc initial_node_pos {nodep size} {
	$self instvar addressType_
	$self instvar energyModel_ 

	if [info exists energyModel_] {  
		set nodeColor "green"
	} else {
		set nodeColor "black"
	}
	if { [info exists addressType_] && $addressType_ == "hierarchical" } {
		# Hierarchical addressing
		$self puts-nam-config "n -t * -a [$nodep set address_] \
-s [$nodep id] -x [$nodep set X_] -y [$nodep set Y_] -Z [$nodep set Z_] \
-z $size -v circle -c $nodeColor"
	} else { 
		# Flat addressing
		$self puts-nam-config "n -t * -s [$nodep id] \
-x [$nodep set X_] -y [$nodep set Y_] -Z [$nodep set Z_] -z $size \
-v circle -c $nodeColor"
	}
}

Simulator instproc trace-all file {
	$self instvar traceAllFile_
	set traceAllFile_ $file
}

Simulator instproc get-nam-traceall {} {
	$self instvar namtraceAllFile_
	if [info exists namtraceAllFile_] {
		return $namtraceAllFile_
	} else {
		return ""
	}
}

Simulator instproc get-ns-traceall {} {
	$self instvar traceAllFile_
	if [info exists traceAllFile_] {
		return $traceAllFile_
	} else {
		return ""
	}
}

# If exists a traceAllFile_, print $str to $traceAllFile_
Simulator instproc puts-ns-traceall { str } {
	$self instvar traceAllFile_
	if [info exists traceAllFile_] {
		puts $traceAllFile_ $str
	}
}

# If exists a traceAllFile_, print $str to $traceAllFile_
Simulator instproc puts-nam-traceall { str } {
	$self instvar namtraceAllFile_
	if [info exists namtraceAllFile_] {
		puts $namtraceAllFile_ $str
	} elseif [info exists namtraceSomeFile_] {
		puts $namtraceSomeFile_ $str
	}
}

# namConfigFile is used for writing color/link/node/queue/annotations. 
# XXX It cannot co-exist with namtraceAll.
Simulator instproc namtrace-config { f } {
	$self instvar namConfigFile_
	set namConfigFile_ $f
}

Simulator instproc get-nam-config {} {
	$self instvar namConfigFile_
	if [info exists namConfigFile_] {
		return $namConfigFile_
	} else {
		return ""
	}
}

# Used only for writing nam configurations to trace file(s). This is different
# from puts-nam-traceall because we may want to separate configuration 
# informations and actual tracing information
Simulator instproc puts-nam-config { str } {
	$self instvar namtraceAllFile_ namConfigFile_
	
	if [info exists namConfigFile_] {
		puts $namConfigFile_ $str
	} elseif [info exists namtraceAllFile_] {
		puts $namtraceAllFile_ $str
	} elseif [info exists namtraceSomeFile_] {
		puts $namtraceSomeFile_ $str
	}
}

Simulator instproc color { id name } {
	$self instvar color_
	set color_($id) $name
}

Simulator instproc get-color { id } {
	$self instvar color_
	return $color_($id)
}

# you can pass in {} as a null file
Simulator instproc create-trace { type file src dst {op ""} } {
	$self instvar alltrace_
	set p [new Trace/$type]
	$p tagged [Simulator set TaggedTrace_]
	if [catch {$p set src_ [$src id]}] {
		$p set src_ $src
	}
	if [catch {$p set dst_ [$dst id]}] {
		$p set dst_ $dst
	}
	lappend alltrace_ $p
	if {$file != ""} {
		$p ${op}attach $file		
	}
	return $p
}


Simulator instproc create-eventtrace {type owner } {
	$self instvar alltrace_ 
	$self instvar eventTraceAll_ eventtraceAllFile_ namtraceAllFile_
	
	if ![info exists eventTraceAll_] return

	if { $eventTraceAll_ == 1 } {
		
		set et [new BaseTrace/$type]
		$owner cmd eventtrace $et
		
		lappend alltrace_ $et
		$et attach $eventtraceAllFile_
		if [info exists namtraceAllFile_] {
			$et namattach $namtraceAllFile_
		}
	}
}


Simulator instproc namtrace-queue { n1 n2 {file ""} } {
	$self instvar link_ namtraceAllFile_
	if {$file == ""} {
		if ![info exists namtraceAllFile_] return
		set file $namtraceAllFile_
	}
	$link_([$n1 id]:[$n2 id]) nam-trace $self $file
    
	# Added later for queue specific tracing events other than enque, 
	# deque and drop as of now nam does not understand special events. 
	# Changes will have to be made to nam for it to understand events 
	# like early drops if they are prefixed differently than "d". - ratul
	set queue [$link_([$n1 id]:[$n2 id]) queue]
	$queue attach-nam-traces $n1 $n2 $file
}

Simulator instproc trace-queue { n1 n2 {file ""} } {
	$self instvar link_ traceAllFile_
	if {$file == ""} {
		if ![info exists traceAllFile_] return
		set file $traceAllFile_
	}
	$link_([$n1 id]:[$n2 id]) trace $self $file
	
	# Added later for queue specific tracing events other than enque, 
	# deque and drop - ratul
	set queue [$link_([$n1 id]:[$n2 id]) queue]
	$queue attach-traces $n1 $n2 $file
}

#
# arrange for queue length of link between nodes n1 and n2
# to be tracked and return object that can be queried
# to learn average q size etc.  XXX this API still rough
#
Simulator instproc monitor-queue { n1 n2 qtrace { sampleInterval 0.1 } } {
	$self instvar link_
	return [$link_([$n1 id]:[$n2 id]) init-monitor $self $qtrace $sampleInterval]
}

Simulator instproc queue-limit { n1 n2 limit } {
	$self instvar link_
	[$link_([$n1 id]:[$n2 id]) queue] set limit_ $limit
}

Simulator instproc drop-trace { n1 n2 trace } {
	$self instvar link_
	[$link_([$n1 id]:[$n2 id]) queue] drop-target $trace
}

Simulator instproc cost {n1 n2 c} {
	$self instvar link_
	$link_([$n1 id]:[$n2 id]) cost $c
}

Simulator instproc attach-agent { node agent } {
	$node attach $agent
	# $agent set nodeid_ [$node id]
}

Simulator instproc attach-tbf-agent { node agent tbf } {
	$node attach $agent
	$agent attach-tbf $tbf
}


Simulator instproc detach-agent { node agent } {

	# Debo added this
	$self instvar conn_ nconn_ sflows_ nsflows_ useasim_

	if {$useasim_ == 1} {
		set list "" 
		set s [$node id]
		set d [[$self get-node-by-addr [$agent set dst_addr_]] id]
		foreach x $conn_ {
			set t [split $x ":"] 
			if {[string compare [lindex $t 0]:[lindex $t 1] $s:$d] != 0} {
				lappend list_ $x
			}
		}
		set conn_ list
		set nconn_ [expr $nconn_ -1]
		# ---------------------------------------
	}

	$self instvar nullAgent_
	$node detach $agent $nullAgent_
}

#
#   Helper proc for setting delay on an existing link
#
Simulator instproc delay { n1 n2 delay {type simplex} } {
	$self instvar link_
	set sid [$n1 id]
	set did [$n2 id]
	if [info exists link_($sid:$did)] {
		set d [$link_($sid:$did) link]
		$d set delay_ $delay
	}
	if {$type == "duplex"} {
		if [info exists link_($did:$sid)] {
			set d [$link_($did:$sid) link]
			$d set delay_ $delay
		}
	}
}

# 
#   Helper proc for setting bandwidth on an existing link
#
Simulator instproc bandwidth { n1 n2 bandwidth {type simplex} } {
        $self instvar link_
        set sid [$n1 id]
        set did [$n2 id]
        if [info exists link_($sid:$did)] {
                set d [$link_($sid:$did) link]
                $d set bandwidth_ $bandwidth
        } 
        if {$type == "duplex"} {
                if [info exists link_($did:$sid)] {
                        set d [$link_($did:$sid) link]
                        $d set bandwidth_ $bandwidth
                }
        }
}


#XXX need to check that agents are attached to nodes already
Simulator instproc connect {src dst} {

	$self instvar conn_ nconn_ sflows_ nsflows_ useasim_

	$self simplex-connect $src $dst
	$self simplex-connect $dst $src


	# Debo

	if {$useasim_ == 1} {
		set sid [$src nodeid]
		set sport [$src set agent_port_]
		set did [$dst nodeid]
		set dport [$dst set agent_port_]
		
		if {[lindex [split [$src info class] "/"] 1] == "TCP"} {
			lappend conn_ $sid:$did:$sport:$dport
			incr nconn_
			# set $nconn_ [expr $nconn_ + 1]
			# puts "Set a connection with id $nconn_ between $sid and $did"
		}
	}

	return $src
}

Simulator instproc simplex-connect { src dst } {
	$src set dst_addr_ [$dst set agent_addr_] 
	$src set dst_port_ [$dst set agent_port_]


        # Polly Huang: to support abstract TCP simulations
        if {[lindex [split [$src info class] "/"] 1] == "AbsTCP"} {
	    $self at [$self now] "$self rtt $src $dst"
	    $dst set class_ [$src set class_]
        }

	return $src
}

#
# Here are a bunch of helper methods.
#

Simulator proc instance {} {
	set ns [Simulator info instances]
	if { $ns != "" } {
		return $ns
	}
	foreach sim [Simulator info subclass] {
		set ns [$sim info instances]
		if { $ns != "" } {
			return $ns
		}
	}
	error "Cannot find instance of simulator"
}

Simulator instproc get-number-of-nodes {} {
	return  [$self array size Node_]
}

Simulator instproc get-node-by-id id {
	$self instvar Node_
	return $Node_($id)
}

# Given an node's address, Return the node-id
Simulator instproc get-node-id-by-addr address {
	$self instvar Node_
	set n [Node set nn_]
	for {set q 0} {$q < $n} {incr q} {
		set nq $Node_($q)
		if {[string compare [$nq node-addr] $address] == 0} {
			return $q
		}
	}
	error "get-node-id-by-addr:Cannot find node with given address"
}

# Given an node's address, return the node 
Simulator instproc get-node-by-addr address {
	return [$self get-node-by-id [$self get-node-id-by-addr $address]]
}

Simulator instproc all-nodes-list {} {
	$self instvar Node_
	set nodes ""
	foreach n [lsort -dictionary [array names Node_]] {
		lappend nodes $Node_($n)
	}
	return $nodes
}

Simulator instproc link { n1 n2 } {
        $self instvar Node_ link_
        if { ![catch "$n1 info class Node"] } {
		set n1 [$n1 id]
	}
        if { ![catch "$n2 info class Node"] } {
		set n2 [$n2 id]
	}
	if [info exists link_($n1:$n2)] {
		return $link_($n1:$n2)
	}
	return ""
}

# Creates connection. First creates a source agent of type s_type and binds
# it to source.  Next creates a destination agent of type d_type and binds
# it to dest.  Finally creates bindings for the source and destination agents,
# connects them, and  returns the source agent.
Simulator instproc create-connection {s_type source d_type dest pktClass} {
	set s_agent [new Agent/$s_type]
	set d_agent [new Agent/$d_type]
	$s_agent set fid_ $pktClass
	$d_agent set fid_ $pktClass
	$self attach-agent $source $s_agent
	$self attach-agent $dest $d_agent
	$self connect $s_agent $d_agent
	
	return $s_agent
}

# Creates a highspeed connection. Similar to create-connection 
# above except the sink agent requires additional work -- Sylvia
Simulator instproc create-highspeed-connection {s_type source d_type dest pktClass} {
        set s_agent [new Agent/$s_type]
        set d_agent [new Agent/$d_type]
        $d_agent resize_buffers
        $s_agent set fid_ $pktClass
        $d_agent set fid_ $pktClass
        $self attach-agent $source $s_agent
        $self attach-agent $dest $d_agent
        $self connect $s_agent $d_agent

        return $s_agent
}

# Creates connection. First creates a source agent of type s_type and binds
# it to source.  Next creates a destination agent of type d_type and binds
# it to dest.  Finally creates bindings for the source and destination agents,
# connects them, and  returns a list of source agent and destination agent.
Simulator instproc create-connection-list {s_type source d_type dest pktClass} {
    set s_agent [new Agent/$s_type]
    set d_agent [new Agent/$d_type]
    $s_agent set fid_ $pktClass
    $d_agent set fid_ $pktClass
    $self attach-agent $source $s_agent
    $self attach-agent $dest $d_agent
    $self connect $s_agent $d_agent

    return [list $s_agent $d_agent]
}   

# Creates connection. First creates a source agent of type s_type and binds
# it to source.  Next creates a destination agent of type d_type and binds
# it to dest.  Finally creates bindings for the source and destination agents,
# connects them, and  returns the source agent. 
# The destination agent is set to listen, for full-tcp.
Simulator instproc create-connection-listen {s_type source d_type dest pktClass} {
    set s_agent [new Agent/$s_type]
    set d_agent [new Agent/$d_type]
    $s_agent set fid_ $pktClass
    $d_agent set fid_ $pktClass
    $self attach-agent $source $s_agent
    $self attach-agent $dest $d_agent
    $self connect $s_agent $d_agent
    $d_agent listen

    return $s_agent 
}   

# This seems to be an obsolete procedure.
Simulator instproc create-tcp-connection {s_type source d_type dest pktClass} {
	set s_agent [new Agent/$s_type]
	set d_agent [new Agent/$d_type]
	$s_agent set fid_ $pktClass
	$d_agent set fid_ $pktClass
	$self attach-agent $source $s_agent
	$self attach-agent $dest $d_agent
	return "$s_agent $d_agent"
}

#
# Other classifier methods overload the instproc-likes to track 
# and return the installed objects.
#
Classifier instproc install {slot val} {
	$self set slots_($slot) $val
	$self cmd install $slot $val
}

Classifier instproc installNext val {
	set slot [$self cmd installNext $val]
	$self set slots_($slot) $val
	set slot
}

Classifier instproc adjacents {} {
	$self array get slots_
}

Classifier instproc in-slot? slot {
	$self instvar slots_
	set ret ""
	if {[array size slots_] < $slot} {
		set ret slots_($slot)
	}
	set ret
}

# For debugging
Classifier instproc dump {} {
	$self instvar slots_ offset_ shift_ mask_
	puts "classifier $self"
	puts "\t$offset_ offset"
	puts "\t$shift_ shift"
	puts "\t$mask_ mask"
	puts "\t[array size slots_] slots"
	foreach i [lsort -integer [array names slots_]] {
		set iv $slots_($i)
		puts "\t\tslot $i: $iv ([$iv info class])"
	}
}

Classifier instproc no-slot slot {
        puts stderr "--- Classfier::no-slot{} default handler (tcl/lib/ns-lib.tcl) ---"
        puts stderr "\t$self: no target for slot $slot"
        puts stderr "\t$self type: [$self info class]"
        puts stderr "content dump:"
        $self dump
        puts stderr "---------- Finished standard no-slot{} default handler ----------"
        # Clear output before we bail out
        [Simulator instance] flush-trace
        exit 1
}

Classifier/Hash instproc dump args {
	eval $self next $args
	$self instvar default_
	puts "\t$default_ default"
}

Classifier/Hash instproc init nbuck {
	# We need to make sure that port shift/mask values are there
	# so we set them after they get their default values
	$self next $nbuck
	$self instvar shift_ mask_
	set shift_ [AddrParams NodeShift 1]
	set mask_ [AddrParams NodeMask 1]
}

Classifier/Port/Reserve instproc init args {
        eval $self next
        $self reserve-port 2
}

Simulator instproc makeflowmon { cltype { clslots 29 } } {
	set flowmon [new QueueMonitor/ED/Flowmon]
	set cl [new Classifier/Hash/$cltype $clslots]
	
	$cl proc unknown-flow { src dst fid }  {
		set fdesc [new QueueMonitor/ED/Flow]
		set dsamp [new Samples]
		$fdesc set-delay-samples $dsamp
		set slot [$self installNext $fdesc] 
		$self set-hash auto $src $dst $fid $slot
	}
	
	$cl proc no-slot slotnum {
		#
		# note: we can wind up here when a packet passes
		# through either an Out or a Drop Snoop Queue for
		# a queue that the flow doesn't belong to anymore.
		# Since there is no longer hash state in the
		# hash classifier, we get a -1 return value for the
		# hash classifier's classify() function, and there
		# is no node at slot_[-1].  What to do about this?
		# Well, we are talking about flows that have already
		# been moved and so should rightly have their stats
		# zero'd anyhow, so for now just ignore this case..
		# puts "classifier $self, no-slot for slotnum $slotnum"
	}
	$flowmon classifier $cl
	return $flowmon
}

# attach a flow monitor to a link
# 3rd argument dictates whether early drop support is to be used

Simulator instproc attach-fmon {lnk fm { edrop 0 } } {
	set isnoop [new SnoopQueue/In]
	set osnoop [new SnoopQueue/Out]
	set dsnoop [new SnoopQueue/Drop]
	$lnk attach-monitors $isnoop $osnoop $dsnoop $fm
	if { $edrop != 0 } {
	    set edsnoop [new SnoopQueue/EDrop]
	    $edsnoop set-monitor $fm
	    [$lnk queue] early-drop-target $edsnoop
	    $edsnoop target [$self set nullAgent_]
	}
	[$lnk queue] drop-target $dsnoop
}

# Added by Yun Wang

Simulator instproc maketbtagger { cltype { clslots 29 } } {

        set tagger [new QueueMonitor/ED/Tagger]
        set cl [new Classifier/Hash/$cltype $clslots]

        $cl proc unknown-flow { src dst fid }  {
                set fdesc [new QueueMonitor/ED/Flow/TB]
                set dsamp [new Samples]
                $fdesc set-delay-samples $dsamp
                set slot [$self installNext $fdesc]
                $self set-hash auto $src $dst $fid $slot
        }

        $cl proc set-rate { src dst fid hashbucket rate depth init} {
                set fdesc [new QueueMonitor/ED/Flow/TB]
                set dsamp [new Samples]
                $fdesc set-delay-samples $dsamp
                $fdesc set target_rate_ $rate
                $fdesc set bucket_depth_ $depth
                # Initialize the bucket as full
                $fdesc set tbucket_ $init  
                set slot [$self installNext $fdesc]
                $self set-hash $hashbucket $src $dst $fid $slot
        }

        $cl proc no-slot slotnum {
                #
                # note: we can wind up here when a packet passes
                # through either an Out or a Drop Snoop Queue for
                # a queue that the flow doesn't belong to anymore.
                # Since there is no longer hash state in the
                # hash classifier, we get a -1 return value for the
                # hash classifier's classify() function, and there
                # is no node at slot_[-1].  What to do about this?
                # Well, we are talking about flows that have already
                # been moved and so should rightly have their stats
                # zero'd anyhow, so for now just ignore this case..
                # puts "classifier $self, no-slot for slotnum $slotnum"
        }
        $tagger classifier $cl
        return $tagger
}

# Added by Yun Wang

Simulator instproc maketswtagger { cltype { clslots 29 } } {

        set tagger [new QueueMonitor/ED/Tagger]
        set cl [new Classifier/Hash/$cltype $clslots]

        $cl proc unknown-flow { src dst fid hashbucket }  {
                set fdesc [new QueueMonitor/ED/Flow/TSW]
                set dsamp [new Samples]
                $fdesc set-delay-samples $dsamp
                set slot [$self installNext $fdesc]
                $self set-hash $hashbucket $src $dst $fid $slot
        }

        $cl proc no-slot slotnum {
                #
                # note: we can wind up here when a packet passes
                # through either an Out or a Drop Snoop Queue for
                # a queue that the flow doesn't belong to anymore.
                # Since there is no longer hash state in the
                # hash classifier, we get a -1 return value for the
                # hash classifier's classify() function, and there
                # is no node at slot_[-1].  What to do about this?
                # Well, we are talking about flows that have already
                # been moved and so should rightly have their stats
                # zero'd anyhow, so for now just ignore this case..
                # puts "classifier $self, no-slot for slotnum $slotnum"
        }
        $tagger classifier $cl
        return $tagger
}

# attach a Tagger to a link
# Added by Yun Wang

Simulator instproc attach-tagger {lnk fm} {
        set isnoop [new SnoopQueue/Tagger]
        $lnk attach-taggers $isnoop $fm
}

# Imported from session.tcl. It is deleted there.
### to insert loss module to regular links in detailed Simulator
Simulator instproc lossmodel {lossobj from to} {
	set link [$self link $from $to]
	$link errormodule $lossobj
}

# This function generates losses that can be visualized by nam.
Simulator instproc link-lossmodel {lossobj from to} {
	set link [$self link $from $to]
	$link insert-linkloss $lossobj
}


#### Polly Huang: Simulator class instproc to support abstract tcp simulations

Simulator instproc rtt { src dst } {
	$self instvar routingTable_ delay_
	set srcid [[$src set node_] id]
	set dstid [[$dst set node_] id]
	set delay 0
	set tmpid $srcid
	while {$tmpid != $dstid} {
		set nextid [$routingTable_ lookup $tmpid $dstid]
		set tmpnode [$self get-node-by-id $tmpid]
		set nextnode [$self get-node-by-id $nextid]
		set tmplink [[$self link $tmpnode $nextnode] link]
		set delay [expr $delay + [expr 2 * [$tmplink set delay_]]]
		set delay [expr $delay + [expr 8320 / [$tmplink set bandwidth_]]]
		set tmpid $nextid
	}
	$src rtt $delay
	return $delay
}

Simulator instproc abstract-tcp {} {
	$self instvar TahoeAckfsm_ RenoAckfsm_ TahoeDelAckfsm_ RenoDelAckfsm_ dropper_ 
	$self set TahoeAckfsm_ [new FSM/TahoeAck]
	$self set RenoAckfsm_ [new FSM/RenoAck]
	$self set TahoeDelAckfsm_ [new FSM/TahoeDelAck]
	$self set RenoDelAckfsm_ [new FSM/RenoDelAck]
	$self set nullAgent_ [new DropTargetAgent]
}

# Chalermek: For Diffusion, Flooding, and Omnicient Multicast 

Simulator instproc create-diffusion-rate-agent {node} {
	global opt
	set diff [new Agent/Diffusion/RateGradient]

	$node set diffagent_ $diff
	$node set ragent_ $diff

	$diff on-node $node

	if [info exist opt(enablePos)] {
		if {$opt(enablePos) == "true"} {
			$diff enable-pos
		} else {
			$diff disable-pos
		}
	} 

	if [info exist opt(enableNeg)] {
		if {$opt(enableNeg) == "true"} {
			$diff enable-neg
		} else {
			$diff disable-neg
		}
	} 
    
	if [info exist opt(suppression)] {
		if {$opt(suppression) == "true"} {
			$diff enable-suppression
		} else {
			$diff disable-suppression
		}
	} 

	if [info exist opt(subTxType)] {
		$diff set-sub-tx-type $opt(subTxType)
	} 

	if [info exist opt(orgTxType)] {
		$diff set-org-tx-type $opt(orgTxType)
	} 

	if [info exist opt(posType)] {
		$diff set-pos-type $opt(posType)
	} 

	if [info exist opt(posNodeType)] {
		$diff set-pos-node-type $opt(posNodeType)
	} 

	if [info exist opt(negWinType)] {
		$diff set-neg-win-type $opt(negWinType)
	} 

	if [info exist opt(negThrType)] {
		$diff set-neg-thr-type $opt(negThrType)
	} 

	if [info exist opt(negMaxType)] {
		$diff set-neg-max-type $opt(negMaxType)
	} 

	$self put-in-list $diff
	$self at 0.0 "$diff start"
	
	return $diff
}

Simulator instproc create-diffusion-probability-agent {node} {
	global opt
	set diff [new Agent/Diffusion/ProbGradient]

	$node set diffagent_ $diff
	$node set ragent_ $diff

	$diff on-node $node

	if [info exist opt(enablePos)] {
		if {$opt(enablePos) == "true"} {
			$diff enable-pos
		} else {
			$diff disable-pos
		}
	} 
	if [info exist opt(enableNeg)] {
		if {$opt(enableNeg) == "true"} {
			$diff enable-neg
		} else {
			$diff disable-neg
		}
	} 
	
	$self put-in-list $diff
	$self at 0.0 "$diff start"

	return $diff
}

# Diffusioncore agent (in diffusion) maps to the wireless routing agent
# in ns
Simulator instproc create-core-diffusion-rtg-agent {node} {
	Node instvar ragent_ dmux_
	set ragent [new Agent/DiffusionRouting]
	$node set ragent_ $ragent
	#$ragent start

	$node create-diffusionApp-agent

	return $ragent
}

Simulator instproc create-flooding-agent {node} {
	set flood [new Agent/Flooding]

	$node set ragent_ $flood

	$flood on-node $node

	$self put-in-list $flood
	$self at 0.0 "$flood start"

	return $flood
}

Simulator instproc create-omnimcast-agent {node} {
	set omni [new Agent/OmniMcast]

	$node set ragent_ $omni

	$omni on-node $node

	$self put-in-list $omni
	$self at 0.0 "$omni start"

	return $omni
}

# XXX These are very simulation-specific methods, why should they belong here?
Simulator instproc put-in-list {agent} {
	$self instvar lagent
	lappend lagent $agent
}

Simulator instproc terminate-all-agents {} {
	$self instvar lagent
	foreach i $lagent {
		$i terminate
	}
}

Simulator instproc prepare-to-stop {} {
	$self instvar lagent
	foreach i $lagent {
		$i stop
	}
}
    
