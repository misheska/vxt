#ifndef   VXT_QUEUE_H
#define   VXT_QUEUE_H

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

#ifndef uint
typedef unsigned int uint;
#endif

#ifndef ulong
typedef unsigned long           ulong;
#endif


#ifndef ASSERT
#define ASSERT(condition, string)           \
	if (!(condition)) { vprintf("FATAL ERROR: %s\n", string) ; exit(1); }
#endif


 /*
  * Calling flags used in get, put and signal
  */
 #define VXTQ_FLAGS_SIGNAL       0x1
 #define VXTQ_FLAGS_WAIT         0x2
 #define VXTQ_AUTO_STREAM        0x4
 #define VXTQ_IGNORE_SHUTDOWN    0x8
 #define VXTQ_FLAGS_ABORT        0x10
 #define VXTQ_SHUTDOWN           0x20

/*
 * Calling flags used in vxtq_init_buf function
 */
#define VXTQ_BUFFER_INITIALIZED  0x1
#define VXTQ_LOCAL_IS_PRODUCER   0x2


/*
 * Calling flags used in vxtq_signal function
 */

#define VXTQ_SIGNAL_SHUTDOWN   0x1
#define VXTQ_SIGNAL_CLOSE      0x2
#define VXTQ_SIGNAL_ERROR      0x4
#define VXTQ_SIGNAL_WAIT       0x8
#define VXTQ_SIGNAL_STOP       0x10
#define VXTQ_SIGNAL_START      0x20




 /*
  * Queue Control Structure:
  *
  * Shared with the remote end
  *
  * The Queue Control struct is largely private.  It is
  * exported to allow for read-only debug through the 
  * attendent device.  And also as a means of advertising
  * It's size.  The device acting as client may choose
  * to recommend a larger header size on queue init
  * as a means of introducing device specific fields
  * to the header.  This allows for direct communication
  * of device control information between the two 
  * endpoints.
  *
  * HYSTERESIS VALUES
  *
  * The consumer can set the following flags:
  *
  *   data_avail_lead
  *   need_signal_data
  *   producer_suspend - It may set
  *   consumer_flush - It may clear
  *   producer_stop - It may set or clear
  *
  * The producer can set the following flags:
  *
  *    space_avail_lead
  *    need_signal_space
  *    producer_suspend - It may clear
  *    consumer_flush - It may set
  *
  */
typedef struct vxtq_control {
   uint32_t data_avail_lead;   /* # of bufs still avail to producer */
                               /* before signal data available */
   uint32_t space_avail_lead;  /* # of empty bufs still avail to */
                               /* consumer before signal space available */
   uint32_t need_signal_data;  /* remote is waiting for data */
   uint32_t need_signal_space; /* remote is waiting for space */
   uint32_t send_cnt;
   uint32_t receive_cnt;
   uint32_t consumer_flush;    /* used before suspend or shutdown */
   uint32_t producer_suspend;  /* used before suspend or shutdown */
   uint32_t producer_stop;     /* suspend/resume I/O, use streaming */
                               /* I/O for dataflow, stop is for temp */
                               /* send disable, from rcv side */
   uint32_t error;             /* transport or endpoints have    */
                               /* experienced an error. */
   uint32_t buf_cnt;           /* number of buffers */
   uint32_t buf_size;
} vxtq_control_t; 

/*
 *
 * vxtq_queue_info_t:
 *
 * The vxtq_queue_info structure is used to pass queue state
 * out to the caller on an ioctl queue query.  The queue
 * query may be used to retrieve error information on the
 * queue or for detailed debug and trace facilities.
 *
 */

typedef struct vxtq_queue_info {
   uint32_t data_avail_lead;   /* # of bufs still avail to producer */
                               /* before signal data available */
   uint32_t space_avail_lead;  /* # of empty bufs still avail to */
                               /* consumer before signal space available */
   uint32_t need_signal_data;  /* remote is waiting for data */
   uint32_t need_signal_space; /* remote is waiting for space */
   uint32_t send_cnt;
   uint32_t receive_cnt;
   uint32_t consumer_flush;    /* used before suspend or shutdown */
   uint32_t producer_suspend;  /* used before suspend or shutdown */
   uint32_t producer_stop;     /* suspend/resume I/O, use streaming */
                               /* I/O for dataflow, stop is for temp */
                               /* send disable, from rcv side */
   uint32_t error;             /* transport or endpoints have    */
                               /* experienced an error. */
   uint32_t buf_cnt;           /* number of buffers */
   uint32_t buf_size;
   uint32_t  buf_index;      /* present pointer for sequential buf walk */
   uint32_t  local_producer; /* 1 if caller is the producer */

} vxtq_queue_info_t;

/*
 * vxtq_set_hysteresis_t
*
 * Used to communicate new settings to the vxtq code 
 * for send and receive hysteresis to use when in
 * streaming mode.
 */

typedef struct vxtq_set_hysteresis {
   int data_avail_lead;
   int space_avail_lead;
} vxtq_set_hysteresis_t;

/*
 * vxtq_signal_callback:
 *
 * vxtq_signal_callback is registered at the time
 * a queue is initialized.  The callback is exercised
 * when, during the exercise of a queue procedure
 * it is deemed appropriate to signal the remote
 * endpoint.  This may be as a result of a direct
 * request or may derive from the state of the
 * queue.  The final decision to signal is made
 * here to facilitate tight synchronization
 * between the needs_data or needs_space condition
 * and the signal.
 */ 
typedef int (*vxtq_signal_callback)(void *callback_data);

/*
 * vxtq_wait_callback:
 */
typedef int (*vxtq_wait_callback)(void *callback_data);


extern int 
vxtq_get_buf(void *tqueue_handle, int flags, void **buffer, 
             int *buf_num, int *data_len);


extern int 
vxtq_put_buf(void *tqueue_handle, int flags, void *buffer, 
             uint32_t buf_num, int data_len);

extern int
vxtq_init_buf(void *sm_buf, int length, int hdr_len, int buf_cnt, 
              int space_avail_hyster, int data_avail_hyster,
              int local_is_producer,
              vxtq_signal_callback signal,
              void *signal_parm,
              vxtq_wait_callback wait,
              void *wait_parm,
              int *sub_buf_size, void **sm_queue);

extern void
vxtq_release_buf(void *sm_queue);


extern int 
vxtq_signal(void *qhandle, int flags);

extern int 
vxtq_io_control(void *qhandle, int cmd, void *info, unsigned int info_len);

#endif /* VXT_QUEUE_H */
