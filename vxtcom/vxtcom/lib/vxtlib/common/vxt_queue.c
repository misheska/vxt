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

#define _VXT_COMPONENT_ 3
#define _VXT_SUBSYSTEM_ 6

#include <vxt_lib_os.h>
#include <vxt_system.h>
#include <vxt_msg_export.h>
#include <vxt_queue.h>
#include <symcom_dev_table.h>
#include <vxt_com.h>
#include <vxt_msg_os.h>


/*
 * Shared memory queue type 1
 *
 * This module imposes a structure on a buffer which corresponds to
 * Symantec message passing queue type 1.
 *
 * Queue type 1 is a one-way buffer with a producer
 * and a consumer.
 *
 * A buffer is carved into sub-buffers of specified size with
 * all buffers being initially empty.
 *
 * Buffers are made available to producers or consumers through
 * the get buffer call.  
 *
 * Buffers have polarity in that they are initialized so that
 * the local party is either a producer or a consumer.
 *
 * When the get buffer routine is called by a producer, an 
 * empty buffer is returned if available.  If not, the caller
 * either waits or is given a TRY_AGAIN response, depending
 * on the control flags sent on the call.
 *
 * When a get buffer request is made by a consumer, a buffer
 * containing data is returned if one is available.  If not,
 * the caller either waits or is given a TRY_AGAIN response
 * depending on the control flags sent on the call.
 *
 * The queue library is designed to be in the same address space
 * as the application code that calls it.  This, along with the
 * shared memory and simple queue mechanism makes for an optimal
 * performance profile.
 *
 * Other models may be layered on top of the queue model.  Stateful
 * and non-stateful socket schemes may be wrapped around the
 * queue primitives.
 *
 * Devices of any variety may be built on top of the queue 
 * abstraction.  The transport queue has no knowledge of
 * the content of the buffers it vends.  Multiple queues may
 * be employed in the construction of a meta-device.
 *
 */

/*
 * The vxtq_sbuf_header can be read by
 * either the producer or consumer at
 * any time.  However, it can only
 * be written by the producer when the
 * owner bit is set to one.  Likewise,
 * it can only be written by the 
 * consumer when the owner bit is set
 * to zero.  This is tue for all fields
 * within the structure.
 */

typedef struct vxtq_sbuf_header {
	uint32_t  
		data_len : 16,
		reserved : 13,
		abort : 1,
		app_held : 1,   /* put specifically */ 
		                /* here for debugging */
		owner : 1; 
} vxtq_sbuf_header_t;

typedef struct vxtq_queue {
	vxtq_control_t *hdr;
	void *buffers;       /* pointer at first buffer in sm array */
	int  buf_index;      /* used to avoid buf search through seq access */
	int  local_producer; /* 1 if caller is the producer */
	vxtq_signal_callback signal;
	void *signal_parm;
	vxtq_wait_callback wait;
	void *wait_parm;
} vxtq_queue_t;


/*
 * The consumer coordinates its setting of the data_avail_lead with
 * its known speed relative to the producer and the amount of time
 * it is willing to live wait for more data.  This allows optimization
 * of streaming with a balance between time wasted on live wait and
 * the overhead of sleeping and waking.
 *
 * Likewise, the producer coordinates its setting of the space_avail_lead
 * with its known speed relative to the consumer and the amount of  time
 * it is willing to live wait for space to be freed.
 *
 * A shutdown or suspend may be initiated by either the producer or
 * the consumer.  In the case of a producer initiated shutdown, the 
 * consumer_flush flag is set.  With this action the producer implies
 * that it will send no more data  The preparation for shutdown or
 * suspend is carried out by the consumer.  When the data has been
 * drained from the queue  a signal is sent.
 *
 * If the consumer initiates a shutdown the producer_suspend flags is
 * set immediately.  When the producer "sees" the flag it will set
 * flush queues and send back a signal.  In this way the consumer
 * will know when it is no longer possible to see more data in the queue.
 *
 * Restartting a queue:
 *
 * Either the producer or the consumer may set the queue_init
 * flag.  The flag may only be set however when both the consumer_flush
 * and the producer_suspend flags are on.
 *
 * Once the queue_init flag is set, the producer may clear the producer_suspend
 * and the consumer may clear the consumer_flush flags.
 */






