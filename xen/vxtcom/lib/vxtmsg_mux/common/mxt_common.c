/* $Id: mxt_common.c,v 1.38 2009/02/04 00:19:12 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/common/mxt_common.c,v $" */

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
#include <mxt_lib.h>

struct sockaddr_in mux_getipa(const char*, int);
extern int is_running_in_dom0();
extern int mux_respond_to_authorization(SOCKET, struct mux_auth *);
extern int mux_initiate_authorization(SOCKET, struct mux_auth *);
int mux_disable_nagle(SOCKET);
extern char * mux_get_log_entry();
extern struct mux_peer_comm_mapping * mux_get_peer_comm_entry(SOCKET);
extern int mux_msg_send(SOCKET, char *, int, int);
extern int mux_msg_recv(SOCKET, char *, int, int);
extern void mux_sock_nonblock(SOCKET);
extern MUTEX_OBJ mux_peer_comm_mutex;
extern char    *domain_uuid;
extern char    *dom0_uuid;
extern struct mux_err mux_err_msgs[];
extern void release_fd_refcnt(SOCKET);

#define VX_U8_T_SZ	(1)

typedef unsigned char	vx_u8_t;

#define VX_U8_T_PACK(x, p)                                      \
	(((unsigned char *)(p))[0] = (x) & 0xff,                \
	 (p) += VX_U8_T_SZ)

#define VX_U8_T_UNPACK(x, p)                                    \
	((x) = ((vx_u8_t)((unsigned char *)(p))[0]),            \
	 (p) += VX_U8_T_SZ)


MUTEX_OBJ       mux_threads_array_mutex = MUTEX_INITIALIZER;

void
mux_add_to_threadlist(threadid)
	THREAD_T	threadid;
{
	int	i = 0;

	MUTEX_LOCK(mux_threads_array_mutex);
	for (i = 0;i < MAX_THREADS; i++) {
		if (mux_threads_array[i] == 0) {
			mux_threads_array[i] = threadid;
			MUTEX_UNLOCK(mux_threads_array_mutex);
			return;
		}
	}
	assert(0);
}

void
mux_remove_from_threadlist(threadid)
	THREAD_T	threadid;
{
	int	i = 0;

	MUTEX_LOCK(mux_threads_array_mutex);
	for (i = 0;i < MAX_THREADS; i++) {
		if (mux_threads_array[i] == threadid) {
			mux_threads_array[i] = 0;
			MUTEX_UNLOCK(mux_threads_array_mutex);
			return;
		}
	}
	assert(0);
}

/*
 * Connects to the local MXT endpoint.
 * Returns the connect descriptor
 */
