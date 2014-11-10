#ifndef VXT_COM_INTERNAL_H
#define VXT_COM_INTERNAL_H

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
 *
 * vxt_com_internal.h
 *
 * This file containes private defines and structures
 * for the implementation of the vxt controller.  The
 * core vxt controller implementation is spread among
 * several "c" modules and hence this file is 
 * required.  The core implementation includes, 
 * the vxtcfg_msgs.c and vxtcom.c files.
 */

/*
 * Used for registered device libraries.
 * Can be loaded at runtime or may be available
 * in a linked version. Will work in a kernel
 * or an application environment
 */

/*
 *
 * symlibs_t:
 *
 * Used to track registered device driver/library
 * code for different types of devices supported
 * by the vxt controller.
 *
 */

typedef struct symlibs {
	vxtdev_type_t       dev_type;
	vxtdev_api_t        callbacks;
	int                 use_count;
	vxtcom_wait_queue_t new_dev_wait;
	int                 dev_event_list;
	struct symlibs      *next;
} symlibs_t;


typedef struct vxtdev vxtdev_t;

/*
 * vxt_slot_t:
 *
 * vxt_slot_t is used to link together
 * the devices that populate a controller
 * The handle is the published moniker
 * for the device and is used by the
 * client to locate the device.
 *
 * vxt_slot_t is embedded in a device
 * structure and the structure is accessed
 * by reverting to parent.
 *
 */

typedef struct vxt_slot {
	uint32_t        handle;
	struct vxt_slot *next;
} vxt_slot_t;

typedef struct vxtcom_ctrlr vxtcom_ctrlr_t;


/*
 * vxtdev_t:
 *
 * vxtdev_t tracks the internal generic state of a device.  The
 * structure is attached to a controller and the list of vxt_dev_t
 * structures associated with that controller constitute the 
 * devices which are "plugged-in". 
 *
 * Each vxtdev_t points to a device specific state structure,
 * has a list of callbacks which are specific to the type of
 * device instantiated on the structure and a type field to
 * identify the device type.
 *
 * Device calls are made by a client on the associated 
 * vxt controller thorugh ioctl's.  These calls devolve through
 * device handle to the device structure and then after
 * handling the generic portion of the command, the appropriate
 * device callback is executed to allow the device type
 * specific response.
 *
 * vxtdev_t has a "name" field which uniquely identifies the
 * device connection within the global reach of the distributed
 * environment.  The routing assoicated with the distributed
 * name is beyond the scope of the vxt com subsystem.  However,
 * the results of that routing are reflected in the contents
 * of the remote_dom field.  The remote_dom field in vxtdev_t
 * is used to contact the device associated with the matched
 * endpoint of the communications link.
 *
 */
struct vxtdev {
	vxt_slot_t        slot;
	vxtdev_type_t     type;
	vxtdev_api_t      *dev;
	vxtcom_ctrlr_t    *controller;
	symlibs_t         *library;
	void              *of_rec;
	uint64_t          uname_hash;
	char              uname[MAX_VXT_UNAME];
	char              dev_file_name[VXTCOM_MAX_DEV_FILE_NAME];
	uint64_t          os_dev;
	vxtsig_handle_t   event;
	void              *instance;   /* device specific instance */
	vxt_domid_t       remote_dom;
	vxt_slot_t        remote_slot;  /* complementary device on other end */
	struct vxtdev     *next;
	int               remote_resources;
	int               state;
	int               error;
	int               origin;
	uint32_t          waiting;
	uint32_t          shutting_down;
	vxtcom_wait_queue_t wq;
	vxtdev_queues_t  *dev_queues;
};


typedef struct vxtcom_ctrlr_hdr {
	vxt_domid_t             rdom;   /* paired connection */
	struct vxtcom_ctrlr_hdr *next;
} vxtcom_ctrlr_hdr_t;


typedef struct vxtcfg_sync_callback {
	uint64_t wait_cookie;
	vxtcom_ctrlr_t *instance;
	vxtcom_cntrl_hdr_t reply_msg;
} vxtcfg_sync_callback_t;


/*
 *
 * vxtcom_ctrlr_t:
 *
 * The vxtcom_ctrlr_t struct maintains the state of a 
 * pseudo bus with a list of devices that are plugged-in, 
 * and a set of registered device libraries that define
 * the type of devices the controller can attach.
 *
 * The vxtcom_ctrlr_t structure holds the state to support
 * a number of sets of operators.
 *
 * Task level clients of vxtcom_ctrlr_t interact with
 * it through the abstraction of a pseudo-bus.  Operators
 * to query the "slots", query the devices found on those
 * slots, and add and remove devices from slots are available.
 *
 * Device libraries, or driver can be associated with a 
 * controller.  These libraries can be loaded and unloaded.
 * A device creation action involves the instantiation of
 * a generic device and its association with a loaded library.
 * The association takes the form of a set of device action
 * callbacks.  
 *
 * In order to be effective the controller must act as a
 * device.  To do this it must be connected with the bus
 * and driver facilities of the underlying operating system
 * or hypervisor.  This is done through the bus level
 * interfaces.
 *
 */

