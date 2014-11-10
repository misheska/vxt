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
#include <vxtctrlr/common/vxt_com_internal.h>
#include <public/kernel/vxt_module.h>
#include <vxtctrlr/os/vxt_com_os.h>
#include <public/vxt_auth.h>
#include <public/kernel/vxt_auth_db.h>



extern int vxtmsg_dev_load_lib(void *controller_handle);
extern int vxtmsg_dev_unload_lib(void *vxtcom_handle);



static void
vxtcom_destroy_controller(vxtcom_ctrlr_t *bus_instance);

static void
vxtcom_unplug_devices(vxtcom_ctrlr_t *instance, int signal_remote);

/* 
CDY needed only for test msleep
#include <linux/delay.h>
 */

/* 
 * Note:  Each device type has a vxt controller facing side and 
 * a user facing side
 * The User facing side is device specific but many common elements are 
 * expected to be present.
 *
 * 1:  To maximize code reuse and simplicity, direct exposure of the
 *     queue construct is expected in the form of send and receive
 *     queues.
 * 2:  All devices have inherent built-in streaming capability through
 *     the use of the shared memory construct and the underlying
 *     needs signal and signal mechanism.
 * 3:  All software devices will use the vxt controller default signalling
 *     method.  However, hardware assisted devices may be able to
 *     signal through a shared memory location.
 * 4:  Queue related QOS mechanisms must fit into the common model
 *     to facilitate virtual system management software.
 *
 */



typedef uint64_t vxtdev_label_t;



unsigned int vxt_aggregate_device_count = 0; /* agg dev #ing across ctrlrs */
unsigned int controller_count = 0;   /* controller file naming instance value */
vxtcom_ctrlr_t *active_controllers = NULL;
vxt_mlock_t active_ctrlr_lock;

static vxtcom_ctrlr_hdr_t *active_ctrlr_pairs = NULL;
static vxt_mlock_t ctrlr_pair_lock;

/*
 * Simple linear table for now
 */
vxt_device_sm_t *sm_token_table = NULL;
vxt_mlock_t      sm_token_lock;



/*
 *  Internal helper routines
 */

/*
 *
 * vxtcom_get_slot_handle:
 *
 * find an empty handle to associate with a new device
 * This handle is passed to the user level caller and 
 * works as an ordinal directed list value, allowing for
 * sequential queries.
 */

int
vxtcom_get_slot_handle(vxtcom_ctrlr_t *vxtcom, vxt_slot_t *slot)
{
	int ret;


	if (vxtcom->vxtcom_slots == 0xFFFFFFFF) {
		/*
		 * From here on we must check all new
		 * requests to see if handle already
		 * in use
		 */
		vxtcom->vxtcom_slots = 1;
		vxtcom->slot_wrap = 1;
      
	}

	if (vxtcom->slot_wrap) {
		vxt_slot_t *old_slot;
		uint handle_index;

		handle_index = vxtcom->vxtcom_slots - 1;
		while (handle_index != vxtcom->vxtcom_slots) {
			ret = vxtcom_find_slot(vxtcom, 
			                       vxtcom->vxtcom_slots,
			                       &old_slot);
			if (ret == VXT_SUCCESS) {
				/*
				 * oops, handle in use, try next one
				 */
				vxtcom->vxtcom_slots++;
			} else {
				break;
			}
		}
		if (vxtcom->vxtcom_slots == handle_index) {
			return VXT_RSRC;
		}
	}

	slot->handle = vxtcom->vxtcom_slots;
	vxtcom->vxtcom_slots++;

	/*
	 * CDY: Order this list for faster lookup
	 */
	
	slot->next = vxtcom->live_slots;
	vxtcom->live_slots = slot;

	return 0;
   
}


/*
 *
 * vxtcom_find_slot:
 *
 * traverse list of active slots to find the structure
 * corresponding to the handle passed.  If found, the 
 * pointer to the associated structure is returned. 
 *
 *
 * Returns:
 *
 *    VXT_SUCCESS => Success
 *    VXT_PARM -  handle passed does not correspond to a 
 *                live slot structure.
 *
 */

int
vxtcom_find_slot(vxtcom_ctrlr_t *vxtcom, int handle, vxt_slot_t **user_slot) {

	vxt_slot_t *slot;

	slot = vxtcom->live_slots;
	while (slot != NULL) {
		if (slot->handle == handle) {
			*user_slot = slot;
			return VXT_SUCCESS;
		}
		slot = slot->next;
	}
	return VXT_PARM;
   
}


/*
 * vxtcom_free_slot_handle:
 *
 * Looks up a slot handle in the live_slots
 * table via the handle passed.  If
 * an entry is found it is removed from
 * the list and returned
 *
 *
 * Returns:
 *
 *   0  =>   Success
 *
 *	VXT_PARMS - Slot not found
 *
 */

int
vxtcom_free_slot_handle(vxtcom_ctrlr_t *vxtcom, uint32_t old_handle)
{
	vxt_slot_t *slot;
	vxt_slot_t *target;

	slot = vxtcom->live_slots;
	if (slot == NULL) {
		return VXT_PARM;
	} else {
		if (slot->handle == old_handle) {
			vxtcom->live_slots = vxtcom->live_slots->next;
			return VXT_SUCCESS;
		}
	}
	while (slot->next != NULL) {
		if (slot->next->handle == old_handle) {
			target = slot->next;
			slot->next = slot->next->next;
			return VXT_SUCCESS;
		}
		slot = slot->next;
	}
	return VXT_PARM;
}


/*
 *
 * vxtcom_find_ctrlr_pair:
 *
 * traverse list of active controller pairs, looking
 * for the targeted remote domain.  If found, the 
 * pointer to the associated structure is instance. 
 *
 *
 * Returns:
 *
 *    0 => Success
 *    VXT_PARM -  handle passed does not correspond to a 
 *                live slot structure.
 *
 */

static int
vxtcom_find_ctrlr_pair(vxt_domid_t target,  
                       vxtcom_ctrlr_t **instance) {

	vxtcom_ctrlr_hdr_t *traveler;

	
	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, "called, target = 0x%llx\n",
	        (unsigned long long)(vxtarch_word)target);
	traveler = active_ctrlr_pairs;

	while (traveler != NULL) {
		UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
		        "controller pair rdom = 0x%llx\n",
		        (unsigned long long)(vxtarch_word)traveler->rdom);
		if ((unsigned long long)(vxtarch_word)traveler->rdom == 
		    (unsigned long long)(vxtarch_word)target) {
			UMI_LOG(0, VXTPRINT_DEBUG_LEVEL, " MATCH\n");
			*instance = 
			   vxtcom_controller_find_instance(traveler);
			return VXT_SUCCESS;
		}
		traveler = traveler->next;
	}
	return VXT_PARM;
   
}


/*
 *
 * vxtcom_add_ctrlr_pair:
 *
 * Add a local controller to the list of active controllers
 * for the system.  The controller has a matched partner
 * indicated by the rdom, (remote domain) field.
 *
 * Locks:  vxtcom_add_ctrlr_pair grabs the active_ctrlr_pairs
 * lock before traversing the active pair list.  It is assumed
 * that the local_bus_ctrlr is stabilized, (locked) before
 * entry.   Therefore the lock ordering is a bit unusual.  The
 * Device lock is held before the lookup list. 
 * 
 *
 *
 *	Returns:
 *
 *		VXT_SUCCESS  - Controller added to list
 *
 *		VXT_FAIL     - RDOM already connected
 *
 */

