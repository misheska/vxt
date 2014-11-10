/*
 *
 * virtio_vxt.c:
 *
 * virtio_vxt.c implements a pci device front end for the VxT controller
 * The VxT controller is an emulation of a hardware bus, or more precisely
 * an abstracted bus that may harbor virtual or physical devices.
 * 
 * The pci device implemented here acts as a bus.  Interaction with it 
 * provides a means of advertising the VxT controller and connecting to
 * its resources.
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


#include <sys/eventfd.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/mman.h>


#include <linux/vxt_kvm.h>
#include "vxt_virtio_cfg.h"
#include "virtio-vxt.h"
#include "virtio.h"
#include "qemu-kvm.h"  /* to acquire vxt bus gsi lines */




static int virtio_vxt_instance = 0;


typedef struct vxt_sg_read_object {
	struct iovec *list;
	size_t sg_off;      /* offset inside of vector entry */
	size_t bytes_read;  /* total bytes read */
	int index;
	int last;
} vxt_sg_read_object_t;




/*
 *
 * vxt_sg_read_object_init:
 *
 * Point a vxt_sg_read_object at a scatter/gather list and
 * initialize its internal read head to point at the first
 * byte of the first element.
 *
 *
 * Returns:
 *		None
 */

static void
vxt_sg_read_object_init(vxt_sg_read_object_t *ro,
                        struct iovec *list, uint32_t last)
{

	ro->index = 0;  /* start on the first sg element */
	ro->sg_off = 0; /* start at the beginning of sg element */
	ro->last = last;
	ro->list = list;
	ro->bytes_read = 0;

	return;
}


/*
 *
 * vxt_sg_object_read:
 *
 * vxt_sg_object_read containerizes the handling of sequential partial
 * reads on a s/g object.  It treats the s/g like a calling card
 * using the minutes stored in the object on each subsequent call
 *
 * An incrementing index and offset are kept to ensure that data is
 * returned from the proper place in the partially read buffer.  Data is
 * read out in segments according to the length requested on each
 * sequential read event.
 *
 *
 * The calling parameters are done in memcpy style
 *
 * Returns:
 *		Number of bytes read
 *
 */

static size_t
vxt_sg_object_read(void *data, vxt_sg_read_object_t *ro, size_t n)
{
    	int i;
	size_t len;
	size_t off;
	size_t out_bytes;

	off = ro->sg_off;
	out_bytes = 0;
	for (i = ro->index; i < ro->last; i++) {
		if ((ro->list[i].iov_len - off) <= 0) {
			/*
			 * Don't trust memcpy to behave on zero length
			 */
			off = 0;
			continue;
		}
		if ((ro->list[i].iov_len - off) < n) {
			len = ro->list[i].iov_len - off;
			memcpy(data, ro->list[i].iov_base + off, len);
		} else {
			memcpy(data, ro->list[i].iov_base + off, n);
			ro->sg_off = off + n;
			ro->index = i;
			ro->bytes_read += n;
			return out_bytes + n;
		}
		data += len;
		off = 0;
		n -= len;
		ro->bytes_read += len;
		out_bytes += len;
	}
	/*
	 * Our read object is empty
	 * just set the index and don't worry about offset
	 */
	ro->index = i; /* one beyond "last" element */
	return out_bytes;
	
}

/*
 *
 * vxt_sg_object_bytes_read:
 *
 * Return the total number of bytes read out of the vxt
 * scatter/gather object.
 *
 *
 * Returns:
 *		Total number of bytes read from the associated sg object
 */

static inline size_t
vxt_sg_object_bytes_read(vxt_sg_read_object_t *ro)
{
	return ro->bytes_read;
}


/*
 * vxt_device_memory_region
 *
 * vxt_device_memory_region is an sequestered range of address space.
 * This address range is registered as a slot and reflected in the guest.
 * Portions of this region are populated via mmap of underlying shared 
 * memory that is provided by the VxT backend.
 */

uint8_t *vxt_device_memory_region;


/*
 * Make this a setable parameter in the product
 */
uint64_t vxt_device_memory_region_size = 0x100000;  /* 1 Meg */


uint32_t vxt_device_memory_alloc_wrapped = 0;






struct virtio_vxt_config
{
	uint64_t guest_inbuf_pa;
	uint64_t host_inbuf_va;
	uint64_t inbuf_size;
	uint64_t guest_outbuf_pa;
	uint64_t host_outbuf_va;
	uint64_t outbuf_size;
	uint64_t guest_sigbuf_pa;
	uint64_t host_sigbuf_va; 
	uint64_t sigbuf_size;
	uint64_t guest_name;
	char     guest_uuid[MAX_VXT_UUID];
	char     ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];
} __attribute__((packed));


typedef struct virtio_vxt_config virtio_vxt_config_t;

typedef struct virtio_vxt
{
	VirtIODevice       vdev;
	vxt_bus_state_t    driver_state;
	VirtQueue          *send_vq;
	VirtQueue          *rcv_vq;
	VirtQueue	   *vxt_bus_cfg_dummy;
	VirtQueue	   *vxt_bus_sig_dummy;
	struct virtio_vxt  *vxt_controller;
	virtio_vxt_config_t cfg;
	int                card_fd;  /* ioctl descriptor for vxt card module */
	int                sig_irq_fd;
	int                cfg_irq_fd;
	int                sig_db;
	int                cfg_db;
	int                response_pending;
	int                suspended;
	int                vqs_initialized;
	struct kvm_irq_routing_entry sig_entry;
	struct kvm_irq_routing_entry cfg_entry;
	vxt_virtio_cfg_t   response;
	struct virtio_vxt  *next;
} virtio_vxt_t;

typedef struct virtio_vxtbus
{
	VirtIODevice       vdev;
	struct virtio_vxtbus  *next;
} virtio_vxtbus_t;

virtio_vxt_t       *vxt_controllers = NULL;




static virtio_vxt_t *vdev_parent(VirtIODevice *vdev)
{
	virtio_vxt_t *parent;

	parent = (virtio_vxt_t *)
	         (((unsigned long)vdev) +
	          (unsigned long)&(((virtio_vxt_t *)0)->vdev));
	return parent;
}


/*
 *
 * virtio_vxt_send_dummy_reponse:
 *
 * virtio_vxt_send_dummy_queue_responses writes data into the buffers
 * provided by the guest driver on the two dummy queues for VxT bus
 * configuration IRQ and VxT bus device event.  These two queues are
 * not used directly but must have data on them so that the virtio_pci
 * handler in the guest will pass the IRQ event through to the VxT guest
 * callback.
 *
 * Note: we assume that there is no pending response and overwrite one
 * if it is present.
 *
 *
 * Returns:
 *
 *		VIRTIO_VXT_SUCCESS
 *
 */

