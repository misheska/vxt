/* $Id: mxt_platform.h,v 1.20 2009/02/04 00:18:23 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/linux/mxt_platform.h,v $" */

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
#include <netdb.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <pthread.h>

#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <poll.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/ioctl.h>


#include <public/vxt_system.h>
#include <public/vxt_msg_export.h>
#include <public/symcom_dev_table.h>
#include <public/vxt_socket.h>
#include <public/vxt_com.h>
#include <public/vxt_msg_dev.h>
#include <public/vxt_auth.h>

#define MUTEX_OBJ	pthread_mutex_t

#define MUTEX_INITIALIZER	PTHREAD_MUTEX_INITIALIZER

#ifndef SOCKET
#define SOCKET		int
#endif


#define MUTEX_INIT(mutex_obj, ret)				\
	if (pthread_mutex_init(&mutex_obj, NULL) != 0) {	\
		MUX_LOG_ENTRY0("Unable to initialize mutex ");\
		ret = -1;					\
	}							\
	ret = 0;

#define MUTEX_DESTROY(mutex_obj)	pthread_mutex_destroy(&mutex_obj)

#define MUTEX_LOCK(mutex_obj)	pthread_mutex_lock(&mutex_obj)

#define MUTEX_UNLOCK(mutex_obj)	pthread_mutex_unlock(&mutex_obj)

#define THREAD_CREATE(thr_obj, thr_function, thr_args, ret, threadid)	\
	if (pthread_create(&thr_obj, NULL,				\
	    (void *) &thr_function, (void *)thr_args) != 0) {		\
		MUX_LOG_ENTRY0("Unable to create the thread ");	\
		ret = -1;						\
	}								\
	ret = 0;

#define XS_HANDLE	struct xs_handle *

#define XS_TRANSACTION	xs_transaction_t

#define THREAD_T	pthread_t

#define	RETRYABLE_ERROR(ret)				\
	if (ret < 0) {					\
		/*					\
		 * If it's a retryable error, retry	\
		 */					\
		if ((errno == EAGAIN) ||		\
		    (errno == EWOULDBLOCK) ||		\
		    (errno == EINTR) ||			\
		    (errno == EMSGSIZE) ||		\
		    (errno == ENOBUFS)) {		\
			ret = 1;			\
		} else {				\
			ret = 0;			\
		}					\
        } else {					\
		ret = 0;				\
	};

#define SLEEP(time)	sleep(time);




#define	INIT_WSA()	{};

#define	INVALID_HANDLE	-1

#define	SOCKET_ERROR	-1

#define CLOSE_SOCKET(fd)		\
		close(fd);

#define SETSOCKOPT(socketfd)			\
	setsockopt(socketfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

#define	EXECUTE_COMMAND(dom0cmd, ptr, buf, ret)			\
	if ((ptr = popen(dom0cmd, "r")) != NULL) {	\
		fgets(buf, BUFSIZ, ptr);		\
		pclose(ptr);				\
		ret = 0;				\
	} else {					\
		ret = 1;				\
	}
	

#define COND_OBJ	pthread_cond_t

#define	COND_VAR_INIT(cond_obj, ret)				\
	if (pthread_cond_init(&cond_obj, NULL) != 0) {		\
		MUX_LOG_ENTRY0("Unable to initialize cv ");	\
		ret = -1;					\
	}							\
	ret = 0;

#define COND_VAR_WAIT(cond_obj, mutex_obj)		\
	pthread_cond_wait(&cond_obj, &mutex_obj);

#define COND_VAR_SIGNAL(cond_obj, mutex_obj)			\
	pthread_cond_signal(&cond_obj);

#define XS_READ(handle, tx, path, len) \
	xs_read(handle, tx, path, &len);

#define XS_TRANSACTION_START(handle) \
	xs_transaction_start(handle)

#ifdef VXT_ENDPOINT
#define	VXT_SEND(fd, buf, len, flags)			\
	vxt_send(fd, buf, &len, flags)
#define	VXT_RECV(fd, buf, len, flags)			\
	vxt_recv(fd, buf, &len, flags)
#define VXT_CLOSE(fd, flags)		\
	vxt_close(fd, flags);

#define VXT_SOCKET()					\
	vxt_socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM)

#define VXT_POLLFD_STRUCT	vxt_poll_obj_t
#define FD_OBJ			object

#define VXT_POLL(mux_vxt_pollfd, mux_vxt_pollfd_size, POLL_TIMEOUT) \
	vxt_poll(mux_vxt_pollfd, mux_vxt_pollfd_size, POLL_TIMEOUT)

#endif /* VXT_ENDPOINT */

#ifdef NETWORK_ENDPOINT
#define	VXT_SEND(fd, buf, len, flags)			\
	send(fd, buf, len, flags)
#define	VXT_RECV(fd, buf, len, flags)			\
	recv(fd, buf, len, flags)

#define VXT_CLOSE(fd, flags)					\
	CLOSE_SOCKET(fd);

#define VXT_SOCKET()					\
	socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM)

#define VXT_POLLFD_STRUCT	struct pollfd
#define FD_OBJ			fd

#define VXT_POLL(mux_vxt_pollfd, mux_vxt_pollfd_size, POLL_TIMEOUT) \
	poll(mux_vxt_pollfd, mux_vxt_pollfd_size, POLL_TIMEOUT)

#endif /* NETWORK_ENDPOINT */

#define GET_THREAD_ID()		\
	pthread_self()

#define THREAD_KILL(threadid)		\
	pthread_kill(threadid, SIGKILL)

#define MAX_CONNECTIONS	255
#define MAX_THREADS	(MAX_CONNECTIONS + 10)
THREAD_T		mux_threads_array[MAX_THREADS];
int			mux_threads_count;
#define SNPRINTF	snprintf

#define SOCKNAME        "/etc/mxtport"

#define MUX_ACCEPT_TESTFILENAME	"/etc/vx/mxt_accept_test"
#define MUX_CONNECT_TESTFILENAME "/etc/vx/mxt_connect_test"

#define SHUTDOWN(fd, flag)	\
	shutdown(fd, flag)

#define STDCALL

#define GET_SOCKNAME()

#define GET_SOCKET_ERROR()	errno

#define MUX_STDUUID_SIZE      ((16 * 2) + 6)

#define STRUCT

#define WSACLEANUP
