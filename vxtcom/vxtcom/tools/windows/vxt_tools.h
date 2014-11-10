#define _VXT_COMPONENT_ 0
#define _VXT_SUBSYSTEM_ 0

#ifndef _VXT_TOOLS_H_
#define _VXT_TOOLS_H_

/*
 *
 * Copyright (c) 2010, Symantec Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the
 * distribution.
 *
 * Neither the name of Symantec Corporation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

#include <windows.h>
#include <winioctl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ntsecapi.h>
#include <sys/stat.h>
#include <winsock.h>
#include <fcntl.h>
#include <string.h>

#include <windows\vxt_com_win.h>
#include <vxt_system.h>
#include <vxt_msg_export.h>
#include <vxt_socket.h>
#include <symcom_dev_table.h>
#include <vxt_trace_entries_windows.h>
#include <vxt_com.h>
#include <vxt_msg_dev.h>


VOID RtlInitUnicodeString(      
    PUNICODE_STRING DestinationString,
    PCWSTR SourceString
);
#define inline FORCEINLINE

#include <vxt_msg_os.h>
#define MAX_FILE_STRING 256

#define vxt_ssize_t LONG_PTR

#define VXT_CTRLR_DEV_NAME "vxtctrlr"

#define vxt_dev_open  vxtdev_open

#define vxt_dev_ioctl  vxtdev_ioctl

#define vxt_file_open  _open

#define vxt_file_read  _read

#define vxt_file_pread pread

#define vxt_file_write _write

#define vxt_file_pwrite pwrite

#define vxt_file_lseek _lseek

#define vxt_file_close _close

#define vxt_main int __cdecl main

#define VXT_BAD_FD NULL

//#define vxtctrlr_handle_t int

#define VXT_USR_WR 0
#define VXT_USR_RD 0
#define VXT_GRP_RD 0
#define VXT_OTH_RD 0

#define vxt_fchmod(x,y) 

#define vxt_atoll _atoi64
#endif /* _VXT_TOOLS_H_ */


