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


//#include <winsock2.h>

#include <mxt_platform.h>
#include <mxt_common.h>
#ifndef _WINNT
#include "poll.h"
#endif

int mux_process_vxt_comm(SOCKET, int*);
int mux_process_local_comm(SOCKET);
extern void mux_close_vxt_sockets();
extern void mux_remove_from_threadlist();
extern THREAD_T processor3, processor1;
extern int poll_threads_exit;
extern void mux_remove_peer_comm_mapping(SOCKET);
extern char* mux_get_log_entry();
extern struct mux_peer_comm_mapping * mux_get_peer_comm_entry(SOCKET);
extern void mux_remove_fd_from_poll(SOCKET, int);

static PXS_DOMAIN_OPEN             fn_xs_domain_open = NULL;
static PXS_DOMAIN_CLOSE            fn_xs_domain_close = NULL;
static PXS_TRANSACTION_START       fn_xs_transaction_start = NULL;
static PXS_READ                    fn_xs_read = NULL;

extern WCHAR mxtport[MAX_PATH];

#define POLL_TIMEOUT		1000
int init_wsa( void )
{
    WORD        wVersionRequested;
    WSADATA     wsaData;
    int         err;

    wVersionRequested = MAKEWORD( 2, 2 );

    err = WSAStartup( wVersionRequested, &wsaData );
    if ( err != 0 )
    {
        err = WSAGetLastError();
        MUX_LOG_ENTRY1("init_was Failed : Error = ", err);
        return 1;
    }
    return 0;   //Success
}

int
mux_sock_blocking(socketfd)
	SOCKET  socketfd;
{
    DWORD           error;
    u_long            iMode = 0;

    error = ioctlsocket(socketfd, FIONBIO, &iMode);
    if(error) {
        error = WSAGetLastError();
        MUX_LOG_ENTRY1("mux_sock_nonblock:ioctlsocker()Failed = %d", error);
        return -1;
    }
	return 0;
};


void
mux_sock_nonblock(socketfd)
	SOCKET  socketfd;
{
	DWORD           error;
    u_long            iMode = 1;

    error = ioctlsocket(socketfd, FIONBIO, &iMode);
    if(error) {
        error = WSAGetLastError();
        MUX_LOG_ENTRY1("mux_sock_nonblock:ioctlsocker()Failed = %d", error);
        exit(1) ;
    }
};


pthread_cond_wait(cond_obj, mutex_obj)
    COND_OBJ *  cond_obj;
    MUTEX_OBJ mutex_obj;

{
    DWORD          result;
    int            last_waiter;
    
    cond_obj->waiters_count++; 
    fprintf(stderr, "KKKK Start pthread_cond_wait called %d", cond_obj->waiters_count);
    ReleaseMutex(mutex_obj);  
    result = WaitForSingleObject(cond_obj->events[0], INFINITE);
    if(result == WAIT_OBJECT_0) {
        MUTEX_LOCK(mutex_obj); 
        fprintf(stderr, "Wait for cond_var is over");
        fflush(stderr);
        cond_obj->waiters_count--;
        fflush(stderr);
    } 
}

pthread_cond_signal(cond_obj, mutex_obj)
    COND_OBJ*  cond_obj;
    MUTEX_OBJ    mutex_obj;
{
    int       have_waiters;
    BOOL      ret;

    have_waiters = cond_obj->waiters_count;
    if(have_waiters){
        ret= SetEvent(cond_obj->events[0]);
    }else {
        ret = SetEvent(cond_obj->events[0]);
        if(!ret) {
            MUX_LOG_ENTRY1("Signlling on expectation Fail %d", GetLastError());
        } else
        {
            MUX_LOG_ENTRY1("MMMMM Signlling on expectation %d", ret);
        }
    }
}