static int
virtio_vxt_send_dummy_response(virtio_vxt_t *vxtctrlr, VirtQueue *vq)
{
	VirtQueueElement elem;
	char             dummy_string[] = "This queue is not used\n";
	char		 *strptr;
	int              send_size;
	int              len;
	int              offset;
	int              i;

	fprintf(stderr, "CDY: virtio_vxt_send_dummy_response called, vq %p\n",
	        vq);
	if (!virtqueue_pop(vq, &elem)) {
		/*
		 * If there is not an element on the queue
		 * the guest has not initialized properly
		 */
		fprintf(stderr, "There is no buffer present to setup IRQ "
		        " response data.  VXT IRQ signal may be blocked. "
		        "vq %p\n", vq);
		return VIRTIO_VXT_RSRCS;
	}

	strptr = dummy_string;
	send_size = strlen(dummy_string) + 1;
	/*
	 * Check out the size of the sg element we have acquired.
	 * The sg buffer may be segmented but it must be at least
	 * big enough to accomodate us or we will reject it.  
	 */
	i = 0;
	offset = 0;
	while (send_size) {
		if (send_size < elem.in_sg[i].iov_len) {
			len = send_size;
		} else {
			len = elem.in_sg[i].iov_len;
		}
		memcpy(elem.in_sg[i].iov_base, strptr + offset, len);
		offset += len;
		send_size -= len;
		i++;
	}
	if (send_size != 0) {
		/*
		 * Guest did not provide a big enough buffer.
		 * Push the message and signal the other side
		 */
		fprintf(stderr, "Insufficent space for dummy message\n");
		virtqueue_push(vq, &elem,
		               (strlen(dummy_string) + 1)-send_size);
		return VIRTIO_VXT_RSRCS;
	}
		
	virtqueue_push(vq, &elem, strlen(dummy_string) + 1);
	return VIRTIO_VXT_SUCCESS;
}


/*
 *
 * virtio_vxt_init_com_channel:
 *
 *
 * virtio_vxt_init_com_channel is called during the transition
 * between  VXT_PROVISIONED and VXT_CONNECTING when the driver
 * provides us with the regular memory backed physical regions
 * for the VxT Bus in, out, and signaling buffers. 
 *
 * virtio_vxt_init_com_channel contacts the VxT kernel module to
 * acquire the vxt controller configuration channel resources
 * and places pointers to them in the vdev->vxt_controller
 * structure.  The size of the shared buffers and their locations
 * in the guest's memory address space are provided.  The 
 * mappings within the kvm user task space are found and
 * noted in the vxt_ctrlr structure.  These locations are then
 * the targets of mmap calls to link the shared memory objects
 * returned by the VxT kernel module to their appropriate 
 * guest memory ranges.
 *
 * Returns:
 *
 *		VIRTIO_VXT_SUCCESS => Upon Successful Invocation
 *		VIRTIO_VXT_PARM - Shared buffer locations invalid 
 *		VIRTIO_VXT_RANGE - Shared buffers too large or small
 *		VIRTIO_VXT_RSRCS - Failed to acquire VxT config resources
 */

