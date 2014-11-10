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
 * The following two defines are for UMI logging and they must
 * be correct or the logged serial number will be invalid and
 * misleading.
 */

#define _VXT_COMPONENT_ 7
#define _VXT_SUBSYSTEM_ 19

#include <public/kernel/os/vxtcom_embedded_os.h>
#include <public/vxt_system.h>
#include <vxtdb/os/vxtdb_mod.h>
#include <public/vxt_auth.h>
#include <public/kernel/vxt_auth_db.h>
#include <public/kernel/os/vxtcom_embedded_os.h>

/*
 *
 *
 * Note: we do not keep the Domain ID's here so that we do not need to
 * update them on migration.  i.e. a simple check:
 *
 *
 * xenstore-read /vm/68e7503c-7b3e-de84-988b-512ebb6cf9da/name
 *
 * Will tell us if we are local and give us the domain at the same time.
 */


typedef struct vxt_dev_rec {
	uint64_t file_offset;
	char dev_name[MAX_VXT_UNAME];
	struct vxt_dev_rec *next;
	uint64_t class_name_length;
	uint64_t            label;
} vxt_dev_rec_t;

typedef struct vxt_db_rec vxt_db_rec_t;

typedef struct vxt_db_hash_entry {
	vxt_db_rec_t *parent;
	struct vxt_db_hash_entry *prev;
	struct vxt_db_hash_entry *next;
} vxt_db_hash_entry_t;


/*
 * vxt_db_rec:
 *
 * vxt_db_rec is used for in-memory authentication records
 * The hash is against the client or server and a linked list
 * of devices for the pair is traversed.
 */

struct vxt_db_rec {
	struct {
		uint64_t	valid : 1,
		                direct : 1,   // doesn't need data auditing
				reserved : 62;
	} priv;
	char client[40];
	char server[40];
	vxt_dev_rec_t       *devices;
	vxt_db_hash_entry_t client_list;
	vxt_db_hash_entry_t server_list;
	uint32_t            device_count;
};

typedef struct vxt_db_rec_list {
	vxt_db_file_rec_t record;
	struct vxt_db_rec_list *next;
} vxt_db_rec_list_t;

typedef struct vxt_db_device_rec {
	char dev_name[MAX_VXT_UNAME];
	struct vxt_db_device_rec *next;
} vxt_db_device_rec_t;


/*
 * The authentication requester is always local to the machine.
 * If a guest migrates the hashed list of authenticated connection
 * targets moves with it.
 *
 * The target endpoint entries will not be on the machine if the 
 * target domain is on a remote platform.  Rather, after
 * doing a local lookup against the client and finding the
 * endpoint is on a different platform, the remote authentication handling 
 * routines will do a remote lookup using the target domain UUID as the hash
 * and using the server/client pair as the key.
 *
 * When a new authentication is recoreded, both endpoints are
 * hashed.
 *
 */



typedef struct vxt_db_free_file_rec_entry {
	uint64_t offset;
	struct vxt_db_free_file_rec_entry *next;
} vxt_db_free_file_rec_entry_t;


/*
 *
 * vxt_db_free_file_rec_entry:
 *
 * When a file has entries removed, there will be records that are 
 * no longer valid distributed in the body of the file.  Rather than
 * allowing the file to grow uncontrollably with repeated removal
 * and insertion cycles, a list of these empty spots is kept and
 * they are used when available rather than appending to the file.
 */

vxt_db_free_file_rec_entry_t *vxt_db_free_file_rec_list = NULL;

/*
 * We are only supporting 50 guests at present so a hash number much more
 * than double this is a waste. i.e. both endpoints hashed.
 */
#define VXT_HASH_TABLE_SIZE 109


/*
 * 
 * vxt_auth_database:
 *
 * Header structure that points to a live database.  This structure
 * is returned from a vxt_db_create call.
 */

typedef struct vxt_auth_database {
	vxt_db_free_file_rec_entry_t *free_file_rec_list;
	uint64_t                     database_end;
	vxt_file_t                   *fds;
	vxt_db_hash_entry_t hash_table[VXT_HASH_TABLE_SIZE];
	char db_file_name[VXT_DB_NAME_MAX];
} vxt_auth_database_t;

vxt_mlock_t vxtdb_request_lock;

/*
 *
 *
 * We will have hash entries for client and server if both are in 
 * our machine.  i.e. there is a has record of each local VM's 
 * authorized VxT connections.
 *
 *
 *
 * Look up the requester and see if there is a record matching:
 *
 *	The dev name
 *	The dev name and remote
 *
 *
 * Since device names are not unique, there may more than one match.
 * In this case, the number of matches is returned along with the
 * first match.  The caller can pass a buffer big enough for the 
 * entire list on the next call.
 *
 *
 */


/*
 *
 * vxt_db_uuid_hash:
 * Calculate the hash value based on the uuid passed.
 *
 */

static inline uint32_t
vxt_db_uuid_hash(char *uuid)
{
	uint32_t bytes;
	uint32_t words;
	uint32_t rem;
	uint32_t rem_result;
	uint32_t accum = 0;
	uint32_t i;
	uint32_t *signature = (uint32_t *)uuid;

	UMI_LOG(1, VXTPRINT_PROFILE_LEVEL, "called\n");
	bytes = strlen(uuid);
	words = bytes >> 2;
	rem = bytes - (words << 2);
	for (i = 0; i < words; i++) {
		accum ^= signature[i];
	}
	bytes -= rem + 1;  // array starts at zero
	for (i = 0; i < rem; i++) {
		rem_result = uuid[bytes + i];
		rem_result = rem_result << (i * 3);
		accum ^= rem_result;
	}

	return (accum % VXT_HASH_TABLE_SIZE);
}


/*
 *
 * vxt_db_uuid_lookup:
 *
 * With vxt_db_uuid_lookup at present it is expected that the
 * lookup will be from the requester which is always local.
 * The requester may have a series of records for all of the
 * other VM's it is attached to.  At this point these are
 * searched sequentially off of a single hash.  A secondary
 * hash may be instituted at this level when we have a 
 * large cross-product of combinations.  If the remote value
 * is NULL, the first entry corresponding to the uuid is returned.
 *
 * vxt_db_uuid_lookup returns the embeded hash entry of a record
 * if there is one for the matched pair of the uuid and the remote. 
 * The record will contain a list of all the devices linking the two
 * endpoints.  The embedded hash entry is returned so that it may
 * be used in extended lookups.  i.e. following the chain on multiple
 * match instances.  Since there is more than one lookup entry, 
 * this is needed to direct the follow-up processing to the right
 * queue.
 *
 * vxt_db_uuid_lookup also returns a boolean named "is_client".
 * This variable communicates whether the target uuid was found
 * in the client or the server field of the discovered record.
 *
 */

static vxt_db_hash_entry_t *
vxt_db_uuid_lookup(vxt_db_hash_entry_t *hash_table, char *uuid,
                   char *remote, uint32_t *is_client)
{
	uint32_t bucket;
	vxt_db_hash_entry_t *record_hash;

	UMI_LOG(2, VXTPRINT_PROFILE_LEVEL, "called\n");
	bucket = vxt_db_uuid_hash(uuid);

	/*
	 * Traverse the linked list of records found here
	 */
	record_hash = hash_table[bucket].next;
	while (record_hash != &(hash_table[bucket])) {
		if (!strncmp(record_hash->parent->client, uuid, 40)) {
			if ((remote == NULL) || 
			    (!strncmp(record_hash->parent->server, 
			              remote, 40))) {
				*is_client = 1;
				break;
			}
		} else if (!strncmp(record_hash->parent->server, uuid, 40)) {
			if ((remote == NULL) || 
			    (!strncmp(record_hash->parent->client,
			              remote, 40))) {
				*is_client = 0;
				break;
			}
		} 
		record_hash = record_hash->next;
	}
	
	if (record_hash == &(hash_table[bucket])) {
		return NULL;
	}
	return record_hash;
	
}


