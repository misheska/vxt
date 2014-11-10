#ifndef VXT_MSG_H
#define VXT_MSG_H

#define POLLVXT       0x2000
#define POLLVXT_DEV   0x4000
#define POLLVXT_CTRLR 0x8000

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
 * vxt_msg.h
 *
 * This module interacts with the vxt_com communication controller
 * It connects specifically to the vxt_msg_dev.  
 */

/*
 * There is a provision in vxt queue type 1
 * for an extended header.  vxt message
 * uses this to id its queues with a
 * cookie.  This cookie provides a 
 * double check on queue initialization
 * when the use of the vxt_msg library
 * doesn't follow prescribed startup
 * i.e.  the connector waits for a message 
 * signal from the provider of the device.
 */

#define vxtmsg_cookie_size 8


#define VXT_MSG_DEFAULT_QUEUE_LENGTH 16384
#define VXT_MSG_DEFAULT_BUFCNT 7



/*
 * Shared memory buffer characteristics
 * for the vxt_msg_add call
 */

typedef struct vxt_msg_buf_parms {
	uint32_t recv_buf_size;
	uint32_t recv_sub_buf_cnt;
	uint32_t send_buf_size;
	uint32_t send_sub_buf_cnt;
} vxt_msg_buf_parms_t;



extern int vxt_msg_init(void);

extern int vxt_msg_ctrlr_plug_event(int timeout, int *revent);

extern int vxt_msg_poll_ctrlr(void *connection, int timeout, int *revent);

extern int vxt_msg_add(vxt_dev_type dev, vxt_dev_addr_t *endpoint, 
                       int endpoint_length, vxt_msg_buf_parms_t *buf_parms,
                       void **connection);

extern int vxt_msg_connect(vxt_dev_type dev, vxt_dev_addr_t *endpoint, 
                           int endpoint_length, char *target_ctrlr,
                           vxt_msg_buf_parms_t *buf_parms,
                           void **connection);

extern int vxt_msg_disconnect(void *connection, int options);


extern int vxt_msg_send(void *connection, void *buffer, 
                        int *len, int flags);

extern int vxt_msg_rcv(void *connection, void *buffer, 
                       size_t *len, int flags);

extern int vxt_msg_wait(void *fds, int nfds, int timeout, int *event_count);

extern int vxt_msg_system_query(vxt_query_sys_struct_t *sys_info);

extern int vxt_msg_bus_query(vxt_query_bus_struct_t *card_info);

extern int vxt_msg_dev_query(vxt_query_dev_struct_t *dev_info);

extern int vxt_msg_bind_query(void *connection,
                              vxt_query_bind_struct_t *bind_info);

extern int vxt_msg_control(void *connection, int cmd,
                           void *info, int info_len);

extern int vxt_msg_ctrlr_wait(void *connection);

extern int vxt_msg_shutdown(void *connection);

extern int vxt_msg_unplug_dev(void *connection,
                              vxt_unplug_dev_struct_t *dev_info);

#endif