static int
virtio_vxt_init_com_channel(virtio_vxt_t *vxt_ctrlr)
{
	kvm_vxt_card_reg_bus_parms_t register_parms;
	kvm_vxt_card_del_bus_parms_t unload_parms;
	struct VirtQueue             *bus_cfg_queue;
	struct VirtQueue             *bus_sig_queue;
	int                          vxt_card_bus;
	CPUState                     *env = first_cpu;
	int                          ret;

	printf("virtio_vxt_init_com_channel: called, env %p, vmfd 0x%x\n",
	       env, ((KVMState *)env->kvm_state)->vmfd);

	fprintf(stderr,"CDY virtio_vxt_init_com_channel: com buffers "
	        "inbuf_pa 0x%llx, outbuf_pa 0x%llx, sigbuf_pa 0x%llx, "
	        "inbuf_size 0x%llx, outbuf_size 0x%llx, "
	        "sigbuf_size 0x%llx, env %p, vmfd 0x%x\n",
	        vxt_ctrlr->cfg.guest_inbuf_pa,
	        vxt_ctrlr->cfg.guest_outbuf_pa,
	        vxt_ctrlr->cfg.guest_sigbuf_pa,
	        vxt_ctrlr->cfg.inbuf_size,
	        vxt_ctrlr->cfg.outbuf_size,
	        vxt_ctrlr->cfg.sigbuf_size,
	        env, ((KVMState *)env->kvm_state)->vmfd);


	/*
	 * Preparation for unload, should it be needed on an error
	 * based exit.
	 */
	unload_parms.flags = VXT_CARD_DEL_BUS_SKIP_NOTIFY;
	/*
	 * The driver has stated initialization of the vxt bus
	 * In this implementation that means the setup and use of
	 * direct kernel shared memory, doorbell events, and 
	 * interrupts.  Below we create two file descriptors to
	 * serve as signal wait points for the Vxt Kernel module
	 * configuration and VxT device signal activities.  The address
	 * and data fields will be passed to the guest driver so
	 * that a write to the doorbell register of the proper
	 * ID will result in config or VxT device signal.
	 */

	vxt_ctrlr->sig_irq_fd = eventfd(0,0);
	if (vxt_ctrlr->sig_irq_fd < 0) {
		return VIRTIO_VXT_RSRCS;
	}
	printf("virtio_vxt_init_com_channel: sig_irq_fd = 0x%x\n",
	       vxt_ctrlr->sig_irq_fd);
	

	vxt_ctrlr->cfg_irq_fd = eventfd(0,0);
	if (vxt_ctrlr->cfg_irq_fd < 0) {
		close(vxt_ctrlr->sig_irq_fd);
		return VIRTIO_VXT_RSRCS;
	}
	printf("virtio_vxt_init_com_channel: sig_irq_fd = 0x%x\n",
	       vxt_ctrlr->cfg_irq_fd);

	vxt_ctrlr->sig_db = eventfd(0, 0);
	if (vxt_ctrlr->sig_db < 0) {
		close(vxt_ctrlr->sig_irq_fd);
		close(vxt_ctrlr->cfg_irq_fd);
		return VIRTIO_VXT_RSRCS;
	}
	vxt_ctrlr->cfg_db = eventfd(0,0);
	if (vxt_ctrlr->cfg_db < 0) {
		close(vxt_ctrlr->sig_irq_fd);
		close(vxt_ctrlr->cfg_irq_fd);
		close(vxt_ctrlr->sig_db);
		return VIRTIO_VXT_RSRCS;
	}
	ret = vxt_ctrlr->vdev.binding->queuefd(vxt_ctrlr->vdev.binding_opaque,
	                                       VXT_BUS_CONFIG_REG,
	                                       vxt_ctrlr->cfg_db);
        if (ret < 0) {
		close(vxt_ctrlr->sig_db);
		close(vxt_ctrlr->cfg_db);
		close(vxt_ctrlr->sig_irq_fd);
		close(vxt_ctrlr->cfg_irq_fd);
		return VIRTIO_VXT_RSRCS;
	}
	ret = vxt_ctrlr->vdev.binding->queuefd(vxt_ctrlr->vdev.binding_opaque,
	                                       VXT_BUS_DEV_SIG_REG,
	                                       vxt_ctrlr->sig_db);
        if (ret < 0) {
		/*
		 * Note:  There is no need to unbind the eventfd at the
		 * kernel level.  The act of closing the file descriptor
		 * causes a NOHUP in the poll action.  The kernel level
		 * handler then releases its resources.
		 */
		close(vxt_ctrlr->sig_db);
		close(vxt_ctrlr->cfg_db);
		close(vxt_ctrlr->sig_irq_fd);
		close(vxt_ctrlr->cfg_irq_fd);
		return VIRTIO_VXT_RSRCS;
	}


	/*
	 * During initialization, we asked for 4 interrupt/queue
	 * objects. ie. virtio queues.  Two of these are set up
	 * to allow for interrupts generated within the base Linux
	 * VxT module to be directed to handlers in the guest VxT
	 * driver.  The vectors are matched with the queues at the
	 * time of bus discovery/device initialization in the guest
	 * (see VIRTIO_MSI_QUEUE_VECTOR action associated with
	 * virtio-pci configuration register writes)
	 *
	 * We now expect that the vectors have been assigned, we
	 * need to retrieve them so that we may make them 
	 * available to the VxT Linux base kernel module and the
	 * guest driver.
	 */

	bus_cfg_queue = vxt_ctrlr->vdev.vq + 2;
	bus_sig_queue = vxt_ctrlr->vdev.vq + 3;
	vxt_ctrlr->sig_entry.gsi = bus_sig_queue->vector;
	vxt_ctrlr->cfg_entry.gsi = bus_cfg_queue->vector;
	printf("virtio_vxt_init_com_channel: Guest IRQ's, "
	       "sig_entry vector 0x%x, cfg_entry vector 0x%x\n",
	       vxt_ctrlr->sig_entry.gsi, vxt_ctrlr->cfg_entry.gsi);
	

	/*
	 * note: our vxt_ctrlr->sig/cfg_entry.gsi fields are actually the
	 * queue vectors, they are used below to find the real gsi's.
	 * These are passed on the irqfd call down to the Virtio kernel
	 * module where a virtual device is created.
	 */
	ret = vxt_ctrlr->vdev.binding->irqfd(vxt_ctrlr->vdev.binding_opaque,
	                                     vxt_ctrlr->sig_entry.gsi,
	                                     vxt_ctrlr->sig_irq_fd);
        if (ret < 0) {
		/*
		 * Note:  There is no need to unbind the eventfd at the
		 * kernel level.  The act of closing the file descriptor
		 * causes a NOHUP in the poll action.  The kernel level
		 * handler then releases its resources.
		 */
		close(vxt_ctrlr->sig_db);
		close(vxt_ctrlr->cfg_db);
		close(vxt_ctrlr->sig_irq_fd);
		close(vxt_ctrlr->cfg_irq_fd);
		return VIRTIO_VXT_RSRCS;
	}

	ret = vxt_ctrlr->vdev.binding->irqfd(vxt_ctrlr->vdev.binding_opaque,
	                                     vxt_ctrlr->cfg_entry.gsi,
	                                     vxt_ctrlr->cfg_irq_fd);
        if (ret < 0) {
		/*
		 * Note:  There is no need to unbind the eventfd at the
		 * kernel level.  The act of closing the file descriptor
		 * causes a NOHUP in the poll action.  The kernel level
		 * handler then releases its resources.
		 */
		close(vxt_ctrlr->sig_db);
		close(vxt_ctrlr->cfg_db);
		close(vxt_ctrlr->sig_irq_fd);
		close(vxt_ctrlr->cfg_irq_fd);
		return VIRTIO_VXT_RSRCS;
	}


	/*
	 * In order for our IRQ assertions to make it to their
	 * ultimate callbacks in the VxT guest driver, they will
	 * have to be processed by the Virtio_PCI handler.  The
	 * handler will only pass the interrupt through if it 
	 * believes there is data on the queue.  Put some
	 * dummy data on the queue.
	 */
	virtio_vxt_send_dummy_response(vxt_ctrlr,
	                               vxt_ctrlr->vxt_bus_cfg_dummy);
	virtio_vxt_send_dummy_response(vxt_ctrlr,
	                               vxt_ctrlr->vxt_bus_sig_dummy);



	/*
	 * first_cpu will give us access to the kvm_fd.  This is needed
 	 * to access the exported kvm module functions from the kernel
	 * such as gfn_to_pfn.  We send the kvm_fd on our registration
	 * ioctl. first_cpu->
 	 * NOTE:  IMPORTANT TO REMEMBER - the file descriptor is based
	 * on the "current" context.  This is why we make this call here
	 * and why we will de-reference to the file structure on the
	 * ioctl call to the VxT module rather than waiting for its
	 * use which may occur in different contexts. kernel module will
	 * use fget/fput.
	 * 
	 */


	/*
	 * Call the underlying host VxT Bus/ctrlr driver to get the device
	 * config queue addresses.
	 */
	vxt_card_bus = open("/dev/vxtcard-config", O_RDWR);
	if (vxt_card_bus == -1) {
		fprintf(stderr,
		        "Unable to open the control port for VxT card, %s\n",
		        strerror(errno));
		close(vxt_ctrlr->sig_irq_fd);
		close(vxt_ctrlr->cfg_irq_fd);
		return 1;
	}

	vxt_ctrlr->card_fd = vxt_card_bus;


	/*
	 * Translation of the guest physical address to the host virtual
	 * address is done in the VxT module.  The guest has provided
	 * us with a general physical memory range.  The shared memory
 	 * will be accomplished through an overlayed linux vma.  As such
	 * there is no direct kvm user task space involvement.
	 */


	register_parms.suspended = vxt_ctrlr->suspended;
	register_parms.guest_inbuf_addr = vxt_ctrlr->cfg.guest_inbuf_pa;
	register_parms.inbuf_size = vxt_ctrlr->cfg.inbuf_size;
	register_parms.guest_outbuf_addr = vxt_ctrlr->cfg.guest_outbuf_pa;
	register_parms.outbuf_size = vxt_ctrlr->cfg.outbuf_size;
	register_parms.guest_sigbuf_addr = vxt_ctrlr->cfg.guest_sigbuf_pa;
	register_parms.sigbuf_size = vxt_ctrlr->cfg.sigbuf_size;
	register_parms.cfg_irq_fd = vxt_ctrlr->cfg_irq_fd;
	register_parms.sig_irq_fd = vxt_ctrlr->sig_irq_fd;
#ifdef notdefcdy
This section was confused:
does this need to be used on the virtio configuration reply?????
	/*
	 * We pass zero to communicate to the guest that it should
	 * use the virtio_pci:vp_notify mechanism.  This is tied to
	 * the virt queues and the ioaddr base for the doorbell is
	 * embedded within the function. The VXT_BUS_CONFIG_REG and
	 * VXT_BUS_DEV_SIG_REG, when passed on vp_notify will be
	 * sufficient.
	 */
	register_parms.cfg_bell = 0;
	register_parms.sig_bell = 0;
#endif
	register_parms.cfg_bell = vxt_ctrlr->cfg_db;
	register_parms.sig_bell = vxt_ctrlr->sig_db;
	register_parms.vmfd = ((KVMState *)(env->kvm_state))->vmfd;
	sprintf(register_parms.guest_uuid, UUID_FMT,
	        qemu_uuid[0], qemu_uuid[1], qemu_uuid[2], qemu_uuid[3],
	        qemu_uuid[4], qemu_uuid[5], qemu_uuid[6], qemu_uuid[7],
	        qemu_uuid[8], qemu_uuid[9], qemu_uuid[10], qemu_uuid[11],
	        qemu_uuid[12], qemu_uuid[13], qemu_uuid[14], qemu_uuid[15]);
	/*
	 * Save the guest uuid in the vxt controller structure
	 * for future use
	 */
	strncpy(vxt_ctrlr->cfg.guest_uuid,
	        register_parms.guest_uuid, MAX_VXT_UUID);

	fprintf(stderr, "CDY: Guest UUID - %s\n", register_parms.guest_uuid);


	/*
	 * Call will register the irq's and doorbells as well as create
	 * shared memory objects for the config buffers.  We make our
	 * call through the KVM file descriptor instead of the VxT
	 * one so that we can associated the KVM kernel module KVM state
 	 * with the VxT controller.
	 */

	ret = ioctl(vxt_ctrlr->card_fd, KVM_PLUG_VXT_BUS, &register_parms);
	if (ret != 0) {
		fprintf(stderr, "KVM_PLUG_VXT_BUS failed\n");
		close(vxt_ctrlr->sig_irq_fd);
		close(vxt_ctrlr->cfg_irq_fd);
		return 1;
	}

	fprintf(stderr,"virtio_vxt_init_com_channel: Returned values "
	        "from KVM_PLUG_VXT_BUS host_inbuf_va 0x%llx, "
	        "host_outbuf_va 0x%llx, host_sigbuf_va 0x%llx, "
	        "controller name %s\n",
	        register_parms.host_inbuf_addr, register_parms.host_outbuf_addr,
	        register_parms.host_sigbuf_addr, register_parms.ctrlr_name);

	/*
	 * The host virtual addresses of the VxT controller communications
	 * buffers are kept for qemu level debugging only
	 */

	vxt_ctrlr->cfg.host_inbuf_va = register_parms.host_inbuf_addr;
	vxt_ctrlr->cfg.host_outbuf_va = register_parms.host_outbuf_addr;
	vxt_ctrlr->cfg.host_sigbuf_va = register_parms.host_sigbuf_addr;
	sprintf(vxt_ctrlr->cfg.ctrlr_name,
	        "/dev/%s", register_parms.ctrlr_name);
	vxt_ctrlr->cfg.guest_name = register_parms.guest_name;
	fprintf(stderr, "virtio_vxt_init_com_channel: controller name %s\n",
	        vxt_ctrlr->cfg.ctrlr_name);


	return 0;

}


