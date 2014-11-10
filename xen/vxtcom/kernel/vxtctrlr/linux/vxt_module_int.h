#ifndef _VXT_MODULE_INT_
#define _VXT_MODULE_INT_

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




extern int
vxtcom_module_init(void);

extern vxtbus_ctrlr_t *
vxtcom_create_controller(void);

extern int
vxtcom_add_ctrlr_pair(vxtbus_ctrlr_t *local_bus_ctrlr, domid_t rdom);

extern int
vxtcom_ctrlr_init(vxtbus_ctrlr_t *bus_instance);

extern int
vxtcom_bus_connect(vxtbus_ctrlr_t *bus_instance);

extern int
vxtcom_bus_disconnect(vxtbus_ctrlr_t *bus_instance);

extern int
vxtcom_bus_initialize_send_queue(void *buffer, long size);

extern int
vxtcom_bus_suspend(vxtbus_ctrlr_t *bus_instance, void *record);

extern int
vxtcom_bus_recover(vxtbus_ctrlr_t *bus_instance, void *record);

extern int
vxtcom_bus_signal_closing(vxtbus_ctrlr_t *bus_instance, uint64_t flags);

extern void
vxtcom_bus_unplug_devices(vxtbus_ctrlr_t *bus_instance, int signal_remote);

extern int
vxtcom_system_shutdown(void);

extern int
vxtcom_remove_ctrlr_pair(vxtbus_ctrlr_t *local_bus_ctrlr, domid_t rdom);

extern void
vxtcom_dec_bus_ref(vxtbus_ctrlr_t *bus_instance);

extern int
vxtcom_parse_config_msg(void *bus_instance);

extern int
vxtcom_dev_lookup_token(vxtarch_word token, vxt_device_sm_t **result);

extern int
vxtcom_dev_token_dec(vxt_device_sm_t *record);

extern int
vxtcom_devreq_find_qpages(uint64_t dev_handle,  void *queue_address,
                          vxtdev_queues_t **queue_resources);




#endif /* _VXT_MODULE_INT_ */
