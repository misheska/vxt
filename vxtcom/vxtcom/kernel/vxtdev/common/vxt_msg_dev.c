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

#define _VXT_COMPONENT_ 2
#define _VXT_SUBSYSTEM_ 1



#include <public/kernel/os/vxtcom_embedded_os.h>
#include <public/vxt_system.h>
#include <public/symcom_dev_table.h>
#include <public/kernel/vxtcom_devlib.h>
#include <public/vxt_msg_dev.h>




/*
 * vxt_msg_dev.c
 * This module implements the Symantec message device.
 * This device is made up of two shared memory queues and a
 * a signalling event.  vxt_msg_dev.c connects directly to
 * the vxt_com module and through callbacks provides symantec
 * communications devices which sit on the symcom bus.
 */


typedef struct vxtmsg_remove_info {
	uint32_t sendq_len;
	uint32_t rcvq_len;
} vxtmsg_remove_info_t;


typedef struct vxtmsg_device {
	int                  state;
	void                 *sendq;
	void                 *sendq_handle;
	vxtarch_word         sendq_name;
	char                 file_name[VXTCOM_MAX_DEV_FILE_NAME];
	uint64_t             file_handle;
	uint32_t             sendq_len;
	uint32_t             sendq_bufcnt;
	void                 *rcvq;
	void                 *rcvq_handle;
	vxtarch_word         rcvq_name;
	uint32_t             rcvq_len;
	uint32_t             rcvq_bufcnt;
	vxtcom_ctrlr_api_t   *callbacks;
	long                 waiting;
	int                  error;
	char                 send_token[1];
	char                 rcv_token[1];
	uint64_t             domain;
	int                  event_port;
	int                  slot;
	vxtcom_wait_queue_t  wq;
	struct vxtmsg_device *next;
} vxtmsg_device_t;


static vxtmsg_device_t *live_devs = NULL;

/*
 * vxtmsg_find_token_parent:
 *
 * vxtmsg_find_token_parent is part of a system of token
 * dereferencing to find a message device and the targeted
 * queue.  The  client is the VXT controller and is therefore
 * trusted.  We use the address and offset into a message
 * device as a token, guaranteeing uniqueness.  At the
 * particular offset associated with the token, we store
 * an r or an s to differentiate the two queues.  The
 * system is fast and compact.
 *
 * Returns:
 *
 *		VXT_SUCCESS upon successful dereference
 *		VXT_PARM the token doesn't point to an "r"
 *		or an "s"
 * 
 */

static int
vxtmsg_find_token_parent(void *token_handle, vxtmsg_device_t **dev)
{
	char            *token = (char *)token_handle;
	vxtmsg_device_t *msg_dev = 0;

	UMI_LOG(1, VXTPRINT_PROFILE_LEVEL, " called, token %p\n", token_handle);

	if (token[0] == 's') {
		*dev = (vxtmsg_device_t *)
		   (((vxtarch_word)token)
		    - (vxtarch_word)&(msg_dev->send_token));
		return VXT_SUCCESS;
	} else if (token[0] == 'r') {
		*dev = (vxtmsg_device_t *)
		   (((vxtarch_word)token)
		    - (vxtarch_word)&(msg_dev->rcv_token));
		return VXT_SUCCESS;
	} 
	return VXT_PARM;
}


/*
 *
 * vxtmsg_insert_dev:
 *
 */

static int
vxtmsg_insert_dev(vxtmsg_device_t *msg_dev)
{
	UMI_LOG(2, VXTPRINT_PROFILE_LEVEL, " called, device %p\n", msg_dev);

	msg_dev->next = live_devs;
	live_devs = msg_dev;
	return VXT_SUCCESS;
}


/*
 *
 * vxtmsg_delete_dev:
 *
 */

static int
vxtmsg_delete_dev(vxtmsg_device_t *msg_dev)
{
	vxtmsg_device_t *traveler;

	UMI_LOG(3, VXTPRINT_PROFILE_LEVEL, " called, device %p\n", msg_dev);

	traveler = live_devs;

	if (traveler == NULL) {
		return VXT_PARM;
	} else if (traveler == msg_dev) {
		live_devs = live_devs->next;
	}

	while (traveler->next != NULL) {
		if (traveler->next == msg_dev) {
			traveler->next = traveler->next->next;
			UMI_LOG(20, VXTPRINT_DEBUG_LEVEL,
			        " device found, %p\n", msg_dev);
			return VXT_SUCCESS;
		}
		traveler = traveler->next;
	}

	UMI_WARN(25, VXTPRINT_BETA_LEVEL, " device not found, %p\n", msg_dev);
	return VXT_PARM;
}


