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

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <pty.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>

#include <vxtcom/vxt_system.h>
#include <vxtcom/vxt_msg_export.h>
#include <vxtcom/vxt_socket.h>
#include <vxtcom/symcom_dev_table.h>
#include <vxtcom/vxt_com.h>
#include <vxtcom/vxt_msg_dev.h>
#include <vrsh.h>

int vsock = -1;
int target_domain;
vxt_dev_addr_t rsh_name;

/*
 * forked_shell_socket is used by the vrsh_run_shell routine.
 * The use of forked_shell_socket is subtle.  The field is global
 * but it is acted on by multiple instances of forked tasks.  
 * Therefore, it is instance level at the forked task.  This is
 * important because it must be global to be seen by the child
 * signal handler, where it is closed.
 */

int forked_shell_socket;




#define RSH_BUF_SIZE 4096


/*
 *
 * vrsh_rcv_thread:
 *
 * vrsh_rcv_thread runs at the transport level of the forked shell
 * instance.  The parent of the running guest shell creates this
 * pthread.  The parent handles the asynchronous characters
 * being sent by the remote through the  vxt communcation device.
 * vrsh_rcv_thread handles the synchronous characters provided
 * by the shell, sending these to the remote shell client.
 *
 * vrsh_rcv_thread blocks, waiting for output from the shell.
 *
 * 
 * Returns:
 *
 *		VXT_SUCCESS on exit - not designed to exit
 */

int
vrsh_rcv_thread(void *parms)
{
	uint32_t *rparms = parms;
	char     buf[1024];
	char     connected_string[] = 
				"vxt_rsh: Connected, starting shell ...\r\n\n";
	int readlen, sendlen;
	int      ret;
	VXTPRINT(3, "vrsh_rcv_thread: called\n");

	/*
	 * Print vrsh connected, starting shell message to remote
	 */
	sendlen = strlen(connected_string);
	strncpy(buf, connected_string, sendlen);
	ret = vxt_send(rparms[0], buf, &sendlen,
	               VXT_SOCK_SIGNAL | VXT_SOCK_WAIT);


	while ((readlen = read(rparms[1], &buf, 1024)) > 0) {
		sendlen = readlen;
		while (sendlen != 0) {
			ret = vxt_send(rparms[0], buf, &sendlen,
			               VXT_SOCK_SIGNAL | VXT_SOCK_WAIT);
			readlen -= sendlen;
			sendlen = readlen;
			
		}
 	}
	return VXT_SUCCESS;
}


/*
 * vrsh_get_pty:
 *
 * The vrsh_server vends execution shells.  In order to do 
 * this correctly, it is necessary to use pseudo-terminal wrappers
 * to handle screen and character processing.  Here we acquire
 * a new pseudo terminal.
 *
 *
 * Returns:
 *		VXT_SUCCESS => upon successful aquisition of a pty
 *
 *		VXT_FAIL: Pseudo terminal request failed.
 */

int
vrsh_get_pty(int *ptyfd, int *ttyfd, char *fdname, size_t fdnamelen)
{
        char *name;
        int i;

        i = openpty(ptyfd, ttyfd, NULL, NULL, NULL);
        if (i < 0) {
                VXTPRINT(0,"openpty: %.100s", strerror(errno));
                return VXT_FAIL;
        }
        name = ttyname(*ttyfd);
        if (!name) {
                VXTPRINT(0, "vrsh_get_pty: no name provided with "
		         "openpty call, %.100s\n", strerror(errno));
		return VXT_FAIL;
	}

        strncpy(fdname, name, fdnamelen);     /* possible truncation */
	fdname[fdnamelen-1] = 0;
        return VXT_SUCCESS;
}


void
vrsh_set_control_tty(int *ttyfd)
{
        int fd;


        /* First disconnect from the old controlling tty. */
        fd = open("/dev/tty", O_RDWR | O_NOCTTY);
        if (fd >= 0) {
                (void) ioctl(fd, TIOCNOTTY, NULL);
                close(fd);
        }
        /*
         * Verify that we are successfully disconnected from the controlling
         * tty.
         */
        fd = open("/dev/tty", O_RDWR | O_NOCTTY);
        if (fd >= 0) {
                error("Failed to disconnect from controlling tty.");
                close(fd);
        }
        if (setsid() < 0) {
                error("setsid: %.100s", strerror(errno));
	}
        /* Make it our controlling tty. */
        VXTPRINT(3,"Making TIOCSCTTY call on new shell.\n");
        if (ioctl(*ttyfd, TIOCSCTTY, NULL) < 0) {
                VXTPRINT(0,"ioctl(TIOCSCTTY): %.100s", strerror(errno));
	}
        /* Verify that we now have a controlling tty. */
        fd = open("/dev/tty", O_WRONLY);
        if (fd < 0) {
                VXTPRINT(0, "open /dev/tty failed - could not set controlling "
		         "tty: %.100s", strerror(errno));
        } else {
                close(fd);
	}
}


