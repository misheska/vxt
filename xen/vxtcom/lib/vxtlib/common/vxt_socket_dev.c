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

#define _VXT_COMPONENT_ 4
#define _VXT_SUBSYSTEM_ 13


#include <vxt_lib_os.h>
#include <vxt_system.h>
#include <vxt_msg_export.h>
#include <symcom_dev_table.h>
#include <vxt_com.h>
#include <vxt_msg_dev.h>
#include <vxt_msg.h>
#include <vxt_socket.h>
#include <vxt_msg_os.h>



int vxt_socket_lock_inited = 0;
vxt_mutex_t vxt_socket_lock;

/*
 *
 * vxt_socket_dev.c
 *
 * A socket interface provided for the Symantec shared memory
 * transport communication service.  The add_msg sub structure
 * is filled with the shared memory buffer characteristics that
 * will be provided on vxt_msg_add calls.  These may be 
 * overridden by a socket ioctl.
 *
 */


typedef struct vxt_sock {
	int    ref_cnt;
	int    close_options;
        int    handle;
        int    state;
	char   target_ctrlr[VXTCOM_MAX_DEV_FILE_NAME];
        void   *dev;
	vxt_msg_buf_parms_t add_msg;
        struct vxt_sock *next;
} vxt_sock_t;



vxt_sock_t *live_sockets = NULL;

/*
 * 4 million lifetime handles
 */
uint32_t socket_handles = 1;
uint32_t socket_wrap = 0;



/*
 *
 * vxt_find_socket:
 *
 * traverse list of sockets to find the structure corresponding
 * to the handle passed.  If found, the pointer to the
 * associated structure is returned. 
 *
 * Note: vxt_find_socket is also called by get_socket when
 * the socket name count has wrapped.  In this case the find_socket
 * is a test. i.e. we do not want to reference the socket structure
 * This means we do not need to raise the socket structure reference
 * count.  Further, we call with the global socket structure lock
 * held.
 *
 *
 * Returns:
 *
 *    0 => Success
 *    EINVAL -  handle passed does not correspond to a 
 *              live socket structure.
 *
 */

static int
vxt_find_socket(int handle, vxt_sock_t **user_socket, int test) {

	vxt_sock_t *socket;
	
	if (test == 0) {
   		vxt_lock(vxt_socket_lock);
	}
	socket = live_sockets;
	while (socket != NULL) {
		if (socket->handle == handle) {
			*user_socket = socket;
			if (test == 0) {
				socket->ref_cnt++;
   				vxt_unlock(vxt_socket_lock);
			}
			return 0;
		}
		socket = socket->next;
	}
	if (test == 0) {
		vxt_unlock(vxt_socket_lock);
	}
	return ENOTSOCK;
   
}


/*
 * vxt_free_socket:
 *
 * Looks up a socket in the live_sockets
 * table via the handle passed.  If
 * an entry is found, and it is in the
 * disconnected or error state, it is
 * freed.
 *
 *
 * Returns:
 *
 *   0  =>   Success
 *
 *   EBUSY - Socket was found but it is still connected
 *
 *   ENOTSOCK - No socket found with the specified handle
 *
 */

static int
vxt_free_socket(uint old_handle)
{
	vxt_sock_t *socket;
	vxt_sock_t *target;

   	vxt_lock(vxt_socket_lock);
	socket = live_sockets;

	if (socket == NULL) {
   		vxt_unlock(vxt_socket_lock);
		return ENOTSOCK;
	}

	if (socket->handle == old_handle) {
		live_sockets = live_sockets->next;
   		vxt_unlock(vxt_socket_lock);
		vxt_free(socket);
		return 0;
	}

	while (socket->next != NULL) {
		if (socket->next->handle == old_handle) {
			if (socket->next->state == VXT_SOCKET_CONNECTED) {
   				vxt_unlock(vxt_socket_lock);
				return EBUSY;
			}
			target = socket->next;
			socket->next = socket->next->next;
   			vxt_unlock(vxt_socket_lock);
			vxt_free(target);
			return 0;
		}
		socket = socket->next;
	}

  	vxt_unlock(vxt_socket_lock);
	return ENOTSOCK;
}


/*
 *
 * vxt_deref_socket:  - Internal Routine
 *
 * vxt_deref_socket will decrement the reference count
 * against the targeted socket.  If the count goes to
 * zero the socket is freed and any underlying connections
 * are severed.  The associated device is also removed
 * if the close options indicate that it should.  These
 * options are set by the vxt_close call.  For a socket
 * enjoying a normal life, the vxt_close call is the only
 * way that the socket can be destroyed and so the options
 * field will be filled in with the clients instructions.
 * If the socket is freed during the creation process
 * the options are set to the minimal cleanup.
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
 *
 *  All combinations of options are acceptible.
 */