SOCKET
mux_connect(service_magic, service_args, auth_arg, error_code)
	int			service_magic;
	void			*service_args;
	void			*auth_arg;
	int			*error_code;
{
	int			ret, retval, index;
	struct sockaddr_in	server;
	char			*uuid;
	struct mux_handshake_frame	handshake;
	struct sockaddr_in	portal;
	char			*destuuid;
	SOCKET			sfd = 0;
	struct mux_auth		newauth;
	struct mux_auth		*auth = (struct mux_auth *)auth_arg;
	struct mux_peer_comm_mapping	*comm_entry = NULL;
	int			*target_fd;
	FILE			*fp = NULL;
	int			port, status = 0;
	STRUCT fd_set		rfds;
	struct timeval		tv;
	struct mux_conn_resp	conn_resp;
	int			error_value = 0;

#ifdef NAMED_PIPE_DOM0
	struct sockaddr_un	sa;
#endif

	if (auth == NULL) {
		auth = &newauth;
	}

	if (error_code) {
		*error_code = 0;
	}

	switch (service_magic) {
		case XPRTLD_MAGIC:
			/*
			 * Connection to local Xprtld daemon
			 */

#ifdef UNIT_TEST
			/*
			 * Try reading the standard test file to find out the
			 * error code that needs to be returned
			 */
			fp = fopen(MUX_CONNECT_TESTFILENAME, "r");
			if (fp == NULL) {
				MUX_LOG_ENTRY2("mux_connect: "
					"Unable to open %s"
					"error code : %d",
					MUX_CONNECT_TESTFILENAME,
					GET_SOCKET_ERROR());
				error_value = GET_SOCKET_ERROR();
				ret = MUX_CONNECT_GENERIC_ERROR;
				fclose(fp);
				goto out1;
			} else {
				if (fscanf(fp, "%d", &status) <= 0) {
					MUX_LOG_ENTRY2("mux_connect: "
						"Unable to read %s"
						"error code : %d",
						SOCKNAME,
						GET_SOCKET_ERROR());
				} else {
					error_value = status;
					ret = MUX_CONNECT_GENERIC_ERROR;
					fclose(fp);
					goto out1;
				}
			}
			
#endif /* UNIT_TEST */

			target_fd = (int *)service_args;

			sfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(sfd == INVALID_HANDLE) {
				MUX_LOG_ENTRY1(
					"mux_connect: "
					"Error creating a tcp socket "
					"error code : %d",
					GET_SOCKET_ERROR());
				error_value = GET_SOCKET_ERROR();
				ret = MUX_CONNECT_GENERIC_ERROR;
				goto out1;
			}

			memset((char *)&portal, '\0', sizeof(portal));
			portal =
				mux_getipa("localhost", XPRTLD_WELL_KNOWN_PORT);

			if(connect(sfd, (struct sockaddr *)&portal,
			   sizeof(struct sockaddr_in)) == INVALID_HANDLE) {
				MUX_LOG_ENTRY1(
					"mux_connect: "
					"Error connecting to server "
					"error code : %d",
					GET_SOCKET_ERROR());
				
				error_value =
					MUX_CONNECT_REMOTE_CONNECTION_FAILED;
				ret = MUX_CONNECT_GENERIC_ERROR;
				goto out1;
			}

			if (mux_disable_nagle(sfd) != 0) {
				MUX_LOG_ENTRY1("mux_connect: "
					"Unable to set TCP_NODELAY "
					"error code : %d",
					GET_SOCKET_ERROR());
				error_value =  GET_SOCKET_ERROR();
				ret = MUX_CONNECT_GENERIC_ERROR;
				goto out1;
			}

			/*
			 * Send the handshake message
			 */
			memset((char *)&handshake, '\0', sizeof(handshake));

			handshake.mux_handshake_magic = XPRTLD_MAGIC;
			strncpy(handshake.mux_handshake_src_endpoint, "MXT",
				strlen("MXT"));
			strncpy(handshake.mux_handshake_dest_endpoint, "XPRT",
				strlen("XPRT"));

			comm_entry = mux_get_peer_comm_entry(*target_fd);
			if (!comm_entry) {
				MUX_LOG_ENTRY0("mux_connect: "
					"No vxt connection to this guest");
				error_value = MUX_CONNECT_NON_EXISTENT_GUEST;
				ret = MUX_CONNECT_GENERIC_ERROR;
				goto out1;
			}

			memcpy(&handshake.mux_handshake_dest_uuid,
				comm_entry->mux_peer_comm_uuid,
				MUX_UUID_SIZE);
			release_fd_refcnt(*target_fd);

			handshake.mux_handshake_flags = 0;
			handshake.mux_handshake_frame_len =
				sizeof(struct mux_handshake_frame);

			MUX_LOG_ENTRY0("Sending handshake message to XPRTLD");

			ret = mux_msg_send(sfd, (char *)&handshake,
				sizeof(struct mux_handshake_frame), 0);
			if (ret < 0) {
				MUX_LOG_ENTRY0("Message send failed");
				error_value =  GET_SOCKET_ERROR();
				ret = MUX_CONNECT_GENERIC_ERROR;
				goto out1;
			}

			if (mux_respond_to_authorization(sfd, auth) < 0) {
				MUX_LOG_ENTRY0("Authorization failed");
				error_value = MUX_CONNECT_REMOTE_AUTH_FAILURE;
				ret = MUX_CONNECT_GENERIC_ERROR;
				goto out1;
			}

			MUX_LOG_ENTRY1("Returning fd %d", sfd);
			break;

out1:
			if (error_code) {
				*error_code = error_value;
			}
			if ((sfd != INVALID_HANDLE) && (sfd != 0))  {
				CLOSE_SOCKET(sfd);
			}
			return ret;

		case MXT_MAGIC:
			/*
			 * Connection to local MXT daemon
			 */
			destuuid = (char *)service_args;

#ifdef UNIT_TEST
			/*
			 * Try reading the standard test file to find out the
			 * error code that needs to be returned
			 */
			fp = fopen(MUX_CONNECT_TESTFILENAME, "r");
			if (fp == NULL) {
				MUX_LOG_ENTRY2("mux_connect: "
					"Unable to open %s"
					"error code : %d",
					MUX_CONNECT_TESTFILENAME,
					GET_SOCKET_ERROR());
				error_value = GET_SOCKET_ERROR();
				ret = MUX_CONNECT_GENERIC_ERROR;
				fclose(fp);
				goto out2;
			} else {
				if (fscanf(fp, "%d", &status) <= 0) {
					MUX_LOG_ENTRY2("mux_connect: "
						"Unable to read %s"
						"error code : %d",
						SOCKNAME,
						GET_SOCKET_ERROR());
					fclose(fp);
				} else {
					error_value = status;
					ret = MUX_CONNECT_GENERIC_ERROR;
					fclose(fp);
					goto out2;
				}
			}
			
#endif /* UNIT_TEST */

#ifdef NAMED_PIPE_DOM0
			if (is_running_in_dom0()) {
				/*
				 * Use the unix domain socket to connect
				 */
				sa.sun_family = AF_UNIX;
				strncpy(sa.sun_path, SOCKNAME,
					strlen(SOCKNAME));
				sa.sun_path[strlen(SOCKNAME)] = '\0';

				sfd = socket(AF_UNIX, SOCK_STREAM, 0);

				if (sfd == INVALID_HANDLE) {
					MUX_LOG_ENTRY1("mux_connect: "
					"Error creating a unix domain socket "
					"error code : %d",
					GET_SOCKET_ERROR());

					error_value = GET_SOCKET_ERROR();
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}
				if (connect(sfd, (const struct sockaddr *) &sa,
				    sizeof(struct sockaddr_un))
				    == INVALID_HANDLE) {
					MUX_LOG_ENTRY1("mux_connect : "
						"Error connecting to server"
						"error code : %d",
						GET_SOCKET_ERROR());

					error_value = GET_SOCKET_ERROR();
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}
			} else {
#endif /* NAMED_PIPE_DOM0 */
				INIT_WSA();
				GET_SOCKNAME();
				fp = fopen(SOCKNAME, "r");
				if (fp == NULL) {
					MUX_LOG_ENTRY2("mux_connect: "
						"Unable to open %s"
						"error code : %d",
						SOCKNAME,
						GET_SOCKET_ERROR());
					error_value = GET_SOCKET_ERROR();
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}

				if (fscanf(fp, "%d", &port) <= 0) {
					MUX_LOG_ENTRY2("mux_connect: "
						"Unable to read %s"
						"error code : %d",
						SOCKNAME,
						GET_SOCKET_ERROR());

					error_value = GET_SOCKET_ERROR();
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}

				MUX_LOG_ENTRY1("Connecting to port : %d", port);
				sfd = socket(PF_INET, SOCK_STREAM,
					IPPROTO_TCP);
				if(sfd == INVALID_HANDLE) {
					MUX_LOG_ENTRY1("mux_connect: "
					"Error creating a tcp socket"
					"error code : %d",
					GET_SOCKET_ERROR());

					error_value = GET_SOCKET_ERROR();
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}

				memset((char *)&server, '\0',
					sizeof(server));
				server =
				mux_getipa("localhost", port);

				if(connect(sfd, (struct sockaddr *)&server,
				   sizeof(server)) == INVALID_HANDLE) {
					MUX_LOG_ENTRY1("mux_connect : "
						"Error connecting to server"
						" error code : %d",
						GET_SOCKET_ERROR());

					error_value = GET_SOCKET_ERROR();
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}

				if (mux_disable_nagle(sfd) != 0) {
					MUX_LOG_ENTRY1("mux_connect: "
						"Unable to set TCP_NODELAY"
						"error code : %d",
						GET_SOCKET_ERROR());

					error_value = GET_SOCKET_ERROR();
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}

				if (mux_respond_to_authorization(sfd, auth)
				    < 0) {
					index = -MUX_CONNECT_LOCAL_AUTH_FAILURE;
					error_value =
						MUX_CONNECT_LOCAL_AUTH_FAILURE;
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}
#ifdef	NAMED_PIPE_DOM0
			}
#endif /* NAMED_PIPE_DOM0 */
			/*
			 * Send the register message for this client.
			 */
			memset(&handshake, '\0', sizeof (handshake));
			handshake.mux_handshake_magic = XPRTLD_MAGIC;
			handshake.mux_handshake_frame_len =
				sizeof(struct mux_handshake_frame);
			handshake.mux_handshake_flags = 0;

			memcpy(handshake.mux_handshake_src_endpoint, "XPRTLD",
				strlen("XPRTLD"));

			/*
			 * Special types in destuuid are being used to
			 * provide  transparent access to Dom0 and debug
			 * messages
			 */
			if((strncmp(destuuid, MUX_BASE_UUID,
			    strlen(MUX_BASE_UUID)) != 0) &&
			   (strncmp(destuuid, MUX_DEBUG_DATA,
			    strlen(MUX_DEBUG_DATA)) != 0) &&
			   (strncmp(destuuid, MUX_REGISTERED_GUESTS,
			    strlen(MUX_REGISTERED_GUESTS)) != 0)) {
				uuid =
				mux_convert_stduuid_to_byteuuid(destuuid);
				memcpy(handshake.mux_handshake_dest_uuid, uuid,
					MUX_UUID_SIZE);
			}else {
				if (((strncmp(destuuid, MUX_BASE_UUID,
				      strlen(MUX_BASE_UUID)) == 0) &&
				      (strlen(destuuid) ==
				      strlen(MUX_BASE_UUID))) ||
				    ((strncmp(destuuid, MUX_DEBUG_DATA,
				      strlen(MUX_DEBUG_DATA)) == 0) &&
				      (strlen(destuuid) ==
				      strlen(MUX_DEBUG_DATA))) ||
				    ((strncmp(destuuid, MUX_REGISTERED_GUESTS,
				      strlen(MUX_REGISTERED_GUESTS)) == 0) &&
				      (strlen(destuuid) ==
				      strlen(MUX_REGISTERED_GUESTS)))) {
					memcpy(
					handshake.mux_handshake_dest_uuid,
					destuuid, MUX_UUID_SIZE);
				} else {
					index = -MUX_CONNECT_NON_EXISTENT_GUEST;

					error_value =
					MUX_CONNECT_NON_EXISTENT_GUEST;
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}
			}
			memcpy(handshake.mux_handshake_dest_endpoint, "XPRTLC",
				strlen("XPRTLC"));

			ret = send(sfd, (char *)&handshake,
					sizeof(handshake), 0);
			if (ret <= 0) {
				MUX_LOG_ENTRY1("Data send failed : %d ",
					GET_SOCKET_ERROR());

				error_value = GET_SOCKET_ERROR();
				ret = MUX_CONNECT_GENERIC_ERROR;
				goto out2;
			}

			/*
			 * Wait for the response from the MXT daemon, which will
			 * let us know if the connection on the remote site
			 * could be established
			 */
			FD_ZERO(&rfds);
			FD_SET(sfd, &rfds);

			/*
			 * let's wait for five seconds for the time being
			 */
			tv.tv_sec = 5;
			tv.tv_usec = 0;

			ret = select(sfd + 1, &rfds, NULL, NULL, &tv);

			if ((ret == -1) || (ret == 0)) {
				error_value = MUX_CONNECT_REQUEST_TIMEOUT;
				ret = MUX_CONNECT_GENERIC_ERROR;
				goto out2;
			} else if ((ret) && (FD_ISSET(sfd, &rfds)))  {
				retval = recv(sfd, (char *)&conn_resp,
						sizeof(conn_resp), 0);
				if (retval != sizeof(conn_resp)) {
					error_value = GET_SOCKET_ERROR();
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}
				status = conn_resp.mux_conn_resp_status;
				if (status != MUX_CONNECT_SUCCESS) {
					if ((status < MUX_CONNECT_GENERIC_ERROR)
					     && (status >= (MUX_MAX_ERROR_CODE+1))) {
						index = -status;
					}

					error_value = status;
					ret = MUX_CONNECT_GENERIC_ERROR;
					goto out2;
				}
			} else {
				error_value = MUX_CONNECT_REQUEST_TIMEOUT;
				ret = MUX_CONNECT_GENERIC_ERROR;
				goto out2;
			}

			break;

out2:
			if (error_code) {
				*error_code = error_value;
			}
			if ((sfd != INVALID_HANDLE) && (sfd != 0))  {
				CLOSE_SOCKET(sfd);
			}
			if (fp) {
				fclose(fp);
			}
			return ret;

		default:
			MUX_LOG_ENTRY0("mux_connect: Invalid service requested ");
			return MUX_CONNECT_INVALID_SERVICE;
	}

	if (fp) {
		fclose(fp);
	}
	return sfd;
}

