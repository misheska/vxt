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

#define _VXT_COMPONENT_ 8
#define _VXT_SUBSYSTEM_ 24


#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
/*
 * The following files will eventually be in /usr/include/linux
 */
#include <public/kvm/temp/virtio_config.h>
#include <public/kvm/temp/virtio.h>

#include <public/vxt_system.h>
#include <public/kvm/vxt_virtio_cfg.h>
#include <public/kernel/bus/kvm/vxtcom_bus_ctrlr.h>
#include <public/kernel/vxt_module.h>
#include <public/kernel/vxt_signal.h>

#include <vxtcard/linux/kvm/vxt_com_kvm.h>


vxtbus_ctrlr_t *temporary_global;

/*
 *
 * virtio_vxt_guest.c
 *
 * This is the KVM/PCI vxt card driver.  It responds to a PCI probe
 * and talks to the VxT-Virtio-PCI device in the KVM base.  This conversation
 * leads to the exchange of buffers, interrupts, and doorbells connecting
 * the virtio_vxt_guest driver with the VxT bus.  The VxT bus is embedded in
 * the Base Linux Hypervisor where it allows the plugging of high performance
 * device emulations and the wrapping of IOV and other virtual aware hardware.
 */

vxtbus_common_api_t vxt_cmds;

DEFINE_SPINLOCK(config_io_lock);


/*
 *
 * vxt_bus_event_signal
 *
 * The method of signaling the remote differs between the guest and the
 * host.  In the case of the host, the method relies on file descriptors
 * set up with kvm bound agents waiting to assert signals on the targeted
 * guest.  In the guest case, signaling of the host is done through
 * virtio queue kick operations.  Therefore, to keep the vxt_card code
 * common between guest and host we call routines loaded in the site
 * specific modules.
 *
 * Returns:
 *		None
 *
 */

void
vxt_bus_event_signal(void *sig)
{
        struct virtqueue *vq = sig;

        UMI_LOG(0, VXTPRINT_DEBUG2_LEVEL, " called, event_sig %p\n", sig);
        vq->vq_ops->kick(vq);

}


/*
 *
 * vxt_bus_management_hub
 *
 * vxt_bus_management_hub is a module level routine that indicates whether
 * or not the loaded vxt module is a hub or a spoke.  As a hub, the 
 * caller has access to vxt system management facilities and is part of
 * the trusted computing base.  As a spoke the caller is part of the 
 * guest OS environment.
 *
 * Returns:
 *		0 - When this file is linked the vxt_card module is
 *		    resident in the guest.
 *
 */	

int
vxt_bus_management_hub(void)
{
 	return 0;
}


/*
 * Local Helper routines
 */


/*
 *
 * vxt_send_request:
 *
 * vxt_send_request pushes the built-in request buffer
 * on to the VxT virtio configuration queue.  If the queue is busy
 * the routine returns.  The queue state is reflected in the response_pending
 * field.  This field guarantees that the data in the request buffer will
 * eventually be sent.  When the req_complete call finishes its handling
 * it checks this field.  If it is set, it calls vxt_send_request.
 *
 * Returns:
 *		VXT_SUCCESS - In all cases
 *
 */

static int
vxt_send_request(vxtbus_ctrlr_t *instance)
{
	struct scatterlist sg[1];
	vxt_virtio_cfg_t *request;
	struct virtqueue *vq;
	int ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL," called, bus %p\n", instance);
	request = instance->request;
	vq = instance->send_vq;
	sg_init_one(sg, request, sizeof(vxt_virtio_cfg_t));

	if (instance->response_pending == 1) {
		/*
		 * Wakeup the VxT PCI device and tell it there is some
		 * pending work.  Go to sleep with request pending
		 */
		instance->request_pending = 1;
/*
		vq->vq_ops->kick(vq);
*/
		return  VXT_SUCCESS;
	}
	ret = vq->vq_ops->add_buf(vq, sg, 1, 0, (void *)instance);
	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL," add_buf invoked, "
	        "return = %d\n", ret);
	if (ret >= 0) {
		instance->request_pending = 0;
		instance->response_pending = 1;
		vq->vq_ops->kick(vq);
        } else {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "VxT controller config traffic may seize");
		/*
		 * Signal the queue, you never know
		 */
		vq->vq_ops->kick(vq);
		instance->request_pending = 1;
	}

	return VXT_SUCCESS;

}


/*
 *
 * vxt_cfg_transition_to_initialized:
 *
 *
 */

static int
vxt_cfg_transition_to_initialized(vxtbus_ctrlr_t *instance)
{
	vxt_virtio_cfg_t *request =  (vxt_virtio_cfg_t *)instance->request;
	int ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL," called, bus %p\n", instance);
	if (instance->request_pending) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Request pending on unused Virtio queue");
		return VXT_RSRC;
	}
	/*
	 * Request a transition from  VXT_CONFIGURED to VXT_INITIALIZED
	 */
	request->current_state = VXT_CONFIGURED;
	request->provisional_state = VXT_INITIALIZED;
	request->cmd.configured.configuring_status = VXT_SUCCESS;

	ret = vxt_send_request(instance);

	if (ret) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Unable to send Virtio VxT config request");
	}

	return ret;
}


/*
 *
 * vxt_cfg_transition_to_quitted:
 *
 * Indicated that the guest drivers has quitted the field
 *
 *
 */

static int
vxt_cfg_transition_to_quitted(vxtbus_ctrlr_t *instance)
{
	vxt_virtio_cfg_t *request =  (vxt_virtio_cfg_t *)instance->request;
	int ret;

	if (instance->request_pending) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Request pending on unused Virtio queue");
		return VXT_RSRC;
	}
	/*
	 * Request a transition from  VXT_RUNNING to VXT_QUITTED
	 */
	request->current_state = VXT_RUNNING;
	request->provisional_state = VXT_QUITTED;
	request->cmd.configured.configuring_status = VXT_SUCCESS;

	ret = vxt_send_request(instance);

	if (ret) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Unable to send Virtio VxT config request");
	}

	return ret;
}


/*
 *
 * vxt_cfg_transition_to_shutdown:
 *
 *
 */

static int
vxt_cfg_transition_to_shutdown(vxtbus_ctrlr_t *instance)
{
	vxt_virtio_cfg_t *request =  (vxt_virtio_cfg_t *)instance->request;
	int ret;

	if (instance->request_pending) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Request pending on unused Virtio queue");
		return VXT_RSRC;
	}
	/*
	 * Request a transition from  VXT_REVOKING to VXT_WINDING_DOWN
	 */
	request->current_state = VXT_REVOKING;
	request->provisional_state = VXT_WINDING_DOWN;
	request->cmd.configured.configuring_status = VXT_SUCCESS;

	ret = vxt_send_request(instance);

	if (ret) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Unable to send Virtio VxT config request");
	}

	return ret;
}