/*
 * vxtq_get_buf:
 *
 * vxtq_get_buff is used to acquire the "next buffer".  The buffers
 * carved out of shared memory are assumed to be ordered.  This
 * cuts down on the search for a suitable buffer.  Buffers are
 * filled sequentially and consequently emptied in the same
 * manner.  Data is passed in an unbroken stream and buffer acquisition
 * wraps when the user asks for a buffer after acquiring the buffer
 * with the highest ordinal value.
 *
 * The caller is allowed to hold more than one buffer at a time
 * this requires that we label each buffer with a buffer_id
 * The natural id is of course the buffer_index.  The passing
 * of the buffer id allows for possible future expansion where
 * order of buffer use is relaxed.  This will be valuable in
 * cases where multiple logical streams are run through the same
 * queue.  While this may be seen in direct shared memory paradigms
 * it is more likely to be seen in hardware supported queues.  Here
 * the relative value of the hardware and possibly it multifunction
 * support make it a likely candidate for multi-stream actions.
 *
 * Producer/Consumer:
 *
 * The state of the buffer when it is provided to the caller is
 * dependent on whether the caller is the sender or the receiver
 * This is determined via a field in the local queue header which
 * was instantiated at initialization time.  If the caller is a
 * receiver, the buffer will have data.  If the caller is a provider
 * the buffer will be empty.
 *
 * The amount of data in a consumer's buffer is returned in the
 * data_len field.  While not strictly necessary, this provides
 * data length visibility to the intermediate transport entities
 * that may exist in the queue.  Without this information it would
 * be necessary to always transfer the entire contents of a buffer.
 *
 * It was decided that the field would always be present and 
 * reflected to the get_buf caller so that the caller could simplify
 * the meta-data assoicated with its transfers.
 *
 *
 * FLAGS:
 *    VXTQ_FLAGS_WAIT - Wait for buffer availability
 *    VXTQ_IGNORE_SHUTDOWN - Finish flushing queue 
 *                      during a shutdown if consumer. 
 *                      Send last cached data if
 *                      producer.
 *
 * Returns:
 *   VXT_SUCCESS
 *
 *   VXT_RSRC  - No buffers available, if the caller
 *               is the producer then there are no
 *               empty buffers.  If the caller is
 *               the consumer then there are no
 *               buffers with data in them.
 *
 *   VXT_ABORT - Return from a wait with no
 *               buffers available.
 *   VXT_SHUTTING_DOWN - Sent when remote side
 *                 is shutting down
 */

