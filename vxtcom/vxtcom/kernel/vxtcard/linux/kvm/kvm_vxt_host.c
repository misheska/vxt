/*
 *
 * kvm_vxt_host, The VxTcom controller:
 *
 * The VxTcom controller for KVM is part of a compound device.
 * It is wrapped by a Virtio PCI device which is in known as the VxT
 * card.  The VxT card allows guest discovery of the VxT controller and
 * hence the VxT bus through the generic PCI discovery mechanism.  The
 * VxT card guest driver handshakes with the KVM VxT PCI device.  This
 * device serves as an intermediary between the VxT controller and the
 * front-end driver and serves up protocols for device initialization,
 * shutdown, suspend, resume, and migration.
 *
 * Primary initialization of the VxTcom controller provided by this
 * module as well as basic device management and shutdown are all
 * mediated through ioctl's from the VxT Card device.  VxT controller
 * initialzation consists of the creation of a communication channel
 * directly between the guest VxT driver and the VxT controller.  The
 * communication channel is comprised of shared memory, IRQs and doorbells.
 *
 * After the VxTcom controller is found and basic device communications 
 * established, bus communications are now possible.  Bus communications
 * via the direct communication channel.
 * 
 * The VxT com controller is part of the trusted computing base.  Therefore
 * it mediates all device plug requests from the guest.  i.e. The guest
 * will request connection to an endpoint via a particular type of VxT
 * device.  The VxT com controller will check for the availability of the
 * necessary hardware or the particular device emulation library and will
 * then check with the authorization database to see if there is an explicit
 * grant for this type of device and this endpoint.
 *
 * The vxtcom_ctrlr_back module will create a new controller instance for each
 * guest domain based on an ioctl request from the KVM/Qemu VxT card emulation.
 * Device plug-in on the associated VxT bus is mediated through the independent
 * authorization service.
 *
 * This particular module is designed adapt the VxT controller to the
 * KVM environment.  It sits on top of the facilities provided for the linux
 * guest port. The utilities and functions of the controller are independent
 * of the linux port and independent of KVM.  They reside in the vxt_com.c
 * module.
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


/*
 * The following two defines are for UMI logging and they must
 * be correct or the logged serial number will be invalid and
 * misleading.
 */

#define _VXT_COMPONENT_ 8
#define _VXT_SUBSYSTEM_ 24

#include <linux/workqueue.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/file.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/eventfd.h>

#include <linux/irqreturn.h>

#include <public/vxt_system.h>
#include <public/kernel/bus/kvm/vxtcom_bus_ctrlr.h>
#include <public/kernel/vxt_module.h>
#include <public/kernel/linux/vxtcom_embedded_os.h>
#include <public/linux/vxt_auth_os.h>
#include <public/kvm/vxt_kvm.h>
#include <public/kernel/vxt_signal.h>



/*
 * KVMCDYKVMCDY: reconcile with KVM authorization port
 */
#define VXTDB_DOM0_ENDPOINT 0

typedef struct vxtcom_card_state {
	vxtbus_ctrlr_t *bus;
	struct file    *kvm_file;
	struct vxtcom_card_state *next; /* linked list of active busses */
} vxtcom_card_state_t;


vxtcom_card_state_t *active_busses;
vxt_mlock_t         active_bus_lock;

vxt_mlock_t         card_mgmt_lock;

vxtbus_common_api_t vxt_cmds;
uint64_t            kvm_card_count;
int                 kvm_card_dev_file;

static void vxtcom_be_connect(vxtbus_ctrlr_t *vxtcom);
static void vxtcom_be_disconnect(vxtbus_ctrlr_t *vxtcom);
static int vxtcom_be_interrupt(vxtbus_ctrlr_t *instance);
static int vxtcom_be_sig_intr(wait_queue_t *wait, unsigned mode,
                              int sync, void *key);

/*
 *
 * vxt_bus_event_signal
 *
 * The method of signaling the remote differs between the guest and the
 * host.  In the case of the host, the method relies on file descriptors
 * set up with kvm bound agents waiting to assert signals on the targeted
 * guest.  In the guest case, signaling of the host is done through
 * virtio queue kick operations.  Therefore, to keep the vxt_card code
 * common between guest and host we call routines loaded in the site
 * specific modules.
 *
 * Returns:
 *		None
 *
 */

void
vxt_bus_event_signal(void *sig)
{
	struct eventfd_ctx       *eventfd = sig;

	UMI_LOG(0, VXTPRINT_DEBUG2_LEVEL, " called, event_ctx %p\n", eventfd);
	eventfd_signal(eventfd, 1);
}


/*
 *
 * vxt_bus_management_hub
 *
 * vxt_bus_management_hub is a module level routine that indicates whether
 * or not the loaded vxt module is a hub or a spoke.  As a hub, the 
 * caller has access to vxt system management facilities and is part of
 * the trusted computing base.  As a spoke the caller is part of the 
 * guest OS environment.
 *
 * Returns:
 *		1 - When this file is linked the vxt_card module is
 *		    resident in the host and is part of the TCB
 *
 */	

int
vxt_bus_management_hub(void)
{
 	return 1;
}

/*
 *
 * vxtcom_cardfile_insert:
 *
 * Returns:
 *
 *		None
 *
 */

void
vxtcom_cardfile_insert(vxtcom_card_state_t *of)
{
	vxt_mlock(&active_bus_lock);
	of->next = active_busses;
	active_busses = of;
	vxt_munlock(&active_bus_lock);
	
}


/*
 *
 * vxtcom_cardfile_remove:
 *
 */

int
vxtcom_cardfile_remove(vxtcom_card_state_t *of)
{
	vxtcom_card_state_t *travel, *follow;

	vxt_mlock(&active_bus_lock);
	travel = active_busses;

	if (active_busses == NULL) {
		vxt_munlock(&active_bus_lock);
		return VXT_FAIL;
	}

	if (of == travel) {
		active_busses = travel->next;
		vxt_munlock(&active_bus_lock);
		return VXT_SUCCESS;
	}

	while (travel->next != NULL) {
		follow = travel;
		travel = travel->next;
		if (travel == of) {
			follow->next = travel->next;
			vxt_munlock(&active_bus_lock);
			return VXT_SUCCESS;
		}	
	}
	
	vxt_munlock(&active_bus_lock);
	return VXT_FAIL;
	
}


/*
 *
 * vxtcom_cardfile_lookup:
 *
 * Note: The active_bus_lock must be held by the caller
 *
 * Returns:
 *		The pointer to the vxtcom_card_state structure
 *		if successful - NULL otherwise
 *
 */

vxtcom_card_state_t *
vxtcom_cardfile_lookup(vxtbus_ctrlr_t *bus)
{
	vxtcom_card_state_t *travel;
	
	travel = active_busses;

	if (bus == NULL) {
		return NULL;
	}

	while (travel != NULL) {
		if (travel->bus == bus) {
			break;
		}
	}

	return travel;
	
}


