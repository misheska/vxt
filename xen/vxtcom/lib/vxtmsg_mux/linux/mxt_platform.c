/* $Id: mxt_platform.c,v 1.23 2009/02/04 00:18:23 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/linux/mxt_platform.c,v $" */

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


#include <mxt_platform.h>
#include <mxt_common.h>

int mux_process_vxt_comm(SOCKET, int *);
int mux_process_local_comm(SOCKET);
extern void mux_close_vxt_sockets();
extern void mux_remove_from_threadlist();
extern THREAD_T processor3, processor1;
extern int poll_threads_exit;
extern void mux_remove_peer_comm_mapping(SOCKET);
extern char* mux_get_log_entry();
extern struct mux_peer_comm_mapping * mux_get_peer_comm_entry(SOCKET);
extern void mux_remove_fd_from_poll(SOCKET, int);


void
transport_poll(mux_vxt_pollfd, mux_vxt_pollfd_size, poll_timeout)
	VXT_POLLFD_STRUCT	*mux_vxt_pollfd;
	int			mux_vxt_pollfd_size;
	int			poll_timeout;
{
	int				ret;
	int				i;
	SOCKET				eventfd;
	int				returnval = 0;

	ret = VXT_POLL(mux_vxt_pollfd, mux_vxt_pollfd_size, poll_timeout);

	if (ret <= 0) {
		return;
	}
#ifdef NETWORK_ENDPOINT
	for (i = 0; i < mux_vxt_pollfd_size; i++) {
		if (mux_vxt_pollfd[i].revents != 0) {
			eventfd = mux_vxt_pollfd[i].FD_OBJ;
			mux_process_vxt_comm(eventfd, &returnval);
			mux_vxt_pollfd[i].revents = 0;
		}
	}
#endif /* NETWORK_ENDPOINT */

#ifdef VXT_ENDPOINT
	for (i = 0; i < mux_vxt_pollfd_size; i++) {
		if (mux_vxt_pollfd[i].revents != 0) {
			eventfd = mux_vxt_pollfd[i].FD_OBJ;
			if (mux_vxt_pollfd[i].revents & VXTSIG_HUP) {
				MUX_LOG_ENTRY1("transport_poll Received %x",
					mux_vxt_pollfd[i].revents);
				mux_remove_peer_comm_mapping(eventfd);
				SLEEP(1);
				continue;
			}

			returnval = 1;
			while (returnval == 1) {
				returnval = 0;
				mux_process_vxt_comm(eventfd, &returnval);
			}
			mux_vxt_pollfd[i].revents = 0;
		}
	}
#endif /* VXT_ENDPOINT */

	return;
};

void
transport_poll_local(pollfd_array, pollfd_array_size, poll_timeout)
	struct pollfd		*pollfd_array;
	int			pollfd_array_size;
	int			poll_timeout;
{
	int				ret;
	int				i;
	SOCKET				eventfd;

	ret = poll(pollfd_array, pollfd_array_size, poll_timeout);

	if (ret <= 0) {
		return;
	}
	for (i = 0; i < pollfd_array_size; i++) {
		if (pollfd_array[i].revents != 0) {
			eventfd = pollfd_array[i].fd;
			if (mux_process_local_comm(eventfd) == -5) {
				/*
				 * We need to get rid of this fd from the
				 * polling array
				 */
				pollfd_array[i].fd = 0;
			}
			pollfd_array[i].revents = 0;
		}
	}
	return;
};

void
mux_sock_nonblock(socketfd)
	SOCKET  socketfd;
{
	int     flags;

	flags = fcntl(socketfd, F_GETFL, 0);
	if (flags < 0) {
		MUX_LOG_ENTRY0("F_GETFL failed ");
		exit(1);
	}
	flags |= O_NONBLOCK;

	if(fcntl(socketfd, F_SETFL, flags) < 0) {
		MUX_LOG_ENTRY0("F_SETFL failed ");
		exit(1);
	}
};

int
mux_sock_blocking(socketfd)
	SOCKET  socketfd;
{
	int     flags;

	if((flags = fcntl(socketfd, F_GETFL, 0)) < 0) {
		MUX_LOG_ENTRY0("F_GETFL failed ");
		return -1;
	}

	flags &= ~O_NONBLOCK;

	if(fcntl(socketfd, F_SETFL, flags) < 0) {
		MUX_LOG_ENTRY0("F_SETFL failed ");
		return -1;
	}
	return 0;
};

int
is_running_in_dom0()
{
	int	fd;

	fd = open("/etc/xensource-inventory", O_RDONLY);
	if (fd > 0) {
		close(fd);
		return 1;
	} else {
		return 0;
	}
};


void mux_signal_handler(signum)
        int     signum;
{
	int		i;

	MUX_LOG_ENTRY1("Received signal %d ", signum);

#ifdef VXT_ENDPOINT
	/*
	 * Close all the vxt endpoints
	 */
	MUX_LOG_ENTRY0("Calling vxt socket close");
	mux_close_vxt_sockets();
	MUX_LOG_ENTRY0("finished vxt socket close");

#endif /* VXT_ENDPOINT */

	/*
	 * Kill all the known threads
	 */
	for (i = 0; i < MAX_THREADS; i++) {
		if (mux_threads_array[i]) {
			/*
			THREAD_KILL(mux_threads_array[i]);
			*/
			mux_remove_from_threadlist(mux_threads_array[i]);
		}
	}

	signal(signum, SIG_DFL);
	raise(signum);
}

