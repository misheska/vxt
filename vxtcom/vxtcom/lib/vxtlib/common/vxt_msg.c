/*
 * vxt_msg.c
 *
 * This module interacts with the vxt_com communication controller
 * It connects specifically to the vxt_msg_dev.  The vxt_com 
 * controller vends different devices from a logical bus.
 * vxt_msg queries the bus to find compatible devices to 
 * connect to. It also requests the addition of new devices
 * and requests connection to remote endpoints.  Interaction
 * with the vxt_com system is done by opening the vxt_com device
 * file.
 * 
 * A connection consists of a piece of shared memory and an event
 * channel to signal on.  vxt_msg_dev runs its data transfers 
 * through the vxt_queue communication library.  This library
 * imposes the communication model on the shared memory transport.
 * 
 * vxt_msg exports a socket interface vxt_msg can be built as
 * a standalone library or as a linked part of an application.
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

#define _VXT_COMPONENT_ 2
#define _VXT_SUBSYSTEM_ 12



#include <vxt_lib_os.h>
#include <vxt_system.h>
#include <vxt_msg_export.h>
#include <symcom_dev_table.h>
#include <vxt_com.h>
#include <vxt_msg_dev.h>
#include <vxt_msg.h>
#include <vxt_queue.h>
#include <vxt_msg_os.h>



/*
 * We add our cookie to the header
 * to guarantee initialization completion
 * on the connect side and to identify
 * the type 1 vxt queue as a VXTMSG
 * queue.
 *
 * There is an 8 byte reserved field.  We
 * will use the last byte as a string 
 * terminator.
 */
char vxtmsg_queue_cookie[] = "XXVMSG1";

vxtctrlr_handle_t vxtctrlr_fd = VXT_INVALID_DEV_OBJ;


typedef struct vxt_signal_parms {
	vxtctrlr_handle_t ctrlr;
	uint64_t dev;
} vxt_signal_parms_t;

typedef struct vxt_wait_parms {
	vxtctrlr_handle_t ctrlr;
	uint64_t dev;
} vxt_wait_parms_t;

typedef struct vxt_msg_dev_instance {
	int                version;
	int                new;  /* just initialized */
	vxt_dev_type       type;  /* for debug */
	uint64_t           slot;
	uint64_t           ctrlr_id;
	vxtctrlr_handle_t  ctrlr_fd;
	vxtctrlr_handle_t  dev_fd;
	void               *sendq;
	void               *send_buf;
	uint64_t           sendq_token;
	int                sendq_size;
	int                sendq_bufcnt;
	int                send_sub_size;
	void               *rcvq;         /* loc of shared memory recv queue */
	void               *rcv_buf;
	uint64_t           rcvq_token;
	int                rcvq_size;     /* in bytes */
	int                rcvq_bufcnt;
	int                rcv_sub_size;
	int                remote_dom;
	char               ep_id[MAX_VXT_UUID];
	char               uname[MAX_VXT_UNAME];  /* name of remote endpoint */
	char               file_name[VXTCOM_MAX_DEV_FILE_NAME];
	uint64_t           dev_handle;
	vxt_signal_parms_t signal_cb;
	vxt_wait_parms_t   wait_cb;
	void               *partial_send;
	int                ps_offset;
	int                ps_len;
	int                ps_buf_index;
	void               *partial_rcv;
	int                pr_offset;
	int                pr_len;
	int                pr_buf_index;
	struct vxt_msg_dev_instance *next;
} vxt_msg_dev_instance_t;

static int vxt_signal(void *info)
{
	vxt_signal_parms_t *parms;

	parms = (vxt_signal_parms_t *)info;

	UMI_LOG(41, VXTPRINT_DEBUG_LEVEL, "calling ioctl\n");
	if (vxt_msg_ioctl(parms->ctrlr, IOCTL_VXTCTRLR_SIGNAL_DEV, 
	                  (void *)(vxtarch_word)parms->dev)) {
		UMI_WARN(77, VXTPRINT_PRODUCT_LEVEL,
		         "Unable to signal data present\n");
	}

	return VXT_SUCCESS;
}



vxt_msg_dev_instance_t *live_connections = NULL;

static int vxt_wait(void *info)
{
	vxt_wait_parms_t *parms;
	int              ret;

	parms = (vxt_wait_parms_t *)info;

	if ((ret = vxt_msg_ioctl(parms->ctrlr, IOCTL_VXTCTRLR_WAIT_DEV, 
	                  (void *)(vxtarch_word)parms->dev))) {
		UMI_WARN(78, VXTPRINT_PRODUCT_LEVEL,
		         "Unable to wait for data, ret = %d\n", ret);
	}

	return VXT_SUCCESS;
}

int
vxt_msg_init()
{

	/* 
	 * Open the default vxt_com_cntrlr
	 * We assume the device node has already been set up
	 */
	 if ((vxtctrlr_fd =
	         vxt_msg_open_com(VXT_COM_BUS_NAME,
	                          VXT_RDWR)) == VXT_INVALID_DEV_OBJ) {
		UMI_WARN(79, VXTPRINT_PRODUCT_LEVEL,
		         "open failed %s\n", VXT_COM_BUS_NAME);
	 	return VXT_FAIL;
	 }

	return VXT_SUCCESS;
   
}


/*
 *
 * vxt_msg_ctrlr_plug_event:
 *
 * wait on controller plug events, i.e. vxt controller
 * creation or deletion.  
 *
 * vxt_msg_ctrlr_plug_event is a specialized poll.  It
 * does not wait on traditional read and write state.
 * Instead, it re-uses the fields to indicate the
 * instantiation of a new, controller, the shutdown of
 * a controller, or the issuance of a suspend command.
 * The following flags are active and returned in the
 * revent field:
 *
 *	POLL_VXT_NEW_CTRLR  
 *	POLL_VXT_CTRLR_DIED
 *	POLL_VXT_SUSPEND
 *
 * vxt_msg_ctrlr_plug_event accepts a timeout as an
 * incoming parameter.  If the value given is -1, the
 * poll call will wait until an event occurs.  Otherwise
 * the timeout value which is read as milleseconds, will
 * cause the poll call to wait for a signal for the 
 * specified time before returning.
 *
 * Returns:
 *
 *		VXT_SUCCESS => The poll action executed successfully
 *		VXT_PARM - poll returned EBADF, EINVAL or EFAULT
 *		VXT_ABORT - poll returned EINTER
 *		VXT_NOMEM - poll returned ENOMEM
 * 
 *
 */

int
vxt_msg_ctrlr_plug_event(int timeout, int *revent)
{
	return vxt_msg_event_wait(vxtctrlr_fd, timeout, revent);
}


/*
 *
 * vxt_msg_poll_ctrlr:
 *
 * wait on controller new device events,
 *
 * vxt_msg_poll_ctrlr can be called with or without an 
 * established connection.  In the case of an established connection
 * the file descriptor for the controller associated with the 
 * connection is used for the poll action.  In this case, the
 * poll will be signalled only on creation of devices of the same
 * type as that associated with the connection.  In the case of
 * an anonymous call, a default controller will be opened.  This
 * file descriptor poll will be signalled by any new device event.
 *
 * Returns:
 *
 *		VXT_SUCCESS => The poll action executed successfully
 *		VXT_DOWN - There are no active controllers
 *		VXT_PARM - couldn't open the default controller or
 *		           poll returned EBADF, EINVAL or EFAULT
 *		VXT_ABORT - poll returned EINTER
 *		VXT_NOMEM - poll returned ENOMEM
 * 
 *
 */

