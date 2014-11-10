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
 * TEMPORARY:  define NO_ZAP_CALLBACK for compilation under Fedora
 * set up Makefile for permanent target
 * Remove the following lines when moving to a release
 */
#include <linux/mm.h>
#define NO_ZAP_CALLBACK
#define zap_page_range(vma, address, size, details)  \
	zap_vma_ptes(vma, address, size)
/*
 * vxt_card.c:
 *
 * vxt_card loads the underlying adapter for hypervisor/distrubted
 * system type into the linux operating system.  Examples of supportable
 * bases include Xen, KVM, and communications cards such as IB.
 *
 * vxt_card publishes the interfaces needed abstract the capabilities of
 * the underlying communication medium, allowing the VxT controller and
 * possibly other communication and bus mechanisms to be implemented without
 * regard to the underlying hypervisor or distributed system method.
 *
 * In order to implement much of the function associated with the
 * exported capabilities, vxt_card makes use of a set of registered
 * callbacks.  These callbacks are made available at the time of controller
 * module installation or load.  The definitions for the callbacks can
 * be found in /vxtcom/public/kernel/vxt_module.h  Most of the registered
 * callbacks are used by the hypervisor specific configuration and 
 * runtime code.
 *
 * Note:  In the Linux/Xen version of vxt_card, the mmap function for
 * VxT shared memory has been included.  This is becuase Xen shared page
 * support is not transparent to Linux and Xen hypervisor calls are
 * intertwined in the mapping functions.  Non-linux Xen and non-Xen linux
 * implementations may choose to ignore the mmap abstraction exported by
 * vxt_card and may do their own, if there are no hypervisor entanglements.
 *
 */

/*
 *
 * Exported functions:
 *
 *
 * ****************************** *********************** ****************
 * Controller Related Initialization:
 * ****************************** *********************** ****************
 *
 * Initizlize Controller instance shared memory and signal resources
 *	vxtcard_ctrlr_init
 *
 * Free/Unplug Controller instance
 *	vxtcard_ctrlr_destroy
 *
 * Initialize subsytem level VxT hypervisor elements
 *	vxt_hub_connect
 *
 * De-install subsystem level VxT hypervisor elements
 *	vxt_hub_disconnect
 *	 
 *
 * ****************************** *********************** ****************
 * Shared Memory hypervisor specific functions
 * ****************************** *********************** ****************
 *
 * Allocate and make shared memory available to remote endpoint
 * 	vxt_alloc_shared_memory
 *
 * Free shared memory, pull back from remote endpoint
 * 	vxt_free_shared_memory
 *
 * Create local shared memory and export as a queue
 * 	vxtcard_create_mem_queue
 *
 * Accept shared memory from remote endpoint
 * 	vxtcard_connect_mem_queue
 *
 * Free shared memory queue
 *	vxtcom_destroy_queue
 *
 * Signal change in VxT Bus configuration or device attention
 * shared memory queues
 * 	vxtcom_event_signal
 *
 * ****************************** *********************** ****************
 * Linux related memory functions:  may not be necessary on other platforms
 * ****************************** *********************** ****************
 *
 * Flush page mappings from task space
 * 	vxtcom_flush_device_pager_range
 *
 * Memory Map a VxT Shared memory region, full mmap registration
 * 	vxt_card_mmap
 *
 * Register hypervisor specific device types, (optional)
 *
 * Note:  The following exports are not dependent on vxtctrlr
 *        callbacks and therefore can be called by the authorization
 *        service prior to the installation of the vxtctrlr module.
 *
 * 	vxt_class_device_create
 *	vxt_class_device_destroy
 *
 */


/*
 * The following two defines are for UMI logging and they must
 * be correct or the logged serial number will be invalid and
 * misleading.
 */

#define _VXT_COMPONENT_ 8
#define _VXT_SUBSYSTEM_ 23


#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/kvm_host.h>
#include <linux/poll.h>
#include <linux/eventfd.h>

/*
 * The following files will eventually be in /usr/include/linux
 */

#include <public/kvm/temp/virtio_config.h>
#include <public/kvm/temp/virtio.h>


#include <public/vxt_system.h>
#include <public/kernel/os/vxtcom_embedded_os.h>
#include <public/kvm/vxt_virtio_cfg.h>
#include <public/kernel/bus/hypervisor/vxtcom_bus_ctrlr.h>
#include <public/kernel/vxt_module.h>
#include <vxtcard/linux/kvm/vxt_com_kvm.h>


vxtbus_common_api_t vxtctrlr;


#ifndef KVM_MODULE_PRESENT
#define kvm_release_page_clean(foreign_page)
#define	gfn_to_hva(kvm, gfn) 0
#endif


#ifndef phys_to_pfn
#define phys_to_pfn(p) ((p) >> PAGE_SHIFT)
#endif


/*
 * Needed for KVM gfn_to_pfn substitute
 */
struct page *bad_page;
pfn_t bad_pfn;

/*
 * vxtcard_set_devfile_group:
 *
 * With the introduction of a non-root user of VxT device files it
 * becomes necessary to adjust the group to match the user.  This
 * routine calls an application level program to accomplish the task.
 *
 *	Returns:
 *		None
 */

static void
vxtcard_set_devfile_group(char *filename, char *group)
{
        static char *env[]={
                        "HOME=/",
                        "PATH=/usr/local/bin:/sbin:/usr/sbin:/bin:/usr/bin",
                        "SHELL=/bin/bash",
                        "TERM=linux",
                        NULL
                        };


        char argv[128];
        char *args[4];
        int ret;

        UMI_LOG(150, VXTPRINT_PROFILE_LEVEL,
                "device file name %s, group name %s\n", filename, group);

        args[0] = argv;
	strcpy(args[0], "/etc/vxtcom/bin/vxtdev_setgrp");
	args[1] = argv + strlen(args[0]) + 1;
	strncpy(args[1], group, 40);
	args[2] = args[1] + strlen(args[1]) + 1;
	strcpy(args[2], "/dev/");
	strncpy(args[2]+5, filename, 40);
	args[3] = NULL;


	ret = call_usermodehelper(args[0], args, env, 0);

	if (ret < 0) {
		UMI_LOG(151, VXTPRINT_PRODUCT_LEVEL,
		       "Unable to set the group for device file %s\n",
		        filename);

	}
	

	return;
}


static void
vxtcom_unmap_remote_buffer(struct page **foreign_pages, uint32_t page_cnt);

/*
 *
 * vxt_get_empty_page:
 *
 * Acquire a kernel page and empty it of its
 * physical memory.  This function is to be
 * used to provide a kernel memory page 
 * structure to join with a shared page from
 * another guest.  It is to be used in lieu
 * of xen alloc_empty_pages_and_pagevec support
 *
 * 
 * Returns:
 *	VXT_SUCCESS => a pointer to an empty page
 *	NULL otherwise
 *
 */

static struct page *
vxt_get_empty_page(void)
{
        unsigned int  order = 0;
	unsigned long page;


	UMI_LOG(3, VXTPRINT_PROFILE_LEVEL,
	        " called\n");

        page = __get_free_pages(GFP_KERNEL, order);

	if (page) {
		memset((void *)page, 0, PAGE_SIZE);

		return virt_to_page(page);
	} else {
		return NULL;
	}
}

/*
 * vxtcard_gfn_to_pfn:
 *
 * vxtcard_gfn_to_pfn exists solely to mimic the actions of kvm's
 * gfn_to_pfn for a targeted mm_struct.  KVM uses the current pointer.
 * this presents an impossible situation for VxT configuration routines.
 * On the one hand they are called via an eventfd and therefore with
 * irq's disabled.  On the other, the irq handler is not free to call
 * a helper thread because the "current" context will be lost.
 * This routine will be unnecessary if the KVM routine is changed to
 * rely on the mm structure associated with the kvm structure passed.
 *
 *
 *	Returns:
 *		address of physical frame number upon success
 */