void
mux_set_sig_handler()
{
        /*
         * Set the signal handler for all the threads
        if (signal (SIGHUP, mux_signal_handler) == SIG_IGN)
                signal (SIGHUP, SIG_IGN);
        if (signal (SIGTERM, mux_signal_handler) == SIG_IGN)
                signal (SIGTERM, SIG_IGN);
        if (signal (SIGKILL, mux_signal_handler) == SIG_IGN)
                signal (SIGKILL, SIG_IGN);
        if (signal (SIGTRAP, mux_signal_handler) == SIG_IGN)
                signal (SIGTRAP, SIG_IGN);
        if (signal (SIGABRT, mux_signal_handler) == SIG_IGN)
                signal (SIGABRT, SIG_IGN);
        if (signal (SIGQUIT, mux_signal_handler) == SIG_IGN)
                signal (SIGQUIT, SIG_IGN);
        if (signal (SIGBUS, mux_signal_handler) == SIG_IGN)
                signal (SIGBUS, SIG_IGN);
        if (signal (SIGINT, mux_signal_handler) == SIG_IGN)
                signal (SIGINT, SIG_IGN);
        if (signal (SIGSEGV, mux_signal_handler) == SIG_IGN)
                signal (SIGSEGV, SIG_IGN);
         */
}

char *
mux_convert_stduuid_to_byteuuid(uuid)
	char	*uuid;
{
	int	i,j;
	char	*output;
	char	*ptr;
	int	tmp1, tmp2;

	ptr = uuid;
	j = 0;
	output = malloc(MUX_UUID_SIZE);
	for (i = 0; i < 16;) {
		if (ptr[j] != '-') {
			if ((ptr[j+1] >= 0x30) && (ptr[j+1] <= 0x39)) {
				tmp1 = ptr[j+1] - 0x30;
			} else {
				tmp1 = ptr[j+1] - 0x57;
			}
			if ((ptr[j] >= 0x30) && (ptr[j] <= 0x39)) {
				tmp2 = ptr[j] - 0x30;
			} else {
				tmp2 = ptr[j] - 0x57;
			}
			output[i] = (char) (tmp2 << 4) | tmp1;
			j += 2;
			i++;
		} else {
			j++;
		}
	}

	return output;
}

char *
mux_convert_byteuuid_to_stduuid(uuid)
	char	*uuid;
{
	int	i,j;
	char	*output;
	char	*ptr;
	char	first, second;

	ptr = uuid;
	j = 0;
	output = malloc(MUX_STDUUID_SIZE);
	memset(output, '\0', MUX_STDUUID_SIZE);
	for (i = 0; i < 16; i++) {
		first = (ptr[i] & 0xf0) >> 4;
		second = ptr[i] & 0x0f;

		if ((first >= 0x0) && (first <= 0x9)) {
			output[j++] = first + 0x30;
		} else {
			output[j++] = first + 0x57;
		}
		if ((second >= 0x0) && (second <= 0x9)) {
			output[j++] = second + 0x30;
		} else {
			output[j++] = second + 0x57;
		}

		if ((j == 8) || (j == 13) || (j == 18) ||
		    (j == 23)) {
			output[j++] = '-';
		}
	}

	return output;
}

char *
mux_get_my_uuid()
{
	char *dom0cmd = "/bin/awk -F'=' '{ "
			"if ($1 == \"INSTALLATION_UUID\") {print $2} }'"
			" /etc/xensource-inventory | sed \"s/'//g\"";
	char 		buf[BUFSIZ], *ptr1;
	FILE		*ptr;
	char		*output;
	size_t		len;
	int		uuid_size_in_ascii = 36;
	int		ret;
	int		authdev;
	vxt_db_local_uuid_arg_t	uuid_arg;

	/*
	 * If the open for the following file succeeds, we can
	 * assume that it's Dom0.
	 */
	memset(buf, '\0', sizeof(buf));
	ret = is_running_in_dom0();
	if (ret == 1) {
retry1:
		EXECUTE_COMMAND(dom0cmd, ptr, buf, ret);
		if (ret == 1) {
			MUX_LOG_ENTRY0("Command failed ");
			memset(buf, '\0', sizeof(buf));
			SLEEP(1);
			ret = 0;
			goto retry1;
		}
		ptr1 = buf;
	} else {
retry2:
		authdev = open("/dev/vxtdb-auth", O_RDWR);
		if (authdev < 0) {
			MUX_LOG_ENTRY0("Unable to open /dev/vxtdb-auth");
			SLEEP(1);
			goto retry2;
		}
		memset((void *)&uuid_arg, '\0',
			sizeof(vxt_db_local_uuid_arg_t));
		if (ioctl(authdev, VXT_AUTH_GET_LOCAL_UUID,
		    (void *)(long)&uuid_arg) < 0) {
			close(authdev);
			SLEEP(1);
			goto retry2;
		}
		ptr1 = uuid_arg.uuid;
		ptr1[uuid_size_in_ascii] = '\0';
	}

	output = mux_convert_stduuid_to_byteuuid(ptr1);
	return output;
}

int
mux_get_conn_cookie()
{
	int	num, fd, ret;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd <= 0) {
		MUX_LOG_ENTRY0("Failed to open /dev/urandom");
		return 0;
	}

	ret = read(fd, (char *)&num, sizeof(num));
	close(fd);
	if (ret != sizeof(num)) {
		return 0;
	}
	return num;
}
