# Copyright (c) 2010, Symantec Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in
# the documentation and/or other materials provided with the
# distribution.
#
# Neither the name of Symantec Corporation nor the names of its
# contributors may be used to endorse or promote products derived
# from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
#

EXTRA_CFLAGS += -ggdb
# EXTRA_CFLAGS += -DCONFIG_X86
OS_TARGET = linux
RELEASE = $(shell uname -r)
VXTCARD_SYMBOLS = Module.symvers

GUESTS_4.7 = 2.6.9-55.ELxenU 2.6.9-78.ELxenU

GUESTS_5.x = 2.6.18-92.el5xen 2.6.18-53.el5xen

ifeq ($(RELEASE), 2.6.16.60-0.21-xenpae)
LVERSION= -DSLES_SP2 
AUTHDB_SYMBOLS = Modules.symvers
endif


ifeq ($(RELEASE), 2.6.16.60-0.21-xen)
LVERSION= -DSLES_SP2 -DNO_LINUX_DEFINED_FOREIGN_MAP -DLINUX_64
VXTCARD_SYMBOLS = Modules.symvers
endif

ifeq ($(findstring $(RELEASE),$(GUESTS_5.x)), $(RELEASE))
LVERSION = -DNO_XEN_CLASS
endif

ifeq ($(findstring $(RELEASE),$(GUESTS_4.7)), $(RELEASE))
LVERSION = -DLEGACY_4X_LINUX -DNO_XEN_CLASS
endif


LVERSION += -Dlinux_ver1=1 -Dwin=2

# if KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language
ifneq ($(KERNELRELEASE),)
   obj-m := vxtauth_db.o

   vxtauth_db-objs := ../common/vxt_db.o vxtdb_mod.o

   vxtctrlr-objs := ../common/vxt_com.o ../../vxtdev/common/vxt_msg_dev.o ../common/vxtcfg_msgs.o ../common/vxt_signal.o $(IOV_TARGET)/vxtcom_cntrlr_front.o vxt_com_os.o ../common/vxt_trace.o



# Otherwise we were called directly from command
# line; invoke the kernel build system
else
   KERNELDIR ?= /lib/modules/$(shell uname -r)/build
   #KERNELDIR ?= /linux/xs3.2/i686-2.6.18-8-vrts/xen
   #KERNELDIR ?= /usr/src/linux/xs3.2/i686-2.6.18-8-vrts/xen
   #KERNELDIR ?=  /usr/src/kernels/2.6.18-8.1.8.el5.xs4.0.1.125.163xen-i686
   #KERNELDIR ?= /usr/src/linux/xs3.2/i686-2.6.18-8-vrts/src
   #KERNELDIR ?= /usr/src/kernels/2.6.18-8.1.6.el5.xs3.2.999.57.75xen-i686
   PWD := $(shell pwd)

default: 
	rm -f ../os
	rm -f ../../../public/kernel/os
	rm -f ../../../public/kernel/bus/hypervisor
	ln -s linux ../os
	ln -s linux ../../../public/kernel/os
	ln -s xen ../../../public/kernel/bus/hypervisor
	echo "^M^MVxtcom controllers are dependent on vxtcard symbols file $(VXTCARD_SYMBOLS)";
	if [ -f $(VXTCARD_SYMBOLS) ]; \
	then \
		echo "Re-using existing $(VXTCARD_SYMBOLS)"; \
	else \
		if [ -f ../../vxtcard/linux/front/$(VXTCARD_SYMBOLS) ]; \
		then \
			echo "Importing vxtcard $(VXTCARD_SYMBOLS) symbols"; \
			cp ../../vxtcard/linux/front/$(VXTCARD_SYMBOLS) ./$(VXTCARD_SYMBOLS);   \
	        else \
			echo "WARNING: There are no vxtcard symbols exported"; \
		fi \
	fi
	$(MAKE) -C $(KERNELDIR) EXTRA_CFLAGS+="$(LVERSION) -Dblinux -DVXT_OS_DEP=linux_ver1 -I$(PWD)/../../.. -I$(PWD)/../.." M=$(PWD) modules
ifeq ($(RELEASE),2.6.9-55.ELxenU)
	$(KERNELDIR)/scripts/mod/modpost -m -o  $(PWD)/$(VXTCARD_SYMBOLS) $(filter-out FORCE,$^) vxtauth_db.o
endif


clean:
	rm -f *.o
	rm -f ../common/vxt_db.o
	rm -f vxtauth_db.mod.c
	rm -f vxtauth_db.mod.o
	rm -f vxtauth_db.o
	rm -f vxtdb_mod.o
	rm -f Module.symvers
	rm -f Modules.symvers
	rm -f .ko
	rm -f vxtauth_db.ko
	rm -f ../os
	rm -f ../../../public/kernel/os
	rm -f ../../../public/kernel/bus/hypervisor

endif