/*
 *
 * vxt_bus_cfg_intr:
 *
 * vxt_bus_cfg_intr is the interrupt handler for configuration event
 * interrupts generated by the VxT Bus located in the Linux Base, (hypervisor)
 * Config events include VxT device plug and unplug events.
 *
 * Returns:
 *
 *		IRQ_HANDLED - Returned in all instances
 *		vxtcom_bus_cfg_intr does not support daisy
 *		chaining.
 *
 */

static irqreturn_t
vxt_bus_cfg_intr(int irq, void *args)
{
        vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)args;
	unsigned long  flags;

	UMI_LOG(0, VXTPRINT_ALPHA_LEVEL," called, bus %p\n", instance);
	if (instance == NULL) {
		UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
		        "interrupt called on dead device\n");
		return IRQ_HANDLED;
	}
	spin_lock_irqsave(&config_io_lock, flags);

	if (unlikely(instance->connected != VXTCOM_STATE_CONNECTED)) {
		UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL,
                        "spurious interrupt, bus: %p not connected.  "
                        "Delay delivery until controller initialization "
                        "is completed\n", instance);
                instance->pending_config_event = 1;
                spin_unlock_irqrestore(&config_io_lock, flags);
                return IRQ_HANDLED;
        }

	/*
	 * We do not have event counting, one signal will handle all
	 * pending requestors.  (Assuming no spurious interrupts,
	 * by design there should be only one anyway)
	 */
	instance->pending_config_event = 0;

	/*
	 * Schedule config message queue parsing
	 * and parse actions
	 */
	schedule_work(&instance->config_work);
	UMI_LOG(0, VXTPRINT_DEBUG2_LEVEL ,"return from schedule work\n");

        spin_unlock_irqrestore(&config_io_lock, flags);
	
	return IRQ_HANDLED;
}


/*
 *
 * vxt_bus_sig_intr:
 *
 * vxt_bus_sig_intr handles composite signal interrupts generated by the
 * VxT Bus.  The composite signal is teased out by consulting the signal
 * buffer shared memory.  Elements there indicated interrupts to specific 
 * VxT devices.
 *
 * Returns:
 *
 *		IRQ_HANDLED - Returned in all instances
 *		vxtcom_bus_sig_intr does not support daisy
 *		chaining.
 *
 *
 */

static irqreturn_t
vxt_bus_sig_intr(int irq, void *args)
{
        vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)args;
        /*
         * This profile log is debug2 because it is on the
         * data path
         */
        UMI_LOG(0, VXTPRINT_DEBUG2_LEVEL," called, bus %p\n", instance);
	if (unlikely(instance->connected != VXTCOM_STATE_CONNECTED)) {
		UMI_LOG(0, VXTPRINT_BETA_LEVEL,
		        " spurious interrupt, bus %p\n", instance);
		return IRQ_HANDLED;
	}
	/*
	 * Process the signals generated by the remote
	 * controller
	 */
	vxt_cmds.sig_interrupt(instance->dev_events, instance->sig_read);
	

	return IRQ_HANDLED;
}


static ssize_t vxt_show_version(struct class *class, char *buf)
{
	sprintf(buf, "%s\n",  VXT_CARD_BANNER);
	return strlen(buf) + 1;
}

static vxt_device_class_t *vxt_class = NULL;
CLASS_ATTR(vxtcard_version, S_IRUGO, vxt_show_version, NULL);

/*
 * 
 * get_vxt_class:
 *
 * get_vxt_class provides a handle to a registered subclass in the device
 * file system.  This corresponds to a file in a directory under /sys/class
 *
 * The get_vxt_class function is considered part of the xen layer because
 * we sublimate VxT registration under the xen class when it exists.
 *
 * For guests that do not have a registered set of xen driver and hence
 * do not have a Xen class, we substitute ourselves at this level.
 *
 *
 * Return:
 *		A handle to the VxT class, or NULL
 */

vxt_device_class_t *get_vxt_class(void)
{
	int ret;

        if (vxt_class) {
                return vxt_class;
	}

#ifdef LEGACY_4X_LINUX
	vxt_class = class_simple_create(THIS_MODULE, "vxt");
#else
        vxt_class = class_create(THIS_MODULE, "vxt");
#endif

	if (IS_ERR(vxt_class)) {
		UMI_LOG(136, VXTPRINT_PRODUCT_LEVEL,
		        "Failed to create xen sysfs class\n");
		vxt_class = NULL;
	}

	/*
	 * Create a file attribute for version
 	 */
#ifndef LEGACY_4X_LINUX
	ret = class_create_file(vxt_class, &class_attr_vxtcard_version);
	if (ret) {
        	UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL,
		        " class_create_file failed, ret = 0x%x\n", ret);
	}
#endif

        return vxt_class;
}


/*
 *
 * vxt_bus_instance_free:
 *
 * Free the resources allocated for the vxt bus instance. 
 *
 * Note: This call is separated from vxt_bus_remove because
 * there are times during initialization when all or most of
 * the resources for vxt_bus have been allocated but no configuration
 * action with respect to the VxT/Virtio Bridge device have been
 * taken.
 *
 * Returns:
 *
 *		None
 *
 */

static void
vxt_bus_instance_free(vxtbus_ctrlr_t *instance, int suspend)
{
	struct virtqueue     *rcv_vq = (struct virtqueue *)instance->rcv_vq;
	struct virtqueue     *send_vq = (struct virtqueue *)instance->send_vq;
	struct virtio_device *vdev = instance->vdev;

        UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
                " called, bus %p, suspend %08x\n",
                instance, suspend);
	instance->connected = (suspend ? VXTCOM_STATE_SUSPENDED
	                                 : VXTCOM_STATE_DISCONNECTED);
	flush_scheduled_work();

	/*
	 * Stop the virtual queues activity at the device end
	 */
	if (suspend != 2) {
		/*
		 * If we are running suspend mode "2" we
		 * are stopping operations because the VxT kernel module
		 * in the base Linux hypervisor will not be available
		 * However, we will keep all of our communications lines
		 * open with the Virtio-VxT controller
		 */
		vdev->config->reset(vdev);
		/*
		 * Delete the Virtio config and signaling queues.
		 */
		if ((send_vq != NULL) 
		    || (rcv_vq != NULL)) {
	        	vdev->config->del_vqs(vdev);
			send_vq = NULL;
			rcv_vq = NULL;
		}
	}

	if (instance->response_buf != NULL) {
		kfree(instance->response_buf);
		instance->response_buf = NULL;
	}

	if (instance->cfg_dummy_buf != NULL) {
		kfree(instance->cfg_dummy_buf);
		instance->cfg_dummy_buf = NULL;
	}

	if (instance->sig_dummy_buf != NULL) {
		kfree(instance->sig_dummy_buf);
		instance->sig_dummy_buf = NULL;
	}

	if (instance->request != NULL) {
		kfree(instance->request);
		instance->request = NULL;
	}

