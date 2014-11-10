#include <vxt_tools.h>

#include <vxt_system.h>
#include <vxt_msg_export.h>
#include <vxt_socket.h>
#include <symcom_dev_table.h>
#include <vxt_com.h>
#include <vxt_msg_dev.h>


/*
 *
 * vxt_scan.c
 *
 * This module implements the vxt_scan tool.  vxt_scan takes a domain number
 * and queries the vxt subsytem for a controller that will reach the domain.
 * This controller is then scanned for active devices.
 *
 * vxt_scan may also create a device record file with an entry for each
 * device it finds in the targeted controller.  The format of the file output
 * is:
 *
 * Domain#  Controller# Slot# <return>
 * Each number is printed in ascii decimal with two spaces in between.
 *
 * The device records are used as input for the vxt_trace facility.
 *
 * vxt_scan does not create device records by default, the user must request 
 * these by asserting the "-I" option on the command line.
 *
 * The vxt device record file default name is VXT_DEVICES.  This however may
 * be overidden with the -f option.  i.e. -f /var/log/vxt_scan_file would output
 * device records to a file named "/var/log/vxt_scan_file
 *
 * All of the following are valid command line choices:
 *
 * ("0" may be substituted by the desired domain number)
 *
 * vxt_scan 0                          -- scan for ctrlr to domain 0 
 * vxt_scan -d 0                       -- scan for ctrlr to domain 0
 * vxt_scan -I 0                       -- scan for ctrlr to dom 0 and create 
 *                                        device record
 * vxt_scan -Id 0                      -- scan for ctrlr to dom 0 and create
 *                                        device record, (default name)
 * vxt_scan -I -d 0                    -- scan for ctrlr to dom 0 and create
 *                                        device record, (default name)
 * vxt_scan -I -d 0 -f <filename>      -- scan for ctrlr to dom 0 and create
 *                                        device record, (filename)
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
 * mxt_vxt_server_dev_lookup:
 *
 * mxt_vxt_server_dev_lookup is called with ctrlr and domain numbers
 * and a bus query that is already set up.  The pre-setup of the query
 * is done to provide for additional flexability on the call.  There are
 * numerous options on the vxt bus query call.
 *
 * mxt_vxt_server_dev_lookup queries the targeted bus on the opened
 * vxt_socket that is sent as a calling parameter. The vxt subsytem
 * returns the first device after the slot number provided in the
 * query.  mxt_vxt_server_dev_lookup then queires the device at this
 * slot and prints out all available status on the device.  If 
 * the -I option has been requested, mxt_vxt_server_dev_lookup also
 * writes a device record to the vxt device file, (record_file)
 *
 * mxt_vxt_server_dev_lookup keeps querying the targeted controller.
 * The slot number field value is the starting point for further 
 * queries.  In this way the entire controller bus can be scanned.
 * mxt_vxt_server_dev_lookup stops its query when the ioctl fails.
 *
 * Returns:
 *
 *		VXT_SUCCESS:  => upon successful bus scan
 *		VXT_RSRC - Less than one device found
 * 
 */