/*
 *
 * vxt_db_lookup_next:
 *
 * vxt_db_lookup_next takes a record hash pointer for a hash
 * element embedded in a vxt_db_rec_t, as well as the target
 * endpoint/guest uuid.  The hash_pointer tells us where to start
 * looking in the list of elements for the next authorization
 * record that contains our endpoint uuid.
 *
 * vxt_db_lookup_next returns a hash_entry if another record is
 * found and NULL otherwise.  No change in the in-memory or
 * on-disk database is made.
 *
 * NOTE:
 * *************** uuid must be linked to prev_rec_hash ***********
 * There is an inherent vulnerability in vxt_db_lookup_next
 * The user passes a uuid and a record hash entry.  If the record
 * hash entry is not in the hash_list associated with the hash on
 * the uuid, this routine will never end.  We are dependent on
 * proper usage by the caller.
 *
 * Returns:
 *		Next matching record upon success
 *		NULL if no more matching records found
 *
 */


static vxt_db_hash_entry_t *
vxt_db_lookup_next(vxt_db_hash_entry_t *hash_table,
                   char *uuid, vxt_db_hash_entry_t *prev_rec_hash)
{
	uint32_t bucket;
	vxt_db_hash_entry_t *record_hash;

	bucket = vxt_db_uuid_hash(uuid);

	/*
	 * Traverse the linked list of records, starting at the previous_record
	 * point. 
	 */
	record_hash = prev_rec_hash->next;
	while (record_hash != &(hash_table[bucket])) {
		if (!strncmp(record_hash->parent->client, uuid, 40)) {
			break;
		} else if (!strncmp(record_hash->parent->server, uuid, 40)) {
			break;
		}
		record_hash = record_hash->next;
	}
	
	if (record_hash == &(hash_table[bucket])) {
		return NULL;
	}
	return record_hash;
}



/*
 *
 * vxt_db_destroy_dev_rec:
 *
 * device records are part of the in-memory hash machinery
 * The device record actually has a one-to-one correspondence
 * with the on disk file record. Hence, there is a file offset
 * within the device record for the on-disk object.  
 * vxt_db_destroy_dev_rec will remove the on-disk record as
 * well as freeing the device record structure. 
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon successful invocation
 *		VXT_FAIL - Unable to clean up the on-disk record
 *		VXT_NOMEM - Unable to reflect the free space in the
 *                          on-disk database
 *
 */

static int
vxt_db_destroy_dev_rec(void *database, vxt_dev_rec_t *dev_rec)
{
	vxt_auth_database_t *db = (vxt_auth_database_t *)database;
	vxt_db_file_rec_t   record;
	vxt_db_free_file_rec_entry_t *free_list_ele;
	uint64_t            tmp_offset;
	int                 ret;

	UMI_LOG(3, VXTPRINT_PROFILE_LEVEL, "called, dev %s, "
	        "file_offset %llx\n", dev_rec->dev_name, dev_rec->file_offset);
	/* The only field that need be filled in is "valid".  The
	 * clearing of this flag indicates the rest of the record
	 * contents are undefined and the record space is available
	 */
	record.priv.valid = 0;
	record.client[0] = 0;
	record.server[0] = 0;
	record.dev_name[0] = 0;
/*
	vxt_file_seek(db->fds, dev_rec->file_offset);
*/
	tmp_offset = dev_rec->file_offset;
	ret = vxt_file_write(db->fds, (void *)&record,
	                     sizeof(vxt_db_file_rec_t), &tmp_offset);
	if (ret == -1) {
		return VXT_FAIL;
	}
	/*
	 * Create an element to add the free record space to our list
	 */
	free_list_ele =
	   vxt_kmalloc(sizeof(vxt_db_free_file_rec_entry_t), VXT_KMEM_KERNEL);
	if (free_list_ele == NULL) {
		return VXT_NOMEM;
	}
	free_list_ele->offset = dev_rec->file_offset;
	free_list_ele->next = db->free_file_rec_list;
	db->free_file_rec_list = free_list_ele;
	
	vxt_kfree(dev_rec);
	return VXT_SUCCESS;
}


/*
 *
 * vxt_db_insert_internal:
 *
 * vxt_db_insert_internal is the operational component of the vxt_db_insert
 * call.  It is wrapped by vxt_db_insert to protect the in-memory recovery
 * from database file function.  When a the vxtdb_auth subsystem first
 * starts, it reads its targeted database file.  If pre-existing records
 * are found they are read in and supplied to repeated vxt_db_insert_internal
 * calls.  vxt_db_insert_internal responds by updating the in-memory hashes
 * but ignoring the on-disk file update.  To do this it checks the flags field
 * If the VXT_DB_INSERT_FILE flag is not asserted, it pushes the value of 
 * record_offset into the appropriate device structure and forgoes the
 * update of the on-disk database file.
 *
 * Returns:
 */

