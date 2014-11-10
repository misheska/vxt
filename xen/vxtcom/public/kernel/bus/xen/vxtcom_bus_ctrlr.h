#ifndef VXTCOM_BUS_CTRLR_H
#define VXTCOM_BUS_CTRLR_H

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



#include <public/kernel/linux/vxtcom_embedded_os.h>

#define VALID_EVTCHN(chn)       ((chn) != 0)
#define VXTCOM_GRANT_INVALID_REF 0


/*
 *
 * vxtcom_bus_ctrlr.h:
 *
 * This file contains define, includes and routine
 * declarations for the bus driver specific support.
 * Bus driver code should not require inclusion of the
 * controller internal, device interface, or client
 * interface vxt header files.
 *
 */

#define VXTCOM_BUS_CNTRLR_VERSION 0x0000000000000001

#define GET_VXTCOM_BUS_CNTRLR_RELEASE(version) \
	((version >> 32) & 0xFFFFFFFF)

#define GET_VXTCOM_BUS_CNTRLR_PATCH(version) \
	(version & FFFF)

#define GET_VXTCOM_BUS_CNTRLR_SUBRELEASE(version) \
	((version >> 16) & 0xFFFF)

#define VXTCOM_STATE_DISCONNECTED 0
#define VXTCOM_STATE_CONNECTED    1
#define VXTCOM_STATE_SUSPENDED    2
#define VXTCOM_STATE_MORIBUND     3

/*
 * For the vxtcom_bus_signal_closing call
 */
#define VXTCOM_CLOSING_BUS_DOWN   1

#if 1
#define WPRINTK(fmt, args...)                           \
        printk(KERN_WARNING "xen_blk: " fmt, ##args)
#else
#define WPRINTK(fmt, args...) ((void)0)
#endif

#define DPRINTK(_f, _a...) pr_debug(_f, ## _a)

#if 0
#define DPRINTK_IOCTL(_f, _a...) printk(KERN_ALERT _f, ## _a)
#else
#define DPRINTK_IOCTL(_f, _a...) ((void)0)
#endif


/*
 * vxtcom_ctrlr_buf_hdr
 *
 * in_use field on sends is set when a 
 * message is written into the send buffer
 * and cleared when the reply comes from
 * the remote controller.
 *
 * The buffer mechanism is a single buffer
 * reply/response.  In order to avoid
 * complex interchain lock-up, remote replies
 * and requests can be received in any
 * order, there is no synchronization.
 * This is essential for the security
 * authroization point.  (Dom 0)
 *
 * Dom0 may send a request to a second
 * remote endpoint upon receiveing a
 * request.  This is because it acts
 * as an itermediary.
 *
 * Fortunately, guest domains only
 * talk to DOM0, not each other.  This
 * simplifies DomU to request/wait/
 * parse reply.  It also bounds
 * Dom0 intermediary actions to 
 * receive DomU1, request DomU2,
 * receive reply DomU2, send reply DomU1
 *
 * Also as a result of the direct
 * reply/request linkage DomU must
 * reject or hold requests when an
 * outstanding reply wait is underway.
 *
 * Dom0 is free to combine requests and
 * replies on a single signaled  
 * communication with its remote partner
 * However, replies must be kept in their
 * appropriate outq, (no mixing of requests
 * and replies on the two outq's).
 *
 * Message hdrs in the body of the
 * message must have the same message
 * transport id. 
 *
 * Stale messages are detected and
 * protected against with a message id.
 *
 */

typedef struct vxtcom_ctrlr_buf_hdr {
	uint32_t msg_id;
	uint32_t remote_msg_id;
	int      reply_in_use;
	int      request_in_use;
} vxtcom_ctrlr_buf_hdr_t;

typedef struct vxtbus_ctrlr {
	int              version;
	int              ctrlr_ref;
	vxt_mlock_t      config_mutex;
	struct work_struct config_work;
	struct xenbus_device *xbdev;
	dev_t            dev;
	int              connected;
	int              vdevice;
	void             *inq;
	struct vm_struct *inbuf_area;
	u32              reply_offset;
	u32              request_offset;
	vxtcom_ctrlr_buf_hdr_t inq_header;
	int              inref;
	grant_handle_t   inbuf_handle;
	void             *outq;
	struct vm_struct *outbuf_area;
	vxtcom_ctrlr_buf_hdr_t outq_header;
	int              outref;
	grant_handle_t   outbuf_handle;
	unsigned int     evtchn;
	void             *sig_buf;   /* split buf used for 2-way signaling */
	void             *sig_read;  /* ptr to incoming signal area */
	void             *sig_write; /* ptr to outoing signal area */
	struct vm_struct *sigbuf_area;
	grant_handle_t   sigbuf_handle;
	int              sigbuf_ref;
	unsigned int     sig_irq;
	unsigned int     sig_evt;
	int              user_ref_cnt;
	unsigned int     irq;
	int              indirect_create;
	int              pending_config_event;
	void             *dev_events;
	struct work_struct work;
	/* used in controller device allocation */
	struct gnttab_free_callback callback; 
	char             remote_uuid[MAX_VXT_UUID];

} vxtbus_ctrlr_t;



/*
 *  vxt_device_sm_t:
 *
 * Symcom device shared memory structure.
 * This structure is used in conjunction with
 * a lookup table to dereference shared memory
 * tokens for mmap support.
 *
 * foreign_map_field:  The foreign_map_field
 * MUST, MUST, be first.  It allows the vxt_device_sm
 * to appear as the token in the VMMAP private data
 * field.  In linux/xen this field is used to
 * special case page map lookups when foreign pages
 * are present.  (Linux apparently hasn't learned
 * how to abstract page objects yet.)
 *
 */

#ifdef NO_LINUX_DEFINED_FOREIGN_MAP
typedef struct vm_foreign_map {
	struct page **map;
} vm_foreign_map_t;
#endif


typedef struct vxt_device_sm {
	struct vm_foreign_map  foreign_map; /* special linux pg map support */
	struct page          **remote_pages; /* alt phys to mach map for usr */
	gnttab_map_grant_ref_t *user_handles;
	struct vm_area_struct  *vma;
	void                 *shadow_queue;
	vxtarch_word         token; /* device's handle */
	vxtarch_word         lookup_token; /* registered mapping token */
	void                 *queue_token;
	void                 *addr;
	uint32_t             len;
	uint64_t             dev_handle;
	uint32_t             ref_cnt;
	struct vxt_device_sm *next;
} vxt_device_sm_t;




#endif /* VXTCOM_BUS_CTRLR_H */
