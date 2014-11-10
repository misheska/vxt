#ifndef VXT_SOCKET_H
#define VXT_SOCKET_H


/*
 * vxt_socket.h
 *
 * This file contains structures and routine declarations
 * for the Symantec communications transport - socket style
 * facility.  
 *
 * In order to support a common socket framework, the fundamental 
 * shared memory transport model is wrapped in a streaming socket
 * interface.
 *
 * The vxt socket interface is intended primarily for communications
 * and message passing applications but may be used for device
 * I/O when accompanied by indirect, scatter/gather support.
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
 *   Socket send/rcv flags
 */

#define VXT_SOCK_SIGNAL          VXT_MSG_SIGNAL
#define VXT_SOCK_WAIT            VXT_MSG_WAIT
#define VXT_SOCK_AUTO_STREAM     VXT_MSG_AUTO_STREAM
#define VXT_SOCK_IGNORE_SHUTDOWN VXT_MSG_IGNORE_SHUTDOWN

/*
 * Socket signal flags
 * These must match the vxtq_signal function
 * flag definitions.
 */

#define VXTS_SIGNAL_SHUTDOWN 0x1
#define VXTS_SIGNAL_CLOSE    0x2
#define VXTS_SIGNAL_ERROR    0x4
#define VXTS_SIGNAL_WAIT     0x8
#define VXTS_SIGNAL_STOP     0x10
#define VXTS_SIGNAL_START    0x20






/*
 *  Socket State
 */

#define VXT_SOCKET_DISCONNECTED   0x1
#define VXT_SOCKET_CONNECTED      0x2
#define VXT_SOCKET_SUSPENDED      0x3
#define VXT_SOCKET_ERROR          0x4

/*
 * PF_VXT is defined as PF_UNIX, this is
 * unimportant at present as vxt_socket
 * is a stand alone library.  Should
 * vxt_socket be subordinated in future
 * under a traditional socket layer
 * PF_VXT will have to be given a 
 * unique protocol number  to allow
 * VXT traffic to reach its proper
 * destination
 */

#define PF_VXT 1  


/*
 *
 * vxt_socket_queue_info_t:
 *
 * The vxt_socket _queue_info structure is used to pass queue state
 * out to the caller on an ioctl queue query.  The queue
 * query may be used to retrieve error information on the
 * queue or for detailed debug and trace facilities.
 *
 * Note: This structure must match vxt_queue_info_t  exactly
 * or the socket interface routine will have to translate.
 *
 */

typedef struct vxt_socket_queue_info {
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

} vxt_socket_queue_info_t;



extern int
vxt_socket_lib_init(void);

/* 
 *
 * vxt_socket:
 *
 * Creates a socket and returns a descriptor
 * The newly created socket is placed in the
 * disconnected state.  Formal handles are
 * used for socket descriptors allowing
 * the caller to be of a different security
 * profile.
 *
 * vxt_socket checks for domain, type and protocol
 * compatibility and if supported, returns a 
 * handle to a new socket.
 *
 * domain: PF_VXT - all connections bear no artifacts
 * with respect to their transport.  PF_VXT is another
 * name of PF_UNIX at present.  The front end library
 * provided here cannot at present be aligned with
 * an existing socket facility because of diferences
 * in some of the naming field layouts.  In future
 * if these are reconciled, vxt_socket could be
 * subordinated and PF_VXT would be the means by
 * which we would direct incoming calls to the
 * proper library.
 *
 * type:  Only type SOCK_STREAM supported
 * protocol: 2-way reliable data connection
 *
 */

extern int vxt_socket(int domain, int type, int protocol);


/*
 * vxt_accept:
 *
 * vxt_accept attempts a connection to the endpoint determined
 * by the uname passed.  The socket passed must exist but
 * need not be connected.  It may be used for controller level
 * queries.
 *
 * Multiple connections to a unique uname endpoint
 * are not supported;  The uname refers to an endpoint 
 * location/instance already.
 *
 * listen is not presently supported so vxt_accept will return
 * with -EAGAIN if a device has not already been established.
 * When listen is supported, a means to provide a "blocking socket"
 * option will be made available.
 *
 * Returns:
 *
 *      new socket -> Success;
 *
 *      EISCON - Endpoint connection already made; 
 *               Connection may be active or in
 *               some form of fault.
 *
 *      ENETUNREACH - Symantec communications 
 *               controller is down or unreachable.
 *
 *	EAGAIN - The targeted device/connection does
 *	         not exist.
 *
 *
 */

