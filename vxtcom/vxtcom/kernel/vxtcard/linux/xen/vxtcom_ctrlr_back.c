/*
 *
 * vxtcom_ctrlr_back:
 *
 * The vxtcom_controller for xen is a front-end/back-end device
 * Its sole purpose is to establish an initial controller
 * and its connection with DOM0.  DOM0 is the local transport
 * authority in the Xen implementation of the Symantec
 * communication model.
 *
 * The vxtcom_ctrlr_back module implements
 * device discovery, creating a controller instance for each
 * guest domain.  The module is present
 * to allow for initialization and
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

#include <linux/blkdev.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <xen/evtchn.h>
#include <asm/hypervisor.h>
#include <xen/gnttab.h>
#include <xen/driver_util.h>
#include <xen/xenbus.h>
#include <public/vxt_system.h>
#include <public/kernel/bus/xen/vxtcom_bus_ctrlr.h>
#include <public/kernel/vxt_signal.h>
#include <public/kernel/vxt_module.h>
#include <vxtcard/linux/xen/vxt_com_xen.h>




#include <linux/delay.h>
#include <public/symcom_dev_table.h>
#include <public/kernel/vxtcom_devlib.h>


/*
 * Remove before check-in, for internal test only CDYCDYCDY
 */
#include <public/vxt_msg_dev.h>

vxtbus_common_api_t vxt_cmds;

extern int
vxtcom_test_add_dev(vxtbus_ctrlr_t *vxtbus, vxtdev_type_t device_type,
                    char *remote_ep, void *dev_parms, uint32_t info_length,
                    vxtdev_handle_t *newdev_instance);
extern int
vxtcom_test_remote_add_dev(vxtbus_ctrlr_t *vxtbus, vxtdev_type_t device_type,
                    char *remote_ep, void *dev_parms,
                    int dev_parm_length);

static void vxtcom_be_connect(vxtbus_ctrlr_t *vxtcom);
static int vxtcom_be_get_bufs(vxtbus_ctrlr_t *vxtcom);
static vxtbus_ctrlr_t *vxtcom_be_alloc(struct xenbus_device *dev);
static void vxtcom_be_disconnect(vxtbus_ctrlr_t *vxtcom);
static irqreturn_t vxtcom_be_interrupt(int irq, void *dev_id,
                                       struct pt_regs *regs);
static irqreturn_t vxtcom_be_sig_intr(int irq, void *dev_id,
                                      struct pt_regs *regs);


#include <public/vxt_auth.h>
#include <public/kernel/vxt_auth_db.h>


/*
 *
 * vxt_reinit_t:
 *
 * vxt_reinit_t holds the identifier for the remote vxt_controller 
 * endpoint.  It is used to target the object of a re-initialization of the 
 * provisioned state.  This is a reset of the virtual hardware state of
 * the VxT controller and its Xenbus presentation affecting both the
 * back and front end.
 *
 */

typedef struct vxt_reinit {
	vxtarch_word parm;
} vxt_reinit_t;




/*
 * vxtcom_be_remove
 *
 * Callback from xenbus driver to remove a vxtcom controller
 *
 * vxtcom_be_remove first acts on the common vxt_controller
 * through the registered callbacks.  The first actions is
 * to stop new devices from being created.  The next
 * action tells the remote that the bus will be going down
 * through a configuration message.  Since the VxT controller
 * is still up. This gives both ends a chance to flush
 * any device traffic that is still in the queues.
 *
 * After returning from the remote endpoint flush signal,
 * vxtcom_be_remove calls the common code to shut down
 * any devices found on the controller/bus.
 *
 * There is a default VxT message device that accompanies
 * any VxT frontend/backend pair.  This device, like
 * any other must have an authorization record.  vxtcom_be_remove
 * contacts the VxT authorization database and asks that
 * the default record be removed.
 *
 * Finally, vxtcom_be_remove contacts the common layer and
 * signals a VxT controller shutdown.  After that, it 
 * changes the Xenbus state to "Closed", signaling through
 * the Xenbus to the frontend that the VxT device has been
 * powered down.
 *
 * Returns:
 *
 *		0
 *
 */

