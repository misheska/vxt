/*
 * vxtdb_mod.c
 *
 * Wrappers for linux functions for the vxt authorization database
 * module.
 */

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
#define _VXT_SUBSYSTEM_ 20

#ifndef LEGACY_4X_LINUX
#ifdef SLES_SP2
#include <asm/uaccess.h>
#else
#include <linux/uaccess.h>
#endif
#else
#include <asm/uaccess.h>
#endif
#include <linux/module.h>
#include <public/kernel/os/vxtcom_embedded_os.h>
#include <public/vxt_system.h>
#include <public/vxt_auth.h>
#include <vxtdb/os/vxtdb_mod.h>
#include <vxtdb/common/vxt_db.h>

#include <linux/namei.h>



#define VXTDB_DEV_FILE_NAME "vxtdb-auth"

int  vxt_database_running = 0;
int  vxtdb_device = 0;
void *db_handle;




vxt_file_t *
vxt_file_open(char *filename, int flags)
{
	struct nameidata *ndparent;
	vxt_file_t       *fd;
	int              ret;


	/*
	 * Check path and fail if it is not present
	 */
	ndparent = kmalloc(sizeof(*ndparent), GFP_KERNEL);
	if (unlikely(!ndparent)) {
		return NULL;
	}
	ret = path_lookup(filename, LOOKUP_PARENT, ndparent);
	if (ret) {
		kfree(ndparent);
		return NULL;
	}
	kfree(ndparent);

	/*
	 * Open the requested file
	 */
	if ((fd = filp_open(filename, flags, S_IRUSR | S_IWUSR)) == NULL) {
		UMI_LOG(55, VXTPRINT_PRODUCT_LEVEL,
		        "failed on: %s\n", filename); 
	} else {
		fd->f_pos = 0;
	}
	
	return fd;
}

int
vxt_file_read(vxt_file_t *fd, void *buffer, uint64_t length, uint64_t *pos)
{
	int count;
	count = vfs_read(fd, buffer, (size_t)length, pos);
	return count;
}


int
vxt_file_write(vxt_file_t *fd, void *buffer, uint64_t length, uint64_t *pos)
{
	int count;
	count = vfs_write(fd, buffer, (size_t)length, pos);
/*
	if (fd->f_op->write) {
		UMI_LOG(56, VXTPRINT_DEBUG_LEVEL, "do a fop->write\n");
		count = fd->f_op->write(fd, buffer,
		                        (size_t)length, (loff_t *)pos);
	} else {
		UMI_LOG(57, VXTPRINT_DEBUG_LEVEL, "do a sync write\n");
		count = do_sync_write(fd, buffer,
		                      (size_t)length, (loff_t *)pos);
	}
*/

	return count;
}


int
vxt_file_close(vxt_file_t *fd)
{
	if (filp_close(fd, current->files)) {
		return VXT_FAIL;
	}
	return VXT_SUCCESS;
}


int
vxt_file_remove(vxt_file_t *fd)
{
#ifdef SLES_SP2
	if (vfs_unlink(fd->f_dentry->d_parent->d_inode,
	               fd->f_dentry, fd->f_vfsmnt)) {
		return VXT_FAIL;
	}
#else
	if (vfs_unlink(fd->f_dentry->d_parent->d_inode,
	               fd->f_dentry)) {
		return VXT_FAIL;
	}
#endif
	return VXT_SUCCESS;
}

/*
 * In our present setup, there is no
 * implied pointer.
 */
int
vxt_file_seek(vxt_file_t *fd, uint64_t offset)
{
	return VXT_SUCCESS;
}

uint64_t
vxt_file_size(vxt_file_t *fd)
{
/*
	return fd->f_path.dentry->d_inode->i_size;
*/
	return fd->f_dentry->d_inode->i_size;
}


/*
 * vxt_uuid_to_local_dom:
 *
 * The VxT authorization database relies on a translation from the
 * universal id for an endpoint to the local guest moniker.  In the
 * case of the Linux/Xen port this is a domain name provided by
 * a lookup against the XenStore database.
 *
 */

