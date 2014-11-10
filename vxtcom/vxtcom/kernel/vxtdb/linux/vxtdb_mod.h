#ifndef _VXTDB_MOD_H_
#define _VXTDB_MOD_H_

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

#ifndef LEGACY_4X_LINUX
#include <asm-generic/fcntl.h>
#endif
#include <linux/fs.h>

/*
 * create an O.S. independent description of the file object
 */

/*
#define vxt_file_t FILE
*/

#define vxt_file_t struct file

#define VXT_FILE_CREATE O_CREAT
#define VXT_FILE_SYNC   O_SYNC
#define VXT_FILE_READ   O_RDONLY
#define VXT_FILE_WRITE  O_WRONLY
#define VXT_FILE_RW     O_RDWR


#define vxt_convert_str_to_ulong(str, endptr, base)             \
        simple_strtoul(str, endptr, base)

#define vxt_convert_str_to_ulonglong(str, endptr, base)             \
        simple_strtoull(str, endptr, base)


extern vxt_file_t *
vxt_file_open(char *filename, int flags);

extern int
vxt_file_read(vxt_file_t *fd, void *buffer, uint64_t length, uint64_t *pos);


extern int
vxt_file_write(vxt_file_t *fd, void *buffer, uint64_t length, uint64_t *pos);

extern int
vxt_file_close(vxt_file_t *fd);

extern int
vxt_file_remove(vxt_file_t *fd);

extern int
vxt_file_seek(vxt_file_t *fd, uint64_t offset);


extern uint64_t
vxt_file_size(vxt_file_t *dv_record_file);

extern int
vxt_uuid_to_local_dom(char *uuid, char *domain_name);

/*
 * Routines exported from vxt_card (part of vxt_modules.h)
 */

extern int
vxt_class_device_create(uint32_t major, uint32_t minor,
                        char *new_dev_name);

extern void
vxt_class_device_destroy(uint32_t major, uint32_t minor);

extern int
vxt_get_local_uuid(char *uuid);

extern int
vxt_auth_db_lookup(char *key, char *field, char* record_field);

extern int
vxt_auth_db_multi_look(char *key, char *field,
                       char **results, uint32_t *rec_cnt);

extern int
vxt_is_management_hub(void);


#endif /* _VXTDB_MOD_H_ */
