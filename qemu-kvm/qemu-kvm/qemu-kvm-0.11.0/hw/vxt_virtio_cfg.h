#ifndef _VXT_VIRTIO_CFG_H_
#define _VXT_VIRTIO_CFG_H_

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
 * vxt_virtio_cfg.h:
 *
 * vxt_virtio_cfg.h contains defines, structures, and function
 * declarations that are used in communication between the KVM/VIRTIO-PCI
 * VxT Card hardware emulation and the Virtio/VxT PCI driver in the
 * guest operating system. 
 *
 * Since the build environments for the KVM user space task and the
 * VxT linux driver are separate this file will have to be duplicated
 * in both places and must be kept up-to-date manually.
 * 
 */

/* The ID for virtio_vxt */
#define VIRTIO_ID_VXT 7



/*
 * VIRTIO_VXT_VERSION format:
 * R = Major Release (may be incompatible with earlier releases)
 * S = Sub-version (dot release)
 * B = builds
 * 8 HEX NIBBLES:  0xRRRRSSBB
 */
#define VIRTIO_VXT_VERSION 0x10000

/*
 * vxt_bus_state:
 *
 * Reflects configuration with respect to the VxT backend that resides
 * in the base linux O.S. kernel.
 *
 * The VxT virtio device presents a PCI bus to the virtual guest. This
 * device is the interface for the VxT virtual controller frontend.
 * To provision devices, i.e. plug them into the VxT controller/bus,
 * the VxT back end must be consulted and resource provisioned there.
 *
 * The VxT back end resides in the linux O.S./hypervisor and is part
 * of the trusted computing base, (TCB).  As such, it can acquire
 * hypervisor owned shared memory and interrupts, configure IOV, and
 * connect any two endponts reachable by the hypervisor.
 *
 * In order to make requests of the hypervisor resident VxT back end it is
 * necessary to communicate with the VxT Virtio presence in the KVM/Qemu
 * space to establish a communication channel.
 *
 * The construction work on the connection between VxT front end and 
 * back end progresses through a series of states.  These states reflect
 * a handshake protocol that involves the aquisition of shared memory and
 * interrupt resources, their broadcast to the remote party and the 
 * acknowledgement that the resources have been initialized and are 
 * ready for use.
 *
 * Note:  The connection state may be updated by either side.  However
 * It is owned by only one side at any particular time.  i.e.  If the
 * state is VXT_PROVISIONED, only the front-end may change it, and even
 * then it can only be changed to CONNECTING or DOWN.  Likewise if
 * the state is VXT_DOWN, only the backend can change it to VXT_PROVISIONED.
 *
 * Only one state may be changed by either side this is transition from
 * VXT_RUNNING.  The state can only be changed directly by the back-end.
 * However, the front end may send a message across the KVM interface or the
 * VxT shared memory interface indicating that it is going down on its own.

 *
 * Shutdown sequence, VxT-Virtio controller unplug scenario
 *
 * backend:  VXT_REVOKING - unplugging
 * frontend: VXT_WINDING_DOWN - flushing data, stop new connections
 * backend: VXT_SHUTDOWN - backend detects VxT devices flushed and down
 * frontend: VXT_QUITTED - front end driver context has been removed.
 *                         the backend will dump  instance resources.
 * ... when controller plugged in again ... 
 * backend: VXT_PROVISIONED - ...
 *
 * Shutdown seqence, guest driver removed
 * Note: we do not wait for flushed work here, shutdown of a driver
 * is a software event.  If the guest wants to synchronize this should
 * be done outside of the driver module unload context.
 * frontend: VXT_QUITTED - front end driver context has been removed.
 *                         the backend will dump  instance resources.
 * backend: VXT_PROVISIONED - backend goes back to initial state, waiting
 *                            for another driver to connect to.
 *
 * Suspend sequence:  Used for migration and interruptions in VxT base 
 * kernel module availability.
 * 
 * backend: VXT_SUSPENDING
 * frontend: VXT_WINDING_DOWN
 * backend: VXT_PROVISIONED - when service restored
 * 
 *
 */