int 
vxtq_get_buf(void *qhandle, int flags, void **buffer, 
             int *buf_num, int *data_len) 
{
	uint32_t next_buf;
	vxtq_sbuf_header_t *buf_header;
	volatile vxtq_queue_t *tqueue = (vxtq_queue_t *)qhandle;

	UMI_LOG(1, VXTPRINT_DEBUG_LEVEL, " called\n");
	if (!(flags & VXTQ_IGNORE_SHUTDOWN)) {
		if (tqueue->local_producer) {
			if (tqueue->hdr->consumer_flush) {
				UMI_LOG(2, VXTPRINT_DEBUG_LEVEL,
				        "we are local producer, flush on \n");
				return VXT_SHUTTING_DOWN;
			}
			if (tqueue->hdr->producer_stop) {
				UMI_LOG(3, VXTPRINT_DEBUG_LEVEL,
				        "local producer, send blocked\n");
				return VXT_BLOCKED;
			}
		} else {
			if (tqueue->hdr->producer_suspend) {
				UMI_LOG(4, VXTPRINT_DEBUG_LEVEL,
				        "we are the consumer, producer "
				        "suspend is on\n");
				return VXT_SHUTTING_DOWN;
			}
		}
	}
	UMI_LOG(5, VXTPRINT_DEBUG_LEVEL,
	        " accessed tqueue header successfully\n");
retry:
	next_buf = tqueue->buf_index;
	buf_header = (vxtq_sbuf_header_t *)
	             (((vxtarch_word)tqueue->buffers) + 
	              (next_buf * tqueue->hdr->buf_size));
	/*
	 * If the buffer is owned by producer and we are 
	 * i.e. 1 and 1.  Or if the buffer is owned
	 * by the consumer and we are the consumer,
	 * 0 and 0.  We can return the buffer.
	 * otherwise we must return empty handed or
	 * wait for the buffer to become available.
	 */
	if ((buf_header->owner && tqueue->local_producer) ||
	    ((!buf_header->owner) && (!tqueue->local_producer))) {
		UMI_LOG(6, VXTPRINT_DEBUG_LEVEL,
		        "buffer available, %d\n", next_buf);
	
		/*
		 * First check for abort buffer, if it is
		 * we simply turn it around.  If we are
		 * a producer, the buffer will be filled
		 * with data that was never read.  If
		 * we are a consumer, the buffer is
		 * empty.
		 */
		if (buf_header->abort) {
			UMI_LOG(7, VXTPRINT_DEBUG_LEVEL,
			        "found a buffer with abort bit set, %d\n",
			        next_buf);
			buf_header->abort = 0;
			buf_header->owner = buf_header->owner ^ 1;
			next_buf++;
			if (next_buf >= tqueue->hdr->buf_cnt) {
				tqueue->buf_index = 0;
				next_buf = 0;
			} else {
				tqueue->buf_index = next_buf;
			}
			goto retry;
		}
		if (buf_header->app_held) {
			UMI_LOG(8, VXTPRINT_DEBUG_LEVEL,
			        "holding all the buffers\n");
			/* looks like we are out of buffers and the
			 * caller holds them all.  Return immediatley
			 * without regard to the wait flag.  Only
			 * caller action can free buffers here.
			 */
			return VXT_NOMEM;
		}
		*buf_num = next_buf;
		next_buf++;
		buf_header->app_held = 1;
		if (next_buf >= tqueue->hdr->buf_cnt) {
			tqueue->buf_index = 0;
		} else {
			tqueue->buf_index = next_buf;
		}
		if (tqueue->local_producer) {
			/* 
			 * Pass back the number of bytes of empty
			 * buffer available for data
			 */
			*data_len = tqueue->hdr->buf_size 
			            - sizeof(vxtq_sbuf_header_t);
		} else {
			/*
			 * Pass back the number of bytes of viable data
			 * Do a check for sanity.
			 */

#ifdef vxtq_remote_ep_buffer_corruption
			if (buf_header->data_len > 
			    (tqueue->hdr->buf_size
			     - sizeof(vxtq_sbuf_header_t))) {
				/*
				 * CORRUPTED SHARED MEMORY BUFFER!
				 * PASS BACK ZERO LENGTH
				 */
				UMI_ERROR(26, VXTPRINT_PRODUCT_LEVEL,
			                  "Corrupted data length on shared "
				          "memory buffer, len = 0x%x, buffer "
				          "%p\n", buf_header->data_len,
				          buf_header);
				*data_len = 0;
			}
#endif
			*data_len = buf_header->data_len;
		}
		buf_header++;
		*buffer = (void *)buf_header;
		return VXT_SUCCESS;
	}
	if (flags & VXTQ_FLAGS_WAIT) {
		/*
		 * Show the remote endpoint that
		 * we will need a signal
		 */
		if (tqueue->local_producer) {
			if (tqueue->hdr->need_signal_space == 0) {
				tqueue->hdr->need_signal_space = 1;
				/*
				 * Avoid race by checking for space
				 * one more time before going to
				 * sleep
				 */
/* machine independent flush action */
				vxt_mb();
				goto retry;
			}
		} else {
			if (tqueue->hdr->need_signal_data == 0) {
				tqueue->hdr->need_signal_data = 1;
				/*
				 * Avoid race by checking for data
				 * one more time before going to
				 * sleep
				 */
/* machine independent flush action */
				vxt_mb();
				goto retry;
			}
		}
		/*
		 * Call wait
		 */
		tqueue->wait(tqueue->wait_parm);
		if ((buf_header->owner && tqueue->local_producer) ||
		    ((!buf_header->owner) && (!tqueue->local_producer))) {
			/*
			 * looks like we now have a buffer
			 */
			goto retry;
		}
		/*
		 * We returned without a valid buffer, this
		 * is a wait abort condition.
		 */
		UMI_LOG(9, VXTPRINT_DEBUG_LEVEL,
		        " no valid buffer after wait, abort\n");
		return VXT_ABORT;
	} else {
		return VXT_RSRC;
	}

	return VXT_SUCCESS;
}


/*
 * 
 * vxtq_put_buf:
 *
 * vxtq_put_buf is invoked by the application to give control
 * of a buffer back to the queue library.  If the caller is
 * a producer, it is assumed the buffer is now filled with data
 * If the caller is a consumer it is assumed the buffer is
 * now empty.  In either case, the buffer app_held flag is
 * reset and the ownership transfers to the remote endpoint.
 *
 * In the case of abort, the client is indicating that it has
 * taken no action after acquring the buffer through the 
 * vxtq_get_buf command.  When possible the buffer is just
 * put back onto the available list.  If subsequent get's
 * have placed the buffer out of reach of our next_buf pointer
 * we give the buffer to the remote endpoint with the abort
 * flag set.  This is equivalent to a turn-around flag.
 *
 * NOTE: It must be remembered that a streaming flow
 * mechanism is employed here.  While a buffer will be
 * remanded to the remote endpoint, it will be effectively
 * unavailable if the caller has not put a buffer in sequence
 * before it and will remain so until all of the buffers
 * before this one are given to the remote party.  Thus
 * a signal may be lost.  If the caller is depending on
 * Auto Stream and must provide a buffer out of sequence
 * the last out of sequence buffer should be accompanied
 * by a SIGNAL assertion in the flags parameter.
 *
 *  VXTQ_FLAGS_SIGNAL - Signal the remote end of data present
 *  VXTQ_FLAGS_ABORT  - Give back the buffer without action
 *  VXTQ_AUTO_STREAM -  Caller is sending a large number of
 *                      buffers and wishes the system to
 *                      signal only when necessary to keep
 *                      the remote party active.
 *
 * Returns:
 *           VXT_SUCCESS
 *           VXT_PARM - Buf index out of range,
 *                      bad buffer address,
 *                      buffer not owned by local,
 *                      buffer not held by caller
 *
 */

