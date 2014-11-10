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
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <poll.h>
#include <linux/un.h>
#include <dlfcn.h>

#define SOCKET	unsigned int
#include "mxt_lib.h"

extern char             *optarg;

struct
sockaddr_in mux_getipa(hostname, port)
        const char*     hostname;
        int             port;
{
        struct sockaddr_in      ipa;

        ipa.sin_family = AF_INET;
        ipa.sin_port = htons(port);

        struct hostent* localhost = gethostbyname(hostname);
        if(!localhost){
                printf("resolving localhost");
                return ipa;
        }

        char* addr = localhost->h_addr_list[0];
        memcpy(&ipa.sin_addr.s_addr, addr, sizeof addr);

        return ipa;
}

int
main(argc, argv)
	int argc;
	char **argv;

{
        struct protoent *tcp1;
        int     listenfd, newfd;
        int     ret, on = 1;
	struct sockaddr_in	portal1;
	char	write_buf[1000];
	char	c;
	int	wfd = 0;
	char	*wfilename = NULL;
	int	nbytes = 0, write_done = 0;
	int	total = 0;
	struct mux_init_config	conf;
	void*	lib_handle;
	char	*target_host;
	int	connection_port;
	char	*error;
	int	(*fn)(struct mux_init_config  *);
	int	(*acceptfn)(int, struct mux_handshake_frame *, void *);
	struct mux_handshake_frame	handshake;

	if ((argc != 5) && (argc != 7)) {
		fprintf(stderr, "Usage : ./serv_test -p <connection port> -h <target host>\n");
		fprintf(stderr, " argc : %d \n", argc);
		exit(0);
	}
	while ((c = getopt(argc, argv, "w:p:h:")) != EOF) {
		switch (c) {
		case 'w':
			wfilename = optarg;
			break;

		case 'p':
			connection_port = atoi(optarg);
			break;

		case 'h':
			target_host = optarg;
			break;

		default:
			fprintf(stderr, "Usage : ./serv_test -p <connection port> -h <target host>\n");
			exit(0);

		}
	}

	if (wfilename) {
		wfd = open(wfilename, O_CREAT | O_WRONLY);
		if (wfd < 0) {
			fprintf(stderr, "file open failed \n");
			exit(0);
		}
	}

        tcp1 = getprotobyname("tcp");

	listenfd = socket(PF_INET, SOCK_STREAM, tcp1->p_proto);
	if (listenfd == -1) {
                printf("Error createing a tcp socket for listening");
                exit(0);
	}

	/*
	portal1	 = mux_getipa("localhost", 5060);
	*/
	portal1	 = mux_getipa("localhost", XPRTLD_WELL_KNOWN_PORT);

	ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (ret != 0) {
                printf("setsockopt failed for SO_REUSEADDR\n");
                return -1;
        }


	if(bind(listenfd, (struct sockaddr*)&portal1, sizeof(portal1)) == -1){
		printf("Failed binding socket to IP address");
		printf("Listener thread is exiting");
		return -1;
	}

	strncpy(conf.mux_target_host, target_host, 32);
	conf.mux_connection_port = connection_port;

	lib_handle = dlopen("./libmxt.so", RTLD_NOW|RTLD_GLOBAL);
	if (!lib_handle) {
		fprintf(stderr, "%s\n", dlerror());
		exit(1);
	}
	fn = dlsym(lib_handle, "mux_lib_init");
	if ((error = dlerror()) != NULL)  {
		fprintf(stderr, "%s\n", error);
		exit(1);
	}

	ret = (*fn)(&conf);
	if (ret) {
		fprintf(stderr, "Unable to init Mux library\n");
		dlclose(lib_handle);
		exit(0);
	}

	for (;;) {
		if (listen(listenfd, 255) == -1) {
                        printf("Failed setting socket to listen");
                        return -1;
                }
		newfd = accept(listenfd, NULL, NULL);
		if (newfd == -1) {
                        printf("Accept failed\n");
                        continue;
                }

		acceptfn = dlsym(lib_handle, "mux_accept");
		if ((error = dlerror()) != NULL)  {
			fprintf(stderr, "%s\n", error);
			close(newfd);
			continue;
		}

		ret = (*acceptfn)(newfd, &handshake, NULL);
		if (ret) {
			fprintf(stderr, "Unable to init Mux library\n");
			close(newfd);
			continue;
		}

		fprintf(stderr, "Forking another thread \n");
		if (fork() == 0) {
			printf("New connection from server mxt \n");
			close(listenfd);
			for (;;) {
				memset(write_buf, '\0',
					sizeof(write_buf));
retry:
				ret = recv(newfd, write_buf,
					sizeof(write_buf), 0);
				if (ret <= 0) {
					printf("Data receive failed : %d \n", ret);
					if (errno == EAGAIN) {
						sleep(1);
						goto retry;
					}
					
					close(newfd);
					close(wfd);
					exit(0);
				} else {
					write_done = 1;
				}

				if (wfilename) {
					nbytes = write(wfd,   write_buf,   ret);
				}
				printf("Data written : %d \n", ret);
				total += ret;
				printf("Total : %d \n", total);
				fflush(stdout);
			}
		} else {
			close(newfd);
			continue;
		}
	}
}

