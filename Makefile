# Makefile


SRC =	main.c list.c debug.c timer_queue.c aodv_socket.c aodv_hello.c \
	aodv_neighbor.c aodv_timeout.c routing_table.c seek_list.c \
	k_route.c aodv_rreq.c aodv_rrep.c aodv_rerr.c packet_input.c \
	packet_queue.c libipq.c icmp.c

SRC_NS = 	debug.c list.c timer_queue.c aodv_socket.c aodv_hello.c \
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

# Set extra DEFINES here. Link layer feedback is now a runtime option.
EXTRA_NS_DEFS=

# Note: OPTS is overridden by NS Makefile
NS_CFLAGS=$(OPTS) $(CPP_OPTS) $(DEBUG) $(NS_DEFS) $(EXTRA_NS_DEFS)

NS_INC= # DON'T CHANGE (overridden by NS Makefile)

# Archiver and options
AR=ar
AR_FLAGS=rc

# These are the options for the kernel modules:
#==============================================
KINC=-nostdinc $(shell $(CC) -print-search-dirs | sed -ne 's/install: \(.*\)/-I \1include/gp') -I/lib/modules/$(KERNEL)/build/include
KDEFS=-D__KERNEL__ -DMODULE
KCFLAGS=-Wall -O2 $(KDEFS) $(KINC)
KCFLAGS_ARM =-Wall -O2 -D__KERNEL__ -DMODULE $(KINC)

.PHONY: default clean install uninstall depend tags aodvd-arm docs

default: aodvd kaodv.o

all: default

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

aodvd-arm: $(OBJS_ARM) Makefile
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
#cd docs && $(MAKE) clean

# DO NOT DELETE

main.o: defs.h timer_queue.h list.h debug.h params.h aodv_socket.h
main.o: aodv_rerr.h ./endian.h routing_table.h aodv_timeout.h k_route.h
main.o: aodv_hello.h aodv_rrep.h packet_input.h packet_queue.h
list.o: list.h
debug.o: aodv_rreq.h ./endian.h defs.h timer_queue.h list.h seek_list.h
debug.o: routing_table.h aodv_rrep.h aodv_rerr.h debug.h params.h
timer_queue.o: timer_queue.h defs.h list.h debug.h
aodv_socket.o: aodv_socket.h defs.h timer_queue.h list.h aodv_rerr.h
aodv_socket.o: ./endian.h routing_table.h params.h aodv_rreq.h seek_list.h
aodv_socket.o: aodv_rrep.h aodv_hello.h aodv_neighbor.h debug.h
aodv_hello.o: aodv_hello.h defs.h timer_queue.h list.h aodv_rrep.h ./endian.h
aodv_hello.o: routing_table.h aodv_timeout.h aodv_rreq.h seek_list.h params.h
aodv_hello.o: aodv_socket.h aodv_rerr.h debug.h
aodv_neighbor.o: aodv_neighbor.h defs.h timer_queue.h list.h routing_table.h
aodv_neighbor.o: aodv_rerr.h ./endian.h aodv_hello.h aodv_rrep.h
aodv_neighbor.o: aodv_socket.h params.h debug.h
aodv_timeout.o: defs.h timer_queue.h list.h aodv_timeout.h aodv_socket.h
aodv_timeout.o: aodv_rerr.h ./endian.h routing_table.h params.h
aodv_timeout.o: aodv_neighbor.h aodv_rreq.h seek_list.h aodv_hello.h
aodv_timeout.o: aodv_rrep.h debug.h packet_queue.h k_route.h icmp.h
routing_table.o: routing_table.h defs.h timer_queue.h list.h aodv_timeout.h
routing_table.o: packet_queue.h aodv_rerr.h ./endian.h aodv_hello.h
routing_table.o: aodv_rrep.h aodv_socket.h params.h k_route.h debug.h
routing_table.o: seek_list.h
seek_list.o: seek_list.h defs.h timer_queue.h list.h aodv_timeout.h params.h
seek_list.o: debug.h
k_route.o: defs.h timer_queue.h list.h debug.h k_route.h
aodv_rreq.o: aodv_rreq.h ./endian.h defs.h timer_queue.h list.h seek_list.h
aodv_rreq.o: routing_table.h aodv_rrep.h aodv_timeout.h k_route.h
aodv_rreq.o: aodv_socket.h aodv_rerr.h params.h debug.h
aodv_rrep.o: aodv_rrep.h ./endian.h defs.h timer_queue.h list.h
aodv_rrep.o: routing_table.h aodv_neighbor.h aodv_hello.h aodv_timeout.h
aodv_rrep.o: aodv_socket.h aodv_rerr.h params.h debug.h
aodv_rerr.o: aodv_rerr.h ./endian.h defs.h timer_queue.h list.h
aodv_rerr.o: routing_table.h aodv_socket.h params.h aodv_timeout.h debug.h
packet_input.o: defs.h timer_queue.h list.h debug.h routing_table.h
packet_input.o: aodv_hello.h aodv_rrep.h ./endian.h aodv_rreq.h seek_list.h
packet_input.o: aodv_rerr.h libipq.h params.h aodv_timeout.h aodv_socket.h
packet_input.o: packet_queue.h packet_input.h
packet_queue.o: packet_queue.h defs.h timer_queue.h list.h debug.h
packet_queue.o: routing_table.h libipq.h params.h aodv_timeout.h
libipq.o: libipq.h
icmp.o: defs.h timer_queue.h list.h debug.h
