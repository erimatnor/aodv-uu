# Makefile

SRC =	main.c debug.c timer_queue.c aodv_socket.c aodv_hello.c \
	aodv_neighbor.c aodv_timeout.c routing_table.c seek_list.c \
	k_route.c aodv_rreq.c aodv_rrep.c aodv_rerr.c packet_input.c \
	packet_queue.c libipq.c icmp.c

SRC_NS = 	debug.c timer_queue.c aodv_socket.c aodv_hello.c \
		aodv_neighbor.c aodv_timeout.c routing_table.c seek_list.c \
		aodv_rreq.c aodv_rrep.c aodv_rerr.c packet_input.c \
		packet_queue.c

SRC_NS_CPP =	aodv-uu.cc

OBJS =	$(SRC:%.c=%.o)
OBJS_ARM = $(SRC:%.c=%-arm.o)
OBJS_NS = $(SRC_NS:%.c=%-ns.o)
OBJS_NS_CPP = $(SRC_NS_CPP:%.cc=%-ns.o)

KERNEL=$(shell uname -r)

# Compiler and options:
CC=gcc
ARM_CC=arm-linux-gcc
CPP=g++
OPTS=-Wall -O3
CPP_OPTS=-Wall

# Change to compile against different kernel:
KERNEL_SRC=/usr/src/linux

# Comment out to disable debug operation...
DEBUG=-g -DDEBUG

DEFS=#-DUSE_IW_SPY
CFLAGS=$(OPTS) $(DEBUG) $(DEFS)

ifneq (,$(findstring USE_IW_SPY,$(DEFS)))
SRC:=$(SRC) link_qual.c
endif

# ARM specific configuration goes here:
#=====================================
ARM_INC=

# NS specific configuration goes here:
#=====================================
NS_DEFS= # DON'T CHANGE (overridden by NS Makefile)

# Set extra DEFINES here, for example -DAODVUU_LL_FEEDBACK to enable 
# link layer feedback:
EXTRA_NS_DEFS=-DAODVUU_LL_FEEDBACK

# Note: OPTS is overridden by NS Makefile
NS_CFLAGS=$(OPTS) $(CPP_OPTS) $(DEBUG) $(NS_DEFS) $(EXTRA_NS_DEFS)

NS_INC= # DON'T CHANGE (overridden by NS Makefile)

# Archiver and options
AR=ar
AR_FLAGS=rc

# These are the options for the kernel modules:
#==============================================
KINC=-nostdinc $(shell $(CC) -print-search-dirs | sed -ne 's/install: \(.*\)/-I \1include/gp') -I$(KERNEL_SRC)/include
KDEFS=-D__KERNEL__ -DMODULE
KCFLAGS=-Wall -O2 $(KDEFS) $(KINC)
KCFLAGS_ARM =-Wall -O2 -D__KERNEL__ -DMODULE $(KINC)

.PHONY: default clean install uninstall depend tags aodvd-arm docs

default: aodvd kaodv.o

all: default docs

arm: aodvd-arm kaodv-arm.o

ns: endian.h aodv-uu.o

endian.h:
	$(CC) $(CFLAGS) -o endian endian.c
	./endian > endian.h

$(OBJS): %.o: %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJS_ARM): %-arm.o: %.c Makefile
	$(ARM_CC) $(CFLAGS) -DARM $(ARM_INC) -c -o $@ $<

$(OBJS_NS): %-ns.o: %.c Makefile
	$(CPP) $(NS_CFLAGS) $(NS_INC) -c -o $@ $<

$(OBJS_NS_CPP): %-ns.o: %.cc Makefile
	$(CPP) $(NS_CFLAGS) $(NS_INC) -c -o $@ $<

aodvd: $(OBJS) Makefile
	$(CC) $(CFLAGS) -o $@ $(OBJS)

aodvd-arm: $(OBJS_ARM) Makefile$(name).pdf
	$(ARM_CC) $(CFLAGS) -DARM -o $(@:%-arm=%) $(OBJS_ARM)

aodv-uu.o: $(OBJS_NS_CPP) $(OBJS_NS)
	$(AR) $(AR_FLAGS) libaodv-uu.a $(OBJS_NS_CPP) $(OBJS_NS) > /dev/null

# Kernel module:
kaodv.o: kaodv.c
	$(CC) $(KCFLAGS) -c -o $@ $<

kaodv-arm.o: kaodv.c
	$(ARM_CC) $(KCFLAGS_ARM) -c -o $(@:%-arm.o=%.o) $<
