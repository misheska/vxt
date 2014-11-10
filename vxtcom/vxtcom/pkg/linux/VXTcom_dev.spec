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


# version/release defined in file rpm.version

Name: VRTSvxtcom-dev
Version: %{version}
Release: %{release}_%{VXTCOMREL}
Summary: Symantec Virtual Bus Controller and Devices by Symantec
Group: Data Centre Management Group
Vendor: Symantec Corporation.
License: Copyright (c) 1990-2007 Symantec Corporation. All rights reserved.  Symantec, the Symantec Logo, Veritas, and Veritas Storage Foundation are trademarks or registered trademarks of Symantec Corporation or its affiliates in the U.S.  and other countries. Other names may be trademarks of their respective owners.  The Licensed Software and Documentation are deemed to be "commercial computer software" and "commercial computer software documentation" as defined in FAR Sections 12.212 and DFARS Section 227.7202.
URL: www.support.veritas.com
Packager: support@symantec.com
# requires:  
AutoReqProv: no
# Source: No sources

%define _unpackaged_files_terminate_build 0
%define _missing_doc_files_terminate_build 0

%description
Symantec Virtual Bus Controller and Devices for Linux

%pre

# Create some dirs and links which are needed for VXTcom
[ -d /etc/vxt ] || mkdir -p /etc/vxt

CPI_VM_RPM_MSG=/etc/vxt/.cpi_vxt_rpm_msg  # name of error/warning file for CPI
cpilog()
{
	echo $* | /usr/bin/tee -a $CPI_VM_RPM_MSG
}
if [ -e $CPI_VM_RPM_MSG ] ; then
	rm -f $CPI_VM_RPM_MSG
fi

cpilog "Starting preinstall of VXTCOM RPM"

# is this rpm is appropriate for this architecture?


%ifarch ia64
if [ `arch` != "ia64" ] ; then
	cpilog "This package is built only for the ia64 processor. Exiting..."
	exit 1
fi
%endif
%ifarch x86_64
if [ `arch` != "x86_64" ] ; then
	cpilog "This package is built only for the x86_64 processor. Exiting..."
	exit 1
fi
%endif
%ifarch i686
if [ `arch` != "i686" ] ; then
	cpilog "This package is built only for the i686 processor.  Exiting..."
	exit 1
fi
%endif


if [ $1 -eq 2 ]; then
	UPGRADE="YES"
else
	UPGRADE="NO"
fi

# set default creation mode
umask 0022



#
# We will need to stop or signal
# pause/restart for all services
# libraries based on the VXT
# controller devices.  This is
# inherently too complex even
# for a clean-shutdown without
# an actual VXTcom general
# service exported through the
# controller from each device
# to signal it's client.  A
# full image of the controller
# devices and their state will
# be needed for restart
# 
# In the mean-time we will just
# check for the MXT service
# (our first client on our
# first device) and shut it
# down if we find it.
# From tiny acorns ...
#

PID=`pgrep mxtlib`
if [ ! -z "$PID" ] ; then
	echo "stopping mxtlib"
	kill -9 $pid 2>/dev/null
fi


cpilog "Pre-install of VXTCOM RPM complete"



%post
CPI_VM_RPM_MSG=/etc/vxt/.cpi_vxt_rpm_msg  # name of error/warning file for CPI
cpilog()
{
	echo $* | /usr/bin/tee -a $CPI_VM_RPM_MSG
}


cpilog "Starting Post-install of VXTCOM RPM"


[ -d /lib/modules/`uname -r`/veritas/vxtcom ] ||  \
	mkdir -m 0755 -p /lib/modules/`uname -r`/veritas/vxtcom 2> /dev/null ;


#clean up existing modules (if any) before loading new ones.
#remove vatauth_db last
for i in vxtctrlr.ko vxtauth_db.ko vxtcard_front.ko vxtcard_back.ko
do
        if lsmod | grep -q $i
        then
		cpilog "Unloading old module $i"
                modprobe -r $i
        fi
done

# Remove old modules

release=`uname -r`
vxcom_list=`find /lib/modules/$release/veritas/vxtcom -name "vxtctrlr.ko"`
for vmmod in $vxcom_list
do
        if [ -f $vmmod ]; then
		cpilog "Removing old module $vmmod"
                rm -f $vmmod
        fi
done
vxcom_list=`find /lib/modules/$release/veritas/vxtcom -name "vxtauth_db.ko"`
for vmmod in $vxcom_list
do
        if [ -f $vmmod ]; then
		cpilog "Removing old module $vmmod"
                rm -f $vmmod
        fi