int
vxtcom_add_ctrlr_pair(vxtbus_ctrlr_t *local_bus_ctrlr, domid_t rdom)
{
	vxtcom_ctrlr_hdr_t *traveler;
	vxtcom_ctrlr_t *local_ctrlr;

	local_ctrlr = vxtcom_find_ctrlr(local_bus_ctrlr);
	vxt_mlock(&ctrlr_pair_lock);
	traveler = active_ctrlr_pairs;
	while(traveler != NULL) {
		if (traveler->rdom == rdom) {
			/* 
			 * only one controller pair for an RDOM
			 * This will be ammended for physical
			 * support to "one controller type"
			 */
			vxt_munlock(&ctrlr_pair_lock);
			return VXT_BUSY;
		}
		traveler = traveler->next;
	}

        local_ctrlr->ctrlr_list.rdom = rdom;
        local_ctrlr->ctrlr_list.next = active_ctrlr_pairs;
        active_ctrlr_pairs = &local_ctrlr->ctrlr_list;
	vxt_munlock(&ctrlr_pair_lock);

	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_remove_ctrlr_pair:
 *
 * vxtcom_remove_ctrlr_pair searches the list of active
 * controllers on the system.  If a match is found
 * for the controller passed then the controller is 
 * removed from the active list.  In addition, the
 * remote domain field is checked.  If this does not
 * match the value sent an ASSERT is generated.
 *
 *
 * Locks:
 *
 *           The vxt controller lock may be held 
 *           entry and the the active ctrlr lock
 *           may be held as well.  This requires
 *           the lock hierarchy active ctrlr ->
 *           controller -> ctrlr pair.
 *
 * Returns:
 *
 *		VXT_SUCCESS  -  A match is found and the
 *		                controller is removed from
 *		                the active list
 *
 *		VXT_FAIL     -  If a match is not found.
 */

int
vxtcom_remove_ctrlr_pair(vxtbus_ctrlr_t *local_bus_ctrlr, domid_t rdom)
{
        vxtcom_ctrlr_hdr_t *traveler;
	vxtcom_ctrlr_t *local_ctrlr;

	local_ctrlr = vxtcom_find_ctrlr(local_bus_ctrlr);
	vxt_mlock(&ctrlr_pair_lock);
        /*
         * Remove local_ctrlr from the active controllers list
         */
        traveler = active_ctrlr_pairs;

	if (traveler == NULL) {
		vxt_munlock(&ctrlr_pair_lock);
		return VXT_FAIL;
	}

        if (vxtcom_controller_find_instance(traveler) == local_ctrlr) {
                active_ctrlr_pairs = active_ctrlr_pairs->next;
		vxt_munlock(&ctrlr_pair_lock);
		return VXT_SUCCESS;
        } else {
                while (traveler->next != NULL) {
                        if (vxtcom_controller_find_instance(traveler->next)
                            == local_ctrlr) {
                                traveler->next = traveler->next->next;
				vxt_munlock(&ctrlr_pair_lock);
                                return VXT_SUCCESS;
                        }
			traveler = traveler->next;
                }
        }
	vxt_munlock(&ctrlr_pair_lock);
	return VXT_FAIL;
}


/*
 * vxtcom_inc_ref:
 *
 * vxtcom_inc_ref raises the reference count on the 
 * targeted vxtctrlr structure.  
 *
 * Locks: The targeted vxtcom mutex must be held
 *
 * Returns
 *
 *		None
 *
 */

void
vxtcom_inc_ref(vxtcom_ctrlr_t *vxtcom)
{
	vxtcom->bus.ctrlr_ref++;
}

/*
 * vxtcom_dec_ref:
 *
 * vxtcom_dec_ref lowers the reference count on the 
 * targeted vxtctrlr structure.  If the structure
 * count goes to zero, the vxtctrlr shutdown is 
 * called and the structure and its resources are
 * deleted.
 *
 * Locks: The targeted vxtcom mutex must be held
 *        on entry.  If the ref count goes to 
 *        zero, pending_shutdown is set.  The
 *        subsequent call to unplug devices may
 *        cause the controller lock to be dropped
 *        but the pending shutdown will insure
 *        against new controller leases.  The
 *        mutex is dropped by vxtcom_destroy_controller
 *        just before the controller structure
 *        is reclaimed.
 *
 * Returns
 *
 *		None
 *
 */

void
vxtcom_dec_ref(vxtcom_ctrlr_t *vxtcom)
{
	
	UMI_LOG(1, VXTPRINT_PROFILE_LEVEL, "called, vxtcom = %p\n", vxtcom);
	if (vxtcom->bus.ctrlr_ref == 1) {
		UMI_LOG(44, VXTPRINT_BETA_LEVEL,
		        "shutting down the controller\n");
		/* 
		 * bring the controller down
		 * Disable new device creation and
		 * tear down existing devices
		 */
		vxtcom->pending_shutdown = 1;
		vxtcom_unplug_devices(vxtcom, 1);
		ASSERT(vxtcom->bus.ctrlr_ref == 1);
		vxtcom->bus.ctrlr_ref--;
		/*
		 * Drop the controller lock in order
		 * to obtain the controller list lock
		 */
		vxt_munlock(&vxtcom->bus.config_mutex);
		vxt_mlock(&active_ctrlr_lock);
		vxt_mlock(&vxtcom->bus.config_mutex);
		
		/*
		 * Remove the controller, 
		 * vxtcom_destroy_controller drops
		 * both the controller and the 
		 * controller list locks.
		 */
		vxtcom_destroy_controller(vxtcom);
		UMI_LOG(66, VXTPRINT_DEBUG_LEVEL,
		        "returning from vxtcom_destroy_controller\n");
		return;
	}
	vxtcom->bus.ctrlr_ref--;
	/*
	 * Drop the mutex on the controller so that our
	 * behavior is consistent across simple ref decrement
	 * and controller object destruction.
	 */
	vxt_munlock(&vxtcom->bus.config_mutex);
}


/*
 *
 * Bus level external call to decrement controller
 * reference and possibly remove the controller
 *
 * Locks:
 *
 *             None on entry, grab controller lock
 *             before calling internal reference
 *             decrement routine.
 *
 */

void
vxtcom_dec_bus_ref(vxtbus_ctrlr_t *bus_instance)
{
	vxtcom_ctrlr_t  *instance;

	UMI_LOG(2, VXTPRINT_PROFILE_LEVEL,
	        "called, bus_instance = %p\n", bus_instance);
	if (bus_instance == NULL) {
		return;
	}

	instance = vxtcom_find_ctrlr(bus_instance);
	
	vxt_mlock(&(instance->bus.config_mutex));
	vxtcom_dec_ref(instance);
	/*
	 * config_mutex is unlocked in vxtcom_dec_ref
	 */

	return;

}


/*
 * 
 * vxtcom_ctrlr_shutdown:
 *
 * vxtcom_ctrlr_shutdown is a helper routine that is called when
 * bus level support wishes to unplug an entire controller
 * vxtcom_ctrlr_shutdown rips the devices out of its slots and tears
 * them down.  The remote endpoints are all notified of the local devices'
 * demise.
 *
 * Returns:
 * 
 *		VXT_SUCCESS => Upon successful removal of controller
 *
 *		VXT_FAIL - Controller resources could not be removed
 *
 */

int
vxtcom_ctrlr_shutdown(vxtcom_ctrlr_t *vxtcom, int signal_remote)
{
	vxtdev_t       *device;
	int            ret;

	UMI_LOG(3, VXTPRINT_PROFILE_LEVEL,
	        "called, controller = %p\n", vxtcom);
	while (vxtcom->live_slots != NULL) {
		device = vxtcom_slot_find_device(vxtcom->live_slots);
		ret = vxtcom_remove_device((void *)vxtcom, (void *)device,
		                           NULL, 0, signal_remote);
		UMI_LOG(67, VXTPRINT_DEBUG_LEVEL, 
		        "return from vxtcom_remove_device, ret = %08x\n",ret);
		if (ret) {
			return VXT_FAIL;
		}
	}
	UMI_LOG(68, VXTPRINT_DEBUG_LEVEL, "exiting\n");
	return VXT_SUCCESS;
}




/*
 * These routines are the callbacks available to devices
 * they are used to establish and tear down underlying services
 * to signal and to quiesce for snapshot.
 */


/*
 *
 * vxtcom_devreq_find_queue:
 *
 * vxtcom_devreq_find_queue is called by device instances to
 * return the address and structure handle of a shared queue
 * The queue chosen is based on the ordinal value passed in
 * q_ord.  The fist queue is represented by 1 (NOT 0).
 *
 * If no queues are present or the number is not as large
 * as q_ord, a failure code is returned.
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS - upon successful execuition
 *		VXT_PARMS - ordinal value to large,
 *		            device structure invalid
 *
 */

static int
vxtcom_devreq_find_queue(void *device_handle, uint32_t q_ord, 
			 void **queue, void **queue_handle)

{
	vxtdev_queues_t   *q_pages;
	vxtdev_t          *device = (void *)device_handle;
	int               i;

	UMI_LOG(4, VXTPRINT_PROFILE_LEVEL,
	        "called, device_handle = %p, queue 0x%x\n",
	        device_handle, q_ord);
	if (device == NULL) {
		UMI_LOG(69, VXTPRINT_DEBUG_LEVEL,"no device\n");
		return VXT_PARM;
	}

	q_pages = device->dev_queues;
	if (q_pages == NULL) {
		UMI_LOG(70, VXTPRINT_DEBUG_LEVEL,"no queues\n");
		return VXT_PARM;
	}

	for (i=0; i < q_ord; i++) {
		q_pages = q_pages->next;
		if (q_pages == NULL) {
			UMI_LOG(71, VXTPRINT_DEBUG_LEVEL,"bad ord\n");
			return VXT_PARM;
		}
	}

	*queue = q_pages->qaddr;
	*queue_handle = (void *)q_pages;
	UMI_LOG(72, VXTPRINT_DEBUG_LEVEL,
	        "success, queue %p, queue_handle %p\n",
	        *queue, *queue_handle);
	return VXT_SUCCESS;

}


/*
 *  Driver hygene Utilities
 *
 * Utilities to isolate bus level support module actions
 */

/*
 *
 * vxtcom_create_controller:
 *
 * Allocate a controller instance structure and initialize
 * the internal fields.  Return a pointer to the bus code
 * visible portion.
 *
 */

vxtbus_ctrlr_t *
vxtcom_create_controller(void)
{
	vxtcom_ctrlr_t *instance;
	int            ret;

	UMI_LOG(5, VXTPRINT_PROFILE_LEVEL, "called\n");
	instance = vxt_kzalloc(sizeof(vxtcom_ctrlr_t), VXT_KMEM_KERNEL);
	if (instance == NULL) {
		return NULL;
	}

	/*
	 * Create the per-controller siganlling aparatus
	 */
	ret = vxtsig_controller_init(instance, &instance->bus.dev_events);
	if (ret != VXT_SUCCESS) {
		vxt_kfree(instance);
		return NULL;
	}


	instance->bus.ctrlr_ref = 1;
	vxtcom_init_wait(&instance->new_dev_wait);
	instance->dev_event_list = 0;
	instance->pending_shutdown = 0;
	instance->dev_list = NULL;
	instance->vxtcom_slots = 1; /* slot space */
	instance->slot_wrap = 0;    /* free ride fist time thru slot space */
	instance->live_slots = NULL; /* list of "plugged-in" devices */
	instance->pending_send = 0;  /* outgoing data needs to be sent */
	instance->major = 0;
	instance->file_name[0] = 0;	
	instance->device_libraries = NULL;
	instance->ctrlr_list.next = NULL;
	instance->ctrlr_list.rdom = 0;

	instance->bus.version = VXTCOM_BUS_CNTRLR_VERSION;
	vxt_mlock_init(&instance->bus.config_mutex);

	/*
	 * Controller placed on active controllers list
	 * in vxtcom_ctrlr_init
         */
	instance->next = NULL;

	return &instance->bus;
}


/*
 *
 * vxtcom_destroy_controller:
 *
 * Free an internal vxt controller state structure
 * clean up any internal resources or state that
 * remains.
 *
 * Active controllers and the targeted controller
 * instance are both locked on entry.  The targeted
 * controller is unlocked before it is freed.
 *
 * Locks:
 *         Both the controller and the controller
 *         list locks are held on entry.  Both
 *         locks are freed by this routine.
 *
 */

static void
vxtcom_destroy_controller(vxtcom_ctrlr_t *instance)
{
	vxtcom_ctrlr_t  *traveler = active_controllers;
	int             controller_found = 0;

	UMI_LOG(6, VXTPRINT_PROFILE_LEVEL,
	        "called, controller = %p\n", instance);
	/*
	 * remove the controller from the active controller
	 * list
	 */
	if (instance == NULL) {
		UMI_WARN(59, VXTPRINT_PRODUCT_LEVEL,
		         "called with NULL instance\n");
		vxt_munlock(&active_ctrlr_lock);
		return;
	}
	if (traveler == NULL) {
		UMI_WARN(60, VXTPRINT_PRODUCT_LEVEL,
		         "synchronization failure, live controller "
		         "is not on active controller list, controller %p\n",
		         instance);
		vxt_munlock(&instance->bus.config_mutex);
		vxt_munlock(&active_ctrlr_lock);
		return;
	}
	if (instance == traveler) {
		active_controllers = active_controllers->next;
		controller_found = 1;
	} else {
		while(traveler->next != NULL) {
			if (instance == traveler->next) {
				traveler->next = traveler->next->next;
				controller_found = 1;
				break;
			}
			traveler = traveler->next;
		}
	}
	if (controller_found == 0) {
		UMI_WARN(61, VXTPRINT_PRODUCT_LEVEL,
		         "synchronization failure, live controller is not "
		         " on active controller list, controller %p\n",
		         instance);
		vxt_munlock(&instance->bus.config_mutex);
		vxt_munlock(&active_ctrlr_lock);
		return;
	}
	if (active_controllers == NULL) {
		vxtcom_os_destroy_controller(instance, TRUE);
	} else {
		vxtcom_os_destroy_controller(instance, FALSE);
	}

	vxt_munlock(&instance->bus.config_mutex);
	vxtsig_controller_shutdown(instance->bus.dev_events);

	vxtmsg_dev_unload_lib(instance);

	ASSERT(instance->live_slots == NULL);
	vxt_kfree(instance);

	vxt_munlock(&active_ctrlr_lock);

	return;
}


/*
 *
 * vxtcom_unplug_devices:
 *
 * Called from the bus context when the
 * controller itself is being decommisioned.
 * All the devices are unceremoniously 
 * unplugged and destroyed by this call
 * vxtcom_unplug_devices is separated from
 * destroy_controller so that the remote
 * side can be notified of the impending
 * loss of connection on a device by
 * device basis.
 *
 */

static void
vxtcom_unplug_devices(vxtcom_ctrlr_t *instance, int signal_remote)
{

	UMI_LOG(7, VXTPRINT_PROFILE_LEVEL,
	        "called, controller = %p\n", instance);
	/*
	 * Attempt to remove the devices
	 * on the controller, return if
	 * we fail
	 */
	vxtcom_ctrlr_shutdown(instance, signal_remote);

	return;
}


void
vxtcom_bus_unplug_devices(vxtbus_ctrlr_t *bus_instance, int signal_remote)
{
	vxtcom_ctrlr_t  *instance;

	UMI_LOG(8, VXTPRINT_PROFILE_LEVEL, "called, bus %p\n", bus_instance);
	if (bus_instance == NULL) {
		return;
	}

	instance = vxtcom_find_ctrlr(bus_instance);
	vxt_mlock(&(instance->bus.config_mutex));

	vxtcom_unplug_devices(instance, signal_remote);

	vxt_munlock(&(instance->bus.config_mutex));
}


/*
 * vxtcom_bus_connect:
 *
 * vxtcom_bus_connect is called to signal when the
 * bus level support goes from the disconnected
 * to the connected state, indicating the
 * vxt controller can now communicate with
 * the outside world through its bus.
 */

int
vxtcom_bus_connect(vxtbus_ctrlr_t *bus_instance)
{
	vxtcom_ctrlr_t *instance;

	UMI_LOG(9, VXTPRINT_PROFILE_LEVEL, "called, bus %p\n", bus_instance);
	instance = vxtcom_find_ctrlr((vxtbus_ctrlr_t *)bus_instance);
	vxt_mlock(&instance->bus.config_mutex);
	/*
	 * Allow new device creation
	 */
	instance->pending_shutdown = 0;
	vxtcom_ctrlr_event++;
	vxtcom_ctrlr_poll_events |= POLL_VXT_NEW_CTRLR;
	vxt_munlock(&instance->bus.config_mutex);
	vxtcom_wakeup(&vxtcom_ctrlr_changed);
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_bus_disconnect:
 * 
 * vxtcom_bus_disconnect is called from the bus driver
 * level to indicate that the bus connection is going
 * down.
 */

int
vxtcom_bus_disconnect(vxtbus_ctrlr_t *bus_instance)
{
	vxtcom_ctrlr_t *instance;

	UMI_LOG(10, VXTPRINT_PROFILE_LEVEL, "called, bus %p\n", bus_instance);
	instance = vxtcom_find_ctrlr((vxtbus_ctrlr_t *)bus_instance);
	vxt_mlock(&instance->bus.config_mutex);
	/*
	 * Turn off new device creation
	 */
	instance->pending_shutdown = 1;
	vxtcom_ctrlr_event++;
	vxtcom_ctrlr_poll_events |= POLL_VXT_CTRLR_DIED;
	vxt_munlock(&instance->bus.config_mutex);
	vxtcom_wakeup(&vxtcom_ctrlr_changed);
	return VXT_SUCCESS;
}


/*
 * vxtcom_bus_recover:
 *
 * vxtcom_bus_recover is called to signal when the
 * bus level support goes from the suspend
 * to the connected state, indicating the
 * vxt controller can now communicate with
 * the outside world through its bus and that we
 * may have just recovered from a migration.
 */

int
vxtcom_bus_recover(vxtbus_ctrlr_t *bus_instance, void *record)
{
	UMI_LOG(11, VXTPRINT_PROFILE_LEVEL, "called, bus %p\n", bus_instance);
	return VXT_SUCCESS;
}


/*
 * vxtcom_bus_suspend:
 *
 * vxtcom_bus_suspend is called to signal when the
 * bus level support is about to go into suspend
 * mode.  This offers the devices on the bus a 
 * chance to suspend their activity and 
 * save their state.
 */

int
vxtcom_bus_suspend(vxtbus_ctrlr_t *bus_instance, void *record)
{
	UMI_LOG(628, VXTPRINT_PROFILE_LEVEL, "called, bus %p\n", bus_instance);
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_bus_initialize_send_queue:
 *
 * Initialize the request and reply send queues
 * so that they appear empty.  The queue
 * entries are self-defining so a message
 * header declaring a VXTCOM_CNTRL_NULL,
 * (null message), serves as a telomere.
 *
 * Note: queue initialization is imperative
 * for systems that may experience early
 * or spurious signalling events as this
 * protects against reading stale or
 * nonsense entries.
 *
 * Returns:
 *
 *		VXT_SUCCESS
 *
 */

int
vxtcom_bus_initialize_send_queue(void *buffer, long size)
{
	vxtcom_cntrl_hdr_t *queue = (vxtcom_cntrl_hdr_t *)buffer;

	UMI_LOG(12, VXTPRINT_PROFILE_LEVEL,
	        "called, buffer %p, size %lx\n", buffer, size);
	ASSERT(size == VXTCOM_CONFIG_QUEUE_SIZE);

	queue->message_type = VXTCOM_CNTRL_NULL;
	queue->message_id = 0;
	queue = (vxtcom_cntrl_hdr_t *)
	        (((char *)buffer)
	         + VXTCOM_CONFIG_QUEUE_REPLY_OFFSET);
	queue->message_type = VXTCOM_CNTRL_NULL;

	return VXT_SUCCESS;
}


/*
 * vxtcom_dev_lookup_token
 *
 * lookup a registered shared memory record for a device
 * based on the token passed.  When a suitable record
 * is found a pointer to it is passed back to the caller.
 *
 * Note:  The "token" compared against in the record is
 * actually the address.  The token field in
 * the registration structure holds the label
 * for the device library routine level identification
 *
 * A second label is required because the linux code
 * "eats" the lower 12 bits of the token passed in the
 * mmap offset field.  The mapped address in the kernel
 * must start on a page boundary and is therfore ideal
 * as a unique label for user level mapping access.
 *
 *
 */

int vxtcom_dev_lookup_token(vxtarch_word token, vxt_device_sm_t **result)
{
	vxt_device_sm_t *record;

	UMI_LOG(13, VXTPRINT_PROFILE_LEVEL,
	        "called, token %lx\n", token);
	vxt_mlock(&sm_token_lock);
	record =  sm_token_table;
	while (record != NULL) {
		if (((vxtarch_word)record->addr) == token) {
			UMI_LOG(73, VXTPRINT_DEBUG_LEVEL,
			        "success, record = %p\n", record);
			*result = record;
			record->ref_cnt++;
			vxt_munlock(&sm_token_lock);
			return VXT_SUCCESS;
		}
		record = record->next;
	}
	UMI_LOG(74, VXTPRINT_DEBUG_LEVEL, "lookup failed\n");
	vxt_munlock(&sm_token_lock);
	return VXT_PARM;
}


void
vxtcom_dev_token_ref(vxt_device_sm_t *record)
{
	UMI_LOG(14, VXTPRINT_PROFILE_LEVEL,
	        "called, token %p\n", record);
	vxt_mlock(&sm_token_lock);
	record->ref_cnt++;
	vxt_munlock(&sm_token_lock);
}


int 
vxtcom_dev_token_dec(vxt_device_sm_t *record)
{
	UMI_LOG(15, VXTPRINT_PROFILE_LEVEL,
	        "called, token %p\n", record);
	
	vxt_mlock(&sm_token_lock);
	record->ref_cnt--;
	vxt_munlock(&sm_token_lock);
	if(record->ref_cnt == 0) {
		/*
		 * We do not need the record lock
		 * at this point because we are
		 * the only entity holding a
		 * reference.
		 */
		vxtcom_destroy_sm_object(record);
	}
	return VXT_SUCCESS;
}


/*
 * vxtcom_devreq_cancel_token
 *
 * remove a registered shared memory token
 * from the shared memory table.
 *
 * The token record will have its reference count
 * decremented and if the registered list
 * is the last user, it will be removed.  If not,
 * it will persist until the remaining users 
 * have finished with it.
 *
 * 
 */

static int vxtcom_dev_cancel_token(void *device_handle, vxtarch_word token)
{
	vxt_device_sm_t *record;
	vxt_device_sm_t *target;
	uint32_t        pg_cnt;
	vxtdev_t *device = (vxtdev_t *)device_handle;
   
	UMI_LOG(16, VXTPRINT_PROFILE_LEVEL,
	        "called, device_handle %p, token %lx\n",
	        device_handle, token);

	vxt_mlock(&sm_token_lock);
	record = sm_token_table;
	if (record == NULL) {
		/*
		 * No record was found for the targeted
		 * token
		 */
		UMI_LOG(75, VXTPRINT_DEBUG_LEVEL, "No record found!\n");
		vxt_munlock(&sm_token_lock);
		return VXT_PARM;
	}

	/*
	 * Find the targeted record
	 */
	if (record->token == token) {
		vxtdev_t *rdev = (vxtdev_t *)(vxtarch_word)(record->dev_handle);
		if (rdev != device) {
			UMI_LOG(76, VXTPRINT_DEBUG_LEVEL, "device mismatch!\n");
			vxt_munlock(&sm_token_lock);
			return VXT_PARM;
		}
		sm_token_table = sm_token_table->next;
	} else {
		target = NULL;
		while (record->next != NULL) {
			if (record->next->token == token) {
				target = record->next;
				record->next = record->next->next;
				record = target;
				break;
			} else {
				record = record->next;
			}
		}
		if (target == NULL) {
			UMI_LOG(77, VXTPRINT_DEBUG_LEVEL,
			        "caller passed bad queue token\n");
			vxt_munlock(&sm_token_lock);
			return VXT_PARM;
		}
	}


	pg_cnt = record->len >> PAGE_SHIFT;
	/*
	 * Make sure to unlock the sm_token_lock as
	 * vxtcom_dev_token_dec will grab the lock
	 * Also flush actions may grab locks that
	 * are at a higher level
	 */
	vxt_munlock(&sm_token_lock);

	/*
	 * First lets remove all mapping resources
	 * associated with user level mappings.
	 */

	if (record->user_handles && (record->vma != NULL)) {
		UMI_LOG(78, VXTPRINT_DEBUG_LEVEL,
		        "calling vxtcom_unmap_user_queue, pg_cnt 0x%x\n",
		        pg_cnt);
		vxtcom_flush_device_pager_range
		   (record->vma, record->vma->vm_start, 
		    record->vma->vm_end - record->vma->vm_start, NULL, record);
		UMI_LOG(79, VXTPRINT_DEBUG_LEVEL,
		        "returning from vxtcom_unmap_user_queue\n");
	}

	vxtcom_dev_token_dec(record);
	return VXT_SUCCESS;
}


/*
 * vxtcom_devreq_reg_token
 *
 * register a shared memory token
 *
 * Note: The caller is in the same security domain as this 
 * function.  Therefore it is reasonable to use the address
 * as the token/handle.  If an abstracted handle is needed
 * a utility will have to be provided to generate handles
 * for the device libraries.
 *
 * Locks and reference counts:
 *
 * There is no overarching lock for the reg_token list
 * as the registration and cancellation of the token
 * is done under the controller lock but the users
 * of the token are called under the mmap routines.
 * The reg_token is appended to the sm_token_list.  It is
 * therefore necessary to grab the sm_token_lock while
 * adding the new entry to the list.  Further, a reference
 * count is kept on the token record as multiple users
 * of the token record may be active simultaneously and
 * we do not want to hold a common lock during record
 * usage.
 *
 * There are no updates to the fields of the token
 * record after its creation within the vxtcom_dev_token
 * framework it is therefore unecessary to hold a record level 
 * lock for record update.  We update the reference count under
 * the global token list lock.
 *
 * This leaves option of record update and the synchronization
 * policy to the caller.  In the case of the Linux guest, mmap 
 * calls have already synchronized using the mmap_sem.  Other 
 * guests may decide to lock before or after the token lookup, 
 * but must choose one or the other and be consistant as a violation
 * of lock ordering will lead to system hangs.
 */

static int vxtcom_dev_reg_token(void *device_handle, void *addr, 
                                uint32_t len, void *queue_handle,
                                vxtarch_word token, vxtarch_word *name)
{
	vxt_device_sm_t *record;
	vxtdev_t *device = (vxtdev_t *)device_handle;
	int ret;
   
	/*
	 * use the addr and not the token because
	 * lookup has been altered to support linux
	 * offset field treatment on mmap
	 */
	UMI_LOG(17, VXTPRINT_PROFILE_LEVEL,
	        "called, device_handle %p , addr %p, len %08x, token %lx\n",
	        device_handle, addr, len, token);
	ret = vxtcom_dev_lookup_token((vxtarch_word)addr, &record);
	if (ret == VXT_SUCCESS) {
		/*
		 * Token is already registered
		 * Drop the reference gained in lookup and return Busy
		 */
		vxtcom_dev_token_dec(record);
		return VXT_BUSY;
	}
	
	record = vxtcom_alloc_sm_object();
	if (record == NULL) {
		return VXT_NOMEM;
	}
	record->token = token;
	record->lookup_token = 0;
	record->shadow_queue = NULL;
	record->queue_token = queue_handle;
	record->dev_handle = (uint64_t)(vxtarch_word)device;
	record->addr = addr;
	record->len = len & ~(PAGE_SIZE - 1);
	if (len & (PAGE_SIZE - 1)) {
		record->len += PAGE_SIZE;
	}

	vxt_mlock(&sm_token_lock);
	record->next = sm_token_table;
	sm_token_table = record;
	vxt_munlock(&sm_token_lock);

	ASSERT((((vxtarch_word)addr) & 0xFFF) == 0); /* lower bits must be 0 */

	*name = (vxtarch_word)addr;   /* address is on page boundary and will */
	                              /* provide a unique name with lower     */
	                              /* 12 bits all zero. */
	UMI_LOG(80, VXTPRINT_DEBUG_LEVEL, "returning, name = %lx\n", *name);
	return VXT_SUCCESS;
}


/*
 * vxtcom_devreq_rem_queue:
 *
 * vxtcom_devreq_rem_queue removes the resources
 * and shared connections associated with the memory
 * resource of a shared queue
 *
 * vxtcom_devreq_rem_queue is called by device instance
 * code when the device is shutting down.  The
 * shared queue is freed along with any internal structures
 * needed to track the state and of the associated
 * shared memory.
 *
 * vxtcom_devreq_rem_queue works for queues comprised of
 * shared memory pages, of both connected and locally
 * allocated types.  Xen is assymetric in its treatment
 * of shared pages and does not hide the shared memory
 * construct in the normal mapping, therefore we must
 * not only recognize shared memory and break down its
 * registration, we must also recognize which part the
 * local party to the aquisition played. 
 *
 * NOTE:  
 *    gnttab_end_foreign_access is not yet fully implemented
 *    in XEN.  It is supposed to put referenced elements
 *    onto a list and release them when the remote side
 *    lets go.  Because of this, it should be required
 *    that the remote party free its connection before
 *    this code is called.
 *
 * Returns:
 *     VXT_SUCCESS
 *     VXT_PARM - Incoming device or controller invalid
 */
static int
vxtcom_devreq_rem_queue(void *device_handle, void *queue_handle)

{
	vxtdev_t          *device = (vxtdev_t *)device_handle;
	vxtdev_queues_t   *queue_header = (vxtdev_queues_t *)queue_handle;
	void              *queue = queue_header->qaddr;
	vxtdev_queues_t   *queue_ptr;
	vxtdev_queues_t   *queue_follow;

	UMI_LOG(18, VXTPRINT_PROFILE_LEVEL,
	        "called, device_handle %p, queue_handle %p\n",
	        device_handle, queue_handle);
	if (device->controller == NULL) {
		return VXT_PARM;
	}
/* CDY substitute a dom initialized variable, there is no invalid
   value here
	if (device->controller->bus.ctrlr_list.rdom == 0) {
		return VXT_PARM;
	}
*/

	/*
	 * Find the internal page list for the queue
	 * targeted with removal.
	 */
	queue_ptr = device->dev_queues;
	queue_follow = queue_ptr;
	while (queue_ptr != NULL) {
		if (queue_ptr->qaddr == queue) {
			ASSERT(queue_header == queue_ptr);
			break;
		}
		queue_follow = queue_ptr;
		queue_ptr = queue_ptr->next;
	}

	if (queue_ptr == NULL) {
		return VXT_PARM;
	}
	/*
	 * Remove queue resource structure
	 * from list
	 */
	if (queue_ptr == device->dev_queues) {
		device->dev_queues = queue_ptr->next;
	} else {
		queue_follow->next = queue_ptr->next;
	}

	/*
	 * Free the grant_table resources,
	 * revoke foreign access on the pages
	 * shared with the remote endpoint,
	 * and free the queue structure itself.
	 */

	vxtcom_destroy_queue(queue_ptr, device->origin);


	return VXT_SUCCESS;
}


/*
 * vxtcom_signal_remote_close:
 *
 * carryout setup and calling of request to remote endpoint
 * to end communication and tear-down its resources.
 *
 * wait for response.
 *
 * RETURNS:
 *
 * 		VXT_SUCCESS
 *
 */

static int
vxtcom_signal_remote_close(vxtdev_t  *device, void * info, uint32_t info_length)
{
	int ret;

	UMI_LOG(19, VXTPRINT_PROFILE_LEVEL, "called, device %p\n", device);
	device->waiting = 0;
	vxtcfg_devreq_remove_device(device, info, info_length);
	ret = vxtcom_ctrlr_wait(device->controller, &device->wq, 
	                        (long *)&device->waiting, 1);
	UMI_LOG(81, VXTPRINT_DEBUG_LEVEL,"WAKING UP, return = 0x%x\n", ret);
	return ret;
}


/*
 *
 * vxtcom_close_local_device:
 *
 * vxtcom_close_local_device calls the device specific 
 * instance tear-down.  After it returns, it checks
 * the generic device shared resources fields to see
 * that the dev specific code did its job properly.
 * If not, vxtcom_close_local_device attempts to free
 * the resources.
 *
 * RETURNS:
 *
 * 		VXT_SUCCESS - if successful clean-up otherwise
 *		              the return code from the failing
 *		              sub-call is passed on.
 */

static int
vxtcom_close_local_device(vxtdev_t  *device, void *info, uint32_t info_length)
{

	vxtdev_queues_t  *device_queue;
	int ret;

	UMI_LOG(20, VXTPRINT_PROFILE_LEVEL, "called, device %p\n", device);


	/*
	 * We will drop the lock in the middle of device
	 * shutdown.  Make sure that no one else starts
	 * shutting down the same device.
	 */
	if (device->shutting_down) {
		return VXT_BUSY;
	}

	device->shutting_down = 1;

	/*
	 * If our device asked for a device file and
	 * it is in use we need to disable the accesses
	 * so that we can remove the device
	 */
	vxtcom_os_disable_device_file(device);
	
	/*
	 * Wake up anyone waiting on poll data
	 */
	vxtsig_object_signal(device->controller->bus.dev_events,
	                     device->event);


	/*
	 * Call the device code to request the device instance
	 * be destroyed and its resources freed
	 */
	ret = 
	   device->dev->destroy((void *)device, info,
	                        info_length, device->instance);

	if (ret != VXT_SUCCESS) {
		UMI_LOG(45, VXTPRINT_BETA_LEVEL,
		        "device specific destroy failed\n");
		return ret;
	}

	/*
 	 * A little check-up to make certain the local device code 
	 * actually cleaned-up after itself.  The shared queues 
	 * must be cleaned up or we will have a memory leak and
	 * a shared memory bookeeping structure leak.
	 */
	if (device->dev_queues != NULL) {
		UMI_WARN(46, VXTPRINT_BETA_LEVEL, "badly behaved device\n"); 
		device_queue = device->dev_queues;
		while (device_queue != NULL) {
			ret = vxtcom_devreq_rem_queue(device, 
			                              device_queue->qaddr);
			if (ret != VXT_SUCCESS) {
				/*
				 * Going to leak here
				 */
				UMI_WARN(62, VXTPRINT_PRODUCT_LEVEL,
				         "Malformed queues\n");
				break;
			}
			device_queue = device->dev_queues;
		}
	}

	/*
	 * Deregister the signaling resource from
	 * the vxt signaling service.
	 */
	ret = vxtsig_object_destroy(device->controller->bus.dev_events, 
	                            device->event);
	if (ret != VXT_SUCCESS) {
		UMI_WARN(63, VXTPRINT_PRODUCT_LEVEL, 
		         "VXT controller cannot release signal\n");
	}

	/*
	 * Make certain there is no one left waiting for
	 * config messages on this device.  There is
	 * a race between us and the endpoint when ctrlr
	 * shutdown has been instituted.
	 */
	device->state =  VXT_COM_DEV_ERROR;
	device->error = VXT_ABORT;
	vxtcom_remove_device_complete((void *)device, device->error);

	/*
	 * Now that the device specific destroy has returned
	 * we are free to disassociate the device from its
	 * device library.
	 */
	device->library->use_count--;
	device->library = NULL;

	return VXT_SUCCESS;
}


/*
 * 
 * vxtcom_remove_device:
 *
 * vxtcom_remove_device is called to remove a device from
 * the vxt controller.  The resources associated with the device
 * are freed as a result of the secondary actions of the device
 * destroy call.  This is a callback made on the specific
 * device type.
 *
 * Upon successful return, the targeted device will be removed
 * from the controller slot it had occupied and the slot handle
 * will be freed for future re-use.
 *
 *
 */

int
vxtcom_remove_device(void *vxtcom_handle, void *device_handle,
                     void *info, uint32_t info_length, uint32_t signal_remote)
{
	vxtcom_ctrlr_t *vxtcom = (vxtcom_ctrlr_t *)vxtcom_handle;
	vxtdev_t  *device = (vxtdev_t *)device_handle;
	symlibs_t *library;
	vxtdev_t  *traveler;
	int ret;

	UMI_LOG(21, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, device %p\n",
	        vxtcom_handle, device_handle);
	/*
	 * Check to see if library is null.  This will be the
	 * case if we are in a race with the remote client 
	 * and the remote client is the origin.  In this case
	 * we will have removed the local device, (and cleared
	 * the library field) and will be waiting for a response
	 * to the removal of the remote device.  The client 
	 * meanwhile has started the removal process and has
	 * sent us this remove request.  We politely exit with
	 * success here, since we have already done what was
	 * requested.
	 */
	if (device->library == NULL) {
		UMI_LOG(47, VXTPRINT_BETA_LEVEL, "remote dev remove race\n");
		return VXT_SUCCESS;
	}
	device->library->use_count++;
	library = device->library;
	/*
	 * Determine if our side initiated the device
	 * creation, if so wait until the other side
	 * has been unplugged.  (Required because of
	 * Xen shared memory awkwardness)
	 *
	 * Xen shared memory is assymetric with respect
	 * to sharing, the party offering the page must
	 * be last to tear down.  (Silly, unnecessary 
	 * artifact).
	 */
	if ((device->dev_queues != NULL) && (device->origin)) {
		if (signal_remote) {
			ret = vxtcom_signal_remote_close(device, 
			                                 info, info_length);
			if (ret == VXT_PARM) {
				return ret;
			}
		}

		if ((ret = vxtcom_close_local_device(device, info, info_length))
		    != VXT_SUCCESS) {
			/*
			 * Failed to remove local device specific
			 * resources.  Just leave and leak
			 */
			return ret;
		}
	} else {
		/* 
		 * Reverse the order of tear-down
		 * No shared memory and doesn't matter what order
		 * devices are unplugged, or the remote party
		 * is the progenitor.
		 */
		if ((ret = vxtcom_close_local_device(device, info, info_length))
		    != VXT_SUCCESS) {
			/*
			 * Failed to remove local device specific
			 * resources.  Just leave and leak
			 */
			return ret;
		}

		if (signal_remote) {
			ret = vxtcom_signal_remote_close(device, 
			                                 info, info_length);
			if (ret == VXT_PARM) {
				return ret;
			}
		}

	}

	/*
	 * We preserved the generic device structures until now
	 * to support message response code lookup and manipulation
	 * actions.
	 *
	 * Remove the device from the list of devices present on
	 * the associated controller.
	 */
	traveler = vxtcom->dev_list;
	if (traveler == device) {
		vxtcom->dev_list = vxtcom->dev_list->next;
	} else {
		if (traveler != NULL) {
			while (traveler->next != NULL) {
				if (traveler->next == device) {
					traveler->next = traveler->next->next;
					traveler = NULL;
					break;
				}
				traveler = traveler->next;
			}
			if (traveler == NULL) {
				UMI_WARN(48, VXTPRINT_BETA_LEVEL,
				       "VXT controller doesn't list device\n");
			}
		
		} else {
			UMI_WARN(601, VXTPRINT_BETA_LEVEL,
			         "VXT controller doesn't list device\n");
		}
	}

	/*
	 * Remove the targeted device
	 * from the controller slot it
	 * has occupied.  i.e.  remove it
	 * from the active_slots list.
	 */

	ret = vxtcom_free_slot_handle(vxtcom, device->slot.handle);
	if (ret != VXT_SUCCESS) {
		/*
		 * device was already freed, just leave
		 * no need to signal and vxt_dev is
		 * gone.
		 */
		return VXT_SUCCESS;
	}
	/*
	 * event channel is allocated by security authority and locally
	 * freed by task level event clean-up after new style registration
	 * find a way to check for leakage here. CDY
	 */


	/*
	 * Signal os level that device is gone
	 */
	ret = vxtcom_os_device_removed(vxtcom, device);
	if (ret != VXT_SUCCESS) {
		return VXT_FAIL;
	}
	/*
	 * Tell any local listeners that
	 * we have lost a device
	 */
	library->dev_event_list |= VXT_POLL_LOST_DEV;
	vxtcom->dev_event_list |= VXT_POLL_LOST_DEV;
	vxtcom_wakeup(&library->new_dev_wait);
	vxtcom_wakeup(&vxtcom->new_dev_wait);
	vxtcom_ctrlr_event++;
	vxtcom_ctrlr_poll_events |= POLL_VXT_DEV_REMOVED;
	vxtcom_wakeup(&vxtcom_ctrlr_changed);
	vxtcom_wakeup(&vxtsys_new_dev_wait);
	vxtsys_dev_event_list |= VXT_POLL_LOST_DEV;

	library->use_count--;

	vxt_kfree(device);

	return VXT_SUCCESS;

}


/*
 *
 * vxtcom_dev_wait:
 *
 * vxtcom_dev_wait releases the controller config lock and sleeps
 * on the wait queue that it is passed.
 * 
 * To avoid separate device and controller locks, a callback
 * on wait from the device context is needed.  vxtcom_dev_wait
 * is a wrapper for vxtcom_ctrlr_wait that is used in the
 * callback utilities exported to device libraries.
 *
 * Returns:
 *
 *		VXT_SUCCESS - after successful wait
 *		VXT_PARM - Controller no longer viable
 */

int
vxtcom_dev_wait(void *device_handle, vxtcom_wait_queue_t *wq, long *condition)
{
	vxtdev_t *device = (vxtdev_t *)device_handle;
	vxtcom_ctrlr_t  *vxtcom;
	UMI_LOG(22, VXTPRINT_PROFILE_LEVEL,
	        "called device %p, wq %p\n",
	        device_handle, wq);
	if (device->controller == NULL) {
		return VXT_PARM;
	}
	vxtcom = device->controller;

	return vxtcom_ctrlr_wait(vxtcom, wq, condition, 0);
}


/*
 *
 * vxtcom_dev_register_file:
 *
 * vxtcom_dev_register_file is a vxtcom controller callback that
 * allows a registered device to create a file to communicate
 * with the user level client.  The device must use this callback
 * so that all communication is still ultimately filtered through
 * the controller.  The file system callbacks are for the present
 * limited to open/close/poll.  The purpose of the poll is to
 * provide an alternate to the classic data wait operation. It
 * allows collective waits on multiple unrelated conditions.
 * This method is not recommended for any performance critical
 * communication path as the multiple wait is unbounded and
 * may add significant delays as well as promote serial
 * processing of asynchronous work.
 *
 * Note:  The caller provides a dev_base_name which is concatenated
 * with the device number.  The result is passed back in a field
 * provided by the caller which must be VXTCOM_MAX_DEV_FILE_NAME
 * or more in size.
 *
 * The controller lock is held upon invocation
 *
 * Returns:
 *
 */

int
vxtcom_dev_register_file(void *device_handle, char *dev_base_name,
                         char *dev_file_name, uint64_t *handle)
{
	vxtdev_t *device = (vxtdev_t *)device_handle;
	char new_dev_name[VXTCOM_MAX_DEV_FILE_NAME];
	int base_name_size;
	int ret;

	UMI_LOG(23, VXTPRINT_PROFILE_LEVEL, "called, base file name %s\n", 
	        dev_base_name);
	base_name_size = strlen(dev_base_name);
	if (base_name_size > (VXTCOM_MAX_DEV_FILE_NAME - 16)) {
		UMI_WARN(64, VXTPRINT_PRODUCT_LEVEL,
		         "device base name too long - %d bytes\n",
		          base_name_size);
		return VXT_PARM;
	}
 	sprintf(new_dev_name, "%s%d",
	        dev_base_name, 
	        vxt_aggregate_device_count);


	ret = vxtcom_os_register_device_file(new_dev_name, device,
	                                     vxt_aggregate_device_count,
	                                     handle);

	vxt_aggregate_device_count++;
	if (ret != VXT_SUCCESS) {
		return VXT_FAIL;
	}

	vxtcom_strncpy(device->dev_file_name, 
	               new_dev_name, VXTCOM_MAX_DEV_FILE_NAME);
	vxtcom_strncpy(dev_file_name, new_dev_name, VXTCOM_MAX_DEV_FILE_NAME);
	
	return VXT_SUCCESS;
}



/*
 *
 * populate a structure full of callbacks for 
 * registering device libraries to use.
 *
 */

vxtcom_ctrlr_api_t vxtcom_utilities = { vxtcom_devreq_add_queue, 
                                        vxtcom_devreq_find_queue,
                                        vxtcom_devreq_rem_queue,
                                        vxtcfg_devreq_connect_remote,
                                        vxtcfg_sync_status_return,
                                        vxtcom_remove_device,
                                        vxtcom_dev_reg_token,
                                        vxtcom_dev_cancel_token,
					vxtcom_dev_register_file,
                                        vxtcom_dev_wait,
                                        vxtcom_init_wait,
					vxtcom_wakeup };



/*
 * User control interfaces
 */


/*
 *
 * vxtcom_list_devices:
 *
 * vxtcom_list_devices returns the next entry in the targeted
 * vxt controller device list.  The caller passes in the last
 * returned label from a previous call.  vxtcom_list_devices
 * will start its search there and return the next entry in its
 * ordinal list. vxtcom_list_devices returns the label of the
 * next entry and a structure giving the general information on
 * the device.
 *
 * When the caller wishes to start off at the beginning of the 
 * list, the routine is invoked with FIRST_VXTCOM_DEV.
 *
 * If there are no more valid entries in the ordinal list of
 * devices for the targeted controller, VXTCOM_NO_DEV is returned
 * and the values for new_dev_label and new_dev are undefined
 * If a device is found, VXT_SUCCESS is returned with the
 * label field and the info field filled in.
 * 
 *  Returns:
 *      VXT_SUCCESS
 *      VXTCOM_NO_DEV
 *
 */

int
vxtcom_list_devices(vxtcom_ctrlr_t *vxtcom,  uint32_t slot,
                    vxtdev_t *new_dev, uint32_t *new_slot)
{
	UMI_LOG(24, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, slot %08x\n", vxtcom, slot);
	return VXT_SUCCESS;
}

/*
 *
 * vxtcom_load_device_library: a.k.a. vxtcom_add_lib? :)
 *
 * vxtcom_load_device_library first checks to see if the accompanying
 * library has already been loaded by searching its device_libraries
 * list.  If it has, VXT_BUSY is returned.  If not,  a new library
 * structure is allocated and instantiated with the callbacks provided
 * by the caller. The library structure is inserted into the vxt
 * controller library list.
 *
 * RETURNS:
 *             VXT_SUCCESS
 *             VXT_BUSY        library already loaded
 *             VXT_NOMEM       couldn't allocate or instantiate
 *
 */

int
vxtcom_load_device_library(void *vxtcom_handle, vxtdev_api_t *callbacks,
                           vxtdev_type_t device_type)
{
	vxtcom_ctrlr_t *vxtcom;
	symlibs_t *library;
	symlibs_t *newlib;
	int ret;

	UMI_LOG(25, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, library %llx\n",
	        vxtcom_handle, device_type);
	vxtcom = (vxtcom_ctrlr_t *)vxtcom_handle;

	ret = vxtcom_find_lib(vxtcom->device_libraries, 
	                      device_type, &library);
	if (ret == VXT_SUCCESS) {
		return VXT_BUSY;
	}

	/*
	 * Allocate a structure for the new device library
	 */
	
	newlib = vxt_kmalloc(sizeof(symlibs_t), VXT_KMEM_KERNEL);
	if (newlib == NULL) {
		return VXT_NOMEM;
	}

	vxtcom_init_wait(&newlib->new_dev_wait);
	newlib->dev_event_list = 0;
	newlib->dev_type = device_type;
	newlib->use_count = 0;  /* # of device instances for lib */
	/*
	 * Copy in callback fields from
	 * callback structure sent.
	 */
	newlib->callbacks = *callbacks;
	/*
	 * Add to the top of the controller library
	 * list.
	 */
	newlib->next = vxtcom->device_libraries;
	vxtcom->device_libraries = newlib;

	return VXT_SUCCESS;
}


/*
 * 
 * vxtcom_unload_device_library:
 *
 * Called when a library module is being removed.  The
 * call will fail if there are outstanding devices allocated
 * against the library device type.
 *
 * Returns:
 *
 *		VXT_SUCCESS - Library removed
 *
 *		VXT_FAIL - Library not found in loaded list
 *
 *		VXT_BUSY - Devices still active on library
 *
 */


int
vxtcom_unload_device_library(void *vxtcom_handle, vxtdev_type_t device_type)
{
	vxtcom_ctrlr_t *vxtcom;
	symlibs_t      *library;
	int            ret;

	UMI_LOG(26, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, library %llx\n",
	        vxtcom_handle, device_type);
	vxtcom = (vxtcom_ctrlr_t *)vxtcom_handle;

	ret = vxtcom_find_lib(vxtcom->device_libraries, 
	                      device_type, &library);
	if (ret != VXT_SUCCESS) {
		return VXT_PARM;
	}

	if (library->use_count != 0) {
		return VXT_BUSY;
	}

	
	ret = vxtcom_rem(&(vxtcom->device_libraries), device_type);

	if (ret) {
		return VXT_FAIL;
	}

	vxt_kfree(library);

	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_add_device:
 *
 * vxtcom_add_device is invoked when a caller wishes to create a device
 * of a specified type.  The caller passes the device_type as a parameter
 * and the vxtcom must check its list to see if this particular device type
 * is registered.
 *
 * With the exception of a single event channel for signalling between 
 * the endpoints, The vxtcom_add_device call does not directly provision
 * the new device. Allocation of shared memory queues, any additional
 * signal event channels and other ancillary resources all happen as a
 * result of callbacks into the controller made by the device library 
 * create routine.  This allows the resources allocated to be easily
 * customized based on device type.
 *
 * Note: Caller must be aware, vxtcom_add_device is asynchronous.  
 * The device connect code and hence this routine may block while
 * the connection with the remote controller is established.
 *
 * The new device instance handle and the structure of callbacks
 * publishing the controller function library for device
 * support are passed back to the caller.
 * 
 * Locks: Called with the Contoller Lock, (bus config mutex) set
 *
 *
 * Out of band error signaling: The sync_complete parameter is an
 * out of band error response channel.  vxtcom_add_device can
 * be called by proxy from the remote controller on systems where
 * the remote controller is not allowed to create devices.  In
 * this way we preserve the ability for both endpoint guests to
 * initiate the creation of a device.
 *
 *
 *
 * RETURNS:
 */

int
vxtcom_add_device(vxtcom_ctrlr_t *vxtcom, vxtdev_type_t device_type,
                  char *remote_ep, void *dev_parms, uint32_t info_length,
                  vxtdev_handle_t *newdev_instance, 
                  vxtcom_ctrlr_api_t *callbacks, 
                  vxtcfg_sync_callback_t *sync_complete)
{

	vxtdev_t        *newdev;
	void            *newdev_handle;
	symlibs_t       *library;
	vxtsig_handle_t event_handle;
	uint32_t	slot_handle;
	int             ret;

	UMI_LOG(27, VXTPRINT_PROFILE_LEVEL,
	        "called, cntrlr %p, lib %llx, remote %s\n",
	        vxtcom, device_type, remote_ep);
	if (vxtcom->pending_shutdown == 1) {
		return VXT_FAIL;
	}

	/*
	 * Check for registration of the targeted device type
	 */
	ret = vxtcom_find_lib(vxtcom->device_libraries, 
	                      device_type, &library);
	if (ret != VXT_SUCCESS) {
		vxtcfg_synchronous_reply(sync_complete, ret);
		vxtcfg_signal_config_queue(vxtcom);
		return ret;
	}

	/* 
	 * set up a new device structure on the targeted
	 * vxt controller
	 */

	newdev = vxt_kmalloc(sizeof(struct vxtdev), VXT_KMEM_KERNEL);
	if (newdev == NULL) {
		vxtcfg_synchronous_reply(sync_complete, VXT_NOMEM);
		vxtcfg_signal_config_queue(vxtcom);
		return VXT_NOMEM;
	}
	vxtcom_init_wait(&newdev->wq);
	newdev->instance = NULL;
	newdev->os_dev = 0;
	newdev->waiting = 0;
	newdev->type = device_type;
	newdev->controller = vxtcom;
	newdev->dev = &(library->callbacks);
	newdev->dev_queues = NULL;
	newdev->remote_resources = FALSE;
	/* string here triggers file dev release on device shutdown */
	newdev->dev_file_name[0] = 0;
	newdev->of_rec = NULL;
	newdev->shutting_down = 0;
	/*
	 * Grab a signal index from vxt_signal.  This
	 * is our local handle.  When we get connection
	 * reply from the remote controller, it will
	 * contain the remote handle.  We will register
	 * it on this local handle by calling the vxt_signal
	 * register remote routine.
	 */
	ret = vxtsig_object_create(vxtcom, newdev,
	                           vxtcom->bus.dev_events, &event_handle);
	if (ret != VXT_SUCCESS) {
		vxt_kfree(newdev);
		vxtcfg_synchronous_reply(sync_complete, VXT_RSRC);
		vxtcfg_signal_config_queue(vxtcom);
		return VXT_RSRC;
	}
	newdev->event = event_handle;
	vxtcom_strncpy(newdev->uname, remote_ep, MAX_VXT_UNAME);
	newdev->error = VXT_SUCCESS;
	newdev->state = VXT_COM_DEV_CONNECTING;
	newdev->library = library;
	newdev->origin = 1;  /* this device will connect to remote */
	newdev->next = NULL;


	if (vxtcom_get_slot_handle(vxtcom, &newdev->slot)) {
		vxtsig_object_destroy(newdev->controller->bus.dev_events, 
		                      newdev->event);
		vxt_kfree(newdev);
		vxtcfg_synchronous_reply(sync_complete, VXT_RSRC);
		vxtcfg_signal_config_queue(vxtcom);
		return VXT_RSRC;
        }

	/*
	 * Note: If we are not the privileged Domain, we do not 
	 * yet know our remote domain.  This is  provided to us
	 * via the remote authorization controller. We fill in the
	 * remote authorization address, (DOM0), and let the connect
	 * call do an update if necessary.
	 *
	 * In the case that this code is run out of DOM0, the default
	 * value is the correct one.  This is because each of the
	 * controllers has its own connection point device file.
	 * The caller does the ioctl against the appropriate device
	 * file.
	 */

	newdev->remote_dom = vxtcom->ctrlr_list.rdom;

	/*
	 * Call the device code to create an instance of the device
	 * Note: dev_parms is a self-defined, device specific field
	 * The field can be used to pass device specific customization
	 * directives, resources which exist outside of the vxt
	 * controller model.
	 */
	UMI_LOG(86, VXTPRINT_ALPHA_LEVEL,
	        "calling msgdev_create with sync_complete = %p\n",
	        sync_complete);
	slot_handle = newdev->slot.handle;
	ret = 
	   newdev->dev->create((void *)newdev, dev_parms, info_length,
	                       callbacks, (void *)sync_complete,
	                       &newdev_handle);

	if (ret != VXT_SUCCESS) {
		int        err;
		vxt_slot_t *slot;
		/*
		 * We may have been waiting with the lock off
		 * Device remove can sneak in here.
		 * Find the device again before continuing
		 */
		err = vxtcom_find_slot(vxtcom, slot_handle, &slot);
		if (err != VXT_SUCCESS) {
			UMI_WARN(600, VXTPRINT_PRODUCT_LEVEL, 
         		         "device destroyed while we were "
			         "creating it\n"); 
			return ret;
		}
		/*
		 * Device is still with us, remove it
		 */
		vxtsig_object_destroy(newdev->controller->bus.dev_events, 
		                      newdev->event);
		vxtcom_free_slot_handle(vxtcom, newdev->slot.handle);
		vxt_kfree(newdev);
		return ret;
	}

	newdev->instance = newdev_handle;

	/*
	 * Caller wants the controller slot
	 * to easily identify the device
	 * in future.
	 */
	*newdev_instance = newdev->slot.handle;


	/*
	 * Add the new device to the 
	 * list of devices on this
	 * controller
	 */
	newdev->next = vxtcom->dev_list;
	vxtcom->dev_list = newdev;

	/*
	 * Increment library use count, library cannot
	 * be unloaded while there are active devices
	 * associated with it.
	 */

	library->use_count++;

	/*
	 * Signal os level that a new device has been plugged in
	 */
	ret = vxtcom_os_device_installed(vxtcom, newdev);
	if (ret != VXT_SUCCESS) {
		return VXT_FAIL;
	}
	/*
	 * Tell any local listeners that
	 * we have a new device
	 */
	library->dev_event_list |= VXT_POLL_NEW_DEV;
	vxtcom->dev_event_list |= VXT_POLL_NEW_DEV;
	vxtcom_wakeup(&library->new_dev_wait);
	vxtcom_wakeup(&vxtcom->new_dev_wait);
	vxtcom_ctrlr_event++;
	vxtcom_ctrlr_poll_events |= POLL_VXT_NEW_DEV;
	vxtcom_wakeup(&vxtcom_ctrlr_changed);
	vxtcom_wakeup(&vxtsys_new_dev_wait);
	vxtsys_dev_event_list |= VXT_POLL_NEW_DEV;


	return VXT_SUCCESS;  
}


/*
 *
 * vxtcom_query_bus:
 *
 * vxtcom_query_bus is an ioctl handler.  It performs lookups
 * on devices found on the target controller.
 *
 * There are three types of lookups:
 *
 * VXTCTRLR_LOOKUP_NAME allows the user to search for a specific
 * connection paired device.  The name is a unique identifier for
 * endpoint matching and communication pipe instance.  If a 
 * device with matching name is found, the slot number and device
 * type are returned.
 *
 * VXTCTRLR_GLOBAL_LOOKUP works the same as VXTCTRLR_LOOKUP_NAME
 * except that it scans all active controllers looking for a name
 * This is the compliment for the global poll which will be 
 * signalled for new device appearance on any controller.  
 *
 * VXTCTRLR_LOOKUP_NEXT searches by slot number for the next
 * card after the slot id given by the user.  Slot 0 is undefined
 * so this can be used as the first value in a bus walk.  If
 * a device is found to occupy a higher valued slot than the
 * one indicated in the calling parameters, its slot number is
 * returned along with the device uname, and device type.
 *
 *
 * Returns:
 *		0 - Device matching criteria found
 *
 *		-EINVAL - file descriptor not set up or bad function
 *
 *		-ENXIO - no device matching description found
 *
 */

int
vxtcom_query_bus(vxtcom_ctrlr_t *vxtcom, vxt_query_bus_struct_t *ibuf)
{
	vxtdev_t *device;
	uint32_t name_match_length = MAX_VXT_UNAME;

	UMI_LOG(28, VXTPRINT_PROFILE_LEVEL, "called, controller %p\n", vxtcom);
	if (ibuf->function == VXTCTRLR_CTRLR_LOOKUP) {
		UMI_LOG(87, VXTPRINT_ALPHA_LEVEL,"VXTCTRLR_CTRLR_LOOKUP\n");
		vxtcom = active_controllers;
		if (vxtcom == NULL) {
			ibuf->controller = 0;
			ibuf->ctrlr_name[0] = 0;
			return 0;
		}
		if (ibuf->controller == 0) {
			ibuf->controller = vxtcom->major;
			vxtcom_strncpy(ibuf->ctrlr_name,
			               vxtcom->file_name,
			               VXTCOM_MAX_DEV_FILE_NAME);
			return 0;
		}
		while (vxtcom != NULL) {
			if (vxtcom->major == ibuf->controller) {
				vxtcom = vxtcom->next;
				if (vxtcom == NULL) {
					ibuf->controller = 0;
					ibuf->ctrlr_name[0] = 0;
				} else {
					ibuf->controller = vxtcom->major;
					vxtcom_strncpy(ibuf->ctrlr_name,
					              vxtcom->file_name,
					              VXTCOM_MAX_DEV_FILE_NAME);
				}
				return 0;
			}
			vxtcom = vxtcom->next;
		}
		ibuf->controller = 0;
		ibuf->ctrlr_name[0] = 0;
		return 0;
	}
	device = vxtcom->dev_list;
	if (ibuf->function == VXTCTRLR_LOOKUP_NAME) {
		UMI_LOG(88, VXTPRINT_ALPHA_LEVEL,"VXTCTRLR_LOOKUP_NAME\n");
		while (device != NULL) {
			if ((ibuf->name_match_length) && 
			    (ibuf->name_match_length < MAX_VXT_UNAME)) {
				name_match_length = ibuf->name_match_length;
			}
			if (strncmp(device->uname,
			            ibuf->uname, name_match_length)) {
				device = device->next;
			} else {
				ibuf->dev_name = device->type;
				ibuf->vxt_bus_id = device->slot.handle;
				ibuf->controller = vxtcom->major;
				vxtcom_strncpy(ibuf->ctrlr_name,
				               vxtcom->file_name,
				               VXTCOM_MAX_DEV_FILE_NAME);
				/*
				 * Copy the entire name, we may have
				 * been matching a prefix
				 */
				vxtcom_strncpy(ibuf->uname,
				               device->uname,
				               MAX_VXT_UNAME);

				return 0;
			}
		}
		return 0;
	} else if (ibuf->function == VXTCTRLR_GLOBAL_LOOKUP) {
		UMI_LOG(89, VXTPRINT_ALPHA_LEVEL,"VXTCTRLR_GLOBAL_LOOKUP\n");
		vxtcom = active_controllers;
		if (ibuf->option == VXTCTRLR_GLOBAL_CTRLR_SCAN) {
			/*
			 * Start scan on the controller after the
			 * one given.
			 */
			while (vxtcom != NULL) {
				if (ibuf->controller == vxtcom->major) {
					vxtcom = vxtcom->next;
					break;
				}
				vxtcom = vxtcom->next;
			}
		}
		while (vxtcom != NULL) {
			device = vxtcom->dev_list;
			while (device != NULL) {
				if ((ibuf->name_match_length) && 
				    (ibuf->name_match_length < MAX_VXT_UNAME)) {
					name_match_length =
					   ibuf->name_match_length;
				}
				if (strncmp(device->uname, 
				    ibuf->uname, name_match_length)) {
					device = device->next;
				} else {
					ibuf->dev_name = device->type;
					ibuf->vxt_bus_id = device->slot.handle;
					ibuf->controller = vxtcom->major;
					vxtcom_strncpy(ibuf->ctrlr_name,
					              vxtcom->file_name,
					              VXTCOM_MAX_DEV_FILE_NAME);
					/*
					 * Copy the entire name, we may have
					 * been matching a prefix
					 */
					vxtcom_strncpy(ibuf->uname,
					            device->uname,
					            MAX_VXT_UNAME);
					return 0;
				}
			}
			vxtcom = vxtcom->next;
		}
		return -ENXIO;
	} else if (ibuf->function == VXTCTRLR_LOOKUP_NEXT) {
		vxtdev_t *nextdev;

		nextdev = NULL;   
		UMI_LOG(90, VXTPRINT_ALPHA_LEVEL,
		        "VXTCTRLR_LOOKUP_NEXT attempt, starting at %llx\n",
		        ibuf->vxt_bus_id);
		while (device != NULL) {
			if (device->slot.handle > ibuf->vxt_bus_id) {
				if ((nextdev == NULL) || 
				   (nextdev->slot.handle > 
				    device->slot.handle)) { 
					nextdev = device;
				}
			}
			device = device->next;
		}

		if (nextdev == NULL) {
			return -ENXIO;
		}

		ibuf->vxt_bus_id = nextdev->slot.handle;
		vxtcom_strncpy(ibuf->uname, nextdev->uname, MAX_VXT_UNAME);
		ibuf->dev_name = nextdev->type;
		ibuf->controller = vxtcom->major;
		vxtcom_strncpy(ibuf->ctrlr_name,
		               vxtcom->file_name, VXTCOM_MAX_DEV_FILE_NAME);
		return 0;
	}
	return -EINVAL;
}


/*
 *
 * vxtcom_query_device:
 *
 * Lookup the device structure associated with the slot handle
 * passed.  Call the device specific query function found
 * in the targeted device with the buffer provided.  Data
 * may be passed to the device in this buffer.  It is expected
 * that data will be returned.  The buffer is a "void *" to
 * this routine.
 *
 *
 * Returns:
 *
 *		0 => Success
 *
 *		-EINVAL - file descriptor not set up
 *		          device handle not valid
 *		          specific device query failed
 *
 *
 */

int
vxtcom_query_device(vxtcom_ctrlr_t *vxtcom, vxt_query_dev_struct_t *ibuf)
{
	vxtdev_t              *device;
	vxt_slot_t            *slot;
	int                   ret;

	UMI_LOG(29, VXTPRINT_PROFILE_LEVEL, "called, controller %p\n", vxtcom);
	ret = vxtcom_find_slot(vxtcom, ibuf->vxt_bus_id, &slot);
	if (ret != VXT_SUCCESS) {
		UMI_LOG(49, VXTPRINT_BETA_LEVEL,
		        "failed to find device, slot %llx, ctrlr %s\n",
		        ibuf->vxt_bus_id, vxtcom->file_name);
		return -EINVAL;
	}

	/*
	 * Find parent structure
	 */
	device = vxtcom_slot_find_device(slot);
	/*
	 * State of connection to remote
	 */
	ibuf->device_state = device->state;
	
	/*
	 * Note: we used the callers info_buf_size to malloc a buffer
	 * and copy the users device specific data.  Therefore we can
	 * use this same field to ensure against buffer overrun in our
	 * device specific query routine.  However, each device
	 * implemenentation must check, since we have no way of knowing
	 * how big the buffer should be.
	 */
	ret = 
	   device->dev->query((void *)device->instance,
	                      ibuf->device_info, ibuf->info_buf_size);

	if (ret != VXT_SUCCESS) {
		UMI_LOG(50, VXTPRINT_BETA_LEVEL,
		        "device specific code failed, slot %llx, ctrlr %s\n",
		        ibuf->vxt_bus_id, vxtcom->file_name);
		return -EINVAL;
	}

	return 0;
}


/*
 * 
 * vxtcom_attach_dev:
 *
 * vxtcom_attach_dev is an ioctl handler that allows the caller
 * to attach to a device instance.  The attach involves the
 * provision of resources associated with the device to the
 * caller.
 *
 * The device may already be attached and may not be sharable
 * The parameters passed in the device specific data may not
 * be compatible, the version may be wrong.  All of these 
 * issues are device specific and are not reflected in the
 * transport at the controller level.  This routine concerns
 * itself with determining that there is a controller that the
 * controller slot targeted is populated and that the proper
 * parameters are passed to the device specific attach routine.
 *
 * The device, connected status at the level of the controller
 * tells us whether the device is acutally connected to the
 * remote endpoint.  This could be used as a gate for attachment
 * but devices can be attached which are not connected. 
 *
 * Returns:
 *
 *		0 => upon successful completion
 *		-EINVAL - file descriptor is not set up,
 *		          controller slot is not populated
 *		          device specific attach fails
 *
 *
 */

int
vxtcom_attach_dev(vxtcom_ctrlr_t *vxtcom, vxt_attach_dev_struct_t *ibuf)
{
	vxtdev_t              *device;
	vxt_slot_t            *slot;
	int                   ret;

	UMI_LOG(30, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, slot %llx\n",
	        vxtcom, ibuf->vxt_bus_id);
	ret = vxtcom_find_slot(vxtcom, ibuf->vxt_bus_id, &slot);
	if (ret != VXT_SUCCESS) {
		return -EINVAL;
	}

	/*
	 * Find parent structure
	 */
	device = vxtcom_slot_find_device(slot);
	/*
	 * State of connection to remote
	 */
	ibuf->device_state = device->state;
	
	ret = 
	   device->dev->attach(device->instance,
	                       ibuf->device_info, ibuf->info_buf_size);

	if (ret != VXT_SUCCESS) {
		return -EINVAL;
	}

	return 0;
}


/*
 *
 * vxtcom_detach_dev:
 *
 * vxtcom_detach_dev is an ioctl that allows the caller to 
 * end its association with a registered device on the 
 * targeted controller.  Checks for the appropriateness
 * of the call are deferred to the device specific code.
 * vxtcom_detach_dev concerns itself with routing the call
 * to the proper device instance based on the controller
 * associated with the file descriptor, and the device
 * slot.  The controller must exist and the controller slot
 * must be populated.  However, the device itself may or
 * may not be connected to a remote endpoint.
 *
 * Returns:
 *
 *		0 => Success
 *		-EINVAL - file descriptor is not set up,
 *		          controller slot is not populated
 *		          device specific detach failed
 *
 *
 */

int
vxtcom_detach_dev(vxtcom_ctrlr_t *vxtcom, vxt_detach_dev_struct_t *ibuf)
{
	vxtdev_t              *device;
	vxt_slot_t            *slot;
	int                   ret;

	UMI_LOG(31, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, slot %llx\n",
	        vxtcom, ibuf->handle);

	ret = vxtcom_find_slot(vxtcom, ibuf->handle, &slot);
	if (ret != VXT_SUCCESS) {
		return -EINVAL;
	}
	UMI_LOG(51, VXTPRINT_BETA_LEVEL,"found slot\n");

	/*
	 * Find parent structure
	 */
	device = vxtcom_slot_find_device(slot);
	/*
	 * State of connection to remote
	 */
	ibuf->device_state = device->state;
	
	ret = 
	   device->dev->detach(device->instance, ibuf->device_info,
	                       ibuf->info_buf_size);
	UMI_LOG(82, VXTPRINT_DEBUG_LEVEL,
	        "return from device specific call 0x%x\n", ret);

	if (ret != VXT_SUCCESS) {
		return -EINVAL;
	}

	return 0;
}


/*
 *
 * vxtcom_lookup_ctrlr:
 *
 * vxtcom_lookup_ctrlr finds the major number and file name
 * for a controller instance that "reaches" the remote domain
 * or universal connection name passed.
 *
 * If the name is NULL then the user is trying to find the 
 * controller that reaches the domain passed.
 *
 * Returns:
 *		0 ==> Success
 *		-EINVAL - No reaching controller found
 */

int
vxtcom_lookup_ctrlr(vxt_query_controller_struct_t *ibuf)
{
	domid_t        remote_domain;
	vxtcom_ctrlr_t *controller;
	vxtdev_t       *device;
	uint32_t       name_match_length = MAX_VXT_UNAME;
	int            ret;

	UMI_LOG(32, VXTPRINT_PROFILE_LEVEL, "called\n");
	if (ibuf->name_match_length) {
		name_match_length = ibuf->name_match_length;
	}
	if (ibuf->uname[0] == 0) {
		remote_domain = ibuf->remote_domain;
	} else {
		vxt_db_lookup_rec_t record;
		/*
		 * Client lookup here is used rather liberally
		 * The application running on the remote indicated by
		 * the ep_id may actually be a client or a server.
		 * In the case of hub action, the client denotation
		 * indicates the many to one relationship.  i.e. 
		 * the ep_id/uname must form a master key.  In the
		 * case of a guest, the Client ep_id is always the
		 * default Hub (Dom0)  This is because all configuration
		 * actions are mediated through the authentication
		 * server located in the hub.  This includes client/server
		 * connection requests in which both endpoints are
		 * local guests, or guest connected through hardware
		 * that bypasses Dom0 involvement during normal data
		 * transfer actions.
		 *
		 * In physical hardware cases where a guest is acting
		 * as a server, mediated routing can be bypassed if
		 * the guest acting as server is considered part of the
		 * Trusted Computing Base, (TCB), of the security domain 
		 * or level comprising the field of connections the 
		 * physical hardware is capable of connecting to.  In 
		 * this case, local authentication records within the
		 * guest auth-database may indicate that the client is
		 * other than the local Dom0.  The subsequent connection
		 * request through the device will not use the default
		 * vxt controller but rather a mechanism specific to the
		 * physical hardware.  The construction of the guest
		 * physical auth-db is general and so can support this
		 * alternate topology.
		 *
		 */

		if (!strncmp(ibuf->ep_id, VXTDB_DOM0_ENDPOINT, MAX_VXT_UUID)) {
			remote_domain = 0;
		} else {
			ret = vxtdb_client_lookup(ibuf->ep_id,
			                          ibuf->uname, &record);
			UMI_LOG(612, VXTPRINT_BETA_LEVEL,
			        "vxtdb_client_lookup of uuid = %s, "
			        "dev = %s, return = 0x%x\n",
			        ibuf->ep_id, ibuf->uname, ret);
	
			if (ret != VXT_SUCCESS) {
				UMI_LOG(613, VXTPRINT_PRODUCT_LEVEL,
				        "Authorization record not found for"
				        " guest %s, device %s\n",
				        ibuf->ep_id, ibuf->uname);
	
				return -EINVAL;
			}
			remote_domain = record.client_dom;
		}
	}
		
	/*
	 * Look for a connection to the remote domain
 	 */
	ret = vxtcom_find_ctrlr_pair(remote_domain, &controller);
	if (ret != VXT_SUCCESS) {
		/*
		 * Authorization came up empty and user domain was empty or
		 * wrong.  Last ditch effort, try to find an existing device
		 * among controllers.
		 */
		if (ibuf->uname == NULL) {
			return -EINVAL;
		}
		controller = active_controllers;
		while (controller != NULL) {
			device = controller->dev_list;
			while (device != NULL) {
				if (strncmp(device->uname, 
				    ibuf->uname, name_match_length)) {
					device = device->next;
				} else {
					break;
				}
			}
			if (device != NULL) {
				break;
			}
			controller = controller->next;
		}
		if (controller == NULL) {
			return -EINVAL;
		}
	}

	/*
	 * Found a local controller instance that
	 * connects
	 */
	UMI_LOG(52, VXTPRINT_BETA_LEVEL,"found the controller pair\n");
	vxtcom_strncpy(ibuf->ctrlr_name, 
	               controller->file_name, VXTCOM_MAX_DEV_FILE_NAME);
	ibuf->controller = controller->major;
	return 0;

}

/*
 *
 * vxtcom_create_dev:
 *
 * IOCTL front end for vxtcom_add_device, vxtcom_create_dev
 * pulls the parameters out of the vxt_create_dev_struct_t passed
 * to supply vxtcom_add_device.
 *
 * There are two modes of vxtcom_create_dev action.  In the primary
 * mode.  vxtcom_add_device is called directly.  Assuming success
 * a device of the prescribed type is allocated and plugged int
 * the targeted vxt controller.  The remote side is then contacted
 * the the vxt controller configuration path causing it to do
 * the same and to mapp whatever associated resources need to be
 * mapped.
 *
 * There is a second method of device creation however.  Because of
 * the assymetric nature of some hypervisor shared memory methods, 
 * (XenSource limited mapping capabilities), shared memory cannot
 * be mapped by untrusted guests in all cases.  A field in the
 * bus specific portion of the vxt controller structure is checked.
 * If it is set, vxtcom_create_dev will contact the remote host to
 * cause it to initiate the device creation act.
 *
 */

int
vxtcom_create_dev(vxtcom_ctrlr_t *vxtcom, vxt_create_dev_struct_t *ibuf)
{
	vxtcom_req_dev_create_wait_t *create_wait;
	int                          ret;

	UMI_LOG(33, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, uname %s\n",
	        vxtcom, ibuf->uname);

	if (vxtcom->pending_shutdown == 1) {
		return -EINVAL;
	}


	/*
	 * Check for indirect create requirement in the bus
	 * portion of the vxt controller structure.
	 */
	if (vxtcom->bus.indirect_create) {
		/*
		 * Create a request_dev_create structure
		 * with a wait queue attached.  Pass
		 * the pointer to this structure as an
		 * instance cookie. on the remote create
		 * request.
		 */
	
		create_wait = vxt_kmalloc(sizeof(vxtcom_req_dev_create_wait_t),
		                          VXT_KMEM_KERNEL);
		if (create_wait == NULL) {
		   return -ENOMEM;
		}
		/*
		 * Initialize the status field so that
		 * the default value indicates abort.
		 * If we are unexpectedly awakened this
		 * will be our response.
		 */
		create_wait->status = VXT_ABORT;
		create_wait->request_finished = 0;
		vxtcom_init_wait(&create_wait->wq);
		
		vxtcfg_request_dev_create(vxtcom, 
		                          (uint64_t)(vxtarch_word)create_wait,
		                          ibuf->dev_name, ibuf->uname,
		                          ibuf->device_info, ibuf->info_length);
		/*
		 * Wait for device creation on a callback
		 * allocate a request_dev_create wait
		 * structure, and pass it as a cookie on the 
		 */

		ret  = vxtcom_ctrlr_wait(vxtcom, &(create_wait->wq), (long *)
		                         &(create_wait->request_finished), 10);
		if (ret == VXT_SUCCESS) {
			if (create_wait->status == VXT_SUCCESS) {
				ibuf->vxt_bus_id = create_wait->slot_handle;
			} else {
				ret = create_wait->status;
			}
		}

		vxt_kfree(create_wait);

	} else {
		ret = vxtcom_add_device(vxtcom, ibuf->dev_name, ibuf->uname,
		                        ibuf->device_info, ibuf->info_length,
		                        &(ibuf->vxt_bus_id),
		                        &vxtcom_utilities, NULL);
	}
	if (ret) {
		return -EINVAL;
        }
	return 0;
}


/* 
 *
 * vxtcom_destroy_dev:
 *
 * IOCTL front end for vxtcom_remove_device, vxtcom_destroy_dev
 * uses the file descriptor to find the targeted controller and 
 * pulls the parameters out of the vxt_destroy_dev_struct_t passed
 * to supply vxtcom_remove_device.
 *
 * Returns:
 *
 *		0 => Success
 *		-EINVAL - Slot empty,
 *		          file descriptor not initialized,
 *		          vxtcom_remove_device failed
 * 
 */

int
vxtcom_destroy_dev(vxtcom_ctrlr_t *vxtcom, vxt_destroy_dev_struct_t *ibuf,
                   void *info, uint32_t info_length)
{
	vxtdev_t              *device;
	vxt_slot_t            *slot;
	int                   ret;

	UMI_LOG(34, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, slot %llx\n",
	        vxtcom, ibuf->vxt_bus_id);

	ret = vxtcom_find_slot(vxtcom, ibuf->vxt_bus_id, &slot);
	if (ret != VXT_SUCCESS) {
		return -EINVAL;
	}

	/*
	 * Find parent structure
	 */
	device = vxtcom_slot_find_device(slot);

	vxtcom_remove_device((void *)vxtcom, (void *)device, 
	                     info, info_length, TRUE);

	return 0;
}


/*
 *
 * vxtcom_remove_device_complete:
 *
 * wakes up the caller of remove request when the remote
 * party signals a reply.
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS
 */

int
vxtcom_remove_device_complete(vxtdev_t *device, int error)
{

	UMI_LOG(35, VXTPRINT_PROFILE_LEVEL,
	        "called device %p, error %08x\n",
	        device, error);

	device->waiting = 1;
	vxtcom_wakeup(&device->wq);
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_closing_ctrlr_complete:
 *
 * wakes up the caller of closing controller signal request when the remote
 * party sends a reply.
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS
 */

int
vxtcom_closing_ctrlr_complete(vxtcom_ctrlr_t *instance,
                              uint64_t wq,
                              uint64_t error)
{

	vxtcom_req_signal_closing_wait_t *closing_reply;

	UMI_LOG(430, VXTPRINT_PROFILE_LEVEL,
	        "called wq 0x%llx, error 0x%llx\n",
	        wq, error);

	closing_reply = (vxtcom_req_signal_closing_wait_t *)(vxtarch_word)wq;
	closing_reply->status = error;
	closing_reply->request_finished = 1;

	vxtcom_wakeup(&closing_reply->wq);
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_signal_ctrlr_closing:
 *
 * Signal to the remote side that the controller is going
 * down.
 *
 * vxtcom_signal_ctrlr_closing makes it call through the standard
 * shared memory configuration mechanism and waits for completion.
 *
 * Returns:
 *
 *		VXT_SUCCESS - Upon Successful completion
 *		VXT_FAIL: The message was either not receieve
 *		          or rejected by the remote.
 *		VXT_NOMEM: Failed malloc
 * 
 */


int
vxtcom_signal_ctrlr_closing(vxtcom_ctrlr_t *vxtcom, uint64_t flags)
{
	vxtcom_req_signal_closing_wait_t *create_wait;
	int                              ret;
	
	UMI_LOG(431, VXTPRINT_PROFILE_LEVEL, "called, controller %p\n",
	        vxtcom);
	create_wait = vxt_kmalloc(sizeof(vxtcom_req_signal_closing_wait_t),
                                  VXT_KMEM_KERNEL);
	if (create_wait == NULL) {
		return VXT_NOMEM;
	}

	create_wait->status = VXT_FAIL;
	create_wait->request_finished = 0;
	vxtcom_init_wait(&create_wait->wq);

	ret = vxtcfg_devreq_ctrlr_shutdown(vxtcom,
	                                   (uint64_t)(vxtarch_word)create_wait,
	                                   flags);

        /*
         * Wait for remote acknowledgment on a callback
         */

	ret  = vxtcom_ctrlr_wait(vxtcom, &(create_wait->wq), (long *)
	                         &(create_wait->request_finished), 2);
	if (ret == VXT_SUCCESS) {
		if (create_wait->status != VXT_SUCCESS) {
			ret = create_wait->status;
		}
	}

	vxt_kfree(create_wait);

	return ret;
}


/*
 *
 * vxtcom_bus_signal_closing:
 * 
 * vxtcom_bus_signal_closing is called from the bus driver
 * level to signal to the remote end that the bus connection is going
 * down.
 */

int
vxtcom_bus_signal_closing(vxtbus_ctrlr_t *bus_instance, uint64_t flags)
{
	vxtcom_ctrlr_t *instance;

	UMI_LOG(432, VXTPRINT_PROFILE_LEVEL, "called, bus %p\n", bus_instance);
	instance = vxtcom_find_ctrlr((vxtbus_ctrlr_t *)bus_instance);
	vxt_mlock(&instance->bus.config_mutex);
	vxtcom_signal_ctrlr_closing(instance, flags);
	vxt_munlock(&instance->bus.config_mutex);
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_system_shutdown:
 *
 * vxtcom_system_shutdown is called by the bus level support
 * It is called when the driver module for the controller is
 * being unloaded and all of the resources in vxt_controller
 * have to be freed.
 *
 * vxtcom_system_shutdown frees all bus level resources and
 * removes existing devices.  It cannot lower controller structure
 * counts however, these remain elevated until all users of the
 * structures are shutdown.  
 *
 * Locks:
 *             No locks are held upon invocation
 *
 * Returns:
 *
 *		VXT_SUCCESS - All controller resources were freed
 *
 * 		VXT_FAIL - Resources could not be freed, memory
 *		           leak will ensue.
 *
 */

int
vxtcom_system_shutdown(void)
{
	vxtcom_ctrlr_t  *instance;

	UMI_LOG(36, VXTPRINT_PROFILE_LEVEL, "called\n");
	vxt_mlock(&active_ctrlr_lock);
	instance = active_controllers;
	while (instance != NULL) {
		UMI_LOG(53, VXTPRINT_BETA_LEVEL,"found a live instance\n");
		/*
		 * remove an relevant controller pair
		 * entry.  
		 */
		vxt_mlock(&instance->bus.config_mutex);
		if (instance->pending_shutdown != 1) {
			vxtcom_remove_ctrlr_pair(&(instance->bus), 0);
			/*
			 * No new create activity allowed
			 */
			instance->pending_shutdown = 1;
			vxtcom_unplug_devices(instance, 1);
		}
		vxt_munlock(&instance->bus.config_mutex);
		instance = instance->next;
	}
	vxt_munlock(&active_ctrlr_lock);
	vxtcom_os_mod_remove();
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_poll_focus:
 *
 * vxtcom_poll_focus sets the device library that subsequent
 * poll operations will wait on.  If the value sent is
 * zero, the waiter on poll will be signaled for any new
 * device that shows up.  Otherwise, only devices for the
 * targeted library will cause a signal.
 *
 * The old value of device_wait is sent back via the by
 * reference parameter "dev_type".
 *
 * Returns:
 *
 *	VXT_SUCCESS
 *	VXT_PARM - The device type does not correspond to
 *	           a registered library.
 *
 */

int
vxtcom_poll_focus(vxtcom_ctrlr_t *instance, vxtcom_file_control_t *of,
                  vxtdev_type_t *dev_type)
{
	vxtdev_type_t old_type;
	symlibs_t     *library;
	int           ret;


	UMI_LOG(37, VXTPRINT_PROFILE_LEVEL,
	        "called, ctrlr %p, fdesc %p, dev_type %p\n",
	        instance, of, dev_type);
	if (*dev_type != VXT_UNIVERSAL) {
		ret = vxtcom_find_lib(instance->device_libraries,
		                      *dev_type, &library);
		if (ret != VXT_SUCCESS) {
			return VXT_PARM;
		}
	}
	/*
	 * The library type is registered go ahead
	 * and load the type into the file descriptor
	 */

	old_type = of->device_wait;
	of->device_wait = *dev_type;
	*dev_type = old_type;

	return VXT_SUCCESS;
	
}


/* IOCTL request routines */

/*
 *
 * vxtcom_process_config_msg:
 *
 *
 * Locks: The vxt controller lock is help upon invocation
 *
 *
 */


int
vxtcom_process_config_msg(vxtcom_ctrlr_t *instance, 
                          vxtcom_cntrl_hdr_t *remote_msg)
{

	UMI_LOG(38, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", instance);
	while (remote_msg->message_type != VXTCOM_CNTRL_NULL) {
		switch((remote_msg->message_type) & ~VXTCOM_CNTRL_ERROR) {
		case VXTCOM_CNTRL_TIMEOUT:
			break;
		case VXTCOM_CNTRL_PLUGIN:
			break;
		case VXTCOM_CNTRL_REMOVE:
			vxtcfg_config_rem_response(instance, remote_msg);
			break;
		case  VXTCOM_CNTRL_BUS_CLOSING:
			vxtcfg_config_closing_response(instance, remote_msg);
			break;
		case VXTCOM_CNTRL_SNAP:
			break;
		case VXTCOM_CNTRL_REQ_ADD:
			vxtcfg_config_create_response(instance, remote_msg);
			break;
		case VXTCOM_CNTRL_ADD_REQ:
			UMI_LOG(91, VXTPRINT_DEBUG_LEVEL,"add request\n");
			vxtcfg_config_add_req(instance, remote_msg);
			break;
		case VXTCOM_CNTRL_DATA:
			break;
		case (VXTCOM_CNTRL_TIMEOUT | VXTCOM_CNTRL_RESPONSE):
			break;
		case (VXTCOM_CNTRL_PLUGIN | VXTCOM_CNTRL_RESPONSE):
			break;
		case (VXTCOM_CNTRL_REMOVE | VXTCOM_CNTRL_RESPONSE):
			vxtcfg_config_rem_reply(instance, remote_msg);
			break;
		case (VXTCOM_CNTRL_BUS_CLOSING | VXTCOM_CNTRL_RESPONSE):
			vxtcfg_config_closing_reply(instance, remote_msg);
			break;
		case (VXTCOM_CNTRL_SNAP | VXTCOM_CNTRL_RESPONSE):
			break;
		case (VXTCOM_CNTRL_REQ_ADD | VXTCOM_CNTRL_RESPONSE):
			vxtcfg_config_proxy_create_reply(instance, remote_msg);
			break;
		case (VXTCOM_CNTRL_ADD_REQ | VXTCOM_CNTRL_RESPONSE):
			vxtcfg_config_add_reply(instance, remote_msg);
			break;
		case (VXTCOM_CNTRL_DATA | VXTCOM_CNTRL_RESPONSE):
			break;


		default:
			UMI_WARN(65, VXTPRINT_PRODUCT_LEVEL,
			         "unknown message type  %llx, remote addr %p\n",
			         remote_msg->message_type, remote_msg);
		}
		UMI_LOG(83, VXTPRINT_DEBUG_LEVEL,"last message, remote_msg "
		        "type %llx, remote_msg data length %llx\n",
		        remote_msg->message_type, remote_msg->datalen);
		/*
		 * We must do this here because we are signaled
		 * to look at the buffer for a request or
		 * a reply.  We may otherwise see stale data
		 */
		remote_msg->message_type = VXTCOM_CNTRL_NULL;
		remote_msg = (vxtcom_cntrl_hdr_t *)(vxtarch_word)
		             (((vxtarch_word)remote_msg) + 
		             sizeof(vxtcom_cntrl_hdr_t) + 
		             ((uint64_t)(remote_msg->datalen)));
		/* 
		 * CDY add a check for bounds here.  
		 * i.e. remote_msg >= instance->bus.inq && 
		 * remote_msg <= instance->bus.inq 
		 *                  + VXTCOM_CONFIG_QUEUE_REPLY_OFFSET
		 * Need a passed parm for bus.inq and a buffer size
		 */
		UMI_LOG(84, VXTPRINT_DEBUG_LEVEL,"next message,  remote_msg "
		        "type %llx, remote_msg data length %llx\n",
		        remote_msg->message_type, remote_msg->datalen);
	}

	return VXT_SUCCESS;
}

/*
 *
 * vxtcom_parse_config_msg:
 *
 * Called when we have been signalled by the remote
 * endpoint to check our receive queue.
 *
 * The receive queue is divided into a request
 * and reply portion.  In this way we can maintain
 * two way communication with requests originated
 * from either endpoint.
 *
 * The signal is treated as a synchronization point
 * If the remote party is sending us a reply, we
 * know the request messages we sent are no longer
 * needed.  If the remote party is sending us a 
 * request, we know that our replies from the
 * earlier series of requests have been handled.
 *
 * Returns:
 *
 *	VXT_SUCCESS - upon successful completion
 */

int
vxtcom_parse_config_msg(void *bus_instance)
{
	vxtcom_ctrlr_t *instance;
	vxtcom_cntrl_hdr_t *remote_msg;
/*  multiple message deliver CDY
	vxtcom_cntrl_hdr_t *next_msg;
*/

	UMI_LOG(39, VXTPRINT_PROFILE_LEVEL, "called, bus %p\n", bus_instance);
	instance = vxtcom_find_ctrlr((vxtbus_ctrlr_t *)bus_instance);
	vxt_mlock(&instance->bus.config_mutex);
	if ((instance->bus.inq == NULL) || (instance->bus.outq == NULL)) {
		UMI_WARN(54, VXTPRINT_BETA_LEVEL,
		         "config buffers are missing\n");
		return VXT_FAIL;
	}

	/*
	 * The 4k buffers are split into two, check the second
	 * sub buffer.  Note: this is done to avoid deadlocks.
	 * Only one request series can be outstanding at a time
	 * However, the remote controller can have an outstanding
	 * request, the following buffer is used for replies
	 */
	remote_msg = (vxtcom_cntrl_hdr_t *)
	             ((char *)instance->bus.inq
	              + VXTCOM_CONFIG_QUEUE_REPLY_OFFSET);
	if (remote_msg->message_type != VXTCOM_CNTRL_NULL) {


		vxtcom_cntrl_hdr_t *local_request = 
		   (vxtcom_cntrl_hdr_t *)instance->bus.outq;
		UMI_LOG(85, VXTPRINT_DEBUG_LEVEL,"Got a reply message, "
		        "addr %p, number %llx, length %llx\n",
		        remote_msg, remote_msg->message_type,
		        remote_msg->datalen);
		instance->bus.request_offset = 0;
		instance->bus.outq_header.request_in_use = 0;
		local_request->message_type = VXTCOM_CNTRL_NULL;
		vxtcom_process_config_msg(instance, remote_msg);
	}

	remote_msg = (vxtcom_cntrl_hdr_t *)instance->bus.inq;
	if (remote_msg == NULL) {
		UMI_WARN(55, VXTPRINT_BETA_LEVEL,
		         "config buffers are missing\n");
		vxt_munlock(&instance->bus.config_mutex);
		return VXT_FAIL;
	}

	/*
	 * Parse the requests if the message id isn't stale
	 * i.e. the producer is larger than the consumer
	 * unless the producer just wrapped.
	 * Note: all message id's in the buffer are 
	 * assumed to be the same.  The message id's then
	 * act as a buffer transaction id.
	 */
	if ((instance->bus.inq_header.msg_id < remote_msg->message_id) ||
	    ((remote_msg->message_id == 1) && 
	     (instance->bus.inq_header.msg_id + 1 == 0))) {
		UMI_LOG(56, VXTPRINT_BETA_LEVEL,
		        "success, we have a good message id\n");
		if ((instance->bus.inq_header.msg_id + 1) == 0) {
			instance->bus.inq_header.msg_id = 1;
		} else { 
			instance->bus.inq_header.msg_id++;
		}
		if (remote_msg->message_type != VXTCOM_CNTRL_NULL) {
			vxtcom_cntrl_hdr_t *local_reply = 
			   (vxtcom_cntrl_hdr_t *)
		           (((char *)instance->bus.outq)
			    + VXTCOM_CONFIG_QUEUE_REPLY_OFFSET);
			/*
			 * Our reply buffer is now free
			 */
			instance->bus.reply_offset = 0;
			instance->bus.outq_header.reply_in_use = 0;
			local_reply->message_type = VXTCOM_CNTRL_NULL;
			/*
			 * Go handle remote requests
			 */
			vxtcom_process_config_msg(instance, remote_msg);
			UMI_LOG(92, VXTPRINT_DEBUG_LEVEL,
			        "return from vxtcom_process_config_msg\n");
		}
	} else {
		UMI_WARN(57, VXTPRINT_BETA_LEVEL,"message format error\n");
	}


	if (instance->pending_send) {
		UMI_LOG(58, VXTPRINT_BETA_LEVEL,"signal config queue\n");
		vxtcfg_signal_config_queue(instance);
		instance->pending_send = 0;
	}

	vxt_munlock(&instance->bus.config_mutex);
	/*
	 * actions taken on behalf of the remote could lead to the
	 * pending loss of controller, check here, after the unlock
	 * action and wake anyone waiting on a HUP
	 */
	if (vxtcom_ctrlr_poll_events & POLL_VXT_CTRLR_DIED) {
		vxtcom_wakeup(&vxtcom_ctrlr_changed);
	}
	return VXT_SUCCESS;
}





/*
 *
 * vxtcom_ctrlr_init:
 *
 * Controller Character file creation/destruction
 *
 *
 */

int
vxtcom_ctrlr_init(vxtbus_ctrlr_t *bus_instance)
{
	vxtcom_ctrlr_t *instance;
	int            ret;

	UMI_LOG(40, VXTPRINT_PROFILE_LEVEL, "called, bus %p\n", bus_instance);
	instance = vxtcom_find_ctrlr(bus_instance);

	/*
	 * Add any built-in librarys here
	 */

	ret = vxtmsg_dev_load_lib((void *)instance);

	/*
	 * Make certain the authorization/routing database is
	 * started
	 */
	vxtdb_start_database(VXT_DB_DEFAULT_FILE_NAME);


	vxt_mlock(&active_ctrlr_lock);
	if (active_controllers == NULL) {
		ret = vxtcom_os_ctrlr_init(instance, TRUE);
	} else {
		ret = vxtcom_os_ctrlr_init(instance, FALSE);
	}
	
	if (ret != VXT_SUCCESS) {
		vxt_munlock(&active_ctrlr_lock);
		return (-EIO);
	}

	/*
	 * Install instance in our table of open controllers
	 */
	instance->next = active_controllers;
	active_controllers = instance;

	controller_count++;


	vxt_munlock(&active_ctrlr_lock);
	return 0;
}


/*
 *
 * vxtcom_moudle_init:
 *
 * vxtcom_module_init is called at module insertion time.  
 * It initializes all global resources.  i.e. those global
 * to individual vxt_controller instances  such as the 
 * vxt_controller active controller table.
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS => upon successful invocation
 *		VXT_FAIL - O.S. specific initialization failed
 */

int
vxtcom_module_init(void)
{
	int ret;

	UMI_LOG(41, VXTPRINT_PROFILE_LEVEL, "called\n");
	controller_count = 0;
	active_controllers = NULL;
	active_ctrlr_pairs = NULL;

	vxt_mlock_init(&sm_token_lock);
	vxt_mlock_init(&ctrlr_pair_lock);
	vxt_mlock_init(&active_ctrlr_lock);

	ret = vxtcom_os_mod_init();
	if (ret != VXT_SUCCESS) {
		return VXT_FAIL;
	}
	vxtcom_init_wait(&vxtcom_ctrlr_changed);
	vxtcom_init_wait(&vxtsys_new_dev_wait);
	return VXT_SUCCESS;
}


/* ********************* Exported local database interface ************** */
/*
 *  Responds to select queries targeting local runtime associations between
 *  guests and their domain_id assignments
 *
 */

/*
 * vxtcom_db_lookup:
 *
 * The vxt communications facility provides a database interface
 * to communicate run-time associations between guest UUIDs and
 * instantiation identifiers.  i.e. We can return a domain identifier
 * for a running guest based on its UUID.  The vxt com database
 * is limited to local runtime associations.  Larger topology 
 * concerns must be picked up at the vxt authorization database
 * level or other topology management tools.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon successful invocation and record match
 *		VXT_FAIL - No record found
 */

int
vxtcom_db_lookup(char *key, char *field, char* record_field)
{
	vxtcom_ctrlr_t     *instance;
	unsigned long long domain_id;
	int                ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, " called key \"%s\", "
	        "field \"%s\"\n", key, field);
	        
	/*
	 * Check for the UUID of a targeted guest via 
 	 * its running instance, (local domain name).
	 */
	if (!strncmp("/local/domain/", key, 14)) {
		if (!strncmp("vm", field, 2)) {
			UMI_LOG(0, VXTPRINT_DEBUG_LEVEL, "Domain to UUID\n");
			/*
			 * Looking up the domain from the UUID
			 *
			 */
			domain_id = simple_strtol(&key[14],
			                          (char **)NULL, 10); 
			vxt_mlock(&ctrlr_pair_lock);
			ret = vxtcom_find_ctrlr_pair(domain_id, &instance);

			if (ret == VXT_SUCCESS) {
				UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
				        "Found controller pair, "
				        "Remote UUID = %p\n",
				        instance->bus.remote_uuid);
				strcpy(record_field, "/vm/");
				strncpy(&record_field[4],
				        instance->bus.remote_uuid,
			                MAX_VXT_UUID);
			} else {
				vxt_munlock(&ctrlr_pair_lock);
				return VXT_FAIL;
			}
			
			vxt_munlock(&ctrlr_pair_lock);
			return VXT_SUCCESS;
		}
	}
	if (!strncmp("vm", key, 2)) {
		if ((field == NULL) || (field[0] == 0)) {
			/*
			 * We are looking for the local UUID of our
			 * Guest/Host, grab any controller from the
			 * the controller pair list and grab the local
			 * identifier field.
			 */
			vxt_mlock(&active_ctrlr_lock);
			instance = active_controllers;
			if (instance == NULL) {
				vxt_munlock(&active_ctrlr_lock);
				return VXT_FAIL;
			}
			strncpy(record_field,
			        instance->bus.local_uuid,
		                MAX_VXT_UUID);
			vxt_munlock(&active_ctrlr_lock);

			return VXT_SUCCESS;

		}
	}

	return VXT_FAIL;
	
}


/*
 *
 * vxtcom_db_multilook:
 *
 * As with vxtcom_db_look this is a local routine for communicating
 * the local bindings between guest UUIDs and local instantiations for
 * domain id's and other resources.
 *
 * vxtcom_db_multilook takes the same lookup parameters.  However,
 * it will return all records that apply.  The records are returned
 * in a long string of strings, and a count of the number of returned
 * records goes back in rec_cnt.
 *
 * Returns:
 *		VXT_SUCCESS => upon successful invocation
 *		VXT_FAIL       No records found or malformed parameter
 */

int
vxtcom_db_multilook(char *key, char *field,
                       char **results, uint32_t *rec_cnt)
{
	vxtcom_ctrlr_t     *instance;
	char               **records;
	int                match_cnt;
	int                i;
	
	match_cnt = 0;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, " called key \"%s\", "
	        "field \"%s\"\n", key, field);

	if (!strncmp("/local/domain", key, 13)) {
		if ((field == NULL) || (field[0] == 0)) {
			vxt_mlock(&active_ctrlr_lock);
			instance = active_controllers;
			if (instance == NULL) {
				UMI_LOG(0, VXTPRINT_DEBUG_LEVEL, 
				        " No Active Instances\n");
				vxt_munlock(&active_ctrlr_lock);
				return VXT_FAIL;
			}
			while (instance != NULL) {
			   instance = instance->next;
			   match_cnt++;
			}

			vxt_munlock(&active_ctrlr_lock);
			records = 
			   vxt_kmalloc((sizeof(char) * 25 * match_cnt)
			               + (match_cnt * sizeof(char **)),
			               VXT_KMEM_KERNEL);

			if (records == NULL) {
				UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL, 
				        " Unalbe to allocate records "
				        "structure\n");
				return VXT_FAIL;
			}

			vxt_mlock(&active_ctrlr_lock);
			instance = active_controllers;
			if (instance == NULL) {
				vxt_munlock(&active_ctrlr_lock);
				vxt_kfree(records);
				return VXT_FAIL;
			}

			i = 0;
			/* set pointer to first string at the end of the
			 * list of string pointers
			 */
			records[0] = (char *)
			             (((uint8_t *)records)
			              + (sizeof(char **) * match_cnt)); 
			while (instance != NULL) {
				sprintf(records[i], "%lld",
			                (unsigned long long)
					(vxtarch_word)instance->bus.kvm);
				UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
				        "Domain Found, Name = %s\n",
				        records[i]);
				i++;
				if (i == match_cnt) {
					break;
				}
				records[i] = (char *)
				             (((uint8_t *)records[i-1])
				              + (25 * (sizeof(char))));
			}

			match_cnt = i;
			vxt_munlock(&active_ctrlr_lock);

			*results = (char *)records;
			*rec_cnt = match_cnt;

			return VXT_SUCCESS;

		}
		
	}
	
	return VXT_FAIL;
			
}


int
vxtcom_test_add_dev(vxtbus_ctrlr_t *vxtbus, vxtdev_type_t device_type,
                    char *remote_ep, void *dev_parms, uint32_t info_length,
                    vxtdev_handle_t *newdev_instance)
{
	vxtcom_ctrlr_t *vxtcom;
	int            ret;

	UMI_LOG(42, VXTPRINT_PROFILE_LEVEL, "called\n");
	vxtcom = vxtcom_find_ctrlr(vxtbus);

	ret = vxtcom_add_device(vxtcom, device_type, remote_ep,
	                        dev_parms, info_length, newdev_instance,
	                        &vxtcom_utilities, NULL);
	return ret;
}


int
vxtcom_test_remote_add_dev(vxtbus_ctrlr_t *vxtbus, vxtdev_type_t device_type,
                    char *remote_ep, void *dev_parms,
                    int dev_parm_length)
{
	vxtcom_req_dev_create_wait_t *create_wait;
	vxtcom_ctrlr_t *vxtcom;
	int ret;

	UMI_LOG(43, VXTPRINT_PROFILE_LEVEL, "called\n");
	vxtcom = vxtcom_find_ctrlr(vxtbus);

	create_wait = vxt_kmalloc(sizeof(vxtcom_req_dev_create_wait_t),
	                          VXT_KMEM_KERNEL);
	if (create_wait == NULL) {
	   return -ENOMEM;
	}
	/*
	 * Initialize the status field so that
	 * the default value indicates abort.
	 * If we are unexpectedly awakened this
	 * will be our response.
	 */
	create_wait->status = VXT_ABORT;
	create_wait->request_finished = 0;
	vxtcom_init_wait(&create_wait->wq);
		
VXTPRINT(0,"calling instance specific create, wait addr = %p\n", create_wait);
	vxtcfg_request_dev_create(vxtcom, (uint64_t)(vxtarch_word)create_wait,
	                          device_type, remote_ep,
	                          dev_parms, dev_parm_length);

	if (vxtcom->pending_send) {
VXTPRINT(0,"signal config queue\n");
		vxtcfg_signal_config_queue(vxtcom);
		vxtcom->pending_send = 0;
	}

	/*
	 * Wait for device creation on a callback
	 * allocate a request_dev_create wait
	 * structure, and pass it as a cookie on the 
	 */
/*   test is inside of interrupt handler
	wait_event_interruptible(create_wait->wq, 
	                         create_wait->request_finished);

	ret = (create_wait->status);
*/
ret = VXT_SUCCESS;
VXTPRINT(0,"remote_test return = 0x%x, slot %llx\n", ret, create_wait->slot_handle);
/*   test is inside of interrupt handler
	vxt_kfree(create_wait);
*/

	return ret;
}