static int vxtcom_be_remove(struct xenbus_device *dev)
{
	vxtbus_ctrlr_t *vxtcom = dev->dev.driver_data;
	char ctrl_state_dir[50];
	int ret;

	if (vxtcom == NULL) {
		UMI_WARN(143, VXTPRINT_PRODUCT_LEVEL,
		         "no device, xenbus has called us twice?\n");
		return 0;
	}

	dev->dev.driver_data = NULL;

	UMI_LOG(144, VXTPRINT_PROFILE_LEVEL,
	        "vxtcom_be_remove called: %s", dev->otherend);
	if (vxtcom) {
		/*
		 * Disable the creation of new devices
		 * on this controller
		 */
		vxt_cmds.bus_disconnect(vxtcom);
		/*
		 * Do the same for the remote 
		 */
		vxt_cmds.bus_signal_closing(vxtcom, VXTCOM_CLOSING_BUS_DOWN);
		/*
		 * Remove all devices present 
		 * on the controller
		 */
		vxt_cmds.bus_unplug_devices(vxtcom, 1);
		/*
		 * Remove the default device authorization 
		 * record
		 */
		UMI_LOG(145, VXTPRINT_DEBUG_LEVEL, 
		        "printing uuid value %s\n", vxtcom->remote_uuid);
		sprintf(ctrl_state_dir,
		        "/local/domain/%d/control/shutdown",
		        dev->otherend_id);
		if (xenbus_exists(XBT_NIL, ctrl_state_dir, "")) {
			/*
			 * We are shutting down
			 * Go ahead and delete the default record
			 */
			ret =
			   vxt_cmds.db_client_record_delete(vxtcom->remote_uuid,
			                                    VXTDB_DOM0_ENDPOINT,
			                                    VXT_DEFAULT_DEVICE);
			if (ret) {
				UMI_WARN(146, VXTPRINT_PRODUCT_LEVEL,
				         " Unable to locate remote domain UUID "
				         "record for %d, uuid = %s\n",
				         dev->otherend_id, vxtcom->remote_uuid);
			}
		}


		if (vxtcom->connected == VXTCOM_STATE_CONNECTED) {
			UMI_LOG(147, VXTPRINT_BETA_LEVEL,
			        " switching xenbus state to Closing\n");
			(void)xenbus_switch_state(vxtcom->xbdev, 
			                          XenbusStateClosing);
		}
		vxtcom->connected = VXTCOM_STATE_DISCONNECTED;

		ret = vxt_cmds.remove_ctrlr_pair(vxtcom, dev->otherend_id);

		vxtcom_be_disconnect(vxtcom);

		if (ret) {
			UMI_WARN(148, VXTPRINT_PRODUCT_LEVEL,
			         "active controller list does not contain "
			         "vxtcom %p, for other end: %s\n",
			         vxtcom, dev->otherend);
        	}

		dev->dev.driver_data = NULL;

		/*
		 * Destroy the controller
		 */
		vxt_cmds.dec_bus_ref(vxtcom);

	}

	UMI_LOG(149, VXTPRINT_BETA_LEVEL,
	        " switching xenbus state to CLOSED\n");
	xenbus_switch_state(dev, XenbusStateClosed);

	return 0;
}


/*
 *
 * vxtcom_reinit_xenstore_record:
 *
 * vxtcom_reinit_xenstore_record invokes a user space application
 * to clean the xenstore record for the targeted domain, preparing
 * it by setting it into the "Initialized" state.
 *
 * Returns:  0
 *
 */

static int
vxtcom_reinit_xenstore_record(void *reinit_handle)
{
	vxt_reinit_t *reinit_parm = (vxt_reinit_t *)reinit_handle;
	int domain_id = (int)(long)(reinit_parm->parm);
	static char *env[]={ 
	                "HOME=/",
	                "PATH=/usr/local/bin:/sbin:/usr/sbin:/bin:/usr/bin",
	                "SHELL=/bin/bash",
	                "TERM=linux",
	                NULL
	                };


	char argv[70];
	char *args[4];
	int ret;

	UMI_LOG(150, VXTPRINT_PROFILE_LEVEL,
	        "running, init record %d\n", domain_id);

	vxt_kfree(reinit_handle);
	args[0] = argv;
	strcpy(args[0], "/etc/vxtcom/bin/vxtcom-setup-domain"); 
	args[1] = argv + strlen(args[0]) + 1;
	sprintf(args[1], "Domain-%d", domain_id);
	args[2] = NULL;
	
	ret = call_usermodehelper(args[0], args, env, 0);

	if (ret < 0) {
		UMI_LOG(151, VXTPRINT_PRODUCT_LEVEL,
		        "XenStore record cleanup failed\n");
	}

	return 0;

}


/*
 * vxtcom_be_probe
 *
 * Entry point when a new controller is created.  Allocate the basic
 * structures, create a controller device and set state to
 * XenbusStateInitialised, waiting for the frontend to change state to
 * XenbusStateConnected.
 *
 */

static int vxtcom_be_probe(struct xenbus_device *dev,
			 const struct xenbus_device_id *id)
{
	int err;
	vxtbus_ctrlr_t	*vxtcom;

	UMI_LOG(152, VXTPRINT_PROFILE_LEVEL,
	        "vxtcom_be_probe called: %s, dev %p, xenbus_dev_id %p\n",
	        dev->otherend, dev, id);
	vxtcom = vxtcom_be_alloc(dev);
	if (IS_ERR(vxtcom)) {
		err = PTR_ERR(vxtcom);
		dev->dev.driver_data = vxtcom;
		vxtcom = NULL;
		xenbus_dev_fatal(dev, err, "creating vxtcom_ctrlr interface");
		goto fail;
	}
	dev->dev.driver_data = vxtcom;

	UMI_LOG(153, VXTPRINT_BETA_LEVEL,
	        " switching xenbus state to Initialized\n");
	err = xenbus_switch_state(dev, XenbusStateInitialised);
	if (err)
		goto fail;
	UMI_LOG(154, VXTPRINT_DEBUG2_LEVEL,
	        "returning from xenbus_switch_state successfully\n");
	return 0;

fail:
	UMI_WARN(155, VXTPRINT_PRODUCT_LEVEL, "failed\n");
	vxtcom_be_remove(dev);
	return err;
}


/*
 * vxtcom_be_frontend_changed:
 *
 * Xenbus registered callback, invoked when the frontend's state changes.
 *
 */

static void vxtcom_be_frontend_changed(struct xenbus_device *dev,
			     enum xenbus_state frontend_state)
{
	vxtbus_ctrlr_t *vxtcom = dev->dev.driver_data;
	vxt_reinit_t *reinit_parm;
	int err;