/*
 *
 * vxtmsg_dev_remove:
 *
 * vxtmsg_dev_remove is a callback that is invoked when the
 * controller determines that a device must be unplugged.
 *
 * The unplugging may be at the behest of a remote party,
 * an ioctl from the task level client, or a controller
 * unplug event.  In the last case, we will not have
 * device specific remove_info and must proceed without
 * it.  Therefore a check for NULL must be done before
 * the remove_info pointer is dereferenced.
 *
 * vxtmsg_dev_remove removes the two queues associated
 * with a message device and frees the local control
 * structure.
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS - Successful completion
 * 
 */

int
static vxtmsg_dev_remove(void *vxtcom, 
                         void *remove_info,
                         uint32_t info_len,
			 void *instance)
{
	vxtmsg_device_t *msg_dev = (vxtmsg_device_t *)instance;
	vxtmsg_device_t *traveler;
	int ret;

	UMI_LOG(4, VXTPRINT_PROFILE_LEVEL,
	        " called, controller_handle %p, device %p\n",
	        vxtcom, msg_dev);
	traveler = live_devs;
	while (traveler != NULL) {
		if (traveler == msg_dev) {
			break;
		}
		traveler = traveler->next;
	}
	if (traveler == NULL) {
		UMI_LOG(12, VXTPRINT_PRODUCT_LEVEL,
		        "Caller passed a bad instance handle\n");
		return VXT_PARM;
	}
	/*
	 * We don't use the remove_info here but we must
 	 * be prepared to remove the device even if the
	 * remove info is NULL.  This happens on a not so
	 * graceful exit.  The device is shutdown by the
	 * controller unplug.
 	 *
	 * If we did use the remove_info field, we would
	 * need to test the info length first.  The size
	 * is derived from a user supplied field and is
	 * used to allocate the info buffer we will use
	 * if the field is too small we must not use the
	 * buffer.
	 */


	ret = msg_dev->callbacks->cancel_sm(vxtcom, (vxtarch_word)
	                                    &(msg_dev->send_token));
	if (ret != VXT_SUCCESS) {
		UMI_WARN(13, VXTPRINT_PRODUCT_LEVEL,
		         "cannot cancel send token\n");
	}

	ret = msg_dev->callbacks->cancel_sm(vxtcom, (vxtarch_word)
	                                    &(msg_dev->rcv_token));
	if (ret != VXT_SUCCESS) {
		UMI_WARN(14, VXTPRINT_PRODUCT_LEVEL,
		         "cannot cancel receive token\n");
	}


	ret = msg_dev->callbacks->rem_queue(vxtcom, msg_dev->sendq_handle);
	if (ret != VXT_SUCCESS) {
		/*
		 * Possible memory leak, possible hypervisor
		 * shared page structure trouble.  We
		 * can't do anything about it here, warn
		 * the user and continue.
		 */
		UMI_WARN(15, VXTPRINT_PRODUCT_LEVEL,
		         "cannot remove send queue\n");
	}
	ret = msg_dev->callbacks->rem_queue(vxtcom, msg_dev->rcvq_handle);
	if (ret != VXT_SUCCESS) {
		/*
		 * Possible memory leak, possible hypervisor
		 * shared page structure trouble.  We
		 * can't do anything about it here, warn
		 * the user and continue.
		 */
		UMI_WARN(16, VXTPRINT_PRODUCT_LEVEL,
		         "cannot remove receive queue\n");
	}
	vxtmsg_delete_dev(msg_dev);
	vxt_kfree(msg_dev);


	return VXT_SUCCESS;
	
}


/*
 *
 * vxtmsg_dev_query:
 *
 * vxtmsg_dev_query returns device specific information
 * needed by the caller to connect to this instance of
 * a vxtmsg IPC device.  The fields include a signalling
 * event and tokens for the memory mapping of the send
 * and receive queues.
 *
 * Returns:
 *
 *		VXT_SUCCESS
 *
 *
 */