int 
vxtq_put_buf(void *qhandle, int flags, void *buffer, 
             uint32_t buf_num, int data_len) 
{
	vxtq_sbuf_header_t *buf_header;
	vxtq_queue_t       *tqueue = (vxtq_queue_t *)qhandle;
	uint32_t           avail_num;


	/*
	 * Retrieve the buffer address based on the 
	 * user provided buffer index
	 */
	if (buf_num >= tqueue->hdr->buf_cnt) {
		return VXT_PARM;
	}

	buf_header = (vxtq_sbuf_header_t *)
	             (((vxtarch_word)tqueue->buffers) + 
	              (buf_num * tqueue->hdr->buf_size));

	if ((buf_header + 1) != (vxtq_sbuf_header_t *)buffer) {
		return VXT_PARM;
	}

	if ((buf_header->owner && !tqueue->local_producer) ||
	    ((!buf_header->owner) && tqueue->local_producer)) {
		return VXT_PARM;
	}

	/*
	 * Double put?
	 */
	if (!buf_header->app_held) {
		return VXT_PARM;
	}
	UMI_LOG(10, VXTPRINT_DEBUG_LEVEL, "ready to check abort\n");

	if (flags & VXTQ_FLAGS_ABORT) {
		/*
		 * The caller is giving back the buffer without
		 * having taken any action on it.  This is
		 * natural for some queue models but
		 * somewhat problematic for us.  We
		 * treat data transfer as a stream. The
		 * next buffer pointer moves across the
		 * buffers.  Therefore, to keep communication
		 * from seizing we must still pass the buffer
		 * to the remote side.
		 * In the case of a producer we simply pass
		 * a buffer of zero data_len.  In the case
		 * of a consumer, the data is still valid
		 * we will pass the buffer back with an
		 * abort_retry flag asserted.  The buffer
		 * will then be sent back to us when it
		 * is encountered at the remote end.
		 *
		 * There is one exception to this policy
		 * if the buffer is only one away from the
		 * next_buf pointer, we simply back the
		 * pointer up.
		 *
		 * Note: Abort is expected to be rare
		 * and is not optimized for.
		 * 
		 */
	   buf_header->app_held = 0;
	   if ((tqueue->buf_index == (buf_num + 1)) ||
	      (((buf_num + 1) == tqueue->hdr->buf_cnt) 
	       && (tqueue->buf_index == 0))) {
			tqueue->buf_index = buf_num;
			if (flags & VXTQ_FLAGS_SIGNAL) {
				tqueue->signal(tqueue->signal_parm);
			}
			return VXT_SUCCESS;
		} else {
			if(tqueue->local_producer) {
				buf_header->data_len = 0;
			}
			buf_header->app_held = 0;
			buf_header->abort = 1;
			/*
			 * This must always be last
			 */
			buf_header->owner = buf_header->owner ^ 1;
		}
	} else {
		UMI_LOG(11, VXTPRINT_DEBUG_LEVEL, "normal processing\n");
		if (!(flags & VXTQ_IGNORE_SHUTDOWN)) {
			if(tqueue->local_producer) {
				if(tqueue->hdr->producer_stop == 1) {
					return VXT_BLOCKED;
				}
			}
		}
		/* 
		 * normal buffer processing, give
		 * the buffer to the remote
		 * endpoint.
		 */
		buf_header->data_len = data_len;
		buf_header->app_held = 0;
/* machine independent flush action */
		vxt_mb();

		/*
		 * This must always be last
		 */
		buf_header->owner = buf_header->owner ^ 1;
	}

	if(tqueue->local_producer) {
		tqueue->hdr->send_cnt++;
	} else {
		tqueue->hdr->receive_cnt++;
	}
	if (flags & VXTQ_FLAGS_SIGNAL) {
		UMI_LOG(12, VXTPRINT_DEBUG_LEVEL,
		        "signal remote: flags 0x%x\n", (uint32_t)flags);
		tqueue->signal(tqueue->signal_parm);
	} else {
		UMI_LOG(13, VXTPRINT_DEBUG_LEVEL,
		        "no explicit signal for remote: flags 0x%x\n",
		        (uint32_t)flags);
		/*
		 * If we haven't asked explicitly for a signal
		 * check to see if caller wants automatic
		 * streaming hysteresis. If so use the
		 * hysteresis value and signal when satisfied.
		 */
		/*
		 * We must flush our processor cache as 586 intel and beyond
		 * do not respect asynchronous changes  i.e. the order
		 * of changes in two shared data fields may be reversed.
		 */
/* machine independent flush action */
		vxt_mb();
		if (flags & VXTQ_AUTO_STREAM) {
			volatile vxtq_control_t *sq_hdr = tqueue->hdr;
			if (tqueue->local_producer) {
				if (sq_hdr->receive_cnt <= sq_hdr->send_cnt) {
					avail_num = 
					   sq_hdr->buf_cnt - 
					   (sq_hdr->send_cnt -
					    sq_hdr->receive_cnt);
				} else {
					/*
					 * one pointer has wrapped and the other
					 * hasn't yet.  Note: we are full when
					 * adding one to send would equal recv
					 * (accounting for wrap)
					 */
					avail_num = sq_hdr->receive_cnt
					            - sq_hdr->send_cnt;
				}
				if ((avail_num == 
				     sq_hdr->space_avail_lead) && 
				    (sq_hdr->need_signal_data)) {
					sq_hdr->need_signal_data = 0;
					tqueue->signal(tqueue->signal_parm);
				}
			} else {
				/* 
				 * We are the consumer
				 */
				if (sq_hdr->receive_cnt 
				    <= sq_hdr->send_cnt) {
					avail_num = (sq_hdr->send_cnt -
					             sq_hdr->receive_cnt);
				   } else {
					/*
					 * one pointer has wrapped and the other
					 * hasn't yet
					 */
					avail_num = sq_hdr->buf_cnt -
					            sq_hdr->receive_cnt +
					            (sq_hdr->send_cnt + 1);
				}
				if ((avail_num == sq_hdr->data_avail_lead)
				    && (sq_hdr->need_signal_space)) {
					sq_hdr->need_signal_space = 0;
					tqueue->signal(tqueue->signal_parm);
				}
			}
		} else {
			volatile vxtq_control_t *sq_hdr = tqueue->hdr;
			/*
			 * We are not streaming but we still have to check
			 * for remote wait on resource
			 */
			if (tqueue->local_producer) {
				if (sq_hdr->need_signal_data) {
					sq_hdr->need_signal_data = 0;
					tqueue->signal(tqueue->signal_parm);
				}
			} else {
				/* 
				 * We are the consumer
				 * No hysteresis, just restart the sender
				 * if there are one or more buffers available
				 */
				if (sq_hdr->need_signal_space) {
					sq_hdr->need_signal_space = 0;
					tqueue->signal(tqueue->signal_parm);
				}
			}
		}
	}
	return VXT_SUCCESS;
}