	UMI_LOG(156, VXTPRINT_PROFILE_LEVEL,
	        "called, dev %s, state %08x\n", dev->otherend, frontend_state);
	switch (frontend_state) {
	case XenbusStateInitialising:
		UMI_LOG(157, VXTPRINT_PROFILE_LEVEL,
		        "frontend_state: XenbusStateInitialized\n");
		if (dev->state == XenbusStateClosed) {
			UMI_LOG(158, VXTPRINT_BETA_LEVEL,
		                "%s: %s: prepare for reconnect\n",
			        __FUNCTION__, dev->nodename);
			xenbus_switch_state(dev, XenbusStateInitialised);
		}
		break;

	case XenbusStateInitialised:
	case XenbusStateConnected:
		UMI_LOG(159, VXTPRINT_PROFILE_LEVEL,
		        "frontend_state: XenbusStateConnected\n");
		if (vxtcom == NULL) {
			UMI_WARN(160, VXTPRINT_PRODUCT_LEVEL, 
			         "controller is unplugged, unexpected state\n");
			return;
		}
		/* Ensure we connect even when two watches fire in 
		   close successsion and we miss the intermediate value 
		   of frontend_state. */
		if (dev->state == XenbusStateConnected) {
			break;
		}

		err = vxtcom_be_get_bufs(vxtcom);
		if (err) {
			UMI_WARN(161, VXTPRINT_PRODUCT_LEVEL,
			         " unable to get resources from front end\n");
			break;
		}
		vxtcom_be_connect(vxtcom);
/*
 * Test Code:
 * Create a device on the remote controller
 *
 */
#ifdef notdefcdy
{
vxt_msg_create_info_t parms;
vxtdev_handle_t       device_handle;
parms.sendq_len = 8192;
parms.rcvq_len = 8192;
UMI_WARN(162, VXTPRINT_PRODUCT_LEVEL,"Calling vxtcom_test_add_dev\n");
/*
msleep(1000);
*/
UMI_WARN(163, VXTPRINT_PRODUCT_LEVEL,"Calling vxtcom_test_add_dev, waking up from sleep\n");
vxtcom_test_add_dev(vxtcom, VXT_REMOTE_COM, "REMOTE_TEST_DEVICE", 
                    (void *)&parms, sizeof(vxt_msg_create_info_t),
                    &device_handle);
}
{
vxt_msg_create_info_t parms;
parms.sendq_len = 8192;
parms.rcvq_len = 8192;
parms.sendq_bufcnt = 4;
parms.rcvq_bufcnt = 4;
	
msleep(3000);
UMI_WARN(164, VXTPRINT_PRODUCT_LEVEL,
	 "Calling vxtcom_test_remote_add_dev, waking up from sleep\n");

vxtcom_test_remote_add_dev(vxtcom, VXT_REMOTE_COM, "PROXY_CREATE_TEST",
			   (void *)&parms, sizeof(parms)); 
}
	                        
#endif
		break;

	case XenbusStateClosing:
		UMI_LOG(165, VXTPRINT_PROFILE_LEVEL,
		        "frontend_state: XenbusStateClosing\n");
		vxtcom_be_disconnect(vxtcom);
		UMI_LOG(166, VXTPRINT_BETA_LEVEL,
		        "switching xenbus state to Closing\n");
		xenbus_switch_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		 UMI_LOG(167, VXTPRINT_PROFILE_LEVEL,
		         "frontend_state: XenbusStateClosed, "
		         "local state = %d\n", dev->state);

		if ((dev->state != XenbusStateConnected) &&
		    (dev->state != XenbusStateClosing) &&
		    (dev->state != XenbusStateClosed)) {
			/*
			 * The frontend was never opened,
			 * no reason to close it.  We will
			 * sit on our clean device creation
			 * until a frontend driver comes along
			 * to connect with.
			 */
			break;
		}
		xenbus_switch_state(dev, XenbusStateClosed);
		if (!(xenbus_dev_is_online(dev))) {
			device_unregister(&dev->dev);
		}
		/*
		 * Clean up the xenstore record so that the
		 * next remote domain driver will initialize
		 * properly
		 */
		{
			char ctrl_state_dir[50];
			sprintf(ctrl_state_dir,
			        "/local/domain/%d/control/shutdown",
			        dev->otherend_id);
			if (xenbus_exists(XBT_NIL, ctrl_state_dir, "")) {
				/*
				 * We are shutting down
				 * No need to re-initialize xenstore
				 */
				break;
			}
		}
		reinit_parm = vxt_kmalloc(sizeof(vxt_reinit_t *),
		                          VXT_KMEM_KERNEL);
		if (reinit_parm) {
			reinit_parm->parm = (vxtarch_word)dev->otherend_id;
			err = kernel_thread(vxtcom_reinit_xenstore_record,
			                    (void *)reinit_parm, 
			                    CLONE_FS | CLONE_FILES);
        		if (err < 0) {
				UMI_LOG(168, VXTPRINT_PRODUCT_LEVEL,
				        "Could not re-initialize "
				        "xenstore record\n");
			}
		}

		break;
		
	case XenbusStateUnknown:
		UMI_LOG(169, VXTPRINT_PROFILE_LEVEL,
		        "frontend_state: XenbusStateUnknown\n");
		device_unregister(&dev->dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
}


/*
 * vxtcom_be_publish_version
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
 */


static int vxtcom_be_publish_version(vxtbus_ctrlr_t *instance)
{
	struct xenbus_transaction xbt;
	struct xenbus_device *dev = instance->xbdev;
	int ret;

	UMI_LOG(170, VXTPRINT_PROFILE_LEVEL,
	        "called, bus %p\n", instance);
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
 * vxtcom_be_alloc
 *
 * Allocate the data structures for a controller. This creates a vxtbus_ctrlr_t
 * structure that is used by the vxt_com module and this module.
 *
 * Returns:
 *		vxtbus_ctrlr object when successful
 *		-EBADF - Version Mismatch
 *		-ENOMEM - Memory allocation failure
 */

static vxtbus_ctrlr_t *vxtcom_be_alloc(struct xenbus_device *dev)
{
	vxtbus_ctrlr_t *instance;
	int		ret, vdevice;

	/*
	 * If "virtual-device" specified, this specifies the controller
	 * major. If not, vxtcom_ctrlr_init() will allocate a new major.
	 */
	UMI_LOG(171, VXTPRINT_PROFILE_LEVEL,
	        " called, dev %p, remote %s\n", dev, dev->otherend);

	ret = xenbus_scanf(XBT_NIL, dev->otherend, "virtual-device", "%i",
	    &vdevice);
	if (ret != 1 ) {
		UMI_LOG(172, VXTPRINT_BETA_LEVEL,
		        " no default device, pick default major number\n");
		vdevice = 0;
	}


	instance = vxt_cmds.create_controller();
	UMI_LOG(173, VXTPRINT_DEBUG2_LEVEL,
	        "vxtcom_create_controller completed\n");
	if (!instance)
		return ERR_PTR(-ENOMEM);

	if (!instance) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating info structure");
		return ERR_PTR(-ENOMEM);
	}

	if (instance->version != VXTCOM_BUS_CNTRLR_VERSION) {
		/*
		 * Destroy the controller
		 */
		vxt_cmds.dec_bus_ref(instance);
		xenbus_dev_fatal(dev, -EBADF, "version mismatch");
		return ERR_PTR(-EBADF);
	}
	UMI_LOG(174, VXTPRINT_DEBUG2_LEVEL,"version check successful\n");

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
	 * Cannot initiate device creation at this end
	 */
	instance->indirect_create = 1;
	instance->vdevice = vdevice;

	ret = vxtcom_be_publish_version(instance);
	if (ret) {
		/*
		 * Destroy the controller
		 */
		vxt_cmds.dec_bus_ref(instance);
		return ERR_PTR(-EBADF);
	}
	UMI_LOG(175, VXTPRINT_DEBUG2_LEVEL,"publish version return\n");

	ret = vxt_cmds.ctrlr_init(instance);
	if (ret) {
		/*
		 * Destroy the controller
		 */
		vxt_cmds.dec_bus_ref(instance);
		return ERR_PTR(-EBADF);
	}

	/*
	 * Copy in the remote guest's uuid
	 * Must be done after vxtcom_ctrlr_init because
	 * vxtcom_ctrlr_init starts the vxt authorization
	 * database.
	 */

	if (vxt_cmds.db_hub_lookup_spoke(dev->otherend_id,
	    instance->remote_uuid) != VXT_SUCCESS) {
		UMI_WARN(176, VXTPRINT_PRODUCT_LEVEL,
		         " Unable to find remote uuid "
		         "for domain %d\n", dev->otherend_id);
		instance->remote_uuid[0] = 0;
	}


	UMI_LOG(177, VXTPRINT_DEBUG2_LEVEL,"vxtcom ctrlr init return\n");

	/*TODO init any spinlocks */

	return instance;
}


/*
 * unmap_buf_page
 *
 * Unmap the buf page that we were granted access to by the frontend driver.
 */

static void unmap_buf_page(void *buf, grant_handle_t handle)
{
	struct gnttab_unmap_grant_ref op;
	int ret;

	UMI_LOG(178, VXTPRINT_PROFILE_LEVEL, " called, buf %p\n", buf);
	vxt_setup_unmap_parms(&op, (unsigned long)buf,
			      GNTMAP_host_map, handle);

	ret = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1);
	ASSERT(ret == 0);
}


/*
 * map_frontend_pages
 *
 * Map the 2 buffers that the frontend granted us access to.
 */

static int map_frontend_pages(vxtbus_ctrlr_t *vxtcom, unsigned long inbuf,
                              unsigned long outbuf, unsigned long sigref)
{
	struct gnttab_map_grant_ref op;
	int ret;

	UMI_LOG(179, VXTPRINT_PROFILE_LEVEL,
	        " called, bus %p, in %lu, out %lu, sig %lu\n",
                 vxtcom, inbuf, outbuf, sigref);
	vxt_setup_map_parms(&op, (unsigned long)vxtcom->inq,
	                    GNTMAP_host_map, inbuf,
	                    vxtcom->xbdev->otherend_id);

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1);
	ASSERT(ret == 0);