int
vxt_msg_poll_ctrlr(void *connection, int timeout, int *revent)
{
	vxt_msg_dev_instance_t *device;
	vxt_query_bus_struct_t  query;
	vxtctrlr_handle_t       ctrlr_fd = 0;
	vxt_poll_focus_struct_t focus;
	char ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME + 6];
	int                     ret;

	device = (vxt_msg_dev_instance_t *)connection;
	UMI_LOG(42, VXTPRINT_DEBUG_LEVEL,"called\n");


	if (device == NULL) {
		UMI_LOG(43, VXTPRINT_DEBUG_LEVEL, " Use default controller\n");
		query.function = VXTCTRLR_CTRLR_LOOKUP;
		query.controller = 0;  /* start from the top */
		query.ctrlr_name[0] = 0;  /* start from the top */

		if (vxt_msg_ioctl(vxtctrlr_fd,
		                  IOCTL_VXTCTRLR_QUERY_BUS, &query)) {
			int saved_errno = errno;
			UMI_LOG(80, VXTPRINT_PRODUCT_LEVEL,
			        "Unable to query the vxt com controller\n");
			errno = saved_errno;
			UMI_LOG(81, VXTPRINT_PRODUCT_LEVEL,
			        "VXTCTRLR_CTRLR_LOOKUP failed\n");
			return VXT_DOWN;
		}

		if (query.controller == 0) {
			UMI_WARN(82, VXTPRINT_PRODUCT_LEVEL,
			         "VXTCTRLR_CTRLR_LOOKUP, no live cntrlrs\n");
			return VXT_DOWN;
		}
		sprintf(ctrlr_name, "%s%s", VXT_DEV_DIR, query.ctrlr_name);
        	if ((ctrlr_fd =
		        vxt_msg_open_com(ctrlr_name,
		                         VXT_RDWR)) == VXT_INVALID_DEV_OBJ) {
			UMI_WARN(83, VXTPRINT_PRODUCT_LEVEL,
			         "couldn't open default controller\n");
			return VXT_PARM;
		}

		/*
		 * When we are doing our wait on an ad-hoc controller
		 * there is a special caveat.  We don't confine our
		 * wait to devices specific to that controller, we
		 * wake on any available device.
		 *
		 * Set up the poll focus so that any listen done on 
		 * this descriptor will end up waiting for any new
 		 * device within the VXT subsystem.
		 */
		focus.dev_type = VXT_UNIVERSAL;
		if (vxt_msg_ioctl(ctrlr_fd, 
		                  IOCTL_VXTCTRLR_POLL_FOCUS, &focus))  {
			UMI_WARN(84, VXTPRINT_PRODUCT_LEVEL,
			         "Unable to set poll focus\n");
			vxt_msg_close_com(ctrlr_fd);
			return VXT_FAIL;
		}
		ret = vxt_msg_event_wait(ctrlr_fd, timeout, revent);
		vxt_msg_close_com(ctrlr_fd);
	} else {
		ret = vxt_msg_event_wait(vxtctrlr_fd, timeout, revent);
	}

	return ret;
}


/*
 *
 * vxt_msg_add:
 *
 * Adds a new vxt_msg device to the vxt controller and
 * attaches it to the requested endpoint.
 *
 * If a device connecting the targeted endpoint already
 * exists the call returns a VXT_BUSY.  Endpoints
 * are unique and account for device type, targeted
 * endpoint and instance.
 *
 * Returns:
 *
 *     VXT_SUCCESS => 0
 *
 *     VXT_DOWN - The vxt controller cannot be opened
 *     VXT_BUSY - A device is already associated with
 *                the targeted endpoint.
 *     VXT_FAIL - The vxt controller create call failed
 *     VXT_NOMEM - Cannot allocate the new device structure
 *
 */

int
vxt_msg_add(vxt_dev_type dev, vxt_dev_addr_t *endpoint, 
            int endpoint_length,
            vxt_msg_buf_parms_t *buf_parms, void **connection)
{
	vxt_query_bus_struct_t query;
	vxt_query_dev_struct_t query_dev;
	vxt_query_controller_struct_t ctrlr_request;
	vxt_poll_focus_struct_t focus;
	char ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME + 6];
	vxt_create_dev_struct_t create_request;
	vxt_attach_dev_struct_t attach_dev;
	vxt_msg_create_info_t create_dev_info;
	vxt_msg_dev_info_t msg_dev_info;
	vxt_msg_attach_info_t attach_info;
	vxt_msg_dev_instance_t *new_device;
	vxtctrlr_handle_t ctrlr_fd;
	char *cookie;
	int ret;

	if (vxtctrlr_fd == VXT_INVALID_DEV_OBJ) {
		/*
		 * No access to the Symantec transport
		 * device 
		 */
		return VXT_DOWN;
	}

	/*
	 * Now find the specific controller that "reaches"
	 * the endpoint.  If the call fails, try to connect with
	 * the default device.
	 */
	/*
	 * Note:  In the case where a uname is provided, an endpoint
	 * id also accompanies.  In this way we can direct the device
	 * to the proper point to point connection.  For guests, where
	 * all connections go through the Dom0 service authority, this
	 * is not critical.  The Dom0 controller will look up the guest 
	 * endpoint and device to find the proper route.  For the
	 * Dom0 case, (or the authentication service endpoint when
	 * such is disassociated from Dom0), the endpoint id is 
	 * absolutely necessary as it is part of the identification 
	 * key for the connection.  i.e. we are directing from the HUB.
	 */
	vxtcom_strncpy(ctrlr_request.uname, endpoint->uname, MAX_VXT_UNAME);
	vxtcom_strncpy(ctrlr_request.ep_id, endpoint->ep_id,  MAX_VXT_UUID);
	ctrlr_request.name_match_length = 0; /* disable prefix only  check */
	ctrlr_request.remote_domain = 0;     /* not available */

	if (vxt_msg_ioctl(vxtctrlr_fd, IOCTL_VXTCTRLR_LOOKUP, &ctrlr_request)) {
		/*
		 * Couldn't find using name, try default controller
		 */
		vxtcom_strncpy(ctrlr_name, 
		               VXT_COM_DEV_NAME, VXTCOM_MAX_DEV_FILE_NAME);
	} else {
		sprintf(ctrlr_name, "%s%s", VXT_DEV_DIR, 
		        ctrlr_request.ctrlr_name);
	}

        if ((ctrlr_fd = vxt_msg_open_com(ctrlr_name,
	                                 VXT_RDWR)) == VXT_INVALID_DEV_OBJ ) {
		return VXT_PARM;
	}


	/*
	 * Query for an existing device, if one exists return
	 * with a VXT_BUSY response.
	 * If none is found, attempt to add the device.
	 */

	query.function = VXTCTRLR_LOOKUP_NAME;
	vxtcom_strncpy(query.ep_id, endpoint->ep_id, MAX_VXT_UUID);
	query.name_match_length = 0; /* disable prefix only  check */
	vxtcom_strncpy(query.uname, endpoint->uname, MAX_VXT_UNAME);
	query.dev_name = dev;    /* The device type, this field is */
	                         /* input/output */
	query.vxt_bus_id = VXTCTRLR_NULL_DEV;  /* this field is input/output */


	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_QUERY_BUS, &query)) {
		int saved_errno = errno;
		UMI_WARN(85, VXTPRINT_PRODUCT_LEVEL,
		         "Unable to query the vxt communication ctrlr\n");
		vxt_msg_close_com(ctrlr_fd);
		errno = saved_errno;
		return VXT_DOWN;
	}

	if (query.vxt_bus_id != VXTCTRLR_NULL_DEV) {
		vxt_msg_close_com(ctrlr_fd);
		return VXT_BUSY;
	}

	
	
	UMI_LOG(44, VXTPRINT_DEBUG_LEVEL,"Attempting VXTCTRLR_DEV_CREATE\n");
	/*
	 * We do not at present need to send the endpoint, ep_id.  This
	 * is the "connect domain" in the add request.  The reason is
	 * that our choice of controller indicates our preference in
	 * the case of the HUB and in the case of the guest, all 
	 * routing is done by the authorization service.  The only
	 * justification for enhancement comes with a need to choose
	 * between similarly named services.  We will resist this as
	 * the downside is the loss of topology transparency in the
	 * guest.
	 */