/*
 *
 * virtio_vxt_shutdown_com_channel:
 *
 * Call the back-end driver, i.e. the Linux base kernel VxT Module
 * and ask it to release all the resources associated with a particular
 * vxt controller device.
 *
 * Note: It is not necessary to do a specific deassign on the eventfd
 * kernel resources as a close of the associated fd will resultin
 * a POLL SIGHUP and consequent release of the kernel resources.
 *
 * Returns:
 *
 */

static int
virtio_vxt_shutdown_com_channel(virtio_vxt_t *vxt_ctrlr, int driver_dead)
{
	kvm_vxtcard_disconnect_bus_parms_t disconnect_parms;
	kvm_vxt_card_del_bus_parms_t unload_parms;
	int                          ret;


	printf("virtio_vxt_shutdown_com_channel: called, bus %p\n", vxt_ctrlr);



	close(vxt_ctrlr->sig_db);
	close(vxt_ctrlr->cfg_db);
	if (driver_dead) {
		/*
		 * Don't send device and bus shutdown config messages
		 * to a non-functional device driver.
		 */
		unload_parms.flags = VXT_CARD_DEL_BUS_SKIP_NOTIFY;
	} else {
		unload_parms.flags = 0;
	}

	disconnect_parms.flags = 0;
	if (vxt_ctrlr->card_fd) {
		ret = ioctl(vxt_ctrlr->card_fd, 
		            KVM_DISCONNECT_VXT_BUS, &disconnect_parms);
		if (ret != 0) {
			fprintf(stderr, "KVM_DISCONNECT_VXT_BUS failed\n");
			return 0;
		}

		ret = ioctl(vxt_ctrlr->card_fd,
		            KVM_UNPLUG_VXT_BUS, &unload_parms);
		if (ret != 0) {
			fprintf(stderr, "KVM_UNPLUG_VXT_BUS failed\n");
			return 0;
		}
	}


	/*
	 * Unmap the VxT buffers that were provided to the guest
	 * No other action is necessary here as the driver is already gone
	 * or we have already forced it down.  Note: The actions below will
	 * result in garbage being found at the old shared queue locations.
	 * This action should only be taken when the device driver has been
	 * properly informed of the impending loss or the device driver is
	 * known to be gone or non-responding.
	 */
	if (vxt_ctrlr->cfg.host_inbuf_va != 0) {
		munmap((void *)(long)vxt_ctrlr->cfg.host_inbuf_va,
		               vxt_ctrlr->cfg.inbuf_size);
	}


	if (vxt_ctrlr->cfg.host_outbuf_va != 0) {
		munmap((void *)(long)vxt_ctrlr->cfg.host_outbuf_va,
		        vxt_ctrlr->cfg.outbuf_size);
	}


	if (vxt_ctrlr->cfg.host_sigbuf_va != 0) {
		munmap((void *)(long)vxt_ctrlr->cfg.host_sigbuf_va,
		        vxt_ctrlr->cfg.sigbuf_size);
	}

	/*
	 * Close the file descriptor we use for ioctls to the underlying
	 * VxT Bus Kernel Module
	 */
	close(vxt_ctrlr->card_fd);
	return 0;
}


/*
 * 
 * virtio_vxt_bus_config:
 *
 * virtio_vxt_bus_config consults the VxT configuration structure
 * associated with the vdev.  This structure is set up with the
 * vxt controller configuration queue, and signal concentrator
 * resources needed by the VxT controller front end to talk
 * with the back-end during controller setup and tear-down.
 *
 * Note: the callback that virtio_vxt_bus_config is associated with,
 * "vdev.get_config", is also used as a synchronization point for
 * configuration state update.  At the moment, all VxT Virtio device
 * state is brought up to date at the moment of state change.  However,
 * if it should turn out that some element of VxT bus state cannot
 * be continuously relevent, this call will be used to freshen the state.
 *
 * Returns:
 *
 *		None
 *
 */

static void virtio_vxt_bus_config(VirtIODevice *vdev, uint8_t *config)
{
	virtio_vxt_t *ctrlr = vdev_parent(vdev);
	struct virtio_vxt_config *ctrlr_cfg;


	ctrlr_cfg = (struct virtio_vxt_config *)config;

	*ctrlr_cfg = ctrlr->cfg;

	return;

}



/*
 * virtio_vxt_bus_features:
 * Returns a list of flags indicating optional capabilities
 * and differentiating characteristics of vxt controllers
 *
 * No options at this time, Returns 0
 *
 */

static uint32_t virtio_vxt_bus_features(VirtIODevice *vdev)
{
	return 0;
}