static int
vxt_deref_socket(vxt_sock_t *socket)
{
	int ret;

	UMI_LOG(16, VXTPRINT_DEBUG_LEVEL,
	        "called on socket %p, ref_cnt %d\n", socket, socket->ref_cnt);
   	vxt_lock(vxt_socket_lock);
	if (socket->ref_cnt > 1) {
		socket->ref_cnt--;
   		vxt_unlock(vxt_socket_lock);
		return 0;
	}
   	vxt_unlock(vxt_socket_lock);
	if (socket->ref_cnt != 1) {
		UMI_WARN(14, VXTPRINT_PRODUCT_LEVEL, "negative ref count\n");
	}
	UMI_LOG(8, VXTPRINT_DEBUG_LEVEL,
	        "calling vxt_shutdown, socket state %d, socket flags %d\n",
	        socket->state, socket->close_options);
	ret = vxt_shutdown(socket->handle);
	if (ret) {
		/*
		 * Ignore, the user may have done an
		 * explicit shutdown
		 */
	}
	if ((socket->state == VXT_SOCKET_SUSPENDED) && 
	    (socket->close_options & (VXT_CLOSE_REMOTE | VXT_CLOSE_LOCAL))) {
		UMI_LOG(9, VXTPRINT_DEBUG_LEVEL,
		        "calling vxt_msg_disconnect\n");
		vxt_msg_disconnect(socket->dev, socket->close_options);
	}

	vxt_free_socket(socket->handle);

	return 0;
}



/*
 *
 * vxt_get_socket:
 *
 * Internal routine to retrieve a socket structure
 * Simple implementation allocates handles from
 * an incrementing counter.  When the counter
 * wraps, possible handle values are checked
 * sequentially.  Because of this, significant
 * delays can be encountered if large numbers
 * of sequential sockets remain active. 
 *
 * Upgrades to handle lookup and handle
 * acquisition in future may include a random
 * number generator to scatter the acquisitions
 * around the number space.  vxt_get_socket
 * and vxt_find_socket are self-contained so
 * any changes made will not require changes
 * to the callers.
 *
 * vxt_get_socket allocates a new structre
 * attaches a handle to it and places it in
 * the live socket repository.  The socket
 * structure and handle are returned to
 * the caller.
 *
 */

static int
vxt_get_socket(vxt_sock_t **new_socket)
{
	vxt_sock_t *socket;
	int        ret;

	socket = (vxt_sock_t *)vxt_malloc(sizeof(vxt_sock_t));
	if (socket == NULL) {
		return ENOMEM;
	}

  	vxt_lock(vxt_socket_lock);
	/*
	 * Protect global fields from re-entry
	 */
	if (socket_handles == 0xFFFFFFFF) {
		/*
		 * From here on we must check all new
		 * requests to see if handle already
		 * in use
		 */
		socket_handles = 1;
		socket_wrap = 1;
      
	}

	if (socket_wrap) {
		vxt_sock_t *old_socket;
		uint handle_index;

		handle_index = socket_handles - 1;
		while(handle_index !=  socket_handles) {
			ret = vxt_find_socket(socket_handles, &old_socket, 1);
			if (ret == 0) {
				/*
				 * oops, handle in use, try next one
				 */
				socket_handles++;
				vxt_deref_socket(old_socket);
			} else {
				break;
			}
		}
		if (socket_handles == handle_index) {
			vxt_free(socket);
			*new_socket = NULL;
   			vxt_unlock(vxt_socket_lock);
			return ENFILE;
		}
	}

	socket->ref_cnt = 1;
	socket->close_options = 0;
	socket->handle = socket_handles;
	socket->state = VXT_SOCKET_DISCONNECTED;
	socket->dev = NULL;
	socket_handles++;
	socket->next = live_sockets;
	live_sockets = socket;
	/*
	 * Set the default shared memory buffer fields 
	 * for the bind call.  These may be overridden
	 * with a socket ioctl
	 */
	
	socket->add_msg.send_buf_size = VXT_MSG_DEFAULT_QUEUE_LENGTH;
	socket->add_msg.recv_buf_size = VXT_MSG_DEFAULT_QUEUE_LENGTH;
	socket->add_msg.send_sub_buf_cnt = VXT_MSG_DEFAULT_BUFCNT;
	socket->add_msg.recv_sub_buf_cnt = VXT_MSG_DEFAULT_BUFCNT;
	socket->target_ctrlr[0] = 0;

	*new_socket = socket;

	vxt_unlock(vxt_socket_lock);
	return 0;
   
}