/*
	vxtcom_strncpy(create_request.ep_id, endpoint->ep_id, MAX_VXT_UUID);
*/
	vxtcom_strncpy(create_request.uname, endpoint->uname, MAX_VXT_UNAME);
	create_request.dev_name = dev;   // device type
	create_request.device_info = &create_dev_info;
	create_request.info_length = sizeof(create_dev_info);
	create_dev_info.sendq_len = buf_parms->send_buf_size;
	create_dev_info.rcvq_len = buf_parms->recv_buf_size;
	create_dev_info.sendq_bufcnt = buf_parms->send_sub_buf_cnt;
	create_dev_info.rcvq_bufcnt = buf_parms->recv_sub_buf_cnt;


	if (vxt_msg_ioctl(ctrlr_fd, 
	                  IOCTL_VXTCTRLR_DEV_CREATE, &create_request)) {
		int saved_errno = errno;
		VXT_PERROR("Unable create a new device\n");
		vxt_msg_close_com(ctrlr_fd);
		errno = saved_errno;
		return VXT_FAIL;
	}

	/*
	 * Create a local instance to track the resources and
	 * state of the new message device
	 */
	new_device = vxt_malloc(sizeof(vxt_msg_dev_instance_t));
	if (new_device == NULL) {
		vxt_msg_close_com(ctrlr_fd);
		return VXT_NOMEM;
	}

	new_device->ctrlr_fd = ctrlr_fd;
	new_device->dev_fd = VXT_INVALID_DEV_OBJ;

	/*
	 * 
	 * Time to connect the device to its remote counterpart
	 *
	 */
	attach_dev.vxt_bus_id = create_request.vxt_bus_id;
	attach_dev.device_info = &attach_info;
	attach_dev.info_buf_size = sizeof(attach_info);
	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_ATTACH, &attach_dev) ||
	                  (attach_info.status != VXT_MSG_ATTACH_SUCCESS))  {
		int saved_errno = errno;
		VXT_PERROR("Unable to connect to the vxt com device\n");
		vxt_msg_close_com(ctrlr_fd);
		vxt_free(new_device);
		errno = saved_errno;
		return VXT_FAIL;
	}

	/*
	 * Now query the device to get back the resource
	 * particulars such as queue and event access
	 * tokens.
	 */

	query_dev.vxt_bus_id = create_request.vxt_bus_id;
	query_dev.device_info = &msg_dev_info;
	query_dev.info_buf_size = sizeof(msg_dev_info);
	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_QUERY_DEV, &query_dev))  {
		int saved_errno = errno;
		VXT_PERROR("Unable to query the vxt communication device\n");
		vxt_msg_close_com(ctrlr_fd);
		vxt_free(new_device);
		errno = saved_errno;
		return VXT_FAIL;
	}
	if (msg_dev_info.state == VXT_MSG_DEV_ERROR) {
		/*
		 * Device has suffered a configuration or run-time failure
		 */
		vxt_msg_close_com(ctrlr_fd);
		vxt_free(new_device);
		return VXT_BUSY;
	} 

	new_device->send_buf = NULL;
	new_device->rcv_buf = NULL;

	/* 
	 * Find queues, queue sizes, and event channels
	 */
	new_device->version = VXT_MSG_DEV_VERSION;
	new_device->type = dev;
	new_device->new = 0; /* don't need cookie check on add, only connect */
	new_device->partial_send = NULL;
	new_device->partial_rcv = NULL;
	new_device->ps_buf_index =  VXT_MSG_BUF_INVALID_INDEX; 
	new_device->sendq_token = (uint64_t)(vxtarch_word)msg_dev_info.sendq;
	new_device->sendq_size = msg_dev_info.sendq_size;
	new_device->sendq_bufcnt = msg_dev_info.sendq_bufcnt;
	new_device->rcvq_token = (uint64_t)(vxtarch_word)msg_dev_info.rcvq;
	new_device->rcvq_size = msg_dev_info.rcvq_size;
	new_device->rcvq_bufcnt = msg_dev_info.rcvq_bufcnt;
	new_device->slot = attach_dev.vxt_bus_id;
	new_device->ctrlr_id = (uint64_t)ctrlr_request.controller;
	vxtcom_strncpy(new_device->ep_id, endpoint->ep_id, MAX_VXT_UUID);
	vxtcom_strncpy(new_device->uname, endpoint->uname, MAX_VXT_UNAME);
	vxtcom_strncpy(new_device->file_name, 
	               msg_dev_info.dev_name, VXTCOM_MAX_DEV_FILE_NAME);
	new_device->dev_handle = msg_dev_info.dev_handle;
	/* 
	 * possible intermediate needed by xen, 
	 * allows us to use evtchn device 
	 */
	new_device->remote_dom = msg_dev_info.remote_dom; 

	/*
	 * Set up the poll focus so that any listen done on 
	 * this device will end up waiting for new devices
 	 * on this controller and on this device type.
	 */
	focus.dev_type = dev;
	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_POLL_FOCUS, &focus))  {
		int saved_errno = errno;
		VXT_PERROR("Unable to set poll focus\n");
		new_device->next = live_connections;
		live_connections = new_device;
		vxt_msg_disconnect(new_device, VXT_CLOSE_REMOTE);
		errno = saved_errno;
		return VXT_FAIL;
	}


	/*
	 *  Map in the new queues.  Note we can memory map these
	 *  into any task opening the cntrlr device.  If we 
	 *  want to add security levels, different files wil
	 *  be necessary and level support on the device queue
	 *  responses from the query/connect.
	 *
	 *  NOTE: sendq_token is not actually an offset, it is an
	 *  identifier, telling the controller where to look
	 *  for the associated shared page set.
	 *	
	 *  Note: it is vital to use the mmap2 call and not mmap.
	 *  mmap silently shifts the last parameter by PAGE_SHIFT.
	 *  Since we send our tokens in this field the page shift
	 *  would result in corruption of the value.  mmap2 takes
	 *  a page shifted argument and hence forgoes the internal
	 *  shifting action.
	 */ 

	new_device->send_buf = (void *)
		vxt_mmap(0, new_device->sendq_size, 
		         VXT_PROT_READ | VXT_PROT_WRITE, VXT_MAP_SHARED,
		         ctrlr_fd, (off_t)new_device->sendq_token);

	new_device->rcv_buf = (void *)
		vxt_mmap(0, new_device->rcvq_size,
		         VXT_PROT_READ | VXT_PROT_WRITE, VXT_MAP_SHARED,
		         ctrlr_fd, (off_t)new_device->rcvq_token);
        

	/*
	 * Fill in built in structure for queue signal
	 * callback
	 */
	new_device->signal_cb.dev = new_device->slot;
	new_device->signal_cb.ctrlr = new_device->ctrlr_fd;
	new_device->wait_cb.dev = new_device->slot;
	new_device->wait_cb.ctrlr = new_device->ctrlr_fd;

	/*
	 * We have acquired our resources and established a
	 * connection to the remote endpoint.  We must now
	 * set-up the shared memory queues and put the
	 * signal and wait callbacks in place to allow
	 * queue policy immediate control
	 */ 
 
	/* 
	 * Initialize the send queue * Note: setting hysteresis values to
	 * 0 triggers init_buf to use its defaults
	 * 25% filled for data signal, 75% empty
	 * for space available.
	 *
	 * Note: We are connecting to an existing
	 * device, not adding a device therefore
	 * we expect to find the queues already
	 * initialized.  Also, we must reverse 
	 * our view of PRODUCER.
	 *
	 */
	ret = vxtq_init_buf(new_device->send_buf, new_device->sendq_size, 
	                    sizeof(vxtq_control_t) + vxtmsg_cookie_size,
	                    new_device->sendq_bufcnt, 0, 0, 
	                    VXTQ_LOCAL_IS_PRODUCER,
	                    &vxt_signal, (void *)&new_device->signal_cb,  
	                    &vxt_wait, (void *)&new_device->wait_cb,
	                    &new_device->send_sub_size, 
	                    &new_device->sendq);

	ret = vxtq_init_buf(new_device->rcv_buf, new_device->rcvq_size, 
	                    sizeof(vxtq_control_t) + vxtmsg_cookie_size,
	                    new_device->rcvq_bufcnt, 0, 0, 
	                    0,
	                    &vxt_signal, (void *)&new_device->signal_cb,  
	                    &vxt_wait, (void *)&new_device->wait_cb,
	                    &new_device->rcv_sub_size, 
	                    &new_device->rcvq);


	/*
	 * Tag the buffers and indicate they are initialized.
	 */
	cookie = ((char *)(new_device->send_buf)) + sizeof(vxtq_control_t);
	vxtcom_strncpy(cookie, vxtmsg_queue_cookie, vxtmsg_cookie_size);

	cookie = ((char *)(new_device->rcv_buf)) + sizeof(vxtq_control_t);
	vxtcom_strncpy(cookie, vxtmsg_queue_cookie, vxtmsg_cookie_size);

   

	new_device->next = live_connections;
	live_connections = new_device;
	*connection = (void *)new_device;

	return VXT_SUCCESS;
}


/*
 *
 * vxt_msg_connect:
 *
 *
 * Note: we are including a handle that we return to the caller
 * This allows us to avoid expensive lookups in a local connection
 * setting but allows transparent insertion of fully abstracted
 * handles when necessary.
 *
 * In this setting global endpoint name is expected to be a 
 * universal name identifying an endpoint and an instance.  i.e.
 * no connection between two endpoints can be created without
 * going through the naming authority. The name id might look
 * something like this:
 *    Global Number:  003001005  i.e. We wish to attach to
 *    Guest OS 003001: Instance 5.  (We are the
 *    5th connection on guest 003001, without regard for
 *    the number of connections between these two endpoints
 *    or on our local endpoint in general).
 *
 *
 */

