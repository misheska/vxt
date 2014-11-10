/* $Id: mxt_lib.h,v 1.15 2009/02/04 00:28:59 surkude Exp $ */
/* #ident "$Source: /project/vxtcom/lib/vxtmsg_mux/common/mxt_lib.h,v $" */

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

#ifndef SOCKET
#define SOCKET int
#endif

#ifndef STDCALL
#define STDCALL
#endif

/*
 * Size of uuid in bytes
 */
#ifndef MUX_UUID_SIZE
#define MUX_UUID_SIZE   16
#endif

/*
 * Following well known port will be used in order to
 * connect to the local Xprtld (server). This port needs
 * to be updated after the Xprtld consultation.
 */
#define XPRTLD_WELL_KNOWN_PORT   5634

/*
 * Following well known port will be use to connect to the
 * local MXT server
 */
#define MXTD_WELL_KNOWN_PORT	5070

/*
 * mux_handshake_frame:
 *
 * This is the first message sent by the mxt to xprtld after the connection
 * is established. The magic would contain XPRTLD_MAGIC (printed below).
 */
struct mux_handshake_frame {
	int	mux_handshake_magic;
	int	mux_handshake_frame_len;
	int	mux_handshake_flags;
	char	mux_handshake_src_uuid[MUX_UUID_SIZE];
	char	mux_handshake_src_endpoint[MUX_UUID_SIZE];
	char	mux_handshake_dest_uuid[MUX_UUID_SIZE];
	char	mux_handshake_dest_endpoint[MUX_UUID_SIZE];
};

/*
 * This magic is used in mux_connect() to connect to the local Xprtld server 
 */
#define XPRTLD_MAGIC (('X' << 24) | \
		      ('P' << 16) | \
		      ('R' <<  8) | \
		      ('T'))
/*
 * This magic is used in mux_connect() to connect to the local Mxtd server 
 */

#define MXT_MAGIC (('M' << 24) | \
		   ('X' << 16) | \
		   ('T' <<  8) | \
		   ('D'))

/*
 * mux_get_my_uuid():
 *
 * Returns a character string containing the uuid of the domain. The return
 * string contains the uuid in the ASCII form.
 */
char * mux_get_my_uuid();

/*
 * mux_convert_stduuid_to_byteuuid:
 *
 * Converts the ASCII uuid in the binary form
 *
 * Input args:
 *	- Pointer to uuid string in ascii form
 *
 * Return value:
 *	- Pointer to uuid string in binary form. This string is malloc'ed
 *	  inside this function. So, it MUST be freed after use.
 */
char * mux_convert_stduuid_to_byteuuid(char *);

/*
 * mux_connect: Connects to the local MXT server.
 *
 * Input args:
 *	1) Mxt server magic (MXT_MAGIC)
 *	2) Pointer to destination uuid in binary form
 *	3) NULL (used for internal purpose)
 *	4) If a pointer to an integer is passed, the error code is returned
 *
 * Return value (positive value):
 * 	- Connect descriptor
 *
 * Following are the possible error values in case of return:
 * MUX_CONNECT_GENERIC_ERROR : Generic error
 * MUX_CONNECT_LOCAL_CONNECT : Connection to local xprtld
 * MUX_CONNECT_INVALID_SERVICE : Connection requested to invalid service
 * MUX_CONNECT_REMOTE_CONNECTION_FAILED : Connection to Xprtld on the remote
 *					  failed
 * MUX_CONNECT_NON_EXISTENT_GUEST : Guest with this UUID is not connected
 * MUX_CONNECT_NO_VXT_CONNECTION : There's no vxt connection with any guest
 * MUX_CONNECT_LOCAL_AUTH_FAILURE : Authorization with local MXT failed
 * MUX_CONNECT_REMOTE_AUTH_FAILURE : Authorization with remote Xprtld failed
 * MUX_CONNECT_REQUEST_TIMEOUT : Remote connection request timed out
 */
#define MUX_CONNECT_SUCCESS			0
#define MUX_CONNECT_GENERIC_ERROR		-1
#define MUX_CONNECT_LOCAL_CONNECT		-2
#define MUX_CONNECT_INVALID_SERVICE		-3
#define MUX_CONNECT_REMOTE_CONNECTION_FAILED	-4
#define MUX_CONNECT_NON_EXISTENT_GUEST		-5
#define MUX_CONNECT_NO_VXT_CONNECTION		-6
#define MUX_CONNECT_LOCAL_AUTH_FAILURE		-7
#define MUX_CONNECT_REMOTE_AUTH_FAILURE		-8
#define MUX_CONNECT_REQUEST_TIMEOUT		-9
#define MUX_MAX_ERROR_CODE			-10

SOCKET STDCALL mux_connect(int, void *, void *, int *);

/*
 * mux_read:

 * Reads the data from the connection
 *
 * Input args:
 * 	1) Read socket descriptor
 *	2) Buffer pointer containing the data
 *	3) Length of the data
 *	4) Receive flags (Default : 0)
 *
 * Return value:
 *	- Number of successful bytes received
 */
int STDCALL mux_read(SOCKET, char *, int, int);

/*
 * mux_write:
 *
 * Writes the data to the connection
 *
 * Input args:
 * 	1) Send socket descriptor
 *	2) Buffer pointer containing the data
 *	3) Length of the data
 *	4) Send flags (Default : 0)
 *
 * Return value:
 *	- Number of successful bytes sent
 */
int STDCALL mux_write(SOCKET, char *, int, int);


/*
 * mux_accept:
 * Identity of the peer is verified in this function.
 *
 * Input args:
 *	1) Socket descriptor on which the connection needs to be accepted
 *	2) A valid pointer to struct mux_handshake_frame
 *	2) NULL (This is used for internal purpose, please pass this as NULL)
 *
 * Return value:
 *	- 0 SUCCESS
 *
 * Return values in case of error:
 * MUX_ACCEPT_GENERIC_ERROR: Not a MXT connection
 * MUX_ACCEPT_AUTHORIZATION_FAILED: Authorization failed
 * MUX_ACCEPT_MAGIC_FAILURE: Invalid magic received
 */
#define MUX_ACCEPT_GENERIC_ERROR		-1
#define MUX_ACCEPT_AUTHORIZATION_FAILED		-2
#define MUX_ACCEPT_MAGIC_FAILURE		-3

 int STDCALL mux_accept(SOCKET, struct mux_handshake_frame *, void *);


/*
 * TEMPORARY. This structure needs to be populated for the networking
 * endpoint in order to let the MXT know the Dom0's address and the listener
 * port for the server MXT. This will not be needed once the VXT endpoints
 * are used.
 */
struct mux_init_config {
	char	mux_target_host[32];
	int	mux_connection_port;
};

/*
 * mux_lib_init:
 *
 * Starts all the threads required for for MXT communication
 */
int STDCALL mux_lib_init(struct mux_init_config *);

/*
 * This functions is to do clanup the for Windows platform. This  
 * cleanup any handles/memory that might be opened/allocated by
 * WSAStartup() in windows. This will be noop for Linux.
 */

void STDCALL mux_connect_cleanup();

/*
 * This function needs to be called by the process that loads the mxt library
 * in order to have the library print out debug log in the trace file.
 *
 * Args: File pointer to the trace file
 */
void STDCALL mux_enable_tracing(FILE *);
