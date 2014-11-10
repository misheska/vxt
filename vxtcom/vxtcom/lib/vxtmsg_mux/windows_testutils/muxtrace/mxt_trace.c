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
#include <signal.h>
#include  "mxt.h"

#define MUX_DEBUG_DATA	"__debug__"
#define MUX_REGISTERED_GUESTS	"__guests__"
#define MUX_DEBUG_MSG_SIZE	100
#define MUX_STDUUID_SIZE        ((16 * 2) + 6)

typedef SOCKET (*PMUX_CONNECT)(SOCKET , void *,  struct mux_auth *);
extern char             *optarg;

int
main(argc, argv)
	int	argc;
	char	**argv;
{
        SOCKET     sfd;
        int     ret;
	char	read_buf[MUX_DEBUG_MSG_SIZE];
	int	(*fn1)(int, void *, void *, int *);
	void	*lib_handle;
	int	req_type = 0, error;
	HINSTANCE	 hinstLib = NULL;

	if (argc != 2) {
		fprintf(stderr, "Usage : mux_trace debug | guests\n");
		fprintf(stderr, "Please specify either debug or guests\n");
		exit(1);
	}

	hinstLib = LoadLibrary("vxtmsg_mux.dll");
	if (hinstLib == NULL) {
		error = GetLastError();
		fprintf(stderr, "mxt_trace : %d\n", error);
		exit(1);
	}

	fn1 = (PMUX_CONNECT)GetProcAddress(hinstLib, "mux_connect");
	if (fn1 == NULL)  {
		error = GetLastError();
		fprintf(stderr, "mxt_trace : %d\n", error);
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
		FreeLibrary(hinstLib);
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
			closesocket(sfd);
            FreeLibrary(hinstLib);
			exit(0);
		}
	}
}

