/* $Id: mxt_bufops.c,v 1.8 2009/02/04 00:19:12 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/common/mxt_bufops.c,v $" */

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

extern char * mux_get_log_entry();
/*
 * Returns index of the slot which contains the buffer address
 */
int
mux_getbuf(comm_buf, comm_index)
	struct mux_comm_buf	*comm_buf;
	int			*comm_index;
{
	int	ret = -1;
	int	i = 0;

	MUTEX_LOCK(comm_buf->mux_comm_buf_mutex);

	if (comm_buf->mux_comm_buf_inuse == MAX_COMM_BUF_SIZE) {
		MUX_LOG_ENTRY0("No memory slots");
		goto exit;
	}
	/*
	 * Look for the next free slot.
	 */
	for (i = 0; i < MAX_COMM_BUF_SIZE; i++) {
		if (comm_buf->mux_comm_buf_alloted[i] == 0) {
			break;
		}
	}

	if (i < MAX_COMM_BUF_SIZE) {
		ret = i;
		*comm_index = i;
		comm_buf->mux_comm_buf_inuse++;
		comm_buf->mux_comm_buf_alloted[i] = 1;
	} else {
		ret = -1;
	}

exit:
	MUTEX_UNLOCK(comm_buf->mux_comm_buf_mutex);

	return ret;
}

	
void
mux_reclaimbuf(comm_buf, comm_index)
	struct mux_comm_buf	*comm_buf;
	int			comm_index;
{

	MUTEX_LOCK(comm_buf->mux_comm_buf_mutex);

	comm_buf->mux_comm_buf_alloted[comm_index] = 0;
	memset(comm_buf->mux_comm_bufptr[comm_index], '\0', MAX_LEN);
	comm_buf->mux_comm_buf_inuse--;
	
	MUTEX_UNLOCK(comm_buf->mux_comm_buf_mutex);

	return;
}

int
mux_get_buf_freespace(comm_buf)
	struct   mux_comm_buf	*comm_buf;
{
	int	ret;

	MUTEX_LOCK(comm_buf->mux_comm_buf_mutex);
	ret = MAX_COMM_BUF_SIZE - comm_buf->mux_comm_buf_inuse;
	MUTEX_UNLOCK(comm_buf->mux_comm_buf_mutex);

	return ret;
}
