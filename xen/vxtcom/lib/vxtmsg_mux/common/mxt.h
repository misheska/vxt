/* $Id: mxt.h,v 1.21 2009/02/04 00:19:12 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/common/mxt.h,v $" */

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
#include <assert.h>
#include <mxt_platform.h>
#include <mxt_common.h>
#include <mxt_lib.h>

struct mux_msg_hdr;
struct mux_comm_buf;

#ifdef GC_DEF

static void mux_garbage_collection();

#endif /* GC_DEF */

#ifdef NETWORK_ENDPOINT

static void mux_network_listener();

#endif /* NETWORK_ENDPOINT */

static SOCKET mux_get_target_fd(SOCKET);
void mux_signal_handler(int);
int mux_initiate_authorization(SOCKET, struct mux_auth *);
int mux_respond_to_authorization(SOCKET, struct mux_auth *);
struct sockaddr_in mux_getipa(const char*, int);

#ifdef VXT_ENDPOINT

void mux_vxt_listener();

#endif /* VXT_ENDPOINT */

int mux_msg_send(SOCKET, char *, int, int);
int mux_msg_recv(SOCKET, char *, int, int);
int mux_send_msg_over_mxt(SOCKET, char *, int, int, struct mux_msg_hdr *);
int mux_recv_msg_over_mxt(SOCKET, char *, size_t, int);
static struct mux_demux_mapping * mux_get_mux_demux_mapping_entry(SOCKET);
void mux_update_peer_comm_mapping(char *, SOCKET);
int mux_add_peer_comm_mapping(SOCKET, int);
void mux_remove_peer_comm_mapping(SOCKET);
static void mux_add_local_fd(SOCKET);
static void mux_remove_local_fds();
void mux_buffer_pack(char *, int, char **);
void mux_buffer_unpack(char *, int, char **);
int is_running_in_dom0();
void mux_add_to_threadlist(THREAD_T);
void mux_remove_from_threadlist(THREAD_T);
void mux_reclaimbuf(struct mux_comm_buf*, int);
int mux_getbuf(struct mux_comm_buf*, int*);
int mux_get_buf_freespace(struct mux_comm_buf *);
int mux_sock_soreuseaddr(SOCKET);
char * mux_get_log_entry();
static void mux_mark_fd_inactive(SOCKET, int, int);
extern void mux_create_xprtld_connections(SOCKET);
extern int mux_get_xprtld_connection(SOCKET);
extern void mux_release_xprtld_connection(SOCKET, SOCKET);
void mux_remove_fd_from_poll(SOCKET, int);
extern void mux_sock_blocking(SOCKET);
void mux_send_debug_data(SOCKET);
extern char* mux_get_my_uuid();
extern char* mux_convert_byteuuid_to_stduuid(char *);
extern void mux_enable_tracing(FILE *);
void mux_send_registered_guests(SOCKET);
extern int mux_get_conn_cookie();

#define MUX_FD_HASH(fd, SIZE)   (unsigned int)(fd % SIZE)

/*
 * Listerner thread is responsible for accepting connections and making
 * corresponding connections on the peer side.
 */
THREAD_T listener1, listener2, listener3;

/*
 * Processor thread is responsible for processing the data received
 * from clients/servers and multiplex/demultiplex the data to channel
 * through the VXT message device
 */
THREAD_T processor1, processor2, processor3;


/*
 *
 */
#define MAX_SERVICES		255
#define POLL_TIMEOUT		1000
#define MUX_FD_SENDING		0x00000001
#define MUX_NEW_DATA_ADDED	0x00000010

/*
 * Portal where clients and services can connect and register
 */

#define SERVICE_MAGIC	(('S' << 24) | \
			 ('E' << 16) | \
			 ('R' <<  8) | \
			 ('V'))

#define CLIENT_MAGIC	(('C' << 24) | \
			 ('L' << 16) | \
			 ('N' <<  8) | \
			 ('T'))

struct service_identifier {
		int			service_magic;
		char			service_name[MAX_SERVICE_NAME_LEN];
		struct sockaddr_in	service_portal;
};

/*
 * Message types
 */
#define MUX_MSG_FD_DISCONNECT		0x00000001
#define MUX_MSG_NEW_CONNECTION		0x00000002
#define MUX_MSG_WINDOW_SIZE_UPDATE	0x00000004
#define MUX_MSG_WINDOW_SIZE_UPDATE_ACK	0x00000008
#define MUX_MSG_NEW_CONNECTION_ACK	0x00000010
#define MUX_MSG_MXT_CONNECTION_REQ	0x00000020
#define MUX_MSG_MXT_CONNECTION_ACK	0x00000040

