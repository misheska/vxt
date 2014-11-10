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
#include <linux/blkdev.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>


#include <xen/interface/xen.h>
#include <xen/gnttab.h>
#include <xen/evtchn.h>
#include <xen/xenbus.h>

#include <public/vxt_system.h>
#include <public/kernel/bus/xen/vxtcom_bus_ctrlr.h>
#include <public/kernel/vxt_signal.h>
#include <public/kernel/vxt_module.h>
#include <vxtcard/linux/xen/vxt_com_xen.h>


#include <xen/driver_util.h>
#include <linux/delay.h>


/*
 *
 * vxtcom_cntrlr_front:
 *
 * The vxtcom_controller for xen is a front-end/back-end device 
 * Its sole purpose is to establish the initial controller
 * and its connection with DOM0.  DOM0 is the local transport
 * authority in the Xen implementation of the Symantec 
 * communication model.
 *
 * The vxtcom_cntrlr_front module implements xen style
 * device discovery, connecting the controller to the
 * transport authority.  The module is present
 * to allow for initialization, checkpoint, and
 * shutdown of the controller.  The endpoint devices
 * supported by the controller are architecturally 
 * independent of xen and so are implemented in
 * other modules.
 *
 * This particular module is designed on top of the
 * facilities provided for the linux guest port. The
 * utilities and functions of the controller are independent
 * of the linux port and largely independent of xen.  They
 * reside in the vxtcom.c module.
 *
 *
 */


vxtbus_common_api_t vxt_cmds;


/*
 * Local Helper routines
 */



static void vxtcom_fe_instance_free(vxtbus_ctrlr_t *instance, int suspend)
{
	UMI_LOG(90, VXTPRINT_PROFILE_LEVEL,
	        " called, bus %p, suspend %08x\n",
	        instance, suspend);
	instance->connected = (suspend ?
		VXTCOM_STATE_SUSPENDED : VXTCOM_STATE_DISCONNECTED);
	/* No more blkif_request(). */

	/* 
	 * No more gnttab callback work.
	 * The callback is used to signal the availability
	 * of shared page references when they have
	 * freed up
 	 */
	gnttab_cancel_free_callback(&instance->callback);

	/* Flush gnttab callback work. Must be done with no locks held. */
	flush_scheduled_work();

	/* Free resources associated with old device channel. */
	if (instance->inref != VXTCOM_GRANT_INVALID_REF) {
#ifdef RW_ON_GNTTAB_END_FOREIGN_ACCESS
		gnttab_end_foreign_access(instance->inref, 0,
		                          (unsigned long)instance->inq);
#else
		gnttab_end_foreign_access(instance->inref,
		                          (unsigned long)instance->inq);
#endif
		instance->inref = VXTCOM_GRANT_INVALID_REF;
		/*
		 * Done in end_foreign_access, (3rd parm)
		 * free_page((unsigned long)instance->inq);
		 */
		instance->inq = NULL;
	}
	if (instance->outref != VXTCOM_GRANT_INVALID_REF) {
#ifdef RW_ON_GNTTAB_END_FOREIGN_ACCESS
		gnttab_end_foreign_access(instance->outref, 0,
	                             (unsigned long)instance->outq);
#else
		gnttab_end_foreign_access(instance->outref,
	                             (unsigned long)instance->outq);
#endif
		instance->outref = VXTCOM_GRANT_INVALID_REF;
		/*
		 * Done in end_foreign_access, (3rd parm)
		 * free_page((unsigned long)instance->outq);
		 */
		instance->outq = NULL;
	}
	if (instance->sigbuf_ref != VXTCOM_GRANT_INVALID_REF) {

#ifdef RW_ON_GNTTAB_END_FOREIGN_ACCESS
		gnttab_end_foreign_access(instance->sigbuf_ref, 0,
	                             (unsigned long)instance->sig_buf);
#else
		gnttab_end_foreign_access(instance->sigbuf_ref,
	                             (unsigned long)instance->sig_buf);
#endif
		instance->sigbuf_ref = VXTCOM_GRANT_INVALID_REF;
		/*
		 * Done in end_foreign_access, (3rd parm)
		 * free_page((unsigned long)instance->sig_buf);
		 */
		instance->sig_buf = NULL;
		instance->sig_read = NULL;
		instance->sig_write = NULL;
	}
	if (instance->irq) {
		unbind_from_irqhandler(instance->irq, instance);
		instance->evtchn = instance->irq = 0;
#ifndef EXPLICIT_FOREIGN_EVTCHN_BIND
	} else if (instance->evtchn != 0) {
		xenbus_free_evtchn(instance->xbdev, instance->evtchn);
		instance->evtchn = 0;
#endif
	}
	if (instance->sig_irq) {
		unbind_from_irqhandler(instance->sig_irq, instance);
		instance->sig_evt = instance->sig_irq = 0;
#ifndef EXPLICIT_FOREIGN_EVTCHN_BIND
	} else if (instance->sig_evt != 0) {
		xenbus_free_evtchn(instance->xbdev, instance->sig_evt);
		instance->sig_evt = 0;
#endif
	}

}


/*
 * This is just a place holder
 * We may want to have multiple locks, one per controller pair in
 * the case of DOM0 or other transport authority/gateway.  In this
 * case the locks would be found in the instance structure.  In
 * the case of the front end, only one IRQ and one configuration
 * connection exists.
 *
 * Note: the spinlock is only needed if the interrupt cannot
 * be guaranteed to be lock step.
 */
DEFINE_SPINLOCK(config_io_lock);


/*
 *
 * vxtcom_fe_sig_intr:
 *
 * The vxtcom_fe_sig_intr is registered with the xenbus interrupt 
 * handler for VxT Data.  vxtcom_fe_sig_intr checks to see that
 * the controller is fully configured and if it is calls the
 * vxt controller signal demultiplexer.  
 *
 * Returns:
 *
 *		IRQ_HANDLED - Returned in all instances
 *		vxtcom_fe_sig_intr does not support daisy
 *		chaining.
 *
 */