pfn_t
vxtcard_gfn_to_pfn(struct task_struct *task, struct kvm *kvm, gfn_t gfn)
{
	struct page *page[1];
	unsigned long addr;
	int npages;
	pfn_t pfn;
	
	might_sleep(); 

	addr = gfn_to_hva(kvm, gfn);


        down_read(&task->mm->mmap_sem);
        npages = get_user_pages(task, task->mm, addr, 1,
                                        1, 0, page, NULL);
        up_read(&task->mm->mmap_sem);


	if (unlikely(npages != 1)) {
		struct vm_area_struct *vma;

		down_read(&task->mm->mmap_sem);
		vma = find_vma(task->mm, addr);

		if (vma == NULL || addr < vma->vm_start ||
		    !(vma->vm_flags & VM_PFNMAP)) {
			up_read(&task->mm->mmap_sem);
			get_page(bad_page);
			return page_to_pfn(bad_page);
	        }

		pfn = ((addr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
		up_read(&task->mm->mmap_sem);
	} else
		pfn = page_to_pfn(page[0]);

	return pfn;

}




/* ********************* *************************** ******************* */

/*     Exported Functions:  Abstracted transport functionality           */

/* ********************* *************************** ******************* */

/*
 *
 * vxt_class_device_create
 *
 * This routine initializes a class if necessary and registers a 
 * new device to it.  It relies on the device class abstraction
 * exported from the hypervisor bus level code
 *
 * This is a VxT abstraction for linux device class.  It wraps
 * xen class where available to facilitate integration with Xen
 * resource objects.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon successful invocation
 *		VXT_FAIL - cannot initialize class
 *
 */

int
vxt_class_device_create(uint32_t major, uint32_t minor, 
                        char *new_dev_name)
{
	vxt_device_class_t *class;

	UMI_LOG(6, VXTPRINT_PROFILE_LEVEL,
	        " called, major %d, minor %d, device name %s\n",
	        major, minor, new_dev_name);

	if ((class = get_vxt_class()) != NULL) {
		UMI_LOG(7, VXTPRINT_DEBUG_LEVEL,"create_device class\n");
#ifdef LEGACY_4X_LINUX
		class_simple_device_add(class, MKDEV(major, minor),
		                         NULL, new_dev_name);
#else
{
/*  ADD ANOTHER IFDEF HERE THIS IS THE FEDORA VERSION CDY */
/*
struct class_device *cdev;
		cdev = class_device_create(class, NULL,
		                           MKDEV(major, minor), NULL,
		                           new_dev_name);
*/
device_create(class, NULL, MKDEV(major, minor), NULL, new_dev_name);
/* CDY TEST */
UMI_LOG(8, VXTPRINT_DEBUG_LEVEL, "Add ref count to class\n");
/* class_get(class); */

}
#endif
	} else {
		UMI_LOG(9, VXTPRINT_DEBUG_LEVEL,
		        "unable to initialize VxT device class\n");
		return VXT_FAIL;
	}
	return VXT_SUCCESS;
}

EXPORT_SYMBOL(vxt_class_device_create);


/*
 *
 * vxtcom_event_signal:
 *
 * vxtcom_event_signal exports the abstraction of an alert for the VxT bus
 * to the common layer.  In the case of this KVM implementation the 
 * abstraction is implemented as a doorbell and the doorbell is associated
 * with a virtio queue.  There are two doorbells, one for VxT bus configuration
 * actions and one for VxT device level signaling.
 *
 * Returns:
 *
 *		None
 */

void
vxtcom_event_signal(void *sig) {
	vxt_bus_event_signal(sig);
}

EXPORT_SYMBOL(vxtcom_event_signal);


/*
 *
 * vxt_class_device_destroy
 *
 * Remove a vxt_class device of the given major and minor number
 *
 * Returns:
 *
 *		NONE
 *
 */

void
vxt_class_device_destroy(uint32_t major, uint32_t minor)
{

	vxt_device_class_t *class;

	UMI_LOG(10, VXTPRINT_PROFILE_LEVEL,
	        " called, major %d, minor %d\n",
	        major, minor);
	if ((class = get_vxt_class()) != NULL) {
#ifdef LEGACY_4X_LINUX
		class_simple_device_remove(MKDEV(major, minor));
#else
/*  ADD ANOTHER IFDEF HERE THIS IS THE FEDORA VERSION CDY */
/*
		class_device_destroy(class,
		                     MKDEV(major, minor));
*/
device_destroy(class, MKDEV(major, minor));
#endif
	}
	return;
}

EXPORT_SYMBOL(vxt_class_device_destroy);


/*
 *
 * vxt_free_shared_memory:
 *
 * vxt_free_shared_memory abstracts the act of returning
 * hypervisor or transport device shared memory back
 * to the transport agency.  The previously acquired
 * memory is represented as an array of page structures
 * and a length in the form of a page count.
 *
 * THIS IS A NOOP IN KVM: where the memory object is
 * freely sharable between guests.
 *
 * Returns:
 *		None
 *
 *
 */

void
vxt_free_shared_memory(struct page **mem_range, int pg_cnt)
{
	int  i;

	UMI_LOG(11, VXTPRINT_PROFILE_LEVEL,
	        " called, page_array %p, page count %d\n",
	        (void *)mem_range, pg_cnt);
	for (i=0; i<pg_cnt; i++) {
		ClearPageReserved(mem_range[i]);
		free_page((unsigned long)mem_range[i]);
	}
	vxt_kfree(mem_range);
	return;

}

EXPORT_SYMBOL(vxt_free_shared_memory);


/*
 *
 * vxt_alloc_shared_memory:
 *
 * Request a group of pages from the VxT transport agency.  In the
 * case of Xen and other platforms where this code resides in an
 * OS context above the hypervisor, pages from the general RAM pool
 * are obtained and then cleared of their platfrom physical page
 * associations.  In this way the underlying hypervisor can supply
 * the shared physical pages.
 *
 * In the case of the Xen Hypervisor the pagevec mechanism is tapped
 * to provide the goods.
 *
 * In KVM the VxT Bus emulation and controller reside inside of the
 * hypervisor which is a Linux operating system.  With no underlying
 * platform physical page mapping, shadow pages are utilized and this
 * routine is bypassed.  (see remap_pfn_range where physical addresses
 * can be associated with Linux VMA structures).
 *
 *
 * THIS IS A NOOP IN KVM: where the memory object is
 * freely sharable between guests.
 *
 * Returns:
 *		
 *		Upon Success => an array of page structs holding
 *                              physical pages
 *
 *		Falure - NULL
 *
 */

struct page **
vxt_alloc_shared_memory(int pg_cnt)
{
	struct page       **remote_pages;
	int               i,j;
		
	UMI_LOG(12, VXTPRINT_PROFILE_LEVEL,
	        " called, page count %d\n", pg_cnt);
		
	remote_pages =
	   vxt_kmalloc(sizeof(struct page) * pg_cnt,
	               VXT_KMEM_KERNEL);
	if (remote_pages == NULL) {
	        return remote_pages;
	}
	for (i = 0; i < pg_cnt; i++) {
		/*
		 * Get the pages one by one to avoid contiguous
		 * memory issues.
		 */
		remote_pages[i] = vxt_get_empty_page();
		/*
		 * Wire pages for sharing
		 */
		SetPageReserved(remote_pages[i]);

		if (remote_pages[i] == NULL) {
			for(j = 0; j < i; j++) {
				ClearPageReserved(remote_pages[j]);
		        	free_page((unsigned long)remote_pages[j]);
			}
			vxt_kfree(remote_pages);
			return NULL;
		}
	}


	return remote_pages;
}

EXPORT_SYMBOL(vxt_alloc_shared_memory);


/*
 *
 * vxtcom_destroy_queue:
 *
 * vxtcom_destroy_queue abstracts the freeing
 * of a vxt_queue.  The vxt_queue object is
 * allocated at the request of a VxT device and
 * has mapping operations to expose the pages to
 * one or more user spaces.  Implementation details
 * are isolated below this call.
 *
 * The queue is structured differently depending on 
 * whether it holds pages which have been loaned from
 * a remote domain or if this is the domain doing the
 * loaning.  The origin variable tells us which case
 * we are dealing with.
 *
 * vxtcom_destroy queue, frees the buffer pages
 * associated with the targeted queue and also frees
 * any mapping references, foreign access grants
 * or foreign page mappings.  The structures 
 * tracking the shared memory resources are freed
 * including the queue itself.
 *
 * Returns:
 *
 *		VXT_SUCCESS upon successful queue destruction
 */

int
vxtcom_destroy_queue(vxtdev_queues_t *queue, uint32_t origin)
{
	vxtdev_ref_list_t *ref_ptr;

	ref_ptr = queue->refs;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        " called, queue %p, origin %08x,\n",
	        queue, origin);

	while (ref_ptr != NULL) {
		/*
		 * This is a no-op on KVM but keep the logic as a
		 * place of reference between Xen and KVM ports
		 */
		UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
		        "Ending foreign access 0x%llx, ref_ptr %lu\n",
		        ref_ptr->ref, (vxtarch_word)ref_ptr);
		queue->refs = 
		   ((vxtdev_ref_list_t *)queue->refs)->next;
		vxt_kfree(ref_ptr);
		ref_ptr = queue->refs;
	}

	if (origin == 0) {
		/*
		 * These pages were supplied by the remote endpoint
		 * In KVM all pages are known to the base Linux.  However
		 * we have a special mapping here which needs to be released
		 * along with a dropping of the elevated references on the
		 * targeted pages.
                 */

		ASSERT(queue->vm_area != NULL);
		vunmap(queue->qaddr);
		vxtcom_unmap_remote_buffer(queue->vm_area, queue->pg_cnt);
		vfree(queue->vm_area);
		queue->vm_area = NULL;
	} else {

		if (queue->active_mapping == 0) {
			void *addr = queue->qaddr;
			int  i;

			UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
			        "No active mappings, free pages, queue %p\n",
			        queue->qaddr);
		
			for (i=0; i<queue->pg_cnt; i++) {
				ClearPageReserved(virt_to_page(addr));
				addr+=PAGE_SIZE;
			}
			free_pages((unsigned long)queue->qaddr, 
			            get_order(queue->pg_cnt << PAGE_SHIFT));
		}
	}
	vxt_kfree(queue);
	return VXT_SUCCESS;
}

