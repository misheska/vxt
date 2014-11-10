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
#include "mxt.h"
#include <windows.h>
#include "xs.h"

#ifndef _WINNT
#include <netdb.h>
#include <signal.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <poll.h>
#include <linux/un.h>
#include <dlfcn.h>
#endif

#ifdef _WINNT
typedef struct  retryParams{
    HANDLE         wfd;
    SOCKET         listenfd;
    SOCKET         newfd;
}retryParams;

HANDLE      wfd;
//FILE        *wfd;

extern int getopt(int , char *, char *) ;

typedef int(*PMUX_LIB_INIT)(struct mux_init_config *);      //Function pointer to initialize MXT
typedef int (*PMUX_ACCEPT)(SOCKET, struct mux_handshake_frame *, void *);    //Function Pointer of mux_accept
//typedef int (*PMUX_ACCEPT)(SOCKET, struct mux_auth *);    //Function Pointer of mux_accept
//typedef struct mux_peer_identity * (*PEER_IDENTITY)(SOCKET);

#endif
extern char             *optarg;

int	                        server_port = 0; 
int                           client_port  = 0;
int                           run_mode = 0;
int                           listener_port = 0;

char                        srcuuid[16] ;
char                        destuuid[16];

HINSTANCE            hinstLib;
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

/*
* Function to set socket options for resuse.
*/

struct
sockaddr_in mux_getipa(hostname, port)
        const char*     hostname;
        int             port;
{
        struct sockaddr_in      ipa;
	struct hostent* localhost;
	char		*addr;


        ipa.sin_family = AF_INET;
        ipa.sin_port = htons(port);

        localhost = gethostbyname(hostname);
        if(!localhost){
                printf("resolving localhost");
                return ipa;
        }

        addr = localhost->h_addr_list[0];
        memcpy(&ipa.sin_addr.s_addr, addr, sizeof addr);

        return ipa;
}
/*
 * This function is called in order to carry out the processing of
 * the data for a particular client. Linux implementation is a fork call.
 */
