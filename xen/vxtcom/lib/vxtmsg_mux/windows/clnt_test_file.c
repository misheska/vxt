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
#include  "mxt.h"

#ifndef _WINNT
#include <netdb.h>
#include <signal.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <poll.h>
#include "mxt_lib.h"
#endif

#ifdef _WINNT
extern int getopt(int argc, char *argv[], char *opstring); 
extern int mux_sock_blocking(SOCKET);
extern int mux_disable_nagle(SOCKET);
typedef SOCKET (*PMUX_CONNECT)(SOCKET , void *,  struct mux_auth *, int *);
typedef int (*PMUX_WRITE)(SOCKET , char *, int, int);
#endif

extern char             *optarg;
extern struct sockaddr_in mux_getipa(const char*, int);
extern int mux_disable_nagle(SOCKET);

int init_wsa( void )
{
    WORD             wVersionRequested;
    WSADATA      wsaData;
    int                   err;

    wVersionRequested = MAKEWORD( 2, 2 );

    err = WSAStartup( wVersionRequested, &wsaData );
    if ( err != 0 )
    {
        err = WSAGetLastError();
        printf("init_was Failed : Error = %d\n", err);
        return 1;
    }
    return 0;   //Success
}

int
main(argc, argv)
	int argc;
	char **argv;

{
    SOCKET              sfd;   
    int                       ret;
	char	                 read_buf[1000];
	char	                c = '0';
#ifdef _WINNT
	HANDLE      	     rfd;
    HINSTANCE        hinstLib = NULL;
    PMUX_CONNECT        fn1;
    PMUX_WRITE             fn2;
#else
    int                 rfd, wfd;
#endif
	char	            *rfilename;
	int	                nbytes = 0;
	int	                read_done = 0, write_done = 0;
	int	                first = 0;
	int	                total = 0;

    char	            *uuid = NULL, *destuuid1 = NULL;
    INT                 error;
    int                   destGiven = 0;
    char    srcuuid[16] ={'v','v','v','v','v','v','v','v','v','v','v','v','v','v','v','v'};
    //char    destuuid[16] ={'n','n','n','n','n','n','n','n','n','n','n','n','n','n','n','n'};
    //char        *destuuid  =   "4e516c58fe56bbd0d9be748066e0b641";
    /*
     * This is hardcoded for testing prupose.
     */

    char                        *destuuid  =   "bad58fe735e54e53b5c3c91b29e7d383";
    BOOL                       readResult;
    struct mux_auth	 auth;
    char	                    *target_host;

    while ((c = getopt(argc, argv, "r:d:h:")) != EOF) {
		switch (c) {
		case 'r':
			/* Filename for writing */
			rfilename = optarg;
			break;

		case 'd':
			destuuid = optarg;
            destGiven = 1;
			break;

        case 'h':
            target_host = optarg;
            break;

		default:
			printf("Usage : ./clnt_test -h <target host>  -r <read filename> -d <domain uuid>\n");
			exit(0);

		}
	}

    rfd =CreateFile(rfilename, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ, NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_READONLY,
                                NULL);

    if(rfd == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "File Open Failed %s, %d\n", rfilename, GetLastError());
    }
    fprintf(stderr, "FileOpen Sucessful\n");
    error = init_wsa();
    if(error = init_wsa()) {
        printf("Socket Initialization Failed\n");
        exit(-1);
    }

    hinstLib = LoadLibrary("vxtmsg_mux.dll");
    if (hinstLib == NULL) 
    { 
        error = GetLastError();
        fprintf(stderr, "vxtmsg_mux Failed :ERROR =%d\n", error);
        exit(0);
    }

    fn1 = (PMUX_CONNECT)GetProcAddress(hinstLib, "mux_connect");
    if(fn1 == NULL){
        fprintf(stderr, "Failed to get Function pointer for mux_connect: %d\n", GetLastError());
        exit(1);
    }

    fn2 = (PMUX_WRITE)GetProcAddress(hinstLib, "mux_write");
    if(fn2 == NULL){
        fprintf(stderr, "Failed to get Function pointer for mux_write: %d\n", GetLastError());
        exit(1);
    }

    fprintf(stderr, "Load vxtmsg_mux.dll Sucessful\n");
   

    ret = (int)(*fn1)(MXT_MAGIC, destuuid, &auth, NULL);
    if(ret < 0) {
        fprintf(stderr, "Mux Connection Falied\n");
    }

    printf("Connection established \n");
    sfd = (SOCKET)ret;
    
	for (;;) {

		memset(read_buf, '\0', sizeof(read_buf));
#ifdef _WINNT
        readResult = ReadFile(rfd, (void*)read_buf, sizeof(read_buf), &nbytes, NULL);
        if(readResult && nbytes == 0) {
            fprintf(stderr, "Reached end of file\n");
        }
        getchar();
        if(!readResult) {
            error = GetLastError();
            fprintf(stderr, "Read Operation Failed\n");
            fprintf(stderr, "The read error is %d\n", error);
        }
#else
		nbytes = read(rfd, read_buf, sizeof(read_buf));
#endif
		if (nbytes != 0) {
            fprintf(stderr, "The number of bytes send %d\n", nbytes);
retry:
			ret = send(sfd, read_buf, nbytes, 0);
			if (ret < 0) {
				printf("Data send failed : %d \n", ret);
				Sleep(1);
				goto retry;
			}
			total += ret;
			printf("Data sent : %d \n", ret);
			printf("Total : %d \n", total);
            fprintf(stderr, "Bytes Sent =%d\n", nbytes);
            getchar();
		} else {
#ifdef _WINNT
            CloseHandle(rfd);
            closesocket(sfd);
#else
			close(rfd);
			close(sfd);
#endif
			exit(0);
		}
	}
}