void
transport_poll_local(mux_local_pollfd, mux_local_pollfd_size, t)
	struct pollfd		*mux_local_pollfd;
	int			        mux_local_pollfd_size;
	int			        t;
{
	int	                ret;
	int	                i;
	SOCKET	eventfd;
    struct   fd_set     readSockets;
    struct   fd_set     writeSockets;
    struct   fd_set     errorSockets;
    struct   timeval    timeout;

    FD_ZERO(&readSockets);
    FD_ZERO(&writeSockets);
    FD_ZERO(&errorSockets);
    timeout.tv_sec = 1;
    timeout.tv_usec = POLL_TIMEOUT;
    //MUX_LOG_ENTRY0("Inside transport_poll_local");
    
        
    if(mux_local_pollfd_size < 1) {
        Sleep(200);
        return;
    }

    for(i=0;  i < mux_local_pollfd_size;  i++) {
       FD_SET(mux_local_pollfd[i].fd, &readSockets);
    }
   
    timeout.tv_sec = 1;
    timeout.tv_usec = POLL_TIMEOUT;
    ret = select(0, &readSockets, &writeSockets,&errorSockets, &timeout);
    /*
    if(ret  == 0) {
        ret = WSAGetLastError();
        SLEEP(1);
        MUX_LOG_ENTRY1("poll: %d", ret);
        return;
    }
    */
    /*
    if(ret == SOCKET_ERROR) {
        MUX_LOG_ENTRY1("poll: %d",WSAGetLastError());
        SLEEP(1);
        return;
    }
    */
    /*
    if(mux_local_pollfd_size < 1) {
        return;
    }
    */
	for (i = 0; i < mux_local_pollfd_size; i++) {
        eventfd = mux_local_pollfd[i].fd;
        if(FD_ISSET(eventfd , &readSockets)){
            if(mux_process_local_comm(eventfd) == -5){
                mux_local_pollfd[i].fd = 0;
            }
			mux_local_pollfd[i].revents = 0;
		}
	}
	return;
};

void
transport_poll(mux_vxt_pollfd, mux_vxt_pollfd_size, t)
	VXT_POLLFD_STRUCT	*mux_vxt_pollfd;
	int			        mux_vxt_pollfd_size;
	int			        t;
{
	int	                ret;
	int	                i;
	SOCKET	            eventfd;
    int                 returnval = 0;
#ifdef VXT_ENDPOINT
	struct mux_peer_comm_mapping    *entry;
#endif


#ifdef NETWORK_ENDPOINT
    struct   fd_set      readSockets;
    struct   fd_set      writeSockets;
    struct   fd_set      errorSockets;
    struct   timeval    timeout;

    FD_ZERO(&readSockets);
    FD_ZERO(&writeSockets);
    FD_ZERO(&errorSockets);
    if(!mux_vxt_pollfd_size) return;
    for(i=0;  i <mux_vxt_pollfd_size;  i++) {
       FD_SET(mux_vxt_pollfd[i].fd, &readSockets);
    }
   
    timeout.tv_sec = 0;
    timeout.tv_usec = POLL_TIMEOUT;
    ret = select(0, &readSockets, &writeSockets,&errorSockets, &timeout);
    if(ret  = 0) {
        ret = WSAGetLastError();
        MUX_LOG_ENTRY1("poll: %d", ret);
        return;
    }

	for (i = 0;  i < mux_vxt_pollfd_size;  i++) {
        eventfd = mux_vxt_pollfd[i].fd;
        if(FD_ISSET(mux_vxt_pollfd[i].FD_OBJ , &readSockets)){
            mux_process_vxt_comm(eventfd, &returnval);
			mux_vxt_pollfd[i].revents = 0;
		}
	}
    return;
#else
    
    if(mux_vxt_pollfd_size < 1) {
        //MUX_LOG_ENTRY0("VXT_POLL: Begin");
        Sleep(1000);
        //MUX_LOG_ENTRY0("VXT_POLL: End");
        return ;
    }
    ret = vxt_poll(mux_vxt_pollfd, mux_vxt_pollfd_size, 1000 * 120);
    
	if (ret <= 0) {
        SLEEP(1);
		return;
	}
	for (i = 0; i < mux_vxt_pollfd_size; i++) {
		if (mux_vxt_pollfd[i].revents != 0) {
			eventfd = (SOCKET)mux_vxt_pollfd[i].FD_OBJ;
            if (mux_vxt_pollfd[i].revents & VXTSIG_HUP) {
				MUX_LOG_ENTRY1("transport_poll Received %x",
					mux_vxt_pollfd[i].revents);
				mux_remove_peer_comm_mapping(eventfd);
				SLEEP(1);
				continue;
			}
            returnval = 1;
            while (returnval == 1) {
				returnval = 0;
			    mux_process_vxt_comm(eventfd, &returnval);
            }
			mux_vxt_pollfd[i].revents = 0;
		}
	}
	return;
#endif
};

