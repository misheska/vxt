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
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef XSTOOLS
#include <xs.h>
#endif
#ifdef INCLUDE_PTHREAD
#include <pthread.h>
#endif


#include <vxtcom/vxt_system.h>
#include <vxtcom/vxt_msg_export.h>
#include <vxtcom/vxt_socket.h>
#include <vxtcom/symcom_dev_table.h>
#include <vxtcom/vxt_com.h>
#include <vxtcom/vxt_msg_dev.h>
#include <vxtcom/vxt_auth.h>
#include <vrsh.h>

#define MAX_FILE_STRING 256

int vsock = -1;
int target_domain;
char target_uuid[MAX_VXT_UUID]; 
vxt_dev_addr_t rsh_name;

#define vxt_mb()  __asm__ __volatile__ ( "mfence" : : : "memory")

#define RSH_BUF_SIZE 4096


pthread_mutex_t rcv_sync_lock;
pthread_cond_t rcv_sync_var;


/*
 * 
 * vrsh_guest:
 *
 * This is an example program, developed to demonstrate the socket
 * style interface of the vxt message device.  The rsh guest and
 * server components talk to each other in an IPC style arrangement,
 * providing a shell telepresence on the virtual machine featuring
 * the host.
 *
 */

void rsh_handle_signal(int signum)
{
	vxt_query_bind_struct_t bind_info;
	int                     i;
	int                     ret;

	fprintf(stderr, "Received signal %d \n", signum);

	sleep(1);

	ret = VXT_FAIL;

	if (vsock != -1) {
		ret = vxt_ioctl(vsock, VXT_IOCTL_QUERY_BIND,
		                &bind_info, sizeof(bind_info));
	}

	if (ret == VXT_SUCCESS) {
		fprintf(stderr,
		        "dumping connection info slot 0x%qx, ctrlr 0x%qx\n",
		        bind_info.dev_slot, bind_info.ctrlr_id);

		fprintf(stderr, "Calling vxt socket close\n");
		vxt_close(vsock, VXT_CLOSE_REMOTE | VXT_CLOSE_LOCAL);
		fprintf(stderr, "finished vxt socket close\n");

		fprintf(stderr, "Signal Receive thread to close\n");
		pthread_mutex_lock(&rcv_sync_lock);
		pthread_cond_signal(&rcv_sync_var);
		pthread_mutex_unlock(&rcv_sync_lock);

	}

	raise(SIGKILL);

}

void
vrsh_set_sig_handler()
{
	/*
	 * Set the signal handler for all the threads
	 */
	if (signal (SIGHUP, rsh_handle_signal) == SIG_IGN)
		signal (SIGHUP, SIG_IGN);
	if (signal (SIGTERM, rsh_handle_signal) == SIG_IGN)
		signal (SIGTERM, SIG_IGN);
	if (signal (SIGKILL, rsh_handle_signal) == SIG_IGN)
		signal (SIGKILL, SIG_IGN);
	if (signal (SIGTRAP, rsh_handle_signal) == SIG_IGN)
		signal (SIGTRAP, SIG_IGN);
	if (signal (SIGABRT, rsh_handle_signal) == SIG_IGN)
		signal (SIGABRT, SIG_IGN);
	if (signal (SIGQUIT, rsh_handle_signal) == SIG_IGN)
		signal (SIGQUIT, SIG_IGN);
	if (signal (SIGBUS, rsh_handle_signal) == SIG_IGN)
		signal (SIGBUS, SIG_IGN);
	if (signal (SIGINT, rsh_handle_signal) == SIG_IGN)
		signal (SIGINT, SIG_IGN);
}


/*
 *
 * vrsh_wait_for_controller:
 *
 * This code may come up when no controller is  present.  If so
 * wait for controller plug-in event.  The program caller is notified
 * so that the program may be aborted if wished.
 *
 */

int
vrsh_wait_for_controller()
{
	vxt_msg_plug_poll_t     poll_info;
	vxt_query_sys_struct_t  query_sys_buf;
	int                     sock;
	int                     ret;


	/*
	 * Ask the vxt_socket provider interface 
	 * for an unconnected socket 
	 */
	sock = vxt_socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM);