static irqreturn_t
vxtcom_fe_sig_intr(int irq, void *dev_id, struct pt_regs *ptregs)
{
	vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)dev_id;
	/*
	 * This profile log is debug2 because it is on the
	 * data path
	 */
	UMI_LOG(91, VXTPRINT_DEBUG2_LEVEL," called, bus %p\n", instance);
	if (unlikely(instance->connected != VXTCOM_STATE_CONNECTED)) {
		UMI_LOG(92, VXTPRINT_BETA_LEVEL,
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


/*
 *
 * vxtcom_fe_interrupt:
 *
 * vxtcom_fe_interrupt is called to signal configuration requests
 * or replies from the remote controller.  
 *
 * Returns:
 *
 *		IRQ_HANDLED - Returned in all instances
 *		vxtcom_fe_interrupt does not support daisy
 *		chaining.
 */

static irqreturn_t
vxtcom_fe_interrupt(int irq, void *dev_id, struct pt_regs *ptregs)
{
	vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)dev_id;
	unsigned long flags;

	UMI_LOG(93, VXTPRINT_ALPHA_LEVEL," called, bus %p\n", instance);
	if (instance == NULL) {
		UMI_LOG(94, VXTPRINT_PROFILE_LEVEL,
		        "interrupt called on dead device\n");
		return IRQ_HANDLED;
	}
	spin_lock_irqsave(&config_io_lock, flags);

	if (unlikely(instance->connected != VXTCOM_STATE_CONNECTED)) {
		UMI_LOG(95, VXTPRINT_PRODUCT_LEVEL,
		        "spurious interrupt, bus: %p not connected.  "
		        "Delay delivery until controller initialization "
		        "is completed\n", instance);
		/*
		 * We take all spurious interrupts seriously as there is
		 * no practical way to secure the sequencing of xenbus
	 	 * initialization.  We therefore accept device configuration
		 * requests and then delay a check of associated queues
		 * until we have completed configuration.  We must of course
		 * initialize the queues before exporting them in case
		 * a spurious interrupt hits us before the remote has
		 * had a chance to set up.
		 */
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
	UMI_LOG(96, VXTPRINT_DEBUG2_LEVEL ,"return from schedule work\n");

	spin_unlock_irqrestore(&config_io_lock, flags);
	return IRQ_HANDLED;

}


/*
 *
 * vxtcom_fe_setup_resources:
 *
 * vxtcom_fe_setup_resources acquires shared memory for
 * the configuration communication buffers to be shared
 * between the remote and local controller.  It also
 * acquires a signal for the shared buffers.
 *
 * Shared memory resources and a signaling facility
 * are also acquired for the virtual device interrupt
 * services.  This allows allows consolidation of
 * signalling for all devices connected through the
 * remote and local controller.  This supports
 * all guest to Dom0 connected  devices.  Guest to guest 
 * facilities use the same mechanism but are set-up elsewhere,
 * using the configuration channel, to coordinate 
 * initialization.
 *
 * All acquired resources are made available to the
 * particular remote guest targeted by the caller.  In
 * the case of an error all resources are freed before
 * returning.
 *
 * Returns:
 *		0 - Upon successful invocation
 *
 *		-ENOMEM  - Unable to obtain memory 
 *
 *		-invoked routine error returns for
 *		 add_ctrlr_pair, grant_foreign_access
 *		 bind signal handler to irq.
 *
 */

static int vxtcom_fe_setup_resources(struct xenbus_device *dev,
                                     vxtbus_ctrlr_t *instance)
{
	void *in_buf;
	void *out_buf;
	void *signal_buf;
	int ret;

	UMI_LOG(97, VXTPRINT_PROFILE_LEVEL,
	        " called, dev %p, bus %p\n", dev, instance);

	in_buf = (void *)__get_free_page(GFP_KERNEL);
	if (!in_buf) {
		xenbus_dev_fatal(dev, -ENOMEM, 
		                 "allocating vxtcom controller 'in' buffer");
		return -ENOMEM;
	}
	out_buf = (void *)__get_free_page(GFP_KERNEL);
	if (!out_buf) {
		xenbus_dev_fatal(dev, -ENOMEM,
		                 "allocating vxtcom controller 'out' buffer");
		free_page((unsigned long)in_buf);
		return -ENOMEM;
	}

	UMI_LOG(98, VXTPRINT_DEBUG2_LEVEL,
	        "virt_to_mfn inbuf %lu\n", virt_to_mfn(in_buf));
	UMI_LOG(99, VXTPRINT_DEBUG2_LEVEL,
	        "virt_to_mfn outbuf %lu\n", virt_to_mfn(out_buf));
	signal_buf = (void *)__get_free_page(GFP_KERNEL);
	if (!signal_buf) {
		xenbus_dev_fatal
		   (dev, -ENOMEM, 
		    "allocating vxtcom controller signaling buffer");
		free_page((unsigned long)in_buf);
		free_page((unsigned long)out_buf);
		return -ENOMEM;
	}

	/*
	 * Give our permission for "otherend_id", in this
	 * case Dom0 to share this page.
	 */

	ret = gnttab_grant_foreign_access(dev->otherend_id, 
	                                  virt_to_mfn(in_buf), 0);

	if (ret < 0) {
		free_page((unsigned long)in_buf);
		free_page((unsigned long)out_buf);
		free_page((unsigned long)signal_buf);
		goto fail;
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
	memset(in_buf, 0, PAGE_SIZE);
	memset(out_buf, 0, PAGE_SIZE);
	memset(signal_buf, 0, PAGE_SIZE);
	UMI_LOG(100, VXTPRINT_DEBUG2_LEVEL,"sanity check setup\n");
strcpy((char *)(((vxtarch_word)in_buf) + 100), "Sanity Check IN");
strcpy((char *)(((vxtarch_word)out_buf) + 100), "Sanity Check OUT");
	instance->inref = ret;
	instance->inq = in_buf;
	instance->inq_header.msg_id = 0;
	instance->inq_header.reply_in_use = 0;
	instance->inq_header.request_in_use = 0;
	instance->request_offset = 0;
	instance->reply_offset = 0;
	instance->outq = out_buf;
	instance->outq_header.msg_id = 1;
	instance->outq_header.reply_in_use = 0;
	instance->outq_header.request_in_use = 0;
	vxt_cmds.initialize_send_queue((void *)instance->outq, PAGE_SIZE);
	vxt_cmds.initialize_send_queue((void *)instance->inq, PAGE_SIZE);
	instance->sigbuf_ref = VXTCOM_GRANT_INVALID_REF;
	instance->outref = VXTCOM_GRANT_INVALID_REF;
	instance->sig_buf = signal_buf;

	/*
	 * The signal buffer is split into two pieces
	 * allowing simultaneous signalling in both
	 * directions.
	 *
	 * We initialize the part that we will treat
	 * as our send, or signalling buffer.
	 */
	instance->sig_read = (void *)(((char *)signal_buf) + 
	                     VXTSIG_SEC_BUFF_OFFSET);
	instance->sig_write = signal_buf;
	vxt_cmds.sig_init_send_buf(instance->sig_write);

	ret = gnttab_grant_foreign_access(dev->otherend_id, 
	                                  virt_to_mfn(out_buf), 0);
	if (ret < 0) {
		free_page((unsigned long)signal_buf);
		free_page((unsigned long)out_buf);
		goto fail;
	}
	instance->outref = ret;

	ret = gnttab_grant_foreign_access(dev->otherend_id, 
	                                  virt_to_mfn(signal_buf), 0);
	if (ret < 0) {
		free_page((unsigned long)signal_buf);
		goto fail;
	}
	instance->sigbuf_ref = ret;

#ifdef EXPLICIT_FOREIGN_EVTCHN_BIND

	ret = xenbus_alloc_evtchn(dev, &instance->evtchn);
        if (ret) {
		xenbus_dev_fatal(dev, ret,
		                 "alloc_evtchn failed");
		goto fail;
	} 

	ASSERT(VALID_EVTCHN(instance->evtchn));

	ret = bind_evtchn_to_irqhandler(instance->evtchn, vxtcom_fe_interrupt,
	                                SA_SAMPLE_RANDOM, "vxtfrt", instance);
	if (ret <= 0) {
		xenbus_dev_fatal(dev, ret,
		                 "bind_evtchn_to_irqhandler failed");
		goto fail;
	}

	instance->irq = ret;

	/*
	 * Find a separate interrupt for virtual device
	 * signal handling
	 */

	ret = xenbus_alloc_evtchn(dev, &instance->sig_evt);
        if (ret) {
		xenbus_dev_fatal(dev, ret,
		                 "alloc_evtchn failed");
		goto fail;
	} 
	ASSERT(VALID_EVTCHN(instance->sig_evt));

	ret = bind_evtchn_to_irqhandler(instance->sig_evt, vxtcom_fe_sig_intr,
	                                SA_SAMPLE_RANDOM, "vxtfrtsig",
	                                instance);
	if (ret <= 0) {
		xenbus_dev_fatal(dev, ret,
		                 "bind_evtchn_to_irqhandler failed");
		goto fail;
	}

	instance->sig_irq = ret;
#else
   
	ret = bind_listening_port_to_irqhandler(dev->otherend_id, 
	                                        vxtcom_fe_interrupt, 
	                                        SA_SAMPLE_RANDOM,
	                                        "vxtfrt", instance);
	if (ret <= 0) {
		xenbus_dev_fatal(dev, ret,
		                "bind_listening_port_to_irqhandler failed");
                goto fail;
        }
        instance->irq = ret;
	/*
	 * Fill in the evtchn for slightly faster signalling
	 */
	instance->evtchn = irq_to_evtchn_port(instance->irq);
	ASSERT(VALID_EVTCHN(instance->evtchn));

	/*
	 * Find a separate interrupt for virtual device
	 * signal handling
	 */
	ret = bind_listening_port_to_irqhandler(dev->otherend_id, 
	                                        vxtcom_fe_sig_intr, 
	                                        SA_SAMPLE_RANDOM,
	                                        "vxtfrtsig", instance);
	if (ret <= 0) {
		xenbus_dev_fatal(dev, ret,
		                "bind_listening_port_to_irqhandler failed");
                goto fail;
        }
        instance->sig_irq = ret;
	/*
	 * Fill in the evtchn for slightly faster signalling
	 */
	instance->sig_evt = irq_to_evtchn_port(instance->sig_irq);
	ASSERT(VALID_EVTCHN(instance->sig_evt));
#endif

	/*
	 * Push the instance structure onto a Domain based lookup 
	 * list.  This is of limited value for front-ends where
	 * the number of controller connections is limited to
	 * the number of connections to controlling authorities
	 * however it is the means by which guest to guest
	 * connections are made and transport configuration
	 * chain messages sent for the back-end.
	 */

	ret = vxt_cmds.add_ctrlr_pair(instance, dev->otherend_id);
	if (ret) {
		UMI_WARN(101, VXTPRINT_PRODUCT_LEVEL, 
		         "vxtcom_add_ctrlr_pair call failed, ret = %08x\n",
		         ret);
		goto fail;
	}

	return 0;
fail:
	UMI_WARN(102, VXTPRINT_PRODUCT_LEVEL, 
		 "startup failed for remote %08x, clean-up and unplug\n",
	         dev->otherend_id);
	vxtcom_fe_instance_free(instance, 0);
	return ret;
}


/*
 * vxtcom_fe_publish_version:
 *
 * Publish the version number in xenstore. Each end will do this and before
 * connecting, will check that the versions match.
 *
 * By having both ends publish and check versions, the endpoint code can
 * support a wide range of backward compatiblity.  i.e. The front end may
 * encounter a newer version of the backend and determine that this is 
 * acceptible.  The front end responds with its version.  The back-end can
 * then determine from its set of supported versions how to respond.
 *
 *
 */

static int vxtcom_fe_publish_version(vxtbus_ctrlr_t *instance)
{
	struct xenbus_transaction xbt;
	struct xenbus_device *dev = instance->xbdev;
	int ret;

	UMI_LOG(103, VXTPRINT_PROFILE_LEVEL, " called, bus %p\n", instance);
again:
	ret = xenbus_transaction_start(&xbt);
	if (ret) {
		xenbus_dev_fatal(dev, ret, "starting transaction");
		return ret;
	}
	ret = xenbus_printf(xbt, dev->nodename, "version", "%u",
	                    instance->version);
	if (ret) {
		xenbus_transaction_end(xbt, 1);
		xenbus_dev_fatal(dev, ret, "writing version");
		return ret;
	}

	ret = xenbus_transaction_end(xbt, 0);
	if (ret) {
		if (ret == -EAGAIN) {
			goto again;
		}
		xenbus_dev_fatal(dev, ret, "completing transaction");
		return ret;
	}
	return 0;
}


/*
 * vxtcom_fe_publish_resources:
 *
 * vxtcom_fe_publish_resources acquires shared memory and event
 * reservations and sends them to the transport authority
 * (backend) through the xenbus utility. 
 *
 * Once the resource have been succfully recorded in the xenstore
 * record, the driver state is advanced to "XenbusStateInitialised"
 * This signals the backend driver that the VxT controller configuration
 * resources have been allocated and published.  The backend can now
 * initialize is interface based on these shared resources.
 */

static int vxtcom_fe_publish_resources(struct xenbus_device *dev,
                                       vxtbus_ctrlr_t *instance)
{
	const char *message = NULL;
	struct xenbus_transaction xbt;
	int ret;
   
	UMI_LOG(104, VXTPRINT_PROFILE_LEVEL,
	        " called, bus %p, dev %p\n", instance, dev);
	/* Create in and out buffers, alloc event channel. */
	ret = vxtcom_fe_setup_resources(dev, instance);
	if (ret) {
		goto out;
	}

again:
	ret = xenbus_transaction_start(&xbt);
	if (ret) {
		xenbus_dev_fatal(dev, ret, "starting transaction");
		goto destroy_instance;
	}

	ret = xenbus_printf(xbt, dev->nodename,
	                    "inbuf-ref","%u", instance->inref);
	if (ret) {
		message = "writing inbuf-ref";
		goto abort_transaction;
	}
	ret = xenbus_printf(xbt, dev->nodename,
	                    "outbuf-ref","%u", instance->outref);
	if (ret) {
		message = "writing outbuf-ref";
		goto abort_transaction;
	}
	ret = xenbus_printf(xbt, dev->nodename,
	                    "sigbuf-ref","%u", instance->sigbuf_ref);
	if (ret) {
		message = "writing sigbuf-ref";
		goto abort_transaction;
	}
#ifdef EXPLICIT_FOREIGN_EVTCHN_BIND
	ret = xenbus_printf(xbt, dev->nodename,
	                    "event-channel", "%u", 
	                    instance->evtchn);
#else
	ret = xenbus_printf(xbt, dev->nodename,
	                    "event-channel", "%u", 
	                    irq_to_evtchn_port(instance->irq));
#endif
	if (ret) {
		message = "writing event-channel";
		goto abort_transaction;
	}

#ifdef EXPLICIT_FOREIGN_EVTCHN_BIND
	ret = xenbus_printf(xbt, dev->nodename,
	                    "vxtdev_signal_event", "%u", 
	                    instance->sig_evt);
#else
	ret = xenbus_printf(xbt, dev->nodename,
	                    "vxtdev_signal_event", "%u", 
	                    irq_to_evtchn_port(instance->sig_irq));
#endif
	if (ret) {
		message = "writing signal_event";
		goto abort_transaction;
	}

	ret = xenbus_transaction_end(xbt, 0);
	if (ret) {
		if (ret == -EAGAIN) {
			goto again;
		}
		xenbus_dev_fatal(dev, ret, "completing transaction");
		goto destroy_instance;
	}

	/*
	 * Log our version to make certain it is supported by
	 * the back-end
	 */
	UMI_LOG(105, VXTPRINT_DEBUG2_LEVEL,"publish our version\n");
	ret = vxtcom_fe_publish_version(instance);
	if (ret) {
		xenbus_dev_fatal(instance->xbdev, ret,
		                 "unable to publish version %lu\n"); 
		return ret;
	}
	/*
	 * Signal the transport authority to pick up the
	 * resource reservations.
	 */
	UMI_LOG(106, VXTPRINT_BETA_LEVEL,"switch state to initialized\n");
	xenbus_switch_state(dev, XenbusStateInitialised);

	return 0;

abort_transaction:
	UMI_WARN(107, VXTPRINT_PRODUCT_LEVEL,
	         "Unable to publish resources, unplugging\n");
	xenbus_transaction_end(xbt, 1);
	if (message)
		xenbus_dev_fatal(dev, ret, "%s", message);
destroy_instance:
	vxtcom_fe_instance_free(instance, 0);
out:
	return ret;

}


/*
 * 
 * vxtcom_fe_cntrlr_connect:
 *
 * Invoked when a state change call is made by the backend
 * indicating that the backend has moved to the "CONNECTED"
 * state.  The backend is ready for general traffic and
 * we must complete preparatons and enter the connected state
 * 
 * In addition to completion of local connection tasks, a 
 * call is made to the queue/device vending layer to allow
 * propagation of the state change to all registered transport
 * devices.
 *
 * Returns: NONE
 *
 */

static void vxtcom_fe_cntrlr_connect(vxtbus_ctrlr_t *instance)
{
	unsigned long version;
	int ret;

	if ((instance->connected == VXTCOM_STATE_CONNECTED) ||
	    (instance->connected == VXTCOM_STATE_SUSPENDED) ) {
		return;
	}

	UMI_LOG(108, VXTPRINT_PROFILE_LEVEL, "called: remote = %s\n",
	        instance->xbdev->otherend);

	ret = xenbus_gather(XBT_NIL, instance->xbdev->otherend,
	                    "version", "%lu", &version,
			    NULL);
	if (ret) {
		xenbus_dev_fatal(instance->xbdev, ret,
		                 "reading backend fields at %s",
		                 instance->xbdev->otherend);
		return;
	}

	UMI_LOG(109, VXTPRINT_BETA_LEVEL,"check version %lu\n", version);
	if (version != instance->version) {
		xenbus_dev_fatal(instance->xbdev, ret,
		                 "incompatible versions frontend "
		                 "%lu, backend %lu", 
		                 instance->version, version);
		return;
	}


	vxt_cmds.bus_connect(instance);

	UMI_LOG(110, VXTPRINT_BETA_LEVEL,"switching to connected state\n");
	(void)xenbus_switch_state(instance->xbdev, XenbusStateConnected);

	/* Signal queue vending/processing */
	instance->connected = VXTCOM_STATE_CONNECTED;

	if (instance->pending_config_event == 1) {
		vxtcom_fe_interrupt(instance->irq, (void *)instance,
		                    (struct pt_regs *) NULL);
	}


}


/*
 *
 * vxtcom_fe_closing:
 *
 *
 * Called when we are signaled that the backed has changed its state to 
 * Closing.  We must delete our device-layer structures now, to ensure 
 * that writes are flushed through to the backend.  Once this is done, 
 * we can switch our local state to Closed in acknowledgement.
 *
 * Returns:
 *		None
 */

static void vxtcom_fe_closing(struct xenbus_device *dev)
{
	vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)dev->dev.driver_data;

	UMI_LOG(111, VXTPRINT_PROFILE_LEVEL, "%s removed\n", dev->nodename);
	if (instance == NULL) {
		return;
	}

	/* No more gnttab callback work. */
	gnttab_cancel_free_callback(&instance->callback);

	/* Flush gnttab callback work. Must be done with no locks held. */
	flush_scheduled_work();

	vxt_cmds.bus_unplug_devices(instance, 1);

	vxt_cmds.remove_ctrlr_pair(instance, dev->otherend_id);


	xenbus_frontend_closed(dev);
}




/* 
 * Callback routines for xenbus
 */


/*
 * vxtcom_fe_probe:
 *
 * vxtcom_fe_probe is called by xenbus when a new device
 * is created.  
 *
 * vxtcom_fe_probe consults the XenStore data base to
 * determine customizations such as device major number
 *
 * vxtcom_fe_probe makes requests for the
 * communication resources that will be needed to
 * talk with the transport authority, (back-end).
 * These include a signalling event and shared
 * memory for incoming and outgoing data areas.
 * 
 * Once all of the resources have been lined
 * up, vxtcom_fe_probe publishes an inventory
 * as part of an instance record and makes
 * the transport authority aware of the
 * record through a xenbus device state
 * change, (a transition to XenbusStateInitialised).
 * The inventory is published through the
 * xenbus utility using xenbus_printf.  A primitive
 * transaction is built from the xenbus_printf and
 * the xenbus_switch_state routines.
 *
 * Memory allocation for shared buffers is straight
 * forward.  However, shared page reservations are
 * done in a round-about manner to allow for 
 * protection against unsecured channels.  A local
 * reference table is kept in the guest OS.  
 * Tentative grants are made from this table and
 * the ref number is sent to the privileged authority
 * (Dom0).  The privileged authority then sets up
 * the reference based on the wishes of the guest.
 * In this way the privileged authority is free to
 * grant or withold the request to grant foreign
 * access to a page or pages within the guest.
 *
 *
 * Returns:
 *
 *             0 => Success
 *
 *             -ENOMEM  Unable to allocate controller 
 *                      instance structure
 *
 *             -EBADF   Incorrect version or unable
 *                      to initialize.
 */


static int vxtcom_fe_probe(struct xenbus_device *dev,
                           const struct xenbus_device_id *id)
{
	int ret, vdevice;
	vxtbus_ctrlr_t *instance;

	UMI_LOG(112, VXTPRINT_PROFILE_LEVEL,
	        "called, dev %p, xenbus_dev_id %p\n", dev, id);
	ret = xenbus_scanf(XBT_NIL, dev->nodename,
	                   "virtual-device", "%i", &vdevice);
	UMI_LOG(113, VXTPRINT_DEBUG_LEVEL,"xenbus_scanf ret %d\n", ret);
	/*
	 * Dynamic device major number allocation will
	 * be employed, no need to pick up the 
	 * value from xenbus.  i.e.
	 * Register the device as a character device
	 * with:
	 *    int register_chrdev(unsigned int major, 
	 *                        const char *name, 
	 *                        struct file_operations *fops);
	 *
	 * Filling the major number in with 0 will cause
	 * the return value to take on the value of a 
	 * dynamic major number.
	 * Application level file access implementations
	 * can find the number via the proc support:
	 *
	 *    major=`grep $DEV /proc/devices | \ 
	 *    awk "{print \\$1}"` 
	 *
	 * The vdevice is picked up here as an override
	 * for custom views.
	 */

	if (ret != 1) {
		vdevice = 0;  
	}

	instance = vxt_cmds.create_controller();
	if (!instance) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating info structure");
		return -ENOMEM;
	}
	UMI_LOG(114, VXTPRINT_DEBUG_LEVEL,
	        "return from vxtcom_create_controller, instance = %p\n",
	        instance);
	
	if (instance->version != VXTCOM_BUS_CNTRLR_VERSION) {
		xenbus_dev_fatal(dev, -EBADF, "version mismatch");
		return -EBADF;
	}
	UMI_LOG(115, VXTPRINT_DEBUG2_LEVEL,"after version check\n");

	instance->connected = VXTCOM_STATE_DISCONNECTED;
	instance->xbdev = dev;
	instance->inq = NULL;
	instance->outq = NULL;
	instance->inref = VXTCOM_GRANT_INVALID_REF;
	instance->outref = VXTCOM_GRANT_INVALID_REF;
	instance->evtchn = 0;
	instance->user_ref_cnt = 0;
	instance->pending_config_event = 0;
	INIT_WORK(&instance->config_work,
	          (void (*)(void *))vxt_cmds.parse_config_msg,
	          (void *)instance);
	/*
	 * Can initiate device creation at this end
	 */
	instance->indirect_create = 0;

	instance->vdevice = vdevice;

	/*
	 * Open up a file to deliver shared memory
	 * to out of kernel devices call the 
	 * vxt_cntrlr module init function which
	 * is common between front end and back
	 * end.
	 */
	ret = vxt_cmds.ctrlr_init(instance);
	UMI_LOG(116, VXTPRINT_DEBUG2_LEVEL,"after vxtcom_ctrlr_init\n");
        if (ret) {
		/*
		 * Destroy the controller
		 */
		vxt_cmds.dec_bus_ref(instance);
		return -EBADF;
        }
	/*
	 * Used for scheduling work restart after free shared
	 * ref callback indicates shared ref resource availability.
	 */
/*
 
	INIT_WORK(&info->work, vxtcom_cntrlr_retry, (void *)info);
*/

	dev->dev.driver_data = instance;

	ret = vxtcom_fe_publish_resources(dev, instance);
	UMI_LOG(117, VXTPRINT_DEBUG2_LEVEL,"return from publish resources\n");
	if (ret) {
		/*
		 * Destroy the controller
		 */
		vxt_cmds.dec_bus_ref(instance);
		dev->dev.driver_data = NULL;
		return ret;
	}

	return 0;

}


/*
 *
 * vxtcom_fe_remove:
 *
 * vxtcom_fe_remove is called when the vxt driver is being removed
 * or when the vxt controller device is unplugged, (deprovisioned).
 *
 * vxtcom_fe_remove calls the common vxt controller module to 
 * disable new device creation, shutdown existing devices and
 * de-register the controller instance.  After the orderly shutdown
 * of the VxT controller function.  The xenbus state is changed
 * to closing, triggering the backend to begin its Xenbus level
 * shutdown.  After returning the hold on the VxT controller instance
 * is dropped and all bus level resources are freed.
 *
 * There is no inherent synchronization with the Xenbus backend
 * In addition, the shared pages used for configuration and multiplexed
 * signaling must be released by the back-end before they are
 * de-registered here.  For this reason delays have been added. 
 * Page release by the backend is triggered by the move to the
 * "Closing" state.  A handshake could be devised, using either the 
 * Xenbus states or the Xenstore database.  However, the handshake
 * would have to rely on a timeout so that this driver would not
 * hang in the event of disabled back-end.  When this was coupled with
 * the fact that the cost of out of order shutdown was simply the leak
 * of the configuration pages until the guest is rebooted.  It was determined
 * that a simple delay was the best course of action. 
 *
 * Returns:
 *
 *		0
 *
 */

static int vxtcom_fe_remove(struct xenbus_device *dev)
{
        vxtbus_ctrlr_t *instance = dev->dev.driver_data;
	int ret;

	msleep(50);
        dev->dev.driver_data = NULL;
        UMI_LOG(118, VXTPRINT_PROFILE_LEVEL,
	        "vxtcom_fe_remove called: %s removed\n", dev->nodename);
	if (instance == NULL) {
		return 0;
	}
	/*
	 * Shutdown all new device creation
	 */
	vxt_cmds.bus_disconnect(instance);
	/* 
	 * Flush gnttab callback work. 
	 * Must be done with no locks held.
	 * We do it here because we have just turned off interrupt handling
	 * No new entries will be placed on the work queue
	 */
	flush_scheduled_work();

	vxt_cmds.bus_unplug_devices(instance, 1);
	if ((instance->connected == VXTCOM_STATE_CONNECTED) ||
	    (instance->connected == VXTCOM_STATE_MORIBUND)) {
		UMI_LOG(119, VXTPRINT_BETA_LEVEL,"switch state to Closing\n");
		(void)xenbus_switch_state(instance->xbdev, XenbusStateClosing);
		msleep(50); 
	}


	ret = vxt_cmds.remove_ctrlr_pair(instance, dev->otherend_id);


	if (ret) {
        	dev->dev.driver_data = NULL;
        	UMI_WARN(120, VXTPRINT_PRODUCT_LEVEL,
		         "not on active controller list\n");
	}


	if (instance->irq) {
		unbind_from_irqhandler(instance->irq, instance);
		instance->evtchn = instance->irq = 0;
	}
        vxtcom_fe_instance_free(instance, 0);

	/*
	 * Destroy the controller
	 */
	vxt_cmds.dec_bus_ref(instance);

        return 0;
}


/*
 * vxtcom_fe_resume:
 *
 * Throw away the old resource connecting the controller with the
 * transport authority, (backend).  Re-establish the connection
 * with the authority by re-acquiring resource reservations and
 * publishing them to the authority for the 3 way handshake.
 * i.e. 1: acquire and publish, 2: authority makes resource requests
 * for shared pages and events on behalf of this controller,
 * 3: authority sends response and further connection info back
 * as reply. We finsh connecting to the newly acquired resources.
 *
 * Return:
 *		0
 */

static int vxtcom_fe_resume(struct xenbus_device *dev)
{
	vxtbus_ctrlr_t *instance = dev->dev.driver_data;
	int oldstate;
	int ret;

        dev->dev.driver_data = NULL;

	UMI_LOG(121, VXTPRINT_PROFILE_LEVEL,
	        "vxtcom_fe_resume called: %s, bus %p\n",
	        dev->nodename, instance);

	if (instance == NULL) {
		return 0;
	}

	/*
	 * Shutdown all new device creation
	 */
	vxt_cmds.bus_disconnect(instance);
	/* 
	 * Flush gnttab callback work. 
	 * Must be done with no locks held.
	 * We do it here because we have just turned off interrupt handling
	 * No new entries will be placed on the work queue
	 */
	flush_scheduled_work();

	/*
	 * remove all existing devices, do not signal remote
	 * as the remote is gone.
	 * Note: whenwe have more stateful devices, we may need a
	 * state save call before the unplug
	 */
/*
	vxt_cmds.bus_suspend(instance, &oldinstance_recover);
*/
	vxt_cmds.bus_unplug_devices(instance, 0);

	ret = vxt_cmds.remove_ctrlr_pair(instance, dev->otherend_id);


	if (ret) {
        	dev->dev.driver_data = NULL;
        	UMI_WARN(122, VXTPRINT_PRODUCT_LEVEL,
		         "not on active controller list\n");
	}
     
	/*
	 * Free up the old connection resources, 
	 * Pass TRUE to indicate that the connection 
	 * should be put in the suspended state
	 * in preparation for reconnection.
	 */
	vxtcom_fe_instance_free(instance, 
	                   (instance->connected == VXTCOM_STATE_CONNECTED) ||
	                   (instance->connected == VXTCOM_STATE_MORIBUND));

	oldstate = instance->connected;

	/*
	 * Destroy the controller
	 */
	vxt_cmds.dec_bus_ref(instance);

	/*
	 * vxtcom recover signals out to the XEN independent portion
	 * of the controller that it needs to re-connect its resources
	 */
	if (oldstate == VXTCOM_STATE_SUSPENDED) {
		/*
		 * Note: we don't use the device id so
		 * just pass NULL here
		 */
		ret = vxtcom_fe_probe(dev, NULL);
		if (ret) {
			return ret;
		}
/*
		vxtcom_bus_recover(oldinstance_recover);
*/
	} else {
		/* vxtcom_bus_suspend(instance); */
	}

	return 0;
}


/*
 *
 * vxtcom_backend_changed:
 *
 * vxtcom_backend_changed is called by xenbus when there
 * has been a state change in the xenbus device as understood
 * either by xenbus or the backend.  The backend state
 * is passed as an incoming parameter.  However, more
 * information is often communicated through the 
 * xenbus transaction printf/gather capability.
 *
 */

static void vxtcom_backend_changed(struct xenbus_device *dev,
                                   enum xenbus_state backend_state)
{
	vxtbus_ctrlr_t *instance = dev->dev.driver_data;

	UMI_LOG(123, VXTPRINT_PROFILE_LEVEL,
	        "called, dev %p, state %08x\n", dev, backend_state);

	if (instance == NULL) {
		UMI_WARN(124, VXTPRINT_PRODUCT_LEVEL, 
		         "controller is unplugged, backend "
		         "changed notification dropped\n");
		return;
	}

	switch (backend_state) {
	case XenbusStateInitWait:
		UMI_LOG(125, VXTPRINT_PROFILE_LEVEL,"XenbusStateInitWait\n");
		break;

	case XenbusStateInitialised:
#ifdef ATTACH_RECONFIG_SUPPORT
	case XenbusStateReconfigured:
#endif
		if (backend_state == XenbusStateInitialised) {
			UMI_LOG(126, VXTPRINT_PROFILE_LEVEL,
			        "XenbusStateInitialized\n");
		} else {
			UMI_LOG(127, VXTPRINT_PROFILE_LEVEL,
			        "XenbusStateReconfigured\n");
		}
		if (dev->dev.driver_data != NULL)  {
			vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)
			                           dev->dev.driver_data;
			if (instance->connected == VXTCOM_STATE_MORIBUND) {
				UMI_LOG(128, VXTPRINT_PRODUCT_LEVEL, 
				        "Detected, dead driver context, "
					"removing and attempting recovery\n");
				vxtcom_fe_resume(dev);
			}
		}
		break;

	case XenbusStateClosed:
		UMI_LOG(129, VXTPRINT_PROFILE_LEVEL,"XenbusStateClosed\n");
		device_unregister(&dev->dev);
		break;

	case XenbusStateInitialising:
#ifdef ATTACH_RECONFIG_SUPPORT
	case XenbusStateReconfiguring:
#endif
		if (backend_state == XenbusStateInitialising) {
			UMI_LOG(130, VXTPRINT_PROFILE_LEVEL,
			        "XenbusStateInitializing\n");
		} else {
			UMI_LOG(131, VXTPRINT_PROFILE_LEVEL,
			        "XenbusStateReconfiguring\n");
		}
		/*
		 * If we get an initializing signal and we believe we are
		 * moribund we must clean-up our existing device, the remote
		 * partner no longer exists.  And we must start the new
		 * controller creation in lieu of a probe signal.
		 *
		 */
		if (dev->dev.driver_data != NULL)  {
			vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)
			                           dev->dev.driver_data;
			if (instance->connected == VXTCOM_STATE_MORIBUND) {
				UMI_LOG(132, VXTPRINT_PRODUCT_LEVEL, 
				        "Detected, dead driver context, "
					"removing and attempting recovery\n");
				vxtcom_fe_resume(dev);
			}
		}
		break;

	case XenbusStateUnknown:
		/*
		 * Xenbus presents the Unknown state at unplumb/deprovision
		 * and migration events.  Unfortunately, there is no
		 * way to distinguish between these, therefore
		 * the Unknown state must either be ignored or the actions
		 * taken must be compatible with both shutdown and migration.
		 * At any rate, Xenbus at the time of this writing does not
		 * provide a signal on migration prior to shutdown on the
		 * origin system, therefore all recovery actions must happen
		 * on the destination.  If better migration behavior is
		 * desired, an extra-bus mechanism must be employed.
		 *
		 * The device unregister operation has been disabled below
		 * and a delayed operation substituted.  This behavior will
		 * make VxT as functional as possible with the limited 
		 * capabilities of Xenbus migration.  However, failing to
		 * deregister here leads to an error condition being reflected
		 * to the XenStore database in the case of a deprovision.
		 * This has the unwanted side effect of causing subsequent
		 * driver loads that occur while the device is unplugged to
		 * fail.
		 */

		/*
		device_unregister(&dev->dev);
		*/
		if (dev->dev.driver_data != NULL)  {
			vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)
			                           dev->dev.driver_data;
			/*
			 * In order to match the behavior of the windows code
			 * which does not experience a bus unknown condition
			 * we will not disconnect here and instead rely on the
			 * vxtcom_ctrlr died config message from Dom0
			 * This is done strictly to match for testing, this
			 * disconnect is a perfectly correct response.
			*/
			/*  see above comment
				vxtcom_bus_disconnect(instance);
			*/
			instance->connected = VXTCOM_STATE_MORIBUND;
		}
		UMI_LOG(133, VXTPRINT_PROFILE_LEVEL,"XenbusStateUnknown\n");
		break;

	case XenbusStateConnected:
		/* Ensure we connect even when two watches fire in 
		   close successsion and we miss the intermediate value 
		   of frontend_state. */
		if (dev->state == XenbusStateConnected)
			break;

		UMI_LOG(134, VXTPRINT_PROFILE_LEVEL,"XenbusStateConnected\n");
		vxtcom_fe_cntrlr_connect(instance);
		break;

	case XenbusStateClosing:
		UMI_LOG(135, VXTPRINT_PROFILE_LEVEL,"XenbusStateClosing\n");
		vxt_cmds.bus_disconnect(instance);
   
