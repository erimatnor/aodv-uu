# Makefile
AODVDIR=$(shell pwd)
NS_DIR=ns-2

SRC =	main.c list.c debug.c timer_queue.c aodv_socket.c aodv_hello.c \
	aodv_neighbor.c aodv_timeout.c routing_table.c seek_list.c \
	aodv_rreq.c aodv_rrep.c aodv_rerr.c nl.c

SRC_NS = 	debug.c list.c timer_queue.c aodv_socket.c aodv_hello.c \
		aodv_neighbor.c aodv_timeout.c routing_table.c seek_list.c \
		aodv_rreq.c aodv_rrep.c aodv_rerr.c

SRC_NS_CPP =	$(NS_DIR)/aodv-uu.cc $(NS_DIR)/packet_queue.cc $(NS_DIR)/packet_input.cc

OBJS =	$(SRC:%.c=%.o)
OBJS_ARM = $(SRC:%.c=%-arm.o)
OBJS_MIPS = $(SRC:%.c=%-mips.o)
OBJS_NS = $(SRC_NS:%.c=%-ns.o)
OBJS_NS_CPP = $(SRC_NS_CPP:%.cc=%-ns.o)

KERNEL=$(shell uname -r)
# Change to compile against different kernel (can be overridden):
KERNEL_DIR=/lib/modules/$(KERNEL)/build
KERNEL_INC=$(KERNEL_DIR)/include

# Compiler and options:
# ##### for RCP use: big-endian
CC=gcc
LD=ld
ARM_CC=arm-linux-gcc
ARM_CCFLAGS=-mbig-endian
ARM_LD=arm-linux-ld
MIPS_CC=mipsel-linux-gcc
MIPS_LD=mipsel-linux-ld
CPP=g++
OPTS=-Wall -O3
CPP_OPTS=-Wall

export CC ARM_CC MIPS_CC

# Comment out to disable debug operation...
DEBUG=-g -DDEBUG
# Add extra functionality. Uncomment or use "make XDEFS=-D<feature>" on 
# the command line.
XDEFS=-DDEBUG
DEFS=-DCONFIG_GATEWAY #-DLLFEEDBACK
CFLAGS=$(OPTS) $(DEBUG) $(DEFS) $(XDEFS)
LD_OPTS=

ifneq (,$(findstring CONFIG_GATEWAY,$(DEFS)))
SRC:=$(SRC) locality.c
endif
ifneq (,$(findstring LLFEEDBACK,$(DEFS)))
SRC:=$(SRC) llf.c
LD_OPTS:=$(LD_OPTS) -liw
endif

# ARM specific configuration goes here:
#=====================================
ARM_INC=

# NS specific configuration goes here:
#=====================================
NS_DEFS= # DON'T CHANGE (overridden by NS Makefile)

# Set extra DEFINES here. Link layer feedback is now a runtime option.
EXTRA_NS_DEFS=-DCONFIG_GATEWAY

ifneq (,$(findstring CONFIG_GATEWAY,$(EXTRA_NS_DEFS)))
SRC_NS:=$(SRC_NS) locality.c
endif

# Note: OPTS is overridden by NS Makefile
NS_CFLAGS=$(OPTS) $(CPP_OPTS) $(DEBUG) $(NS_DEFS) $(EXTRA_NS_DEFS)

NS_INC= # DON'T CHANGE (overridden by NS Makefile)

NS_TARGET=libaodv-uu.a

# Archiver and options
AR=ar
AR_FLAGS=rc

.PHONY: default clean install uninstall depend tags aodvd-arm docs kaodv kaodv-arm kaodv-mips

default: aodvd kaodv

arm: aodvd-arm kaodv-arm

mips: aodvd-mips kaodv-mips

endian.h:
	$(CC) $(CFLAGS) -o endian endian.c
	./endian > endian.h

$(OBJS): %.o: %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJS_ARM): %-arm.o: %.c Makefile
	$(ARM_CC) $(ARM_CCFLAGS) $(CFLAGS) -DARM $(ARM_INC) -c -o $@ $<

$(OBJS_MIPS): %-mips.o: %.c Makefile
	$(MIPS_CC) $(MIPS_CCFLAGS) $(CFLAGS) -DMIPS $(MIPS_INC) -c -o $@ $<

$(OBJS_NS): %-ns.o: %.c Makefile
	$(CPP) $(NS_CFLAGS) $(NS_INC) -c -o $@ $<

$(OBJS_NS_CPP): %-ns.o: %.cc Makefile
	$(CPP) $(NS_CFLAGS) $(NS_INC) -c -o $@ $<

aodvd: $(OBJS) Makefile
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LD_OPTS)

aodvd-arm: $(OBJS_ARM) Makefile
	$(ARM_CC) $(ARM_CCFLAGS) $(CFLAGS) -DARM -o $(@:%-arm=%) $(OBJS_ARM) $(LD_OPTS)

aodvd-mips: $(OBJS_MIPS) Makefile
	$(MIPS_CC) $(MIPS_CCFLAGS) $(CFLAGS) -DMIPS -o $(@:%-mips=%) $(OBJS_MIPS) $(LD_OPTS)

$(NS_TARGET): $(OBJS_NS_CPP) $(OBJS_NS) endian.h 
	$(AR) $(AR_FLAGS) $@ $(OBJS_NS_CPP) $(OBJS_NS) > /dev/null

# Kernel module:
kaodv: 
	$(MAKE) -C $(AODVDIR)/lnx KERNEL_DIR=$(KERNEL_DIR) KCC=$(CC) XDEFS=$(XDEFS)

