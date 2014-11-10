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


# Note: On SLES9 cut is located at /usr/bin/cut.
# If /usr is a seperate filesystem it is not mounted when we run the
# vxfs-startup script.
# So we cannot use cut in our startup scripts.
# We have replaced all calls to cut by awk & sed.

error_load()
{
	echo "Error in loading module \"$1\". See documentation." >&2
}

get_kern_from_mod()
{
	vxmod=$1

	echo $vxmod | awk -F . '{for (i = 3 ; i <= NF; ++i) if (w == "") {w = $i} else {w = w"."$i}} END {print w}'
}

#
# Usage: modinst [-r kern_version] source_dir dest_subdir modname ...
#
# 'modname' should be specified with a trailing extension (e.g., vxfs.ko)r
#
# Determine the best match of 'modname' modules in 'source_dir' with
# a specific kernel version, and (optionally) try to load and install
# them.  By default, we match modules to the running kernel and do the
# load/install, but a different kernel version can be specified with
# a '-r kern_version' argument, in which case the matching version is
# echoed to stdout, but the module is not loaded or installed.
#
# In the default (non "-r" case), the module that loads is installed in
# /lib/modules/`uname -r`/${dest_subdir}.  The first 'modname' is used
# to verify module loading and is loaded first.  Additional modules
# can be specified after the first, and the corresponding versions of
# those modules will be loaded and installed.
#
# On success, the modules remain loaded
#
function modinst
{
	if [ $# -lt 3 ]
	then
		echo "Usage: modinst [-r kern_version] source_dir dest_subdir modname ..." >&2
		return 1
	fi

	if [ $1 == "-r" ]; then
		# -r kern_version - means try best fit and return it, dont load
		kern_version=$2
		no_load=1
		shift 2
	else
		kern_version=$(uname -r)
		no_load=0
	fi
	mod_loc=$1
	dest_subdir=$2
	shift 2
	modname=$1
	allmods=$*

	modname_nosfx=`echo $modname | sed s'/\.[^.]*$//'`

	export LC_COLLATE=C

	if [ ! -z "$modinst_debug" ]
	then
		set -x
	fi

	target_dir="/lib/modules/${kern_version}/${dest_subdir}"
	target_path=${target_dir}/${modname}

	rmmod $allmods > /dev/null 2>&1

	mkdir -m 0755 -p $target_dir

	# Try and find modules to load:
	#
	# 1 - If we have a module that exactly matches the kernel then use this
	#
	# 2 - If we did not find an exact module that will load we try
	#     and find the best match for this kernel.
	#
	#     We split the kernel version up into a number of sections:
	#
	#       a - short_kern_version    d - kern_buildno
	#       b - kern_major	    e - kern_flavour
	#       c - kern_minor
	#
	#     SuSE9: 2.6.5-7.139-smp    SuSE10: 2.6.16.20-0.12-smp
	#	     ^^^^^|^|^^^|^^^	        ^^^^^^|^^|^|^^|^^^
	#	       a   b  c   e		   a   b  c d   e
	#
	#     RHEL4: 2.6.9-34.ELsmp      RHEL5: 2.6.18-1.2747.el5v
	#	    ^^^^^|^^|^^^^^	        ^^^^^^|^|^^^^|^^^^
	#	      a   b    e		   a   b  c    e
	#
	#     RedFlag: 2.6.9-34.21AX
	#	      ^^^^^|^^|^^|^^
	#		a   b  c  e
	#
	#     We look for modules that match the short_kern_version
	#
	#     If we dont find any modules that match then just try
	#     short_kern_major as we may be trying to install on a
	#     RedHat patched kernel
	#	       eg 2.6.9-11.EL.root.IT74053.V4smp
	#
	#     We only look for modules that match the short_kern_version
	#     and the kern_flavour.
	#
	#     We first try and find either an exact match for the kern-major
	#     if we cant find a module that has a matching kern_major we
	#     will use the highest mod_major/mod_minor that is less than
	#     the actual kern_major/kern_minor version.
	#
	#     Then try and load this module if it loads assume
	#     this is the module version to use.

	mod_loc="/etc/vxtcom"
	kern_version=`uname -r`
	short_kern_version=`echo $kern_version | \
		awk -F [.-] '{print $1"."$2"."$3}'`
	kern_major=`echo $kern_version | awk -F [.-] '{print $4}'`
	kern_minor=`echo $kern_version | \
		awk -F [.-] '$5~/^[0-9]+/ {print $5}' | sed -e 's/[^a-zA-Z]//g'`
	kern_flavour=`echo $kern_version | awk -F [.-] '{print $NF}' | \
		sed 's/k//'`

	if [ -f /etc/redflag-release ]; then
		# RedFlag is really just RedHat
		# with the one change that 'uname -r' is a different format
		# The kernl flavout is 'AX' insted of 'EL' ior 'rl'
		# and  there is no delimiter after kern_minor
		# i.e. RedHat  -2.6.9-42.EL
		#      RedFlag - 2.6.9-34.21AX
		# so load the 'EL' modules on RedFlag

		kern_flavour=`echo $kern_version | \
			sed -e 's/[^a-zA-Z]//g' | sed 's/k//' | sed -e 's/AX/EL/'`
	fi

	# find exact match(s) based on kern_major and flavour
	if [ -z "$kern_minor" ]; then
		exact_mod=`ls ${mod_loc}/${modname}.${short_kern_version}[.-]${kern_major}*[.-]${kern_flavour} 2>/dev/null`
	else
		exact_mod=`ls ${mod_loc}/${modname}.${short_kern_version}[.-]${kern_major}*[.-]${kern_minor}*[.-]${kern_flavour} 2>/dev/null`
	fi
	probable_mod_list=`ls ${mod_loc}/${modname}.${short_kern_version}*[-.]${kern_flavour} 2>/dev/null`

	if [ -z "$probable_mod_list" ];then
		# We do not have any module that matches $kern_flavour
		# We should really try and install if this matches a
		# $short_kern_version module
		probable_mod_list=`ls ${mod_loc}/${modname}.${short_kern_version}* 2>/dev/null`
	fi

	if [ $no_load -eq 0 ] && [ -z "$exact_mod" ] && \
			[ -z "$probable_mod_list" ] ; then
		echo "This release of $modname_nosfx does not contain any modules which "  >&2
		echo "are suitable for your $kern_version kernel."  >&2
		return 1
	fi

	noexactmod=

	# Now try and load the exact module

	mod_to_load=

	if [ $no_load -eq 1 ] && [ ! -z $exact_mod ] ; then
		# Asked for test best fit - give module version
		mod_to_load=`get_kern_from_mod $exact_mod`
		echo $mod_to_load
		return 0
	else

		for mod in $exact_mod
		do
			version=`echo $mod | sed 's/.*'${modname}'\.//'`
			insmod $MODINST_INS_ARGS $mod $MODINST_MOD_ARGS \
				> /dev/null 2>&1
			if [ $? -eq 0 ] ; then
				# loads ok, this is the module to use
				mod_to_load=`get_kern_from_mod $mod`
				break;
			fi
		done
	fi

	no_minor=

	if [ -z "$mod_to_load" ] ; then
		# We do not have an exact module for this kernel so
		# we need to find the best fit.
		# This is the highest mod_major/mod_minor not greater than
		# actual kern_major/kern_minor

		# build list of mod_majors to iterate through
		for mod in $probable_mod_list
		do
			mod_version=`echo $mod | sed 's/.*'${modname}'\.//'`
			mod_major=`echo $mod_version | awk -F [.-] '{print $4}'`
			echo $mod_major >>/tmp/sort.$$
		done
		major_list=`sort -run /tmp/sort.$$`
		rm /tmp/sort.$$
		for mod_major in $major_list
		do
			use_major=0;
			max_minor=0;
			if [ ${mod_major} -eq ${kern_major} ]; then
				# Exact major match
				use_major=$mod_major
				# If we have a kern_major match we only want
				# to try a kern_minor less than kern_minor
				max_minor=$kern_minor
			else
				if [ $kern_major -gt $mod_major ]  && \
						[ $mod_major -gt $use_major ]; then
					# Get next highest major
					use_major=$mod_major
					max_minor=9999
				fi
			fi

			probable_mod_list=`ls ${mod_loc}/${modname}.${short_kern_version}[-.]${use_major}*[-.]${kern_flavour} 2>/dev/null`
			if [ -z "$probable_mod_list" ];then
			    # No match lessen granuality in case patched kernel
			    probable_mod_list=`ls ${mod_loc}/${modname}.${short_kern_version}[-.]${use_major}* 2>/dev/null`
			fi

			# We have major revision, pick the highest minor.
			use_minor=0
			no_minor=
			for mod in $probable_mod_list
			do
				if [ -z "$kern_minor" ]
				then
					no_minor=1
					break
				fi
				mod_version=`echo $mod | sed 's/.*'${modname}'\.//'`
				if [ -f /etc/SuSE-release ] &&
					grep -q "VERSION = 10" /etc/SuSE-release
				then
					# Suse 10 format is 2.6.16.20-0.12-smp
					mod_minor=`echo $mod_version | \
						awk -F [-.] '{print $6}'`
				else
					mod_minor=`echo $mod_version | \
						awk -F [.-] '$5~/^[0-9]+/ {print $5}'`
				fi
				if [ -z "$mod_minor" ]; then
					# We dont have a minor
					no_minor=1
					break;
				fi
				if [ $mod_minor -gt $use_minor ] && \
						[ $mod_minor -le $max_minor ]; then
					use_minor=$mod_minor
					fi
			done
			if [ $use_minor -ne 0 ] || [ ! -z $no_minor ]; then
				break
			fi
		done
		if [ ! -z $no_minor ]; then
			mod=`ls ${mod_loc}/${modname}.${short_kern_version}[-.]${use_major}*[-.]${kern_flavour} 2>/dev/null`
		else
			mod=`ls ${mod_loc}/${modname}.${short_kern_version}[-.]${use_major}*${use_minor}*[-.]${kern_flavour} 2>/dev/null`
		fi
		# We have computed the best fit
		if [ $no_load -eq 1 ]; then
			# We are just doing a test best fit
			# So give best module version
			if [ ! -z "$mod" ]; then
				mod_to_load=`get_kern_from_mod $mod`
				echo $mod_to_load
				return 0
			else
				return 1
			fi
		fi
		if [ ! -z "$mod" ]; then
		  # We can try to load closet module that matches given kernel.
		  version=`echo $mod | sed 's/.*'${modname}'\.//'`
		  insmod $MODINST_INS_ARGS $mod $MODINST_MOD_ARGS > /dev/null 2>&1

		  if [ $? -eq 0 ] ; then
			# loads ok, this is the module to use
			mod_to_load=`get_kern_from_mod $mod`
			noexactmod=1
		  else
			error_load $modname_nosfx
			return 1
		  fi
		else
		    # This must be a patched kernel we are running so see if
		    # any module will load!
		    if [ ! -z $no_minor ]; then
			try_mod=`ls ${mod_loc}/${modname}.${short_kern_version}[-.]${use_major}[-.]* 2>/dev/null`

		    else
			try_mod=`ls ${mod_loc}/${modname}.${short_kern_version}[-.]${use_major}[-.]${use_minor}[-.]* 2>/dev/null`
		    fi
		    for mod in ${try_mod}; do
			version=`echo $mod | sed 's/.*'${modname}'\.//'`
			insmod $MODINST_INS_ARGS ${mod} $MODINST_MOD_ARGS >/dev/null 2>&1
			if [ $? -eq 0 ];then
				# Use this one
				mod_to_load=`get_kern_from_mod $mod`
				rmmod $modname_nosfx
				break
			fi
		    done
		fi
	fi

	if [ -z "$mod_to_load" ] ; then
		rm -rf $target_path
		echo "ERROR: No appropriate modules found." >&2
		error_load $modname_nosfx
		return 1
	fi

	need_depmod=0
	for mod in $allmods; do
		target=${target_dir}/${mod}
		source=${mod_loc}/${mod}.$mod_to_load
		if [ -x /usr/bin/stat -a -e "${target}" ]; then
			inum1=$(stat -c "%i" "${target}");
			if [ $? -eq 0 ]; then
				inum2=$(stat -c "%i" "${source}");
				if [ $? -eq 0 -a $inum1 -eq $inum2 ]; then
					continue
				fi
			fi
		fi

		if [ -n "$noexactmod" -a -z "$noexactmod_warn" ] ; then
			echo "WARNING:  No modules found for $kern_version, using compatible modules for ${mod_to_load}." >&2
			noexactmod_warn=1
		fi

		ln -sf $source $target
		# Check for ln error?  No, will display an error if it fails.
		need_depmod=1

	done

	# Links created/changed?  If so, refresh module dependencies.
	if [ $need_depmod -eq 1 ]; then
		depmod  >/dev/null 2>&1
	fi

	#
	# Now that we know the links and depmod are set up properly,
	# we can load the modules for real
	#
	for mod in $allmods; do
		mod_nosfx=`echo $mod | sed s'/\.[^.]*$//'`
		allmods_nosfx="$mod_nosfx $allmods_nosfx"
		modprobe $MODINST_PROBE_ARGS $mod_nosfx $MODINST_MOD_ARGS
		if [ $? -ne 0 ]; then
			echo "ERROR: modprobe error for $mod_nosfx.  See documentation." >&2
			rmmod $allmods_nosfx > /dev/null 2>&1
			for mod in $allmods; do
				rm -f ${target_dir}/${mod} > /dev/null 2>&1
			done
			return 1
		fi
	done

#	echo $mod_to_load
	return 0
}


