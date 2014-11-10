/*
 * vxt_auth.c
 *
 * The VxT authorization service provides the for Vxt controller 
 * guest identification and authorization.  To do this it relies
 * on an Authorization database.  In some cases this database
 * resides in the hypervisor or transport device specific domain
 *
 * To support this properly we export an implementation independent
 * interface to query the authorization database.
 *
 * Exported Interfaces:

 * Note:  The following export is not dependent on vxtctrlr
 *        callbacks and therefore can be called by the authorization
 *        service prior to the installation of the vxtctrlr module.
 *
 *		vxt_auth_db_lookup(char *key, char* record_field);
 *
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



#define _VXT_COMPONENT_ 9
#define _VXT_SUBSYSTEM_ 23


#include <linux/err.h>
#include <linux/module.h>

#include <public/vxt_system.h>
#include <public/kernel/os/vxtcom_embedded_os.h>
#include <public/kernel/bus/kvm/vxtcom_bus_ctrlr.h>
#include <public/kernel/vxt_module.h>

extern vxtbus_common_api_t vxt_cmds;

/*
 *
 * vxt_auth_db_lookup:
 *
 * vxt_auth_db_lookup provides a simple database query capability
 * A key and a targeted field are provided.  The call is made
 * and if successful returns the value from that field.  All
 * parameters are strings.
 *
 * In the case of KVM we are limiting our database lookup to
 * the information that is contained in our collection of
 * vxt bus instances.  At the moment our queries are limited
 * to variants on the lookup of local domain id's from their
 * UUID's
 *
 * Our list of local domains will not be exhaustive, but will
 * contain all domains that have vxt busses.  i.e. all domains
 * we can attach to.
 *
 * In future we can re-direct our queries to an external KVM source
 * if necessary.
 *
 * Returns:
 *
 */

int
vxt_auth_db_lookup(char *key, char *field, char* record_field)
{
	int  ret;

	UMI_LOG(1, VXTPRINT_PROFILE_LEVEL,
	        " called for a database lookup of the Domain-ID for %s\n",
	        key);

	/*
	 * Lookup the local database instantiation relationships first
	 * If we don't find something on our vxt_com context, we will
	 * try an external topology database.
	 */
	ret = VXT_FAIL;

	if (vxt_cmds.vxtctrlr_module_init == NULL) {
		return VXT_FAIL;
	}

	ret = vxt_cmds.db_lookup(key, field, record_field);

	

	/* No external authority at this time, return result from vxt_com */
	if (ret == VXT_SUCCESS) {
		ret = 1;
	} else {
		ret = 0;
	}
	return ret;
}

EXPORT_SYMBOL(vxt_auth_db_lookup);


/*
 *
 * vxt_auth_multi_look:
 *
 * vxt_auth_multi_look provides a database query for
 * multi-record response.  In the case of Xen this
 * is used as a wrapper for directory contents requests
 * but it may serve to provide the records which match
 * a particular field.  This is in contrast to vxt_auth_lookup
 * which behaves as a lookup of a primary key.
 *
 * Upon successful invocation, the "results" parameter is pointed
 * to a set of strings representing the records matching the query.
 * the "rec_cnt" field is set to the number of records returned.
 * 
 *
 * Returns:
 *		VXT_SUCCESS => Upon successful invocation
 *		VXT_FAIL - bad or unrecognized key or field
 *
 */

int
vxt_auth_db_multi_look(char *key, char *field,
                       char **results, uint32_t *rec_cnt)
{
	char *records;
	int ret;
	unsigned int cnt;

	UMI_LOG(0, VXTPRINT_PROFILE_LEVEL,
	        " called for a xenbus_directory on %s\n",
	        key);

/*
	records = xenbus_directory(XBT_NIL, key, 
	                           field, &cnt);
        if (IS_ERR(records)) {
                return VXT_FAIL;
        }
	*results = (char *)records;
	*rec_cnt = (uint32_t)cnt;

	return VXT_SUCCESS;
records = NULL;
*results = NULL;
return VXT_FAIL;
*/

	records = NULL;

	if (vxt_cmds.vxtctrlr_module_init == NULL) {
		UMI_LOG(0, VXTPRINT_PRODUCT_LEVEL,
		        "VxT Com callback table is not initialized\n");
		return VXT_FAIL;
	}

	ret = vxt_cmds.db_multi_lookup(key, field, &records, &cnt);

	

	if (ret == VXT_SUCCESS) {
		*results = (char *)records;
		*rec_cnt = (uint32_t)cnt;

		return VXT_SUCCESS;
	}

	/* No external authority at this time, return result from vxt_com */

	return ret;

}


EXPORT_SYMBOL(vxt_auth_db_multi_look);
