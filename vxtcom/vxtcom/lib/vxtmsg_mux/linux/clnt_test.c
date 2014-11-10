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
#include <linux/un.h>
#include <poll.h>
#include "mxt_platform.h"
#include "mxt_lib.h"

extern char             *optarg;

int
main(argc, argv)
	int argc;
	char **argv;

{
        struct protoent* tcp;
        int     sfd;
        struct sockaddr_in      isa;
        int     ret;
        struct mux_handshake_frame       handshake;
	char	*target_host;
	int	client_port;
	struct sockaddr_in	portal;
	char	buf[1000];
	char	c;
	struct sockaddr_un      server;
	char	*uuid = NULL, *destuuid = NULL;

	while ((c = getopt(argc, argv, "d:")) != EOF) {
                switch (c) {
                case 'd':
                        destuuid = optarg;
                        break;

		default:
			exit(0);
		}
	}


        tcp = getprotobyname("tcp");

        sfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if(sfd == -1){
                printf("Error createing a tcp socket");
                exit(0);
        }

	server.sun_family = AF_UNIX;
	strcpy(server.sun_path, MY_MXT_UDS_PATH);

        if(connect(sfd, (struct sockaddr *)&server, sizeof(server)) == -1){
                printf("Error connecting to server");
		exit(0);
        }

        /*
         * Send the register message for this client.
         */
        memset(&handshake, '\0', sizeof (handshake));
        handshake.mux_handshake_magic = XPRTLD_MAGIC;
	handshake.mux_handshake_frame_len = sizeof(struct mux_handshake_frame);
	handshake.mux_handshake_flags = 0;

	uuid = mux_get_my_uuid();
	memcpy(handshake.mux_handshake_src_uuid, uuid, MUX_UUID_SIZE);
	memcpy(handshake.mux_handshake_src_endpoint, "XPRTLD", strlen("XPRTLD"));

	uuid = mux_convert_stduuid_to_byteuuid(destuuid);
	memcpy(handshake.mux_handshake_dest_uuid, uuid, MUX_UUID_SIZE);
	memcpy(handshake.mux_handshake_dest_endpoint, "XPRTLC", strlen("XPRTLC"));

	ret = send(sfd, (char *)&handshake, sizeof(handshake), 0);
	if (ret <= 0) {
		printf("Data send failed : %d \n", ret);
		exit(0);
	}

	for (;;) {
		memset(buf, '\0', sizeof(buf));
		printf("Enter a string: ");
		scanf("%s", buf);
		ret = send(sfd, buf, strlen(buf), 0);
		if (ret < 0) { 			
			printf("Data send failed : %d \n", ret);
			exit(0);
		}
		printf("Data sent : %d \n", ret);

		memset(buf, '\0', sizeof(buf));
		ret = recv(sfd, buf, sizeof(buf), 0);
		if (ret <= 0) {
			printf("Data receive failed : %d \n", ret);
			exit(0);
		}
		printf("Data received : %s \n", buf);
	}
}