EXPORT_SYMBOL(vxtcom_destroy_queue);


/*
 * 
 * vxtcard_connect_mem_queue:
 *
 * vxtcard_connect_mem_queue maps the shared pages provided into a
 * contiguous range of virtual space.  It returns the address of 
 * the range as a queue.
 *
 * When pages are provided by a remote endpoint.  The method of
 * allocating and mapping these pages may prove different from
 * the corresponding methods of the remote site.  For this reason
 * a separate abstraction has been provided. This abstraction
 * takes the object containing the description of the foreign
 * pages and a handle to the remote endpoint and uses them
 * to locate and map the shared physical pages.
 *
 * Returns:
 *		VXT_SUCCESS
 *		VXT_NOMEM - kmalloc failed or could not alloc
 *		               vm_area
 *		VXT_FAIL  - Could not map shared pages
 *
 */

int
vxtcard_connect_mem_queue(vxtcom_cntrl_page_ref_t *page_ref_array, 
                          int page_cnt, uint64_t rhandle, void *gc,
                          vxtdev_queues_t **qpages)
{
	struct page            **foreign_pages;
	vxtdev_queues_t        *q_pages;
	vxtdev_ref_list_t      *ref_ptr;
	struct kvm             *kvm = (struct kvm *)(vxtarch_word)rhandle;
	struct task_struct     *task = (struct task_struct *)gc;
	int  i;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        "called, qpages %p, pgcnt %08x rhandle 0x%llx\n",
	        page_ref_array, page_cnt, rhandle);


	foreign_pages = vmalloc(page_cnt * sizeof(struct page *));
	if (foreign_pages == NULL) {
		return VXT_NOMEM;
	}
	for (i=0; i<page_cnt; i++) {
		foreign_pages[i] = NULL;
	}
	/*
	 * Allocate structure to track queue resource from within
	 * the controller.
	 */
	q_pages = vxt_kmalloc(sizeof(vxtdev_queues_t), VXT_KMEM_KERNEL);
	if (!q_pages) {
		vfree(foreign_pages);
		return VXT_NOMEM;
	}
	q_pages->refs = NULL;
	q_pages->next = NULL;
	q_pages->vm_area = NULL;
	q_pages->active_mapping = 0;
	q_pages->qaddr = 0;
	q_pages->pg_cnt = page_cnt;



	for (i = 0; i < page_cnt; i++) {
		ref_ptr = vxt_kmalloc(sizeof(vxtdev_ref_list_t),
		                      VXT_KMEM_KERNEL);
		if (!ref_ptr) {
			goto connect_mem_queue_fail;
		}

		/*
		 * Note: the references will again be stored
		 * in reverse order. However, this is what
		 * we want.  It matches the originating
		 * sides queue pages structure layout.
		 */
		ref_ptr->ref = page_ref_array[(page_cnt - 1) - i].page_ref,
		ref_ptr->handle = 0;
		ref_ptr->next = q_pages->refs;
		q_pages->refs = ref_ptr;
		/* 
		 * get the real physical page frame number.
		 * Note: this call to KVM also wires the page
		 */
		foreign_pages[i] = 
		   (pfn_to_page(vxtcard_gfn_to_pfn(task, kvm,
		                                   phys_to_pfn(ref_ptr->ref))));
		UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
		        "foreign_page %p page count 0x%x, index 0x%x, "
		        "from foreign phys ref 0x%llx\n",
		         foreign_pages[i], page_count(foreign_pages[i]),
		         i, ref_ptr->ref);

	}
	/*
	 * Note:  We want to be able to run without PAGE_NO_CACHE
	 * However we may need to introduce it during testing until 
	 * buffer synchronization is worked out.  This is also a good place
	 * to go if debugging is needed on buffer data content integrity.
	 */
	q_pages->qaddr = vmap(foreign_pages, page_cnt, 0, PAGE_KERNEL);
	
	if (q_pages->qaddr == NULL) {
		UMI_ERROR(0, VXTPRINT_PRODUCT_LEVEL,
	        "failed vmap, foreign_pages %p, pgcnt %08x rhandle 0x%llx\n",
	        foreign_pages, page_cnt, rhandle);
		goto connect_mem_queue_fail;
	}

	q_pages->vm_area = foreign_pages;  

	*qpages = q_pages;
	
	return VXT_SUCCESS;


connect_mem_queue_fail:
		for (i=0; i<page_cnt; i++) {
			if (foreign_pages[i] != NULL) {
				kvm_release_page_clean(foreign_pages[i]);
			}
		}
		vfree(foreign_pages);
		while (q_pages->refs != NULL) {
			ref_ptr = (vxtdev_ref_list_t *)q_pages->refs;
			q_pages->refs = ref_ptr->next;
			vxt_kfree(ref_ptr);
		}
		vxt_kfree(q_pages);
		return VXT_FAIL;

}

EXPORT_SYMBOL(vxtcard_connect_mem_queue);


