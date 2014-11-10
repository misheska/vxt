/*
 * vxt_prov.c: vxt_provision_daemon
 *
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
 *
 * The vxt_provision_daemon is started on a Xen based Dom-0 operating
 * system.  Its purpose is to monitor the /vm portion of the data base
 * and provision any newly created virtual domains with a vxt controller.
 *
 * The existence of a vxt controller allows the guest to communicate with
 * Dom-0 to set up vxt devices.  The actual connection of such devices is
 * controlled through Dom-0 based vxt authorization.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <sys/select.h>
#include <string.h>
#include <xs.h>
#include <sys/stat.h>

#include "public/vxt_system.h"
#include "public/vxt_auth.h"


#define MAX_PATH 512
#define DEPROVISION "deprovision"
#define PROVISION "provision"
#define UPDATE_VM_SCRIPT "/opt/VRTSvxvi/bin/update_vm_params"


/*
 *
 * xsd_vm_name:
 *
 * Parses the incoming string, fills in the
 * target_uuid parameter and returns 1 if
 * the prefix of the string is of the form:
 *
 * /vm/<UUID>/name
 *
 * If the string is of the form /vm/<UUID>
 * xsd_vm_name will return 2, indicating
 * that a valid UUID was found but that
 * the "name" field was absent.  This denotes
 * the beginning of the process of name removal.
 * (deprovisioning)
 *
 * If the incoming string does not conform to
 * one of the two preceeding forms a "0" is
 * returned.
 *
 * The target_uuid field is filled in with the
 * uuid of the associated record when the
 * string is of the proper form.
 *
 *
 * Returns:
 *
 *		0 - Cannot parse string
 *		1 - Valid name and UUID found
 * 		2 - UUID found but no valid name present
 *
 */

int
xsd_vm_name(char *path, char *target_uuid)
{
	char *tail;
	char *tokenstring;
	char *token;

	tokenstring = strdup(path);

	if (strtok_r(tokenstring, "/", &tail) == 0) {
		printf("vxtp_provision failed, path = %s\n", path);
		free(tokenstring);
		return 0;
	}
	if (strncmp("/vm", tokenstring, 3)) {
		printf("vxtp_provision failed, path = %s\n", path);
		free(tokenstring);
		return 0;
	}
	token = tail;
	if (strtok_r(token, "/", &tail) == 0) {
		free(tokenstring);
		return 0;
	}
	/*
	 * Copy the UUID for later use
	 */
	/* 
	printf("xsd_vm_name UUID for new entry: %s\n", token);
	*/
	strncpy(target_uuid, token, MAX_VXT_UUID);
	target_uuid[MAX_VXT_UUID-1] = 0;
	token = tail;
	
	if (!strncmp("name", tail, 4)) {
		free(tokenstring);
		return 1;
	}
	
	/*
	 * Found a UUID but not a "/name" 
	 * afterwards.
	 */
	free(tokenstring);
	return 2;
}


/*
 *
 * xsd_provision_controller:
 *
 * Provision a vxt_controller for the domain assoicated
 * domain id passed by writing to the associated xenstore
 * record.
 *
 * Returns:  provision status.
 */