/* 
 *
 * vxt_socket:
 *
 * Creates a socket and returns a descriptor
 *
 * vxt_socket checks for domain, type and protocol
 * compatibility and if supported, returns a 
 * handle to a new socket.
 *
 * domain: PF_UNIX - all connections bear no artifacts
 * with respect to their transport
 * type:  Only type SOCK_STREAM supported
 * protocol: 2-way reliable data connection
 *
 * Returns:
 *
 *        socket handle  =>  Success
 *        0 - Failure
 *           errno:
 *
 *                EAFNOSUPPORT -  bad domain
 *                EACCES - bad type
 *                EINVAL - wrong protocol
 *
 *
 *
 */

int
vxt_socket(int domain, int type, int protocol)
{
	vxt_sock_t *socket;
	int ret;

	if (domain != PF_UNIX) {
		errno = EAFNOSUPPORT;
		return 0;
	}
	if (type != SOCK_STREAM) {
		errno = EACCES;
		return 0;
	}
	if (protocol != SOCK_STREAM) {
		errno = EINVAL;
		return 0;
	}
	ret = vxt_get_socket(&socket);
	if (ret) {
		errno = ret;
		return 0;
	}
	return socket->handle;
}


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
 * Returns:
 *
 *      VXT_SUCCESS => 0
 *
 *      EISCONN - Device already exists
 *
 *      ENETUNREACH - Symantec communications 
 *               controller is down or unreachable.
 *
 *      ENOTSOCK - Catch-all for non-enumerated errors
 *
 * 
 */

int
vxt_bind(int socket_handle, vxt_dev_addr_t *uname, int uname_len)
{
	vxt_sock_t *socket;
	void *msg_dev_handle;
	int  ret;

	ret = vxt_find_socket(socket_handle, &socket, 0);
	if (ret) {
		errno = ret;
		return 1;
	}
 
	if (socket->state != VXT_SOCKET_DISCONNECTED) {
		vxt_deref_socket(socket);
		errno = EISCONN;
		return 1;
	}

	UMI_LOG(1, VXTPRINT_DEBUG_LEVEL,
	        "calling vxt_msg_add, smb parms rlen %08x, rbufcnt %08x, "
		"slen %08x, sbufcnt %08x\n",
		socket->add_msg.send_buf_size, socket->add_msg.recv_buf_size,
	        socket->add_msg.send_sub_buf_cnt,
	        socket->add_msg.recv_sub_buf_cnt);

	ret = vxt_msg_add(VXTCTRLR_MSG_DEV, uname, uname_len,
	                  &(socket->add_msg), &msg_dev_handle);

	if (ret != VXT_SUCCESS) {
		if (ret == VXT_BUSY) {
			vxt_deref_socket(socket);
			errno = EISCONN;
			return 1;
		}
		if (ret == VXT_DOWN) {
			vxt_deref_socket(socket);
			errno = ENETUNREACH ;
			return 1;
		}
		errno = ENOTSOCK ;
		vxt_deref_socket(socket);
		return 1;
	}

	socket->dev = msg_dev_handle;
	socket->state = VXT_SOCKET_CONNECTED;
	vxt_deref_socket(socket);
	return 0;
}


/*
 *
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
 *      EISCONN - Endpoint connection already made;
 *                Connection may be active or in
 *                some form of fault.
 *
 *      EAGAIN - The targeted device/connection does
 *               not exist.
 *
 *
 *
 */

int
vxt_accept(int socket_handle, vxt_dev_addr_t *sock_name, int uname_len)
{
	vxt_query_bus_struct_t bus_info;
	vxt_query_sys_struct_t sys_info;
	int ret = VXT_SUCCESS;
	vxt_sock_t *socket;

	/*
	 * get the "reaching" controller for
	 * the targeted uname.
	 */

	vxtcom_strncpy(sys_info.uname, sock_name->uname, MAX_VXT_UNAME);
	vxtcom_strncpy(sys_info.ep_id, sock_name->ep_id, MAX_VXT_UUID);
	sys_info.ctrlr_name[0] = 0;
	sys_info.controller = 0;
	sys_info.domain = 0;
	ret = vxt_msg_system_query(&sys_info);
	if (ret != VXT_SUCCESS) {
		errno = ENETUNREACH;
		return 1;
	}


	bus_info.vxt_bus_id = 0;
	vxtcom_strncpy(bus_info.ctrlr_name, 
	               sys_info.ctrlr_name, VXTCOM_MAX_DEV_FILE_NAME);
	bus_info.uname[0] = 0;

	while (ret == VXT_SUCCESS) {
		if (!strncmp(bus_info.uname, 
		             sock_name->uname, MAX_VXT_UNAME)) {
			/*
			 * We have a device answering to the correct
			 * uname
			 */
			break;
		}
		ret = vxt_msg_bus_query(&bus_info);
        }
	if (ret != VXT_SUCCESS) {
		errno = EAGAIN;
		return 0;
	}

	ret = vxt_get_socket(&socket);
	if (ret) {
	   errno = ret;
	   return 0;
	}
   
	/*
	 * No need to raise the reference count here, we are
	 * the only holder of the socket
	 */
   	ret = vxt_connect(socket->handle, sock_name, uname_len);
	if (ret != 0) {
		vxt_free_socket(socket->handle);
		if (errno == ENOTSOCK) {
			errno = EAGAIN;
		}
		return 0;
	}
	return socket->handle;
}


