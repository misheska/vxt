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


all: common vxt_scan vxt_trace vxt_db_reader vxt_auth


common:
	rm -f os
	ln -s ../../public/linux os


OS_VERSIONS = -Dlinux_ver1=1 -Dwin=2 -DVXT_OS_DEP=linux_ver1

vxt_auth:	../common/vxt_auth.c
	rm -f vxt_auth
	cc $^ -I. -I../../public -I../.. $(OS_VERSIONS) -L../../lib/vxtlib/linux -lvxt_socket -o vxt_auth

vxt_db_reader:	../common/vxt_db_reader.c
	rm -f vxt_db_reader
	cc $^ -I. -I../../public -I../.. $(OS_VERSIONS) -L../../lib/vxtlib/linux -lvxt_socket -o vxt_db_reader


vxt_scan:	../common/vxt_scan.c
	rm -f vxt_scan
	cc $^ -I. -I../../public -L../../lib/vxtlib/linux -lvxt_socket -o vxt_scan

vxt_trace:	../common/vxt_trace.c
	rm -f vxt_trace
	cc $^ -I. -I../../public -L../../lib/vxtlib/linux -lvxt_socket -o vxt_trace

clean: 
	rm -f vxt_db_reader
	rm -f vxt_trace
	rm -f vxt_scan
	rm -f vxt_auth
	rm -f os