/*
 *
 * vxtq_init_buf:
 *
 * vxtq_init_buf lays out the shared memory that is passed to it.
 *
 * The header length must be at minimum the size of the vxtq_control
 * structure.  Any additional space can be used by the caller for 
 * additional control protocols.  i.e. debug and tracing
 *
 * The rest of the buffer will be cut up into sub-buffers.  The
 * length depending on the number of buffers requested.  The
 * buffer is a shared buffer and hence may already have been
 * set up by the remote endpoint.  In this case the flags
 * register will have the VXTQ_BUFFER_INITIALIZED condition
 * asserted.
 *
 * The ownership flags appear at the top of each of their
 * respective buffers.  While this is not space optimal it
 * should provide better performance.  If the ownership flags
 * appear as a bitwise list, the producer and consumer would
 * be flushing each others caches on many accesses. The 
 * dispersal avoids this.
 *
 * Note: The queue mechanism itself makes no provision for
 * buffer alignment.  If the caller believes there will be an
 * impact, it should postion the calling buffer accordingly
 * i.e. If the caller wants 4k buffers aligned on page 
 * boundaries it should request 4k * buffer_count + 1 shared
 * memory locations.  The buffer passed to this routine should
 * be at an offset to put the first buffer address on a 4k
 * boundary and the number of sub_buffers should match the
 * number of pages.  Assuming the header area is less than a
 * page the starting address should be 4k - hdr_len.
 */


