/* $Id: mxt.c,v 1.41 2009/07/15 19:23:51 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/common/mxt.c,v $" */

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

#include "mxt.h"

int
mux_msg_send(fd, buf, len, flags)
	SOCKET	fd;
	char	*buf;
	int	len;
	int	flags;
{
	int	ret = 0, ret1;

retry:
	ret = send(fd, buf, len, flags);
	ret1 = ret;
	if (ret < 0) {
		RETRYABLE_ERROR(ret);
		if (ret == 1) {
			SLEEP(1);
			goto retry;
		}
		return ret1;
	}
	return ret;
}

int
mux_msg_recv(fd, buf, len, flags)
	SOCKET	fd;
	char	*buf;
	int	len;
	int	flags;
{
	int	ret = 0;

	ret = recv(fd, buf, len, flags);
	return ret;
}

int
mux_send_msg_over_mxt(fd, buf, len, flags, unpacked_msg_hdr)
	SOCKET			fd;
	char			*buf;
	int			len;
	int			flags;
	struct mux_msg_hdr	*unpacked_msg_hdr;
{
	int			ret;
	struct mux_peer_comm_mapping	*entry = NULL;
	char			*tmpbuf = buf;
	int			len1 = len;


	entry = mux_get_peer_comm_entry(fd);
	if (!entry) {
		return -1;
	}

	MUTEX_LOCK(entry->mux_peer_comm_send_mutex);

	unpacked_msg_hdr->mux_msg_seqno = entry->mux_peer_comm_send_seqno++;

	mux_msg_hdr_pack((*unpacked_msg_hdr), tmpbuf);

	if (entry->mux_peer_comm_is_vxt_ep) {
#ifdef VXT_ENDPOINT
retry:
		ret = VXT_SEND((int)fd, buf, len1, VXT_SOCK_SIGNAL | \
			VXT_SOCK_WAIT);

		/*
		 * TODO: vxt_send is supposed to block, but sometimes it
		 * returns -1 for unavailability of space which needs to
		 * be ignored and send should be retried
		 */
		if (ret < 0) {
			if ((errno == ENOTSOCK) || (errno == EPIPE) ||
			    (errno == EXDEV)) {
				MUX_LOG_ENTRY2("Data send failed ret : %d"
					" errno : %d", ret, errno);
				MUTEX_UNLOCK(entry->mux_peer_comm_send_mutex);
				release_fd_refcnt(fd);
				return ret;
			} else {
				MUX_LOG_ENTRY2("ret : %d errno : %d ", ret,
					errno);
				SLEEP(1);
				goto retry;
			}
		}
#endif /* VXT_ENDPOINT */
	} else {
		ret = mux_msg_send(fd, buf, len1, flags);
	}


	if (ret > 0) {
		MUX_LOG_ENTRY2("mxt_send_msg_over_mxt: size : %x "
			"Sent Seq no : %llx", ret,
			unpacked_msg_hdr->mux_msg_seqno);
		MUX_LOG_ENTRY2("Send fd : %d Recv fd : %d",
			unpacked_msg_hdr->mux_msg_send_fd,
			unpacked_msg_hdr->mux_msg_recv_fd);
			
		assert(ret == len);
	} else {
		MUX_LOG_ENTRY1("Data send failed ret : %x", ret);
		assert(0);
	}
	MUTEX_UNLOCK(entry->mux_peer_comm_send_mutex);
	release_fd_refcnt(fd);
	return ret;
}

int
mux_recv_msg_over_mxt(fd, buf, len, flags)
	SOCKET	fd;
	char	*buf;
	size_t	len;
	int	flags;
{
	struct mux_msg_hdr	hdr;
	int			ret = 0;
	struct mux_peer_comm_mapping	*entry = NULL;
	char			*tmpbuf;
	size_t			len1 = len;

	entry = mux_get_peer_comm_entry(fd);
	if (!entry) {
		return -1;
	}

	MUTEX_LOCK(entry->mux_peer_comm_recv_mutex);
	if (entry->mux_peer_comm_is_vxt_ep) {
#ifdef VXT_ENDPOINT
		ret = VXT_RECV((int)fd, buf, len1, 0);
#endif /* VXT_ENDPOINT */
	} else {
		ret = recv(fd, buf, len1, flags);
	}
	
	if (ret > 0) {
		assert(ret == mux_msg_hdr_pack_sz(hdr));
		tmpbuf = buf;
		mux_msg_hdr_unpack(hdr, tmpbuf);

		MUX_LOG_ENTRY2("mxt_recv_msg_over_mxt: size : %x "
			"Rcvd Seq no : %llx", ret, hdr.mux_msg_seqno);

		if (hdr.mux_msg_seqno != entry->mux_peer_comm_recv_seqno) {
			MUX_LOG_ENTRY2("mux_recv_msg_over_mxt: "
				"Invalid recv seqno "
				"Actual : %llx Expected : %llx",
				hdr.mux_msg_seqno,
				entry->mux_peer_comm_recv_seqno);
		}
		assert(hdr.mux_msg_seqno == entry->mux_peer_comm_recv_seqno);
		entry->mux_peer_comm_recv_seqno++;
		MUX_LOG_ENTRY2("Send fd : %d Recv fd : %d",
			hdr.mux_msg_send_fd, hdr.mux_msg_recv_fd);
	} else {
		MUX_LOG_ENTRY1("Receive failed ret : %d", ret);
	}
	MUTEX_UNLOCK(entry->mux_peer_comm_recv_mutex);

	release_fd_refcnt(fd);
	return ret;
}


int
mux_lib_init(config_ptr)
	struct mux_init_config	*config_ptr;
{
	int		ret, i;
	long long	threadid;
	char		*uuid;

	/*
	 * Initialize the vxt socket library, linking it
	 * to the vxt controller.
	 */
	memset(mux_threads_array, '\0', sizeof(mux_threads_array));
	ret = vxt_socket_lib_init();

	if (ret != VXT_SUCCESS) {
		MUX_LOG_ENTRY0("Failed to connect to the VXT controller");
		ret = -1;
		goto exit;
	}


	INIT_WSA();

	/*
	 * Allocate the debug array
	 */
	mux_debug_log = (char **)malloc(sizeof(char*) * MUX_DEBUG_ENTRIES);
	for (i = 0; i < MUX_DEBUG_ENTRIES; i++) {
		mux_debug_log[i] = (char *)malloc(MUX_DEBUG_MSG_SIZE);
		memset(mux_debug_log[i], '\0', MUX_DEBUG_MSG_SIZE);
	}

	/*
	 * If the open for the following file succeeds, we can
	 * assume that it's Dom0.
	 */
	ret = is_running_in_dom0();
	if (ret == 1) {
		is_dom0 = 1;
		MUX_LOG_ENTRY0("Running in Dom0 ");
	} else {
		is_dom0 = 0;
		MUX_LOG_ENTRY0("Running in DomU ");
	}

	/*
	 * Set the scheduler priority
	 */

	memset(mux_local_pollfd, '\0', sizeof(mux_local_pollfd));
	memset(mux_vxt_pollfd, '\0', sizeof(mux_vxt_pollfd));
	memset(mux_mappings, '\0', sizeof(mux_mappings));
	memset(mux_peer_comm_mappings, '\0', sizeof(mux_peer_comm_mappings));

	mux_set_sig_handler();

	MUTEX_INIT(mux_demux_array_mutex, ret);
	if (ret == -1) {
		goto exit;
	}
	MUTEX_INIT(mux_services_array_mutex, ret);
	if (ret == -1) {
		goto exit;
	}
	MUTEX_INIT(mux_peer_comm_mutex, ret);
	if (ret == -1) {
		goto exit;
	}
	MUTEX_INIT(mux_local_pollfd_mutex, ret);
	if (ret == -1) {
		goto exit;
	}
	MUTEX_INIT(mux_vxt_pollfd_mutex, ret);
	if (ret == -1) {
		goto exit;
	}
	MUTEX_INIT(mux_vxt_connections, ret);
	if (ret == -1) {
		goto exit;
	}
	MUTEX_INIT(mux_log_counter_mutex, ret);
	if (ret == -1) {
		goto exit;
	}

	domain_uuid = (char *) malloc(sizeof(char) * MUX_UUID_SIZE);
	if (domain_uuid == NULL) {
		ret = -1;
		goto exit;
	}
	memset(domain_uuid, '\0', MUX_UUID_SIZE);
	uuid = mux_get_my_uuid();
	memcpy(domain_uuid, uuid, MUX_UUID_SIZE);
	free(uuid);

	/*
	 * allocate memory for Dom0 UUID.
	 */
	dom0_uuid = (char *) malloc(sizeof(char) * MUX_UUID_SIZE);
	if (dom0_uuid == NULL) {
		ret = -1;
		goto exit;
	}
	memset(dom0_uuid, '\0', MUX_UUID_SIZE);

	threadid = 0L;
	/*
	 * Listens for connections from Xprtlc and Xprtld
	 */
	THREAD_CREATE(listener1, mux_listener, NULL, ret, threadid);
	if (ret == -1) {
		goto exit;
	}
	mux_add_to_threadlist(listener1);

#ifdef NETWORK_ENDPOINT
	/*
	 * Listens for MXT connections over network from other domains
	 */
	THREAD_CREATE(listener2, mux_network_listener, NULL, ret, threadid);
	if (ret == -1) {
		goto exit;
	}
	mux_add_to_threadlist(listener2);

#endif /* NETWORK_ENDPOINT */

#ifdef VXT_ENDPOINT
	/*
	 * Listens for MXT connections over VXT from other domains
	 */
	THREAD_CREATE(listener3, mux_vxt_listener, NULL, ret, threadid);
	if (ret == -1) {
		goto exit;
	}
	mux_add_to_threadlist(listener3);

#endif /* VXT_ENDPOINT */

	/*
	 * Polls on connected descriptors and processes the received data
	 */
	THREAD_CREATE(processor1, mux_local_comm_poll, NULL, ret, threadid);
	if (ret == -1) {
		goto exit;
	}
	mux_add_to_threadlist(processor1);

	THREAD_CREATE(processor3, mux_vxt_comm_poll, NULL, ret, threadid);
	if (ret == -1) {
		goto exit;
	}
	mux_add_to_threadlist(processor3);

#ifdef GC_DEF
	/*
	 * Misc thread : May be used to carry out some housekeeping later
	*/
	THREAD_CREATE(processor2, mux_garbage_collection, NULL, ret, threadid);
	if (ret == -1) {
		goto exit;
	}
	mux_add_to_threadlist(processor2);

#endif /* GC_DEF */

	return 0;

exit:
	/*
	 * Kill all the threads and free the allocated memory
	 */
	if (domain_uuid) {
		free(domain_uuid);
	}
	if (dom0_uuid) {
		free(dom0_uuid);
	}
	for (i = 0; i < MAX_THREADS; i++) {
		if (mux_threads_array[i] != 0) {
			THREAD_KILL(mux_threads_array[i]);
			mux_remove_from_threadlist(mux_threads_array[i]);
		}
	}

	return ret;
}

/*
 * Listener thread
 */

static void
mux_listener()
{
	SOCKET			listener_fd;
	struct sockaddr_in	server;
	SOCKET			newfd;
	struct mux_auth		auth;
	int			port;
	int			ret;
	socklen_t		size;
	FILE			*fp;
#ifdef NAMED_PIPE_DOM0
	struct sockaddr_un	sa;
#endif

#ifdef NAMED_PIPE_DOM0
	if (is_dom0) {
		unlink(SOCKNAME);
		bzero((void *)&sa, sizeof(sa));
		sa.sun_family = AF_UNIX;
		strncpy(sa.sun_path, SOCKNAME, strlen(SOCKNAME));
		listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);

		if (listener_fd == INVALID_HANDLE) {
			MUX_LOG_ENTRY0("mux_listener: socket call failed for "
				"unix domain socket");
			return;
		}

		if ((bind(listener_fd, (struct sockaddr *) &sa,
		     sizeof(struct sockaddr_un))) == INVALID_HANDLE) {
			MUX_LOG_ENTRY0("mux_listener: failed binding to unix"
				" domain socket");
			CLOSE_SOCKET(listener_fd);
			return;
		}
	} else {
#endif /* NAMED_PIPE_DOM0 */
		listener_fd = socket(PF_INET, SOCK_STREAM,
				IPPROTO_TCP);
		if(listener_fd == INVALID_HANDLE){
			MUX_LOG_ENTRY0("mux_listener: Failed creating "
				"local listener socket");
			return;
		}

		memset((char *) &server, '\0', sizeof(server));
		server = mux_getipa("localhost", 0);
		if (bind(listener_fd, (struct sockaddr*)&server,
		    sizeof(server)) == INVALID_HANDLE) {
			MUX_LOG_ENTRY1("mux_listener: Failed binding to Mxtd "
				" socket %d", MXTD_WELL_KNOWN_PORT);
			CLOSE_SOCKET(listener_fd);
			return;
		}

		memset((char *)&server, '\0', sizeof(server));
		size = sizeof(server);
		ret =
		getsockname(listener_fd, (struct sockaddr*) &server, &size);

		if (ret < 0) {
			MUX_LOG_ENTRY0("mux_connect: getsockname failed");
			return;
		}

		port = ntohs(server.sin_port);
		GET_SOCKNAME();
		fp = fopen(SOCKNAME, "w");
		if (fp == NULL) {
			MUX_LOG_ENTRY1("mux_connect: Unable to open %s",
				SOCKNAME);
			return;
		}
		ret = fprintf(fp, "%d", port);
		if (ret <= 0) {
			MUX_LOG_ENTRY1("mux_connect: Unable to write %s",
				SOCKNAME);
			return;
		}
		fclose(fp);
#ifdef NAMED_PIPE_DOM0
	}
