#ifndef VXT_COM_H
#define VXT_COM_H

/*
 *
 * vxt_com.h:
 *
 * vxt_com.h contains the structures, external declarations
 * and defines for the vxt controller device driver.
 *
 * This file allows the client to access and manipulate
 * the devices on the associated instance of the vxt controller
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


typedef uint64_t  vxt_dev_type;

/*
 *
 * Message handle macros:
 *
 * The message handle is not necessary in all implementations.  Where file
 * system access requires more than the name or it is more efficient to
 * access a fle through an indexing handle, the following routines may be
 * useful.  The handle is a 64 bit value which has default handlers to
 * separate it into a primary and secondary field. Individual implementations
 * may use this, may use the entire field as an address or may divide it up
 * in whatever way is deemed appropriate. The field is a private 
 * communciation between the kernel device and the user level device type
 * library.
 *
 */

#define VXT_DEV_HANDLE_GET_PRIMARY(handle)                        \
	(((unsigned long long int)(handle) >> 32) & 0xFFFFFFFF)

#define VXT_DEV_HANDLE_GET_SECONDARY(handle)                      \
	((handle) & 0xFFFFFFFF)

#define VXT_DEV_HANDLE_PUT_PRIMARY(primary, handle)               \
	((handle) =                                               \
	 (((handle) & 0xFFFFFFFF) |                               \
	  ((((uint64_t)(primary)) & (uint64_t)0xFFFFFFFF) << 32)))

#define VXT_DEV_HANDLE_PUT_SECONDARY(secondary, handle)           \
	((handle) =                                               \
	 (((handle) & (((uint64_t)0xFFFFFFFF) << 32)) |           \
	  ((secondary) & 0xFFFFFFFF)))



/*
 * A file is opened to allow access to 
 * generic controller query functions in
 * the absence of any VXT controllers.
 * The following field provides the
 * ability to choose a major number
 * for the device.  An instantiation
 * with zero yields a system chosen
 * value.
 */
#define VXTCOM_CTRLR_BUS_DEVICE 0


#define VXTCOM_CNTRLR_VERSION 0x0000000000000001

#define GET_VXTCOM_CNTRLR_RELEASE(version) \
	((version >> 32) & 0xFFFFFFFF)

#define GET_VXTCOM_CNTRLR_PATCH(version) \
	(version & FFFF)

#define GET_VXTCOM_CNTRLR_SUBRELEASE(version) \
	((version >> 16) & 0xFFFF)

/*
 * Basic query options
 */

#define VXTCTRLR_LOOKUP_NAME   0x1
/*
 * Name lookup across all of the
 * active controllers.  Call
 * returns the major number of
 * the controller the target
 * is found on.
 */
#define VXTCTRLR_GLOBAL_LOOKUP 0x2
/*
 * return the device with the next ordinal value
 * after the value that has been passed.  If there
 * are no more fail the query.
 */
#define VXTCTRLR_LOOKUP_NEXT   0x3
/*
 *
 * Returns the next controller in
 * the active controller list.  The
 * major field is set to 0 to get
 * the first controller, the
 * controller major is returned in
 * the major field.
 */
#define VXTCTRLR_CTRLR_LOOKUP  0x4



/*
 *
 * Global Lookup Options:
 *
 */
#define VXTCTRLR_GLOBAL_DEV_NAME   0x1  // scan entire space for name
#define VXTCTRLR_GLOBAL_CTRLR_SCAN 0x2  // start scan from current ctrlr



/*
 * Communicates that no device
 * is present
 */

#define VXTCTRLR_NULL_DEV 0x0





/*
 * Supported device types
 */

#define VXTCTRLR_MSG_DEV 0x1



/*
 * Symantec communications controller IOCTL defines
 */


#define IOCTL_VXTCTRLR_SIGNAL_DEV         0x1
#define IOCTL_VXTCTRLR_EXECUTE_DONT_USE   0x2  /* permissions issues */
#define IOCTL_VXTCTRLR_QUERY_BUS          0x3
#define IOCTL_VXTCTRLR_QUERY_DEV          0x4
#define IOCTL_VXTCTRLR_ATTACH             0x5
#define IOCTL_VXTCTRLR_DETACH             0x6
#define IOCTL_VXTCTRLR_DEV_CREATE         0x7
#define IOCTL_VXTCTRLR_DEV_REMOVE         0x8
#define IOCTL_VXTCTRLR_POLL_FOCUS         0x9
#define IOCTL_VXTCTRLR_LOOKUP             0xa
#define IOCTL_VXTCTRLR_TRACE_PLAYBACK     0xb
#define IOCTL_VXTCTRLR_WAIT_DEV           0xc


/*
 * Generic Device Power State
 *
 */

#define VXT_COM_DEV_CONNECTING     0x1
#define VXT_COM_DEV_ERROR          0x2 /* error while connecting */
#define VXT_COM_DEV_CONNECTED      0x3
#define VXT_COM_DEV_CONN_ERR       0x4 /* error while connected */

/*
 *
 * POLL signals
 *
 */
#define VXTSIG_IN	0x0001
#define VXTSIG_ERR	0x0008
#define VXTSIG_HUP	0x0010
#define VXTSIG_POLNVAL  0x0020
#define VXTSIG_REMOVE   0x0800


/*
 * Symantec non-maskable events
 */

#define VXTSIG_MANDATORY	(VXTSIG_HUP | VXTSIG_ERR | VXTSIG_REMOVE)