int
vxtq_init_buf(void *sm_buf, int length, int hdr_len, int buf_cnt, 
              int space_avail_hyster, int data_avail_hyster,
              int flags,
              vxtq_signal_callback signal,
              void *signal_parm,
              vxtq_wait_callback wait,
              void *wait_parm,
              int *sub_buf_size, void **sm_queue) 
{
	vxtq_queue_t *qstate;
	vxtq_sbuf_header_t *sub_buf_header;
	int  i;

	/*
	 *  Make sure the hdr_len requested is big enough for minimal
	 *  structure elements.
	 */
	UMI_LOG(14, VXTPRINT_DEBUG_LEVEL, " called\n");
	if ((buf_cnt == 0) || (hdr_len < sizeof(vxtq_control_t))) {
		return VXT_RANGE;
	}


	if (length < ((buf_cnt * (int)sizeof(vxtq_sbuf_header_t)) + hdr_len)) {
		return VXT_RANGE;
	}

	/*
	 * Calculate the size of the sub-buffers
	 * ((bufsize - hdr_size) div bufcnt = sub_buf_size
	 */
	*sub_buf_size = (length - hdr_len) / buf_cnt;
	if (*sub_buf_size == 0) {
		return VXT_RANGE;
	}
	UMI_LOG(15, VXTPRINT_DEBUG_LEVEL, "sub_buf_size 0x%x, buf_cnt 0x%x\n",
	        (uint32_t) *sub_buf_size, (uint32_t) buf_cnt);

	qstate = (vxtq_queue_t *)vxt_malloc(sizeof(vxtq_queue_t));
	if (qstate == NULL) {
		return VXT_NOMEM;
	}

	qstate->hdr = (vxtq_control_t *)sm_buf;
	/*
	 * Remember, the address of buffers is local
	 * It will be mapped at different locations
	 * at the two endpoints.
	 */
	qstate->buffers = (char *)sm_buf + hdr_len;
    
	/*
	 * If the shared memory buffer isn't already
	 * initialized, set up the buffer headers
	 */


	UMI_LOG(16, VXTPRINT_DEBUG_LEVEL, "Initialize qstate\n");
	if (space_avail_hyster) {
		qstate->hdr->space_avail_lead = space_avail_hyster;
	} else {
		qstate->hdr->space_avail_lead = (buf_cnt >> 1) + (buf_cnt >> 2);
	}
	if (data_avail_hyster) {
		qstate->hdr->data_avail_lead = data_avail_hyster;
	} else {
		qstate->hdr->data_avail_lead = buf_cnt >> 2;
	}
	qstate->hdr->need_signal_data = 0;
	qstate->hdr->need_signal_space = 0;
	qstate->hdr->consumer_flush = 0;
	qstate->hdr->producer_suspend = 0;
	qstate->hdr->producer_stop = 0;
	qstate->hdr->error = 0;
	qstate->hdr->buf_size = *sub_buf_size;
	qstate->hdr->buf_cnt = buf_cnt;
	qstate->hdr->send_cnt = 0;
	qstate->hdr->receive_cnt = 0;

	if (!(flags & VXTQ_BUFFER_INITIALIZED)) {
		sub_buf_header = (vxtq_sbuf_header_t *)qstate->buffers;
		for (i = 0; i < buf_cnt; i++) {
			/*
			 * Producer owns the buffers, they are empty
			 */
			sub_buf_header->owner = 1;
			sub_buf_header->abort = 0;
			sub_buf_header->app_held = 0;
			sub_buf_header = 
			   (vxtq_sbuf_header_t *)(((char *)sub_buf_header) 
			                          + *sub_buf_size);
   
		 }
	 }

	 qstate->buf_index = 0;
	 if (flags & VXTQ_LOCAL_IS_PRODUCER) {
		 qstate->local_producer = 1;
	 } else {
		 qstate->local_producer = 0;
	 }
	 qstate->signal = signal;
	 qstate->signal_parm = signal_parm;
	 qstate->wait = wait;
	 qstate->wait_parm = wait_parm;

	UMI_LOG(17, VXTPRINT_DEBUG_LEVEL,
	        "Returning with qstate = %lu\n", (vxtarch_word) qstate);
	 *sm_queue = (void *)qstate;
    
	 return VXT_SUCCESS;
    
    
}


/*
 *
 * vxtq_release_buf:
 *
 * Dereference any resources associated with the queue header targeted
 * and free the queue header.
 *
 * Returns:
 *
 *		NONE
 *
 *
 */

void
vxtq_release_buf(void *sm_queue)
{
	vxtq_queue_t *qstate = (vxtq_queue_t *)sm_queue;
	vxt_free(qstate);
}