struct mux_msg_hdr {
	SOCKET	mux_msg_send_fd;
	SOCKET	mux_msg_recv_fd;
	int	mux_msg_size;
	int	mux_msg_window_size;
	int	mux_msg_flag;
	int	mux_msg_error_code;
	int	mux_msg_conn_cookie;
	long long	mux_msg_seqno;
};

#define OFFSET  (int)(&((struct mux_msg_hdr *)0)->mux_msg_data)

struct clnt_msg_info {
	SOCKET	clnt_msg_info_sendfd;
	int	clnt_msg_info_index;
	int	clnt_msg_info_partial_send;
};

#define PEER_DESCRIPTOR		1
#define LOCAL_DESCRIPTOR	0

#define VX_U32_T_SZ		4
#define VX_U64_T_SZ		8

#define VX_U32_T_PACK_SZ(x)     (4)
#define VX_U64_T_PACK_SZ(x)     (8)

#define vx_u32_t	unsigned int
#define vx_u64_t	unsigned long long

#define VX_U32_T_PACK(x, p)                                     	\
	(((unsigned char *)(p))[0] = (unsigned char)((x) >> 24) & 0xff,	\
	 ((unsigned char *)(p))[1] = (unsigned char)((x) >> 16) & 0xff,	\
	 ((unsigned char *)(p))[2] = (unsigned char)((x) >> 8) & 0xff,	\
	 ((unsigned char *)(p))[3] = (unsigned char)(x) & 0xff,		\
	 (p) += VX_U32_T_SZ)

#define VX_U32_T_UNPACK(x, p)                                   \
	((x) = (((vx_u32_t)((unsigned char *)(p))[0] << 24) |   \
		((vx_u32_t)((unsigned char *)(p))[1] << 16) |   \
		((vx_u32_t)((unsigned char *)(p))[2] << 8)  |   \
		((vx_u32_t)((unsigned char *)(p))[3])),         \
	 (p) += VX_U32_T_SZ)

#define VX_U64_T_PACK(x, p)                                     	  \
	(((unsigned char *)(p))[0] = (unsigned char)((x) >> 56LL) & 0xff, \
	 ((unsigned char *)(p))[1] = (unsigned char)((x) >> 48LL) & 0xff, \
	 ((unsigned char *)(p))[2] = (unsigned char)((x) >> 40LL) & 0xff, \
	 ((unsigned char *)(p))[3] = (unsigned char)((x) >> 32LL) & 0xff, \
	 ((unsigned char *)(p))[4] = (unsigned char)((x) >> 24) & 0xff,	  \
	 ((unsigned char *)(p))[5] = (unsigned char)((x) >> 16) & 0xff,	  \
	 ((unsigned char *)(p))[6] = (unsigned char)((x) >> 8) & 0xff,	  \
	 ((unsigned char *)(p))[7] = (unsigned char)(x) & 0xff,		  \
	 (p) += VX_U64_T_SZ)

#define VX_U64_T_UNPACK(x, p)                                   \
	((x) = (((vx_u64_t)((unsigned char *)(p))[0] << 56LL) | \
		((vx_u64_t)((unsigned char *)(p))[1] << 48LL) | \
		((vx_u64_t)((unsigned char *)(p))[2] << 40LL) | \
		((vx_u64_t)((unsigned char *)(p))[3] << 32LL) | \
		((vx_u64_t)((unsigned char *)(p))[4] << 24) |   \
		((vx_u64_t)((unsigned char *)(p))[5] << 16) |   \
		((vx_u64_t)((unsigned char *)(p))[6] << 8)  |   \
		((vx_u64_t)((unsigned char *)(p))[7])),         \
	 (p) += VX_U64_T_SZ)



#define mux_msg_hdr_pack_sz(x)				\
	(VX_U32_T_PACK_SZ((x).mux_msg_send_fd) + 	\
	 VX_U32_T_PACK_SZ((x).mux_msg_recv_fd) + 	\
	 VX_U32_T_PACK_SZ((x).mux_msg_size) + 		\
	 VX_U32_T_PACK_SZ((x).mux_msg_window_size) + 	\
	 VX_U32_T_PACK_SZ((x).mux_msg_flag) + 		\
	 VX_U32_T_PACK_SZ((x).mux_msg_error_code) + 	\
	 VX_U32_T_PACK_SZ((x).mux_msg_conn_cookie) + 	\
	 VX_U64_T_PACK_SZ((x).mux_msg_seqno))

