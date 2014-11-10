/*
 *
 * vxt_module.c:
 *
 * vxt_module.c adapts the VxT O.S. independent code to the Linux
 * loadable module form, making it possible to load or unload the
 * VxT subsystem from a Linux kernel.
 *
 * The vxt_module and the VxT controller code it supports depend upon
 * the function and interfaces provided by the vxt_card module.
 *
 * The vxt_card module adapts the particular hypervisor or other distributed
 * system functionality to the common form consumed by VxT.
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


#define _VXT_COMPONENT_ 1
#define _VXT_SUBSYSTEM_ 2


#include <linux/device.h>

#include <public/vxt_system.h>
#include <public/kernel/bus/hypervisor/vxtcom_bus_ctrlr.h>
#include <public/vxt_auth.h>
#include <public/kernel/vxt_auth_db.h>
#include <public/kernel/vxt_signal.h>
#include <public/kernel/vxt_module.h>
#include <vxtctrlr/linux/vxt_module_int.h>


/*
 * Common Hub communication functions
 */



/*
 * Register the methods necessary for bus initiated update of common
 * vxt configuration and state.
 */

vxtbus_common_api_t vxtbus_utilities = {
	.vxtctrlr_module_init = vxtcom_module_init,
	.create_controller = vxtcom_create_controller,
        .add_ctrlr_pair = vxtcom_add_ctrlr_pair,
	.ctrlr_init = vxtcom_ctrlr_init,
	.bus_connect = vxtcom_bus_connect,
        .bus_disconnect = vxtcom_bus_disconnect,
        .initialize_send_queue = vxtcom_bus_initialize_send_queue,
        .bus_suspend = vxtcom_bus_suspend,
        .bus_recover = vxtcom_bus_recover,
        .bus_signal_closing = vxtcom_bus_signal_closing,
        .bus_unplug_devices = vxtcom_bus_unplug_devices,
        .system_shutdown = vxtcom_system_shutdown,
        .remove_ctrlr_pair = vxtcom_remove_ctrlr_pair,
        .dec_bus_ref = vxtcom_dec_bus_ref,
        .parse_config_msg = vxtcom_parse_config_msg,
        .sig_init_send_buf = vxtsig_init_send_buf,
        .sig_interrupt = vxtsig_interrupt,
        .db_hub_lookup_spoke = vxtdb_hub_lookup_spoke,
        .db_client_record_delete = vxtdb_client_record_delete,
        .dev_lookup_token = vxtcom_dev_lookup_token,
        .dev_token_dec = vxtcom_dev_token_dec,
        .devreq_find_qpages = vxtcom_devreq_find_qpages,
	.db_lookup = vxtcom_db_lookup,
	.db_multi_lookup = vxtcom_db_multilook,


};
	


/*
 *
 * vxt_subsystem_init:
 *
 * vxt_subsystem_init calls the vxt_card initialization routine
 * This routine contacts the underlying hypervisor or distributed
 * system bus to set up communication with the VxT hub.
 *
 * Returns:
 *		0  		=> Upon Success
 *		ENODEV		Cardbus cannot be connected
 *		EBUSY		Some other VxT module is installed
 *
 */

static int __init vxt_subsystem_init(void)
{
	int ret;
	ret = vxt_hub_connect(&vxtbus_utilities);
	return ret;
}


/*
 *
 * vxt_subsystem_cleanup:
 *
 * vxt_subsystem_init calls the vxt_card unplug routine
 * This routine severs its connection with the underlying
 * VxT hub.
 *
 * Returns:
 *		None
 *
 */

static void vxt_subsystem_cleanup(void)
{
	vxt_hub_disconnect();
	return;
}





module_init(vxt_subsystem_init);
module_exit(vxt_subsystem_cleanup);


MODULE_LICENSE("Symantec Proprietary");

