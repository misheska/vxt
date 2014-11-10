/* $Id: mxt_vxt.c,v 1.20 2009/02/04 00:28:59 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/common/mxt_vxt.c,v $" */

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


#include <mxt_platform.h>
#include <mxt_common.h>
#include <public/vxt_system.h>
#include <public/vxt_msg_export.h>
#include <public/vxt_socket.h>
#include <public/symcom_dev_table.h>
#include <public/vxt_com.h>
#include <public/vxt_msg_dev.h>
#include <public/vxt_queue.h>
#include <public/vxt_auth.h>
#include <sys/types.h>

extern struct mux_peer_comm_mapping *mux_peer_comm_mappings[MAX_CONNECTIONS];
extern int mux_add_peer_comm_mapping(SOCKET, int);
extern void mux_remove_peer_comm_mapping(SOCKET);
extern char * mux_get_log_entry();

#ifdef VXT_ENDPOINT

void
mux_close_socket(fd, initiator)
	SOCKET	fd;
	int	initiator;
{
	char	buf[1024];
	int	ret, signal;
	size_t	len = sizeof(buf);

	MUX_LOG_ENTRY1("Sending close signal on fd %d", fd);
	if (initiator) {
		/*
		 * Receive all the remaining data
		 */
		for(;;) {
			ret = VXT_RECV((int)fd, buf, len, 0);
			if (ret <= 0) {
				break;
			}
		}

		/*
		 * Signal for shutdown
		 */
		signal = VXTS_SIGNAL_SHUTDOWN;
		vxt_ioctl((int)fd, VXT_IOCTL_SIGNAL_SEND, &signal, sizeof(int));
		SLEEP(1)

		/*
		 * Send a disconnect
		 */
		VXT_CLOSE((int)fd, VXT_CLOSE_LOCAL | VXT_CLOSE_REMOTE);
	} else {
		/*
		 * Receive all the remaining data
		 */
		for(;;) {
			ret = VXT_RECV((int)fd, buf, len, VXTQ_IGNORE_SHUTDOWN);
			if (ret <= 0) {
				break;
			}
		}

		/*
		 * Signal for shutdown
		 */
		signal = VXTS_SIGNAL_CLOSE;
		vxt_ioctl((int)fd, VXT_IOCTL_SIGNAL_RCV, &signal, sizeof(int));

		/*
		 * Send a disconnect
		 */
		VXT_CLOSE((int)fd, VXT_CLOSE_LOCAL);
	}
}


void
mux_close_vxt_sockets()
{
	int				i;
	struct mux_peer_comm_mapping	*mux_entry;
	SOCKET				fd;

	for (i = 0; i < MAX_NUMBER_OF_GUESTS; i++) {
		mux_entry = mux_peer_comm_mappings[i];
		while (mux_entry) {
			/*
			 * 1) Set a STOP signal for Recv Q
			 * 2) Flush Recv Q by receiving all the data
			 * 3) Send a shutdown on the send Q
			 * 4) Disconnect
			 */
			fd = mux_entry->mux_peer_comm_fd;
			mux_close_socket(fd, 1);
			mux_entry = mux_entry->mux_peer_comm_mapping_next;
		}
	}
}


/*
 *
 * mxt_vxt_server_dev_lookup:
 *
 *  mxt_vxt_server_dev_lookup is a helper routine
 *  It takes a controller query for a device
 *  and if successful, queries the device returned
 *  If that device is not connected VXT_SUCCESS is
 *  returned.  If it is already connected VXT_BUSY
 *  is returned.  If the device is in error, 
 *  VXT_FAIL is returned.  If the controller
 *  fails to find a device with the query provided
 *  VXTRSRC is returned.
 *
 *  Note: The fields of query_bus_buf are altered
 *  by the vxt_ioctl call, allowing for serial 
 *  traversal of the controller space, on query
 *  functions that support it.
 * 
 */