int
xsd_provision_controller(struct xs_handle *xs, int domid) 
{
	xs_transaction_t t;

	struct xs_permissions xsperms[2];
	xsperms[0].id = domid;
	xsperms[0].perms = XS_PERM_READ | XS_PERM_WRITE;
	struct stat buf;
	char s[MAX_PATH];
	char s2[MAX_PATH];
	char domname[20];
	char backend[MAX_PATH];
	char frontend[MAX_PATH];
	int devno = 0;
	int result;
	char* home;
	char* dom0_home;
	int provision_status = 0;


	home = xs_get_domain_path(xs,domid);
	dom0_home = xs_get_domain_path(xs,0);

retry_provision:

	/* 
	printf("xsd_provision_controller Domain-%d, home %s, dom0_home %s\n",
	       domid, home, dom0_home);
	*/

	/*
	 *
	 * Create the vxtcom_controller records Dom0 and the
	 * target domain.  The code that follows will:
	 *
	 *
	 * Write the backend vxt controller entry and place it
	 * in Dom-0.  i.e. /local/domain/0 ..
 	 *
	 * backend = ""
	 *    vxtcom_ctrlr = ""
	 *       <Domain-id> (9) = ""
	 *          <Device-id> (0) = ""
	 *             domain = <Domain name> "Domain-9"
	 *             frontend = "/local/domain/9/device/vxtcom_ctrlr/0"
	 *             frontend-id = "9"
	 *             state = "3"
	 *             version = "1"
	 * 
	 *
	 * Write the front end record in the target domain device
	 * subdirectory:  ... e.g.
	 *
   	 * device = ""
	 *     vxtcom_ctrlr = ""
	 *      0 = ""
	 *       backend = "/local/domain/0/backend/vxtcom_ctrlr/9/0"
	 *       backend-id = "0"
	 *       state = "1"
	 */

	t = xs_transaction_start(xs);
	/*
	printf("xsd_provision_controller Domain-%d, transaction token %d\n",
	       domid, t);
	*/
	sprintf(backend, "%s/backend/vxtcom_ctrlr/%d/%d", dom0_home,
		    domid, devno);
	sprintf(frontend, "%s/device/vxtcom_ctrlr/%d", home, devno);

	/*
	 * Create Dom0 device directory for target domain:
	 * ../device/vxtcom_ctrlr/
	 */
	xs_mkdir(xs, t, backend);
	result = xs_set_permissions(xs, t, backend, xsperms, 1);
	/*
	printf("xsd_privision_controller: xs_set_permissions result %d\n",
	       result);
	*/

	/*
	 * create the target domain device record 
	 * /local/domain/<domain #>/device/vxtcom_ctrlr/<domain #>/backend = 
	 *              "/local/domain/0/backend/vxtcom_ctrlr/<domain #>/0"
	 *
	 */
	sprintf(s, "%s/backend", frontend);
	xs_write(xs, t, s, backend, strlen(backend));

	/*
	 * create target domian backend-id record: 
	 * /local/domain/<domain #>/device/vxtcom_ctrlr/<domain #>/backend = 0
	 *
	 */
	sprintf(s, "%s/backend-id", frontend);
	xs_write(xs, t, s, "0", 1);




	/*
	 * create target domian state record: 
	 * /local/domain/<domain #>/device/vxtcom_ctrlr/<domain #>/state = 1
	 *
	 */

	sprintf(s, "%s/state", frontend);
	xs_write(xs, t, s, "1", 1);

	/*
	 * create Dom0 domname (domain-name) record for target domain:
	 * /local/domain/0/backend/vxtcom_ctrlr/<domain#>/0/domname
	 *                                                = Domain-<domain#>
	 */
	sprintf(domname, "Domain-%d", domid);
	sprintf(s, "%s/domain", backend);
	xs_write(xs, t, s, domname, strlen(domname));

	/*
	 * Create Dom0 target vxtctlr device record - "frontend"
	 *
	 * /local/domain/0/device/vxtcom_ctrlr/9/0/frontend = 
	 *           /local/domain/<domain #>/device/vxtcom_ctrlr/0
	 *
	 */
	sprintf(s, "%s/frontend", backend);
	xs_write(xs, t, s, frontend, strlen(frontend));

	/*
	 * Create Dom0 target vxtctlr device record - "frontend-id"
	 *
	 * /local/domain/0/device/vxtcom_ctrlr/9/0/frontend-id = 
	 *           <domid>  e.g. 9
	 *
	 */
	sprintf(s, "%s/frontend-id", backend);
	sprintf(s2,"%d",domid);
	xs_write(xs, t, s, s2, strlen(s2));


	/*
	 * Create Dom0 target vxtctlr device record - "state"
	 *
	 * /local/domain/0/device/vxtcom_ctrlr/9/0/state = 1
	 *
	 */
	sprintf(s, "%s/state", backend);
	xs_write(xs, t, s, "1", 1);

	/*
	 * Create Dom0 target vxtctlr device record - "frontend"
	 *
	 * /local/domain/0/device/vxtcom_ctrlr/<domain #>/0/frontend = 
	 *                      /local/domain/<domain #>/device/vxtcom_ctrlr/0
	 *
	 */
	sprintf(s, "%s/frontend", backend);
	xs_write(xs, t, s, frontend, strlen(frontend));

	if(!xs_transaction_end(xs, t, 0)) {
		printf("xsd_privision_controller: xs_transaction_end failed\n");
		goto retry_provision;
	} else {
		provision_status = 1;
	}
	free(home);
	free(dom0_home);
	return (provision_status);
}

/*
 *
 * xsd_write_auth_record:
 *
 * Write the authorization record for the default message device,
 * connecting the guest with dom0.  This record will allow
 * the guest to establish communication with Dom0 without explicit
 * intervention by an application.
 *
 */

xsd_write_auth_record(int auth_dev, char *guest_uuid)
{
	vxt_db_insert_arg_t insert_arg;

	strncpy(insert_arg.record.client, guest_uuid, MAX_VXT_UUID);
	strncpy(insert_arg.record.server, VXTDB_DOM0_ENDPOINT, 40);
	strncpy(insert_arg.record.dev_name, "MXT_VXT_PIPE", MAX_VXT_UNAME);
	insert_arg.record.priv.valid = 1;
	insert_arg.record.label = 0x1324;
	/*
	 * Compare entire device name. i.e. length=0 default
	 */
	insert_arg.record.class_name_length=0;
	insert_arg.flags = (VXT_DB_INSERT_CLIENT | VXT_DB_INSERT_SERVER);
	ioctl(auth_dev, VXT_AUTH_DB_INSERT, (void *)(long)&insert_arg);
}