/*
 * vxt_connect:
 *
 * Connect a vxt_socket with a set of resources to the
 * endpoint specified by uname.  The nature of the
 * underlying transport is implementation specific
 * but must conform to fully performant streaming
 * socket semantics.
 *
 * Returns:
 *
 *      0 -> Success;
 *      EISCONN - Endpoint connection already made; 
 *                Connection may be active or in
 *                some form of fault.
 *
 *      ENETUNREACH - Symantec communications 
 *               controller is down or unreachable.
 *
 *      ENOTSOCK - Catch-all for non-enumerated errors
 *
 */

int
vxt_connect(int socket_handle, vxt_dev_addr_t *uname, int uname_len)
{
	vxt_sock_t *socket;
	void *msg_dev_handle;
	int  ret;

	ret = vxt_find_socket(socket_handle, &socket, 0);
	if (ret) {
		errno = ret;
		return 1;
	}
 
	if (socket->state != VXT_SOCKET_DISCONNECTED) {
		UMI_WARN(2, VXTPRINT_PRODUCT_LEVEL,
		         "socket not in disconnected state\n");
		errno = EISCONN;
		vxt_deref_socket(socket);
		return 1;
	}

	ret = vxt_msg_connect(VXTCTRLR_MSG_DEV, uname, uname_len,
	                      socket->target_ctrlr, &(socket->add_msg),
	                      &msg_dev_handle);
	UMI_LOG(3, VXTPRINT_DEBUG_LEVEL,
	        "vxt_msg_connect succeeded handle %p\n",
	        msg_dev_handle);

	if (ret != VXT_SUCCESS) {
		UMI_WARN(4, VXTPRINT_PRODUCT_LEVEL, "vxt_msg_connect failed\n");
		if (ret == VXT_BUSY) {
			errno = EISCONN;
			vxt_deref_socket(socket);
			return 1;
		}
		if (ret == VXT_DOWN) {
			errno = ENETUNREACH;
			vxt_deref_socket(socket);
			return 1;
		}
		errno = ENOTSOCK;
		vxt_deref_socket(socket);
		return 1;
	}

	socket->dev = msg_dev_handle;
	socket->state = VXT_SOCKET_CONNECTED;
	vxt_deref_socket(socket);
	return 0;
}


/*
 *
 * vxt_listen:
 *
 * vxt_listen provides an asynchronous method to 
 * discover new connections.
 *
 * Unlike a socket listen, vxt_listen does not attempt
 * a connection.  It simply returns and allows the caller
 * to do a query and connection.
 *
 * vxt_listen can be called on a connected or an unconnected
 * socket.  When called on a connected socket, vxt_listen
 * will wait on the controller associated with the socket.
 * When called on an unconnected socket, vxt_listen will
 * wait on all controllers.
 *
 * vxt_list will wait until a new device is detected on
 * its target controller or set of controllers.
 *
 *
 * Upon successful return, the caller should do a 
 * VXT_IOCTL_QUERY_BUS to determine where the new device is
 *
 */

int
vxt_listen(int socket_handle)
{
	vxt_sock_t *socket;
	int        ret;

	ret = vxt_find_socket(socket_handle, &socket, 0);
	if (ret) {
	   errno = ret;
	   return 1;
	}

	if (socket->state != VXT_SOCKET_CONNECTED) {
		ret = vxt_msg_ctrlr_wait(NULL);
	} else {
		ret = vxt_msg_ctrlr_wait(socket->dev);
	}
	
	if (ret != VXT_SUCCESS) {
		if (ret == VXT_PARM) {
			errno = EINVAL;
		} else if (ret == VXT_ABORT) {
			errno = EINTR;
		} else if (ret == VXT_NOMEM) {
			errno = ENOMEM;
		}
		vxt_deref_socket(socket);
		return 1;
	}

	vxt_deref_socket(socket);
	return 0;
}