int
vxt_db_insert_internal(void *database, vxt_db_file_rec_t *record,
                       uint32_t flags, uint64_t record_offset)
{
	vxt_auth_database_t *db = (vxt_auth_database_t *)database;
	vxt_dev_rec_t       *device;
	vxt_db_rec_t        *db_rec;
	vxt_db_hash_entry_t *rec_hash;
	int                 write_cnt;
	int                 remainder;
	uint32_t            is_client;
	uint32_t            bucket;
	

	UMI_LOG(4, VXTPRINT_PROFILE_LEVEL, "called\n");
	if ((flags & (VXT_DB_INSERT_CLIENT | VXT_DB_INSERT_SERVER)) == 0) {
		UMI_LOG(5, VXTPRINT_BETA_LEVEL,
		        "Bad flags field, 0x%x\n", flags);
		return VXT_PARM;
	}
	rec_hash = vxt_db_uuid_lookup(db->hash_table, record->client,
	                              record->server, &is_client);
	if (rec_hash == NULL) {
		rec_hash = vxt_db_uuid_lookup(db->hash_table, record->server,
		                              record->client, &is_client);
	}
	if (rec_hash != NULL) {
		UMI_LOG(6, VXTPRINT_BETA_LEVEL, "found existing record\n");
		db_rec = rec_hash->parent;
	} else {
		db_rec = NULL;
	}

	if (db_rec != NULL) {
		/* The record already exists, just add the new
	 	 * device structure
		 */
		device = db_rec->devices;
		while (device != NULL) {
			UMI_LOG(7, VXTPRINT_DEBUG_LEVEL,
			        "running devices, record: %s, device: %s\n",
			        record->dev_name, device->dev_name);
			if (!strncmp(record->dev_name,
			             device->dev_name, MAX_VXT_UNAME)) {
				break;
			}
			device = device->next;
		}
		if (device == NULL) {
			UMI_LOG(8, VXTPRINT_BETA_LEVEL,
			"adding new device to in-memory record\n");
			device = vxt_kmalloc(sizeof(vxt_dev_rec_t),
			                     VXT_KMEM_KERNEL);
			if (device == NULL) {
				return VXT_NOMEM;
			}
			device->label = record->label;
			strncpy(device->dev_name,
			        record->dev_name, MAX_VXT_UNAME);
			device->class_name_length = record->class_name_length;
			device->next = db_rec->devices;
			db_rec->device_count++;
			db_rec->devices = device;
			if (flags & VXT_DB_INSERT_FILE) {
				device->file_offset = 0;
			} else {
				device->file_offset = record_offset;
			}
		}
	} else {
		/*
		 * Need to insert a new db_rec
		 */
		db_rec = vxt_kmalloc(sizeof(vxt_db_rec_t), VXT_KMEM_KERNEL);
		if (db_rec == NULL) {
			return VXT_NOMEM;
		}
		UMI_LOG(9, VXTPRINT_BETA_LEVEL,
		        "Creating new db_rec entry for in-memory hashes\n");
		strncpy(db_rec->client, record->client, 40);
		strncpy(db_rec->server, record->server, 40);
		db_rec->client_list.parent = db_rec;
		db_rec->client_list.next = &(db_rec->client_list);
		db_rec->client_list.prev = &(db_rec->client_list);
		db_rec->server_list.parent = db_rec;
		db_rec->server_list.next = &(db_rec->server_list);
		db_rec->server_list.prev = &(db_rec->server_list);
		db_rec->devices = NULL;
		db_rec->priv.valid = record->priv.valid;
		db_rec->priv.direct = record->priv.direct;
		db_rec->device_count = 1;
		device = vxt_kmalloc(sizeof(vxt_dev_rec_t), VXT_KMEM_KERNEL);
		if (device == NULL) {
			vxt_kfree(db_rec);
			return VXT_NOMEM;
		}
		device->next = NULL;
		db_rec->devices = device;

		if (flags & VXT_DB_INSERT_FILE) {
			device->file_offset = 0;
		} else {
			device->file_offset = record_offset;
		}
		strncpy(device->dev_name, 
		        record->dev_name, MAX_VXT_UNAME);
		device->label = record->label;
		device->class_name_length = record->class_name_length;

	}

	/*
	 * Insert the new record in the hash table
	 */

	if (flags & VXT_DB_INSERT_CLIENT) {
		if (db_rec->client_list.next ==
		    &(db_rec->client_list)) {
			UMI_LOG(10, VXTPRINT_BETA_LEVEL,
				"Adding client hash\n");
			/* 
			 * Record needs to be inserted
			 */
			bucket = vxt_db_uuid_hash(record->client);
			/*
			 * Place new record in the linked list of 
			 * records found here
			 */
			db_rec->client_list.next = 
			   db->hash_table[bucket].next;
			db_rec->client_list.prev =
			   db->hash_table[bucket].next->prev;
			db->hash_table[bucket].next->prev =
			   &(db_rec->client_list);
			db->hash_table[bucket].next = 
			   &(db_rec->client_list);
		}
	}
	if (flags & VXT_DB_INSERT_SERVER) {
		if (db_rec->server_list.next == 
		    &(db_rec->server_list)) {
			UMI_LOG(11, VXTPRINT_BETA_LEVEL,
				"Adding server hash\n");
			/* 
			 * Record needs to be inserted
			 */
			bucket = vxt_db_uuid_hash(record->server);
			/*
			 * Place new record in the linked list of 
			 * records found here
			 */
			db_rec->server_list.next =
			   db->hash_table[bucket].next;
			db_rec->server_list.prev =
			   db->hash_table[bucket].next->prev;
			db->hash_table[bucket].next->prev =
			   &(db_rec->server_list);
			db->hash_table[bucket].next = 
			   &(db_rec->server_list);

		}
	}

	if ((device->file_offset == 0) &&
	    (flags & VXT_DB_INSERT_FILE)) {
		uint64_t offset;
		uint64_t tmp_offset;
		vxt_db_free_file_rec_entry_t *file_rec;
		UMI_LOG(12, VXTPRINT_BETA_LEVEL,
		        "Adding on-disk record\n");
		if (db->free_file_rec_list) {
			file_rec = db->free_file_rec_list;
			db->free_file_rec_list = db->free_file_rec_list->next;
			offset = file_rec->offset;
			vxt_kfree(file_rec);
		} else {
			offset = db->database_end;
			db->database_end += sizeof(vxt_db_file_rec_t);
		}
		device->file_offset = offset;
/*
		vxt_file_seek(db->fds, offset, SEEK_SET);
*/

		write_cnt = 0;
		remainder = sizeof(vxt_db_file_rec_t);
		tmp_offset = offset;

		while (remainder) {
			UMI_LOG(13, VXTPRINT_DEBUG_LEVEL,
			        "Write to db, count 0x%x\n", remainder);

			write_cnt = vxt_file_write(db->fds, 
		                                  record,
			                          remainder, &tmp_offset);
			if (write_cnt < 0) {
				file_rec = 
				   vxt_kmalloc(
				      sizeof(vxt_db_free_file_rec_entry_t),
				      VXT_KMEM_KERNEL);
				if (file_rec != NULL) {
					file_rec->offset = offset;
					file_rec->next =
					   db->free_file_rec_list;
					db->free_file_rec_list = file_rec;
				}
				/*
				 * This is actually a fatal error, the
				 * data base may be corrupted as well
				 * Fatal if remainder 
				 * 		!= sizeof(vxt_db_file_rec_t)
				 */
				UMI_LOG(14, VXTPRINT_PRODUCT_LEVEL,
				        "Unable to write record, "
				        "data base entry may be lost\n");
				return VXT_ERROR;
			}
			UMI_LOG(15, VXTPRINT_DEBUG_LEVEL,
			        "write count: 0x%x\n", write_cnt);
			remainder -= write_cnt;
		}
		UMI_LOG(16, VXTPRINT_DEBUG_LEVEL,
		        "new on-disk record offset 0x%llx\n", offset);
		device->file_offset = offset;
	}
	return VXT_SUCCESS;
}


/*
 *
 * vxt_db_insert:
 *
 * Add a new authorization record to the vxt database.  Note the
 * hash can be done on the client or the server uuid. Both sides
 * must be checked for an existing in-memory record.
 * 
 * The VXT_DB_INSERT_CLIENT and VXT_DB_INSERT_SERVER flags are checked
 * to see which hashes will be active.  The caller will not ask for
 * a record to be inserted for a remote client.
 *
 * The in-memory hash records have a secondary lookup based
 * on the devices associated with the client/server pair.
 * If vxt_db_insert finds a pre-existing record for the client/server
 * pair, it will run through the device list to see if the targeted
 * device is already registered.  If not a new device entry is
 * made against the existing in-memory endpoint pair hash record.
 *
 * 
 *
 */

int
vxt_db_insert(void *database, vxt_db_file_rec_t *record, uint32_t flags)
{
	int ret;
	ret =  vxt_db_insert_internal(database, record,
	                              flags | VXT_DB_INSERT_FILE, 0);
	return ret;
}


/*
 *
 * vxt_db_dump_list:
 *
 * vxt_db_dump_list is an externalized function but also doubles as
 * helper function for vxt_db_dump_guest.
 * It is called to free the resources associated with a partially
 * or fully assembled list of records reflection the connection
 * arrangements of a guest/endpoint.
 *
 * vxt_db_dump_list is called by external elements that call vxt_db_dump_list
 * to free the resources when they are done with the list.  If the contents
 * of a file list are transferred to another memory resource, this resource
 * should be handled separately from the vxt_db_dump_list method.  i.e.
 * flattened resources sent between nodes should be freed by the complimentary
 * means to their allocation, not through a vxt_db_dump_list_call.
 *
 *
 *	Returns:  NONE
 *
 */

void
vxt_db_dump_list(vxt_db_rec_list_t   *file_list)
{
	vxt_db_rec_list_t *file_rec;

	while (file_list != NULL) {
		file_rec = file_list;
		file_list = file_list->next;
		vxt_kfree(file_rec);
	}

	return;
}

/*
 *
 * vxt_db_create:
 *
 * create a local vxt authorization database, returns
 * a pointer to the new database when successful.
 *
 * vxt_db_create will open a database file with the name given
 * in a calling parameter.  If the file already exists, the entries
 * will be read out and placed in the live hash lookup tables.  If
 * not a file will be created and initialized.
 *
 * The database file is synchronized on each create, edit, and delete
 * transaction so that it may be recovered in the event of a data
 * base crash.
 *
 * NOTE: The directory is implicit:   /opt/VRTSvxt
 *
 * 
 */