int
vxt_msg_connect(vxt_dev_type dev, vxt_dev_addr_t *endpoint, 
                int endpoint_length, char *target_ctrlr,
                vxt_msg_buf_parms_t *buf_parms, void **connection)
{
	vxt_query_bus_struct_t query;
	vxt_query_controller_struct_t ctrlr_request;
	vxt_poll_focus_struct_t focus;
	char ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME + 6];
	vxt_query_dev_struct_t query_dev;
	vxt_attach_dev_struct_t attach_dev;
	vxt_msg_dev_info_t dev_info;
	vxt_msg_dev_instance_t *new_device;
	vxtctrlr_handle_t ctrlr_fd;
	int ret;

	if (vxtctrlr_fd == VXT_INVALID_DEV_OBJ) {
		/*
		 * No access to the Symantec transport
		 * device 
		 */
		return VXT_DOWN;
	}

	if (!strncmp(target_ctrlr, "", 1)) {
		/*
		 * The user did not supply a reaching controller.  Find 
		 * the specific controller that "reaches" the endpoint.
		 * If the call fails, try to connect with the default device.
		 */
		vxtcom_strncpy(ctrlr_request.uname,
		               endpoint->uname, MAX_VXT_UNAME);
		ctrlr_request.name_match_length = 0; /* disable prefix check */
		UMI_LOG(45, VXTPRINT_DEBUG_LEVEL, 
		        "finding controller for uname %s\n",
		        ctrlr_request.uname);
		ctrlr_request.remote_domain = 0;  /* not available */

		if (vxt_msg_ioctl(vxtctrlr_fd, 
		                  IOCTL_VXTCTRLR_LOOKUP, &ctrlr_request)) {
			/*
			 * Couldn't find using name, try default controller
			 */
			vxtcom_strncpy(ctrlr_name, 
			               VXT_COM_DEV_NAME,
			               VXTCOM_MAX_DEV_FILE_NAME);
		} else {
			sprintf(ctrlr_name, "%s%s", VXT_DEV_DIR,
			        ctrlr_request.ctrlr_name);
		}

		query.function = VXTCTRLR_GLOBAL_LOOKUP;

	} else {
		sprintf(ctrlr_name, "%s%s", VXT_DEV_DIR,
		        target_ctrlr);
		query.function = VXTCTRLR_LOOKUP_NAME;
	}
	UMI_LOG(46, VXTPRINT_DEBUG_LEVEL, "controller name: %s\n", ctrlr_name);

	if ((ctrlr_fd = vxt_msg_open_com(ctrlr_name,
	                                 VXT_RDWR)) == VXT_INVALID_DEV_OBJ) {
		return VXT_PARM;
	}

	/*
	 * Query for an existing device, if one exists connect to it
	 * if none is found, attempt to add the device.
	 * Note: The endpoint not only describes the remote guest
	 * connection point but also a specific connection instance
	 * and hence is unique.
	 */

	query.option = VXTCTRLR_GLOBAL_DEV_NAME;
	vxtcom_strncpy(query.ep_id, endpoint->ep_id, MAX_VXT_UUID);
	vxtcom_strncpy(query.uname, endpoint->uname, MAX_VXT_UNAME);
	query.name_match_length = 0; /* disable prefix only check */
	query.dev_name = dev;    /* The device type, this field is */
	                         /* input/output */
	query.vxt_bus_id = VXTCTRLR_NULL_DEV;  /* this field is input/output */

	/*
	 * First see if the device exists.  If it does the call
	 * returns a bus slot id we can use for further device
	 * level query. (The device ordinal number)
	 */

	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_QUERY_BUS, &query)) {
		int saved_errno = errno;
		UMI_WARN(86, VXTPRINT_PRODUCT_LEVEL,
		         "Global Lookup failed, uname %s, \n", 
		         endpoint->uname);
		VXT_PERROR("Unable to query the vxt communication ctrlr\n");
#ifndef cdy_readyforbindonconnectfail
/*
 * May want to stick with this approach, addding a device under these
 * conditions precludes the use of connect for simple response to new
 * device ready signal.  i.e. As we scan across ctrlr space, we would be
 * creating devices we do not want. In controllers that are not
 * otherwise targeted.
 */
{
	vxt_msg_close_com(ctrlr_fd);
	errno = saved_errno;
	return VXT_DOWN;
}
#endif
		query.vxt_bus_id = VXTCTRLR_NULL_DEV;
	}
	if ((query.vxt_bus_id == VXTCTRLR_NULL_DEV)  
	    || (query.dev_name != dev)) {
		vxt_msg_close_com(ctrlr_fd);
		UMI_WARN(87, VXTPRINT_PRODUCT_LEVEL,
		         "Failing device match id 0x%llx, dev_name 0x%llx\n",
		         (long long)query.vxt_bus_id, 
		         (long long)query.dev_name);
		ret = vxt_msg_add(dev, endpoint, endpoint_length, 
		                  buf_parms, connection);
		return ret;
	}
	UMI_LOG(47, VXTPRINT_DEBUG_LEVEL,
	        "Global Lookup succeeded for dev %s, slot %llu\n",
	        endpoint->uname, (long long) query.vxt_bus_id);
	/*
	 * Check for connected status:  Use the bus id to do a 
	 * device level query.
	 */
	query_dev.vxt_bus_id = query.vxt_bus_id;
	query_dev.device_info = &dev_info;
	query_dev.info_buf_size = sizeof(dev_info);
	UMI_LOG(48, VXTPRINT_DEBUG_LEVEL, "IOCTL_VXT_QUERY_DEV:\n");
	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_QUERY_DEV, &query_dev))  {
		int saved_errno = errno;
		VXT_PERROR("Unable to query the vxt communication device\n");
		errno = saved_errno;
		vxt_msg_close_com(ctrlr_fd);
		return VXT_FAIL;
	}
	if ((dev_info.state == VXT_MSG_DEV_CONNECTED) ||
	    (dev_info.state == VXT_MSG_DEV_ERROR)) {
		UMI_WARN(88, VXTPRINT_PRODUCT_LEVEL,
		         "IOCTL_VXT_QUERY_DEV, device in wrong state 0x%x\n",
		         (uint32_t)dev_info.state);
		/*
		 * Device is either already attached and in-use or
		 * has suffered a configuration or run-time failure
		 */
		vxt_msg_close_com(ctrlr_fd);
		return VXT_BUSY;
	} 
	if (dev_info.version != VXT_MSG_DEV_VERSION) {
		/*
		 * Version Mismatch
		 */
		vxt_msg_close_com(ctrlr_fd);
		return VXT_FAIL;
	}

	/*
	 * Create a local instance to track the resources and
	 * state of the new message device
	 */
	new_device = vxt_malloc(sizeof(vxt_msg_dev_instance_t));
	if (new_device == NULL) {
		vxt_msg_close_com(ctrlr_fd);
		return VXT_NOMEM;
	}

	/*
	 * 
	 * Time to connect the device to its remote counterpart
	 *
	 */
	UMI_LOG(49, VXTPRINT_DEBUG_LEVEL,"attempt device attach\n");
	attach_dev.vxt_bus_id = query.vxt_bus_id;
	attach_dev.device_info = &dev_info;
	attach_dev.info_buf_size = sizeof(dev_info);
	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_ATTACH, &attach_dev))  {
		int saved_errno = errno;
		UMI_WARN(89, VXTPRINT_PRODUCT_LEVEL, "device attach failed\n");
		VXT_PERROR("Unable to connect to vxt communication device\n");
		errno = saved_errno;
		vxt_msg_close_com(ctrlr_fd);
		vxt_free(new_device);
		return VXT_FAIL;
	}


	new_device->send_buf = NULL;
	new_device->rcv_buf = NULL;
	/* 
	 * Find queues, queue sizes, and event channels
	 */
	new_device->version = VXT_MSG_DEV_VERSION;
	new_device->type = dev;
	new_device->new = 1; /* need cookie on connect, 1st send or rcv  */
	new_device->partial_send = NULL;
	new_device->partial_rcv = NULL;
	new_device->ps_buf_index =  VXT_MSG_BUF_INVALID_INDEX; 
	new_device->ctrlr_fd = ctrlr_fd;
	new_device->dev_fd = VXT_INVALID_DEV_OBJ;
	new_device->sendq_token = (uint64_t)(vxtarch_word)dev_info.sendq;
	new_device->sendq_size = dev_info.sendq_size;
	new_device->sendq_bufcnt = dev_info.sendq_bufcnt;
	new_device->rcvq_token = (uint64_t)(vxtarch_word)dev_info.rcvq;
	new_device->rcvq_size = dev_info.rcvq_size;
	new_device->rcvq_bufcnt = dev_info.rcvq_bufcnt;
	new_device->slot = attach_dev.vxt_bus_id;
	new_device->ctrlr_id = (uint64_t)query.controller;
	vxtcom_strncpy(new_device->ep_id, endpoint->ep_id, MAX_VXT_UUID);
	vxtcom_strncpy(new_device->uname, endpoint->uname, MAX_VXT_UNAME);
	vxtcom_strncpy(new_device->file_name, 
	               dev_info.dev_name, VXTCOM_MAX_DEV_FILE_NAME);
	new_device->dev_handle = dev_info.dev_handle;
	/* 
	 * possible intermediate needed by xen, 
	 * allows us to use evtchn device
	 */
	new_device->remote_dom = dev_info.remote_dom; 


	/*
	 * Set up the poll focus so that any listen done on 
	 * this device will end up waiting for new devices
 	 * on this controller and on this device type.
	 */
	UMI_LOG(50, VXTPRINT_DEBUG_LEVEL, "attempt to set POLL focus\n");
	focus.dev_type = dev;
	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_POLL_FOCUS, &focus))  {
		int saved_errno = errno;
		VXT_PERROR("Unable to set poll focus\n");
		new_device->next = live_connections;
		live_connections = new_device;
		vxt_msg_disconnect(new_device, 0);
		errno = saved_errno;
		return VXT_FAIL;
	}
	UMI_LOG(51, VXTPRINT_DEBUG_LEVEL, "attempt to map in new queues\n");

	/*
	 *  Map in the new queues.  Note we can memory map these
	 *  into any task opening the cntrlr device.  If we 
	 *  want to add security levels, different files will
	 *  be necessary and level support on the device queue
	 *  responses from the query/connect.
	 *
	 *  Note: it is vital to use the mmap2 call and not mmap.
	 *  mmap silently shifts the last parameter by PAGE_SHIFT.
	 *  Since we send our tokens in this field the page shift
	 *  would result in corruption of the value.  mmap2 takes
	 *  a page shifted argument and hence forgoes the internal
	 *  shifting action.
	 */ 

	new_device->send_buf = (void *)
		vxt_mmap(0, new_device->sendq_size, 
		         VXT_PROT_READ | VXT_PROT_WRITE, VXT_MAP_SHARED,
		         ctrlr_fd, (off_t)new_device->sendq_token);

	new_device->rcv_buf = (void *)
		vxt_mmap(0, new_device->rcvq_size,
		         VXT_PROT_READ | VXT_PROT_WRITE, VXT_MAP_SHARED,
		         ctrlr_fd, (off_t)new_device->rcvq_token);
        
        
