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

# if KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language
ifneq ($(KERNELRELEASE),)
   obj-m := vxtcard_host.o

   vxtcard_host-objs := ../$(IOV_TARGET)/kvm_vxt_host.o ../$(IOV_TARGET)/vxt_card.o ../$(IOV_TARGET)/vxt_auth.o



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

	$(MAKE) -C $(KERNELDIR) EXTRA_CFLAGS+="-I$(PWD)/../../../.. -I$(PWD)/../../.." M=$(PWD) modules


clean:
	rm -f *.o
	rm -f vxtcard_host.ko
	rm -f vxtcard_host.mod.o
	rm -f vxtcard_host.o
	rm -f vxtcard_host.mod.c
	rm -f .vxtcard_host.mod.o.cmd
	rm -f .vxtcard_host.ko.cmd
	rm -f .vxtcard_host.o.cmd
	rm -rf .tmp_versions
	rm -f Module.markers
	rm -f modules.order
	rm -f Module.symvers
	rm -f Modules.symvers
	rm -f .vxtcard_host.ko.cmd
	rm -f .vxtcard_host.mod.o.cmd
	rm -f vxtcard_host.ko
	rm -f vxtcard_host.mod.c
	rm -f .vxtcard_host.o.cmd

endif