int
is_running_in_dom0()
{
	int	fd;

    return 0;
    /*
     * We need to work on this. Right now Windows platfor is supposed to run only in
     * DOMU.
     */
    /*
	fd = open("/etc/xensource-inventory", O_RDONLY);
	if (fd > 0) {
		close(fd);
		return 1;
	} else {
		return 0;
	}
    */
};


void __cdecl mux_signal_handler(signum)
        int     signum;
{
	int		i;

	MUX_LOG_ENTRY1("Received signal %d ", signum);
    poll_threads_exit = 1;
	SLEEP(1);
    
#ifdef VXT_ENDPOINT
	/*
	 * Close all the vxt endpoints
	 */
	MUX_LOG_ENTRY0("Calling vxt socket close");
	mux_close_vxt_sockets();
	MUX_LOG_ENTRY0("finished vxt socket close");

#endif /* VXT_ENDPOINT */
     /*
	  * Kill all the known threads
	  */
	for (i = 0; i < MAX_THREADS; i++) {
		if (mux_threads_array[i]) {
			/*
			THREAD_KILL(mux_threads_array[i]);
			*/
			mux_remove_from_threadlist(mux_threads_array[i]);
		}
	}
    exit(0);
    //raise(SIGKILL);

}

mux_set_sig_handler()
{
    typedef void(*SignalHandlerPointer)(int);
    SignalHandlerPointer previousSignalHandler;

    previousSignalHandler = (void*)signal(SIGINT, mux_signal_handler);
    if(previousSignalHandler == (void*)SIG_IGN) {
        signal(SIGINT, SIG_IGN);
    }
    /*
    previousSignalHandler = (void*)signal(SIGKILL, mux_signal_handler);
    if(previousSignalHandler == (void*)SIG_IGN) {
        signal(SIGINT, SIG_IGN);
    }
    */
    previousSignalHandler = (void*)signal(SIGABRT, mux_signal_handler);
    if(previousSignalHandler == (void*)SIG_IGN) {
        signal(SIGINT, SIG_IGN);
    }
    previousSignalHandler = (void*)signal(SIGTERM, mux_signal_handler);
    if(previousSignalHandler == (void*)SIG_IGN) {
        signal(SIGINT, SIG_IGN);
    }
    MUX_LOG_ENTRY0("Setting Signal Handler ");
    return;
}

/*
 * This function is called to load the xs.dll library. vxtmsg_mux.dll
 * Uses xs_read function from this library to read the uuid.
 */

int loadXsLibrary()
{

    LONG	status;

    HKEY        hKey = NULL;
    ULONG       valueType = REG_SZ;
    static WCHAR MsgPath[MAX_PATH];
    DWORD        dwBufSize = MAX_PATH;
    HINSTANCE    hinstLib;
    DWORD	     error = 0;

    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, XENPATH, 0, KEY_READ, &hKey);
    if (status != ERROR_SUCCESS) 
    {
        goto next;
    }

    /*
     * Get the XenTools Install_Dir key.
     */
    status = RegQueryValueEx(hKey, INSTALL_KEY, NULL, &valueType, 
            (PBYTE)MsgPath, &dwBufSize);

    if((status != ERROR_SUCCESS) || (valueType != REG_SZ))
    {   RegCloseKey(hKey);
        goto next;
    }
   
    strcat(MsgPath, "\\xs.dll");
    MUX_LOG_ENTRY1("xs.dll Path = %s", MsgPath);
    hinstLib = LoadLibrary(MsgPath);
    if(hinstLib != NULL) goto get_fuction_pointers;