/* doorbell is now pci mmio update this code
	if (instance->doorbell != NULL) {
		free_page((long)instance->doorbell);
		instance->doorbell = NULL;
	}
*/

	if (instance->inq != NULL) {
		free_page((long)instance->inq);
		instance->inq = NULL;
	}


	if (instance->outq != NULL) {
		free_page((long)instance->outq);
		instance->outq = NULL;
	}

	if (instance->sig_buf != NULL) {
		free_page((long)instance->sig_buf);
		instance->sig_buf = NULL;
	}

#ifdef UNWRAPPED_IRQS
	free_irq(instance->cfg_irq, instance);
	free_irq(instance->sig_evt, instance);
#endif

	return;

}


/*
 *
 * vxt_bus_disconnect:
 *
 */

int
vxt_bus_disconnect(vxtbus_ctrlr_t *instance)
{
	int ret;

        ret = vxt_cmds.remove_ctrlr_pair(instance, instance->remote_domid);

        if (ret) {
/* kvm equivalent?
                dev->dev.driver_data = NULL;
*/
                UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
                         "not on active controller list\n");
        }


        vxt_bus_instance_free(instance, 0);

        /*
         * Destroy the controller
         */
        vxt_cmds.dec_bus_ref(instance);

	return VXT_SUCCESS;
}


/*
 * 
 * vxt_bus_remove:
 *
 * vxt_bus_remove is a callback published as part of the virtio 
 * controller structure passed during virtio driver registration
 * vxt_bus_remove is called when de-registration of the driver
 * is indicated.
 *
 * vxt_bus_remove can destroy the connection of the vxt_virtio
 * controller context with the virtio/pci bus.  However, existing
 * relationships with the VxT controller context may persist.  For
 * this reason, the controller is reference counted.  If there are
 * extant relationships, an elevated reference count will allow the
 * moribund VxT controller structure to linger.
 *
 * In order to stop the creation of new relationships, vxt_bus_remove
 * calls the common VxT controller module to disable new device creation,
 * shutdown existing devices, and de-register the controller instance. 
 * After an orderly shutdown of on-going traffic and disengagement of
 * communications links, the Virtio Bus status is changed to reflect
 * shutdown status.  This triggers the Virtio/VxT device to begin
 * the process of VxT bus device and VxT back-end controller device
 * shutdown.
 *
 * After return, the hold on the local common VxT controller instance
 * is dropped and resource freeing of common elements can take place
 * when latent caller's requests return.
 *
 *
 * Returns:
 *
 *		None
 *
 */

static void
vxt_bus_remove(struct virtio_device *vdev)
{
        vxtbus_ctrlr_t *instance = vdev->priv;
	int            ret;

	vdev->priv = NULL; /* avoid being called twice */

        if (instance == NULL) {
		UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
		        "vxtcom_bus_remove called on NULL controller");
                return;
        }
	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        "vxtcom_bus_remove called: %p removed\n", instance);

        vxt_cmds.bus_disconnect(instance);

        flush_scheduled_work();

        vxt_cmds.bus_unplug_devices(instance, 1);


/*  CDY KVM equivalent to MORIBUND?  */
        if ((instance->connected == VXTCOM_STATE_CONNECTED) ||
            (instance->connected == VXTCOM_STATE_MORIBUND)) {
                UMI_LOG(0, VXTPRINT_BETA_LEVEL,"switch state to Closing\n");
		/*
		 * Once we send the VXT_QUITTED provisional state, the
		 * remote knows we cannot respond to VIRTIO traffic anymore
		 */
		vxt_cfg_transition_to_quitted(instance);
        }

	/*
	 * can be invoked from a extended handshake if the extra
	 * step is needed for quiece.
	 */
	ret = vxt_bus_disconnect(instance);

}


/*
 *
 * vxt_supply_dummy_bufs:
 *
 * Called to load a buffers into the Virtio queues associated with
 * the VxT Bus Config IRQ and the VxT device action IRQ.  These queues
 * are not used for the communication between the VxT Bus and this
 * driver.  Internal shared memory and a separate queue protocol are
 * employed.  The queues must have an active element on them however.
 * Virtio_pci has a handler that checks for data before calling the
 * relevant callback on the IRQ event.
 *
 * Returns:
 *
 * 		VXT_SUCCESS => Upon successful invocation
 *		VXT_FAIL - Could not load buffer into virtio queue
 */

static int
vxt_supply_dummy_bufs(vxtbus_ctrlr_t *instance)
{
	struct virtqueue *cfg_vq = (struct virtqueue *)instance->cfg_cb;
	struct virtqueue *sig_vq = (struct virtqueue *)instance->sig_cb;
	struct scatterlist sg[1];

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL," called\n");
	sg_init_one(sg, instance->cfg_dummy_buf, PAGE_SIZE);
        if (cfg_vq->vq_ops->add_buf(cfg_vq, sg, 0, 1,
	                            instance) != 0) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "failure loading buffer into the cfg_dummy virtio"
		          "queue VxT device config traffic may seize");
		return VXT_FAIL;
	}

	sg_init_one(sg, instance->sig_dummy_buf, PAGE_SIZE);
        if (sig_vq->vq_ops->add_buf(sig_vq, sg, 0, 1,
	                            instance) != 0) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "failure loading buffer into the sig_dummy virtio"
		          "queue VxT device config traffic may seize");
		return VXT_FAIL;
	}
        cfg_vq->vq_ops->kick(cfg_vq);
        sig_vq->vq_ops->kick(sig_vq);
	/*
	 * No need to signal the other side here, the queues are dummys
         * cfg_vq->vq_ops->kick(cfg_vq);
         * sig_vq->vq_ops->kick(sig_vq);
	 */

	return VXT_SUCCESS;

}


/*
 *
 * vxt_supply_response_queue:
 *
 * Called to load a buffer into the in queue of the receive, (response)
 * virtio queue.  Only one buffer is loaded at a time, limiting traffic
 * to one outstanding response.  Most config traffic is in the form of 
 * request response with the front end driver initiating the conversation.
 * The asynchronous alerts that the VxT back-end, (device emulation)
 * send can wait behind normal response processing.
 *
 * Returns:
 *
 * 		VXT_SUCCESS => Upon successful invocation
 *		VXT_FAIL - Could not load buffer into virtio queue
 */

static int
vxt_supply_response_queue(vxtbus_ctrlr_t *instance)
{
	struct virtqueue *rcv_vq = (struct virtqueue *)instance->rcv_vq;
	struct scatterlist sg[1];
	sg_init_one(sg, instance->response_buf, PAGE_SIZE);

        if (rcv_vq->vq_ops->add_buf(rcv_vq, sg, 0, 1,
	                            instance) != 0) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "failure loading buffer into the response virtio"
		          "queue VxT controller config traffic may seize");
		return VXT_FAIL;
	}
        rcv_vq->vq_ops->kick(rcv_vq);

	return VXT_SUCCESS;

}