void *
vxt_db_create(char *db_file_name)
{
	char db_name[VXT_DB_NAME_MAX+VXT_DB_PATH_LENGTH];
	char vxt_domain_name[30];
	vxt_db_header_t file_header;
	vxt_db_file_rec_t inrec;
	struct file       *db_record_file;
	vxt_auth_database_t *database;
	int32_t  read_cnt;
	uint32_t banner_offset;
	uint64_t file_offset;
	uint64_t file_size;
	uint32_t flags;
	uint32_t i;
	int ret;


	/*
	 * Allocate a new database record
	 */
	database = vxt_kmalloc(sizeof(vxt_auth_database_t), VXT_KMEM_KERNEL);
	if (database == NULL) {
		return NULL;
	}
	database->free_file_rec_list = NULL;
	for(i = 0; i < VXT_HASH_TABLE_SIZE; i++) {
		database->hash_table[i].next = &(database->hash_table[i]);
		database->hash_table[i].prev = &(database->hash_table[i]);
		database->hash_table[i].parent = NULL;
	}
	database->db_file_name[0] = 0;

	/*
	 * Construct the database file name and path
	 */
	strncpy(db_name, VXT_DB_PATH,  VXT_DB_NAME_MAX);
	strncpy(&db_name[strlen(VXT_DB_PATH)],
	        db_file_name, 
	        VXT_DB_NAME_MAX);
	db_name[VXT_DB_NAME_MAX + VXT_DB_PATH_LENGTH - 1] = 0;
	strncpy(database->db_file_name, db_name, VXT_DB_NAME_MAX);

	UMI_LOG(17, VXTPRINT_DEBUG_LEVEL, "open file %s\n", db_name);

	db_record_file = vxt_file_open(db_name,
	                               VXT_FILE_CREATE | 
	                               VXT_FILE_SYNC | VXT_FILE_RW);

	if (db_record_file == NULL) {
		UMI_LOG(18, VXTPRINT_PRODUCT_LEVEL, 
		        " open file failed %s\n", db_name);
		vxt_kfree(database);
		return NULL;
	}
	database->fds = db_record_file;

	/*
	 * Now check if the file is empty, if not read its
	 * contents.
	 */
	file_size = vxt_file_size(db_record_file);
	if (file_size < 0) {
		UMI_LOG(19, VXTPRINT_PRODUCT_LEVEL,
		        " stat on %s failed\n", db_name);
		vxt_kfree(database);
		vxt_file_close(db_record_file);
		return NULL;
	}
	if (file_size) {
		banner_offset = 0;
		file_offset = 0;
		UMI_LOG(20, VXTPRINT_BETA_LEVEL, " file %s, alread exists, "
		       "file size 0x%llx\n", db_name, file_size);
		if (file_size < sizeof(vxt_db_header_t)) {
			UMI_LOG(21, VXTPRINT_PRODUCT_LEVEL, 
			        ": file %s, malformed\n", db_name);
			vxt_kfree(database);
			vxt_file_close(db_record_file);
			return NULL;
		}
		read_cnt = vxt_file_read(db_record_file, 
		                         &file_header,
		                         sizeof(vxt_db_header_t),
		                         &file_offset);
/*
		banner_offset = read_cnt;
		while (banner_offset < sizeof(vxt_db_header_t)) {
			read_cnt =
			   vxt_file_read(db_record_file,
			        ((void *)&file_header) + banner_offset,
		                sizeof(vxt_db_header_t) - banner_offset,
			        &file_offset);
			if ((read_cnt) < 0) {
				UMI_LOG(22, VXTPRINT_PRODUCT_LEVEL, 
				        " file %s, malformed\n",
				        db_name);
				vxt_kfree(database);
				vxt_file_close(db_record_file);
				return NULL;
			}
		}
		ASSERT(file_offset == sizeof(vxt_db_header_t));
*/
		UMI_LOG(23, VXTPRINT_DEBUG_LEVEL, 
		        " retrieved file header, "
		        "header_size %d, header name: %s\n",
		        read_cnt, file_header.banner);
		/*
		 * We have a pre-existing file and a valid header
		 * Now retrieve the database records.
		 * If we find that one or the other endpoint is
		 * local, push a hash with the record into our
		 * hash list.
		 */
		while (TRUE) {
			uint64_t tmp_offset;
			tmp_offset = file_offset;
			read_cnt = 
			   vxt_file_read(db_record_file, &inrec, 
			                 sizeof(vxt_db_file_rec_t),
			                 &tmp_offset);
			if (read_cnt <= 0) {
				break;
			}
			UMI_LOG(24, VXTPRINT_BETA_LEVEL,
	        	        "found resident endpoint record: client %s,"
	        	        " remote %s, device %s\n",
	        	        inrec.client, inrec.server, inrec.dev_name);
			if (inrec.priv.valid == 0) {
				vxt_db_free_file_rec_entry_t *free_list_entry;
				UMI_LOG(25, VXTPRINT_DEBUG_LEVEL,
				        "on file record not valid\n");
				/*
				 * Throw an extra entry into the free
				 * file entries field.
				 */
				free_list_entry = 
				   vxt_kmalloc(
				      sizeof(vxt_db_free_file_rec_entry_t),
				      VXT_KMEM_KERNEL);
				if (free_list_entry == NULL) {
					vxt_kfree(database);
					vxt_file_close(db_record_file);
					return NULL;
				}
				free_list_entry->offset = file_offset;
				free_list_entry->next =
				   database->free_file_rec_list;
				database->free_file_rec_list = free_list_entry;
				file_offset += sizeof(vxt_db_file_rec_t);
				continue;
			}
			/*
			 * Check to see if the endpoint is the abstract
			 * common default (Dom0).  i.e. the hub of our
			 * hub and spoke topology if not, construct the
			 * query string for client UUID and see if the VM 
			 * is in the Xen Database
			 */
			if (!strncmp(inrec.client, 
			    VXTDB_DOM0_ENDPOINT, 40)) {
				flags = VXT_DB_INSERT_CLIENT;
			} else {
				ret = vxt_uuid_to_local_dom(inrec.client,
				                            vxt_domain_name);
				if (ret == 1 ) {
					flags = VXT_DB_INSERT_CLIENT;
				} else {
					flags = 0;
				}
			}
			if (!strncmp(inrec.server, 
			             VXTDB_DOM0_ENDPOINT, 40)) {
				flags |= VXT_DB_INSERT_SERVER;
			} else {
				ret = vxt_uuid_to_local_dom(inrec.server,
				                            vxt_domain_name);
				if (ret == 1 ) {
					flags |= VXT_DB_INSERT_SERVER;
				}
			}
			if (flags) {
				/* Device is local, push it into the hash */
				UMI_LOG(26, VXTPRINT_BETA_LEVEL, 
				        "create record, client %s, "
				        "device %s, remote %s, "
				        "compare length %lld, domain %s\n",
				        inrec.client, inrec.server,
				        inrec.dev_name, inrec.class_name_length,
				        vxt_domain_name);
				ret =
				   vxt_db_insert_internal(database,
				                          &inrec, flags,
				                          file_offset);
        		}
			file_offset += sizeof(vxt_db_file_rec_t);
		}
		database->database_end = file_offset;
	} else {
		file_offset = 0;
		/*
		 * Write the Database file header into the 
		 * database file.
		 */
		UMI_LOG(27, VXTPRINT_BETA_LEVEL, " Write the file header\n");
		strncpy(file_header.banner, VXT_BANNER, VXT_DB_NAME_MAX);
		file_header.version = VXT_DB_VERSION;
		ret = vxt_file_write(db_record_file, (void *)&file_header,
		                     sizeof(file_header), &file_offset);
		UMI_LOG(28, VXTPRINT_DEBUG_LEVEL, " bytes written: %d\n", ret);
		if (ret < 0) {
			UMI_LOG(29, VXTPRINT_PRODUCT_LEVEL,
			        " write to db file failed\n");
			vxt_kfree(database);
			vxt_file_close(db_record_file);
			return NULL;
		}
		database->database_end = sizeof(file_header);
		
	}

	return database;
}


/*
 *
 * vxt_db_lookup_client:
 *
 * vxt_db_lookup_client will hash for a particular requestor and a name.
 * vxt_db_lookup_client is meant to be used only by client requestors
 * This is because some servers support the re-use of a service
 * name.  i.e. the service name will only show up once in each
 * of the client record sets but will show up many times in a 
 * heavily used server record set.
 *
 * The record buffer is supplied by the caller
 *
 *
 * 
 */