#endif /* NAMED_PIPE_DOM0 */

	for(;;) {
retry:
		/*
		 * Do not accept any more local connections if we have
		 * reached the MAX_CONNECTIONS limit
		 */ 
		MUTEX_LOCK(mux_local_pollfd_mutex);
		if (mux_local_pollfd_size == MAX_CONNECTIONS) {
			MUTEX_UNLOCK(mux_local_pollfd_mutex);
			MUX_LOG_ENTRY0("Reached limit for the max number of"
				" connection");
			SLEEP(1);
			goto retry;
		}
		MUTEX_UNLOCK(mux_local_pollfd_mutex);

		if (listen(listener_fd, MAX_CONNECTIONS) == SOCKET_ERROR) {
			MUX_LOG_ENTRY0("mux_listener: Failed setting socket"
				" to listen");
			CLOSE_SOCKET(listener_fd);
			return;
		}

		newfd = accept(listener_fd, NULL, NULL);

		if (newfd == INVALID_HANDLE) {
			MUX_LOG_ENTRY0("mux_listener: Accept failed");
			continue;
		}

#ifdef NAMED_PIPE_DOM0
		if (!is_dom0) {
#endif
		if (mux_accept(newfd, NULL, &auth) < 0) {
			MUX_LOG_ENTRY0("Authorization failed");
			CLOSE_SOCKET(newfd);
			continue;
		}
#ifdef NAMED_PIPE_DOM0
		}
#endif
		/*
		 * Add the new file descriptor in the list of descriptors.
		 */
		mux_add_local_fd(newfd);
	}
}

static void
mux_update_mapping(sendfd, recvfd)
	SOCKET		sendfd;
	SOCKET		recvfd;
{
	int				index;
	struct mux_demux_mapping	*entry = NULL;

	MUTEX_LOCK(mux_demux_array_mutex);

	index = MUX_FD_HASH(sendfd, MAX_CONNECTIONS);
	entry = mux_mappings[index];

	while (entry) {
		if (entry->mux_demux_mapping_myside_fd == sendfd) {
			MUTEX_LOCK(entry->mux_demux_mapping_fd_mutex);
			entry->mux_demux_mapping_peerside_fd = recvfd;
			MUTEX_UNLOCK(entry->mux_demux_mapping_fd_mutex);
			break;
		}
		entry = entry->mux_demux_mapping_next;
	}

	MUTEX_UNLOCK(mux_demux_array_mutex);
}

/*
 * Adds the communication descriptor to the mux_peer_comm_mapping array
 */
void
mux_update_peer_comm_mapping(uuid, fd)
	char	*uuid;
	SOCKET	fd;
{
	int				i = 0;
	struct mux_peer_comm_mapping	*entry = NULL;

	MUTEX_LOCK(mux_peer_comm_mutex);
	i = MUX_FD_HASH(fd, MAX_NUMBER_OF_GUESTS);
	entry = mux_peer_comm_mappings[i];

	while (entry) {
		if (entry->mux_peer_comm_fd == fd) {
			break;
		}
		entry = entry->mux_peer_comm_mapping_next;
	}

	assert(entry);
	memcpy(entry->mux_peer_comm_uuid, uuid, MUX_UUID_SIZE);
	MUTEX_UNLOCK(mux_peer_comm_mutex);
	return;
}

int
mux_add_peer_comm_mapping(fd, is_vxt)
	SOCKET	fd;
	int	is_vxt;
{
	int				index, i, ret = 0;
	struct mux_peer_comm_mapping	*entry = NULL, *prev_entry = NULL;
	char				*ptr;

	MUTEX_LOCK(mux_peer_comm_mutex);
	index = MUX_FD_HASH(fd, MAX_NUMBER_OF_GUESTS);
	entry = mux_peer_comm_mappings[index];
	while (entry) {
		prev_entry = entry;
		entry = entry->mux_peer_comm_mapping_next;
	}

	entry = (struct mux_peer_comm_mapping *)
			malloc(sizeof(struct mux_peer_comm_mapping));
	memset(entry, '\0', sizeof(struct mux_peer_comm_mapping));
	entry->mux_peer_comm_mapping_next = NULL;

	/*
	 * Initialize send/recv buffers
	 */

	MUTEX_INIT(entry->mux_peer_comm_send_mutex, ret);
	if (ret == -1) {
		MUX_LOG_ENTRY0("mux_peer_comm_send_mutex,: Unable to "
			"initialize mux_peer_comm_send_mutex");
		goto exit;
	}

	MUTEX_INIT(entry->mux_peer_comm_recv_mutex, ret);
	if (ret == -1) {
		MUX_LOG_ENTRY0("mux_peer_comm_recv_mutex,: Unable to "
			"initialize mux_peer_comm_recv_mutex");
		goto exit;
	}

	MUTEX_INIT(entry->mux_peer_comm_my_recv_current_window_size_mutex, ret);
	if (ret == -1) {
		MUX_LOG_ENTRY0("mux_add_peer_comm_mapping: Unable to "
			"initialize "
			"mux_peer_comm_my_recv_current_window_size_mutex");
		goto exit;
	}
		
	entry->mux_peer_comm_send_buf = (struct mux_comm_buf *)
		malloc(sizeof(struct mux_comm_buf) * MAX_COMM_BUF_SIZE);
	if (! entry->mux_peer_comm_send_buf ) {
		MUX_LOG_ENTRY0("mux_add_peer_comm_mapping : Memory "
			"allocation failure");
		ret = -1;
		goto exit;
	}
	memset((void *)entry->mux_peer_comm_send_buf, '\0',
		sizeof(struct mux_comm_buf) * MAX_COMM_BUF_SIZE);

	/*
	 * Initialize the communication buffers
	 */
	for (i = 0; i < MAX_COMM_BUF_SIZE; i++) {
retry1:
		ptr = (char *) malloc(MAX_LEN * sizeof(char));
		if (ptr == NULL) {
			MUX_LOG_ENTRY0("Memory allocation failed");
			SLEEP(1);
			goto retry1;
		}
		entry->mux_peer_comm_send_buf->mux_comm_bufptr[i] = ptr;
	}

	MUTEX_INIT(entry->mux_peer_comm_send_buf->mux_comm_buf_mutex, ret);

	if (ret == -1) {
		MUX_LOG_ENTRY0("mux_add_peer_comm_mapping: Unable to "
			"initialize mux_comm_buf_mutex");
		ret = -1;
		goto exit;
	}

	entry->mux_peer_comm_recv_buf = (struct mux_comm_buf *)
		malloc(sizeof(struct mux_comm_buf) * MAX_COMM_BUF_SIZE);
	if (! entry->mux_peer_comm_recv_buf ) {
		MUX_LOG_ENTRY0("mux_add_peer_comm_mapping : Memory "
			"allocation failure");
		ret = -1;
		goto exit;
	}
	memset((void *)entry->mux_peer_comm_recv_buf, '\0',
		sizeof(struct mux_comm_buf) * MAX_COMM_BUF_SIZE);
	/*
	 * Initialize the communication buffers
	 */
	for (i = 0; i < MAX_COMM_BUF_SIZE; i++) {
retry2:
		ptr = (char *) malloc(MAX_LEN * sizeof(char));
		if (ptr == NULL) {
			MUX_LOG_ENTRY0("Memory allocation failed");
			SLEEP(1);
			goto retry2;
		}
		entry->mux_peer_comm_recv_buf->mux_comm_bufptr[i] = ptr;
	}

	MUTEX_INIT(entry->mux_peer_comm_recv_buf->mux_comm_buf_mutex, ret);

	if (ret == -1) {
		MUX_LOG_ENTRY0("mux_add_peer_comm_mapping: Unable to "
			"initialize mux_comm_buf_mutex");
		ret = -1;
		goto exit;
	}

	MUTEX_INIT(entry->mux_peer_comm_mutex, ret);

	if (ret == -1) {
		MUX_LOG_ENTRY0("mux_add_peer_comm_mapping: Unable to "
			"initialize mux_peer_comm_mutex");
		ret = -1;
		goto exit;
	}

	entry->mux_peer_comm_send_buf_index = 0;
	entry->mux_peer_comm_recv_buf_index = 0;
	entry->mux_peer_comm_send_seqno = 0L;
	entry->mux_peer_comm_recv_seqno = 0L;
	entry->mux_peer_comm_fd = fd;
	entry->mux_peer_comm_is_vxt_ep = is_vxt;
	entry->mux_peer_comm_peer_recv_current_window_size = MAX_COMM_BUF_SIZE;

	MUTEX_LOCK(mux_vxt_pollfd_mutex);
	mux_vxt_pollfd[mux_vxt_pollfd_size].FD_OBJ = fd;
	mux_vxt_pollfd[mux_vxt_pollfd_size].events = POLLIN;
	if (is_vxt) {
		mux_vxt_pollfd[mux_vxt_pollfd_size].events |=
			POLLVXT | VXTSIG_HUP | VXTSIG_IN;
	}
	mux_vxt_pollfd[mux_vxt_pollfd_size].revents = 0;
	mux_vxt_pollfd_size++;

	if (prev_entry) {
		prev_entry->mux_peer_comm_mapping_next = entry;
	} else {
		mux_peer_comm_mappings[index] = entry;
	}

	MUTEX_UNLOCK(mux_vxt_pollfd_mutex);
	MUTEX_UNLOCK(mux_peer_comm_mutex);
	return ret;

exit:
	MUTEX_UNLOCK(mux_peer_comm_mutex);

	/*
	 * Free up all the allocated buffers and initialized mutex objects
	 */
	MUTEX_DESTROY(entry->mux_peer_comm_send_mutex);
	MUTEX_DESTROY(entry->mux_peer_comm_recv_mutex);
	MUTEX_DESTROY(entry->mux_peer_comm_my_recv_current_window_size_mutex);
	for (i = 0; i < MAX_COMM_BUF_SIZE; i++) {
		free(entry->mux_peer_comm_send_buf->mux_comm_bufptr[i]);
		free(entry->mux_peer_comm_recv_buf->mux_comm_bufptr[i]);
	}
	MUTEX_DESTROY(entry->mux_peer_comm_send_buf->mux_comm_buf_mutex);
	MUTEX_DESTROY(entry->mux_peer_comm_recv_buf->mux_comm_buf_mutex);
	MUTEX_DESTROY(entry->mux_peer_comm_mutex);
	free(entry->mux_peer_comm_send_buf);
	free(entry->mux_peer_comm_recv_buf);
	free(entry);

	return ret;
}

/*
 * Removes the communication descriptor from the mux_peer_comm_mapping array
 */
