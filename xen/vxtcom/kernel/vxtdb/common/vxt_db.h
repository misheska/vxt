#ifndef _VXT_DB_H_
#define _VXT_DB_H_

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


extern vxt_mlock_t vxtdb_request_lock;

extern int
vxtdb_module_init(void **db);

extern void *
vxt_db_create(char *db_file_name);

extern int
vxtdb_module_shutdown(void *db_handle);

extern int
vxt_db_insert(void *database, vxt_db_file_rec_t *record, uint32_t flags);

extern int
vxt_db_lookup_client(void *database, char *requester, char *name,
                     vxt_db_lookup_rec_t *record);

extern int
vxt_db_delete_record(void *database, char *client, char *server, char *device);

extern int
vxt_db_dump_guest(void *database, char *guest,
                  void *buffer, uint32_t *size, uint32_t *overrun);

extern int
vxt_db_guest_dev_count(void *database, char *guest, uint32_t *record_count);

extern int
vxt_db_delete_guest(void *database, char *endpoint, uint32_t migrate);

extern int
vxt_db_restore_guest(void *database, char *guest_uuid,
                     vxt_db_file_rec_t *record_array,
                     uint32_t record_count, uint32_t migrate);

#endif /* _VXT_DB_H_ */
