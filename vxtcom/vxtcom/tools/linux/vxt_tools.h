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

/*
 *
 * vxt_tools.h
 *
 * vxt_tools.h is an os dependent file.  In this case
 * it is a linux specific file.  Functions, data types,
 * etc.  that are not shared amongst Operating system
 * posix environments are abstracted and defined here
 * for the targeted environment.
 *
 */ 

#include <stdio.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>





#define VXT_DEBUG_LEVEL 0

#define VXTPRINT_PRODUCT_LEVEL 0
#define VXTPRINT_LOG_LEVEL     1
#define VXTPRINT_DEBUG_LEVEL   2
#define VXTPRINT_OPTION_PARSE_DEBUG 4

/*
 * VXTPRINT will support debug levels in future through VXT_DEBUG_LEVEL
 */


#define VXTPRINT(level,a,b...)                                   \
        do { if ((level) <= VXT_DEBUG_LEVEL)                     \
             printf("%s: " a, __FUNCTION__ , ## b);              \
        } while (0)

#define vxtarch_word long

#define MAX_FILE_STRING 256

#define vxt_ssize_t ssize_t


#define VXT_CTRLR_DEV_NAME "/dev/vxtctrlr"

#define vxt_dev_open  open

#define vxt_dev_ioctl ioctl

#define vxt_dev_close close

#define vxt_file_open  open

#define vxt_file_read  read

#define vxt_file_pread pread

#define vxt_file_write write

#define vxt_file_pwrite pwrite

#define vxt_file_lseek lseek

#define vxt_file_close close

#define vxt_malloc malloc

#define vxt_free free

#define vxt_main main

#define VXT_BAD_FD -1

#define vxtctrlr_handle_t int

#define vxt_atoll atoll


#define VXT_USR_WR S_IWUSR
#define VXT_USR_RD S_IRUSR
#define VXT_GRP_RD S_IRGRP
#define VXT_OTH_RD S_IROTH

#define vxt_fchmod fchmod


#endif /* _VXT_TOOLS_H_ */


