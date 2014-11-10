#  -*- mode: Makefile; -*-

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


# `all' is the default target
all:

# include $(XEN_ROOT)/Config.mk
include ./Config.mk

CONFIG_$(shell uname -s) := y

XEN_XC             = $(XEN_ROOT)/tools/python/xen/lowlevel/xc
XEN_LIBXC          = $(XEN_ROOT)/tools/libxc
XEN_XENSTORE       = $(XEN_ROOT)/tools/xenstore
XEN_LIBXENSTAT     = $(XEN_ROOT)/tools/xenstat/libxenstat/src

X11_LDPATH = -L/usr/X11R6/$(LIBDIR)

CFLAGS += -D__XEN_TOOLS__

%.opic: %.c
	$(CC) $(CPPFLAGS) -DPIC $(CFLAGS) -fPIC -c -o $@ $<

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.cc
	$(CC) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

OS = $(shell uname -s)

.PHONY: mk-symlinks mk-symlinks-xen mk-symlinks-$(OS)

mk-symlinks-Linux: LINUX_ROOT=$(XEN_ROOT)/linux-2.6-xen-sparse
mk-symlinks-Linux:
	mkdir -p xen/linux
	( cd xen/linux && \
	  ln -sf ../../$(LINUX_ROOT)/include/xen/public/*.h . )
	( cd xen && rm -f sys && ln -sf linux sys )

mk-symlinks-xen:
	mkdir -p xen
	( cd xen && ln -sf ../$(XEN_ROOT)/xen/include/public/*.h . )
	mkdir -p xen/hvm
	( cd xen/hvm && ln -sf ../../$(XEN_ROOT)/xen/include/public/hvm/*.h . )
	mkdir -p xen/io
	( cd xen/io && ln -sf ../../$(XEN_ROOT)/xen/include/public/io/*.h . )

mk-symlinks: mk-symlinks-xen mk-symlinks-$(OS)