next:
    error = GetLastError();
    MUX_LOG_ENTRY1("xs.dll Load Failed from %s Failed",MsgPath);
    MUX_LOG_ENTRY0("xs.dll Load Trying from Alternat Directory");

    /*
     * Try alternate pathb which is SOFTWARE\\XenSource.
     */ 

    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, XENPATH_ALT, 0, KEY_READ, &hKey);
    if (status != ERROR_SUCCESS) 
    {
        goto next1;
    }

    /*
     * Get XenTools Install_Dir key.
     */

    status = RegQueryValueEx(hKey, INSTALL_KEY, NULL, &valueType, 
            (PBYTE)MsgPath, &dwBufSize);

    if((status != ERROR_SUCCESS) || (valueType != REG_SZ))
    {
        RegCloseKey(hKey);
        goto next1;
    }
   
    strcat(MsgPath, "\\xs.dll");
    MUX_LOG_ENTRY1("xs.dll Path = %s", MsgPath);
    hinstLib = LoadLibrary(MsgPath);
    if(hinstLib != NULL) goto get_fuction_pointers;

    /*
     * Try to load the library from the current directory.
     */ 
next1:
    error = GetLastError();
    MUX_LOG_ENTRY1("xs.dll Load Failed from %s Alternat Directory",MsgPath);
    MUX_LOG_ENTRY0("xs.dll Load Trying from Local Directory");
    hinstLib = LoadLibrary("xs.dll");
    if (hinstLib == NULL) {
        error = GetLastError();
        MUX_LOG_ENTRY0("xs.dll Load Failed from Local Directory");
        goto try64;
    }

try64:
    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, XENPATH_ALT_64, 0, KEY_READ, &hKey);
    if (status != ERROR_SUCCESS) 
    {
        fprintf(stderr, "KM Reg 64 alt path %s", XENPATH_ALT_64);
        fflush(stderr);
        goto out;
    }

    /*
     * Get the XenTools Install_Dir key.
     */
    status = RegQueryValueEx(hKey, INSTALL_KEY, NULL, &valueType, 
            (PBYTE)MsgPath, &dwBufSize);

    if((status != ERROR_SUCCESS) || (valueType != REG_SZ))
    {   
        fprintf(stderr, "KM alt path %s",INSTALL_KEY );
        fflush(stderr);
        RegCloseKey(hKey);

        goto out;
    }
   
    strcat(MsgPath, "\\xs.dll");
    MUX_LOG_ENTRY1("xs.dll Path = %s", MsgPath);
    hinstLib = LoadLibrary(MsgPath);
    if(hinstLib != NULL) goto get_fuction_pointers;
    goto out;
    /*
     * Get all the required function pointers of xs.dll library.
     */

get_fuction_pointers:
    /*
     * xs_domain_open
     */

    fn_xs_domain_open = (PXS_DOMAIN_OPEN)GetProcAddress(hinstLib, "xs_domain_open");
    if(fn_xs_domain_open == NULL)
    {
        MUX_LOG_ENTRY0("Getting xs_domaon_open function pointer failed");
        goto out;
    }

    /*
     * xs_domain_close
     */

    fn_xs_domain_close = (PXS_DOMAIN_CLOSE)GetProcAddress(hinstLib, "xs_daemon_close");
    if(fn_xs_domain_close == NULL)
    {
        MUX_LOG_ENTRY0("Getting xs_domaon_close function pointer failed");
        goto out;
    }

    /*
     * xs_domain_read
     */

    fn_xs_read = (PXS_READ)GetProcAddress(hinstLib, "xs_read");
    if(fn_xs_read == NULL)
    {
        MUX_LOG_ENTRY0("Getting xs_read function pointer failed");
        goto out;
    }

    /*
     * xs_transaction_start
     */

    fn_xs_transaction_start = (PXS_TRANSACTION_START)GetProcAddress(hinstLib, "xs_domain_open");
    if(fn_xs_transaction_start == NULL)
    {
        MUX_LOG_ENTRY0("Getting xs_transaction_start function pointer failed");
        goto out;
    }
    return 0;
out:
    if (NULL != hKey && INVALID_HANDLE_VALUE != hKey) 
    {
        RegCloseKey(hKey);
        hKey = NULL;
    }
    return -1;
}

char *
mux_convert_stduuid_to_byteuuid(uuid)
	char	*uuid;
{
	int	i,j;
	char	*output;
	char	*ptr;
	int	tmp1, tmp2;

	ptr = uuid;
	j = 0;
	output = malloc(MUX_UUID_SIZE);
	for (i = 0; i < 16;) {
		if (ptr[j] != '-') {
			if ((ptr[j+1] >= 0x30) && (ptr[j+1] <= 0x39)) {
				tmp1 = ptr[j+1] - 0x30;
			} else {
				tmp1 = ptr[j+1] - 0x57;
			}
			if ((ptr[j] >= 0x30) && (ptr[j] <= 0x39)) {
				tmp2 = ptr[j] - 0x30;
			} else {
				tmp2 = ptr[j] - 0x57;
			}
			output[i] = (char) (tmp2 << 4) | tmp1;
			j += 2;
			i++;
		} else {
			j++;
		}
	}

	return output;
}