/*
 *
 * vxtcard_open:
 */

static int
vxtcard_open(struct inode *inode, struct file *filep)
{
	vxtcom_card_state_t *of;
	int major;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, " called\n");
	major = imajor(inode);

	if (major != kvm_card_dev_file) {
		UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL, " device major mismatch "
		        "expected 0x%x, presented 0x%x\n",
		        kvm_card_dev_file, major);
		return -EINVAL;
	}

	of = vxt_kmalloc(sizeof(vxtcom_card_state_t), VXT_KMEM_KERNEL);
	if (of == NULL) {
		UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL, " allocation failure\n");
		return -ENOMEM;
	}
	of->bus = NULL;
	of->next = NULL;
	vxtcom_cardfile_insert(of);
	filep->private_data = of;
	return 0;
}


/*
 *
 * vxtcard_close:
 *
 *
 */

static int
vxtcard_close(struct inode *inode, struct file *filep)
{
	vxtcom_card_state_t *of;

	of = filep->private_data;

	return 0;
} 


/*
 *
 * vxtcom_sig_ptable_proc
 *
 * Wrapper function to associate the self-contained device signal poll table
 * for vxt_bus with the device signal wait queue.  In this way subsequent calls
 * to poll on the targeted poll table will result in a wait on the
 * device attention queue.
 *
 * Returns:
 *		None
 */

static void
vxtcom_sig_ptable_proc(struct file *file,
                       wait_queue_head_t *wqh, poll_table *pt)
{
	vxtbus_ctrlr_t *vxt_bus = container_of(pt, struct vxtbus_ctrlr,
	                                       sigpoll_tbl);
	vxt_bus->sig_wq_head = wqh;
	add_wait_queue(wqh, &vxt_bus->sig_wait);
}


/*
 *
 * vxtcom_cfg_ptable_proc
 *
 * Wrapper function to associate the self-contained config signal poll table
 * for vxt_bus with the config wait queue.  In this way subsequent calls
 * to poll on the targeted poll table will result in a wait on the
 * config wait queue.
 *
 * Returns:
 *		None
 */

static void
vxtcom_cfg_ptable_proc(struct file *file,
                       wait_queue_head_t *wqh, poll_table *pt)
{
	vxtbus_ctrlr_t *vxt_bus = container_of(pt, struct vxtbus_ctrlr,
	                                       cfgpoll_tbl);
	vxt_bus->cfg_wq_head = wqh;
	add_wait_queue(wqh, &vxt_bus->cfg_wait);
}


/*
 *
 * vxtcom_cfg_ptable_exit:
 *
 * Remove the config wait element form the config poll table in
 * preparation for tear down.
 *
 * Returns:
 *		None
 */

static void
vxtcom_cfg_ptable_exit(vxtbus_ctrlr_t *vxt_bus)
{
        if (vxt_bus->cfg_wq_head) {
                remove_wait_queue(vxt_bus->cfg_wq_head,  &vxt_bus->cfg_wait);
                vxt_bus->cfg_wq_head = NULL;
        }
}


/*
 *
 * vxtcom_sig_ptable_exit:
 *
 * Remove the config wait element form the config poll table in
 * preparation for tear down.
 *
 * Returns:
 *		None
 */

static void
vxtcom_sig_ptable_exit(vxtbus_ctrlr_t *vxt_bus)
{
        if (vxt_bus->sig_wq_head) {
                remove_wait_queue(vxt_bus->sig_wq_head,  &vxt_bus->sig_wait);
                vxt_bus->sig_wq_head = NULL;
        }
}


static int
vxtcard_cfg_signal(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	vxtbus_ctrlr_t *instance = container_of(wait, vxtbus_ctrlr_t, cfg_wait);

	vxtcom_be_interrupt(instance);
	return 0;
}

/*
 * vxtcard_register_cfg_doorbell:
 *
 * Set up a poll table, a queue and an element to wait on the
 * guest doorbell signal for VxT bus configuration requests.
 * This signal/interrupt is the method by which KVM guests can 
 * alert Linux base kernel/hypervisor modules directly of changes
 * without first passing through the Qemu/KVM task space.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon Successful Invocation
 *		VXT_PARM - Our sig_bell file descriptor is
 *		           invalid
 *		VXT_FAIL - Unable to push our wait element
 *		           on to the wait queue.
 */

static int
vxtcard_register_cfg_doorbell(vxtbus_ctrlr_t *vxt_bus)
{
	int ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,": called, vxt_bus 0x%p\n",
	        vxt_bus);
	vxt_bus->cfg_bellfile = fget(vxt_bus->cfg_bell);
	if (IS_ERR(vxt_bus->cfg_bellfile)) {
		return VXT_PARM;
	}

	init_waitqueue_func_entry(&(vxt_bus->cfg_wait),
	                          vxtcard_cfg_signal);
	init_poll_funcptr(&(vxt_bus->cfgpoll_tbl), vxtcom_cfg_ptable_proc);


        ret = vxt_bus->cfg_bellfile->f_op->poll(vxt_bus->cfg_bellfile,
	                                        &(vxt_bus->cfgpoll_tbl));
        if (ret < 0) {
		if (vxt_bus->cfg_wq_head) {
                	remove_wait_queue(vxt_bus->cfg_wq_head,
			                  &(vxt_bus->cfg_wait));
		}
		fput(vxt_bus->cfg_bellfile);
		vxt_bus->cfg_bellfile = NULL;
		return VXT_FAIL;
	}

	return VXT_SUCCESS;
	
}


/*
 * vxtcard_register_sig_doorbell:
 *
 * Set up a poll table, a queue and an element to wait on the
 * guest device attention doorbell signal.  This signal is thrown 
 * when one or more VxT devices have data available or space available.
 * The doorbell is forgiving of un-needed alerts but must never be
 * lost.  This signal/interrupt is the method by which KVM guests can 
 * alert Linux base kernel/hypervisor modules directly of changes without
 * first passing through the Qemu/KVM task space.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon Successful Invocation
 *		VXT_PARM - Our sig_bell file descriptor is
 *		           invalid
 *		VXT_FAIL - Unable to push our wait element
 *		           on to the wait queue.
 */

static int
vxtcard_register_sig_doorbell(vxtbus_ctrlr_t *vxt_bus)
{
	int ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,": called, vxt_bus 0x%p\n",
	        vxt_bus);
	vxt_bus->sig_bellfile = fget(vxt_bus->sig_bell);
	if (IS_ERR(vxt_bus->sig_bellfile)) {
		return VXT_PARM;
	}

	init_waitqueue_func_entry(&(vxt_bus->sig_wait), vxtcom_be_sig_intr);
	init_poll_funcptr(&(vxt_bus->sigpoll_tbl), vxtcom_sig_ptable_proc);


        ret = vxt_bus->sig_bellfile->f_op->poll(vxt_bus->sig_bellfile,
	                                        &(vxt_bus->sigpoll_tbl));
        if (ret < 0) {
		if (vxt_bus->sig_wq_head) {
                	remove_wait_queue(vxt_bus->sig_wq_head,
			                  &(vxt_bus->sig_wait));
	}
		fput(vxt_bus->sig_bellfile);
		vxt_bus->sig_bellfile = NULL;
		return VXT_FAIL;
	}

	return VXT_SUCCESS;
	
}


