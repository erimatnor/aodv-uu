ifneq (,$(findstring 2.6,$(KERNELRELEASE)))
EXTRA_CFLAGS += -D KERNEL26 $(XDEFS)
obj-m += kaodv.o
kaodv-objs := kaodv-mod.o kaodv-debug.o kaodv-netlink.o kaodv-queue.o kaodv-ipenc.o kaodv-expl.o 
else

KOBJS := kaodv-mod.o kaodv-debug.o kaodv-netlink.o kaodv-queue.o kaodv-ipenc.o kaodv-expl.o
KOBJS_ARM := $(KOBJS:%.o=%-arm.o)
KOBJS_MIPS := $(KOBJS:%.o=%-mips.o)

KERNEL=$(shell uname -r)
KERNEL_DIR=/lib/modules/$(KERNEL)/build
KERNEL_INC=$(KERNEL_DIR)/include
THIS_DIR=$(shell pwd)

# You might want to use gcc32 for the kernel module on Fedora core 1
KCC=gcc
LD=ld
XDEFS=

# Some shell scripting to find out Linux kernel version
VERSION=$(shell if [ ! -d $(KERNEL_DIR) ]; then echo "No linux source found!!! Check your setup..."; exit; fi; grep ^VERSION $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
PATCHLEVEL=$(shell grep ^PATCHLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
SUBLEVEL=$(shell grep ^SUBLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)

# These are the options for the kernel module with kernel 2.4.x:
#==============================================
KINC=-nostdinc -DMODVERSIONS -include $(KERNEL_INC)/linux/modversions.h $(shell $(CC) -print-search-dirs | sed -ne 's/install: \(.*\)/-I \1include/gp') -I$(KERNEL_INC)
KDEFS=-D__KERNEL__ -DMODULE
KCFLAGS=-Wall -Wno-strict-aliasing -O2 $(KDEFS) $(KINC) $(XDEFS)
KCFLAGS_ARM=-Wall -O2 -D__KERNEL__ -DMODULE -nostdinc $(shell $(ARM_CC) -print-search-dirs | sed -ne 's/install: \(.*\)/-I \1include/gp') -I$(KERNEL_INC)
KCFLAGS_MIPS=-Wall -mips2 -O2 -fno-pic -mno-abicalls -mlong-calls -G0 -msoft-float -D__KERNEL__ -DMODULE -nostdinc $(shell $(MIPS_CC) -print-search-dirs | sed -ne 's/install: \(.*\)/-I \1include/gp') -I$(KERNEL_INC) $(XDEFS)

.PHONY: clean clean-2.4 clean-2.6 indent default

# Check for kernel version
ifeq ($(PATCHLEVEL),6)
default: kaodv.ko
clean: clean-2.6
else 
# Assume kernel 2.4
default: kaodv.o
clean: clean-2.4
endif

#default: kaodv.ko
$(KOBJS): %.o: %.c Makefile
	$(KCC) $(KCFLAGS) -c -o $@ $<

$(KOBJS_ARM): %-arm.o: %.c Makefile
	$(KCC) $(KCFLAGS_ARM) -D ARM -c -o $@ $<

$(KOBJS_MIPS): %-mips.o: %.c Makefile
	$(KCC) $(KCFLAGS_MIPS) -D MIPS -c -o $@ $<

kaodv.o: $(KOBJS) Makefile
	$(LD) -r $(KOBJS) -o $@
	cp $@ ../

kaodv-arm.o: $(KOBJS_ARM) Makefile
	$(LD) -r $(KOBJS_ARM) -o $@
	cp $@ ../kaodv.o

kaodv-mips.o: $(KOBJS_MIPS) Makefile
	$(LD) -r $(KOBJS_MIPS) -o $@
	cp $@ ../kaodv.o

kaodv.ko: $(KOBJS:%.o=%.c) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(THIS_DIR) modules
	cp $@ ../

kaodv-arm.ko: $(KOBJS_ARM:%.o=%.c) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(THIS_DIR) modules
	cp $@ ../kaodv.ko

kaodv-mips.ko: $(KOBJS_MIPS:%.o=%.c) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(THIS_DIR) modules
	cp $@ ../kaodv.ko


clean-2.6:
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(THIS_DIR) clean
	rm -rf *.o *~

clean-2.4:
	rm -rf *.o *~

TAGS:
	etags *.c *.h
indent:
	indent -kr -i8 -ts8 -sob -l80 -ss -ncs *.c *.h 
endif

