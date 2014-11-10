/*
 * vxt_signal.c:
 *
 * vxt_signal.c provides the virtual device signal capability
 * for vxt controller devices. The vxt controller operates as
 * a pseudo bus and may support both virtual and physically 
 * backed devices.  In the case of virtual devices, the means
 * of signalling is via soft interrupt.  These soft interrupts
 * are transmitted through the vxt controller and demultiplexed
 * here.
 *
 * vxt_signal supports device signal mux/demux upon a single
 * interrupt callback.
 *
 * A handle field is included to allow for formal, opaque
 * handles.  A lookup of the handle is done upon signal request
 * from the remote controller, allowing communication with and
 * between untrusted domains
 *
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
 * The following two defines are for UMI logging and they must
 * be correct or the logged serial number will be invalid and
 * misleading.
 */

#define _VXT_COMPONENT_ 1
#define _VXT_SUBSYSTEM_ 1


#include <public/kernel/os/vxtcom_embedded_os.h>
#include <public/vxt_system.h>
#include <public/symcom_dev_table.h>
#include <vxtctrlr/common/vxtcom_controller.h>
#include <public/vxt_com.h>
#include <public/kernel/vxtcom_devlib.h>
#include <public/kernel/bus/hypervisor/vxtcom_bus_ctrlr.h>
#include <public/kernel/vxt_signal.h>
#include <public/vxt_trace_entries.h>
#include <vxtctrlr/common/vxt_com_internal.h>
#include <public/kernel/vxt_module.h>
#include <vxtctrlr/common/vxt_trace.h>
#include <vxtctrlr/os/vxt_com_os.h>



typedef struct vxtsig_object {
	vxtsig_handle_t      handle;
	vxtcom_ctrlr_t       *controller;
	vxtdev_t             *device;
	vxtsig_handle_t      remote_signal;
	vxtcom_wait_queue_t  callback;  /* user wait on signal */
	uint32_t             pending_signal;
	uint32_t             pending_poll_wakeup;
	uint32_t             shutting_down;
	uint32_t             hidden;
	uint32_t             ref_cnt;
	struct vxtsig_object *next;
} vxtsig_object_t;

typedef struct vxtsig_com_header {
	uint32_t send_index;
	uint32_t recv_index;
	uint32_t restart_signal_request;
	uint32_t filler;
} vxtsig_com_header_t;

#define VXTSIG_SEC_BUFF_OFFSET 2048
#define VXTSIG_HTABLE_SIZE 64 // Must be pwr of 2 for HASH function
#define VXTSIG_MAX_SIG_CNT 20

typedef struct vxtsig_controller {
	/*
	 * lock:
	 *
	 * lock is a non-blocking spinlock
	 * used to preserve single entry to the routines
	 * manipulating signal objects.  These routines
	 * are called by the ioctl based synchronous wait
	 * and asynchronous interrupt sources.
	 *
	 */
	vxt_slock_t          lock;
	vxtcom_ctrlr_t       *controller;
	void                 *trace;
	vxtcom_wait_queue_t  signal_wait;
	uint32_t             signal_running;
	uint32_t             shutting_down;
	uint32_t             sig_cnt;
	uint32_t             int_cnt;
	vxtsig_object_t      **hash_table;
} vxtsig_controller_t;


#define VXTSIG_HASH(addr)  \
	(((addr) >> 6) & (VXTSIG_HTABLE_SIZE - 1))



/*
 *
 * vxtsig_controller_init:
 *
 * vxtsig_controller_init is called to set up
 * controller wide resources for signalling
 * objects.  In order to protect against 
 * paired controllers in untrusted guests
 * the signal handle must be opaque.  This
 * requires a lookup upon signal receipt
 * A controller level hash table is kept
 * to do lookups against signals for active
 * devices.
 *
 * Upon successful invocation, vxtsig_controller_init
 * returns a handle to the controller signalling
 * object.
 *
 *
 * Returns:
 *
 *	VXT_SUCCESS => Upon successful invocation
 *	VXT_NOMEM - unable to malloc controller object
 *	            or hash table
 */