/*
 *
 * vxt_cfgparse_wrapper:
 *
 * This routine exists to modify the incoming parameter into one
 * the generic exported callback can use.  Specifically it finds
 * the parent structure of the embedded queue the caller signaled
 *
 * CDY: FOR FEDORA PORT, may not be long-lived
 *
 */
static void
vxt_cfgparse_wrapper(struct work_struct *work)
{
	vxtbus_ctrlr_t *data =
	   container_of(work, struct vxtbus_ctrlr, config_work);

	vxt_cmds.parse_config_msg((void *)data);
	return;
}


/*
 *
 * vxt_card_bus_register:
 *
 * vxt_card_bus_register would most naturally be called out of a
 * VxT IOCTL mechanism.  However, it is necessary to key the new VxT
 * controller to a specific kvm guest context.  This context is not known
 * to the guest.  Therefore a registered callback is setup with the
 * KVM module.  When VxT loads it registers vxtcom_vxt_create_bus and
 * vxtcom_vxt_destroy_bus.  These callbacks are made when the KVM
 * user level caller makes the KVM_PLUG_VXT_BUS and KVM_UNPLUG_VXT_BUS
 * ioctls respectively.  The internal KVM context is tied to the
 * ioctl file descriptor and so is available on the callback.
 *
 * vxtcom_vxt_create_bus makes the call to create a new vxt controller
 * and then calls vxt_card_bus_register to set up the connection to the
 * targeted guest.  These actions include:
 *
 *	. Translation of the in,out, and sigbuf guest physical addresses
 *	  to KVM user space virtual
 *
 *	. Invocation of kvm_vxt_create_dev to embed the cfg and sig 
 *	  interrupts provided by the caller in an internal device 
 *	  structure, making it possible to inject interrupts from the
 *	  VxT kernel module.
 */

int
vxt_card_bus_register(vxtbus_ctrlr_t *vxt_bus, void *kvm,
                      kvm_vxt_card_reg_bus_parms_t *register_parms) 
{
	struct page *pg_array[1];
	int         ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,": called, "
	        "vxt_bus 0x%p, kvm 0x%p, register_parms 0x%p\n",
	        vxt_bus, kvm, register_parms);

	vxt_bus->kvm = kvm;
	/*
	 * Need this because kvm gfn_to_pfn routines use current
	 * and don't dereference against kvm pointer context.
	 */
	vxt_bus->mm = current;
	/*
	 * Allocate shared memory buffers
	 */

	/*
	 * translate and find host virtual addresses
	 */

	register_parms->host_inbuf_addr = 
	   gfn_to_hva(kvm, register_parms->guest_inbuf_addr >> PAGE_SHIFT);
	register_parms->host_outbuf_addr = 
	   gfn_to_hva(kvm, register_parms->guest_outbuf_addr >> PAGE_SHIFT);
	register_parms->host_sigbuf_addr = 
	   gfn_to_hva(kvm, register_parms->guest_sigbuf_addr >> PAGE_SHIFT);

	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,": host virtual addresses "
	        "inbuf 0x%llx, outbuf 0x%llx, sigbuf 0x%llx\n",
		register_parms->host_inbuf_addr,
	        register_parms->host_outbuf_addr,
	        register_parms->host_sigbuf_addr);


	/*
	 * Note: We could use the full shared memory queue
 	 * primitive here.  i.e. Allocate a VxT queue, populate
 	 * it with kernel memory and then map that memory over
	 * the associated virtual memory locations found in
	 * the Qemu/KVM user task space.  However, this is
	 * unnecessary.  The shared memory needed for the
	 * configuration queue has two permanently fixed endpoints:
	 * the guest and the hypervisor.  Based on this we 
	 * use the KVM gfn_to_pfn call to wire down the physical
	 * page and map these pages into the kernel.  This
	 * removes the mmaping function the Virtio caller/device
	 * would otherwise be required to do.
	 */

	
	/*
	 * NOTE:  we reverse the in_page and out_page here to allow
	 * common code for the VxT controller configuration parser
	 * engine.
	 */
	vxt_bus->out_page = 
	   gfn_to_pfn(kvm, register_parms->guest_inbuf_addr >> PAGE_SHIFT);
	vxt_bus->in_page = 
	   gfn_to_pfn(kvm, register_parms->guest_outbuf_addr >> PAGE_SHIFT);
	vxt_bus->sig_page = 
	   gfn_to_pfn(kvm, register_parms->guest_sigbuf_addr >> PAGE_SHIFT);
	

	/*
	 * Map the guest VxT Bus Config queue pages
	 */

	pg_array[0] =  pfn_to_page(vxt_bus->in_page);
	vxt_bus->inq =  vmap(pg_array, 1, 0, PAGE_KERNEL_NOCACHE);
	pg_array[0] =  pfn_to_page(vxt_bus->out_page);
	vxt_bus->outq = vmap(pg_array, 1, 0, PAGE_KERNEL_NOCACHE);
	pg_array[0] =  pfn_to_page(vxt_bus->sig_page);
	vxt_bus->sig_buf = vmap(pg_array, 1, 0, PAGE_KERNEL_NOCACHE);

	/*
	 * Setup vxtcom bus structure fields for the signal buffer
	 * 
         * The signal buffer is split into two pieces
         * allowing simultaneous signalling in both
         * directions.
         *
         * We initialize the part that we will treat
         * as our send, or signalling buffer.
         */
        vxt_bus->sig_write = 
	   (void *)(((char *)vxt_bus->sig_buf) + VXTSIG_SEC_BUFF_OFFSET);
        vxt_bus->sig_read = vxt_bus->sig_buf;
        vxt_cmds.sig_init_send_buf(vxt_bus->sig_write);

	/*
	 * Register VxT Bus IRQs
	 */

	vxt_bus->cfg_irq = register_parms->cfg_irq_fd;
	vxt_bus->sig_evt = register_parms->sig_irq_fd;

	/*
	 * De-reference the file descriptor and bump the
	 * count on the underlying file structure for the
	 * irq fd's.
	 */
	vxt_bus->cfg_irq_file = fget(vxt_bus->cfg_irq);
	if (!vxt_bus->cfg_irq_file) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "fget failed on cfg_irq_file, ret = %p\n",
		         vxt_bus->cfg_irq_file);
		vunmap(vxt_bus->inq);
		vunmap(vxt_bus->outq);
		vunmap(vxt_bus->sig_buf);
		kvm_release_page_clean(pfn_to_page(vxt_bus->in_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->out_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->sig_page));
		return VXT_FAIL;
	}
	vxt_bus->sig_irq_file = fget(vxt_bus->sig_evt);
	if (!vxt_bus->sig_irq_file) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "fget failed on sig_irq_file, ret = %p\n",
		         vxt_bus->sig_irq_file);
		fput(vxt_bus->cfg_irq_file);
		vunmap(vxt_bus->inq);
		vunmap(vxt_bus->outq);
		vunmap(vxt_bus->sig_buf);
		kvm_release_page_clean(pfn_to_page(vxt_bus->in_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->out_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->sig_page));
		return VXT_FAIL;
	}

	vxt_bus->cfg_irq_ctx = eventfd_ctx_fileget(vxt_bus->cfg_irq_file);
	if (IS_ERR(vxt_bus->cfg_irq_ctx)) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "eventfd_ctx_fileget failed on cfg_irq_file, "
		         "ret = %p\n",
		         vxt_bus->cfg_irq_ctx);
		fput(vxt_bus->cfg_irq_file);
		fput(vxt_bus->sig_irq_file);
		vunmap(vxt_bus->inq);
		vunmap(vxt_bus->outq);
		vunmap(vxt_bus->sig_buf);
		kvm_release_page_clean(pfn_to_page(vxt_bus->in_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->out_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->sig_page));
		return VXT_FAIL;
	}
	vxt_bus->sig_irq_ctx = eventfd_ctx_fileget(vxt_bus->sig_irq_file);
	if (IS_ERR(vxt_bus->sig_irq_ctx)) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "eventfd_ctx_fileget failed on sig_irq_file, "
		         "ret = %p\n",
		         vxt_bus->sig_irq_ctx);
		eventfd_ctx_put(vxt_bus->cfg_irq_ctx);
		fput(vxt_bus->sig_irq_file);
		fput(vxt_bus->sig_irq_file);
		vunmap(vxt_bus->inq);
		vunmap(vxt_bus->outq);
		vunmap(vxt_bus->sig_buf);
		kvm_release_page_clean(pfn_to_page(vxt_bus->in_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->out_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->sig_page));
		return VXT_FAIL;
	}


	/*
	 * Load the Guest doorbells for device signal and
	 * configuration.  Set up the interrupt handlers for them.
	 */
	vxt_bus->cfg_bell = register_parms->cfg_bell;
	vxt_bus->sig_bell = register_parms->sig_bell;

	ret = vxtcard_register_cfg_doorbell(vxt_bus);

	if (ret) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "vxtcard_register_cfg_doorbell failed, ret = 0x%x\n",
		         ret);
		eventfd_ctx_put(vxt_bus->cfg_irq_ctx);
		eventfd_ctx_put(vxt_bus->sig_irq_ctx);
		fput(vxt_bus->cfg_irq_file);
		fput(vxt_bus->sig_irq_file);
		vunmap(vxt_bus->inq);
		vunmap(vxt_bus->outq);
		vunmap(vxt_bus->sig_buf);
		kvm_release_page_clean(pfn_to_page(vxt_bus->in_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->out_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->sig_page));
		return VXT_PARM;
        }
	
	ret = vxtcard_register_sig_doorbell(vxt_bus);

	if (ret) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "vxtcard_register_sig_doorbell failed, ret = 0x%x\n",
		         ret);
		eventfd_ctx_put(vxt_bus->cfg_irq_ctx);
		eventfd_ctx_put(vxt_bus->sig_irq_ctx);
		fput(vxt_bus->cfg_irq_file);
		fput(vxt_bus->sig_irq_file);
		vunmap(vxt_bus->inq);
		vunmap(vxt_bus->outq);
		vunmap(vxt_bus->sig_buf);
		kvm_release_page_clean(pfn_to_page(vxt_bus->in_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->out_page));
		kvm_release_page_clean(pfn_to_page(vxt_bus->sig_page));
		if (vxt_bus->cfg_wq_head) {
			remove_wait_queue(vxt_bus->cfg_wq_head,
			                  &(vxt_bus->cfg_wait));
		}
		fput(vxt_bus->cfg_bellfile);
		vxt_bus->cfg_bellfile = NULL;

		return VXT_PARM;
        }


	return VXT_SUCCESS;
}