int
static vxtmsg_dev_query(void *instance, void *ibuf, uint32_t buf_size)
{
	vxtmsg_device_t *msg_dev = (vxtmsg_device_t *)instance;
	vxt_msg_dev_info_t *info = (vxt_msg_dev_info_t *)ibuf;

	UMI_LOG(5, VXTPRINT_PROFILE_LEVEL,
	        " called, controller_handle %p\n", instance);

	/*
	 * Important: check the size of the incoming buffer
	 * This is the caller's notion of size and reflects the
	 * size of the buffer passed.
	 */

	if (buf_size < sizeof(vxt_msg_dev_info_t)) {
		UMI_WARN(98, VXTPRINT_PRODUCT_LEVEL,
		         " caller has provided an inadequate buffer size"
		         " for device data.  Provided: %d, Needed: %d\n",
		         buf_size, (uint32_t)sizeof(vxt_msg_dev_info_t));
		return VXT_FAIL;
	}

	info->version = VXT_MSG_DEV_VERSION;
	info->vxt_bus_id = msg_dev->slot;
	info->sendq = (void *)msg_dev->sendq_name;
	vxtcom_strncpy(info->dev_name,
	               msg_dev->file_name, VXTCOM_MAX_DEV_FILE_NAME);
	info->dev_handle = msg_dev->file_handle;
	info->sendq_size = msg_dev->sendq_len; 
	info->rcvq = (void *)msg_dev->rcvq_name;
	info->rcvq_size =  msg_dev->rcvq_len;
	info->rcvq_bufcnt = msg_dev->rcvq_bufcnt;
	info->sendq_bufcnt = msg_dev->sendq_bufcnt;
	info->evt_chn = msg_dev->event_port;
	info->remote_dom = msg_dev->domain;
	info->state = msg_dev->state;

	return VXT_SUCCESS;
}


/*
 *
 * vxtmsg_dev_create:
 *
 * Sets up the device specific data structure, allocates the
 * queues necessary for this device type,  registeres the queues
 * for driver file I/O dereferencing.
 *
 * vxtmsg_dev_create waits for the reply on the device connect call
 * it makes.  If the connect fails the device is broken down and
 * an error is returned.  
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS - Successful completion
 *		VXT_NOMEM - Failed memory allocation
 *		VXT_FAIL - Request to connect the remote 
 *                         endpoint failed.
 * 
 */