/*
 *
 * vxtcom_event:
 *
 * *************** ************* ************* ************* ************
 * *************** ************* ************* ************* ************
 * NOTE:  This routine is a NOOP for now.  At present we are able
 * to signal via a local action in the common code.  The hypervisor
 * specific action is masked behind a header resident, "inline" function.
 * This is as it should be.  The signaling action and the interrupt
 * actions are most sensitive to performance issues and the extra
 * overhead associated with unnecessary calls goes directly to the
 * bottom line on latency.  If an implementation is ever encountered
 * which will not allow for a local action to signal, this call will
 * be made active and will be invoked from the current "inline" mechanism.
 * *************** ************* ************* ************* ************
 * *************** ************* ************* ************* ************
 *
 *  We signal an event through a hypervisor call "EVTCHNOP_send"
 *  with a structure that passes an event port: evtchn_port_t
 *  
 *  struct evtchn_send {
 *      IN parameters.
 *     evtchn_port_t port;
 *  };
 *
 *  Linux users make use of an event channel service exported through evtchn.c
 *  the call is notify_remote_via_evtchn.  This is turned into a hypervisor
 *  call.  The actual user level action is a file op, evtchn_ioctl with a
 *  cmd field "IOCTL_EVTCHN_NOTIFY"
 *
 *  static inline void notify_remote_via_evtchn(int port)
 *  {
 *     struct evtchn_send send = { .port = port };
 *     (void)HYPERVISOR_event_channel_op(EVTCHNOP_send, &send);
 *  }
 *
 *
 * Linux transmits the event upstream via evtchn_do_upcall after consulting
 * a cpu based hypervisor field.
 *
 * The same device which we use for sending events evtchn.c, allows callers
 * to wait on events:  evtchn_read  another of the evtchn_fops.
 */

/*
irqreturn_t vxtcom_event(int irq, void *dev_id, struct pt_regs *regs)
*/


/*
 *
 * vxtcom_flush_device_pager_range:
 *
 * flush_device_pager_range is a wrapper for the os specific
 * mechanism to flush a range of user address space that has
 * been backed by shared memory pages from this module.  In
 * some implementations, the pager will be a direct mapping
 * and this call will be a direct unmapping of range.  In
 * other implementations as with Linux as shown here there is
 * a full pager mechanism and the call is an indirect invocation
 * of a paging activity.
 *
 * In either approach, flush_device_pager_range is the call
 * to expose to O.S. independent range clearing actions.  
 *
 * Returns:
 *
 *	A restart address is returned 
 *
 *	
 *
 */

unsigned long
vxtcom_flush_device_pager_range(struct vm_area_struct *vma,
                                unsigned long start, 
                                unsigned long range, 
                                void *flush_details,
                                vxt_device_sm_t *sm_record)
{
	struct mm_struct *mm = vma->vm_mm;
	vxtdev_queues_t  *device_queue;
	long             offset;
	int              ret = 0;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        "called, vma %p, flags 0x%lx, start %lu, range %lu\n",
	        vma, vma->vm_flags, start, range);

	down_read(&mm->mmap_sem);
	/*
	 * If we have an active mapping we must rip away
	 * the old pages here.  However, the guest must get
	 * COW treatment.  (as if a device sharing memory
	 * stops responding).  To effect this we allocate
	 * shadow pages, copy queue contents, and make the
	 * pages available to the record structure.
	 * Subsequent paging events will find these pages.
	 *
	 */
	device_queue = (vxtdev_queues_t *)sm_record->queue_token;

	if ((device_queue->active_mapping) &&
	    (sm_record->shadow_queue == NULL)) {
/*
if (0) {
*/
		sm_record->shadow_queue = vxt_kmalloc(range, VXT_KMEM_KERNEL);
		/*
		 * If we are NULL, fall back on basic
		 * execution, i.e. the pages are gone, if
		 * the mapping application accesses the range
		 * it will seg fault.
		 */
		if (sm_record->shadow_queue != NULL) {
			memcpy(sm_record->shadow_queue,
			       device_queue->qaddr, range);
		}
	}

#ifdef LEGACY_4X_LINUX
	zap_page_range(vma, start, range, 
	               (struct zap_details *)flush_details);
#else
	ret =  zap_page_range(vma, start, range, 
	                     (struct zap_details *)flush_details);
#endif
	offset = 0;


/*
if (0) {
*/
	if (sm_record->shadow_queue != NULL) {
		struct page     *page;
		void            *page_object_address;
		unsigned long   address;


	       	page_object_address = sm_record->shadow_queue;
		for (address = vma->vm_start; 
		     address < vma->vm_end; address += PAGE_SIZE) {
			/*
			 * Get the kernel segment virtual address
			 * and find the page structure
			 */
			page = vmalloc_to_page((void *)page_object_address);
			SetPageReserved(page);

			get_page(page);
			UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
			        "ref_cnt 0x%x\n", page_count(page));
#ifdef LEGACY_4X_LINUX
			ret = io_remap_page_range(vma, address,
			                          page_to_pfn(page)<<PAGE_SHIFT,
			                          PAGE_SIZE,
			                          vma->vm_page_prot);
#else
			ret = vm_insert_page(vma, address, page);
#endif
			UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
			        "ret = 0x%x, ref_cnt after 0x%x\n",
			        ret, page_count(page));

			page_object_address += PAGE_SIZE;
		}
	} 

	up_read(&mm->mmap_sem);

	return ret;

}

EXPORT_SYMBOL(vxtcom_flush_device_pager_range);


/*
 * vxt_pte_clear:
 *
 * vxt_pte_clear flushes the page table entry in the Intel
 * style page table mappings of the guest and the underlying
 * Hypervisor physical mappings.  (SPT)
 *
 * Returns:
 *		None
 *
 */

#ifdef NO_MODERN_PTE_CLEAR
static 
inline void vxt_pte_clear(struct mm_struct *mm,
                          unsigned long addr, pte_t *ptep)
{
	ptep->pte_low = 0;
	smp_wmb();
	ptep->pte_high = 0;
}
#else
#define vxt_pte_clear pte_clear

#endif


/*
 *
 * vxtcom_mmap_unmap:
 *
 * Remove guest and shadow page table mappings for a
 * given virtual address.
 *
 * Returns:
 *		0
 *
 */

static int
vxtcom_mmap_unmap(pte_t *ptep, struct page *pmd_page, 
                  unsigned long addr, void *data)
{
        struct mm_struct *mm = (struct mm_struct *)data;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"ptep %p\n", ptep);
        vxt_pte_clear(mm, addr, ptep);
        return 0;
}



/*
 *
 * vxtcom_unmap_remote_buffer:
 *
 * vxtcom_unmap_remote_buffer is called to remove shared page
 * mappings for pages provided by a remote domain.  The
 * mappings are for kernel level access of the pages
 *
 * This call gives back the physical shared pages granted to
 * the guest by the transport agency/hypervisor.
 *
 * Returns:
 *
 *              None
 */

static void
vxtcom_unmap_remote_buffer(struct page **foreign_pages, uint32_t page_cnt)
{
	int i;
	
	for (i=0; i<page_cnt; i++) {
		if (foreign_pages[i] != NULL) {
			kvm_release_page_clean(foreign_pages[i]);
		}
	}
	return;
}


/*
 * vxtcom_clear_pte:
 *
 * vxtcom_clear_pte is a callback for the MMAP "zap" function
 * It clears the  the user guest PTE, updates the shadow page
 * table, and drops the mapping count on the shared pages from
 * the remote end point.
 *
 *
 * Returns:
 *
 *      A copy of the pte information found at the user virtual address
 *
 */