/*
 *
 * vxtcom_vxt_create_bus:
 *
 * Allocate the data structures for a controller. This creates a vxtbus_ctrlr_t
 * structure that is used by the vxt_com module and this module.
 *
 * Returns:
 *		VXT_SUCCESS => Upon successful invocation
 *		VXT_PARM -  Version Mismatch,
 *		            Unable to get the file descriptors
 *		VXT_NOMEM - Memory allocation failure
 *		VXT_FAIL -  Unable to initialize the controller
 *
 */

int vxtcom_vxt_create_bus(vxtcom_card_state_t *of,
                          kvm_vxt_card_reg_bus_parms_t *parms)
{
        vxtbus_ctrlr_t *vxt_bus;
	int            ret;


	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,": called\n");
	UMI_LOG(0, VXTPRINT_BETA_LEVEL, "Calling Parameters:\n"
	        "\tinbuf_size 0x%llx, outbuf_size 0x%llx\n"
	        "\tsigbuf_size 0x%llx, guest_inbuf_addr 0x%llx\n"
	        "\tguest_outbuf_addr 0x%llx, guest_sigbuf_addr 0x%llx\n"
	        "\thost_inbuf_addr 0x%llx, host_outbuf_addr 0x%llx\n"
	        "\thost_sigbuf_addr 0x%llx, cfg_irq_fd 0x%x, \n"
	        "\tsig_irq_fd 0x%x, suspended 0x%x, vmfd 0x%x\n"
	        "\tsig_bell 0x%llx, cfg_bell 0x%llx\n"
		"\tLocal uuid %s\n"
		"\tController Name %s\n\n",
	        parms->inbuf_size, parms->outbuf_size, 
	        parms->sigbuf_size, parms->guest_inbuf_addr,
	        parms->guest_outbuf_addr, parms->guest_sigbuf_addr,
	        parms->host_inbuf_addr, parms->host_outbuf_addr,
	        parms->host_sigbuf_addr, parms->cfg_irq_fd,
	        parms->sig_irq_fd, parms->suspended, parms->vmfd,
	        parms->sig_bell, parms->cfg_bell, parms->guest_uuid,
		parms->ctrlr_name);
	/*
	 * Create a VxT bus instance
	 * Note: We will have the bus instance in place early
	 * this will allow the set-up of the shared memory
	 * token mechanism to happen before we need it below
	 * to push the config and signal buffers into the guest.
	 */


        vxt_bus = vxt_cmds.create_controller();
        UMI_LOG(0, VXTPRINT_DEBUG2_LEVEL,
                "vxtcom_create_controller completed\n");
        if (!vxt_bus) {
		UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL,
		        "Controller instantiation failed\n");
                return VXT_NOMEM;
	}

        if (vxt_bus->version != VXTCOM_BUS_CNTRLR_VERSION) {
                /*
                 * Destroy the controller
                 */
                vxt_cmds.dec_bus_ref(vxt_bus);
		UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL,
			" incompatible VxT controller module\n");
                return VXT_PARM;
        }
        UMI_LOG(0, VXTPRINT_DEBUG2_LEVEL,"version check successful\n");

        vxt_bus->connected = VXTCOM_STATE_DISCONNECTED;
	vxt_bus->vdev = NULL; /* no Linux bus connection to this module */
	vxt_bus->vdevice = 0; /* may pass in a default later */
        vxt_bus->inq = NULL;
        vxt_bus->outq = NULL;
        vxt_bus->sig_buf = NULL;
        vxt_bus->sig_evt = 0;
        vxt_bus->cfg_irq = 0;
        vxt_bus->user_ref_cnt = 0;
        vxt_bus->pending_config_event = 0;
	vxt_bus->cfg_bell = 0;
	vxt_bus->cfg_bellfile = NULL;
	vxt_bus->cfg_wq_head = NULL;
	vxt_bus->sig_bell = 0;
	vxt_bus->sig_bellfile = NULL;
	vxt_bus->sig_wq_head = NULL;
	vxt_bus->ctrlr_name[0] = 0;  /* No guest KVM pointer affiliate */
	strncpy(vxt_bus->remote_uuid, parms->guest_uuid, MAX_VXT_UUID); 
	strncpy(vxt_bus->local_uuid,
	        "00000000-0000-0000-0000-000000000000", MAX_VXT_UUID); 