int
static vxtmsg_dev_create(void     *vxtcom, 
                         void     *create_info,
                         uint32_t info_length,
                         vxtcom_ctrlr_api_t *callbacks,
                         void     *sync_response,
                         void     **instance)
{
	vxtmsg_device_t *msg_dev;
	vxt_msg_create_info_t *parms = (vxt_msg_create_info_t *)create_info;
	int ret;

	UMI_LOG(6, VXTPRINT_PROFILE_LEVEL,
	        " called, controller_handle %p\n", vxtcom);

	/*
	 * Important: check the size of the incoming buffer
	 * This is the caller's notion of size and reflects the
	 * size of the buffer passed.
	 */

	if (info_length < sizeof(vxt_msg_create_info_t)) {
		UMI_WARN(99, VXTPRINT_PRODUCT_LEVEL,
		         " caller has provided an inadequate buffer size"
		         " for device data.  Provided: %d, Needed: %d\n",
		         info_length, (uint32_t)sizeof(vxt_msg_create_info_t));
		return VXT_FAIL;
	}
	msg_dev = vxt_kmalloc(sizeof(vxtmsg_device_t), VXT_KMEM_KERNEL);
	if (msg_dev == NULL) {
		callbacks->sync_status(sync_response, VXT_NOMEM);
		UMI_WARN(26, VXTPRINT_BETA_LEVEL,
			 "unable to allocate memory\n");
		return VXT_NOMEM;
	}

	msg_dev->callbacks = callbacks;
	msg_dev->sendq_len = parms->sendq_len;
	msg_dev->rcvq_len = parms->rcvq_len;
	msg_dev->sendq_bufcnt = parms->sendq_bufcnt;
	msg_dev->rcvq_bufcnt = parms->rcvq_bufcnt;
	callbacks->vxt_init_wait(&msg_dev->wq);
	msg_dev->error = VXT_FAIL;
	msg_dev->state = VXT_MSG_DEV_DISCONNECTED;

	ret = callbacks->add_queue(vxtcom, parms->sendq_len,
	                           &(msg_dev->sendq),
	                           &(msg_dev->sendq_handle));
	if (ret != VXT_SUCCESS) {
		vxt_kfree(msg_dev);
		callbacks->sync_status(sync_response, ret);
		UMI_WARN(27, VXTPRINT_BETA_LEVEL,
			 "unable to allocate queue\n");
		return ret;
	}
	/*
	 * Seed the new queue with a string for a test of
	 * connectiion, (strictly a sanity check)
	 */
	strcpy(msg_dev->sendq, "Falls mainly in the plain");
	ret = callbacks->add_queue(vxtcom, parms->rcvq_len,
	                           &(msg_dev->rcvq),
	                           &(msg_dev->rcvq_handle));
	if (ret != VXT_SUCCESS) {
		callbacks->rem_queue(vxtcom, msg_dev->sendq_handle);
		vxt_kfree(msg_dev);
		callbacks->sync_status(sync_response, ret);
		UMI_WARN(28, VXTPRINT_BETA_LEVEL,
			 "unable to allocate queue\n");
		return ret;
	}

	/*
	 * Seed the new queue with a string for a test of
	 * connectiion, (strictly a sanity check)
	 */
	strcpy(msg_dev->rcvq, "The rain in Spain");

	/*
	 * append our new device to the live device
	 * list
	 */
	vxtmsg_insert_dev(msg_dev);

	/*
	 * Ask controller to connect resources with remote
	 * set the waiting flag in the device, this
	 * will be reset on the reply.
	 *
	 * This is the async path for message create
	 * past this point there is no need to call
	 * sync status.
	 */
	msg_dev->waiting = 0;
	ret = callbacks->connect_remote(vxtcom,
	                                sync_response,
	                                (void *)parms, 
	                                sizeof(vxt_msg_create_info_t),
	                                (void *)msg_dev);

	if (ret != VXT_SUCCESS) {
		msg_dev->waiting = 1;
		callbacks->rem_queue(vxtcom, msg_dev->sendq_handle);
		callbacks->rem_queue(vxtcom, msg_dev->rcvq_handle);
		vxtmsg_delete_dev(msg_dev);
		vxt_kfree(msg_dev);
		UMI_LOG(29, VXTPRINT_BETA_LEVEL,
		        "unable to connect remote device\n");
		return ret;
	}

	/*
	 * We can pass whatever we like as the token
	 * However it must be unique across all controller
	 * device tokens.  We therefore should choose an
	 * address.  We choose the msg_dev address plus
	 * the offset of the target queue token field.
	 */

	msg_dev->send_token[0] = 's';
	msg_dev->rcv_token[0] = 'r';
	{
		vxtmsg_device_t *parent_dev;
		/*
		 * Test the correctness of the parent lookup
		 * code.  Note: this goes away when we have
		 * a dereference use to keep the code from
		 * getting stale.
		 */
		parent_dev = NULL;
		vxtmsg_find_token_parent((void *)&(msg_dev->rcv_token[0]), 
		                         &parent_dev);
		ASSERT(parent_dev == msg_dev);
	}
	/*
	 * Note: The memory mapping field constraint in linux mmap
	 * obliterates the lower 12 bits of any token.  For this
	 * reason we take a "name" from the registration service
	 * The "name" is the thing that is passed back to the 
	 * caller.  In this way we can keep our local token for
	 * communication back and forth with vxt controller and
	 * the mapping guest can use the disjoint label with the
	 * appropriate naming space.
	 */
	ret = callbacks->reg_sm(vxtcom, msg_dev->sendq, msg_dev->sendq_len,
	                        msg_dev->sendq_handle,
				(vxtarch_word)&(msg_dev->send_token), 
	                        &msg_dev->sendq_name);
	ASSERT(ret == VXT_SUCCESS);
	ret = callbacks->reg_sm(vxtcom, msg_dev->rcvq, msg_dev->rcvq_len,
	                        msg_dev->rcvq_handle,
				(vxtarch_word)&(msg_dev->rcv_token), 
	                        &msg_dev->rcvq_name);
	ASSERT(ret == VXT_SUCCESS);

	/*
	 * Wait for reply from the security authority/remote controller
	 */
	if (sync_response == NULL) {
		/*
		 * If this is a proxy based device
		 * creation attempt, do not wait
		 * we are in the interrupt handler
		 * finish creating the device on this side
		 * let the complete routine do the tear-down
		 * if we fail
		 */
		UMI_LOG(30, VXTPRINT_BETA_LEVEL,"local call, going to sleep\n");
		ret = callbacks->wait(vxtcom, &msg_dev->wq, &msg_dev->waiting);

		UMI_LOG(31, VXTPRINT_BETA_LEVEL,"local call, waking up\n");
		if ((ret != VXT_SUCCESS) || (msg_dev->error != VXT_SUCCESS)) {
			vxtmsg_remove_info_t rem_info;

			UMI_WARN(17, VXTPRINT_PRODUCT_LEVEL,
			         "device creation failed, return = 0x%x\n",
			          msg_dev->error);
			rem_info.sendq_len = parms->sendq_len;
			rem_info.rcvq_len = parms->rcvq_len;
			vxtmsg_dev_remove(vxtcom, 
			                  (void *) &rem_info,
			                  sizeof(rem_info),
			                  (void *) msg_dev);
			return VXT_FAIL;
		}
	} else {
		msg_dev->waiting = 1;
		msg_dev->error = VXT_SUCCESS;
	}

	if (msg_dev->error != VXT_SUCCESS) {
		vxtmsg_remove_info_t rem_info;

		UMI_WARN(18, VXTPRINT_PRODUCT_LEVEL,
		         "device creation failed, return = 0x%x\n",
		         msg_dev->error);
		rem_info.sendq_len = parms->sendq_len;
		rem_info.rcvq_len = parms->rcvq_len;
		vxtmsg_dev_remove(vxtcom, 
		                  (void *) &rem_info,
			          sizeof(rem_info),
		                  (void *) msg_dev);
		return VXT_FAIL;
	}

	/*
	 * This device requests a device file presence.
	 * Generally un-necessary, the device file is used
	 * for polling on data/space available signals
	 * This is method is generally discouraged because 
	 * of its poor performance. The client is encouraged
	 * to use the ioctl wait mechanism
	 */

	msg_dev->file_name[0] = 0;
	callbacks->vxtcom_dev_register_file(vxtcom, 
	                                    "vxtmsg", msg_dev->file_name,
	                                    &msg_dev->file_handle);
	UMI_LOG(32, VXTPRINT_BETA_LEVEL,
	        "device file name %s\n", msg_dev->file_name);

	/*
	 * return the address of our internal message structure
	 * as an instance handel to the controller.
	 */

	*instance = (void *)msg_dev;

	return VXT_SUCCESS;
	
}


