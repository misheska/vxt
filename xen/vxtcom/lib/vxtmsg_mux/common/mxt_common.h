/* $Id: mxt_common.h,v 1.15 2009/02/04 00:28:59 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/common/mxt_common.h,v $" */

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
 * Communication buffer
 */
#define MAX_COMM_BUF_SIZE       100
#define MUX_UUID_SIZE   16

#define MAX_LEN		9192	/* 8K + 1000 */

extern FILE *mux_trace_fp;

struct mux_comm_buf {
	char		*mux_comm_bufptr[MAX_COMM_BUF_SIZE];
	int		mux_comm_buf_alloted[MAX_COMM_BUF_SIZE];
	MUTEX_OBJ	mux_comm_buf_mutex;
	int		mux_comm_buf_inuse;
};

struct mux_conn_resp {
	int	mux_conn_resp_status;
};

struct mux_err {
	int	mux_err_code;
	char	*mux_err_msg;
};

struct mux_auth {
        SOCKET  mux_auth_fd;
        int     mux_auth_magic;
	int	mux_auth_key;
};

#define READ_MAGIC (('R' << 24) | \
		    ('E' << 16) | \
		    ('A' <<  8) | \
		    ('D'))

#define SHMSZ           130
#define KEY             1234
#define COOKIESZ        128

#define MAX_SERVICE_NAME_LEN    255

#define MAX_PRECREATED_CONNS	5 

struct mux_demux_mapping {
	char	mux_demux_mapping_service[MAX_SERVICE_NAME_LEN];
        SOCKET	mux_demux_mapping_myside_fd;
        SOCKET	mux_demux_mapping_peerside_fd;
	MUTEX_OBJ	mux_demux_mapping_fd_mutex;
	int	mux_demux_mapping_fd_flags;	/*
						 * Flags to identify the fd
						 * processing
						 */
	int	mux_demux_mapping_comm_data[MAX_COMM_BUF_SIZE];
						/*
						 * Contains the index of the
						 * data in the communication
						 * buffer
						 */
	int	mux_demux_mapping_comm_data_valid[MAX_COMM_BUF_SIZE];
						/*
						 * If set to 1, entry is
						 * considered valid
						 */
	int	mux_demux_mapping_end_index;
	int	mux_demux_mapping_start_index;
	int	mux_demux_mapping_fd_refcnt;
	int	mux_demux_mapping_fd_bufcnt;
					/*
					 * Track the buffer allocation for a
					 * particular connection
					 */
	int	mux_demux_mapping_first_msg;
	SOCKET	mux_demux_mapping_target_fd;
	SOCKET	mux_demux_mapping_local_peer_fd;
	int	mux_demux_mapping_is_vxt_ep;
	int	mux_demux_mapping_is_inactive;
	int	mux_demux_mapping_removed_from_poll;
	int	mux_demux_mapping_conn_cookie;
	int	mux_demux_mapping_precreated_conns[MAX_PRECREATED_CONNS];
	struct mux_demux_mapping	*mux_demux_mapping_next;
};


/*
 * MUX_BASE_UUID : If destination uuid is specified as this, it means
 * the request needs to be forwarded to Dom0
 */
#define MUX_BASE_UUID	"__base__"
/*
 * MUX_DEBUG_DATA : If destination uuid is specified as this, it means
 * the debug data in mux_debug_log needs to be sent to the client
 */
#define MUX_DEBUG_DATA	"__debug__"

/*
 * MUX_REGISTERED_GUEST : If destination uuid is specified as this, it means
 * the known guests to MXT should be sent to the client
 */
#define MUX_REGISTERED_GUESTS "__guests__"

#define MAX_NUMBER_OF_GUESTS	64


/*
 * mux_peer_comm_flags
 */
#define MUX_PEER_COMM_MARKED_FOR_REMOVAL 0x00000001

/*
 * Every connection from MXT of the local domain to the MXT of the target
 * domain will require following structure to be managed
 */
#define MAX_XPRTLD_CONNECTIONS		3

struct mux_xprtld_conns {
	SOCKET	mux_xprtld_conn_fd;
	int	mux_xprtld_conn_alloted;
};

struct mux_peer_comm_mapping {
	char			mux_peer_comm_uuid[16];
	SOCKET			mux_peer_comm_fd;
	int			mux_peer_comm_my_recv_current_window_size;
	MUTEX_OBJ		mux_peer_comm_my_recv_current_window_size_mutex;
	int     		mux_peer_comm_peer_recv_current_window_size;
	int     		mux_peer_comm_window_update_message_sent;
	struct mux_comm_buf	*mux_peer_comm_send_buf;
	struct mux_comm_buf	*mux_peer_comm_recv_buf;
	int			mux_peer_comm_send_buf_index;
	int			mux_peer_comm_recv_buf_index;
	int			mux_peer_comm_is_vxt_ep;
	long long		mux_peer_comm_send_seqno;
	long long		mux_peer_comm_recv_seqno;
	int			mux_peer_comm_fd_refcnt;
	int			mux_peer_comm_flags;
	MUTEX_OBJ		mux_peer_comm_send_mutex;
	MUTEX_OBJ		mux_peer_comm_recv_mutex;
	MUTEX_OBJ		mux_peer_comm_mutex;
	struct mux_xprtld_conns
			mux_peer_comm_xprtld_conns[MAX_XPRTLD_CONNECTIONS];
	struct mux_peer_comm_mapping	*mux_peer_comm_mapping_next;
};