void
mux_remove_peer_comm_mapping(fd)
	SOCKET	fd;
{
	int				i;
	struct mux_peer_comm_mapping	*entry = NULL, *prev_entry = NULL;
	struct mux_demux_mapping	*mux_entry;

	MUX_LOG_ENTRY1("Removing peer comm entry for fd %d", fd);
	MUTEX_LOCK(mux_peer_comm_mutex);
	i = MUX_FD_HASH(fd, MAX_NUMBER_OF_GUESTS);
	entry = mux_peer_comm_mappings[i];

	while (entry) {
		if (entry && entry->mux_peer_comm_fd == fd) {
			break;
		}
		prev_entry = entry;
		entry = entry->mux_peer_comm_mapping_next;
	}

	if (!entry) {
		/*
		 * We could land here when some other thread is already
		 * trying to close this peer comm fd
		 */
		MUTEX_UNLOCK(mux_peer_comm_mutex);
		return;
	}

	MUTEX_LOCK(entry->mux_peer_comm_mutex);
	if (entry->mux_peer_comm_fd_refcnt != 0) {
		entry->mux_peer_comm_flags |=
			MUX_PEER_COMM_MARKED_FOR_REMOVAL;
		MUTEX_UNLOCK(entry->mux_peer_comm_mutex);
		MUTEX_UNLOCK(mux_peer_comm_mutex);
		return;
	}

	if (prev_entry) {
		prev_entry->mux_peer_comm_mapping_next =
			entry->mux_peer_comm_mapping_next;
		entry->mux_peer_comm_mapping_next = NULL;
	} else {
		mux_peer_comm_mappings[i] = NULL;
	}
	MUTEX_UNLOCK(entry->mux_peer_comm_mutex);
	MUTEX_UNLOCK(mux_peer_comm_mutex);

	/*
	 * Walk through the list of all fds and close the ones
	 * that correspond to this fd as target_fd
	 */
	MUTEX_LOCK(mux_demux_array_mutex);
	for (i = 0; i < MAX_CONNECTIONS; i++) {
		mux_entry = mux_mappings[i];

		while (mux_entry) {
			if (mux_entry->mux_demux_mapping_target_fd == fd) {
				mux_entry->mux_demux_mapping_is_inactive = 1;
				SHUTDOWN(
				mux_entry->mux_demux_mapping_myside_fd, 2);
				mux_entry->mux_demux_mapping_target_fd = 0;
			}
			mux_entry = mux_entry->mux_demux_mapping_next;
		}
	}
	MUTEX_UNLOCK(mux_demux_array_mutex);

	MUTEX_LOCK(mux_vxt_pollfd_mutex);
	for (i = 0; i < mux_vxt_pollfd_size; i++) {
#ifdef NETWORK_ENDPOINT
		if (mux_vxt_pollfd[i].FD_OBJ == fd) {
#else
		/*
		 * For vxt endpoints
		 */
		if (mux_vxt_pollfd[i].FD_OBJ == (unsigned int)fd) {
#endif
			mux_vxt_pollfd[i].FD_OBJ  = 0;
			break;
		}
	}
	MUTEX_UNLOCK(mux_vxt_pollfd_mutex);

	if (entry->mux_peer_comm_is_vxt_ep) {
#ifdef VXT_ENDPOINT
		mux_close_socket(entry->mux_peer_comm_fd, 0);
#endif /* VXT_ENDPOINT */
	} else {
		CLOSE_SOCKET(entry->mux_peer_comm_fd);
	}
	MUTEX_LOCK(mux_vxt_connections);
	mux_active_vxt_connections--;
	MUTEX_UNLOCK(mux_vxt_connections);

	/*
	 * Free up all the allocated buffers
	 */
	MUTEX_DESTROY(entry->mux_peer_comm_send_mutex);
	MUTEX_DESTROY(entry->mux_peer_comm_recv_mutex);
	MUTEX_DESTROY(entry->mux_peer_comm_my_recv_current_window_size_mutex);
	for (i = 0; i < MAX_COMM_BUF_SIZE; i++) {
		free(entry->mux_peer_comm_send_buf->mux_comm_bufptr[i]);
		free(entry->mux_peer_comm_recv_buf->mux_comm_bufptr[i]);
	}
	MUTEX_DESTROY(entry->mux_peer_comm_send_buf->mux_comm_buf_mutex);
	MUTEX_DESTROY(entry->mux_peer_comm_recv_buf->mux_comm_buf_mutex);
	MUTEX_DESTROY(entry->mux_peer_comm_mutex);
	free(entry->mux_peer_comm_send_buf);
	free(entry->mux_peer_comm_recv_buf);

	/*
	 * Xprtld connections are disabled for the time being
	for (i = 0; i < MAX_XPRTLD_CONNECTIONS; i++) {
		if (entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_alloted== 0) {
			SHUTDOWN(
			entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_fd,
			2);
			CLOSE_SOCKET(
			entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_fd);
		} else if (entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_alloted == 1) {
			SHUTDOWN(
			entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_fd,
			2);
		}
	}
	*/
	memset((void*)entry, '\0', sizeof(struct mux_peer_comm_mapping));
	free(entry);
}

/*
 * Get the descriptor corresponding to this uuid
 */
static SOCKET
mux_get_peer_comm_fd(uuid)
	char	*uuid;
{
	int	i, found = 0;
	struct mux_peer_comm_mapping	*entry = NULL;
	SOCKET	ret;

	MUTEX_LOCK(mux_peer_comm_mutex);

	for (i = 0;i < MAX_NUMBER_OF_GUESTS; i++) {
		entry = mux_peer_comm_mappings[i];
		while(entry) {
			if (!memcmp(entry->mux_peer_comm_uuid, uuid,
			    MUX_UUID_SIZE)) {
				found = 1;
				break;
			}
			entry = entry->mux_peer_comm_mapping_next;
		}
		if (found == 1) {
			break;
		}
	}

	if (found) {
		ret = entry->mux_peer_comm_fd;
	} else {
		ret = 0;
	}

	MUTEX_UNLOCK(mux_peer_comm_mutex);
	return ret;
}

struct mux_peer_comm_mapping *
mux_get_peer_comm_entry(fd)
	SOCKET	fd;
{
	int				i;
	struct mux_peer_comm_mapping	*entry;

	MUTEX_LOCK(mux_peer_comm_mutex);
	i = MUX_FD_HASH(fd, MAX_NUMBER_OF_GUESTS);
	entry = mux_peer_comm_mappings[i];

	while (entry) {
		if (entry->mux_peer_comm_fd == fd) {
			/*
			 * See if it's marked for removal
			 */
			MUTEX_LOCK(entry->mux_peer_comm_mutex);
			if (entry->mux_peer_comm_flags &
			    MUX_PEER_COMM_MARKED_FOR_REMOVAL) {
				MUX_LOG_ENTRY1("Entry for fd %d marked "
					"for removal", fd);
				MUTEX_UNLOCK(entry->mux_peer_comm_mutex);
				break;
			}

			/*
			 * Get a reference count to this fd
			 * so that the entry does not get deleted
			 * while in use
			 */
			entry->mux_peer_comm_fd_refcnt++;
			MUTEX_UNLOCK(entry->mux_peer_comm_mutex);

			MUTEX_UNLOCK(mux_peer_comm_mutex);
			return (entry);
		}
		entry = entry->mux_peer_comm_mapping_next;
	}
	
	MUTEX_UNLOCK(mux_peer_comm_mutex);
	MUX_LOG_ENTRY1("Entry for fd %d not found", fd);
	return NULL;
}

static void
mux_add_local_fd(newfd)
	SOCKET	newfd;
{
	int	index = 0;
	struct mux_demux_mapping	*entry = NULL, *prev_entry = NULL;
	int	ret = 0;

	MUX_LOG_ENTRY1("mux_add_local_fd : %d ", newfd);

	MUTEX_LOCK(mux_demux_array_mutex);
	index = MUX_FD_HASH(newfd, MAX_CONNECTIONS);
	entry = mux_mappings[index];

	if (entry) {
		while (entry) {
			assert(entry->mux_demux_mapping_myside_fd != newfd);
			prev_entry = entry;
			entry = entry->mux_demux_mapping_next;
		}
	}
	entry = (struct mux_demux_mapping *)
		malloc(sizeof(struct mux_demux_mapping));
	memset((void *) entry, '\0', sizeof(struct mux_demux_mapping));
	entry->mux_demux_mapping_next = NULL;
	entry->mux_demux_mapping_myside_fd = newfd;
	entry->mux_demux_mapping_fd_flags = 0;
	entry->mux_demux_mapping_fd_refcnt = 0;
	entry->mux_demux_mapping_fd_bufcnt = 0;
	entry->mux_demux_mapping_start_index = 0;
	entry->mux_demux_mapping_end_index = 0;
	entry->mux_demux_mapping_conn_cookie = mux_get_conn_cookie();

	MUTEX_INIT(entry->mux_demux_mapping_fd_mutex, ret);

	if (prev_entry) {
		prev_entry->mux_demux_mapping_next = entry;
	} else {
		mux_mappings[index] = entry;
	}

	MUTEX_LOCK(mux_local_pollfd_mutex);

	/*
	 * Add the entry at the end of the array
	 */
	mux_local_pollfd[mux_local_pollfd_size].fd = newfd;
	mux_local_pollfd[mux_local_pollfd_size].events = POLLIN;
	mux_local_pollfd[mux_local_pollfd_size].revents = 0;
	mux_local_pollfd_size++;

	MUTEX_UNLOCK(mux_local_pollfd_mutex);
	MUTEX_UNLOCK(mux_demux_array_mutex);
}

static void
mux_mark_fd_inactive(fd, remove_from_poll, send_disconnect)
	SOCKET	fd;
	int	remove_from_poll;
	int	send_disconnect;
{
	struct mux_demux_mapping	*mux_entry = NULL;
	char	buf[100];
	struct mux_msg_hdr	hdr;
	SOCKET			target_fd;

	MUX_LOG_ENTRY1("Marking fd inactive %d", fd);

	if (!fd) {
		return;
	}

	MUTEX_LOCK(mux_demux_array_mutex);
	mux_entry = mux_get_mux_demux_mapping_entry(fd);
	if (!mux_entry) {
		MUX_LOG_ENTRY1("Entry for fd : %d not found", fd);
		MUTEX_UNLOCK(mux_demux_array_mutex);
		return;
	}

	MUTEX_LOCK(mux_entry->mux_demux_mapping_fd_mutex);

	mux_entry->mux_demux_mapping_is_inactive = 1;

	if (mux_entry->mux_demux_mapping_fd_flags & MUX_FD_SENDING) {
		/*
		 * A dedicated thread is sending the data on this fd
		 * so defer the close until that's done
		 */
		MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
		MUTEX_UNLOCK(mux_demux_array_mutex);
		return;
	}

	SHUTDOWN(fd, 2);

	if (remove_from_poll) {
		mux_entry->mux_demux_mapping_removed_from_poll = 1;
	}

	target_fd = mux_entry->mux_demux_mapping_target_fd;
	/*
	 * target_fd could be zero if a connection is requested to a domain
	 * that does not exist
	 */
	if (target_fd == 0) {
		MUX_LOG_ENTRY0("target_fd is zero");
		MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
		MUTEX_UNLOCK(mux_demux_array_mutex);
		return;
	}

	/*
	 * mux_demux_mapping_peerside_fd could be zero if the connection
	 * to xprtld is not successful. So, no need to send a disconnect
	 */
	if (mux_entry->mux_demux_mapping_peerside_fd == 0) {
		MUX_LOG_ENTRY0("mux_demux_mapping_peerside_fd is zero");
		MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
		MUTEX_UNLOCK(mux_demux_array_mutex);
		return;
	}

	/*
	 * Return from here itself if there's no need to send a disconnect
	 * message
	 */
	if (!send_disconnect) {
		MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
		MUTEX_UNLOCK(mux_demux_array_mutex);
		return;
	}
	memset(buf, '\0', sizeof(buf));
	memset((char *)&hdr, '\0', sizeof(hdr));
	hdr.mux_msg_recv_fd = mux_entry->mux_demux_mapping_peerside_fd;
	hdr.mux_msg_conn_cookie = mux_entry->mux_demux_mapping_conn_cookie;
	hdr.mux_msg_flag |= MUX_MSG_FD_DISCONNECT;
	hdr.mux_msg_size = 0;
	hdr.mux_msg_send_fd = 0;
	MUX_LOG_ENTRY1("mux_mark_fd_inactive: Sending the control message"
		" on fd %d", target_fd);

	if (remove_from_poll) {
		mux_entry->mux_demux_mapping_peerside_fd = 0;
	}
	MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
	MUTEX_UNLOCK(mux_demux_array_mutex);

	if (mux_send_msg_over_mxt(target_fd, buf,
	    mux_msg_hdr_pack_sz(hdr), 0, &hdr) < 0) {
		MUX_LOG_ENTRY0("mux_mark_fd_inactive: Unable to send "
			" disconnect message");
		mux_remove_peer_comm_mapping(target_fd);
	}
}

static void
mux_remove_local_fds()
{
	int	j;
	int	comm_index = 0;
	int	index = 0;
	struct mux_demux_mapping	*mux_entry = NULL, *prev_entry = NULL;
	struct mux_demux_mapping	*remove_entry = NULL;
	struct mux_peer_comm_mapping	*entry;


	MUTEX_LOCK(mux_demux_array_mutex);

	for (index = 0; index < MAX_CONNECTIONS; index++) {
		mux_entry = mux_mappings[index];
		prev_entry = NULL;
		remove_entry = NULL;

		while (mux_entry) {
			if ((mux_entry->mux_demux_mapping_is_inactive) &&
			    (mux_entry->mux_demux_mapping_removed_from_poll) &&
			    (mux_entry->mux_demux_mapping_fd_refcnt == 0)) {
				MUTEX_LOCK(mux_entry->mux_demux_mapping_fd_mutex);
				MUX_LOG_ENTRY2("Removing fd : %d mux_entry :%p",
				mux_entry->mux_demux_mapping_myside_fd,
				mux_entry);
				if (prev_entry) {
					prev_entry->mux_demux_mapping_next =
						mux_entry->mux_demux_mapping_next;
				} else {
					mux_mappings[index] =
						mux_entry->mux_demux_mapping_next;
				}
				remove_entry = mux_entry;
				mux_entry = mux_entry->mux_demux_mapping_next;
				remove_entry->mux_demux_mapping_next = NULL;
			} else {
				prev_entry = mux_entry;
				mux_entry = mux_entry->mux_demux_mapping_next;
				continue;
			}

			if (remove_entry) {
				/*
				 * Clean up the mux_demux_mapping_comm_data
				 */
				for (j = remove_entry->mux_demux_mapping_start_index;
				     j < remove_entry->mux_demux_mapping_end_index; j++) {
					if (remove_entry->mux_demux_mapping_comm_data_valid[j]) {
						comm_index =
							remove_entry->mux_demux_mapping_comm_data[j];
						entry = mux_get_peer_comm_entry(
							remove_entry->mux_demux_mapping_target_fd);
						if (entry) {
							mux_reclaimbuf(
						entry->mux_peer_comm_send_buf,
						comm_index);
							release_fd_refcnt(
						remove_entry->mux_demux_mapping_target_fd);
						}
					}
				}
				CLOSE_SOCKET(
				remove_entry->mux_demux_mapping_myside_fd);
				MUTEX_UNLOCK(
				remove_entry->mux_demux_mapping_fd_mutex);
				MUTEX_DESTROY(
				remove_entry->mux_demux_mapping_fd_mutex);
				free(remove_entry);
			}
			remove_entry = NULL;
		}
	}

	MUTEX_UNLOCK(mux_demux_array_mutex);
}

static SOCKET
get_recv_fd(fd)
	SOCKET	fd;
{
	struct mux_demux_mapping	*entry = NULL;
	int				ret = 0;

	MUTEX_LOCK(mux_demux_array_mutex);
	entry = mux_get_mux_demux_mapping_entry(fd);
	if (entry) {
		ret = entry->mux_demux_mapping_peerside_fd;
	}
	MUTEX_UNLOCK(mux_demux_array_mutex);
	return (ret);
}

static int
check_fd_validity(fd, type)
	SOCKET	fd;
	int	type;
{
	struct mux_demux_mapping	*entry = NULL;
	struct mux_peer_comm_mapping	*entry1 = NULL;

	if (type == PEER_DESCRIPTOR) {
		entry1 = mux_get_peer_comm_entry(fd);
		if (entry1) {
			release_fd_refcnt(fd);
		}
	} else {
		entry = mux_get_mux_demux_mapping_entry(fd);
	}
	
	if (entry || entry1) {
		return 1;
	} else {
		return 0;
	}
}

void
release_local_fd_refcnt(fd)
	SOCKET          fd;
{
	struct mux_demux_mapping        *entry = NULL;

	MUTEX_LOCK(mux_demux_array_mutex);

	entry = mux_get_mux_demux_mapping_entry(fd);
	assert(entry);
	MUTEX_LOCK(entry->mux_demux_mapping_fd_mutex);
	entry->mux_demux_mapping_fd_refcnt--;
	MUTEX_UNLOCK(entry->mux_demux_mapping_fd_mutex);

	MUTEX_UNLOCK(mux_demux_array_mutex);
}

static int
is_fd_active(fd, cookie)
	SOCKET		fd;
	int		cookie;
{
	struct mux_demux_mapping	*entry = NULL;

	MUTEX_LOCK(mux_demux_array_mutex);
	entry = mux_get_mux_demux_mapping_entry(fd);
	if ((!entry) || (entry->mux_demux_mapping_is_inactive) ||
	    ((cookie != 0) &&
	     (entry->mux_demux_mapping_conn_cookie != cookie))) {
		MUX_LOG_ENTRY1("fd : %d is not active", fd);
		MUTEX_UNLOCK(mux_demux_array_mutex);
		return 0;
	}
	MUTEX_LOCK(entry->mux_demux_mapping_fd_mutex);
	entry->mux_demux_mapping_fd_refcnt++;
	MUTEX_UNLOCK(entry->mux_demux_mapping_fd_mutex);

	MUTEX_UNLOCK(mux_demux_array_mutex);
	return 1;
}

/*
 * Do we need a lock here since we are modifying target_fd ?
 * mux_process_local_comm is single threaded as of now.
 */
static void
mux_update_target_fd(fd, target_fd)
	SOCKET	fd;
	SOCKET	target_fd;
{
	struct mux_demux_mapping	*entry = NULL;

	MUX_LOG_ENTRY2("Updating targetfd for %d target_fd is %d ",
		fd, target_fd);
	MUTEX_LOCK(mux_demux_array_mutex);
	entry = mux_get_mux_demux_mapping_entry(fd);
	assert(entry);
	entry->mux_demux_mapping_target_fd = target_fd;
	MUTEX_UNLOCK(mux_demux_array_mutex);
}

static SOCKET
mux_get_target_fd(fd)
	SOCKET	fd;
{
	struct mux_demux_mapping	*entry = NULL;
	int				ret = 0;

	entry = mux_get_mux_demux_mapping_entry(fd);
	if (entry) {
		ret = entry->mux_demux_mapping_target_fd;
		return (ret);
	} else {
		return 0;
	}
}

void
release_fd_refcnt(fd)
	SOCKET	fd;
{
	struct mux_peer_comm_mapping	*entry = NULL;
	int	remove = 0, i = 0;

	MUTEX_LOCK(mux_peer_comm_mutex);
	i = MUX_FD_HASH(fd, MAX_NUMBER_OF_GUESTS);
	entry = mux_peer_comm_mappings[i];

	while (entry) {
		if (entry->mux_peer_comm_fd == fd) {
			/*
			 * If the refcnt is zero and the fd is marked
			 * for removal, Remove it.
			 */
			MUTEX_LOCK(entry->mux_peer_comm_mutex);
			entry->mux_peer_comm_fd_refcnt--;
			assert(entry->mux_peer_comm_fd_refcnt >= 0);
			if ((entry->mux_peer_comm_fd_refcnt == 0) &&
			    (entry->mux_peer_comm_flags &
			     MUX_PEER_COMM_MARKED_FOR_REMOVAL)) {
				remove = 1;
			}
			MUTEX_UNLOCK(entry->mux_peer_comm_mutex);

			break;
		}
		entry = entry->mux_peer_comm_mapping_next;
	}
	MUTEX_UNLOCK(mux_peer_comm_mutex);

	if (remove) {
		mux_remove_peer_comm_mapping(fd);
	}
}

int
get_first_msg_info(fd)
	SOCKET	fd;
{
	struct mux_demux_mapping	*entry = NULL;

	MUTEX_LOCK(mux_demux_array_mutex);
	entry = mux_get_mux_demux_mapping_entry(fd);
	assert(entry);
	if (entry->mux_demux_mapping_first_msg) {
		MUTEX_UNLOCK(mux_demux_array_mutex);
		return 0;
	} else {
		MUTEX_UNLOCK(mux_demux_array_mutex);
		return 1;
	}
}

void
set_first_msg_info(fd)
	SOCKET	fd;
{
	struct mux_demux_mapping	*entry = NULL;

	MUTEX_LOCK(mux_demux_array_mutex);

	entry = mux_get_mux_demux_mapping_entry(fd);
	assert(entry);
	entry->mux_demux_mapping_first_msg = 1;
	MUX_LOG_ENTRY1("Setting first msg for fd %d", fd);

	MUTEX_UNLOCK(mux_demux_array_mutex);
}

static struct mux_demux_mapping *
mux_get_mux_demux_mapping_entry(fd)
	SOCKET	fd;
{
	struct mux_demux_mapping	*entry = NULL;
	int				index = 0;

	index = MUX_FD_HASH(fd, MAX_CONNECTIONS);
	entry = mux_mappings[index];

	while (entry) {
		if (entry->mux_demux_mapping_myside_fd == fd) {
			return entry;
		}
		entry = entry->mux_demux_mapping_next;
	}
	return NULL;
}

static int
mux_check_client(sendfd, index)
	SOCKET	sendfd;
	int	index;
{
	int	ret = 0;
	struct mux_demux_mapping	*entry = NULL;


	MUTEX_LOCK(mux_demux_array_mutex);
	entry = mux_get_mux_demux_mapping_entry(sendfd);
	
	MUTEX_LOCK(entry->mux_demux_mapping_fd_mutex);
	if (entry->mux_demux_mapping_fd_flags & MUX_FD_SENDING) {
		/*
		 * A dedicated thread has already been scheduled for this,
		 * Add yourself to the list.
		 */

		/*
		 * Assert if the buffer is full and there's more data to
		 * be queued. We should never get into this situation since
		 * the remote side would never send any data if there's
		 * no space on this side
		 */
		assert(entry->mux_demux_mapping_fd_bufcnt < MAX_COMM_BUF_SIZE);

		entry->mux_demux_mapping_comm_data
			[entry->mux_demux_mapping_end_index] = index;
		entry->mux_demux_mapping_comm_data_valid
			[entry->mux_demux_mapping_end_index] = 1;
		entry->mux_demux_mapping_fd_bufcnt++;
		/* 
		 * If this is the first entry in the
		 * mux_demux_mapping_comm_data, then track the starting index.
		 */
		if (!(entry->mux_demux_mapping_fd_flags &
		      MUX_NEW_DATA_ADDED)) {
			entry->mux_demux_mapping_start_index =
				entry->mux_demux_mapping_end_index;
			entry->mux_demux_mapping_fd_flags |= MUX_NEW_DATA_ADDED;
		}

		entry->mux_demux_mapping_end_index =
				(entry->mux_demux_mapping_end_index + 1) %
				MAX_COMM_BUF_SIZE;
		ret = 1;
		MUX_LOG_ENTRY1("mux_check_client: Added buf with index :"
			" %d ", index);
	} else {
		entry->mux_demux_mapping_fd_flags = MUX_FD_SENDING;
		ret = 0;
	}
	MUTEX_UNLOCK(entry->mux_demux_mapping_fd_mutex);
	MUTEX_UNLOCK(mux_demux_array_mutex);

	return ret;
}


static void
mux_clear_fd_flags(fd, flag)
	SOCKET	fd;
	int	flag;
{
	struct mux_demux_mapping	*entry = NULL;
	int				index = 0;

	MUTEX_LOCK(mux_demux_array_mutex);

	index = MUX_FD_HASH(fd, MAX_CONNECTIONS);
	entry = mux_mappings[index];

	while (entry) {
		if (entry->mux_demux_mapping_myside_fd == fd) {
			break;
		}
		entry = entry->mux_demux_mapping_next;
	}
	if (!entry) {
		MUTEX_UNLOCK(mux_demux_array_mutex);
		return;
	}

	MUTEX_LOCK(entry->mux_demux_mapping_fd_mutex);
	entry->mux_demux_mapping_fd_flags &= ~flag;
	MUTEX_UNLOCK(entry->mux_demux_mapping_fd_mutex);

	MUTEX_UNLOCK(mux_demux_array_mutex);
}

/*
 * This function is called in order to carry out the processing of
 * the data for a particular client
 */
static void
mux_client_thread(clnt)
	struct clnt_msg_info	*clnt;
{
	int	j = 0;
	int	data_to_send = 1, ret = 0;
	char	*buf = NULL, *tmpbuf;
	int	index_to_send = 0;
	int	new_data_index = 0;
	int	count = 0;
	int	index, partial_send;
	struct mux_peer_comm_mapping	*entry = NULL;
	struct mux_msg_hdr	hdr;
	struct mux_demux_mapping	*mux_entry = NULL;
	SOCKET	target_fd, sendfd;
	THREAD_T	thisthreadid = GET_THREAD_ID();

	sendfd = clnt->clnt_msg_info_sendfd;
	index = clnt->clnt_msg_info_index;
	partial_send = clnt->clnt_msg_info_partial_send;

	free(clnt);

	mux_entry = mux_get_mux_demux_mapping_entry(sendfd);
	MUTEX_LOCK(mux_entry->mux_demux_mapping_fd_mutex);

	target_fd = mux_get_target_fd(sendfd);
	entry = mux_get_peer_comm_entry(target_fd);
	if (!entry) {
		MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
		return;
	}

	/*
	 * Start index of the new data
	 */
	new_data_index = mux_entry->mux_demux_mapping_start_index;
	/*
	 * Set the fd to blocking since we can afford to block in the 
	 * dedicated thread
	 */
	mux_sock_blocking(sendfd);

	index_to_send = index;

	while (data_to_send) {
		buf = entry->mux_peer_comm_send_buf->
			mux_comm_bufptr[index_to_send];
		tmpbuf = buf;
		mux_msg_hdr_unpack(hdr, tmpbuf);

		/*
		 * Data may have been sent partially the first time, so just
		 * resend the remaining one for the time being
		 * Note: We are releasing the mutex while the send is tried.
		 * Some more data can sneak in while we are blocked trying to
		 * do a send. So, make sure to look at the new data after this
		 * send has completed.
		 */
		MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
		if (partial_send) {
			MUX_LOG_ENTRY0("mux_client_thread: sending "
				"remaining data ");
			ret = mux_msg_send(sendfd,
				buf + mux_msg_hdr_pack_sz(hdr) + partial_send,
				hdr.mux_msg_size - partial_send, 0);
			partial_send = 0;
		} else {
			ret = mux_msg_send(sendfd,
				buf + mux_msg_hdr_pack_sz(hdr),
				hdr.mux_msg_size, 0);
		}
		MUTEX_LOCK(mux_entry->mux_demux_mapping_fd_mutex);

		if (ret == -1) {
			MUX_LOG_ENTRY0("mux_client_thread: send failed from"
				" dedicated thread ");
			mux_reclaimbuf(entry->mux_peer_comm_send_buf,
				index_to_send);
			mux_clear_fd_flags(sendfd,
				MUX_FD_SENDING|MUX_NEW_DATA_ADDED);
			MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
			mux_mark_fd_inactive(sendfd, 0, 1);
			mux_remove_from_threadlist(thisthreadid);
			mux_release_xprtld_connection(target_fd, sendfd);
			release_local_fd_refcnt(sendfd);
			release_fd_refcnt(target_fd);
			return;
		} else {
			mux_reclaimbuf(entry->mux_peer_comm_send_buf,
				index_to_send);

			/*
			 * See if anything got added while we were sending
			 * the data
			 */

			if ((mux_entry->mux_demux_mapping_fd_flags &
			     MUX_NEW_DATA_ADDED) &&
			     (mux_entry->mux_demux_mapping_comm_data_valid
			      [new_data_index])) {
				MUX_LOG_ENTRY0("mux_client_thread: "
					"Processing the newly added data ");
				index_to_send =
					mux_entry->mux_demux_mapping_comm_data
					[new_data_index];
				new_data_index = (new_data_index + 1) %
							MAX_COMM_BUF_SIZE;
				mux_entry->mux_demux_mapping_fd_bufcnt--;
				mux_entry->mux_demux_mapping_start_index =
					new_data_index;
			} else {
				mux_entry->mux_demux_mapping_fd_flags = 0;
				data_to_send = 0;
				/* Clean up the buffers */
				for (j =
				     mux_entry->mux_demux_mapping_start_index;
				     j < mux_entry->mux_demux_mapping_end_index;
				     j++) {
					mux_entry->
				mux_demux_mapping_comm_data_valid[j] = 0;
					mux_entry->
					mux_demux_mapping_comm_data[j] = 0;
					count++;
				}
				MUX_LOG_ENTRY1("Cleaned up %d records ",
					count);
			}
			
		}
	}
	MUX_LOG_ENTRY0("mux_client_thread: dedicated thread is exiting ");
	mux_remove_from_threadlist(thisthreadid);
	/*
	 * Check if this fd has been marked inactive while this thread was
	 * sending out the data
	 */
	mux_clear_fd_flags(sendfd, MUX_FD_SENDING|MUX_NEW_DATA_ADDED);
	if (mux_entry->mux_demux_mapping_is_inactive) {
		MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
		mux_mark_fd_inactive(sendfd, 0, 1);
	} else {
		MUTEX_UNLOCK(mux_entry->mux_demux_mapping_fd_mutex);
	}
	release_local_fd_refcnt(sendfd);
	release_fd_refcnt(target_fd);
	return;
}

#ifdef GC_DEF

static void
mux_garbage_collection()
{
}

#endif /* GC_DEF */

void
mux_process_vxt_comm(eventfd, returnval)
	SOCKET	eventfd;
	int	*returnval;
{
	char	*comm_buf = NULL, *buf = NULL, *bufptr = NULL, *unpacked_ptr;
	char	*packed_hdr2, *tmpbuf;
	int	ret = 0, ret1 = 0, check = 0;
	struct mux_msg_hdr	hdr, hdr1;
	unsigned int	len = 0, len1 = 0;
	struct mux_handshake_frame	ptr;
	int	sendlen;
	SOCKET	sendfd, mysidefd = 0;
	int	index = 0;
	struct clnt_msg_info	*clnt;
	THREAD_T client;
	struct mux_peer_comm_mapping	*entry = NULL;
	struct mux_handshake_frame	hdr2;
	size_t	hdr_len = 0;
	long long	threadid;
	int		error_code;
	char		*uuid;

	*returnval = 1;
	entry = mux_get_peer_comm_entry(eventfd);
	if (!entry) {
		*returnval = 0;
		return;
	}

	threadid = 0LL;
	/*
	 * A data message from peer Mxt
	 */
	MUX_LOG_ENTRY1("Message over Mxt : %d ",
		entry->mux_peer_comm_fd);

	index = mux_getbuf(entry->mux_peer_comm_send_buf,
		&entry->mux_peer_comm_send_buf_index);

	if (index == -1) {
		release_fd_refcnt(eventfd);
		SLEEP(1);
		return;
	} else {
		comm_buf =
		entry->mux_peer_comm_send_buf->mux_comm_bufptr[index];
		buf = comm_buf;
	}
	memset(buf, '\0', MAX_LEN);

	/*
	 * Read the header first
	 */
	hdr_len = mux_msg_hdr_pack_sz(hdr1);
	ret = mux_recv_msg_over_mxt(eventfd, buf, hdr_len, 0);
	if (ret <= 0) {
		MUX_LOG_ENTRY1("errno : %x", errno)
		mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);

#ifdef NETWORK_ENDPOINT
		mux_remove_peer_comm_mapping(eventfd);
#endif /* NETWORK_ENDPOINT */

#ifdef VXT_ENDPOINT
		if ((errno == EXDEV) || (errno == EPIPE)) {
			mux_remove_peer_comm_mapping(eventfd);
		}
#endif /* VXT_ENDPOINT */
		*returnval = 0;
		release_fd_refcnt(eventfd);
		return;
	}
	tmpbuf = buf;
	mux_msg_hdr_unpack(hdr, tmpbuf);

	assert(hdr.mux_msg_size <=
		(MAX_LEN - mux_msg_hdr_pack_sz(hdr)));
	len = hdr.mux_msg_size;
	MUX_LOG_ENTRY1("need to receive more %d bytes ", len);
	if (len) {
		bufptr = buf + hdr_len;
		len1 = len;
		if (entry->mux_peer_comm_is_vxt_ep) {
#ifdef VXT_ENDPOINT
			ret = VXT_RECV((int)eventfd, bufptr, len1, 0);
#endif  /* VXT_ENDPOINT */
		} else {
			ret = mux_msg_recv(eventfd, bufptr, (int)len1, 0);
		}
		if (ret <= 0) {
			MUX_LOG_ENTRY1("errno : %x", errno)
			/*
	 		 * We may have to teardown the connection
	 		 * corresponding to this.
	 		 */
#ifdef NETWORK_ENDPOINT
			mux_remove_peer_comm_mapping(eventfd);
#endif /* NETWORK_ENDPOINT */

#ifdef VXT_ENDPOINT
			if ((errno == EXDEV) || (errno == EPIPE)) {
				mux_remove_peer_comm_mapping(eventfd);
			}
#endif /* VXT_ENDPOINT */
			mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);
			*returnval = 0;
			release_fd_refcnt(eventfd);
			return;
		} else {
			assert(ret == (int)len);
		}
	}

	sendfd = hdr.mux_msg_recv_fd;

	switch (hdr.mux_msg_flag) {

	case MUX_MSG_FD_DISCONNECT:
		MUX_LOG_ENTRY0("MUX_MSG_FD_DISCONNECT");
		/*
		 * File descriptor corresponding to this has closed
		 * on the peer side, hence it has sent a disconnect
		 * message to close the fd on the peer side.
		 */
		if (!is_fd_active(sendfd, hdr.mux_msg_conn_cookie)) {
			mux_reclaimbuf(entry->mux_peer_comm_send_buf,
				index);
			release_fd_refcnt(eventfd);
			return;
		}
		assert(hdr.mux_msg_send_fd == 0);
		MUX_LOG_ENTRY0("Received a disconnect message ");
		mux_mark_fd_inactive(hdr.mux_msg_recv_fd, 0, 0);

		mux_reclaimbuf(entry->mux_peer_comm_send_buf,
			index);
		release_local_fd_refcnt(sendfd);
		release_fd_refcnt(eventfd);
		return;

	case MUX_MSG_NEW_CONNECTION:
		MUX_LOG_ENTRY0("MUX_MSG_NEW_CONNECTION");
		/*
		 * If this fd is zero, that means no endpoint exists on
		 * this side, so we need to make a connection with the
		 * service
		 */
		MUX_LOG_ENTRY0("calling"
			" mux_create_connection_to_xprtld ");
		mysidefd = mux_create_connection_to_xprtld(
				hdr.mux_msg_send_fd, eventfd,
				&error_code, hdr.mux_msg_conn_cookie);
		if (mysidefd == MUX_CONNECT_GENERIC_ERROR) {
			/*
			 * Send a disconnect message to the sender
			 */
			bufptr = (char *)
				malloc(sizeof(struct mux_msg_hdr));
			memset(bufptr, '\0',
				sizeof(struct mux_msg_hdr));
			memset((char *)&hdr1, '\0', sizeof(struct mux_msg_hdr));

			hdr1.mux_msg_flag = MUX_MSG_NEW_CONNECTION_ACK;
			hdr1.mux_msg_send_fd = MUX_CONNECT_GENERIC_ERROR;
			hdr1.mux_msg_recv_fd = hdr.mux_msg_send_fd;
			hdr1.mux_msg_conn_cookie = hdr.mux_msg_conn_cookie;
			hdr1.mux_msg_size = 0;
			hdr1.mux_msg_error_code = error_code;
			sendlen = mux_msg_hdr_pack_sz(hdr1);
			if (mux_send_msg_over_mxt(
			    entry->mux_peer_comm_fd,
			    bufptr, sendlen, 0, &hdr1) < sendlen) {
				MUX_LOG_ENTRY0("mux_process_local_comm: "
					"Data send failed");
				mux_remove_peer_comm_mapping(
					entry->mux_peer_comm_fd);
			}
			free(bufptr);
			mux_reclaimbuf(entry->mux_peer_comm_send_buf,
				index);
			release_fd_refcnt(eventfd);
			return;
		}
		/*
		 * New connection has been created, send the fd to
		 * the peer. If the connection creation has failed,
		 * we will be sending back "-1" causing the client
		 * to get disconnected
		 */

		/*
		 * Send the connection ACK to the peer
		 */
		bufptr = (char *) malloc(sizeof(struct mux_msg_hdr));
		memset(bufptr, '\0', sizeof(struct mux_msg_hdr));
		memset((char *)&hdr1, '\0', sizeof(struct mux_msg_hdr));
		hdr1.mux_msg_flag = MUX_MSG_NEW_CONNECTION_ACK;
		hdr1.mux_msg_send_fd = mysidefd;
		hdr1.mux_msg_recv_fd = hdr.mux_msg_send_fd;
		hdr1.mux_msg_conn_cookie = hdr.mux_msg_conn_cookie;
		hdr1.mux_msg_size = 0;
		sendlen = mux_msg_hdr_pack_sz(hdr1);
		if (mux_send_msg_over_mxt(entry->mux_peer_comm_fd,
		    bufptr, sendlen, 0, &hdr1) < sendlen) {
			MUX_LOG_ENTRY0("mux_process_local_comm: Data send "
				" failed");
			mux_remove_peer_comm_mapping(
				entry->mux_peer_comm_fd);
		}
		free(bufptr);
		mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);
		release_fd_refcnt(eventfd);
		return;

	case MUX_MSG_NEW_CONNECTION_ACK:
		MUX_LOG_ENTRY0("MUX_MSG_NEW_CONNECTION_ACK");

		/*
		 * It's possible that the local connection may have been 
		 * closed while we were waiting for the connection response
		 * so, check if the local connection is still around and send
		 * a disconnect response to the peer
		 */
		if (!is_fd_active(sendfd, hdr.mux_msg_conn_cookie)) {
			bufptr = (char *)
				malloc(sizeof(struct mux_msg_hdr));
			memset(bufptr, '\0',
				sizeof(struct mux_msg_hdr));
			memset((char *)&hdr1, '\0', sizeof(hdr));
			hdr1.mux_msg_recv_fd = hdr.mux_msg_send_fd;
			hdr1.mux_msg_conn_cookie = hdr.mux_msg_conn_cookie;
			hdr1.mux_msg_flag |= MUX_MSG_FD_DISCONNECT;
			hdr1.mux_msg_size = 0;
			hdr1.mux_msg_send_fd = 0;
			MUX_LOG_ENTRY2("mux_process_vxt_comm: Sending a"
				" disconnect message on fd %d for fd %d",
				eventfd, hdr.mux_msg_send_fd);

			if (mux_send_msg_over_mxt(eventfd, bufptr,
	    		    mux_msg_hdr_pack_sz(hdr1), 0, &hdr1) < 0) {
				MUX_LOG_ENTRY0("mux_process_vxt_comm: Unable"
					" to send disconnect message");
				mux_remove_peer_comm_mapping(eventfd);
			}
			free(bufptr);
			mux_reclaimbuf(entry->mux_peer_comm_send_buf,
					index);
			release_fd_refcnt(eventfd);
			return;
		}

		/*
		 * If mux_msg_send_fd is -1, it means the connection
		 * has failed. close the corresponding fd.
		 */
		if (hdr.mux_msg_send_fd == MUX_CONNECT_GENERIC_ERROR) {
			mux_connect_response(hdr.mux_msg_recv_fd,
				hdr.mux_msg_error_code);
			mux_mark_fd_inactive(hdr.mux_msg_recv_fd, 0, 0);
		} else {
			mux_update_mapping(hdr.mux_msg_recv_fd,
				hdr.mux_msg_send_fd);
			mux_connect_response(hdr.mux_msg_recv_fd,
				MUX_CONNECT_SUCCESS);
		}
		mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);
		release_local_fd_refcnt(sendfd);
		release_fd_refcnt(eventfd);
		return;

	case MUX_MSG_WINDOW_SIZE_UPDATE:
		MUX_LOG_ENTRY0("MUX_MSG_WINDOW_SIZE_UPDATE");
		/* Record the window size sent by peer */
		MUTEX_LOCK(
			entry->mux_peer_comm_my_recv_current_window_size_mutex);
		entry->mux_peer_comm_peer_recv_current_window_size =
			hdr.mux_msg_window_size;
		MUTEX_UNLOCK(
			entry->mux_peer_comm_my_recv_current_window_size_mutex);

		/*
		 * Send our window size to the peer 
		 */
		entry->mux_peer_comm_my_recv_current_window_size =
		mux_get_buf_freespace(entry->mux_peer_comm_send_buf);
		bufptr = (char *) malloc(sizeof(struct mux_msg_hdr));
		memset(bufptr, '\0', sizeof(struct mux_msg_hdr));
		memset((char *)&hdr1, '\0', sizeof(struct mux_msg_hdr));
		hdr1.mux_msg_window_size =
		entry->mux_peer_comm_my_recv_current_window_size;
		hdr1.mux_msg_flag = MUX_MSG_WINDOW_SIZE_UPDATE_ACK;
		hdr1.mux_msg_size = 0;
		sendlen = mux_msg_hdr_pack_sz(hdr1);
		if (mux_send_msg_over_mxt(entry->mux_peer_comm_fd,
		    bufptr, sendlen, 0, &hdr1) < sendlen) {
			MUX_LOG_ENTRY0("mux_process_local_comm: Data send"
				" failed");
			mux_remove_peer_comm_mapping(
				entry->mux_peer_comm_fd);
		}
		free(bufptr);
		mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);

		MUX_LOG_ENTRY2("mux_process_local_comm: Received window "
			"update message mine : %d peers: %d ",
		entry->mux_peer_comm_my_recv_current_window_size,
		entry->mux_peer_comm_peer_recv_current_window_size);

		release_fd_refcnt(eventfd);
		return;


	case MUX_MSG_WINDOW_SIZE_UPDATE_ACK:
		MUX_LOG_ENTRY0("MUX_MSG_WINDOW_SIZE_UPDATE_ACK");
		/* Record the window size sent by peer */
		MUTEX_LOCK(
			entry->mux_peer_comm_my_recv_current_window_size_mutex);
		entry->mux_peer_comm_peer_recv_current_window_size =
			hdr.mux_msg_window_size;
		entry->mux_peer_comm_window_update_message_sent = 0;
		MUTEX_UNLOCK(
			entry->mux_peer_comm_my_recv_current_window_size_mutex);

		MUX_LOG_ENTRY1("mux_process_local_comm: Received ACK for"
			" window update message peers: %d ",
		entry->mux_peer_comm_peer_recv_current_window_size);
		/* 
		 * We should be signalling the sender thread in
		 * case it's waiting for the window update from the
		 * peer
		 */
		mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);
		release_fd_refcnt(eventfd);
		return;

	case MUX_MSG_MXT_CONNECTION_REQ:
		MUX_LOG_ENTRY0("MUX_MSG_MXT_CONNECTION_REQ");
		
		unpacked_ptr = buf + hdr_len;
		mux_handshake_frame_unpack(ptr, unpacked_ptr);

		mux_update_peer_comm_mapping(
			ptr.mux_handshake_src_uuid, eventfd);
		MUX_LOG_ENTRY0("Handshake message from domain:");
		uuid = mux_convert_byteuuid_to_stduuid(
			ptr.mux_handshake_src_uuid);
		MUX_LOG_ENTRY1("%s", uuid);
		free(uuid);

		sendlen = mux_handshake_frame_pack_sz(hdr2) +
				mux_msg_hdr_pack_sz(hdr1);

		bufptr = (char *) malloc(sendlen);
		memset(bufptr, '\0', sendlen);
		memset((char *)&hdr1, '\0', sizeof(hdr1));
		memset((char *)&hdr2, '\0', sizeof(hdr2));

		/*
		 * Message header 
		 */
		hdr1.mux_msg_flag = MUX_MSG_MXT_CONNECTION_ACK;
		hdr1.mux_msg_size = mux_handshake_frame_pack_sz(hdr2);

		/*
		 * Handshake message
		 */
		packed_hdr2 = bufptr + mux_msg_hdr_pack_sz(hdr1);
		hdr2.mux_handshake_magic = MXT_MAGIC;
		memcpy(&hdr2.mux_handshake_src_uuid,
			domain_uuid, MUX_UUID_SIZE);
		strncpy(hdr2.mux_handshake_src_endpoint,
			"MXT", strlen("MXT"));
		hdr2.mux_handshake_flags = 0;

		mux_handshake_frame_pack(hdr2, packed_hdr2);

		ret = mux_send_msg_over_mxt(eventfd, bufptr, sendlen,
			0, &hdr1);

		mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);
		free(bufptr);

		if (ret < 0) {
			mux_remove_peer_comm_mapping(eventfd);
			release_fd_refcnt(eventfd);
			return;
		}

		MUX_LOG_ENTRY0("Received mxt connection req ");
		mux_create_xprtld_connections(eventfd);
		release_fd_refcnt(eventfd);
		return;

	case MUX_MSG_MXT_CONNECTION_ACK:
		MUX_LOG_ENTRY0("MUX_MSG_MXT_CONNECTION_ACK");

		unpacked_ptr = buf + hdr_len;
		mux_handshake_frame_unpack(ptr, unpacked_ptr);

		mux_update_peer_comm_mapping(
			ptr.mux_handshake_src_uuid, eventfd);
		MUX_LOG_ENTRY0("Handshake message from domain:");
		uuid = mux_convert_byteuuid_to_stduuid(
			ptr.mux_handshake_src_uuid);
		MUX_LOG_ENTRY1("%s", uuid);
		free(uuid);

		/*
		 * Save the Dom0 uuid from ptr.mux_handshake_src_uuid.
		 */
		if (!is_dom0) {
			memcpy(dom0_uuid, ptr.mux_handshake_src_uuid,
				MUX_UUID_SIZE);
		}
		mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);
		mux_create_xprtld_connections(eventfd);
		MUX_LOG_ENTRY0("Received mxt connection ack ");
		release_fd_refcnt(eventfd);
		return;

	default:
		break;
	}

	assert(sendfd != 0);

	if (!is_fd_active(sendfd, hdr.mux_msg_conn_cookie)) {
		mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);
		release_fd_refcnt(eventfd);
		return;
	}

	release_fd_refcnt(eventfd);
	assert(get_recv_fd(sendfd));

	/*
	 * Check if a client thread already exists for processing this
	 * client. If so, add the index of buffer to
	 * mux_demux_mapping_comm_data
	 */

	check = mux_check_client(sendfd, index);

	/*
	 * If check is "0" it means it's okay to send the data right away
	 * If check is "1", it means the data has been queued and the
	 * dedicated thread will pick up the data and try sending it
	 */

	if (check == 0) {
		ret = send(sendfd, buf + mux_msg_hdr_pack_sz(hdr),
			hdr.mux_msg_size, 0);
		MUX_LOG_ENTRY2("Sent data to client fd : %d ret : %d", sendfd,
			ret);
		ret1 = ret;
		RETRYABLE_ERROR(ret1)
		if ((ret == -1) || ((ret > 0 ) &&
		    (ret != hdr.mux_msg_size))) {
			if ((ret1 == 1) ||
			    ((ret > 0 ) &&
			     (ret != hdr.mux_msg_size))) {
				/*
			 	 * Looks like the send needs to be
				 * retried and we don't know how long
				 * it's going to take for this to
				 * finish. Hence, in order to avoid
				 * blocking other threads just
				 * fork a child process to carry out
				 * the processing for this client
			 	 */
				clnt = (struct clnt_msg_info *)
					malloc(sizeof(struct
					clnt_msg_info));
				clnt->clnt_msg_info_sendfd = sendfd;
				clnt->clnt_msg_info_index = index;
				if ((ret > 0 ) &&
				    (ret != hdr.mux_msg_size)) {
					MUX_LOG_ENTRY1("Couldn't send "
						"the complete data "
						" errno is %d ",
						errno);
					clnt->clnt_msg_info_partial_send
						 = ret;
				} else {
					clnt->clnt_msg_info_partial_send
						 = 0;
				}
thread_create_retry:
				THREAD_CREATE(client, mux_client_thread, clnt,
					ret, threadid);
				if (ret == -1) {
					MUX_LOG_ENTRY0("Unable to "
						"create the "
						"mux_client_thread "
						"thread");
					SLEEP(1);
					goto thread_create_retry;
				}
				MUX_LOG_ENTRY0("mux_process_local_comm: "
					"Created a dedicated "
					"thread ");
				return;
			} else {
				/*
				 * Client connection is probably closed.
				 */
				mux_clear_fd_flags(sendfd,
					MUX_FD_SENDING|MUX_NEW_DATA_ADDED);
				mux_mark_fd_inactive(sendfd, 0, 1);
			}
		} else {
			mux_clear_fd_flags(sendfd,
				MUX_FD_SENDING|MUX_NEW_DATA_ADDED);
		}
		release_local_fd_refcnt(sendfd);
		assert(index != -1);
		mux_reclaimbuf(entry->mux_peer_comm_send_buf, index);
	} else {
		/*
		 * We should not be freeing up the buf till the data
		 * is sent. Freeing will be done once the client thread
		 * is done sending
		 */
		return;
	}
}