/*
 *
 * rsh_shell_close:
 *
 * Signal handler for forked shell parent, closes the 
 * associated vxt communciation channel
 *
 */

void rsh_shell_close(int sig, siginfo_t *info, void *data)
{
	ucontext_t   *uc = (ucontext_t *)data;

	/*
	 * Note We will not exit if someone stops us
         * 
         * Exit on:
	 *      CLD_EXITED  child has exited  
         *      CLD_KILLED  child was killed  
         *      CLD_DUMPED  child terminated abnormally  
         *      CLD_TRAPPED  traced child has trapped  
	 *
	 * Ignore:
	 *      CLD_STOPPED  child has stopped  
	 *      CLD_CONTINUED  stopped child has continued 
	 *
	 */
	if ((info->si_signo == CLD_STOPPED) ||
	    (info->si_signo == CLD_CONTINUED)) {
		return;
	}

	VXTPRINT(3, "Close signal handler called, normal close\n");

	vxt_close(forked_shell_socket,
	           VXT_CLOSE_LOCAL | VXT_CLOSE_REMOTE | VXT_CLOSE_FORCE);
	exit(0);

	return;

}


/*
 * vrsh_run_shell:
 *
 */

int
vrsh_run_shell(int sock, char *device_name)
{
	vxt_dev_addr_t		connect_name;
	int                     ret;

	strncpy(connect_name.uname, device_name,  MAX_VXT_UNAME); 
	VXTPRINT(3, "called, connect %s\n", connect_name.uname);
	/*
	 * communciate the socket to the child signal handler
	 */
	forked_shell_socket = sock;
	ret = vxt_connect(sock, &connect_name, sizeof(vxt_dev_addr_t));
	if (ret != 0) {
		VXTPRINT(3, "vrsh_run_shell: vxt_connect failed give up\n");
		/*
		 * Tear down unsuccessful socket
		 */
		vxt_close(sock, VXT_CLOSE_LOCAL | VXT_CLOSE_REMOTE);
		return VXT_FAIL;
	} else {
		VXTPRINT(3,"vrsh_run_shell: vxt_connect succeeded "
		         "socket : %x\n", sock);
	}
	{
		int fdout;
		int ptyfd;
		int ttyfd; 
		int ptymaster;
		pid_t pid;
		int ret;
		char fdname[40];
		int fdnamelen = 40;


		ret = vrsh_get_pty(&ptyfd, &ttyfd, fdname, fdnamelen);
		if (ret != VXT_SUCCESS) {
			exit(-1);
		}


		pid=fork();
		if (pid==-1) {
			VXTPRINT(0,"vrsh_run_shell, fork failed: %s",
			         strerror(errno));
			vxt_close(sock, 0);
			exit(0);
		}

		if (pid==0) {
			close(ptyfd);
			vrsh_set_control_tty(&ttyfd);
		
			if (dup2(ttyfd, 0) < 0) {
				error("dup2 stdin: %s", strerror(errno));
			}
			if (dup2(ttyfd, 1) < 0) {
				error("dup2 stdout: %s", strerror(errno));
			}
			if (dup2(ttyfd, 2) < 0) {
				error("dup2 stderr: %s", strerror(errno));
			}
			close(ttyfd);
		

			if (execl("/bin/sh", " - ", NULL) < 0) {
				VXTPRINT(0,"Shell fork failed, now closing the "
				         "vxt socket\n");
				vxt_close(sock, 
				          VXT_CLOSE_REMOTE | VXT_CLOSE_LOCAL);
				exit(-1);
			}
		} else {
			struct sigaction act;
			close(ttyfd);
			VXTPRINT(3,"Setup rsh_shell_close signal handler\n");
			act.sa_sigaction = rsh_shell_close;
			act.sa_flags = SA_RESTART | SA_NOCLDWAIT | SA_SIGINFO;
			sigaction (SIGCHLD, &act, NULL);
		
		}


		{
			uint32_t rparms[2];
			rparms[0]=sock;
			rparms[1]=ptyfd;
			pthread_t rcv_thread;

			ret = pthread_create(&rcv_thread, NULL,
			                     (void *) &vrsh_rcv_thread,
			                     (void *)&rparms);
		}

		while(1) {
			char           buf[1024];
			ssize_t        len, rcv_len;
			vxt_poll_obj_t fds;

			fds.events = POLLVXT | VXTSIG_IN;
			fds.revents = 0;
			fds.object = sock;

/*
			sleep(1);
*/
	
			while (1) {
				len = 1024;
				while (1) {
					rcv_len = 1024;
					ret = vxt_recv(sock, buf, 
					               &rcv_len,
					               VXT_SOCK_SIGNAL);
					if (!(( rcv_len == 0) || (ret == -1))) {
						write(ptyfd, buf, rcv_len);
					}
/*
buf[rcv_len] = 0;
VXTPRINT(3,"rsh_recv_thread: vxt_recv: receive"
         " data len = %lu\n", (long)rcv_len);
VXTPRINT(3,"string from remote:%s", buf);
*/
					vxt_poll(&fds, 1, -1);
					if (fds.revents & VXTSIG_HUP) {
						VXTPRINT(3, 
					                 "remote disconnect: "
						         "shutting down\n");
						break;
					}
					fds.revents = 0;
				}
			}
		}

	}


	return VXT_SUCCESS;
	
}