#define mux_msg_hdr_unpack_sz(x)				\
	mux_msg_hdr_pack_sz(x)

#define mux_msg_hdr_pack(x, p)				\
	VX_U32_T_PACK((x).mux_msg_send_fd, p);		\
	VX_U32_T_PACK((x).mux_msg_recv_fd, p);		\
	VX_U32_T_PACK((x).mux_msg_size, p);		\
	VX_U32_T_PACK((x).mux_msg_window_size, p);	\
	VX_U32_T_PACK((x).mux_msg_flag, p);		\
	VX_U32_T_PACK((x).mux_msg_error_code, p);	\
	VX_U32_T_PACK((x).mux_msg_conn_cookie, p);	\
	VX_U64_T_PACK((x).mux_msg_seqno, p)

#define mux_msg_hdr_unpack(x, p)				\
	VX_U32_T_UNPACK((x).mux_msg_send_fd, p);	\
	VX_U32_T_UNPACK((x).mux_msg_recv_fd, p);	\
	VX_U32_T_UNPACK((x).mux_msg_size, p);		\
	VX_U32_T_UNPACK((x).mux_msg_window_size, p);	\
	VX_U32_T_UNPACK((x).mux_msg_flag, p);		\
	VX_U32_T_UNPACK((x).mux_msg_error_code, p);	\
	VX_U32_T_UNPACK((x).mux_msg_conn_cookie, p);	\
	VX_U64_T_UNPACK((x).mux_msg_seqno, p)

#define buffer_pack_sz(x, n)	(n)
#define buffer_pack(x, n, p)	mux_buffer_pack(x, n, &p)
#define buffer_unpack(x, n, p)	mux_buffer_unpack(x, n, &p)

#define mux_handshake_frame_pack_sz(x)					\
	(VX_U32_T_PACK_SZ((x).mux_handshake_magic) + 			\
	 VX_U32_T_PACK_SZ((x).mux_handshake_frame_len) +		\
	 VX_U32_T_PACK_SZ((x).mux_handshake_flags) +			\
	 buffer_pack_sz((x).mux_handshake_src_uuid, MUX_UUID_SIZE) +	\
	 buffer_pack_sz((x).mux_handshake_src_endpoint, MUX_UUID_SIZE) +	\
	 buffer_pack_sz((x).mux_handshake_dest_uuid, MUX_UUID_SIZE) +	\
	 buffer_pack_sz((x).mux_handshake_dest_endpoint, MUX_UUID_SIZE))

#define mux_handshake_frame_unpack_sz(x)	\
	mux_handshake_frame_pack_sz(x)

#define mux_handshake_frame_pack(x, p)					\
	VX_U32_T_PACK((x).mux_handshake_magic, p);			\
	VX_U32_T_PACK((x).mux_handshake_frame_len, p);			\
	VX_U32_T_PACK((x).mux_handshake_flags, p);			\
	buffer_pack((x).mux_handshake_src_uuid, MUX_UUID_SIZE, p);	\
	buffer_pack((x).mux_handshake_src_endpoint, MUX_UUID_SIZE, p);	\
	buffer_pack((x).mux_handshake_dest_uuid, MUX_UUID_SIZE, p);	\
	buffer_pack((x).mux_handshake_dest_endpoint, MUX_UUID_SIZE, p)

#define mux_handshake_frame_unpack(x, p)				\
	VX_U32_T_UNPACK((x).mux_handshake_magic, p);			\
	VX_U32_T_UNPACK((x).mux_handshake_frame_len, p);		\
	VX_U32_T_UNPACK((x).mux_handshake_flags, p);			\
	buffer_unpack((x).mux_handshake_src_uuid, MUX_UUID_SIZE, p);	\
	buffer_unpack((x).mux_handshake_src_endpoint, MUX_UUID_SIZE, p);\
	buffer_unpack((x).mux_handshake_dest_uuid, MUX_UUID_SIZE, p);	\
	buffer_unpack((x).mux_handshake_dest_endpoint, MUX_UUID_SIZE, p)