extern int vxt_accept(int socket_handle, 
                      vxt_dev_addr_t *uname, int uname_len);



/*
 * vxt_bind:
 *
 * Attempt to add a new vxt_msg device to the vxt controller
 * The caller passes a universal name which is passed to the
 * transport authority: Dom0 vxt controller, where it is 
 * translated into either a local or remote endpoint. The
 * the underlying transport is wrapped in a  shared memory 
 * queue structure and so is hidden from the application
 * and upper communication layers.
 *
 * Endpoint to endpoint connection is entirely determined 
 * by the universal endpoint name.  Upper layers support 
 * multiple connections between endpoints by providing
 * unique universal names.
 *
 * vxt_bind must be attempted on an unconnected socket
 * The underlying devices which will supply the connection
 * should not already exist.  i.e. vxt_bind initiates
 * the attempt to establish a connection.  If a bind is
 * attempted by the remote endpoint, the caller would
 * invoke vxt_connect instead of vxt_bind.
 *
 * Returns:
 *
 *      VXT_SUCCESS => 0
 *
 *      EISCON - Device already exists
 *
 *      ENETUNREACH - Symantec communications 
 *               controller is down or unreachable.
 *
 *      ENOTSOCK - Catch-all for non-enumerated errors
 *
 * 
 */

extern int vxt_bind(int socket_handle, 
                    vxt_dev_addr_t *uname,
                    int uname_len);


/*
 * vxt_connect:
 *
 * Connects the vxt_socket passed with a set of 
 * resources to the endpoint specified by uname.
 * The nature of the underlying transport is
 * implementation specific but must conform to
 * fully performant streaming socket semantics.
 *
 * vxt_connect will link to an existing device
 * if it is in the unconnected state.  i.e. the
 * remote party has attempted to establish 
 * a connection on the uname and we have not
 * yet done a vxt_connect call.  If the appropriate
 * unconnected device is not found, vxt_connect
 * will attempt to establish the connection.
 * Thus vxt_connect, can be used in lieu of 
 * vxt_bind.
 *
 * Returns:
 *
 *      0 -> Success;
 *      EISCON - Endpoint connection already made; 
 *               Connection may be active or in
 *               some form of fault.
 *
 *      ENETUNREACH - Symantec communications 
 *               controller is down or unreachable.
 *
 *      ENOTSOCK - Catch-all for non-enumerated errors
 *
 */

extern int vxt_connect(int socket_handle, 
                       vxt_dev_addr_t *uname, 
                       int uname_len);

/*
 *
 * vxt_listen:
 *
 *
 * vxt_listen will provide an asynchronous method to 
 * discover new connections.
 *
 * Each device type in the vxt_controller has a wait queue.
 * The vxt_listen here calls the vxt_msg layer which does
 * a poll on the open controller.  The poll information 
 * indicates the type of device we are waiting on, and optionally
 * the remote address.  
 *
 * When a new device request comes up matching the criteria, the
 * vxt_listen thread returns. 
 *
 * Parms:
 *
 *   The uname parameter provides the opportunity to wait on a 
 *   specific endpoint.  If the uname_len is zero.  The thread
 *   does a generic wait on any new vxt_msg device.
 *
 *   Upon successful return, the caller should do a 
 *   VXT_IOCTL_QUERY_BUS to determine where the new device is
 *
 */

extern int vxt_listen(int socket_handle);



/*
 * vxt_send:
 *
 * vxt_send transfers supplied data on a specified connection.
 * Data is retrieved from the "buf" parameter passed by the caller
 * and up to "len" bytes are sent.  If VXT_SOCK_WAIT is not 
 * asserted, the VXT message device will write as many bytes
 * as will fit in the available send buffers and return with
 * an altered count field, ("len"),  that matches the number 
 * of bytes written.
 *
 *
 * When a vxt_send is done, the data may be retained in intermediate
 * caches.  In this wise it is unnecessary to run the transport machinery
 * on less than full buffers in a streaming context.  When an endpoint
 * requires immediate action or is at the end of a transfer it may 
 * do a send with the VXT_SOCK_SIGNAL flag asserted.  This will cause
 * a flush of all intermediate buffers and will guarantee that the 
 * remote endpoint is aware of the data. 
 *
 * Flags:  
 *       VXT_SOCK_SIGNAL  - Flush data, make certain remote
 *                          endpoint is aware of data.
 *                          (Signal when necessary)
 *
 *       VXT_SOCK_WAIT    - If there is not enough room for
 *                          data, write what can be written
 *                          and wait.  If not set, write
 *                          what data can be written and
 *                          return with altered length field
 *
 *       VXT_AUTO_STREAM - When this flag is set, signalling
 *                         is done only when the transport 
 *                         layer deems it necessary to maintain
 *                         remote participation.  The signaling
 *                         action is altered to conform with the
 *                         hysteresis values placed on the device.
 *
 *
 * Returns:
 *
 *        0 =>   - Success
 *
 *        EPIPE  - socket is not in connected state
 *
 *        EAGAIN - buffers are full and we did not
 *                 ask to wait.
 *
 *
 */

