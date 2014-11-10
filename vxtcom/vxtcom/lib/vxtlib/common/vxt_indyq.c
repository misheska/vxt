/*
 * vxt_indyq:
 *
 * This module implements the Symantec VxT Indirect queue type, or VxT
 * queue type 2.  indyq is implemented on top of the VxT queue type one
 * and is based on a queue header structure that communicates the pointer
 * and range of a virtual mapping.  The header is multiform, supporting 
 * transfers of direct data as well as indirect ones.
 *
 * Indirect transfers are made from memory spaces that may be virtual.  Since
 * the remote endpoint of the transfer may sit within a different memory space
 * or may not have direct access to the virtual layout of the local endpoint,
 * options to translate the local remote mapping are made available.  These
 * options are all accessible within the multiform header of the transfer
 * request.  A default mapping option may be provide on buffer initialization.
 * Users of the indyq are encouraged to use the inboard mapping transforms
 * when convenient as this isolates hypervisor specifics and opens up the
 * possibility of transparent hardware assist.
 *
 *
 * Please see vxt_queue for the underlying communication characteristics of
 * vxt_indyq.
 *
 * Note:  Unlike VxT Direct queue implementation, vxt_indyq does have
 * access locks on the queue resource.  Since the vast majority of 
 * indirect data usage is asynchronous and/or multi-caller, it was determined
 * that the tighter lock ranges of lock support inside of the queue outweighed
 * the overhead for the single thread caller scenarios.
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






#define VXTIQ_MAX_CELL_SG 128   /* Maximum number of s/g elements in a req */
#define VXTIQ_MAX_DIRECT  2048  /* Maximum size of direct data in a req */


/*
 *
 * vxt_indyq_map_parms_t:
 *
 * Structure passed on vxtiq_init_buf to provide identifier for 
 * address mapping transform, intermediate buffer size, intermediate
 * buffer count, and various other configuration options for indirect
 * queue support.
 *
 * The producer and consumer endpoints operate independently and setup
 * is customizable for the endpoint without regard to the nature of the
 * remote endpoint or the configuration of the shared memory buffer.
 *
 * The choice to use intermediate buffers is based on a change in the nature
 * of the data, or a need to isolate the endpoint from changes in the data.
 * (pointer updates, etc).  Data may change substantially in size when it moves
 * from a linear aggregate expression to a description of descrete elements
 * as in the translation of a virtual address range into a list of physical
 * page addresses.
 */

typedef struct vxt_indyq_map_parms {
	uint64_t map_id;
	uint32_t buffer_count;
	uint32_t buffer_size;
} vxt_indyq_map_parms_t;

/*
 *
 * vxt_indyq_header:
 *
 * The vxt_indyq_header will appear at the top of the buffer returned
 * on a send "put".  There may be multiple headers in the same buffer
 * these can be accessed via the chain_next pointer.
 *
 * An explicit pointer is used instead of an array of headers to allow
 * for in-stream data to be interleved with the indyq headers.  Not all 
 * data can be accessed from a memory source and the buffer may be the 
 * most efficient place to put data that is streaming in.
 *
 * In-stream data is of course direct.  An in-stream pointer and an
 * indirect flag assertion is an error, although it's behavior is
 * indeterminate and the error may not be flagged.
 *
 * The indirect flag is associated with two other fields, the
 * mapping_id and the cell_count.
 *
 * Multiple indirect data elements can be aggregated under a 
 * single indyq_header in a scatter gather list.  mapping_id
 * represents the mapping identifier for the mapping transform
 * and cell_count the number of individual scatter or gather
 * data segments that are enumerated.
 *
 * Address and size are used for direct data, address is used
 * for indirect data scatter gather lists.  Cell count is used
 * exclusively for indirect data.  Size is not consulted.
 * 
 *
 */

typedef struct vxt_indyq_header {
	struct {
		uint64_t
			direct : 1,
			indirect : 1,
			explicit_mapping : 1,
			write : 1,  /* reading or writing into indirect bufs */
			reserved : 60;
	} flags;
	
	uint64_t mapping_id;  /* override local if not NULL, */
	                      /* ignore if security violation */
	uint64_t chain_next;  /* next header in req chain, null if last */
	uint64_t address;
	uint32_t size;        /* including this header */
	uint32_t cell_count;
} vxt_indyq_header_t;



