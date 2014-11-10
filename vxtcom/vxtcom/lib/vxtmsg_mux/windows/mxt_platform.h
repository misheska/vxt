#ifndef _PLATFORM_H
#define _PLATFORM_H
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <winsock2.h>
#include <signal.h>
#include <WS2tcpip.h>

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
//typedef unsigned long ULONG_PTR, *PULONG_PTR;
typedef uint32_t evtchn_port_t;
typedef HANDLE vxtsig_handle_t;

#include <assert.h>
//#include <public/vxt_defs.h>
#include <public\vxt_system.h>
#include <public\vxt_msg_export.h>
#include <public\vxt_socket.h>
#include <public\symcom_dev_table.h>
#include <public\vxt_system.h>
#include <public\vxt_com.h>
#include <public\vxt_msg_dev.h>

#define EWOULDBLOCK WSAEWOULDBLOCK

#define STDCALL __stdcall
/* Include header files for windows here */
#define THREAD_T HANDLE

#define MUTEX_OBJ	HANDLE

#define MUTEX_INITIALIZER	NULL

#define SOCKET		SOCKET

/*
 * These defines are used for loading xs.dll from xentools install dircetor.
 */

#define HKLM                      HKEY_LOCAL_MACHINE
#define XENPATH TEXT("SOFTWARE\\Citrix\\XenTools")
#define XENPATH_ALT_64 TEXT("SOFTWARE\\Wow6432Node\\Citrix\\XenTools")
#define XENPATH_ALT TEXT("SOFTWARE\\XenSource")
#define INSTALL_KEY TEXT("Install_dir") 

#define VXVIPATH TEXT("SOFTWARE\\Symantec\\VxVI\\xprtl")
#define VXVI_INSTAL_KEY TEXT("InstallDir")
#define MUX_STDUUID_SIZE      ((16 * 2) + 6)

struct pollfd { \
        SOCKET            fd;           \
        short           events;           \
        short           revents;         \
};

#define POLLFD   struct pollfd

#define MUTEX_INIT(mutex_obj, ret)				\
    mutex_obj = CreateMutex(                            \
                                        NULL,                             \
                                        FALSE,                           \
                                        NULL);                           \
	if (mutex_obj  == NULL) {	\
        ret = GetLastError();                              \
		printf("Unable to initialize mutex \n");	\
		ret = -1;					\
	}							\
	ret = 0;

#define  POLLFD   struct pollfd

#define MUTEX_DESTROY(mutex_obj)	 CloseHandle(mutex_obj)

#define MUTEX_LOCK(mutex_obj)	WaitForSingleObject(mutex_obj, INFINITE)
#define MUTEX_UNLOCK(mutex_obj)	ReleaseMutex(mutex_obj)


#define THREAD_CREATE(thr_obj, thr_function, thr_args, ret, threadid)	\
    thr_obj = CreateThread(NULL,      \
                                               0 ,             \
                                               (LPTHREAD_START_ROUTINE)&thr_function, \
                                                thr_args, \
                                                0, \
                                                (DWORD*)&threadid); \
     if(thr_obj == NULL) {  \
            ret = GetLastError(); \
            printf("Unable to create the thread :ERROR \n", ret);		\
            ret = -1; \
     }						\
	ret = 0;

#define XS_HANDLE	HANDLE

/*
 * This is defined as NULL as there is no xs_transaction in windows. Need to consult
 * XDC fols.
 */

#define XS_TRANSACTION	BOOL

#define XS_TRANSACTION_START(handle) \
    xs_transaction_start(handle)

#define THREAD_T	HANDLE


#define	RETRYABLE_ERROR(ret)				\
	if (ret == SOCKET_ERROR) {					\
		/*					\
		 * If it's a retryable error, retry	\
		 */					\
         ret = WSAGetLastError();     \
		if ((ret == WSAENOBUFS) ||		\
		    (ret == WSAEINTR) ||		\
		    (ret == WSAEINPROGRESS) ||			\
		    (ret == WSAEMSGSIZE) ||		\
            (ret == WSAEWOULDBLOCK) || \
		    (errno == WSAEMSGSIZE)) {		\
			ret = 1;			\
		} else {				\
			ret = 0;			\
		}					\
        } else {					\
		ret = 0;				\
	};

#define SLEEP(time)	Sleep(time * 1000);
extern int init_wsa();
#define INIT_WSA()  init_wsa()
//#define INIT_WSA()  init_wsa()
//#define IS_DOM0()  0

#define IS_DOM0(ret) 							\
	{								\
		ret = 0;						\
	}
   
//#define FD_OBJ			fd
#define	INVALID_HANDLE	 INVALID_SOCKET

#define CLOSE_SOCKET(fd)	closesocket(fd);

#define SETSOCKOPT(socketfd)			\
	setsockopt(socketfd, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));

#define POLLIN 0

#define	EXECUTE_COMMAND(dom0cmd, ptr)	