int
vxtsig_controller_init(vxtcom_ctrlr_t *vxtcom, void **vxtsig_service_handle)
{
	vxtsig_controller_t *ctrlr_obj;
	void                *trace_table;
	uint32_t            i;
	vxt_sflags_t        lock_flags = 0;
	int                 ret;

	UMI_LOG(93, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", vxtcom);

	/*
	 * Get a trace table to follow signalling operations
	 */
	ret = vxt_trace_create(500, &trace_table);
	if ((ret != VXT_SUCCESS) || (trace_table == NULL)) {
		return VXT_NOMEM;
	}
	vxt_trace_start(trace_table);

	ctrlr_obj = vxt_kmalloc(sizeof(vxtsig_controller_t), VXT_KMEM_KERNEL);
	if (ctrlr_obj == NULL) {
		vxt_trace_destroy(trace_table);
		return VXT_NOMEM;
	}
	ctrlr_obj->sig_cnt = 0;
	ctrlr_obj->int_cnt = 0;
	ctrlr_obj->controller = vxtcom;
	ctrlr_obj->hash_table =  (vxtsig_object_t **)
	   vxt_kmalloc(sizeof(vxtsig_object_t *) * VXTSIG_HTABLE_SIZE,
	               VXT_KMEM_KERNEL);
	if (ctrlr_obj->hash_table == NULL) {
		vxt_kfree(ctrlr_obj);
		vxt_trace_destroy(trace_table);
		return VXT_NOMEM;
	}
	UMI_LOG(106, VXTPRINT_BETA_LEVEL, "ctrlr_obj %p, ctrlr hash %p\n",
                ctrlr_obj, ctrlr_obj->hash_table);
	/*
	 * Initialize the hash table
	 */
	for (i = 0; i < VXTSIG_HTABLE_SIZE; i++) {
		ctrlr_obj->hash_table[i] = NULL;
	}

	vxt_slock_init(&(ctrlr_obj->lock));
	vxt_slock(&(ctrlr_obj->lock), &lock_flags);
	VXT_TRACE(trace_table, 0, (vxtarch_word)ctrlr_obj, 
	          (vxtarch_word)vxtcom, 0);
	vxt_sunlock(&(ctrlr_obj->lock), lock_flags);
	vxtcom_init_wait(&ctrlr_obj->signal_wait);
	ctrlr_obj->signal_running = 1;
	ctrlr_obj->shutting_down = 0;
	ctrlr_obj->trace = trace_table;
	*vxtsig_service_handle = (void *)ctrlr_obj;
	
	UMI_LOG(113, VXTPRINT_DEBUG2_LEVEL, "return\n");
	return VXT_SUCCESS;
}


/*
 *
 * vxtsig_controller_shutdown:
 *
 * Deletes controller specific hash table provided.
 *
 * vxtsig_controller_shutdown checks for stray entries
 * if one is found a message stating the leak is logged.
 *
 * Note: vxtsig_controller_shutdown is called after the
 * communication process between controller and device
 * emulation has already been disabled.  Its actions are
 * limited to a recovery of signal related resources.
 * last in the 
 *
 * Returns:
 *
 *	None
 */

void
vxtsig_controller_shutdown(void *ctrlr_obj_handle)
{
	vxtsig_controller_t *ctrlr_obj;
	uint32_t            i;

	ctrlr_obj = (vxtsig_controller_t *)ctrlr_obj_handle;
	ctrlr_obj->shutting_down = 1;
	UMI_LOG(94, VXTPRINT_PROFILE_LEVEL,
	        "called, obj handle %p\n", ctrlr_obj);
	/*
	 * Wake up any senders who might be waiting for buffer
	 * space
	 */
	ctrlr_obj->signal_running = 1;
	vxtcom_wakeup_all(&ctrlr_obj->signal_wait);

	for (i = 0; i < VXTSIG_HTABLE_SIZE; i++) {
		if (ctrlr_obj->hash_table[i] != NULL) {
			/*
			 * Wakeup any clients waiting on a remote signal
			 */
			vxtcom_wakeup_all(&ctrlr_obj->hash_table[i]->callback);
			UMI_WARN(130, VXTPRINT_PRODUCT_LEVEL,
			         "hash table not empty on controller %p, "
			         "sigobj = %p\n",
			         ctrlr_obj->controller, ctrlr_obj);
		}
	}
	if (ctrlr_obj->trace != NULL) {
		vxt_trace_destroy(ctrlr_obj->trace);
	}
	vxt_kfree(ctrlr_obj->hash_table);
	vxt_kfree(ctrlr_obj);
	return;
}


/*
 * 
 * vxt_hash_insert:
 *
 * vxt_hash_insert pushes the signal object passed into
 * the hash table provided.  The hash bucket is chosen
 * based on a hash of the lable field.
 *
 * Associated controller signal object spin lock
 * must be held by the caller.
 *
 * Returns:
 *
 *		None
 *
 */

void 
vxt_hash_insert(vxtsig_controller_t *hash_obj, vxtsig_object_t *sigobj)
{
	vxtsig_object_t **table;

	UMI_LOG(95, VXTPRINT_PROFILE_LEVEL,
	        "called,  hash_obj %p, sig_obj %p\n", hash_obj, sigobj);
	table = hash_obj->hash_table;

	sigobj->next = table[VXTSIG_HASH(sigobj->handle)];

	table[VXTSIG_HASH(sigobj->handle)] = sigobj;
	
}


extern vxtsig_object_t *
vxtsig_hash_lookup(vxtsig_controller_t *hash_obj, vxtsig_handle_t handle,
                   uint32_t find_hidden);

/*
 * vxtsig_hash_lookup:
 *
 * Hash on the correct line using the handle passed.
 * Traverse the buckets found there and return the
 * object the handle is associated with.  If no
 * match is found, return NULL.
 *
 * Lookup will increment the reference count on
 * a signal object if one is found.  This insures
 * the objects persistence, until a release is
 * performed.
 *
 */

vxtsig_object_t *
vxtsig_hash_lookup(vxtsig_controller_t *hash_obj, vxtsig_handle_t handle,
                   uint32_t find_hidden)
{
	vxtsig_object_t *marker;
	vxtsig_object_t **table;

	/*
	 * This profile log is debug2 because it is on the
	 * data path
	 */
	UMI_LOG(115, VXTPRINT_DEBUG2_LEVEL,
	        "called,  hash_obj %p, handle %llx\n", hash_obj, handle);
	table = hash_obj->hash_table;
	VXT_TRACE(hash_obj->trace, 1, (vxtarch_word)hash_obj, handle, 0);

	marker = table[VXTSIG_HASH(handle)];
	if (marker == NULL) {
		UMI_LOG(107, VXTPRINT_BETA_LEVEL, "lookup failed\n");
		return NULL;
	}
	while (marker != NULL) {
		if (marker->handle ==  handle) {
			if (!find_hidden && marker->hidden) {
				/*
				 * Record exists but it is hidden from
				 * normal record lookup.
				 */
				return NULL;
			}
			marker->ref_cnt++;
			break;
		}
		marker = marker->next;
	}
	UMI_LOG(116, VXTPRINT_DEBUG2_LEVEL,
	        "lookup succeeded, sigobj %p\n", marker);
	return marker;
}


/*
 *
 * vxtsig_hash_delete:
 *
 * Find an object in the targeted hash table that
 * has a handle that matches the handle passed.
 * If one is found remove the entry.
 *
 * Returns:
 *		VXT_SUCCESS => if the entry is found and removed
 *
 *		VXT_PARM - No match was found, no action taken
 *
 */

int
vxtsig_hash_delete(vxtsig_controller_t *hashobj, vxtsig_handle_t handle)
{
	vxtsig_object_t *marker;
	vxtsig_object_t *trailer;
	vxtsig_object_t **table;

	table = hashobj->hash_table;

	UMI_LOG(97, VXTPRINT_PROFILE_LEVEL,
	        "called, hashobj %p, sigobj %llx\n",
	        hashobj, handle);
	marker = table[VXTSIG_HASH(handle)];
	if (table[VXTSIG_HASH(handle)]->handle == handle) {
		table[VXTSIG_HASH(handle)] = marker->next;
		return VXT_SUCCESS;
	}
	trailer = marker;
	marker = marker->next;
	while (marker != NULL) {
		if (marker->handle == handle) {
			trailer->next = trailer->next->next;
			UMI_LOG(108, VXTPRINT_BETA_LEVEL,
	                        "deleted entry from table, sigobj %llx\n",
			         (long long)handle);
			return VXT_SUCCESS;
		}
		trailer = marker;
		marker = marker->next;
	}
	UMI_LOG(109, VXTPRINT_BETA_LEVEL,
	        "failed to find sigobj %llx\n", (long long)handle);
	return VXT_FAIL;
}


/*
 *
 * vxtsig_object_release:
 *
 * vxtsig_object_release reduces the reference count
 * on the signal object passed in.  If the reference
 * count goes to zero, the signal object is freed
 *
 * RETURNS:
 *
 *		None
 *
 */

void
vxtsig_object_release(vxtsig_object_t *sigobj, vxtsig_controller_t *hash_obj)
{
	/*
	 * This profile log is debug2 because it is on the
	 * data path
	 */
	UMI_LOG(114, VXTPRINT_DEBUG2_LEVEL, "called, sigobj %p\n", sigobj);
	VXT_TRACE(hash_obj->trace, 2, (vxtarch_word)hash_obj, 
	          (vxtarch_word)sigobj, 0);
	if (sigobj->ref_cnt == 1) {
		UMI_LOG(96, VXTPRINT_PROFILE_LEVEL, "free the signal object\n");

		/*
		 * remove the targeted object from
		 * the controller related hash table
		 * No one else can access it.
		 */
		vxtsig_hash_delete(hash_obj, sigobj->handle);

		/*
		 * Free the signal object resources
		 */
		vxt_kfree(sigobj);
	} else {
		sigobj->ref_cnt--;
	}
}


/*
 *
 * vxtsig_object_create:
 *
 * vxtsig_object_create instantiates a vxt signal
 * structure.  This structure is bound to a virtual
 * device and can be used to signal the remote
 * device counter part or to wait for a signal from
 * the remote endpoint.
 *
 * vxtsig_object_create registers the new object
 * with a controller level hash lookup table.
 * In this way, opaque handles can be used by
 * the remote controller to effect signals against
 * the local signal objects.
 *
 * vxtsig_object_create returns a handle to
 * the newly minted signal object.
 *
 * Locks:
 *	Sig object lookup and manipulation
 *	require aquisition of the signal spin lock
 *
 * Returns:
 *
 * 		VXT_SUCCESS => upon successful invocation
 *		VXT_NOMEM - vxt_kmalloc failed.
 *
 */

int
vxtsig_object_create(vxtcom_ctrlr_t *ctrlr, vxtdev_t *device,
                     void *hash_obj_handle, vxtsig_handle_t *handle)
{
	vxtsig_object_t     *sigobj;
	vxtsig_controller_t *hash_obj;
	vxt_sflags_t        lock_flags = 0;

	hash_obj = (vxtsig_controller_t *)hash_obj_handle;
	UMI_LOG(98, VXTPRINT_PROFILE_LEVEL,
	        " called hash_obj %p, device %p\n",
	        hash_obj, device);
	sigobj = vxt_kmalloc(sizeof(vxtsig_object_t), VXT_KMEM_KERNEL);
	if (sigobj == NULL) {
		return VXT_NOMEM;
	}
	sigobj->controller = ctrlr;
	sigobj->device = device;
	sigobj->shutting_down = 0;
	sigobj->remote_signal = 0; /* must be set later */
	sigobj->pending_signal = 0;
	sigobj->pending_poll_wakeup = 0;
	sigobj->hidden = 0;
	sigobj->ref_cnt = 1;
	vxtcom_init_wait(&sigobj->callback);
	sigobj->next = NULL;

	sigobj->handle = (vxtarch_word)sigobj;
	*handle = sigobj->handle;

	vxt_slock(&(hash_obj->lock), &lock_flags);
	vxt_hash_insert(hash_obj, sigobj);
	vxt_sunlock(&(hash_obj->lock), lock_flags);

	UMI_LOG(129, VXTPRINT_DEBUG_LEVEL,
	         "returns, hash_obj %p, sig_obj %p\n",
	         hash_obj, sigobj);
	return VXT_SUCCESS;

}

/*
 *
 * vxtsig_object_destroy:
 *
 * vxtsig_object_destroy frees the resources associated
 * with a targeted signal object.  The object is first
 * found and removed from the associated hash table.
 *
 * Locks:
 *	Sig object lookup and manipulation
 *	require aquisition of the signal spin lock
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon successful clean-up of 
 *		               the targeted signal object
 *
 *		VXT_FAIL - Unable to find object in hash 
 *		           table.
 *
 */

int
vxtsig_object_destroy(void *hash_obj_handle, vxtsig_handle_t handle)
{
	vxtsig_object_t     *sigobj;
	vxtsig_controller_t *hash_obj;
	vxt_sflags_t        lock_flags = 0;

	hash_obj = (vxtsig_controller_t *)hash_obj_handle;

	UMI_LOG(99, VXTPRINT_PROFILE_LEVEL,
	        "called, hash_obj %p, handle %llx\n",
	        hash_obj, handle);
	vxt_slock(&(hash_obj->lock), &lock_flags);
	/*
	 * find the signaling object that corresponds
	 * to the handle passed.  Note, if the object is
	 * not there or already hidden, do not drop a 
	 * reference count.
	 */
	sigobj = vxtsig_hash_lookup(hash_obj, handle, 0);
	if (sigobj == NULL) {
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_FAIL;
	}
	sigobj->shutting_down = 1;
	/*
	 * Wakeup any clients waiting on a remote signal
	 */
	sigobj->pending_signal = 1;
	sigobj->pending_poll_wakeup = 1;
	vxtcom_wakeup_all(&sigobj->callback);
	/*
	 * Wake up any senders who might be waiting for buffer
	 * space.  This will potentially wakeup other waiters
	 * but this is benign.  If there is still no room in
	 * the signalling buffer, they will simply go back to
	 * sleep.
	 */
	hash_obj->signal_running = 1;
	vxtcom_wakeup_all(&hash_obj->signal_wait);
	/*
	 * hide the hash lookup of this signal to all but reference
	 * delete code.  Remove the object from the actual hash only
	 * when the last reference count goes away.  This allows the
	 * external holders of references to delete their outstanding
	 * references while holding the lookup handle only.
	 */
	sigobj->hidden = 1;
	/*
	 * Remove the configuration reference on
	 * the signal object.  Ultimately this
	 * frees the signal object resources when
	 * the last user exits
	 */

	vxtsig_object_release(sigobj, hash_obj);
	/*
	 * first release cancels lookup
	 * at the beginning of the routine
	 */
	vxtsig_object_release(sigobj, hash_obj);
	vxt_sunlock(&(hash_obj->lock), lock_flags);

	return VXT_SUCCESS;
}


/*
 *
 * vxtsig_object_signal:
 *
 * vxtsig_object_signal wakes up any waiters on the
 * targeted signal object.  This call should not
 * be confused with vxtsig_signal which signals
 * data available to the remote party.  This call
 * is made to wake local waiters when an event
 * such as shutdown has been ordered
 *
 * Locks:
 *	Sig object lookup and manipulation
 *	require aquisition of the signal spin lock
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon successful signaling of 
 *		               the targeted signal object
 *
 *		VXT_FAIL - Unable to find object in hash 
 *		           table.
 *
 */

int
vxtsig_object_signal(void *hash_obj_handle, vxtsig_handle_t handle)
{
	vxtsig_object_t     *sigobj;
	vxtsig_controller_t *hash_obj;
	vxt_sflags_t        lock_flags = 0;

	hash_obj = (vxtsig_controller_t *)hash_obj_handle;

	UMI_LOG(100, VXTPRINT_PROFILE_LEVEL,
	        "called, hash_obj %p, handle %llx\n",
	        hash_obj, handle);
	vxt_slock(&(hash_obj->lock), &lock_flags);
	/*
	 * find the signaling object that corresponds
	 * to the handle passed.
	 */
	sigobj = vxtsig_hash_lookup(hash_obj, handle, 0);
	if (sigobj == NULL) {
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_FAIL;
	}
	sigobj->shutting_down = 1;
	/*
	 * Wakeup any clients waiting on a remote signal
	 */
	sigobj->pending_signal = 1;
	sigobj->pending_poll_wakeup = 1;
	vxtcom_wakeup_all(&sigobj->callback);
	/*
	 * Wake up any senders who might be waiting for buffer
	 * space.  This will potentially wakeup other waiters
	 * but this is benign.  If there is still no room in
	 * the signalling buffer, they will simply go back to
	 * sleep.
	 */
	hash_obj->signal_running = 1;
	vxtcom_wakeup_all(&hash_obj->signal_wait);
	vxtsig_object_release(sigobj, hash_obj);
	vxt_sunlock(&(hash_obj->lock), lock_flags);

	return VXT_SUCCESS;
}


/*
 *
 * vxtsig_object_register_remote:
 *
 * vxtsig_object_register_remote updates the remote signal name
 * field in the targeted signal object.  No concern is given
 * to the previous name, it is overwritten.
 *
 * Locks:
 *	Sig object lookup and manipulation
 *	require aquisition of the signal spin lock
 *
 *
 *
 * Returns:
 *		VXT_SUCCESS => upon successful update
 *
 *		VXT_FAIL - if hash lookup fails.
 *
 */

int
vxtsig_object_register_remote(void *hash_obj_handle,
                              vxtsig_handle_t handle, vxtsig_handle_t remote)
{
	vxtsig_object_t     *sigobj;
	vxtsig_controller_t *hash_obj;
	vxt_sflags_t        lock_flags = 0;

	hash_obj = (vxtsig_controller_t *)hash_obj_handle;
	UMI_LOG(101, VXTPRINT_PROFILE_LEVEL,
	        "called, hash_obj %p, handle %llx, remote %llx\n",
	        hash_obj, handle, remote);

	vxt_slock(&(hash_obj->lock), &lock_flags);
	/*
	 * find the signaling object that corresponds
	 * to the handle passed.
	 */
	sigobj = vxtsig_hash_lookup(hash_obj, handle, 0);
	if (sigobj == NULL) {
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_FAIL;
	}

	sigobj->remote_signal = remote;
	vxtsig_object_release(sigobj, hash_obj);

	vxt_sunlock(&(hash_obj->lock), lock_flags);
	return VXT_SUCCESS;
	
}


/*
 *
 * vxtsig_interrupt:
 *
 * Note:  This call is made in the context of a primary
 * level interrupt.  It is made with no mutex locks held
 * and must not block.  The hash object existence must
 * be guaranteed for the duration of the call.
 *
 * In order to protect the shared structures a spin
 * lock is employed.
 *
 * 
 */

void
vxtsig_interrupt(void *vxtsig_ctrlr_handle, void *buffer)
{
	volatile vxtsig_com_header_t *sig_head;
	volatile vxtsig_handle_t     *sig_list;
	vxtsig_controller_t          *hash_obj;
	vxtsig_object_t              *sigobj;
	uint32_t                     signal_request_index;
	uint32_t                     last_index;
	uint32_t                     next_index;
	uint32_t                     signal;
	uint32_t                     signal_cnt = 0;
	vxt_sflags_t                 lock_flags = 0;

	hash_obj = (vxtsig_controller_t *) vxtsig_ctrlr_handle;
	hash_obj->int_cnt++;
	UMI_LOG(117, VXTPRINT_DEBUG2_LEVEL,
	        "called, hashobj %p, read buffer %p\n",
	        vxtsig_ctrlr_handle, buffer);


	vxt_slock(&(hash_obj->lock), &lock_flags);

	VXT_TRACE(hash_obj->trace, 5,
	          (vxtarch_word)hash_obj,
	          (vxtarch_word)buffer,
	          (vxtarch_word)hash_obj->int_cnt);
	/*
	 * First, a little house-keeping.  Our
	 * remote partner may be indicating the
	 * receipt of signals, check and see if
	 * our signal sender is waiting.
	 */

	if (hash_obj->signal_running == 0) {
		hash_obj->signal_running = 1;
		vxtcom_wakeup_all(&hash_obj->signal_wait);
	}

	/*
	 * Now, check out the remote signal requests
	 */

	sig_head = (vxtsig_com_header_t *)buffer;
	sig_list = (vxtsig_handle_t *)(sig_head + 1);
	UMI_LOG(118, VXTPRINT_DEBUG2_LEVEL,
	        "sig_head %p, sig_list %p\n", sig_head, sig_list);

process_signals:
	/*
	 * Note: we choose to use a static pointer for
	 * the send index to avoid staying in the
	 * interrupt handler forever.
	 */
	last_index = sig_head->send_index;

	signal_request_index = sig_head->recv_index; 
	UMI_LOG(119, VXTPRINT_DEBUG2_LEVEL,
	        "receive index 0x%x, send index 0x%x\n",
	        last_index, signal_request_index);
	/*
	 * Determine if we are full.  If so, we will
	 * need to wake-up the blocked sender.  Note:
	 * we freeze the view by grabbing the index
	 * values in local variables.  We will
	 * receive whatever these indicies provide.
	 * Anything that shows up after the freeze
	 * will show up in our final signal check
	 * below.  This check takes place after we
	 * update the receive pointer.  If no more
	 * requests are made, the receive ptr and
	 * send pointer will be equal and hence we
	 * do not signal.  Doing the check after
	 * update of the pointer and using local
	 * indicies guarantees that any new requests
	 * granted after the local send variable
	 * has been filled in will cause a signal.
	 *
	 * ORDERING:  CDY  NOTE check that page
	 * type bits are sufficient to guarantee
	 * that no cache is used.  If not protected
	 * we must synchronize between receive index
	 * update and local variable check.
	 */
	next_index = last_index + 1;
	if (next_index == VXTSIG_MAX_SIG_CNT) {
		next_index = 0;
	}
	if (next_index == signal_request_index) {
		signal = 1;
	} else {
		signal = 0;
	}

	while (signal_request_index != last_index) {
		UMI_LOG(120, VXTPRINT_DEBUG2_LEVEL, 
		        "signal index 0x%x, signal token %llx\n",
		        signal_request_index,
		        sig_list[signal_request_index]);
		sigobj = vxtsig_hash_lookup(hash_obj,
		                            sig_list[signal_request_index], 0);
		if (sigobj != NULL) {
			VXT_TRACE(hash_obj->trace, 6,
			          (vxtarch_word)sigobj, 0, 0);
			sigobj->pending_signal = 1;
			sigobj->pending_poll_wakeup = 1;
			vxtcom_wakeup_all(&sigobj->callback);
			vxtsig_object_release(sigobj, hash_obj);
		}
		signal_request_index++;
		if (signal_request_index == VXTSIG_MAX_SIG_CNT) {
			signal_request_index = 0;
		}
		signal_cnt++;
	}

	/*
	 * Indicate we have read, if no more
	 * work comes in before the next test
	 * we can leave without signaling explicitly
	 * Order of this update and following test
	 * is important.
	 * use compare and swap as a sync primitive
	 * after recv index update 
	 */
	sig_head->recv_index = last_index;

	vxt_mb();

	/*
	 * Has more work come in?
	 * If so, go back to the top of the loop and keep
	 * going.
	 */
	if (sig_head->send_index != sig_head->recv_index) {
		goto process_signals;
	}

	/*
	 * This next test provides capture of a possible 
	 * dynamic wait by the signal sender.  If we are not
	 * full of signals at the beginning of this run
	 * we may still hit an apparent full condition as 
	 * we are processing. i.e. Lets assume we have
	 * 1 less than full condition when we enter.
	 * Our first check indicates we are not full so
	 * we will not bother to wake the remote.  As we
	 * are processing and before we update the receive
	 * field, two more signal requests are made from the
	 * remote side, blocking the remote.  When we finish
	 * processing and proceed to look for new arrivals
	 * we find one.  Clearly are initial test will not
	 * catch this condition.  However, we can rely on
	 * a simple count of signals processed to know we 
	 * MAY have blocked on the remote side.
	 */
	if (signal_cnt >= (VXTSIG_MAX_SIG_CNT - 1)) {
		signal = 1;
	}
	if (signal) {
		sig_head->restart_signal_request = 1;
		/*
		 * Signal remote that we have done work
		 * and unblock. 
		 */
		hash_obj->sig_cnt++;
		vxtcom_event_signal(hash_obj->controller->bus.sig_evt);
		VXT_TRACE(hash_obj->trace, 13, 
		          (vxtarch_word)hash_obj,
		          hash_obj->sig_cnt, 0);
	}

	vxt_sunlock(&(hash_obj->lock), lock_flags);
	return;
}


/*
 *
 * vxtsig_wait:
 *
 * The vxt controller is locked on entry, 
 * the lock must be released while waiting
 * this is accomplished in the vxtcom_ctrlr_wait
 * function.
 *
 * Returns:
 *
 *	VXT_SUCCESS => upon successful invocation
 *                     we have been signaled
 *
 *	VXT_FAIL - Unable to hash handle, no
 *	           signal received.
 *
 *	VXT_PARM - VXT Controller is no longer
 *	           valid
 *
 */

int
vxtsig_wait(vxtcom_ctrlr_t *vxt_com,
            void *vxtsig_ctrlr_handle,
            vxtsig_handle_t handle)
{
	vxtsig_object_t     *sigobj;
	vxtsig_controller_t *hash_obj;
	vxt_sflags_t        lock_flags = 0;
	int                 ret;

	hash_obj = (vxtsig_controller_t *)vxtsig_ctrlr_handle;
	UMI_LOG(121, VXTPRINT_DEBUG2_LEVEL,
	        "called, controller %p, sigobj %p, handle %llx\n",
	        vxt_com, vxtsig_ctrlr_handle, handle);

	vxt_slock(&(hash_obj->lock), &lock_flags);
	VXT_TRACE(hash_obj->trace, 7, (vxtarch_word)hash_obj, handle, 0);
	/*
	 * find the signaling object that corresponds
	 * to the handle passed.
	 */
	sigobj = vxtsig_hash_lookup(hash_obj, handle, 0);
	if (sigobj == NULL) {
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_FAIL;
	}

	/*
	 * Signal came in while we 
	 * were away?
	 */
	if (sigobj->pending_signal) {
		UMI_LOG(122, VXTPRINT_DEBUG2_LEVEL, "found pending signal\n");
		sigobj->pending_signal = 0;
		vxtsig_object_release(sigobj, hash_obj);
		VXT_TRACE(hash_obj->trace, 8, 
		          (vxtarch_word)hash_obj, (vxtarch_word)sigobj, handle);
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_SUCCESS;
	}
	vxt_sunlock(&(hash_obj->lock), lock_flags);

	ret  = vxtcom_ctrlr_wait(hash_obj->controller,
	                         &(sigobj->callback), (long *)
                                 &(sigobj->pending_signal), 0);
        if (ret == VXT_PARM) {
		/*
		 *  The controller is no longer live
		 *  The caller must know this and not
		 *  try to access the controller object
		 *  or unlock the controller
		 */
		return ret;
	}
	UMI_LOG(123, VXTPRINT_DEBUG2_LEVEL,
	        "returning after wait, handle %llx\n", handle);

	vxt_slock(&(hash_obj->lock), &lock_flags);
	VXT_TRACE(hash_obj->trace, 8, 
	          (vxtarch_word)hash_obj, (vxtarch_word)sigobj, handle);
	sigobj->pending_signal = 0;
	vxtsig_object_release(sigobj, hash_obj);
	vxt_sunlock(&(hash_obj->lock), lock_flags);
	return VXT_SUCCESS;

}


/*
 * 
 * vxtsig_poll:
 *
 * vxtsig_poll is the poll version of the vxtsig_wait
 * function, like vxtsig_wait the controller is locked
 * on entry.
 *
 * vxtsig_poll uses the polling primitive instead of the
 * wait primitive, unlike vxtcom_ctrlr_wait, vxtcom_poll_wait
 * does not actually block, therfore the controller lock
 * does not need to be dropped and re-acquired.
 *
 * It is strongly recommended that applications use the
 * ioctl wait method rather than the poll method as
 * the poll method has unbounded overhead based on the
 * number of poll elements being waited on.  The 
 * poll method has been provided to support legacy
 * applications that have been written without regard
 * to performance.
 *
 * NOTE:  The actual reference count holding the signal
 * object is acquired in the poll open routine and
 * discarded in the poll close.  The reference obtained
 * during the vxtsig_object_lookup here cannot be held
 * outside of the routine as there is no callback on the
 * wakeup.
 *
 *
 * Returns:
 *
 *	VXT_SUCCESS => upon successful invocation
 *                     we have been signaled
 *
 *	VXT_FAIL - Unable to hash handle, no
 *	           signal received.
 *
 *	VXT_PARM - VXT Controller is no longer
 *	           valid
 *
 */


int
vxtsig_poll(vxtcom_ctrlr_t *vxt_com,
            void *vxtsig_ctrlr_handle,
            vxtsig_handle_t handle,
            void *file, void *wait,
            int *revents)
{
	vxtsig_object_t     *sigobj;
	vxtsig_controller_t *hash_obj;
	vxt_sflags_t        lock_flags = 0;

	hash_obj = (vxtsig_controller_t *)vxtsig_ctrlr_handle;
	/*
	 * This profile log is debug2 because it is on the
	 * data path
	 */
	UMI_LOG(124, VXTPRINT_DEBUG2_LEVEL,
	        "called, controller %p, sigobj %p, handle %llx\n",
	        vxt_com, vxtsig_ctrlr_handle, handle);

	vxt_slock(&(hash_obj->lock), &lock_flags);
	VXT_TRACE(hash_obj->trace, 9, 
	          (vxtarch_word)hash_obj, handle, 0);
	/*
	 * find the signaling object that corresponds
	 * to the handle passed.
	 */
	sigobj = vxtsig_hash_lookup(hash_obj, handle, 0);
	if (sigobj == NULL) {
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		*revents |= VXTSIG_HUP;
		return VXT_SUCCESS;
	}



	/*
	 * Most versions of vxt_poll_wait do not
	 * block.  However, some early Linux
	 * implementations do allocations that can
	 * block waiting for memory.  The sigobj
	 * has its reference count elevated so
	 * wa can drop the lock here. This is
	 * an unnecessary expense for well written
	 * handlers though and so we leave the 
	 * unlock operation to the poll routine.
	 */

	vxt_poll_wait(file, &(sigobj->callback), wait,
	              &(hash_obj->lock), &lock_flags);
	*revents = 0;
	if (sigobj->pending_poll_wakeup) {
		sigobj->pending_poll_wakeup = 0;
		*revents = VXTSIG_IN;
	}
	if (sigobj->shutting_down == 1) {
		*revents |= VXTSIG_HUP;
	}
	VXT_TRACE(hash_obj->trace, 12, 
	          (vxtarch_word)sigobj, (unsigned int)*revents, 0);
	vxtsig_object_release(sigobj, hash_obj);

	vxt_sunlock(&(hash_obj->lock), lock_flags);

	return VXT_SUCCESS;
}


/*
 *
 * vxtsig_inc_ref:
 *
 * External call to increment the reference count on a signal
 * object.  This allows the object to persist after the 
 * device it is associated with has died and been freed.
 * This is necessary to accommodate any straggling waiters
 * on the poll function.  Poll cannot hold a reference
 * of its own because it does not block locally.
 *
 * Locks:
 *	Sig object lookup and manipulation
 *	require aquisition of the signal spin lock
 *
 * Returns:
 *
 * 	VXT_SUCCESS => Upon successful invocation
 *	VXT_FAIL - object does not exist.
 *
 */

int
vxtsig_inc_ref(void *vxtsig_ctrlr_handle, vxtsig_handle_t handle)
{
	vxtsig_object_t     *sigobj;
	vxtsig_controller_t *hash_obj;
	vxt_sflags_t        lock_flags = 0;

	hash_obj = (vxtsig_controller_t *)vxtsig_ctrlr_handle;
	UMI_LOG(102, VXTPRINT_PROFILE_LEVEL,
	        "called, sigobj %p, handle %llx\n",
	        vxtsig_ctrlr_handle, handle);

	vxt_slock(&(hash_obj->lock), &lock_flags);
	/*
	 * find the signaling object that corresponds
	 * to the handle passed.
	 */
	sigobj = vxtsig_hash_lookup(hash_obj, handle, 0);
	if (sigobj == NULL) {
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_FAIL;
	}
	/*
	 * The reference count is incremented on lookup
	 * Nothing more to do.
	 */
	vxt_sunlock(&(hash_obj->lock), lock_flags);
	return VXT_SUCCESS;

}


/*
 *
 * vxtsig_dec_ref:
 *
 * Decrements the reference count and may ultimately cause
 * the release of the signal object if the reference
 * count goes to zero.
 *
 * Locks:
 *	Sig object lookup and manipulation
 *	require aquisition of the signal spin lock
 *
 * Returns:
 *
 * 	VXT_SUCCESS -> Upon successful invocation
 *
 *	VXT_FAIL -> object does not exist.
 */

int
vxtsig_dec_ref(void *vxtsig_ctrlr_handle, vxtsig_handle_t handle)
{
	vxtsig_object_t     *sigobj;
	vxtsig_controller_t *hash_obj;
	vxt_sflags_t        lock_flags = 0;

	hash_obj = (vxtsig_controller_t *)vxtsig_ctrlr_handle;
	UMI_LOG(103, VXTPRINT_PROFILE_LEVEL,
	        "called, sigobj %p, handle %llx\n",
	        vxtsig_ctrlr_handle, handle);

	vxt_slock(&(hash_obj->lock), &lock_flags);
	/*
	 * find the signaling object that corresponds
	 * to the handle passed.
	 */
	sigobj = vxtsig_hash_lookup(hash_obj, handle, 1);
	if (sigobj == NULL) {
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_FAIL;
	}
	/*
	 * Release the reference gained on the 
	 * lookup and then the one requested.
	 */
	vxtsig_object_release(sigobj, hash_obj);
	vxtsig_object_release(sigobj, hash_obj);

	vxt_sunlock(&(hash_obj->lock), lock_flags);
	return VXT_SUCCESS;
}


/*
 *
 * vxtsig_signal:
 *
 * vxtsig_signal is called from a vxtctrlr
 * ioctl.  It therefore is invoked with
 * the vxtctrlr mutex lock on.  Based on
 * this, the vxtctrlr wait routine may
 * be called.  The vxtctrlr mutex is 
 * held during configuration events where
 * vxt_kmalloc operations take place.
 * To guarantee latencies, vxtsig_signal
 * must in future either be entered 
 * through a different file descriptor
 * or vxt_kmalloc will have to do malloc
 * tries and drop the lock if storage
 * is not available.
 *
 * vxtsig_signal places signal requests
 * in a shared memory buffer.  If the buffer
 * turns out to be full, the caller
 * is made to wait for a buffer available
 * event.  Each endpoint in the controller
 * pair has access to a send buffer. 
 * Whenever a receiver determines that
 * a wait condition may have arisen, i.e.
 * it determines that the send and receive
 * indicies are 1 from equal, it sends an event
 * of its own.  It is assumed the other 
 * side will check its send buffers and
 * will wake any waiters.
 *
 * The controller signal lock must be
 * acquired.  It must be dropped
 * prior to wait when wait is necessary.
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS => remote party signaled
 *		VXT_FAIL - sigobj is not valid
 *		VXT_PARM - Controller is gone - i.e and not locked
 * 
 *
 */

int
vxtsig_signal(vxtcom_ctrlr_t *vxtcom, void *vxtsig_ctrlr_handle,
              vxtsig_handle_t handle, void *buffer)
{
	vxtsig_object_t               *sigobj;
	volatile vxtsig_com_header_t  *sig_head;
	volatile vxtsig_handle_t      *sig_array;
	vxtsig_controller_t           *hash_obj;
	volatile uint32_t             next_index;
	volatile uint32_t             prev_index;
	uint32_t                      signal;
	vxt_sflags_t                  lock_flags = 0;
	int                           ret;

	hash_obj = (vxtsig_controller_t *)vxtsig_ctrlr_handle;
	
	UMI_LOG(125, VXTPRINT_DEBUG2_LEVEL,
	        "called on ctrlr %p, sig_obj %p, handle %llx,  buffer %p\n",
	        vxtcom, vxtsig_ctrlr_handle, handle, buffer);

	vxt_slock(&(hash_obj->lock), &lock_flags);

	VXT_TRACE(hash_obj->trace, 3, handle, hash_obj->sig_cnt, 0);
	
signal_retry:
	sig_head = (vxtsig_com_header_t *)buffer;
	if (hash_obj->shutting_down == 1) {
		UMI_LOG(110, VXTPRINT_BETA_LEVEL,
	                "can't signal remote object shutting down\n");
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_FAIL;
	}

	next_index = sig_head->send_index + 1;
	if (next_index == VXTSIG_MAX_SIG_CNT) {
		next_index = 0;
	}

	hash_obj->signal_running = 0;
	if (next_index == sig_head->recv_index) {
		UMI_LOG(111, VXTPRINT_BETA_LEVEL,
	                "sig buf is full, send_index 0x%x, rcv_index  0x%x\n",
		        sig_head->send_index,
	                sig_head->recv_index);
		VXT_TRACE(((vxtsig_controller_t *)
		           (vxtcom->bus.dev_events))->trace,
		           10, (vxtarch_word)hash_obj, 0, 0);


		/*
		 * Our buffer is full of requests, go to
		 * sleep waiting for space
		 */
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		ret = vxtcom_ctrlr_wait(vxtcom,
		                        &(hash_obj->signal_wait), (long *)
		                        &(hash_obj->signal_running), 0);
       		if (ret == VXT_PARM) {
			/*
			 *  The controller is no longer alive
			 *  The caller must know this and not
			 *  try to access the controller object
			 *  or unlock the controller
			 */
			return ret;
		}
		vxt_slock(&(hash_obj->lock), &lock_flags);
		VXT_TRACE(((vxtsig_controller_t *)
		           (vxtcom->bus.dev_events))->trace,
		           11, (vxtarch_word)hash_obj, 0, 0);
		goto signal_retry;
	} else {
		hash_obj->signal_running = 1;
	}

	/*
	 * find the signaling object that corresponds
	 * to the handle passed.
	 */
	UMI_LOG(126, VXTPRINT_DEBUG2_LEVEL,
	        "call vxtsig_hash_lookup, handle %llx\n", handle);
	sigobj = vxtsig_hash_lookup(hash_obj, handle, 0);
	if (sigobj == NULL) {
		UMI_LOG(112, VXTPRINT_BETA_LEVEL,
	                "call vxtsig_hash_lookup failed\n");
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_FAIL;
	}

	sig_array = (vxtsig_handle_t *)(sig_head + 1);
	sig_array[sig_head->send_index] = sigobj->remote_signal;
	vxtsig_object_release(sigobj, hash_obj);
	/*
	 * Capture the existing send_index and use
	 * it to insure that we signal the remote
	 * controller when the signal buffer has
	 * gone empty.  The order is important here
	 * and we may need to flush cache before the check
	 * of recv_index.  586 and beyond do not guarantee
	 * order of access on volatile variables.  This is
	 * a shame since it is the one way to guarantee 
	 * maximum throughput with minimum overhead. i.e.
	 * real streaming.
	 *
	 */

	prev_index = sig_head->send_index;
	/*
	 * Flush to make certain the data arrives before the
	 * indication that there is data to be had.
	 */
	/*
	 * use spinlock, i.e. compare and swap
	 * primitive, or MFENCE to flush processor cache,
	 * protecting against multi-cpu out of order
	 * processing.  (Intel 586 and above processors)
	 * 1.  Flush to make certain the data arrives before the
	 *     indication that there is data to be had.
	 * 2.  Flush to get a fresh perspective on the recv_index
	 */
	vxt_mb();
	sig_head->send_index = next_index;
	vxt_mb();
	next_index++;
	if (next_index == VXTSIG_MAX_SIG_CNT) {
		next_index = 0;
	}

	/*
	 * If we became empty, according to our
	 * post update assement of the receive
	 * index, or we are full, send a signal
	 * to process the events.
	 */


	if ((prev_index == sig_head->recv_index) || 
	    (next_index == sig_head->recv_index)) {
		signal = 1;
	} else {
		signal = 0;
	}

	if (signal) {
		UMI_LOG(127, VXTPRINT_DEBUG2_LEVEL,
	                "call notify_remote_via_evtchn\n");
 		/*
		 * Interrupt remote signal processing
		 */
		hash_obj->sig_cnt++;
		vxtcom_event_signal(vxtcom->bus.sig_evt);
		VXT_TRACE(hash_obj->trace, 13, 
		          (vxtarch_word)hash_obj,
		          hash_obj->sig_cnt, 0);
	}

	vxt_sunlock(&(hash_obj->lock), lock_flags);
	return VXT_SUCCESS;
}


/*
 * vxtsig_check_pending_signals
 *
 * vxtsig_check_pending signals must be called out of 
 * every vxtsig secondary interrupt.  This calls
 * checks for pending signal requests that didn't get
 * serviced on the last transaction.
 *
 * Returns:
 *
 *		None
 *
 */

void
vxtsig_check_pending_signals(vxtcom_ctrlr_t *vxtcom, 
                             vxtsig_controller_t *ctrlr_obj, void *buffer)
{
	volatile vxtsig_com_header_t  *sig_head;
	
	sig_head = (vxtsig_com_header_t *)buffer;

	UMI_LOG(128, VXTPRINT_DEBUG2_LEVEL,
	        "called on ctrlr %p, buffer %p\n",
	        vxtcom, buffer);
	VXT_TRACE(((vxtsig_controller_t *)(vxtcom->bus.dev_events))->trace,
	          4, (vxtarch_word)vxtcom->bus.dev_events, 0, 0);
	if (sig_head->restart_signal_request) {
		sig_head->restart_signal_request = 0;
 		/*
		 * Interrupt remote signal processing
		 */
		ctrlr_obj->sig_cnt++;
		vxtcom_event_signal(vxtcom->bus.sig_evt);
		VXT_TRACE(ctrlr_obj->trace, 13, 
		          (vxtarch_word)ctrlr_obj,
		          ctrlr_obj->sig_cnt, 0);
	}
}


/*
 * vxtsig_init_send_buf:
 *
 * vxtsig_init_send_buf initializes the communication
 * buffer used for signaling that is shared between
 * controllers.
 *
 * The buffer supports a number of signal request
 * entry fields.  Two counters are kept, a send
 * and a receive.  The two counter chase each other
 * around the buffer, indexing the request entry
 * fields.  When the two counters are equal, the
 * request entry fields are all empty.
 *
 *
 * Returns:
 *
 *		None
 *
 */

void
vxtsig_init_send_buf(void *buffer)
{
	vxtsig_com_header_t *sig_head;

	UMI_LOG(104, VXTPRINT_PROFILE_LEVEL, "called on buffer %p\n", buffer);
	sig_head = (vxtsig_com_header_t *)buffer;
	sig_head->send_index = 0;
	sig_head->recv_index = 0;
	sig_head->restart_signal_request = 0;

	return;

}


/*
 * vxtsig_trace_playback
 *
 * vxtsig_trace_playback dumps the contents of the controller
 * pair specific object, (the vxtsig_controller_t object), 
 * trace file.  This file contains the traces for the
 * activity of all the devices on the controller pair,
 * not just the work on the targeted device.  Still the
 * device serves as a check to make certain that the right
 * controller pair has been obtained.
 *
 * Returns:
 *
 *	VXT_SUCCESS => Upon successful invocation
 *	VXT_FAIL - Device handle is not valid.
 *
 */

int
vxtsig_trace_playback(void *vxtsig_ctrlr_handle,
                      vxtsig_handle_t sig_handle,
                      void *buffer, uint32_t *entry_count)
{
        vxtsig_controller_t           *hash_obj;
        vxtsig_object_t               *sigobj;
	vxt_sflags_t                  lock_flags = 0;
	int                           ret;

        hash_obj = (vxtsig_controller_t *)vxtsig_ctrlr_handle;
	UMI_LOG(105, VXTPRINT_PROFILE_LEVEL,
	        "called, hash_obj %p, handle %llx\n",
	        (char *)hash_obj, sig_handle);
	vxt_slock(&(hash_obj->lock), &lock_flags);

	sigobj = vxtsig_hash_lookup(hash_obj, sig_handle, 0);
	
	if (sigobj == NULL) {
		vxt_sunlock(&(hash_obj->lock), lock_flags);
		return VXT_FAIL;
	}

	vxtsig_object_release(sigobj, hash_obj);

	ret = vxt_trace_playback(hash_obj->trace, buffer, entry_count);
	vxt_sunlock(&(hash_obj->lock), lock_flags);
	return ret;
}