/* Put back after move from Fedora
	INIT_WORK(&vxt_bus->config_work,
	          (void (*)(void *))vxt_cmds.parse_config_msg,
	          (void *)instance);
*/
INIT_WORK(&vxt_bus->config_work, vxt_cfgparse_wrapper);
	/*
	 * Cannot initiate device creation at this end
	 */
	vxt_bus->indirect_create = 1;

        ret = vxt_cmds.ctrlr_init(vxt_bus);
        if (ret) {
                /*
                 * Destroy the controller
                 */
                vxt_cmds.dec_bus_ref(vxt_bus);
                return VXT_FAIL;
        }

        /*
         * Copy in the remote guest's uuid
         * Must be done after vxtcom_ctrlr_init because
         * vxtcom_ctrlr_init starts the vxt authorization
         * database.
         */
	of->kvm_file = fget(parms->vmfd);
	if (!of->kvm_file) {
                /*
                 * Destroy the controller
                 */
                vxt_cmds.dec_bus_ref(vxt_bus);
                return VXT_PARM;
	}
	if (!of->kvm_file->private_data) {
		fput(of->kvm_file);
                /*
                 * Destroy the controller
                 */
                vxt_cmds.dec_bus_ref(vxt_bus);
                return VXT_PARM;
	}

/*
        if (vxt_cmds.db_hub_lookup_spoke(dev->otherend_id,
            vxt_bus->remote_uuid) != VXT_SUCCESS) {
                UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
                         " Unable to find remote uuid "
                         "for domain %d\n", dev->otherend_id);
                vxt_bus->remote_uuid[0] = 0;
        }
*/



        /*TODO init any spinlocks */

	of->bus = vxt_bus;
	vxt_bus->vdev = (void *)of;

	strncpy(parms->ctrlr_name, vxt_bus->ctrlr_name,
	        VXTCOM_MAX_DEV_FILE_NAME);


	vxt_card_bus_register(vxt_bus, of->kvm_file->private_data, parms);
	/*
	 * Capture the KVM pointer as the Domain name to give to the guest
	 * This will be used as a handle in guest_uuid lookup
	 */
	parms->guest_name = (unsigned long long)(vxtarch_word)vxt_bus->kvm;

        UMI_LOG(0, VXTPRINT_BETA_LEVEL, "vxtcom registration return\n");

	return VXT_SUCCESS;
}


/*
 * vxtcom_be_remove
 *
 * remove a vxtcom controller
 *
 * vxtcom_be_remove first acts on the common vxt_controller
 * through the registered callbacks.  The first actions is
 * to stop new devices from being created.  The next
 * action tells the remote that the bus will be going down
 * through a configuration message.  Since the VxT controller
 * is still up. This gives both ends a chance to flush
 * any device traffic that is still in the queues.
 *
 * After returning from the remote endpoint flush signal,
 * vxtcom_be_remove calls the common code to shut down
 * any devices found on the controller/bus.
 *
 * There is a default VxT message device that accompanies
 * any VxT frontend/backend pair.  This device, like
 * any other must have an authorization record.  vxtcom_be_remove
 * contacts the VxT authorization database and asks that
 * the default record be removed.
 *
 * Finally, vxtcom_be_remove contacts the common layer and
 * signals a VxT controller shutdown.  After that, it 
 * changes the Xenbus state to "Closed", signaling through
 * the Xenbus to the frontend that the VxT device has been
 * powered down.
 *
 * Returns:
 *
 *		0
 *
 */

static int vxtcom_be_remove(vxtbus_ctrlr_t *vxtcom)
{
/*
	char ctrl_state_dir[50];
*/
	vxtcom_card_state_t *of;
	int ret;

	of = (vxtcom_card_state_t *)vxtcom->vdev;
	if (of == NULL) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "no device, has kvm_task called us twice?\n");
		return 0;
	}

	of->bus = NULL;
	vxtcom->vdev = NULL;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        "vxtcom_be_remove called: kvm fd 0x%p\n", of->kvm_file);
	if (vxtcom) {
		UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
		        "Vxtcom is present, showing signal descriptors: "
		        " cfgdb 0x%p, sigdb 0x%p, cfgirq 0x%p, sig irq 0x%p\n",
		        vxtcom->cfg_bellfile, vxtcom->sig_bellfile,
		        vxtcom->cfg_irq_file, vxtcom->sig_irq_file);

		/*
		 * Disable the creation of new devices
		 * on this controller
		 */
		vxt_cmds.bus_disconnect(vxtcom);
		/*
		 * Do the same for the remote 
		 */
		vxt_cmds.bus_signal_closing(vxtcom, VXTCOM_CLOSING_BUS_DOWN);
		/*
		 * Remove all devices present 
		 * on the controller
		 */
		vxt_cmds.bus_unplug_devices(vxtcom, 1);
		/*
		 * Remove the default device authorization 
		 * record
		 */
		UMI_LOG(145, VXTPRINT_DEBUG_LEVEL, 
		        "printing uuid value %s\n", vxtcom->remote_uuid);
/*
		sprintf(ctrl_state_dir,
		        "/local/domain/%d/control/shutdown",
		        dev->otherend_id);
		if (xenbus_exists(XBT_NIL, ctrl_state_dir, "")) {
*/
			/*
			 * We are shutting down
			 * Go ahead and delete the default record
			 */
			ret =
			   vxt_cmds.db_client_record_delete(vxtcom->remote_uuid,
			                                    VXTDB_DOM0_ENDPOINT,
			                                    VXT_DEFAULT_DEVICE);
