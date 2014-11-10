#ifndef VXT_COM_OS_H
#define VXT_COM_OS_H

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

#include <linux/mm.h>
#include <linux/vmalloc.h>

#define MAX_TARGET 1000  /* number of available grant references */

extern vxtcom_wait_queue_t vxtcom_ctrlr_changed;
extern int vxtcom_ctrlr_poll_events;
extern int vxtcom_ctrlr_event;
extern vxtcom_wait_queue_t vxtsys_new_dev_wait;
extern int vxtsys_dev_event_list;


/*
 * Structure used to track the attach state
 * of open file descriptors for controllers.
 */
typedef struct vxtcom_file_control {
	vxtdev_type_t device_wait;
	vxtcom_ctrlr_t *controller;
} vxtcom_file_control_t;

/*
 * Structure used to track the attach state
 * of open file descriptors for devices.
 */
typedef struct vxtcom_dev_control {
	vxtcom_ctrlr_t *controller;
	vxtdev_t *device;
	vxtsig_handle_t devevt;
	char     dev_file_name[VXTCOM_MAX_DEV_FILE_NAME];
	uint64_t os_dev;
	int      refcnt;
} vxtcom_dev_control_t;



extern vxtcom_ctrlr_t *active_controllers;
extern vxt_mlock_t active_ctrlr_lock;
extern unsigned int controller_count; 

extern int
vxtcom_os_ctrlr_init(vxtcom_ctrlr_t *instance, uint32_t first_controller);

extern void
vxtcom_os_destroy_controller(vxtcom_ctrlr_t *instance,
                             uint32_t last_controller);

extern int
vxtcom_os_mod_init(void);

extern void
vxtcom_os_mod_remove(void);

extern int
vxtcom_init_wait(vxtcom_wait_queue_t *vxt_wait);

extern int
vxtcom_ctrlr_wait(vxtcom_ctrlr_t *vxtcom,
                  vxtcom_wait_queue_t *wq,
                  long *condition, int timoeut);

extern int
vxtcom_wakeup(vxtcom_wait_queue_t *vxt_wait);

extern int
vxtcom_wakeup_all(vxtcom_wait_queue_t *vxt_wait);

extern int
vxtcom_devreq_add_queue(void *device_handle, int qlen, 
			void **queue, void **queue_handle);

extern int
vxtcom_destroy_queue(vxtdev_queues_t *queue, uint32_t origin);

extern unsigned long
vxtcom_flush_device_pager_range(struct vm_area_struct *vma,
                                unsigned long start, 
                                unsigned long range, 
                                void *flush_details, 
                                vxt_device_sm_t *record);

extern int
vxtcfg_connect_remote_queue(vxtdev_t *device,
                            vxtcom_cntrl_page_ref_t *page_ref_array, 
                            int page_cnt);


static inline void *
vxt_vm_area_addr(void *region)
{
	struct vm_struct *vma;

	vma = (struct vm_struct *)region;
	return vma->addr;
}

#define vxt_mb() mb()


extern void
vxtcom_dev_token_ref(vxt_device_sm_t *record);

extern int 
vxtcom_dev_token_dec(vxt_device_sm_t *record);

extern int
vxtcom_dev_lookup_token(vxtarch_word token, vxt_device_sm_t **result);

extern int
vxtcom_query_bus(vxtcom_ctrlr_t *vxtcom, vxt_query_bus_struct_t *ibuf);

extern int
vxtcom_query_device(vxtcom_ctrlr_t *vxtcom, vxt_query_dev_struct_t *ibuf);

extern int
vxtcom_lookup_ctrlr(vxt_query_controller_struct_t *ibuf);

extern int
vxtcom_attach_dev(vxtcom_ctrlr_t *vxtcom, vxt_attach_dev_struct_t *ibuf);

extern int
vxtcom_detach_dev(vxtcom_ctrlr_t *vxtcom, vxt_detach_dev_struct_t *ibuf);

extern int
vxtcom_create_dev(vxtcom_ctrlr_t *vxtcom, vxt_create_dev_struct_t *ibuf);

extern int
vxtcom_destroy_dev(vxtcom_ctrlr_t *vxtcom, vxt_destroy_dev_struct_t *ibuf,
                   void *info, uint32_t info_length);

extern int
vxtcom_poll_focus(vxtcom_ctrlr_t *instance, vxtcom_file_control_t *of,
                  vxtdev_type_t *dev_type);

extern void
vxt_poll_wait(void *file_handle, 
              vxtcom_wait_queue_t *wait_queue,
              void *wait_handle, vxt_slock_t *lock,
              vxt_sflags_t *lock_flags);

extern vxt_device_sm_t *
vxtcom_alloc_sm_object(void);

extern void
vxtcom_destroy_sm_object(vxt_device_sm_t *record);

extern int
vxtcom_os_register_device_file(char *new_dev_name, vxtdev_t *device,
                               int device_number, uint64_t *device_handle);

extern void
vxtcom_os_disable_device_file(vxtdev_t *device);

extern void
vxtcom_os_unregister_device_file(char *dev_file_name,
                                 uint64_t os_dev, int minor);

extern int
vxtcom_os_device_installed(vxtcom_ctrlr_t *vxtcom, vxtdev_t *device);

extern int
vxtcom_os_device_removed(vxtcom_ctrlr_t *vxtcom, vxtdev_t *device);


#endif /* VXT_COM_OS_H */