int
vxt_db_lookup_client(void *database, char *requester, char *name,
                     vxt_db_lookup_rec_t *record)
{
	vxt_db_rec_t        *db_rec;
	vxt_auth_database_t *db = (vxt_auth_database_t *)database;
	char                vxt_domain_name[30];
	vxt_dev_rec_t       *device;
	vxt_db_hash_entry_t *rec_hash;
	uint32_t            is_client;
	int                 ret;



	UMI_LOG(30, VXTPRINT_PROFILE_LEVEL, "called, client %s\n", requester);
	rec_hash = vxt_db_uuid_lookup(db->hash_table, 
	                              requester, NULL, &is_client);
	if (rec_hash == NULL) {
		UMI_LOG(31, VXTPRINT_BETA_LEVEL, 
		        "record not found on %s\n", requester);
		return VXT_FAIL;
	}
	db_rec = rec_hash->parent;
	while (db_rec != NULL) {
		/*
		 * The vxt_db_uuid_lookup could return a server
		 * match.  However, the hash lookup was on the client
		 * and if there is a valid record it will be in the
		 * chain.
		 * Check only the client side and check all the 
		 * remaining records in the chain.  We do not
		 * test for the server in this routine.
		 */
		if (is_client) {
			if(!strncmp(db_rec->client, requester,  40)) {
				break;
			}
			db_rec = db_rec->client_list.next->parent;
		} else {
			if(!strncmp(db_rec->server, requester,  40)) {
				break;
			}
			db_rec = db_rec->server_list.next->parent;
		}
	}
	if (db_rec == NULL) {
		return VXT_FAIL;
	}
	device = db_rec->devices;
	while (device != NULL) {
		UMI_LOG(32, VXTPRINT_DEBUG_LEVEL,
		        "devices on record %p, %s, compare string "
			"length %lld\n",
		         db_rec, device->dev_name, device->class_name_length);
		/*
		 * Note: The VxT authorization database permits
		 * class permissions.  The class is represented
		 * as a prefix with the instance usually being
		 * presented as a numerical suffix.  To effect this
		 * a value is kept in the record which indicates
		 * the amount of the device name to compare, zero
		 * connotes the whole string.
		 */
		if(device->class_name_length == 0) {
			if(!strncmp(device->dev_name, name, MAX_VXT_UNAME)) {
				break;
			}
		} else {
			if(!strncmp(device->dev_name, 
			   name, device->class_name_length)) {
				break;
			}
			
		}
		device = device->next;
	}
	if (device == NULL) {
		return VXT_FAIL;
	}

	record->priv.direct = db_rec->priv.direct;
	record->priv.reserved = 0;
	strncpy(record->client, requester, 40);
	strncpy(record->server, db_rec->server, 40);
	strncpy(record->dev_name, name, MAX_VXT_UNAME);
	record->label = device->label;

	/*
	 * Check for the domain of the client UUID
	 * if the UUID is the default domain string 
	 * then this is a default Dom0 endpoint.  If not,
	 * construct the query string for client UUID
	 * and see if the VM is in the Xen Database
	 */
	if (!strncmp(record->client, VXTDB_DOM0_ENDPOINT, 40)) {
		record->server_dom = 0;
		vxt_domain_name[0] = 0;
	} else {
		ret = vxt_uuid_to_local_dom(record->client,
		                            vxt_domain_name);
		if (ret != 1 ) {
			/* Endpoint is remote, return null domain field */
			record->client_dom = -1;
       		} else {
			/*
			 * Endpoint is local, return the domain field
			 * (remove the Domain- in the source string)
			 */
			record->client_dom = 
			   vxt_convert_str_to_ulonglong(&vxt_domain_name[7],
			                                NULL, 0); 
		}
	}
	UMI_LOG(33, VXTPRINT_BETA_LEVEL, 
	        "remote endpoint record, "
	        "client %s, remote %s, device %s "
	        "domain %s, client_dom 0x%llx\n",
	        record->client, record->server,
	        record->dev_name, vxt_domain_name, record->client_dom);
	/*
	 * Check for the domain of the server UUID
	 * if the UUID is the default domain string 
	 * then this is a default Dom0 endpoint.  If not,
	 * construct the query string for server UUID
	 * and see if the VM is in the Xen Database
	 */
	if (!strncmp(record->server, VXTDB_DOM0_ENDPOINT, 40)) {
		record->server_dom = 0;
		vxt_domain_name[0] = 0;
	} else {
		ret = vxt_uuid_to_local_dom(record->server,
		                            vxt_domain_name);
		if (ret == 1 ) {
			/*
			 * Server is local, return the domain
			 * (remove the Domain- in the source string)
			 */
			record->server_dom =
			   vxt_convert_str_to_ulonglong(&vxt_domain_name[7],
			                                NULL, 0); 
	       	} else {
			/* Server is remote, return null domain field */
			record->server_dom = -1;
		}
	}

	UMI_LOG(34, VXTPRINT_BETA_LEVEL,
	        "vxt_db_lookup_client: resident endpoint record, client %s,"
	        " remote %s, device %s, label 0x%llx, domain %s, "
		" server_dom# %lld\n",
	        record->client, record->server, record->dev_name, 
	        device->label, vxt_domain_name, record->server_dom);

	return VXT_SUCCESS;
}


/*
 *
 * vxt_db_edit:
 *
 * vxt_db_edit allows the caller to update an existing database
 * record.  The new data is taken from the database record.
 *
 * Note all edits must provide both the requester and name fields
 * for lookup.
 *
 *
 * Returns:
 *		VXT_SUCCESS => Upon success
 *		VXT_PARM:  Database record or device not found
 *
 */

int
vxt_db_edit(void *database, char *requester, char *remote,
            char *dev_name, void *update, uint64_t label)
{
	vxt_auth_database_t *db = (vxt_auth_database_t *)database;
	vxt_db_rec_t        *db_rec;
	vxt_db_hash_entry_t *rec_hash;
	uint32_t            is_client;
	uint32_t            flags;
	vxt_dev_rec_t       *device;
	
	flags = *(uint32_t *)update;

	rec_hash = vxt_db_uuid_lookup(db->hash_table, requester,
	                              remote, &is_client);
	if (rec_hash == NULL) {
		rec_hash = vxt_db_uuid_lookup(db->hash_table, remote,
		                              requester, &is_client);
	}
	if (rec_hash != NULL) {
		db_rec = rec_hash->parent;
	} else {
		db_rec = NULL;
	}

	if (db_rec == NULL) {
		return VXT_PARM;
	}

	if (dev_name == NULL) {
		if (flags & VXT_DEV_DIRECT_CONNECT) {
			db_rec->priv.direct = 1;
		} else {
			db_rec->priv.direct = 0;
		}
		return VXT_SUCCESS;
	}


	/*
	 * Find the device
	 */

	device = db_rec->devices;
	while (device != NULL) {
		UMI_LOG(139, VXTPRINT_DEBUG_LEVEL,
		        "devices on record %p, %s, compare string "
			"length %lld\n",
		         db_rec, device->dev_name, device->class_name_length);
		/*
		 * Note:  while we do support classes of devices
		 * that share a name prefix, we do not honor
		 * it here.  If the caller wishes to change
		 * a device record, it must be done on a 
		 * record by record basis.
		 */
		if(!strncmp(device->dev_name, dev_name, MAX_VXT_UNAME)) {
			break;
		}
			
		device = device->next;
	}

	if (device == NULL) {
		return VXT_PARM;
	}

	if (flags & VXT_DEV_DIRECT_CONNECT) {
		db_rec->priv.direct = 1;
	} else {
		db_rec->priv.direct = 0;
	}

	device->label = label;

	return VXT_SUCCESS;
}


/*
 *
 * vxt_db_delete_record:
 *
 * Lookup the client/server pair in the vxt auth database.  If a record
 * is found.  Delete the device entry.  If the device entry is null
 * treat it as a wild card and delete all entries for the client/server
 * pair.
 *
 * If the device entry is the last for the client server pair or if
 * we are deleting all of the client server devices, all outstanding
 * hashes of the in-memory database record are removed and the
 * on-disk file record are deleted.
 *
 * Locks:
 *		Request lock held on invocation 
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon successful invocation
 *
 *		VXT_PARM - No db records are found for the client
 *		           server pair
 */

