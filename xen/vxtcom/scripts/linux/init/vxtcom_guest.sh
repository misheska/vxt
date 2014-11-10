#! /bin/sh
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
#
# vxtcom_guest    Install the vxt controller front end
#
# chkconfig: 12345 0 99
# description: Start Guest VXTcom services
#

# Prepare VRTSvxt directory for authentication database file

mkdir --mode=0755 -p /opt/VRTSvxt

# Install the vxt xen adapter module
if [ -f /etc/vxtcom/vxtcard_front.ko.`uname -r` ]
then
	insmod /etc/vxtcom/vxtcard_front.ko.`uname -r`
fi

# Install the vxt authorization and routing service
if [ -f /etc/vxtcom/vxtauth_db.ko.`uname -r` ]
then
	insmod /etc/vxtcom/vxtauth_db.ko.`uname -r`
fi


# Install the vxt controller

if [ -f /etc/vxtcom/vxtctrlr.ko.`uname -r` ]
then
        insmod /etc/vxtcom/vxtctrlr.ko.`uname -r`
fi