static void
virtio_vxt_save(QEMUFile *f, void *opaque)
{
	virtio_vxt_t *vxt_ctrlr = (virtio_vxt_t *)opaque;

	/*
	 * Save the vdev state, including ring buffers
	 */
	virtio_save(&vxt_ctrlr->vdev, f);

	/*
	 * Save the VxT controller state
	 */
	qemu_put_buffer(f, (uint8_t *)&(vxt_ctrlr->driver_state),
	                (sizeof(vxt_ctrlr) + (unsigned long)vxt_ctrlr) - 
	                (unsigned long)&(((virtio_vxt_t *)0)->driver_state));

/* HAVE TO ASSUME KVM has already suspended guest ?
	if(vxt_ctrlr->driver_state == VXT_RUNNING 
	   || vxt_ctrlr->driver_state == VXT_CONFIGURED) {
		vxt_ctrlr->driver_state = VXT_SUSPENDING;
		virtio_vxt_cfgd_response(vxtctrlr, 0);
	}
*/
	/*
	 * Since we cannot apriori tell the guest we are suspending
	 * we must save the contents of the VxT bus shared memory queues
	 * that we have mapped.
	 * Its a curious thing.  The saving of the contents of guest memory
	 * is done by copying from the virtual memory mapped region of the
	 * host.  We cannot know when guest memory will be saved i.e. if the
	 * device is saved first or memory is saved first, so we keep around
	 * our mapped buffers till the last.  Likewise with restore, so
	 * we update our contents after we remap our bus shared memory.
	 *
	 * The VxT devices are different.  The guest will not know what
	 * hit until after it is re-started.  At this time, it will be
	 * running with the contents of VxT device shared memory buffers
	 * but not the shared memory mappings.  (They will have been
	 * copied out as part of the Ram checkpoint image)
	 * This will allow the guest to continue operation without 
	 * the risk of panic if it was accessing these buffers at the time.
	 *  Device resets sent to each of the VxT devices will allow
	 * them to shutdown old activity and recover by reconnecting to
	 * the underlying VxT device on the VxT bus.
	 */
	/*
	 * save the VxT bus config buffer contents
	 */
	qemu_put_buffer(f, (uint8_t *)(long)vxt_ctrlr->cfg.host_inbuf_va,
	                vxt_ctrlr->cfg.inbuf_size);
	
	qemu_put_buffer(f, (uint8_t *)(long)vxt_ctrlr->cfg.host_outbuf_va,
	                vxt_ctrlr->cfg.outbuf_size);
	/*
	 * save the VxT bus sigbuf contents
	 */
	qemu_put_buffer(f, (uint8_t *)(long)vxt_ctrlr->cfg.host_sigbuf_va,
	                vxt_ctrlr->cfg.sigbuf_size);
	/*
	 * Must call the VxT module to tell to suspend operations
	 * We must assume that the guest is already suspended.  However,
	 * individual, hardware backed devices may need to take action
	 * to preserve coherent state.  The VxT bus signals all of the
	 * active VxT devices and thus device specific actions can be
	 * taken.  Note: VxT requires that all devices be able to
 	 * re-construct state.  Therefore, no device state is recovered here.
	 * If a specific device must store state for its driver, it is
	 * done through an external database that is outside the scope
	 * of the VxT architecture.
	 * We will find it necessary to save and then seed the config 
	 * and signal shared memory queues.  If the guest is suspended and
	 * restarted in the middle of an access here, the results would be
	 * undefined without a rewrite of existing data.  However, this
	 * can be accomplished without resorting to the VxT kernel module.
	 */
/*
	ret = ioctl(vxt_ctrlr->card_fd, VXT_GUEST_BUS_SUSPEND, &suspend_parms);
*/
	close(vxt_ctrlr->card_fd);
}



static int
virtio_vxt_load(QEMUFile *f, void *opaque, int version_id)
{
	virtio_vxt_t *vxt_ctrlr = (virtio_vxt_t *)opaque;
	int          ret;


	if (version_id != VIRTIO_VXT_VERSION) {
		return -EINVAL;
	}

	/*
	 * recover the vdev state including
	 * ring buffer contents
	 */
	virtio_load(&vxt_ctrlr->vdev, f);

	/*
	 * Restore the VxT controller state
	 */
	qemu_get_buffer(f, (uint8_t *)&(vxt_ctrlr->driver_state),
	                (sizeof(vxt_ctrlr) + (unsigned long)vxt_ctrlr) - 
	                (unsigned long)&(((virtio_vxt_t *)0)->driver_state));
fprintf(stderr, "CDY virtio_vxt_load: after guest buffer get\n");


	/*
	 * Ask the VxT kernel module to re-map the configuration
	 * and signaling buffers.
	 */
	vxt_ctrlr->suspended = TRUE;
	ret = virtio_vxt_init_com_channel(vxt_ctrlr);
	if (ret) {
		return ret;
	}
	/*
	 * We have just mapped are shared memory queues on top of the
	 * guest physical pages and our host virtual address ranges
	 * This has the effect of blowing away the saved content in
	 * these pages.  We must therefore push our saved state manually.
	 */
	/*
	 * recover the VxT bus config buffer contents
	 */
	qemu_get_buffer(f, (uint8_t *)(long)vxt_ctrlr->cfg.host_inbuf_va,
	                vxt_ctrlr->cfg.inbuf_size);
	
	qemu_get_buffer(f, (uint8_t *)(long)vxt_ctrlr->cfg.host_outbuf_va,
	                vxt_ctrlr->cfg.outbuf_size);
	/*
	 * recover the VxT bus sigbuf contents
	 */
	qemu_get_buffer(f, (uint8_t *)(long)vxt_ctrlr->cfg.host_sigbuf_va,
	                vxt_ctrlr->cfg.sigbuf_size);
	
	vxt_ctrlr->suspended = FALSE;

	return 0;
}


/*
 *
 * virtio_vxt_send_response:
 *
 * Note: we assume that there is no pending response and overwrite one
 * if it is present.
 *
 */

static int
virtio_vxt_send_response(virtio_vxt_t *vxtctrlr, vxt_virtio_cfg_t *response)
{
	VirtQueueElement elem;
	int              send_size;
	int              len;
	int              offset;
	int              first_time;
	int              i;

fprintf(stderr, "CDY virtio_vxt_send_response, queues initialized? %d\n", 
        vxtctrlr->vqs_initialized);
fprintf(stderr, "virtio_vxt_send_response: 0x%x\n", response->current_state);
	first_time = 1;
	if(vxtctrlr->vqs_initialized == 0) {
		return 0;
	}
	if (!virtqueue_pop(vxtctrlr->send_vq, &elem)) {
		/*
		 * sleep and wait for buffer available event
		 * from guest.  Ping the guest to remind 
		 * that we need a buffer.
		 */
fprintf(stderr, "CDY virtio_vxt_send_response, virtqueue_pop failed\n");
		vxtctrlr->response_pending = TRUE;
		vxtctrlr->response = *response;  
		if (first_time) {
			virtio_notify(&vxtctrlr->vdev, vxtctrlr->send_vq);
		}
		return 1;
	}
	first_time = 0;
	/*
	 * Check out the size of the sg element we have acquired.
	 * The sg buffer may be segmented but it must be at least
	 * big enough to accomodate us or we will reject it.  
	 */
	i = 0;
	offset = 0;
	send_size = sizeof(vxt_virtio_cfg_t);
	while (send_size) {
fprintf(stderr, "CDY virtio_vxt_send_response, response buffer size 0x%x\n",
        elem.in_sg[i].iov_len);
		if (send_size < elem.in_sg[i].iov_len) {
			len = send_size;
		} else {
			len = elem.in_sg[i].iov_len;
		}
		memcpy(elem.in_sg[i].iov_base,
		       ((char *)response) + offset, len);
		offset += len;
		send_size -= len;
		i++;
	}
	if (send_size != 0) {
		/*
		 * Guest did not provide a big enough buffer.
		 * Push the pending response and signal the other side
		 */
		fprintf(stderr, "Insufficent space for response\n");
		vxtctrlr->response_pending = TRUE;
		vxtctrlr->response = *response;  
		virtqueue_push(vxtctrlr->send_vq, &elem,
		               sizeof(response)-send_size);
fprintf(stderr, "CDY virtio_vxt_send_response, guest buffer too small\n");
		virtio_notify(&vxtctrlr->vdev, vxtctrlr->send_vq);
		return 1;
	}
		
fprintf(stderr, "CDY virtio_vxt_send_response, virtqueue_push\n");
	virtqueue_push(vxtctrlr->send_vq, &elem, sizeof(response));
	virtio_notify(&vxtctrlr->vdev, vxtctrlr->send_vq);
	return 0;
}


