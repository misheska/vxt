# $Id: libmxt.mk,v 1.22 2009/07/15 19:23:51 surkude Exp $
#ident "$Source: /project/vxtcom/lib/vxtmsg_mux/linux/libmxt.mk,v $"

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


MAKEFILE = libmxt.mk
STATIC = -Bdynamic
CC=gcc
COMMON = ../common

OS_VERSIONS = -Dlinux_ver1=1 -Dwin=2 -DVXT_OS_DEP=linux_ver1


#
# This defines all objects which are resident in the common dir.
#

OBJS = \
	mxt.o		\
	mxt_common.o	\
	mxt_bufops.o	\
	mxt_vxt.o	\
	mxt_auth.o	\
	mxt_platform.o

SRCS = \
	$(COMMON)/mxt.c		\
	$(COMMON)/mxt_common.c	\
	$(COMMON)/mxt_bufops.c	\
	$(COMMON)/mxt_vxt.c	\
	mxt_auth.c		\
	mxt_platform.c

HDRS = \
	mxt.h \
	mxt_platform.h \
	mxt_lib.h

STANDALONE_OBJS = \
	clnt_test_file.o \
	serv_test_file.o \
	mxt_trace.o

STANDALONE = \
	clnt_test_file \
	serv_test_file \
	mxt_trace

PIC=
LIBRARY = libmxt.so
LOCALDEF = 
PWD = $(shell pwd)
INCLUDES = -I. -I$(PWD) -I$(PWD)/../linux/ -I$(PWD)/../common/ \
		-I$(PWD)/../../../ $(OS_VERSIONS)
LIBS= -lnsl -lpthread -lvxt_socket
#LIBS= -lnsl -lpthread -lxenstore

VXTLIB_PATH=${SANDBOX}/vxtcom/lib/vxtlib/linux
LIBS1= ${LIBS} -L${PWD} -lmxt
#CFLAGS=-c -DNETWORK_ENDPOINT -DLINUX -Wall -Wextra
CFLAGS=-c -DVXT_ENDPOINT -DNAMED_PIPE_DOM0 -Wall -Wextra -fPIC
#CFLAGS=-c -DVXT_ENDPOINT -DNAMED_PIPE_DOM0 -DUNIT_TEST -Wall -Wextra
#CFLAGS=-c -DVXT_ENDPOINT -DLINUX -Wall -Wextra

TARGETS = $(LIBRARY)

all: $(TARGETS) $(STANDALONE)

mxt.o: $(COMMON)/mxt.c
	$(CC) $(CFLAGS) -o $@ $(COMMON)/mxt.c $(INCLUDES)

mxt_common.o: $(COMMON)/mxt_common.c
	$(CC) $(CFLAGS) -o $@ $(COMMON)/mxt_common.c $(INCLUDES)

mxt_bufops.o: $(COMMON)/mxt_bufops.c
	$(CC) $(CFLAGS) -o $@ $(COMMON)/mxt_bufops.c $(INCLUDES)

mxt_auth.o: mxt_auth.c
	$(CC) $(CFLAGS) -o $@ mxt_auth.c $(INCLUDES)

mxt_vxt.o: $(COMMON)/mxt_vxt.c
	$(CC) $(CFLAGS) -o $@ $(COMMON)/mxt_vxt.c $(INCLUDES)

mxt_platform.o: mxt_platform.c
	$(CC) $(CFLAGS) -o $@ mxt_platform.c $(INCLUDES)

libmxt.so: $(OBJS)
	$(CC) -o $@ $(OBJS) -L${VXTLIB_PATH} ${LIBS} \
	-shared -Wl,-soname,libmxt.so -o libmxt.so

clnt_test_file.o: clnt_test_file.c
	$(CC) $(CFLAGS) -o $@ clnt_test_file.c  $(INCLUDES)
	
clnt_test_file: clnt_test_file.o
	$(CC) -o $@ clnt_test_file.o -ldl

serv_test_file.o: serv_test_file.c
	$(CC) $(CFLAGS) -o $@ serv_test_file.c  $(INCLUDES)

serv_test_file: serv_test_file.o
	$(CC) -o $@ serv_test_file.o -ldl

mxt_trace.o: mxt_trace.c
	$(CC) $(CFLAGS) -o $@ mxt_trace.c  $(INCLUDES)
	
mxt_trace: mxt_trace.o
	$(CC) -o $@ mxt_trace.o -ldl


clobber: clean

clean:
	rm -f os
	rm -f $(LIBRARY) $(OBJS) $(STANDALONE) $(STANDALONE_OBJS)
	
