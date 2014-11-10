#ifndef _VXT_AUTH_H_
#define _VXT_AUTH_H_

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
 * User level os dependent files for exported VxT function
 * definition are sublimated inside of the O.S. dependent
 * layer.
 */
#if (VXT_OS_DEP == linux_ver1)
#include <public/linux/vxt_auth_os.h>
#endif

#if (VXT_OS_DEP == win)
#include <public/windows/vxt_auth_os.h>
#endif




/*
 * vxt database file header fields and
 * version.  We export these so that we
 * may create debug aids and database
 * image file readers.
 */

#define VXT_BANNER  "VxT Local Authorization File, version 1.0"
/*
 * Version 1.00.00
 */
#define VXT_DB_VERSION 0x10000

#define GET_VXT_DB_VER(version) \
	(((version) >> 16) & 0xFFFFFFFF)

#define GET_VXT_DB_REL(version) \
	(((version) >> 8) & 0xFF)

#define GET_VXT_DB_MINOR(version) \
	((version) & 0xFF)


/*
 *
 * vxt_db_header:
 *
 */

typedef struct vxt_db_header {
	char banner[VXT_DB_NAME_MAX];
	uint32_t  extended_version;
	uint32_t  version;
} vxt_db_header_t;

/*
 * vxt ioctl command defines
 */
#define VXT_AUTH_DB_INSERT        0x1
#define VXT_AUTH_DB_CLIENT_LOOKUP 0x2
#define VXT_AUTH_DB_DELETE_REC    0x3
#define VXT_AUTH_DB_DUMP_GUEST    0x4
#define VXT_AUTH_DB_RESTORE_GUEST 0x5
#define VXT_AUTH_DB_QUERY         0x6
#define VXT_AUTH_DB_START         0x7
#define VXT_AUTH_GET_LOCAL_UUID   0x8


/*
 * vxt_db_insert command invocation flags
 */
#define VXT_DB_INSERT_CLIENT 0x1
#define VXT_DB_INSERT_SERVER 0x2
#define VXT_DB_INSERT_FILE   0x1000000 /* system only */


/*
 *
 * Default UUID substitution string for Local Dom0
 * connections.  No need to change records when
 * guest migrates with this approach
 *
 */

#define VXTDB_DOM0_ENDPOINT "Default Local Domain 0 Connection"


/*
 *
 * vxt_db_file_rec:
 *
 * The vxt_db_file_rec is used for file based records.  It differs from the
 * in-memory version in that there is one record for each client/server/devname
 * triple.
 *
 * The fully defined nature of the vxt_db_file_rec means that its size
 * is invariant.  This makes file records easier to maintain.
 */

typedef struct vxt_db_file_rec {
	struct {
		uint64_t        valid : 1,
		direct : 1,   // doesn't need data auditing
		reserved : 62;
	} priv;
	char client[40];
	char server[40];
	char dev_name[MAX_VXT_UNAME];
	uint64_t class_name_length;      // client controlled
	uint64_t label;     		 // client controlled
} vxt_db_file_rec_t;

/*
 * vxt_db_lookup_rec:
 *
 * The vxt_db_lookup_rec is used on calls to client_lookup
 * and any other related lookup or insert call.
 * It differs from the file record in that it returns additional
 * information on derived from the client and server uuids
 * In the cases where these fields are to local virtual machines
 * the related domain field is filled in.
 *
 *  Also of note is the label field.  This field is not used
 * internally but is faithfully recorded and returned, allowing
 * application level routing facilities to authorize and route
 * services as easily as device traffic.  The integration allows
 * services to move back and forth between direct device and communication
 * channel without requiring separate handling
 */

typedef struct vxt_db_lookup_rec {
        struct  {
                uint64_t        direct : 1,   // doesn't need data auditing
                                reserved : 63;
        } priv;
        char client[40];
        char server[40];
        char dev_name[MAX_VXT_UNAME];
        uint64_t client_dom;
        uint64_t server_dom;
	uint64_t label;      // client controlled
} vxt_db_lookup_rec_t;


typedef struct vxt_db_lookup_client_arg {
	char requester[40];
	char dev_name[MAX_VXT_UNAME];
	vxt_db_lookup_rec_t record;   /* filled in on call */
} vxt_db_lookup_client_arg_t;




typedef struct vxt_db_insert_arg {
	vxt_db_file_rec_t record;
	uint32_t flags;
} vxt_db_insert_arg_t;


typedef struct vxt_db_delete_rec_arg {
	char client[40];
	char server[40];
        char dev_name[MAX_VXT_UNAME];	
} vxt_db_delete_rec_arg_t;


typedef struct vxt_db_dump_guest_arg {
	char guest[40];
	void *record_buffer;
	uint32_t buf_size;   /* in number of records */
	uint32_t overrun;    /* was the buffer big enough? */
	uint32_t delete;     /* delete in-memory records after reading them? */
	uint32_t migrate;    /* remove whole record or just guest side */
} vxt_db_dump_guest_arg_t;


typedef struct vxt_db_restore_guest_arg {
	char guest[40];
	void *record_buffer;
	uint32_t buf_size;   /* in number of records */
	uint32_t migrate;    /* remove whole record or just guest side */
} vxt_db_restore_guest_arg_t;

typedef struct vxt_db_query_arg {
	uint32_t hub;        /* 1 if we are a hub, 0 otherwise */
} vxt_db_query_arg_t;

typedef struct vxt_db_start_arg {
	char db_file_name[VXT_DB_NAME_MAX];
} vxt_db_start_arg_t;

typedef struct vxt_db_local_uuid_arg {
	char uuid[MAX_VXT_UUID];
} vxt_db_local_uuid_arg_t;


#endif /* _VXT_AUTH_H_ */