	if (op.status) {
		UMI_ERROR(180, VXTPRINT_PRODUCT_LEVEL,
		          " Grant table operation failure!, status %08x\n",
		          op.status);
		return op.status;
	}

	vxtcom->inref = inbuf;
	vxtcom->inbuf_handle = op.handle;

	vxt_setup_map_parms(&op, (unsigned long)vxtcom->outq,
	                    GNTMAP_host_map, outbuf,
	                    vxtcom->xbdev->otherend_id);

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1);
	ASSERT(ret == 0);

	if (op.status) {
		UMI_ERROR(181, VXTPRINT_PRODUCT_LEVEL,
		          " Grant table operation failure!, status %08x\n",
		          op.status);
		unmap_buf_page(vxtcom->inq, vxtcom->inbuf_handle);
		return op.status;
	}

	vxtcom->outref = outbuf;
	vxtcom->outbuf_handle = op.handle;

	UMI_LOG(182, VXTPRINT_DEBUG2_LEVEL,"sanity check test\n");
	UMI_LOG(183, VXTPRINT_DEBUG2_LEVEL,
	        "sanity check test inbuf %s\n",
	        (char *)(((vxtarch_word)vxtcom->inq) + 100));
	UMI_LOG(184, VXTPRINT_DEBUG2_LEVEL,
	        "sanity check test outbuf %s\n",
	        (char *)(((vxtarch_word)vxtcom->outq) + 100));

	vxt_setup_map_parms(&op, (unsigned long)vxtcom->sig_buf,
	                    GNTMAP_host_map, sigref,
	                    vxtcom->xbdev->otherend_id);

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1);
	ASSERT(ret == 0);

	if (op.status) {
		UMI_ERROR(185, VXTPRINT_PRODUCT_LEVEL,
		          " Grant table operation failure!, status %08x\n",
		          op.status);
		return op.status;
	}

	vxtcom->sigbuf_ref = sigref;
	vxtcom->sigbuf_handle = op.handle;

	/*
	 * Setup vxtcom bus structure fields for the signal buffer
	 * 
         * The signal buffer is split into two pieces
         * allowing simultaneous signalling in both
         * directions.
         *
         * We initialize the part that we will treat
         * as our send, or signalling buffer.
         */
        vxtcom->sig_write = (void *)(((char *)vxtcom->sig_buf) +
                            VXTSIG_SEC_BUFF_OFFSET);
        vxtcom->sig_read = vxtcom->sig_buf;
        vxt_cmds.sig_init_send_buf(vxtcom->sig_write);

	return 0;
}


