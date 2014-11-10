# -*- mode: Makefile; -*-

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

# A debug build of Xen and tools?
debug ?= n

XEN_COMPILE_ARCH    ?= $(shell uname -m | sed -e s/i.86/x86_32/ \
                                              -e s/ppc/powerpc/)
XEN_TARGET_ARCH     ?= $(XEN_COMPILE_ARCH)
XEN_TARGET_X86_PAE  ?= n

# Tools to run on system hosting the build
HOSTCC     = gcc
HOSTCFLAGS = -Wall -Werror -Wstrict-prototypes -O2 -fomit-frame-pointer

AS         = $(CROSS_COMPILE)as
LD         = $(CROSS_COMPILE)ld
CC         = $(CROSS_COMPILE)gcc
CPP        = $(CROSS_COMPILE)gcc -E
AR         = $(CROSS_COMPILE)ar
RANLIB     = $(CROSS_COMPILE)ranlib
NM         = $(CROSS_COMPILE)nm
STRIP      = $(CROSS_COMPILE)strip
OBJCOPY    = $(CROSS_COMPILE)objcopy
OBJDUMP    = $(CROSS_COMPILE)objdump

DISTDIR     ?= $(XEN_ROOT)/dist
DESTDIR     ?= /

INSTALL      = install
INSTALL_DIR  = $(INSTALL) -d -m0755
INSTALL_DATA = $(INSTALL) -m0644
INSTALL_PROG = $(INSTALL) -m0755

ifneq ($(debug),y)
# Optimisation flags are overridable
CFLAGS    ?= -O2 -fomit-frame-pointer
CFLAGS    += -DNDEBUG
else
# Less than -O1 produces bad code and large stack frames
CFLAGS    ?= -O1 -fno-omit-frame-pointer
CFLAGS    += -g
endif

# ************** x86_64.mk flags ****************
CONFIG_X86 := y
CONFIG_HVM := y
CONFIG_MIGRATE := y
CONFIG_XCUTILS := y
CONFIG_IOEMU := y
CONFIG_MBOOTPACK := y

CFLAGS += -m64
LIBDIR = lib64

# *************************************************


ifneq ($(EXTRA_PREFIX),)
EXTRA_INCLUDES += $(EXTRA_PREFIX)/include
EXTRA_LIB += $(EXTRA_PREFIX)/$(LIBDIR)
endif

test-gcc-flag = $(shell $(1) -v --help 2>&1 | grep -q " $(2) " && echo $(2))

CFLAGS += -Wall -Wstrict-prototypes

HOSTCFLAGS += $(call test-gcc-flag,$(HOSTCC),-Wdeclaration-after-statement)
CFLAGS     += $(call test-gcc-flag,$(CC),-Wdeclaration-after-statement)

LDFLAGS += $(foreach i, $(EXTRA_LIB), -L$(i))
CFLAGS += $(foreach i, $(EXTRA_INCLUDES), -I$(i))

# Choose the best mirror to download linux kernel
KERNEL_REPO = http://www.kernel.org

# If ACM_SECURITY = y, then the access control module is compiled
# into Xen and the policy type can be set by the boot policy file
#        y - Build the Xen ACM framework
#        n - Do not build the Xen ACM framework
ACM_SECURITY ?= n

# If ACM_SECURITY = y and no boot policy file is installed,
# then the ACM defaults to the security policy set by
# ACM_DEFAULT_SECURITY_POLICY
# Supported models are:
#       ACM_NULL_POLICY
#       ACM_CHINESE_WALL_POLICY
#       ACM_SIMPLE_TYPE_ENFORCEMENT_POLICY
#       ACM_CHINESE_WALL_AND_SIMPLE_TYPE_ENFORCEMENT_POLICY
ACM_DEFAULT_SECURITY_POLICY ?= ACM_NULL_POLICY

# Optional components
XENSTAT_XENTOP ?= y

VTPM_TOOLS ?= n

-include $(XEN_ROOT)/.config

