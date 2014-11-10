#ifndef VXT_MSG_OS_H
#define VXT_MSG_OS_H

/*
 *
 * vxt_msg_os.h:
 *
 * This file implements the os specific elements of the
 * VXT system message device library interface.  The message
 * device, (vxt_msg) is comprised of a driver and device
 * emulation as well as a user level interface.  The 
 * device emulation layer provides devices that reside
 * on the VXTcom controller bus.  To effect this, a kernel
 * level library exists that is registered with the vxt
 * controller.  Subsequent calls to device create on the
 * controller may invoke the library, creating or connecting to
 * vxt_msg devices.
 *
 * The purpose of this file is to supplement the user level
 * library that talks to the VXT controller and communicates
 * with the vxt_msg library through VXTcom system interfaces.
 *
 */

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

#define vxt_mb MemoryBarrier
#define STATUS_SUCCESS                   ((NTSTATUS)0x00000000L)

/*
 * VXT_INVALID_DEV_OBJ:
 *
 * In the linux port this value is used to denote an
 * invalid file descriptor
 *
 */

#define VXT_INVALID_DEV_OBJ NULL

/*
 *
 * VXT_DEV_DIR:
 *
 * The root directory that serves as a repository
 * for VXT controller and device files
 */

#define VXT_DEV_DIR "\\device\\Vxt\\"


/*
 *
 * VXT_COM_DEV_NAME:
 * 
 * The device path and name of the first VXT
 * controller
 */

#define VXT_COM_DEV_NAME  "\\device\\Vxt\\vxtctrlr0"


/*
 * VXT_COM_BUS_NAME
 *
 * The device path and name of the VXT
 * system bus in which the VXT controllers
 * are plugged.
 *
 */

#define VXT_COM_BUS_NAME  "\\device\\Vxt\\vxtctrlr-bus"



/*
 * 
 * VXT_NO_TIMEOUT:
 *
 * The value provided to disable timeouts
 *
 */

#define VXT_NO_TIMEOUT -1



#define VXT_MSG_SIGNAL       0x1
#define VXT_MSG_WAIT         0x2
#define VXT_MSG_AUTO_STREAM  0x4

 

/*
 * Shared memory mapping flags
 */

#define VXT_MAP_SHARED MAP_SHARED


#define VXTPRINT_PRODUCT_LEVEL 0
#define VXTPRINT_DEBUG_LEVEL   1
#define VXTPRINT_LOG_LEVEL     2

#define VXTPRINT_ERROR 0


/*
 * UMI variant support
 */

#ifndef _VXT_COMPONENT_
#error "VxT component designation is missing\n"
#endif
#ifndef _VXT_SUBSYSTEM_
#error "VxT subsystem designation is missing\n"
#endif

extern char dbgfuncname[64];
extern int VXT_DEBUG_LEVEL;
extern int umi_error_level;

#ifdef VXT_DEBUG
#define VXTPRINT sprintf(dbgfuncname, "%s: ",__FUNCTION__); _VXTPRINT
#define VXT_PERROR printf("%s: ",__FUNCTION__); _VXT_PERROR
#define UMI_LOG umi_error_level = 2; sprintf(dbgfuncname, "%s",__FUNCTION__); _UMI_LOG
#define UMI_WARN umi_error_level = 1; sprintf(dbgfuncname, "%s",__FUNCTION__); _UMI_LOG
#define UMI_ERROR umi_error_level = 0; sprintf(dbgfuncname, "%s",__FUNCTION__); _UMI_LOG
#else
#define VXTPRINT _VXTPRINT
#define VXT_PERROR _VXT_PERROR
#define UMI_LOG  umi_error_level = 2; _UMI_LOG
#define UMI_WARN umi_error_level = 1; _UMI_LOG
#define UMI_ERROR umi_error_level = 0; 
#endif

static inline void
_UMI_LOG (long umi, int level, char *fmt, ...)
{
    char s[1024], t[1000];
    int len=0;
    va_list argp;

	if (level <= VXT_DEBUG_LEVEL){
        va_start(argp, fmt);
        vsprintf(t, fmt, argp);
        switch(umi_error_level){
            case 0:
                sprintf(s, "VxT ERROR V-320-%d-%d-%d %s: %s", _VXT_COMPONENT_, _VXT_SUBSYSTEM_, umi, dbgfuncname, t);
                break;
            case 1:
                sprintf(s, "VxT WARNING V-320-%d-%d-%d %s: %s", _VXT_COMPONENT_, _VXT_SUBSYSTEM_, umi, dbgfuncname, t);
                break;
            case 2:
                sprintf(s, "VxT Info V-320-%d-%d-%d %s: %s", _VXT_COMPONENT_, _VXT_SUBSYSTEM_, umi, dbgfuncname, t);
                break;
        }
        len = strlen(s);
        if(len){
            printf("%s", s);
	        if (level == VXTPRINT_PRODUCT_LEVEL){ 
                //vxt_applog(s);
	        }
        }
        va_end(argp);
	} 
}