/*
 * vxt_indybuf_t:
 *
 * A type to label the fixed indirect queue
 * buffer size
 */

typedef uint8_t vxt_indybuf_t[VXTIQ_BUF_SIZE];  

/*
 *
 * vxt_indyq_cell:
 *
 * a scatter/gather list header
 *
 */
typedef struct vxt_indyq_cell {
	uint64_t address;
	uint64_t size;
} vxt_indyq_cell_t;



#define VXTIQ_BUF_CALLER_OWNED	0x1

typedef struct vxtiq_buf_state {
	void               *lbuf;
	void               *recv_buf;
	uint32_t           buf_index;
	uint32_t           buf_length;
	uint32_t           flags;
} vxtiq_buf_state_t;

/*
 *
 * vxtiq_instance
 *
 * State and resources repository for an instance of a vxt indirect
 * queue.
 */

typedef struct vxtiq_instance {
	vxt_mutex_t        lock;
        uint64_t           smap;  /* mapping transform, send side */
        uint64_t           rmap;  /* mapping transform, receive side */
	vxt_indyq_header_t *request_array;
	vxtiq_buf_state_t *buf_ctrl;
	vxt_indyq_header_t current_xfer;  /* for buffer spanning transfers */
	void               *current_smb;
	void               *shared_queue;
	uint32_t           buf_index;  /* active indyq buffer */
	uint32_t           buf_cnt;    /* number of indyq buffers */
	uint32_t           receiver_type;
	uint32_t           local_producer;
	uint32_t           current_xfer_offset;
	uint32_t           current_smb_offset;
	uint32_t           current_smb_length;
	uint32_t           current_smb_index;
	uint32_t           busy;
} vxtiq_instance_t;

/*
 *
 * vxtiq_get_lbuf:
 *
 * Get a local buffer, if the caller is not the producer then
 * the buffer must be primed with data from the shared memory queues.
 * The data is translated and transmuted according to the indirect
 * queue setup and returned to the caller.
 *
 * The instance lock is held on invocation
 *
 */