retry:

	while (1) {
		sprintf(query_sys_buf.uname, "VXT_RSH_%d", getpid());
		query_sys_buf.uname[MAX_VXT_UNAME-1] = 0;
		strncpy(query_sys_buf.ep_id, target_uuid, MAX_VXT_UUID);
		query_sys_buf.name_match_length = 8; 
		query_sys_buf.domain = 0;

		VXTPRINT(3, "Attempt a wait for "
		         " a live controller, socket = 0x%x\n", sock);

		ret = vxt_ioctl(sock, VXT_IOCTL_QUERY_SYS,
		                &query_sys_buf, sizeof(vxt_query_sys_struct_t));


		VXTPRINT(3, "Return from reaching "
		         "controller query: ret %08x, domain %08x\n",
		         ret, query_sys_buf.domain);
		if (ret != 0) {
			VXTPRINT(3, "Attempt a "
			         "wait for a controller plug event\n");

			poll_info.timeout = -1;
			poll_info.revent = 0;

			ret = vxt_ioctl(sock, VXT_IOCTL_CTRLR_PLUG,
				&poll_info, sizeof(poll_info));
			VXTPRINT(3, "Return from "
			         "PLUG POLL: ret %08x, revent %08x\n",
			          ret, poll_info.revent);
			if (ret != 0 ) {
				break;
			}
		} else {
			break;
		}
	}
	vxt_close(sock, 0);

}




int
vrsh_create_device(int *sock)
{
	vxt_query_sys_struct_t  query_sys_buf;
	vxt_ioctl_buf_parms_t   queue_parms;
	int                     ret;

	/*
	 * Initialize the vxt socket library, linking it
	 * to the vxt controller.  This call will succeed 
	 * if the VxT subsystem is installed, even if there
	 * are no vxt controllers plugged in.
	 */
	ret = vxt_socket_lib_init();

	if (ret != VXT_SUCCESS) {
		VXTPRINT(0,"VxT Subsystem is not installed\n");
		return VXT_FAIL;
	}

	sprintf(rsh_name.uname, "VXT_RSH_%d", getpid());
	strncpy(rsh_name.ep_id, target_uuid, MAX_VXT_UUID);
	rsh_name.ep_id[MAX_VXT_UUID-1] = 0;
	VXTPRINT(3, "Attempting to open a device with name %s\n",
	         rsh_name.uname);

	/*
	 * Ask the vxt_socket provider interface 
	 * for an unconnected socket 
	 */
	*sock = vxt_socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM);

	vrsh_wait_for_controller();
	
	/*
         * Check to see if a controller, reaching the intended target is
	 * already already up and running.  If not, check that we have
	 * a working connection to our authorization service on
	 * Dom0
         */

	/*
	 * NOTE: at present we do not have guest to guest support so
	 * we only check for direct guest connection, attempting to
	 * get a guest to guest provisioning is not yet implemented.
	 * In future, if we fail on a direct connection, we will check
	 * for a domain 0 connection and if one exists, try to connect
	 * throuh domain0.
	 */
        query_sys_buf.uname[0] = 0;
        query_sys_buf.ep_id[0] = 0;
        query_sys_buf.domain = target_domain;

        ret = vxt_ioctl(*sock, VXT_IOCTL_QUERY_SYS,
                        &query_sys_buf, sizeof(vxt_query_sys_struct_t));

	if (ret != VXT_SUCCESS) {
		VXTPRINT(0, "Cannot connect to domain %d\n", target_domain);
		return VXT_PARM;
	}

	/*
	 * Set up the request to device create
	 * We chose 10 4k buffers for the send and receive queues.
	 * There is a header for the overall buffer and a sub-buffer
	 * header.  The header is 48 bytes and the sub-buf headers are
         * 4 bytes.  We want our send and receive buffers to hold a 
	 * minimum of 4k worth of data so we must allocate at least
	 * 48 = 10 * 4 or 88 extra bytes over 10 * 4k.  Since our
	 * allocations are in page size chunks, we just round to the
	 * next 4k modulus.  i.e. 4096 * 11
	 */
	queue_parms.send_buf_size = 45056;
	queue_parms.recv_buf_size = 45056;
	queue_parms.send_sub_buf_cnt = 10;
	queue_parms.recv_sub_buf_cnt = 10;

	vxt_ioctl(*sock, VXT_IOCTL_SET_BUF_PARMS,
	          &queue_parms, sizeof(queue_parms));

	/*
	 * Attempt to create a new device and connect our
	 * vxt_socket to it.
 	 * With vxt_bind, if a device of the same name
	 * already exists, the call will fail.
	 */
	VXTPRINT(3, "Do vxt_bind\n");
        vxt_bind(*sock, &rsh_name, sizeof(vxt_dev_addr_t));
        if (ret != 0) {
                VXTPRINT(0, "vxt_bind failed\n");
		exit(1);
                return VXT_BUSY;
        }



	return VXT_SUCCESS;


}

