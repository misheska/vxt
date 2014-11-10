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



#include <vxt_tools.h>

#include <vxt_system.h>
#include <symcom_dev_table.h>
#include <vxt_trace_entries.h>
#include <vxt_com.h>

DECLARE_VXT_TRACE_ENTRIES

DECLARE_VXT_TRACE_PARMS	

vxtctrlr_handle_t fd;

/*
 *
 * command_line_error:
 *
 * Prints message to be used when command line input formats
 * are detected.
 *
 * Returns:
 *		-1
 *
 */

int
command_line_error()
{
	printf("\n\n usage ... \n\n");
	printf("        vxt_trace <controller #> <slot #>\n\n");
	printf("        vxt_trace -f <file>\n\n\n");
	return -1;
}


/*
 * vxt_trace:
 *
 * vxt_trace is called with a vxt_controller number and a
 * slot on that controller.  vxt_trace calls the
 * vxt ioctl that dumps the contents of the trace buffer
 * for thte targeted controller/slot.
 *
 * Errors are reported for failure to open the requested
 * controller device file and failure of a device in 
 * the targeted slot to respond.
 *
 * Returns:
 *
 *		None
 */

void
vxt_trace(uint32_t ctrlr_number, uint32_t device_number)
{

	vxt_trace_playback_parms_t playback_parms;
	vxt_trace_entry_t buffer[500];
	uint64_t          *data;
	char              file_name[64];
	uint32_t          i;
	int               ret;

	sprintf(file_name, VXT_CTRLR_DEV_NAME "%d", ctrlr_number);
        printf("VXT_TRACE on slot %lu, in controller %s\n",
	       device_number, file_name);

	fd = vxt_dev_open(file_name, O_RDWR);
	if (fd == VXT_BAD_FD) {
		printf("Error opening %s\n\n\n", file_name);
		return;
	}
	playback_parms.buffer = buffer;
	playback_parms.device_handle = device_number;
	playback_parms.entry_count = 500;
	ret = vxt_dev_ioctl(fd, IOCTL_VXTCTRLR_TRACE_PLAYBACK,
                            (void *)&playback_parms);

	if (ret == -1) {
		printf("VxT Device in slot %lu, did not respond\n\n\n",
		       device_number);
		return;
	}

	data = (uint64_t *)buffer;
/*
	printf("first line of data:");
	printf("0x%llx, 0x%llx, 0x%llx, 0x%llx\n", 
	       data[0], data[1], data[2], data[3]);
*/
	printf("Log has %d entries\n\n\n\n", playback_parms.entry_count);

	for (i = 0; i < playback_parms.entry_count; i++) {
		data = (uint64_t *)&buffer[i];

		switch (vxt_trace_parm_cnt[data[0]]) {
		case 0:
			printf(vxt_trace_entries[data[0]]);
			break;
		case 1:
			printf(vxt_trace_entries[data[0]], data[1]);
			break;
		case 2:
			printf(vxt_trace_entries[data[0]],
			       data[1], data[2]);
			break;
		case 3:
			printf(vxt_trace_entries[data[0]],
			       data[1], data[2], data[3]);
			break;
		default:
			printf("\nLog entry parameter count out of bounds\n");
		}
	}

}

/*
 *
 * main:
 *
 *  processes the calling arguments and calls the trace routine to
 *  pick up the trace buffer form the targeted devices.
 *
 *  vxt_trace will return a single trace file if it is responding
 *  to targeting from the command line.  However, it will loop over 
 *  multiple requests if it is directed to a vxt_device record table.
 *
 */

vxt_main(int argc, char **argv)
{
	char     device_records_name[MAX_FILE_STRING];
	char     record[100];
	char     *rec_ptr;
	int      device_records;
	uint32_t domain_number;
	uint32_t ctrlr_number;
	uint32_t device_number;
	uint32_t i;

	if (argc == 1) {
		return command_line_error();
	}
	if (argv[1][0] == '-') {
		if (argv[1][1] != 'f') {
			return command_line_error();
		}
		if (argv[1][2] != '\0') {
			int j,k;

			if (argc != 2) {
				return command_line_error();
			}

			k = 0;
			j = 2;
			while (argv[1][j] != '\0') {
				if (argv[1][j] == ' ') {
					j++;
					continue;
				}
				if (((argv[1][j] < 'a') || 
				     (argv[1][j] > 'z')) && 
				    ((argv[1][j] < 'A') ||
				     (argv[1][j] > 'Z')) && 
				    (argv[1][j] != '/')) {
					return command_line_error();
				}
				device_records_name[k] = argv[1][j];
				k++;
				j++;
				if (k >= MAX_FILE_STRING) {
					return command_line_error();
				}
			}
			device_records_name[k] = 0;

		} else {
			if (argc != 3) {
				return command_line_error();
			}
			strncpy(device_records_name, argv[2],  MAX_FILE_STRING);
		}
		device_records = vxt_file_open(device_records_name, O_RDONLY);
		if (device_records == -1) {
		   printf("\nVXT_TRACE: bad device record file name\n");
		   return -1;
		}
		vxt_file_lseek(device_records, 0, SEEK_SET);
		while (1) {
			/*
			 *  read the device records, one rec at a time
			 */
			i = 0;
			while (1) {
				vxt_file_read(device_records, &record[i], 1);
				if (record[i] == '\n') {
					break;
				}
				i++;
				if (i >= 100) {
					/* nothing more to read */
					return 0;
					
				}
			}
			if (i < 10) {
				/* nothing more to read */
				return 0;
				
			}
			domain_number = strtol(record, &rec_ptr, 10);
			ctrlr_number = strtol(rec_ptr, &rec_ptr, 10);
			device_number = strtol(rec_ptr, &rec_ptr, 10);
			printf("\n\n\n");
			printf("VXT_TRACE from scan record, ");
			printf("targeting controller connecting Domain %lu\n",
			       domain_number);
			vxt_trace(ctrlr_number, device_number);
		}
	} else {
		if (argc != 3) {
			return command_line_error();
		}
		ctrlr_number = atoi(argv[1]);
		device_number = atoi(argv[2]);
		printf("\n\n\n");
		printf("VXT_TRACE from command line parameters\n");
		vxt_trace(ctrlr_number, device_number);
	}

	
}