static int
vxtiq_get_lbuf(vxtiq_instance_t *instance, int flags, void **buffer,
             int *buf_num, int *data_len)
{
	vxtiq_instance_t *instance;
	vxt_indybuf_t    *buf_array;
	void             *recv_buf;
	int              *lbuf_index;
	int		 *next_index;
	int              *rbuf_index;
	int              *rbuf_length;

	/*
	 * Get a free local buffer and
	 */
	lbuf_index = instance->buf_index;
	while ((instance->buf_ctrlr[lbuf_index].flags & VXTIQ_BUF_CALLER_OWNED) 
	    || (instance->busy)) {
		if (flags & VXTQ_FLAGS_WAIT) {
			/* 
			 * Conflict can only legitimately be experienced if 
			 * there is more than one caller thread.  Otherwise
			 * it will lead to lock-up.
			 */
			/* same thread test? */
vxt_unlock(&instance->lock);
wait

vxt_lock(&instance->lock);
			lbuf_index = instance->buf_index;
			if (instance->buf_ctrlr[lbuf_index].flags &
			    VXTIQ_BUF_CALLER_OWNED) {
				vxt_unlock(&instance->lock);
				/*
				 * Someone got in and used the buffer
				 * before we could run.  Tell the caller
				 * and let them retry on a wait if they
				 * want.
				 */
				return VXT_RETRY;
			}
		} else {
			vxt_unlock(&instance->lock);
			return VXT_RSRC;
		}
	}
	instance->busy = 1;
	instance->buf_ctrlr[lbuf_index].flags |= VXTIQ_BUF_CALLER_OWNED;
	next_index = lbuf_index + 1;
	if (next_index >= instance->buf_cnt) {
		next_index = 0;
	}
	instance->buf_index = next_index;

	if (instance->local_producer == 1) {
		/*
		 * Just return the buffer
		 * Use our index so we can follow, through on
		 * multi-buffer sends.  We must so we can
		 * translate on the fly.
		 */
		instance->busy = 0;
		*buf_num = lbuf_index;
		*buffer = instance->buf_ctrlr[lbuf_index]->lbuf;
		*datalen = instance->buf_length;
		vxt_unlock(&instance->lock);
		return VXT_SUCCESS;

	}
	/*
	 * Grab a shared buffer
 	 * Look for a local, partially used buffer first
	 *
	 */
	if (instance->current_smb_offset != 0) {
		recv_buf = instance->current_smb + instance->current_smb_offset;
		rbuf_length = instance->current_smb_length
		              - instance->current_smb_offset;
		rbuf_index = instance->current_smb_index;
	} else {
		vxt_unlock(&instance->lock);
		ret = vxtq_get_buf(instance->shared_queue, flags,
		                   &recv_buf, &rbuf_index, &rbuf_length);
		/*
		 * Note:  If we were supposed to wait here, the direct
		 * vxtq will have waited.  Respect its judgement and return
		 * without regard to the state of the flag.
		 */
		if (ret != VXT_SUCCESS) {
			vxt_lock(&instance->lock);
			instance->busy = 0;
			/*
			 * Don't play with the index, it only moves in
			 * one direction, just alter the ownership flag
			 * so that the next time around the buffer is availble.
			 */
			instance->buf_ctrlr[lbuf_index].flags &=
			   ~VXTIQ_BUF_CALLER_OWNED;
			vxt_unlock(&instance->lock);
			return ret;
		}
		instance->current_smb = recv_buf;
		instance->current_smb_index = rbuf_index;
		instance->current_smb_offset = 0;
	}


	/*
	 * don't have to lock, we have exclusive 
	 * access to this buffer
	 */
	instance->buf_ctrlr[lbuf_index].recv_buf = recv_buf;
	instance->buf_ctrlr[lbuf_index].buf_index = rbuf_index;

	instance->buf_ctrlr[lbuf_index].buf_length = rbuf_length;   




	if (instance->buf_ctrl == VXTIQ_RQ_SIRSI) {
		/* 
		 * SIRSI - send indirect receive scatter indirect
		 * implication: remote wrote, possibly translated
		 * an indirect list, now we can read it and
		 * translate.  (We support translation both at the
		 * send and receive).
		 */
		/*
 		 * Do we have a pending transfer?  
		 * If so provide the header for the ongoing 
		 * transfer.  If not, send a NULL
		 * Note: busy protects the receive side here
		 * no need to use the buffer lock.
		 */
		/*
		 * If translate finds that current_xfer_offset is not
		 * 0 it carries on with the current_transfer.
		 * If translate finds the transfer is not finished
		 * after translating the buffer it
		 * fills the current_xfer structure in and sets the
		 * current_transfer_offset.
		 */
		
		write_cnt = 
		   vxt_indyq_copy_translate(&instance->current_xfer, 
		                       &current_xfer_offset,
		                       instance->rmap, recv_buf,
		                       rbuf_length);

		if (write_cnt <  rbuf_length) {
			/*
			 * We have more of the shared memory
			 * buffer, (smb) to translate.  Hold
			 * for next time.
			 */
			instance->current_smb = current_smb;
			instance->current_smb_offset += write_cnt;
		}
	recv_buf = instance->current_smb + instance->current_smb_offset;
	rbuf_length = instance->current_smb_length
	              - instance->current_smb_offset;
	rbuf_index = instance->current_smb_index;
	instance->current_smb = NULL;
		*buf_num = lbuf_index;
		*buffer = caller_buf;
		*datalen = write_cnt;
		instance->buf_ctrlr[lbuf_index].buf_length = write_cnt;
		if (write_cnt == 0) {
			/*
			 * Considered an error, return as such
			 * and null out any remaining data sent
			 * from the guest in the shared buffer.
			 * Note: Caller must put this buffer
			 * to return the shared memory buffer to
			 * active duty.  Also, the translator is
			 * free to leave any useful message in
			 * the local data buffer.
			 */
			vxt_lock(&instance->lock);
			instance->busy = 0;
			vxt_unlock(&instance->lock);
			
			return VXT_ERROR;
		}
	} else {
		/*
		 * Just return the buffer
		 * Use our index so we can follow, through on
		 * multi-buffer sends.  We must so we can
		 * translate on the fly.
		 */
		vxt_lock(&instance->lock);
		instance->busy = 0;
		*buf_num = lbuf_index;
		*buffer = recv_buf;
		*datalen = rbuf_length;
		vxt_unlock(&instance->lock);
		return VXT_SUCCESS;
	}

	return VXT_SUCCESS;
}


