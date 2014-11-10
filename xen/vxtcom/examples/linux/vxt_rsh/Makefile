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

CFLAGS    ?= -O2 -fomit-frame-pointer
CFLAGS    += -DNDEBUG
RELEASE = $(shell uname -r)
CURSES_LIB = curses

OS_VERSIONS = -Dlinux_ver1=1 -Dwin=2 -DVXT_OS_DEP=linux_ver1


CFLAGS += $(shell if ( test -f /usr/include/xs.h ); then echo "-DXSTOOLS -lxenstore"; fi)

GUESTS = 2.6.18-53.el5xen 2.6.9-55.ELxenU 2.6.9-78.ELxenU 2.6.18-92.el5xen 2.6.16.60-0.21-xenpae

ifeq ($(findstring $(RELEASE),$(GUESTS)), $(RELEASE))
	CFLAGS += -lpthread
endif

ifeq ($(RELEASE), 2.6.9-55.ELxenU)
	CFLAGS += -DINCLUDE_PTHREAD
endif

ifeq ($(RELEASE), 2.6.16.60-0.21-xenpae)
	CURSES_LIB = ncurses
endif

ifeq ($(RELEASE), 2.6.16.60-0.21-xen)
	CURSES_LIB = ncurses
endif

CFLAGS += $(OS_VERSIONS)


HOSTCC       = gcc

PWD =  $(shell pwd)


all: vxt_rsh_server vxt_rsh 

vxt_rsh: vrsh_guest.c
	rm -f vxtcom
	ln -s ../../../public vxtcom
	$(HOSTCC) -o vxt_rsh -I$(PWD) -I$(PWD)/../../.. $(CFLAGS) -lvxt_socket -l$(CURSES_LIB) vrsh_guest.c

vxt_rsh_server: vrsh_server.c
	rm -f vxtcom
	ln -s ../../../public vxtcom
	$(HOSTCC) -o vxt_rsh_server -I$(PWD) -I$(PWD)/../../.. $(CFLAGS) -lvxt_socket vrsh_server.c -lutil


clean:
	rm -f vxt_rsh
	rm -f vxt_rsh_server
	rm -f vxtcom