/*
 * vxtcom_map_fe_buffers
 *
 * Allocates vm areas for the input and output buffers and maps the grant
 * references that we have been passed from the frontend driver. Also binds the
 * passed event channel to a virtual irq.
 */
int vxtcom_map_fe_buffers(vxtbus_ctrlr_t *vxtcom, unsigned long inbuf,
                          unsigned long outbuf, unsigned int evtchn,
                          unsigned long sigref, unsigned int sig_evt)
{
	int err;

	UMI_LOG(186, VXTPRINT_PROFILE_LEVEL, 
	        " called, bus %p, in %lu, out %lu, sig %lu, "
	        "cfgint %08x, sigint %08x\n", 
	        vxtcom, inbuf, outbuf, sigref, evtchn, sig_evt);

	/* Already connected through? */
	if (vxtcom->irq)
		return 0;

	if ( (vxtcom->inbuf_area = alloc_vm_area(PAGE_SIZE)) == NULL ) {
		return -ENOMEM;
	}

	if ( (vxtcom->outbuf_area = alloc_vm_area(PAGE_SIZE)) == NULL ) {
		free_vm_area(vxtcom->inbuf_area);
		return -ENOMEM;
	}

	if ( (vxtcom->sigbuf_area = alloc_vm_area(PAGE_SIZE)) == NULL ) {
		free_vm_area(vxtcom->inbuf_area);
		free_vm_area(vxtcom->outbuf_area);
		return -ENOMEM;
	}

	vxtcom->inq = vxtcom->inbuf_area->addr;
	vxtcom->outq = vxtcom->outbuf_area->addr;
	vxtcom->sig_buf = vxtcom->sigbuf_area->addr;

	err = map_frontend_pages(vxtcom, inbuf, outbuf, sigref);
	if (err) {
		free_vm_area(vxtcom->inbuf_area);
		vxtcom->inbuf_area = NULL;
		free_vm_area(vxtcom->outbuf_area);
		vxtcom->outbuf_area = NULL;
		free_vm_area(vxtcom->sigbuf_area);
		vxtcom->sigbuf_area = NULL;
		vxtcom->inq = NULL;
		vxtcom->outq = NULL;
		vxtcom->sig_read = NULL;
		vxtcom->sig_write = NULL;
		return err;
	}

#ifdef EXPLICIT_FOREIGN_EVTCHN_BIND
{
	struct evtchn_bind_interdomain bind_interdomain;
	
	bind_interdomain.remote_dom  = vxtcom->xbdev->otherend_id;
	bind_interdomain.remote_port = evtchn;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain,
	                                  &bind_interdomain);

	if (err) {
		xenbus_dev_fatal(vxtcom->xbdev, err,
		                 "bind_interdomain failed");
		goto fail;
	}

	vxtcom->evtchn = bind_interdomain.local_port;

	err = bind_evtchn_to_irqhandler(vxtcom->evtchn, vxtcom_be_interrupt,
	                                0, "vxtcom_ctrlr-backend", vxtcom);
	if (err <= 0) {
		xenbus_dev_fatal(vxtcom->xbdev, err,
		    "bind_interdomain_evtchn_to_irqhandler failed");
		goto fail;
	}
	vxtcom->irq = err;

	/*
	 * Now set up the signaling event
	 */
	bind_interdomain.remote_dom  = vxtcom->xbdev->otherend_id;
	bind_interdomain.remote_port = sig_evt;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain,
	                                  &bind_interdomain);

	if (err) {
		xenbus_dev_fatal(vxtcom->xbdev, err,
		                 "bind_interdomain failed");
		goto fail;
	}

	vxtcom->sig_evt = bind_interdomain.local_port;

	err = bind_evtchn_to_irqhandler(vxtcom->sig_evt, vxtcom_be_sig_intr,
	                                0, "vxtcom_sig_backend", vxtcom);
	if (err <= 0) {
		xenbus_dev_fatal(vxtcom->xbdev, err,
		    "bind_interdomain_evtchn_to_irqhandler failed");
		goto fail;
	}
	vxtcom->irq = err;
}
#else
	err = bind_interdomain_evtchn_to_irqhandler(
	    vxtcom->xbdev->otherend_id, evtchn,
	    vxtcom_be_interrupt, 0, "vxtcom_ctrlr-backend", vxtcom);
	if (err <= 0) {
		xenbus_dev_fatal(vxtcom->xbdev, err,
		    "bind_interdomain_evtchn_to_irqhandler failed");
		goto fail;
	}
	vxtcom->irq = err;
	/*
	 * Fill in the evtchn for slightly faster signalling
	 */
	vxtcom->evtchn = irq_to_evtchn_port(vxtcom->irq);

	err = bind_interdomain_evtchn_to_irqhandler(
	    vxtcom->xbdev->otherend_id, sig_evt,
	    vxtcom_be_sig_intr, 0, "vxtcom_sig_backend", vxtcom);
	if (err <= 0) {
		xenbus_dev_fatal(vxtcom->xbdev, err,
		    "bind_interdomain_sigevt_to_irqhandler failed");
		goto fail;
	}
	vxtcom->sig_irq = err;
	/*
	 * Fill in the evtchn for slightly faster signalling
	 */
	vxtcom->sig_evt = irq_to_evtchn_port(vxtcom->sig_irq);
