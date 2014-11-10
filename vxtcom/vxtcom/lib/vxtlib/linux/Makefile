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


LIBRARIES = 

ifeq ($(XEN_ROOT), )
XEN_ROOT = /usr/src/kernels/2.6.18-8.1.6.el5.xs3.2.999.57.75xen-i686
endif

include ./Rules.mk
RELEASE = $(shell uname -r)
GUESTS_5.x = 2.6.18-92.el5xen 2.6.18-53.el5xen 2.6.16.60-0.21-xen

ifeq ($(findstring $(RELEASE),$(GUESTS_5.x)), $(RELEASE))
LVERSION = -DBASIC_ASSERT -fPIC
endif

ifneq (2.6.16.60-0.21-xen, $(RELEASE))
LIBRARIES += -lz
endif

ifeq ($(RPM_BUILD),yes)
LVERSION += $(CFLAGS)
endif

EXTRA_CFLAGS = $(LVERSION) -I./ -I../../../public -I$(XEN_ROOT)/include -D__x86_64__

LIB = libvxt_socket.a
LIB += libvxt_socket.so 

TARGET = libvxt_socket.a libvxt_socket.so

.PHONY: all
all: $(TARGET)


#libvxt_socket.so: vxt_queue.o vxt_msg.o vxt_socket_dev.o
#	$(HOSTCC) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -Wl,-soname -Wl,libvxt_socket.so -shared -o $@ $^ -lz 
#

libvxt_socket.so: ../common/vxt_queue.o ../common/vxt_msg.o ../common/vxt_socket_dev.o
	$(HOSTCC) $(EXTRA_CFLAGS) $(LDFLAGS) -Wl,-soname -Wl,libvxt_socket.so -shared -o $@ $^  $(LIBRARIES)
#

.PHONY: clean
clean:
	rm -rf $(TARGET) ../common/*.o *~ core

.PHONY: install
install:


%.o: %.c
	$(HOSTCC) $(EXTRA_CFLAGS) $(HOSTCFLAGS) -I$(XEN_ROOT)/xen/include -c -o $@ $<





# libvxt_socket

# 	$(AR) rc $@ $^

libvxt_socket.a: libvxt_socket.so
	$(AR) rc $@ $^