extern struct sockaddr_in mux_getipa(const char*, int);
extern int mux_disable_nagle(SOCKET);
extern SOCKET mux_connect(int, void *, void *, int *);
extern void mux_connect_response(SOCKET, int);
extern int mux_read(SOCKET, char *, int, int);
extern int mux_write(SOCKET, char *, int, int);
extern int mux_accept(SOCKET, struct mux_handshake_frame *, void *);
extern void mux_set_sig_handler();
extern void vxt_cleanup_devices();
extern void vxt_scan_and_connect_devices(SOCKET);
extern int mux_vxt_server();
extern int mux_vxt_client();
extern void mux_close_vxt_sockets();
extern void transport_poll(VXT_POLLFD_STRUCT *, int, int);
extern void transport_poll_local(struct pollfd *, int, int);
extern void mux_buffer_pack(char *, int, char **);
extern void mux_buffer_unpack(char *, int, char **);
void mux_close_socket(SOCKET, int);
extern void test1();



static void mux_listener();
static void mux_update_mapping(SOCKET, SOCKET);
static SOCKET mux_create_connection_to_xprtld(SOCKET, int, int *, int);
void release_fd_refcnt(SOCKET);
struct mux_peer_comm_mapping * mux_get_peer_comm_entry(SOCKET);
static void mux_local_comm_poll();
static void mux_vxt_comm_poll();
static void mux_add_local_fd(SOCKET);
static void mux_remove_local_fds();
void mux_process_vxt_comm(SOCKET, int *);
int mux_process_local_comm(SOCKET);


/*
 * Mapping between Dom0 to DomU and vice-versa
 */
struct mux_peer_comm_mapping* mux_peer_comm_mappings[MAX_NUMBER_OF_GUESTS];

/*
 * Maintains mapping from src to destination fd
 */
struct mux_demux_mapping *mux_mappings[MAX_CONNECTIONS];

/*
 * Descriptor array
 */
struct pollfd			mux_local_pollfd[MAX_CONNECTIONS];

static VXT_POLLFD_STRUCT        mux_vxt_pollfd[MAX_CONNECTIONS];

static int      mux_local_pollfd_size = 0;
static int      mux_vxt_pollfd_size = 0;


/*
 * Following synchronization primitive is to protect the mux_demux_array
 */
MUTEX_OBJ mux_demux_array_mutex = MUTEX_INITIALIZER;
/*
 * Following synchronization primitive is to protect the sevices_array
 */
MUTEX_OBJ mux_services_array_mutex = MUTEX_INITIALIZER;

MUTEX_OBJ mux_peer_comm_mutex = MUTEX_INITIALIZER;
MUTEX_OBJ mux_local_pollfd_mutex = MUTEX_INITIALIZER;
MUTEX_OBJ mux_vxt_pollfd_mutex = MUTEX_INITIALIZER;
MUTEX_OBJ mux_vxt_connections = MUTEX_INITIALIZER;
MUTEX_OBJ mux_log_counter_mutex = MUTEX_INITIALIZER;
COND_OBJ mux_local_pollfd_cv1;
COND_OBJ mux_local_pollfd_cv2;

int	is_dom0 = 0;
char    *target_host = NULL;
int     connection_port;
int	poll_threads_exit = 0;

#define	MUX_DEBUG_ENTRIES 10000

char    **mux_debug_log = NULL;
int	mux_debug_log_counter = 0;
int	mux_active_vxt_connections = 0;
char    *domain_uuid = NULL;
char    *dom0_uuid = NULL;
FILE	*mux_trace_fp = NULL;

struct mux_err  mux_err_msgs[] = {
{ 0, "NULL"},
{ MUX_CONNECT_GENERIC_ERROR, "Generic error occured on the connection request to MXT" },
{ MUX_CONNECT_LOCAL_CONNECT, "This is a connection request to local Xprtld" },
{ MUX_CONNECT_INVALID_SERVICE, "Invalid service requested from MXT" },
{ MUX_CONNECT_REMOTE_CONNECTION_FAILED, "Connection to Xprtld on the remote site failed" },
{ MUX_CONNECT_NON_EXISTENT_GUEST, "Domain with this uuid is not connected to VXT" },
{ MUX_CONNECT_NO_VXT_CONNECTION, "There are no domains connected to VXT" },
{ MUX_CONNECT_LOCAL_AUTH_FAILURE, "Authorization with local MXT failed" },
{ MUX_CONNECT_REMOTE_AUTH_FAILURE, "Authorization with remote Xprtld failed" },
{ MUX_CONNECT_REQUEST_TIMEOUT, "Connection request to remote timed out" }
};
