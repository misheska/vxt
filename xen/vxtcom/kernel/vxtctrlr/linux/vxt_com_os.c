/*
 *
 * vxt_com_os.c:
 *
 * vxt_com_os.c holds the components of the vxt controller that 
 * are linux specific.  The module mostly consists of file handling
 * callbacks.  
 *
 * File handling callbacks have been instituted for vxt controller
 * IOCTLS, shared memory handling, and device polling.
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
#define _VXT_SUBSYSTEM_ 2


#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/mm.h>
#ifndef LEGACY_4X_LINUX
#ifdef SLES_SP2
#include <asm/uaccess.h>
#else
#include <linux/uaccess.h>
#endif
#else
#include <asm/uaccess.h>
#include <asm-generic/pgtable.h>
#endif
#include <linux/poll.h>
#include <linux/device.h>
#include <public/vxt_system.h>
#include <public/symcom_dev_table.h>
#include <vxtctrlr/common/vxtcom_controller.h>
#include <public/vxt_com.h>
#include <public/kernel/linux/vxtcom_embedded_os.h>
#include <public/kernel/vxtcom_devlib.h>
#include <public/kernel/bus/hypervisor/vxtcom_bus_ctrlr.h>
#include <public/kernel/vxt_signal.h>
#include <public/vxt_trace_entries.h>
#include <vxtctrlr/common/vxt_com_internal.h>
#include <public/kernel/vxt_module.h>
#include <vxtctrlr/linux/vxt_com_os.h>


/*
 * vxtcom_ctrlr_changed_signal:
 *
 * This wait queue is used by clients wishing
 * to be signaled when a new controller comes
 * on line.  Or a controller is shutdown
 */

vxtcom_wait_queue_t vxtcom_ctrlr_changed;
int vxtcom_ctrlr_event = 0;
int vxtcom_ctrlr_poll_events = 0;

vxtcom_wait_queue_t vxtsys_new_dev_wait;
int vxtsys_dev_event_list= 0;



/*
 * A generic bus device for query of the 
 * VXTCom subsystem controllers is provided
 */
int vxtcom_ctrlr_bus_device = 0;


/*
 * vxtcom_os_device_installed:
 *
 * vxtcom_os_device_installed is used to signal some O.S. specific
 * bus logic that a plug event has taken place.  This is done
 * after a device is fully initialized.
 *
 * The O.S. may find that it needs to register with services that
 * are not within the context of the VXT subsystem.  If such 
 * registration is asynchronous. A wait is permitted here as long
 * as it is done through the VxT IOCTL call context preserving,
 * vxtcom_ctrlr_wait function.
 *
 * This call is not needed in linux
 *
 * Returns:
 *
 *		None
 *
 */


int
vxtcom_os_device_installed(vxtcom_ctrlr_t *vxtcom, vxtdev_t *device)
{
	return VXT_SUCCESS;
}


/*
 * vxtcom_os_device_removed:
 *
 * vxtcom_os_device_removed is used to signal some O.S. specific
 * bus logic that an unplug event has taken place.  This is done
 * after a device is fully removed.
 *
 * This call is not needed in linux
 *
 * Returns:
 *
 *		None
 *
 */


int
vxtcom_os_device_removed(vxtcom_ctrlr_t *vxtcom, vxtdev_t *device)
{
	return VXT_SUCCESS;
}




/*
 *
 * vxtcom_init_wait:
 *
 * Wrapper for os specific thread wait
 * initialization primitive.
 *
 * Returns:
 *
 *            VXT_SUCCESS
 */

