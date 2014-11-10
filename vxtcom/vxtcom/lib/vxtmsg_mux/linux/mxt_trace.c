/* $Id: mxt_trace.c,v 1.4 2009/02/04 00:25:27 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/linux/mxt_trace.c,v $" */

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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <poll.h>
#include <linux/un.h>
#include <dlfcn.h>
#define SOCKET	unsigned int
#include "mxt_lib.h"

#define MUX_DEBUG_DATA	"__debug__"
#define MUX_REGISTERED_GUESTS	"__guests__"
#define MUX_DEBUG_MSG_SIZE	100
#define MUX_STDUUID_SIZE        ((16 * 2) + 6)

extern char             *optarg;

int
main(argc, argv)
	int	argc;
	char	**argv;
{
        int     sfd;
        int     ret;
	char	read_buf[MUX_DEBUG_MSG_SIZE];
	int	(*fn1)(int, void *, void *, int *);
	void	*lib_handle;
	char	*error;
	int	req_type = 0;

	if (argc != 2) {
		fprintf(stderr, "Usage : ./mux_trace debug | guests\n");
		fprintf(stderr, "Please specify either debug or guests\n");
		exit(1);
	}

	lib_handle = dlopen("/usr/lib/libmxt.so", RTLD_LAZY);
	if (!lib_handle) {
		fprintf(stderr, "mxt_trace : %s\n", dlerror());
		exit(1);
	}

	fn1 = dlsym(lib_handle, "mux_connect");
	if ((error = dlerror()) != NULL)  {
		fprintf(stderr, "mxt_trace : %s\n", error);
		exit(1);
	}

	if ((strncmp(argv[1], "guests",
	     strlen("guests")) == 0) && 
	     (strlen("guests") == strlen(argv[1]))) {
		ret = (*fn1)(MXT_MAGIC, MUX_REGISTERED_GUESTS, NULL, NULL);
		req_type = 1;
	} else if ((strncmp(argv[1], "debug",
		   strlen("debug")) == 0) &&
		   (strlen("debug") == strlen(argv[1]))) {
		ret = (*fn1)(MXT_MAGIC, MUX_DEBUG_DATA, NULL, NULL);
		req_type = 2;
	} else {
		fprintf(stderr, "Invalid parameter : %s\n", argv[1]);
		fprintf(stderr, "Usage : ./mux_trace debug | guests\n");
		fprintf(stderr, "Please specify either debug or guests\n");
		exit(1);
	}

	if (ret < 0) {
		fprintf(stderr, "mxt_trace: mux_connect failed\n");
		dlclose(lib_handle);
		exit(1);
	}

	sfd = ret;

	for (;;) {
		memset(read_buf, '\0', sizeof(read_buf));
		if (req_type == 1) {
			ret = recv(sfd, read_buf, MUX_STDUUID_SIZE, 0);
		} else {
			ret = recv(sfd, read_buf, MUX_DEBUG_MSG_SIZE, 0);
		}
		if ((ret > 0) && (strlen(read_buf))) {
			fprintf(stdout, "%s\n", read_buf);
		} else if (ret <= 0) {
			close(sfd);
			exit(0);
		}
	}
}

