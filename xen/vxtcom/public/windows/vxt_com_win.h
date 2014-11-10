#ifndef VXT_COM_WIN_H
#define VXT_COM_WIN_H

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



typedef unsigned __int64 uint64;
typedef unsigned __int32 uint32;
typedef uint32 u32;
typedef unsigned long   ulong;
typedef unsigned short  ushort;
typedef unsigned int    uint;
typedef __int64         int64_t;
typedef unsigned __int64 uint64_t;
typedef unsigned __int32 uint32_t;
typedef unsigned long   ulong_t;
typedef unsigned short  ushort_t;
typedef unsigned int    uint_t;
typedef unsigned char   uchar_t;
typedef char * PCHAR;
typedef void * PVOID;
typedef uint32_t evtchn_port_t;

#define vxtarch_word LONG_PTR
#define off_t vxtarch_word
#define vxt_mb MemoryBarrier

#define PROT_READ    0x0001
#define PROT_WRITE   0x0002

#define MAP_SHARED   0x0001

#define EAFNOSUPPORT WSAEAFNOSUPPORT
#define EISCONN WSAEISCONN
#define ENETUNREACH WSAENETUNREACH
#define ENOTSOCK WSAENOTSOCK

#define inline FORCEINLINE

/*
 * object/file handle for VXTcom controllers
 * and the VXT system bus
 */

typedef struct vxtctrlr_handle{
    HANDLE handle;
    VOID   *privdata;
    VOID   *vde;
} *vxtctrlr_handle_t;
#define VXT_NULL_FD NULL

typedef struct vxt_mmap_struct  {
    uint64_t        size;
    uint64_t        token;
    uint32_t        access;
    uint32_t        share;
    HANDLE          process;
    PVOID           VAPointer;
} vxt_mmap_struct_t;

typedef struct vxt_munmap_struct  {
    uint64_t        token;
    HANDLE          process;
    PVOID           VAPointer;    
} vxt_munmap_struct_t;

typedef struct vxt_fd_struct  {
    VOID           *private_data;
    VOID           *vde;
} vxt_fd_struct_t;

typedef struct vxt_pollfd {
    vxtctrlr_handle_t vfh;
    short events;
    short revents;
} vxt_pollfd_t;
typedef unsigned int nfds_t;

typedef struct vxt_pollfd_flat {
    HANDLE handle;
    VOID   *privdata;
    VOID   *vde;
    short events;
    short revents;
} vxt_pollfd_flat_t;

typedef struct vxt_poll_struct  {
    uint32_t        nfd;
    uint32_t        timeout;
    vxt_pollfd_flat_t        fds[1];
} vxt_poll_struct_t;

typedef struct vxt_dumplog_struct  {
    char           *buf;
} vxt_dumplog_struct_t;

#define FILE_DEVICE_VXTCOM        16888

#define FILE_ANY_ACCESS                 0
#define FILE_READ_ACCESS          ( 0x0001 )    // file & pipe
#define FILE_WRITE_ACCESS         ( 0x0002 )    // file & pipe

/* 
 * VXTIOC - ioctl number space for vxt ioctls 
 * 
 * All vxt ioctl numbers should be or'd with this value. 
 */ 
#define VXTIOC CTL_CODE(FILE_DEVICE_VXTCOM, 0, METHOD_BUFFERED, \
FILE_READ_ACCESS | FILE_WRITE_ACCESS)

/* 
 * VXT_IOCTL_CODE - ioctl commands encoding macro 
 */ 
#define VXT_IOCTL_CODE(code)    (VXTIOC | (((code) | 0x800) << 2))
#define VXT_IOCTL_DECODE(code)    (((code)>>2)%1024)

/* flags for volioctl_info.vioc_flags */
#define VXTIOC_FLAG_KERNEL      0x00000001      /* kernel-to-kernel ioctl */
#define VXTIOC_FLAG_RDOKAY      0x00000002      /* read operations are okay */
#define VXTIOC_FLAG_WROKAY      0x00000004      /* write operations are okay */

#define ACCESS_FROM_CTL_CODE(ctrlCode)  (((ULONG) ((ctrlCode) & 0x0000C000)) >> 14)
/*
 * Possible valid range of VxVM ioctls:
 */
#define VXT_MIN_IOCTL           VXT_IOCTL_CODE(0)

#define VXT_MAX_IOCTL           VXT_IOCTL_CODE(0x33)

#define VXT_VALID_IOCTL(cmd)    \
    (VXT_MIN_IOCTL <= (cmd) && (cmd) <= VXT_MAX_IOCTL)

#define IOCTL_VXTCTRLR_MMAP       0x20
#define IOCTL_VXTCTRLR_MUNMAP     0x21
#define IOCTL_VXTCTRLR_POLL       0x22
#define IOCTL_FD_OPEN             0x31
#define IOCTL_FD_CLOSE            0x32
#define IOCTL_DUMP_LOG            0x33

  
typedef struct vxt_iocarg {
    void                    *privdata;
    void                    *arg;        
} vxt_iocarg_t;

/*
 *
 * vxtcom_strncpy:
 *
 * Modifies the behavior of strncpy so that
 * a NULL/0 is always placed in the last
 * position of the destination string, regardless
 * of the length of the source string.
 *
 */

static inline char *
vxtcom_strncpy(char *s1, char *s2, size_t cnt)
{
	char *ret;
	ret = strncpy(s1, s2, cnt);
	s1[cnt-1] = 0;
	return ret;
}

#ifndef FILE_LOG
#define FILE_LOG
#define VXT_LOG_FILE "vxtlog.txt"
#define VXT_LOG_PATH "C:\\Program Files\\Symantec\\VxVI\\"
extern void *vxt_log_fd;
#endif
#endif 