/*
 * vxt_send:
 *
 * Send supplied data on a specified connection
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
 *
 *       VXT_SOCK_WAIT    - If there is not enough room for
 *                          data, write what can be written
 *                          and wait.  If not set, write
 *                          what data can be written and
 *                          return with altered length field
 *
 *       VXT_AUTO_STREAM - Signalling is done only when the
 *                         transport layer deems it necessary
 *                         to maintain remote participation.
 *
 *
 * Returns:
 *
 *        Upon Success: return the number of bytes sent
 *
 *	  Upon Failure: return -1 and an errno as follows:
 *
 *	        EPIPE  - socket is not in connected state
 *			 or serious internal error
 *              EXDEV  - Device is shutting down
 *              EBUSY  - Device is stoped by the receiver
 *              ENOSPC - The send buffers are full
 *              EAGAIN - Awakened out of a wait but no buffers
 *                       available
 *              ENOTSOCK - Indirectly sent from vxt_find_socket
 *                       The socket handle is not valid.
 *                       
 *
 */

int
vxt_send(int socket_handle, void *buf, int *len, int flags)
{
   
	vxt_sock_t *socket;
	int        ret;

	ret = vxt_find_socket(socket_handle, &socket, 0);
	if (ret) {
		errno = ret;
		return -1;
	}

	if (socket->state != VXT_SOCKET_CONNECTED) {
		errno =  EPIPE;
		vxt_deref_socket(socket);
		return -1;
	}

	/*
	 * Note: Flags are engineered to match the underlying vxt_msg
	 * flags locations and polarity.
	 */

	/*
	 * Note:  Signal/Wait/Auto Stream flags have
	 * all been set up to match between 
	 * socket and message layers i.e.
	 * VXT_MSG_SIGNAL = VXT_SOCK_SIGNAL
	 */
	ret = vxt_msg_send(socket->dev, buf, len, flags);
	UMI_LOG(5, VXTPRINT_DEBUG_LEVEL, "return from vxt_msg_send\n");

	if (ret) {
		if (ret == VXT_BUF_SPACE) {
			/*
			 * Not actually an error.  If the WAIT flag is
			 * not set and there is not enough room for 
			 * "len" bytes.  What bytes can be written 
			 * are placed in the send buffers and the
			 * length field is updated to correspond
			 * to the number of bytes sent.  The caller
			 * must poll or take some other action to
			 * know when more space will be made available.
			 */
			vxt_deref_socket(socket);
			return *len;
		}

		if (ret == VXT_SHUTTING_DOWN) {
			/*
			 * We have been alerted by the remote
			 * endpoint that the queue is being shut
			 * down.  The caller must be told so that
			 * it can flush and exit.
			 */
			errno = EXDEV;
		} else if (ret == VXT_BLOCKED) {
			/*
			 * I/O stopped by the receiver
			 */
			errno = EBUSY;
		} else if (ret == VXT_RSRC) {
			errno = ENOSPC;
		} else if (ret == VXT_ABORT) {
			errno = EAGAIN;
		} else {
			errno = EPIPE;
		}

		vxt_deref_socket(socket);
		return -1;
	}

	/*
	 * Return the number of bytes sent,  in this case
	 * all of them. vxt_msg_send returns this value in 
	 * the original length field.
	 */
	vxt_deref_socket(socket);
	return *len;
   
}


/*
 *
 * vxt_recv:
 *
 * Receive data on a specified socket/connection
 *
 * vxt_recv will fill in data up to the length requested
 * If less data is available and the VXT_SOCK_WAIT flag
 * is not set the len field is altered to reflect the
 * data found and the call returns.  If more data is
 * available than the provided receive buffer can hold
 * additional data is held for a subsequent vxt_receive
 * call.
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
 *        Upon Success:  returns the number of bytes
 *	                 received;
 *
 *	  Upon error: returns -1 and fills in errno as follows:
 *
 *	        EPIPE  - socket is not in connected state
 *
 *
 */

