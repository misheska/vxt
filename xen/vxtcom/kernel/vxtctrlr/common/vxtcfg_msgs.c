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
#include <public/kernel/vxtcom_devlib.h>
#include <public/kernel/bus/hypervisor/vxtcom_bus_ctrlr.h>
#include <vxtctrlr/common/vxtcom_controller.h>
#include <public/vxt_com.h>
#include <public/kernel/vxt_signal.h>
#include <vxtctrlr/common/vxt_com_internal.h>
#include <public/kernel/vxt_module.h>
#include <vxtctrlr/os/vxt_com_os.h>
#include <public/vxt_auth.h>
#include <public/kernel/vxt_auth_db.h>



/*
 * 
 * vxtcfg_msgs.c:
 *
 * Routines to set-up and parse vxt controller config 
 * messages.
 *
 * This file lives within the internal vxt controller space
 * it is an addendum file to vxt_com.c.
 *
 */




/*
 *
 * Internal Helper Routines
 *
 */




/*
 *
 * vxtcfg_connect_device:
 *
 *
 * Request Handler Helper- Parser:
 *
 * vxtcfg_connect_device is called out of a remote request
 * to add a device to this domain.  Memory and port are
 * supplied by the remote endpoint. The memory is 
 * represented in a series of references to the
 * remote resource in the far endpoint. 
 *
 *
 * Returns:
 *
 *	VXT_SUCCESS - Upon successful completion
 *
 *	VXT_PARMS -  Unreachable or undefined endpoint
 *
 *	VXT_RSRC -  Out of controller device slots
 *
 *	VXT_NOMEM - kmalloc failed
 *
 */