int
mux_process_local_comm(eventfd)
	SOCKET	eventfd;
{
	char	bkp_recv_buf[MAX_LEN], c;
	char	*comm_buf = NULL, *buf = NULL, *bufptr = NULL;
	int	ret = 0;
	struct mux_msg_hdr	hdr, hdr1;
	int	len = 0;
	struct mux_handshake_frame	*ptr;
	int	sendlen;
	int	index = 0;
	int	is_first_msg;
	SOCKET	target_fd;
	struct mux_peer_comm_mapping	*entry = NULL;
	char	*uuid;
	struct mux_demux_mapping	*mux_entry = NULL;

	/*
	 * A message from the clients/services
	 */
	if (!is_fd_active(eventfd, 0)) {
		mux_mark_fd_inactive(eventfd, 1, 0);
		return -5;
	}

	/*
	 * It's possible that mux_connect would have timed out and
	 * gone away, let's see if there's anything to read or else
	 * we could close this fd
	 */
	ret = recv(eventfd, (char *)&c, sizeof(char), MSG_PEEK);
	if (ret <= 0) {
		MUX_LOG_ENTRY1("mux_process_local_comm: Client "
			"closed the connection : %d", eventfd);
		mux_mark_fd_inactive(eventfd, 1, 1);
		release_local_fd_refcnt(eventfd);
		return -5;
	}

	/* Check if this is a first message on this fd */
	is_first_msg = get_first_msg_info(eventfd);

	if (!is_first_msg && !get_recv_fd(eventfd)) {
		release_local_fd_refcnt(eventfd);
		return 0;
	}

	target_fd = mux_get_target_fd(eventfd);
	if (target_fd != 0) {
		entry = mux_get_peer_comm_entry(target_fd);
	}

	/*
	 * Do not receive the data from the clients if the window size
	 * of the peer is zero
	 */
	if (entry &&
	     entry->mux_peer_comm_peer_recv_current_window_size == 0) {
		/*
		 * Send a window update message
		 */
		MUX_LOG_ENTRY1("window_update_message : %d ",
			entry->mux_peer_comm_window_update_message_sent);
		if (!entry->mux_peer_comm_window_update_message_sent) {
		     entry->mux_peer_comm_my_recv_current_window_size =
			mux_get_buf_freespace(
				entry->mux_peer_comm_send_buf);
			bufptr = (char *)
				malloc(sizeof(struct mux_msg_hdr));
			memset(bufptr, '\0',
				sizeof(struct mux_msg_hdr));
			memset((char *)&hdr1, '\0', sizeof(hdr1));

			hdr1.mux_msg_window_size =
			entry->mux_peer_comm_my_recv_current_window_size;
			hdr1.mux_msg_flag = MUX_MSG_WINDOW_SIZE_UPDATE;
			hdr1.mux_msg_size = 0;
			sendlen = mux_msg_hdr_pack_sz(hdr1);
			MUX_LOG_ENTRY0("Sending window update "
				"message ");
			entry->mux_peer_comm_window_update_message_sent
				= 1;
			if (mux_send_msg_over_mxt(
			    entry->mux_peer_comm_fd,
			    bufptr, sendlen, 0, &hdr1) < sendlen) {
				MUX_LOG_ENTRY0("Data send failed");
				entry->mux_peer_comm_window_update_message_sent
				= 0;
				mux_remove_peer_comm_mapping(target_fd);
			}

			free(bufptr);
		}

		/*
		 * If window size is zero, we should not send anything.
		 * So we are ignoring the event on this fd. Assumtion
		 * here is that the poll will keep getting event on
		 * this fd for this data and as and when the window on
		 * the peer opens up, the data will be read and sent
		 * to the peer
		 */
		MUX_LOG_ENTRY0("Returning due to window size being zero");
		release_fd_refcnt(target_fd);
		release_local_fd_refcnt(eventfd);
		SLEEP(1);
		return 0;
	}

	MUX_LOG_ENTRY1("Message from clients/services : %d ",
		eventfd);

	if (entry) {
		index = mux_getbuf(entry->mux_peer_comm_recv_buf,
			   &entry->mux_peer_comm_recv_buf_index);

		if (index == -1) {
			release_fd_refcnt(target_fd);
			release_local_fd_refcnt(eventfd);
			return 0;
		} else {
			comm_buf = entry->mux_peer_comm_recv_buf->
					mux_comm_bufptr[index];
			buf = comm_buf;
		}
	} else {
		index = -1;
		buf = bkp_recv_buf;
	}

	memset(buf, '\0', MAX_LEN);

	if (is_first_msg) {
		/*
		 * First receive on this fd, which means it has to be
		 * mux_handshake_frame
		 */
		len = mux_handshake_frame_pack_sz(hdr2);
	} else {
		len = MAX_LEN - mux_msg_hdr_pack_sz(hdr);
	}

	bufptr = buf + mux_msg_hdr_pack_sz(hdr);
	ret = mux_msg_recv(eventfd, bufptr, len, 0);
	if (ret <= 0) {
		MUX_LOG_ENTRY0("mux_process_local_comm: Client Receive"
			" failed ");
		/*
	 	 * We may have to teardown the connection
	 	 * corresponding to this.
	 	 */
		mux_mark_fd_inactive(eventfd, 1, 1);
		ret = -5;
		mux_release_xprtld_connection(target_fd, eventfd);
		goto exit2;
	}

	memset((char *)&hdr, '\0', sizeof(hdr));
	hdr.mux_msg_send_fd = eventfd;
	hdr.mux_msg_recv_fd = get_recv_fd(eventfd);
	hdr.mux_msg_size = ret;

	if (target_fd && !check_fd_validity(target_fd, PEER_DESCRIPTOR)) {
		mux_mark_fd_inactive(eventfd, 1, 1);
		ret = -5;
		mux_release_xprtld_connection(target_fd, eventfd);
		goto exit2;
	}

	/*
	 * If the client already has a connection, we must be having
	 * the mapping already. If the mapping doesn't exist, that means
	 * we need to create one first.
	 */
	if ((hdr.mux_msg_recv_fd == 0) && (is_first_msg)) {
		/*
		 * Following assert guarantees that the buffer allocated
		 * is not from any existing connection over vxt. So, no
		 * need to release the ref count when we go to exit2
		 */
		assert(index == -1);

		/*
		 * First message should always be the registration.
		 */
		ptr = (struct mux_handshake_frame *) bufptr;
		if (ptr->mux_handshake_magic == XPRTLD_MAGIC) {
			/*
			 * This is a xprtlc wanting to connect to the
			 * xprtld in the other domain. So verify that
			 * domain DomU exists with this dest uuid
			 */

			if(strncmp(ptr->mux_handshake_dest_uuid,
			   MUX_BASE_UUID, strlen(MUX_BASE_UUID)) == 0){
				/*
				 * Guests may request a connection do Dom0
				 * by just saying "__base__" as uuid
				 */
				memcpy(ptr->mux_handshake_dest_uuid,
					dom0_uuid, MUX_UUID_SIZE);
			} else if (strncmp(ptr->mux_handshake_dest_uuid,
				    MUX_DEBUG_DATA,
				    strlen(MUX_DEBUG_DATA)) == 0) {
				/*
				 * Request to provide debug log
				 */
				mux_connect_response(eventfd,
					MUX_CONNECT_SUCCESS);
				mux_send_debug_data(eventfd);
				mux_mark_fd_inactive(eventfd, 1, 0);
				ret = -5;
				goto exit2;
			} else if (strncmp(ptr->mux_handshake_dest_uuid,
				    MUX_REGISTERED_GUESTS,
				    strlen(MUX_REGISTERED_GUESTS)) == 0) {
				/*
				 * Request to provide the registered guests
				 */
				mux_connect_response(eventfd,
					MUX_CONNECT_SUCCESS);
				mux_send_registered_guests(eventfd);
				mux_mark_fd_inactive(eventfd, 1, 0);
				ret = -5;
				goto exit2;
			} else if (memcmp(domain_uuid,
					  ptr->mux_handshake_dest_uuid,
					  MUX_UUID_SIZE) == 0) {
				/*
				 * Request for a connection to local xprtld
				 */
				mux_connect_response(eventfd,
					MUX_CONNECT_LOCAL_CONNECT);
				mux_mark_fd_inactive(eventfd, 1, 0);
				ret = -5;
				goto exit2;
			}

			MUX_LOG_ENTRY0("Request to connect to domain:");
			uuid = mux_convert_byteuuid_to_stduuid(
				ptr->mux_handshake_dest_uuid);
			MUX_LOG_ENTRY1("%s", uuid);
			free(uuid);

			target_fd = mux_get_peer_comm_fd(
				ptr->mux_handshake_dest_uuid);
			if (!target_fd) {
				MUX_LOG_ENTRY0("Destination does not"
					" exist");
				mux_connect_response(eventfd,
					MUX_CONNECT_NON_EXISTENT_GUEST);
				mux_mark_fd_inactive(eventfd, 1, 0);
				ret = -5;
				goto exit2;
			}

			entry = mux_get_peer_comm_entry(target_fd);
			if (!entry) {
				MUX_LOG_ENTRY0("Destination does not"
					" exist");
				mux_connect_response(eventfd,
					MUX_CONNECT_NON_EXISTENT_GUEST);
				mux_mark_fd_inactive(eventfd, 1, 0);
				ret = -5;
				goto exit2;
			}

			memcpy(ptr->mux_handshake_src_uuid, domain_uuid,
				MUX_UUID_SIZE);
			/*
			 * Send the connect message to the peer
			 */
			hdr.mux_msg_flag = MUX_MSG_NEW_CONNECTION;
			hdr.mux_msg_send_fd = eventfd;
			mux_entry = mux_get_mux_demux_mapping_entry(eventfd);
			hdr.mux_msg_conn_cookie =
				mux_entry->mux_demux_mapping_conn_cookie;
			hdr.mux_msg_recv_fd = 0;
			hdr.mux_msg_size = 0;
			sendlen = mux_msg_hdr_pack_sz(hdr);
			if (mux_send_msg_over_mxt(target_fd,
			    buf, sendlen, 0, &hdr) < sendlen) {
				MUX_LOG_ENTRY0("mux_process_local_comm: "
					"Incomplete data has been sent"
					" or the sent failed");
				mux_remove_peer_comm_mapping(target_fd);
				mux_connect_response(eventfd,
					MUX_CONNECT_GENERIC_ERROR);
				mux_mark_fd_inactive(eventfd, 1, 0);
				ret = -5;
				release_fd_refcnt(target_fd);
				goto exit2;
			}
			set_first_msg_info(eventfd);
			mux_update_target_fd(eventfd, target_fd);
			release_fd_refcnt(target_fd);
		} else {
			MUX_LOG_ENTRY1("mux_process_local_comm: Invalid "
				"registration string for "
				" client/service: %x ",
				ptr->mux_handshake_magic);
		}
	} else {
		assert(target_fd);
		assert(index != -1);
		/*
		 * Mapping exists on the peer side, so just send the
		 * data over.
		 */
		sendlen = hdr.mux_msg_size +
				mux_msg_hdr_pack_sz(hdr);

		ret = mux_send_msg_over_mxt(target_fd, buf, sendlen,
			0, &hdr);

		if ((ret < sendlen) || (ret == 0)) {
			MUX_LOG_ENTRY1("mux_process_local_comm: "
				" Data send failed %d ", ret);
			mux_remove_peer_comm_mapping(target_fd);
			mux_mark_fd_inactive(eventfd, 1, 0);
			ret = -5;
			goto exit2;
		}
		ret = 0;
		/*
	 	 * Update the window size for every successful send
		 * to the peer
	 	 */
		MUTEX_LOCK(
			entry->mux_peer_comm_my_recv_current_window_size_mutex);
		--entry->mux_peer_comm_peer_recv_current_window_size;
		MUTEX_UNLOCK(
			entry->mux_peer_comm_my_recv_current_window_size_mutex);

	}

exit2:
	if (index != -1) {
		mux_reclaimbuf(entry->mux_peer_comm_recv_buf, index);
		release_fd_refcnt(target_fd);
	}
	release_local_fd_refcnt(eventfd);
	return ret;
}