/*
 * update_vm_state:
 *
 * This function is called on provision or deprovision of domain. It
 * invokes a shell script to update the VM params according to state of
 * VM. These params are used for HA decision during physical node fault.
 * This function is invoked with a NON-NULL target_uuid
 * Returns:
 *
 *		None
 *
 */

void update_vm_state(char *target_uuid, int is_provision)
{
	char cmd[MAX_PATH];
	char state[32];

	if ( is_provision ) {
		strcpy(state, PROVISION);
	}  else  {
		strcpy(state, DEPROVISION);
	}
	sprintf(cmd, "%s %s %s", UPDATE_VM_SCRIPT, target_uuid, state);
	system (cmd);
}

main(
int argc, char **argv)
{

	vxt_db_start_arg_t         start_db_args;
	struct xs_handle *xs;
	char    token;
	char   **vec;
	char   *tail;
	int    ret;
	int    parse_result;
	int    num;
	int    domid;
	int    auth_dev;
	fd_set set;
	int    fd;
	char   target_uuid[MAX_VXT_UUID] = " ";
	int is_provision = 0;

	if (daemon(0, 0) < 0) {
		perror("daemon");
	}

	auth_dev = open(VXT_AUTH_DEVICE_NAME, O_RDWR);
	if(auth_dev < 0) {
		printf("main: Failed to open the Vxt authorization" 
		       " database device\n");
	}

	/*
	 * Start the database if it hasn't already been 
	 * started
	 */
	
	strncpy(start_db_args.db_file_name,
	        VXT_DB_DEFAULT_FILE_NAME, VXT_DB_NAME_MAX);
	
	ioctl(auth_dev, VXT_AUTH_DB_START, (void *)(long)&start_db_args);


	/* printf("The domain device %s\n", xs_domain_dev()); */
	xs = xs_domain_open();
	if (!xs) {
		perror("xs_domain_open failure");
		exit(1);
	}

	ret = xs_watch(xs,  "/vm", "vxtToken");
	if (ret == 0) {
		perror("xs_watch failed\n");
		exit(1);
	}

	fd = xs_fileno(xs);

	while (1) {

		FD_ZERO(&set);
		FD_SET(fd, &set);

		/* 
		 * Poll for data 
		 * printf("Poll for data, 0x%x\n", fd);
		 */
		if ((ret = select(fd+1, &set, NULL, NULL, NULL)) >= 0) {
			/* printf("Watch point triggered ret = 0x%x\n", ret); */
			vec = xs_read_watch(xs, &num);
			printf("\n after read watch ");
			if (!vec) error();

			parse_result =
			   xsd_vm_name(vec[XS_WATCH_PATH], target_uuid);

			if (parse_result == 1) {
				char **list;
			  	char path[64];
				int  entry_cnt;
				int  i;
				/*
				 * get a list of all the domain ids, 
				 * then check the name of each one
				 * to find the id for the specified domain
				 */
				list = xs_directory(xs, XBT_NULL,
				                    "/local/domain",
				                    &entry_cnt);
				for (i = (entry_cnt-1); i > 0; i--) {
					char *rec_uuid;
					sprintf(path,
					        "/local/domain/%s/vm",
						list[i]);
					rec_uuid = xs_read(xs, XBT_NULL,
					                   path, NULL);
					if ( rec_uuid == NULL ) {
						continue;
					}
					if (strcmp(&(rec_uuid[4]),
					           target_uuid) == 0) {
						free(rec_uuid);
						domid = atoi(list[i]);
						break;
					}
					free(rec_uuid);
				}
				free(list);
				if (i == 0) {
					free(vec);
					continue;
				}
	

				xsd_write_auth_record(auth_dev, target_uuid);
				/* 
				printf("main: call vxt_auth_db on uuid = %s\n",
				       target_uuid);
				*/

				xsd_provision_controller(xs, domid);
				is_provision = 1;
				update_vm_state(target_uuid, is_provision);

			} else if (parse_result == 2) {
				/* treating this case as deprovision */
				is_provision = 0;
				update_vm_state(target_uuid, is_provision);
			} else {
				/* target_uuid is undefined
				 * don't call update_vm_state
				 */
			}
			free(vec);
		} else {
			break;
		}
	}

	printf("returned from xs_watch, xenstore exited\n");

}