/*
 *
 * rsh_server_dev_lookup:
 *
 *  rsh_server_dev_lookup is a helper routine
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
rsh_server_dev_lookup(int sock, vxt_query_bus_struct_t *query_bus_buf)
{
	vxt_query_dev_struct_t query_dev_buf;
	vxt_msg_dev_info_t     msgdev_info;
	int                    ret;


	/*
	 * query_bus_buf is set up by the caller
	 */

	ret = vxt_ioctl(sock, VXT_IOCTL_QUERY_BUS, 
	                query_bus_buf, sizeof(vxt_query_bus_struct_t));
	if (ret) {
		VXTPRINT(3, "Failed to find our device on the "
		         "controller\n");
		return VXT_RSRC;
	}

	VXTPRINT(3, "returned name %s\n", query_bus_buf->uname);

	query_dev_buf.vxt_bus_id = query_bus_buf->vxt_bus_id;
	query_dev_buf.device_info = (void *)&msgdev_info;
	query_dev_buf.info_buf_size = sizeof(vxt_msg_dev_info_t);
	query_dev_buf.ctrlr_name[0] = 0;
	strncpy(query_dev_buf.ctrlr_name,
	        query_bus_buf->ctrlr_name, VXTCOM_MAX_DEV_FILE_NAME);
	
	ret = vxt_ioctl(sock, VXT_IOCTL_QUERY_DEV,
	                &query_dev_buf, sizeof(vxt_query_dev_struct_t));
	if (ret) {
		VXTPRINT(0,
		         "Failed query of device on the controller,"
			 " ctrlr %s, %100s\n", query_dev_buf.ctrlr_name,
		         strerror(errno));
		return VXT_RSRC;
	}
	VXTPRINT(3,
	         "Device Info: dev state %lu sendq_size %lu\n",
		 (long)query_dev_buf.device_state,
	         (long)msgdev_info.sendq_size);

	VXTPRINT(3,"rsh_server_dev_lookup: rcvq_size %lu, bus slot 0x%x\n",
	         (long)msgdev_info.rcvq_size, query_bus_buf->vxt_bus_id);

	if (msgdev_info.state == VXT_MSG_DEV_CONNECTED) {
		VXTPRINT(3, "IOCTL_VXT_QUERY_DEV,"
			 " device in wrong state %lu\n",
		         (long)msgdev_info.state);
		/*
		 * Device is either already attached and in-use or
		 * has suffered a configuration or run-time failure
		 */
		return VXT_BUSY;
	}
	if (msgdev_info.state == VXT_MSG_DEV_ERROR) {
		VXTPRINT(0, "rsh_server_dev_lookup: "
		         "IOCTL_VXT_QUERY_DEV, device in wrong "
		  	 "state %lu\n", (long)msgdev_info.state);
		/*
		 * Device is either already attached and in-use or
		 * has suffered a configuration or run-time failure
		 */
		return VXT_ERROR;
	}

	return VXT_SUCCESS;

}


/*
 *
 * We will use the global scan mechanism here.
 * We could divide up the scan based a poll on the
 * arrival of new controllers and then separate polls
 * on the individual controllers for new devices.
 * This would limit our search for new devices on
 * an specific new device event to a search on the
 * specific controller.  However, the number of 
 * controllers and devices is expected to be small
 * enough not to warrant this.  We will fork a 
 * separate thread for each new device we connect
 * to, providing a separate execution context for
 * each remote shell session.
 */