static void
mux_vxt_comm_poll()
{
	VXT_POLLFD_STRUCT	tmp_array[MAX_CONNECTIONS];
	int			i, j;

	for (;;) {
		/*
		 * Poll for an event on the client descriptor
		 */
		if (poll_threads_exit) {
			return;
		}

		transport_poll(mux_vxt_pollfd, mux_vxt_pollfd_size,
			POLL_TIMEOUT);

		/*
		 * Remove all the fds that have been set to zero
		 */
		j = 0;
		memset((char *)tmp_array, '\0', sizeof(tmp_array));
		MUTEX_LOCK(mux_vxt_pollfd_mutex);
		for (i = 0; i < mux_vxt_pollfd_size; i++) {
			if (mux_vxt_pollfd[i].FD_OBJ > 0) {
				tmp_array[j++] = mux_vxt_pollfd[i];
			}
		}
		memset((char *)mux_vxt_pollfd, '\0', sizeof(mux_vxt_pollfd));
		for (i = 0; i < j; i++) {
			mux_vxt_pollfd[i] = tmp_array[i];
		}
		mux_vxt_pollfd_size = j;
		MUTEX_UNLOCK(mux_vxt_pollfd_mutex);
	}
}

static void
mux_local_comm_poll()
{
	int		count =	1, j = 0;
	struct pollfd	tmp_pollfds[MAX_CONNECTIONS];

	for (;;) {
		/*
		 * Poll for an event on the local clients descriptor
		 */
		if (++count == 100) {
			mux_remove_local_fds();
			count = 0;
		}

		if (poll_threads_exit) {
			return;
		}

		transport_poll_local(mux_local_pollfd, mux_local_pollfd_size,
			POLL_TIMEOUT);

		/*
		 * Clean up the array in case some entries have been removed
		 */
		MUTEX_LOCK(mux_local_pollfd_mutex);
		for (j = 0; j < MAX_CONNECTIONS; j++) {
			tmp_pollfds[j] = mux_local_pollfd[j];
		}
		mux_local_pollfd_size = 0;
		memset(mux_local_pollfd, '\0', sizeof(mux_local_pollfd));
		for (j = 0; j < MAX_CONNECTIONS; j++) {
			if (tmp_pollfds[j].fd > 0) {
				mux_local_pollfd[mux_local_pollfd_size++] =
					tmp_pollfds[j];
			}
		}
		MUTEX_UNLOCK(mux_local_pollfd_mutex);
	}
}