/*
printf("TestSend: %s\n", (char *)new_device->send_buf);
printf("TestRcv: %s\n", (char *)new_device->rcv_buf);
*/
        


	/*
	 * Fill in built in structure for queue signal
	 * callback
	 */
	new_device->signal_cb.dev = new_device->slot;
	new_device->signal_cb.ctrlr = new_device->ctrlr_fd;
	new_device->wait_cb.dev = new_device->slot;
	new_device->wait_cb.ctrlr = new_device->ctrlr_fd;

	/*
	 * We have acquired our resources and established a
	 * connection to the remote endpoint.  We must now
	 * set-up the shared memory queues and put the
	 * signal and wait callbacks in place to allow
	 * queue policy immediate control
	 */ 
	
	/* 
	 * Initialize the send queue * Note: setting hysteresis values to
	 * 0 triggers init_buf to use its defaults
	 * 25% filled for data signal, 75% empty
	 * for space available.
	 *
	 * Note: We are connecting to an existing
	 * device, not adding a device therefore
	 * we expect to find the queues already
	 * initialized.  Also, we must reverse 
	 * our view of PRODUCER.
	 *
	 */

	ret = vxtq_init_buf(new_device->send_buf, new_device->sendq_size, 
	                    sizeof(vxtq_control_t) + vxtmsg_cookie_size,
	                    new_device->sendq_bufcnt, 0, 0, 
	                    VXTQ_BUFFER_INITIALIZED | VXTQ_LOCAL_IS_PRODUCER,
	                    &vxt_signal, (void *)&new_device->signal_cb,  
	                    &vxt_wait, (void *)&new_device->wait_cb,
	                    &new_device->send_sub_size, 
	                    &new_device->sendq);
	if (ret != VXT_SUCCESS) {
		int saved_errno = errno;
		new_device->next = live_connections;
		live_connections = new_device;
		vxt_msg_disconnect(new_device, 0);
		errno = saved_errno;
		ret = ret;
	}

	ret = vxtq_init_buf(new_device->rcv_buf, new_device->rcvq_size, 
	                    sizeof(vxtq_control_t) + vxtmsg_cookie_size,
	                    new_device->rcvq_bufcnt, 0, 0, 
	                    VXTQ_BUFFER_INITIALIZED,
	                    &vxt_signal, (void *)&new_device->signal_cb,  
	                    &vxt_wait, (void *)&new_device->wait_cb,
	                    &new_device->rcv_sub_size, 
	                    &new_device->rcvq);
	if (ret != VXT_SUCCESS) {
		int saved_errno = errno;
		new_device->next = live_connections;
		live_connections = new_device;
		vxt_msg_disconnect(new_device, 0);
		errno = saved_errno;
		ret = ret;
	}

/*
 * CDYCDYCDY test code for DomU side only
 * reduces work necessary for back to back runs
 */
/*
ret = vxtq_init_buf(new_device->send_buf, new_device->sendq_size, 
                    sizeof(vxtq_control_t) + vxtmsg_cookie_size,
                    new_device->sendq_bufcnt, 0, 0, 
                    VXTQ_LOCAL_IS_PRODUCER,
                    &vxt_signal, (void *)&new_device->signal_cb,  
                    &vxt_wait, (void *)&new_device->wait_cb,
                    &new_device->send_sub_size, 
                    &new_device->sendq);
if (ret != VXT_SUCCESS) {
	int saved_errno = errno;
	new_device->next = live_connections;
	live_connections = new_device;
	vxt_msg_disconnect(new_device, 0);
	errno = saved_errno;
	ret = ret;
}

ret = vxtq_init_buf(new_device->rcv_buf, new_device->rcvq_size, 
                    sizeof(vxtq_control_t) + vxtmsg_cookie_size,
                    new_device->rcvq_bufcnt, 0, 0, 
                    0,
                    &vxt_signal, (void *)&new_device->signal_cb,  
                    &vxt_wait, (void *)&new_device->wait_cb,
                    &new_device->rcv_sub_size, 
                    &new_device->rcvq);
if (ret != VXT_SUCCESS) {
	int saved_errno = errno;
	new_device->next = live_connections;
	live_connections = new_device;
	vxt_msg_disconnect(new_device, 0);
	errno = saved_errno;
	ret = ret;
}
*/

   

	/*
	 * Update the callers notion of shared memory
	 * buffer parameters.
	 */
	buf_parms->recv_buf_size = new_device->rcvq_size;
	buf_parms->recv_sub_buf_cnt = new_device->rcvq_bufcnt;
	buf_parms->send_buf_size = new_device->sendq_size;
	buf_parms->send_sub_buf_cnt = new_device->sendq_bufcnt;

	new_device->next = live_connections;
	live_connections = new_device;
	*connection = (void *)new_device;

	return VXT_SUCCESS;
}

/*
 * Close out the queues, clean-up any
 * remaining buffers and put the queues
 * in flush/suspend state.
 *
 */

int
vxt_msg_shutdown(void *connection)
{
	vxt_msg_dev_instance_t *device;
	int ret;
#ifndef notdefcdy
/*
 * Waiting for register/unregister event support
 */
return VXT_SUCCESS;
#endif
	device = (vxt_msg_dev_instance_t *)connection;
	ret = vxtq_signal(device->sendq,
	                  VXTQ_SIGNAL_SHUTDOWN | VXTQ_SIGNAL_WAIT);
	if (ret != VXT_SUCCESS) {
		return ret;
	}
	ret = vxtq_signal(device->rcvq,
	                  VXTQ_SIGNAL_SHUTDOWN | VXTQ_SIGNAL_WAIT);
	if (ret != VXT_SUCCESS) {
		return ret;
	}

	/*
	 * Wait for a response
	 */
   
	ret = vxtq_signal(device->sendq, VXTQ_SIGNAL_CLOSE);
	if (ret != VXT_SUCCESS) {
		return ret;
	}
	ret = vxtq_signal(device->rcvq, VXTQ_SIGNAL_CLOSE);
	if (ret != VXT_SUCCESS) {
		return ret;
	}
	return VXT_SUCCESS;
}



int
vxt_msg_disconnect(void *connection, int options)
{
	vxt_msg_dev_instance_t  *device;
	vxt_msg_dev_instance_t  *traveler;
	vxt_detach_dev_struct_t detach_dev;
	vxt_msg_attach_info_t   dev_info;
	uint32_t                connection_found = 0;

	device = (vxt_msg_dev_instance_t *)connection;
	traveler = live_connections;
	if (traveler == NULL) {
		UMI_WARN(90, VXTPRINT_PRODUCT_LEVEL, "no live connections!\n");
		return VXT_PARM;
	}
	if (live_connections == device) {
		live_connections = live_connections->next;
		connection_found = 1;
	} else {
		while (traveler->next != NULL) {
			if (traveler->next == device) {
				traveler->next = traveler->next->next;
				connection_found = 1;
				break;
			}
			traveler = traveler->next;
		}
	}
	if (connection_found == 0) {
		UMI_WARN(91, VXTPRINT_PRODUCT_LEVEL,
		         "bad device handle passed\n");
		return VXT_PARM;
	}
	/*
	 * Release the queue resources before calling unmap
	 * Not implementationally necessary but queue state
	 * references shared memory so this is required to be
	 * strictly correct.
	 */
	vxtq_release_buf(device->sendq);
	vxtq_release_buf(device->rcvq);

	if (device->send_buf != NULL) {
		vxt_munmap(device->send_buf, device->ctrlr_fd, 
		           device->sendq_token, device->sendq_size);
	}
	if (device->rcv_buf != NULL) {
		vxt_munmap(device->rcv_buf, device->ctrlr_fd,
		           device->rcvq_token, device->rcvq_size);
	}

	detach_dev.handle = device->slot;
	/*
	 * detach will send back information on
	 * the device specific configuration of 
	 * the device we are detaching.
	 *
	 * We can cross-check this against the 
	 * info we have cached in the device structure
	 * locally.
	 */
	detach_dev.device_info = &dev_info;
	detach_dev.info_buf_size = sizeof(vxt_msg_attach_info_t);
	UMI_LOG(52, VXTPRINT_DEBUG_LEVEL, "calling VXTCTRLR_DETACH\n");
	if (vxt_msg_ioctl(device->ctrlr_fd, 
	                  IOCTL_VXTCTRLR_DETACH, &detach_dev) ||
	    dev_info.status != VXT_MSG_ATTACH_SUCCESS)  {
		int saved_errno = errno;
		UMI_LOG(53, VXTPRINT_DEBUG_LEVEL,
		        "Unable to delete old connection\n");
		vxt_msg_close_com(device->ctrlr_fd);
		if (device->dev_fd != VXT_INVALID_DEV_OBJ) {
			vxt_msg_close_com(device->dev_fd);
		}
		vxt_free(device);
		errno = saved_errno;
		return VXT_PARM;
   	}
	UMI_LOG(54, VXTPRINT_DEBUG_LEVEL, "VXTCTRLR_DETACH succeeded\n");
	if (options & VXT_CLOSE_REMOTE) {
		vxt_msg_destroy_info_t   msgdev_info;
		vxt_destroy_dev_struct_t parms;
		parms.vxt_bus_id = device->slot;
		parms.device_info = &msgdev_info;
		parms.info_buf_size = sizeof(vxt_destroy_dev_struct_t);

		vxt_msg_ioctl(device->ctrlr_fd, 
		              IOCTL_VXTCTRLR_DEV_REMOVE, &parms);
	}
	vxt_msg_close_com(device->ctrlr_fd);
	if (device->dev_fd != VXT_INVALID_DEV_OBJ) {
		vxt_msg_close_com(device->dev_fd);
	}
	vxt_free(device);
	return VXT_SUCCESS;
}