/*
 * IOCTL related structures
 */

/*
 * Responds with proper major device number
 * and file name if there is a controller
 * that can reach the domain or universal 
 * name.  Note: universal name query
 * may only be available from Dom0
 */

typedef struct vxt_query_controller_struct  {
	char      uname[MAX_VXT_UNAME];
	uint32_t  name_match_length;
	char      ep_id[MAX_VXT_UUID];
	uint32_t  remote_domain;
	uint32_t  controller;  /* major driver number */
	char      ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];  /* device file name */
} vxt_query_controller_struct_t;

/*
 *
 * The query bus ioctl is controller specific
 * with the controller determined by the file
 * descriptor it was called for all of its
 * functions save one.
 *
 * The VXTCTRLR_GLOBAL_LOOKUP function looks
 * across the set of active controllers for
 * one that contains a device with the
 * given endpoint name.
 * 
 */

typedef struct vxt_query_bus_struct  {
	uint32_t  function;
	uint32_t  option;
	char      ep_id[MAX_VXT_UUID];
	char      uname[MAX_VXT_UNAME];
	uint32_t  name_match_length;
	uint64_t  dev_name;   // device type
	uint64_t  vxt_bus_id; // returned value
	uint32_t  controller;  /* major driver number */
        char      ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];  /* device file name */
} vxt_query_bus_struct_t;


/*
 * Note: with all device specific ioctls,
 * the controller is determined by the
 * file descriptor the call is made on
 */

typedef struct vxt_query_dev_struct  {
        char      ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];
	uint64_t  vxt_bus_id;
	uint32_t  device_state;
	void      *device_info;   /* device specific */
	uint32_t  info_buf_size;
} vxt_query_dev_struct_t;


typedef struct vxt_attach_dev_struct  {
	uint64_t  vxt_bus_id;
	uint32_t  device_state;
	void      *device_info;   /* device specific */
	uint32_t  info_buf_size;
} vxt_attach_dev_struct_t;

typedef struct vxt_detach_dev_struct  {
	uint64_t  handle;       /* vxt ctrlr bus id */
	uint32_t  device_state;
	void     *device_info;   /* device specific */
	uint32_t  info_buf_size;  /* passed back on close */
} vxt_detach_dev_struct_t;


typedef struct vxt_create_dev_struct  {
	uint64_t   global_name_hash;
	char       uname[MAX_VXT_UNAME];
	uint64_t   dev_name;   // device type
	ulong      vxt_bus_id; // returned value
	void       *device_info;
	uint32_t   info_length;
} vxt_create_dev_struct_t;

typedef struct vxt_unplug_dev_struct  {
        char      ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];
	uint32_t  controller;  /* major driver number for ctrlr */
	uint64_t  vxt_bus_id;
	void      *device_info;   /* device specific */
	uint32_t  info_buf_size;
} vxt_unplug_dev_struct_t;


/*
 *
 * vxt_query_sys_struct:
 *
 * Used on the vxt_msg_system_query call to determine the
 * name and existence of a controller that can reach a
 * specific uname.
 *
 */

typedef struct vxt_query_sys_struct  {
        char      uname[MAX_VXT_UNAME];
	char      ep_id[MAX_VXT_UUID];
	uint32_t  name_match_length;
	uint32_t  domain;
        uint32_t  controller;  /* major driver number */
        char      ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];  /* device file name */
} vxt_query_sys_struct_t;



/*
 *
 * Note: There is no device specific information 
 * on this call.  This is because the destroy or
 * unplug event can be done as a result of 
 * a task level invocation through an ioctl as 
 * show here, or may be triggered through a
 * controller level activity such as remote device
 * removal.  The task level caller of 
 * VXT_CNTRLR_DEV_REMOVE is expected to have
 * called detach first for a clean shutdown.
 * However, the controller level device may
 * well have been unplugged without direct action
 * on the part of the task level client.  In 
 * this case the client will fail when it 
 * attempts to do a device level function. 
 *
 * It is also expected that the device level
 * attach will use a generation number and that
 * this generation number will be passed on
 * all subsequent activities, attach, query,
 * detach, etc.  In the unlikely event that a new
 * device attaches in the same slot, and that the
 * device is of the same type, the device will
 * then fail on in its access attempts.
 */

typedef struct vxt_destroy_dev_struct  {
	uint64_t  vxt_bus_id;     /* controller slot */
	void      *device_info;   /* device specific */
	uint32_t  info_buf_size;  /* passed back on close */
} vxt_destroy_dev_struct_t;

/*
 * Set the open file poll 
 * device type field so that it
 * will wait for new devices of the
 * specified type.  If the field
 * is set to zero, a poll will
 * be signaled on any device
 * create call.
 */

typedef struct vxt_poll_focus_struct  {
	vxtdev_type_t dev_type;
} vxt_poll_focus_struct_t;



/*
 * Asynchronous poll flag fields
 * for VXT system poll.
 *
 */

#define POLL_VXT_NEW_CTRLR    0x1
#define POLL_VXT_CTRLR_DIED   0x2
#define POLL_VXT_SUSPEND      0x4
#define POLL_VXT_NEW_DEV      0x40
#define POLL_VXT_DEV_REMOVED  0x80

/*
 * Asyncrhonous poll flag fields
 * for VXT controller poll
 *
 */

#define VXT_POLL_NEW_DEV    0x1
#define VXT_POLL_LOST_DEV   0x2



#endif /* VXT_COM_H */
