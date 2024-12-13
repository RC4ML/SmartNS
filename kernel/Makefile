# SPDX-License-Identifier: GPL-2.0
ifneq  ($(KERNELRELEASE),)

obj-m := smartns.o
smartns-y := \
	smartns_kernel.o \
	smartns_rdma.o \
	smartns_tcp.o

else
KDIR := /lib/modules/$(shell uname -r)/build

PWD:=$(shell pwd)

## add extra symbols for nvidia driver
# EXTRA_SYMBOLS := ${PWD}/extra_symvers.updated

# EXTRA_CFLAGS := -DKBUILD_EXTRA_SYMBOLS=$(EXTRA_SYMBOLS)


all:
	make -C $(KDIR)  M=$(PWD) modules
clean:
	rm -f *.ko *.o *.symvers *.cmd .*.cmd *.cmd.o *.mod* modules.order
endif