#ifndef NO_ZAP_CALLBACK
static pte_t
vxtcom_clear_pte(struct vm_area_struct *vma,
                 unsigned long uvaddr,
                 pte_t *ptep, int is_fullmm)
{
	vxt_device_sm_t *sm;
	pte_t           copy = *ptep;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        "vma %p, uvaddr %lu, is full 0x%x\n",
	        vma, uvaddr, is_fullmm);
	if (vma == NULL) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,"No vma\n");
		return copy;
	}

	sm = (vxt_device_sm_t *) vma->vm_private_data;

	if (sm == 0) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,"No token\n");
		return copy;
	}

	copy =  ptep_get_and_clear_full(vma->vm_mm,
	                                uvaddr, ptep, is_fullmm);

	return copy;

}
#endif


/*
 *
 * vxtcom_vm_close:
 *
 * This is the pager close operation for the "foreign_ops" instance
 * of the mmap pager callback.  This routine is called when the
 * guest task dies or decides to unmap the device backed region.  In
 * either case the guest page table and the SPT are cleared.  The
 * mapping count for the hypervisor shared page elements are decremented
 * and any shadow pages are removed.  Shadow pages are allocated and
 * mapped if the device/hypervisor shared memory is ripped away while
 * the device region is still mapped.  This avoids bus errors on
 * access to otherwise non-backed regions.
 *
 * Returns:
 *
 *              None
 *
 */

static void
vxtcom_vm_close(struct vm_area_struct *vma)
{
	vxtdev_queues_t       *device_queue;
	void                  *shadow_queue;
	vxt_device_sm_t       *sm;
	vxt_device_sm_t       *verify_sm;
	int                   ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, " called, vma %p\n", vma);
        if (vma == NULL) {
                UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,"No vma\n");
                return;
        }

	sm = (vxt_device_sm_t *) vma->vm_private_data;

	if (sm == 0) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,"No token\n");
		return;
	}

	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,"zap_page range\n");


	shadow_queue = sm->shadow_queue;
	sm->shadow_queue = NULL;

	zap_page_range(vma, vma->vm_start, vma->vm_end - vma->vm_start, NULL);
	sm->vma = NULL;

	ret = vxtctrlr.dev_lookup_token(sm->lookup_token << 12,
	                                &verify_sm);
	if ((ret == VXT_SUCCESS) && (verify_sm == sm)) {
		UMI_LOG(0, VXTPRINT_BETA_LEVEL,
		        "token found, vma %p, queue %p\n", vma, sm->addr);
		device_queue = (vxtdev_queues_t *)sm->queue_token;
		device_queue->active_mapping = 0;
		/*
		 * Drop the reference taken in lookup
		 */
		vxtctrlr.dev_token_dec(sm);
	} else {
		UMI_LOG(0, VXTPRINT_BETA_LEVEL,
		        "no token found, vma %p, queue %p\n", vma, sm->addr);
	}

	if (shadow_queue) {
		void            *page_object_address;
		struct page     *page;
		int              i;
		/*
		 * We had a local back-up for our shared
		 * pages.  Delete the shadow queue
		 */
		UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,"Delete shadow queue\n");

		page_object_address = shadow_queue;

		for (i = 0; i < sm->len; i+=PAGE_SIZE) {
			page = vmalloc_to_page(page_object_address);
				if (page) {
					UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
					"clear rsrvd, page %p, flags "
					"0x%lx, count 0x%x\n",
					page, page->flags, page_count(page));
				/*
				 * reset pages back to clean vmalloc state
				 */
				page->flags = 1 << PG_slab;
				page_object_address += PAGE_SIZE;
				put_page_testzero(page);
			} else {
				UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
				         "missing page, %p\n",
				         page_object_address);
				page_object_address += PAGE_SIZE;
			}
		}

		vxt_kfree(shadow_queue);
	}

	vxtctrlr.dev_token_dec(sm);
	return;
}


/*
 *
 * vxtcom_nopage:
 *
 * This is the pager page_fault operation for the "foreign_ops" instance
 * of the mmap pager callback.  This routine is called when the guest
 * attempts to access a portion of the mmaped device region and does
 * not find a valid mapping there.  If such a call does come through
 * for the foreign_ops backed regions it causes a SIGBUS.  This is
 * because we do a full mapping at the time of the original MMAP and
 * do not expect anything to remove these mappings until a close is done.
 *
 * Returns:
 *
 *              NOPAGE_SIGBUS
 *
 */
/* ifdef for nopage or just put back when not Fedora
static struct page *
vxtcom_nopage(struct vm_area_struct *vma,
              unsigned long address, int *type)
{

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        "vxtcom_nopage called, vma %p, address %lu, type %p\n",
	         vma, address, type);

	return NOPAGE_SIGBUS;
}
*/

static int
vxtcom_nopage(struct vm_area_struct *vma, struct vm_fault *vmf)

{

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        "vxtcom_nopage called, vma %p, address %p\n",
	        vma, vmf->virtual_address);

	return VM_FAULT_SIGBUS;
}





static void vxtcom_mmap_open(struct vm_area_struct *vma);
static void vxtcom_mmap_close(struct vm_area_struct *vma);
#ifndef NO_ZAP_CALLBACK
static pte_t vxtcom_mmap_zap(struct vm_area_struct *vma,
                             unsigned long uvaddr,
                             pte_t *ptep, int is_fullmm);
#endif
/*
static struct page *
vxtcom_mmap_vmmap(struct vm_area_struct *vma, 
                   unsigned long address, int *type);
*/
static int vxtcom_mmap_vmmap(struct vm_area_struct *vma, struct vm_fault *vmf);

static struct vm_operations_struct vxtcom_vm_ops =
{
	.open =		vxtcom_mmap_open,
	.close =	vxtcom_mmap_close,
#ifndef NO_ZAP_CALLBACK
        .zap_pte =      vxtcom_mmap_zap
#endif
.fault =	vxtcom_mmap_vmmap,
/*
	.nopage =	vxtcom_mmap_vmmap,
*/
};

static struct vm_operations_struct vxtcom_foreign_ops =
{
/*
	nopage:   vxtcom_nopage,
*/
fault: vxtcom_nopage,
#ifndef NO_ZAP_CALLBACK
	zap_pte:  vxtcom_clear_pte,
#endif
	close:    vxtcom_vm_close,
};



/*
 *
 * vxtcom_mmap_vmmap:
 *
 * vxtcom_mmap_vmmap is the nopage, device pager routine
 * for vxtcom.
 *
 */

/*
static struct page *
vxtcom_mmap_vmmap(struct vm_area_struct *vma, 
                   unsigned long address, int *type)
*/
static int vxtcom_mmap_vmmap(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	vxt_device_sm_t *sm;
	struct page     *page;
	long            token;
	unsigned long   offset;
	void            *page_object;
	void            *page_object_address;
	void            *address = vmf->virtual_address;
	int		ret;



	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        "called, vma %p, address %p\n",
	        vma, address);
/*
	UMI_LOG(50, VXTPRINT_PROFILE_LEVEL,
	        "called, vma %p, address %lu, type %p\n",
	        vma, address, type);
return NOPAGE_SIGBUS;
*/
return VM_FAULT_SIGBUS;

	if (vma == NULL) {
/*
		return NOPAGE_SIGBUS;
*/
return VM_FAULT_SIGBUS;
        }

        sm = (vxt_device_sm_t *)(vma->vm_private_data);

	token = sm->lookup_token;

	if (token == 0) {
/*
		return NOPAGE_SIGBUS;
*/
return VM_FAULT_SIGBUS;
        }

	ret = vxtctrlr.dev_lookup_token(token << 12, &sm);
	if (ret != VXT_SUCCESS) {
/*
		return NOPAGE_SIGBUS;
*/
return VM_FAULT_SIGBUS;
	}

	page_object =  sm->addr;

	if (page_object == NULL) {
		/*
		 * Drop the reference taken in lookup
		 */
		vxtctrlr.dev_token_dec(sm);
/*
		return NOPAGE_OOM;
*/
return VM_FAULT_SIGBUS;
	}

	/*
	 * Remember, vma->vm_pgoff is a token
	 * we always have a mapping offset into
	 * the buffer of zero
	 */
	offset = ((unsigned long)address) - vma->vm_start;

	if (offset > sm->len) {
		/*
		 * Drop the reference taken in lookup
		 */
		vxtctrlr.dev_token_dec(sm);
/*
		return NOPAGE_SIGBUS;
*/
return VM_FAULT_SIGBUS;
	}
        

        page_object_address = page_object + offset;

	/*
	 * Get the kernel segment virtual address
	 * and find the page structure
	 */
	page = vmalloc_to_page(page_object_address);
	get_page(page); /* increment page count */