int
vxt_db_delete_record(void *database, char *client, char *server, char *device)
{
	vxt_auth_database_t *db = (vxt_auth_database_t *)database;
	vxt_dev_rec_t       *dev_rec, *trailer;
	vxt_db_hash_entry_t *rec_hash;
	vxt_db_rec_t        *db_rec;
	uint32_t            is_client;

	UMI_LOG(35, VXTPRINT_PROFILE_LEVEL, "called\n");
	rec_hash = vxt_db_uuid_lookup(db->hash_table, client,
	                              server, &is_client);
	if (rec_hash == NULL) {
		rec_hash = vxt_db_uuid_lookup(db->hash_table, server,
		                              client, &is_client);
	}
	if (rec_hash == NULL) {
		UMI_LOG(36, VXTPRINT_BETA_LEVEL,
		        "no in-memory record was found for client %s, "
		        "server %s\n", client, server);
		return VXT_PARM;
	}
	db_rec = rec_hash->parent;
	/*
	 * Now that we have the in-memory database record, remove the
	 * device.  If the device field is null remove all of the attached
	 * devices and remove the underlying in-memory record.
	 */
	if (device != NULL) {
		dev_rec = db_rec->devices;
		trailer = dev_rec;
		while (dev_rec != NULL) {
			if(!strncmp(dev_rec->dev_name, 
			            device, MAX_VXT_UNAME)) {
				if (dev_rec == db_rec->devices) {
					db_rec->devices =
					   db_rec->devices->next;
				} else {
					trailer->next = dev_rec->next;
				}
				vxt_db_destroy_dev_rec(database, dev_rec);
				db_rec->device_count--;
				break;
			}
			trailer = dev_rec;
			dev_rec = dev_rec->next;
		}
		
	} else {
		/* 
		 * Remove all the devices and the db_rec
		 */
		dev_rec = db_rec->devices;
		while (db_rec->devices != NULL) {
			db_rec->devices =
			   db_rec->devices->next;
			vxt_db_destroy_dev_rec(database, dev_rec);
			dev_rec = db_rec->devices;
		}
		db_rec->device_count = 0;
	}

	if (db_rec->devices == NULL) {
		/*
		 * Note: we initialize next and prev
		 * to point at the base element.  
		 * Thus, if the element is not on a hash
		 * list the following lines are a NOOP
		 */
		UMI_LOG(37, VXTPRINT_BETA_LEVEL, "remove in-memory db-rec\n");
		        
		db_rec->client_list.next->prev = db_rec->client_list.prev;
		db_rec->client_list.prev->next = db_rec->client_list.next;
		db_rec->server_list.next->prev = db_rec->server_list.prev;
		db_rec->server_list.prev->next = db_rec->server_list.next;
		vxt_kfree(db_rec);
	}
	 
	return VXT_SUCCESS;
}


/*
 * 
 * vxt_db_dump_guest_local:
 *
 * Returns a linked list of file records, recording the 
 * in-memory hash entries for a virtual machine.
 * The call makes no change in either the in-memory lookup
 * hash or the on-disk database.
 *
 *
 */

vxt_db_rec_list_t *
vxt_db_dump_guest_local(void *database, char *guest)
{
	vxt_auth_database_t *db = (vxt_auth_database_t *)database;
	vxt_db_rec_list_t   *file_rec, *file_list;
	vxt_dev_rec_t       *dev_rec;
	vxt_db_hash_entry_t *rec_hash;
	vxt_db_rec_t        *db_rec;
	uint32_t            is_client;

	UMI_LOG(38, VXTPRINT_PROFILE_LEVEL, "called\n");

	file_list = NULL;
	rec_hash = vxt_db_uuid_lookup(db->hash_table, guest,
	                              NULL, &is_client);
	while (rec_hash != NULL) {
		db_rec = rec_hash->parent;
		dev_rec = db_rec->devices;
		while (dev_rec != NULL) {
			file_rec =
			   vxt_kmalloc(sizeof(vxt_db_rec_list_t),
			               VXT_KMEM_KERNEL);
			if (file_rec == NULL) {
				vxt_db_dump_list(file_list);
				return NULL;
			}
			file_rec->record.priv.valid = 1;
			file_rec->record.priv.direct = db_rec->priv.direct;
			file_rec->record.label = dev_rec->label;
			strncpy(file_rec->record.client, db_rec->client, 40);
			strncpy(file_rec->record.server, db_rec->server, 40);
			strncpy(file_rec->record.dev_name,
			        dev_rec->dev_name, MAX_VXT_UNAME);
			file_rec->record.class_name_length =
			   dev_rec->class_name_length;
			file_rec->next = file_list;
			file_list = file_rec;
			dev_rec = dev_rec->next;
		}
		/*
		 * Lookup up the next record in which the guest
		 * is one of the endpoints.  The list order is
		 * arbitrary, but guaranteed to list all of the
		 * suitable records and list them only once.
		 */
		rec_hash = vxt_db_lookup_next(db->hash_table, guest, rec_hash);
	}
	return file_list;
}


/*
 *
 * vxt_db_guest_dev_count:
 *
 * vxt_db_guest_dev_count accesses the in-memory data base record structures
 * for the guest uuid provided and counts up the number of devices that
 * are associated with them.  When a dump of the device records is made
 * each device that counts the guest as an endpoint will account for a record
 * This routine counts up the number of devices in the in-memory records
 * To allow the caller to provide an appropriate sized buffer.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon successful invocation
 *
 */

int
vxt_db_guest_dev_count(void *database, char *guest, uint32_t *record_count)
{
	vxt_auth_database_t *db = (vxt_auth_database_t *)database;
	vxt_db_hash_entry_t *rec_hash;
	vxt_db_rec_t        *db_rec;
	uint32_t            is_client;

	UMI_LOG(39, VXTPRINT_PROFILE_LEVEL, "called\n");
	*record_count = 0;
	rec_hash = vxt_db_uuid_lookup(db->hash_table, guest,
	                              NULL, &is_client);
	while (rec_hash != NULL) {
		db_rec = rec_hash->parent;
		*record_count+=db_rec->device_count;
		/*
		 * Lookup up the next record in which the guest
		 * is one of the endpoints.  The list order is
		 * arbitrary, but guaranteed to list all of the
		 * suitable records and list them only once.
		 */
		rec_hash = vxt_db_lookup_next(db->hash_table, guest, rec_hash);
	}
	return VXT_SUCCESS;
}


/*
 * 
 * vxt_db_dump_guest:
 *
 * Pushes device records, into a piece of memory provided by the caller.
 * The size as a count of records and the pointer to the memory are passed.
 * vxt_db_dump_guest pushes as many records as possible into the space and
 * sends back a flag indicating whether or not the space was sufficient to
 * record all of the records.  The size variable is also updated to record
 * the number of records written.  
 *
 * The records chronicle the in-memory hash entries for a target guest on 
 * the virtual machine.  The call makes no change in either the in-memory
 * lookup hash or the on-disk database.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon successful invocation
 *
 *
 */