static SOCKET
mux_create_connection_to_xprtld(sendfd, target_fd, error_code, cookie)
	SOCKET				sendfd;
	int				target_fd;
	int				*error_code;
	int				cookie;
{
	SOCKET			sfd;
	struct	mux_auth	auth;
	struct mux_demux_mapping	*entry = NULL;

	/*
	 * See if we have an existing connection
	 */
	sfd = mux_get_xprtld_connection(target_fd);

	if (sfd == INVALID_HANDLE) {
		sfd = mux_connect(XPRTLD_MAGIC, &target_fd, &auth, error_code);
		if (sfd == MUX_CONNECT_GENERIC_ERROR) {
			return MUX_CONNECT_GENERIC_ERROR;
		}
	}

	mux_add_local_fd(sfd);
	MUTEX_LOCK(mux_demux_array_mutex);
	entry = mux_get_mux_demux_mapping_entry(sfd);
	entry->mux_demux_mapping_local_peer_fd = auth.mux_auth_fd;
	entry->mux_demux_mapping_target_fd = target_fd;
	entry->mux_demux_mapping_conn_cookie = cookie;
	MUTEX_UNLOCK(mux_demux_array_mutex);

	mux_update_mapping(sfd, sendfd);
	set_first_msg_info(sfd);
	MUX_LOG_ENTRY0("Connection successful ");

	return sfd;
}