int
vxt_recv(int socket_handle, void *buf, size_t *len, int flags)
{
	vxt_sock_t *socket;
	int        ret;

	ret = vxt_find_socket(socket_handle, &socket, 0);
	if (ret) {
		errno = ret;
		return -1;
	}

	if (socket->state != VXT_SOCKET_CONNECTED) {
		errno =  EPIPE;
		vxt_deref_socket(socket);
		return -1;
	}

	errno = 0;

	/*
	 * Note:  Signal/Wait/Auto Stream flags have
	 * all been set up to match between 
	 * socket and message layers i.e.
	 * VXT_MSG_SIGNAL = VXT_SOCK_SIGNAL
	 */
	ret = vxt_msg_rcv(socket->dev, buf, len, flags);
	UMI_LOG(6, VXTPRINT_DEBUG_LEVEL, 
	        "returning from vxt_msg_rcv, ret = %lu\n",
	        (long)ret);
	if (ret) {
		if ((ret == VXT_BUF_SPACE) || (ret == VXT_RSRC)) {
			errno = EAGAIN;
			vxt_deref_socket(socket);
			return *len;
		} else if (ret == VXT_BLOCKED) {
			/*
			 * I/O stopped by the receiver
			 */
			errno = EBUSY;
			vxt_deref_socket(socket);
			return *len;
		}
		if (ret == VXT_ABORT) {
			errno = EAGAIN;
			vxt_deref_socket(socket);
			return *len;
		}
		if (ret == VXT_SHUTTING_DOWN) {
			/*
			 * We have been alerted by the remote
			 * endpoint that the queue is being shut
			 * down.  The caller must be told so that
			 * it can flush and exit.
			 */
			errno = EXDEV;
		} else {
			errno = EPIPE;
		}

		vxt_deref_socket(socket);
		return -1;
	}
   
	vxt_deref_socket(socket);
	return *len;
}


/*
 *
 * vxt_ioctl:
 *
 * IOCTLs to communicate with the symantec
 * controller and signal specific endpoints. 
 *
 * Controller actions can be undertaken on
 * a socket in any connection state.  This
 * allows the caller to open a socket and
 * probe the underlying controller directly.
 *
 * While the expected mode of operation in
 * many instances relies on existential,
 * apriori connection information, the IOCTL
 * controller action offers a method of 
 * ground-up discovery.
 *
 * Connection oriented actions such as 
 * VXT_IOCTL_SIGNAL_ATTN must be undertaken
 * on an active connection.
 *
 *
 * VXT_IOCTL_SIGNAL_ATTN
 * VXT_IOCTL_QUERY_BUS
 * VXT_IOCTL_QUERY_DEV
 * VXT_IOCTL_QUERY_SYS
 *
 *
 */

int
vxt_ioctl(int s, int cmd, void *buffer, int arg_len)
{
	vxt_sock_t *socket;
	int        ret;

	UMI_LOG(7, VXTPRINT_DEBUG_LEVEL, "cmd = 0x%x\n", cmd);

	ret = vxt_find_socket(s, &socket, 0);
	if (ret) {
		errno = ret;
		return 1;
	}
	if ((cmd == VXT_IOCTL_SIGNAL_SEND)  || 
	    (cmd == VXT_IOCTL_SIGNAL_RCV)  || 
	    (cmd == VXT_IOCTL_QUERY_SEND) ||
	    (cmd == VXT_IOCTL_QUERY_RCV) || 
	    (cmd == VXT_IOCTL_SET_SEND_ERROR) ||
	    (cmd == VXT_IOCTL_SET_RCV_ERROR) ||
	    (cmd == VXT_IOCTL_RESET_SEND) ||
	    (cmd == VXT_IOCTL_RESET_RCV) ||
	    (cmd == VXT_IOCTL_SET_SEND_HYSTERESIS) ||
	    (cmd == VXT_IOCTL_QUERY_BIND) ||
	    (cmd == VXT_IOCTL_SET_RCV_HYSTERESIS)) {
		if ((socket->state != VXT_SOCKET_CONNECTED) ||
		    (socket->state == VXT_SOCKET_ERROR)) {
			errno = EINVAL;
			vxt_deref_socket(socket);
			return 1;
		}

		ret = vxt_msg_control(socket->dev, cmd, buffer, arg_len);
	}

	if (cmd == VXT_IOCTL_UNPLUG_DEV) {
		/*
		 * Note: socket->dev may be NULL, we do not need an
		 * attached device on this call
		 */
		ret = vxt_msg_unplug_dev(socket->dev, 
		                         (vxt_unplug_dev_struct_t *)buffer);
	}

	if (cmd == VXT_IOCTL_QUERY_BIND) {
		ret = vxt_msg_bind_query(socket->dev,
		                         (vxt_query_bind_struct_t *)buffer);
	}

	if (cmd == VXT_IOCTL_QUERY_DEV) {
		ret = vxt_msg_dev_query((vxt_query_dev_struct_t *)buffer);
	}
  
	if (cmd == VXT_IOCTL_QUERY_BUS) {
		ret = vxt_msg_bus_query((vxt_query_bus_struct_t *)buffer);
	}
	if (cmd == VXT_IOCTL_QUERY_SYS) {
		ret = vxt_msg_system_query((vxt_query_sys_struct_t *)buffer);
	}
	if (cmd == VXT_IOCTL_SET_BUF_PARMS) {
		vxt_ioctl_buf_parms_t *buf_parms;

		buf_parms = (vxt_ioctl_buf_parms_t *)buffer;
		if (socket->state == VXT_SOCKET_CONNECTED) {
			errno = EINVAL;
			vxt_deref_socket(socket);
			return 1;
		}
		socket->add_msg.send_buf_size = buf_parms->send_buf_size;
		socket->add_msg.recv_buf_size = buf_parms->recv_buf_size;
		socket->add_msg.send_sub_buf_cnt = buf_parms->send_sub_buf_cnt;
		socket->add_msg.recv_sub_buf_cnt = buf_parms->recv_sub_buf_cnt;
		vxt_deref_socket(socket);
		return 0;
	}
	if (cmd == VXT_IOCTL_TARGET_CTRLR) {
		vxt_ioctl_ctrlr_parms_t *ctrlr_parms;

		ctrlr_parms = (vxt_ioctl_ctrlr_parms_t *)buffer;
		if (socket->state == VXT_SOCKET_CONNECTED) {
			errno = EINVAL;
			vxt_deref_socket(socket);
			return 1;
		}
		vxtcom_strncpy(socket->target_ctrlr,
		               ctrlr_parms->ctrlr, VXTCOM_MAX_DEV_FILE_NAME);
		vxt_deref_socket(socket);
		return 0;
	}
	if (ret != VXT_SUCCESS) {

		if (ret == VXT_PARM) {
			errno = EINVAL;
		} else if (VXT_BUSY) {
		   errno = EBUSY;
		} else {
		   errno = EINVAL;
		}
		vxt_deref_socket(socket);
		return 1;
	}
  
	if (cmd == VXT_IOCTL_CTRLR_PLUG) {
		vxt_msg_plug_poll_t *plug_parms;
		plug_parms = (vxt_msg_plug_poll_t *)buffer;
		ret = vxt_msg_ctrlr_plug_event(plug_parms->timeout, 
		                               &(plug_parms->revent));
		if (ret != VXT_SUCCESS) {
			vxt_deref_socket(socket);
			return 1;
		}
	}

	if (cmd == VXT_IOCTL_CTRLR_POLL) {
		vxt_msg_plug_poll_t *plug_parms;
		plug_parms = (vxt_msg_plug_poll_t *)buffer;

		if (socket->state == VXT_SOCKET_CONNECTED) {
			ret = vxt_msg_poll_ctrlr(socket, plug_parms->timeout,
			                         &(plug_parms->revent));
		} else {
			ret = vxt_msg_poll_ctrlr(NULL, plug_parms->timeout,
			                         &(plug_parms->revent));
		}
		if (ret != VXT_SUCCESS) {
			vxt_deref_socket(socket);
			return 1;
		}
	}

	vxt_deref_socket(socket);
	return 0;

   
}


