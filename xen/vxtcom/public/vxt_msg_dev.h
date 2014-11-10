#ifndef   VXT_MSG_DEV_H
#define   VXT_MSG_DEV_H

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
 * vxt_msg_dev.h
 *
 * This file contains defines and structure definitions
 * for interfaces to the Symantec RPC messaging device.
 * The messaging facility is built on top of the Symantec
 * shared memory transport product and interfaces direclty
 * with the VXT controller.  The VXT controller vends
 * devices that connect endpoints through shared memory
 * or other hardware assisted communication.
 *
 * vxt_msg_dev is a suite of modules which present a 
 * device/library to the vxt_controller to allow
 * the controller to create vxt_msg devices.  The
 * suite also provides an application level library
 * That talks to the controller top end to
 * create and manipulate vxt_msg devices, connecting
 * them with their counterparts in other guests.
 */


/*
 * Flags for the vxt_msg_disconnect call
 */
#define VXT_CLOSE_REMOTE 0x1
#define VXT_CLOSE_LOCAL  0x2
#define VXT_CLOSE_FORCE  0x4


/*
 * Flags for the state of the
 * IPC style message device
 * state
 */

#define VXT_MSG_DEV_DISCONNECTED     0x1
#define VXT_MSG_DEV_CONNECTED        0x2
#define VXT_MSG_DEV_ERROR            0x3

typedef struct vxt_msg_dev_info {
	int  version;
        char ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];  /* cntrl dev is on */
        char dev_name[VXTCOM_MAX_DEV_FILE_NAME];  /* dev file for polling */
	uint64_t vxt_bus_id;         
	uint64_t dev_handle;
	void *sendq;     /* token to be used on mmap call */
	int  sendq_size; /* in bytes */
	int  sendq_bufcnt;
	void *rcvq;      /* token to be used on mmap call */
	int  rcvq_size; /* in bytes */
	int  rcvq_bufcnt;
	int state;
	int evt_chn;
	int remote_dom;
} vxt_msg_dev_info_t;


typedef struct vxt_msg_create_info {
	uint32_t sendq_len;
	uint32_t rcvq_len;
	uint32_t sendq_bufcnt;
	uint32_t rcvq_bufcnt;
} vxt_msg_create_info_t;

typedef struct vxt_msg_destroy_info {
	uint32_t filler;
} vxt_msg_destroy_info_t;


typedef struct vxt_msg_bus_info {
	uint64_t vxt_bus_id;   // in/out
	uint	    device_state;
	char uname[MAX_VXT_UNAME];
	uint64_t dev_name;   // device type
} vxt_msg_bus_info;

#define VXT_MSG_QUEUE_RUNNING         0x1
#define VXT_MSG_QUEUE_HALTED          0x2
#define VXT_MSG_QUEUE_ERROR           0x4

typedef struct vxt_msg_queue_info {
	int bytes_avail;
	int queue_state;
	int buf_size;
	int buf_count;
} vxt_msg_queue_info_t;

#define VXT_MSG_ATTACH_SUCCESS 0x1
#define VXT_MSG_ATTACH_BUSY    0x2
#define VXT_MSG_ATTACH_ERROR   0x3

typedef struct vxt_msg_attach_info {
	int status;
} vxt_msg_attach_info_t;

typedef struct vxt_msg_queue_hyster_values {
	int signal_low;
	int signal_high;
} vxt_msg_queue_hyster_values;


/*
 * Version  
 * Release a  Subrelease b
 * 0xa00b00.
 * 0x00a is release
 * 0x00b is subrelease 
 * and the lower nibble is used
 * for finer grain subrelease information
 */

#define VXT_MSG_DEV_0_1			0x100
#define VXT_MSG_DEV_1_0			0x100000

#define VXT_MSG_DEV_VERSION		VXT_MSG_DEV_0_1


#define VXT_MSG_BUF_INVALID_INDEX 	0xFFFFFFFF




#endif    /* VXT_MSG_DEV_H */