/*
 *
 * vxt_initialize_bus:
 *
 * Initialize the VxT Card bus interface.  Find queue locations in
 * main memory for the Device configuration queues and signal queues.
 * Send the Initialization request to recover the IRQ's and Doorbell
 * For communication with the Hypervisor based VxT Bus backend.
 *
 * Note: a failure response here leads to destruction of the VxT bus
 * instance and recovery of its resources.  All successfully allocated
 * resources are associated with the instance and will be released
 * upon its destruction.
 *
 */

static int
vxt_initialize_bus(struct virtio_device *vdev, vxtbus_ctrlr_t *instance)
{
	vxt_virtio_cfg_t *request;
	int ret;
	
	instance->response_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (instance->response_buf == NULL) {
		return VXT_RSRC;
	}
	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,"Response buf allocated 0x%p\n",
	        instance->response_buf);

	instance->cfg_dummy_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (instance->cfg_dummy_buf == NULL) {
		kfree(instance->response_buf);
		return VXT_RSRC;
	}
	instance->sig_dummy_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (instance->sig_dummy_buf == NULL) {
		kfree(instance->response_buf);
		kfree(instance->cfg_dummy_buf);
		return VXT_RSRC;
	}
	/*
	 * Load a solitary buffer into the response virtual queue
	 * Only one outstanding response is needed at a time.
	 */
	ret = vxt_supply_response_queue(instance);

	/*
	 * Load Dummy buffers onto the VxT Bus Config and 
	 * Vxt Bus Device signal queues.  These buffers must
	 * be present and appear to have data for the
	 * IRQ interrupts to be passed onto our handlers.
	 * (Unwrapped IRQs passed directly through the Virtio
	 * layer would be less cumbersome)
	 */
	ret = vxt_supply_dummy_bufs(instance);

	/*
	 * Pick up a buffer to use for Virtio PCI config requests
	 */
	request = kmalloc(sizeof(vxt_virtio_cfg_t), GFP_KERNEL);
	if (request == NULL) {
		kfree(instance->response_buf);
		kfree(instance->cfg_dummy_buf);
		kfree(instance->sig_dummy_buf);
		return VXT_RSRC;
	}
	instance->request = (void *)request;

	/*
	 * Pick up mmio addresses of VxT Bus Config and VxT device doorbells
	 */
/* doorbell is now pci mmio  update CDY
hmm, will have to kludge to get pci_dev, pci_request_regions already done
	ret = pci_request_regions(vdev->pci_dev, "
	instance->doorbell = (char *)get_zeroed_page(GFP_KERNEL);

	if (instance->doorbell == NULL) {
		return VXT_RSRC;
	}
*/

	/*
	 * Allocate VxT Bus Configuation buffers and Signal buffer
	 */
	instance->inq = (char *)get_zeroed_page(GFP_KERNEL);
	if (instance->inq == NULL) {
		return VXT_RSRC;
	}
	instance->outq = (char *)get_zeroed_page(GFP_KERNEL);
	if (instance->outq == NULL) {
		return VXT_RSRC;
	}
	instance->sig_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (instance->sig_buf == NULL) {
		return VXT_RSRC;
	}
	/*
	 * Protection against spurious interrupts on
	 * initial controller CONNECTED state transition
	 *
	 * It is possible to receive a spurious signal.  It
	 * is not possible to detect and block all spurious
	 * signals.  Therefore the system has been made to
	 * forgiving in this regard.  Config queue processing
	 * checks the request field of each request block.
	 * If this request field reads VXTCOM_CNTRL_NULL
	 * no action is taken and the call returns.
	 *
	 */

	/*
	 * The signal buffer is split into two pieces
	 * allowing simultaneous signalling in both
	 * directions.
	 *
	 * We initialize the part that we will treat
	 * as our send, or signalling buffer.
	 */
	instance->sig_read = (void *)(((char *)instance->sig_buf) +
	                     VXTSIG_SEC_BUFF_OFFSET);
	instance->sig_write = instance->sig_buf;
	vxt_cmds.sig_init_send_buf(instance->sig_write);

	/*
	 * Initialize the VxT bus configuration queues
	 */
	vxt_cmds.initialize_send_queue((void *)instance->outq, PAGE_SIZE);
	vxt_cmds.initialize_send_queue((void *)instance->inq, PAGE_SIZE);



	/*
	 * Request a transition from  VXT_PROVISIONED to VXT_CONNECTING
	 */
	request->current_state = VXT_PROVISIONED;
	request->provisional_state = VXT_CONNECTING;
	request->cmd.connecting.cfg_in =
	   virt_to_phys(instance->inq);
	request->cmd.connecting.cfg_out =
	   virt_to_phys(instance->outq);
	request->cmd.connecting.signal =
	   virt_to_phys(instance->sig_buf);
	request->cmd.connecting.cfg_in_size = PAGE_SIZE;
	request->cmd.connecting.cfg_out_size = PAGE_SIZE;
	request->cmd.connecting.signal_buf_size = PAGE_SIZE;
/*
need pci mmio addresses for sig_bel and cfg_bell
	request->cmd.connecting.doorbell = 
	   virt_to_phys(instance->doorbell);
*/

	if (instance->request_pending) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Request pending on unused Virtio queue");
		return VXT_RSRC;
	}
	ret = vxt_send_request(instance);

	if (ret) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Unable to send Virtio VxT config request");
	}

	return VXT_SUCCESS;
}


/*
 *
 * vxt_parse_config_response:
 *
 * vxt_parse_config_response is the implementation of the response
 * side of the state machine for Virtio-PCI VxT controller configuration.
 * It is used to transition the controller from boot to Configured and ready
 * for VxT device plug-in.  It is also used to transition through suspend and
 * recover actions as well as controller unplug events and driver unload.
 *
 */