/*
 * VXTPRINT will support debug levels in future through VXT_DEBUG_LEVEL
 */


static inline void 
_VXTPRINT (int level, char *fmt, ...)
{
    char s[1024], t[1000];
    int len=0;
    va_list argp;

	if (level <= VXT_DEBUG_LEVEL){
        va_start(argp, fmt);
        vsprintf(t, fmt, argp);
        sprintf(s, "%s: %s",dbgfuncname, t);
        len = strlen(s);
        if(len){
            printf("%s", s);
	        if (level == VXTPRINT_PRODUCT_LEVEL){ 
                //vxt_applog(s);
	        }
        }
        va_end(argp);
	}   
}

static inline void 
_VXT_PERROR (char *fmt, ...)
{
    char s[1024];
    va_list argp;

    va_start(argp, fmt);
    vsprintf(s, fmt, argp);
    printf("%s", s);
    va_end(argp);
}
/*
 * File and object mapping protections
 */

#define VXT_PROT_READ  PROT_READ
#define VXT_PROT_WRITE PROT_WRITE

/*
 * Shared memory mapping flags
 */

#define VXT_MAP_SHARED MAP_SHARED

/*
 * object/file access and write request flags
 */

#define VXT_RDONLY O_RDONLY 
#define VXT_WRONLY O_WRONLY 
#define VXT_RDWR   O_RDWR

extern int vxtdev_ioctl(vxtctrlr_handle_t fh, int cmd, VOID *arg);
extern vxtctrlr_handle_t vxtdev_open(char * device, unsigned int permissions);
extern void vxtdev_close(vxtctrlr_handle_t vh);

extern void * vxt_mmap(void *starting_addr, uint32_t len, 
         int prot, int flags, vxtctrlr_handle_t fd, int64_t offset);
extern  void vxt_munmap(void *addr, vxtctrlr_handle_t fd, uint64_t token, uint32_t len);
extern int poll(vxt_pollfd_t *fds, nfds_t nfd, int timeout);
extern int vxt_msg_event_wait(vxtctrlr_handle_t fd, int timeout, int *revent);
extern void *vxt_malloc_poll_obj(int object_count);
extern void vxt_free_poll_obj(void *poll_object, int object_count);
extern void vxt_write_event_wait(void *poll_obj_handle, int count,
                     uint64_t object, uint32_t events, uint32_t revents);
extern void vxt_read_event_wait(void *poll_obj_handle, int count,
                    uint64_t *object, uint32_t *events, uint32_t *revents);
extern int vxt_msg_cookie_check (char *cookie, char *id, uint32_t namelen);


/*
 * 
 * vxt_msg_ioctl
 *
 * OS dependent wrapper for user level
 * commands on an open object/file
 *
 */

static inline int
vxt_msg_ioctl(vxtctrlr_handle_t handle, uint64_t command, void *data)
{
   return vxtdev_ioctl(handle, (int) command, data);
}

/*
 * 
 * vxt_msg_open_com
 *
 * OS dependent wrapper for object/file
 * open support
 *
 */

static inline vxtctrlr_handle_t
vxt_msg_open_com(char *name, unsigned int permissions)
{
   return (vxtctrlr_handle_t)vxtdev_open(name, permissions);
}

/*
 *
 * vxt_msg_open_dev
 *
 * OS dependent wrapper for device object open.
 * vxt_msg_open_dev differs from vxt_msg_open_com
 * in that an extra handle parameter is passed.
 * The handle represents an alternate way to 
 * establish connection.  The file name works
 * if there is a traditional file presence.  In
 * the case of some unix systems, the device file
 * may not be present at the time of this request
 * in this case, the handle is used to transmit
 * major and minor numbers, and a mknod
 * is invoked to create the device file.
 *
 */


static inline vxtctrlr_handle_t
vxt_msg_open_dev(char *name, unsigned int permissions, uint64_t handle)
{
    return vxt_msg_open_com(name, permissions);
}

/*
 * 
 * vxt_msg_open_close
 *
 * OS dependent wrapper for object/file
 * close support
 *
 */

static inline void
vxt_msg_close_com(vxtctrlr_handle_t fd)
{
	vxtdev_close(fd);
}


/*
 *
 * vxt_malloc:
 *
 * OS independent wrapper for space/buffer
 * allocation 
 *
 */

static inline void *
vxt_malloc(unsigned int size)
{
	return malloc(size);
}


/*
 *
 * vxt_free:
 *
 * OS independent wrapper for space/buffer
 * release 
 *
 */

static inline void 
vxt_free(void *buffer)
{
    free(buffer);
    return;
}

#endif  /* VXT_MSG_OS_H */