#include <fcntl.h>
#include <pty.h>
#include <errno.h>
/*
 * 
 * rsh_rcv_thread:
 *
 * We will keep a separate thread around to wait on data sent from
 * the server.
 *
 * We use the poll on the receive so that we can run disaggregated
 * signals on the sends and receives.  i.e. the Sends will use the
 * synchronous wait mechanism.
 *
 */

static int
rsh_rcv_thread(void *args)
{
	int            dev_sock = *(int *)args;
	vxt_poll_obj_t fds[1];
	ssize_t        len;
	uint8_t        rcv_buf[RSH_BUF_SIZE];
	int            connected = 0;
	int            ret;

	VXTPRINT(3, "starting\n");

	while(1) {

		fds[0].events = POLLVXT | VXTSIG_IN;
		fds[0].revents = 0;
		fds[0].object = dev_sock;

		ret = vxt_poll(fds, 1, -1);

		if ((ret == -1) || (fds[0].revents & VXTSIG_HUP)) {
			VXTPRINT(1, "rsh_rcv_thread: vxt_poll call%s",
			         " failed, sleep and try again\n");
			sleep(2);
			ret = vxt_poll(fds, 1, -1);
			if ((ret == -1) || (fds[0].revents & VXTSIG_HUP)) {
				VXTPRINT(3, "rsh_rcv_thread: vxt_poll call%s",
				         " failed, exiting\n");

				endwin();
				/*
				 * Exit from the entire client shell
				 */
				exit(0);
			}

		}
	
{
/*
struct termios termios_p;
tcsetattr (1, TCSANOW, &termios_p);
*/
}



		while (1) {
			int offset;
			int writelen;
			len = RSH_BUF_SIZE-1;
			ret = vxt_recv(dev_sock, rcv_buf, &len, 0);
			rcv_buf[RSH_BUF_SIZE-2] = 0;

			VXTPRINT(4, "vxt_recv: receive data len = %lu\n",
			          (long)len);
			if (len == 0) {
				break;
			}
			rcv_buf[len] = 0;

			VXTPRINT(4, "string from remote:%s\n", rcv_buf);

			if (!connected) {
				setsid();
				/*
				 * Initialize Curses
				 */
				initscr();

				raw();
				connected = 1;
			}

			offset = 0;
			while(len) {
				writelen =
				   write(STDOUT_FILENO, &rcv_buf[offset], len);
				if (writelen <= 0) {
					usleep(100);
					continue;
				}
				if (len == writelen) {
					break;
				}
				len -= writelen;
				offset+=writelen;
	
	
			}
		}

	}


	VXTPRINT(3, "shuting down\n");

	return VXT_SUCCESS;
}


/*
 * vxtdb_parse_option_parm:
 *
 * vxtdb_parse_option_parm is a helper function for the
 * parse engine switch statement.  It parses the
 * string after the option character.  e.g. in "-fabc"
 * vxtdb_parse_option_parm is responsible for parsing
 * "abc".  It must be noted that "-f abc" is a different
 * case and only the space will be recognized below.
 * 
 */ 

static int
vxtdb_parse_option_parm(char **argv, int argc, char *parse_string,
                        int *option, int *token, int *index)
{
	int i = *token;
	int j = *index;

	if (!(*option)) {
		VXTPRINT(4, "where is \"-\"\n");
		return command_line_error();
	}
	if (*option == 1) {
		j++;
	}
	if (argv[i][j] != '\0') {
		while (argv[i][j] != '\0') {
			if (argv[i][j] == ' ') {
				j++;
				continue;
			} else {
				break;
			}
		}
		strncpy(parse_string, &(argv[i][j]), MAX_FILE_STRING);
		*option = 0;
		parse_string[MAX_FILE_STRING - 1] = 0;
		parse_string = NULL;
		VXTPRINT(4, "finished getting parameter string " "name\n");
		*token = ++i;
		*index = 0;
		return 0;
	}
	VXTPRINT(4, "Null char found\n");
	*option = 2;
	i++;
	if (i >= argc) {
		VXTPRINT(4, "i is larger than argc\n");
		return command_line_error();
	}
	*token = i;
	*index = 0;
	return 0;
}