/*
 * Response to mux_connect() request
 */
void
mux_connect_response(sendfd, error_code)
	SOCKET		sendfd;
	int		error_code;
{
	struct mux_conn_resp	conn_resp;
	int			ret;

	conn_resp.mux_conn_resp_status = error_code;

	ret = send(sendfd, (char *)&conn_resp, sizeof(conn_resp), 0);
	if (ret <= 0) {
		MUX_LOG_ENTRY1("Mux_connect_response send failed: %d ",
				ret);
	}
}

/*
 * Reads the data from the recvfd
 */
int
mux_read(recvfd, recvbuf, recvsize, recvflags)
	SOCKET	recvfd;
	char	*recvbuf;
	int	recvsize;
	int	recvflags;
{
	return (recv(recvfd, recvbuf, recvsize, recvflags));
}

/*
 * Writes the data to sendfd
 */
int
mux_write(sendfd, sendbuf, sendsize, sendflags)
	SOCKET	sendfd;
	char	*sendbuf;
	int	sendsize;
	int	sendflags;
{
	return (send(sendfd, sendbuf, sendsize, sendflags));
}

/*
 * Identity of the peer is verified in this function.
 * Returns 0 if verification is successful, 1 otherwise
 */
int
mux_accept(fd, handshake_frame, auth_arg)
	SOCKET				fd;
	struct mux_handshake_frame	*handshake_frame;
	void				*auth_arg;
{
	struct mux_auth			newauth;
	struct mux_auth			*auth = (struct mux_auth *)auth_arg;
	int				ret = 0;
	int				magic;
	int				retryCount = 0;
#ifdef UNIT_TEST
	FILE				*fp;
	int				status;

	/*
	 * Try reading the standard test file to find out the
	 * error code that needs to be returned
	 */
	fp = fopen(MUX_ACCEPT_TESTFILENAME, "r");
	if (fp == NULL) {
		MUX_LOG_ENTRY2("mux_connect: "
			"Unable to open %s"
			"error code : %d",
			MUX_ACCEPT_TESTFILENAME,
			GET_SOCKET_ERROR());
	} else {
		if (fscanf(fp, "%d", &status) <= 0) {
			MUX_LOG_ENTRY2("mux_connect: "
				"Unable to read %s"
				"error code : %d",
				SOCKNAME,
				GET_SOCKET_ERROR());
		} else {
			return status;
		}
	}
			
#endif /* UNIT_TEST */

	/*
	 * Verify the handshake message
	 */
	mux_sock_nonblock(fd);

	if (handshake_frame) {
retry1:
		ret = recv(fd, (char *)&magic, sizeof(int), MSG_PEEK);
		if (ret != sizeof(int)){
			RETRYABLE_ERROR(ret);
			SLEEP(1);
			if ((ret ==  1) && (retryCount++ < 5)) {
				goto retry1;
			}
			MUX_LOG_ENTRY0("No matching header found");
			return MUX_ACCEPT_GENERIC_ERROR;
		}
        
		if (magic == XPRTLD_MAGIC) {
			MUX_LOG_ENTRY0("XPRTLD magic found");
		} else {
			MUX_LOG_ENTRY2("Magic expected : %x actual : %x",
					XPRTLD_MAGIC, magic)
			return MUX_ACCEPT_MAGIC_FAILURE;
		}

retry2:
		ret = mux_msg_recv(fd, (char *)handshake_frame,
			sizeof(struct mux_handshake_frame), 0);

		MUX_LOG_ENTRY2("errno : %d ret : %d", errno, ret);
		if ((ret != sizeof(struct mux_handshake_frame)) &&
		    ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
			SLEEP(1);
			goto retry2;
		}
	}

	ret = 0;
	if (auth == NULL) {
		auth = &newauth;
	}
	ret = mux_initiate_authorization(fd, auth);
	if (ret < 0) {
		ret = MUX_ACCEPT_AUTHORIZATION_FAILED;
	}
	return ret;
}