/*
 *
 * vxtiq_init_buf:
 *
 * vxtiq_init_buf sets up the shared memory to support the VxT indirect
 * transfer buffer protocol.  This protocol may support mapping functions
 * on the producer or consumer side and may involve intermediate buffers
 * These options are all supported by additional Indirect options flags
 * and the map_parms structure.  
 *
 * vxtiq_init_buf relies on the vxt direct queue implementation for
 * manipulation of the shared memory bufs and calls the vxtq_init_buf
 * function with the appropriate parameters.
 *
 */

int
vxtiq_init_buf(void *sm_buf, int length, int hdr_len, int buf_cnt,
               int space_avail_hyster, int data_avail_hyster,
               vxt_indyq_map_parms_t map_parms, int flags,
               vxtq_signal_callback signal,
               void *signal_parm,
               vxtq_wait_callback wait,
               void *wait_parm,
               int sub_buf_size, void **sm_queue)
{
	vxtiq_instance_t *instance;
	void             *direct_queue;
	uint32_t         receiver_type;
	uint32_t         receiver_owner;
	int              i;



	receiver_type = VXT_IQ_RQ_TYPE_MASK & flags;
	receiver_type = receiver_type >> VXTIQ_RQ_SHIFT;
	if (receiver_type == VXTIQ_RQ_UNKNOWN) {
		return VXT_PARM;
	}

	instance = (vxtiq_instance_t *)vxt_malloc(sizeof(vxtiq_instance_t));
	if (instance == NULL) {
		return VXT_RSRC;
	}

	vxt_lock_init(instance->lock);
	instance->current_smb = NULL;
	instance->current_smb_offset = 0;
	instance->current_xfer_offset = 0;
	instance->buf_index = 0;
	instance->busy = 0;

	instance->receiver_type = receiver_type;
	if (flags & VXTQ_LOCAL_IS_PRODUCER) {
		instance->local_producer = 1;
	} else {
		instance->local_producer = 0;
	}

	if (map_parms == NULL) {
		if (!(flags & VXTIQ_DIRECT_OUT)) {
		error message: Need a map
			vxt_free(instance);
			return VXT_PARMS;
		}
		instance->buf_cnt = buf_cnt;
		
	} else {
		if (map_parms->map_id == 0) {
			if (!(flags & VXTIQ_DIRECT_OUT)) {
			error message: Need a map
				vxt_free(instance);
				return VXT_PARMS;
			}
		}
		map_parms->map_id;
		instance->buf_cnt = map_parms->buffer_count;
		instance->buf_size = map_parms->buffer_size;
	}


	instance->buf_ctrl = (vxtiq_buf_state_t *)
	                     vxt_malloc(sizeof(vxtiq_instance_t) * buf_cnt);
	if (instance->buf_ctrl == NULL) {
		vxt_free(instance);
		return VXT_RSRC;
	}

	if (flags & VXTIQ_DIRECT_OUT) {
		instance->request_array = NULL;
		for (i = 0; i < buf_cnt i++) {
			instance->buf_ctrlr[i].lbuf = NULL;
			instance->buf_ctrl[i].flags = 0;
		}
	} else {
		/*
		 * Allocate the intermediate buffers
		 * that we will use to talk with the caller
		 */
		
		instance->request_array =
		   (vxt_indyq_header_t *)
		   vxt_malloc(VXTIQ_BUF_SIZE * instance->buf_cnt);
		if (instance->request_array == NULL) {
			vxt_free(instance->buf_ctrl);
			vxt_free(instance);
			return ret;
		}
		for (i = 0; i < buf_cnt i++) {
			instance->buf_ctrl[i].lbuf = 
			   (void *)
			   (((uint8_t *)instance->request_array) + 
			    (instance->buf_length * i));
			instance->buf_ctrl[i].flags = 0;
		}
	}

	ret = vxtq_init_buf(sm_buf, length, hdr_len, buf_cnt,
	                    space_avail_hyster, data_avail_hyster, flags
	                    signal, signal_parm, wait, wait_parm,
	                    sub_buf_size, &direct_queue);
	if (ret != VXT_SUCCESS) {
		if (instance->request_array != NULL) {
			vxt_free(instance->request_array);
		}
		vxt_free(instance->buf_ctrl);
		vxt_free(instance);
		return ret;
	}


	instance->shared_queue = direct_queue;

	return VXT_SUCCESS;
}