#endif

	return 0;
fail:
	unmap_buf_page(vxtcom->inq, vxtcom->inbuf_handle);
	vxtcom->inq = NULL;
	free_vm_area(vxtcom->inbuf_area);
	vxtcom->inbuf_area = NULL;
	unmap_buf_page(vxtcom->outq, vxtcom->outbuf_handle);
	vxtcom->outq = NULL;
	free_vm_area(vxtcom->outbuf_area);
	vxtcom->outbuf_area = NULL;
	free_vm_area(vxtcom->sigbuf_area);
	vxtcom->sigbuf_area = NULL;
	vxtcom->sig_read = NULL;
	vxtcom->sig_write = NULL;
	return err;

}

/*
 * syscom_be_disconnect
 *
 * Disconnect a controller. Unbinds the irq and unmaps buffers.
 */ 
void vxtcom_be_disconnect(vxtbus_ctrlr_t *vxtcom)
{
	UMI_LOG(187, VXTPRINT_PROFILE_LEVEL, " called on %s, bus %p\n",
	        vxtcom->xbdev->otherend, vxtcom);
	/* 
	 * Flush gnttab callback work. 
	 * Must be done with no locks held.
 	 * We must remove work before we attempt to use
	 * inbuf/outbuf and a stale vxtctrlr
	 */
	flush_scheduled_work();

	if (vxtcom->irq) {
		unbind_from_irqhandler(vxtcom->irq, vxtcom);
		vxtcom->irq = 0;
		vxtcom->evtchn = 0;
	}

	if (vxtcom->sig_irq) {
		unbind_from_irqhandler(vxtcom->sig_irq, vxtcom);
		vxtcom->sig_irq = 0;
		vxtcom->sig_evt = 0;
	}

	if (vxtcom->inbuf_area) {
		unmap_buf_page(vxtcom->inq, vxtcom->inbuf_handle);
		free_vm_area(vxtcom->inbuf_area);
		vxtcom->inbuf_area = NULL;
		vxtcom->inq = NULL;
	}

	if (vxtcom->outbuf_area) {
		unmap_buf_page(vxtcom->outq, vxtcom->outbuf_handle);
		free_vm_area(vxtcom->outbuf_area);
		vxtcom->outbuf_area = NULL;
		vxtcom->outq = NULL;
	}

	if (vxtcom->sigbuf_area) {
		unmap_buf_page(vxtcom->sig_buf, vxtcom->sigbuf_handle);
		free_vm_area(vxtcom->sigbuf_area);
		vxtcom->sigbuf_area = NULL;
		vxtcom->sig_buf = NULL;
		vxtcom->sig_read = NULL;
		vxtcom->sig_write = NULL;
	}
}


/*
 * vxtcom_be_sig_intr:
 *
 * Process interrupt for virtual device signals from the
 * front end.
 *
 * Note: We made the dev_events hash table part of the
 * bus ctrlr structure.  In future when we have a list of
 * these for all of the endpoints this code should move
 * to a list off kept off of the vxtcom controller structure
 * The actual field sent here should be the hash instance
 * not the bus controller.
 * 
 */

static irqreturn_t
vxtcom_be_sig_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)dev_id;
	UMI_LOG(188, VXTPRINT_DEBUG2_LEVEL," called, bus %p\n", instance);
	/*
	 * Process the signals generated by the remote
	 * controller
	 */
	vxt_cmds.sig_interrupt(instance->dev_events, instance->sig_read);
	return IRQ_HANDLED;
}


/*
 * vxtcom_be_interrupt
 *
 * Process interrupt from frontend. 
 */

DEFINE_SPINLOCK(config_io_lock);

static irqreturn_t vxtcom_be_interrupt(int irq, void *dev_id,
    struct pt_regs *regs)
{

	vxtbus_ctrlr_t *instance = (vxtbus_ctrlr_t *)dev_id;
	unsigned long flags;

	UMI_LOG(189, VXTPRINT_DEBUG_LEVEL,
	        "vxtcom_be_interrupt called, bus %p\n", instance);
	
	spin_lock_irqsave(&config_io_lock, flags);