/*
	if (type) {
		*type = VM_FAULT_MINOR;
	}
*/

	/*
	 * Drop the reference taken in lookup
	 */
	vxtctrlr.dev_token_dec(sm);

	/*
	 * Return the page pointer
	 */
/*
	return (page);
*/
vmf->page = page;
return 0;

}


/*
 *
 * vxtcom_mmap_open:
 *
 * vxtcom_mmap_open grabs a file reference.  This
 * call ensures that the this module will not be
 * unloaded while a user has associated shared
 * memory mapped.
 *
 * vxtcom_mmap_open is the "open" function of the
 * supplier side of the vxtcom shared memory mmap
 * ops.  (vxtcom_vm_ops)
 *
 * Returns:
 *
 *		NONE
 *
 */

static void 
vxtcom_mmap_open(struct vm_area_struct *vma)
{ 
	/*
	 * Hold file open while there is an
	 * outstanding memory mapping
	 */
	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL, "called vma %p\n", vma);
	return;
}


#ifndef NO_ZAP_CALLBACK
static pte_t
vxtcom_mmap_zap(struct vm_area_struct *vma,
                unsigned long uvaddr,
                pte_t *ptep, int is_fullmm)
{
	pte_t           copy = *ptep;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,"uvaddr %p\n", (void *)uvaddr);
	if (vma == NULL) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,"No vma\n");
		return copy;
        }


        vxt_pte_clear(vma->vm_mm, uvaddr, ptep);



	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,"finished\n");
	return copy;

	
}
#endif



/*
 *
 * vxtcom_mmap_close:
 *
 * vxtcom_mmap_drops grabs the module reference.
 * count.  This call is the complement to 
 * vxtcom_mmap_open and is called when the
 * munmap function is invoked against the
 * appropriate vm_area_struct.
 *
 * vxtcom_mmap_close is the "close" function of the
 * supplier side of the vxtcom shared memory mmap
 * ops.  (vxtcom_vm_ops)
 *
 *
 * Returns:
 *
 *		NONE
 *
 */

static void 
vxtcom_mmap_close(struct vm_area_struct *vma)
{
	unsigned long         addr;
	vxtdev_queues_t       *device_queue;
	vxt_device_sm_t       *sm;
	vxtarch_word          token;
	int                   ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL," called\n");
	if (vma == NULL) {
		return;
        }

        sm = (vxt_device_sm_t *)(vma->vm_private_data);
#ifdef LEGACY_4X_LINUX
{
	void            *page_object_address;
	unsigned long   address;
	struct page     *page;

	page_object_address =  (void *)(unsigned long)sm->addr;

	/*
	 * Drop the page count but do not allow automatic page
	 * reclamation this happens in free_pages below
	 */
	if (page_object_address != 0) {
		for (address = vma->vm_start; 
		     address < vma->vm_end; address += PAGE_SIZE) {

			/*
			 * Get the kernel segment virtual address
			 * and find the page structure
			 */
			page = vmalloc_to_page((void *)
			                       page_object_address);
			atomic_add_negative(-1, &(page)->_count);
			UMI_LOG(0, VXTPRINT_BETA_LEVEL,
		                "state of page to be freed pcount %d, "
			        "pmappings %d, flags 0x%x\n",
			        (int)page_count(page),
			        (int)page->mapping, (int)page->flags);

			page_object_address += PAGE_SIZE;
		}
	}
}
#endif
        token = sm->lookup_token;
	ret = vxtctrlr.dev_lookup_token(token << 12, &sm);
	if (ret != VXT_SUCCESS) {
		/*
		 * The shared memory object has already been removed
		 * we are a laggard, holding on to the page resource and
		 * so are responsible for cleaning it up.
		 */
		int i;
		void *addr = sm->addr;
		UMI_LOG(0, VXTPRINT_BETA_LEVEL,
		        "no token found, vma %p, queue %p\n", vma, sm->addr);
		for (i=0; i<(sm->len >> PAGE_SHIFT); i++) {
			ClearPageReserved(virt_to_page(addr));
			addr+=PAGE_SIZE;
		}
		free_pages((unsigned long)sm->addr, get_order(sm->len));
		vxtctrlr.dev_token_dec(sm);
		return;
	}
	/*
	 * Drop the reference taken in lookup
	 */
	vxtctrlr.dev_token_dec(sm);
	device_queue = (vxtdev_queues_t *)sm->queue_token;
	device_queue->active_mapping = 0;
	vxtctrlr.dev_token_dec(sm);
return;

	if (vma == NULL) {
		return;
        }


	for (addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
		apply_to_page_range(vma->vm_mm, addr, PAGE_SIZE, 
		                    vxtcom_mmap_unmap, vma->vm_mm);
	}

	/*
	 * VMA mapping dropped by caller
	 * drop file use count
	 */
	return;
}


/*
 * vxtcard_create_mem_queue:
 *
 * vxtcard_create_mem_queue allocates only a single queue
 * of specified length.
 *
 * vxtcard_create_mem_queue is called from the vxt_controller
 * module when the code is initializing its shared memory
 * queues.  The controller does not keep track of the
 * size, number or disposition of queues, though it
 * does track the outstanding queue and mapping
 * allocations for extraordinary device clean-up
 * needs and the tracking of general clean-up information
 * which is particular to implementation.  i.e. page
 * share references from the grant table.
 *
 * Returns:
 *     VXT_SUCCESS
 *     VXT_NOMEM - Cannot allocate required memory
 *     VXT_PARM - Incoming device or controller invalid
 *     VXT_RSRC - Cannot allocate grant-table reference
 */

int
vxtcard_create_mem_queue(uint64_t rhandle, int page_cnt,
                         vxtdev_queues_t **qpages, void **queue_ptr)