struct vxtcom_ctrlr {
	vxtbus_ctrlr_t bus;
	/*
	 * Following fields isolated from bus port
	 */
	vxtcom_wait_queue_t new_dev_wait;
	int                dev_event_list;
	int                pending_shutdown;
	vxtcom_ctrlr_hdr_t ctrlr_list;
	vxtdev_t            *dev_list;
	uint32_t            vxtcom_slots;
	uint32_t            slot_wrap;
	uint32_t            pending_send;
	vxt_slot_t          *live_slots;
	int                 major;           /* controller instance */
	char                file_name[VXTCOM_MAX_DEV_FILE_NAME];
	symlibs_t           *device_libraries;
	struct vxtcom_ctrlr *next;
};


/*
 *
 * vxtcom_find_ctrlr:
 *
 * vxtcom_find_ctrlr finds the controller parent structure 
 * for the vxtbus_ctrlr_t structure passed.
 *
 * The parameter passed must be embedded in a vxtcom_ctrlr_t
 * structure.
 * 
 */

static inline vxtcom_ctrlr_t *
vxtcom_find_ctrlr(vxtbus_ctrlr_t *bus)
{
	vxtcom_ctrlr_t *internal_ctrlr = (vxtcom_ctrlr_t *)0;
	return (vxtcom_ctrlr_t *) 
	       (((vxtarch_word)bus) - (vxtarch_word)&(internal_ctrlr->bus));
}


/*
 *
 * vxtcom_slot_find_device:
 *
 * vxtcom_slot_find_device finds the parent vxtdev_t 
 * device structure of the slot structure passed.
 *
 * The vxt_slot_t structure is assumed to be 
 * embedded in a device structure.
 * 
 */

static inline vxtdev_t *
vxtcom_slot_find_device(vxt_slot_t *slot)
{
        vxtdev_t *device = (vxtdev_t *)0;

        return (vxtdev_t *)
               (((vxtarch_word)slot) - (vxtarch_word)&(device->slot));
}


/*
 *
 * vxtcom_controller_find_instance:
 *
 * Find the vxtcom_ctrlr_t parent of the
 * vxtcom_ctrlr_hdr structure. Return
 * the result.
 *
 */

static inline vxtcom_ctrlr_t *
vxtcom_controller_find_instance(vxtcom_ctrlr_hdr_t *ctrlr)
{
	vxtcom_ctrlr_t *instance = (vxtcom_ctrlr_t *)0;

	return (vxtcom_ctrlr_t *)
	       (((vxtarch_word)ctrlr) - (vxtarch_word)&(instance->ctrlr_list));
}


/*
 *
 * vxtcom_find_lib:
 *
 * vxtcom_find_lib searches the list of libraries passed
 * for lib_name.  If found VXT_SUCCESS is returned and
 * the library field pointer is updated to point at the
 * lib_name entry.
 *
 * Returns:
 *
 *		VXT_SUCCESS
 *		VXT_FAIL - library not found
 *
 * 
 */

static inline int vxtcom_find_lib(symlibs_t *liblist, 
                                  vxtdev_type_t lib_name,
                                  symlibs_t **library) 
{
	symlibs_t *libptr;
	libptr = liblist;
	while (libptr != NULL) {
		if(libptr->dev_type == lib_name) {
			*library = libptr;
			return VXT_SUCCESS;
		}
		libptr = libptr->next;
	}
	return VXT_FAIL;
}


/*
 *
 * vxtcom_rem:
 *
 * vxtcom_rem_lib searches the list of libraries passed
 * for lib_name.  If found, the targeted library is 
 * removed from the list and VXT_SUCCESS is returned
 *
 * Returns:
 *
 *		VXT_SUCCESS - library removed
 *		VXT_FAIL - library not found
 *
 * 
 */

static inline int vxtcom_rem(symlibs_t **liblist_base, 
                             vxtdev_type_t lib_name)
{
	symlibs_t *libptr = *liblist_base;

	if (libptr == NULL) {
		return VXT_FAIL;
	}
	if (libptr->dev_type == lib_name) {
		*liblist_base = libptr->next;
		return VXT_SUCCESS;
	}
	while (libptr->next != NULL) {
		if(libptr->next->dev_type == lib_name) {
			libptr->next = libptr->next->next;
			return VXT_SUCCESS;
		}
		libptr = libptr->next;
	}
	return VXT_FAIL;
}

typedef struct vxtcom_req_dev_create_wait {
	uint64_t          slot_handle; /* filled in on successful return */
	int               status;      /* remote create status */
	int               request_finished;
	vxtcom_wait_queue_t wq;
} vxtcom_req_dev_create_wait_t;

typedef struct vxtcom_req_signal_closing_wait {
	int               status;      /* remote status */
	int               request_finished;
	vxtcom_wait_queue_t wq;
} vxtcom_req_signal_closing_wait_t;





extern int
vxtcfg_devreq_ctrlr_shutdown(vxtcom_ctrlr_t *vxtcom, uint64_t wait_cookie,
                             uint64_t shutdown_flags);

extern int
vxtcfg_config_closing_response(vxtcom_ctrlr_t *instance,
                               vxtcom_cntrl_hdr_t *closing_msg);

