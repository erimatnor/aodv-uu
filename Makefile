# Makefile

SRC =	main.c debug.c timer_queue.c aodv_socket.c aodv_hello.c \
	aodv_timeout.c routing_table.c seek_list.c k_route.c aodv_rreq.c \
	aodv_rrep.c aodv_rerr.c packet_input.c libipq.c icmp.c link_qual.c

OBJS =	$(SRC:%.c=%.o)
OBJS_ARM = $(SRC:%.c=%-arm.o)

KERNEL=$(shell uname -r)

# Compiler and options:
CC=gcc
ARM_CC=arm-linux-gcc

OPTS=-Wall -O3
# Uncomment to enable debug operation...
DEBUG=-g -DDEBUG
DEFS=-DUSE_IW_SPY
CFLAGS=$(OPTS) $(DEBUG) $(DEFS)

ARM_INC=
# These are the options for the kernel modules:
KINC=-I/usr/src/linux/include
KDEFS=-D__KERNEL__ -DMODULE
KCFLAGS=-Wall -O2 $(KDEFS) $(KINC)
KCFLAGS_ARM =-Wall -O2 -D__KERNEL__ -DMODULE $(KINC)

.PHONY: default clean install uninstall depend tags aodvd-arm

all: aodvd kaodv.o

arm: aodvd-arm kaodv-arm.o

$(OBJS): %.o: %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJS_ARM): %-arm.o: %.c Makefile
	$(ARM_CC) $(CFLAGS) -DARM $(ARM_INC) -c -o $@ $<

aodvd: $(OBJS) Makefile
	$(CC) $(CFLAGS) -o $@ $(OBJS)

aodvd-arm: $(OBJS_ARM) Makefile
	$(ARM_CC) $(CFLAGS) -DARM -o $(@:%-arm=%) $(OBJS_ARM)

# Kernel module:
kaodv.o: kaodv.c
	$(CC) $(KCFLAGS) -c -o $@ $<

kaodv-arm.o: kaodv.c
	$(ARM_CC) $(KCFLAGS_ARM) -c -o $(@:%-arm.o=%.o) $<
tags:
	etags *.c *.h
indent:
	indent -kr *.c *.h
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

clean: 
	rm -f aodvd *~ *.o core *.log
# DO NOT DELETE

main.o: defs.h timer_queue.h debug.h params.h aodv_socket.h aodv_timeout.h
main.o: k_route.h routing_table.h aodv_hello.h aodv_rrep.h packet_input.h
main.o: link_qual.h
debug.o: aodv_rreq.h defs.h timer_queue.h seek_list.h aodv_rrep.h aodv_rerr.h
debug.o: routing_table.h debug.h params.h
timer_queue.o: timer_queue.h defs.h debug.h
aodv_socket.o: aodv_socket.h defs.h timer_queue.h aodv_rreq.h seek_list.h
aodv_socket.o: aodv_rerr.h routing_table.h aodv_rrep.h aodv_hello.h debug.h
aodv_socket.o: link_qual.h
aodv_hello.o: aodv_hello.h defs.h timer_queue.h aodv_rrep.h routing_table.h
aodv_hello.o: aodv_timeout.h aodv_rreq.h seek_list.h packet_input.h params.h
aodv_hello.o: aodv_socket.h debug.h
aodv_timeout.o: defs.h timer_queue.h aodv_timeout.h aodv_socket.h aodv_rreq.h
aodv_timeout.o: seek_list.h aodv_rerr.h routing_table.h debug.h params.h
aodv_timeout.o: packet_input.h k_route.h icmp.h
routing_table.o: routing_table.h defs.h timer_queue.h aodv_timeout.h
routing_table.o: aodv_rerr.h aodv_socket.h k_route.h debug.h params.h
seek_list.o: seek_list.h defs.h timer_queue.h aodv_timeout.h params.h debug.h
k_route.o: defs.h timer_queue.h debug.h k_route.h
aodv_rreq.o: aodv_rreq.h defs.h timer_queue.h seek_list.h aodv_rrep.h
aodv_rreq.o: routing_table.h aodv_timeout.h k_route.h aodv_socket.h params.h
aodv_rreq.o: debug.h
aodv_rrep.o: aodv_rrep.h defs.h timer_queue.h aodv_hello.h routing_table.h
aodv_rrep.o: packet_input.h aodv_timeout.h aodv_socket.h seek_list.h debug.h
aodv_rrep.o: params.h
aodv_rerr.o: aodv_rerr.h defs.h timer_queue.h routing_table.h aodv_socket.h
aodv_rerr.o: debug.h
packet_input.o: defs.h timer_queue.h debug.h routing_table.h aodv_rreq.h
packet_input.o: seek_list.h aodv_rerr.h libipq.h params.h aodv_timeout.h
packet_input.o: aodv_hello.h aodv_rrep.h aodv_socket.h
libipq.o: libipq.h
icmp.o: defs.h timer_queue.h debug.h
link_qual.o: defs.h timer_queue.h debug.h