/* CDY
	need to wait on a semaphore like this?
		 down(&bd->bd_sem);
 */
		if (instance->user_ref_cnt > 0) {
			xenbus_dev_error(dev, -EBUSY,
		   			 "Device in use; refusing to close");
		} else {
			vxtcom_fe_closing(dev);
		}
/*
     	 up(&bd->bd_sem);
*/
		break;
	}
}



/*
 * Module registration and callback publication
 * to xenbus
 */


static struct xenbus_device_id  vxtcom_ids[] = {
	{ "vxtcom_ctrlr" },
	{ "" }
};


static struct xenbus_driver vxtcom_controller = {
	.name = "vxtcom_ctrlr",
	.owner = THIS_MODULE,
	.ids = vxtcom_ids,
	.probe = vxtcom_fe_probe,
	.remove = vxtcom_fe_remove,
	.resume = vxtcom_fe_resume,
	.otherend_changed = vxtcom_backend_changed,
};

static ssize_t vxt_show_version(struct class *class, char *buf)
{
	sprintf(buf, "%s\n",  VXT_CARD_BANNER);
	return strlen(buf) + 1;
}


CLASS_ATTR(vxtcard_version, S_IRUGO, vxt_show_version, NULL);

#ifdef NO_XEN_CLASS
static vxt_device_class_t *vxt_class = NULL;

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
	class_create_file(vxt_class, &class_attr_vxtcard_version);
