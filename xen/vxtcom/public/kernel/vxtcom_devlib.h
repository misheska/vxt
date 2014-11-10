#ifndef VXTCOM_DEVLIB_H
#define VXTCOM_DEVLIB_H

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
 * vxtcom_devlib.h
 *
 * This file contains defines and structure definitions
 * for interfaces exported by the vxt controller to
 * device library facility.  
 *
 * The device library facility allows device "drivers"
 * to be registered with the vxt controller.
 *
 * These "drivers" can be of the normal variety if they
 * are interfacing with traditional hardware.  They may
 * also emulate hardware through the use of shared memory
 * and signalling events.  These pseudo devices may be
 * fashioned in a wide variety of ways but the system is
 * geared towards tightly defined simple transport.  
 * Complex device characteristics, filtering, media awareness
 * are all better dealt with in the layer immediately above 
 * transport.  Developers are encouraged to push these matters
 * into the meta-data which is passed on the low-level transport.
 *
 * Pseudo devices are encouraged to use the accompanying
 * vxt-queue support for their communication synchronization
 * needs.  Physically attached devices will need to write
 * queue support to match the device communication specifications
 *
 * Even when older devices are employed that do not make use
 * of shared memory queues, pseudo device transport can often
 * improve performance by fronting such devices and reducing 
 * task level context switching.
 *
 */


/*
 * Note:  It is the aim of this architecture to decouple
 * library support definitions from vxt controller 
 * client definitions, i.e. ioctl's and other file
 * operations available to clients of the vxt controller
 * To this end versioning is decoupled and incorporation
 * of the vxt_com.h file by device library/driver support
 * code is forbidden 
 */

#define VXTCOM_CNTRLR_DEV_VERSION 0x0000000000000001

#define GET_VXTCOM_CNTRLR_DEV_RELEASE(version) \
	((version >> 32) & 0xFFFFFFFF)

#define GET_VXTCOM_CNTRLR_DEV_PATCH(version) \
	(version & FFFF)

#define GET_VXTCOM_CNTRLR_DEV_SUBRELEASE(version) \
	((version >> 16) & 0xFFFF)


typedef ulong vxtdev_handle_t;

typedef struct vxtcom_wait_queue  vxtcom_wait_queue_t;

/*
 * vxtcom_ctrlr_api:
 *
 * This structure of callbacks is exported to the device
 * on a device create call.  It publishes the internal 
 * library of utility functions the controller provides
 * to its devices.
 *
 */

typedef struct vxtcom_ctrlr_api {
	int (*add_queue)
	    (void *device, int qlen, void **queue, void **queue_handle);
	int (*find_queue)
	    (void *device_handle, uint32_t q_ord,
             void **queue, void **queue_handle);
	int (*rem_queue)
	    (void *device, void *queue);
	int (*connect_remote)
	    (void *vxtcom, void *sync_response, void *dev_data, 
	    int datalen, void *device_instance);
	void (*sync_status)
	     (void *sync_response, int status);
	int (*remove_device)
	    (void *vxtcom, void  *device, void *info, 
	     uint32_t info_length, uint32_t signal_remote);
	int (*reg_sm)                  /* register shared memory */
	    (void *device, void *addr, uint32_t len, void *queue_token,
             vxtarch_word token, vxtarch_word *name);
	int (*cancel_sm)               /* register shared memory */
	    (void *device, vxtarch_word token);
	int (*vxtcom_dev_register_file)
	    (void *device_handle, char *dev_base_name,
	     char *dev_file_name, uint64_t *handle);
	int (* wait)
	    (void *device_handle, vxtcom_wait_queue_t *vxt_wait, 
	     long *condition);
	int (* vxt_init_wait)
	    (vxtcom_wait_queue_t *vxt_wait);
	int (* wakeup)
	    (vxtcom_wait_queue_t *vxt_wait);
} vxtcom_ctrlr_api_t;


/* 
 * Device callbacks for customized device responses.
 * This structure is filled in by the device library
 * and provided to the controller.
 */

typedef struct vxtdev_api {
	int (*query)(void *handle, void *query_dev_info,
	             uint32_t buf_size); /* size/format dev specific */
	int (*create)(void *controller, 
	              void *create_dev_struct, 
	              uint32_t info_length,
	              vxtcom_ctrlr_api_t *callbacks,
	              void *sync_response,
	              void **new_device);
 	int (*create_complete)(void *device, void *instance,
	                       int event_port, int slot,
	                       uint64_t domain, int error);
	int (*connect)(void *vxtcom, void *create_info, uint32_t info_len,
	               uint32_t qcount, vxtcom_ctrlr_api_t *callbacks,
                       void **instance);
	int (*attach)(void *device, void *info, uint32_t buf_size);
	int (*detach)(void *device, void *info, uint32_t buf_size);
	int (*destroy)(void *device,  void *info,
	               uint32_t info_length, void *handle);
	int (*snapshot)(vxtdev_handle_t device, void **data, int *size);
} vxtdev_api_t;


extern int
vxtcom_load_device_library(void *vxtcom_handle, vxtdev_api_t *callbacks,
                           vxtdev_type_t device_type);

extern int
vxtcom_unload_device_library(void *vxtcom_handle,
                             vxtdev_type_t device_type);

#endif /* VXTCOM_LIBRARY_H */