int
vrsh_scan_and_connect_devices(int *new_socket)
{
	int			ret;
	int			rsh_sock;
	vxt_query_bus_struct_t	query_bus_buf;
	vxt_ioctl_ctrlr_parms_t	socket_ctrlr_parms;

	rsh_sock = *new_socket;
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

	for (;;) {
		/* 
		 * When name is a prefix, it must be refreshed
		 * Each successful lookup will give the whole name
		 * of the associated device.
		 */
		query_bus_buf.name_match_length = 8; 
		strncpy(query_bus_buf.uname, "VXT_RSH_",  MAX_VXT_UNAME);
		ret = rsh_server_dev_lookup(rsh_sock, &query_bus_buf);

		if ((ret == VXT_BUSY) || (ret == VXT_ERROR)) {
			VXTPRINT(3, "vrsh_scan_and_connect_devices: "
			         "Received VXT_BUSY \n");
			query_bus_buf.function = VXTCTRLR_GLOBAL_LOOKUP;
			query_bus_buf.option = VXTCTRLR_GLOBAL_CTRLR_SCAN;
			sleep(1);
			continue;
		}
	
		if (ret == VXT_RSRC) {
			/*
			 * No more new connections to make
			 */
			VXTPRINT(3, "vrsh_scan_and_connect_devices: "
			         "Received VXT_RSRC \n");
			*new_socket = rsh_sock;
			return VXT_SUCCESS;
		}

/*
		assert(ret == VXT_SUCCESS);
*/
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

		VXTPRINT(3, "vrsh_scan_and_connect_devices: Set a preferred "
		         "controller for the connect on our socket\n");

		strncpy(socket_ctrlr_parms.ctrlr,
	        	query_bus_buf.ctrlr_name, VXTCOM_MAX_DEV_FILE_NAME);
		vxt_ioctl(rsh_sock, VXT_IOCTL_TARGET_CTRLR, 
			  &socket_ctrlr_parms, sizeof(socket_ctrlr_parms));

		VXTPRINT(3, "vrsh_scan_and_connect_devices: "
		         "Attempting a vxt_connect, fork task\n");
		/*
		 * Remember, the device memory attach must be done after
		 * forking.  It is not sharable between multiple tasks
		 */
		{
			pid_t pid;

			pid=fork();
			if (pid==0) {
				vrsh_run_shell(rsh_sock, query_bus_buf.uname);
				exit(0);
			}
			signal(SIGCHLD, SIG_IGN);
		}

		rsh_sock = vxt_socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM);
		if (rsh_sock <= 0) {
			VXTPRINT(0, "vrsh_scan_and_connect_devices: "
			         "Failed to acquire socket\n");
			*new_socket = 0;
			return VXT_RSRC;
		}
		*new_socket = rsh_sock;
		/*
		 * Search the remaining list of controllers for additional
		 * device that need to be attached.
		 */
		query_bus_buf.function = VXTCTRLR_GLOBAL_LOOKUP;
		query_bus_buf.option = VXTCTRLR_GLOBAL_CTRLR_SCAN;
	}

}

/*
 *
 * vrsh_wait_for_device:
 *
 * This code may come up when no controller is  present.  If so
 * wait for controller plug-in event.  The program caller is notified
 * so that the program may be aborted if wished.
 *
 * vrsh_wait_for_device polls for new controller and device events
 * whenever such an event occurs, the existing set of controllers is
 * scanned for unconnected devices of the right type and name.  When
 * one is found a thread is forked and a connect is done.  The new
 * thread is then dedicated to the communication link with the remote
 * endpoint attached to the associated device.
 *
 */

int
vrsh_wait_for_device()
{
	vxt_msg_plug_poll_t     poll_info;
	int                     sock;
	int                     ret;


	/*
	 * Ask the vxt_socket provider interface 
	 * for an unconnected socket 
	 */
	sock = vxt_socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM);