{
	void              *new_queue;
	char              *page_ptr;
	uint64_t	  frame;
	vxtdev_queues_t   *q_pages;
	vxtdev_ref_list_t *ref_ptr;
	int               i;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        " called, remote endpoint 0x%llx, page_cnt 0x%x,\n",
	        rhandle, page_cnt);

	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
	        "******* GET FREE PAGES ***********\n");
	new_queue = (void *)
	            __get_free_pages(GFP_KERNEL,
	            get_order(page_cnt << PAGE_SHIFT));
	if (!new_queue) {
		UMI_WARN(80, VXTPRINT_PRODUCT_LEVEL,
			 "* GET FREE PAGES FAILED *\n");
		return VXT_NOMEM;
	}  
	memset(new_queue, 0, page_cnt << PAGE_SHIFT);
   
	if (((vxtarch_word)new_queue) & (PAGE_SIZE - 1)) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "* GET FREE PAGES ALIGNMENT FAILED *\n");
		free_pages((unsigned long)new_queue, 
		            get_order(page_cnt << PAGE_SHIFT));
		return VXT_ALIGN;
	}

	/*
	 * Allocate structure to track queue resource from within
	 * the controller.
	 */
	q_pages = vxt_kmalloc(sizeof(vxtdev_queues_t), VXT_KMEM_KERNEL);
	if (!q_pages) {
		free_pages((unsigned long)new_queue, 
		           get_order(page_cnt << PAGE_SHIFT));
		return VXT_NOMEM;
	}
	q_pages->refs = NULL;
	q_pages->next = NULL;
	q_pages->vm_area = NULL;
	q_pages->active_mapping = 0;
	q_pages->pg_cnt = page_cnt;

   
	page_ptr = (char *)new_queue;

	/*
	 * Create ref_ptr elements and fill in their
	 * ref fields with the guest physical addresses
	 * of the shared pages associated with the queues.
	 */
        for (i = 0; i < page_cnt; i++) {
		struct page *pg;
		pg = virt_to_page(page_ptr);
                frame = (uint64_t)page_to_phys(pg);

                /*
                 * Allocate an element to track the shared
                 * status of the page
                 */
                ref_ptr = vxt_kmalloc(sizeof(vxtdev_ref_list_t),
                                      VXT_KMEM_KERNEL);
                if (!ref_ptr) {
                        goto addqueue_fail;
                }
		SetPageReserved(pg);

                /*
                 * Note:  Our references will be traversed
                 * from last to first, i.e. from the highest
                 * address page to the lowest.
                 */
                ref_ptr->ref = frame;
                ref_ptr->handle = 0;
                ref_ptr->next = q_pages->refs;
                q_pages->refs = ref_ptr;

                UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
                        "******* ADDR 0x%p ******FRAME %llx********\n",
                        page_ptr, (uint64_t)frame);
                page_ptr += PAGE_SIZE;
        }

	*qpages = q_pages;
	*queue_ptr = new_queue;

	UMI_LOG(0, VXTPRINT_DEBUG_LEVEL," returning - queue %p, token %p\n",
	        new_queue, q_pages);


	return VXT_SUCCESS;

addqueue_fail:
	{
		vxtdev_ref_list_t *ref_ptr;

		UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL,
		        " Unable to allocate resources, aborting\n");
		/*
		 * revoke foreign access on the pages
		 * shared so far.
		 */
		ref_ptr = q_pages->refs;
		while (ref_ptr != NULL) {
			ClearPageReserved(virt_to_page(ref_ptr->ref));
			q_pages->refs =
			   ((vxtdev_ref_list_t *)(q_pages->refs))->next;
			vxt_kfree(ref_ptr);
                        ref_ptr = (vxtdev_ref_list_t *)(q_pages->refs);
                }

		free_pages((unsigned long)new_queue, 
		           get_order(page_cnt << PAGE_SHIFT));
		vxt_kfree(q_pages);

		return VXT_RSRC;
	}
}

EXPORT_SYMBOL(vxtcard_create_mem_queue);


/*
 *
 * vxtcard_ctrlr_init:  
 *
 * vxt_card_ctrlr_init is called when the vxt_ctrlr module initializes
 * a new instance of a VxT controller.  Any resources needed to handle
 * the VxT card resources or tie in hypervisor specific aspects of the vxt
 * device instance are handled here.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon Successful Invocation
 *		VXT_RSRC - Unable to allocate grant handles
 *
 */

int
vxtcard_ctrlr_init(vxtbus_ctrlr_t *bus, void *fops_handle,
                   uint32_t first_controller, int ctrlr_slot_number,
                   int vdevice)
{
	vxt_device_class_t     *class;
	struct file_operations *vxt_ctrlr_fops = (struct file_operations *)
	                                         fops_handle;
	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        " called, bus %p, fops %p, first %08x"
		" controller slot 0x%x, vdevice %d\n",
	        bus, vxt_ctrlr_fops, first_controller,
	        ctrlr_slot_number, vdevice);
	if (first_controller) {
		/* NO-OP no resources needed at present that span controllers */
	}


	if ((class = get_vxt_class()) != NULL) {
		UMI_LOG(63, VXTPRINT_DEBUG_LEVEL,"create_device class\n");
#ifdef LEGACY_4X_LINUX
		class_simple_device_add(class, MKDEV(vdevice, 0),
		                        NULL, "vxtctrlr%d", ctrlr_slot_number);
#else
/*  ADD ANOTHER IFDEF HERE THIS IS THE FEDORA VERSION CDY */
/*
		class_device_create(class, NULL,
		                    MKDEV(vdevice, 0), NULL,
		                    "vxtctrlr%d", ctrlr_slot_number);
*/
device_create(class, NULL, MKDEV(vdevice, 0),
              NULL, "vxtctrlr%d", ctrlr_slot_number);
#endif
	}

	/*
	 * For KVM we store the controller instance name.  This allows
	 * us to pass it back on the VxTCard Plug ioctl.  The KVM client
	 * is free to open the VxTBus device and do bus queries but this
	 * is not necessary when the controller name is passed back.
	 */

	sprintf(bus->ctrlr_name, "vxtctrlr%d", ctrlr_slot_number);

	/*
	 * Linux makes it difficult to set the group on a device file from
	 * inside the kernel.  For now call user space to get the job done.
	 */

	vxtcard_set_devfile_group(bus->ctrlr_name, "qemu");

	return 0;
}

EXPORT_SYMBOL(vxtcard_ctrlr_init);


/*
 *
 * vxtcard_ctrlr_destroy:
 *
 * vxtcard_ctrlr_destroy is called when a controller instances
 * is being unplugged.  Any resources associated with hypervisor/
 * vxt_card shared memory, signaling or abstract device elements
 * are removed.
 *
 * Returns:
 *
 *		NONE
 *
 */

void
vxtcard_ctrlr_destroy(int vdev, uint32_t last_controller)
{
        vxt_device_class_t *class;

        UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
                " called, virtual device 0x%x\n",
                vdev);
        if ((class = get_vxt_class()) != NULL) {
#ifdef LEGACY_4X_LINUX
                class_simple_device_remove(MKDEV(vdev, 0));
#else
/*  ADD ANOTHER IFDEF HERE THIS IS THE FEDORA VERSION CDY */
/*
                class_device_destroy(class, MKDEV(vdev, 0));
*/
                device_destroy(class, MKDEV(vdev, 0));
#endif
        }

        if (last_controller) {
		/* no extra controller resources at present */
        }

        return;
}

EXPORT_SYMBOL(vxtcard_ctrlr_destroy);


/*
 *
 * vxt_hub_connect:
 *
 * vxt_hub_connect abstracts the process of the vxt_controller connecting
 * to its bus.
 */

int vxt_hub_connect(vxtbus_common_api_t *utilities)
{
	vxtctrlr = *utilities;

	bad_page = alloc_page(GFP_KERNEL | __GFP_ZERO);

	if (bad_page == NULL) {
		return VXT_RSRC;
	}

	bad_pfn = page_to_pfn(bad_page);

	return vxtcom_hub_connect(utilities);
}

EXPORT_SYMBOL(vxt_hub_connect);


/*
 *
 * vxt_hub_disconnect:
 *
 * vxt_hub_disconnect abstracts the process of the vxt_controller unplugging
 * from its bus.
 *
 * Returns:
 *		None
 */

void vxt_hub_disconnect(void)
{
	__free_page(bad_page);
	vxtcom_hub_disconnect();
	return;
}
EXPORT_SYMBOL(vxt_hub_disconnect);


/*
 *
 * vxtcom_hub_mmap: Foreign/page acceptor side of user task
 *                  space mmap.
 *
 * vxtcom_hub_mmap for KVM inserts pages from the page list provided
 * into the vm_struct provided.  The permissions are those found
 * in the vm_struct.  The page list can be of any valid pages
 * associated with the system but are expected to be a list
 * of pages comprising a shared buffer, linked with a remote
 * endpoint.
 *
 * Returns:
 *
 *		0 => Success
 *		-ENXIO - vm_insert_page failure
 *			
 *
 */

