#ifndef   VXTCOM_CONTROLLER_H
#define   VXTCOM_CONTROLLER_H

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
 * This module contains the definitions of control 
 * structures passed between vxt controllers.
 *
 * During runtime execution, vxt controllers 
 * communicate to set up or tear down point to
 * point connections.  This module provides the
 * definitions for the configuration commands
 * that are passed back and forth and the
 * interface definitions for buffer control.
 *
 * Control information falls into two catagories:
 *
 * 1: Configuration:  
 * Most of these interfaces are for contact with the
 * DOM0 or other secure authority.  They are used to
 * Request and establish point to point connections 
 * with other remote endpoints.
 *
 * 2: Data flow signalling.  When generic device
 * callback is used or an Attention is sent, the
 * controller associated with the remote endpoint
 * signals the paired controller directly.  The
 * message structure in the shared memory page
 * communicates the details.
 *
 */

#define VXTCOM_CNTRL_NULL	 0x0
#define VXTCOM_CNTRL_ABORT	 0x1
#define VXTCOM_CNTRL_TIMEOUT	 0x2
#define VXTCOM_CNTRL_PLUGIN 	 0x3  /* new device available */
#define VXTCOM_CNTRL_REMOVE      0x4  /* req to unplug remote dev */
#define VXTCOM_CNTRL_SNAP	 0x5  /* snapshot for live migration */
#define VXTCOM_CNTRL_ADD_REQ	 0x6  /* req authority to connect endpt */
#define VXTCOM_CNTRL_REQ_ADD     0x7  /* request remote initiate add req */
#define VXTCOM_CNTRL_DATA        0x8  /* generic message construct */
#define VXTCOM_CNTRL_BUS_CLOSING 0x9  /* vxt controller being shutdown */

#define VXTCOM_CNTRL_RESPONSE	0x10000000  
#define VXTCOM_CNTRL_ERROR	0x20000000  
/*
 * The VXTCOM_CNTRL_RESPONSE flag is or'd into the message
 * type field.  This allows requests and responses to be
 * sent in the same buffer.  The message_id field is used
 * to match up request to response.  The message_id for
 * the two endpoints is independent and so the RESPONSE
 * field is necessary to delineate.
 */

#define VXTCOM_CONFIG_QUEUE_SIZE 4096
#define VXTCOM_CONFIG_QUEUE_REPLY_OFFSET 2048



/*
 * The contents of the shared memory buffer are self
 * defining.  vxtcom_cntrl_hdr_t appears at the top
 * and after each message.  The reading of the 
 * incoming buffer ends when VXTCOM_CNTRL_NULL is
 * encountered or the end of the buffer is reached. 
 */

typedef struct vxtcom_cntrl_hdr {
	uint64_t  message_type;
	uint64_t  message_id;
	uint64_t  remote_dom; 
	uint64_t  connecting_dom; 
	uint64_t  config_dom; 
	vxtdev_type_t   dev_type;
	char      remote_ep[MAX_VXT_UNAME];
	uint64_t  remote_cp;
	uint64_t  local_cp; /* local endpoint, (device_handle) */
	uint64_t  datalen;  /* any data which accompanies */
} vxtcom_cntrl_hdr_t;


typedef struct vxtcom_cntrl_remote_add {
	uint64_t create_wait_cookie;   /* void * for create wait structure */
	uint64_t device_info_length;   /* for data following this struct */
} vxtcom_cntrl_remote_add_t;

typedef struct vxtcom_cntrl_signal_closing {
	uint64_t create_wait_cookie;   /* void * for create wait structure */
	uint64_t shutdown_flags;       /* closing flavor and cause */
} vxtcom_cntrl_signal_closing_t;

typedef struct vxtcom_cntrl_error {
	uint64_t error;
	uint64_t event_port;
} vxtcom_cntrl_error_t;

/*
 * Additional device specific data may follow
 * this structure, its length is specified
 * in cfg_data_len
 */

typedef struct vxtcom_cntrl_attach {
	uint64_t      create_wait;  /* remote create request waiter */
	uint64_t      rport;        /* remote port to bind */
	vxtdev_type_t dev_type;     /* device type */
	uint64_t      cfg_data_len; /* dev specific */
	/*  reference number to shared page list */
	uint64_t      shared_queue_cnt;
} vxtcom_cntrl_attach_t;


typedef struct vxtcom_connect_dev_info {
        vxtdev_type_t device_type;
        char          remote_ep[MAX_VXT_UNAME];
        void          *dev_parms;
        void          *mem_parms;
	uint64_t      remote_dom; 
        uint32_t      queue_cnt;
} vxtcom_connect_dev_info_t;



/*
 * vxtcom_cntrl_squeue:
 * The vxtcom_cntrl_attach_t may be followed
 * by any number of vxtcom_cntrl_squeue
 * structures and their accompaning 
 * ref_queue structures as specified by
 * shared_queue_cnt.
 */

typedef struct vxtcom_cntrl_squeue {
	int shared_page_cnt;
} vxtcom_cntrl_squeue_t;


typedef struct vxtcom_cntrl_add_dev_req {
	int filler;  /* needed by some compilers */
} vxtcom_cntrl_add_dev_req_t;




#endif   /* VXTCOM_CONTROLLER_H */