void
mux_client_thread(LPVOID lpParam)
{
	int	                             i = 0, j = 0;
	int	                            data_to_send = 1, ret = 0;
    char	                        write_buf[1000];
	int	                            index_to_send = 0;
	int	                            new_data_index = 0;
    struct  fd_set            masterSockets;        // The fs_set structures are meant for polling.
    struct  fd_set            readSockets;
    struct  fd_set            writeSockets;
    struct   fd_set           errorSockets;
    struct   timeval         timeout;
    SOCKET                     newfd;
    int                               error;
    int                               write_done;
    int	                            total = 0;
    int                               on;
    BOOL                          writeResult;
    HANDLE                      writefd;
    
    static struct   pollfd		mux_pollfd[1];

    FD_ZERO(&masterSockets);          //Initiazes the set NULL set;
    FD_ZERO(&readSockets);               //Initiazes the set NULL set;
    FD_ZERO(&writeSockets);              //Initiazes the set NULL set;
    FD_ZERO(&errorSockets);               //Initiazes the set NULL set;
   
    newfd = ((struct retryParams*)lpParam)->newfd;
    wfd = ((struct retryParams*)lpParam)->wfd;
    FD_SET(newfd, &masterSockets);

    /*
     *Child process. It is a thread in windows.
    */
    fprintf(stderr, "New connection from server mxt \n");
    fflush(stderr);
    
    ret =  setsockopt(newfd, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
	if (ret != 0) {
		printf("Unable to set SO_REUSEADDR \n");
		closesocket(newfd);
		exit(0);
	}
    
    mux_pollfd[0].fd = newfd;
	mux_pollfd[0].events = 0;
	mux_pollfd[0].revents = 0;

	for (;;) {
        memcpy(&readSockets, &masterSockets, sizeof(masterSockets));
        memset(write_buf, '\0', sizeof(write_buf));
        ret = select(0, &readSockets, &writeSockets,&errorSockets, &timeout);
        if(ret  = 0) {
            error = WSAGetLastError();
            fprintf(stderr, "poll: %d\n", error);
            continue;
        }
        fprintf(stderr, "Receiving data: expected: %d\n", sizeof(write_buf));
        ret = recv(newfd, write_buf, sizeof(write_buf), 0);
        if (ret < 0) {
            fprintf(stderr, "Data receive failed : %d \n", ret);
            return;
        }
        if(ret == 0) {
            fprintf(stderr, " The socket %d closed gracefully\n", newfd);
            return;
        } else
        {
            fprintf(stderr, "Recive done on fd %d: len = %d\n", newfd, ret); 
            write_done = 1;
        }			 
        if(wfd){ 
            writeResult = WriteFile(wfd, (void*)write_buf, sizeof(write_buf), &ret, NULL);
       
            if(!writeResult) {
                fprintf(stderr, "Write failed %d\n", GetLastError());
            }
            if(writeResult && ret == 0) {
                fprintf(stderr, "Reached end of file\n");
            }
        }
        /*
         nbytes = write(wfd,   write_buf,   ret);
         */
		printf("Data written : %d \n", ret);
		total += ret;
		printf("Total : %d \n", total);
    }
}

int
main(argc, argv)
	int argc;
	char **argv;

{
    struct protoent*       tcp1;
    SOCKET                     listenfd, newfd;
    int                               ret;
	struct sockaddr_in	portal1;
	char	                        c;
	SOCKET      	            rfd = 0, wfd = 0;
	char	                        *wfilename;
	int	                            nbytes = 0, read_done = 0, write_done = 0;
	int	                                    total = 0;
	struct mux_init_config	conf;
    int                                       on;
    HANDLE                             *pmux_lib_init(struct mux_init_config *);
    PMUX_LIB_INIT                 pfnMux_lib_init;
    PMUX_ACCEPT                  pfnMux_accept;
    //PEER_IDENTITY                pfnpeerfn;
    //struct mux_peer_identity *ptr;
    BOOL                                  fFreeResult, fRunTimeLinkSuccess = FALSE; 
    struct  retryParams         retryArgs;
    HANDLE                             retryThread;
    DWORD                               ThreadID1;
	char	                                *target_host;
    int	                                    connection_port;
	DWORD	                            error;
	
	struct mux_auth		        auth;
    struct mux_handshake_frame	handshake;
    int                                       i;

    fprintf(stderr, "Serv_test_file.exe : ENRTY\n");
	if (argc < 3) {
		fprintf(stderr, "Usage : ./serv_test -s <server port>/-c <client port> -h <target host>\n");
		fprintf(stderr, " argc : %d \n", argc);
		exit(0);
	}
    while ((c = getopt(argc, argv, "w:s:c:h:p:")) != EOF) {
		switch (c) {
		case 'w':
			wfilename = optarg;
            
            wfd = CreateFile(wfilename, GENERIC_WRITE | GENERIC_READ,
                            0 , NULL,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
        
        if(wfd == INVALID_HANDLE) {
            fprintf(stderr, "File Open Failed %s, %d\n", wfilename, GetLastError());
        }
		break;

        case 'p':
            connection_port = atoi(optarg);
            break;

		case 'h':
			target_host = optarg;
			break;

		default:
            /*
             * server_port is the port mxt in  doms connect, listen and accept functions.
             */
			fprintf(stderr, "Usage : ./serv_test -p <server port> -h <target host>");
			exit(0);

		}
	}
    fprintf(stderr, "KM\n");
    if(error = init_wsa()){
        printf("Socket Initialization Failed\n");
        exit(-1); ;
    }
    tcp1 = getprotobyname("tcp");

	listenfd = socket(PF_INET, SOCK_STREAM, tcp1->p_proto);
	if (listenfd == -1) {
        printf("Error createing a tcp socket for listening");
        exit(0);
	}
   ret =  setsockopt(listenfd, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
	if (ret != 0) {
		printf("Unable to set SO_REUSEADDR \n");
		closesocket(listenfd);
		exit(0);
	}

	portal1	 = mux_getipa("localhost", XPRTLD_WELL_KNOWN_PORT);

	if(bind(listenfd, (struct sockaddr*)&portal1, sizeof(portal1)) == -1){
        ret = WSAGetLastError();
		printf("Failed binding socket to IP address ERROR %d\n", ret);
		printf("Listener thread is exiting");
		return -1;
	}

	strncpy(conf.mux_target_host, target_host, 32);
    conf.mux_connection_port = connection_port;
    
    hinstLib = LoadLibrary("vxtmsg_mux.dll");
    if (hinstLib == NULL) 
    { 
        error = GetLastError();
        fprintf(stderr, "vxtmsg_mux Failed :ERROR =%d\n", error);
        exit(0);
    }
   pfnMux_lib_init = (PMUX_LIB_INIT)GetProcAddress(hinstLib, "mux_lib_init"); 
 
   // If the function address is valid, call the function.
   if (NULL == pfnMux_lib_init) {
       fprintf(stderr, "Failed to get mux_lib_init Function Pointer\n");
       fFreeResult = FreeLibrary(hinstLib); 
	    exit(1);
   }
   fRunTimeLinkSuccess = TRUE;
   pfnMux_accept = (PMUX_ACCEPT)GetProcAddress(hinstLib, "mux_accept"); 
   if(pfnMux_accept == NULL) {
       FreeLibrary(hinstLib); 
       fprintf(stderr, "Getting accept Function Pointer Falied\n");
       exit(0);
   }

   //pfnpeerfn =(PEER_IDENTITY)GetProcAddress(hinstLib, "mux_get_peer_id");
   /*
   if(pfnpeerfn == NULL) {
       fprintf(stderr, "Getting Peer indentity fn pointer failed\n");
       fflush(stderr);
       exit(0);
   }
   */
   fprintf(stderr, "calling mxt initialization\n");
   ret = pfnMux_lib_init(&conf);
   if (ret) {
		fprintf(stderr, "Unable to init Mux library\n");
        fFreeResult = FreeLibrary(hinstLib); 
		exit(0);
	}
   fprintf(stderr, "serv_test_file: mux_lib_int: SUCCESS\n");
    fflush(stderr);
	for (;;) {
		if (listen(listenfd, 255) == -1) {
                        printf("Failed setting socket to listen");
                        return;
                }
		newfd = accept(listenfd, NULL, NULL);
		if (newfd == INVALID_SOCKET) {
            printf("Accept failed\n");
            continue;
        }

        fprintf(stderr, "serv_test_file: calling mux_accept\n");
        fflush(stderr);
        ret = pfnMux_accept(newfd, &handshake, NULL);
        if (ret) {
			fprintf(stderr, "ACCEPTUnable to init Mux library\n");
			closesocket(newfd);
			continue;
		}
        /*
         * This functions is not available with latestr mxt.
         */
        /*
        ptr = pfnpeerfn(newfd);
        
        if(ptr) {
            fprintf(stderr, "Peer fd : %d \n", ptr->mux_peer_fd);
            fprintf(stderr, "Peer UUID:\n");
            fflush(stderr);
            for (i = 0; i < MUX_UUID_SIZE; i++) {
                fprintf(stderr, "%02x",(unsigned char)ptr->mux_peer_uuid[i]);
            }
			fprintf(stderr, "\n");
			free(ptr);
		}
        
        fprintf(stderr, "serv_test_file: peerfn called\n");
        fflush(stdout);
        */

        error = 0;
        retryArgs.listenfd = listenfd;
        retryArgs.wfd = wfd;

        retryArgs.newfd = newfd;
        printf("Creating mux_client_thread\n");
        //
        // This thread is created and runs inmmediately.
        // dwCreationflags:  CREATE_SUSPENDED.
        // To start the newly created thread, call ResumeThread.
        //
        retryThread = CreateThread(NULL,            //default security attributes
                                0,                                                 //default stack size
                                (LPTHREAD_START_ROUTINE)&mux_client_thread,
                                (LPVOID)&retryArgs,           //no thread function arguments
                                0,                                             //default creation flags
                                &ThreadID1);                        //received thread Id
        if(retryThread == NULL) {
            error = GetLastError();
            printf("retrythread creation failed: error = %d\n", error);
            exit(-1);
        }
	}
}