/*
 * vxtq_signal
 *
 * Send a change of state signal:  i.e. ATTN
 *
 * Note: when the shutdown option is asserted
 * the producer client is expected not to
 * send any more data.  The consumer will
 * flush all remaining data.  A signal will
 * be sent from the remote endpoint in
 * either case once it has quiesced.
 *
 * Flags:
 *          VXTQ_SIGNAL_STOP -  called by the receiver to 
 *	                        stop sender get/put buf action
 *                              VXTQ_SIGNAL_STOP works like
 *                              VXTQ_SIGNAL_SHUTDOWN but is
 *                              reversable and has the same
 *                              semantics as XON/XOFF.  NOTE:
 *                              VXTQ_SIGNAL_STOP should not
 *                              be used instead of streaming
 *                              Streaming is inherently more
 *                              efficient when properly set.
 *                              VXTQ_SIGNAL_STOP is meant to
 *                              allow a receiver to initiate
 *                              a queue flush without requiring
 *                              a three way handshake.
 *
 *          VXTQ_SIGNAL_START - Resume sender get/put actions
 *
 *          VXTQ_SIGNAL_SHUTDOWN - caller is shutting down
 *
 *          VXTQ_SIGNAL_CLOSE - caller is responding to shutdown
 *
 *          VXTQ_SIGNAL_ERROR - caller experienced a transport error.
 *
 *                       Note:  This is unnecessary
 *                       from an endpoint perspective
 *                       The endpoints can track 
 *                       trouble in the meta-data
 *                       portion of their communication.
 *                       This is a nod to the non-
 *                       shared memory implemtations
 *                       of transport.
 *
 *          VXTQ_SIGNAL_WAIT - caller wishes to wait for remote
 *
 * Return
 *        VXT_SUCCESS;
 *        VXT_PARM - Shutdown or closed performed with
 *                   queue in improper state 
 */

int 
vxtq_signal(void *qhandle, int flags)
{
	volatile vxtq_queue_t  *tqueue = (vxtq_queue_t *)qhandle;

	if (flags & VXTQ_SIGNAL_STOP) {
		if(tqueue->local_producer) {
			return VXT_PARM;
		} else {
			tqueue->hdr->producer_stop = 1;
		}
	}
	if (flags & VXTQ_SIGNAL_START) {
		if(tqueue->local_producer) {
			return VXT_PARM;
		} else {
			tqueue->hdr->producer_stop = 0;
		}
	}
	if (flags & VXTQ_SIGNAL_SHUTDOWN) {
		if(tqueue->local_producer) {
			UMI_LOG(18, VXTPRINT_DEBUG_LEVEL,
			        " VXTQ_SIGNAL_SHUTDOWN, local producer\n");
			tqueue->hdr->producer_suspend = 1;
		} else {
			UMI_LOG(19, VXTPRINT_DEBUG_LEVEL, 
			        " VXTQ_SIGNAL_SHUTDOWN, !local producer\n");
			tqueue->hdr->consumer_flush = 1;
		}
	}
	if (flags & VXTQ_SIGNAL_CLOSE) {
		if(!(tqueue->local_producer)) {
			if (tqueue->hdr->producer_suspend != 1) {
				return VXT_PARM;
			}
			UMI_LOG(20, VXTPRINT_DEBUG_LEVEL,
			        " VXTQ_SIGNAL_CLOSE, !local producer\n");
			tqueue->hdr->consumer_flush = 1;
		} else {
			if (tqueue->hdr->consumer_flush != 1) {
				return VXT_PARM;
			}
			UMI_LOG(21, VXTPRINT_DEBUG_LEVEL,
			        " VXTQ_SIGNAL_CLOSE, local producer\n");
			tqueue->hdr->producer_suspend = 1;
		}
	}
	if (flags & VXTQ_SIGNAL_ERROR) {
		/*
		 * Can only be cleared on reset
		 */
		tqueue->hdr->error = 1;
	}
	tqueue->signal(tqueue->signal_parm);
   
	if (flags & VXTQ_SIGNAL_WAIT) {
		tqueue->wait(tqueue->wait_parm);
	}
	return VXT_SUCCESS;
}


/*
 * vxtq_io_control:
 *
 * Implements a number of queue level IOCTLS. The
 * ioctls are a subset of the ones that occur on a device
 * The ones implemented here pertain to the queue state
 * and queue action specifically.  IO_CONTROL functions
 * are further separated.  Signal related IO command
 * actions are supported in vxtq_signal.
 * 
 * vxtq_io_control concentrates on getting and setting local
 * queue state.
 *
 * Returns:
 *
 *    VXT_SUCCESS
 *
 */