/*
 *
 * vxtmsg_dev_create_complete:
 *
 * vxtmsg_dev_create_complete is called on the reply of a device 
 * connection request to the remote controller.  
 *
 * vxtmsg_dev_create_complete is passed the return code showing
 * the outcome of the request.  The outcome is pushed into the
 * device instance state for the targeted device and the waiting
 * request code is awakened.
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS - Successful completion
 * 
 */

int
static vxtmsg_dev_create_complete(void *vxtcom, void *instance,
                                  int event_port, int slot,
                                  uint64_t domain, int error)
{
	vxtmsg_device_t *msg_dev = (vxtmsg_device_t *)instance;

	UMI_LOG(7, VXTPRINT_PROFILE_LEVEL,
	        " called, ctrlr_handle %p, dev %p, sig %08x, err %08x\n",
	        vxtcom, instance, event_port, error);
	/*
	 * If we failed, reflect it in the error field
	 */
	msg_dev->error = error;
	msg_dev->domain = domain;
	msg_dev->event_port = event_port;
	msg_dev->slot = slot;
	/*
	 * If we are not waiting, we will have to
	 * handle device clean-up in the case of
	 * an error from the other side.
	 */
	if (msg_dev->waiting && msg_dev->error) {
		/*
		 * Device clean-up
		 */
		vxtmsg_remove_info_t rem_info;

		UMI_WARN(19, VXTPRINT_PRODUCT_LEVEL,
		         "create failed, ctrlr_hdl %p, dev %p, ret = 0x%x\n",
		          vxtcom, msg_dev, msg_dev->error);
		rem_info.sendq_len = msg_dev->sendq_len;
		rem_info.rcvq_len = msg_dev->rcvq_len;
		vxtmsg_dev_remove(vxtcom, 
		                  (void *) &rem_info,
			          sizeof(rem_info),
		                  (void *) msg_dev);
	}
	msg_dev->waiting = 1;
	msg_dev->callbacks->wakeup(&msg_dev->wq);
	return VXT_SUCCESS;
}


/*
 * 
 * vxtmsg_dev_connect:
 *
 * vxtmsg_dev_connect is called when there is a remote
 * device on another controller that has targeted 
 * this device library.  The remote device has already
 * allocated resources and made them available for
 * sharing.  We will connect to these resources as
 * opposed to creating them as with vxtsg_dev_create
 *
 * Called by controller, vxtmsg_dev_connect creates a local
 * device structure to manage device state and the proceeds
 * to connect to the device queues of the device targeted
 * by the caller.
 *
 * Once the device queues are found, they are advertised
 * to the paging service in the controller for mapping
 * into task space users of the targeted device.
 *
 *	Returns:
 *		VXT_SUCCESS
 *		VXT_NOMEM  - Failed malloc
 *
 */

