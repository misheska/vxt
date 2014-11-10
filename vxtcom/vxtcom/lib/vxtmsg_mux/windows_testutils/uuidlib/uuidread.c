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

#include <stdio.h>
#include <xs.h>
#include <stdlib.h>
#include <windows.h>
#include <memory.h>

typedef (*PXS_DOMAIN_OPEN)();
typedef void   (*PXS_DOMAIN_CLOSE)(HANDLE);
typedef (*PXS_TRANSACTION_START)(HANDLE);
typedef void * (*PXS_READ)(HANDLE, char*, size_t);

#define HKLM                      HKEY_LOCAL_MACHINE
#define XENPATH TEXT("SOFTWARE\\Citrix\\XenTools")
#define XENPATH_ALT_64 TEXT("SOFTWARE\\Wow6432Node\\Citrix\\XenTools")
#define XENPATH_ALT TEXT("SOFTWARE\\XenSource")
#define INSTALL_KEY TEXT("Install_dir") 
#define MUX_UUID_SIZE 16

#define XS_HANDLE	HANDLE

/*
 * This is defined as NULL as there is no xs_transaction in windows. Need to consult
 * XDC fols.
 */

#define XS_TRANSACTION	BOOL

#define XS_TRANSACTION_START(handle) \
    xs_transaction_start(handle)


static PXS_DOMAIN_OPEN             fn_xs_domain_open = NULL;
static PXS_DOMAIN_CLOSE            fn_xs_domain_close = NULL;
static PXS_TRANSACTION_START       fn_xs_transaction_start = NULL;
static PXS_READ                    fn_xs_read = NULL;





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
    hinstLib = LoadLibrary(MsgPath);
    if(hinstLib != NULL) goto get_fuction_pointers;

next:
    error = GetLastError();
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
    hinstLib = LoadLibrary(MsgPath);
    if(hinstLib != NULL) goto get_fuction_pointers;

    /*
     * Try to load the library from the current directory.
     */ 
next1:
    error = GetLastError();
    hinstLib = LoadLibrary("xs.dll");
    if (hinstLib != NULL) goto get_fuction_pointers;
        
try64:
    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, XENPATH_ALT_64, 0, KEY_READ, &hKey);
    if (status != ERROR_SUCCESS) 
    {
        goto out;
    }

    /*
     * Get the XenTools Install_Dir key.
     */
    status = RegQueryValueEx(hKey, INSTALL_KEY, NULL, &valueType, 
            (PBYTE)MsgPath, &dwBufSize);

    if((status != ERROR_SUCCESS) || (valueType != REG_SZ))
    {   
        RegCloseKey(hKey);
        goto out;
    }
    strcat(MsgPath, "\\xs.dll");
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
        goto out;
    }

    /*
     * xs_domain_close
     */

    fn_xs_domain_close = (PXS_DOMAIN_CLOSE)GetProcAddress(hinstLib, "xs_daemon_close");
    if(fn_xs_domain_close == NULL)
    {
        goto out;
    }

    /*
     * xs_domain_read
     */

    fn_xs_read = (PXS_READ)GetProcAddress(hinstLib, "xs_read");
    if(fn_xs_read == NULL)
    {
        goto out;
    }

    /*
     * xs_transaction_start
     */

    fn_xs_transaction_start = (PXS_TRANSACTION_START)GetProcAddress(hinstLib, "xs_domain_open");
    if(fn_xs_transaction_start == NULL)
    {
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

int
get_my_uuid(char *inBuf)
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
            goto out;
        }
    }

retry2:
    handle = fn_xs_domain_open();
    if (handle == NULL) {
        Sleep(1000);
        goto retry2;
    }
    
    tx = fn_xs_transaction_start(handle);

retry3:
    ptr1 = fn_xs_read(handle, "vm", &len);
    if (ptr1 == NULL) {
        Sleep(1000);
        goto retry3;
    }
    strncpy(buf, ptr1, len);
    //strncpy(buf, ptr1, len);
    free(ptr1);
    fn_xs_domain_close(handle);
    /* Get rid of "/vm/" */
    ptr1 = buf;
    ptr1 += 4;
    ptr1[uuid_size_in_ascii] = '\0';
    memset(inBuf, '\0', BUFSIZ);
    strncpy(inBuf, ptr1, uuid_size_in_ascii);
	return 0;
out:
    return -1;
}


  