int
vxt_uuid_to_local_dom(char *uuid, char *domain_name)
{
	char     vxt_uuid_str[50];
	uint32_t rec_cnt;
	char     **records;
	char     domain_path[40];
	int      i;
	int      ret;

	/*
	 * We cannot do these checks on the local guest
	 * The xenstore database is not available to us.
	 * For now we will return success with a domain of
	 * name of  "NONE".  In future we will call a local
	 * resolution mechanism for our security/routing
	 * sub-zone.
	 */
	if(!is_initial_xendomain()) {
		strncpy(domain_name, "NONE", 8);
		return 1;
	}


	/*
	 * Do a lookup of the list of active local domains
	 */
	ret = vxt_auth_db_multi_look("/local/domain", "",
	                             (char **)&records, &rec_cnt);
	if (ret == VXT_SUCCESS) {
		UMI_LOG(140, VXTPRINT_DEBUG_LEVEL, "domain count %d\n", rec_cnt);
		for (i = 0; i < rec_cnt; i++) {
			UMI_LOG(141, VXTPRINT_DEBUG_LEVEL,
			        "Active domain record %d: Domain-%s\n",
			        i, records[i]);
			sprintf(domain_path, "/local/domain/%s", records[i]);
			ret = vxt_auth_db_lookup(domain_path, 
			                         "vm", vxt_uuid_str);
			if (ret == 1) {
				UMI_LOG(142, VXTPRINT_DEBUG_LEVEL,
				        "Domain-%s uuid value %s\n",
				        records[i], vxt_uuid_str);
				if (!strncmp(uuid, &vxt_uuid_str[4], 40)) {
					sprintf(domain_name, "Domain-%s",
					        records[i]);
					kfree(records);
					return 1;
				}
			}
		}
		kfree(records);
	}
	return 0;
}


/*
 *
 * vxt_get_local_uuid:
 *
 * vxt_get_local_uuid queries the xenstore database for the uuid of
 * the local vm context.  If the local context is that of Dom0 then
 * the unity uuid (all 0's) is returned.  In this way the caller will
 * either get the VM uuid or by checking for unity will be able to
 * determine that it is running in Domain 0 and can act accordingly.
 *
 * Returns:
 *
 *		VXT_SUCCESS => Upon Success
 *		VXT_FAIL - The xenstore database is either unresponsive
 *                         or the specified field is missing.
 *
 */

int
vxt_get_local_uuid(char *uuid)
{
	char vxt_uuid_str[MAX_VXT_UUID];
	int ret;

	UMI_LOG(59, VXTPRINT_PROFILE_LEVEL, "called\n");

	if(is_initial_xendomain()) {
		UMI_LOG(60, VXTPRINT_BETA_LEVEL,
		        " DOM0 query for own uuid\n");
		/*
		 * We identify Dom0 uuid as the identity.  If there is
		 * another Dom0 uuid, it is not retrievable from within the
		 * kernel.  The caller will have to use another means
		 * to determine it.
		 */
		strncpy(uuid, "00000000-0000-0000-0000-000000000000",
		        MAX_VXT_UUID);
		ret = 1;
	} else {
		UMI_LOG(61, VXTPRINT_BETA_LEVEL,
		        " Guest query for own uuid\n");
		vxt_uuid_str[0] = 0;
		ret = vxt_auth_db_lookup("vm", "", vxt_uuid_str);
		if (ret == 1) {
			strncpy(uuid, &vxt_uuid_str[4], MAX_VXT_UUID-4);
		}
	}
	if (ret == 1) {
		return VXT_SUCCESS;
	} else {
		return VXT_FAIL;
	}
}


