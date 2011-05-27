# Makefile for the USB/IP driver
#
# $Id$
#
# Copyright (C) 2003-2008 Takahiro Hirofuchi
#
#
# This is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
# USA.
#

 
# Please modify here or set environments.
# KSOURCE should be pointed to the build directory of your kernel.
# 
DEBUG ?= n
KSOURCE ?= /usr/src/linux


%.x:%.c
	gcc -o $@ $<

KBUILD_VERBOSE:=1

HCD_HEADER:=$(KSOURCE)/drivers/usb/core/hcd.h

usbip_common_mod-objs := usbip_common.o usbip_event.o
usbip-objs 	  := stub_dev.o stub_main.o stub_rx.o stub_tx.o
vhci-hcd-objs	  := vhci_sysfs.o vhci_tx.o vhci_rx.o vhci_hcd.o 

obj-m += usbip_common_mod.o
obj-m += usbip.o
obj-m += vhci-hcd.o

EXTRA_CFLAGS += -DHCD_HEADER=\"$(HCD_HEADER)\"

ifeq ($(DEBUG),y)
	EXTRA_CFLAGS += -DCONFIG_USB_DEBUG
endif


default:
	make -C $(KSOURCE) LANG=C KBUILD_VERBOSE=${KBUILD_VERBOSE}  M=`pwd` modules

check:
	#make -C $(KSOURCE) LANG=C KBUILD_VERBOSE=${KBUILD_VERBOSE} C=1 M=`pwd` modules checkstack namespacecheck headers_check
	make -C $(KSOURCE) LANG=C KBUILD_VERBOSE=${KBUILD_VERBOSE} C=1 M=`pwd` modules

.PHONY: cscope
cscope:
	cscope -b -k -R

.PHONY: clean
clean:
	make -C $(KSOURCE) LANG=C KBUILD_VERBOSE=${KBUILD_VERBOSE}  M=`pwd` clean
	rm -f *.x *~