/*
 *
 * virtio_vxt_cfgd_response:
 *
 * Sets up the response based on the state the vxtctrlr is in
 * and whether or not the caller has signalled an error.
 */

static int
virtio_vxt_cfgd_response(virtio_vxt_t *vxtctrlr, int error)
{
	vxt_virtio_cfg_t response;

fprintf(stderr, "CDY virtio_vxt_cfgd_response\n");
	switch ((int)vxtctrlr->driver_state) {
	case VXT_CONFIGURED:
	{
		response.version = VIRTIO_VXT_VERSION;
		response.current_state = VXT_CONFIGURED;
		if (error) {
			response.provisional_state = VXT_CFG_ERROR;
			response.cmd.error.error = error;
			break;
		}
		response.provisional_state = VXT_CONFIGURED;
		/*
		 * Front end will know to match the "configuring" union
		 * member with this provisional state.
		 */
		response.cmd.configuring.cfg_irq = vxtctrlr->cfg_entry.gsi;
		response.cmd.configuring.sig_irq = vxtctrlr->sig_entry.gsi;
		strncpy(response.cmd.configuring.guest_uuid,
		        vxtctrlr->cfg.guest_uuid, MAX_VXT_UUID);
		response.cmd.configuring.guest_name = vxtctrlr->cfg.guest_name;
		response.cmd.configuring.connecting_status = VXT_CONFIGURED;
		break;
	}
        case VXT_RUNNING:
		response.version = VIRTIO_VXT_VERSION;
		response.current_state = VXT_RUNNING;
		response.provisional_state = VXT_RUNNING;
		if (error) {
			response.provisional_state = VXT_CFG_ERROR;
			response.cmd.error.error = error;
			break;
		}
		response.cmd.running.configured_status = TRUE;
		break;
        case VXT_SUSPENDING:
		response.version = VIRTIO_VXT_VERSION;
		response.current_state = VXT_SUSPENDING;
		response.provisional_state = VXT_SUSPENDING;
		if (error) {
			response.provisional_state = VXT_CFG_ERROR;
			response.cmd.error.error = error;
			break;
		}
		break;
        case VXT_REVOKING:
		response.version = VIRTIO_VXT_VERSION;
		response.current_state = VXT_REVOKING;
		response.provisional_state = VXT_REVOKING;
		if (error) {
			response.provisional_state = VXT_CFG_ERROR;
			response.cmd.error.error = error;
			break;
		}
		break;
/*
	case VXT_WINDING_DOWN:
		response.version = VIRTIO_VXT_VERSION;
		response.current_state = VXT_SHUTDOWN;
		response.provisional_state = VXT_SHUTDOWN;
		if (error) {
			response.provisional_state = VXT_CFG_ERROR;
			response.cmd.error.error = error;
		}
*/
        case VXT_SHUTDOWN:   
		if (error) {
			response.provisional_state = VXT_CFG_ERROR;
			response.cmd.error.error = error;
			break;
		}
		break;
        case VXT_RESET:   
		response.version = VIRTIO_VXT_VERSION;
		response.current_state = VXT_RESET;
		response.provisional_state = VXT_PROVISIONED;
		if (error) {
			response.provisional_state = VXT_CFG_ERROR;
			response.cmd.error.error = error;
			break;
		}
		break;
	default:
		/*
		 * Respond with error status befitting the current state
		 * of the  vxtctrlr
		 */
		response.version = VIRTIO_VXT_VERSION;
		response.current_state = vxtctrlr->driver_state;
		response.provisional_state = VXT_CFG_ERROR;
		response.cmd.error.error = VXT_UNKNOWN_STATE;
		
	}

	return virtio_vxt_send_response(vxtctrlr, &response);
}


/*
 *
 * virtio_vxt_reset:
 *
 * Note virtio_vxt_reset is called at any time, including during
 * the first steps of initialization.  Therefore we must be mindeful
 * of the state of our queues before calling vxt_cfgd_response
 *
 */

static void
virtio_vxt_reset(VirtIODevice *vdev)
{
	virtio_vxt_t          *vxt_ctrlr;

	vxt_ctrlr = vdev_parent(vdev);

	/*
	 * Same actions as finding that the device driver
	 * is gone ie. QUITTED - as in "quitted the field"
	 * Except that the driver at the other end may still
	 * be responding, try sending shutdown config messages
	 * from the UNPLUG call,  (second parameter 0)
	 */
fprintf(stderr, "CDY virtio_vxt_reset\n");
	virtio_vxt_shutdown_com_channel(vxt_ctrlr, 0);
	vxt_ctrlr->driver_state = VXT_RESET;
	/*
	 * Unlike the QUITTED case, we must tell the device
	 * driver that we initialized.  i.e. send a VXT_RESET
	 * message.
	 */
	virtio_vxt_cfgd_response(vxt_ctrlr, 0);
	vxt_ctrlr->driver_state = VXT_PROVISIONED;
fprintf(stderr, "CDY virtio_vxt_reset completed\n");
}


/*
 *
 * vxt_bus_cfg_dummy_handler:
 *
 * Should never be called, the guest never acts on this queue it
 * is carried along with the IRQ that we need.  We must handle it becuase
 * the queue is wrapped over the IRQ.
 *
 */

static void
vxt_bus_cfg_dummy_handler(VirtIODevice *vdev, VirtQueue *vq)
{
	printf("vxt_bus_cfg_dummy_handler: "
	       "Unexpected invocation\n");
}

static void
vxt_bus_sig_dummy_handler(VirtIODevice *vdev, VirtQueue *vq)
{
	printf("vxt_bus_sig_dummy_handler: "
	       "Unexpected invocation\n");
}

/*
 *
 * vxt_configure_update_request:
 *
 * Callback for handling virtio ring queue actions
 *
 * The VxT virtio driver in the guest passes fixed size requests back
 * to this handler for the device emulation layer.  No assumptions are
 * made about the intermediate transport, except that the data is presented
 * all, or nothing,  i.e. no partial sends.  It is assumed that the virtio
 * ring system is free to split requests across separate s/g elements and
 * may contain more than one request in a single element.
 *
 */

