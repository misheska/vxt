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
#include <fcntl.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <poll.h>
#include "mxt.h"

extern char             *optarg;

int
main(argc, argv)
	int argc;
	char **argv;

{
        struct protoent* tcp, *tcp1;
        int     sfd, listenfd, newfd;
        struct sockaddr_in      isa;
        int     ret;
	char	*target_host;
	struct sockaddr_in	portal, portal1;
	char	buf[200];
	char	c;
	int	server_port;
	int	i;

	/*
	 * Create a listener socket. Listening on server_port+1.
	 */
        tcp1 = getprotobyname("tcp");

	listenfd = socket(PF_INET, SOCK_STREAM, tcp1->p_proto);
	if (listenfd == -1) {
                printf("Error createing a tcp socket for listening");
                exit(0);
	}
	if (mux_sock_soreuseaddr(listenfd) != 0) {
		printf("Unable to set SO_REUSEADDR \n");
		close(listenfd);
		exit(0);
	}

	portal= mux_getipa("localhost", XPRTLD_WELL_KNOW_PORT); 

	if(bind(listenfd, (struct sockaddr*)&portal, sizeof(portal)) == -1){
		printf("Failed binding socket to IP address");
		printf("Listener thread is exiting");
		return -1;
	}

	for (;;) {
		if (listen(listenfd, 255) == -1) {
                        printf("Failed setting socket to listen");
                        return;
                }
		newfd = accept(listenfd, NULL, NULL);
		if (newfd == -1) {
                        printf("Accept failed\n");
                        continue;
                }
		if (fork() == 0) {
			/* Child process */
			printf("New connection from server mxt \n");
			close(listenfd);
			if (mux_disable_nagle(newfd) != 0) {
				printf("Unable to set TCP_NODELAY \n");
				close(newfd);
				exit(0);
			}

			for (;;) {
				memset(buf, '\0', sizeof(buf));
				ret = recv(newfd, buf, sizeof(buf), 0);
				if (ret <= 0) {
					printf("Data receive failed : %d \n", ret);
					exit(0);
				}
				printf("Data received : %d %s\n", ret, buf);

				/* Convert in capital */
				for (i = 0; i < ret; i++) {
					buf[i] -= 32;
				}
				 
				ret = send(newfd, buf, strlen(buf), 0);
				if (ret <= 0) {
					printf("Data send failed : %d \n", ret);
					exit(0);
				}
				printf("Data sent : %d %s\n", ret, buf);

			}
		} else {
			close(newfd);
			/* Parent process */
			continue;
		}
	}
}