/*
 * vxt_msg_send:
 *
 * vxt_msg_send is made to provide a data copyin/copyout
 * front end to the shared memory queue model.  For
 * direct access to shared memory support, the communication
 * interface should vxt_msg_get_buf/vxt_msg_put_buf
 * these are thin layer wrappers for the vxtq support.
 *
 * When in streaming mode, vxt_msg_send will hold partially
 * completed buffers, assuming that more data is coming
 * immediately.
 */


int
vxt_msg_send(void *connection, void *buffer, int *len, int flags)
{
	vxt_msg_dev_instance_t *device;
	void *send_buf;
	int  source_offset;
	int  dest_offset;
	int  buf_num;
	int  sbuf_len;
	int  remainder;
	int  total_len;
	int  ret;

	UMI_LOG(55, VXTPRINT_DEBUG_LEVEL,
	        " called, flags 0x%x\n", (uint32_t)flags);
	device = (vxt_msg_dev_instance_t *)connection;
	if (device->new) {
		char *cookie;
		/*
		 * Check the queue initialization status
		 * We have a vxt_msg level cookie at the
		 * end of the header which must match
		 */
		cookie = ((char *)
		          (device->send_buf)) + sizeof(vxtq_control_t);
		ret = vxt_msg_cookie_check(cookie,
		                           vxtmsg_queue_cookie,
		                           vxtmsg_cookie_size);
		if (ret != VXT_SUCCESS) {
			return ret;
		}
		device->new = 0;
	}

	total_len = *len;
	remainder = *len;
	source_offset = 0;
	dest_offset = 0;
	if (device->partial_send !=  NULL) {
		send_buf = device->partial_send;
		dest_offset = device->ps_offset;
		device->partial_send = NULL;
		sbuf_len = device->ps_len;
		buf_num = device->ps_buf_index; 
	} else {
		ret = vxtq_get_buf(device->sendq, flags, 
		                   &send_buf, &buf_num, &sbuf_len);

		UMI_LOG(56, VXTPRINT_DEBUG_LEVEL,
		        "return from vxtq_get_buf\n");
		if (ret != VXT_SUCCESS) {
			UMI_LOG(57, VXTPRINT_DEBUG_LEVEL,
			        "vxtq_get_buf failed\n");
			return ret;
		}

	}

	while(TRUE) {
		if ((sbuf_len - dest_offset) < remainder) {
			remainder = sbuf_len - dest_offset;
		}
		memcpy((void *)(((char *)send_buf) + dest_offset), 
		       (void *)(((char *)buffer) + source_offset), remainder);
		dest_offset += remainder;
		source_offset += remainder;
		if (source_offset == total_len) {
			/*
			 * Transfer is finished
			 */
/* CDY:  remove this if and do if(0) for testing, our test doesn't trigger
   at the end of input
 */
			if ((dest_offset < sbuf_len) 
			    && (flags & VXT_MSG_AUTO_STREAM)) {
				device->partial_send = send_buf;
				device->ps_offset = dest_offset;
				device->ps_len = sbuf_len;
				device->ps_buf_index = buf_num;
			} else {
				UMI_LOG(58, VXTPRINT_DEBUG_LEVEL,
				        "calling vxtq_put_buf, buf_num 0x%x, "
				        "data_len 0x%x, flags 0x%x\n",
				        (uint32_t)buf_num,
				        (uint32_t)dest_offset,
					(uint32_t)flags);
				ret = vxtq_put_buf(device->sendq, flags,
				                   send_buf, buf_num,
				                   dest_offset);
				UMI_LOG(59, VXTPRINT_DEBUG_LEVEL,
				        "return from vxtq_put_buf, "
				        "ret = 0x%x\n", (uint32_t)ret);
				if (ret == VXT_BLOCKED) {
					UMI_LOG(60, VXTPRINT_DEBUG_LEVEL,
					        "send is blocked, caller should"
					        " flush any partial sends\n");
					return ret;
				}
#ifdef BASIC_ASSERT
				assert(ret == VXT_SUCCESS);
#else
				ASSERT(ret == VXT_SUCCESS, "vxt_msg_send");
#endif
			}
			return VXT_SUCCESS;
		} else {
			UMI_LOG(61, VXTPRINT_DEBUG_LEVEL,
			        "calling vxtq_put_buf for part of send, "
			        "buf_num 0x%x, data_len 0x%x, flags 0x%x\n",
			        (uint32_t)buf_num, (uint32_t)dest_offset,
			        (uint32_t)flags);
			ret = vxtq_put_buf(device->sendq, flags,
			                   send_buf, buf_num,
			                   dest_offset);
			UMI_LOG(62, VXTPRINT_DEBUG_LEVEL,
			        "return from vxtq_put_buf, ret = 0x%x\n",
			        (uint32_t)ret);
			if (ret == VXT_BLOCKED) {
				*len = source_offset;
				return VXT_BLOCKED;
			}
			remainder = total_len - source_offset;
			dest_offset = 0;
			ret = vxtq_get_buf(device->sendq, flags, 
			                   &send_buf, &buf_num, &sbuf_len);

			if (ret != VXT_SUCCESS) {
				if (flags & VXT_MSG_WAIT) {
					/*
					 * We have returned for some 
					 * other reason besides lack of
					 * buffer space
					 */
					*len = source_offset;
					return ret;
				} else {
					/* 
					 * return how much we have written
					 * in the callers length parameter.
					 */
					*len = source_offset;
					if ((ret == VXT_SHUTTING_DOWN) 
					    || (ret == VXT_BLOCKED)) {
						return ret;
					}
					return VXT_BUF_SPACE;
				}
			}
		}
	}


	return VXT_SUCCESS;
}



