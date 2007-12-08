#
# Copyright (c) 1996-1997 Regents of the University of California.
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

#
# OTcl methods for the Agent base class
#

#
# The following overload was added to inform users of the backward
# compatibility issues resulted from having a 32-bit addressing space.
# 
Agent instproc set args {
	if { [lindex $args 0] == "dst_" } {
		puts "Warning dst_ is no longer being supported in NS. $args"
		puts "Use dst_addr_ and dst_port_ instead"
		$self instvar dst_addr_ dst_port_
		set addr [lindex $args 1]
		set baseAddr [Simulator set McastBaseAddr_]
		if { $addr >= $baseAddr } {
			$self set dst_addr_ $addr
			$self set dst_port_ 0
		} else {
			$self set dst_addr_ [expr ($addr >> 8) ]
			$self set dst_port_ [expr ($addr % 256) ]
			exit
		}
		return
	}
	eval $self next $args
}

# Debo

Agent instproc init {} {
	#$self instvar nodeid_
	#$self set nodeid_ -1
}

Agent instproc nodeid {} { 
        [$self set node_] id
}

Agent instproc port {} {
	$self instvar agent_port_
	return $agent_port_
}

#
# Lower 8 bits of dst_ are portID_.  this proc supports setting the interval
# for delayed acks
#       
Agent instproc dst-port {} {
	$self instvar dst_port_
	return [expr $dst_port_]
}

#
# Add source of type s_type to agent and return the source
# Source objects are obsolete; use attach-app instead
#
Agent instproc attach-source {s_type} {
	set source [new Source/$s_type]
	$source attach $self
	$self set type_ $s_type
	return $source
}

# 
# Add application of type s_type to agent and return the app
# Note that s_type must be defined as a packet type in packet.h
# 
Agent instproc attach-app {s_type} {
	set app_ [new Application/$s_type]
	$app_ attach-agent $self
	$self set type_ $s_type
	return $app_
}

#
# Attach tbf to an agent
#
Agent instproc attach-tbf { tbf } {
	$tbf target [$self target]
	$self target $tbf

}

#
# OTcl support for classes derived from Agent
#
Class Agent/Null -superclass Agent

Agent/Null instproc init args {
    eval $self next $args
}

Agent/LossMonitor instproc log-loss {} {
}

#Signalling agent attaches tbf differently as none of its signalling mesages
#go via the tbf
Agent/CBR/UDP/SA instproc attach-tbf { tbf } {
	$tbf target [$self target]
	$self target $tbf
	$self ctrl-target [$tbf target]
}

#
# A lot of agents want to store the maxttl locally.  However,
# setting a class variable based on the Agent::ttl_ variable
# does not help if the user redefines Agent::ttl_.  Therefore,
# Agents interested in the maxttl_ should call this function
# with the name of their class variable, and it is set to the
# maximum of the current/previous value.
#
# The function itself returns the value of ttl_ set.
#
# I use this function from agent constructors to set appropriate vars:
# for instance to set Agent/rtProto/DV::INFINITY, or
# Agent/SRM/SSM::ttlGroupScope_
# 
Agent proc set-maxttl {objectOrClass var} {
	if { [catch "$objectOrClass set $var" value] ||	\
	     ($value < [Agent set ttl_]) } {
		$objectOrClass set $var [Agent set ttl_]
	}
	$objectOrClass set $var
}



Agent/TCP instproc init {} {
    eval $self next
    set ns [Simulator instance]
    $ns create-eventtrace Event $self
}

#Agent instproc init args {
#        $self next $args
#}       

#Agent/rtProto instproc init args {
#        puts "DOWN HERE 2"
#        $self next $args
#}       
#Agent/rtProto/TORA -superclass Agent
Agent/TORA instproc init args {

         $self next $args
}       

Agent/TORA set sport_	0
Agent/TORA set dport_	0

Agent/AODV instproc init args {

         $self next $args
}

Agent/AODV set sport_   0
Agent/AODV set dport_   0

# AODV-UU routing agent
Agent/AODVUU instproc init args {
    $self next $args
}

Agent/AODVUU set sport_   0
Agent/AODVUU set dport_   0