#ifdef NETWORK_ENDPOINT

static void
mux_network_listener()
{
	struct protoent*	tcp;
	SOCKET			listener_fd, sfd;
	struct sockaddr_in	portal;
	SOCKET			newfd;
	char			*bufptr;
	int			sendlen;
	struct mux_handshake_frame	hdr;
	struct mux_msg_hdr		hdr1;
	char				*uuid, *packed_hdr;
	int				ret;

	if (is_dom0 == 1) {
		/* Server mode */
		tcp = getprotobyname("tcp");
		if (tcp == NULL) {
			MUX_LOG_ENTRY0("mux_network_listener: getprotobyname failed");
		}

		listener_fd = socket(PF_INET, SOCK_STREAM, tcp->p_proto);
		if(listener_fd == INVALID_HANDLE){
			MUX_LOG_ENTRY0("Failed creation of tcp socket");
			return;
		}

		if (mux_sock_soreuseaddr(listener_fd) != 0) {
			MUX_LOG_ENTRY0("Unable to set SO_REUSEADDR ");
			return;
		}

		memset((char *) &portal, '\0', sizeof(portal));
		portal = mux_getipa(target_host, connection_port);

		if(bind(listener_fd, (struct sockaddr*)&portal,
		   sizeof(portal)) == SOCKET_ERROR){
			MUX_LOG_ENTRY0("Failed binding socket to IP address");
			MUX_LOG_ENTRY0("Listener thread is exiting");
			CLOSE_SOCKET(listener_fd);
			return;
		}

		for(;;) {
			MUX_LOG_ENTRY0("Listening on the Dom0 ");
			if (listen(listener_fd, MAX_CONNECTIONS) == INVALID_HANDLE) {
				MUX_LOG_ENTRY0("Failed setting socket to"
						" listen");
				return;
			}

			newfd = accept(listener_fd, NULL, NULL);

			if (newfd == -1) {
				MUX_LOG_ENTRY0("Accept failed");
				continue;
			}
			if (mux_disable_nagle(newfd) != 0) {
				MUX_LOG_ENTRY0("Unable to set TCP_NODELAY ");
			}
			mux_add_peer_comm_mapping(newfd, 0);
		}
	} else {
		/* Client mode */
		tcp = getprotobyname("tcp");
		if (tcp == NULL) {
			MUX_LOG_ENTRY0("mux_network_listener: getprotobyname failed"); 
			return;
		}

new_connection:
		MUTEX_LOCK(mux_vxt_connections);
		if (mux_active_vxt_connections > 0) {
			/*
			 * A connection already exists with Dom0, so need of
			 * retrying another connection
			 */
			MUTEX_UNLOCK(mux_vxt_connections);
			SLEEP(5);
			goto new_connection;
		}
		MUTEX_UNLOCK(mux_vxt_connections);

retry:
		MUX_LOG_ENTRY0("Retrying a connection ");

		sfd = socket(PF_INET, SOCK_STREAM, tcp->p_proto);
		if(sfd == INVALID_HANDLE){
			MUX_LOG_ENTRY0("Error creating a tcp socket");
			SLEEP(1);
			goto retry;
		}

		memset((char *)&portal, '\0', sizeof(portal));
		portal = mux_getipa(target_host, connection_port);

		MUX_LOG_ENTRY0("Connecting to the Dom0 ");
		if(connect(sfd, (struct sockaddr *)&portal,
		   sizeof(struct sockaddr_in)) == INVALID_HANDLE){
			MUX_LOG_ENTRY0("Error connecting to server");
			close(sfd);
			SLEEP(1);
			goto retry;
		}

		if (mux_disable_nagle(sfd) != 0) {
			MUX_LOG_ENTRY0("Unable to set TCP_NODELAY ");
			close(sfd);
			SLEEP(1);
			goto retry;
		}

		MUX_LOG_ENTRY0("Adding fd "); 
		mux_add_peer_comm_mapping(sfd, 0);
		MUTEX_LOCK(mux_vxt_connections);
		mux_active_vxt_connections++;
		MUTEX_UNLOCK(mux_vxt_connections);

		/*
		 * Send the handshake message
		 */
		sendlen = mux_handshake_frame_pack_sz(hdr) +
				mux_msg_hdr_pack_sz(hdr1);
		bufptr = (char *) malloc(sendlen);
		memset(bufptr, '\0', sendlen);
		memset((char *)&hdr, '\0', sizeof(hdr));
		memset((char *)&hdr1, '\0', sizeof(hdr1));

		hdr1.mux_msg_flag = MUX_MSG_MXT_CONNECTION_REQ;
		hdr1.mux_msg_send_fd = sfd;
		hdr1.mux_msg_recv_fd = 0;
		hdr1.mux_msg_size = mux_handshake_frame_pack_sz(hdr);

		packed_hdr = bufptr + mux_msg_hdr_pack_sz(hdr1);
		hdr.mux_handshake_magic = MXT_MAGIC;
		memcpy(&hdr.mux_handshake_src_uuid, domain_uuid, MUX_UUID_SIZE);
		strncpy(hdr.mux_handshake_src_endpoint, "MXT", strlen("MXT"));
		hdr.mux_handshake_flags = 0;

		mux_handshake_frame_pack(hdr, packed_hdr);

		ret = mux_send_msg_over_mxt(sfd, bufptr, sendlen, \
			0, &hdr1);

		if (ret < 0) {
			mux_remove_peer_comm_mapping(sfd);
		}
		free(bufptr);
		goto new_connection;
	}
}