command_line_error()
{
	printf("\n\nusage ... \n\n");
	printf("\tvxt_rsh [-I <command input file>] <name>\n\n");
	return -1;
}


main(int argc, char **argv)
{
	int ret;
	pthread_t rcv_thread;
	uint64_t  threadid;
	uint32_t  redirect_input = 0;
	char      *parse_string;
	char send_buf[RSH_BUF_SIZE];
	char input_file_name[MAX_FILE_STRING];
	int option = 0;
	int target_endpoint = 0;
	int input_commands;
	int i,j;


	if (argc < 2) {
		printf("\n\nusage ... \n\n");
		printf("\tvxt_rsh <name>\n\n");
		return -1;
	}


	i = 1;
	j = 0;
	input_commands = 0;
	input_file_name[0] = '\0';
	parse_string = NULL;
	while(i < argc) {
		/*
		 * We may parse multiple flags or options into
		 * one token if there are no spaces.  Hence
		 * the while loop below.
		 */
		while ((i < argc) && (argv[i][j] != '\0')) {
			VXTPRINT(4, "switch on, %c\n", argv[i][j]);
			switch(argv[i][j]) {
			case '-': {
				if (option) {
					VXTPRINT(4, "double option \"-\"\n");
					return command_line_error();
				}
				option = 1;
				j++;
                                break;
                        }
			case 'I': {
				if (option == 1) {
					VXTPRINT(4, "case I, "
					         "Input redirection\n");
					parse_string = input_file_name;
				}
				/*
				 * We are now in an option parse
				 * for a string, go to the option
				 * string reader, we may not have a
				 * space to separate the option letter from
				 * its string.
				 */
				ret = vxtdb_parse_option_parm(argv, argc, 
				                              parse_string,
				                              &option,
				                              &i, &j);
				if (ret != 0) {
					return ret;
				}

				if (option == 0) {
					parse_string = NULL;
				}

				break;
			}
			default:
			{
				VXTPRINT(4, "default, i %d, j %d\n",i,j);
				if (option) {
					VXTPRINT(4, "in option parsing\n");
					if (parse_string == input_file_name) {
						VXTPRINT(4, "in filestring "
						         "aquisition\n");
						strncpy(parse_string,
						        &(argv[i][j]),
						        MAX_FILE_STRING);
						option = 0;
						j=0;
						i++;
						parse_string
						   [MAX_FILE_STRING - 1] = 0;
						parse_string = NULL;
						VXTPRINT(4, "finished getting "
						         "filename after "
							 "space\n");
						break;
					} else {
						printf("\n\nUnknown Option\n");
						return command_line_error();
					}
				} else {
					if (j != 0) {
						VXTPRINT(4, "starting in the"
						         "middle of default "
						         "string\n");
						return command_line_error();
					}
					if (target_endpoint != 0) {
						return command_line_error();
					}
					target_endpoint = 1;
					target_domain = atoi(&argv[i][j]);
					i++;
				}
			}
			if (i >= argc) {
				break;
			}
			}
		}
		option = 0;
			j = 0;
			i++;

	}


 	pthread_mutex_init(&rcv_sync_lock, NULL);
	pthread_cond_init(&rcv_sync_var, NULL);
					
	VXTPRINT(4, "redirect file name: %s\n", input_file_name);

	if (target_domain == 0) {
		/*
		 * Default connection to the HUB
		 */
		strncpy(target_uuid, VXTDB_DOM0_ENDPOINT, MAX_VXT_UUID); 
	} else {
		vxt_db_query_arg_t query_arg;
		int                authdev;

		VXTPRINT(3, "Open VxT Authorization Database\n");
		authdev = open("/dev/vxtdb-auth", O_RDWR);
		if(authdev < 0) {
			VXTPRINT(0, "\n\nFailed to open the Vxt authorization "
			         "database device\n\n");
			return -1;
		}
		query_arg.hub = 1;
		ioctl(authdev, VXT_AUTH_DB_QUERY, (void *)(long)&query_arg);
		if (!query_arg.hub) {
			/*
			 * Get the routing and authorization through the
			 * auth db intermediary.
			 */
			VXTPRINT(3, "We are not a hub, use default routing."
			         " If in future we utilize a physical device"
			         " With internal routing, code may go here\n");
			strncpy(target_uuid,
				VXTDB_DOM0_ENDPOINT, MAX_VXT_UUID); 
		} else {
#ifdef XSTOOLS
			struct xs_handle *xs;
			xs_transaction_t th;
			int len;
			char ep_id_path[32];
			char *xenstore_uuid_path;
	
			xs = xs_domain_open();
			if (!xs) {
				perror("xs_domain_open failure");
				return -1;
			}

			sprintf(ep_id_path, "/local/domain/%d/vm",
			        target_domain);
			th = xs_transaction_start(xs);
			xenstore_uuid_path = 
			   (char *) xs_read(xs, th, ep_id_path, &len);
			if (!xenstore_uuid_path) {
				VXTPRINT(0, "Target domain %d not valid. "
				         "Exiting...\n", target_domain);
				return -1;
			}
			strncpy(target_uuid,
			        &(xenstore_uuid_path[4]), MAX_VXT_UUID-4);
			free(xenstore_uuid_path);
			target_uuid[MAX_VXT_UUID-1] = 0;
			if(!xs_transaction_end(xs, th, 0)) {
	       	        	VXTPRINT(0,
				         "Could not read xenstore database\n");
				return -1;
			}
#else
			strncpy(target_uuid,
				VXTDB_DOM0_ENDPOINT, MAX_VXT_UUID); 
#endif
		}
        }



	vrsh_set_sig_handler();

	ret = vrsh_create_device(&vsock);
	if (ret != VXT_SUCCESS) {
		return -1;
	}

	vxt_mb();

        ret = pthread_create(&rcv_thread, NULL, 
	                     (void *) &rsh_rcv_thread, (void *)&vsock);

	if (ret == -1) {
		endwin();
		return -1;
	}


	/*
	 * Starting local interpreter
	 */
	while(TRUE) {
		char buf[1024];
		int vxt_send_len;
		int read_len;
		size_t len = 0;
		ssize_t read_size;
		
/*
		char *line = NULL;
		read_size = getline(&line, &len, stdin);
		VXTPRINT(4, "Picked up command: %s of length %d\n",
		         line, read_size);
*/

		if (strnlen(input_file_name, MAX_FILE_STRING) != 0) {
			input_commands = open(input_file_name, O_RDWR);
			if(input_commands < 0) {
				VXTPRINT(0, "\n\n\tFailed to open the Vxt "
				         "input redirection command file\n\n");
				return -1;
			}
			while ((read_len =
			        read(input_commands, &buf, 1024)) > 0) {
				vxt_send_len = read_len;
				while (vxt_send_len != 0) {
					ret =
					   vxt_send(vsock, buf,
					            &vxt_send_len,
					            VXT_SOCK_SIGNAL
					               | VXT_SOCK_WAIT);
					read_len -= vxt_send_len;
					vxt_send_len = read_len;
					
				}
			}

			pthread_mutex_lock(&rcv_sync_lock);
			pthread_cond_wait(&rcv_sync_var, &rcv_sync_lock);
			pthread_mutex_unlock(&rcv_sync_lock);
			return 0;
		}
		while ((read_len = read(STDOUT_FILENO, &buf, 1024)) > 0) {
		 	/*
			 * Local debug trace, writes stream bound for
			 * remote to stdout
	  		 *  write(STDOUT_FILENO, &buf, sendlen);
			 */
			vxt_send_len = read_len;
			while (vxt_send_len != 0) {
				ret = vxt_send(vsock, buf, &vxt_send_len,
				               VXT_SOCK_SIGNAL | VXT_SOCK_WAIT);
				read_len -= vxt_send_len;
				vxt_send_len = read_len;
				
			}
	 	}
	}


}