int
vxtdb_dev_ioctl(struct inode *inode, struct file *filep,
             unsigned command, unsigned long argument)
{
	int ret;
	mm_segment_t file_segment;

	union ioctl_structures {
		vxt_db_start_arg_t         start_db_args;
		vxt_db_insert_arg_t        insert_args;
		vxt_db_lookup_client_arg_t get_client_args;
		vxt_db_delete_rec_arg_t    delete_rec_args;
		vxt_db_dump_guest_arg_t    dump_guest_args;
		vxt_db_restore_guest_arg_t restore_guest_args;
		vxt_db_query_arg_t         query_guest_args;
	} ibuf;

	UMI_LOG(63, VXTPRINT_PROFILE_LEVEL, "called\n");

	vxt_mlock(&vxtdb_request_lock);

	if ((vxt_database_running == 0) && 
	    ((command != VXT_AUTH_DB_START)
	     && (command != VXT_AUTH_GET_LOCAL_UUID))) {
		UMI_LOG(64, VXTPRINT_BETA_LEVEL, "Database not up\n");
		vxt_munlock(&vxtdb_request_lock);
		return -EBUSY;
	}

	switch (command) {

	case VXT_AUTH_DB_INSERT:
	{

		UMI_LOG(65, VXTPRINT_BETA_LEVEL, "VXT_AUTH_DB_INSERT\n");
		ret = copy_from_user((void *)&ibuf, (void __user *)argument,
		                     sizeof(vxt_db_insert_arg_t));
                if (ret) {
                        UMI_LOG(66, VXTPRINT_BETA_LEVEL,
                                "Error copying db_insert bus argument\n");
			vxt_munlock(&vxtdb_request_lock);
                        return -EFAULT;
                }
		UMI_LOG(67, VXTPRINT_DEBUG_LEVEL,
		        "Insert arguments, server %s, client %s, "
		        "dev_name %s, flags %d\n",
		        ibuf.insert_args.record.client,
		        ibuf.insert_args.record.server,
		        ibuf.insert_args.record.dev_name,
		        ibuf.insert_args.flags);
		

		/*
		 * Set up address for kernel file reads and writes
		 */
		file_segment = get_fs();
		set_fs(KERNEL_DS);

		ret =
		   vxt_db_insert(db_handle,
		                 &ibuf.insert_args.record,
		                 ibuf.insert_args.flags);
		/*
		 * Return to caller addressing frame
		 */
		set_fs(file_segment);
		if (ret) {
                        UMI_LOG(68, VXTPRINT_BETA_LEVEL,
                                "Error trying to insert a vxt db record\n");
			vxt_munlock(&vxtdb_request_lock);
                        return -EFAULT;
		}
		break;
	}
	case VXT_AUTH_DB_CLIENT_LOOKUP:
	{
		vxt_db_lookup_client_arg_t *user_arg =
				(vxt_db_lookup_client_arg_t *)argument;
		int ret;

		UMI_LOG(69, VXTPRINT_BETA_LEVEL, "VXT_AUTH_DB_CLIENT_LOOKUP\n");
		ret = copy_from_user((void *)ibuf.get_client_args.requester,
		                     (void __user *)user_arg->requester, 40);
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			return -EFAULT;
		}
		ret = copy_from_user((void *)ibuf.get_client_args.dev_name,
		                     (void __user *)user_arg->dev_name,
		                     MAX_VXT_UNAME);
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			return -EFAULT;
		}
		ret = vxt_db_lookup_client(db_handle,
		                           ibuf.get_client_args.requester,
		                           ibuf.get_client_args.dev_name,
		                           &ibuf.get_client_args.record);
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			return -ENXIO;
		}
		ret = copy_to_user((void __user *)&user_arg->record,
		                   &ibuf.get_client_args.record,
		                   sizeof(vxt_db_lookup_rec_t));
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			return -EFAULT;
		}
		break;
	}
	case VXT_AUTH_DB_DELETE_REC:
	{
		int ret;

		UMI_LOG(70, VXTPRINT_BETA_LEVEL, "VXT_AUTH_DB_DELETE_REC\n");
		ret = copy_from_user((void *)&ibuf.delete_rec_args,
		                     (void __user *)argument,
		                     sizeof(vxt_db_delete_rec_arg_t));
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			return -EFAULT;
		}

		/*
		 * Set up address for kernel file reads and writes
		 */
		file_segment = get_fs();
		set_fs(KERNEL_DS);


		ret = vxt_db_delete_record(db_handle,
		                           ibuf.delete_rec_args.client,
		                           ibuf.delete_rec_args.server,
		                           ibuf.delete_rec_args.dev_name);

		/*
		 * Return to caller addressing frame
		 */
		set_fs(file_segment);

		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			return -ENXIO;
		}
		break;
	}
	case VXT_AUTH_DB_DUMP_GUEST:
	{
		int ret;
		uint32_t in_memory_records;
		uint32_t overrun;
		vxt_db_file_rec_t *user_rec_buf;

		UMI_LOG(71, VXTPRINT_BETA_LEVEL, "VXT_AUTH_DB_DUMP_GUEST\n");


		ret = copy_from_user((void *)&ibuf.dump_guest_args,
		                     (void __user *)argument,
		                     sizeof(vxt_db_dump_guest_arg_t));
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			return -EFAULT;
		}

		ret = 
		   vxt_db_guest_dev_count(db_handle,
		                          ibuf.dump_guest_args.guest,
		                          &in_memory_records);

		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			return -EFAULT;
		}
		UMI_LOG(72, VXTPRINT_DEBUG_LEVEL,
			"in_memory_records = %d for guest %s\n",
		        in_memory_records, ibuf.dump_guest_args.guest);

		/*
		 * Calculate the number of records requested early
		 * to avoid searching further than necessary.
		 */
		if (in_memory_records < ibuf.dump_guest_args.buf_size) {
			ibuf.dump_guest_args.buf_size = in_memory_records;
			ibuf.dump_guest_args.overrun = 0;
		} else {
			in_memory_records = ibuf.dump_guest_args.buf_size;
			/*
			 * Caller cannot delete records if we cannot
			 * record all of the records that are going
			 * to be deleted.
			 */
			ibuf.dump_guest_args.delete = 0;
			ibuf.dump_guest_args.overrun = 1;
		}

		if (in_memory_records == 0) {
			/* 
			 * No records to return, we're done
			 */
			ret = 
			   copy_to_user((void __user *)argument,
			                (void *)&ibuf.dump_guest_args,
			                sizeof(vxt_db_delete_rec_arg_t));
			if (ret) {
				vxt_munlock(&vxtdb_request_lock);
				return -EFAULT;
			}
			break;
		}

		user_rec_buf = 
		   vxt_kmalloc(sizeof(vxt_db_file_rec_t)
		               * in_memory_records, VXT_KMEM_KERNEL);
        	if (user_rec_buf == NULL) {
			vxt_munlock(&vxtdb_request_lock);
                	return -ENOSPC;
		}

		/*
		 * Set up address for kernel file reads and writes
		 */
		file_segment = get_fs();
		set_fs(KERNEL_DS);

		ret = vxt_db_dump_guest(db_handle, ibuf.dump_guest_args.guest,
		                        user_rec_buf, &in_memory_records,
		                        &overrun);

		/*
		 * Return to caller addressing frame
		 */
		set_fs(file_segment);


		UMI_LOG(73, VXTPRINT_DEBUG_LEVEL,
		        "returning from vxt_db_dump_guest, ret = %d\n", ret);
        	if (ret != VXT_SUCCESS) {
			vxt_munlock(&vxtdb_request_lock);
			kfree(user_rec_buf);
                	return -EINVAL;
		}

		ret = 
		   copy_to_user((void __user *)
		                ibuf.dump_guest_args.record_buffer,
		                (void *)user_rec_buf,
		                sizeof(vxt_db_file_rec_t) 
		                * in_memory_records);
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			kfree(user_rec_buf);
			return -EFAULT;
		}

		/*
		 * Check for request to delete records, if caller
		 * wishes to delete the records and we were able
		 * to write them all back.  We will attempt deletion.
		 */
		if(ibuf.dump_guest_args.delete) {
			ret = vxt_db_delete_guest(db_handle,
			                          ibuf.dump_guest_args.guest,
			                          ibuf.dump_guest_args.migrate);
			if (ret) {
				/*
				 * communicate our failure to delete
				 * the records to the caller without failing
				 * overall call.
				 */
				ibuf.dump_guest_args.delete = 0;
			}
		}

		ret = 
		   copy_to_user((void __user *)argument,
		                (void *)&ibuf.dump_guest_args,
		                sizeof(vxt_db_delete_rec_arg_t));
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			kfree(user_rec_buf);
			return -EFAULT;
		}

		kfree(user_rec_buf);
		break;
		
	}
	case VXT_AUTH_DB_RESTORE_GUEST:
	{
		int ret;
		uint32_t in_memory_records;
		vxt_db_file_rec_t *user_rec_buf;

		UMI_LOG(74, VXTPRINT_BETA_LEVEL, "VXT_AUTH_DB_RESTORE_GUEST\n");


		ret = copy_from_user((void *)&ibuf.restore_guest_args,
		                     (void __user *)argument,
		                     sizeof(vxt_db_restore_guest_arg_t));
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			return -EFAULT;
		}

		in_memory_records = ibuf.restore_guest_args.buf_size;

		user_rec_buf = 
		   vxt_kmalloc(sizeof(vxt_db_file_rec_t)
		               * in_memory_records, VXT_KMEM_KERNEL);
        	if (user_rec_buf == NULL) {
			vxt_munlock(&vxtdb_request_lock);
                	return -ENOSPC;
		}

		ret = 
		   copy_from_user((void *)user_rec_buf,
		                  (void __user *)
		                  ibuf.restore_guest_args.record_buffer,
		                  sizeof(vxt_db_file_rec_t) 
		                  * in_memory_records);
		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			kfree(user_rec_buf);
			return -EFAULT;
		}

		/*
		 * Set up address for kernel file reads and writes
		 */
		file_segment = get_fs();
		set_fs(KERNEL_DS);


		ret =
		   vxt_db_restore_guest(db_handle,
		                        ibuf.restore_guest_args.guest,
                                        user_rec_buf,
		                        ibuf.restore_guest_args.buf_size,
		                        ibuf.restore_guest_args.migrate);
		/*
		 * Return to caller addressing frame
		 */
		set_fs(file_segment);


		if (ret) {
			vxt_munlock(&vxtdb_request_lock);
			kfree(user_rec_buf);
			return -EINVAL;
		}
		kfree(user_rec_buf);

		break;
	}
	case VXT_AUTH_DB_QUERY:
	{
		int ret;

		vxt_munlock(&vxtdb_request_lock);

		UMI_LOG(75, VXTPRINT_BETA_LEVEL, "VXT_AUTH_DB_QUERY\n");
#ifdef LEGACY_4X_LINUX
		if (xen_start_info->flags & SIF_INITDOMAIN) {
#else
		if (is_initial_xendomain()) {
#endif
			UMI_LOG(76, VXTPRINT_BETA_LEVEL, "auth_db is hub\n");
			ibuf.query_guest_args.hub = 1;
		} else {
			UMI_LOG(77, VXTPRINT_BETA_LEVEL, "auth_db is spoke\n");
			ibuf.query_guest_args.hub = 0;
		}

		ret = copy_to_user((void __user *)argument, 
		                   (void *)&ibuf.query_guest_args,
		                   sizeof(vxt_db_query_arg_t));
		if (ret) {
                        UMI_LOG(78, VXTPRINT_PRODUCT_LEVEL,
                                "Error copying guest query args back\n");
			return -EFAULT;
		}
		return 0;
	}
	case VXT_AUTH_DB_START:
	{
		int ret;

		UMI_LOG(79, VXTPRINT_BETA_LEVEL, "VXT_AUTH_DB_START\n");

		ret = copy_from_user((void *)&ibuf, (void __user *)argument,
		                     sizeof(vxt_db_start_arg_t));
                if (ret) {
                        UMI_LOG(80, VXTPRINT_BETA_LEVEL,
                                "Error copying db_start argument\n");
			vxt_munlock(&vxtdb_request_lock);
                        return -EFAULT;
                }

		if ((vxt_database_running != 0) || (db_handle != NULL)) {
			vxt_munlock(&vxtdb_request_lock);
			return -EBUSY;
		}

		file_segment = get_fs();
		set_fs(KERNEL_DS);

		db_handle = vxt_db_create(ibuf.start_db_args.db_file_name);
		if(db_handle == NULL) {
			vxt_munlock(&vxtdb_request_lock);
			return -ENODEV;
		}

		/*
		 * Return to caller addressing frame
		 */
		set_fs(file_segment);

		vxt_database_running = 1;
		break;
	}
	case VXT_AUTH_GET_LOCAL_UUID:
	{
		vxt_db_local_uuid_arg_t *user_arg =
				(vxt_db_local_uuid_arg_t *)argument;
		int ret;
		char callers_uuid[MAX_VXT_UUID];

		UMI_LOG(81, VXTPRINT_PROFILE_LEVEL,
		        "VXT_AUTH_GET_LOCAL_UUID\n");

		vxt_get_local_uuid(callers_uuid);

		ret = copy_to_user((void __user *)user_arg->uuid,
		                   &callers_uuid,
		                   sizeof(vxt_db_local_uuid_arg_t));
		if (ret) {
                        UMI_LOG(82, VXTPRINT_BETA_LEVEL,
                                "Error copying back argument\n");
			vxt_munlock(&vxtdb_request_lock);
			return -EFAULT;
		}
		break;
	}
	default:
	{
		UMI_LOG(83, VXTPRINT_PRODUCT_LEVEL, "command unknown\n");
		break;
	}

	} /* end command switch */
	vxt_munlock(&vxtdb_request_lock);
	return 0;
}