extern int vxt_send(int socket_handle, 
                    void *buf, int *len, int flags);


/*
 *
 * vxt_recv:
 *
 * Receive data on a specified socket/connection
 *
 * vxt_recv receives data on a specified connection.
 * vxt_recv will copy data to the buffer provided up to
 * the length requested.  If less data is available and
 * the VXT_SOCK_WAIT flag is not set, the "len" field is
 * altered to reflect the data found and the call returns.
 * If more data is present in the shared memory queue than
 * the receive buffer can hold, additional data is held 
 * for a subsequent vxt_receive call.
 *
 * Flags:
 *       VXT_SOCK_WAIT    - If there is not enough data to
 *                          fill the receive buffer, wait
 *                          for more before returning.
 *                          If VXT_SOCK_WAIT is not set
 *                          we will return whatever data
 *                          is available and shorten the
 *                          length field accordingly.
 *
 * Returns:
 *
 *        0 =>   - Success
 *
 *        EPIPE  - socket is not in connected state
 *
 *        EAGAIN - buffers are empty and we did not
 *                 ask to wait for data.

 */

extern int vxt_recv(int s, void *buf, 
                    size_t *len, int flags);


/*
 *
 * vxt_ioctl:
 *
 * vxt_ioctl delivers commands to communicate
 * with the vxt controller and to signal
 * specific endpoints.
 *
 * Controller actions can be underaken on a
 * socket with the socket in any connection
 * state.  This allows the caller to open a
 * socket and probe the underlying controller
 * directly without the need to establish
 * a connection to a remote endpoint.
 *
 * While many modes of operation depend on
 * the information and state associated with
 * an already established connection, the 
 * IOCTL controller query actions offer a
 * method of ground-up discovery.
 *
 * Connection oriented actions such as 
 * VXT_IOCTL_SIGNAL_ATTN must be undertaken
 * on an active connection.
 *
 * -- Controller Commands
 * VXT_IOCTL_QUERY_BUS - Return the device with the next highest slot
 *                       number after the value passed in.
 *
 * VXT_IOCTL_QUERY_DEV - Given a specific slot number, return information
 *                       on the device found there.
 *
 * -- Socket endpoint Commands
 *
 * VXT_IOCTL_SIGNAL_SEND - Signal the send queue
 * VXT_IOCTL_SIGNAL_RCV - Signal the receive queue
 * VXT_IOCTL_QUERY_SEND - Returns info on state of queue, amt of space, state
 * VXT_IOCTL_QUERY_RCV - Returns info on state of queue, amt of data, state
 * VXT_IOCTL_SET_SEND_ERROR - Set Error state on the send queue
 * VXT_IOCTL_SET_RCV_ERROR - Set Error state on the rcv queue
 * VXT_IOCTL_RESET_SEND - Reset the send queue
 * VXT_IOCTL_RESET_RCV - Reset the receive queeu
 * VXT_IOCTL_SET_SEND_HYSTERESIS - Set send hysteresis
 * VXT_IOCTL_SET_RCV_HYSTERESIS - Set receive hysteresis
 * VXT_IOCTL_SET_BUF_PARMS - Set the shared memory characteristics that
 *                           will be passed on create device calls.
 * VXT_IOCTL_TARGET_CTRLR - Set connect and add controller target default
 *
 *
 *
 *
 */


extern int vxt_ioctl(int s, int cmd, 
                     void *buffer, int arg_len);


/*
 * vxt_ioctl_buf_parms
 *
 * This buffer is sent with the VXT_IOCTL_SET_BUF_PARMS command
 * it allows the caller to customize the size and partitioning
 * of shared memory send and receive buffers at device creation
 * time.  i.e. VXT_BIND or VXT_CONNECT when the device is not
 * already present.
 *
 */