/*
			if (ret) {
				UMI_WARN(146, VXTPRINT_PRODUCT_LEVEL,
				         " Unable to locate remote domain UUID "
				         "record for %d, uuid = %s\n",
				         dev->otherend_id, vxtcom->remote_uuid);
			}
*/
/*
		}
*/


		vxtcom->connected = VXTCOM_STATE_DISCONNECTED;


	}


	return 0;
}


/*
 *
 * vxtcom_ioctl
 *
 */


int
vxtcard_ioctl(struct inode *inode, struct file *filep,
              unsigned command, unsigned long argument)
{
	vxtbus_ctrlr_t *bus;
	vxtcom_card_state_t *of;     /* open file state */
	int            ret;

	union ioctl_structures {
		kvm_vxt_card_reg_bus_parms_t register_parms;
	} ibuf;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"command %llu\n", (uint64_t)command);

	ret = -EFAULT; /* default failure status */

        if ((of = (vxtcom_card_state_t *)filep->private_data) == NULL) {
                UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL, "invalid file pointer\n");
                return -EINVAL;
        }

        of = filep->private_data;
        bus = of->bus;
        vxt_mlock(&card_mgmt_lock);
	
        switch (command) {

	case KVM_PLUG_VXT_BUS:
	{
		ret = copy_from_user((void *)&ibuf, (void __user *)argument,
		                     sizeof(kvm_vxt_card_reg_bus_parms_t));
		if (ret) {
			UMI_LOG(0, VXTPRINT_BETA_LEVEL,
			        "Error copying Register bus argument\n");
		}
        	if (bus == NULL) {
			/*
			 * The only valid call under these circumstances is a 
			 * KVM_PLUG_VXT_BUS.
			 */

			ret = vxtcom_vxt_create_bus(of, &ibuf.register_parms);
			if (ret) {
				break;
			}

			UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
			        "virtio_vxt_init_com_channel: "
			        "Returned values from KVM_PLUG_VXT_BUS "
			        "host_inbuf_va 0x%llx, "
			        "host_outbuf_va 0x%llx, "
			        "host_sigbuf_va 0x%llx\n",
			        ibuf.register_parms.host_inbuf_addr,
			        ibuf.register_parms.host_outbuf_addr,
			        ibuf.register_parms.host_sigbuf_addr);

			ret =
			   copy_to_user((void __user *)argument,
			                (void *)&ibuf,
			                sizeof(kvm_vxt_card_reg_bus_parms_t));

		} else {
			UMI_LOG(0, VXTPRINT_BETA_LEVEL,
			        "KVM_VXT_PLUG on an already initialized bus\n");
			ret = -EINVAL;
		}
		break;
	}

	case KVM_CONNECT_VXT_BUS:
	{
		vxtcom_be_connect(bus);
		ret = 0;
		break;
	}

	case KVM_DISCONNECT_VXT_BUS:
	{
        	if (bus == NULL) {
			/*
			 * The only valid call under these circumstances is a 
			 * KVM_PUB_VXT_BUS.
			 */
			ret = -EINVAL;
			break;
		}
		ret = vxtcom_be_remove(bus);
		if (ret) {
			break;
		}
		break;
	}

	case KVM_UNPLUG_VXT_BUS:
	{
		vxtcom_be_disconnect(bus);
		of->bus = NULL;
		ret = 0;
		break;
	}

	default:
		ret = -EINVAL;
	}
        vxt_munlock(&card_mgmt_lock);



	return ret;
}


static struct file_operations vxt_card_fops =
{
	.owner   = THIS_MODULE,
	.ioctl   = vxtcard_ioctl,
	.open    = vxtcard_open,
	.release = vxtcard_close,
};


/*
 *
 * vxtcom_init_kvm_card_dev:
 *
 * Export a file descriptor to publish the vxt_card management 
 * ioctl's.
 *
 */

int
vxtcom_init_kvm_card_dev(void)
{
	int                major;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, " called\n");

	major = register_chrdev(0, "vxtcard-config", &vxt_card_fops);

	kvm_card_dev_file = major;

	if (major < 0) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         " unable to create a character device\n");
                return (VXT_FAIL);
        }
	vxt_class_device_create(major, 0, "vxtcard-config");

	return VXT_SUCCESS;

}


/*
 *
 * vxtcom_release_kvm_card_dev:
 *
 * vxtcom_release_kvm_card_dev removes the exported file name
 * and descriptor for KVM Card registrartion and management
 * routines.
 *
 * Returns:
 *
 *		None
 *
 */

void
vxtcom_release_kvm_card_dev(void)
{
	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, " called\n");

	vxt_class_device_destroy(kvm_card_dev_file, 0);

        unregister_chrdev(kvm_card_dev_file, "vxtcard-config");

        return;

}


/*
 *
 * vxt_reinit_t:
 *
 * vxt_reinit_t holds the identifier for the remote vxt_controller 
 * endpoint.  It is used to target the object of a re-initialization of the 
 * provisioned state.  This is a reset of the virtual hardware state of
 * the VxT controller and its Xenbus presentation affecting both the
 * back and front end.
 *
 */

typedef struct vxt_reinit {
	vxtarch_word parm;
} vxt_reinit_t;



/*
 *
 * vxtcom_reinit_xenstore_record:
 *
 * vxtcom_reinit_xenstore_record invokes a user space application
 * to clean the xenstore record for the targeted domain, preparing
 * it by setting it into the "Initialized" state.
 *
 * Returns:  0
 *
 */

#ifdef notdefcdy
static int
vxtcom_reinit_xenstore_record(void *reinit_handle)
{
	vxt_reinit_t *reinit_parm = (vxt_reinit_t *)reinit_handle;
	int domain_id = (int)(long)(reinit_parm->parm);
	static char *env[]={ 
	                "HOME=/",
	                "PATH=/usr/local/bin:/sbin:/usr/sbin:/bin:/usr/bin",
	                "SHELL=/bin/bash",
	                "TERM=linux",
	                NULL
	                };


	char argv[70];
	char *args[4];
	int ret;

	UMI_LOG(150, VXTPRINT_PROFILE_LEVEL,
	        "running, init record %d\n", domain_id);

	vxt_kfree(reinit_handle);
	args[0] = argv;
	strcpy(args[0], "/etc/vxtcom/bin/vxtcom-setup-domain"); 
	args[1] = argv + strlen(args[0]) + 1;
	sprintf(args[1], "Domain-%d", domain_id);
	args[2] = NULL;
	
	ret = call_usermodehelper(args[0], args, env, 0);

	if (ret < 0) {
		UMI_LOG(151, VXTPRINT_PRODUCT_LEVEL,
		        "XenStore record cleanup failed\n");
	}

	return 0;

}
#endif






/*
 * vxtcom_be_disconnect
 *
 * Disconnect a controller. Unbinds the irq's and doorbells, unmaps buffers.
 *
 */ 

