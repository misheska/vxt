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



#include <sys/types.h>
#ifndef _WINNT
#include <linux/shm.h>
#include <fcntl.h>
#include <poll.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <aclapi.h>

#include <string.h>
#include "mxt_platform.h"
#include <mxt_common.h>
#include "mxt_lib.h"

#define SHMSZ		130
#define KEY		    1234
#define COOKIESZ	128
#define POLL_TIMEOUT	100

#define BUF_SIZE 128

char            *sharedmemoryName = "Global\\mxt_shared_memory";
extern char*    mux_get_log_entry();

#define MAX_RETRY_COUNT	20

PSECURITY_DESCRIPTOR        pSD = NULL;
PSID                        pEveryoneSID = NULL, pAdminSID = NULL;
SID_IDENTIFIER_AUTHORITY    SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
SID_IDENTIFIER_AUTHORITY    SIDAuthNT = SECURITY_NT_AUTHORITY;
SECURITY_ATTRIBUTES         sa;
PACL                        pACL = NULL;
EXPLICIT_ACCESS             ea[1];

int
mux_prepare_security()
{
    DWORD dwRes, dwDisposition;
    int static  security_initliazed = 0;

    MUX_LOG_ENTRY0("Entering mux_prepare_security");
    if(security_initliazed) return 0;
    security_initliazed = 1;

    // Initialize an EXPLICIT_ACCESS structure for an ACE.
    // The ACE will allow Everyone read access to the key.
    //ZeroMemory(&ea, 2 * sizeof(EXPLICIT_ACCESS));

    ZeroMemory(&ea, 1 * sizeof(EXPLICIT_ACCESS));
    
    // Create a SID for the BUILTIN\Administrators group.

    if(! AllocateAndInitializeSid(&SIDAuthNT, 2,
                     SECURITY_BUILTIN_DOMAIN_RID,
                     DOMAIN_ALIAS_RID_ADMINS,
                     0, 0, 0, 0, 0, 0,
                     &pAdminSID)) 
    {
        MUX_LOG_ENTRY1("Entering mux_prepare_security: AllocateAndInitializeSid Error %u", 
                        GetLastError());
        goto Cleanup; 
    }

    // Initialize an EXPLICIT_ACCESS structure for an ACE.
    // The ACE will allow the Administrators group full access to
    // the key.

    ea[0].grfAccessPermissions = KEY_ALL_ACCESS;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance= NO_INHERITANCE;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    ea[0].Trustee.ptstrName  = (LPTSTR) pAdminSID;
    
    // Create a new ACL that contains the new ACEs.

    dwRes = SetEntriesInAcl(1, ea, NULL, &pACL);

    if (ERROR_SUCCESS != dwRes) 
    {
        MUX_LOG_ENTRY1("Entering mux_prepare_security: SetEntriesInAcl Error %u",
                        GetLastError());
        goto Cleanup;
    }

    // Initialize a security descriptor. 

    pSD = (PSECURITY_DESCRIPTOR) LocalAlloc(LPTR, 
                             SECURITY_DESCRIPTOR_MIN_LENGTH); 
    if (NULL == pSD) 
    { 
        MUX_LOG_ENTRY1("Entering mux_prepare_security: LocalAlloc Error %u",
                        GetLastError());
        goto Cleanup; 
    } 
    if (!InitializeSecurityDescriptor(pSD,
            SECURITY_DESCRIPTOR_REVISION)) 
    {  
        MUX_LOG_ENTRY1("Entering mux_prepare_security: nitializeSecurityDescripton Error %u",
                        GetLastError());
        goto Cleanup; 
    } 
 
    // Add the ACL to the security descriptor. 

    if (!SetSecurityDescriptorDacl(pSD, 
            TRUE,     // bDaclPresent flag   
            pACL, 
            FALSE))   // not a default DACL 
    {  
        MUX_LOG_ENTRY1("Entering mux_prepare_security: setSecurityDescriptorDacl Error %u",
                        GetLastError());
        goto Cleanup; 
    } 

    // Initialize a security attributes structure.

    sa.nLength = sizeof (SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;
    return 0;
Cleanup:
    if (pEveryoneSID) 
        FreeSid(pEveryoneSID);
    if (pAdminSID) 
        FreeSid(pAdminSID);
    if (pACL) 
        LocalFree(pACL);
    if (pSD) 
        LocalFree(pSD);
    return -1;
}

int
mux_initiate_authorization(sendfd, auth)
	SOCKET		sendfd;
	struct mux_auth	*auth;
{
	int	                    i = 0;
	int	                    num, index, remaining;
	int	                    fd, ret;
    HANDLE             hMapFile;
    char                    *pBuf, *pBuf1;
    int                       randNumber;
    struct  fd_set    readSockets;
    struct  timeval   timeout;
    int	                buf[BUF_SIZE/4];
    struct pollfd pollfd[1];
	int	poll_timeout = POLL_TIMEOUT * 100;
	int	retry_count = 0;


	/*
	 * A shared memeory segmement of name mxt_shared_memory is
     * created for authorization of 
	 * 
	 */

    MUX_LOG_ENTRY0("Entering Authorization");
    //return 0;
    ret = mux_prepare_security();
    if(ret) {
        MUX_LOG_ENTRY0("Entering Authorization: Prpare Security Failed");
        return -1;
    }
    hMapFile = CreateFileMapping(
                 INVALID_HANDLE_VALUE,    // use paging file
                 &sa,                    // default security 
                 PAGE_READWRITE,          // read/write access
                 0,                       // max. object size 
                 BUF_SIZE,                // buffer size  
                 sharedmemoryName);                 // name of mapping object
 
   if (hMapFile == NULL) 
   { 
      MUX_LOG_ENTRY1("Could not create file mapping object (%d).", 
             GetLastError());
      return  -1;
   }
   MUX_LOG_ENTRY1(" Create File Mapping Succesful: %s", sharedmemoryName);
   pBuf = (char*) MapViewOfFile(hMapFile,   // handle to map object
                        FILE_MAP_ALL_ACCESS, // read/write permission
                        0,                   
                        0,                   
                        BUF_SIZE);           
 
   
   if (pBuf == NULL) 
   { 
      MUX_LOG_ENTRY1("Could not map view of file (%d).", 
             GetLastError()); 
      CloseHandle(hMapFile);
      return -1;
   }
   /*
    * Write 64 random number to the shared memory.
    */

   pBuf1  = (char*)buf;
   
   for(i = 0;  i < 32;  i++) {
        randNumber = rand();
        buf[i] = randNumber;
   }
	
   /*
   * Copy the random data generated to the mapped memory.
   */

   memcpy(pBuf, pBuf1, BUF_SIZE);
   memset(auth, '\0', sizeof(struct mux_auth));
   auth->mux_auth_fd = sendfd;
   auth->mux_auth_magic = READ_MAGIC;
retry1:
    MUX_LOG_ENTRY2("Author:Send :fd %d, size = %d", sendfd, sizeof(struct mux_auth));
   ret = send(sendfd, auth, sizeof(struct mux_auth), 0);
   if(ret == SOCKET_ERROR){
       	MUX_LOG_ENTRY0("Send failed ");
        UnmapViewOfFile(pBuf);
        CloseHandle(hMapFile);
		return -1;
   }
   MUX_LOG_ENTRY1("Auth: Send Sucessfile %d", ret);
   RETRYABLE_ERROR(ret)
   if (ret == 1) {
		SLEEP(1);
		goto retry1;
   } 
   FD_ZERO(&readSockets);                           //Initiazes the set NULL set;
   FD_SET(sendfd, &readSockets);
   timeout.tv_sec = 0;
   timeout.tv_usec = POLL_TIMEOUT * 100 * 1000;
retry:
   ret = select(0, &readSockets, NULL, NULL, &timeout);
   if (ret <= 0) {
       MUX_LOG_ENTRY0("mux_initiate_authorization poll returned");
       MUX_LOG_ENTRY0("poll returned ");
       if (++retry_count < MAX_RETRY_COUNT) {
           goto retry;
       } else {
           UnmapViewOfFile(pBuf);
           CloseHandle(hMapFile);
           MUX_LOG_ENTRY0("Authorization failed ");
           return -1;
		}
	}

   if(ret  == 0) {
       ret = WSAGetLastError();
       MUX_LOG_ENTRY1("mux_initiate_authorization_nt:poll Fail: %d", ret);
       UnmapViewOfFile(pBuf);
       CloseHandle(hMapFile);
        return -1;
    }
   MUX_LOG_ENTRY0("Auth: Waiting for socket Recv");
   memset(buf, '\0', sizeof(buf));
   ret = recv(sendfd,  buf,  sizeof(buf), 0);
   if (ret == SOCKET_ERROR) {
      MUX_LOG_ENTRY1("mux_initiate_authorization_nt:recv failed %d ", WSAGetLastError());
      UnmapViewOfFile(pBuf);
      CloseHandle(hMapFile);
       return -1;
   } else {
       MUX_LOG_ENTRY2("Auth: Receive: Sendfd %d, Received =%d", sendfd, ret);
       if (memcmp(pBuf, buf, BUF_SIZE) == 0) {
           MUX_LOG_ENTRY0("Authorization completed Successfully");
           UnmapViewOfFile(pBuf);
           CloseHandle(hMapFile);
           return 0;
       }
	}
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
	return -1;
}

int
mux_respond_to_authorization(recvfd, auth)
	SOCKET	recvfd;
	struct mux_auth	*auth;
{
    char	 data[] = "READDATA";
	int	                    ret;
	int	                    i = 0;
	char	                recvbuf[20];
	struct	mux_auth	*recv_auth;
    struct  fd_set    readSockets;
    struct  timeval  timeout;
    int	                    buf[BUF_SIZE/4];
     HANDLE             hMapFile;
    char                    *pBuf, *pBuf1;
    int                 retry_count = 0;

    MUX_LOG_ENTRY0("Enter mux_respond_to_authorization");
    //return 0;
	
    FD_ZERO(&readSockets);                           //Initiazes the set NULL set;
    FD_SET(recvfd, &readSockets);
    timeout.tv_sec = 0;
    timeout.tv_usec = POLL_TIMEOUT * 100 * 1000;
retry:
    ret = select(0, &readSockets, NULL, NULL, &timeout);
    if(ret  == SOCKET_ERROR) {
        ret = WSAGetLastError();
        MUX_LOG_ENTRY0("mux_respond_to_authorization poll returned");
		MUX_LOG_ENTRY0("poll returned ");
		if (++retry_count < MAX_RETRY_COUNT) {
			goto retry;
		} else {
			MUX_LOG_ENTRY0("Authorization failed ");
		    return -1;
		}
	}
    ret = recv(recvfd, recvbuf, sizeof(struct mux_auth), 0);
    if (ret == SOCKET_ERROR) {
        MUX_LOG_ENTRY1("mux_respond_to_authorization:recv failed %d", WSAGetLastError());
        return -1;
    } else {
			recv_auth = (struct mux_auth *)recvbuf;
			if (recv_auth->mux_auth_magic == READ_MAGIC) {
                MUX_LOG_ENTRY0("mux_respond_to_authorization:Received msg READDATA");
			} else {
				return -1;
			}
	}
    MUX_LOG_ENTRY0("Auth:Response Enter");
    hMapFile = OpenFileMapping(
                               FILE_MAP_ALL_ACCESS,   // read/write access
                               FALSE,                 // do not inherit the name
                               sharedmemoryName);                      
   if (hMapFile == NULL) 
   { 
      MUX_LOG_ENTRY1("mux_respond_to_authorizationCould not create file mapping object (%d).", 
             GetLastError());
      return  -1;
   }

   pBuf = (char*) MapViewOfFile(hMapFile,   // handle to map object
                        FILE_MAP_ALL_ACCESS, // read/write permission
                        0,                   
                        0,                   
                        BUF_SIZE);           
 
   if (pBuf == NULL) 
   { 
       MUX_LOG_ENTRY1("mux_respond_to_authorization:Could not map view of file (%d).", 
             GetLastError());
       CloseHandle(hMapFile);
       return -1;
   }
   MUX_LOG_ENTRY1("Auth: Response Sending Size %d", BUF_SIZE);
	ret = send(recvfd, pBuf, BUF_SIZE, 0);
	if (ret <= 0) {
        ret = WSAGetLastError();
        UnmapViewOfFile(pBuf);
        CloseHandle(hMapFile);
        MUX_LOG_ENTRY1("Send failed: Error %d ", ret);
		return -1;
	}
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
    MUX_LOG_ENTRY1("Auth. Resp: Sucess: Send bytes %d", ret);
	*auth = *recv_auth;
    return 0;
}