typedef struct vxt_ioctl_buf_parms {
	uint32_t recv_buf_size;
	uint32_t recv_sub_buf_cnt;
	uint32_t send_buf_size;
	uint32_t send_sub_buf_cnt;
} vxt_ioctl_buf_parms_t;

/*
 *
 * vxt_ioctl_ctrlr_parms_t:
 *
 * The data structure that is passed on the
 * vxt_ioctl of type VXT_IOCTL_TARGET_CTRLR
 * vxt_ioctl_ctrlr_parms_t contains the 
 * name of the controller which is to be 
 * targeted for connect and add activity.
 */

typedef struct vxt_ioctl_ctrlr_parms {
	char ctrlr[VXTCOM_MAX_DEV_FILE_NAME];
} vxt_ioctl_ctrlr_parms_t;


/*
 *
 * vxt_close:
 *
 *  vxt_close will unlink the targeted socket structure
 *  from the underlying vxt_msg device and its connection.
 *  Depending on the options provided it may go further,
 *  shutting down the underlying transport and removing
 *  the vxt controller device and its connection.
 *
 * Options:
 *     VXT_CLOSE_REMOTE
 *     VXT_CLOSE_LOCAL
 *
 *  If neither of the above options is asserted, the
 *  queue is flushed and quiesced.  
 *  
 *  if VXT_CLOSE_LOCAL is asserted the socket and 
 *  task level vxt_msg resources are freed and the
 *  task level is detached from the underlying device.
 *
 *  If VXT_CLOSE_REMOTE is set, the underlying devices
 *  are disconnected and the controller, "unplugs"
 *  the devices from its bus.  The remove configuration
 *  protocol is exercised and the remote device is told
 *  of the "unplugging" of the local device.
 *
 *  All combinations of options are acceptable, the
 *  VXT_CLOSE_LOCAL is unnecessary when VXT_CLOSE_REMOTE
 *  is set.
 *
 *
 * Returns:
 *
 *    0 => Success
 *    EINVAL -  handle passed does not correspond to a
 *              live socket structure.
 *
 *
 *
 */

extern int vxt_close(int socket_handle, int options);


/*
 *
 * vxt_shutdown:
 *
 *
 * vxt_shutdown operates at the queue level.
 * Shutdown is signaled to the remote endpoint
 * and the data is flushed.  A handshake is
 * employed and the call waits until all of the
 * data has been retrieved.
 *
 * The queues are left in a suspended condition
 * ready for a reset action.
 *
 * Returns:
 *
 *    0 => Success
 *    EINVAL -  handle passed does not correspond to a
 *              live socket structure.
 *
 * 
 */

extern int vxt_shutdown(int socket_handle);

/*
 *
 *  vxt_poll:
 *
 *  The pollfd field is standard and the
 *  fields for events will be respected
 *  However, only VXT socket descriptors
 *  are acceptible in the fd field.
 *
 *  As with standard poll, timeout specifies the 
 *  the time in milliseconds the poll will wait.
 *  A negative value will disable the timeout
 *  and the poll will wait until one of the
 *  events it is waiting on occur.
 *
 * vxt_poll can wait on a set of sockets and
 * on multiple events.  i.e. space available,
 * data available, and device events such as
 * shutdown or close.  vxt_poll can also 
 * wait on standard file descriptors, to
 * differentiate between the two, a new
 * flag, POLLVXT has been introduced.
 * When POLLVXT is present the associated
 * entry is considered to be a vxt socket
 * when it is not, it is considered a 
 * traditional file descriptor.  In this 
 * way, it will be possible for the caller
 * to simultaneously wait on vxt and non-vxt
 * events using the same thread.

 * POLLVXT - uses the following define:
 * #define POLLVXT 0x2000 
 * Maintainers of this code must be
 * careful if there is an extension of
 * bits usage in pollfd.
 *
 * When POLLVXT is found we re-define the
 * bits around it, this allows for the
 * flexible reuse of bits needed for
 * POLLVXT_DEV and POLLVXT_CTRLR
 *
 * Returns:
 *
 *    0 => Success
 *    EINVAL -  handle passed does not correspond to a
 *              live socket structure.
 *
 *
 *   
 */

extern int vxt_poll(vxt_poll_obj_t *fds, 
                    int nfds, int timeout);

#endif /* VXT_SOCKET_H */