/*
 *
 * vxt_close:
 *
 * Shutdown the underlying transport and remove the
 * connection.
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
 *
 *  All combinations of options are acceptible.
 */

int
vxt_close(int socket_handle, int options)
{
	vxt_sock_t *socket;
	int        ret;

	ret = vxt_find_socket(socket_handle, &socket, 0);
	if (ret) {
		errno = ret;
		return 1;
	}


	if (options & VXT_CLOSE_FORCE) {

		/*
		 * User wants to close this socket even
		 * if there are threads waiting for receive
		 * send, or polling.  This option is only
		 * used when a process is shutting down and
		 * the programmer wants to make certain the 
		 * vxt device is destroyed.
		 */

		UMI_LOG(17, VXTPRINT_DEBUG_LEVEL,
	        	"calling vxt_shutdown, "
		        "socket state %d, socket flags %d\n",
	        	socket->state, options);
	
		vxt_shutdown(socket->handle);

		if ((options & 
		     (VXT_CLOSE_REMOTE | VXT_CLOSE_LOCAL))) {
			UMI_LOG(18, VXTPRINT_DEBUG_LEVEL,
			        "calling vxt_msg_disconnect\n");
			vxt_msg_disconnect(socket->dev, options);
		}

		socket->close_options = 0;

	} else {
		/*
		 * We will close with the caller's options whether
		 * or not the close is deferred by an elevated reference count
		 */
		socket->close_options = options;
	}



	/*
	 * Drop the reference gained in vxt_find_socket
	 */
	
	vxt_deref_socket(socket);
	/*
	 * Drop the live socket reference
	 */
	vxt_deref_socket(socket);
	return 0;
}


/*
 *
 * vxt_shutdown:
 *
 * Equivalent to vxt_disconnect, signal shutdown to the remote
 * endpoint.  Stop the queues and return when both sides are
 * quiesced.
 * 
 */