done
vxcom_list=`find /lib/modules/$release/veritas/vxtcom -name "vxtcard_back.ko"`
for vmmod in $vxcom_list
do
        if [ -f $vmmod ]; then
		cpilog "Removing old module $vmmod"
                rm -f $vmmod
        fi
done
vxcom_list=`find /lib/modules/$release/veritas/vxtcom -name "vxtcard_front.ko"`
for vmmod in $vxcom_list
do
        if [ -f $vmmod ]; then
		cpilog "Removing old module $vmmod"
                rm -f $vmmod
        fi
done


# Load the VXTcom module, and set the provisioning daemon and 
# vxtcom_boot in init.d. Load vxtauth_db before vxtcom_back or
# vxtctrlr.

. /etc/vxtcom/modinst-vxt
if [ -f /usr/bin/xenstore-ls ]; then
if `/usr/bin/xenstore-ls /mh 2>&1 | grep -q "XenSource-TM_XenEnterprise-TM"`; then
	cpilog "Detected Dom0, installing vxtcard_back.ko.*"

	mkdir --mode=0755 -p /opt/VRTSvxt

	mod_err=`modinst /etc/vxtcom veritas/vxtcom vxtcard_back.ko 2>&1`
	if [ $? = 1 ] ; then
	        cpilog "$mod_err"
	        exit 1
	fi
	mod_err=`modinst /etc/vxtcom veritas/vxtcom vxtauth_db.ko 2>&1`
	if [ $? = 1 ] ; then
	        cpilog "$mod_err"
	        exit 1
	fi
	mod_err=`modinst /etc/vxtcom veritas/vxtcom vxtctrlr.ko 2>&1`
	if [ $? = 1 ] ; then
	        cpilog "$mod_err"
	        exit 1
	fi
	if [ -f /etc/vxtcom/bin/vxt_provision_daemon -a -O  /etc/vxtcom/bin/vxt_provision_daemon ]
	then
		while :
		do
			pid=$(/sbin/pidof vxt_provision_daemon)
			if [ "$pid" == "" ]; then break; fi
			kill -9 $pid
			usleep 100
		done

		/etc/vxtcom/bin/vxt_provision_daemon 2 >&1 > /dev/null &
	fi

	if [ -f /etc/SuSE-release ] ; then
		cp /etc/vxtcom/vxtcom_dom0.sh /etc/init.d/vxtcom_boot
	else
		cp /etc/vxtcom/vxtcom_dom0.sh /etc/rc.d/init.d/vxtcom_boot
	fi
	echo "Installing file /etc/init.d/vxtcom_boot"
	chkconfig --add vxtcom_boot 2>/dev/null
else
	cpilog "This is a guest, installing vxtctrlr.ko.*"

	mkdir --mode=0755 -p /opt/VRTSvxt

	mod_err=`modinst /etc/vxtcom veritas/vxtcom vxtcard_front.ko 2>&1`
	if [ $? = 1 ] ; then
	        cpilog "$mod_err"
	        exit 1
	fi
	mod_err=`modinst /etc/vxtcom veritas/vxtcom vxtauth_db.ko 2>&1`
	if [ $? = 1 ] ; then
	        cpilog "$mod_err"
	        exit 1
	fi
	mod_err=`modinst /etc/vxtcom veritas/vxtcom vxtctrlr.ko 2>&1`
	if [ $? = 1 ] ; then
	        cpilog "$mod_err"
	        exit 1
	fi

	if [ -f /etc/SuSE-release ] ; then
		cp /etc/vxtcom/vxtcom_guest.sh /etc/init.d/vxtcom_boot
	else
		cp /etc/vxtcom/vxtcom_guest.sh /etc/rc.d/init.d/vxtcom_boot
	fi
	echo "Installing file /etc/init.d/vxtcom_boot"
	chkconfig --add vxtcom_boot 2>/dev/null
fi
else
	cpilog "xenstore-ls is not present-are we Xen?"
	cpilog "There are no xen tools, do stand-alone install"

	mkdir --mode=0755 -p /opt/VRTSvxt

	mod_err=`modinst /etc/vxtcom veritas/vxtcom vxtcard_front.ko 2>&1`
	if [ $? = 1 ] ; then
	        cpilog "$mod_err"
	        exit 1
	fi
	mod_err=`modinst /etc/vxtcom veritas/vxtcom vxtauth_db.ko 2>&1`
	if [ $? = 1 ] ; then
	        cpilog "$mod_err"
	        exit 1
	fi
	mod_err=`modinst /etc/vxtcom veritas/vxtcom vxtctrlr.ko 2>&1`
	if [ $? = 1 ] ; then
	        cpilog "$mod_err"
	        exit 1
	fi

	if [ -f /etc/SuSE-release ] ; then
		cp /etc/vxtcom/vxtcom_guest.sh /etc/init.d/vxtcom_boot 2>/dev/null
	else
		cp /etc/vxtcom/vxtcom_guest.sh /etc/rc.d/init.d/vxtcom_boot 2>/dev/null
	fi
	echo "Installing file /etc/init.d/vxtcom_boot"
	chkconfig --add vxtcom_boot 2>/dev/null