int
mxt_vxt_server_dev_lookup(int socket, vxt_query_bus_struct_t *query_bus_buf,
                          int integrate_record, int record_file, 
                          char *record_ctrlr_number, char *record_domain_number)
{
	vxt_query_dev_struct_t query_dev_buf;
	vxt_msg_dev_info_t     msgdev_info;
	char                   ctrlr_name[VXTCOM_MAX_DEV_FILE_NAME];
	char                   filestring[40];
	int                    ret;


	/*
	 * Make a local copy of the controller name to refresh
	 * with on subsequent calls.
	 */
	strncpy(ctrlr_name, query_bus_buf->ctrlr_name,
		VXTCOM_MAX_DEV_FILE_NAME);
	/*
	 * query_bus_buf is set up by the caller
	 */

	ret = vxt_ioctl(socket, VXT_IOCTL_QUERY_BUS, 
	                query_bus_buf, sizeof(vxt_query_bus_struct_t));
	if (ret) {
		printf("Failed to find a device on the controller\n");
		return VXT_RSRC;
	}

	printf("	Device Name: %s\n", query_bus_buf->uname);
	printf("	Device Type: 0x%llx\n", query_bus_buf->dev_name);
	printf("	Slot Number: 0x%x\n", query_bus_buf->vxt_bus_id);


	query_dev_buf.vxt_bus_id = query_bus_buf->vxt_bus_id;
	query_dev_buf.device_info = (void *)&msgdev_info;
	query_dev_buf.info_buf_size = sizeof(vxt_msg_dev_info_t);
	query_dev_buf.ctrlr_name[0] = 0;
	strncpy(query_dev_buf.ctrlr_name,
	        query_bus_buf->ctrlr_name, VXTCOM_MAX_DEV_FILE_NAME);
	

	while (1) {
		ret = vxt_ioctl(socket, VXT_IOCTL_QUERY_DEV,
		                &query_dev_buf, sizeof(vxt_query_dev_struct_t));
		if (ret) {
			return VXT_RSRC;
		}
		if (msgdev_info.state == VXT_MSG_DEV_CONNECTED) {
			printf("	Device state: CONNECTED\n");
		} else if (msgdev_info.state == VXT_MSG_DEV_DISCONNECTED) {
			printf("	Device state: DISCONNECTED\n");
		} else {
			printf("	Device state: ERROR\n");
		}
		printf("	SendQ size %lu\n", 
		       (long) msgdev_info.sendq_size);
		printf("	RcvQ size %lu\n\n\n", 
		       (long) msgdev_info.rcvq_size);

		if (integrate_record) {
			vxt_file_write(record_file, 
			               record_domain_number,
			               strlen(record_domain_number)); 
			vxt_file_write(record_file, record_ctrlr_number,
			               strlen(record_ctrlr_number)); 
			vxt_file_write(record_file, "  ", 2);
			sprintf(filestring, "%d   ", query_bus_buf->vxt_bus_id);
			vxt_file_write(record_file, filestring,
			               strlen(filestring)); 
			vxt_file_write(record_file, "\n", 1); 
		}

		/*
		 * Look for next device on the controller
		 */
		query_bus_buf->vxt_bus_id = query_dev_buf.vxt_bus_id;

		query_bus_buf->uname[0] = 0;
		strncpy(query_bus_buf->ctrlr_name,
		        ctrlr_name, VXTCOM_MAX_DEV_FILE_NAME);
		ret = vxt_ioctl(socket, VXT_IOCTL_QUERY_BUS, 
	                query_bus_buf, sizeof(vxt_query_bus_struct_t));
		if (ret) {
			return VXT_SUCCESS;
		}

		printf("	Device Name: %s\n",
		       query_bus_buf->uname);
		printf("	Device Type: 0x%llx\n",
		       query_bus_buf->dev_name);
		printf("	Slot Number: 0x%x\n",
		       query_bus_buf->vxt_bus_id);

		query_dev_buf.vxt_bus_id = query_bus_buf->vxt_bus_id;
	}


	return VXT_SUCCESS;

}


/*
 *
 * command_line_error:
 *
 * Called when an error in the format of command line arguments passed by the
 * user is detected. 
 *
 *
 */

int
command_line_error()
{
	printf("\n\n usage ... \n\n");
	printf("\tvxt_scan OPTIONS[-I -d <domain> -f <file>]\n%s",
	       "\t\t-I create interconnect record (default VXT_DEVICES)\n"
	       "\t\t-f Provide interconnect record filename\n"
	       "\t\t-d Provide domain number\n"
	       "\tvxt_scan  <domain #> \n\n\n");
	return -1;
}


/*
 *
 * main:
 *
 * Use a local wrapper for main to allow different operating systems
 * to compile this code without alteration
 */