#endif

        return vxt_class;
}
#else
vxt_device_class_t *get_vxt_class(void)
{
	return get_xen_class();
}
#endif


/*
 *
 * vxtcom_fe_init:
 *
 * vxtcom_fe_init is called on module load
 * After determining that the operating system
 * has a Xen undercarriage, xenbus_register_frontend
 * is called, advertising the xen front end 
 * callbacks.  Unlike normal device drivers
 * The xen virtual driver model has a 
 * front end and a back end version.  The
 * front end behaves like a normal device driver
 * the back end acts as an adapter between
 * real physical hardware and the guests front
 * end.
 *
 */ 

static int vxtcom_fe_init(void)
{
	int ret;

	UMI_LOG(137, VXTPRINT_PROFILE_LEVEL,"called\n");

	if (!is_running_on_xen())
		return -ENODEV;

	ret = vxt_cmds.vxtctrlr_module_init();
	if (ret != VXT_SUCCESS) {
		return -ENODEV;
	}

	/*
	 * Register with xenbus frontend/backend driver
	 * facility.
	 */
	return xenbus_register_frontend(&vxtcom_controller);
}


static void vxtcom_fe_cleanup(void)
{

	UMI_LOG(138, VXTPRINT_PROFILE_LEVEL,"called\n");
 	xenbus_unregister_driver(&vxtcom_controller);
	vxt_cmds.system_shutdown();

        return;
}