int
vxt_shutdown(int socket_handle)
{
      
	vxt_sock_t *socket;
	int        ret;

	ret = vxt_find_socket(socket_handle, &socket, 0);
	if (ret) {
		errno = ret;
		return 1;
	}
	if (socket->state == VXT_SOCKET_CONNECTED) {
		ret = vxt_msg_shutdown(socket);
		if (ret != VXT_SUCCESS) {
			errno = EINVAL;
			vxt_deref_socket(socket);
			return 1;
		}
		socket->state = VXT_SOCKET_SUSPENDED;
	}

	vxt_deref_socket(socket);
	return 0;
}


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
 *  POLLVXT - For above behaviour, 
 *  #define POLLVXT 0x2000  Must be
 *  careful if there is an extension of
 *  bits.
 *
 *  Returns:
 *
 *		Upon success:  The number of events seen, zero if
 *		               poll timed-out without finding an event
 *
 *		Upon failure:  -1 and the errno is filled in as follows:
 *
 *			EINVAL - VXT_PARM
 *
 *			EINTR - VXT_ABORT
 *
 *			ENOMEM - VXT_NOMEM
 *   
 */

int
vxt_poll(vxt_poll_obj_t *fds, int nfds, int timeout)
{
	void           *vxt_fds;
	uint64_t       object;
	uint32_t       events;
	vxt_sock_t     **socket;
	int            event_count;
	int            i, j;
	int            ret;

	/*
	 * translate fds->fd's into vxt_msg instance handles
	 */
	UMI_LOG(10, VXTPRINT_DEBUG_LEVEL, "called\n");

	vxt_fds = vxt_malloc_poll_obj(nfds);
	if (vxt_fds == NULL) {
		errno = ENOMEM;
		return -1;
	}
	socket = (vxt_sock_t **)vxt_malloc(sizeof(vxt_sock_t *) * nfds);
	if (socket == NULL) {
		vxt_free_poll_obj(vxt_fds, nfds);
		errno = ENOMEM;
		return -1;
	}
	for (i = 0; i < nfds; i++) {
		if (fds[i].events & POLLVXT) {
			UMI_LOG(11, VXTPRINT_DEBUG_LEVEL,
			        "poll on socket data\n");
			/*
			 * One of ours, translate from socket
			 * handle to internal socket structure
			 */
			ret = vxt_find_socket((int)fds[i].object,
			                      &(socket[i]), 0);
			if (ret) {
				UMI_WARN(12, VXTPRINT_PRODUCT_LEVEL,
				         "invalid socket handle\n");
				errno = ret;
				for (j = 0; j < i; j++) {
					if (socket[i]) {
						vxt_deref_socket(socket[j]);
					}
				}
				vxt_free_poll_obj(vxt_fds, nfds);
				vxt_free(socket);
				return -1;
			}
			object = (uint64_t)(vxtarch_word)(socket[i])->dev;
		} else {
			socket[i] = 0;
			object = fds[i].object;
		}
		UMI_LOG(13, VXTPRINT_DEBUG_LEVEL, "translate fds entry\n");
		vxt_write_event_wait(vxt_fds, i, object, fds[i].events, 0);
	}
	ret = vxt_msg_wait(vxt_fds, nfds, timeout, &event_count);

	for (i = 0; i < nfds; i++) {
		vxt_read_event_wait(vxt_fds, i, &object, &events,
		                    &fds[i].revents);
	}

	if (ret != VXT_SUCCESS) {
		if (ret == VXT_PARM) {
			errno = EINVAL;
		} else if (ret == VXT_ABORT) {
			errno = EINTR;
		} else if (ret == VXT_NOMEM) {
			errno = ENOMEM;
		}
		for (i = 0; i < nfds; i++) {
			if (socket[i]) {
				/*
				 * If we have an outstanding reference against a
				 * vxt_socket decrement it here.
				 */
				vxt_deref_socket(socket[i]);
			}
		}
		vxt_free_poll_obj(vxt_fds, nfds);
		vxt_free(socket);
		return -1;
	}
	for (i = 0; i < nfds; i++) {
		if (socket[i]) {
			/*
			 * If we have an outstanding reference against a
			 * vxt_socket decrement it here.
			 */
			vxt_deref_socket(socket[i]);
		}
	}
	vxt_free_poll_obj(vxt_fds, nfds);
	vxt_free(socket);
	return event_count;
}


int
vxt_socket_lib_init(void)
{
   if (!vxt_socket_lock_inited) {
   	if (vxt_lock_init(vxt_socket_lock)) {
		UMI_LOG(15, VXTPRINT_PRODUCT_LEVEL,
		        "Unable to initialize mutex\n");
	}
	vxt_socket_lock_inited = 1;
   }
   return  vxt_msg_init();
}