struct
sockaddr_in mux_getipa(hostname, port)
	const char	*hostname;
        int             port;
{
	struct sockaddr_in	ipa;
	struct hostent*		localhost;
	char			*addr;

	ipa.sin_family = AF_INET;
	ipa.sin_port = htons((unsigned short)port);
	if (strncmp(hostname, "localhost", strlen(hostname)) == 0) {
		ipa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	} else {
		localhost = gethostbyname(hostname);
		if(!localhost){
			MUX_LOG_ENTRY0("resolving localhost");
			return ipa;
        	}
		addr = localhost->h_addr_list[0];
		memcpy(&ipa.sin_addr.s_addr, addr, sizeof addr);
	}

	return ipa;
}

int
mux_disable_nagle(socketfd)
        SOCKET	socketfd;
{
	int	on = 1;
	int	ret = 0;

	ret = SETSOCKOPT(socketfd);

	if (ret != 0) {
		MUX_LOG_ENTRY0("setsockopt failed for TCP_NODELAY");
		return -1;
	}
	return 0;
}

int
mux_sock_soreuseaddr(socketfd)
	SOCKET	socketfd;
{
	int	on = 1;
	int	ret = 0;

	ret = SETSOCKOPT(socketfd);

	if (ret != 0) {
		MUX_LOG_ENTRY0("setsockopt failed for SO_REUSEADDR");
		return -1;
	}

	return 0;
}