static int
vxtcom_hub_mmap(struct page **sm_pages, uint32_t page_cnt,
                struct vm_area_struct *vma)
{
	int i;
	int ret;
	
	for (i=0; i<page_cnt; i++) {
/*
		ret = vm_insert_page(vma, vma->vm_start + (i * PAGE_SIZE),
		      sm_pages[i]);
*/
ret = vm_insert_pfn(vma, vma->vm_start + (i * PAGE_SIZE),
                    page_to_pfn(sm_pages[i]));
		if (ret) {
			UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
			         "vm_insert_page failed, ret = 0x%x "
			         "vma_start 0x%lx, vma_end 0x%lx, "
			         "vma_prot 0x%lx\n", ret,
			         vma->vm_start, vma->vm_end,
			         (vxtarch_word)vma->vm_page_prot.pgprot);
			zap_page_range(vma, vma->vm_start,
			               vma->vm_end - vma->vm_start, NULL);
		}
	}
	return 0;

}


/*
 * vxt_card_mmap:
 *
 * vxt_card_mmap registers a device pager facility for
 * this module.  The device pager vends memory provided
 * via either shared memory or reflective shared memory
 * mechanisms exported by the vxt hub. 
 *
 * Hub memory can be memory made availabe across guest
 * machine boundaries, (shared memory or pseudo device
 * memory), or device memory associated with hardware
 * backed transport queues. This memory is requested
 * via the standard paging mechanism after being advertised
 * and bound via the mmap call.
 *
 * Return:
 *
 *		0 => SUCCESS
 *		-ENXIO  - 
 *			No registered handler for vma
 *			length does not match backing object
 *
 *
 */

int
vxt_card_mmap(struct file *file, struct vm_area_struct *vma, uint32_t page_origin, uint32_t remote_ep)
{
   
	vxt_device_sm_t       *sm;
	vxtdev_queues_t       *device_queue;
	struct page           *page;
	unsigned long         page_object_address;
	unsigned long         address;
	vxtarch_word          token;
	int                   ret;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        "called filep %p, vma %p\n",
	        file->private_data, vma);

	/*
	 * Treat the offset as a shared memory
	 * token.  The vxt device associated
	 * with the caller will have registered
	 * it and the caller will have retrieved
	 * the handle in a query prior to this
	 * call.
	 *
	 * Linux 2.6 and later page-shifts the
	 * offset.  No action is required here
	 * to adjust for this, however it is
	 * vital that the caller  use the
	 * mmap2 function, this takes the
	 * offset as a page number instead
	 * of silently shifting it.
	 * 
	 */

        token = (vxtarch_word)(vma->vm_pgoff);
	ret = vxtctrlr.dev_lookup_token(token << 12, &sm);
	if (ret != VXT_SUCCESS) {
		UMI_WARN(0, VXTPRINT_PRODUCT_LEVEL,
		         "no token found, vma %p\n", vma);
		return -ENXIO;
	}

	if (sm->len  != (vma->vm_end - vma->vm_start)) {
		/*
		 * It would be rather pointless to map
		 * only a portion of a shared buffer queue
		 */
		UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
		        "range invalid vma start 0x%lx, vma end 0x%lx, "
		        "sm_range 0x%x\n",
		        vma->vm_start, vma->vm_end, sm->len);
		/*
		 * Drop the reference taken in lookup
		 */
		vxtctrlr.dev_token_dec(sm);
		return -ENXIO;
        }

	/*
	 * Check to see if the pages are foreign,
	 * if they are, we must take the physical
	 * pages we have been granted and map them
	 * into a virtual range we can use.
	 *
	 */

        if (page_origin == 0) {
		int               pg_cnt;

		pg_cnt = sm->len >> PAGE_SHIFT;
		UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
                        "vxtcom_mmap, foreign called: pg_cnt = 0x%x, "
		        "vma flags 0x%lx\n", pg_cnt, vma->vm_flags);

		/*
		 * Go find the queue resources to
		 * access the page refs.
		 */
		ret = vxtctrlr.devreq_find_qpages(sm->dev_handle,
		                                  sm->addr, &device_queue);
		if (ret != VXT_SUCCESS) {
			/*
			 * Drop the reference taken in lookup
			 */
			vxtctrlr.dev_token_dec(sm);
			return VXT_PARM;
                }
                UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
                        "return from find_qpages, device_queue %p\n",
		        device_queue);

		/*
		 * Fill in the remote pages with the page "refs"  In the
		 * case of KVM the "refs" are actual guest physical
		 * page addresses.
		 */

		vma->vm_flags |= (VM_IO | VM_RESERVED 
		                  | VM_PFNMAP | VM_DONTEXPAND);

		vxtcom_hub_mmap((struct page **)device_queue->vm_area,
		                pg_cnt, vma);

		sm->foreign_map.map = NULL;  /* Xen thing only */
		/* 
		 * We are KVM and we do not need the foreign_map for
		 * the intrusive Xen interference in Linux VM operations
		 * but we need the sm structure.  (foreign_map is the first
		 * field)
		 */
		vma->vm_private_data = &sm->foreign_map;
		sm->vma = vma;
		sm->remote_pages = NULL;
		sm->user_handles = NULL;
		sm->lookup_token = token;

		device_queue->active_mapping = 1;
		vma->vm_ops = &vxtcom_foreign_ops;

		return 0;

	} else {

		/*
		 *
	 	 * Local Case
		 *
		 * Disable paging
		 *
	 	 * Don't copy will keep the pages shared on a
		 * task fork.
		 */

		vma->vm_flags |=  VM_RESERVED | VM_DONTCOPY 
		                  | VM_PFNMAP | VM_SHARED;
	
		vma->vm_ops = &vxtcom_vm_ops;
		/*
		 * Increase the sm reference count so we can
		 * always find the queue addr.  When we
		 * outlast the parent device, we will need
		 * to free the queue pages.
		 */
		sm->lookup_token = token;

		vma->vm_private_data = (void *)sm;

		page_object_address =  (unsigned long)sm->addr;

		if (page_object_address == 0) {
			return -ENXIO;
		}

		/*
		 * We cannot rip the pages away when a device goes down
		 * unexpectedly, hence we must share responsibility
		 * for removing queue pages if the device needs to
		 * leave before the user level mapping.  Set the
		 * active mapped bit in the associated queue.  Note
		 * we rely on the fact that the queue exists as long
		 * as token lookup works.  i.e. we remove the token
		 * from the lookup table prior to device removal.
		 */
		device_queue = (vxtdev_queues_t *)sm->queue_token;
		UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
		        "parent queue %p\n", device_queue);
		device_queue->active_mapping = 1;

		for (address = vma->vm_start; 
		     address < vma->vm_end; address += PAGE_SIZE) {

			/*
			 * Get the kernel segment virtual address
			 * and find the page structure
			 */
			page = virt_to_page(page_object_address);

			UMI_LOG(0, VXTPRINT_DEBUG_LEVEL,
			        "mapping physical address 0x%lx to 0x%lx\n",
			        page_to_pfn(page), address);
			remap_pfn_range(vma, address, page_to_pfn(page),
			                PAGE_SIZE, vma->vm_page_prot);

			page_object_address += PAGE_SIZE;
		}

	}

        vxtcom_mmap_open(vma);
	
	return 0;

}

EXPORT_SYMBOL(vxt_card_mmap);

/*
 * 
 * vxt_is_mangement_hub
 *
 * Calls a routine in the context specific host or guest file
 * The response indicates that this module is loaded either in the
 * trusted computing base where it has access to management services
 * or is part of a guest operating system environment.
 *
 * Returns:
 *		0 - Code is resident in the trusted computing base
 *		1 - Code is resident in a guest operating system context
 */

int
vxt_is_management_hub(void)
{
	return vxt_bus_management_hub();
}

EXPORT_SYMBOL(vxt_is_management_hub);