int
static vxtmsg_dev_connect(void *vxtcom, void *create_info,
                          uint32_t dev_info_len, uint32_t qcount,
                          vxtcom_ctrlr_api_t *callbacks,
                          void **instance)
{
	vxtmsg_device_t *msg_dev;
	vxt_msg_create_info_t *parms = (vxt_msg_create_info_t *)create_info;
	int ret;

	UMI_LOG(8, VXTPRINT_PROFILE_LEVEL,
	        " called, controller handle %p\n", vxtcom);


	/*
	 * Important: check the size of the incoming buffer
	 * This is the caller's notion of size and reflects the
	 * size of the buffer passed.
	 */

	if (dev_info_len < sizeof(vxt_msg_create_info_t)) {
		UMI_WARN(100, VXTPRINT_PRODUCT_LEVEL,
		         " caller has provided an inadequate buffer size"
		         " for device data.  Provided: %d, Needed: %d\n",
		         dev_info_len, (uint32_t)sizeof(vxt_msg_create_info_t));
		return VXT_PARM;
	}


	msg_dev = vxt_kmalloc(sizeof(vxtmsg_device_t), VXT_KMEM_KERNEL);
	if (msg_dev == NULL) {
		return VXT_NOMEM;
	}

	UMI_LOG(33, VXTPRINT_BETA_LEVEL,
	        "parms sendq_len 0x%x, rcvq_len 0x%x\n",
	        parms->sendq_len, parms->rcvq_len);

	msg_dev->callbacks = callbacks;
	msg_dev->sendq_len = parms->sendq_len;
	msg_dev->rcvq_len = parms->rcvq_len;
	msg_dev->sendq_bufcnt = parms->sendq_bufcnt;
	msg_dev->rcvq_bufcnt = parms->rcvq_bufcnt;

	msg_dev->error = VXT_SUCCESS;
	msg_dev->state = VXT_MSG_DEV_DISCONNECTED;

	/*
	 * Note: the queues are placed in reverse order
	 * on the device from the way that they are
	 * found on the remote endpoint and the
	 * way they are presented in the connect
	 * controller request.  However, this is
	 * exactly the way we wish to find them here
	 * as the send queue on one end is the receive
	 * queue on the other.  Nice that it worked
	 * out that way, but too clever by half to
	 * code without a comment.
	 */
	ret = callbacks->find_queue(vxtcom, 1,
	                            &(msg_dev->sendq),
	                            &(msg_dev->sendq_handle));
	if (ret != VXT_SUCCESS) {
		vxt_kfree(msg_dev);
		return ret;
	}

	/*
	 * Note: the sanity test data is overwritten during normal
	 * running by the user level device initialization.  To
	 * test this user level init must be halted or delayed.
 	 * ... Or take your chances
	 */
#ifdef TESTONLY
	UMI_LOG(21, VXTPRINT_DEBUG_LEVEL,
	        "shared memory sanity test, THE RAIN ...\n");

	UMI_LOG(22, VXTPRINT_DEBUG_LEVEL,"%s\n", (char *)msg_dev->sendq);
#endif

	ret = callbacks->find_queue(vxtcom, 0,
	                            &(msg_dev->rcvq),
	                            &(msg_dev->rcvq_handle));
	if (ret != VXT_SUCCESS) {
		vxt_kfree(msg_dev);
		return ret;
	}

#ifdef TESTONLY
	UMI_LOG(23, VXTPRINT_DEBUG_LEVEL,
	        "shared memory sanity test, FALLS MAINLY ...\n");

	UMI_LOG(24, VXTPRINT_DEBUG_LEVEL,"%s\n", (char *)msg_dev->rcvq);
#endif


	/*
	 * append our new device to the live device
	 * list
	 */
	vxtmsg_insert_dev(msg_dev);


	/*
	 * We can pass whatever we like as the token
	 * However it must be unique across all controller
	 * device tokens.  We therefore should choose an
	 * address.  We choose the msg_dev address plus
	 * the offset of the target queue token field.
	 */

	msg_dev->send_token[0] = 's';
	msg_dev->rcv_token[0] = 'r';
	/*
	 * Note: The memory mapping field constraint in linux mmap
	 * obliterates the lower 12 bits of any token.  For this
	 * reason we take a "name" from the registration service
	 * The "name" is the thing that is passed back to the 
	 * caller.  In this way we can keep our local token for
	 * communication back and forth with vxt controller and
	 * the mapping guest can use the disjoint label with the
	 * appropriate naming space.
	 */
	ret = callbacks->reg_sm(vxtcom, msg_dev->sendq, msg_dev->sendq_len,
	                        msg_dev->sendq_handle,
				(vxtarch_word)&(msg_dev->send_token),  
	                        &msg_dev->sendq_name);
	ASSERT(ret == VXT_SUCCESS);
	ret = callbacks->reg_sm(vxtcom, msg_dev->rcvq, msg_dev->rcvq_len,
	                        msg_dev->rcvq_handle,
				(vxtarch_word)&(msg_dev->rcv_token), 
	                        &msg_dev->rcvq_name);
	ASSERT(ret == VXT_SUCCESS);

	/*
	 * This device requests a device file presence.
	 * Generally un-necessary, the device file is used
	 * for polling on data/space available signals
	 * This is method is generally discouraged because 
	 * of its poor performance. The client is encouraged
	 * to use the ioctl wait mechanism
	 */

	msg_dev->file_name[0] = 0;
	callbacks->vxtcom_dev_register_file(vxtcom, 
	                                    "vxtmsg", msg_dev->file_name,
	                                    &msg_dev->file_handle);
	UMI_LOG(34, VXTPRINT_BETA_LEVEL,
	        "device file name %s\n", msg_dev->file_name);


	/*
	 * return the address of our internal message structure
	 * as an instance handel to the controller.
	 */

	*instance = (void *)msg_dev;

	return VXT_SUCCESS;
	
}