int
vxtdb_start_database(char *db_file_name)
{
	mm_segment_t file_segment;

	UMI_LOG(84, VXTPRINT_PROFILE_LEVEL,
	        "called, running:%d, db_handle %p\n", 
	        vxt_database_running, db_handle);
	vxt_mlock(&vxtdb_request_lock);

	if ((vxt_database_running != 0) || (db_handle != NULL)) {
		vxt_munlock(&vxtdb_request_lock);
		return -EBUSY;
	}

	file_segment = get_fs();
	set_fs(KERNEL_DS);

	db_handle = vxt_db_create(db_file_name);
	if(db_handle == NULL) {
		vxt_munlock(&vxtdb_request_lock);
                return -ENODEV;
	}

	/*
	 * Return to caller addressing frame
	 */
	set_fs(file_segment);

	vxt_database_running = 1;
	vxt_munlock(&vxtdb_request_lock);
	return 0;
}

EXPORT_SYMBOL(vxtdb_start_database);


int
vxtdb_client_lookup(char *requester, char *name,
                    vxt_db_lookup_rec_t *record)
{
	int ret;

	UMI_LOG(85, VXTPRINT_PROFILE_LEVEL,
	        "called, requester %s, dev_name %s\n",
	        requester, name);

	if (vxt_database_running == 0) {
		return VXT_DOWN;
	}

	vxt_mlock(&vxtdb_request_lock);
	ret = vxt_db_lookup_client(db_handle, requester, name, record);
	vxt_munlock(&vxtdb_request_lock);
	return ret;
}
EXPORT_SYMBOL(vxtdb_client_lookup);