struct pthread_cond_t{ \
    unsigned    waiters_count; \
    HANDLE      mutex_obj; \
    HANDLE      events[2]; \
};

#define COND_OBJ	struct pthread_cond_t 

#define	COND_VAR_INIT(cond_obj, ret)    \
         cond_obj.mutex_obj = NULL; \
         cond_obj.waiters_count = 0; \
         ret = 0; \
        cond_obj.events[0] = CreateEvent(NULL, \
                                         FALSE, \
                                         FALSE, \
                                         NULL);\
       cond_obj.events[1] = CreateEvent(NULL, \
                                        TRUE, \
                                        FALSE, \
                                        NULL);\
       if((cond_obj.events[0]  == NULL)  || \
           (cond_obj.events[1]  == NULL)) {   \
            ret = -1;  \
       } 


extern pthread_cond_wait(COND_OBJ *cond_obj, MUTEX_OBJ mutex_obj);
extern pthread_cond_signal(COND_OBJ *cond_obj, MUTEX_OBJ mutex_obj);
extern char *mux_get_my_uuid();

#define COND_VAR_WAIT(cond_obj , mutex_obj)	\
 pthread_cond_wait(&cond_obj, mutex_obj)
	
#define COND_VAR_SIGNAL(cond_obj, mutex_obj)			\
	pthread_cond_signal(&cond_obj, mutex_obj)	

#define XS_READ(handle, tx, path, len) \
	xs_read(handle,  path, &len);

#ifdef VXT_ENDPOINT
#define	VXT_SEND(fd, buf, len, flags)			\
	vxt_send(fd, buf, &len, flags)
#define	VXT_RECV(fd, buf, len, flags)			\
	vxt_recv(fd, buf, &len, flags)
#define VXT_CLOSE(fd, flags)		\
	vxt_close(fd, flags);

#define VXT_SOCKET()					\
	vxt_socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM)

#define VXT_POLLFD_STRUCT vxt_poll_obj_t
#define FD_OBJ			object

#define VXT_POLL(mux_vxt_pollfd, mux_vxt_pollfd_size, POLL_TIMEOUT) \
	vxt_poll(mux_vxt_pollfd, mux_vxt_pollfd_size, POLL_TIMEOUT)

#endif /* VXT_ENDPOINT */

#ifdef NETWORK_ENDPOINT
#define	VXT_SEND(fd, buf, len, flags)			\
	send(fd, buf, len, flags)
#define	VXT_RECV(fd, buf, len, flags)			\
	recv(fd, buf, len, flags)

#define VXT_CLOSE(fd)					\
	CLOSE_SOCKET(fd);

#define VXT_SOCKET()					\
	socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM)

#define VXT_POLLFD_STRUCT	POLLFD
#define FD_OBJ			fd

#define VXT_POLL(mux_vxt_pollfd, mux_vxt_pollfd_size, POLL_TIMEOUT) \
	poll(mux_vxt_pollfd, mux_vxt_pollfd_size, POLL_TIMEOUT)

#endif /* NETWORK_ENDPOINT */

#define POLLVXT       0x2000    //TODO KM WHERE THESE constants defined.
#define POLLVXT_DEV   0x4000
#define POLLVXT_CTRLR 0x8000
#define POLLVXT 0x2000
#define POLLVXT_DEV   0x4000
#define POLLVXT_CTRLR 0x8000

#define MAX_CONNECTIONS    255
#define SOCKNAME mxtport

//#define SOCKNAME "C:\\Program Files\\Symantec\\VxVI\\xprtl\\bin\\mxtport"
#define SHUTDOWN(fd, flag) shutdown(fd, flag);
/*
 * For windows, we need to get path name for the path where xprtl is installed.
 * It is obtained from the Registery.
 */

WCHAR mxtport[MAX_PATH];
#define GET_SOCKNAME    GetVxVIPath
extern int GetVxVIPath();

#define MAX_THREADS          (MAX_CONNECTIONS + 10)
THREAD_T                    mux_threads_array[MAX_THREADS];
int                         mux_threads_count;
#define GET_THREAD_ID()     NULL
#define SNPRINTF  _snprintf

/*
 * Function pointers definition for xs.dll library.
 */
typedef (*PXS_DOMAIN_OPEN)();
typedef void   (*PXS_DOMAIN_CLOSE)(HANDLE);
typedef (*PXS_TRANSACTION_START)(HANDLE);
typedef void * (*PXS_READ)(HANDLE, char*, size_t);

#define GET_SOCKET_ERROR()	WSAGetLastError()

#define MUX_ACCEPT_TESTFILENAME "C:\\Program Files\\Symantec\\VxVI\\xprtl\\bin\\mxt_accept_test"
#define MUX_CONNECT_TESTFILENAME "C:\\Program Files\\Symantec\\VxVI\\xprtl\\bin\\mxt_connect_test"

#define STRUCT struct
#define WSACLEANUP	WSACleanup()
#define THREAD_KILL(threadid)
#define ENOTSOCK WSAENOTSOCK
#endif // PLATFORM_H
