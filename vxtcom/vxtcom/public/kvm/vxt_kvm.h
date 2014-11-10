#ifndef _VXT_KVM_H_ 
#define _VXT_KVM_H_ 

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
 * This file publishes the exports from the KVM version of
 * the VxT bus emulator module for Linux.  i.e. kvm_vxt_host.c,
 * the base Linux (hypervisor Linux) VxT module.  These
 * exports include the defines for the KVM/VxT defines made
 * available to the Virtio/Qemu task space entity.
 */




typedef struct kvm_vxt_card_reg_bus_parms {
	uint64_t inbuf_size;
	uint64_t outbuf_size;
	uint64_t sigbuf_size;
	uint64_t guest_inbuf_addr;
	uint64_t guest_outbuf_addr;
	uint64_t guest_sigbuf_addr;
	uint64_t host_inbuf_addr;
	uint64_t host_outbuf_addr;
	uint64_t host_sigbuf_addr;
	int      cfg_irq_fd;
	int      sig_irq_fd;
	int      suspended;
	int      vmfd;
	uint64_t sig_bell;
	uint64_t cfg_bell;
	uint64_t guest_name;  /* Domain name, KVM object address in KVM port */
	char     ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];
	char     guest_uuid[MAX_VXT_UUID];
} kvm_vxt_card_reg_bus_parms_t;

typedef struct kvm_vxtcard_connect_bus_parms {
	uint64_t flags;
} kvm_vxtcard_connect_bus_parms_t;

typedef struct kvm_vxtcard_disconnect_bus_parms {
	uint64_t flags;
} kvm_vxtcard_disconnect_bus_parms_t;

/*
 * Flags bits for vxt_card_del_bus_parms
 * flags field.
 */
#define VXT_CARD_DEL_BUS_SKIP_NOTIFY 0x1 /* No driver shutdown signal */
typedef struct kvm_vxt_card_del_bus_parms {
	uint64_t flags;
} kvm_vxt_card_del_bus_parms_t;

/*
 * ioctls for VxT KVM management fds
 */


#define KVM_PLUG_VXT_BUS	0x1
#define KVM_UNPLUG_VXT_BUS	0x2
#define KVM_CONNECT_VXT_BUS	0x3
#define KVM_DISCONNECT_VXT_BUS	0x4



#endif /* _VXT_KVM_H_  */