char *
mux_convert_byteuuid_to_stduuid(uuid)
	char	*uuid;
{
	int	i,j;
	char	*output;
	char	*ptr;
	char	first, second;

	ptr = uuid;
	j = 0;
	output = malloc(MUX_STDUUID_SIZE);
	memset(output, '\0', MUX_STDUUID_SIZE);
	for (i = 0; i < 16; i++) {
		first = (ptr[i] & 0xf0) >> 4;
		second = ptr[i] & 0x0f;

		if ((first >= 0x0) && (first <= 0x9)) {
			output[j++] = first + 0x30;
		} else {
			output[j++] = first + 0x57;
		}
		if ((second >= 0x0) && (second <= 0x9)) {
			output[j++] = second + 0x30;
		} else {
			output[j++] = second + 0x57;
		}

		if ((j == 8) || (j == 13) || (j == 18) ||
		    (j == 23)) {
			output[j++] = '-';
		}
	}

	return output;
}

char *
mux_get_my_uuid()
{
    char        buf[BUFSIZ], *ptr1;
    FILE        *ptr;
    char	    *output = NULL;
    size_t	    len;
    XS_HANDLE 	handle = NULL;
    XS_TRANSACTION	tx;
    int	uuid_size_in_ascii = 36;
    int	        ret;

    if((fn_xs_transaction_start == NULL) || (fn_xs_domain_close == NULL) || 
        (fn_xs_read == NULL) || fn_xs_domain_open)
    {
        ret = loadXsLibrary();
        if(ret) {
            MUX_LOG_ENTRY0("mux_get_my_uuid: xs.dll load falied");
            goto out;
        }
    }

retry2:
    handle = fn_xs_domain_open();
    if (handle == NULL) {
        MUX_LOG_ENTRY0("Unable to get xs handle ");
        SLEEP(1);
        goto retry2;
    }
    
    tx = fn_xs_transaction_start(handle);

retry3:
    ptr1 = fn_xs_read(handle, "vm", &len);
    if (ptr1 == NULL) {
        MUX_LOG_ENTRY0("Unable to read from xenstore");
        SLEEP(1);
        goto retry3;
    }
    strncpy(buf, ptr1, len);
    free(ptr1);
    fn_xs_domain_close(handle);
    /* Get rid of "/vm/" */
    ptr1 = buf;
    ptr1 += 4;
    ptr1[uuid_size_in_ascii] = '\0';
    
    output = mux_convert_stduuid_to_byteuuid(ptr1);
	return output;
out:
    return output;
}

/*
 * This function is called to load the xs.dll library. vxtmsg_mux.dll
 * Uses xs_read function from this library to read the uuid.
 */

int GetVxVIPath()
{

    LONG	status;

    HKEY        hKey = NULL;
    ULONG       valueType = REG_SZ;
    static WCHAR MsgPath[MAX_PATH];
    DWORD        dwBufSize = MAX_PATH;
    HINSTANCE    hinstLib;
    DWORD	     error = 0;

    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, VXVIPATH, 0, KEY_READ, &hKey);
    if (status != ERROR_SUCCESS) 
    {
        goto exit;
    }

    /*
     * Get the XenTools Install_Dir key.
     */
    
    status = RegQueryValueEx(hKey, VXVI_INSTAL_KEY, NULL, &valueType, 
            (PBYTE)mxtport, &dwBufSize);

    if((status != ERROR_SUCCESS) || (valueType != REG_SZ))
    {   
        MUX_LOG_ENTRY0("Get mxtport File Path Failed");
        goto exit;
    } 
exit:
    if(hKey != NULL) RegCloseKey(hKey);
    strcat(mxtport, "\\vxtport");
    MUX_LOG_ENTRY1("Path To mxtport File %s ", mxtport);
}

int
mux_get_conn_cookie()
{
	return(rand());
} 