retry:
	poll_info.timeout = 0;
	poll_info.revent = 0;

	while (1) {
		/*
		 * First lets check to see if we have a live controller, if
		 * not, we will need to poll for a controller plug event.
		 * If there is a controller we will jump directly to a 
		 * scan through all the live controllers for suitable 
		 * devices.  We will fork a thread to connect to any
		 * unconnected rsh requests we find.
		 */
		VXTPRINT(3,"vrsh_wait_for_device: Attempt a wait for a "
		         "controller poll event, socket = 0x%x\n", sock);
		ret = vxt_ioctl(sock, VXT_IOCTL_CTRLR_POLL,
	                	&poll_info, sizeof(poll_info));
		VXTPRINT(3, "Return from DEVICE PLUG POLL:"
		         " ret %08x, revent %08x\n", ret, poll_info.revent);
		if (ret != 0) {
			VXTPRINT(3, "No controller found, wait on a new "
			         "controller, and or new device\n");
			poll_info.timeout = -1;
			poll_info.revent = 0;

			ret = vxt_ioctl(sock, VXT_IOCTL_CTRLR_PLUG,
			                &poll_info, sizeof(poll_info));
			VXTPRINT(3,"vrsh_wait_for_device: Return from starting"
			       " PLUG POLL: ret %08x, revent %08x\n", 
			       ret, poll_info.revent);
		}
		/* 
		 * When we get here we know we have at least one good
		 * controller.  This routine is called at start-up
		 * When we succeed we will scan for devices and then, 
		 * begin a new poll for new ctrlrs/devs.  We will
		 * check and if we find that we have lost a controller
		 * we will again run the POLL and call this code if
		 * if fails.  In all other instances we go back to sleep
		 * on the PLUG after scanning the list of controllers.
		 */
		vrsh_scan_and_connect_devices(&sock);
		poll_info.timeout = -1;
		poll_info.revent = 0;

		ret = vxt_ioctl(sock, VXT_IOCTL_CTRLR_PLUG,
		                &poll_info, sizeof(poll_info));
		VXTPRINT(3,"vrsh_wait_for_device: Return from "
		         " running PLUG POLL: ret %08x, revent %08x\n",
		         ret, poll_info.revent);
	}

	vxt_close(sock, 0);
	return VXT_SUCCESS;

}

/*
 * 
 * vrsh_server:
 *
 * This is the server, or connect side of an example program
 * designed to demonstrate the socket style interface of the vxt
 * message device.  The rsh guest and server components talk to 
 * each other in an IPC style arrangement, providing a shell 
 * telepresence on the guest side, featuring a server side
 * ksh session.
 *
 * vxtrsh_server_init:
 *
 * vxtrsh_server_init opens the vxt subsystem and requests a
 * non-bound socket.  These actions will succeed regardless of
 * the hardware status of the subsystem.  i.e. whether or not there
 * are controllers or devices populating the slots of those controllers.
 *
 * Returns: None
 *
 */

void
vxtrsh_server_init()
{
	int ret;

	/*
	 * Initialize the vxt socket library, linking it
	 * to the vxt controller.  Establish an internal
	 * connection with the vxtcom subsystem through
	 * a connection to the vxtcom controller bus
	 * device.
	 */
	ret = vxt_socket_lib_init();

	if (ret != VXT_SUCCESS) {
	        VXTPRINT(0, "Failed to connect to the VXT controller\n");
	}

	/*
	 * Acquire a vxtcom unbound socket.  This
	 * socket can be used for system queries, including
	 * scans for particular devices.
	 */
	vsock = vxt_socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM);
	VXTPRINT(3, "Returned from vxt_socket %lu\n", (long)socket);

	if (socket == 0) {
		VXTPRINT(0, "Failed to acquire VxT socket\n");
		exit(0);
	}

}


usage()
{
	printf("\n\nusage ... \n\n");
	printf("\tvxt_rsh_server [-d]\n");
	printf("\t\t-d run the binary as a daemon\n\n");
	return;
}


/*
 * vrsh_sever is an example program with
 * many simplifications when compared 
 * with a full telnet or ssh implementation.
 * There is at present no guest console level
 * control information flow and therefore,
 * no communication of window size or terminal
 * type.  It was kept simple deliberately to
 * elucidate more clearly the vxt elements.
 * The program may be expanded but is
 * not in its present state a product level
 * 
 * 
 */

main(int argc, char **argv)
{
	int run_as_daemon = 0;

	if (argc > 3) {
		usage();
		return -1;
        } else if (argc == 3) {
		if ((argv[1][0] != '-') || (argv[1][1] != 0) ||
		    (argv[2][0] != 'd') || (argv[2][1] != 0)) {
			usage();
			return -1;
		}
		run_as_daemon = 1;
		
	} else if (argc == 2) {
		if((argv[1][0] != '-') || (argv[1][1] != 'd')) {
			usage();
			return -1;
		}
		run_as_daemon = 1;
		
	}

	if (run_as_daemon) {
		if (daemon(0, 0) < 0) {
			perror("daemon");
		}
	}

	vxtrsh_server_init();
	vrsh_wait_for_device();

	/*
	 * Never Reached
	 */
	while (1) {
	}


}
