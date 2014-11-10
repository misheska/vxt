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


EXTRA_CFLAGS+=-ggdb
# EXTRA_CFLAGS += -DCONFIG_X86
OS_TARGET = linux
IOV_TARGET = xen
RELEASE = $(shell uname -r)
PROC_TYPE = $(shell uname -p)
AUTHDB_SYMBOLS = Module.symvers


GUESTS_4.7 = 2.6.9-55.ELxenU 2.6.9-78.ELxenU

GUESTS_5.x = 2.6.18-92.el5xen 2.6.18-53.el5xen 



ifeq ($(RELEASE), 2.6.16.60-0.21-xenpae)
LVERSION= -DSLES_SP2 -DNO_LINUX_DEFINED_FOREIGN_MAP
AUTHDB_SYMBOLS = Modules.symvers
endif

ifeq ($(RELEASE), 2.6.16.60-0.21-xen)
LVERSION= -DSLES_SP2 -DNO_LINUX_DEFINED_FOREIGN_MAP -DLINUX_64
AUTHDB_SYMBOLS = Modules.symvers
endif


ifeq ($(RELEASE),2.6.18-53.1.13.el5.xs4.1.1.260.276xen)
LVERSION = -DV2_6_18__53_1_13_el5 -DRW_ON_GNTTAB_END_FOREIGN_ACCESS
endif
ifeq ($(findstring $(RELEASE),$(GUESTS_5.x)), $(RELEASE))
ifeq ($(PROC_TYPE),i686)
LVERSION = -DEXPLICIT_FOREIGN_EVTCHN_BIND -DNO_LINUX_DEFINED_FOREIGN_MAP -DRW_ON_GNTTAB_END_FOREIGN_ACCESS -DNO_ZAP_CALLBACK -DNO_XEN_CLASS
else
LVERSION = -DLINUX_64 -DEXPLICIT_FOREIGN_EVTCHN_BIND -DNO_LINUX_DEFINED_FOREIGN_MAP -DRW_ON_GNTTAB_END_FOREIGN_ACCESS -DNO_ZAP_CALLBACK -DNO_XEN_CLASS
endif
endif
ifeq ($(findstring $(RELEASE),$(GUESTS_4.7)), $(RELEASE))
LVERSION = -DLEGACY_4X_LINUX -DNO_XEN_CLASS -DNO_LINUX_DEFINED_FOREIGN_MAP -DNO_MODERN_PTE_CLEAR -DRW_ON_GNTTAB_END_FOREIGN_ACCESS -DNO_ZAP_CALLBACK -DNO_FOREIGN_MAPPINGS_FIELD -DEXPLICIT_FOREIGN_EVTCHN_BIND
endif

ifeq ($(RELEASE), 2.6.18-128.1.1.el5.xs5.1.0.483.1000xen)
LVERSION += -DATTACH_RECONFIG_SUPPORT
endif

LVERSION += -Dlinux_ver1=1 -Dwin=2

# if KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language
ifneq ($(KERNELRELEASE),)
   obj-m := vxtctrlr.o

   vxtctrlr-objs := ../common/vxt_com.o ../../vxtdev/common/vxt_msg_dev.o ../common/vxtcfg_msgs.o ../common/vxt_signal.o vxt_com_os.o ../common/vxt_trace.o vxt_module.o



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

	echo "Vxtcom controllers are dependent on vxtauth_db symbols file $(AUTHDB_SYMBOLS)";
	if [ -f $(AUTHDB_SYMBOLS) ]; \
	then \
		echo "Re-using existing $(AUTHDB_SYMBOLS)"; \
	else \
		if [ -f ../../vxtdb/linux/$(AUTHDB_SYMBOLS) ]; \
		then \
			echo "Importing vxtauth_db $(AUTHDB_SYMBOLS) symbols"; \
			cp ../../vxtdb/linux/$(AUTHDB_SYMBOLS) .;	\
		else \
			echo "WARNING: There are no vxtauth_db symbols exported"; \
		fi \
	fi
	$(MAKE) -C $(KERNELDIR) EXTRA_CFLAGS+="$(LVERSION) -DVXT_OS_DEP=linux_ver1 -I$(PWD)/../../.. -I$(PWD)/../.." M=$(PWD) modules


clean:
	rm -f *.o
	rm -f vxtctrlr.ko
	rm -f Module.symvers
	rm -f Modules.symvers
	rm -f vxtctrlr.mod.c
	rm -f ../common/vxt_com.o
	rm -f ../common/vxtcfg_msgs.o
	rm -f ../common/vxt_signal.o
	rm -f ../common/vxt_trace.o
	rm -f $(IOV_TARGET)/vxtcom_ctrlr_back.o
	rm -f $(IOV_TARGET)/vxtcom_cntrlr_front.o
	rm -f ../../vxtdev/common/vxt_msg_dev.o
	rm -f ../os
	rm -f ../../../public/kernel/os
	rm -f ../../../public/kernel/bus/hypervisor
endif