int
mxt_vxt_server_dev_lookup(int socket, vxt_query_bus_struct_t *query_bus_buf)
{
	vxt_query_dev_struct_t query_dev_buf;
	vxt_msg_dev_info_t     msgdev_info;
	int                    ret;


	/*
	 * query_bus_buf is set up by the caller
	 */

	ret = vxt_ioctl(socket, VXT_IOCTL_QUERY_BUS, 
	                query_bus_buf, sizeof(vxt_query_bus_struct_t));
	if (ret) {
		MUX_LOG_ENTRY0("Failed to find our device on the controller");
		return VXT_RSRC;
	}


	query_dev_buf.vxt_bus_id = query_bus_buf->vxt_bus_id;
	query_dev_buf.device_info = (void *)&msgdev_info;
	query_dev_buf.info_buf_size = sizeof(vxt_msg_dev_info_t);
	query_dev_buf.ctrlr_name[0] = 0;
	strncpy(query_dev_buf.ctrlr_name,
	        query_bus_buf->ctrlr_name, VXTCOM_MAX_DEV_FILE_NAME);
	
	ret = vxt_ioctl(socket, VXT_IOCTL_QUERY_DEV,
	                &query_dev_buf, sizeof(vxt_query_dev_struct_t));
	if (ret) {
		MUX_LOG_ENTRY1("Failed query of device on the controller,"
			" ctrlr %s", query_dev_buf.ctrlr_name);
		return VXT_RSRC;
	}
	MUX_LOG_ENTRY2("Device Info: dev state %lu sendq_size %lu",
		(long)query_dev_buf.device_state, (long)msgdev_info.sendq_size);

	MUX_LOG_ENTRY1("rcvq_size %lu", (long)msgdev_info.rcvq_size);

	if (msgdev_info.state == VXT_MSG_DEV_CONNECTED) {
		MUX_LOG_ENTRY1("IOCTL_VXT_QUERY_DEV,"
			" device in wrong state %lu",
		       (long)msgdev_info.state);
		/*
		 * Device is either already attached and in-use or
		 * has suffered a configuration or run-time failure
		 */
		return VXT_BUSY;
	}
	if (msgdev_info.state == VXT_MSG_DEV_ERROR) {
		MUX_LOG_ENTRY1("IOCTL_VXT_QUERY_DEV, device in wrong "
			"state %lu", (long)msgdev_info.state);
		/*
		 * Device is either already attached and in-use or
		 * has suffered a configuration or run-time failure
		 */
		return VXT_ERROR;
	}

	return VXT_SUCCESS;

}

int
vxt_scan_and_connect_devices(new_socket)
	int			*new_socket;
	
{
	int			ret;
	int			socket;
	vxt_query_bus_struct_t	query_bus_buf;
	vxt_dev_addr_t		connect_name;
	vxt_ioctl_ctrlr_parms_t	socket_ctrlr_parms;

	socket = *new_socket;
	/*
	 * Find the reaching controller
	 * No authorization facility yet, any string will give
	 * back the default controller use "default"
	 */

	/*
	 * Use the device name search method for finding
	 * the new device we wish to connect to.
	 * Do the initial lookup against the entire controller
	 * name space.  Return the first device entry of
	 * the specified name.
	 */
	query_bus_buf.function = VXTCTRLR_GLOBAL_LOOKUP;
	query_bus_buf.option = VXTCTRLR_GLOBAL_DEV_NAME;
	query_bus_buf.vxt_bus_id = 0;
	query_bus_buf.name_match_length = 0; /* disable, check full */
	strncpy(query_bus_buf.uname, "MXT_VXT_PIPE",  MAX_VXT_UNAME);

	for (;;) {
		ret = mxt_vxt_server_dev_lookup(socket, &query_bus_buf);

		if ((ret == VXT_BUSY) || (ret == VXT_ERROR)) {
			MUX_LOG_ENTRY0("Received VXT_BUSY");
			query_bus_buf.function = VXTCTRLR_GLOBAL_LOOKUP;
			query_bus_buf.option = VXTCTRLR_GLOBAL_CTRLR_SCAN;
			SLEEP(1);
			continue;
		}
	
		if (ret == VXT_RSRC) {
			/*
			 * No more new connections to make
			 */
			MUX_LOG_ENTRY0("Received VXT_RSRC");
			*new_socket = socket;
			return VXT_SUCCESS;
		}

		assert(ret == VXT_SUCCESS);
		/*
		 * If the first device matching the targeted name is
		 * not acceptible, keep checking through subsequent
		 * controllers.  There may be more than one device
		 * with the expected name.  i.e. make the unique
		 * search key the controller/name pair.
		 *
		 * Note: query_bus_buf.controller was filled in
		 * on the last query. Start the search from the
		 * controller after this one.  i.e. leave the
		 * controller field alone.
		 *
		 */

		MUX_LOG_ENTRY0("Set a preferred controller for the connect"
			" on our socket");

		strncpy(socket_ctrlr_parms.ctrlr,
	        	query_bus_buf.ctrlr_name, MAX_VXT_UNAME);
		vxt_ioctl(socket, VXT_IOCTL_TARGET_CTRLR, 
			&socket_ctrlr_parms, sizeof(socket_ctrlr_parms));

		MUX_LOG_ENTRY0("Attempting a vxt_connect");
		strncpy(connect_name.uname, "MXT_VXT_PIPE",  MAX_VXT_UNAME); 
		/*
		 * Note:  There are two ways to connect a particular device
		 * 1: General search of controllers and a connect on a match
		 * with a unique controller/device name pair.
		 * 2: Knowledge of the remote endpoint uuid and a connect
		 * on a match with a unique uuid/device name pair.
		 * We have filled in the TARGET_CTRLR field above
		 * therefore we are attempting case 1 and do not need
		 * to fill in the connect_name.ep_id, uuid field
		 */
		ret = vxt_connect(socket, &connect_name, 
	                  sizeof(vxt_dev_addr_t));
		if (ret != 0) {
			MUX_LOG_ENTRY0("vxt_connect failed give up");
			/*
			 * Tear down unsuccessful socket
			 */
			VXT_CLOSE(socket, VXT_CLOSE_LOCAL | VXT_CLOSE_REMOTE);
		} else {
			mux_add_peer_comm_mapping(socket, 1);
			MUX_LOG_ENTRY1("vxt_connect succeeded socket : %x",
				socket);
		}
		socket = VXT_SOCKET();
		if (socket <= 0) {
			MUX_LOG_ENTRY0("Failed to acquire socket");
			*new_socket = 0;
			return VXT_RSRC;
		}
		*new_socket = socket;
		/*
		 * Search the remaining list of controllers for additional
		 * device that need to be attached.
		 */
		query_bus_buf.function = VXTCTRLR_GLOBAL_LOOKUP;
		query_bus_buf.option = VXTCTRLR_GLOBAL_CTRLR_SCAN;
	}

}