extern int
vxtcfg_config_closing_reply(vxtcom_ctrlr_t *instance,
                            vxtcom_cntrl_hdr_t *closing_msg);

extern int
vxtcfg_request_dev_create(vxtcom_ctrlr_t * vxtcom, uint64_t wait_cookie,
                          uint64_t device_type, char *dev_name,
                          void *dev_data, int datalen);

extern int
vxtcfg_config_create_response(vxtcom_ctrlr_t *instance,
                              vxtcom_cntrl_hdr_t *add_msg);

extern int
vxtcfg_config_proxy_create_reply(vxtcom_ctrlr_t *instance, 
                                 vxtcom_cntrl_hdr_t *add_msg);


extern int
vxtcfg_devreq_connect_remote(void *device_handle, void *sync_reply_handle, 
                             void *dev_data, int datalen,
                             void *device_specific_instance);



extern int
vxtcfg_devreq_remove_device(void *device_handle,
                            void *dev_data, uint32_t datalen);

extern int
vxtcfg_config_rem_response(vxtcom_ctrlr_t *instance,
                           vxtcom_cntrl_hdr_t *rem_msg);

extern int
vxtcfg_config_add_req(vxtcom_ctrlr_t *instance, vxtcom_cntrl_hdr_t *add_msg);

extern int
vxtcfg_config_rem_reply(vxtcom_ctrlr_t *instance, vxtcom_cntrl_hdr_t *rem_msg);


extern int
vxtcfg_config_add_reply(vxtcom_ctrlr_t *instance, vxtcom_cntrl_hdr_t *add_msg);


extern void
vxtcfg_synchronous_reply(vxtcfg_sync_callback_t *parms, int status);

extern void
vxtcfg_sync_status_return(void *sync_response, int status);

extern int
vxtcfg_signal_config_queue(vxtcom_ctrlr_t *vxtcom);




extern vxtcom_ctrlr_api_t vxtcom_utilities;

extern void
vxtcom_inc_ref(vxtcom_ctrlr_t *vxtcom);

extern void
vxtcom_dec_ref(vxtcom_ctrlr_t *vxtcom);

extern int
vxtcom_get_slot_handle(vxtcom_ctrlr_t *vxtcom, vxt_slot_t *slot);

int
vxtcom_free_slot_handle(vxtcom_ctrlr_t *vxtcom, uint32_t old_handle);

int
vxtcom_find_slot(vxtcom_ctrlr_t *vxtcom, int handle, vxt_slot_t **user_slot);

extern int
vxtcom_remove_device(void *vxtcom_handle, void *device_handle,
                     void *info, uint32_t info_length, uint32_t signal_remote);

extern int
vxtcom_remove_device_complete(vxtdev_t *device, int error);

extern int
vxtcom_closing_ctrlr_complete(vxtcom_ctrlr_t *instance,
                              uint64_t wq, uint64_t error);

extern int
vxtcom_add_device(vxtcom_ctrlr_t *vxtcom, vxtdev_type_t device_type,
                  char *remote_ep, void *dev_parms, uint32_t info_length,
                  vxtdev_handle_t *newdev_instance, 
                  vxtcom_ctrlr_api_t *callbacks, 
                  vxtcfg_sync_callback_t *sync_complete);

/*
 * vxt_signal externally defined routines
 * These routines are exported only to the
 * controller implementation level.
 */
extern int
vxtsig_controller_init(vxtcom_ctrlr_t *vxtcom, 
                       void **vxtsig_service_handle);

extern void
vxtsig_controller_shutdown(void *sig_events);

extern int
vxtsig_object_create(vxtcom_ctrlr_t *ctrlr, vxtdev_t *device,
                     void *hash_obj, vxtsig_handle_t *handle);

extern int
vxtsig_object_signal(void *hash_obj_handle, vxtsig_handle_t handle);

extern int
vxtsig_object_destroy(void *hash_obj, vxtsig_handle_t handle);

extern int
vxtsig_object_register_remote(void *hash_obj, vxtsig_handle_t handle,
                              vxtsig_handle_t remote);

extern int
vxtsig_signal(vxtcom_ctrlr_t *vxtcom, void *vxtsig_ctrlr_handle,
              vxtsig_handle_t handle, void *buffer);

extern int
vxtsig_wait(vxtcom_ctrlr_t *vxt_com, void *vxtsig_ctrlr_handle,
            vxtsig_handle_t handle);

extern int
vxtsig_poll(vxtcom_ctrlr_t *vxt_com, void *vxtsig_ctrlr_handle,
            vxtsig_handle_t handle, void *file, void *wait,
            int *revents);

extern int
vxtsig_inc_ref(void *vxtsig_ctrlr_handle, vxtsig_handle_t handle);

extern int
vxtsig_dec_ref(void *vxtsig_ctrlr_handle, vxtsig_handle_t handle);

extern int
vxtsig_trace_playback(void *vxtsig_ctrlr_handle,
                      vxtsig_handle_t sig_handle,
                      void *buffer, uint32_t *entry_count);

#endif /* VXT_COM_INTERNAL_H */