void
vxtcom_be_disconnect(vxtbus_ctrlr_t *vxtcom)
{
	int ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, " on bus %p\n", vxtcom);
	/* 
	 * Flush gnttab callback work. 
	 * Must be done with no locks held.
 	 * We must remove work before we attempt to use
	 * inbuf/outbuf and a stale vxtctrlr
	 */
	flush_scheduled_work();

	/*
	 * Shutdown the polled doorbell handlers before
	 * letting go of the file descriptors on the eventfds.
	 */

	vxtcom_sig_ptable_exit(vxtcom);
	vxtcom_cfg_ptable_exit(vxtcom);

	fput(vxtcom->cfg_bellfile);
	fput(vxtcom->sig_bellfile);

	/*
	 * Drop our reference on the IRQ file descriptors as
	 * well. (on the irqfd's)
	 */
	eventfd_ctx_put(vxtcom->cfg_irq_ctx);
	eventfd_ctx_put(vxtcom->sig_irq_ctx);
	fput(vxtcom->cfg_irq_file);
	fput(vxtcom->sig_irq_file);

	vxtcom->cfg_bellfile = NULL;
	vxtcom->sig_bellfile = NULL;
	vxtcom->cfg_irq_file = NULL;
	vxtcom->sig_irq_file = NULL;

	/*
	 * Unmap the guest VxT Bus Config queue pages
	 */

	vunmap(vxtcom->inq);
	vxtcom->inq = NULL;
	vunmap(vxtcom->outq);
	vxtcom->outq = NULL;
	vunmap(vxtcom->sig_buf);
	vxtcom->sig_buf = NULL;


	/*
	 * Release the page references and wiring references
	 * associated with our shared queue pages.
	 */
	kvm_release_pfn_clean(vxtcom->in_page);
	kvm_release_pfn_clean(vxtcom->out_page);
	kvm_release_pfn_clean(vxtcom->sig_page);


	vxtcom->sig_read = NULL;
	vxtcom->sig_write = NULL;


	ret = vxt_cmds.remove_ctrlr_pair(vxtcom, (vxtarch_word)vxtcom->kvm);

	if (ret) {
		UMI_WARN(148, VXTPRINT_PRODUCT_LEVEL,
		         "active controller list does not contain "
		         "vxtcom %p, for other end: %p\n",
		         vxtcom, vxtcom->kvm);
       	}


	/*
	 * Destroy the controller
	 */
	vxt_cmds.dec_bus_ref(vxtcom);
}


/*
 * vxtcom_be_sig_intr:
 *
 * Process interrupt for virtual device signals from the
 * front end.
 *
 * Note: We made the dev_events hash table part of the
 * bus ctrlr structure.  In future when we have a list of
 * these for all of the endpoints this code should move
 * to a list off kept off of the vxtcom controller structure
 * The actual field sent here should be the hash instance
 * not the bus controller.
 * 
 */

static int
vxtcom_be_sig_intr(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	vxtbus_ctrlr_t *instance = container_of(wait, vxtbus_ctrlr_t, sig_wait);
	UMI_LOG(0, VXTPRINT_DEBUG2_LEVEL," called, bus %p\n", instance);
	/*
	 * Process the signals generated by the remote
	 * controller
	 */
	vxt_cmds.sig_interrupt(instance->dev_events, instance->sig_read);
	return 0;
}


/*
 * vxtcom_be_interrupt
 *
 * Process interrupt from frontend. 
 */

static int
vxtcom_be_interrupt(vxtbus_ctrlr_t *instance)
{


	UMI_LOG(189, VXTPRINT_DEBUG_LEVEL,
	        "vxtcom_be_interrupt called, bus %p\n", instance);
	
	if (unlikely(instance->connected != VXTCOM_STATE_CONNECTED)) {
		UMI_LOG(190, VXTPRINT_PRODUCT_LEVEL,
		        "spurious interrupt, bus: %p not connected.  "
		        "Delay delivery until controller initialization "
		        "is completed\n", instance);
		/*
		 * We take all spurious interrupts seriously as there is
		 * no practical way to secure the sequencing of xenbus
		 * initialization.  We therefore accept device configuration
		 * requests and then delay a check of associated queues
		 * until we have completed configuration.  We must of course
		 * initialize the queues before exporting them in case
		 * a spurious interrupt hits us before the remote has
		 * had a chance to set up.
		 */
		instance->pending_config_event = 1;
		return IRQ_HANDLED;
	}

	/*
	 * We do not have event counting, one signal will handle all
	 * pending requestors.  (Assuming no spurious interrupts,
	 * by design there should be only one anyway)
	 */
	instance->pending_config_event = 0;


	/*
	 * Schedule config message queue parsing
	 * and parse actions
	 */
        schedule_work(&instance->config_work);




	return IRQ_HANDLED;
}


/*
 * vxtcom_be_connect
 *
 * Complete instance initialisation for the message
 * queues, connect the vxt bus to the vxtcom structure,
 * register at the common vxt function level and 
 * assert any pending interrupts.
 *
 * Returns:
 *
 *		None
 */

static void
vxtcom_be_connect(vxtbus_ctrlr_t *vxtcom)
{
	int err;

	UMI_LOG(191, VXTPRINT_PROFILE_LEVEL,
	        "bus %p\n", vxtcom);

	/*
	 * Initialise buffer headers and output buffer
	 */
	vxtcom->inq_header.msg_id = 0;
	vxtcom->outq_header.msg_id = 1;
	vxtcom->outq_header.reply_in_use = 0;
	vxtcom->outq_header.request_in_use = 0;
	/*
	 * Use these two fields as a way of
	 * restarting processing when the controller lock
	 * stops interrupt level queue handling.
	 * The flags are checked on controller lock
	 * release.
	 */
	vxtcom->inq_header.reply_in_use = 0;
	vxtcom->inq_header.request_in_use = 0;
	vxtcom->request_offset = 0;
	vxtcom->reply_offset = 0;

	vxt_cmds.initialize_send_queue((void *)vxtcom->outq, PAGE_SIZE);

	UMI_LOG(192, VXTPRINT_BETA_LEVEL,"Config queues are now connected\n");

	/*
	 * Push the vxtcom structure onto a Domain based lookup
	 * list.  This is of limited value for front-ends where
	 * the number of controller connections is limited to
	 * the number of connections to controlling authorities
	 * however it is the means by which guest to guest
	 * connections are made and transport configuration
	 * chain messages sent for the back-end.
	 */

	err = vxt_cmds.add_ctrlr_pair(vxtcom, (vxtarch_word)vxtcom->kvm);
	if (err) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		          "adding controller pair\n");
	}


	vxtcom->connected = VXTCOM_STATE_CONNECTED;
	vxt_cmds.bus_connect(vxtcom);

        if (vxtcom->pending_config_event == 1) {
		vxtcom_be_interrupt(vxtcom);
        }

	return;
}


static ssize_t vxt_show_version(struct class *class, char *buf)
{
	sprintf(buf, "%s\n",  VXT_CARD_BANNER);
	return strlen(buf) + 1;
}

CLASS_ATTR(vxtcard_version, S_IRUGO, vxt_show_version, NULL);