	if (unlikely(instance->connected != VXTCOM_STATE_CONNECTED)) {
		UMI_LOG(190, VXTPRINT_PRODUCT_LEVEL,
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


	spin_unlock_irqrestore(&config_io_lock, flags);
	return IRQ_HANDLED;
}


/*
 * vxtcom_be_connect
 *
 * Check frontend and backend have compatible version numbers and the
 * Complete instance initialisation and switch to Connected state.
 *
 * Returns:
 *
 *		None
 */

static void vxtcom_be_connect(vxtbus_ctrlr_t *vxtcom)
{
	int err;
	struct xenbus_device *dev = vxtcom->xbdev;
	int version;

	UMI_LOG(191, VXTPRINT_PROFILE_LEVEL,
	        "%s on bus %p", dev->otherend, vxtcom);

	err = xenbus_gather(XBT_NIL, vxtcom->xbdev->otherend,
		"version", "%lu", &version, NULL);
	if (err) {
		xenbus_dev_fatal(vxtcom->xbdev, err,
		    "reading backend fields at %s", vxtcom->xbdev->otherend);
		return;
	}

	if (version != vxtcom->version) {
		xenbus_dev_fatal(vxtcom->xbdev, -EINVAL,
		    "incompatible versions frontend %lu, backend %lu",
		    vxtcom->version, version);
		return;
	}

	/*
	 * Initialise buffer headers and output buffer
	 */
	vxtcom->inq_header.msg_id = 0;
	vxtcom->outq_header.msg_id = 1;
	vxtcom->outq_header.reply_in_use = 0;
	vxtcom->outq_header.request_in_use = 0;
	/*
	 * Use these two fields as a way of
	 * restarting processing when the controller lock
	 * stops interrupt level queue handling.
	 * The flags are checked on controller lock
	 * release.
	 */
	vxtcom->inq_header.reply_in_use = 0;
	vxtcom->inq_header.request_in_use = 0;
	vxtcom->request_offset = 0;
	vxtcom->reply_offset = 0;

	vxt_cmds.initialize_send_queue((void *)vxtcom->outq, PAGE_SIZE);

	UMI_LOG(192, VXTPRINT_BETA_LEVEL,"switch state to Connected\n");
	err = xenbus_switch_state(dev, XenbusStateConnected);
	if (err)
		xenbus_dev_fatal(dev, err, "switching to Connected state",
				 dev->nodename);

	/*
	 * Push the vxtcom structure onto a Domain based lookup
	 * list.  This is of limited value for front-ends where
	 * the number of controller connections is limited to
	 * the number of connections to controlling authorities
	 * however it is the means by which guest to guest
	 * connections are made and transport configuration
	 * chain messages sent for the back-end.
	 */

	err = vxt_cmds.add_ctrlr_pair(vxtcom, dev->otherend_id);
	if (err) {
		xenbus_dev_fatal(dev, err, "adding controller pair",
				 dev->nodename);
	}


	vxtcom->connected = VXTCOM_STATE_CONNECTED;
	vxt_cmds.bus_connect(vxtcom);

        if (vxtcom->pending_config_event == 1) {
                vxtcom_be_interrupt(vxtcom->irq, (void *)vxtcom,
                                    (struct pt_regs *) NULL);
        }

	return;
}


/*
 * vxtcom_be_get_bufs
 *
 * Obtain the buffer grant references and event channel that were published
 * by the frontend and then call vxtcom_map_fe_buffers() to map the buffers
 * and bind the event channel to an irq.
 * Note that the frontend publishes its input and output buffer references as
 * inbuf-ref and outbuf-ref and that these are our output and input buffers
 * respectively.
 */

static int vxtcom_be_get_bufs(vxtbus_ctrlr_t *vxtcom)
{
	struct xenbus_device *dev = vxtcom->xbdev;
	unsigned long        inbuf_ref, outbuf_ref, sigbuf_ref;
	unsigned int         evtchn, sig_evt;
	int                  err;

	UMI_LOG(193, VXTPRINT_PROFILE_LEVEL,
	        "%s on bus %p", dev->otherend, vxtcom);

	err = xenbus_gather(XBT_NIL, dev->otherend, "inbuf-ref", "%lu",
	    &outbuf_ref, "outbuf-ref", "%lu", &inbuf_ref, "event-channel",
	    "%u", &evtchn, "sigbuf-ref", "%lu", &sigbuf_ref, 
	    "vxtdev_signal_event", "%u", &sig_evt, NULL);

	if (err) {
		xenbus_dev_fatal(dev, err,
		    "reading %s/inbuf-ref, outbuf_ref, event-channel, and sig fields",
		    dev->otherend);
		return err;
	}
	UMI_LOG(194, VXTPRINT_DEBUG_LEVEL,
	        "sigbuf_ref %lu, sig_evt %u\n", sigbuf_ref, sig_evt);

	/* Map the shared buffers, irq etc. */
	UMI_LOG(195, VXTPRINT_DEBUG2_LEVEL,"after gather before map\n");
	err = vxtcom_map_fe_buffers(vxtcom, inbuf_ref, outbuf_ref, evtchn,
	                            sigbuf_ref, sig_evt);
	if (err) {
		xenbus_dev_fatal(dev, err,
		    "mapping buffers ref %lu,%lu port %u, remote domain %lu",
		     inbuf_ref, outbuf_ref, evtchn, dev->otherend_id);
		return err;
	}
	UMI_LOG(196, VXTPRINT_DEBUG2_LEVEL, "after map\n");

	return 0;
}



/*
 * Driver Registration
 */


static struct xenbus_device_id vxtcom_be_ids[] = {
	{ "vxtcom_ctrlr" },
	{ "" }
};


static struct xenbus_driver vxtcom_ctrlr = {
	.name = "vxtcom_ctrlr",
	.owner = THIS_MODULE,
	.ids = vxtcom_be_ids,
	.probe = vxtcom_be_probe,
	.remove = vxtcom_be_remove,
	.otherend_changed = vxtcom_be_frontend_changed
};


static ssize_t vxt_show_version(struct class *class, char *buf)
{
	sprintf(buf, "%s\n",  VXT_CARD_BANNER);
	return strlen(buf) + 1;
}

CLASS_ATTR(vxtcard_version, S_IRUGO, vxt_show_version, NULL);

#ifdef NO_XEN_CLASS

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
 *              A handle to the VxT class, or NULL
 */

vxt_device_class_t *get_vxt_class(void)
{
        static vxt_device_class_t *vxt_class;

        if (vxt_class) {
                return vxt_class;
	}

#ifdef LEGACY_4X_LINUX
	vxt_class = class_simple_create(THIS_MODULE, "vxt");
#else
        vxt_class = class_create(THIS_MODULE, "vxt");
#endif

	if (IS_ERR(vxt_class)) {
		UMI_LOG(197, VXTPRINT_PRODUCT_LEVEL,
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
 * vxtcom_be_ctrlr_init:
 *
 * vxtcom_be_ctrlr_init is called by vxtcom_hub_connect
 * Traditionally this function would be called at module
 * load time but driver initialization must be put off until
 * the common VxT controller code is present.
 * vxtcom_be_ctrlr_init registers the Xenbus callback
 * function for the VxT card adapter.  As part of this
 * registration, the Xenbus implementation will call the
 * probe function if there is a virtual VxT card present.
 * The probe will initialize the VxT Card Bus level instance
 * but also the VxT controller that resides in common code.
 * For this reason, the actual registration of the VxT card
 * is delayed until the common module had registered itself. 
 *
 */

static int vxtcom_be_ctrlr_init(void)
{
	int ret;
	UMI_LOG(198, VXTPRINT_PROFILE_LEVEL," called\n");
	if (!is_running_on_xen())
		return -ENODEV;
	UMI_LOG(199, VXTPRINT_BETA_LEVEL,"running on xen\n");
	ret = vxt_cmds.vxtctrlr_module_init();
	if (ret != VXT_SUCCESS) {
		return -ENODEV;
	}

	return xenbus_register_backend(&vxtcom_ctrlr);
}


/*
 * vxtcom_be_ctrlr_cleanup:
 *
 * Traditionally called by vxtcom_hub_disconnect, vxtcom_be_ctrlr_cleanup
 * deregisters the VxT subsystem from Xenbus.  vxtcom_hub_disconnect is called
 * by the common VxT controller module when it is unloading.
 *
 * The unloading of a xenbus driver causes the "remove" xenbus callback.
 * This corresponds to vxtcom_be_remove. vxtcom_be_remove shuts down
 * the common VxT controller as well as the VxT card functions.  Hence
 * it must be called when the common module is being removed.
 *
 * Returns:
 *		None
 * 
 *
 */

static void vxtcom_be_ctrlr_cleanup(void)
{
	UMI_LOG(200, VXTPRINT_PROFILE_LEVEL," called\n");
	xenbus_unregister_driver(&vxtcom_ctrlr);
	vxt_cmds.system_shutdown();
	return;
}


/*
 *
 * vxtcom_be_module_load:
 *
 * Called at module load time, vxtcom_be_module_load does not register
 * the backend with the xenbus subsystem. Instead it initializes the 
 * common callback structure and exits, waiting for the load of the the
 * VxT subsystem common module and its subsequent call to  vxtcom_hub_connect.
 *
 * Returns:
 *
 *		0 => Upon Success
 *		-ENODEV if the module is loaded on a system lacking Xen support
 */

static int __init vxtcom_be_module_load(void)
{
        UMI_LOG(201, VXTPRINT_PROFILE_LEVEL," called\n");
        if (!is_running_on_xen())
                return -ENODEV;
        UMI_LOG(202, VXTPRINT_BETA_LEVEL,"running on xen\n");

	vxt_cmds.vxtctrlr_module_init=NULL;

        return 0;
}

/*
 *
 * vxtcom_be_module_unload:
 *
 * Called at the time of VxT Card module unload, vxtcom_be_module_unload
 * checks to see if the common VxT controller module still has its
 * callback routines registered before exiting.
 *
 * vxtcom_be_module_unload also removes the VxT class if it is not backed
 * by the umbrella xen class.
 *
 * Returns:
 *
 *		None
 *
 */

static void vxtcom_be_module_unload(void)
{
        static vxt_device_class_t *vxt_class;

	if (vxt_cmds.vxtctrlr_module_init != NULL) {
        	UMI_WARN(203, VXTPRINT_PRODUCT_LEVEL,
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
 *              0 => Upon successful invocation
 *              -EBUSY: A common vxtctrlr module is already loaded.
 */

int vxtcom_hub_connect(vxtbus_common_api_t *utilities)
{
	if (vxt_cmds.vxtctrlr_module_init != NULL) {
		return -EBUSY;
	}
	vxt_cmds = *utilities;
	if (vxt_cmds.vxtctrlr_module_init == NULL) {
		UMI_WARN(204, VXTPRINT_PRODUCT_LEVEL,
			 "vxt_cmds did not initialize properly\n");
	}
	return vxtcom_be_ctrlr_init();
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
 *              0
 */

int vxtcom_hub_disconnect(void)
{
	vxtcom_be_ctrlr_cleanup();
	vxt_cmds.vxtctrlr_module_init=NULL;
	return 0;
	
}


module_init(vxtcom_be_module_load);
module_exit(vxtcom_be_module_unload);

MODULE_LICENSE("Dual BSD/GPL");
