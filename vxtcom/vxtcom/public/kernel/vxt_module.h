#ifndef VXT_MODULE_H
#define VXT_MODULE_H

/*
 *
 * vxt_module.h:
 *
 * Definitions and callbacks
 * isolating the common VXT from
 * its mode of VXT Hub connect.
 * i.e. isolating it from its hypervisor
 * or hardware bus device interconnect.
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


#define VXT_CARD_BANNER  "VxT CARD: version 1.0"


#define INVALID_GRANT_HANDLE   0xFFFF

/*
 * vxtdev_ref_list:
 *
 * tracks the shared pages associated with
 * a shared memory queue.  The structure
 * is subordinated to an internal device
 * structure and tracks the queue resource
 * allocated on the associated device.
 *
 */

typedef struct vxtdev_ref_list {
        uint64_t ref;
        uint64_t handle;
        struct vxtdev_ref_list *next;
} vxtdev_ref_list_t;



/*
 * vxtcom_cntrl_page_ref holds a hypervisor ref list
 * entry for a shared page.  This structure appears
 * in an array under the smcom_cntrl_attach
 * structure with number of entries equal to
 * shared_page_cnt.
 *
 */

typedef struct vxtcom_cntrl_page_ref {
	uint64_t page_ref;
} vxtcom_cntrl_page_ref_t;


typedef struct vxtbus_common_api {
	int (*vxtctrlr_module_init) (void);
	vxtbus_ctrlr_t *(*create_controller) (void);
	int (*add_ctrlr_pair) (vxtbus_ctrlr_t *local_bus_ctrlr, domid_t rdom);
	int (*ctrlr_init) (vxtbus_ctrlr_t *bus_instance);
	int (*bus_connect) (vxtbus_ctrlr_t *bus_instance);
	int (*bus_disconnect) (vxtbus_ctrlr_t *bus_instance);
	int (*initialize_send_queue) (void *buffer, long size);
	int (*bus_suspend) (vxtbus_ctrlr_t *bus_instance, void *state);
	int (*bus_recover) (vxtbus_ctrlr_t *bus_instance, void *state);
	int (*bus_signal_closing)
	    (vxtbus_ctrlr_t *bus_instance, uint64_t flags);
	void (*bus_unplug_devices)
	     (vxtbus_ctrlr_t *bus_instance, int signal_remote);
	int (*system_shutdown) (void);
	int (*remove_ctrlr_pair) 
	    (vxtbus_ctrlr_t *local_bus_ctrlr, domid_t rdom);
	void (*dec_bus_ref) (vxtbus_ctrlr_t *bus_instance);
	int (*parse_config_msg) (void *instance);
	void (*sig_init_send_buf) (void *buffer);
	void (*sig_interrupt)(void *vxtsig_ctrlr_handle, void *buffer);
	int (*db_hub_lookup_spoke) (int spoke, char *remote_uuid);
	int (*db_client_record_delete)
	    (char *client, char *server, char *dev_name);
	int (*dev_lookup_token) (vxtarch_word token,
	                                vxt_device_sm_t **result);
	int (*dev_token_dec) (vxt_device_sm_t *record);
	int (*devreq_find_qpages)
	    (uint64_t dev_handle,  void *queue_address,
	     vxtdev_queues_t **queue_resources);
	int (*db_lookup) (char *key, char *field, char* record_field);
	int (*db_multi_lookup)
	    (char *key, char *field, char **results, uint32_t *rec_cnt);
} vxtbus_common_api_t;


extern int
vxt_hub_connect(vxtbus_common_api_t *utlities);

extern void
vxt_hub_disconnect(void);

extern int
vxt_card_mmap(struct file *file, struct vm_area_struct *vma,
              uint32_t page_origin, uint32_t remote_ep);

extern void
vxt_free_shared_memory(struct page **mem_range, int pg_cnt);

extern int
vxtcard_create_mem_queue(uint64_t rhandle, int page_cnt,
                         vxtdev_queues_t **qpages, void **queue_ptr);

extern int
vxtcard_connect_mem_queue(vxtcom_cntrl_page_ref_t *page_ref_array,
                          int page_cnt, uint64_t rhandle, void *mm,
                          vxtdev_queues_t **qpages);


/*
 * Linux Version Abstractions
 * *************************************************************************
 */

#ifdef LEGACY_4X_LINUX
#define vxt_device_class_t struct class_simple
#else
#define vxt_device_class_t struct class
#endif

/*
 * End Linux Version Abstractions
 * *************************************************************************
 */

extern int
vxtcard_ctrlr_init(vxtbus_ctrlr_t *bus_instance, void *vxt_ctrlr_fops,
                   uint32_t first_controller, int ctrlr_slot_number,
                   int vdevice);

extern void
vxtcard_ctrlr_destroy(int vdev, uint32_t last_controller);

extern int
vxt_class_device_create(uint32_t major, uint32_t minor, 
                        char *new_dev_name);

extern void
vxt_class_device_destroy(uint32_t major, uint32_t minor);

extern int
vxt_uuid_to_local_dom(char *uuid, char *domain_name);

extern int
vxt_get_local_uuid(char *uuid);

extern int
vxtdb_hub_lookup_spoke(int spoke, char *remote_uuid);

extern void
vxtcom_event_signal(void *sig);

#endif /* VXT_MODULE_H */