/*
 * vxtmsg_dev_attach:
 *
 * vxtmsg_dev_attach is called when a local client wishes to 
 * access the resources associated with a device.  The 
 * vxtmsg device does not support multiple simultaneous
 * clients on the same connection.  Therefore, this call
 * returns an error in the attach_info structure if the
 * device is already attached.  Likewise an error is 
 * returned if the device does not exist.  If the device
 * does exist and is in the unattached state, we
 * set the state to attached and return.
 *
 * Returns:
 *
 *		VXT_SUCCESS - call succeeds even when attach 
 *		              does not.
 *
 */

int
static vxtmsg_dev_attach(void *instance, void *parms, uint32_t info_length)
{
	vxtmsg_device_t *msg_dev = (vxtmsg_device_t *)instance;
	vxt_msg_attach_info_t *attach_info = (vxt_msg_attach_info_t *)parms;

	UMI_LOG(9, VXTPRINT_PROFILE_LEVEL,
	        " called, device %p\n", msg_dev);

	/*
	 * Important: check the size of the incoming buffer
	 * This is the caller's notion of size and reflects the
	 * size of the buffer passed.
	 */

	if (info_length < sizeof(vxt_msg_attach_info_t)) {
		UMI_WARN(101, VXTPRINT_PRODUCT_LEVEL,
		         " caller has provided an inadequate buffer size"
		         " for device data.  Provided: %d, Needed: %d\n",
		         info_length, (uint32_t)sizeof(vxt_msg_attach_info_t));
		return VXT_FAIL;
	}

	if (msg_dev->state == VXT_MSG_DEV_DISCONNECTED) {
		attach_info->status =  VXT_MSG_ATTACH_SUCCESS;
		msg_dev->state = VXT_MSG_DEV_CONNECTED;
	} else if (msg_dev->state == VXT_MSG_DEV_ERROR) {
		UMI_LOG(35, VXTPRINT_BETA_LEVEL,
		        "device in error state, can't attach\n");
		attach_info->status =  VXT_MSG_ATTACH_ERROR;
	} else if (msg_dev->state == VXT_MSG_DEV_CONNECTED) {
		UMI_LOG(36, VXTPRINT_BETA_LEVEL,
		        "device is already in attached state\n");
		attach_info->status =  VXT_MSG_ATTACH_BUSY;
	} else {
		UMI_LOG(37, VXTPRINT_BETA_LEVEL,
		        "device is in uninown state, can't attach\n");
		attach_info->status =  VXT_MSG_ATTACH_ERROR;
	}

	return VXT_SUCCESS;
}


/*
 *
 * vxtmsg_dev_detach:
 *
 * vxtmsg_dev_detach is called when a local client wishes to 
 * relinquish access to the shared queues and event, i.e. the
 * IPC device associated with this target.  
 *
 * The mappings are controlled elsewhere, the call to this 
 * routine is to allow device specific customization of shared
 * access.  In the case of this library, shared access is 
 * not supported.  Hence, the logic for attach is a simple 
 * one resource monitor that charts the attach state and
 * only allows one party access at a tim.
 *
 * Returns:
 *
 *		VXT_SUCCESS - call succeeds even when attach 
 *		              does not.
 *
 *
 */