int
vxt_db_dump_guest(void *database, char *guest,
                  void *buffer, uint32_t *size, uint32_t *overrun)
{
	vxt_auth_database_t *db = (vxt_auth_database_t *)database;
	vxt_db_file_rec_t   *file_rec;
	vxt_db_file_rec_t   *caller_rec_array = (vxt_db_file_rec_t *)buffer;
	vxt_dev_rec_t       *dev_rec;
	vxt_db_hash_entry_t *rec_hash;
	vxt_db_rec_t        *db_rec;
	uint32_t            is_client;
	uint32_t            rec_ele;

	UMI_LOG(40, VXTPRINT_PROFILE_LEVEL, "called\n");

	rec_ele = 0;
	*overrun = 0;
	rec_hash = vxt_db_uuid_lookup(db->hash_table, guest,
	                              NULL, &is_client);
	while (rec_hash != NULL) {
		db_rec = rec_hash->parent;
		dev_rec = db_rec->devices;
		while (dev_rec != NULL) {
			if (rec_ele >= *size) {
				*overrun = 1;
				return VXT_SUCCESS;
			}
			file_rec = &caller_rec_array[rec_ele];
			rec_ele++;
			file_rec->priv.valid = 1;
			file_rec->priv.direct = db_rec->priv.direct;
			file_rec->label = dev_rec->label;
			strncpy(file_rec->client, db_rec->client, 40);
			strncpy(file_rec->server, db_rec->server, 40);
			strncpy(file_rec->dev_name,
			        dev_rec->dev_name, MAX_VXT_UNAME);
			file_rec->class_name_length =
			   dev_rec->class_name_length;
			dev_rec = dev_rec->next;
		}
		/*
		 * Lookup up the next record in which the guest
		 * is one of the endpoints.  The list order is
		 * arbitrary, but guaranteed to list all of the
		 * suitable records and list them only once.
		 */
		rec_hash = vxt_db_lookup_next(db->hash_table, guest, rec_hash);
	} 
	*size = rec_ele;
	return VXT_SUCCESS;
}


/*
 *
 * vxt_db_delete_guest:
 *
 * Remove all the hash elements associated with a particular guest
 * Remove the associated records if the other endpoint is not hashed
 * or the migrate parameter is FALSE.
 *
 * In the case where migrate is TRUE, the targeted guest records
 * are to be moved to another node.  In this case, the local endpoint
 * will still be able to connect using its hashed record.  In this case
 * routing to the non-local remote will be handled via device special
 * case routine.
 *
 * Locks:
 *		Request lock held on invocation 
 *
 *
 * Returns:
 *
 */

int
vxt_db_delete_guest(void *database, char *endpoint, uint32_t migrate)
{
	vxt_auth_database_t *db = (vxt_auth_database_t *)database;
	vxt_dev_rec_t       *dev_rec;
	vxt_db_hash_entry_t *rec_hash;
	vxt_db_rec_t        *db_rec;
	uint32_t            is_client;

	UMI_LOG(41, VXTPRINT_PROFILE_LEVEL, "called\n");
	while(TRUE) {
		rec_hash = vxt_db_uuid_lookup(db->hash_table, endpoint,
		                              NULL, &is_client);
		if (rec_hash == NULL) {
			break;
		}
		db_rec = rec_hash->parent;
		/*
		 * Remove the in-memory hash record from the
		 * targeted lists.  If the migrate flag is not on
		 * remove the record from both lists.  If it is
		 * on, just remove the record from the list
		 * associated with the targeted endpoint.
		 */
		if (!strncmp(db_rec->client, endpoint, 40)) {
			/*
			 * Note: we initialize next and prev
			 * to point at the base element.  
			 * Thus, if the element is not on a hash
			 * list the following lines are a NOOP
			 */
			UMI_LOG(42, VXTPRINT_PROFILE_LEVEL,
			        "delete client hash for record, "
			        "client %s, server %s\n",
			        db_rec->client, db_rec->server);
			db_rec->client_list.next->prev = 
			   db_rec->client_list.prev;
			db_rec->client_list.prev->next =
			   db_rec->client_list.next;
			/*
			 * We may continue to hold this db_rec
			 * so it is vital that we re-initialize
			 * our internal record pointers
			 */
			db_rec->client_list.next = &(db_rec->client_list);
			db_rec->client_list.prev = &(db_rec->client_list);
			/*
			 * Remove the other hash if we are doing
			 * a general record removal or if this is
			 * a migrate and we have a default Dom0
			 * hub (of hub/spoke) style record hash
			 */
			if ((!migrate) ||
			    (!strncmp(db_rec->server,
			              VXTDB_DOM0_ENDPOINT, 40))) {
				UMI_LOG(43, VXTPRINT_PROFILE_LEVEL,
				       	 "delete server hash for record, "
				        "client %s, server %s\n",
				        db_rec->client, db_rec->server);
				db_rec->server_list.next->prev =
				   db_rec->server_list.prev;
				db_rec->server_list.prev->next =
				   db_rec->server_list.next;
				db_rec->server_list.next =
				   &(db_rec->server_list);
				db_rec->server_list.prev =
				   &(db_rec->server_list);
			}
		} else {
			db_rec->server_list.next->prev =
			   db_rec->server_list.prev;
			db_rec->server_list.prev->next =
			   db_rec->server_list.next;
			db_rec->server_list.next =
			   &(db_rec->server_list);
			db_rec->server_list.prev =
			   &(db_rec->server_list);
			/*
			 * Remove the other hash if we are doing
			 * a general record removal or if this is
			 * a migrate and we have a default Dom0
			 * hub (of hub/spoke) style record hash
			 */
			if ((!migrate) || 
			    (!strncmp(db_rec->client,
			              VXTDB_DOM0_ENDPOINT, 40))) {
				UMI_LOG(44, VXTPRINT_PROFILE_LEVEL,
				       	 "delete server hash for record, "
				        "client %s, server %s\n",
				        db_rec->client, db_rec->server);
				db_rec->client_list.next->prev =
				   db_rec->client_list.prev;
				db_rec->client_list.prev->next =
				   db_rec->client_list.next;
				/*
				 * We may continue to hold this db_rec
				 * so it is vital that we re-initialize
				 * our internal record pointers
				 */
				db_rec->client_list.next = 
				   &(db_rec->client_list);
				db_rec->client_list.prev =
				   &(db_rec->client_list);
			}
		}
		if ((db_rec->client_list.next != &db_rec->client_list)
		    || (db_rec->server_list.next != &db_rec->server_list)) {
			/* 
			 * If we are still on a list, don't remove the
			 * record from the database.
			 */
			continue;
		}
		UMI_LOG(45, VXTPRINT_PROFILE_LEVEL,
		        "delete in-memory record\n");
		/* 
		 * Remove all the devices and the db_rec
		 */
		dev_rec = db_rec->devices;
		while (db_rec->devices != NULL) {
			db_rec->devices =
			   db_rec->devices->next;
			vxt_db_destroy_dev_rec(database, dev_rec);
			dev_rec = db_rec->devices;
		}
		vxt_kfree(db_rec);
	}

	return VXT_SUCCESS;

}


/*
 *
 * vxt_db_restore_guest_local:
 *
 * vxt_db_restore_guest takes a list of vxt_db file records and
 * imports them into the targeted vxt authorization database.
 * vxt_db_restore_guest does not free the resources associated with
 * the file list.  This provides for the maximal flexibility with respect
 * to the memory medium used.
 *
 * vxt_db_restore_guest is a migration operation.  Because of this
 * it only restores the hash records associated with the targeted
 * endpoint/guest.
 *
 * Note:  If a failure occurs, any preliminary record inserts are
 * removed, leaving the database in the condition it was in before
 * invocation.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon Success
 *		VXT_FAIL - record insert failed
 *
 */

int
vxt_db_restore_guest_local(void *database, 
                           char *guest_uuid, vxt_db_rec_list_t *file_list)
{
	vxt_db_rec_list_t   *file_rec;
	uint32_t            flags;

	while (file_list != NULL) {
		file_rec = file_list;
		file_list = file_list->next;
		/*
		 * This is a migration action, therefore we will only
		 * be adding the guest_uuid side of the hash.  The guest
		 * may be a server or a client, and may alternate with
		 * the particular device entry.
		 */
		if (strncmp(file_rec->record.client, guest_uuid, 40)) {
			flags = VXT_DB_INSERT_SERVER;
			if (!strncmp(file_rec->record.client,
			    VXTDB_DOM0_ENDPOINT, 40)) {
				flags = VXT_DB_INSERT_CLIENT;
			}
		} else {
			flags = VXT_DB_INSERT_CLIENT;
			if (!strncmp(file_rec->record.server,
			    VXTDB_DOM0_ENDPOINT, 40)) {
				flags = VXT_DB_INSERT_SERVER;
			}
		}
		flags |= VXT_DB_INSERT_FILE;
		if (vxt_db_insert(database, 
		    &file_rec->record, flags) != VXT_SUCCESS) {
			vxt_db_delete_guest(database, guest_uuid, 1);
			return VXT_FAIL;
		}
	}

	return VXT_SUCCESS;
}


