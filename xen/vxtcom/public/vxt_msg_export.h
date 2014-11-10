#ifndef VXT_MSG_EXPORT
#define VXT_MSG_EXPORT

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


#define POLLVXT 0x2000
#define POLLVXT_DEV   0x4000
#define POLLVXT_CTRLR 0x8000


/*
 *  msg send/rcv flags
 */


#define VXT_MSG_SIGNAL           0x1
#define VXT_MSG_WAIT             0x2
#define VXT_MSG_AUTO_STREAM      0x4
#define VXT_MSG_IGNORE_SHUTDOWN  0x8


/*
 *  VXT_IOCTLs
 */

#define VXT_IOCTL_SIGNAL_SEND         0x1
#define VXT_IOCTL_SIGNAL_RCV          0x2
#define VXT_IOCTL_QUERY_SYS           0x3
#define VXT_IOCTL_QUERY_BUS           0x4
#define VXT_IOCTL_QUERY_DEV           0x5
#define VXT_IOCTL_QUERY_SEND          0x6
#define VXT_IOCTL_QUERY_RCV           0x7
#define VXT_IOCTL_SET_SEND_ERROR      0x8
#define VXT_IOCTL_SET_RCV_ERROR       0x9
#define VXT_IOCTL_RESET_SEND          0xa
#define VXT_IOCTL_RESET_RCV           0xb
#define VXT_IOCTL_SET_SEND_HYSTERESIS 0xc
#define VXT_IOCTL_SET_RCV_HYSTERESIS  0xd
#define VXT_IOCTL_SET_BUF_PARMS       0xe
#define VXT_IOCTL_CTRLR_PLUG          0xf
#define VXT_IOCTL_CTRLR_POLL          0x10
#define VXT_IOCTL_TARGET_CTRLR        0x11
#define VXT_IOCTL_UNPLUG_DEV          0x12
#define VXT_IOCTL_QUERY_BIND          0x13


typedef struct vxt_dev_addr {
/*
        uint64_t uname_hash;
*/
	char ep_id[MAX_VXT_UUID];
        char uname[MAX_VXT_UNAME];
} vxt_dev_addr_t;


typedef struct vxt_msg_plug_poll {
	int timeout;
	int revent;
} vxt_msg_plug_poll_t;

/*
 * vxt_poll_obj_t:
 *
 * array structure used to abstract the
 * multiple wait signal capability of 
 * poll and signal primitives in 
 * the various O.S.'s VXT runs on.
 *
 */

typedef struct vxt_poll_obj {
	uint64_t object;
	uint32_t events;
	uint32_t revents;
} vxt_poll_obj_t;


/*
 * vxt_query_bind_struct:
 * 
 * used on vxt_msg_bind_query calls to get the
 * mapping between a bound vxt socket/device
 * and the underlying vxt_device
 */

typedef struct vxt_query_bind_struct {
	uint64_t ctrlr_id;
	uint64_t dev_slot;
} vxt_query_bind_struct_t;



#endif /* VXT_MSG_EXPORT */