int
static vxtmsg_dev_detach(void *instance, void *parms, uint32_t info_length)
{
	vxtmsg_device_t *msg_dev = (vxtmsg_device_t *)instance;
	vxt_msg_attach_info_t *attach_info = (vxt_msg_attach_info_t *)parms;
	uint32_t write_info = 1;

	UMI_LOG(10, VXTPRINT_PROFILE_LEVEL,
	        " called, device %p\n", msg_dev);

	/*
	 * Important: check the size of the incoming buffer
	 * This is the caller's notion of size and reflects the
	 * size of the buffer passed.
	 */

	if (info_length < sizeof(vxt_msg_attach_info_t)) {
		UMI_WARN(102, VXTPRINT_PRODUCT_LEVEL,
		         " caller has provided an inadequate buffer size"
		         " for device data.  Provided: %d, Needed: %d."
		         " We will continue to detach but device level action"
		         " may be impacted\n",
		         info_length, (uint32_t)sizeof(vxt_msg_attach_info_t));
		write_info = 0;
	}

	if (msg_dev->state == VXT_MSG_DEV_DISCONNECTED) {
		UMI_LOG(38, VXTPRINT_BETA_LEVEL,
		        "device is already disconnected, cannot detach\n");
		if (write_info) {
			attach_info->status =  VXT_MSG_ATTACH_ERROR;
		}
	} else if (msg_dev->state == VXT_MSG_DEV_ERROR) {
		UMI_LOG(39, VXTPRINT_BETA_LEVEL,
		        "device is in error state, detached anyway\n");
		if (write_info) {
			attach_info->status =  VXT_MSG_ATTACH_SUCCESS;
		}
	} else if (msg_dev->state == VXT_MSG_DEV_CONNECTED) {
		attach_info->status =  VXT_MSG_ATTACH_SUCCESS;
		if (write_info) {
			msg_dev->state = VXT_MSG_DEV_DISCONNECTED;
		}
	} else {
		UMI_LOG(40, VXTPRINT_BETA_LEVEL,
		        "device is in unknown state, cannot detach\n");
		if (write_info) {
			attach_info->status =  VXT_MSG_ATTACH_ERROR;
		}
	}

	return VXT_SUCCESS;
}


vxtdev_api_t vxt_msg_callbacks = {
	vxtmsg_dev_query,
	vxtmsg_dev_create,
	vxtmsg_dev_create_complete,
	vxtmsg_dev_connect,
	vxtmsg_dev_attach,
	vxtmsg_dev_detach,
	vxtmsg_dev_remove,
	NULL,
/*
        int (*snapshot)(vxtdev_handle_t device, void **data, int *size);
*/
}; 


/*
 *
 * vxtmsg_dev_load_lib:
 *
 * vxtmsg_dev_load_lib calls the vxt controller,
 * vxtcom_load_device_library routine to advertise
 * itself.  
 *
 * vxtmsg_dev_load_lib takes a vxt controller as an
 * argument.  This is so devices can be loaded into
 * specific controllers in the advent of device 
 * support or multiple supported revisions.
 *
 * On loadable drivers, the registration with
 * the vxt controller takes place at module init
 * time.  In this case, vxtmsg_dev_load_lib is
 * called out of module init.  At present
 * vxt_msg_dev is a built in device for vxt
 * controller.  Therefore, this init is called
 * as part of the vxt controller initialization
 * activity and hence vxtmsg_dev_load_lib is
 * exported.
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS = upon successful lib load
 *
 *		VXT_FAIL - If vxtctrlr lib load fails
 *
 */

int
vxtmsg_dev_load_lib(void *vxtcom_handle)
{

	int ret;
	
	UMI_LOG(11, VXTPRINT_PROFILE_LEVEL,
	        " called, controller handle %p\n", vxtcom_handle);

	ret = vxtcom_load_device_library(vxtcom_handle,
	                                 &vxt_msg_callbacks, VXT_REMOTE_COM);
	if(ret) {
		return VXT_FAIL;
	}

	return VXT_SUCCESS;

}


/*
 *
 * vxtmsg_dev_unload_lib:
 *
 * vxtmsg_dev_unload_lib calls the vxt controller
 * vxtcom_unload_device_library routine to remove
 * itself from the targeted vxt controller.
 *
 * vxt_msg_dev_unload_lib's call to the vxt_com layer
 * can fail because the library is not found or because
 * the library is still busy.  This is reflected in the 
 * the return code which is logged.
 * 
 * Returns:
 *
 *		VXT_SUCCESS = upon successful lib load
 *
 *		VXT_FAIL - If vxtctrlr lib unload fails
 *
 */

int
vxtmsg_dev_unload_lib(void *vxtcom_handle)
{

	int ret;
	
	UMI_LOG(96, VXTPRINT_PROFILE_LEVEL,
	        " called, controller handle %p\n", vxtcom_handle);

	ret = vxtcom_unload_device_library(vxtcom_handle, VXT_REMOTE_COM);
	if(ret) {
		UMI_LOG(97, VXTPRINT_PRODUCT_LEVEL,
		        "error attempting to unload library, ret = 0x%x\n",
		        ret);
		return VXT_FAIL;
	}

	return VXT_SUCCESS;

}