void
mux_buffer_pack(x, size, p)
	char	*x;
	int	size;
	char	**p;
{
	int offset;

	for (offset = 0; offset < size; offset++) {
		VX_U8_T_PACK(x[offset], *p);
	}
}

void
mux_buffer_unpack(x, size, p)
	char	*x;
	int	size;
	char	**p;
{
	int offset;

	for (offset = 0; offset < size; offset++) {
		VX_U8_T_UNPACK(x[offset], *p);
	}
}

/*
 * Create some connections to xprltd which we could use to serve the client
 * requests
 */
void
mux_create_xprtld_connections(fd)
	SOCKET		fd;
{
	struct mux_peer_comm_mapping	*entry = NULL;
	int			i = 0;
	struct  mux_auth	auth;
	int			targetfd;
	int			error_code;

	/*
	 * Disabling this code until the xprtld connection timeout issue is sorted out
	 */
	return;

	entry = mux_get_peer_comm_entry(fd);
	if (!entry) {
		return;
	}

	MUTEX_LOCK(mux_peer_comm_mutex);
	for (i = 0; i < MAX_XPRTLD_CONNECTIONS; i++) {
		if (entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_fd
		    != 0) {
			continue;
		}

		targetfd = entry->mux_peer_comm_fd;
		MUTEX_UNLOCK(mux_peer_comm_mutex);
		fd = mux_connect(XPRTLD_MAGIC, &targetfd, &auth, &error_code);
		if (fd <= MUX_CONNECT_GENERIC_ERROR) {
			MUX_LOG_ENTRY0("mux_create_xprtld_connections: connect"
					" failed");
			goto exit;
		} else {
			MUX_LOG_ENTRY0("connect succeeded");
		}
		MUTEX_LOCK(mux_peer_comm_mutex);
		entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_fd = fd;
		entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_alloted
			= 0;
	}

exit:
	MUTEX_UNLOCK(mux_peer_comm_mutex);
	release_fd_refcnt(fd);

	return;
}