void
vxtiq_release_buf(void local_queue)
{
	vxtiq_instance_t *instance;

	instance = (vxtiq_instance_t *)local_queue;

	vxtq_release_buf(instance->shared_queue);
	vxt_free(instance->buf_ctrl);
	if (instance->request_array != NULL) {
		vxt_free(instance->request_array);
	}
	vxt_free(instance);
	
}


/*
 *
 * vxtiq_get_buf:
 *
 *
 * Returns:
 *		VXT_RETRY - No buffer available after a resource wait
 *		            Probably a competing thread grabbing the
 *		            resource ahead of us but let the caller
 *		            decide.  
 *		VXT_RSRC -  No local buffer available
 *		VXT_BUSY -  No shared memory buffer available
 *		VXT_ERROR - VxTGet error on s/g address translation, (recv)
 */

int
vxtiq_get_buf(void *qhandle, int flags, void **buffer,
             int *buf_num, int *data_len)
{
	vxtiq_instance_t *instance;
	vxt_indybuf_t    *buf_array;
	void             *recv_buf;
	int              *lbuf_index;
	int		 *next_index;
	int              *rbuf_index;
	int              *rbuf_length;

	instance = (vxtiq_instance_t *)qhandle;

	vxt_lock(&instance->lock);
	if (instance->request_array) {
		/*
		 * This is the path where local buffers serve as intermediaries
		 * either for data expansion or for privacy.
		 */
		return vxtiq_get_lbuf(instance, flags, buffer,
		                      buf_num, data_len);
	}
	lbuf_index = instance->buf_index;
	while ((instance->buf_ctrlr[lbuf_index].flags & VXTIQ_BUF_CALLER_OWNED) 
	    || (instance->busy)) {
		if (flags & VXTQ_FLAGS_WAIT) {
			/* 
			 * Conflict can only legitimately be experienced if 
			 * there is more than one caller thread.  Otherwise
			 * it will lead to lock-up.
			 */
			/* same thread test? */
vxt_unlock(&instance->lock);
wait

vxt_lock(&instance->lock);
			lbuf_index = instance->buf_index;
			if (instance->buf_ctrlr[lbuf_index].flags &
			    VXTIQ_BUF_CALLER_OWNED) {
				vxt_unlock(&instance->lock);
				/*
				 * Someone got in and used the buffer
				 * before we could run.  Tell the caller
				 * and let them retry on a wait if they
				 * want.
				 */
				return VXT_RETRY;
			}
		} else {
			vxt_unlock(&instance->lock);
			return VXT_RSRC;
		}
	}
	instance->busy = 1;
	instance->buf_ctrlr[lbuf_index].flags |= VXTIQ_BUF_CALLER_OWNED;
	next_index = lbuf_index + 1;
	if (next_index >= instance->buf_cnt) {
		next_index = 0;
	}
	instance->buf_index = next_index;

	/*
	 * Grab a shared buffer
	 */
	vxt_unlock(&instance->lock);
	ret = vxtq_get_buf(instance->shared_queue, flags,
	                   &recv_buf, &rbuf_index, &rbuf_length);

	/*
	 * Note:  If we were supposed to wait here, the direct
	 * vxtq will have waited.  Respect its judgement and return
	 * without regard to the state of the flag.
	 */
	if (ret != VXT_SUCCESS) {
		vxt_lock(&instance->lock);
		instance->busy = 0;
		/*
		 * Don't play with the index, it only moves in
		 * one direction, just alter the ownership flag
		 * so that the next time around the buffer is availble.
		 */
		instance->buf_ctrlr[lbuf_index].flags &=
		   ~VXTIQ_BUF_CALLER_OWNED;
		vxt_unlock(&instance->lock);
		return ret;
	}


	/*
	 * don't have to lock, we have exclusive 
	 * access to this buffer
	 */
	instance->buf_ctrlr[lbuf_index].recv_buf = recv_buf;
	instance->buf_ctrlr[lbuf_index].buf_index = rbuf_index;

	instance->buf_ctrlr[lbuf_index].buf_length = rbuf_length;   



	if (instance->local_producer == 1) {
		/*
		 * Just return the buffer
		 * Use our index so we can follow, through on
		 * multi-buffer sends.  We must so we can
		 * translate on the fly.
		 */
		vxt_lock(&instance->lock);
		instance->busy = 0;
		*buf_num = lbuf_index;
		*buffer = recv_buf;
		*datalen = rbuf_length;
		vxt_unlock(&instance->lock);
		return VXT_SUCCESS;
	} else {
		if (instance->buf_ctrl == VXTIQ_RQ_SIRSI) {
			/* 
			 * SIRSI - send indirect receive scatter indirect
			 * implication: remote wrote, possibly translated
			 * an indirect list, now we can read it and
			 * translate.  (We support translation both at the
			 * send and receive).
			 */
			/*
 			 * Do we have a pending transfer?  
			 * If so provide the header for the ongoing 
			 * transfer.  If not, send a NULL
			 * Note: busy protects the receive side here
			 * no need to use the buffer lock.
			 */
			/*
			 * If translate finds that current_xfer_offset is not
			 * 0 it carries on with the current_transfer.
			 * If translate finds the transfer is not finished
			 * after translating the buffer it
			 * fills the current_xfer structure in and sets the
			 * current_transfer_offset.
			 */
			
			write_cnt = 
			   vxt_indyq_translate(&instance->current_xfer, 
			                       &current_xfer_offset,
			                       instance->rmap, recv_buf,
			                       rbuf_length);
			*buf_num = lbuf_index;
			*buffer = caller_buf;
			*datalen = write_cnt;
			instance->buf_ctrlr[lbuf_index].buf_length = write_cnt;
			if (write_cnt == 0) {
				/*
				 * Considered an error, return as such
				 * and null out any remaining data sent
				 * from the guest in the shared buffer.
				 * Note: Caller must put this buffer
				 * to return the shared memory buffer to
				 * active duty.  Also, the translator is
				 * free to leave any useful message in
				 * the local data buffer.
				 */
				vxt_lock(&instance->lock);
				instance->busy = 0;
				vxt_unlock(&instance->lock);
				
				return VXT_ERROR;
			}
		} else {
			/*
			 * Just return the buffer
			 * Use our index so we can follow, through on
			 * multi-buffer sends.  We must so we can
			 * translate on the fly.
			 */
			vxt_lock(&instance->lock);
			instance->busy = 0;
			*buf_num = lbuf_index;
			*buffer = recv_buf;
			*datalen = rbuf_length;
			vxt_unlock(&instance->lock);
			return VXT_SUCCESS;
		}
	}

	return VXT_SUCCESS;
}

int
vxtiq_put_buf(void *qhandle, int flags, void **buffer, 
             int *buf_num, int *data_len)
{
	instance = (vxtiq_instance_t *)qhandle;

	/*
	 * Note: we assume proper behavior on the part of the
	 * caller.  ie. If the receive is indirect, the caller
	 * has set itself up as the local producer.
	if (instance->local_producer == 1) {
		if ((instance->buf_ctrl == VXTIQ_RQ_SIRSD) ||
		    (instance->buf_ctrl == VXTIQ_RQ_SIRSI)) {
			/*
			 * Local Receiver telling us where to put 
			 * the sender's future data, translate the
			 * scatter list and put the  result in 
			 * the shared buffer
			 */
helper routine:
get shared buffer
	if not available, wait or return based on flag, support partial sends
translate list
write shared buffer
put shared buffer
repeat until all data sent

			
		} else {
			/*
			 * Standard Send, consult header
			 */
Just put associated shared buffers, (list off of local_buf struct) and 
take back control of local buf
		}
	} else {
		if ((instance->buf_ctrl == VXTIQ_RQ_SIRD) ||
		    (instance->buf_ctrl == VXTIQ_RQ_SIRI)) {
		} else {
		}
	}

	
}