/*
 *
 * vxt_db_restore_guest:
 *
 * vxt_db_restore_guest takes an array of vxt_db file records and
 * imports them into the targeted vxt authorization database.
 * vxt_db_restore_guest does not free the resources associated with
 * the file list.  This provides for the maximal flexibility with respect
 * to the memory medium used.
 *
 * vxt_db_restore_guest is a migration operation.  Because of this
 * it only restores the hash records associated with the targeted
 * endpoint/guest.  There is an exception to this.  If it is determined
 * that the remote endpoint is the default Dom0 record, both hashes
 * are restored as the migration has given the guest a new hub.  (hub/spoke
 * routing and config layout).
 *
 * Note:  If a failure occurs, any preliminary record inserts are
 * removed, leaving the database in the condition it was in before
 * invocation.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon Success
 *		VXT_FAIL - record insert failed
 *
 */

int
vxt_db_restore_guest(void *database, char *guest_uuid,
                     vxt_db_file_rec_t *record_array,
                     uint32_t record_count, uint32_t migrate)
{
	vxt_db_file_rec_t  *file_rec;
	uint32_t            flags;
	uint32_t            i;

	UMI_LOG(46, VXTPRINT_PROFILE_LEVEL, "called\n");

	for (i = 0; i < record_count; i++) {
		file_rec = &record_array[i];
		/*
		 * This is a migration action, therefore we will only
		 * be adding the guest_uuid side of the hash.  The guest
		 * may be a server or a client, and may alternate with
		 * the particular device entry.
		 */
		if (strncmp(file_rec->client, guest_uuid, 40)) {
			flags = VXT_DB_INSERT_SERVER;
			if (!strncmp(file_rec->client,
			    VXTDB_DOM0_ENDPOINT, 40)) {
				flags |= VXT_DB_INSERT_CLIENT;
			}
		} else {
			flags = VXT_DB_INSERT_CLIENT;
			if (!strncmp(file_rec->server,
			    VXTDB_DOM0_ENDPOINT, 40)) {
				flags |= VXT_DB_INSERT_SERVER;
			}
		}
		flags |= VXT_DB_INSERT_FILE;
		if (vxt_db_insert(database, 
		    file_rec, flags) != VXT_SUCCESS) {
			vxt_db_delete_guest(database, guest_uuid, 1);
			return VXT_FAIL;
		}
	}

	return VXT_SUCCESS;
}


/*
 *
 * vxt_db_delete:
 *
 * vxt_db_delete will delete all in-memory hash records associated
 * with the targeted database.  The delete_db_file parameter is 
 * consulted to determine whether to just close the on-disk 
 * database file or remove it.
 *
 * vxt_db_delete must be called on module unload to properly free
 * all in-memory allocations.
 *
 *
 * Locks:
 *		Request lock held on invocation 
 *
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon successful invocation
 *
 */

int
vxt_db_delete(void *database, uint32_t delete_db_file)
{
	vxt_auth_database_t          *db = (vxt_auth_database_t *)database;
	vxt_db_hash_entry_t          *hash_table;
	vxt_db_hash_entry_t          *hash_ele;
	vxt_db_rec_t                 *db_rec;
	vxt_dev_rec_t                *dev_rec;
	vxt_db_free_file_rec_entry_t *free_rec;
	int                          i;

	UMI_LOG(47, VXTPRINT_PROFILE_LEVEL, "called\n");
	hash_table = db->hash_table;
	/*
	 * Free all of the in-memory hash table entries
	 */
	for(i = 0; i < VXT_HASH_TABLE_SIZE; i++) {
		while(hash_table[i].next != &(hash_table[i])) {
			hash_ele = hash_table[i].next;
			db_rec = hash_table[i].next->parent;	
			hash_ele->next->prev = hash_ele->prev;
			hash_ele->prev->next = hash_ele->next;
			UMI_LOG(48, VXTPRINT_DEBUG_LEVEL, 
			        "found hash entry: array index 0x%x, "
			        "client %s, server %s\n", 
			        i, db_rec->client, db_rec->server);
			/*
			 * Now remove the alternate hash
			 */
			if (hash_ele == &db_rec->client_list) {
				hash_ele = &db_rec->server_list;
			} else {
				hash_ele = &db_rec->client_list;
			}
			if (hash_ele->next != hash_ele) {
				/* 
				 * If it is on a list
				 */
				hash_ele->next->prev = hash_ele->prev;
				hash_ele->prev->next = hash_ele->next;
			}
			/*
			 * Here we free the list of device records
			 * We don't call the vxt_db_destroy_dev_rec
			 * routine because we will either preserve
			 * the on-disk database intact or remove it 
			 * completely.  Also, we do not need
			 * to remove records from the in-memory
			 * hash one by one.
			 */
			dev_rec = db_rec->devices;
			while (db_rec->devices != NULL) {
				db_rec->devices =
				   db_rec->devices->next;
				UMI_LOG(49, VXTPRINT_DEBUG_LEVEL, 
			        "freeing device record %s\n",
				dev_rec->dev_name);
				vxt_kfree(dev_rec);
				dev_rec = db_rec->devices;
			}
			vxt_kfree(db_rec);
		}
	}
	/*
	 * Free all of the elements in the free file records list
	 */
	free_rec = db->free_file_rec_list;
	while (db->free_file_rec_list != NULL) {
		db->free_file_rec_list = free_rec->next;
		vxt_kfree(free_rec);
		free_rec = db->free_file_rec_list;
	}
	if (delete_db_file) {
		vxt_file_remove(db->fds);
	} else {
		vxt_file_close(db->fds);
	}
	vxt_kfree(db);

	return VXT_SUCCESS;
}


/*
 *
 * vxtdb_module_init:
 *
 * vxtdb_module_init is called at the time of module load.  vxt_module_init
 * will create or open the existing database file, initialize global locks, 
 * and create an external file for database actions from user space.
 * vxtdb_module will also export its database handling interfaces for use
 * in the kernel.
 *
 * Locks:
 *		No specific lock required, access to the
 *		database is not possible until return from 
 *		vxtdb_module_init.
 *
 * Returns:
 *		VXT_SUCCESS => Upon successful invocation
 *
 *		VXT_FAIL - Failed to open the authorization file
 *		           and create the in-memory data base.
 *
 */

int
vxtdb_module_init(void **db_handle)
{
	vxt_auth_database_t *db = NULL;
	
	UMI_LOG(50, VXTPRINT_PROFILE_LEVEL, "called\n");
	/*
	 * Initialize module level locks
	 */
		/*
		 * guided by os dependent caller locks
		 */
	/*
 	 * Open database file and initialize
	 * in-memory database hash tables
	 */
		/*
		 * Dependent on xenbus which is
		 * not up at initialization
		 * Database is opened explicitly
		 * after xenbus is up.
		 */
	/*
	 * Create user level device file
	 */
	/*
	 * Export interfaces for use in kernel
	 */
	*db_handle = (void *)db;
	return VXT_SUCCESS;
}


/*
 * 
 * vxtdb_module_shutdown:
 *
 * External interface for the shutdown and resource release
 * of a database file.  There may be multiple databases
 * running simultaneously.  vxtdb_module_shutdown is
 * meant to be called during vxt auth module unload, however
 * it may be called at anytime as the compliment to 
 * vxtdb_module_init.
 *
 * Locks:
 *		Request lock held on invocation 
 *
 * Returns:
 *		VXT_SUCCESS => upon successful invocation
 *		ret from vxt_db_delete
 *
 */

int
vxtdb_module_shutdown(void *db_handle)
{
	int ret;

	UMI_LOG(51, VXTPRINT_PROFILE_LEVEL, "called\n");
	ret = VXT_SUCCESS;
	if (db_handle != NULL) {
		ret  = vxt_db_delete(db_handle, FALSE);
	}
	return ret;
}