/*
 * Get an existing connection to xprtld
 */
int
mux_get_xprtld_connection(fd)
	SOCKET		fd;
{
	struct mux_peer_comm_mapping	*entry = NULL;
	int			i = 0;
	int			sfd;

	/*
	 * Disabling this code until the xprtld connection timeout issue is sorted out
	 */
	goto out;

	entry = mux_get_peer_comm_entry(fd);
	if (!entry) {
		return -1;
	}

	MUTEX_LOCK(mux_peer_comm_mutex);
	for (i = 0; i < MAX_XPRTLD_CONNECTIONS; i++) {
		if ((entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_alloted
		    == 0) && (entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_fd > 0)) {
			entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_alloted = 1;
			sfd = entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_fd;
			MUTEX_UNLOCK(mux_peer_comm_mutex);
			return sfd;
		}
	}

	MUTEX_UNLOCK(mux_peer_comm_mutex);
	release_fd_refcnt(fd);

out:
	return -1;
}

/*
 * Release an existing connection to xprtld
 */
void
mux_release_xprtld_connection(vxtfd, localfd)
	SOCKET		vxtfd;
	SOCKET		localfd;
{
	struct mux_peer_comm_mapping	*entry = NULL;
	int				i = 0;

	/*
	 * Disabling this code until the xprtld connection timeout issue is sorted out
	 */
	return;

	entry = mux_get_peer_comm_entry(vxtfd);
	if (!entry) {
		return;
	}

	MUTEX_LOCK(mux_peer_comm_mutex);
	for (i = 0; i < MAX_XPRTLD_CONNECTIONS; i++) {
		if (entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_fd
		    == localfd) {
			entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_alloted = -1;
			entry->mux_peer_comm_xprtld_conns[i].mux_xprtld_conn_fd = 0;
			break;
		}
	}

	MUTEX_UNLOCK(mux_peer_comm_mutex);
	release_fd_refcnt(vxtfd);

	mux_create_xprtld_connections(vxtfd);

	return;
}

/*
 * mux_connect_cleanup()
 * This is required for windows platform and calls WSACLAENUP macro. 
 * It is empty for Linux and WSACleanup() for Windows.
 */ 
void mux_connect_cleanup()
{
	WSACLEANUP;
	return;
}

/*
 * Enables logging the debug messages to a file for which a file pointer
 * has been passed by the caller
 */
void mux_enable_tracing(fp)
	FILE	*fp;
{
	mux_trace_fp = fp;
}