#define MUX_DEBUG_MSG_SIZE 100

/*
 * Following macro accepts one formated string and NO argument to it
 */
#ifdef DEBUG

#define MUX_LOG_ENTRY0(buf_fmt)					\
	{							\
		char	*bufptr;				\
		bufptr = mux_get_log_entry();			\
		if (bufptr) {					\
			SNPRINTF(bufptr, MUX_DEBUG_MSG_SIZE, buf_fmt);	\
			fprintf(stderr, "MXT_DEBUG : %s\n", bufptr);	\
		} else {					\
			fprintf(stderr, buf_fmt);		\
			if (mux_trace_fp) {				\
				fprintf(mux_trace_fp, buf_fmt);	\
				fprintf(mux_trace_fp, "\n");	\
				fflush(mux_trace_fp);			\
			}						\
		}						\
	};

/*
 * Following macro accepts one formated string and ONE argument to it
 */

#define MUX_LOG_ENTRY1(buf_fmt, buf_arg)			\
	{							\
		char	*bufptr;				\
		bufptr = mux_get_log_entry();			\
		if (bufptr) {					\
			SNPRINTF(bufptr, MUX_DEBUG_MSG_SIZE, buf_fmt,	\
				(buf_arg));				\
			fprintf(stderr, "MXT_DEBUG : %s\n", bufptr);	\
		} else {					\
			fprintf(stderr, buf_fmt, (buf_arg));	\
			if (mux_trace_fp) {				\
				fprintf(mux_trace_fp, buf_fmt, (buf_arg));\
				fprintf(mux_trace_fp, "\n");		\
				fflush(mux_trace_fp);			\
			}						\
		}						\
	};

/*
 * Following macro accepts one formated string and TWO argument to it
 */

#define MUX_LOG_ENTRY2(buf_fmt, buf_arg1, buf_arg2)		\
	{							\
		char	*bufptr;				\
		bufptr = mux_get_log_entry();			\
		if (bufptr) {					\
			SNPRINTF(bufptr, MUX_DEBUG_MSG_SIZE, buf_fmt,	\
				(buf_arg1), (buf_arg2));		\
			fprintf(stderr, "MXT_DEBUG : %s\n", bufptr);	\
		} else {					\
			fprintf(stderr, buf_fmt, (buf_arg1), (buf_arg2)); \
			if (mux_trace_fp) {				\
				fprintf(mux_trace_fp, buf_fmt, (buf_arg1), \
					(buf_arg2));			\
				fprintf(mux_trace_fp, "\n");		\
				fflush(mux_trace_fp);			\
			}						\
		}							\
	};

#else /* DEBUG */

/* Non debug macros */
#define MUX_LOG_ENTRY0(buf_fmt)					\
	{							\
		char	*bufptr;				\
		bufptr = mux_get_log_entry();			\
		if (bufptr) {					\
			SNPRINTF(bufptr, MUX_DEBUG_MSG_SIZE, buf_fmt);	\
		}						\
		if (mux_trace_fp) {				\
			fprintf(mux_trace_fp, buf_fmt);		\
			fprintf(mux_trace_fp, "\n");		\
			fflush(mux_trace_fp);				\
		}						\
	};

/*
 * Following macro accepts one formated string and ONE argument to it
 */

#define MUX_LOG_ENTRY1(buf_fmt, buf_arg)			\
	{							\
		char	*bufptr;				\
		bufptr = mux_get_log_entry();			\
		if (bufptr) {					\
			SNPRINTF(bufptr, MUX_DEBUG_MSG_SIZE, buf_fmt,	\
				(buf_arg));				\
		}						\
		if (mux_trace_fp) {				\
			fprintf(mux_trace_fp, buf_fmt, (buf_arg));	\
			fprintf(mux_trace_fp, "\n");		\
			fflush(mux_trace_fp);				\
		}						\
	};

/*
 * Following macro accepts one formated string and TWO argument to it
 */

#define MUX_LOG_ENTRY2(buf_fmt, buf_arg1, buf_arg2)		\
	{							\
		char	*bufptr;				\
		bufptr = mux_get_log_entry();			\
		if (bufptr) {					\
			SNPRINTF(bufptr, MUX_DEBUG_MSG_SIZE, buf_fmt,	\
				(buf_arg1), (buf_arg2));		\
		}						\
		if (mux_trace_fp) {				\
			fprintf(mux_trace_fp, buf_fmt, (buf_arg1), \
				(buf_arg2));			\
			fprintf(mux_trace_fp, "\n");		\
			fflush(mux_trace_fp);				\
		}						\
	};

#endif /* DEBUG */
