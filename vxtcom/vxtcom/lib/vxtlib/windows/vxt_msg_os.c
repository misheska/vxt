#define _VXT_COMPONENT_ 4
#define _VXT_SUBSYSTEM_ 8

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

#include <vxt_lib_os.h>
#include <vxt_system.h>
#include <vxt_msg_export.h>
#include <vxt_msg_os.h>

int errno; //needed because our build env does not include the lib which declares this var

#define OBJ_CASE_INSENSITIVE    0x00000040L
#define FILE_SYNCHRONOUS_IO_ALERT               0x00000010

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;        // Points to type SECURITY_DESCRIPTOR
    PVOID SecurityQualityOfService;  // Points to type SECURITY_QUALITY_OF_SERVICE
} OBJECT_ATTRIBUTES;
typedef OBJECT_ATTRIBUTES *POBJECT_ATTRIBUTES;

#define InitializeObjectAttributes( p, n, a, r, s ) { \
    (p)->Length = sizeof( OBJECT_ATTRIBUTES );          \
    (p)->RootDirectory = r;                             \
    (p)->Attributes = a;                                \
    (p)->ObjectName = n;                                \
    (p)->SecurityDescriptor = s;                        \
    (p)->SecurityQualityOfService = NULL;               \
    }

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };

    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

NTSYSCALLAPI
NTSTATUS
NTAPI
NtOpenFile(
    OUT PHANDLE FileHandle,
    IN ACCESS_MASK DesiredAccess,
    IN POBJECT_ATTRIBUTES ObjectAttributes,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN ULONG ShareAccess,
    IN ULONG OpenOptions
    );

NTSYSCALLAPI
NTSTATUS
NTAPI
NtClose(
    IN HANDLE Handle
    );


/*
 * VXTPRINT will support debug levels in future through VXT_DEBUG_LEVEL
 */

char dbgfuncname[64]={0};
int VXT_DEBUG_LEVEL= VXTPRINT_PRODUCT_LEVEL;
int umi_error_level;



int
vxtdev_ioctl(vxtctrlr_handle_t fh, int cmd, VOID *arg)
{
    int res;
    ulong retVal;
    vxt_iocarg_t vxtarg;

    vxtarg.privdata = fh->privdata;
    vxtarg.arg = arg;
    
    res = DeviceIoControl(fh->handle,
                             VXT_IOCTL_CODE(cmd),
                             (VOID *)&vxtarg,
                             sizeof(vxt_iocarg_t),
                             (VOID *)&vxtarg,
                             sizeof(vxt_iocarg_t),
                             &retVal,
                             NULL);
    return (res == 0);
}

