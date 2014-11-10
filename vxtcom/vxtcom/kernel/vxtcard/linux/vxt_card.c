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


#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/device.h>
#include <xen/interface/io/blkif.h>
#include <xen/balloon.h>
#include <public/vxt_system.h>
#include <public/kernel/os/vxtcom_embedded_os.h>
#include <public/kernel/bus/hypervisor/vxtcom_bus_ctrlr.h>
#include <public/kernel/vxt_module.h>
#include <vxtcard/linux/xen/vxt_com_xen.h>

grant_ref_t gref_head;
#define MAX_TARGET 1000  /* number of available grant references */

vxtbus_common_api_t vxtctrlr;



static void
vxtcom_unmap_remote_buffer(struct vm_struct *vm_area, 
                           vxtdev_ref_list_t *handles,
                           gnttab_map_grant_ref_t *alt_handles,
                           int page_cnt);

static int
vxtcom_unmap_user_queue(struct vm_area_struct *vma,
                        gnttab_map_grant_ref_t *mparms,
                        uint32_t end, uint32_t offset, uint64_t user_ptep);


/*
 * This routine should not be needed, it
 * is probably missing because of an anomaly in 
 * the compilation of the kernel modules we are
 * depending on. It is not necessary in later Xen/Linux compilations
 */
#ifdef V2_6_18__53_1_13_el5
static void xen_l1_entry_update(pte_t *ptr, pte_t val)
{
        mmu_update_t u;
        u.ptr = virt_to_machine(ptr);
        u.val = pte_val_ma(val);
	UMI_LOG(2, VXTPRINT_PROFILE_LEVEL,
	        "ptep %p, value %lx%lx\n", ptr, val.pte_low, val.pte_high);
        ASSERT(HYPERVISOR_mmu_update(&u, 1, NULL, DOMID_SELF) >= 0);
}
#endif


#ifdef LEGACY_4X_LINUX
void xen_invlpg(unsigned long ptr)
{
	struct mmuext_op op;
	op.cmd = MMUEXT_INVLPG_LOCAL;
	op.arg1.linear_addr = ptr & PAGE_MASK;
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}

/*
 *
 * local_xen_machphys_update:
 *
 * The export symbol is missing from 4.7 kernel build
 */

static void local_xen_machphys_update(unsigned long mfn, unsigned long pfn)
{
	mmu_update_t u;
	if (xen_feature(XENFEAT_auto_translated_physmap)) {
		BUG_ON(pfn != mfn);
		return;
	}
	u.ptr = ((unsigned long long)mfn << PAGE_SHIFT) | MMU_MACHPHYS_UPDATE;
	u.val = pfn;
	BUG_ON(HYPERVISOR_mmu_update(&u, 1, NULL, DOMID_SELF) < 0);
}




#include <xen/interface/memory.h>
/*
 *
 * This is a helper function for vxt_restore_page
 * It is called via the apply_to_page_range method
 * in order to get access to the locked memory mapping
 * structures.
 *
 * Returns:
 *		0 Upon success
 */

static int vxt_clear_pte(
        pte_t *pte, struct page *pmd_page, unsigned long addr, void *data)
{
        unsigned long mfn = pte_mfn(*pte);
	__guest_handle_xen_pfn_t pfn_handle;
        struct xen_memory_reservation mem_ref;
        int ret;

	pfn_handle.p = (unsigned long *)mfn;
        mem_ref.extent_start = pfn_handle;
        mem_ref.nr_extents = 1;
        mem_ref.extent_order = 0;
        mem_ref.domid = DOMID_SELF;

        set_pte_at(&init_mm, addr, pte, __pte_ma(0));
        set_phys_to_machine(__pa(addr) >> PAGE_SHIFT, INVALID_P2M_ENTRY);
        ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation, &mem_ref);
        BUG_ON(ret != 1);
        return 0;
}


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
        int ret;


	UMI_LOG(3, VXTPRINT_PROFILE_LEVEL,
	        " called\n");

        page = __get_free_pages(GFP_KERNEL, order);

	if (page) {
        	scrub_pages(page, 1 << order);

        	ret = apply_to_page_range(&init_mm, page,
		                          PAGE_SIZE << order,
		                          vxt_clear_pte, NULL);
		BUG_ON(ret);
		return virt_to_page(page);
	} else {
		return NULL;
	}
}


#include <linux/blkdev.h>
/*
 *
 * vxt_restore_page:
 *
 * After the caller has emptied a page structure of 
 * a shared physical page from a remote guest,
 * vxt_restore_page is called to repopulate the
 * page structure with a non-shared locally held
 * physical page.
 *
 * Returns:
 *		0
 *
 */

static int
vxt_restore_page(struct page *page)
{
	__guest_handle_xen_pfn_t      pfn_handle;
	struct xen_memory_reservation mem_ref;
	unsigned long                 pfn;
	long                          rc;
	int                           ret;


	UMI_LOG(4, VXTPRINT_PROFILE_LEVEL,
	        " called, page_struct %p\n", page);
	pfn_handle.p = (xen_pfn_t *)page_to_pfn(page);

	mem_ref.extent_start = pfn_handle;
	mem_ref.nr_extents   = 1;
	mem_ref.extent_order = 0;
	mem_ref.domid = DOMID_SELF;
	rc = HYPERVISOR_memory_op(
		XENMEM_populate_physmap, &mem_ref);
	if (rc != 1) {
		UMI_LOG(5, VXTPRINT_PRODUCT_LEVEL,
		        "Unable to recover physical page, leaking page\n");
		return 0;
	}

	pfn = page_to_pfn(page);
	BUG_ON(phys_to_machine_mapping_valid(pfn));

	/* 
	 * Update the machine to phys and phys to machine tables.
	 */
	set_phys_to_machine(pfn, (unsigned long)pfn_handle.p);
	local_xen_machphys_update((unsigned long)pfn_handle.p, pfn);
            
	if (pfn < blk_max_low_pfn) {
		ret = HYPERVISOR_update_va_mapping(
			(unsigned long)__va(pfn << PAGE_SHIFT),
			pfn_pte_ma((unsigned long)pfn_handle.p, PAGE_KERNEL),
			0);
		BUG_ON(ret);
	}

	ClearPageReserved(page);
	set_page_count(page, 1);
	__free_page(page);

	return 0;
}

#endif

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
struct class_device *cdev;
		cdev = class_device_create(class, NULL,
		                           MKDEV(major, minor), NULL,
		                           new_dev_name);
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
		class_device_destroy(class,
		                     MKDEV(major, minor));
#endif
	}
	return;
}

EXPORT_SYMBOL(vxt_class_device_destroy);


/*
 * 
 * vxt_vm_area_addr:
 *
 * vxt_vm_area_addr dereferences and retuns the "addr" field
 * of the vma struct.  This field corresponds to the contiguous
 * kernel virtual address space range backing the vma.  This
 * region supplies the physical pages that are mapped into the
 * address space the vma is associated with.
 *
 * Returns:
 *		=> Target VMA addr field contents
 *
 */