int
vxt_msg_rcv(void *connection, void *buffer, size_t *len, int flags)
{
	vxt_msg_dev_instance_t *device;
	void *rcv_buf;
	int  source_offset;
	int  dest_offset;
	int  buf_num;
	int  rbuf_len;
	int  remainder;
	int  total_len;
	int  ret;

	device = (vxt_msg_dev_instance_t *)connection;

	if (device->new) {
		char *cookie;
		/*
		 * Check the queue initialization status
		 * We have a vxt_msg level cookie at the
		 * end of the header which must match
		 */
		cookie = ((char *)
		          (device->rcv_buf)) + sizeof(vxtq_control_t);
		ret = vxt_msg_cookie_check(cookie,
		                           vxtmsg_queue_cookie,
		                           vxtmsg_cookie_size);
		if (ret != VXT_SUCCESS) {
			return ret;
		}
		device->new = 0;
	}

	total_len = *len;
	remainder = *len;
	source_offset = 0;
	dest_offset = 0;
	if (device->partial_rcv !=  NULL) {
		rcv_buf = device->partial_rcv;
		source_offset = device->pr_offset;
		device->partial_rcv = NULL;
		rbuf_len = device->pr_len;
		buf_num = device->pr_buf_index; 
	} else {
		ret = vxtq_get_buf(device->rcvq, flags, 
		                   &rcv_buf, &buf_num, &rbuf_len);

		if (ret != VXT_SUCCESS) {
			*len = 0;
			if (ret == VXT_RSRC) {
				return VXT_SUCCESS;
			} else {
				return ret;
			}
		}

	}

	while(TRUE) {
		if ((rbuf_len - source_offset) < remainder) {
			remainder = rbuf_len - source_offset;
		}
		memcpy((void *)(((char *)buffer) + dest_offset), 
		       (void *)(((char *)rcv_buf) + source_offset), remainder);
		dest_offset += remainder;
		source_offset += remainder;
		if (dest_offset == total_len) {
			/*
			 * Transfer is finished
			 */
			if (source_offset < rbuf_len) {
				device->partial_rcv = rcv_buf;
				device->pr_offset = source_offset;
				device->pr_len = rbuf_len;
				device->pr_buf_index = buf_num;
			} else {
				ret = vxtq_put_buf(device->rcvq, 
				                   flags, rcv_buf, 
				                   buf_num, *len);
#ifdef BASIC_ASSERT
				assert(ret == VXT_SUCCESS);
#else
				ASSERT(ret == VXT_SUCCESS, "vxt_msg_rcv");
#endif
			}
	  		return VXT_SUCCESS;
		} else {
			ret = vxtq_put_buf(device->rcvq, 
			                   flags, rcv_buf, 
			                   buf_num, *len);
#ifdef BASIC_ASSERT
			assert(ret == VXT_SUCCESS);
#else
			ASSERT(ret == VXT_SUCCESS, "vxt_msg_rcv");
#endif
#ifdef notdefcdy
/*
 * Just for the Demo, I don't want
 * to parse the output in the test
 */
return VXT_SUCCESS;
#endif
			remainder = total_len - dest_offset;
			source_offset = 0;
			flags &= ~VXT_MSG_WAIT;
			ret = vxtq_get_buf(device->rcvq, flags, 
			                   &rcv_buf, &buf_num, &rbuf_len);

			if (ret != VXT_SUCCESS) {
				if (flags & VXT_MSG_WAIT) {
					/*
					 *  We have returned for some other
					 *  reason besides lack of buffer
					 *  space
					 */
					*len = dest_offset;
					return ret;
				} else {
					/* 
					 * return how much we have read
					 * in the callers length parameter.
					 */
					*len = dest_offset;
					if ((ret == VXT_SHUTTING_DOWN)
					    || (ret == VXT_BLOCKED)) {
						return ret;
					}
					return VXT_BUF_SPACE;
				}
			}
		}
	}


	return VXT_SUCCESS;
}

/*
 *
 * vxt_msg_ctrlr_wait:
 *
 * vxt_msg_ctrlr_wait allows the caller to wait on the
 * appearance of a new device.  If the connection field
 * holds a device, the wait will be on the specific
 * controller and for the specific device type associated
 * with the device. If the connection field is null,
 * the wait will be across all controllers for all device
 * types.
 *
 */

int
vxt_msg_ctrlr_wait(void *connection)
{
	vxt_msg_dev_instance_t *device;
	int timeout;
	int revent;

	device = (vxt_msg_dev_instance_t *)connection;
	timeout = VXT_NO_TIMEOUT;  /* no timeout, wait until event */
	revent = 0;  
	if (device == NULL) {
		/*
		 * Do the generic wait
		 */
		return vxt_msg_event_wait(vxtctrlr_fd, timeout, &revent);
	} else {
		return vxt_msg_event_wait(device->ctrlr_fd, timeout, &revent);
	}
}


/*
 * 
 * vxt_msg_wait:
 *
 * vxt_msg_wait will wait simultaneously on a 
 * group of descriptors.  The vxt_socket
 * support has been augmented to allow 
 * for a mix of waits on vxt socket files
 * and others.  This is accomplished by
 * having the user differentiate vxt socket
 * entries with the POLLVXT flag. When present 
 * the POLLVXT flag causes the dereference
 * from a message device to the underlying
 * event channel file.  The POLLVXT is
 * cleared before returning.
 *
 */

int
vxt_msg_wait(void *fds, int nfds, int timeout, int *events_found)
{
	vxtctrlr_handle_t dev_fd = VXT_INVALID_DEV_OBJ;
	char              dev_name[VXTCOM_MAX_DEV_FILE_NAME];
	uint32_t          events;
	uint32_t          revents;
	uint64_t          object;
	int               i;

	/*
	 * de-reference the vxt_msg instances into their
	 * evtchn file descriptors.
	 */
	UMI_LOG(63, VXTPRINT_DEBUG_LEVEL, "calling vxt_msg_wait\n");
	for (i = 0; i < nfds; i++) {
		vxt_read_event_wait(fds, i, &object, &events, &revents);
		if (events & POLLVXT) {
			/*
			 * There are several varieties of VXT Poll
			 * Poll data is the default.  If there are
			 * no other flags present.  Poll Controller
			 * and poll device are also possible.
			 */
			if (events & POLLVXT_CTRLR) {
				UMI_LOG(64, VXTPRINT_DEBUG_LEVEL, 
				        "vxt_msg_wait: socket ctrlr wait\n");
				object = (uint64_t)vxtctrlr_fd;
				vxt_write_event_wait(fds, i, object, 
				                     events, revents);
				continue;
			}
			if (events & POLLVXT_DEV) {
				vxt_msg_dev_instance_t *device;
				UMI_LOG(65, VXTPRINT_DEBUG_LEVEL, 
				        "vxt_msg_wait: socket dev wait %s\n",
				         ((vxt_msg_dev_instance_t *)
				          (vxtarch_word)object)->file_name);
				device = (vxt_msg_dev_instance_t *)
				         (vxtarch_word)object;
				if (device == NULL) {
					/*
					 * Do the generic wait
					 */
					object = (uint64_t)vxtctrlr_fd;
				} else {
					object = (uint64_t)(device->ctrlr_fd);
				}
				vxt_write_event_wait(fds, i, object, 
				                     events, revents);
				continue;
			}
			if (object == 0) {
				UMI_WARN(92, VXTPRINT_PRODUCT_LEVEL,
				         "data wait, on unattached socket\n");
				return VXT_PARM;
			} 
			UMI_LOG(66, VXTPRINT_DEBUG_LEVEL,
			        "socket data wait %s\n",
			        ((vxt_msg_dev_instance_t *)
			         (vxtarch_word)object)->file_name);

			/* 
			 * We cache the file descriptor i.e. allocate it
			 * once here, keep it with the object, and delete
			 * it when the device/object closes.
			 */
 
			sprintf(dev_name, "%s%s", VXT_DEV_DIR, 
			        ((vxt_msg_dev_instance_t *)
			         (vxtarch_word)object)->file_name);
			if (((vxt_msg_dev_instance_t *)
			    (vxtarch_word)object)->dev_fd
			    == VXT_INVALID_DEV_OBJ) {
        		   if ((dev_fd = 
			        vxt_msg_open_dev(dev_name, VXT_RDWR,
			                         ((vxt_msg_dev_instance_t *)
                                                  (vxtarch_word)object)
				                                ->dev_handle))
				                 == VXT_INVALID_DEV_OBJ) {
				UMI_WARN(93, VXTPRINT_PRODUCT_LEVEL,
				         "vxt_msg_wait: fopen failed\n");
				events &= ~POLLVXT;
				vxt_write_event_wait(fds, i, -1, events,
				                     revents | VXTSIG_HUP);
				*events_found = 1;
				return VXT_SUCCESS;
			   }
			   ((vxt_msg_dev_instance_t *)
			    (vxtarch_word)object)->dev_fd = dev_fd;
			}
			object = (uint64_t)
			         (((vxt_msg_dev_instance_t *)
			           (vxtarch_word)object)->dev_fd);
			events &= ~POLLVXT;
			vxt_write_event_wait(fds, i, object, events, revents);
		}
	}
	if ((*events_found = poll(fds, nfds, timeout)) < 0) {
		if (errno == EBADF) {
			return (VXT_PARM);
		}
		if (errno == EFAULT) {
			return (VXT_PARM);
		}
		if (errno == EINTR) {
			return (VXT_ABORT);
		}
		if (errno == EINVAL) {
			return (VXT_PARM);
		}
		if (errno == ENOMEM) {
			return (VXT_NOMEM);
		}

	}
	/*
	 * Note:  we will want to augment the responses based on 
	 * control information in the shared memory queues
	 * Anything more than generic info however will still
	 * need to be passed in a query.
	 */

	return VXT_SUCCESS;
}


/*
 *
 * vxt_msg_system_query:
 *
 * vxt_msg_system_query checks for a vxtctrlr that reaches
 * the uname provided.  If one is found, the filename for
 * it is returned to the caller.
 *
 * Returns:
 *
 *     VXT_SUCCESS => 0
 *
 *     VXT_PARM - No reaching device found
 *     VXT_DOWN - There are no live controllers
 *
 */