static int
vxt_parse_config_response(vxtbus_ctrlr_t *instance)
{
	vxt_virtio_cfg_t *response;
	int              ret;

	response = (vxt_virtio_cfg_t *)instance->response_buf;

	switch ((int)response->current_state) {
	case VXT_PROVISIONED:
	{
		struct virtio_device *vdev = instance->vdev;
		/*
		 * Called when service is restored after VxT base
		 * linux driver has been off line.  i.e. The Virtio-VXT
		 * device did not undergo a state change so we don't get
		 * a probe signal.
		 */
		vxt_cmds.bus_connect(instance);
		instance->connected = VXTCOM_STATE_DISCONNECTED;
		ret = vxt_initialize_bus(vdev, instance);
		if (ret) {
			vxt_bus_instance_free(instance, 0);
			vxt_cmds.dec_bus_ref(instance);
			return -ENXIO;
		}
		break;
	}
	case VXT_CONFIGURED:
	{
		/*
		 * Note: Probe kicks off configuration, VXT_CONFIGURED
		 * is triggered by the driver requesting configuration
		 * out of the probe routine, by sending a request to
		 * enter the "CONNECTING" state.
		 */
		UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"VxT cfg bus state: "
		        "VXT_CONFIGURED\n");
		if(response->provisional_state != VXT_CONFIGURED) {
			/*
			 * An error was encountered.
			 */
			UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
			          "Failed to initialize Virtio VxT controller "
			          "Error moving to VXT_CONFIGURED");
			return VXT_FAIL;
		}
		if (response->version != VIRTIO_VXT_VERSION) {
			UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
			          "Version Mismatch: Theirs 0x%llx, "
			           "Ours 0x%x\n",
			           response->version, VIRTIO_VXT_VERSION);

		}
		if ((instance->connected != VXTCOM_STATE_DISCONNECTED) && 
		    (instance->connected != VXTCOM_STATE_SUSPENDED))  {
			UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
			          "VxT controller state mismatch with "
				  "connection provisional status of "
			          "VXT_CONFIGURED:  expected "
			          "VXTCOM_STATE_DISCONNECTED or "
			          "VXTCOM_STATE_SUSPENDED got 0x%x\n",
			          instance->connected);
			return VXT_FAIL;
		}
		instance->cfg_irq = response->cmd.configuring.cfg_irq;
		instance->sig_evt = response->cmd.configuring.sig_irq;
		instance->kvm = (void *)(vxtarch_word)
		                response->cmd.configuring.guest_name;
		strncpy(instance->local_uuid,
		        response->cmd.configuring.guest_uuid, MAX_VXT_UUID);
		strncpy(instance->remote_uuid,
		        "00000000-0000-0000-0000-000000000000", MAX_VXT_UUID);



		/* 
		 * Connect up our handlers for config and signal
		 * interrupts from the VxT bus.
		 */
#ifdef UNWRAPPED_IRQS
		/* 
		 *  Leave out until we have an un-wrapped irq
		 *  For now we depend on the virtio queue callback
		 *  to get our signal
		 */
		ret = request_irq(instance->cfg_irq,
		                  vxt_bus_cfg_intr, 0, "vxt_bus_cfg",
		                  instance);
		if (ret) {
			UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
			          "VxT cfg bus irq initialization failure\n");
			return VXT_FAIL;
		}

		ret = request_irq(instance->sig_evt,
		                  vxt_bus_sig_intr, 0, "vxt_bus_sig",
		                  instance);
		if (ret) {
			UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
			          "VxT sig bus irq initialization failure\n");
			free_irq(instance->cfg_irq, instance);
			return VXT_FAIL;
		}
#endif
		/*
		 * Set our local vxt controller to the initialized state
		 */
		instance->connected = VXTCOM_STATE_INITIALIZED;
		vxt_cfg_transition_to_initialized(instance);
		break;

	}
	case VXT_RUNNING:
	{
		UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"VxT cfg bus state: "
		        "VXT_RUNNING\n");
		if((response->provisional_state != VXT_RUNNING) ||
		   (instance->connected != VXTCOM_STATE_INITIALIZED)) {
			/*
			 * An error was encountered.
			 */
			UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
			          "Failed to initialize Virtio VxT controller "
			          "Error moving to VXT_RUNNING\n"
			          "Local connected state = 0x%x, "
			          "hardware provisional state = 0x%x\n",
			          instance->connected, 
			          response->provisional_state);
			return VXT_FAIL;
		}
schedule_work(&instance->bus_activate);
/*
		vxt_cmds.bus_connect(instance);

		UMI_LOG(0, VXTPRINT_BETA_LEVEL,
		        "switching to running state\n");
		instance->connected = VXTCOM_STATE_CONNECTED;

		if (instance->pending_config_event == 1) {
			vxt_bus_cfg_intr(instance->cfg_irq, (void *)instance);
		}
*/


		break;
	}
	case VXT_REVOKING:
	{
		/*
		 * The Virtio-VxT card is being unplugged
		 * Flush all existing work and turn off new
		 * device creation.  Tell the Virtio that 
		 * it can shutdown when all of the outstanding
		 * work is done.
		 */
		UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"VxT cfg bus state: "
		        "VXT_REVOKING\n");
                vxt_cmds.bus_disconnect(instance);

                if (instance->user_ref_cnt > 0) {
			UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
			          "Device in use; refusing to close");
		} else {
			vxt_cmds.bus_unplug_devices(instance, 1);
			vxt_cmds.remove_ctrlr_pair(instance,
			                           instance->remote_domid);
			vxt_cfg_transition_to_shutdown(instance);
		}
		break;

	}
	case VXT_SUSPENDING:
	{
		UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"VxT cfg bus state: "
		        "VXT_SUSPENDING\n");
                vxt_cmds.bus_disconnect(instance);
		vxt_cmds.bus_unplug_devices(instance, 1);
		if(response->cmd.suspending.flags & VXT_SUSPEND_VXT_BUS) {
			/*
			 * We're not losing contact with the Virtio-VxT
			 * controller
			 */
			vxt_bus_instance_free(instance, 2);
		} else {
			/*
			 * We are expecting a remove event when the
			 * hardware, i.e. the Virtio-VxT device goes
			 * away.  No need to do anything here
			 */
		}
	}
	case VXT_SHUTDOWN:
	{
		struct virtio_device *vdev = instance->vdev;
		UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"VxT cfg bus state: "
		        "VXT_SHUTDOWN\n");
		/*
		 * The controller is giving us the go-ahead to free up the
		 * vxt context on this side.
	 	 */
		vxt_bus_remove(vdev);
		break;
	}
	default:
		UMI_ERROR(0, VXTPRINT_PROFILE_LEVEL,"VxT cfg bus state: "
		        "UNKNOWN\n");
		return VXT_FAIL;
	}
	return VXT_SUCCESS;
}



/*
 *
 * vxt_get_rcv_queue_parent:
 *
 * Return the parent structure of the receive virtio queue. Note
 * only a receive queue embedded in a VxT bus structure should be provided
 * as a parameter.
 *
 */

static inline vxtbus_ctrlr_t *
vxt_get_rcv_queue_parent(struct virtqueue *vq)
{
	long offset;

	offset = (long)&(((vxtbus_ctrlr_t *)0)->rcv_vq);
	return (vxtbus_ctrlr_t *) (((uint8_t *)vq) - offset);
}

/*
 *
 * vxt_cfg_response:
 *
 * The Virtio-PCI VxT controller is responding to an earlier
 * request or sending an asynchronous update to our configuration
 * status.
 *
 *	Returns:
 *		None
 */