vxt_main(int argc, char **argv)
{
	int socket;
	int ret;
	int i,j,k;
	int option;
	int integrate_record;
	char filename[MAX_FILE_STRING];
	char *record_ctrlr_number = NULL;
	char record_domain_number[40] = "";
	char *strptr;
	int record_file;
	vxt_query_bus_struct_t query_bus_buf;
	vxt_query_sys_struct_t query_sys_buf;

	if (argc < 2) {
		return command_line_error();
	}

	i = 1;
	j = 0;
	option = 0;
	integrate_record = 0;
	query_sys_buf.domain = atoi(argv[1]);
	strncpy(filename, "VXT_DEVICES", MAX_FILE_STRING);
	while (i < argc) {
		while (argv[i][j] != '\0') {
			switch(argv[i][j]) {
			case '-': {
				if (option) {
					return command_line_error();
				}
				option = 1;
				j++;
				break;
			}
			case 'I': {
				if (!option) {
					return command_line_error();
				}
				integrate_record = 1;
				j++;
				break;
			}
			case 'd': {
				if (!option) {
					return command_line_error();
				}
				j++;
				if (argv[i][j] != '\0') {
					query_sys_buf.domain = 
					   atoi(&(argv[i][j]));
					j = 0;
					option = 0;
					i++;
					break;
				}
				i++;
				if (i >= argc) {
					return command_line_error();
				}
				query_sys_buf.domain = atoi(argv[i]);
				j = 0;
				i++;
				option = 0;
				break;
			}
			case 'f': {
				if (!option) {
					return command_line_error();
				}
				j++;
				if (argv[i][j] != '\0') {
					k = 0;
					while (argv[i][j] != '\0') {
						if (argv[i][j] == ' ') {
							j++;
							continue;
						}
						if (((argv[i][j] < 'a') ||
						    (argv[i][j] > 'z')) && 
						    ((argv[i][j] < 'A') ||
						    (argv[i][j] > 'Z')) && 
						    (argv[i][j] != '/')) {
							return 
							   command_line_error();
						}
						filename[k] = argv[i][j];
						k++;
						j++;
						if (k >= MAX_FILE_STRING) {
							return 
							   command_line_error();
						}
					}
					filename[k] = 0;
					j = 0;
					option = 0;
					i++;
					break;
				}
				i++;
				if (i >= argc) {
					return command_line_error();
				}
				j = 0;
				strncpy(filename, argv[i],  MAX_FILE_STRING);
				i++;
				option = 0;
				break;
			}
			default:
				if (option) {
					return command_line_error();
				}
				if ((argv[i][j] >= '0')
				    && (argv[i][j] <= '9')) {
					query_sys_buf.domain = atoi(argv[i]);
					j = 0;
					i++;
					option = 0;
					break;
				}
				return command_line_error();
			}
			if (i >= argc) {
				break;
			}
		}
		option = 0;
		j = 0;
		i++;
	}
	query_sys_buf.uname[0] = 0;

	printf("\nVXT_SCAN, scanning controller for Domain %d\n\n",
	       query_sys_buf.domain);

	if (integrate_record) {
		record_file = vxt_file_open(filename, 
		                            O_CREAT | O_TRUNC | O_RDWR);
		vxt_fchmod(record_file,
		           VXT_USR_WR | VXT_USR_RD | VXT_GRP_RD | VXT_OTH_RD);
		sprintf(record_domain_number, "%d  ", query_sys_buf.domain);
	}

	/*
	 * Initialize the vxt socket library, linking it
	 * to the vxt controller.
	 */
	ret = vxt_socket_lib_init();

	if (ret != VXT_SUCCESS) {
		printf("Failed to connect to the VXT subsystem\n");
		return;
	}


	socket = vxt_socket(PF_UNIX, SOCK_STREAM, SOCK_STREAM);
	if (socket == 0) {
		printf("Failed to acquire socket\n");
		return;
	}



        ret = vxt_ioctl(socket, VXT_IOCTL_QUERY_SYS,
                        &query_sys_buf, sizeof(vxt_query_sys_struct_t));


	if (ret) {
		printf("Requested domain is not connected, there is no reaching controller\n");
		
		return;
	}
	printf("	Domain %d connecting controller: %s\n\n",
	       query_sys_buf.domain, query_sys_buf.ctrlr_name);

	if (integrate_record) {
		record_ctrlr_number =
		   strpbrk(query_sys_buf.ctrlr_name, "0123456789");
	}


	printf("Scanning for devices ...\n\n");


 	query_bus_buf.function = VXTCTRLR_LOOKUP_NEXT;
	query_bus_buf.vxt_bus_id = 0;
	query_bus_buf.uname[0] = 0;
	strncpy(query_bus_buf.ctrlr_name,
		query_sys_buf.ctrlr_name, VXTCOM_MAX_DEV_FILE_NAME);
/*
	strncpy(query_bus_buf.uname, "MXT_VXT_PIPE",  MAX_VXT_UNAME);
	strncpy(query_bus_buf.uname, "UserLevelTest",  MAX_VXT_UNAME);
*/

	ret = mxt_vxt_server_dev_lookup(socket, &query_bus_buf,
	                               integrate_record, record_file,
	                               record_ctrlr_number,
	                               record_domain_number);

	vxt_file_write(record_file, "\n", 1); 
	vxt_file_close(record_file);

	printf("Scan completed, returning\n");


}