static void
vxt_configure_update_request(VirtIODevice *vdev, VirtQueue *vq)
{
	vxt_virtio_cfg_t      request;
	VirtQueueElement      elem;
	virtio_vxt_t          *vxt_ctrlr;
	size_t                send_size;
	size_t                req_off;
	vxt_sg_read_object_t  ro;
	int                   ret;

fprintf(stderr, "CDY vxt_configure_update_request\n");
	vxt_ctrlr = vdev_parent(vdev);
	vxt_ctrlr->vqs_initialized = 1;

	if (vxt_ctrlr->response_pending) {
		/*
		 * HMM, we are supposed to be in a lock-step protocol
		 * but our partner has sent us a request without getting
		 * a response.  There are certain circumstances when we
		 * send an unsolicited message so we will not throw 
		 * anything out.  However, we will not respond here until
		 * the guest gives us a buffer so we can give them a 
		 * message.  We will just ping the send channel again
		 * here and leave.  The guest will have to provide a 
		 * buffer, ping our send and re-ping our receive queue.
		 */
		virtio_notify(&vxt_ctrlr->vdev, vxt_ctrlr->send_vq);
		return;
	}

	req_off = 0;
	while (virtqueue_pop(vq, &elem)) {
fprintf(stderr, "CDY vxt_configure_update_request:found something, out_sg 0x%p, out_num %d, in_sg 0x%p, in_num %d\n", elem.out_sg, elem.out_num, elem.in_sg, elem.in_num);
		vxt_sg_read_object_init(&ro, elem.out_sg, elem.out_num);
		
		while (req_off < sizeof(request)) {
more_work:
			send_size = 
			   vxt_sg_object_read(
			      ((uint8_t *)&request) + req_off, &ro,
			      sizeof(request) - req_off);
fprintf(stderr, "CDY vxt_configure_update_request:guest_request_size 0x%x\n", send_size);


			req_off += send_size;
fprintf(stderr, "CDY vxt_configure_update_request:req_off = %d, request_size = %d\n", req_off, sizeof(request));
			if (req_off == sizeof(request)) {
			switch (request.provisional_state) {
			   case VXT_CONNECTING:
fprintf(stderr, "CDY vxt_configure_update_request:move to REQUEST_CONNECTING\n");
	
				if ((request.current_state != VXT_PROVISIONED)
				    && (vxt_ctrlr->driver_state !=
				        request.current_state)) {
					/*
					 * Ignore Request: send Failed response
					 */
					virtio_vxt_cfgd_response(
					   vxt_ctrlr, VXT_STATE_MISMATCH);
				
					break;
				}
				vxt_ctrlr->cfg.guest_inbuf_pa =
				   request.cmd.connecting.cfg_in;
				vxt_ctrlr->cfg.guest_outbuf_pa =
				   request.cmd.connecting.cfg_out;
				vxt_ctrlr->cfg.guest_sigbuf_pa =
				   request.cmd.connecting.signal;
				vxt_ctrlr->cfg.inbuf_size =
				   request.cmd.connecting.cfg_in_size;
				vxt_ctrlr->cfg.outbuf_size =
				   request.cmd.connecting.cfg_out_size;
				vxt_ctrlr->cfg.sigbuf_size =
				   request.cmd.connecting.signal_buf_size;
				ret = virtio_vxt_init_com_channel(vxt_ctrlr);
				if (ret == VIRTIO_VXT_SUCCESS) {
					/* virtio ring buf msg */
					vxt_ctrlr->driver_state =
					   VXT_CONFIGURED;
					virtio_vxt_cfgd_response(vxt_ctrlr, 0);
				} else {
					/* virtio ring buf msg */
					virtio_vxt_cfgd_response(
					   vxt_ctrlr, VXT_CFG_ERROR);
				}
				break;
			   case VXT_CONFIGURED:
fprintf(stderr, "CDY vxt_configure_update_request:move to VXT_CONFIGURED\n");
				/*
				 * VXT_CONFIGURED:  Guest is rejecting our
				 * resource offer
				 */
				break;
			   case VXT_INITIALIZED:
			   {
fprintf(stderr, "CDY vxt_configure_update_request:move to VXT_INITIALIZED\n");
				kvm_vxt_card_del_bus_parms_t connect_parms;
				/*
				 * VXT_INITIALIZED: Guest has accepted our
				 * resource offer
				 */
				if ((request.current_state != VXT_PROVISIONED)
				    && (vxt_ctrlr->driver_state !=
				        request.current_state)) {
					/*
					 * Ignore Request: send Failed response
					 */
fprintf(stderr, "CDY vxt_configure_update_request:STATE MISMATCH\n");
					virtio_vxt_cfgd_response(
					   vxt_ctrlr, VXT_STATE_MISMATCH);
				
					break;
				}
				vxt_ctrlr->driver_state = VXT_RUNNING;
				/*
				 * A separate connect action in handshake
				 * to make certain guest is ready for traffic
				 */
				connect_parms.flags = 0;
				ret = ioctl(vxt_ctrlr->card_fd,
				            KVM_CONNECT_VXT_BUS,
				            &connect_parms);
				
				virtio_vxt_cfgd_response(vxt_ctrlr, 0);
				break;
			   }
			   case VXT_SUSPENDING:
fprintf(stderr, "CDY vxt_configure_update_request:move to VXT_SUSPENDING\n");
				/*
				 * Not a guest initiated state transition
				 */
				break;
#ifdef notdefcdy 
update header file
			   case VXT_SUSPENDED:
fprintf(stderr, "CDY vxt_configure_update_request:move to VXT_SUSPENDED");
				break;
			   case VXT_WINDING_DOWN:
fprintf(stderr, "CDY vxt_configure_update_request:move to VXT_WINDING_DOWN");
				/*
				 * The guest is dumping its context at our
				 * request.  i.e. a revoked.
				 * The VxT devices are being flushed.
				 * After we detect that they are all down
				 * send a VXT_SHUTDOWN, telling the guest
				 * to go ahead and dump its context.  We will
				 * see the result as a VXT_QUITTED.
				 */
/*
 * Note, we need a timeout and we should set a conditional in the vxt
 * controller.  When either the timeout hits or our last vxt device is
 * shutdown do the following:
 */
vxtctrlr->driver_state = VXT_WINDING_DOWN;
virtio_vxt_cfgd_response(vxtctrlr, 0);
virtio_vxt_shutdown_com_channel(vxt_ctrlr, 1);
vxtctrlr->driver_state = VXT_SHUTDOWN;
#endif /* update header file */
			   case VXT_QUITTED:
fprintf(stderr, "CDY vxt_configure_update_request:move to VXT_QUITTED\n");
				/*
				 * The guest driver is gone. If we are
				 * in any state other than an error state
				 * dump any resources associated with the
				 * paired device and go back to the
				 * "PROVISIONED" state.
				 */
				if (vxt_ctrlr->driver_state != VXT_CFG_ERROR) {
					virtio_vxt_shutdown_com_channel(
					   vxt_ctrlr, 1);
					vxt_ctrlr->driver_state =
					   VXT_PROVISIONED;
				};
				break;
			   case VXT_REVOKING:
fprintf(stderr, "CDY vxt_configure_update_request:move to VXT_REVOKING\n");
				/*
				 * Not a guest initiated state transition
				 */
				break;
			   case VXT_SHUTDOWN:
fprintf(stderr, "CDY vxt_configure_update_request:move to VXT_SHUTDOWN\n");
				break;
			   default:
fprintf(stderr, "CDY vxt_configure_update_request:provisional state not recognized, %d\n", request.provisional_state);
				break;
			}
				break;  /* leave inner while loop */
			}
			if (send_size == 0) {
				break;
			}
		}
		if (req_off == sizeof(request)) {
			/*
			 * Somewhat subtle:  If we drop out of inner loop
			 * because we satisfied request size, we may still
			 * have something left in the elem buffer.  We must
			 * try memcpy_from_iovec one more time.  We will of
			 * course be starting on a new request.
			 *
			 * There are two other possibilities, we have stopped
			 * on a request boundary in which case req_off is zero
			 * or we have a partial request.  A partial request
			 * may be satisfied by the next elem so we do not
			 * flag it here.  We wait until we have popped the
			 * last elem to check that.  Hence we do not reset
			 * a partial req_off here.
			 */
			req_off = 0;
			goto more_work;
		}
		virtqueue_push(vq, &elem, 
		               vxt_sg_object_bytes_read(&ro));
		virtio_notify(vdev, vq);
	}
	if (req_off) {
		/* 
		 * A partial request?  Ignore
		 */
		printf("Vxt_configure_update_request: "
		       "malformed configuration request, "
		       "buffer truncated\n");
	}
}

