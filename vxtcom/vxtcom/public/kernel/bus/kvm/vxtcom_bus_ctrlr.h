#ifndef VXTCOM_BUS_CTRLR_H
#define VXTCOM_BUS_CTRLR_H

#include <linux/workqueue.h>
#include <linux/mm.h>
#include <public/kernel/linux/vxtcom_embedded_os.h>
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


typedef uint64_t domid_t;  

#define VXTCOM_BUS_CNTRLR_VERSION 0x0000000000000001

#define GET_VXTCOM_BUS_CNTRLR_RELEASE(version) \
	((version >> 32) & 0xFFFFFFFF)

#define GET_VXTCOM_BUS_CNTRLR_PATCH(version) \
	(version & FFFF)

#define GET_VXTCOM_BUS_CNTRLR_SUBRELEASE(version) \
	((version >> 16) & 0xFFFF)

#define VXTCOM_STATE_DISCONNECTED 0
#define VXTCOM_STATE_CONNECTED    1
#define VXTCOM_STATE_INITIALIZED  2
#define VXTCOM_STATE_SUSPENDED    3
#define VXTCOM_STATE_MORIBUND     4

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
	int               version;
	int               ctrlr_ref;
	vxt_mlock_t       config_mutex;
	void              *vdev;
	void              *kvm;       /* Virtual machine instance/context */
	void              *mm;        /* memory management struct of guest */
	int               connected;
	int               vdevice;    /* Linux device major number */
	void              *inq;
	uint64_t          in_page;    /* physical page needed for inq */
	u32               reply_offset;
	u32               request_offset;
	vxtcom_ctrlr_buf_hdr_t inq_header;
	void              *outq;
	uint64_t          out_page;
	vxtcom_ctrlr_buf_hdr_t outq_header;
	void              *sig_buf;   /* split buf used for 2-way signaling */
	void              *sig_read;  /* ptr to incoming signal area */
	void              *sig_write; /* ptr to outoing signal area */
	uint64_t          sig_page;
	unsigned int      sig_evt;
	struct file       *sig_irq_file;
	void              *sig_irq_ctx;
	int               user_ref_cnt;
	unsigned int      cfg_irq;
	struct file       *cfg_irq_file;
	void              *cfg_irq_ctx;
	int               indirect_create;
	int               pending_config_event;
	void              *dev_events;
	struct work_struct work;
	struct work_struct config_work;
	struct work_struct bus_activate;
	char              remote_uuid[MAX_VXT_UUID];
	char              local_uuid[MAX_VXT_UUID];
	domid_t           remote_domid;
	void              *request;
	void              *send_vq;
	void              *rcv_vq;
	void              *cfg_cb;
	void              *sig_cb;
	int               response_pending;
	int               request_pending;
	void              *response_buf;
	void              *cfg_dummy_buf;
	void              *sig_dummy_buf;
	/*
	 * The following section supports file descriptor polling as
	 * a front-end for guest doorbell style interrupts on device
	 * data and VxT bus configuration paths.
	 */
	uint64_t          cfg_bell;
	struct file       *cfg_bellfile;
	poll_table        cfgpoll_tbl;
	wait_queue_t      cfg_wait;
	wait_queue_head_t *cfg_wq_head;

	uint64_t          sig_bell;
	struct file       *sig_bellfile;
	poll_table        sigpoll_tbl;
	wait_queue_t      sig_wait;
	wait_queue_head_t *sig_wq_head;
	char              ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];
} vxtbus_ctrlr_t;



/*
 *  vxt_device_sm_t:
 *
 * Symcom device shared memory structure.
 * This structure is used in conjunction with
 * a lookup table to dereference shared memory
 * tokens for mmap support.
 *
 *
 */



typedef struct vxt_device_sm {
	struct {
		struct page **map;
	} foreign_map;
	struct page          **remote_pages; /* alt phys to mach map for usr */
	struct vm_area_struct  *vma;
	void                 *user_handles;
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



extern void
vxtcom_event_signal(void *sig);

static inline void
vxtcom_event_bus_signal(vxtbus_ctrlr_t *bus)
{
	vxtcom_event_signal((void *)bus->cfg_irq_ctx);
}

static inline void
vxtcom_event_device_signal(vxtbus_ctrlr_t *bus)
{
	vxtcom_event_signal((void *)bus->sig_irq_ctx);
}




#endif /* VXTCOM_BUS_CTRLR_H */