tags:
	etags *.c *.h
indent:
	indent -kr -l 80 *.c \
	$(filter-out $(SRC_NS_CPP:%.cc=%.h),$(wildcard *.h))
depend:
	@echo "Updating Makefile dependencies..."
	@makedepend -Y./ -- $(DEFS) -- $(SRC) &>/dev/null
	@makedepend -a -Y./ -- $(KDEFS) kaodv.c &>/dev/null

install: all
	install -s -m 755 aodvd /usr/sbin/aodvd
	@if [ ! -d /lib/modules/$(KERNEL)/aodv ]; then \
		mkdir /lib/modules/$(KERNEL)/aodv; \
	fi
	install -m 644 kaodv.o /lib/modules/$(KERNEL)/aodv/kaodv.o

	/sbin/depmod -a
uninstall:
	rm -f /usr/sbin/aodvd
	rm -rf /lib/modules/$(KERNEL)/aodv

docs:
	cd docs && $(MAKE) all
clean: 
	rm -f aodvd *~ *.o core *.log libaodv-uu.a endian endian.h
	cd docs && $(MAKE) clean

# DO NOT DELETE

main.o: defs.h timer_queue.h debug.h params.h aodv_socket.h aodv_rerr.h
main.o: routing_table.h aodv_timeout.h k_route.h aodv_hello.h aodv_rrep.h
main.o: packet_input.h packet_queue.h link_qual.h
debug.o: aodv_rreq.h defs.h timer_queue.h seek_list.h routing_table.h
debug.o: aodv_rrep.h aodv_rerr.h debug.h params.h
timer_queue.o: timer_queue.h defs.h debug.h
aodv_socket.o: aodv_socket.h defs.h timer_queue.h aodv_rerr.h routing_table.h
aodv_socket.o: aodv_rreq.h seek_list.h aodv_rrep.h params.h aodv_hello.h
aodv_socket.o: aodv_neighbor.h debug.h link_qual.h
aodv_hello.o: aodv_hello.h defs.h timer_queue.h aodv_rrep.h routing_table.h
aodv_hello.o: aodv_timeout.h aodv_rreq.h seek_list.h params.h aodv_socket.h
aodv_hello.o: aodv_rerr.h debug.h
aodv_neighbor.o: aodv_neighbor.h defs.h timer_queue.h aodv_hello.h
aodv_neighbor.o: aodv_rrep.h routing_table.h aodv_timeout.h aodv_rreq.h
aodv_neighbor.o: seek_list.h params.h aodv_socket.h aodv_rerr.h debug.h
aodv_timeout.o: defs.h timer_queue.h aodv_timeout.h aodv_socket.h aodv_rerr.h
aodv_timeout.o: routing_table.h aodv_rreq.h seek_list.h aodv_hello.h
aodv_timeout.o: aodv_rrep.h debug.h params.h packet_queue.h k_route.h icmp.h
routing_table.o: routing_table.h defs.h timer_queue.h aodv_timeout.h
routing_table.o: packet_queue.h aodv_rerr.h aodv_hello.h aodv_rrep.h
routing_table.o: aodv_socket.h k_route.h debug.h params.h seek_list.h
seek_list.o: seek_list.h defs.h timer_queue.h aodv_timeout.h params.h debug.h
k_route.o: defs.h timer_queue.h debug.h k_route.h
aodv_rreq.o: aodv_rreq.h defs.h timer_queue.h seek_list.h routing_table.h
aodv_rreq.o: aodv_rrep.h aodv_timeout.h k_route.h aodv_socket.h aodv_rerr.h
aodv_rreq.o: params.h debug.h
aodv_rrep.o: aodv_rrep.h defs.h timer_queue.h routing_table.h aodv_timeout.h
aodv_rrep.o: aodv_socket.h aodv_rerr.h debug.h params.h
aodv_rerr.o: aodv_rerr.h defs.h timer_queue.h routing_table.h aodv_socket.h
aodv_rerr.o: aodv_timeout.h debug.h params.h
packet_input.o: defs.h timer_queue.h debug.h routing_table.h aodv_rreq.h
packet_input.o: seek_list.h aodv_rerr.h libipq.h params.h aodv_timeout.h
packet_input.o: aodv_socket.h packet_queue.h packet_input.h
packet_queue.o: packet_queue.h defs.h timer_queue.h debug.h routing_table.h
packet_queue.o: libipq.h params.h
libipq.o: libipq.h
icmp.o: defs.h timer_queue.h debug.h
link_qual.o: defs.h timer_queue.h debug.h
