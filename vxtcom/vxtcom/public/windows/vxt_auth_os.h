#ifndef _VXT_AUTH_OS_H_
#define _VXT_AUTH_OS_H_

#define VXT_AUTH_DEVICE_NAME "\\Device\\Vxt\\vxtdb-auth"
#define VXT_DEFAULT_DEVICE   "MXT_VXT_PIPE"

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


/*
 *
 * vxt authorization file constants
 *
 */
#define VXT_DB_DEFAULT_FILE_NAME "vxtauth_db_file"
//TODO find DB path
#define VXT_DB_PATH "C:\\Program Files\\Symantec\\VxVI\\"
#define VXT_DB_PATH_LENGTH MAX_PATH
#define VXT_DB_NAME_MAX 256

extern int 
vxtdb_auth_init();

extern int
vxtdb_dev_ioctl(unsigned command, unsigned long argument);

extern int
vxtdb_dev_open(vxt_fd_struct_t * filep, struct vxtdev_ext *vde);

extern int
vxtdb_dev_close(vxt_fd_struct_t * filep);

#endif /* _VXT_AUTH_OS_H_ */