static void *
vxt_vm_area_addr(void *region)
{
	struct vm_struct *vma;

	vma = (struct vm_struct *)region;
	return vma->addr;
}


/*
 *
 * vxt_nop_func:
 *
 * vxt_nop_func is an empty function.  It is needed by apply_to_page
 * range below.  While we have no specific function that we need to
 * apply, we need the side effect, namely the setup of the page tables
 * and page mappings that ensue from the range traversal.
 *
 * Returns:
 *
 *		None
 *
 */

static int
vxt_nop_func(pte_t *pte, struct page *pmd_page, unsigned long addr, void *data)
{
        return 0;
}


/*
 *
 * vxt_alloc_vm_area:
 *
 * vxt_alloc_vm_area is a wrapper for the standard
 * vm_alloc_area because in some circumstances
 * the standard call does not populate the region
 * returned with page table entries.  Xen calls
 * which rely on the vm_struct managed areas
 * cannot operate properly unless the associated
 * address space is backed by mapping PTEs.
 *
 * vxt_alloc_vm_area is also an os independent
 * wrapper, the call is used to sequestor a 
 * range of physical/virtual space in a guest
 * to serve as a repository for foreign pages.
 *
 * Return:
 *
 *		A pointer to the region serving
 * 		as repository for foreign pages
 *		is returned if a successful 
 *		allocation has occured, Null
 *		is returned otherwise.
 *
 */

static void *
vxt_alloc_vm_area(unsigned long size)
{
	int ret;
	struct vm_struct *vma;

	vma = alloc_vm_area(size);

	ret = apply_to_page_range(&init_mm, (unsigned long)vma->addr, size,
	                          vxt_nop_func, NULL);

	return (void *)vma;
}


/*
 *
 * vxt_free_vm_area:
 *
 * vxt_free_vm_area is a wrapper for the free_vm_area function 
 * providing an OS independent interface to manipulate a
 * region of space acting as a container for foreign pages.
 *
 * Returns:
 *		None
 *
 */

static void
vxt_free_vm_area(void *address_region)
{
	free_vm_area((struct vm_struct *)address_region);
}


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
 * Returns:
 *		None
 *
 *
 */

void
vxt_free_shared_memory(struct page **mem_range, int pg_cnt)
{
#ifdef LEGACY_4X_LINUX
{
	int i;

	UMI_LOG(11, VXTPRINT_PROFILE_LEVEL,
	        " called, page_array %p, page count %d\n",
	        (void *)mem_range, pg_cnt);
	/*
	 * We pushed a structure to map the individual pages
	 * In this case our underlying implementation treated
	 * the entire range as one page.  i.e. a contiguous
	 * region.  To avoid the obvious issues with this we
	 * asked for regions of 1 page.  Hence, we remove them
	 * in similar fashion.
	 */
	
	for (i = 0; i < pg_cnt; i++) {
		vxt_restore_page(mem_range[i]);
	}
	vxt_kfree(mem_range);
}
#else
	/*
	 * The underlying implementation is expecting a struture which
	 * holds page structures.  We simply let the called routine
	 * handle all the necessaries.
	 */
	free_empty_pages_and_pagevec(mem_range, pg_cnt);
#endif
	return;

}

EXPORT_SYMBOL(vxt_free_shared_memory);