int
vxtdb_client_record_delete(char *client, char *server, char *dev_name)
{
	mm_segment_t file_segment;
	int          ret;

	UMI_LOG(86, VXTPRINT_PROFILE_LEVEL,
	        "called, client %s, server %s, dev_name %s\n",
	        client, server, dev_name);

	if (vxt_database_running == 0) {
		return VXT_DOWN;
	}

	/*
	 * Set up address for kernel file reads and writes
	 */
	file_segment = get_fs();
	set_fs(KERNEL_DS);

	vxt_mlock(&vxtdb_request_lock);

	ret = vxt_db_delete_record(db_handle, client, server, dev_name);

	/*
	 * Return to caller addressing frame
	 */
	set_fs(file_segment);

	vxt_munlock(&vxtdb_request_lock);
	return ret;
}

EXPORT_SYMBOL(vxtdb_client_record_delete);


/*
 *
 * vxtdb_hub_lookup_spoke:
 *
 * Convert the local domain name into the universal id, (uuid).
 *
 * vxtdb_hub_lookup_spoke codifies part of the hub spoke architecture
 * and isolates some of the xen implementation away from the caller
 * This call really belongs in a xen level file instead of a 
 * linux level but it is the only call.  Should the need arise
 * to isolate authentication xen implementation this call and
 * any others relying on the xen interfaces need to be abstracted
 * and isolated.
 *
 * Upon successful invocation, the remote_uuid field passed is
 * filled in.
 *
 * Returns:
 *		VXT_SUCCESS => Upon Successful invocation
 *		VXT_FAIL - xenbus cannot dereference the
 *		domain name, (spoke) passed.
 *
 */