static void
vxt_cfg_response(struct virtqueue *vq)
{
	vxt_virtio_cfg_t *resp_buf;
	vxtbus_ctrlr_t *instance;
        unsigned int len;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, "called, vq %p\n", vq);
	instance = vq->vq_ops->get_buf(vq, &len);
	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
	        "recovered instance pointer 0x%p\n", instance);
	if (instance == NULL) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Buffer coming off of receive_vq is NULL\n");
		return;
	}
	resp_buf = instance->response_buf;
	if (len != sizeof(resp_buf)) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Buffer coming off of rcv_vq is not the "
		          "proper size, expected 0x%x, got 0x%x",
		          sizeof(resp_buf), len);
	}
/*
	if (vxt_parse_config_response(instance) == VXT_SUCCESS) {
*/
vxt_parse_config_response(instance);
		vxt_supply_response_queue(instance);
/*
	}
*/

	return;
}


/*
 *
 * vxt_get_send_queue_parent:
 *
 * Return the parent structure of the send virtio queue. Note
 * only sendqueue embedded in a VxT bus structure should be provided
 * as a parameter.
 *
 */

static inline vxtbus_ctrlr_t *
vxt_get_send_queue_parent(struct virtqueue *vq)
{
	long offset;

	offset = (long)&(((vxtbus_ctrlr_t *)0)->send_vq);
	return (vxtbus_ctrlr_t *) (((uint8_t *)vq) - offset);
}



/*
 * 
 * vxt_bus_cfg_cb:
 *
 * vxt_bus_cfg_cb is the interrupt handler for the VxT bus configuration
 * communication channel.  The associated queue however is a VxT exclusive
 * one that is shared between the VxT controller in the base/hypervisor
 * linux and this guest driver instantiation, not the virtio queue that is
 * wrapped with the signal via virtio-pci.  The virtio-pci queue has
 * a dummy element that we leave alone.  This element gives the queue a
 * non-empty status.  The status is checked by the virtio-pci handler before
 * passing the IRQ signal through to this handler.
 *
 * Returns:
 *
 *		None
 */

static void
vxt_bus_cfg_cb(struct virtqueue *vq)
{
	vxtbus_ctrlr_t *instance;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, "called, vq %p\n", vq);
instance = temporary_global;
#ifdef notdefcdy
        unsigned int len;
	struct scatterlist sg[1];
	/*
	 * We have to retrieve the buffer just to get the instance pointer
	 */
	instance = vq->vq_ops->get_buf(vq, &len);
	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
	        "recovered instance pointer 0x%p\n", instance);
	if (instance == NULL) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Buffer coming off of config callback is NULL\n");
		return;
	}

	/*
	 * We've looked at it, put it back
	 */

	sg_init_one(sg, instance->cfg_dummy_buf, PAGE_SIZE);
        if (vq->vq_ops->add_buf(vq, sg, 0, 1,
	                            instance) != 0) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "failure loading buffer into the cfg_dummy virtio"
		          "queue VxT device config traffic may seize");
		return;
	}
#endif

	vxt_bus_cfg_intr(instance->cfg_irq, (void *)instance);
	return;
}


/*
 * 
 * vxt_bus_sig_cb:
 *
 * vxt_bus_sig_cb is the interrupt handler for the VxT bus configuration
 * communication channel.  The associated queue however is a VxT exclusive
 * one that is shared between the VxT controller in the base/hypervisor
 * linux and this guest driver instantiation, not the virtio queue that is
 * wrapped with the signal via virtio-pci.  The virtio-pci queue has
 * a dummy element that we leave alone.  This element gives the queue a
 * non-empty status.  The status is checked by the virtio-pci handler before
 * passing the IRQ signal through to this handler.
 *
 * Returns:
 *
 *		None
 */

static void
vxt_bus_sig_cb(struct virtqueue *vq)
{
	vxtbus_ctrlr_t *instance;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, "called, vq %p\n", vq);
instance = temporary_global;
#ifdef notdefcdy
        unsigned int len;
	struct scatterlist sg[1];
	/*
	 * We have to retrieve the buffer just to get the instance pointer
	 */
	instance = vq->vq_ops->get_buf(vq, &len);
	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
	        "recovered instance pointer 0x%p\n", instance);
	if (instance == NULL) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Buffer coming off of signal callback is NULL\n");
		return;
	}

	/*
	 * We've looked at it, put it back
	 */

	sg_init_one(sg, instance->sig_dummy_buf, PAGE_SIZE);
        if (vq->vq_ops->add_buf(vq, sg, 0, 1,
	                        instance) != 0) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "failure loading buffer into the sig_dummy virtio"
		          "queue VxT device config traffic may seize");
		return;
	}
#endif

	vxt_bus_sig_intr(instance->sig_evt, (void *)instance);
	return;
}

/*
 * vxt_cfg_req_complete:
 *
 */

static void
vxt_cfg_req_complete(struct virtqueue *vq)
{
	vxt_virtio_cfg_t *req_buf;
	vxtbus_ctrlr_t   *instance;
        unsigned int     len;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, "called, vq %p\n", vq);
        instance = vq->vq_ops->get_buf(vq, &len);
	if (instance == NULL) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Buffer coming off of send_vq is NULL\n");
	}
	req_buf = instance->request;
	if (len != sizeof(*req_buf)) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
		          "Buffer coming off of send_vq is not the "
		          "proper size, expected 0x%x, got 0x%x",
		          sizeof(*req_buf), len);
	}
	instance->response_pending = 0;
	if (instance->request_pending) {
		vxt_send_request(instance);
	}

	/*
	 * No other action needed here.  We will get a message from the
	 * other side through the response queue.
	 */

	return;
}


/*
 *
 * parse_config_msg_wrapper:
 *
 * Some Linux versions do not pass a data pointer, we will derefence
 * from the queue here to adapt to our common code interface.
 *
 */

static void
parse_config_msg_wrapper(struct work_struct *work)
{
	vxtbus_ctrlr_t *instance = 
	   container_of(work, vxtbus_ctrlr_t, config_work);

	vxt_cmds.parse_config_msg((void *)instance);

	return;
}


/*
 *
 * bus_activate_msg_wrapper:
 *
 * Some Linux versions do not pass a data pointer, we will derefence
 * from the queue here to adapt to our common code interface.
 *
 */

static void
bus_activate_msg_wrapper(struct work_struct *work)
{
	vxtbus_ctrlr_t *instance = 
	   container_of(work, vxtbus_ctrlr_t, bus_activate);

	vxt_cmds.bus_connect((void *)instance);

	UMI_LOG(0, VXTPRINT_BETA_LEVEL,
	        "switching to running state\n");

	instance->connected = VXTCOM_STATE_CONNECTED;

/*
	vxt_cmds.add_ctrlr_pair(instance, dev->otherend_id);
*/
/*
CDY: TODO: We need to set up authorization service, for now
we are not doing guest to guest anyway so test like this.
*/
vxt_cmds.add_ctrlr_pair(instance, 0);

	if (instance->pending_config_event == 1) {
		vxt_bus_cfg_intr(instance->cfg_irq, (void *)instance);
	}

	return;
}