static int
vxtcfg_connect_device(vxtcom_ctrlr_t *vxtcom, 
                      vxtcom_connect_dev_info_t *dev_info,
                      uint32_t dev_info_len,
                      vxtcom_ctrlr_api_t *callbacks,
                      vxtsig_handle_t event_local_port,
                      vxtdev_t **new_endpoint)
{

	vxtdev_t                 *newdev;
	void                     *newdev_handle;
	symlibs_t                *library;
	vxtcom_cntrl_squeue_t    *page_queues_entry;
	vxtcom_cntrl_page_ref_t  *page_ref_entry;
	uint32_t                 qcount;
	int                      ret;

	UMI_LOG(131, VXTPRINT_PROFILE_LEVEL, "called, controller %p\n", vxtcom);

	/*
	 * Check for registration of the targeted device type
	 */
	ret = vxtcom_find_lib(vxtcom->device_libraries, 
	                      dev_info->device_type, &library);
	if (ret != VXT_SUCCESS) {
		return ret;
	}

	/* 
	 * set up a new device structure on the targeted
	 * vxt controller
	 */

	newdev = vxt_kmalloc(sizeof(struct vxtdev), VXT_KMEM_KERNEL);
	if (newdev == NULL) {
		return VXT_NOMEM;
	}
	vxtcom_init_wait(&newdev->wq);
	newdev->waiting = 0;
	newdev->type = dev_info->device_type;
	newdev->controller = vxtcom;
	newdev->dev = &(library->callbacks);
	newdev->dev_queues = NULL;
	newdev->remote_resources = TRUE;
	/* string here triggers file dev release on device shutdown */
	newdev->dev_file_name[0] = 0;
	newdev->of_rec = NULL;
	newdev->shutting_down = 0;
        newdev->event = event_local_port;
	vxtcom_strncpy(newdev->uname, dev_info->remote_ep, MAX_VXT_UNAME);
	newdev->error = VXT_SUCCESS;
	newdev->state = VXT_COM_DEV_CONNECTING;
	newdev->library = library;
	newdev->origin = 0;  /* this dev is connected to */
	newdev->next = NULL;


	if (vxtcom_get_slot_handle(vxtcom, &newdev->slot)) {
		vxt_kfree(newdev);
		return VXT_RSRC;
        }


	newdev->remote_dom = dev_info->remote_dom;

	/*
	 * Add any queues that come along with this request
	 * The device specific section can sort out what they
	 * are used for
	 */
	/*
	 * Preset page_ref_entry so it will work in the loop
	 */
	page_ref_entry = (vxtcom_cntrl_page_ref_t *) dev_info->mem_parms;
	for (qcount=0; qcount < dev_info->queue_cnt; qcount++) {
		page_queues_entry = 
		   (vxtcom_cntrl_squeue_t *)page_ref_entry;
		page_ref_entry = 
		   (vxtcom_cntrl_page_ref_t *) (page_queues_entry + 1);

		vxtcfg_connect_remote_queue(newdev, page_ref_entry, 
		                            page_queues_entry->
		                               shared_page_cnt);
		page_ref_entry = 
		   page_ref_entry + page_queues_entry->shared_page_cnt;
	}

	/*
	 * Call the device code to create an instance of the device
	 * Note: dev_parms is a self-defined, device specific field
	 * The field can be used to pass device specific customization
	 * directives, resources which exist outside of the vxt
	 * controller model.
	 */
	ret = 
	   newdev->dev->connect((void *)newdev, 
	                        dev_info->dev_parms, dev_info_len,
	                        qcount, callbacks, &newdev_handle);

	UMI_LOG(153, VXTPRINT_BETA_LEVEL,
	        "dev specific connect ret = 0x%x\n", ret);
	if (ret != VXT_SUCCESS) {
		vxtcom_free_slot_handle(vxtcom, newdev->slot.handle);
		vxt_kfree(newdev);
		return ret;
	}

	newdev->instance = newdev_handle;

	/*
	 * Give Caller a handle to the
	 * new local, generic device
	 * structure.
	 */
	*new_endpoint = newdev;


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
	UMI_LOG(154, VXTPRINT_BETA_LEVEL,
	        "returning, dev %p, type %llx\n", newdev, newdev->type);

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
 * vxtcfg_msg_response
 *
 * Reply - Message Create:
 *
 * Generic response for IPC requests
 *
 * Returns:
 *
 *		VXT_SUCCESS - Upon Successful completion
 *		VXT_BUSY    - Buffer in use
 *
 */

int
vxtcfg_msg_response(vxtcom_ctrlr_t *vxtcom,
                    vxtcom_cntrl_hdr_t *failed_msg,
                    uint64_t event_port, int error)
{

	/*
	 * Set message header to the top of our shared
	 * communication buffer - reply section.
	 */
	vxtcom_cntrl_hdr_t *reply_msg = 
	   (vxtcom_cntrl_hdr_t *) (((char *)vxtcom->bus.outq)
	                           + 2048 + vxtcom->bus.reply_offset);
	vxtcom_cntrl_hdr_t *base = reply_msg;

	vxtcom_cntrl_error_t  *error_response_field;

	UMI_LOG(132, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, port 0x%llx, error %08x\n",
	        vxtcom, event_port, error);

	UMI_LOG(164, VXTPRINT_DEBUG_LEVEL,
	        "make reply message: reply addr %p, busbuf_base %p\n",
	        reply_msg, vxtcom->bus.outq);

	/*
	 * Make sure we own the buffer
         */
	if (vxtcom->bus.outq_header.reply_in_use) {
		/*
		 * Fail call
		 */
		return VXT_FAIL;
	}
	vxtcom->bus.outq_header.reply_in_use = 1;

	memcpy(reply_msg, failed_msg, sizeof(vxtcom_cntrl_hdr_t));

	UMI_LOG(155, VXTPRINT_BETA_LEVEL,
	        "making reply message: type %llx\n",
	         reply_msg->message_type);
	if (error == VXT_SUCCESS) {
		reply_msg->message_type |= VXTCOM_CNTRL_RESPONSE;
	} else {
		reply_msg->message_type |= 
		   (VXTCOM_CNTRL_RESPONSE | VXTCOM_CNTRL_ERROR);
	}

	reply_msg->datalen = sizeof(vxtcom_cntrl_error_t);

	error_response_field = (vxtcom_cntrl_error_t *)(reply_msg + 1);
	
	error_response_field->error = error;
	error_response_field->event_port =  event_port;

	reply_msg = (vxtcom_cntrl_hdr_t *)(error_response_field + 1);

	/*
	 * Indicate termination of messages to 
	 * transport authority.
	 */
	reply_msg->message_type = VXTCOM_CNTRL_NULL;

	/*
	 * Update the reply message offset.  Note: we
	 * update the offset so the next message overwrites
	 * the message terminator.
	 */
	vxtcom->bus.reply_offset += (uint32_t)
	                            (((vxtarch_word)reply_msg) - 
	                             (vxtarch_word)base);
	vxtcom->pending_send = 1;
	
	return VXT_SUCCESS;

}



/*
vxtmsg_disconnect_device()
*/



/* ************************** Config Message Parsing ******************** */


/*
 *
 * vxtcfg_request_dev_create:
 *
 * Request - **Remote** Message Creation:
 *
 * Some systems that the VXT communication module is
 * ported to do not allow the mapping of shared memory
 * on the non-secure guest.  In this case, all device
 * creation must be initiated from the untrusted 
 * guest.  i.e. The untrusted guest produces a local
 * device, allocates the shared memory and other
 * resources and then invites the remote party to
 * map them.  In the case of untrusted guest to trusted
 * domain, this works automatically.  In the case of
 * an untrusted remote party, the trusted domain acts
 * as a proxy, mapping the pages for the remote domain.
 * The trusted domain is in a postion to do this because
 * it brokers all configuration communication between
 * untrusted endpoints.
 *
 * This routine initiates a request for the remote
 * endpoint to start the creation of a device. It is
 * a remote immplementation of the standared IOCTL
 * for create device.
 *
 * Returns:
 *
 *		VXT_SUCCESS - Upon Successful completion
 *		VXT_BUSY    - Buffer in use
 *
 *
 */

int
vxtcfg_request_dev_create(vxtcom_ctrlr_t * vxtcom, uint64_t wait_cookie,
                          uint64_t device_type, char *dev_name,
                          void *dev_data, int datalen)
{
	/*
	 * Prepare the message requesting the start of a
	 * request for a new endpoint connection. The request
	 * is sent to the untrusted, (front-end) controller
	 * This is necessary for ports where the untrusted
	 * controller is not allowed to map foreign memory.
	 *
	 * Set message header to the top of our shared
	 * communication buffer.
	 */
	vxtcom_cntrl_hdr_t        *add_msg;
	void                      *start_msg;
	vxtcom_cntrl_remote_add_t *add_request;
	void                      *dev_data_buf;

	UMI_LOG(133, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p, dev_type %llx, dev_name %s\n",
	         vxtcom, device_type, dev_name);
	/*
	 * Make sure we own the buffer
         */
	if ((vxtcom->bus.outq != NULL) &&
	    (vxtcom->bus.outq_header.request_in_use)) {
		UMI_WARN(144, VXTPRINT_PRODUCT_LEVEL, "failed buffer in use\n");
		/*
		 * Fail call
		 */
		return VXT_FAIL;
	}
	vxtcom->bus.outq_header.request_in_use = 1;
	add_msg = 
	   (vxtcom_cntrl_hdr_t *)
	   (((char *)vxtcom->bus.outq) + vxtcom->bus.request_offset);
	UMI_LOG(165, VXTPRINT_DEBUG_LEVEL,
	        "Config Message... addMsg_addr = %p, request offset = 0x%x\n",
	        add_msg, vxtcom->bus.request_offset);
	start_msg = (void *)add_msg;
	
	/*
	 * Make call to transport authority to establish event
	 */
	add_msg->message_type = VXTCOM_CNTRL_REQ_ADD;
	add_msg->message_id = vxtcom->bus.outq_header.msg_id;
	/*
	 * This is not a generic, guest-to-guest configuarion
	 * protocol.  It is specific to the special relation
	 * between the secure controller and its matching remote
	 * counterpart.  It is specific to a controller pair.
	 */
	add_msg->remote_dom = vxtcom->ctrlr_list.rdom;
	/*
	 * This is filled in for dom to dom by Dom0
	 * at the handoff, proxy hop.
	 */
	add_msg->connecting_dom = 0;
	add_msg->config_dom = vxtcom->ctrlr_list.rdom;
	add_msg->dev_type = device_type;
	add_msg->local_cp = 0;  /* N/A on this call */
	add_msg->remote_cp = 0;  /* Not known yet */
	vxtcom_strncpy(add_msg->remote_ep, dev_name, MAX_VXT_UNAME);
	/*
	 * add_msg->datalen filled in below after
	 * queue data and other add_msg fields are
	 * appended to the buffer.
	 */

	add_request = (vxtcom_cntrl_remote_add_t *)(add_msg + 1);
	UMI_LOG(166, VXTPRINT_DEBUG_LEVEL,
	        "Add Message ... add_request addr %p\n", add_request);
	add_request->create_wait_cookie = wait_cookie;
	add_request->device_info_length = datalen;

	/*
	 * Add in device specific ioctl request information
	 */
	dev_data_buf = (void *) (add_request + 1);

	UMI_LOG(167, VXTPRINT_DEBUG_LEVEL,
	        "Add Msg Dev data... dev_data_buf addr %p\n", dev_data_buf);
	memcpy(dev_data_buf, dev_data, datalen);
	/*
	 * Fill in total length to next message header
	 */
	add_msg->datalen = (((vxtarch_word)(dev_data_buf)) 
	                    + (vxtarch_word)datalen) 
	                   - (vxtarch_word)add_request;
	/*
	 * Push add_request beyond the shared queue info and the
	 * device specific data
	 */
	add_msg = (vxtcom_cntrl_hdr_t *)
	          (((vxtarch_word)dev_data_buf) + (vxtarch_word)datalen);
	UMI_LOG(168, VXTPRINT_DEBUG_LEVEL,
	        "Next Msg ... add_msg addr %p\n", add_msg);
	/*
	 * Indicate termination of messages to 
	 * transport authority.
	 */
	add_msg->message_type = VXTCOM_CNTRL_NULL;
	vxtcom->bus.request_offset += 
	   (uint32_t)(((vxtarch_word)add_msg) - (vxtarch_word)start_msg);
	UMI_LOG(156, VXTPRINT_BETA_LEVEL,"set pending_send\n");
	vxtcom->pending_send = 1;
	/*
	 * Do not wait for the response
	 * let the caller check/wait for it
	 */

	return VXT_SUCCESS;
}


/*
 *
 * vxtcfg_config_create_response:
 *
 * Request: Message Parse - for remote device create
 *
 * vxtcfg_config_create_reponse takes a remote request
 * initiate the create of a new device.  The call is
 * made because the local controller sits in a domain
 * which cannot map foreign pages.  It is therefore
 * necessary that all device creation initiate from
 * this controller.  i.e. all shared memory is allocated
 * locally and made accessible to the foreign domain of
 * the controller counterpart.
 *
 * 
 * 
 *
 *
 */

int
vxtcfg_config_create_response(vxtcom_ctrlr_t *instance, 
                              vxtcom_cntrl_hdr_t *add_msg)
{
	vxtcom_cntrl_remote_add_t *add_request;
	vxtcfg_sync_callback_t    *vxtcfg_callback;
	vxtdev_handle_t           new_device;
        int                       ret;


	UMI_LOG(134, VXTPRINT_PROFILE_LEVEL, "called\n");
	ASSERT(add_msg->message_type == VXTCOM_CNTRL_REQ_ADD);


	vxtcfg_callback = vxt_kmalloc(sizeof(vxtcfg_sync_callback_t),
	                              VXT_KMEM_KERNEL);
	if (vxtcfg_callback == NULL) {
		vxtcfg_msg_response(instance, add_msg, 0, VXT_NOMEM);
		return VXT_SUCCESS;
	}


	add_request = (vxtcom_cntrl_remote_add_t *)(add_msg + 1);
	UMI_LOG(169, VXTPRINT_DEBUG_LEVEL,
	        "add_request = %p\n", add_request);



	/*
	 * make a local copy of the message for syncrhonous
	 * response callback.  We have to reply to this
	 * request before any device creation related
	 * traffic is generated.  This protects our
	 * strict request/reponse behavior on the link
	 */
	vxtcfg_callback->wait_cookie = add_request->create_wait_cookie;
	vxtcfg_callback->instance = instance; 
	memcpy(&vxtcfg_callback->reply_msg,
	       add_msg, sizeof(vxtcom_cntrl_hdr_t));


	UMI_LOG(170, VXTPRINT_DEBUG_LEVEL, "add_device parms - dev_type %llx, remote_ep %s, cfg_callback %p\n", add_msg->dev_type, add_msg->remote_ep, vxtcfg_callback);
	ret = vxtcom_add_device(instance, add_msg->dev_type,
	                        add_msg->remote_ep, 
				(vxtdev_handle_t *) (add_request + 1),
	                        add_request->device_info_length,
	                        &new_device, &vxtcom_utilities,
				vxtcfg_callback);

	UMI_LOG(171, VXTPRINT_DEBUG_LEVEL,
	        "return from vxtcom_add_device and exit\n");

	return VXT_SUCCESS;
}


/*
 * 
 * vxtcfg_synchronous_reply:
 *
 * vxtcfg_synchronous_reply is necessary because of the
 * common IPC/RPC issue of nested messages.  VXT com
 * configuration protocols do not support nested messages
 * therefore to get proper signalling in an RPC context
 * we must guarantee that we either respond with a
 * success and follow through with a compound command
 * or that we send a failure.  Synchronous reply
 * callback is provided either at the first failure
 * or at the moment of the commitment to a compound
 * communication step whichever comes first.
 *
 *
 * Returns:
 *
 *		None
 */

void
vxtcfg_synchronous_reply(vxtcfg_sync_callback_t *parms, int status)
{


	UMI_LOG(135, VXTPRINT_PROFILE_LEVEL, "called, status %08x\n", status);

	if (parms == NULL) {
		return;
	}


	/*
	 * override the local_cp field on this call
	 */
	parms->reply_msg.local_cp = parms->wait_cookie;
	vxtcfg_msg_response(parms->instance, 
	                    &parms->reply_msg, 0,
	                    status);

	vxt_kfree(parms);

}


/*
 *
 * vxtcfg_config_proxy_create_reply:
 *
 * Reply: Message Parse - for remote/proxy device create
 *
 * The proxy create simply kicks off a remote device
 * create process.  This is done by a trusted controller
 * when its untrusted companion cannot map foreign pages.
 *
 * 
 * 
 *
 *
 */

int
vxtcfg_config_proxy_create_reply(vxtcom_ctrlr_t *instance, 
                                 vxtcom_cntrl_hdr_t *add_msg)
{
	vxtcom_cntrl_error_t  *error_response_field;
	vxtcom_req_dev_create_wait_t   *create_wait;

	UMI_LOG(136, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", instance);
	ASSERT((add_msg->message_type & 
	        (VXTCOM_CNTRL_REQ_ADD | VXTCOM_CNTRL_RESPONSE)) ==
	       (VXTCOM_CNTRL_REQ_ADD | VXTCOM_CNTRL_RESPONSE));

	UMI_LOG(157, VXTPRINT_BETA_LEVEL,
	        "device creation started remote proxy returned\n");
	if (add_msg->message_type & VXTCOM_CNTRL_ERROR) {
		UMI_WARN(145, VXTPRINT_PRODUCT_LEVEL,
		         "ERROR: Error in 1st stage of message create\n");
		/*
		 * If we have an error, wake up the waiting
		 * remote device request client, there will
		 * be no asynchronous connect request to do
		 * it.
		 */
		/*
		 * local_cp overriden to avoid extra structure
		 * on return
		 */
		create_wait = (vxtcom_req_dev_create_wait_t *)
		              (vxtarch_word) add_msg->local_cp;
		error_response_field = (vxtcom_cntrl_error_t *)(add_msg + 1);
		UMI_WARN(146, VXTPRINT_PRODUCT_LEVEL, "Error status %llx\n",
		         error_response_field->error);

		create_wait->status = error_response_field->error;
		vxtcom_wakeup(&create_wait->wq);
	} 

	return VXT_SUCCESS;
}

/*
 *
 * vxtcfg_devreq_connect_remote:
 *
 * Request - Message Creation:
 *
 * This routine initiates a request to connect a remote
 * endpoint to a new local device.  The local device
 * has already been set-up, device structure created,
 * queues allocated, and device_state brought up to
 * the waiting-for-connection level.  The event channel
 * however is not allocated here.  The event is allocated
 * by the transport authority and passed back on a reply
 * to this message.
 *
 * Returns:
 *
 *		VXT_SUCCESS - Upon Successful completion
 *		VXT_BUSY    - Buffer in use
 *
 *
 */

int
vxtcfg_devreq_connect_remote(void *device_handle, void *sync_reply_handle,
                             void *dev_data, int datalen, 
                             void *device_specific_instance)
{
	vxtdev_t                *device = (vxtdev_t *)device_handle;
	vxtcom_ctrlr_t          *vxtcom = device->controller;

	/*
	 * Prepare the message requesting a new endpoint
	 * connection. The request is sent to the transport 
	 * authority
	 *
	 * Set message header to the top of our shared
	 * communication buffer.
	 */
	vxtcom_cntrl_hdr_t      *add_msg;
	void                    *start_msg;
	vxtcfg_sync_callback_t  *sync_ret_parms;
	vxtcom_cntrl_attach_t   *add_request;
	vxtcom_cntrl_squeue_t   *page_queues_entry;
	vxtcom_cntrl_page_ref_t *page_ref_entry;
	vxtdev_queues_t         *dev_queues;
	vxtdev_ref_list_t       *ref_list;
	void                    *dev_data_buf;
	int                     i;
	int                     j;

	sync_ret_parms = (vxtcfg_sync_callback_t *)sync_reply_handle;
	UMI_LOG(137, VXTPRINT_PROFILE_LEVEL,
	        "called, dev_handle %p, sync_handle %p, dev_inst %p\n",
	        device_handle, sync_reply_handle, device_specific_instance);

	/*
	 * The device instance is given by the device specific
	 * code here because the reply callback will come
	 * before the device specific code for device
	 * create has returned -- We will wait on this
	 * message.  The device specific instance is needed
	 * to wake up the create call.
	 */
	device->instance = device_specific_instance;
	/*
	 * Make sure we own the buffer
         */
	if (vxtcom->bus.outq_header.request_in_use) {
		UMI_WARN(147, VXTPRINT_PRODUCT_LEVEL, "failed buffer in use\n");
		/*
		 * Fail call
		 */
		if (sync_ret_parms != NULL) {
			vxtcfg_synchronous_reply(sync_ret_parms, VXT_FAIL);
			vxtcfg_signal_config_queue(vxtcom);
		}
		return VXT_FAIL;
	}
	vxtcom->bus.outq_header.request_in_use = 1;
	add_msg = 
	   (vxtcom_cntrl_hdr_t *)
	   (((char *)vxtcom->bus.outq) + vxtcom->bus.request_offset);
	UMI_LOG(172, VXTPRINT_DEBUG_LEVEL,
	        "addMsg_addr = %p, request offset = 0x%x\n",
	        add_msg, vxtcom->bus.request_offset);
	start_msg = (void *)add_msg;
	
	/*
	 * Make call to transport authority to establish event
	 */
	add_msg->message_type = VXTCOM_CNTRL_ADD_REQ;
	add_msg->message_id = vxtcom->bus.outq_header.msg_id;
	add_msg->remote_dom = device->remote_dom;
	/*
	 * This is filled in for dom to dom by Dom0
	 */
	add_msg->connecting_dom = 0;
	add_msg->config_dom = vxtcom->ctrlr_list.rdom;
	add_msg->dev_type = device->type;
	add_msg->local_cp = device->slot.handle;
	add_msg->remote_cp = 0;  /* Not known yet */
	vxtcom_strncpy(add_msg->remote_ep, device->uname, MAX_VXT_UNAME);
	/*
	 * add_msg->datalen filled in below after
	 * queue data and other add_msg fields are
	 * appended to the buffer.
	 */

	add_request = (vxtcom_cntrl_attach_t *)(add_msg + 1);
	UMI_LOG(173, VXTPRINT_DEBUG_LEVEL,
	        "add_request addr %p, sync_ret_parms %p\n",
	        add_request, sync_ret_parms);
	UMI_LOG(174, VXTPRINT_DEBUG_LEVEL,
	        "test address of add_request->create_wait, %llx\n",
		add_request->create_wait);
	if (sync_ret_parms != NULL) {
		UMI_LOG(158, VXTPRINT_BETA_LEVEL,
		        "this is a proxied device instantiate\n");
		UMI_LOG(175, VXTPRINT_DEBUG_LEVEL,
	                "test address of sync_ret_parms->wait_cookie, %llx\n",
		        sync_ret_parms->wait_cookie);
		add_request->create_wait = sync_ret_parms->wait_cookie;
	} else {
		add_request->create_wait = 0;
	}


	/*
	 * We pass the handle to our local event to the
	 * remote party.  There are several subtleties
	 * to this:
	 * 1: The event handle is keyed to the ultimate endpoint
	 *    i.e. even though configuration will pass through
	 *    the security authority (Dom0), runtime events will
	 *    be triggered by the targeted remote endpoint.  This
	 *    implies that the add function knew the name of the
	 *    ulitmate endpoint and ran a configuration protocol
	 *    to establish an interrupt connection between the
	 *    two endpoints prior to this call.
	 * 2: The handle passed will serve as a remote handle
	 *    to the remote endpoint.  That endpoint will allocate
	 *    a signal structure and will send a handle to it
	 *    on the reply to this call.  That handle will be stored
	 *    in the local structure as the remote handle and used
	 *    for locally initiated interrupts of the remote device.
	 */
	 
	add_request->rport = device->event;
	add_request->dev_type = device->type;
	add_request->cfg_data_len = datalen;

	page_queues_entry = (vxtcom_cntrl_squeue_t *)(add_request + 1);
	page_ref_entry = (vxtcom_cntrl_page_ref_t *)(page_queues_entry + 1);
	UMI_LOG(176, VXTPRINT_DEBUG_LEVEL,
	        "page_queues_entry addr %p, page_ref_entry addr %p\n",
	        page_queues_entry, page_ref_entry);
	i = 0;
	dev_queues = device->dev_queues;
	while (dev_queues != NULL) {
		i++;
		j = 0;
		ref_list = dev_queues->refs;
		while (ref_list != NULL) {
			j++;
			/*
			 * copy ref value into buffer destined for
			 * transport authority.
			 * IMPORTANT:  Note that the page refs are
			 * resident on the device in reverse order
			 * i.e. the page with the highest address
			 * is the first addressed.
			 */
			page_ref_entry->page_ref = ref_list->ref;
			ref_list = ref_list->next;
			page_ref_entry++;
		}
		page_queues_entry->shared_page_cnt = j;
		/*
		 * page_queues_entry incremented to
		 * start after last ref entry.
		 */
		page_queues_entry = (vxtcom_cntrl_squeue_t *)page_ref_entry;
		page_ref_entry = (vxtcom_cntrl_page_ref_t *)
		                 (page_queues_entry + 1);
		dev_queues = dev_queues->next;
   
	}
	add_request->shared_queue_cnt = i;
	/*
	 * Add in device specific data
	 */
	dev_data_buf = (void *)page_queues_entry;
	UMI_LOG(177, VXTPRINT_DEBUG_LEVEL,
	        "dev_data_buf addr %p\n", dev_data_buf);
	memcpy(dev_data_buf, dev_data, datalen);
	/*
	 * Fill in total length to next message header
	 */
	add_msg->datalen = 
	   (((vxtarch_word)(page_queues_entry)) + (vxtarch_word)datalen) 
	   - (vxtarch_word)add_request;
	/*
	 * Push add_request beyond the shared queue info and the
	 * device specific data
	 */
	add_msg = (vxtcom_cntrl_hdr_t *)
	          (((vxtarch_word)dev_data_buf) + datalen);
	UMI_LOG(178, VXTPRINT_DEBUG_LEVEL,
	        "add_msg addr %p\n", add_msg);
	/*
	 * Indicate termination of messages to 
	 * transport authority.
	 */
	add_msg->message_type = VXTCOM_CNTRL_NULL;
	vxtcom->bus.request_offset += 
	   (uint32_t)(((vxtarch_word)add_msg) - (vxtarch_word)start_msg);

	/*
	 * Our first part of device create is successful
	 * we will be sending a request to create a
	 * paired device to the remote party.
	 * If this was a proxy request for device create,
	 * signal the synchronous waiter on the remote
	 * side via a reply to the proxy request.
	 *
	 */ 
	if (sync_ret_parms != NULL) {
		vxtcfg_synchronous_reply(sync_ret_parms, VXT_SUCCESS);
	}
	vxtcom->pending_send = 1;
	/*
	 * Do not wait for the response
	 * let the device check/wait for it
	 */

	return VXT_SUCCESS;
}


/*
 *
 * vxtcfg_config_add_req:
 *
 * Request: Message Parse
 *
 * vxtcfg_config_add_req takes a remote request to create a device
 * parses it and attempts to execute the order.
 *
 * The domain this code is running in must be privileged.  Or
 * the caller must be privileged. Security is maintained by 
 * consulting a third party security authority regarding the
 * connection being requested.
 *
 * The remote endpoint may be trying to connect to this domain, in
 * this case vxtcfg_config_add_req will undertake the aquisition
 * of resources including the mapping of pages and aqusition of
 * an event channel.  The resources are then bundled off to a 
 * vxtcfg_connect_device call.
 *
 * If the remote endpoint is connecting to another guest, the
 * event channel is allocated and the pages and event are packed
 * off to the second endpoint on a connect request.
 * 
 * 
 * 
 *
 *
 */

int
vxtcfg_config_add_req(vxtcom_ctrlr_t *instance, vxtcom_cntrl_hdr_t *add_msg)
{
	vxtcom_cntrl_attach_t          *add_request;
	uint64_t                       caller_event_port;
	vxtsig_handle_t                event_local_port;
        int                            ret;
        vxtcom_connect_dev_info_t      connect_dev_info;
	vxtcom_cntrl_squeue_t          *page_queues_entry;
	vxtcom_cntrl_page_ref_t        *page_ref_entry;
	vxtdev_t                       *device = NULL;
	vxtcom_req_dev_create_wait_t   *create_wait;
	int                            i;


	UMI_LOG(138, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", instance);
	ASSERT(add_msg->message_type == VXTCOM_CNTRL_ADD_REQ);

	add_request = (vxtcom_cntrl_attach_t *)(add_msg + 1);
	create_wait = (vxtcom_req_dev_create_wait_t *)
	              (vxtarch_word)add_request->create_wait;
	/*
	 * Find the remote device local device pair and
	 * check to see if the requestor is allowed
	 * to connect.  
	 */
	
/*
CDY CDY fill in when available
Check unspoofable remote domid and the determinant univesal connection name
get back remote dom to double check guest.
	if (is_initial_xendomain()) {
		ret = AuthorizationFacility(add_msg->remote_ep, 
		                            instance->bus.ctrlr_list.rdom,
		                            &ep_dom);
		if (ret) {
			vxtcfg_msg_response(instance, add_msg,
			                    NULL, ret);
			if (create_wait != NULL) {
				create_wait->request_finished = 1;
				create_wait->status = VXT_FAIL;
				vxtcom_wakeup(&create_wait->wq);

			}
			return VXT_FAULT;
		}
		if (add_msg->remote_device != DOMID_SELF) {
			vxtcfg_msg_response(instance, add_msg,
			                    NULL, VXT_PARM);
			if (create_wait != NULL) {
				create_wait->request_finished = 1;
				create_wait->status = VXT_FAIL;
				vxtcom_wakeup(&create_wait->wq);

			}
			return VXT_FAULT;
		}
		add_msg->remote_device = ep_dom;
	}
*/
{
	int ret;
	vxt_db_lookup_rec_t record;
	ret = vxtdb_client_lookup(instance->bus.remote_uuid,
	                          add_msg->remote_ep, &record);
	UMI_LOG(614, VXTPRINT_PRODUCT_LEVEL,
	        "vxtdb_client_lookup return = 0x%x\n", ret);
	if (ret != VXT_SUCCESS) {
		UMI_LOG(615, VXTPRINT_PRODUCT_LEVEL,
		        "Authorization record not found for"
		        " guest %s, device %s\n",
		        instance->bus.remote_uuid,
		        add_msg->remote_ep);
		if (create_wait != NULL) {
			create_wait->status = VXT_FAIL;
			create_wait->request_finished = 1;
			vxtcom_wakeup(&create_wait->wq);

		}
		vxtcfg_msg_response(instance, add_msg, 0, VXT_RSRC);
		return VXT_RSRC;
		
	}
	/*
	 *  The remote uuid and the device provide a key to a record.  The
	 *  record contains routing information as well as authorization
	 *  Check the server domain id.  If it is a Dom0 device we can connect
	 *  If it is not, we need to set up a guest to guest connection or
	 *  do an indirect connection through our client vxt message
	 *  device.
	 */
	/*
	 * At present we are the only endpoint the guest can connect to
	 * Therefore for direct connection, the remote dom in add_msg, (us)
	 * must match the server_dom.  
	 */
	if (record.server_dom != add_msg->remote_dom) {
		UMI_WARN(616, VXTPRINT_PRODUCT_LEVEL,
		         "VxT DB record for guest %s, "
		         "device %s, is routed to the endpoint "
		         "%lld and not Dom0.  We do not support "
		         "direct guest to guest at present\n",
		         instance->bus.remote_uuid,
		         add_msg->remote_ep, record.server_dom); 
		if (create_wait != NULL) {
			create_wait->status = VXT_FAIL;
			create_wait->request_finished = 1;
			vxtcom_wakeup(&create_wait->wq);

		}
		vxtcfg_msg_response(instance, add_msg, 0, VXT_RSRC);
		return VXT_RSRC;
	}

}

	/*
	 * If we are DOM0
	 * Create an event channel and provide it to both
	 * endpoints.
	 */

#ifdef notdefcdy
	if (is_initial_xendomain()) {
Leave this out until issues of guest to guest are resolved
       		alloc_unbound.dom = DOMID_SELF;
		if (add_msg->connecting_dom) {
			/*
			 * If non-zero, this was filled in by
			 * Dom0 proxy between two guests and
			 * referes to the remote guest.
			 */
        		alloc_unbound.remote_dom = add_msg->connecting_dom;
		} else {
			alloc_unbound.remote_dom = instance->ctrlr_list.rdom;
		}

        	ret = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound,
		                                  &alloc_unbound);

		if (ret) {
			if (create_wait != NULL) {
				create_wait->status = VXT_FAIL;
				create_wait->request_finished = 1;
				vxtcom_wakeup(&create_wait->wq);

			}
			vxtcfg_msg_response(instance, add_msg, 0, VXT_RSRC);
			return VXT_RSRC;
		}

		caller_event_port = alloc_unbound.port;
		/* 
	 	* The  remote party will have to do an interdomain bind
	 	* the caller can use the port as is. CDYCDY
	 	* Except that evtchn **driver** will not register to
	 	* connect ports!!! Must alter driver. CDYCDY
	 	*/
	} else 
#endif
{
		add_request = (vxtcom_cntrl_attach_t *)(add_msg + 1);
		/*
		 * Privileged controller is connecting to us, port
		 * already exists
	 	 */
		caller_event_port = add_request->rport;
		UMI_LOG(159, VXTPRINT_BETA_LEVEL,
		        "DomU, event port 0x%llx\n",
		        caller_event_port);
	}

	/*
	 * We check to see if this connection is bound for
	 * us.  If so send the page references and event 
	 * channel off to be loaded into a new device
	 *
	 */

	if (add_msg->remote_dom == add_msg->config_dom) {

		UMI_LOG(160, VXTPRINT_BETA_LEVEL,
		        "we are the remote endpoint\n");
		/*
		 * Grab a signal index from vxt_signal.  This
		 * is our local handle.   Assuming we are 
		 * successful, we will register the remote
		 * handle with our local signal structure.
		 *
		 * Note:  The remote handle and indeed the
		 * handle we request are keyed to the binding
		 * between the two endpoints.  This is set
		 * up a-priori.
		 *
		 * CDYCDYCDY:  for now there is no guest to
		 * guest support and so all requests are made
		 * to the default DOM0 connection in the guest.
		 * We therefore use the signal hash object set
		 * for the associated VXTCom controller.  Eventually
		 * this will have to check the remote domain to
		 * get the proper hash object.
		 */
		/*
		 * Note:  the device field is null at present
		 * signal implementation does not use this
		 * field at present, if this changes, the
		 * code below must either be moved or the
		 * vxtsig code will need a registration call
		 * The field may be used to bind the guest to
		 * guest endpoint mapping.  It might be just
		 * as effective to map a structure associated
		 * with the guest to guest pairing.
		 */
		ret = vxtsig_object_create(instance, NULL,
		                           instance->bus.dev_events,
		                           &event_local_port);
		if (ret) {
			UMI_WARN(148, VXTPRINT_PRODUCT_LEVEL,
			         "failed remote endpoint event connect\n");
			/*
			 * Have to send prot back to guest to
			 * remove
			 */
			if (create_wait != NULL) {
				create_wait->status = VXT_FAIL;
				create_wait->request_finished = 1;
				vxtcom_wakeup(&create_wait->wq);
		
			}
			vxtcfg_msg_response(instance, 
			                    add_msg,
			                    caller_event_port,
			                    VXT_RSRC);
			return VXT_RSRC;
		}
		UMI_LOG(161, VXTPRINT_BETA_LEVEL, 
		        "reg the remote port:%08llx with our sig_obj: %08llx\n",
		        caller_event_port, event_local_port);
		ret = vxtsig_object_register_remote(instance->bus.dev_events,
		                                    event_local_port,
		                                    caller_event_port);
		if (ret) {
			UMI_WARN(149, VXTPRINT_PRODUCT_LEVEL,
			         "failed remote endpoint event connect\n");
			/*
			 * Have to send prot back to guest to
			 * remove
			 */
			if (create_wait != NULL) {
				create_wait->status = VXT_FAIL;
				create_wait->request_finished = 1;
				vxtcom_wakeup(&create_wait->wq);
		
			}
			vxtcfg_msg_response(instance, 
			                    add_msg,
			                    caller_event_port,
			                    VXT_RSRC);
			return VXT_RSRC;
		}

		/*
		 *  Create a new device to match the request
		 */


		/*
		 * If no mem_parms, dev_parms sits after add_request
		 * If there are mem_parms, dev_parms is updated in the
		 * loop below.
		 */
		connect_dev_info.dev_parms = (void *)(add_request + 1);
		connect_dev_info.device_type = add_msg->dev_type;
		vxtcom_strncpy(connect_dev_info.remote_ep, 
		               add_msg->remote_ep, MAX_VXT_UNAME);
		UMI_LOG(162, VXTPRINT_BETA_LEVEL,
		        "remote_ep %s\n", add_msg->remote_ep);
		connect_dev_info.mem_parms = (void *)(add_request + 1);
		connect_dev_info.queue_cnt = add_request->shared_queue_cnt;


		for (i=0; i < add_request->shared_queue_cnt; i++) {
			UMI_LOG(186, VXTPRINT_ALPHA_LEVEL,
			        "shared queue found\n");
			page_queues_entry = (vxtcom_cntrl_squeue_t *)
			                    connect_dev_info.dev_parms;
			page_ref_entry = (vxtcom_cntrl_page_ref_t *)
			                 (page_queues_entry + 1);
			connect_dev_info.dev_parms = 
			   (void *)(page_ref_entry + 
			            (page_queues_entry->shared_page_cnt));
			
		}

		/*
		 * Find the identity of the remote domain we are
		 * connecting to.  (Different from the remote domain
		 * of the paired controller in guest to guest cases.)
		 */
		if (add_msg->connecting_dom) {
			/*
			 * If non-zero, this was filled in by
			 * Dom0 proxy between two guests and
			 * refers to the remote guest.
			 */
        		connect_dev_info.remote_dom = add_msg->connecting_dom;
		} else {
			connect_dev_info.remote_dom = instance->ctrlr_list.rdom;
		}
		UMI_LOG(179, VXTPRINT_DEBUG_LEVEL,"before memory call - add_msg %p, add_request %p, mem_parms %p, queue_cnt 0x%x, dev_parms %p\n", add_msg, add_request, connect_dev_info.mem_parms, connect_dev_info.queue_cnt, connect_dev_info.dev_parms);
		ret = vxtcfg_connect_device(instance, &connect_dev_info,
		                            (uint32_t)add_request->cfg_data_len,
		                            &vxtcom_utilities, 
		                            event_local_port, &device);
		UMI_LOG(180, VXTPRINT_DEBUG_LEVEL,
		        "vxtcfg_connect_device return 0x%x\n", ret);
		if (ret != VXT_SUCCESS) {
			/* release event */
	
			UMI_WARN(150, VXTPRINT_PRODUCT_LEVEL,
			         "vxtcfg_connect_device failed, ret 0x%x\n",
			         ret);
			vxtsig_object_destroy(instance->bus.dev_events,
			                      event_local_port);
			/*
			 * Have to send port back to guest to
			 * remove
			 */
			if (create_wait != NULL) {
				create_wait->status = VXT_FAIL;
				create_wait->request_finished = 1;
				vxtcom_wakeup(&create_wait->wq);

			}
			vxtcfg_msg_response(instance, 
			                    add_msg,
			                    caller_event_port,
			                    VXT_NOMEM);

			vxtcfg_signal_config_queue(instance);
			return VXT_SUCCESS;
		}

		/* 
		 * set the remote slot number and communicate our
		 * slot number back to the other side.
		 */

		device->remote_slot.handle = add_msg->local_cp;
		add_msg->remote_cp = device->slot.handle;

	} else {
#ifdef CDY_GUEST_TO_GUEST
		/*
		 * Only a privileged domain that is part of the TCB
		 * is allowed to connect up to endpoints.
		 */

		if (!is_initial_xendomain()) {
			struct evtchn_close close;
			close.port = event_local_port;
	
			ret = HYPERVISOR_event_channel_op(EVTCHNOP_close,
			                                  &close);
			if (create_wait != NULL) {
				create_wait->status = VXT_FAIL;
				create_wait->request_finished = 1;
				vxtcom_wakeup(&create_wait->wq);

			}
			/*
			 * Have to send prot back to guest to
			 * remove
			 */
			vxtcfg_msg_response(instance, 
			                    add_msg,
			                    caller_event_port,
			                    VXT_NOMEM);
			return VXT_PERM;
		}
		/*
		 * call routine to set up  IPC to remote
		 * guest to finish connection
		 */

		vxtcom_request_remote_device_connect
#else
/*
 * These statements have no semantic value
 * they are here to silence compiler warnings and will
 * be removed when we uncomment guest to guest
 */
event_local_port = 0;
create_wait = NULL;
#endif
	}
	UMI_LOG(181, VXTPRINT_DEBUG_LEVEL,"send the vxtcfg_msg_response\n");
	vxtcfg_msg_response(instance, 
	                    add_msg,
	                    event_local_port,
	                    VXT_SUCCESS);

	/*
	 *
	 * Wake up the waiter if we were asking the remote
	 * to start this connection
	 */
	if (create_wait != NULL) {
		UMI_LOG(187, VXTPRINT_ALPHA_LEVEL,
		        "waking proxy create client wait = %p\n", 
		        create_wait);
		create_wait->status = VXT_SUCCESS;
		create_wait->request_finished = 1;
		create_wait->slot_handle = device->slot.handle;
		vxtcom_wakeup(&create_wait->wq);
	}
	/*
	 * Tell any local waiters that we have just
	 * added a new device.
	 */
	instance->dev_event_list |= VXT_POLL_NEW_DEV;
	vxtcom_wakeup(&instance->new_dev_wait);
	if (device != NULL) {
		device->library->dev_event_list |= VXT_POLL_NEW_DEV;
		vxtcom_wakeup(&device->library->new_dev_wait);
	}

	vxtcfg_signal_config_queue(instance);

	return VXT_SUCCESS;
}


/*
 *
 * vxtcfg_config_add_reply:
 *
 * Reply - Message Parse:
 *
 * vxtcfg_config_add_reply is called on message reply when the
 * message parser determines message type is add_request with the
 * reply bit set.
 *
 * vxtcfg_config_add_reply handles successful and failed add_request
 * attempts.  In the case of a failed attempt, the requestor may
 * be saddled with an unnecessary port.   vxtcfg_config_add_reply
 * removes the port before setting the error state in the associated
 * device and returning.
 *
 * In the case of a successful add_request, the device state is set
 * to Connected and the device callback add_request_complete is
 * invoked.
 *
 */

int
vxtcfg_config_add_reply(vxtcom_ctrlr_t *instance, vxtcom_cntrl_hdr_t *add_msg)
{
	vxtcom_cntrl_error_t  *error_response_field;
	vxtdev_t              *device;
	vxt_slot_t            *slot;
	int                   ret;

	UMI_LOG(139, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", instance);
	ASSERT((add_msg->message_type & 
	        (VXTCOM_CNTRL_ADD_REQ | VXTCOM_CNTRL_RESPONSE)) == 
	       (VXTCOM_CNTRL_ADD_REQ | VXTCOM_CNTRL_RESPONSE));

	if (add_msg->message_type & VXTCOM_CNTRL_ERROR) {
		/*
		 * Give back event port if one was allocated
		 * half-way through initialization.
		 * Tell anyone waiting that device must be 
		 * deleted.
	         */
		error_response_field = (vxtcom_cntrl_error_t *)(add_msg + 1);
		if (error_response_field->event_port) {
			/*
			 * No action needed with vxt signal, the
			 * local port object will be removed when
			 * the device is deleted.
			 */
		}
		ret = vxtcom_find_slot(instance, add_msg->local_cp, &slot);
		if (ret != VXT_SUCCESS) {
			/*
			 * No action needed with vxt signal, the
			 * local port object will be removed when
			 * the device is deleted.
			 */
		}
		device = vxtcom_slot_find_device(slot);
		device->state =  VXT_COM_DEV_ERROR;
		device->error = error_response_field->error;
		device->dev->create_complete((void *)device, 
		                             device->instance,
		                             0, device->slot.handle,
		                             0, device->error);
		UMI_LOG(188, VXTPRINT_ALPHA_LEVEL, "signal config queue\n");
		vxtcfg_signal_config_queue(instance);
		return error_response_field->error;
	} else {
#ifdef notdefcdy
		ret = vxtcom_find_slot(instance, add_msg->local_cp, &slot);
		if (ret != VXT_SUCCESS) {
			/*
			 * No action needed with vxt signal, the
			 * local port object will be removed when
			 * the device is deleted.
			 */
		}
		device = vxtcom_slot_find_device(slot);
		error_response_field = (vxtcom_cntrl_error_t *)(add_msg + 1);
		ret = vxtsig_object_register_remote(instance->bus.dev_events,
		             device->event, error_response_field->event_port);
		if (ret) {
			device->state =  VXT_COM_DEV_ERROR;
			device->error = VXT_PARM;
			device->dev->create_complete(
			   (void *)device, device->instance,
			   device->event, device->slot.handle, 
			   device->remote_dom, device->error);
			return VXT_FAIL;
		}

		device->state =  VXT_COM_DEV_CONNECTED;
#endif
		ret = vxtcom_find_slot(instance, add_msg->local_cp, &slot);
		UMI_LOG(182, VXTPRINT_DEBUG_LEVEL,
		        "local slot %llx, status 0x%x, slot addr %p\n", 
		        add_msg->local_cp, (uint32_t)ret, slot);

		if (ret != VXT_SUCCESS) {
			UMI_WARN(151, VXTPRINT_PRODUCT_LEVEL, 
			         "slot empty, controller %p, slot %llu\n",
				instance, add_msg->local_cp);
			return VXT_FAIL;
		}
		device = vxtcom_slot_find_device(slot);
		UMI_LOG(189, VXTPRINT_ALPHA_LEVEL,
		        "device addr %p\n", device);
		if (device == NULL) {
			return VXT_FAIL;
		}
		error_response_field = (vxtcom_cntrl_error_t *)(add_msg + 1);
		ret = vxtsig_object_register_remote(instance->bus.dev_events,
		             device->event, error_response_field->event_port);
		UMI_LOG(190, VXTPRINT_ALPHA_LEVEL,
		        "register remote interrupt "
		        "port %llx, local port %llx\n",
			device->event, error_response_field->event_port);
		/* filled in by the remote transport authority */
		device->remote_dom = add_msg->connecting_dom;
		device->remote_slot.handle = add_msg->remote_cp;
		ASSERT(device->remote_slot.handle != 0);
		device->state =  VXT_COM_DEV_CONNECTED;
		/*
		 * device specific complete usually wakes the
		 * sleeping device create to finish processing
		 * an IOCTL create request.
		 */
		device->dev->create_complete((void *)device, 
		                             device->instance,
		                             device->event,
		                             device->slot.handle,
		                             device->remote_dom,
		                             VXT_SUCCESS);
		return VXT_SUCCESS;
	}
}


/*
 *
 * vxtcfg_devreq_remove_device:
 *
 * Request - Message Creation:
 *
 * This routine initiates a request to remove
 * a device on the remote controller.  The 
 * local device is destroyed  if it was the 
 * connecting device.  If the local device
 * controller instigated the original connection
 * then it must not be destroyed until the remote
 * has been shutdown.  (Xen shared memory
 * artifacts.)
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS - Upon Successful completion
 *		VXT_BUSY    - Buffer in use
 *
 *
 */


int
vxtcfg_devreq_remove_device(void *device_handle, void *info, uint32_t info_len)
{
	vxtdev_t *device = (vxtdev_t *)device_handle;
	vxtcom_ctrlr_t *vxtcom = device->controller;
	void *msg_info_ptr;

	/*
	 * Prepare the message requesting to remove
	 * an endpoint on the remote controller.
 	 * "unplug a device from the vxt controller"
	 *
	 */
	vxtcom_cntrl_hdr_t *rem_msg;
	void *start_msg;

	UMI_LOG(140, VXTPRINT_PROFILE_LEVEL,
	        "called, device %p\n", device_handle);

	/*
	 * Make sure we own the buffer
         */
	if ((vxtcom->bus.outq_header.request_in_use) || 
	    (vxtcom->bus.outq == NULL)) {
		/*
		 * Fail call
		 */
		return VXT_FAIL;
	}
	vxtcom->bus.outq_header.request_in_use = 1;
	rem_msg = 
	   (vxtcom_cntrl_hdr_t *)
	   (((char *)vxtcom->bus.outq) + vxtcom->bus.request_offset);
	start_msg = (void *)rem_msg;
	
	/*
	 * Make call to transport authority to establish event
	 */
	rem_msg->message_type = VXTCOM_CNTRL_REMOVE;
	rem_msg->message_id = vxtcom->bus.outq_header.msg_id;
	rem_msg->remote_dom = device->remote_dom;
	rem_msg->dev_type = device->type;
	rem_msg->local_cp = device->slot.handle;
	rem_msg->remote_cp = device->remote_slot.handle; 
	vxtcom_strncpy(rem_msg->remote_ep, device->uname, MAX_VXT_UNAME);
	rem_msg->datalen = info_len;

	msg_info_ptr = (void *)(rem_msg + 1);
	memcpy(msg_info_ptr, info, info_len);

	/*
	 * Push rem_request pointer beyond the present request
	 * and set the next descriptor to the NULL message.
	 */
	rem_msg = (vxtcom_cntrl_hdr_t *)
	          (((vxtarch_word)msg_info_ptr) + info_len);

	vxtcom->bus.request_offset += 
	   (uint32_t)(((vxtarch_word)rem_msg) - (vxtarch_word)start_msg);

	/*
	 * Indicate termination of messages to 
	 * transport authority.
	 */
	UMI_LOG(183, VXTPRINT_DEBUG_LEVEL,
	        "null rem_msg addr %p\n", rem_msg);
	rem_msg->message_type = VXTCOM_CNTRL_NULL;
	vxtcom->pending_send = 1;
	/*
	 * Do not wait for the response
	 * let the caller check/wait for it
	 */

	return VXT_SUCCESS;
}
			

/*
 *
 * vxtcfg_config_rem_response:
 *
 * reply - Message Parse: respond to remove request
 *
 * vxtcfg_config_rem_response is called on message remove request
 * when the message parser determines message type is a remove 
 * device request
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS - Upon successful completion
 *		VXT_FAIL - Message does not refer to a populated slot
 *
 */

int
vxtcfg_config_rem_response(vxtcom_ctrlr_t *instance, 
                           vxtcom_cntrl_hdr_t *rem_msg)
{
	void        *rem_info;
	vxtdev_t    *device;
	vxt_slot_t  *slot;
	void        *msg_info_ptr;
	int          ret;

	ASSERT((rem_msg->message_type & VXTCOM_CNTRL_REMOVE)
	       == VXTCOM_CNTRL_REMOVE);

	UMI_LOG(141, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", instance);
	ret = vxtcom_find_slot(instance, rem_msg->remote_cp, &slot);
	if (ret != VXT_SUCCESS) {
		vxtcfg_msg_response(instance, rem_msg, 0, VXT_FAIL);
		vxtcfg_signal_config_queue(instance);
		return VXT_FAIL;
	}
	device = vxtcom_slot_find_device(slot);
	UMI_LOG(163, VXTPRINT_BETA_LEVEL, "device found %p\n", device);
	if (rem_msg->datalen != 0) {
		msg_info_ptr = vxt_kmalloc(rem_msg->datalen, VXT_KMEM_KERNEL);
		if (msg_info_ptr == NULL) {
			vxtcfg_msg_response(instance, rem_msg, 0, VXT_NOMEM);
			vxtcfg_signal_config_queue(instance);
			return VXT_NOMEM;
		}
		rem_info = (void *)(rem_msg + 1);
		memcpy(msg_info_ptr, rem_info, rem_msg->datalen);
	} else {
		msg_info_ptr = NULL;
	}
	UMI_LOG(184, VXTPRINT_DEBUG_LEVEL, "calling remove_device\n");
	ret = vxtcom_remove_device(instance, device,
	                           msg_info_ptr, rem_msg->datalen, FALSE);

	if (ret != VXT_SUCCESS) {
		UMI_WARN(152, VXTPRINT_PRODUCT_LEVEL, "remove device failed\n");
		vxtcfg_msg_response(instance, rem_msg, 0, ret);
	} else {
		UMI_LOG(185, VXTPRINT_DEBUG_LEVEL, "remove device succeeded\n");
		vxtcfg_msg_response(instance, rem_msg, 0, VXT_SUCCESS);
	}
	if (msg_info_ptr != NULL) {
		vxt_kfree(msg_info_ptr);
	}
	vxtcfg_signal_config_queue(instance);
	return VXT_SUCCESS;
}


/*
 *
 * vxtcfg_config_rem_reply:
 *
 * Reply - Message Parse:
 *
 * vxtcfg_config_rem_reply is called on message reply when the
 * message parser determines message type is remove device 
 * request with the reply bit set.
 *
 * vxtcfg_config_rem_reply handles successful and failed remove
 * attempts.  
 *
 * In the case of a successful remove request, the remote device
 * has been removed if the local device has not yet been
 * freed, it is done on the complete call.
 *
 */

int
vxtcfg_config_rem_reply(vxtcom_ctrlr_t *instance, vxtcom_cntrl_hdr_t *rem_msg)
{
	vxtcom_cntrl_error_t  *error_response_field;
	vxtdev_t              *device;
	vxt_slot_t            *slot;
	int                   ret;

	UMI_LOG(142, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", instance);
	ASSERT((rem_msg->message_type & 
	        (VXTCOM_CNTRL_REMOVE | VXTCOM_CNTRL_RESPONSE)) ==
	       (VXTCOM_CNTRL_REMOVE | VXTCOM_CNTRL_RESPONSE));

	ret = vxtcom_find_slot(instance, rem_msg->remote_cp, &slot);
	if (ret != VXT_SUCCESS) {
		return VXT_FAIL;
	}
	device = vxtcom_slot_find_device(slot);

	if (rem_msg->message_type & VXTCOM_CNTRL_ERROR) {
		error_response_field = (vxtcom_cntrl_error_t *)(rem_msg + 1);
		device->state =  VXT_COM_DEV_ERROR;
		device->error = error_response_field->error;
		vxtcom_remove_device_complete((void *)device, device->error);
		return error_response_field->error;
	} else {
		error_response_field = (vxtcom_cntrl_error_t *)(rem_msg + 1);

		/*
		 * remove complete usually wakes the
		 * sleeping device remove call to finish processing
		 * an  remove device IOCTL request.
		 */
		vxtcom_remove_device_complete((void *)device, VXT_SUCCESS);
		return VXT_SUCCESS;
	}
}


/*
 *
 * vxtcfg_devreq_ctrlr_shutdown:
 *
 * Request - Message Creation:
 *
 * This routine signals the remote party that
 * a controller shutdown has been initiated.
 * It can be called by either endpoint and
 * it is used to stop device addition and
 * begin the process of synchronized shutdown
 *
 * Returns:
 *
 *		VXT_SUCCESS - Upon Successful completion
 *		VXT_BUSY    - Buffer in use
 *
 *
 */

int
vxtcfg_devreq_ctrlr_shutdown(vxtcom_ctrlr_t *vxtcom, uint64_t wait_cookie,
                             uint64_t shutdown_flags)
{
	vxtcom_cntrl_hdr_t            *ctrlr_shutdown_msg;
	vxtcom_cntrl_signal_closing_t *closing_args;
	void                          *start_msg;

	/*
	 * Prepare the message requesting to remove
	 * an endpoint on the remote controller.
 	 * "unplug a device from the vxt controller"
	 *
	 */

	UMI_LOG(433, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", vxtcom);

	/*
	 * Make sure we own the buffer
         */
	if ((vxtcom->bus.outq_header.request_in_use) || 
	    (vxtcom->bus.outq == NULL)) {
		/*
		 * Fail call
		 */
		return VXT_FAIL;
	}
	vxtcom->bus.outq_header.request_in_use = 1;
	ctrlr_shutdown_msg = 
	   (vxtcom_cntrl_hdr_t *)
	   (((char *)vxtcom->bus.outq) + vxtcom->bus.request_offset);
	start_msg = (void *)ctrlr_shutdown_msg;
	
	/*
	 * Make call to transport authority to establish event
	 */
	ctrlr_shutdown_msg->message_type = VXTCOM_CNTRL_BUS_CLOSING;
	ctrlr_shutdown_msg->message_id = vxtcom->bus.outq_header.msg_id;
	ctrlr_shutdown_msg->remote_dom = vxtcom->ctrlr_list.rdom;
	ctrlr_shutdown_msg->connecting_dom = 0;
	ctrlr_shutdown_msg->config_dom = vxtcom->ctrlr_list.rdom;
	ctrlr_shutdown_msg->dev_type = 0;     /* N/A, ctrlr call */
	ctrlr_shutdown_msg->local_cp = 0;     /* N/A, ctrlr call */
	ctrlr_shutdown_msg->remote_cp = 0;    /* N/A, ctrlr call */
	ctrlr_shutdown_msg->remote_ep[0] = 0; /* N/A, ctrlr call */
	ctrlr_shutdown_msg->datalen = sizeof(vxtcom_cntrl_signal_closing_t);

	closing_args = (vxtcom_cntrl_signal_closing_t *)
	               (ctrlr_shutdown_msg + 1);
	closing_args->create_wait_cookie = wait_cookie;
	closing_args->shutdown_flags = shutdown_flags;

	/*
	 * Push rem_request pointer beyond the present request
	 * and set the next descriptor to the NULL message.
	 */
	ctrlr_shutdown_msg = (vxtcom_cntrl_hdr_t *)
	                     (((vxtarch_word)closing_args) 
	                      + sizeof(vxtcom_cntrl_signal_closing_t));

	vxtcom->bus.request_offset += 
	   (uint32_t)(((vxtarch_word)ctrlr_shutdown_msg) - 
	              (vxtarch_word)start_msg);

	/*
	 * Indicate termination of messages to 
	 * transport authority.
	 */
	UMI_LOG(434, VXTPRINT_DEBUG_LEVEL,
	        "null controller shutdown addr %p\n", ctrlr_shutdown_msg);
	ctrlr_shutdown_msg->message_type = VXTCOM_CNTRL_NULL;
	vxtcom->pending_send = 1;
	/*
	 * Do not wait for the response
	 * let the caller check/wait for it
	 */

	return VXT_SUCCESS;
}
			

/*
 *
 * vxtcfg_config_closing_response:
 *
 * reply - Message Parse: respond to the remote closing controller request
 * signal.  The local action is to turn off all new device creation 
 * attempts.  
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS - Upon successful completion
 *		VXT_FAIL - Message is not properly structured
 *
 */

int
vxtcfg_config_closing_response(vxtcom_ctrlr_t *instance, 
                               vxtcom_cntrl_hdr_t *closing_msg)
{
	vxtcom_cntrl_signal_closing_t *closing_args;
	uint64_t    shutdown_flags;

	ASSERT((closing_msg->message_type & VXTCOM_CNTRL_BUS_CLOSING)
	       == VXTCOM_CNTRL_BUS_CLOSING);

	UMI_LOG(435, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", instance);



	if (closing_msg->datalen != sizeof(vxtcom_cntrl_signal_closing_t)) {
		UMI_LOG(436, VXTPRINT_PRODUCT_LEVEL,
	                "Serious error, data field size mismatch\n");
		vxtcfg_msg_response(instance, closing_msg, 0, VXT_PARM);
                vxtcfg_signal_config_queue(instance);
                return VXT_FAIL;
        }

	closing_args = (vxtcom_cntrl_signal_closing_t *)(closing_msg + 1);
	shutdown_flags = closing_args->shutdown_flags;

	UMI_LOG(437, VXTPRINT_DEBUG_LEVEL,
	        "signaling imminent controller shutdown\n");
	/*
	 * Turn off new device creation
	 * and indicate the the controller is shutting down
	 */
	instance->pending_shutdown = 1;
	vxtcom_ctrlr_event++;
	vxtcom_ctrlr_poll_events |= POLL_VXT_CTRLR_DIED;


	UMI_LOG(438, VXTPRINT_DEBUG_LEVEL,
	        "signal closing controller succeeded\n");
	vxtcfg_msg_response(instance, closing_msg,
	                    closing_args->create_wait_cookie, VXT_SUCCESS);

	vxtcfg_signal_config_queue(instance);
	return VXT_SUCCESS;
}


/*
 *
 * vxtcfg_config_closing_reply:
 *
 * Reply - Message Parse:
 *
 * vxtcfg_config_closing_reply is called on message reply when the
 * message parser determines message type is the closing controller
 * request with the reply bit set.
 *
 * vxtcfg_config_closing_reply handles successful and failed controller
 * closing attempts.
 *
 * In the case of a successful closing request, the remote controller
 * new device actions have been turned off and the remote controller is
 * prepared for shutdown.
 *
 */

int
vxtcfg_config_closing_reply(vxtcom_ctrlr_t *instance,
                            vxtcom_cntrl_hdr_t *closing_msg)
{
	vxtcom_cntrl_error_t  *error_response_field;

	UMI_LOG(439, VXTPRINT_PROFILE_LEVEL,
	        "called, controller %p\n", instance);
	ASSERT((closing_msg->message_type & 
	        (VXTCOM_CNTRL_BUS_CLOSING | VXTCOM_CNTRL_RESPONSE)) ==
	       (VXTCOM_CNTRL_BUS_CLOSING | VXTCOM_CNTRL_RESPONSE));


	if (closing_msg->message_type & VXTCOM_CNTRL_ERROR) {
		error_response_field = (vxtcom_cntrl_error_t *)
		                       (closing_msg + 1);
		vxtcom_closing_ctrlr_complete(instance,
		                              error_response_field->event_port,
		                              error_response_field->error);
		return error_response_field->error;
	} else {
		error_response_field = (vxtcom_cntrl_error_t *)
		                       (closing_msg + 1);

		/*
		 * remove complete usually wakes the
		 * sleeping device remove call to finish processing
		 * an  remove device IOCTL request.
		 */
		vxtcom_closing_ctrlr_complete(instance,
		                              error_response_field->event_port,
		                              VXT_SUCCESS);
		return VXT_SUCCESS;
	}
}


/*
 *
 * vxtcfg_sync_status_return:
 *
 * Callback version of the vxtcfg_syncrhonous_reply
 * call.  This wrapper, takes a void * which it
 * checks for null and then set to the sync_callback
 * structure before calling vxtcfg_synchronous_reply
 *
 */

void
vxtcfg_sync_status_return(void *sync_response, int status)
{
	vxtcfg_sync_callback_t *parms;

	UMI_LOG(143, VXTPRINT_PROFILE_LEVEL, "called, status %08x\n", status);
	if (sync_response == NULL) {
		return;
	}

	parms = (vxtcfg_sync_callback_t *)sync_response;

	vxtcfg_synchronous_reply(parms, status);

	return;
}