vxtctrlr_handle_t
vxtdev_open(char * device, unsigned int permissions)
{
    HANDLE          filehandle;
    OBJECT_ATTRIBUTES objattrs;
    NTSTATUS        status;
    IO_STATUS_BLOCK iostatus;
    UNICODE_STRING  cwsname;
    wchar_t         wszname[1024];
    wchar_t         *wcp;
    wchar_t         wc;
    const char      *cp;
    vxtctrlr_handle_t vh;
    vxt_fd_struct_t vps;
    vh = malloc(sizeof(*vh));    

    /*
     * form a Unicode pathname.  If the object directory is not
     * given (the name does not begin with / or \) then prefix
     * with the raw volume object directory.
     */
    if (device[0] != '/' && device[0] != '\\') {
        wcscpy(wszname, L"\\Device\\Vxt\\");
            wcp = wszname + wcslen(wszname);
    } else {
            wcp = wszname;
    }
    for (cp = device; (wc = (wchar_t)(uchar_t)*cp) != 0; cp++) {
            if (wc == L'/') {
                wc = L'\\';
            }
            *wcp++ = wc;
    }
    *wcp = 0;
    RtlInitUnicodeString(&cwsname, wszname);

    /*
     * open the device.  If the open fails, then return NULL and
     * set volnt_devfile_errno to indicate the error
     */
    InitializeObjectAttributes(&objattrs, &cwsname, OBJ_CASE_INSENSITIVE,
                                  NULL, NULL);
    status = NtOpenFile(&(vh->handle),
                        SYNCHRONIZE | FILE_READ_DATA | FILE_WRITE_DATA,
                        &objattrs, &iostatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (status != STATUS_SUCCESS) {
            return VXT_INVALID_DEV_OBJ;
    }
    if (iostatus.Status != STATUS_SUCCESS || vh->handle == NULL) {
        if (vh->handle) {
            NtClose(vh->handle);
        }
        free(vh);
        return VXT_INVALID_DEV_OBJ;
    }


    vh->privdata = vh->vde = NULL;
	if(vxtdev_ioctl(vh, IOCTL_FD_OPEN, &vps)){
        NtClose(vh->handle);
        free(vh);         
        return VXT_INVALID_DEV_OBJ;
    }
    vh->privdata = vps.private_data;
    vh->vde = vps.vde;
    return vh;
}


void 
vxtdev_close(vxtctrlr_handle_t vh)
{

    vxt_fd_struct_t vps;

    if(vh->handle){
        vps.private_data = vh->privdata;
        vps.vde = vh->vde;
        vxtdev_ioctl(vh, IOCTL_FD_CLOSE, &vps);
    }
    NtClose(vh->handle);
    free(vh);
}

/*
 * vxt_mmap:
 *
 * This routine is provided as an os independent wrapper for the
 * insertion of vxt device memory a.k.a vxt shared memory into
 * the callers address space.  It may be implemented as an
 * mmap call where the O.S. supports the driver mmap mechanism
 * it may be implemented as a synchronous call into the kernel
 * if this is more appropriate.
 *
 */

void *
vxt_mmap(void *starting_addr, uint32_t len, 
         int prot, int flags, vxtctrlr_handle_t fd, int64_t offset)
{
    vxt_mmap_struct_t mmap_request;

    mmap_request.size = len;
    mmap_request.access = prot;;
    mmap_request.share = flags;
    mmap_request.token = offset;
    mmap_request.process = GetCurrentProcess();
    vxtdev_ioctl(fd, IOCTL_VXTCTRLR_MMAP, &mmap_request);
    return mmap_request.VAPointer;
}

/*
 * vxt_munmap:
 *
 * This routine is provided as an os independent wrapper for the
 * unmapping of VXT com shared memory space.  The shared memory
 * space may be ordinary ram or physical device memory.  It is 
 * provided by a messaging device through the abstraction of a
 * memory token.  The token is used on the mapping side of the
 * call.  The map routine associates the user address with
 * the underlying shared memory resource and the unmap call
 * takes the starting address of the region the shared memory
 * is found at.  This is in line with the semantics of the
 * linux/unix mmap capability.  It is also sufficient for systems
 * where there is no explicit built-in capaiblity and an
 * ioctl must be used.
 */

void
vxt_munmap(void *addr, vxtctrlr_handle_t fd, uint64_t token, uint32_t len)
{
	vxt_munmap_struct_t munmap_request; 

    munmap_request.token = token;
    munmap_request.VAPointer = addr;
    munmap_request.process = GetCurrentProcess();
    vxtdev_ioctl(fd, IOCTL_VXTCTRLR_MUNMAP, &munmap_request);
}

int poll(vxt_pollfd_t *fds, nfds_t nfd, int timeout)
{
	vxt_poll_struct_t *poll_request; 
    uint32_t i;
    int res;
    int events = 0;

    if (nfd <= 0){
        errno = EBADF;
        return -1;
    }
    errno = VXT_SUCCESS;
    poll_request = (vxt_poll_struct_t *)malloc(
        sizeof(vxt_poll_struct_t)+(nfd-1)*sizeof(vxt_pollfd_flat_t));
    if(!poll_request){
        errno = VXT_NOMEM;
        return -1;
    }
    poll_request->nfd = nfd;
    poll_request->timeout = timeout;
    for(i=0;i<nfd;i++){
        poll_request->fds[i].handle = fds[i].vfh->handle;
        poll_request->fds[i].privdata = fds[i].vfh->privdata;
        poll_request->fds[i].vde = fds[i].vfh->vde;
        poll_request->fds[i].events = fds[i].events;
        poll_request->fds[i].revents = fds[i].revents;
    }    
    errno = vxtdev_ioctl(fds[0].vfh, IOCTL_VXTCTRLR_POLL, poll_request);
    if (errno){
        free(poll_request);
        return -1;
    }
// TODO-LOW use any fd to call since they all open vxt
//TODO-NORMAL need to handle return error code properly from IOCTL    
    for(i=0;i<nfd;i++){
        fds[i].revents = poll_request->fds[i].revents;
        if(fds[i].revents){
            events++;
        }
    } 
    free(poll_request);
    return events;
}
int
vxt_msg_event_wait(vxtctrlr_handle_t fd, int timeout, int *revent)
{
	vxt_pollfd_t fds[2];
	nfds_t nfds;

	fds[0].vfh = fd;
	fds[0].events = 0xff;
	fds[0].revents = 0;
	nfds = 1;
	if (poll(fds, nfds, timeout) <0 ) {
		if (errno == EBADF) {
			return (VXT_PARM);
		}
		if (errno == EFAULT) {
			return (VXT_PARM);
		}
		if (errno == EINTR) {
			return (VXT_ABORT);
		}
		if (errno == EINVAL) {
			return (VXT_PARM);
		}
		if (errno == ENOMEM) {
			return (VXT_NOMEM);
		}

	}
	*revent = fds[0].revents;
	return VXT_SUCCESS;
}


void *
vxt_malloc_poll_obj(int object_count)
{
	return vxt_malloc(sizeof(vxt_poll_obj_t) * object_count);
}

void
vxt_free_poll_obj(void *poll_object, int object_count)
{
	free(poll_object);
}

void
vxt_write_event_wait(void *poll_obj_handle, int count,
                     uint64_t object, uint32_t events, uint32_t revents)
{
	vxt_pollfd_t *poll_obj = &((vxt_pollfd_t *)poll_obj_handle)[count];
	poll_obj->vfh = (vxtctrlr_handle_t) object;
	poll_obj->events = (short) events;
	poll_obj->revents = (short) revents;
}


void
vxt_read_event_wait(void *poll_obj_handle, int count,
                    uint64_t *object, uint32_t *events, uint32_t *revents)
{
	vxt_pollfd_t *poll_obj = &((vxt_pollfd_t *)poll_obj_handle)[count];
	*object = (uint64_t)(poll_obj->vfh);
	*events = (uint32_t)poll_obj->events;
	*revents = (uint32_t)poll_obj->revents;
	
}

/*
 *
 * vxt_msg_cookie_check:
 *
 * This call is made on a vxt_msg_connect attempt.  It checks a field
 * at the end of the header to make certain that the device being connected
 * is indeed a vxt_msg device.  It also serves as a guarantor of queue
 * initialization.  A connecting guest should not read or write from a
 * device until the guest creating the device has written data and signaled
 * data available.  This protects against the race of connecting before
 * the queues are initialized.  Should this be violated, we may see a
 * transitory failure here.  To make the interface more robust in the face
 * of connecting guest transgression, we will wait for 1 second and try
 * again when a cookie mismatch occurs.
 *
 * Returns:
 *
 *	VXT_SUCCESS => Upon succesful cookie match
 *
 *	VXT_FAIL: ERRNO = EINVAL,  on cookie mismatch and timeout.
 *
 */

int
vxt_msg_cookie_check (char *cookie, char *id, uint32_t namelen)
{
        /*
         * Check the queue initialization status
         * We have a vxt_msg level cookie at the
         * end of the header which must match
         */
        if (strncmp(cookie, id, namelen)) {
                VXTPRINT(0, "waiting for send queue initialization\n");
                Sleep(1000);
                if (strncmp(cookie, id, namelen)) {
                        VXTPRINT(0, "timeout waiting for send queue "
                                 "initialization, connect failed\n");
			errno = EINVAL;
                        return VXT_FAIL;
                }
        }
	return VXT_SUCCESS;
}