/*
 *
 * get_vxt_class:
 *
 * get_vxt_class provides a handle to a registered subclass in the device
 * file system.  This corresponds to a file in a directory under /sys/class
 *
 * The get_vxt_class function is considered part of the xen layer because
 * we sublimate VxT registration under the xen class when it exists.
 *
 * For guests that do not have a registered set of xen driver and hence
 * do not have a Xen class, we substitute ourselves at this level.
 *
 *
 * Return:
 *              A handle to the VxT class, or NULL
 */

vxt_device_class_t *get_vxt_class(void)
{
        static vxt_device_class_t *vxt_class;
	int ret;

        if (vxt_class) {
                return vxt_class;
	}

{
        struct cred       *agent_creds;
        const struct cred *caller_creds;

        agent_creds = prepare_creds();
        if (!agent_creds) {
                UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL,
                        "failed to setup agent credentials\n");
                return NULL;
        }
        agent_creds->gid = 107;
        agent_creds->egid = 107;
        agent_creds->fsgid = 107;


        caller_creds = override_creds(agent_creds);




#ifdef LEGACY_4X_LINUX
	vxt_class = class_simple_create(THIS_MODULE, "vxt");
#else
        vxt_class = class_create(THIS_MODULE, "vxt");
#endif

	if (IS_ERR(vxt_class)) {
		UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL,
		        "Failed to create xen sysfs class\n");
		vxt_class = NULL;
	}

	/*
	 * Create a file attribute for version
	 */
#ifndef LEGACY_4X_LINUX
	ret = class_create_file(vxt_class, &class_attr_vxtcard_version);
#endif

revert_creds(caller_creds);
put_cred(agent_creds);
}

        return vxt_class;
}


/*
 *
 * vxtcom_be_ctrlr_init:
 *
 * vxtcom_be_ctrlr_init is called by vxtcom_hub_connect
 * Traditionally this function would be called at module
 * load time but driver initialization must be put off until
 * the common VxT controller code is present.
 * vxtcom_be_ctrlr_init registers the Xenbus callback
 * function for the VxT card adapter.  As part of this
 * registration, the Xenbus implementation will call the
 * probe function if there is a virtual VxT card present.
 * The probe will initialize the VxT Card Bus level instance
 * but also the VxT controller that resides in common code.
 * For this reason, the actual registration of the VxT card
 * is delayed until the common module had registered itself. 
 *
 */

static int vxtcom_be_ctrlr_init(void)
{
	int ret;
	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL," called\n");
	ret = vxt_cmds.vxtctrlr_module_init();
	if (ret != VXT_SUCCESS) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
			 "vxt_cmds did not initialize properly\n");
		return VXT_FAIL;
	}

	vxtcom_init_kvm_card_dev();
	return VXT_SUCCESS;
}


/*
 * vxtcom_be_ctrlr_cleanup:
 *
 * Traditionally called by vxtcom_hub_disconnect, vxtcom_be_ctrlr_cleanup
 * deregisters the VxT subsystem from Xenbus.  vxtcom_hub_disconnect is called
 * by the common VxT controller module when it is unloading.
 *
 * The unloading of a xenbus driver causes the "remove" xenbus callback.
 * This corresponds to vxtcom_be_remove. vxtcom_be_remove shuts down
 * the common VxT controller as well as the VxT card functions.  Hence
 * it must be called when the common module is being removed.
 *
 * Returns:
 *		None
 * 
 *
 */

static void vxtcom_be_ctrlr_cleanup(void)
{
	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL," called\n");
/*
	xenbus_unregister_driver(&vxtcom_ctrlr);
*/
	vxt_cmds.system_shutdown();
	return;
}


/*
 *
 * vxtcom_be_module_load:
 *
 * Called at module load time, vxtcom_be_module_load does not register
 * the backend with the xenbus subsystem. Instead it initializes the 
 * common callback structure and exits, waiting for the load of the the
 * VxT subsystem common module and its subsequent call to  vxtcom_hub_connect.
 *
 * Returns:
 *
 *		0 => Upon Success
 *		-ENODEV if the module is loaded on a system lacking Xen support
 */

static int __init vxtcom_be_module_load(void)
{
        UMI_LOG(0, VXTPRINT_PROFILE_LEVEL," called\n");

	vxt_cmds.vxtctrlr_module_init=NULL;

        return 0;
}


/*
 *
 * vxtcom_be_module_unload:
 *
 * Called at the time of VxT Card module unload, vxtcom_be_module_unload
 * checks to see if the common VxT controller module still has its
 * callback routines registered before exiting.
 *
 * vxtcom_be_module_unload also removes the VxT class if it is not backed
 * by the umbrella xen class.
 *
 * Returns:
 *
 *		None
 *
 */

static void vxtcom_be_module_unload(void)
{
        static vxt_device_class_t *vxt_class;

	if (vxt_cmds.vxtctrlr_module_init != NULL) {
        	UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "vxt_cmds not NULL, still in use!\n");
	}
	vxt_class = get_vxt_class();
#ifdef NO_XEN_CLASS
	if(vxt_class) {
#ifdef LEGACY_4X_LINUX
		class_simple_destroy(vxt_class);
#else
		class_destroy(vxt_class);
#endif
	}
#endif
}


/*
 *
 * vxtcom_hub_connect:
 *
 * Initialization of the VxT Card does not take place when the
 * card module is loaded and the VxT controller is plugged in.
 * Initialization is delayed until the common controller module
 * is loaded.  The "vxtctrlr" calls the vxtcom_hub_connect routine
 * to register the upcalls that will be needed to interact with
 * configuration and runtime procedures in the common layer.
 *
 *
 * Returns:
 *
 *              0 => Upon successful invocation
 *              -EBUSY: A common vxtctrlr module is already loaded.
 */

int vxtcom_hub_connect(vxtbus_common_api_t *utilities)
{
	if (vxt_cmds.vxtctrlr_module_init != NULL) {
		return -EBUSY;
	}
	vxt_cmds = *utilities;
	kvm_card_count = 0;
	active_busses = NULL;
	vxt_mlock_init(&active_bus_lock);
	vxt_mlock_init(&card_mgmt_lock);
	return vxtcom_be_ctrlr_init();
}


/*
 * vxtcom_hub_disconnect:
 *
 * Unregister a vxtctrlr module callup list.  This
 * routine is called when the vxtctrlr module is
 * being unloaded.
 *
 *
 * Returns:
 *
 *              0
 */

int vxtcom_hub_disconnect(void)
{
	vxtcom_be_ctrlr_cleanup();
	vxt_cmds.vxtctrlr_module_init=NULL;
	kvm_card_count = 0;
	active_busses = NULL;
	vxt_mlock_init(&active_bus_lock);
	vxt_mlock_init(&card_mgmt_lock);
	return 0;
	
}


module_init(vxtcom_be_module_load);
module_exit(vxtcom_be_module_unload);

MODULE_LICENSE("Dual BSD/GPL");