/*
 *
 * vxt_probe:
 *
 * vxt_probe is called out of the KVM virtio PCI bus discovery
 * mechanism, as vxt_probe is a callback registered during
 * virtio device registration.
 *
 * 
 * As an endpoint, in the discovery process, vxt_probe assumes
 * it is the right driver for the targeted device and attempts to
 * contact it.  It proceeds to talk to the VxT/Virtio PCI Card it
 * believes is occupying the targeted slot.  This card is resident
 * on the Virtio bus and it presents a gateway to the VxT Bus.
 * Two virtqueues are the primary means of talking to the Virtio VxT card.
   From this we acquire connection to shared memroy buffers, interrupts
 * and doorbells with which to interact with the VxT Bus.  In
 * effect the Virtio VxT card acts as a bridge, allowing us to attach
 * and interact with the VxT Bus and any cards that might be plugged in
 * there.
 *
 * The vxt_probe also contacts the hypervisor independent layer of the
 * VxT driver, asking it to create a VxT controller instance with which
 * users of VxT can communicate with the VxT bus.
 *
 * Returns:
 *             0 => Success
 *
 *             -ENOMEM  Unable to allocate controller
 *                      instance structure
 *
 *             -EBADF   Incorrect version or unable
 *                      to initialize.
 *
 *             -ENXIO   Virtio VxT Card did not respond or rejected
 *                      initialization attempts.
 *
 */

static int
vxt_probe(struct virtio_device *vdev)
{
	int ret;
	vxtbus_ctrlr_t *instance;
	struct virtqueue *vqs[4];
	/*
	 * request/response --> send/receive
	 */
	const char *queue_names[] = { 
	                              "vxt_cfg_request", "vxt_cfg_response",
	                              "vxt_bus_cfg", "vxt_bus_sig"
	                            };
	vq_callback_t *callbacks[] = { 
	                               vxt_cfg_req_complete, vxt_cfg_response,
	                               vxt_bus_cfg_cb, vxt_bus_sig_cb
	                             };

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, "called, dev %p\n", vdev);

	instance = vxt_cmds.create_controller();
temporary_global = instance;

	if (!instance) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL, "failure allocating "
		          "a new bus controller");
		return -ENOMEM;
	}

	if (instance->version != VXTCOM_BUS_CNTRLR_VERSION) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL, "version mismatch");
		vxt_cmds.dec_bus_ref(instance);
		return -EBADF;
        }
        UMI_LOG(0, VXTPRINT_DEBUG2_LEVEL,"after version check\n");

        instance->connected = VXTCOM_STATE_DISCONNECTED;
	instance->pending_config_event = 0;
        instance->vdev = (void *)vdev;
	instance->kvm = NULL;  /* not used on the guest side */
        instance->inq = NULL;
        instance->outq = NULL;
	instance->sig_buf = NULL;
	instance->sig_bell = 0;
	instance->cfg_bell = 0;
	instance->cfg_irq = 0; /* unused if no direct IRQ available */
	instance->sig_evt = 0; /* unused if no direct IRQ available */
	instance->response_buf = NULL;
	instance->request = NULL;
        instance->send_vq = NULL;
        instance->rcv_vq =  NULL;
	instance->inq_header.msg_id = 0;
	instance->inq_header.reply_in_use = 0;
	instance->inq_header.request_in_use = 0;
	instance->request_offset = 0;
	instance->reply_offset = 0;
	instance->outq_header.msg_id = 1;
	instance->outq_header.reply_in_use = 0;
	instance->outq_header.request_in_use = 0;

/*
 * The following code is a work-around until KVM supports a Virtio
 * mechanism for exporting doorbell PCI Bar related addresses
 *
{
	struct pci_dev *pci_dev;

	pci_dev = (struct pci_dev *)
	          (((void *)vdev) + sizeof(struct virtio_device));

	instance->cfg_bell = (uint64_t)(vxtarch_word)
	                     pci_iomap(pci_dev, 2, 32); 
	instance->sig_bell = instance->cfg_bell + 4;
}
 */

        INIT_WORK(&instance->config_work, parse_config_msg_wrapper);
        INIT_WORK(&instance->bus_activate, bus_activate_msg_wrapper);
	/*
	 * Can initiate device creation at this end
	 */
	instance->indirect_create = 0;

	/*
	 * If we want something customizable, will need to talk to 
	 * the vxtcard before we set vdevice and call ctrlr_init
	 */
	instance->vdevice = 0;

	/*
	 * Open up a file to deliver shared memory objects
	 * to out-of-kernel devices. Call the
	 * vxt_cntrlr module init function which
	 * is common between front end and back
	 * end.
	 */
	ret = vxt_cmds.ctrlr_init(instance);
	UMI_LOG(0, VXTPRINT_DEBUG2_LEVEL,"after vxtcom_ctrlr_init\n");
	if (ret) {
		/*
		 * Destroy the controller
		 */
		vxt_bus_instance_free(instance, 0);
		vxt_cmds.dec_bus_ref(instance);
		return -EBADF;
	}

	vdev->priv = instance;

	/*
	 * We expect two virtqueues for our VxT virtio traffic.
	 * We also must initialize two virtqueues for our VxT bus
	 * We will not use the virtio queues from our VxT bus resources
	 * however. Virtio wraps its interrupts in the queue model and 
	 * this is the only way we can get interrupts for our VxT bus.
	 * We must have working queues in order to receive our VxT bus
	 * interrupts.
	 *
	 * NOTE:  Its important to remember that virtio_pci has 
	 * a side effect here.  We must call find_vqs before we
	 * make our configuration call to the VxT virtio device
	 * emulation.  This call initializes the "vector" field for
	 * the queues.  This is where the IRQ number is obtained 
	 * and provided to the VxT kernel module and sent back to us.
	 * We will not need to wait on the IRQ directly as the
	 */

	ret = vdev->config->find_vqs(vdev, 4, vqs,
	                                       callbacks, queue_names);
        if (ret) {
		vxt_bus_instance_free(instance, 0);
		vxt_cmds.dec_bus_ref(instance);
		return -ENXIO;
	}

	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
	        "vqs allocated:, send_vq %p, rcv_vq %p, "
	        "cfg_cb %p, sig_cb %p\n", vqs[0], vqs[1], vqs[2], vqs[3]);

        instance->send_vq = (void *)vqs[0]; /* request queue */
        instance->rcv_vq = (void *)vqs[1];  /* response queue */
	/* vxt_bus_cfg_intr, wrapper */
        instance->cfg_cb = (void *)vqs[2];  
	/* vxt_bus_sig_intr, wrapper */
        instance->sig_cb = (void *)vqs[3]; 

	/*
	 * Important!  In order to work properly with the common
	 * code modules, the callbacks must be present in the fields
	 * the host side uses to target signaling from.  These are the
	 * ctx fields.  The callback virt queue will be dereferenced
	 * in order to do a "kick" operation in the bus_event_signal
	 * routine contained in this module
	 */
	instance->cfg_irq_ctx = instance->cfg_cb;
	instance->sig_irq_ctx = instance->sig_cb;

        instance->response_pending = 0;
        instance->request_pending = 0;


	ret = vxt_initialize_bus(vdev, instance);
	if (ret) {
		vxt_bus_instance_free(instance, 0);
		vxt_cmds.dec_bus_ref(instance);
		return -ENXIO;
	}


	return 0;
}


