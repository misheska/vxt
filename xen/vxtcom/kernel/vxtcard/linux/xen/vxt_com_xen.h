#ifndef _VXT_COM_XEN_H_
#define _VXT_COM_XEN_H_

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
 * Linux Version Abstractions
 * *************************************************************************
 */



/*
 * End Linux Version Abstractions
 * *************************************************************************
 */

/*
 * Xen Version Abstractions
 * *************************************************************************
 */

/*
 * 
 * vxt_com_xen.h:
 *
 * This file containes Macros and inline functions that are Xen specific
 * and common to the front end, back end, and linux layers.
 *
 */


static inline void
vxt_setup_map_parms(struct gnttab_map_grant_ref *grant_op, maddr_t addr,
                    uint32_t flags, grant_ref_t ref, domid_t remote_dom)
{
#ifdef LEGACY_4X_LINUX
	if (flags & GNTMAP_contains_pte) {
		grant_op->host_addr = addr;
	} else if (xen_feature(XENFEAT_auto_translated_physmap)) {
		grant_op->host_addr = __pa(addr);
	} else {
		grant_op->host_addr = addr;
	}

	grant_op->flags = flags;
	grant_op->ref = ref;
	grant_op->dom = remote_dom;
	grant_op->dev_bus_addr = 0;
	grant_op->handle = 0;

	/*
	 * Set default to status to error
	 * This will catch error in cases 
	 * where the subsequent Hypervisor
	 * call fails before status set.
	 */
	grant_op->status = GNTST_general_error;

#else
	gnttab_set_map_op(grant_op, addr, flags, ref, remote_dom);
#endif
}

static inline void
vxt_setup_unmap_parms(struct gnttab_unmap_grant_ref *grant_op, maddr_t addr,
                      uint32_t flags, grant_handle_t handle)
{
#ifdef LEGACY_4X_LINUX


	if (flags & GNTMAP_contains_pte) {
		grant_op->host_addr = addr;
	} else {
		grant_op->host_addr = __pa(addr);
	}

	grant_op->dev_bus_addr = 0;
	grant_op->handle = handle;
	/*
	 * Set default to status to error
	 * This will catch error in cases 
	 * where the subsequent Hypervisor
	 * call fails before status set.
	 */
	grant_op->status = GNTST_general_error;

#else
	gnttab_set_unmap_op(grant_op, addr, flags, handle);
#endif
}



extern vxt_device_class_t *
get_vxt_class(void);

extern int vxtcom_hub_connect(vxtbus_common_api_t *utilities);

extern int vxtcom_hub_disconnect(void);





#endif /* _VXT_COM_XEN_H_ */