int
vxtcom_init_wait(vxtcom_wait_queue_t *vxt_wait)
{
	init_waitqueue_head(&(vxt_wait->wq));
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_wakeup:
 *
 * Wrapper for os specific thread wakeup
 * primitive.
 *
 * Returns:
 *
 *            VXT_SUCCESS
 */

int
vxtcom_wakeup(vxtcom_wait_queue_t *vxt_wait)
{
	wake_up(&(vxt_wait->wq));
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_wakeup_all:
 *
 * A variant of vxtcom_wakeup, vxtcom_wakeup_all
 * wakes all waiters on the targeted wait queue
 * instead of just the "next" one.
 *
 * Returns:
 *
 *            VXT_SUCCESS
 */

int
vxtcom_wakeup_all(vxtcom_wait_queue_t *vxt_wait)
{
	wake_up_all(&(vxt_wait->wq));
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_ctrlr_wait:
 *
 * vxtcom_ctrlr_wait releases the controller config lock and sleeps
 * on the wait queue that it is passed.
 *
 * 
 * Returns:
 *
 *		VXT_SUCCESS - after successful wait
 *		VXT_PARM - Controller no longer viable
 */

int
vxtcom_ctrlr_wait(vxtcom_ctrlr_t *vxtcom, 
                  vxtcom_wait_queue_t *vxt_wait,
                  long *condition, int timeout)
{
	vxtcom_ctrlr_t    *traveler;
	wait_queue_head_t *wq;
	int               timed_out = 0;

	wq = &vxt_wait->wq;

	UMI_LOG(248, VXTPRINT_DEBUG2_LEVEL,
                " called, controller %p wait_queue %p\n",
	        vxtcom, vxt_wait);
	/*
	 * Check to see if we have any config 
	 * messages to send.  We check in three
	 * places, all before unlocking the 
	 * controller lock.
	 * 1: When going to sleep on work from
	 *    the remote side
	 * 2: When returning from doing work
	 *    on an interrupt (config reply/request)
	 * 3: When returning from ioctl's
	 *    (spawned asynchronous config requests)
	 */
	if (vxtcom->pending_send) {
		UMI_LOG(299, VXTPRINT_BETA_LEVEL,"signal config queue\n");
		vxtcfg_signal_config_queue(vxtcom);
		vxtcom->pending_send = 0;
	}

	/*
	 * Release the controller lock before we sleep
	 */
	vxt_munlock(&(vxtcom->bus.config_mutex));

	if (timeout == 0) {
		wait_event_interruptible(*wq, *condition);
	} else {
		if (!(wait_event_interruptible_timeout(*wq, 
		                                       *condition,
		                                        timeout * HZ))) {
			UMI_WARN(220, VXTPRINT_PRODUCT_LEVEL, "timed out\n");
			timed_out = 1;
		}
	}


	/*
	 * find the controller in the active controller
	 * list.  It may have been removed while unlocked
	 */

	vxt_mlock(&active_ctrlr_lock);
	traveler = active_controllers;

	while (traveler != NULL) {
		if (vxtcom == traveler) {
			break;
		}
		traveler = traveler->next;
	}

	if (traveler == NULL) {
		/*
		 * Note: this may well be turned into an assert
		 * all calls coming into this routine are based
		 * on a reference count against the active controller
		 */
		vxt_munlock(&active_ctrlr_lock);
		return VXT_PARM;
	}

	vxt_mlock(&(vxtcom->bus.config_mutex));
	vxt_munlock(&active_ctrlr_lock);

	if (timed_out) {
		return VXT_TIMEOUT;
	}

	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_alloc_sm_object:
 *
 * Allocates a shared memory object to maintain the
 * resources associated with a shared memory queue.
 * The shared memory object holds a number of fields
 * that are operating system specific.  
 * vxtcom_alloc_sm_object initializes both os specific
 * and non-specific fields.
 *
 * Returns:
 *
 *		vxt_devcie_sm_t structure upon success,
 *		NULL otherwise.
 */

vxt_device_sm_t *
vxtcom_alloc_sm_object(void)
{
	vxt_device_sm_t *record;

	UMI_LOG(194, VXTPRINT_PROFILE_LEVEL, " called\n");
	record = vxt_kmalloc(sizeof(vxt_device_sm_t), VXT_KMEM_KERNEL);
	if (record == NULL) {
		UMI_WARN(221, VXTPRINT_PRODUCT_LEVEL,
		        "failed to allocate a structure\n");
		return record;
	}
	record->token = 0;
	record->dev_handle = 0;
	record->addr = NULL;
	record->len = 0;
	record->next = NULL;
	record->foreign_map.map = NULL;
	record->vma = NULL;
	record->ref_cnt = 1;
	record->remote_pages = NULL;
	record->user_handles = NULL;

	UMI_LOG(249, VXTPRINT_DEBUG_LEVEL, "returning, record = %p\n", record);
	return record;
}


/*
 *
 * vxtcom_destroy_sm_object:
 *
 * Delete the resources and structure of a targeted
 * shared memory object.
 *
 * Returns:
 *		None
 */

void
vxtcom_destroy_sm_object(vxt_device_sm_t *record)
{
	uint32_t pg_cnt;

	UMI_LOG(195, VXTPRINT_PROFILE_LEVEL, " called, record = %p\n", record);

	pg_cnt = record->len >> PAGE_SHIFT;

	/*
	 * First lets remove all mapping resources
	 * associated with user level mappings.
	 */

	if (record->user_handles) {
		vxt_kfree(record->user_handles);
	}
	if (record->foreign_map.map) {
		vxt_kfree(record->foreign_map.map);
	}
	if (record->remote_pages) {
		vxt_free_shared_memory(record->remote_pages, pg_cnt);
	}

	vxt_kfree(record);
	return; 
}


/*
 * vxtcom_devreq_add_queue:
 *
 * vxtcom_devreq_add_queue adds only a single queue
 * of specified length.
 *
 * vxtcom_devreq_add_queue is called by device instance
 * code when the code is initializing its shared memory
 * queues.  The controller does not keep track of the
 * size, number or disposition of queues, though it
 * does track the outstanding queue and mapping
 * allocations for extraordinary device clean-up
 * needs and the tracking of general clean-up information
 * which is particular to implementation.  i.e. page
 * share references from the grant table.
 *
 * Returns:
 *     VXT_SUCCESS
 *     VXT_NOMEM - Cannot allocate required memory
 *     VXT_PARM - Incoming device or controller invalid
 *     VXT_RSRC - Cannot allocate grant-table reference
 */

int
vxtcom_devreq_add_queue(void *device_handle, int qlen, 
			void **queue, void **queue_handle)

{
	void              *new_queue;
	vxtdev_queues_t   *q_pages;
	int               page_cnt;
	uint64_t          remote;
	vxtdev_t          *device = (void *)device_handle;
	int               ret;

/* CDY: Note this code has to be fixed
 * for guest to guest support.  We
 * must grant foreign access before we
 * attempt connection to the opposite side
 * we will need to insert a domain 
 * resolution call for the device
 * remote domain.
 */
	UMI_LOG(198, VXTPRINT_PROFILE_LEVEL,
	        " called, device %p, queue length %08x,\n",
	        device_handle, qlen);

	if (device->controller == NULL) {
		return VXT_PARM;
	}
/* CDY substitute a dom initialized variable, there is no invalid
   value here
	if (device->controller->bus.ctrlr_list.rdom == 0) {
		return VXT_PARM;
	}
*/
	remote = device->controller->ctrlr_list.rdom;
	/*
	 * Setup device queues and allow remote endpoint to connect
	 */
	page_cnt = (qlen + (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	/*
	 * Call the hypervisor specific shared memory allocation 
	 * function
	 */
	ret = vxtcard_create_mem_queue(remote, page_cnt, &q_pages, &new_queue);
	if (ret) {
		return ret;
	}

	/*
	 * Store queue allocation information on
	 * internal device state structure.
	 */
	q_pages->qaddr =  new_queue;
	q_pages->next = device->dev_queues;
	device->dev_queues = q_pages;

	*queue = (void *)new_queue;
	*queue_handle = (void *)q_pages;
	UMI_LOG(259, VXTPRINT_DEBUG_LEVEL," returning - queue %p, token %p\n",
	        *queue, *queue_handle);


	return VXT_SUCCESS;

}




/*
 * vxtcom_devreq_find_qpages:
 *
 * This is a helper function to allow linux style queue mapping
 * to find the original remote page references.  The linux
 * port of Xen special cases foreign page handling.  i.e. it
 * knows about it, opening up endless complications.
 *
 * In this case, we must remap because the associated physical
 * page address may be a pte entry location.
 *
 */

int
vxtcom_devreq_find_qpages(uint64_t dev_handle,  void *queue_address, 
                          vxtdev_queues_t **queue_resources)

{
	vxtdev_queues_t   *q_pages;
	vxtdev_t          *device = (vxtdev_t *)(vxtarch_word)dev_handle;

	UMI_LOG(200, VXTPRINT_PROFILE_LEVEL,
	        " called, device %p, queue address %p,\n",
	        device, queue_address);
	if (device == NULL) {
		UMI_WARN(226, VXTPRINT_PRODUCT_LEVEL, "no device\n");
		return VXT_PARM;
	}

	q_pages = device->dev_queues;
	if (q_pages == NULL) {
		UMI_WARN(227, VXTPRINT_PRODUCT_LEVEL, "no queues\n");
		return VXT_PARM;
	}

	while (q_pages != NULL) {
		if (q_pages->qaddr == queue_address) {
			UMI_LOG(262, VXTPRINT_DEBUG_LEVEL,"success\n");
			*queue_resources = q_pages;
			return VXT_SUCCESS;
		}
		q_pages = q_pages->next;
	}
	UMI_WARN(228, VXTPRINT_PRODUCT_LEVEL,"fail\n");
	*queue_resources = NULL;
	return VXT_PARM;

}


/*
 *
 * vxtcom_ioctl
 *
 */


int
vxtcom_ioctl(struct inode *inode, struct file *filep,
             unsigned command, unsigned long argument)
{
	vxtcom_ctrlr_t *instance;
	vxtcom_file_control_t *of;   /* open file state */
	vxt_slot_t     *slot;        /* used for signal and wait dev */
	vxtdev_t       *device;      /* used for signal and wait dev */
	int            ret;

	union ioctl_structures {
		vxt_query_bus_struct_t        query_bus_buf;
		vxt_query_dev_struct_t        query_dev_buf;
		vxt_query_controller_struct_t find_ctrlr_buf;
		vxt_attach_dev_struct_t       attach_dev_buf;
		vxt_detach_dev_struct_t       detach_dev_buf;
		vxt_create_dev_struct_t       create_dev_buf;
		vxt_destroy_dev_struct_t      destroy_dev_buf;
		vxt_poll_focus_struct_t       poll_focus_buf;
		vxt_trace_playback_parms_t    playback_parms_buf;
	} ibuf;
	
	/*
	 * This profile log is debug2 because it is on the
	 * data path
	 */
	UMI_LOG(263, VXTPRINT_DEBUG2_LEVEL,"command %llu\n", (uint64_t)command);

	if ((of = (vxtcom_file_control_t *)filep->private_data) == NULL) {
		UMI_WARN(229, VXTPRINT_PRODUCT_LEVEL, "invalid file pointer\n");
		return -EINVAL;
	}

	of = filep->private_data;
	instance = of->controller;
	if (instance == NULL) {
		vxt_mlock(&active_ctrlr_lock);
		instance = active_controllers;
		if (instance == NULL) {
			vxt_munlock(&active_ctrlr_lock);
			return -EINVAL;
		}
		vxt_mlock(&instance->bus.config_mutex);
		vxtcom_inc_ref(instance);
		vxt_munlock(&active_ctrlr_lock);
	} else {
		vxt_mlock(&instance->bus.config_mutex);
	}

	switch (command) {

	case IOCTL_VXTCTRLR_SIGNAL_DEV:
		/*
		 * Argument is a device slot.  The 
		 * proper signal hash is taken from the
		 * the device controller fields.  In
		 * this way overhead is diminished on
		 * this important data path call.
		 */
		ret = vxtcom_find_slot(instance, (int)argument, &slot);
		if (ret != VXT_SUCCESS) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EINVAL;
		}

		/*
		 * Find parent structure
		 */
		device = vxtcom_slot_find_device(slot);

		UMI_LOG(264, VXTPRINT_DEBUG2_LEVEL, "calling vxtsig_signal\n");
		vxtsig_signal(instance, instance->bus.dev_events, 
		              device->event, instance->bus.sig_write);
		break;

	case IOCTL_VXTCTRLR_WAIT_DEV:
		/*
		 * Argument is a device slot.  The 
		 * proper signal hash is taken from the
		 * the device controller fields.  In
		 * this way overhead is diminished on
		 * this important data path call.
		 */
		ret = vxtcom_find_slot(instance, (int)argument, &slot);
		UMI_LOG(265, VXTPRINT_DEBUG2_LEVEL,
		        "return from vxtcom_find_slot, ret 0x%x, slot %p\n",
		        ret, slot);
		if (ret != VXT_SUCCESS) {
			UMI_LOG(266, VXTPRINT_DEBUG_LEVEL,
			        "failed wait unlock ctrlr %p\n", instance);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EINVAL;
		}

		/*
		 * Find parent structure
		 */
		device = vxtcom_slot_find_device(slot);

		vxtsig_wait(instance, instance->bus.dev_events, 
		              device->event);
		break;

	case IOCTL_VXTCTRLR_QUERY_BUS:
		ret = copy_from_user((void *)&ibuf, (void __user *)argument, 
		                     sizeof(vxt_query_bus_struct_t));
		if (ret) {
			UMI_LOG(304, VXTPRINT_BETA_LEVEL,
			        "Error copying Query bus argument\n");
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		ret = vxtcom_query_bus(instance, 
		                       (vxt_query_bus_struct_t *)&ibuf);
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return ret;
		}
		ret = copy_to_user((void __user *)argument, (void *)&ibuf, 
		                   sizeof(vxt_query_bus_struct_t));
		if (ret) {
			UMI_LOG(305, VXTPRINT_BETA_LEVEL,
			        "Error writing Query bus argument\n");
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		
		break;

	case IOCTL_VXTCTRLR_QUERY_DEV:
	{
		void *device_buffer;
		void *user_dev_buf;
		ret = copy_from_user((void *)&ibuf, (void __user *)argument, 
		                     sizeof(vxt_query_dev_struct_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		/*
		 * Get a buffer for device specific fields
		 */
		device_buffer =
		   vxt_kmalloc(ibuf.query_dev_buf.info_buf_size,
		               VXT_KMEM_KERNEL);
		if (device_buffer == NULL) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -ENOMEM;
		}
		/*
		 * Copy data for device, customized query
		 */
		user_dev_buf = (void *)ibuf.query_dev_buf.device_info;
		ret = copy_from_user(device_buffer, 
		                     (void __user *)user_dev_buf,
		                     ibuf.query_dev_buf.info_buf_size);
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		/*
		 * Update local query structure to point to local
		 * device specific data.
		 */
		ibuf.query_dev_buf.device_info = device_buffer;
		/*
		 * Controller level query,  makes callback to
		 * device specific query.
		 */
		ret = vxtcom_query_device(instance, 
		                          (vxt_query_dev_struct_t *)&ibuf);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return ret;
		}
		/*
		 * Put back user address for device_buffer
		 */
		ibuf.query_dev_buf.device_info = user_dev_buf;

		/*
		 * Copy out device specific results
		 */
		ret = copy_to_user((void __user *)user_dev_buf, device_buffer, 
		                   ibuf.query_dev_buf.info_buf_size);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}

		/*
		 * Copy out controller fields
		 */
		ret = copy_to_user((void __user *)argument, (void *)&ibuf, 
		                   sizeof(vxt_query_dev_struct_t));
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		
		vxt_kfree(device_buffer);
		break;
	}

	case IOCTL_VXTCTRLR_ATTACH:
	{
		void *device_buffer;
		void *user_dev_buf;   /* usr addr for dev buf */

		ret = copy_from_user((void *)&ibuf, (void __user *)argument, 
		                     sizeof(vxt_attach_dev_struct_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		/*
		 * Get a buffer for device specific fields
		 */
		device_buffer =
		   vxt_kmalloc(ibuf.attach_dev_buf.info_buf_size,
		               VXT_KMEM_KERNEL);
		if (device_buffer == NULL) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -ENOMEM;
		}

		/*
 		 * Save pointer to guest device specific buffer
		 */
		user_dev_buf = (void *)ibuf.attach_dev_buf.device_info;
		/*
		 * Copy data for device, customized attach
		 */
		ret = copy_from_user(device_buffer, 
		                     (void __user *)user_dev_buf,
		                     ibuf.attach_dev_buf.info_buf_size);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		/*
		 * Update local attach structure to point to local
		 * device specific data.
		 */
		ibuf.attach_dev_buf.device_info = device_buffer;
		/*
		 * Controller level attach,  makes callback to
		 * device specific attach.
		 */
		ret = vxtcom_attach_dev(instance, 
		                        (vxt_attach_dev_struct_t *)&ibuf);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return ret;
		}
		/*
		 * Put back user address for device_buffer
		 */
		ibuf.attach_dev_buf.device_info = user_dev_buf;

		/*
		 * Copy out device specific results
		 */
		ret = copy_to_user((void __user *)user_dev_buf, device_buffer, 
		                   ibuf.attach_dev_buf.info_buf_size);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}

		/*
		 * Copy out controller fields
		 */
		ret = copy_to_user((void __user *)argument, (void *)&ibuf, 
		                   sizeof(vxt_attach_dev_struct_t));
		
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		vxt_kfree(device_buffer);
		break;
	}

	case IOCTL_VXTCTRLR_DETACH:
	{
		void *device_buffer;
		void *user_dev_buf;   /* usr addr for dev buf */

		ret = copy_from_user((void *)&ibuf, (void __user *)argument, 
		                     sizeof(vxt_detach_dev_struct_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		UMI_LOG(306, VXTPRINT_BETA_LEVEL, 
		        "IOCTL_VXTCTRLR_DETACH: copied detach "
		        "dev struct, dev info size 0x%x\n",
		        (uint32_t)ibuf.detach_dev_buf.info_buf_size);
		/*
		 * Get a buffer for device specific fields
		 */
		device_buffer =
		   vxt_kmalloc(ibuf.detach_dev_buf.info_buf_size,
		               VXT_KMEM_KERNEL);
		if (device_buffer == NULL) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -ENOMEM;
		}
		/*
		 * Copy data for device, customized detach
		 */
		user_dev_buf = (void *)ibuf.detach_dev_buf.device_info;
		ret = copy_from_user(device_buffer, 
		                     (void __user *)user_dev_buf,
		                     ibuf.detach_dev_buf.info_buf_size);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		UMI_LOG(267, VXTPRINT_DEBUG_LEVEL,
		        "IOCTL_VXTCTRLR_DETACH:copied dev info to temp buf\n");
		/*
		 * Update local attach structure to point to local
		 * device specific data.
		 */
		ibuf.detach_dev_buf.device_info = device_buffer;
		/*
		 * Controller level attach,  makes callback to
		 * device specific attach.
		 */
		ret = vxtcom_detach_dev(instance, 
		                        (vxt_detach_dev_struct_t *)&ibuf);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return ret;
		}
		/*
		 * Put back user address for device_buffer
		 */
		ibuf.detach_dev_buf.device_info = user_dev_buf;

		/*
		 * Copy out device specific results
		 */
		ret = copy_to_user((void __user *)user_dev_buf, device_buffer, 
		                   ibuf.detach_dev_buf.info_buf_size);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}

		/*
		 * Copy out controller fields
		 */
		ret = copy_to_user((void __user *)argument, (void *)&ibuf, 
		                   sizeof(vxt_detach_dev_struct_t));
		
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		vxt_kfree(device_buffer);
		break;
	}

	case IOCTL_VXTCTRLR_DEV_CREATE:
	{
		void *device_buffer;
		void *user_dev_buf;   /* usr addr for dev buf */

		ret = copy_from_user((void *)&ibuf, (void __user *)argument, 
		                     sizeof(vxt_create_dev_struct_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		/*
		 * Get a buffer for device specific fields
		 */
		device_buffer =
		   vxt_kmalloc(ibuf.create_dev_buf.info_length,
		               VXT_KMEM_KERNEL);
		if (device_buffer == NULL) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -ENOMEM;
		}
		/*
		 * Copy data for device, customized create
		 */
		user_dev_buf = (void *)ibuf.create_dev_buf.device_info;
		ret = copy_from_user(device_buffer, 
		                     (void __user *)user_dev_buf,
		                     ibuf.create_dev_buf.info_length);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		/*
		 * Update local attach structure to point to local
		 * device specific data.
		 */
		ibuf.create_dev_buf.device_info = device_buffer;
		/*
		 * Controller level attach,  makes callback to
		 * device specific attach.
		 */
		ret = vxtcom_create_dev(instance,
		                        (vxt_create_dev_struct_t *)&ibuf);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return ret;
		}
		/*
		 * Put back user address for device_buffer
		 */
		ibuf.create_dev_buf.device_info = user_dev_buf;

		/*
		 * Copy out device specific results
		 */
		ret = copy_to_user((void __user *)user_dev_buf, device_buffer, 
		                   ibuf.create_dev_buf.info_length);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}

		/*
		 * Copy out controller fields
		 */
		ret = copy_to_user((void __user *)argument, (void *)&ibuf, 
		                   sizeof(vxt_create_dev_struct_t));
		
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		vxt_kfree(device_buffer);
		break;
	}

	case IOCTL_VXTCTRLR_DEV_REMOVE:
	{
		void *device_buffer;
		void *user_dev_buf;   /* usr addr for dev buf */

		ret = copy_from_user((void *)&ibuf, (void __user *)argument, 
		                     sizeof(vxt_destroy_dev_struct_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		/*
		 * Get a buffer for device specific fields
		 */
		device_buffer =
		   vxt_kmalloc(ibuf.destroy_dev_buf.info_buf_size,
		               VXT_KMEM_KERNEL);
		if (device_buffer == NULL) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -ENOMEM;
		}

		/*
		 * Copy data for device, customized remove
		 */
		user_dev_buf = (void *)ibuf.destroy_dev_buf.device_info;
		ret = copy_from_user(device_buffer, 
		                     (void __user *)user_dev_buf,
		                     ibuf.destroy_dev_buf.info_buf_size);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		/*
		 * Controller level destroy,  makes callback to
		 * device specific remove.  The call to device
		 * specific remove can be initiated from an ioctl
		 * or from controller configuration. 
		 */
		ret = vxtcom_destroy_dev(instance,
		                        (vxt_destroy_dev_struct_t *)&ibuf,
		                        device_buffer, (uint32_t) 
		                        ibuf.destroy_dev_buf.info_buf_size);
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return ret;
		}

		/*
		 * Copy out controller fields
		 */
		ret = copy_to_user((void __user *)argument, (void *)&ibuf, 
		                   sizeof(vxt_destroy_dev_struct_t));
		
		if (ret) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		vxt_kfree(device_buffer);
		break;
	}
	case IOCTL_VXTCTRLR_POLL_FOCUS:
		ret = copy_from_user((void *)&ibuf, (void __user *)argument, 
		                     sizeof(vxt_poll_focus_struct_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		ret = vxtcom_poll_focus(instance, of,
		                        &(ibuf.poll_focus_buf.dev_type));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EINVAL;
		}
		ret = copy_to_user((void __user *)argument, (void *)&ibuf, 
		                   sizeof(vxt_poll_focus_struct_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		
		break;

	case IOCTL_VXTCTRLR_LOOKUP:
		/*
		 * Find the proper major number and device
		 * file name for the controller connected
		 * to either the universal name or endpoint
		 * provided.
		 */
		ret = copy_from_user((void *)&ibuf, (void __user *)argument, 
		                     sizeof(vxt_query_controller_struct_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		ret = 
		   vxtcom_lookup_ctrlr((vxt_query_controller_struct_t *)&ibuf);
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return ret;
		}
		ret = copy_to_user((void __user *)argument, (void *)&ibuf, 
		                   sizeof(vxt_query_controller_struct_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		
		break;

	case IOCTL_VXTCTRLR_TRACE_PLAYBACK:
	{
		void *device_buffer;
		UMI_LOG(307, VXTPRINT_BETA_LEVEL, "calling trace playback\n");

		ret = copy_from_user((void *)&ibuf, (void __user *)argument, 
		                     sizeof(vxt_trace_playback_parms_t));
		if (ret) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EFAULT;
		}
		/*
		 * Argument is a device slot.  The 
		 * proper signal hash is taken from the
		 * the device controller fields.  In
		 * this way overhead is diminished on
		 * this important data path call.
		 */
		ret = vxtcom_find_slot(instance, 
		                       ibuf.playback_parms_buf.device_handle,
		                       &slot);
		if (ret != VXT_SUCCESS) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -EINVAL;
		}

		/*
		 * Find parent structure
		 */
		device = vxtcom_slot_find_device(slot);
		/*
		 * Get a temporary buffer
		 */
		device_buffer =
		   vxt_kmalloc(ibuf.playback_parms_buf.entry_count
		               * sizeof(vxt_trace_entry_t), VXT_KMEM_KERNEL);
		if (device_buffer == NULL) {
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -ENOMEM;
		}
		ret = 
		   vxtsig_trace_playback(instance->bus.dev_events,
		                         device->event,
		                         device_buffer,
		                         &ibuf.playback_parms_buf.entry_count);

		if (ret != VXT_SUCCESS) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -ENOMEM;
		}

		ret = copy_to_user((void __user *)
		                   ibuf.playback_parms_buf.buffer, 
		                   device_buffer, 
		                   ibuf.playback_parms_buf.entry_count
		                   * sizeof(vxt_trace_entry_t));

		if (ret != VXT_SUCCESS) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -ENOMEM;
		}

		ret = copy_to_user((void __user *) argument, (void *)&ibuf, 
		                   sizeof(vxt_trace_playback_parms_t));

		if (ret != VXT_SUCCESS) {
			vxt_kfree(device_buffer);
			if (of->controller == NULL) {
				vxtcom_dec_ref(instance);
			} else {
				vxt_munlock(&instance->bus.config_mutex);
			}
			return -ENOMEM;
		}

		vxt_kfree(device_buffer);
		break;
	}



	default:
		/*printk(KERN_ALERT "ioctl %08x not supported by VXT controller\n",
		  command);*/
		if (of->controller == NULL) {
			vxtcom_dec_ref(instance);
		} else {
			vxt_munlock(&instance->bus.config_mutex);
		}
		return -EINVAL; /* same return as native Linux */
	}

	if (instance->pending_send) {
		UMI_LOG(308, VXTPRINT_BETA_LEVEL,"signal config queue\n");
		vxtcfg_signal_config_queue(instance);
		instance->pending_send = 0;
	}

	if (of->controller == NULL) {
		/*
		 * mutex is dropped by vxtcom_dec_ref
		 */
		vxtcom_dec_ref(instance);
		return 0;
	}
	vxt_munlock(&instance->bus.config_mutex);
	return 0;

}


/*
 * 
 * vxtcfg_connect_remote_queue:
 *
 * Request Handler Helper - Parser
 *
 * Maps the shared pages provided into a contiguous range of
 * virtual space.  Returns the address of the range as a 
 * queue.
 *
 * vxtcfg_connect_remote_queue treats the region being connected
 * as a device queue and adds the page lists and queue headers
 * to the device structure sent.
 *
 * Returns:
 *		VXT_SUCCESS
 *		VXT_NOMEM - kmalloc failed or could not alloc
 *		               vm_area
 *		VXT_FAIL  - Could not map shared pages
 *
 */

int
vxtcfg_connect_remote_queue(vxtdev_t *device,
                            vxtcom_cntrl_page_ref_t *page_ref_array, 
                            int page_cnt)
{
	vxtdev_queues_t *q_pages;
	int ret;

	UMI_LOG(201, VXTPRINT_PROFILE_LEVEL,
	        "called, device %p, qpages %p, pgcnt %08x\n",
	        device, page_ref_array, page_cnt);

	ret = vxtcard_connect_mem_queue(page_ref_array, page_cnt,
	                               device->remote_dom, &q_pages);
	if (ret != VXT_SUCCESS) {
		return ret;
	}

	/*
	 * Store queue allocation information on
	 * internal device state structure.
	 */

	q_pages->next = device->dev_queues;
	device->dev_queues = q_pages;
	return VXT_SUCCESS;

}


/*
 *
 * vxtcfg_signal_config_queue:
 *
 * vxtcfg_signal_config_queue cause the interrupt of the remote
 * controller configuration handler.  The signal indicates the
 * presence of either request or reply information on the shared
 * configuration command space.  The signal is also used to 
 * synchronize queue access.  i.e. When a signal is raised the
 * party issuing the signal relinquishes access to the request and/or
 * reply queue until the foreign party sends the complimentary 
 * request/reply.
 * 
 * Returns:
 *		VXT_SUCCESS
 *
 */

int
vxtcfg_signal_config_queue(vxtcom_ctrlr_t *vxtcom)
{
	UMI_LOG(309, VXTPRINT_BETA_LEVEL,"controller %p\n", vxtcom);
	/*
	 * Signal transport authority
	 */
	vxtcom_event_signal(vxtcom->bus.evtchn);

	/*
	 * We will stay busy until replies and 
	 * requests are sent.  However, we
	 * can update the msg_id counter.
	 */
	if (vxtcom->bus.request_offset) {
		/*
		 * We have a request to send
		 * increment the message id
		 * for the next set of requestors
		 * upon completion of this cycle.
		 * i.e. After the remote sends
		 * a reply.
		 */
		vxtcom->bus.outq_header.msg_id++;
	}
	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_open:
 */

static int
vxtcom_open(struct inode *inode, struct file *filep)
{
	vxtcom_ctrlr_t *controller;
	vxtcom_file_control_t *of;   /* open file state */
	int major;

	major = imajor(inode);

	UMI_LOG(203, VXTPRINT_PROFILE_LEVEL, " called, inode %p, filep %p\n",
	        inode, filep);
	if (major == vxtcom_ctrlr_bus_device) {
		UMI_LOG(272, VXTPRINT_DEBUG_LEVEL," bus device passed\n");
		/*
		 * This is the generic bus, it will take
		 * on any active controller for IOCTLs
		 * In addition poll waits will be for controller
	 	 * plug actions, i.e. either new controller
		 * events or controller remove events
		 */
		of = vxt_kmalloc(sizeof(vxtcom_file_control_t),
		                 VXT_KMEM_KERNEL);
		if (of == NULL) {
			return -ENOMEM;
		}

		of->controller = NULL;/* no fixed controller */
		of->device_wait = 0;  /* generic, wait on all types */

		filep->private_data = of;

		return 0;
	}

	vxt_mlock(&active_ctrlr_lock);
	controller = active_controllers;
	while (controller != NULL) {
		if (controller->major == major) {
			vxt_mlock(&controller->bus.config_mutex);

			if (controller->pending_shutdown == 1) {
				UMI_LOG(310, VXTPRINT_BETA_LEVEL,
				        "shutting down\n");
				vxt_munlock(&controller->bus.config_mutex);
				vxt_munlock(&active_ctrlr_lock);
				return -EINVAL;
			}

			vxtcom_inc_ref(controller);
			vxt_munlock(&controller->bus.config_mutex);
			vxt_munlock(&active_ctrlr_lock);

			/* 
			 * Can be expanded to support security
			 * on the open minor number.
			 */
			of = vxt_kmalloc(sizeof(vxtcom_file_control_t),
			                 VXT_KMEM_KERNEL);
			if (of == NULL) {
				UMI_WARN(231, VXTPRINT_PRODUCT_LEVEL,
				         "kmalloc failed\n");
				return -ENOMEM;
			}

			of->controller = controller;
			of->device_wait = 0;  /* generic, wait on all types */
			UMI_LOG(273, VXTPRINT_DEBUG_LEVEL,
			        "found controller %p\n", controller);

			filep->private_data = of;

			return 0;
		}
		controller = controller->next;
	}
	vxt_munlock(&active_ctrlr_lock);
	return -EINVAL;
}


/*
 *
 * vxtcom_close:
 *
 *
 */

static int
vxtcom_close(struct inode *inode, struct file *filep)
{
	vxtcom_file_control_t *of;

	of = filep->private_data;
	filep->private_data = 0;
	UMI_LOG(204, VXTPRINT_PROFILE_LEVEL,
	        " called, file desc %p, file data %p\n",
	        filep, of);
	if (of->controller != NULL) {
		UMI_LOG(311, VXTPRINT_BETA_LEVEL,
		        " controller %p\n", of->controller);
		vxt_mlock(&of->controller->bus.config_mutex);
		/*
		 * mutex is dropped by vxtcom_dec_ref
		 */
		vxtcom_dec_ref(of->controller);
	}
	vxt_kfree(of);
	UMI_LOG(274, VXTPRINT_DEBUG_LEVEL, "returning\n");
	return 0;
}


/*
 *
 * vxtcom_poll:
 *
 */

static unsigned int
vxtcom_poll(struct file *file, poll_table *wait)
{
	vxtcom_file_control_t *of;
	vxtdev_type_t         device_type;
	unsigned int          mask = 0;
        symlibs_t             *library;
	int                   ret;

	/*
	 * This profile log is debug2 because it is on the
	 * data path
	 */
	UMI_LOG(275, VXTPRINT_DEBUG2_LEVEL, " called\n");
	of = (vxtcom_file_control_t *)file->private_data;
	if (of == NULL) {
		UMI_LOG(312, VXTPRINT_BETA_LEVEL, " poll file gone\n");
		return 0;
	}

	/*
	 * If this open file descriptor
	 * is based on the vxt bus
	 * device, there is nothing to
	 * wait on.  
	 *
	 * We may want to expand this in
	 * future to allow waits on
	 * new controller creation.
	 */
	if (of->controller == NULL) {
		/*
		 * Wait on the VXT module event for controller
		 * plug events
		 */
		UMI_LOG(276, VXTPRINT_DEBUG2_LEVEL,
		        " wait on controller plug event\n");
		poll_wait(file, &vxtcom_ctrlr_changed.wq, wait);
		if (vxtcom_ctrlr_event) {
			UMI_LOG(313, VXTPRINT_BETA_LEVEL,
			        " ctrlr plug event bits 0x%x\n",
			        vxtcom_ctrlr_poll_events);
			mask = vxtcom_ctrlr_poll_events;
			vxtcom_ctrlr_poll_events = 0;
			vxtcom_ctrlr_event = 0;
			return mask;
		} else {
			return 0;
		}
	}

	device_type = of->device_wait;

	if (device_type == 0) {
		/*
		 * wait on the controller
		 */
		UMI_LOG(277, VXTPRINT_DEBUG2_LEVEL,
		        "wait on controller, controller %p\n",
		        of->controller);
		poll_wait(file, &of->controller->new_dev_wait.wq, wait);
		if (of->controller->dev_event_list) {
			mask  = of->controller->dev_event_list;
			of->controller->dev_event_list = 0;
		}
	} else if (device_type == VXT_UNIVERSAL) {
		/*
		 * wait across the vxt subsytem for a 
		 * new device, not just on a single controller
		 */
		UMI_LOG(278, VXTPRINT_DEBUG2_LEVEL,
		        "wait subsystem, for a new device\n");
		poll_wait(file, &vxtsys_new_dev_wait.wq, wait);
		if (vxtsys_dev_event_list) {
			mask  = vxtsys_dev_event_list;
			vxtsys_dev_event_list = 0;
		}
	} else {
		ret = vxtcom_find_lib(of->controller->device_libraries, 
		                      device_type, &library);
		if (ret != VXT_SUCCESS) {
			return -EINVAL;
		}
		UMI_LOG(279, VXTPRINT_DEBUG2_LEVEL,
		        "wait on library, device_type %llu\n", device_type);
		poll_wait(file, &library->new_dev_wait.wq, wait);
		if (library->dev_event_list) {
			mask  = library->dev_event_list;
			library->dev_event_list = 0;
		}
	}

	UMI_LOG(280, VXTPRINT_DEBUG2_LEVEL, " return from poll\n");

	return mask;

}

/*
 *
 * vxtcom_mmap:
 *
 * vxtcom_mmap is a front end for the hypervisor specific
 * mapping mechanism.  It is interposed so that a check
 * can be made to make certain the call is being made
 * on the proper subset of device file type.
 *
 * Returns:
 *
 *		0 => SUCCESS
 *		-ENXIO  -
 *			No registered handler for vma
 *			length does not match backing object
 *
 */

int
vxtcom_mmap(struct file *file, struct vm_area_struct *vma)
{

	vxtcom_file_control_t *of;   /* open file state */
	vxt_device_sm_t       *sm;
	vxtarch_word          token;
	vxtdev_t              *device;
	int                   ret;

	UMI_LOG(629, VXTPRINT_PROFILE_LEVEL,
		"called filep %p, vma %p\n",
		file->private_data, vma);
	/*
	 * Check for bus device, don't allow mappings
	 * through generic bus file
	 * May want to expand this for tighter control
	 * in future, not allowing users of wrong ctrlr
	 * file to create mappings.
	 *
	 */
	of = (vxtcom_file_control_t *)file->private_data;

	if (of->controller == NULL) {
		UMI_WARN(630, VXTPRINT_PRODUCT_LEVEL, "no controller\n");
		return -ENXIO;
	}
 
	token = (vxtarch_word)(vma->vm_pgoff);
	ret = vxtcom_dev_lookup_token(token << 12, &sm);
	if (ret != VXT_SUCCESS) {
		UMI_WARN(238, VXTPRINT_PRODUCT_LEVEL,
		         "no token found, vma %p\n", vma);
		return -ENXIO;
        }

	device = (vxtdev_t *)(vxtarch_word)sm->dev_handle;


	/*
	 * Call the hypervisor specific mapping method
	 */
	return vxt_card_mmap(file, vma, device->origin, device->remote_dom);

}


static struct file_operations vxt_ctrlr_fops =
{
	.owner   = THIS_MODULE,
	.poll    = vxtcom_poll,
	.mmap    = vxtcom_mmap, 
/*
	.read    =
	.write   =
*/
	.ioctl   = vxtcom_ioctl,
	.open    = vxtcom_open,
	.release = vxtcom_close,
};

/*
 *
 * vxtcom_os_ctrlr_init:
 *
 * Controller Character file creation/destruction
 * Card level allocation of resources for singal and mapping
 * functions.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon Successful invocation
 *		VXT_FAIL - Unable to  initialize a character device
 *		VXT_RSRC - Returned from vxt_card_rsrc_init on failure
 *
 *
 */

int
vxtcom_os_ctrlr_init(vxtcom_ctrlr_t *instance, uint32_t first_controller)
{
	int                major;
	char               device_name[VXTCOM_MAX_DEV_FILE_NAME];
	int                ret;

	UMI_LOG(213, VXTPRINT_PROFILE_LEVEL,
	        " called, controller %p, first %08x\n",
	        instance, first_controller);

	sprintf(device_name, "vxtctrlr%d", controller_count);
	major = register_chrdev(instance->bus.vdevice, 
	                        device_name, &vxt_ctrlr_fops);

	if (instance->bus.vdevice == 0) {
		instance->bus.vdevice = major;
	}

	vxtcom_strncpy(instance->file_name,
	               device_name, VXTCOM_MAX_DEV_FILE_NAME);
	if (major < 0) {
		UMI_WARN(244, VXTPRINT_PRODUCT_LEVEL,
		         "vxt_com: unable to create a character device\n");
		return (VXT_FAIL);
	}

	/*
         * Use this on subsequent 
	 * vxt_ctlr_fops:open calls
	 * to dereference the correct
	 * controller instance.
	 */
	instance->major = major;

	/*
	 * Call the vxtcard layer to initizlize any controller
	 * instance, hypervisor specific resources.
	 */
	ret = vxtcard_ctrlr_init((void *)&vxt_ctrlr_fops, first_controller,
	                         controller_count, major);

	if (ret != VXT_SUCCESS) {
		UMI_WARN(631, VXTPRINT_PRODUCT_LEVEL,
		         "vxt_com: unable to allocate card resources\n");
		unregister_chrdev(instance->bus.vdevice, instance->file_name);
		return ret;
	}


	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_os_destroy_controller:
 *
 * Remove the file system registration for the targeted
 * controller.
 *
 * Returns:
 *		None
 */

void
vxtcom_os_destroy_controller(vxtcom_ctrlr_t *instance,
                             uint32_t last_controller)
{

	UMI_LOG(214, VXTPRINT_PROFILE_LEVEL,
	        " called, controller %p, last %08x\n",
	        instance, last_controller);

	vxtcard_ctrlr_destroy(instance->bus.vdevice, last_controller);

	unregister_chrdev(instance->bus.vdevice, instance->file_name);


	return;
}


/*
 *
 * vxtcom_os_mod_init:
 *
 * Controller Character file creation/destruction for
 * generic, non instance specific services.  The
 * vxtctrlr bus file type allows system queries
 * routing and discovery of appropriate controller
 * for specific routes.
 *
 *
 */

int
vxtcom_os_mod_init(void)
{
	int                major;

	UMI_LOG(215, VXTPRINT_PROFILE_LEVEL," called\n");

	major = register_chrdev(VXTCOM_CTRLR_BUS_DEVICE, 
	                        "vxtctrlr-bus", &vxt_ctrlr_fops);

	/*
         * Use this on subsequent 
	 * vxt_ctlr_ops:open calls
	 * to recognize the bus device
	 */
	if (VXTCOM_CTRLR_BUS_DEVICE == 0) {
		vxtcom_ctrlr_bus_device = major;
	} else {
		vxtcom_ctrlr_bus_device = VXTCOM_CTRLR_BUS_DEVICE;
	}
	UMI_LOG(319, VXTPRINT_BETA_LEVEL,
	        " vxtcom_ctrlr_bus_device 0x%08u\n",
	        vxtcom_ctrlr_bus_device);

	if (major < 0) {
		UMI_WARN(328, VXTPRINT_PRODUCT_LEVEL,
		         "vxt_com: unable to create a character device\n");
		return (VXT_FAIL);
	}

	vxt_class_device_create(major, 0, "vxtctrlr-bus");

	return 0;
}


/*
 *
 * vxtcom_os_mod_remove:
 *
 * Remove the file system registration for the generic
 * vxtctrlr bus.
 *
 *
 */

void
vxtcom_os_mod_remove(void)
{

	UMI_LOG(216, VXTPRINT_PROFILE_LEVEL," called\n");

	vxt_class_device_destroy(vxtcom_ctrlr_bus_device, 0);

	unregister_chrdev(vxtcom_ctrlr_bus_device,  "vxtctrlr-bus");

	return;
}


/* ************** vxt device file polling support ******************** */

/*
 *
 * vxtcom_os_dev_file_open:
 */

static int
vxtcom_os_dev_file_open(struct inode *inode, struct file *filep)
{
	vxtcom_ctrlr_t *controller;
	vxtcom_dev_control_t *of;   /* open file state */
	vxtdev_t *device;
	int major;
	int minor;

	major = imajor(inode);
	minor = iminor(inode);

	/*
	 * Initialize the private data field in case the
	 * caller hasn't
	 */
	filep->private_data = NULL;

	UMI_LOG(217, VXTPRINT_PROFILE_LEVEL, " called, inode %p, filep %p\n",
	        inode, filep);

	vxt_mlock(&active_ctrlr_lock);
	controller = active_controllers;
	while (controller != NULL) {
		/* 
		 * We pushed the controller major as our minor
		 * device number so that we can identify the
		 * parent controller.
		 */
		if (controller->major == minor) {
			vxt_mlock(&controller->bus.config_mutex);

			if (controller->pending_shutdown == 1) {
				UMI_LOG(320, VXTPRINT_BETA_LEVEL,
				        "shutting down\n");
				vxt_munlock(&controller->bus.config_mutex);
				vxt_munlock(&active_ctrlr_lock);
				return -EINVAL;
			}
			UMI_LOG(321, VXTPRINT_BETA_LEVEL,
			        " found controller %p\n", controller);

			/* 
			 * We have found the parent controller, now see
			 * if we can match the minor number with a resident
			 * device.
			 */
			device = controller->dev_list;
			while (device != NULL) {
				if (device->os_dev == major) {
				   /*
				    * Found our target device
				    * Use the existing of_rec
				    * if there is one present
				    * and just bump the reference
				    * count.
				    */
				    UMI_LOG(322, VXTPRINT_BETA_LEVEL,
			                    " found device %p\n", device);
				   if (device->of_rec) {
				      of = (vxtcom_dev_control_t *)
				           device->of_rec;
				      of->refcnt++;
				   }  else {
				      of = 
				         vxt_kmalloc(
				            sizeof(vxtcom_dev_control_t),
				                   VXT_KMEM_KERNEL);
				      if (of == NULL) {
				         UMI_WARN(245, VXTPRINT_PRODUCT_LEVEL,
				                  "kmalloc failed\n");
				         vxt_munlock(&controller->
				                     bus.config_mutex);
				         vxt_munlock(&active_ctrlr_lock);
				         return -ENOMEM;
				      }
				      of->controller = controller;
				      of->devevt = device->event;
				      of->device = device;
				      of->refcnt = 1;
				      of->os_dev = device->os_dev;
				      vxtcom_strncpy(of->dev_file_name,
				                     device->dev_file_name,
				                     VXTCOM_MAX_DEV_FILE_NAME);
				      device->of_rec = (void *)of;
				      vxtcom_inc_ref(controller);
				   }
				   filep->private_data = of;
				   break;
				}
				device = device->next;
			}
			
			vxt_munlock(&controller->bus.config_mutex);
			vxt_munlock(&active_ctrlr_lock);

			if (device == NULL) {
				/*
				 * Failed to find a live device
				 */
				UMI_WARN(246, VXTPRINT_PRODUCT_LEVEL, "no matching device found: major %08x, minor %08x\n", major, minor);
				return  -EINVAL;
			}


			UMI_LOG(296, VXTPRINT_DEBUG_LEVEL, "found device %p\n",
			        device);

			return 0;
		}
		controller = controller->next;
	}
	vxt_munlock(&active_ctrlr_lock);
	return -EINVAL;
}

/*
 *
 * vxtcom_os_dev_file_close:
 *
 * This is the file descriptor close for the device
 * file.  This file is used to do a poll wait.
 * The of structure is owned by the open/close
 * device file operations.  It is allocated in
 * open and deleted in close.  At most, only one
 * of_rec structure exists per device.  The
 * structure is associated with the device and
 * the device address is pushed into a field
 * within the structure.  If the device is
 * deallocated while there are still extant file
 * descriptors, the of structure will survive
 * the shutdown of the device but the device
 * field will be zero.  In this way, no polling
 * actions can take place after the device is
 * gone.
 *
 */

static int
vxtcom_os_dev_file_close(struct inode *inode, struct file *filep)
{
	vxtcom_dev_control_t *of;
	vxtcom_ctrlr_t *controller;

	of = filep->private_data;
	filep->private_data = 0;
	if (of == NULL) {
		UMI_LOG(323, VXTPRINT_BETA_LEVEL, " poll file gone\n");
		return 0;
	}
	UMI_LOG(218, VXTPRINT_PROFILE_LEVEL,
	        " called, file desc %p, file data %p\n",
	        filep, of);
	/*
	 * controller is guranteed to exist because
	 * we have bumped the refcnt
	 */
	controller = of->controller;
	vxt_mlock(&controller->bus.config_mutex);
	if (of->device != NULL) {
		UMI_LOG(324, VXTPRINT_BETA_LEVEL,
		        " device %p\n", of->device);
		/*
		 * Drop the of_rec count if not 1
		 * if 1, release the of object
		 */
		if (of->refcnt == 1) {
			of->device->of_rec = NULL;
			vxt_kfree(of);
			/*
			 * mutex is dropped by vxtcom_dec_ref
			 */
			vxtcom_dec_ref(controller);
		} else {
			of->refcnt--;
			vxt_munlock(&controller->bus.config_mutex);
		}
	} else {
		/*
		 * Drop the of_rec count if not 1
		 * if 1, release the of object
		 * Note: the controller reference count was dropped
		 * when the device was removed.
		 */
		/*
		 * IMPORTANT:  Remember we do not keep a reference
		 * count on the device for the device file.  There is
		 * no need to keep all of the device structure overhead
		 * Therefore, do not use any field derived from a device
		 * pointer.  The devevt field points to a separate structure
		 * used for device signalling events that we do keep an
		 * elevated count on.
		 */
		if (of->refcnt == 1) {
			vxtsig_dec_ref(of->controller->bus.dev_events,
		                       of->devevt);
			/*
			 * The device is gone, however we must hold
			 * onto the device file until the last
			 * file descriptor is gone, i.e. NOW
			 */
			vxtcom_os_unregister_device_file(of->dev_file_name,
			                                 of->os_dev,
			                                 MINOR(of->controller
			                                       ->bus.vdevice));
			/*
			 * Free the local file structure and
			 * drop the controller reference
			 */
			vxt_kfree(of);
			vxtcom_dec_ref(controller);
		} else {
			of->refcnt--;
			vxt_munlock(&controller->bus.config_mutex);
		}
	}
	return 0;
}


/*
 *
 * vxtcom_os_dev_file_poll:
 *
 */

static unsigned int
vxtcom_os_dev_file_poll(struct file *file, poll_table *wait)
{
	vxtcom_dev_control_t *of;
	int                   revents;

	/*
	 * This profile log is debug2 because it is on the
	 * data path
	 */
	UMI_LOG(281, VXTPRINT_DEBUG2_LEVEL, " called\n");
	of = (vxtcom_dev_control_t *)file->private_data;

	if (of == NULL) {
		UMI_LOG(325, VXTPRINT_BETA_LEVEL, " poll file gone\n");
		return 0;
	}

	vxt_mlock(&of->controller->bus.config_mutex);
	/*
	 * Can't check the device field until the controller
	 * is locked.
	 */
	if (of->device == NULL) {
		/*
		 * The device is gone, nothing to wait on.
		 */
		vxt_munlock(&of->controller->bus.config_mutex);
		/*
		 * Return to user, could just return NOHUP
		 */
		return 0xff;
	}

	vxtsig_poll(of->controller, of->controller->bus.dev_events,
                    of->device->event, (void *)file, (void *)wait, &revents);

	vxt_munlock(&of->controller->bus.config_mutex);


	UMI_LOG(282, VXTPRINT_DEBUG2_LEVEL,
	        " return from poll, revents 0x%x\n", revents);

	return revents;

}


static struct file_operations vxt_dev_fops =
{
	.owner   = THIS_MODULE,
	.open    =  vxtcom_os_dev_file_open,
	.release =  vxtcom_os_dev_file_close,
	.poll    =  vxtcom_os_dev_file_poll
};


/*
 *
 * vxtcom_os_register_device_file:
 *
 * vxtcom_os_register_device_file creates a device file presence
 * for user level clients.  The Major number is derived from the
 * controller the targeted device sits on. This promotes faster
 * lookup of the proper signal on device open.
 *
 */

int
vxtcom_os_register_device_file(char *new_dev_name, 
                               vxtdev_t *device, int device_number,
                               uint64_t *device_handle)
{
	vxtcom_ctrlr_t     *vxt_ctrlr;
	int                file_number;
	int                major;
	int                minor;
	int                ret;

	vxt_ctrlr = device->controller;
	if (vxt_ctrlr == NULL) {
		return VXT_FAIL;
	}
	/*
	 * Increment the count on the assoicated signal structure
	 * This way it will persist if we have an extant waiters
	 * at the time of device shutdown.
	 */
	ret = vxtsig_inc_ref(vxt_ctrlr->bus.dev_events, device->event);
	if (ret != VXT_SUCCESS) {
		return VXT_FAIL;
	}

	major = MAJOR(vxt_ctrlr->bus.vdevice);
	minor = MINOR(vxt_ctrlr->bus.vdevice);
	UMI_LOG(297, VXTPRINT_DEBUG_LEVEL, 
	        "ctrlr # 0x%x, file major 0x%x, file minor 0x%x\n",
	        vxt_ctrlr->bus.vdevice, major, minor);
	file_number = register_chrdev(MKDEV(major,0),
	                              new_dev_name, &vxt_dev_fops);
        if (file_number < 0) {
                UMI_WARN(247, VXTPRINT_PRODUCT_LEVEL,
                         "unable to create a character device\n");
		vxtsig_dec_ref(vxt_ctrlr->bus.dev_events, device->event);
                return (VXT_FAIL);
        }
	device->os_dev = (uint64_t) file_number;
	UMI_LOG(326, VXTPRINT_BETA_LEVEL,"os_dev 0x%llx, file %s\n", 
	        device->os_dev, new_dev_name);

	vxt_class_device_create(file_number, minor, new_dev_name);

	*device_handle = 0;
	VXT_DEV_HANDLE_PUT_PRIMARY(file_number, *device_handle);
	VXT_DEV_HANDLE_PUT_SECONDARY(minor, *device_handle);

	return VXT_SUCCESS;

}


void
vxtcom_os_unregister_device_file(char *dev_file_name, uint64_t os_dev,
                                 int minor)
{

	if ((strlen(dev_file_name)) && (os_dev != 0)) {
		UMI_LOG(219, VXTPRINT_PROFILE_LEVEL,
	       	        " called, device %s, file #0x%llx, minor 0x%x\n",
		        dev_file_name, os_dev, minor);

		unregister_chrdev(os_dev, dev_file_name);

		vxt_class_device_destroy(os_dev, minor);
		
	}

	return;
}


/*
 *
 * vxtcom_os_disable_device_file:
 *
 * Under the parent controller lock
 *
 * Disables the calls to poll by any remaining clients of the 
 * device file.
 *
 */

void
vxtcom_os_disable_device_file(vxtdev_t *device)
{
	vxtcom_dev_control_t *of_rec;
	

	of_rec = (vxtcom_dev_control_t *)device->of_rec;

	if (of_rec != NULL) {
		if (of_rec->device != NULL) {
			of_rec->device = NULL;
		}
	} else {
		/*
		 * It is possible for vxtcom_os_disable_device_file to be
		 * called before the vxtcom_os_register_device_file invocation
		 * This is because we must wait for remote device add on
		 * the guest side.  Because of this we predicate vxtsig_dec_ref
		 * on the os_dev field.
		 */
		if (device->os_dev != 0) {
			vxtsig_dec_ref(device->controller->bus.dev_events,
		        	       device->event);
			/*
			 * If our device asked for a device file, it
			 * is time to remove it.  There are no
			 * open file descriptors to worry about.
			 */
			vxtcom_os_unregister_device_file(
			         device->dev_file_name,
			         device->os_dev,
		                 MINOR(device->controller->bus.vdevice));
		}
	}
	
}


/*
 *
 * vxt_poll_wait:
 *
 * vxt_poll_wait is a vxt wrapper for the poll function to 
 * shield the os independent code from the os specific 
 * poll implementation. 
 *
 * It is expected that the poll function will not block
 *
 */

void
vxt_poll_wait(void *file_handle, 
              vxtcom_wait_queue_t *wait_queue, void *wait_handle,
              vxt_slock_t *lock, vxt_sflags_t *lock_flags)
{
	struct file *file = (struct file *)file_handle;
	poll_table  *wait = (poll_table *)wait_handle;

#ifndef LEGACY_4X_LINUX
	poll_wait(file, (wait_queue_head_t *)wait_queue, wait);
#else
	vxt_sunlock(lock, *lock_flags);
	poll_wait(file, (wait_queue_head_t *)wait_queue, wait);
	vxt_slock(lock, lock_flags);
#endif
}
