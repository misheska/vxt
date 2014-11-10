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


extern char             *optarg;


int
main(argc, argv)
	int argc;
	char **argv;

{
        int     sfd;
        int     ret;
	char	read_buf[1000];
	char	c;
	int	rfd;
	char	*rfilename;
	int	nbytes = 0;
	int	total = 0;
	char	*destuuid;
	int	(*fn1)(int, void *, void *, int *);
	int	(*fn2)(int, char *, int, int);
	void	*lib_handle;
	char	*error;
	

	while ((c = getopt(argc, argv, "r:d:")) != EOF) {
		switch (c) {
		case 'r':
			/* Filename for writing */
			rfilename = optarg;
			break;

		case 'd':
			destuuid = optarg;
			break;

		default:
			printf("Usage : ./clnt_test -h <target host> -p <server port> -r <read filename> -w <writefilename>\n");
			exit(0);

		}
	}

	rfd = open(rfilename, O_RDONLY);
	if (rfd == -1) {
		printf("File open for %s failed \n", rfilename);
		exit(0);
	}

	fprintf(stderr, "opening libmxt \n");
	lib_handle = dlopen("./libmxt.so", RTLD_LAZY);
	if (!lib_handle) {
		fprintf(stderr, "%s\n", dlerror());
		exit(1);
	}

	fn1 = dlsym(lib_handle, "mux_connect");
	if ((error = dlerror()) != NULL)  {
		fprintf(stderr, "%s\n", error);
		exit(1);
	}
	fprintf(stderr, "opening libmxt  2\n");

	ret = (*fn1)(MXT_MAGIC, destuuid, NULL, NULL);
	fprintf(stderr, "opening libmxt  ret : %d\n", ret);
	if (ret < 0) {
		fprintf(stderr, "Unable to connect using mux_connect\n");
		dlclose(lib_handle);
		exit(0);
	}

	sfd = ret;
	fn2 = dlsym(lib_handle, "mux_write");
	if ((error = dlerror()) != NULL)  {
		fprintf(stderr, "%s\n", error);
		exit(1);
	}

	for (;;) {

		memset(read_buf, '\0', sizeof(read_buf));
		nbytes = read(rfd, read_buf, sizeof(read_buf));

		if (nbytes != 0) {
retry:
			/*
			ret = send(sfd, read_buf, nbytes, 0);
			*/
			ret = (*fn2)(sfd, read_buf, nbytes, 0);
			if (ret < 0) {
				printf("Data send failed : %d \n", ret);
				sleep(1);
				goto retry;
			}
			total += ret;
			printf("Data sent : %d \n", ret);
			printf("Total : %d \n", total);
		} else {
			close(rfd);
			close(sfd);
			exit(0);
		}
	}
}

