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
OS_TARGET = linux
IOV_TARGET = xen
RELEASE = $(shell uname -r)
PROC_TYPE = $(shell uname -p)


GUESTS_4.7 = 2.6.9-55.ELxenU 2.6.9-78.ELxenU

GUESTS_5.x = 2.6.18-92.el5xen 2.6.18-53.el5xen 


ifeq ($(RELEASE), 2.6.32)
EXTRA_CFLAGS+=-DKVM_MODULE_PRESENT
endif




ifeq ($(RELEASE), 2.6.16.60-0.21-xenpae)
LVERSION= -DSLES_SP2 -DNO_LINUX_DEFINED_FOREIGN_MAP
endif

ifeq ($(RELEASE), 2.6.16.60-0.21-xen)
LVERSION= -DSLES_SP2 -DNO_LINUX_DEFINED_FOREIGN_MAP -DLINUX_64
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

ifeq ($(RELEASE), 2.6.18-128.1.6.el5.xs5.5.0.496.1012xen)
LVERSION += -DATTACH_RECONFIG_SUPPORT
endif

LVERSION += -Dlinux_ver1=1 -Dwin=2


# Export the option variables for the follow-on Make actions

export IOV_TARGET
export EXTRA_CFLAGS
export OS_TARGET

default: 
	rm -f ../os
	rm -f ../../../public/kernel/os
	rm -f ../../../public/kernel/bus/hypervisor
	ln -s linux ../os
	ln -s linux ../../../public/kernel/os
	ln -s xen ../../../public/kernel/bus/hypervisor

	cd front; make EXTRA_CFLAGS+="$(LVERSION) -DVXT_OS_DEP=linux_ver1"
	cd back; make EXTRA_CFLAGS+="$(LVERSION) -DVXT_OS_DEP=linux_ver1"

clean:
	rm -f *.o
	rm -f $(IOV_TARGET)/vxtcom_ctrlr_back.o
	rm -f $(IOV_TARGET)/vxtcom_cntrlr_front.o
	rm -f ../os
	rm -f ../../../public/kernel/os
	rm -f ../../../public/kernel/bus/hypervisor
	cd front; make clean
	cd back; make clean
	cd guest; make clean
	cd host; make clean

make_kvm: 
	rm -f ../os
	rm -f ../../../public/kernel/os
	rm -f ../../../public/kernel/bus/hypervisor
	ln -s linux ../os
	ln -s linux ../../../public/kernel/os
	ln -s kvm ../../../public/kernel/bus/hypervisor

	cd guest; make IOV_TARGET=kvm EXTRA_CFLAGS+="$(LVERSION) -DVXT_OS_DEP=linux_ver1"
	cd host; make IOV_TARGET=kvm EXTRA_CFLAGS+="$(LVERSION) -DVXT_OS_DEP=linux_ver1"