typedef enum vxt_bus_state {
	VXT_PROVISIONED = 0, /* vxt controller is plugged in and reset */
	VXT_CONNECTING,      /* front-end contacts back-end through KVM */
	VXT_CONFIGURED,      /* back-end supplies rsrcs and tells front-end */
	VXT_INITIALIZED,     /* front-end acknowledges rsrcs and shows ready */
	VXT_RUNNING,         /* back-end enters running state, tells frt end */
	VXT_SUSPENDING,      /* back-end when quiescing */
	VXT_REVOKING,        /* back-end when vxt_bus has been taken away */
	VXT_QUITTED,         /* front-end driver is shutting down */
	VXT_WINDING_DOWN,    /* front-end, I/O being flushed after REVOKING */
	VXT_SHUTDOWN,        /* back-end, I/O flush finished */
	VXT_RESET,           /* Sent by back-end, reset button alternative */
	                     /* to VXT_PROVISIONED, (same endstate) */
	VXT_CFG_ERROR        /* Either side can push from any state */
} vxt_bus_state_t;



/*
 * The following defines are Bus register addresses for the standard
 * VxT bus doorbells.
 */

/*
#define VXT_BUS_CONFIG_REG  1001
#define VXT_BUS_DEV_SIG_REG 1002
*/
/*
 * We are using the KVM wrapped doorbells for now, that means the
 * signal corresponds to the queue number.  Our queues are numbered
 * 2 and 3.  We use the virtio_pci:vp_notify on the guest side.
 */
#define VXT_BUS_CONFIG_REG  2
#define VXT_BUS_DEV_SIG_REG 3


#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif


#define VIRTIO_VXT_SUCCESS 0
#define VIRTIO_VXT_FAIL    1
#define VIRTIO_VXT_RANGE   2
#define VIRTIO_VXT_RSRCS   3



/*
 *  VXT Configuration error states
 */

#define VXT_UNKNOWN_STATE  0x1
#define VXT_STATE_MISMATCH 0x2


typedef struct vxt_virtio_cfg {

	uint64_t version;
	vxt_bus_state_t current_state; /* interlock, reject if not correct */
	vxt_bus_state_t provisional_state; /* used to determine union field */

	union {
		struct {
			/*
			 * Locations for shared memory buffers, provided
			 * by guest VxT Virtio driver.
			 */
			uint64_t cfg_in;
			uint64_t cfg_in_size;
			uint64_t cfg_out;
			uint64_t cfg_out_size;
			uint64_t signal;
			uint64_t signal_buf_size;
		} connecting;
		struct {
			/*
			 * After memory mapping buffers, device emulation
			 * supplies mapping status and the irqs that will
			 * be used by the VxT bus device.
			 */
			uint64_t cfg_irq;
			uint64_t sig_irq;
			uint64_t guest_name;
			uint64_t connecting_status;
			char     guest_uuid[MAX_VXT_UUID];

		} configuring;
		struct {
			/*
			 * Handshake from guest driver showing 
			 * receipt of the VxT bus interrupts.
			 * State is "INITIALIZED"
			 */
			uint64_t configuring_status;
		} configured;
		struct {
			/*
			 * Handshake from device showing we are
			 * in running state.
			 */
			uint64_t configured_status;
		} running;
		struct {
			/*
			 * Message from device asking driver to prepare
			 * for migration.
			 */
			uint64_t flags;
		} suspending;
		struct {
			/*
			 * Message from driver acknowledging suspend
			 */
			uint64_t suspending_status;
		} suspended;
		struct {
			/*
			 * Message from device asking driver to prepare
			 * for unplug of VxT Bus Card. (clean shutdown)
			 */
			uint64_t flags;
		} revoking;
		struct {
			/*
			 * Message from driver acknowledging unplug
			 */
			uint64_t revocation_status;
		} down;
		struct {
			/*
			 * Message from driver delivered during running state
			 * indicating the driver is being removed.
			 */
			uint64_t flags;
		} unload;

		struct {
			/*
			 * Message from device emulation layer accepting
			 * unload.  Driver will respond with down.
			 */
			uint64_t unload_status;
		} unload_accept;
		struct {
			/*
			 * Message from device telling driver that
			 * the device has experienced a reset with
			 * total loss of previous running state
			 * (abrupt and immediate power cycle)
			 */
			uint64_t flags;
		} reset;
		struct {
			/*
			 * Message from device emulation layer accepting
			 * unload.  Driver will respond with down.
			 */
			uint64_t error;
		} error;
	} cmd;
	
} vxt_virtio_cfg_t;


/*
 * Flags for the vxt_virtio_cfg_t suspend struct flags field
 */
#define VXT_SUSPEND_VXT_BUS 0x1


#endif /* _VXT_VIRTIO_CFG_H_ */