void
vxt_cleanup_devices()
{
	int			socket;
	int			ret;
	vxt_query_bus_struct_t	query_bus_buf;

	MUX_LOG_ENTRY0("Acquiring an unattached vxt socket.");

	socket = vxt_socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM);
	MUX_LOG_ENTRY1("Returned from vxt_socket %lu", (long)socket);
	if (socket == 0) {
		MUX_LOG_ENTRY0("Failed to acquire socket");
		return;
	}

	MUX_LOG_ENTRY0("Scanning for stale devices ...");


 	query_bus_buf.function = VXTCTRLR_GLOBAL_LOOKUP;
	query_bus_buf.option = VXTCTRLR_GLOBAL_DEV_NAME;
	query_bus_buf.vxt_bus_id = 0;
	query_bus_buf.name_match_length = 0; /* disable, check full */
	strncpy(query_bus_buf.uname, "MXT_VXT_PIPE",  MAX_VXT_UNAME);

	ret = mxt_vxt_server_dev_lookup(socket, &query_bus_buf);

	
	while ((ret != VXT_FAIL) && (ret != VXT_RSRC)) { 

		MUX_LOG_ENTRY2("Found a device on controller major 0x%x, "
			" name %s",
		       query_bus_buf.controller,
		       query_bus_buf.ctrlr_name);

		if ((ret == VXT_BUSY) || (ret == VXT_ERROR) ||
		    (ret == VXT_SUCCESS)) {
			vxt_unplug_dev_struct_t unplug_struct;
			vxt_msg_destroy_info_t dev_info;
			/*
			 * Device was running, shut it down
			 */
			MUX_LOG_ENTRY0("Device is stale, attempting remove");

			unplug_struct.device_info = (void *)&dev_info;
			unplug_struct.info_buf_size = sizeof(dev_info);
			unplug_struct.vxt_bus_id = query_bus_buf.vxt_bus_id;
			unplug_struct.controller = query_bus_buf.controller;
			strncpy(unplug_struct.ctrlr_name,
	        	        query_bus_buf.ctrlr_name,
			        VXTCOM_MAX_DEV_FILE_NAME);

			ret = vxt_ioctl(socket, VXT_IOCTL_UNPLUG_DEV,
			                &unplug_struct, 
			                sizeof(vxt_unplug_dev_struct_t));
			if (ret) {
				MUX_LOG_ENTRY0("Failed device shutdown");
			}
		}

		query_bus_buf.function = VXTCTRLR_GLOBAL_LOOKUP;
		query_bus_buf.option = VXTCTRLR_GLOBAL_CTRLR_SCAN;

		ret = mxt_vxt_server_dev_lookup(socket, &query_bus_buf);
	}


	MUX_LOG_ENTRY0("Scan completed, returning");
}

