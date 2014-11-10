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
#include <vxt_com.h>


vxtctrlr_handle_t fd;

#define MAX_FILE_STRING 256
#define memLogMax 1048576


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
	printf("        vxt_dump <controller #> <slot #>\n\n");
	printf("        vxt_dump -f <file>\n\n\n");
	return -1;
}


/*
 * vxt_dump:
 *
 * vxt_dump is called with a vxt_controller number and a
 * slot on that controller.  vxt_dump calls the
 * vxt ioctl that dumps the contents of the dump buffer
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
vxt_dump()
{

	vxt_dumplog_struct_t dumplog;
	char  *buffer;
	uint64_t          *data;
	char              file_name[64];
	uint32_t          i;
	int               ret;
        char *p, *q;

	fd = vxt_dev_open("VxtCtrlr-Bus", O_RDWR);
	if (fd == VXT_BAD_FD) {
		printf("Error opening VxtCtrlr-Bus\n\n\n");
		return;
	}
        buffer = (char *)malloc(memLogMax);
	dumplog.buf = buffer;
	ret = vxt_dev_ioctl(fd, IOCTL_DUMP_LOG,
                            (void *)&dumplog);

	if (ret == -1) {
		printf("cannot open bus device\n\n\n");
		return;
	}


        p = buffer;
        while(1){
            while( *p!='^' && p<buffer + memLogMax) p++;
            if(*p != '^') return;
            q = ++p;
            while( *p!='$' && p<buffer + memLogMax) p++;
            if(*p != '$') return;
            *p++ = '\0';
            printf("%s", q);
       }
}

/*
 *
 * main:
 *
 *  processes the calling arguments and calls the dump routine to
 *  pick up the dump buffer form the targeted devices.
 *
 *  vxt_dump will return a single dump file if it is responding
 *  to targeting from the command line.  However, it will loop over 
 *  multiple requests if it is directed to a vxt_device record table.
 *
 */

vxt_main(int argc, char **argv)
{


vxt_dump();

	
}