kaodv-arm: 
	$(MAKE) -C $(AODVDIR)/lnx KERNEL_DIR=$(KERNEL_DIR) KCC=$(ARM_CC) LD=$(ARM_LD) XDEFS=$(XDEFS) kaodv-arm

kaodv-mips: 
	$(MAKE) -C $(AODVDIR)/lnx KERNEL_DIR=$(KERNEL_DIR) KCC=$(MIPS_CC) LD=$(MIPS_LD) XDEFS=$(XDEFS) kaodv-mips

tags: TAGS
TAGS: lnx/TAGS
	etags *.c *.h

lnx/TAGS:
	cd lnx && $(MAKE) TAGS

indent:
	indent -kr -l 80 *.c \
	$(filter-out $(SRC_NS_CPP:%.cc=%.h),$(wildcard *.h))
	$(MAKE) -C lnx indent
depend:
	@echo "Updating Makefile dependencies..."
	@makedepend -Y./ -- $(DEFS) -- $(SRC) &>/dev/null
	@makedepend -a -Y./ -- $(KDEFS) kaodv.c &>/dev/null

install: default
	install -s -m 755 aodvd /usr/sbin/aodvd
	@if [ ! -d /lib/modules/$(KERNEL)/aodv ]; then \
		mkdir /lib/modules/$(KERNEL)/aodv; \
	fi

	@echo "Installing kernel module in /lib/modules/$(KERNEL)/aodv/";
	@if [ -f ./kaodv.ko ]; then \
		install -m 644 kaodv.ko /lib/modules/$(KERNEL)/aodv/kaodv.ko; \
	else \
		install -m 644 kaodv.o /lib/modules/$(KERNEL)/aodv/kaodv.o; \
	fi
	/sbin/depmod -a
uninstall:
	rm -f /usr/sbin/aodvd
	rm -rf /lib/modules/$(KERNEL)/aodv

docs:
	cd docs && $(MAKE) all
clean: 
	rm -f aodvd *~ *.o core *.log $(NS_TARGET) kaodv.ko endian endian.h $(NS_DIR)/*.o $(NS_DIR)/*~
	cd lnx && $(MAKE) clean
#cd docs && $(MAKE) clean

# DO NOT DELETE

main.o: defs.h timer_queue.h list.h debug.h params.h aodv_socket.h
main.o: aodv_rerr.h routing_table.h aodv_timeout.h aodv_hello.h aodv_rrep.h
main.o: nl.h
list.o: list.h
debug.o: aodv_rreq.h defs.h timer_queue.h list.h seek_list.h routing_table.h
debug.o: aodv_rrep.h aodv_rerr.h debug.h params.h
timer_queue.o: timer_queue.h defs.h list.h debug.h
aodv_socket.o: aodv_socket.h defs.h timer_queue.h list.h aodv_rerr.h
aodv_socket.o: routing_table.h params.h aodv_rreq.h seek_list.h aodv_rrep.h
aodv_socket.o: aodv_hello.h aodv_neighbor.h debug.h
aodv_hello.o: aodv_hello.h defs.h timer_queue.h list.h aodv_rrep.h
aodv_hello.o: routing_table.h aodv_timeout.h aodv_rreq.h seek_list.h params.h
aodv_hello.o: aodv_socket.h aodv_rerr.h debug.h
aodv_neighbor.o: aodv_neighbor.h defs.h timer_queue.h list.h routing_table.h
aodv_neighbor.o: aodv_rerr.h aodv_hello.h aodv_rrep.h aodv_socket.h params.h
aodv_neighbor.o: debug.h
aodv_timeout.o: defs.h timer_queue.h list.h aodv_timeout.h aodv_socket.h
aodv_timeout.o: aodv_rerr.h routing_table.h params.h aodv_neighbor.h
aodv_timeout.o: aodv_rreq.h seek_list.h aodv_hello.h aodv_rrep.h debug.h nl.h
routing_table.o: routing_table.h defs.h timer_queue.h list.h aodv_timeout.h
routing_table.o: aodv_rerr.h aodv_hello.h aodv_rrep.h aodv_socket.h params.h
routing_table.o: debug.h seek_list.h nl.h
seek_list.o: seek_list.h defs.h timer_queue.h list.h aodv_timeout.h params.h
seek_list.o: debug.h
aodv_rreq.o: aodv_rreq.h defs.h timer_queue.h list.h seek_list.h
aodv_rreq.o: routing_table.h aodv_rrep.h aodv_timeout.h aodv_socket.h
aodv_rreq.o: aodv_rerr.h params.h debug.h locality.h
aodv_rrep.o: aodv_rrep.h defs.h timer_queue.h list.h routing_table.h
aodv_rrep.o: aodv_neighbor.h aodv_hello.h aodv_timeout.h aodv_socket.h
aodv_rrep.o: aodv_rerr.h params.h debug.h
aodv_rerr.o: aodv_rerr.h defs.h timer_queue.h list.h routing_table.h
aodv_rerr.o: aodv_socket.h params.h aodv_timeout.h debug.h
nl.o: defs.h timer_queue.h list.h lnx/kaodv-netlink.h debug.h aodv_rreq.h
nl.o: seek_list.h routing_table.h aodv_timeout.h aodv_hello.h aodv_rrep.h
nl.o: params.h aodv_socket.h aodv_rerr.h
locality.o: locality.h defs.h timer_queue.h list.h debug.h