fi


CVSFILES=`find /usr/vxtcom_ddk/include -name "CVS"`
for CVSDIR in $CVSFILES 
do
	if [ -d $CVSDIR ]; then
		rm -rf $CVSDIR
	fi
done

cpilog "Post-install of VXTCOM RPM complete"



%preun
CPI_VM_RPM_MSG=/etc/vxt/.cpi_vm_rpm_msg  # name of error/warning file for CPI
cpilog()
{
        echo $* | /usr/bin/tee -a $CPI_VM_RPM_MSG
}

cpilog "Starting Pre-uninstall actions"



# remove init.d vxtcom_boot script

chkconfig --del vxtcom_boot 2>/dev/null
rm -f /etc/init.d/vxtcom_boot

cpilog "Remove VXTcom binaries"
rm -rf /etc/vxtcom >/dev/null 2>&1

# if a vxt provisioning daemon is running, shut it down
while :
                do
                pid=$(/sbin/pidof vxt_provision_daemon)
                if [ "$pid" == "" ]; then break; fi
                kill -9 $pid
                usleep 100

                done



vxcom_list=`find /lib/modules/*/veritas/vxtcom -name "vxtctrlr.ko"`
for vmmod in $vxcom_list
do
        if [ -f $vmmod ]; then
		cpilog "removing module $vmmod"
                rm -f $vmmod
        fi
done
vxcom_list=`find /lib/modules/*/veritas/vxtcom -name "vxtauth_db.ko"`
for vmmod in $vxcom_list
do
        if [ -f $vmmod ]; then
		cpilog "removing module $vmmod"
                rm -f $vmmod
        fi
done
vxcom_list=`find /lib/modules/*/veritas/vxtcom -name "vxtcard_back.ko"`
for vmmod in $vxcom_list
do
        if [ -f $vmmod ]; then
		cpilog "removing module $vmmod"
                rm -f $vmmod
        fi
done
vxcom_list=`find /lib/modules/*/veritas/vxtcom -name "vxtcard_front.ko"`
for vmmod in $vxcom_list
do
        if [ -f $vmmod ]; then
		cpilog "removing module $vmmod"
                rm -f $vmmod
        fi
done

#clean up existing modules remove vxtauth_db last
for i in vxtctrlr vxtauth_db vxtcard_back vxtcard_front
do
        if lsmod | grep -q $i
        then
		cpilog "Unloading old module $i"
                modprobe -r $i
        fi
done



cpilog "Pre-uninstall actions complete"

%postun
CPI_VM_RPM_MSG=/etc/vxt/.cpi_vm_rpm_msg  # name of error/warning file for CPI
cpilog()
{
        echo $* | /usr/bin/tee -a $CPI_VM_RPM_MSG
}

cpilog "Stating Post-uninstall actions"

rm -f /usr/lib/libvxt_socket.so
rm -f /usr/lib64/libvxt_socket.so
rm -f /usr/lib/libmxt.so
rm -f /usr/include/mxt_lib.h
rm -rf /usr/vxtcom_ddk

cpilog "Post-uninstall actions complete"


%files
%defattr(0555,root,sys)

%attr(0755, root, sys) %dir /etc/vxtcom
%attr(0644, root, root) /etc/vxtcom/vxtauth_db.ko.*
%attr(0644, root, root) /etc/vxtcom/vxtcard_back.ko.*
%attr(0644, root, root) /etc/vxtcom/vxtcard_front.ko.*
%attr(0644, root, root) /etc/vxtcom/vxtctrlr.ko.*
%attr(0644, root, root) /etc/vxtcom/pkginfo
%attr(0444, root, root) /etc/vxtcom/README.contents
%attr(0755, root, sys) /etc/vxtcom/modinst-vxt
%attr(0744, root, sys) /etc/vxtcom/vxtcom_dom0.sh
%attr(0744, root, sys) /etc/vxtcom/vxtcom_guest.sh


/etc/vxtcom/bin
%attr(0755, root, sys) /usr