int
vxtdb_hub_lookup_spoke(int spoke, char *remote_uuid)
{
	char uuid_path[35];
	char uuid_string[MAX_VXT_UUID];
	int ret;

	UMI_LOG(87, VXTPRINT_PROFILE_LEVEL, "called on remote id = %d\n", spoke);

	if (vxt_database_running == 0) {
		return VXT_DOWN;
	}

	/*
	 * Construct the query string for xenstore
	 */

	sprintf(uuid_path, "/local/domain/%d", spoke);

	ret = vxt_auth_db_lookup(uuid_path, "vm", uuid_string);

	if (ret != 1 ) {
		UMI_WARN(88, VXTPRINT_PRODUCT_LEVEL,
		         " Unable to find remote uuid "
		         "record for %d, uuid = %s\n",
		         spoke, uuid_path);
		return VXT_FAIL;
        }

	UMI_LOG(89, VXTPRINT_BETA_LEVEL,
	        "xenstore returns remote id = %s\n", uuid_path);
	/*
	 * Copy in the remote guest's uuid, remove the
	 * leading "/vm/"
	 */
        strncpy(remote_uuid, &uuid_string[4], MAX_VXT_UUID);
	remote_uuid[MAX_VXT_UUID-1] = 0;

	return VXT_SUCCESS;
}

