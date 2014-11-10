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



#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xs.h>
#include <sys/stat.h>
#include <libgen.h>

#define	MAX_PATH 512

void usage()
{
	printf("Usage: vxtcom-setup <DomId>\n");
	exit(1);
}

main(
int argc, char **argv)
{
	int domid;
	char path[MAX_PATH];
	char *domname;
	struct xs_handle *xs;
	int num, i;
	char **list;
	xs_transaction_t t;

	struct xs_permissions xsperms[2];
	xsperms[0].id = domid;
	xsperms[0].perms = XS_PERM_READ | XS_PERM_WRITE;
	struct stat buf;
	char s[MAX_PATH];
	char s2[MAX_PATH];
	char backend[MAX_PATH];
	char frontend[MAX_PATH];
	int devno = 0;
	int result;
	char* home;
	char* dom0_home;
	
	if (argc < 2) {
		usage();
		exit(1);
	}

	domname = argv[1];

	/*
 	 * Check the Prefix to make sure the string is in the proper form
	 * i.e. "Domain-" followed by a number
	 */

	/*
	 * "X" is where the bcd number goes
	 * "X" is not compared, only 7 characters are
	 * compared.  However, strncmp for linux gives
	 * an unexpected failure if the last character
	 * of the comparison is followed by a null when
	 * the user string is not.
	 */
	if(strncmp(domname, "Domain-X", 7) != 0) {
		/*
		 * User argment is not in proper form
		 */
		usage();
		exit(1);
	}

	/*
	 * Skip the prefix
	 */
	domname = domname + 7;

	xs = xs_domain_open();
	if (!xs) {
		perror("xs_domain_open failure");
		exit(1);
	}

	/*
	 * get a list of all the domain ids, then check the name of each one
	 * to find the id for the specified domain
	 */
	list = xs_directory(xs, XBT_NULL, "/local/domain", &num);
	for (i = 1; i < num; i++) {
		char *n;
		sprintf(path, "/local/domain/%s/domid", list[i]);
		n = xs_read(xs, XBT_NULL, path, NULL);
		if ( n == NULL ) continue;
		if (strcmp(n, domname) == 0) {
			domid = atoi(list[i]);
			free(n);
			break;
		}
		free(n);
	} 
	free(list);

	if (i == num) {
		usage();
		exit(1);
	}

	home = xs_get_domain_path(xs,domid);
	dom0_home = xs_get_domain_path(xs,0);

	t = xs_transaction_start(xs);
	sprintf(backend, "%s/backend/vxtcom_ctrlr/%d/%d", dom0_home,
		    domid, devno);
	sprintf(frontend, "%s/device/vxtcom_ctrlr/%d", home, devno);

	xs_mkdir(xs, t, backend);
	result = xs_set_permissions(xs, t, backend, xsperms, 1);

	sprintf(s, "%s/backend", frontend);
	xs_write(xs, t, s, backend, strlen(backend));

	sprintf(s, "%s/backend-id", frontend);
	xs_write(xs, t, s, "0", 1);

	sprintf(s, "%s/state", frontend);
	xs_write(xs, t, s, "1", 1);

	sprintf(s, "%s/domain", backend);
	xs_write(xs, t, s, domname, strlen(domname));

	sprintf(s, "%s/frontend", backend);
	xs_write(xs, t, s, frontend, strlen(frontend));

	sprintf(s, "%s/frontend-id", backend);
	sprintf(s2,"%d",domid);
	xs_write(xs, t, s, s2, strlen(s2));


	sprintf(s, "%s/state", backend);
	xs_write(xs, t, s, "1", 1);

	sprintf(s, "%s/frontend", backend);
	xs_write(xs, t, s, frontend, strlen(frontend));

	xs_transaction_end(xs, t, 0);
}