/*
 *
 * vxt_alloc_shared_memory:
 *
 * Request a group of pages from the VxT transport agency.  In this
 * case the Xen Hypervisor pagevec mechanism is tapped to provide
 * the goods.
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
#ifdef LEGACY_4X_LINUX
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

			if (remote_pages[i] == NULL) {
				for(j = 0; j < i; j++) {
					vxt_restore_page(remote_pages[j]);
				}
				vxt_kfree(remote_pages);
				return NULL;
			}
		}
		return remote_pages;
}
#else
	return alloc_empty_pages_and_pagevec(pg_cnt);
#endif
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

	UMI_LOG(13, VXTPRINT_PROFILE_LEVEL,
	        " called, queue %p, origin %08x,\n",
	        queue, origin);
	if (origin == 0) {
		/* 
		 * The Xen gnttab is assymetric, these pages
		 * were granted by the foreign endpoint
		 * free them in the appropriate fashion.
		 */
		ASSERT(queue->vm_area != NULL);

		/*
		 * Free kernel mapping resources
		 */
		vxtcom_unmap_remote_buffer(queue->vm_area, ref_ptr,
		                           NULL, queue->pg_cnt);
		free_vm_area(queue->vm_area);
		while (ref_ptr != NULL) {
			queue->refs =
			   ((vxtdev_ref_list_t *)queue->refs)->next;
			vxt_kfree(ref_ptr);
			ref_ptr = queue->refs;
		}
		vxt_kfree(queue);
	} else {
		while (ref_ptr != NULL) {
			UMI_LOG(14, VXTPRINT_DEBUG_LEVEL,
			        "Ending foreign access 0x%x, ref_ptr %lu\n",
			        ref_ptr->ref, (vxtarch_word)ref_ptr);
#ifdef RW_ON_GNTTAB_END_FOREIGN_ACCESS
			gnttab_end_foreign_access_ref(ref_ptr->ref, FALSE);
#else
			gnttab_end_foreign_access_ref(ref_ptr->ref);
#endif
			/*
			 * Note release_grant_reference should occur
			 * on a callback out of end_foreign_access but
			 * XEN not complete yet.
			 */
			gnttab_release_grant_reference(&gref_head,
			                               ref_ptr->ref);
			queue->refs = 
			   ((vxtdev_ref_list_t *)queue->refs)->next;
			vxt_kfree(ref_ptr);
			ref_ptr = queue->refs;
		}
		if (queue->active_mapping == 0) {
			UMI_LOG(15, VXTPRINT_DEBUG_LEVEL,
			        "No active mappings, free pages, queue %p\n",
			        queue->qaddr);
			
			free_pages((unsigned long)queue->qaddr, 
			            get_order(queue->pg_cnt << PAGE_SHIFT));
		}
		vxt_kfree(queue);
	}
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
 * the corresponding methods the remote site.  For this reason
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
                          int page_cnt, uint32_t rhandle,
                          vxtdev_queues_t **qpages)
{
	void                   *new_queue;
	gnttab_map_grant_ref_t *map_parms;
	vxtdev_queues_t        *q_pages;
	vxtdev_ref_list_t      *ref_ptr;
	domid_t                remote = (domid_t)rhandle;
	void *addr;
	int i;
	int ret;

	UMI_LOG(16, VXTPRINT_PROFILE_LEVEL,
	        "called, qpages %p, pgcnt %08x\n",
	        page_ref_array, page_cnt);

	if ((new_queue = vxt_alloc_vm_area(page_cnt * PAGE_SIZE)) == NULL ) {
 		return VXT_NOMEM;
	}

	map_parms = vxt_kmalloc(sizeof(gnttab_map_grant_ref_t) * page_cnt,
	                        VXT_KMEM_KERNEL);
	if (!map_parms) {
		vxt_free_vm_area(new_queue);
	 	return VXT_NOMEM;
	}

	/*
	 * Allocate structure to track queue resource from within
	 * the controller.
	 */
	q_pages = vxt_kmalloc(sizeof(vxtdev_queues_t), VXT_KMEM_KERNEL);
	if (!q_pages) {
		vxt_kfree(map_parms);
		vxt_free_vm_area(new_queue);
		return VXT_NOMEM;
	}
	q_pages->refs = NULL;
	q_pages->next = NULL;
	q_pages->vm_area = NULL;
	q_pages->active_mapping = 0;
	q_pages->qaddr = 0;
	q_pages->pg_cnt = page_cnt;


	/*
	 * set parameter for call to map pages into newly
	 * allocated shared area
	 */
	addr = vxt_vm_area_addr(new_queue);

	UMI_LOG(17, VXTPRINT_DEBUG_LEVEL,
	        "add page refs, pagcnt 0x%x\n", (uint32_t)page_cnt);
	for (i=0; i < page_cnt; i++) {
		/*
		 * Note: for multiple page buffers, the
		 * references are passed to us in reverse
		 * order.  We must reverse them here.
		 */
		UMI_LOG(18, VXTPRINT_DEBUG_LEVEL,
		        "map op parms: ref 0x%x, rdom 0x%x, addr %lu\n",
		        (page_ref_array[(page_cnt - 1) - i].page_ref),
		        remote, (vxtarch_word)addr); 
		vxt_setup_map_parms(&(map_parms[i]), (unsigned long)addr,
		                    GNTMAP_host_map, 
		                    page_ref_array[(page_cnt-1) - i].page_ref, 
		                    remote);
		addr = (char *)((unsigned long)addr) + PAGE_SIZE;
	}

	/*
	 * Make hypervisor map call
	 */

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, 
	                                map_parms, page_cnt);

	if (ret) {
		vxt_free_vm_area(new_queue);
		vxt_kfree(q_pages);
		UMI_ERROR(87, VXTPRINT_PRODUCT_LEVEL,
		         "grant table op failed, ret = %d, status %d\n",
		         ret, map_parms[0].status);
		return VXT_FAIL;
	}


	for (i = 0; i < page_cnt; i++) {
		ref_ptr = vxt_kmalloc(sizeof(vxtdev_ref_list_t),
		                      VXT_KMEM_KERNEL);
		if (!ref_ptr) {
			vxtcom_unmap_remote_buffer(new_queue, NULL,
			                           map_parms, page_cnt);
			vxt_free_vm_area(new_queue);
			if (q_pages->refs != NULL) {
				vxtdev_ref_list_t *reflist_ptr; 
				reflist_ptr = (vxtdev_ref_list_t *)
				              q_pages->refs;
				while(reflist_ptr->next->next != NULL) {
					vxtdev_queues_t *temp;
					temp = q_pages->next;
					reflist_ptr->next = 
					   reflist_ptr->next->next;
					vxt_kfree(temp);
				}
				vxt_kfree(q_pages->refs);
			}
			vxt_kfree(q_pages);
			return VXT_FAIL;
		}

		/*
		 * Note: the references will again be stored
		 * in reverse order. However, this is what
		 * we want.  It matches the originating
		 * sides queue pages structure layout.
		 */
		ref_ptr->ref = page_ref_array[(page_cnt - 1) - i].page_ref,
		ref_ptr->handle = map_parms[(page_cnt - 1) - i].handle;
		ref_ptr->next = q_pages->refs;
		q_pages->refs = ref_ptr;
	}

	q_pages->qaddr = vxt_vm_area_addr(new_queue);
	q_pages->vm_area =  new_queue;
	vxt_kfree(map_parms);

	*qpages = q_pages;
	
	return VXT_SUCCESS;

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

	UMI_LOG(19, VXTPRINT_PROFILE_LEVEL,
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
#ifdef NO_ZAP_CALLBACK
	/*
	 * Remember, vma->vm_pgoff is a token
	 * we always have a mapping offset into
	 * the buffer of zero
	 */
	range = range >> PAGE_SHIFT;
	UMI_LOG(20, VXTPRINT_DEBUG_LEVEL,"page offset %lu\n", offset);

	for (offset = 0; offset < range; offset++) {
		ret = vxtcom_unmap_user_queue(vma, sm_record->user_handles, 
		                              offset + 1, offset, 
		                              (uint64_t)(vxtarch_word)0);
		if (ret != VXT_SUCCESS) {
			UMI_LOG(21, VXTPRINT_BETA_LEVEL,
			        "failed user queue unmap\n");
		}
	}
#endif

/*
if (0) {
*/
	if ((sm_record->shadow_queue != NULL) 
	    && (vma->vm_flags & VM_FOREIGN)) {
		struct page     *page;
		void            *page_object_address;
		unsigned long   address;

		vma->vm_flags &= ~(VM_FOREIGN);
#ifdef CONFIG_X86
#ifndef NO_FOREIGN_MAPPINGS_FIELD
        	vma->vm_mm->context.has_foreign_mappings = 0;
#endif
#endif

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
			UMI_LOG(22, VXTPRINT_DEBUG_LEVEL,
			        "ref_cnt 0x%x\n", page_count(page));
#ifdef LEGACY_4X_LINUX
			ret = io_remap_page_range(vma, address,
			                          page_to_pfn(page)<<PAGE_SHIFT,
			                          PAGE_SIZE,
			                          vma->vm_page_prot);
#else
			ret = vm_insert_page(vma, address, page);
#endif
			UMI_LOG(23, VXTPRINT_DEBUG_LEVEL,
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
	if ((mm != current->mm && mm != &init_mm)
	    || HYPERVISOR_update_va_mapping(addr, __pte(0), 0)) {
		ptep->pte_low = 0;
		smp_wmb();
		ptep->pte_high = 0;
	}
}
#else
#define vxt_pte_clear pte_clear

#endif


/*
 *
 * vxtcom_mmap_map:
 *
 * vxtcom_mmap_map sets the Intel page table entry
 * for the specified guest virtual address.  It 
 * also invalidates any pre-existing mapping in
 * the hypervisor SPT
 *
 * Returns:
 *		Returns 0
 *
 */

static int
vxtcom_mmap_map(pte_t *ptep, struct page *pmd_page, 
                unsigned long addr, void *data)
{
        pte_t *pte = (pte_t *)data;

#ifdef V2_6_18__53_1_13_el5
	UMI_LOG(24, VXTPRINT_PROFILE_LEVEL,
	        "ptep %p -> %012llx/%012llx\n",
	        ptep, pte_val(*pte), pte_val_ma(*pte));
#else
#ifdef LINUX_64
	UMI_LOG(25, VXTPRINT_PROFILE_LEVEL,
	        "ptep %p -> %012lx\n",
	        ptep, pte_val(*pte));
#else
	UMI_LOG(26, VXTPRINT_PROFILE_LEVEL,
	        "ptep %p -> %012llx\n",
	        ptep, pte_val(*pte));
#endif
#endif
        set_pte(ptep, *pte);
        xen_invlpg(addr);
        return 0;
}


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

	UMI_LOG(27, VXTPRINT_PROFILE_LEVEL,"ptep %p\n", ptep);
        vxt_pte_clear(mm, addr, ptep);
        xen_invlpg(addr);
        return 0;
}


/*
 * vxtcom_unmap_user_queue:
 *
 * In linux, the foreign page side of the user space mapping
 * of a device queue requires additional structures, additional
 * phys to machine mappings and additional resources.  This
 * routine is used to reclaim the resources.  It cannot do the
 * specific unmapping function, this is left to the zap_pages
 * routine, but the resources which remain at the time of 
 * device shutdown are reclaimed after the unmapping functions
 * done here.
 *
 * Returns:
 *		VXT_SUCCESS => Upon Success
 *
 *		VXT_PARM - Unmapping more than one page at a time OR
 *		           Mapping handle is invalid
 */

static int
vxtcom_unmap_user_queue(struct vm_area_struct *vma,
                        gnttab_map_grant_ref_t *mparms,
                        uint32_t end, uint32_t offset, uint64_t user_ptep)

{

	gnttab_unmap_grant_ref_t unmap_parms[4];
	uint32_t                 i;
	uint32_t                 op_index;
	uint64_t                 ptep;

	UMI_LOG(28, VXTPRINT_PROFILE_LEVEL,
	        " called, queue %p, page cnt %08x, offset %08x\n",
	        vma, end, offset); 

	if ((end - offset) > 2) {
		UMI_ERROR(88, VXTPRINT_PRODUCT_LEVEL,
		          "Serious error,  unmap range to large\n");
		return VXT_PARM;
	}
	ptep = user_ptep;

	op_index = 0;
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		for (i=offset * 2; i<(end * 2); i+=2) {
			if (mparms[i].handle != INVALID_GRANT_HANDLE) {
				UMI_LOG(29, VXTPRINT_BETA_LEVEL,
				        "set unmap, host addr %llu, mfn %llu\n",
				        mparms[i].host_addr,
				        mparms[i].dev_bus_addr);
		                vxt_setup_unmap_parms(&unmap_parms[op_index],
				                      mparms[i].host_addr,
		                                      GNTMAP_host_map,
				                      mparms[i].handle);

                		set_phys_to_machine(
				   __pa(mparms[i].host_addr) >> PAGE_SHIFT,
                                   INVALID_P2M_ENTRY);

				ClearPageReserved(
				   pfn_to_page(__pa(mparms[i].host_addr)
				               >> PAGE_SHIFT));
	
				mparms[i].handle = INVALID_GRANT_HANDLE;
				op_index++;
			} else {
				UMI_LOG(30, VXTPRINT_BETA_LEVEL,
				        "invalid grant handle, addr %llu\n",
				        mparms[i].host_addr);
			}

			if (ptep == 0) {
				UMI_LOG(31, VXTPRINT_BETA_LEVEL,"find ptep\n");
				if (create_lookup_pte_addr(vma->vm_mm,
				                           vma->vm_start + 
				                           (i/2 * PAGE_SIZE), 
				                           &ptep) != 0) {
					UMI_ERROR(89, VXTPRINT_PRODUCT_LEVEL,
					          "no pte addr!\n");
					unmap_parms[i+1].handle =
					   INVALID_GRANT_HANDLE;
					continue;
				}
			} else {
				ptep = virt_to_machine(ptep);
			}
			if (mparms[i+1].handle != INVALID_GRANT_HANDLE) {
				ASSERT(!xen_feature(
				       XENFEAT_auto_translated_physmap));


				UMI_LOG(32, VXTPRINT_DEBUG_LEVEL,
				        "unregister pte, pte = %p, new = %p\n",
				        (void *)(vxtarch_word)
				        mparms[i+1].host_addr,
				        (void *)(vxtarch_word)
				        ptep);
                		vxt_setup_unmap_parms(&unmap_parms[op_index],
				                      ptep,
				                      GNTMAP_host_map |
				                       GNTMAP_application_map |
				                       GNTMAP_contains_pte,
				                      mparms[i+1].handle);

				mparms[i+1].handle = INVALID_GRANT_HANDLE;
				op_index++;
        		} else {
				UMI_LOG(33, VXTPRINT_BETA_LEVEL,
				        "invalid grant handle, addr %llu\n",
				        mparms[i+1].host_addr);

			}
		}
	} else {

		for (i=offset; i<(end); i++) {
			if (mparms[i].handle != INVALID_GRANT_HANDLE) {
				vxt_setup_unmap_parms(&unmap_parms[op_index], 
				                      mparms[i].host_addr,
				                      GNTMAP_host_map,
				                      mparms[i].handle);

                		set_phys_to_machine(
				   __pa(mparms[i].host_addr)>>PAGE_SHIFT,
				   INVALID_P2M_ENTRY);

				ClearPageReserved(
				   pfn_to_page(__pa(mparms[i].host_addr)
				               >> PAGE_SHIFT));

				mparms[i].handle = INVALID_GRANT_HANDLE;
				op_index++;
		        } else {
			}

		}
	}

	if (op_index == 0) {
		return VXT_PARM;
	}

	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		UMI_LOG(34, VXTPRINT_DEBUG_LEVEL,"call gnttab unmap\n");
		if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, 
		                              unmap_parms, op_index)) {
			BUG();
		}
	} else {
		UMI_LOG(35, VXTPRINT_DEBUG_LEVEL,
		        "XENFEAT flavor: call gnttab unmap\n");
		if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
		                              unmap_parms, op_index)) {
			BUG();
		}
	}

	return VXT_SUCCESS;

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
 *		None
 */