static int __init vxtcom_fe_module_load(void)
{
        UMI_LOG(139, VXTPRINT_PROFILE_LEVEL," called\n");
        if (!is_running_on_xen())
                return -ENODEV;
        UMI_LOG(140, VXTPRINT_BETA_LEVEL,"running on xen\n");

	vxt_cmds.vxtctrlr_module_init=NULL;

        return 0;
}

static void vxtcom_fe_module_unload(void)
{
        static vxt_device_class_t *vxt_class;

	if (vxt_cmds.vxtctrlr_module_init != NULL) {
        	UMI_WARN(141, VXTPRINT_PRODUCT_LEVEL,
		         "vxt_cmds not NULL, still in use!\n");
	}
	vxt_class = get_vxt_class();
#ifdef NO_XEN_CLASS
	if(vxt_class) {
#ifdef LEGACY_4X_LINUX
		class_simple_destroy(vxt_class);
#else
		class_destroy(vxt_class);
#endif
	}
#endif
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
		UMI_WARN(142, VXTPRINT_PRODUCT_LEVEL,
			 "vxt_cmds did not initialize properly\n");
	}
	return vxtcom_fe_init();
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
	vxtcom_fe_cleanup();
	vxt_cmds.vxtctrlr_module_init=NULL;
	return 0;
	
}


module_init(vxtcom_fe_module_load);
module_exit(vxtcom_fe_module_unload);


/* MODULE_LICENSE("Symantec Proprietary"); */
MODULE_LICENSE("Dual BSD/GPL");