EXPORT_SYMBOL(vxtdb_hub_lookup_spoke);


static int
vxtdb_dev_open(struct inode *inode, struct file *filep)
{
	UMI_LOG(90, VXTPRINT_PROFILE_LEVEL, "called\n");
	/*
	 * There is no instance specific data for
	 * the file descriptor.  Just rely on the
	 * built-in reference count for module rmmod
	 * lock, (until close)
	 */
	filep->private_data = NULL;
	return 0;
}


static int
vxtdb_dev_close(struct inode *inode, struct file *filep)

{
	UMI_LOG(91, VXTPRINT_PROFILE_LEVEL, "called\n");
	filep->private_data = NULL;
	return 0;
}


static struct file_operations vxtdb_dev_fops =
{
        .owner   = THIS_MODULE,
/*
        .poll    = vxtcom_poll,
        .mmap    = vxtcom_mmap,
        .read    =
        .write   =
*/
        .ioctl   = vxtdb_dev_ioctl,
        .open    = vxtdb_dev_open,
        .release = vxtdb_dev_close,
};



int
vxtdb_os_register_device_file(void)
{
	UMI_LOG(92, VXTPRINT_PROFILE_LEVEL, "called\n");
	/*
	 * Register the vxtaut_db device and set the global device
	 * file number field
	 */
	vxtdb_device = register_chrdev(0, VXTDB_DEV_FILE_NAME,
	                               &vxtdb_dev_fops);
	 if (vxtdb_device < 0) {
                UMI_LOG(93, VXTPRINT_PRODUCT_LEVEL,
		        "unable to create a character device\n");
                return (VXT_FAIL);
        }

	vxt_class_device_create(vxtdb_device, 0, VXTDB_DEV_FILE_NAME);

	return VXT_SUCCESS;


}

int
vxtdb_os_unregister_device_file(void)
{

	UMI_LOG(95, VXTPRINT_PROFILE_LEVEL, "called\n");

	vxt_class_device_destroy(vxtdb_device, 0);

        unregister_chrdev(vxtdb_device, VXTDB_DEV_FILE_NAME);

	return VXT_SUCCESS;

}



static int __init vxtdb_auth_init(void)
{
	mm_segment_t file_segment;
        int          ret;

	UMI_LOG(96, VXTPRINT_PROFILE_LEVEL, "called\n");
/*
        if (!is_running_on_xen())
                return -ENODEV;
*/
	file_segment = get_fs();
	set_fs(KERNEL_DS);
	/*
	 * Initialize the request lock
	 */
	vxt_mlock_init(&vxtdb_request_lock);
        ret = vxtdb_module_init(&db_handle);
        if (ret != VXT_SUCCESS) {
		set_fs(file_segment);
                return -ENODEV;
        }
	set_fs(file_segment);

	ret = vxtdb_os_register_device_file();
	if (ret != VXT_SUCCESS) {
		UMI_LOG(97, VXTPRINT_PRODUCT_LEVEL, 
		        "vxtdb_os_register_device_file failed\n");
		vxtdb_module_shutdown(db_handle);
	}

	return 0;

}

static void vxtdb_auth_cleanup(void)
{

	UMI_LOG(98, VXTPRINT_PROFILE_LEVEL, "called\n");
	vxt_mlock(&vxtdb_request_lock);
	vxtdb_os_unregister_device_file();
        vxtdb_module_shutdown(db_handle);
	vxt_munlock(&vxtdb_request_lock);
        return;
}



module_init(vxtdb_auth_init);
module_exit(vxtdb_auth_cleanup);

MODULE_LICENSE("Symantec Proprietary");