static void
vxtcom_unmap_remote_buffer(struct vm_struct *vm_area, 
                           vxtdev_ref_list_t *handles,
                           gnttab_map_grant_ref_t *alt_handles,
                           int page_cnt)
{
        struct gnttab_unmap_grant_ref unmap[BLKIF_MAX_SEGMENTS_PER_REQUEST];
        uint32_t i, j, invcount = 0;
	void *buffer = (void *)vm_area->addr;
        int ret;

	UMI_LOG(36, VXTPRINT_PROFILE_LEVEL,
	        " called, area %p, buf %p, cnt %08x\n",
	        vm_area, vm_area->addr, page_cnt);
	j = 0;
	for (i = 0; i < page_cnt; i++) {
		if (alt_handles) {
                	vxt_setup_unmap_parms(&unmap[j], 
			                      (unsigned long) buffer, 
			                      GNTMAP_host_map, 
			                      alt_handles[i].handle);
		} else {
			ASSERT(handles != NULL);
			UMI_LOG(37, VXTPRINT_DEBUG_LEVEL,
			        "regular handles handle %p, ref %p\n",
			        (void *)(vxtarch_word)handles->handle,
			        (void *)(vxtarch_word)handles->ref);
                	vxt_setup_unmap_parms(&unmap[j], 
			                      (unsigned long) buffer, 
			                      GNTMAP_host_map, 
			                      handles->handle);
			handles = handles->next;
		}
		buffer = (void *)(((unsigned long)buffer) + PAGE_SIZE);
                invcount++;
		j++;
		if (j == BLKIF_MAX_SEGMENTS_PER_REQUEST) {
			UMI_LOG(38, VXTPRINT_DEBUG_LEVEL,
			        "calling unmap_grant_ref, cnt 0x%x\n",
			        invcount);
		        ret = HYPERVISOR_grant_table_op(
		                GNTTABOP_unmap_grant_ref, unmap, invcount);
		        ASSERT(ret == 0);
			j = 0;
			invcount = 0;
		}
        }

	if (invcount) {
		UMI_LOG(39, VXTPRINT_DEBUG_LEVEL,
		        "calling unmap_grant_ref, cnt 0x%x\n", invcount);
		ret = HYPERVISOR_grant_table_op(
		GNTTABOP_unmap_grant_ref, unmap, invcount);
		ASSERT(ret == 0);
	}

	/*
	 * The next bit of code should be un-necessary
	 * the caller will be making a free_vm_area call
	 * this should be sufficient to remove the shadow
	 * page table mappings.  However it has proven not
	 * to remove the primary mappings.  If this code
	 * is removed and the underlying problem is not
	 * addressed, the system will suffer full hypervisor
	 * hang or other corruption.
	 */
	buffer = (void *)vm_area->addr;
	for (i = 0; i < page_cnt; i++) {
		ret = HYPERVISOR_update_va_mapping((unsigned long)buffer,
		                                   __pte(0), 0);
		buffer = (void *)(((unsigned long)buffer) + PAGE_SIZE);
	}
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
 *	A copy of the pte information found at the user virtual address
 *
 */

#ifndef NO_ZAP_CALLBACK
static pte_t
vxtcom_clear_pte(struct vm_area_struct *vma,
                 unsigned long uvaddr,
                 pte_t *ptep, int is_fullmm)
{
	vxt_device_sm_t *sm;
	unsigned long   offset;
	int             ret;
	pte_t           copy = *ptep;

	UMI_LOG(40, VXTPRINT_PROFILE_LEVEL,
	        "vma %p, uvaddr %lu, is full 0x%x\n",
	        vma, uvaddr, is_fullmm);
	if (vma == NULL) {
		UMI_WARN(74, VXTPRINT_PRODUCT_LEVEL,"No vma\n");
		return copy;
        }

	sm = (vxt_device_sm_t *) vma->vm_private_data;

	if (sm == 0) {
		UMI_WARN(75, VXTPRINT_PRODUCT_LEVEL,"No token\n");
		return copy;
        }



	/*
	 * Remember, vma->vm_pgoff is a token
	 * we always have a mapping offset into
	 * the buffer of zero
	 */

	offset = (uvaddr - vma->vm_start) >> PAGE_SHIFT;
	UMI_LOG(41, VXTPRINT_DEBUG_LEVEL,"page offset %lu\n", offset);

	ret = vxtcom_unmap_user_queue(vma, sm->user_handles, 
	                              offset + 1, offset, 
	                              (uint64_t)(vxtarch_word)ptep);
	if (ret != VXT_SUCCESS) {
		UMI_LOG(42, VXTPRINT_BETA_LEVEL,"failed user queue unmap\n");
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
 *		None
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

	UMI_LOG(43, VXTPRINT_PROFILE_LEVEL, " called, vma %p\n", vma);
	if (vma == NULL) {
		UMI_WARN(76, VXTPRINT_PRODUCT_LEVEL,"No vma\n");
		return;
        }

	sm = (vxt_device_sm_t *) vma->vm_private_data;

	if (sm == 0) {
		UMI_WARN(77, VXTPRINT_PRODUCT_LEVEL,"No token\n");
		return;
        }

	UMI_LOG(44, VXTPRINT_DEBUG_LEVEL,"zap_page range\n");

	
	shadow_queue = sm->shadow_queue;
	sm->shadow_queue = NULL;

        zap_page_range(vma, vma->vm_start, vma->vm_end - vma->vm_start, NULL);
	sm->vma = NULL;

	ret = vxtctrlr.dev_lookup_token(sm->lookup_token << 12,
	                                       &verify_sm);
	if ((ret == VXT_SUCCESS) && (verify_sm == sm)) {
		UMI_LOG(45, VXTPRINT_BETA_LEVEL,
		        "token found, vma %p, queue %p\n", vma, sm->addr);
		device_queue = (vxtdev_queues_t *)sm->queue_token;
		device_queue->active_mapping = 0;
		/*
		 * Drop the reference taken in lookup
		 */
		vxtctrlr.dev_token_dec(sm);
	} else {
		UMI_LOG(46, VXTPRINT_BETA_LEVEL,
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
		UMI_LOG(47, VXTPRINT_DEBUG_LEVEL,"Delete shadow queue\n");

	        page_object_address = shadow_queue;

		for (i = 0; i < sm->len; i+=PAGE_SIZE) {
			page = vmalloc_to_page(page_object_address);
			if (page) {
				UMI_LOG(48, VXTPRINT_DEBUG_LEVEL,
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
				UMI_WARN(78, VXTPRINT_PRODUCT_LEVEL,
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
 *		NOPAGE_SIGBUS
 *
 */

static struct page *
vxtcom_nopage(struct vm_area_struct *vma, 
              unsigned long address, int *type)
{

	UMI_LOG(49, VXTPRINT_PROFILE_LEVEL,
	        "vxtcom_nopage called, vma %p, address %lu, type %p\n",
	        vma, address, type);

	return NOPAGE_SIGBUS;
}


static void vxtcom_mmap_open(struct vm_area_struct *vma);
static void vxtcom_mmap_close(struct vm_area_struct *vma);
#ifndef NO_ZAP_CALLBACK
static pte_t vxtcom_mmap_zap(struct vm_area_struct *vma,
                             unsigned long uvaddr,
                             pte_t *ptep, int is_fullmm);
#endif
static struct page *
vxtcom_mmap_vmmap(struct vm_area_struct *vma, 
                   unsigned long address, int *type);

static struct vm_operations_struct vxtcom_vm_ops =
{
	.open =		vxtcom_mmap_open,
	.close =	vxtcom_mmap_close,
	.nopage =	vxtcom_mmap_vmmap,
#ifndef NO_ZAP_CALLBACK
        .zap_pte =      vxtcom_mmap_zap
#endif
};

static struct vm_operations_struct vxtcom_foreign_ops =
{
        nopage:   vxtcom_nopage,
        close:    vxtcom_vm_close,
#ifndef NO_ZAP_CALLBACK
        zap_pte:  vxtcom_clear_pte,
#endif
};

/*
 *
 * vxtcom_mmap_vmmap:
 *
 * vxtcom_mmap_vmmap is the nopage, device pager routine
 * for vxtcom.
 *
 */

static struct page *
vxtcom_mmap_vmmap(struct vm_area_struct *vma, 
                   unsigned long address, int *type)
{
	vxt_device_sm_t *sm;
	struct page     *page;
	long            token;
	unsigned long   offset;
	void            *page_object;
	void            *page_object_address;
	int		ret;



	UMI_LOG(50, VXTPRINT_PROFILE_LEVEL,
	        "called, vma %p, address %lu, type %p\n",
	        vma, address, type);
return NOPAGE_SIGBUS;

	if (vma == NULL) {
		return NOPAGE_SIGBUS;
        }

        sm = (vxt_device_sm_t *)(vma->vm_private_data);

	token = sm->lookup_token;

	if (token == 0) {
		return NOPAGE_SIGBUS;
        }

	ret = vxtctrlr.dev_lookup_token(token << 12, &sm);
	if (ret != VXT_SUCCESS) {
		return NOPAGE_SIGBUS;
	}

	page_object =  sm->addr;

	if (page_object == NULL) {
		/*
		 * Drop the reference taken in lookup
		 */
		vxtctrlr.dev_token_dec(sm);
		return NOPAGE_OOM;
	}

	/*
	 * Remember, vma->vm_pgoff is a token
	 * we always have a mapping offset into
	 * the buffer of zero
	 */
	offset = address - vma->vm_start;

	if (offset > sm->len) {
		/*
		 * Drop the reference taken in lookup
		 */
		vxtctrlr.dev_token_dec(sm);
		return NOPAGE_SIGBUS;
	}
        

        page_object_address = page_object + offset;

	/*
	 * Get the kernel segment virtual address
	 * and find the page structure
	 */
	page = vmalloc_to_page(page_object_address);
	get_page(page); /* increment page count */

	if (type) {
		*type = VM_FAULT_MINOR;
	}

	/*
	 * Drop the reference taken in lookup
	 */
	vxtctrlr.dev_token_dec(sm);

	/*
	 * Return the page pointer
	 */
	return (page);

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
	UMI_LOG(51, VXTPRINT_PROFILE_LEVEL, "called vma %p\n", vma);
	return;
}


#ifndef NO_ZAP_CALLBACK
static pte_t
vxtcom_mmap_zap(struct vm_area_struct *vma,
                unsigned long uvaddr,
                pte_t *ptep, int is_fullmm)
{
	pte_t           copy = *ptep;

	UMI_LOG(52, VXTPRINT_PROFILE_LEVEL,"uvaddr %p\n", (void *)uvaddr);
	if (vma == NULL) {
		UMI_WARN(79, VXTPRINT_PRODUCT_LEVEL,"No vma\n");
		return copy;
        }


        vxt_pte_clear(vma->vm_mm, uvaddr, ptep);
        xen_invlpg(uvaddr);



	UMI_LOG(53, VXTPRINT_DEBUG_LEVEL,"finished\n");
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

	UMI_LOG(54, VXTPRINT_PROFILE_LEVEL," called\n");
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
			UMI_LOG(55, VXTPRINT_BETA_LEVEL,
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
		UMI_LOG(56, VXTPRINT_BETA_LEVEL,
		        "no token found, vma %p, queue %p\n", vma, sm->addr);
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
	unsigned long     frame;
	grant_ref_t       ref;
	void              *new_queue;
	char              *page_ptr;
	vxtdev_ref_list_t *ref_ptr;
	vxtdev_queues_t   *q_pages;
	domid_t           remote = (domid_t)rhandle;
	int               i;

/* CDY: Note this code has to be fixed
 * for guest to guest support.  We
 * must grant foreign access before we
 * attempt connection to the opposite side
 * we will need to insert a domain 
 * resolution call for the device
 * remote domain.
 */
	UMI_LOG(57, VXTPRINT_PROFILE_LEVEL,
	        " called, remote endpoint 0x%llx, page_cnt 0x%x,\n",
	        rhandle, page_cnt);

	UMI_LOG(58, VXTPRINT_DEBUG_LEVEL,
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
		UMI_WARN(81, VXTPRINT_PRODUCT_LEVEL,
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

	for (i = 0; i < page_cnt; i++) {
		frame = virt_to_mfn(page_ptr);
		/*
		 * Get a pre-allocated element from 
		 * the grant table
		 */
     
		ref = gnttab_claim_grant_reference(&gref_head);
		if (!ref) {
			goto  addqueue_fail;
		}

		/*
		 * Allocate an element to track the shared
		 * status of the page
		 */
		ref_ptr = vxt_kmalloc(sizeof(vxtdev_ref_list_t),
		                      VXT_KMEM_KERNEL);
		if (!ref_ptr) {
#ifdef RW_ON_GNTTAB_END_FOREIGN_ACCESS
			gnttab_end_foreign_access(ref, FALSE, 0UL);
#else
			gnttab_end_foreign_access(ref, 0UL);
#endif
			gnttab_release_grant_reference(&gref_head, ref);
			goto addqueue_fail;
		}

		/*
		 * Note:  Our references will be traversed
	 	 * from last to first, i.e. from the highest
		 * address page to the lowest.
		 */
		ref_ptr->ref = ref;
		ref_ptr->handle = 0;
		ref_ptr->next = q_pages->refs;
		q_pages->refs = ref_ptr;

		UMI_LOG(59, VXTPRINT_DEBUG_LEVEL,
		        "******* REF 0x%x ******FRAME %llx********\n",
		        ref, (uint64_t)frame);
		gnttab_grant_foreign_access_ref(ref, remote, frame, 0);
		UMI_LOG(60, VXTPRINT_DEBUG_LEVEL,
		        "****** grant foreign access ref 0x%x, remote 0x%x\n",
	                ref, remote);
		page_ptr += PAGE_SIZE;
	}


	*qpages = q_pages;
	*queue_ptr = new_queue;

	UMI_LOG(61, VXTPRINT_DEBUG_LEVEL," returning - queue %p, token %p\n",
	        new_queue, q_pages);


	return VXT_SUCCESS;

addqueue_fail:
	{
		vxtdev_ref_list_t *ref_ptr;

		/*
		 * revoke foreign access on the pages
		 * shared so far.
		 */
		ref_ptr = q_pages->refs;
		while (ref_ptr != NULL) {
#ifdef RW_ON_GNTTAB_END_FOREIGN_ACCESS
			gnttab_end_foreign_access(ref, FALSE, 0UL);
#else
			gnttab_end_foreign_access(ref, 0UL);
#endif
			gnttab_release_grant_reference(&gref_head, 
			                               ref_ptr->ref);
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
vxtcard_ctrlr_init(void *fops_handle,
                   uint32_t first_controller, int ctrlr_slot_number,
                   int vdevice)
{
	vxt_device_class_t     *class;
	struct file_operations *vxt_ctrlr_fops = (struct file_operations *)
	                                         fops_handle;
	int                    ret;

	UMI_LOG(62, VXTPRINT_PROFILE_LEVEL,
	        " called, fops %p, first %08x"
		" controller slot 0x%x, vdevice %d\n",
	        vxt_ctrlr_fops, first_controller, ctrlr_slot_number, vdevice);
	if (first_controller) {
		ret = gnttab_alloc_grant_references(MAX_TARGET, &gref_head);
		if (ret) {
			UMI_WARN(82, VXTPRINT_PRODUCT_LEVEL,
			         "Unable to secure grant references\n");
			return VXT_RSRC;
		}
	}


	if ((class = get_vxt_class()) != NULL) {
		UMI_LOG(63, VXTPRINT_DEBUG_LEVEL,"create_device class\n");
#ifdef LEGACY_4X_LINUX
		class_simple_device_add(class, MKDEV(vdevice, 0),
		                        NULL, "vxtctrlr%d", ctrlr_slot_number);
#else
		class_device_create(class, NULL,
		                    MKDEV(vdevice, 0), NULL,
		                    "vxtctrlr%d", ctrlr_slot_number);
#endif
	}

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

        UMI_LOG(64, VXTPRINT_PROFILE_LEVEL,
                " called, virtual device 0x%x\n",
                vdev);
        if ((class = get_vxt_class()) != NULL) {
#ifdef LEGACY_4X_LINUX
                class_simple_device_remove(MKDEV(vdev, 0));
#else
                class_device_destroy(class, MKDEV(vdev, 0));
#endif
        }

        if (last_controller) {
                gnttab_free_grant_references(gref_head);
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
	vxtcom_hub_disconnect();
	return;
}
EXPORT_SYMBOL(vxt_hub_disconnect);

/*
 * vxtcom_hub_mmap: Foreign/page acceptor side of user task
 *                  space mmap.
 *
 * vxtcom_hub_mmap pulls pages through the Xen
 * shared memory reference tables and maps them
 * into the map provided.  The phys_pgs array is
 * filled in with the new physical page structures
 *
 * vxtcom_hub_mmap is a helper routine for vxt_card_mmap
 *
 * Return:
 *
 *		0 => SUCCESS
 *		-ENXIO  - 
 *			No registered handler for vma
 *			length does not match backing object
 *
 * NOTE: To generalize into a Xen Foreign page map, treat the
 * "vma == NULL" case.
 *
 */

static int
vxtcom_hub_mmap(void *labels, uint32_t pg_cnt, struct page **remote_pages,
                uint64_t remote_ep, struct page **phys_pgs,
                struct vm_area_struct *vma, void **mapping_parameters)
{
	gnttab_map_grant_ref_t *map_parms;
	unsigned long     kvaddr;
	vxtdev_ref_list_t *labels_ptr = NULL;
	struct page       *pg;
	uint64_t          ptep;
	uint32_t          i,j;
	int               ret;
   
	labels_ptr = (vxtdev_ref_list_t *)labels;
	UMI_LOG(65, VXTPRINT_PROFILE_LEVEL,
	        "called labels %p, pg_cnt %d, remote_pages %p,"
		" remote endpoint %llx, phys_pgs %p, vma %p\n",
		labels, pg_cnt, (void *)remote_pages, 
		remote_ep, (void *)phys_pgs, vma);



	map_parms =
	   vxt_kmalloc(sizeof(gnttab_map_grant_ref_t) * pg_cnt * 2,
	               VXT_KMEM_KERNEL);
	if (!map_parms) {
		return VXT_NOMEM;
	}
	*mapping_parameters = (void *)map_parms;


	/*
	 * Now that we have:
	 * 	1: some empty physical pages
	 *	2: an array to hold our mapping handles and map ops
	 *	3: re-acquired access to our remote page refs
	 * 	4: user level vma for user virt addresses
	 *
 	 * We must map our remote pages again.  Before we can do
	 * so however, we must determine whether Xen "auto-translates
	 * phys-maps".  If not we will substitute the address in 
	 * the pte table for the kvaddr before calling gnttab.
	 */



	/*
	 * Set traversal pointer for our remote 
	 * page references
	 */
		
	i = pg_cnt;
	j = (pg_cnt * 2) - 1;
	for (; i > 0; i--, j-=2) {
		ASSERT(labels_ptr != NULL);
		kvaddr = (unsigned long)
		         pfn_to_kaddr(page_to_pfn(remote_pages[i-1]));
		
		/* 
		 * Set up the gnttab mapping op.  The physical address
		 * depends on the auto translate facility.  In the
		 * event that auto-translate is not available, we
		 * must map the pte address as well.
		 */
		
		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			UMI_LOG(66, VXTPRINT_BETA_LEVEL,
			        "No Xen physmap auto-translation\n");

			vxt_setup_map_parms(&map_parms[j-1], kvaddr,
			                    GNTMAP_host_map,
			                    labels_ptr->ref,
			                    remote_ep);
	
			ret = 
			   create_lookup_pte_addr(
			      vma->vm_mm, 
			      vma->vm_start + ((i -1) * PAGE_SIZE),
			      &ptep);
		
			if (ret) {
				WPRINTK("vxtcom_mmap pte map table missing\n");
				vxt_kfree(map_parms);
				return -ENXIO;
			}

			UMI_LOG(67, VXTPRINT_DEBUG_LEVEL,
			        "gnttab call on pte, pte = %llx\n",
			        ptep);
			vxt_setup_map_parms(&map_parms[j], ptep, 
			                    GNTMAP_host_map
			                       | GNTMAP_application_map
			                       | GNTMAP_contains_pte,
			                    labels_ptr->ref, 
			                    remote_ep);
		} else {
			UMI_LOG(68, VXTPRINT_DEBUG_LEVEL,
			        "Xen is auto-translating physmaps\n");
			vxt_setup_map_parms(&map_parms[i - 1], kvaddr,
			                    GNTMAP_host_map,
			                    labels_ptr->ref,
			                    remote_ep);
		}
		labels_ptr = labels_ptr->next;
	}

	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref,
		                                map_parms, pg_cnt * 2);
	} else {
		ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref,
		                                map_parms, pg_cnt);
	}

	if(ret) {
		UMI_WARN(83, VXTPRINT_PRODUCT_LEVEL,
		         "FAILED TO MAP USER LEVEL REMOTE PAGES\n");
	}
	ASSERT(ret == 0);


	/*
	 * We have now associated the machine pages with
	 * appropriate guest physical addresses
	 * Now we must map the guest physical addresses
	 * into the appropriate user virtual space
	 * using only the prescribed method.
	 */
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		for (i = 0, j = 0; i < pg_cnt; i++, j+=2) {

			kvaddr = (unsigned long)
			         pfn_to_kaddr(page_to_pfn(remote_pages[i]));

			set_phys_to_machine(
			   __pa(kvaddr) >> PAGE_SHIFT,
			   FOREIGN_FRAME(map_parms[j].dev_bus_addr
			                 >> PAGE_SHIFT));
			pg = pfn_to_page(__pa(kvaddr) >> PAGE_SHIFT);
			SetPageReserved(pg);
			phys_pgs[i] = pg;
		}
	} else {
		for (i = 0; i < pg_cnt; i++) {
			kvaddr = (unsigned long)
			   pfn_to_kaddr(page_to_pfn(remote_pages[i]));

			pg = pfn_to_page(__pa(kvaddr) >> PAGE_SHIFT);
			phys_pgs[i] = pg;
			SetPageReserved(pg);
#ifdef LEGACY_4X_LINUX
			ret = io_remap_page_range(vma,
			                          vma->vm_start
			                             + (i * PAGE_SIZE),
			                          page_to_pfn(pg)
			                             << PAGE_SHIFT,
			                          PAGE_SIZE,
			                          vma->vm_page_prot);
#else
			ret = vm_insert_page(vma, 
			                     vma->vm_start + (i * PAGE_SIZE),
			                     pg);
#endif
			if (ret) {
				UMI_WARN(84, VXTPRINT_PRODUCT_LEVEL,
				         "vm_insert_page failed\n");
			}
			if (ret) {
				/* CDY: fill in recovery here */
			}
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
	pte_t                 pte;
	struct page           **map = NULL;
	int                   ret;

	UMI_LOG(69, VXTPRINT_PROFILE_LEVEL,
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
		UMI_WARN(85, VXTPRINT_PRODUCT_LEVEL,
		         "no token found, vma %p\n", vma);
		return -ENXIO;
	}

	if (sm->len  != (vma->vm_end - vma->vm_start)) {
		/*
		 * It would be rather pointless to map
		 * only a portion of a shared buffer queue
		 */
		UMI_LOG(70, VXTPRINT_DEBUG_LEVEL,
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
	 * if they are, we must take extraordinary
	 * steps.  The combined linux/xen implementation
	 * in this area does not abstract the
	 * paging/memory object well and Xen
	 * allows linux to be aware of the "special"
	 * nature of foreign pages.
  	 *
         */
	
	if (page_origin == 0) {
		struct page       **remote_pages;
	        void              *mapping_parameters = 0;
		vxtdev_queues_t   *qpages;
		uint32_t          pg_cnt;

		pg_cnt = sm->len >> PAGE_SHIFT;
		UMI_LOG(71, VXTPRINT_DEBUG_LEVEL,
		        "vxtcom_mmap called: pg_cnt = 0x%x, vma flags 0x%lx\n",
		        pg_cnt, vma->vm_flags);

		/*
		 * Go find the queue resources to
		 * access the page refs.
		 */
		ret = vxtctrlr.devreq_find_qpages(sm->dev_handle,
		                                  sm->addr, &qpages);
		if (ret != VXT_SUCCESS) {
			/*
			 * Drop the reference taken in lookup
			 */
			vxtctrlr.dev_token_dec(sm);
			return VXT_PARM;
		}
		UMI_LOG(72, VXTPRINT_DEBUG_LEVEL,
		        "return from find_qpages, qpages %p\n", qpages);

		/*
		 * To begin our adventure we will 
		 * need to remap the pages.  The 
		 * kernel level mapping is insufficent
		 * because, we may need to map the pte
		 * table entry addresses instead of the
		 * actual guest physical pages.
		 * (no auto-translate of physmap)
		 *
		 * Get an array of physical pages
		 */

		remote_pages = vxt_alloc_shared_memory(pg_cnt);
		if (remote_pages == NULL) {
			UMI_WARN(86, VXTPRINT_PRODUCT_LEVEL,
			         "no physpages available for remote mapping\n");
			/*
			 * Drop the reference taken in lookup
			 */
			vxtctrlr.dev_token_dec(sm);
			return VXT_NOMEM;
		}







		/* Here we go, we must fill in the 
		 * vm_private_data field so that 
		 * the mm/memory module can use
		 * it.  .. Yes I know the field
		 * is supposed to be private.
		 */
		
		map = kzalloc(((vma->vm_end - vma->vm_start) >> PAGE_SHIFT)
		              * sizeof(struct page_struct*), GFP_KERNEL);

		if (map == NULL) {
			WPRINTK("Couldn't alloc VM_FOREIGN map.\n");
			vxt_free_shared_memory(remote_pages, pg_cnt);
			/*
			 * Drop the reference taken in lookup
			 */
			vxtctrlr.dev_token_dec(sm);
                	return -ENOMEM;
        	}

        	sm->foreign_map.map = map;
        	sm->vma = vma;
        	sm->remote_pages = remote_pages;
        	vma->vm_private_data = &sm->foreign_map;
		vma->vm_flags |= VM_FOREIGN | VM_RESERVED | VM_DONTCOPY;
		sm->lookup_token = token;
#ifdef CONFIG_X86
#ifndef NO_FOREIGN_MAPPINGS_FIELD
        vma->vm_mm->context.has_foreign_mappings = 1;
#endif
#endif

		ret = vxtcom_hub_mmap(qpages->refs, pg_cnt,
		                      remote_pages, remote_ep,
		                      sm->foreign_map.map, vma,
		                      &mapping_parameters);
		if (ret != 0) {
			WPRINTK("Failed be_mmap.\n");
			vxt_free_shared_memory(remote_pages, pg_cnt);
			/*
			 * Drop the reference taken in lookup
			 */
			sm->foreign_map.map = NULL;
			sm->remote_pages = NULL;
			sm->user_handles = NULL;
			vxt_kfree(map);
			vxtctrlr.dev_token_dec(sm);
			return ret;
		}

		sm->user_handles = mapping_parameters;

		device_queue = (vxtdev_queues_t *)sm->queue_token;
		device_queue->active_mapping = 1;
	        vma->vm_ops = &vxtcom_foreign_ops;

		return 0;
	}

	/*
	 * Local Case
	 *
	 * Disable paging
	 *
 	 * Don't copy will keep the pages shared on a
	 * task fork.
	 */

#ifdef LEGACY_4X_LINUX
	/*
	 * Remove PFNMAP - we do this because it is not available.
	 * It turns out in both the Legacy and later support cases
	 * we are mapping our machine physical against mapped physcal
	 * page ranges and hence have associated page structures.
	 * However, we want to maintain as much control over page
	 * handling as possible on these pages and pushing them off
	 * of the "Normal" path seems the better choice when available.
	 */
	vma->vm_flags |=  VM_RESERVED | VM_DONTCOPY | VM_FOREIGN | VM_SHARED;
#else
	vma->vm_flags |=  VM_RESERVED | VM_DONTCOPY | VM_PFNMAP | VM_SHARED;
#endif

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
	UMI_LOG(73, VXTPRINT_DEBUG_LEVEL, "parent queue %p\n", device_queue);
	device_queue->active_mapping = 1;

	for (address = vma->vm_start; 
	     address < vma->vm_end; address += PAGE_SIZE) {

		/*
		 * Get the kernel segment virtual address
		 * and find the page structure
		 */
/*
	page = virt_to_page(page_object_address);
*/
		page = vmalloc_to_page((void *)page_object_address);

#ifdef LEGACY_4X_LINUX
		atomic_add(1, &page->_mapcount);
		get_page(page);
		get_page(page);
#endif
		pte = mk_pte(page, vma->vm_page_prot);
		pte = pte_mkwrite(pte);
		apply_to_page_range(vma->vm_mm, address, 
		                    PAGE_SIZE, vxtcom_mmap_map, &pte);
		page_object_address += PAGE_SIZE;
	}

        vxtcom_mmap_open(vma);

	return 0;

}

EXPORT_SYMBOL(vxt_card_mmap);