/*
 *
 * vxt_push_configure_update:
 *
 * vxt_push_configure_update is the Virtio send queue callback
 * We utilize a request, response mechanism and so may have
 * a pending response, if so we handle it here.
 */

static void
vxt_push_configure_update(VirtIODevice *vdev, VirtQueue *vq)
{
	virtio_vxt_t     *vxt_ctrlr;
	vxt_virtio_cfg_t response;

	vxt_ctrlr = vdev_parent(vdev);
	if(vxt_ctrlr->response_pending == FALSE) {
		/*
		 * We've got a new buffer, but no one is waiting
		 * for it.
		 */
		return;
	}

	vxt_ctrlr->response_pending = FALSE;
	response = vxt_ctrlr->response;
	virtio_vxt_send_response(vxt_ctrlr, &response);
	return;
}


/*
 *
 * virtio_vxt_init:
 *
 */

VirtIODevice *
virtio_vxt_init(DeviceState *dev)
{
	virtio_vxt_t *vxt_ctrlr;

fprintf(stderr, "CDY virtio_vxt_init\n");
	vxt_ctrlr = (virtio_vxt_t *)
	                 virtio_common_init("virtio-vxt", VIRTIO_ID_VXT,
	                                    sizeof(struct virtio_vxt_config),
	                                    sizeof(virtio_vxt_t));

	if (!vxt_ctrlr) {
fprintf(stderr, "CDY virtio_vxt_init failed, no vxt_ctrlr\n");
		return NULL;
	}

	vxt_ctrlr->vqs_initialized = 0;
#ifdef UNWRAPPED_IRQ
	/*
	 * Get the guest level system interrupts for the VxT
	 * kernel level bus controller.
	 */
	vxt_ctrlr->sig_entry.gsi = kvm_get_irq_route_gsi(kvm_context);
	if (vxt_ctrlr->sig_entry.gsi < 0) {
fprintf(stderr, "CDY virtio_vxt_init failed, no sig_gsi\n");
		return NULL;
	}
	/* KVM_IRQFD_FLAG_DEASSIGN is used in an identical call when removing */
	{
	int fd;
	fd = kvm_irqfd(kvm_context, vxt_ctrlr->sig_entry.gsi, 0);
	}

        vxt_ctrlr->sig_entry.u.msi.address_lo = 0;
	vxt_ctrlr->sig_entry.u.msi.address_hi = 0;
	vxt_ctrlr->sig_entry.u.msi.data = 0;
	vxt_ctrlr->sig_entry.type = KVM_IRQ_ROUTING_MSI;
	vxt_ctrlr->card_fd = 0;

	kvm_add_routing_entry(kvm_context, &vxt_ctrlr->sig_entry);

	vxt_ctrlr->cfg_entry.gsi = kvm_get_irq_route_gsi(kvm_context);
	if (vxt_ctrlr->cfg_entry.gsi < 0) {
		kvm_del_routing_entry(kvm_context, &vxt_ctrlr->sig_entry);
fprintf(stderr, "CDY virtio_vxt_init failed, no cfg_gsi\n");
		return NULL;
	}

        vxt_ctrlr->cfg_entry.u.msi.address_lo = 0;
	vxt_ctrlr->cfg_entry.u.msi.address_hi = 0;
	vxt_ctrlr->cfg_entry.u.msi.data = 0;
	vxt_ctrlr->cfg_entry.type = KVM_IRQ_ROUTING_MSI;

	kvm_add_routing_entry(kvm_context, &vxt_ctrlr->cfg_entry);
#endif



	vxt_ctrlr->response_pending = FALSE;
	vxt_ctrlr->suspended = FALSE;

	/*
	 * Note we need 5 IRQs for our device.  This is because
	 * we must resort to a  model in PCI virt based on MSIX
	 * that will give an IRQ to each queue.  We will therefore
	 * have two for our read and write configuration queues and
	 * two for our VxT Bus configuration and device signal paths.
	 * The other IRQ is for the base device.
	 * The VxT Bus resources needed are only two IRQ's, however
	 * pci-virtio does not export a naked IRQ and instead wraps 
	 * it in it's queue primitive.  Further, the pci_virtio 
	 * model in the linux driver inserts itself on the interrupt
	 * path.  To overcome this we will have to initialize the 
	 * unused queues and place dummy elements on them so that
	 * the routine will pass the interrupts through to their
	 * ultimate handlers.
	 *
	 * Note: virtio-pci should export naked IRQ's if it intends
	 * to support the wrapping of hardware devices.  It is
	 * unreasonable to assume that the hardware supported queues
	 * will match virtio-pci forms.
	 */

	vxt_ctrlr->vdev.nvectors = 5;

	vxt_ctrlr->vdev.get_config = virtio_vxt_bus_config;
	vxt_ctrlr->vdev.get_features = virtio_vxt_bus_features;
	vxt_ctrlr->vdev.reset = virtio_vxt_reset;
	vxt_ctrlr->driver_state = VXT_PROVISIONED;
	vxt_ctrlr->vxt_controller = vxt_ctrlr;

	/*
	 * Set up the configuration queues.  virtio-vxt driver
	 * will use them to communicate with us, pushing us through
	 * the initialization of the VxT Bus
	 */

	vxt_ctrlr->rcv_vq = virtio_add_queue(&vxt_ctrlr->vdev, 128,
	                                     vxt_configure_update_request);
fprintf(stderr, "CDY virtio_vxt_init rcv_vq %p\n", vxt_ctrlr->rcv_vq);

	vxt_ctrlr->send_vq = virtio_add_queue(&vxt_ctrlr->vdev, 128,
	                                      vxt_push_configure_update);
fprintf(stderr, "CDY virtio_vxt_init send_vq %p\n", vxt_ctrlr->send_vq);


	/*
         * Set up the dummy queues to allow the passage of the VxT
	 * bus interrupts on the guest side.
         */
        vxt_ctrlr->vxt_bus_cfg_dummy =
	   virtio_add_queue(&vxt_ctrlr->vdev, 32, vxt_bus_cfg_dummy_handler);

        vxt_ctrlr->vxt_bus_sig_dummy =
	   virtio_add_queue(&vxt_ctrlr->vdev, 32, vxt_bus_sig_dummy_handler);





	/*
	 * Note: version is Version 1, subversion 1.  3 bytes for
	 * subversion
	 */
	register_savevm("virtio-vxt", virtio_vxt_instance++,
	                VIRTIO_VXT_VERSION,
	                virtio_vxt_save, virtio_vxt_load, vxt_ctrlr);

/*
config_lock
*/
	vxt_ctrlr->next = vxt_controllers;
	vxt_controllers = vxt_ctrlr;

/*
config_unlock
*/

fprintf(stderr, "CDY leaving virtio_vxt_init vxt_ctrlr %p\n", vxt_ctrlr);
	return (VirtIODevice *)vxt_ctrlr;
}