#endif /* NETWORK_ENDPOINT */

#ifdef VXT_ENDPOINT

void
mux_vxt_listener()
{
	SOCKET				fd;
	int				sendlen;
	char				*bufptr, *packed_hdr;
	struct mux_handshake_frame	hdr;
	int				ret;
	struct mux_msg_hdr		hdr1;
	struct mux_peer_comm_mapping	*entry = NULL;

	MUX_LOG_ENTRY0("Inside mux_vxt_listener ");

	vxt_cleanup_devices();

	if (is_dom0 == 1) {
		/*
		 * Server mode
		 */
		for (;;) {
			MUX_LOG_ENTRY0("Calling mux_vxt_server");
			mux_vxt_server();
			MUX_LOG_ENTRY0("After Calling mux_vxt_server");
		}
	} else {
		/*
		 * Client mode
		 */

new_connection:

		MUTEX_LOCK(mux_vxt_connections);
		if (mux_active_vxt_connections > 0) {
			/*
			 * A connection already exists with Dom0, so need of
			 * retrying another connection
			 */
			MUTEX_UNLOCK(mux_vxt_connections);
			SLEEP(1);
			goto new_connection;
		}
		MUTEX_UNLOCK(mux_vxt_connections);

retry:
		MUX_LOG_ENTRY0("Retrying a connection ");
		fd = mux_vxt_client();
		if (fd == SOCKET_ERROR) {
			SLEEP(1);
			goto retry;
		}
		mux_add_peer_comm_mapping(fd, 1);
		MUTEX_LOCK(mux_vxt_connections);
		mux_active_vxt_connections++;
		MUTEX_UNLOCK(mux_vxt_connections);

		/*
		 * Send the handshake message
		 */
		sendlen = mux_handshake_frame_pack_sz(hdr) +
				mux_msg_hdr_pack_sz(hdr1);
		bufptr = (char *) malloc(sendlen);
		memset(bufptr, '\0', sendlen);
		memset((char *)&hdr, '\0', sizeof(hdr));
		memset((char *)&hdr1, '\0', sizeof(hdr1));

		hdr1.mux_msg_flag = MUX_MSG_MXT_CONNECTION_REQ;
		hdr1.mux_msg_send_fd = fd;
		hdr1.mux_msg_recv_fd = 0;
		hdr1.mux_msg_size = mux_handshake_frame_pack_sz(hdr);

		packed_hdr = bufptr + mux_msg_hdr_pack_sz(hdr1);
		hdr.mux_handshake_magic = MXT_MAGIC;
		memcpy(&hdr.mux_handshake_src_uuid, domain_uuid, MUX_UUID_SIZE);
		strncpy(hdr.mux_handshake_src_endpoint, "MXT", strlen("MXT"));
		hdr.mux_handshake_flags = 0;

		mux_handshake_frame_pack(hdr, packed_hdr);
		entry = mux_get_peer_comm_entry(fd);
		if (!entry) {
			free(bufptr);
			goto new_connection;
		}
		ret = mux_send_msg_over_mxt(fd, bufptr, sendlen, \
			0, &hdr1);
		if (ret < 0) {
			mux_remove_peer_comm_mapping(fd);
		}
		release_fd_refcnt(fd);
		free(bufptr);
		goto new_connection;
	}
}
#endif /* VXT_ENDPOINT */

char *
mux_get_log_entry()
{
	char	*bufptr;

	/*
	 * If we are here due to a debug entry from mux_connect from xprtlc
	 * the mutex mux_debug_log_counter may not be initialized. So just
	 * return NULL in that case.
	 */
	if (mux_debug_log == NULL) {
		return NULL;
	}

	MUTEX_LOCK(mux_log_counter_mutex);
	if (mux_debug_log_counter == MUX_DEBUG_ENTRIES) {
		mux_debug_log_counter = 0;
	}

	/*
	 * mux_debug_log isn't initialized in case the xprtlc client
	 * connects and hence we shouldn't be adding this entry in the
	 * debug log
	 */
	bufptr = mux_debug_log[mux_debug_log_counter++];
	MUTEX_UNLOCK(mux_log_counter_mutex);
	return bufptr;
}

void
mux_send_debug_data(sendfd)
	SOCKET		sendfd;
{
	int		i;

	for (i = 0; i < MUX_DEBUG_ENTRIES; i++) {
		/*
		 * Ignore the errors, this is just debug data
		 */
		mux_msg_send(sendfd, mux_debug_log[i],
			MUX_DEBUG_MSG_SIZE, 0);
	}
}

void
mux_send_registered_guests(sendfd)
	SOCKET		sendfd;
{
	int				i;
	struct mux_peer_comm_mapping	*entry;
	char				*uuid;

	MUTEX_LOCK(mux_peer_comm_mutex);

	for (i = 0; i < MAX_NUMBER_OF_GUESTS; i++) {
		entry = mux_peer_comm_mappings[i];
		while (entry) {
			if (entry->mux_peer_comm_fd != 0) {
				uuid =
				mux_convert_byteuuid_to_stduuid(
				entry->mux_peer_comm_uuid);
				uuid[MUX_STDUUID_SIZE-2] = '\0';
				uuid[MUX_STDUUID_SIZE-1] = '\n';
				mux_msg_send(sendfd, uuid,
					MUX_STDUUID_SIZE, 0);
				free(uuid);
			}
			entry = entry->mux_peer_comm_mapping_next;
		}
	}
	
	MUTEX_UNLOCK(mux_peer_comm_mutex);
}