int
mux_vxt_server()
{
	int			socket;
	int			ret;
	vxt_msg_plug_poll_t	poll_info;

	socket = VXT_SOCKET();
	if (socket <= 0) {
		MUX_LOG_ENTRY0("Failed to acquire socket");
		return -1;
	}

	/*
	 * Wait on creation of a new controller
	 */


retry:
	poll_info.timeout = -1;
	poll_info.revent = 0;

	while (1) {
		MUX_LOG_ENTRY1("Attempt a wait for a controller poll event,"
			" socket = 0x%x", socket);
		ret = vxt_ioctl(socket, VXT_IOCTL_CTRLR_POLL,
	                	&poll_info, sizeof(poll_info));
		MUX_LOG_ENTRY2("Return from DEVICE PLUG POLL: ret %08x, "
			" revent %08x", ret, poll_info.revent);
		if (ret != 0) {
			MUX_LOG_ENTRY0("Attempt a wait for a controller "
				" plug event");

			poll_info.timeout = -1;
			poll_info.revent = 0;

			ret = vxt_ioctl(socket, VXT_IOCTL_CTRLR_PLUG,
				&poll_info, sizeof(poll_info));
			MUX_LOG_ENTRY2("Return from PLUG POLL: ret %08x, "
				"revent %08x", ret, poll_info.revent);
			if (ret != 0 ) {
				break;
			}
		} else {
			break;
		}
	}

	/*
	 * If the return is not POLL_VXT_NEW_CTRLR, we should just ignore
	 * for the time being
	 */
	if (! (poll_info.revent & VXT_POLL_NEW_DEV)) {
		MUX_LOG_ENTRY0("Waking up without a new device, "
				" go back to sleep");
		SLEEP(1);
		goto retry;
	}

	/*
	 * Find the reaching controller
	 * No authorization facility yet, any string will give
	 * back the default controller use "default"
	 */

	/*
	 * Use the device name search method for finding
	 * the new device we wish to connect to.
	 * Do the initial lookup against the entire controller
	 * name space.  Return the first device entry of
	 * the specified name.
	 */
	ret = vxt_scan_and_connect_devices(&socket);

	if (ret == VXT_RSRC) {
		return -1;
	}

	goto retry;
}

int
mux_vxt_client()
{
	int			ret;
	vxt_ioctl_buf_parms_t	queue_parms; 
	vxt_msg_plug_poll_t	poll_info;
	vxt_dev_addr_t		connect_name;
	vxt_query_sys_struct_t	query_sys_buf;
	int 			socket;


	MUX_LOG_ENTRY0("Hello");

	/*
	 * Create a new device pair:
	 * uname - Is the name of the device we wish to connect to
	 * As we are a making this call from the guest, all
	 * routing and authentication take place through the HUB
	 * i.e. Dom0.  This is true whether or not the ultimate
	 * endpoint is another guest.  We therefore set our
	 * ep_id to that of the default HUB.  A HUB resident service
	 * would have to pick either the connecting domain or the
	 * endpoint  UUID in order to identify the ultimate connection
	 * point.
	 */
	strncpy(connect_name.uname, "MXT_VXT_PIPE",  MAX_VXT_UNAME);
	strncpy(connect_name.ep_id,  VXTDB_DOM0_ENDPOINT, MAX_VXT_UUID);
	connect_name.ep_id[MAX_VXT_UUID-1] = 0;
	socket = VXT_SOCKET();

	/*
	 * Check to see if our controller is already up and running
	 * (mxt restart on a running system)
	 */

	query_sys_buf.uname[0] = 0;
	query_sys_buf.name_match_length = 0; /* disable, check full */
	query_sys_buf.domain = 0;

	ret = vxt_ioctl(socket, VXT_IOCTL_QUERY_SYS,
			&query_sys_buf, sizeof(vxt_query_sys_struct_t));

	if (ret != VXT_SUCCESS) {
		MUX_LOG_ENTRY0("Attempt a wait for a controller plug event");

		poll_info.timeout = -1;
		poll_info.revent = 0;

		ret = vxt_ioctl(socket, VXT_IOCTL_CTRLR_PLUG,
				&poll_info, sizeof(poll_info));

		MUX_LOG_ENTRY2("Return from PLUG POLL: ret %08x, revent %08x",
			ret, poll_info.revent);
	}

	/*
	 * Customize the shared queue sizes and sub buffer counts
	 * 31 bytes accounts for buffer and sub-buffer headers and
	 * still provides MAX_LEN for each sub-buffer, avoiding
	 * split buffer transfers of requests.  This is important
	 * because it avoids partial receives.
	 */
	queue_parms.send_buf_size = 9 * (MAX_LEN + 31);
	queue_parms.recv_buf_size = 9 * (MAX_LEN + 31);
	queue_parms.send_sub_buf_cnt = 9;
	queue_parms.recv_sub_buf_cnt = 9;

	vxt_ioctl(socket, VXT_IOCTL_SET_BUF_PARMS, 
	          &queue_parms, sizeof(queue_parms));

	ret = vxt_bind(socket, &connect_name, sizeof(vxt_dev_addr_t));
	if (ret != 0) {
		MUX_LOG_ENTRY0("vxt_bind failed");
		return -1;
	}
	MUX_LOG_ENTRY1("VXT_BIND SUCCEEDED SOCKET : %x", socket);

	return socket;
}

#endif /* VXT_ENDPOINT */