/*
 *
 * vxt_apply_config:
 *
 * vxt_apply_config checks to see if there is a change
 * in the status of the VxT Bus provisioning.  i.e. A
 * VxT Linux Kernel module has been loaded into the 
 * base kernel.
 *
 * When the Virtio/VxT driver first comes up, it does so
 * because the PCI probe has detected a VxT Virtio card.
 * This card is a gateway for the VxT Bus.  If the VxT
 * backend is not present however, there is no way to
 * initialize.
 *
 * This code makes it possible to add a base kernel Linux
 * VxT module after the boot of a guest and have a 
 * viable VxT connection without reboot.
 *
 *
 *
 * Returns:
 *		None
 */
static void
vxt_apply_config(struct virtio_device *vdev)
{
	vxtbus_ctrlr_t *instance;
	vxt_virtio_cfg_t *request;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, "called, dev %p\n", vdev);

	instance = vdev->priv;
	request =  (vxt_virtio_cfg_t *)instance->request;

	/*
	 * Check state, if we are in the VXT_PROVISIONED state
	 * attempt to initialize our card.  
	 */
	if (request->current_state == VXT_PROVISIONED) {
		vxt_probe(vdev);
	}
	return;
}


static struct virtio_device_id id_table[] = {
        { VIRTIO_ID_VXT, VIRTIO_DEV_ANY_ID },
        { 0 },
};

static unsigned int features[] = {};

static struct virtio_driver virtio_vxt = {
        .feature_table = features,
        .feature_table_size = ARRAY_SIZE(features),
        .driver.name =  KBUILD_MODNAME,
        .driver.owner = THIS_MODULE,
        .id_table =     id_table,
        .probe =        vxt_probe,
	.remove =       __devexit_p(vxt_bus_remove),
        .config_changed = vxt_apply_config,
};


/*
 * *********** ************ ************ ************* ***************
 * Linux Extended Module initializaion section
 * *********** ************ ************ ************* ***************
 */
/*
 *
 * vxt_bus_init:
 *
 * Called during load of the common module.  Availability of common
 * module code brings the necessary function to register the driver and
 * instigate device initialization.
 *
 * Once it is determined that the common module is compatible and will
 * present with its initial state, a call is made to register the
 * driver callbacks with the Virtio-PCI parent bus.
 *
 * Returns:
 *
 *		0 => upon successful invocation
 *		-ENODEV - unable to initizlze the common vxt module state
 *
 */

static int 
vxt_bus_init(void)
{
	int ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"called\n");

	ret = vxt_cmds.vxtctrlr_module_init();
	if (ret != VXT_SUCCESS) {
		return -ENODEV;
	}

        return register_virtio_driver(&virtio_vxt);
}


/*
 *
 * vxt_bus_cleanup:
 *
 * Unregister the driver from the Virtio PCI bus parent and
 * signal to the upper module that it should clean-up it module/global
 * level resources. 
 *
 * Returns:
 *
 *		None
 */

static void vxt_bus_cleanup(void)
{

        UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"called\n");
	unregister_virtio_driver(&virtio_vxt);
        vxt_cmds.system_shutdown();

        return;
}



/*
 *
 * vxtcom_hub_connect:
 *
 * Initialization of the VxT Card does not take place when the
 * card module is loaded and the VxT controller is plugged in.
 * Initialization is delayed until the common controller module 
 * is loaded.  The "vxtctrlr" calls the vxtcom_hub_connect routine
 * to register the upcalls that will be needed to interact with
 * configuration and runtime procedures in the common layer.
 *
 *
 * Returns:
 *
 *		0 => Upon successful invocation
 *		-EBUSY: A common vxtctrlr module is already loaded.
 */


int vxtcom_hub_connect(vxtbus_common_api_t *utilities)
{
	if (vxt_cmds.vxtctrlr_module_init != NULL) {
		return -EBUSY;
	}
	vxt_cmds = *utilities;
	if (vxt_cmds.vxtctrlr_module_init == NULL) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
			 "vxt_cmds did not initialize properly\n");
	}
	return vxt_bus_init();
}


/*
 * vxtcom_hub_disconnect:
 *
 * Unregister a vxtctrlr module callup list.  This
 * routine is called when the vxtctrlr module is
 * being unloaded.
 *
 *
 * Returns:
 *
 *		0
 */

int vxtcom_hub_disconnect(void)
{
	vxt_bus_cleanup();
	vxt_cmds.vxtctrlr_module_init=NULL;
	return 0;
	
}


/*
 *
 * vxt_bus_module_load:
 *
 * The VxT Driver is made up of multiple downloadable components.
 * The VxT card is the first.  Initialize the global callback
 * table and wait on the rest of the driver before proceeding to
 * register.
 *
 *
 * Returns:
 *		0
 */

static int __init vxt_bus_module_load(void)
{
	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL," called\n");

	vxt_cmds.vxtctrlr_module_init=NULL;

	return 0;
}

/*
 *
 * vxt_bus_module_unload
 *
 * vxt_bus_module_unload must not exit if the other
 * modules that depend on it are still loaded.  Further
 * as the base module certain config resources such as
 * the device class are allocated at this level and shared
 * by all the other loadable modules cascaded on top. These
 * must be released here as our exit signals the close of
 * all VxT driver context.
 *
 * Returns:
 *
 *		None
 */

static void vxt_bus_module_unload(void)
{
	static vxt_device_class_t *vxt_class;

	if (vxt_cmds.vxtctrlr_module_init != NULL) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "vxt_cmds not NULL, still in use!\n");
	}
	vxt_class = get_vxt_class();

	if(vxt_class) {
#ifdef LEGACY_4X_LINUX
		class_simple_destroy(vxt_class);
#else
		class_destroy(vxt_class);
#endif
        }
}


module_init(vxt_bus_module_load);
module_exit(vxt_bus_module_unload);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio VxT driver");
MODULE_LICENSE("Dual BSD/GPL");