int 
vxtq_io_control(void *qhandle, int cmd, void *info, uint info_len)
{
	vxtq_queue_t       *tqueue = (vxtq_queue_t *)qhandle;

	UMI_LOG(22, VXTPRINT_DEBUG_LEVEL,
	        "called, queue %p, command = 0x%x\n", qhandle, cmd);
	if ((cmd == VXT_IOCTL_SET_SEND_HYSTERESIS) 
	    || (cmd == VXT_IOCTL_SET_RCV_HYSTERESIS)) {
		vxtq_set_hysteresis_t *stream_settings;
		stream_settings = (vxtq_set_hysteresis_t *)info;
#ifdef BASIC_ASSERT
		assert(info_len == sizeof (vxtq_set_hysteresis_t));
#else
		ASSERT(info_len == sizeof (vxtq_set_hysteresis_t),
		       "vxtq_io_control");
#endif
		tqueue->hdr->data_avail_lead =
		   stream_settings->data_avail_lead;
		tqueue->hdr->space_avail_lead =
		   stream_settings->space_avail_lead;
	} else if ((cmd == VXT_IOCTL_QUERY_SEND) 
	           || (cmd == VXT_IOCTL_QUERY_RCV)) {
		vxtq_queue_info_t *qstate;
		qstate = (vxtq_queue_info_t *)info;
#ifdef BASIC_ASSERT
		assert(info_len == sizeof (vxtq_queue_info_t));
#else
		ASSERT(info_len == sizeof (vxtq_queue_info_t),
		       "vxtq_io_control");
#endif
		UMI_LOG(23, VXTPRINT_DEBUG_LEVEL,
		        "VXT_IOCTL_QUERY_ send/receive\n");

		qstate->data_avail_lead = tqueue->hdr->data_avail_lead;
		qstate->space_avail_lead = tqueue->hdr->space_avail_lead;
		qstate->need_signal_data = tqueue->hdr->need_signal_data;
		qstate->need_signal_space = tqueue->hdr->need_signal_space;
		qstate->send_cnt = tqueue->hdr->send_cnt;
		qstate->receive_cnt = tqueue->hdr->receive_cnt;
		qstate->consumer_flush = tqueue->hdr->consumer_flush;
		qstate->producer_suspend = tqueue->hdr->producer_suspend;
		qstate->producer_stop = tqueue->hdr->producer_stop;
		qstate->error = tqueue->hdr->error;
		qstate->buf_cnt = tqueue->hdr->buf_cnt;
		qstate->buf_size = tqueue->hdr->buf_size;
		qstate->buf_index = tqueue->buf_index;
		qstate->local_producer = tqueue->local_producer;

	} else if ((cmd == VXT_IOCTL_SET_SEND_ERROR) 
	           || (cmd == VXT_IOCTL_SET_RCV_ERROR)) {
		UMI_LOG(24, VXTPRINT_DEBUG_LEVEL,
		        "VXT_IOCTL_SET_ERROR send/receive\n");
		tqueue->hdr->error =  *(int *)info;
	} else if ((cmd == VXT_IOCTL_RESET_SEND) 
	           || (cmd == VXT_IOCTL_RESET_RCV)) {
		UMI_LOG(25, VXTPRINT_DEBUG_LEVEL,
		        "VXT_IOCTL_RESET send/receive\n");
		if (tqueue->local_producer) {
			vxtq_sbuf_header_t *sub_buf_header;
			uint32_t  i;

			tqueue->hdr->need_signal_data = 0;
			tqueue->hdr->need_signal_space = 0;
			tqueue->hdr->consumer_flush = 0;
			tqueue->hdr->producer_suspend = 0;
			tqueue->hdr->producer_stop = 0;
			tqueue->hdr->error = 0;
			tqueue->hdr->send_cnt = 0;
			tqueue->hdr->receive_cnt = 0;
			tqueue->buf_index = 0;

			sub_buf_header = (vxtq_sbuf_header_t *)
			                 tqueue->buffers;
			for (i = 0; i < tqueue->hdr->buf_cnt; i++) {
				/*
				 * Producer owns the buffers, 
				 * they are empty
				 */
				sub_buf_header->owner = 1;
				sub_buf_header =
				   (vxtq_sbuf_header_t *)
				   (((char *)sub_buf_header)
				    + tqueue->hdr->buf_size);
			}
		} else {
			/*
			 * Reset only the local fields
			 */
			tqueue->buf_index = 0;
			return VXT_SUCCESS;
		}
	}
	return VXT_PARM;
}