int
vxt_msg_system_query(vxt_query_sys_struct_t *sys_info)
{
	vxt_query_controller_struct_t query;

	if (vxtctrlr_fd == VXT_INVALID_DEV_OBJ) {
		/*
		 * No access to the Symantec transport
		 * device 
		 */
		return VXT_DOWN;
	}
	/*
	 * Find the specific controller that "reaches"
	 * the endpoint.
	 */
	query.uname[0] = 0;
	query.name_match_length = 0; /* disable prefix only check */
	vxtcom_strncpy(query.uname, sys_info->uname, MAX_VXT_UNAME);
	vxtcom_strncpy(query.ep_id, sys_info->ep_id,  MAX_VXT_UUID);
	if (query.uname[0] == 0) {
		query.remote_domain = sys_info->domain; 
	} else {
		query.remote_domain = 0;  /* not available */
	}
	UMI_LOG(67, VXTPRINT_DEBUG_LEVEL,
	        "uname %s, remote_domain 0x%x\n", query.uname,
	        query.remote_domain);

	if (vxt_msg_ioctl(vxtctrlr_fd, IOCTL_VXTCTRLR_LOOKUP, &query)) {
		return VXT_PARM;
	} else {
		sprintf(sys_info->ctrlr_name, 
		        "%s%s", VXT_DEV_DIR, query.ctrlr_name);
		/*
		 * Pass back the major name for the reaching
		 * controller as well as the file name
		 */
		sys_info->controller = query.controller;
	}

	return VXT_SUCCESS;

}


/*
 * vxt_msg_bus_query
 *
 * Wraps the vxt_cntrlr bus query function, looking
 * only for devices of suitable, (msg) type.
 * This call provides function for blind device
 * discovery.  
 *
 */

int
vxt_msg_bus_query(vxt_query_bus_struct_t *card_info)
{
	vxtctrlr_handle_t      ctrlr_fd = VXT_INVALID_DEV_OBJ;

	if (!strncmp(card_info->ctrlr_name, VXT_DEV_DIR, 5)) {
        	if ((ctrlr_fd = 
		     vxt_msg_open_com(card_info->ctrlr_name, 
			              VXT_RDWR)) == VXT_INVALID_DEV_OBJ) {
			return VXT_PARM;
		}
	} else {
		if (vxtctrlr_fd == VXT_INVALID_DEV_OBJ) {
			return VXT_FAIL;
		}
		ctrlr_fd = vxtctrlr_fd;
	}
	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_QUERY_BUS, card_info)) {
		int saved_errno = errno;
		UMI_LOG(68, VXTPRINT_DEBUG_LEVEL,
		        "Unable to query the vxt communication ctrlr\n");
		errno = saved_errno;
		if (vxtctrlr_fd != ctrlr_fd) {
			vxt_msg_close_com(ctrlr_fd);
		}
		return VXT_DOWN;
	}

	if (vxtctrlr_fd != ctrlr_fd) {
		vxt_msg_close_com(ctrlr_fd);
	}
	return VXT_SUCCESS;

}


/*
 * vxt_msg_dev_query
 *
 * Wraps the vxt_cntrlr device query function, responds
 * with specific information about a specified device.
 * The device is assumed to be of type vxt_msg.
 *
 */

int
vxt_msg_dev_query(vxt_query_dev_struct_t *dev_info)
{
	vxtctrlr_handle_t      ctrlr_fd = VXT_INVALID_DEV_OBJ;
	char                   ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME + 6];

	UMI_LOG(69, VXTPRINT_DEBUG_LEVEL,
	        "called, on ctrlr_name %s\n", dev_info->ctrlr_name);
	if (dev_info->ctrlr_name[0] != 0) {
		sprintf(ctrlr_name, "%s%s", 
		        VXT_DEV_DIR, dev_info->ctrlr_name);
		UMI_LOG(70, VXTPRINT_DEBUG_LEVEL,
		        "query on caller's pick, ctrlr %s\n", ctrlr_name);
        	if ((ctrlr_fd = 
		     vxt_msg_open_com(ctrlr_name,
		                      VXT_RDWR)) == VXT_INVALID_DEV_OBJ) {
			return VXT_PARM;
		}
	} else {
		if (vxtctrlr_fd == VXT_INVALID_DEV_OBJ) {
			return VXT_FAIL;
		}
		ctrlr_fd = vxtctrlr_fd;
	}
	if (vxt_msg_ioctl(ctrlr_fd, IOCTL_VXTCTRLR_QUERY_DEV, dev_info)) {
		int saved_errno = errno;
		UMI_WARN(94, VXTPRINT_PRODUCT_LEVEL,
		         "Unable to query the vxt com device\n");
		errno = saved_errno;
		if (vxtctrlr_fd != ctrlr_fd) {
			vxt_msg_close_com(ctrlr_fd);
		}
		return VXT_FAIL;
	}

	if (vxtctrlr_fd != ctrlr_fd) {
		vxt_msg_close_com(ctrlr_fd);
	}
	return VXT_SUCCESS;
}


/*
 *
 * vxt_msg_bind_query:
 *
 * Return the device and controller the device is attached to.
 *
 * Returns:
 *		VXT_SUCCESS => Upon successful invocation
 *
 *		VXT_FAIL - connection is null
 *
 *
 */

int
vxt_msg_bind_query(void *connection, vxt_query_bind_struct_t *bind_info)
{
	vxt_msg_dev_instance_t *device;

	device = (vxt_msg_dev_instance_t *)connection;

	if (device == NULL) {
		return VXT_FAIL;
	}

	bind_info->ctrlr_id = device->ctrlr_id;
	bind_info->dev_slot = device->slot;

	return VXT_SUCCESS;
}


int
vxt_msg_unplug_dev(void *connection, vxt_unplug_dev_struct_t *parms)
{
	char                     ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME + 6];
	vxtctrlr_handle_t        parent_ctrlr_fd = 0;
	vxt_destroy_dev_struct_t destroy_parms; 

	sprintf(ctrlr_name, "%s%s", VXT_DEV_DIR, parms->ctrlr_name);
       	if ((parent_ctrlr_fd =
	        vxt_msg_open_com(ctrlr_name,
	                         VXT_RDWR)) == VXT_INVALID_DEV_OBJ) {
		UMI_WARN(95, VXTPRINT_PRODUCT_LEVEL,
		         "couldn't open default controller\n");
		return VXT_PARM;
	}

	destroy_parms.vxt_bus_id = parms->vxt_bus_id;
	destroy_parms.device_info = parms->device_info;
	destroy_parms.info_buf_size = parms->info_buf_size;
	UMI_LOG(71, VXTPRINT_DEBUG_LEVEL,
	        "Calling dev destroy on bus_id 0x%llx\n",
	        (long long) parms->vxt_bus_id);

	if (vxt_msg_ioctl(parent_ctrlr_fd, 
	                  IOCTL_VXTCTRLR_DEV_REMOVE, &destroy_parms)) {
		vxt_msg_close_com(parent_ctrlr_fd);
		return VXT_FAIL;
	}

	vxt_msg_close_com(parent_ctrlr_fd);

	return VXT_SUCCESS;
}


int 
vxt_msg_control(void *connection, int cmd,
                void *info, int info_len)
{
	vxt_msg_dev_instance_t *device;
	int ret;

	UMI_LOG(72, VXTPRINT_DEBUG_LEVEL,
	        "called, device = %p\n", connection);

	device = (vxt_msg_dev_instance_t *)connection;

	if (cmd == VXT_IOCTL_SIGNAL_SEND)  {
		UMI_LOG(73, VXTPRINT_DEBUG_LEVEL,
		        "Signal Send queue = %p\n", device->sendq);
#ifdef BASIC_ASSERT
		assert(info_len == sizeof(int));
#else
		ASSERT(info_len == sizeof(int), "vxt_msg_control");
#endif
		ret = vxtq_signal(device->sendq, *(int *)info);
		return ret;
	} else if (cmd == VXT_IOCTL_SIGNAL_RCV)  {
		UMI_LOG(74, VXTPRINT_DEBUG_LEVEL,
		        "Signal Receive queue = %p\n", device->rcvq);
#ifdef BASIC_ASSERT
		assert(info_len == sizeof(int));
#else
		ASSERT(info_len == sizeof(int), "vxt_msg_control");
#endif
		ret = vxtq_signal(device->rcvq, *(int *)info);
		return ret;
	} else if ((cmd == VXT_IOCTL_QUERY_SEND) ||
		   (cmd == VXT_IOCTL_SET_SEND_ERROR) ||
		   (cmd == VXT_IOCTL_RESET_SEND)) {
		/*
		 * Note SIGNAL_ATTN can be done on either
		 * queue, the evtchn is common for the device
		 */

		UMI_LOG(75, VXTPRINT_DEBUG_LEVEL,
		        "IOCTL on send queue = %p\n", device->sendq);
		ret = vxtq_io_control(device->sendq, cmd, info, info_len);
		return ret;
	} else if ((cmd == VXT_IOCTL_QUERY_RCV) ||
	           (cmd == VXT_IOCTL_SET_RCV_ERROR) ||
	           (cmd == VXT_IOCTL_RESET_RCV)) {

		UMI_LOG(76, VXTPRINT_DEBUG_LEVEL,
		        "IOCTL on receive queue = %p\n", device->rcvq);
		ret = vxtq_io_control(device->rcvq, cmd, info, info_len);
		return ret;
	}

	return VXT_PARM;
}


