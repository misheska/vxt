/*
 * auth_db_test.c
 *
 * Test of VxT authorization service function.
 *
 * This module tests the basic function of the VxT authorization driver.
 * The test depends on the pre-loading of the vxtctrlr and vxtauth_db
 * modules and requires that it be running in a Client environment.  
 * This is done because it is easier to use generic names for clients and
 * servers.  The code being tested is common between the Dom0 and Guest
 * environments and so auth_db_test serves as a qualifier for both
 * situations.
 *
 * The VxT authorization server is a database with customized queries,
 * lookup mechanisms, and input and removal mechanisms.  These include:
 *
 *   . Disk file for long term configuration state tracking
 *   . In memory presence for fast lookup response
 *   . Specialized hashes for fast lookup based on client name
 *   . Specialized hashes for fast lookup based on server name
 *   . Differential lookup for client and server, making it
 *     possible to have records available from one or the
 *     other query.
 *   . Gang removal of records based on client name
 *   . Differential removal on gang action, allowing server
 *     hash to remain
 *   . Gang insertion of records based on a client name
 *   . Gang lookup based on a client name.
 *
 * In addition there are several authorization commands provided as
 * a convenience.  These are:
 *
 *   . A DataBase location query to determine if we are running
 *     on the client or Dom0
 *
 *   . A base UUID query to determine the name of the guest or Dom0
 *
 *
 * auth_db_test exercises all of these mechanisms and tests the results
 * for correctness.
 *
 * auth_db_test is chatty, charting its progress with output to standard in.
 * However, it also prints messages to standard error whenever an anomaly
 * crops up.  In this way, the caller may supress standard-in or re-direct
 * std-error to use as an exception mechanism for unexpected behavior. 
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



typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;



#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include <public/vxt_system.h>
#include <public/vxt_auth.h>

typedef struct test_record_struct {
	char client[MAX_VXT_UUID];
	char server[MAX_VXT_UUID];
	char dev[20];
	uint64_t label;
} test_record_struct_t;


/*
 *
 * authtest_readback_compare:
 *
 * authtest_readback_compare looks for the recovered_rec in the test_recs
 * array.  It uses the client name and the device name as the primary key.
 * Not finding the record is considered an error.
 *
 * After finding the record, the remaining fields are compared with the
 * test_recs entry.  Any differences are flagged as errors.
 *
 * authtest_readback_compare writes notification of any anomalies it finds
 * to std_error and then exits the program.
 *
 * Returns:
 *
 *		None.
 *
 */

void
authtest_readback_compare(test_record_struct_t *test_recs,
                          vxt_db_file_rec_t *recovered_rec,
                          test_record_struct_t *base_rec)
{
	int j;

	/*
	 * Check for data corruption or lost info
	 * We have read all the records pretaining to
	 * base_rec->client.  Therefore, the client
	 * field should match base_rec.
	 */
	if (strncmp(base_rec->client,
	            recovered_rec->client, MAX_VXT_UUID)) {
		printf("\n\t\t ******WARNING:  Mismatch on "
		       "client field  **********\n\n");
               	fprintf(stderr, "VxT Authorization Test: "
		        "Auth Device VXT_AUTH_DB_DUMP_GUEST\n" 
		        "\tmismatch in client field.  "
			"expected %s, got %s\n",
		        base_rec->client, recovered_rec->client);
		exit(1);
	}


	/*
	 * Use base_rec->client and dev field as our 
	 * primary key and do a search to find the record
	 * in our list of test records
	 */
	for (j = 0; j < 15; j++ ) {
    		if (!strncmp(test_recs[j].client,
	            recovered_rec->client, MAX_VXT_UUID)) {
			if (!strncmp(test_recs[j].dev,
			    recovered_rec->dev_name, MAX_VXT_UNAME)) {
				break;
			}
		}
	}
	if (j == 15) {
		printf("\n\t\t ******Warning: Record read from "
		       "database does not match any of our test "
		        "records\n\n");
               	fprintf(stderr, "VxT Authorization Test: "
		        "Auth Device VXT_AUTH_DB_DUMP_GUEST\n"
		        "\tClient/dev primary key not found.  "
			"client %s, device %s\n",
		        recovered_rec->client, recovered_rec->dev_name);
		exit(1);
	}
	/*
	 * Test for correctness of other record fields.
	 * Note: We are depending on key uniqueness for client/dev
	 * in our test record database.
	 */
	if (strncmp(recovered_rec->server,
	            test_recs[j].server,  MAX_VXT_UUID) ||
	    (recovered_rec->label != test_recs[j].label) ||
	    (recovered_rec->priv.direct != 0)) {
		printf("\n\t\t ******Warning: Record read from "
		       "database has a corrupted field\n\n");
               	fprintf(stderr, "VxT Authorization Test: "
		        "Auth Device VXT_AUTH_DB_DUMP_GUEST\n"
		        "\tCorrupted field.  "
			"recovered: server %s, label %lld, direct %d,"
		        "expected: server %s, label %lld, direct 0\n",
		        recovered_rec->server, recovered_rec->label,
			recovered_rec->priv.direct, 
		        test_recs[j].server, test_recs[j].label);
		exit(1);
	}
	return;
}


/*
 *
 * authtest_test_record_retrieve:
 *
 * authtest_test_record_retrieve will check the state of a target
 * record in the database.  Attempts to look up the record on the
 * client and server side hash are made.   The parameters, "client_hash"
 * and "server_hash" tell authtest_test_record_retrieve whether the caller
 * believes there are lookup hashes available for the client and server
 * name associated with the target record.  If there is a mismatch between
 * the caller's expectation and the lookup result it is flagged as an error.
 *
 * authtest_test_record_retrieve writes notification of any anomalies it finds
 * to std_error and then exits the program.
 *
 * Returns:
 *		None
 *
 *
 */

void
authtest_test_record_retrieve(int authdev, vxt_db_file_rec_t *target,
                              int client_hash, int server_hash)
{
        vxt_db_lookup_client_arg_t lookup_arg;
	int                        ret;

        strncpy(lookup_arg.requester,
	        target->client, MAX_VXT_UNAME);
        strncpy(lookup_arg.dev_name, target->dev_name, 20);
        ret = ioctl(authdev, VXT_AUTH_DB_CLIENT_LOOKUP,
	            (void *)(long)&lookup_arg);
	/*
	 * The conditional below is just an exclusive or
	 * the two equality statments were needed to 
	 * force the compiler to view the oeprands
	 * as logicals
	 */
	if ((client_hash == 1) ^ (ret == 0)) {
		if (ret) {
			printf("\n\t\t ******WARNING:  Client side hash "
			       "lookup failed **********\n\n");
                	fprintf(stderr, "VxT Authorization Test: "
			        "Auth Device VXT_AUTH_DB_CLIENT_LOOKUP "
			        "Client hash for targeted record is missing "
			        "ret = %d\n", ret);
		} else {
			printf("\n\t\t ******WARNING:  Lookup Succeeded after "
			       "removing client hash **********\n\n");
	                fprintf(stderr, "VxT Authorization Test: "
			        "Auth Device VXT_AUTH_DB_CLIENT_LOOKUP "
			        "We found a record after removing the client "
			        "hash, ret = %d\n", ret);
		}
		exit(1);
	}

        strncpy(lookup_arg.requester,
	        target->server, MAX_VXT_UNAME);
        strncpy(lookup_arg.dev_name, target->dev_name, 20);
        ret = ioctl(authdev, VXT_AUTH_DB_CLIENT_LOOKUP,
	            (void *)(long)&lookup_arg);

	if ((server_hash == 1) ^ (ret == 0)) {
		if (ret) {
		printf("\n\t\t ******WARNING:  Server side Record "
		       "Lookup FAILED **********\n\n");
                fprintf(stderr, "VxT Authorization Test: "
		        "Auth Device VXT_AUTH_DB_CLIENT_LOOKUP "
		        "\n\tserver side hash failed, ret = %d\n", ret);
		} else {
		printf("\n\t\t ******WARNING:  Server side hash "
		       "lookup succeeded **********\n\n");
                fprintf(stderr, "VxT Authorization Test: "
		        "Auth Device VXT_AUTH_DB_CLIENT_LOOKUP "
		        "\n\tserver side hash unexpectedly succeeded ",
		        "ret = %d\n", ret);
		}
		exit(1);
	}
}


main()
{

	test_record_struct_t test_recs[] = {
		{ "client1", "server1", "dev1", 12341 },
		{ "client2", "server1", "dev1", 12342 },
		{ "client3", "server1", "dev1", 12343 },
		{ "client4", "server1", "dev1", 12344 },
		{ "client5", "server1", "dev1", 12345 },
		{ "client6", "server1", "dev1", 12346 },
		{ "client7", "server1", "dev1", 12347 },
		{ "client7", "server1", "dev2", 12348 },
		{ "client7", "server1", "dev3", 12349 },
		{ "client7", "server1", "dev4", 123410 },
		{ "client8", "server2", "dev1", 123411 },
		{ "client9", "server2", "dev1", 123412 },
		{ "client10", "server2", "dev1", 123413 },
		{ "client11", "server2", "dev1", 123414 },
		{ "client12", "server2", "dev1", 123415 }
	};
        int authdev;
        vxt_db_insert_arg_t insert_arg;
        vxt_db_lookup_client_arg_t lookup_arg;
        vxt_db_delete_rec_arg_t delete_arg;
	vxt_db_query_arg_t query_guest_args;
	vxt_db_local_uuid_arg_t uuid_arg;
        volatile vxt_db_dump_guest_arg_t dump_guest_args;
        volatile vxt_db_restore_guest_arg_t restore_guest_args;
        volatile char rec_buf[4096];
        vxt_db_file_rec_t *record_list;
        int i,j;
	int ret;

        printf("\n\n\nVxT Authorization Database Functional Test:\n");
        authdev = open("/dev/vxtdb-auth", O_RDWR);
        /* printf("authdev = 0x%x\n", authdev); */
        if(authdev < 0) {
                printf("Failed to open the Vxt authorization "
                       "database device\n");
                fprintf(stderr, "VxT Authorization Test: "
		        "Failed to open the Vxt authorization "
                        "database device\n");
		exit(1);
        }

	/*
	 * Use the DB Query function to determine where we are running
	 */

        ret = ioctl(authdev, VXT_AUTH_DB_QUERY, 
	            (void *)(long)&query_guest_args);
	if (ret) {
		printf("\n\n\t\t ******WARNING:  Query for guest status failed"
		       "**********\n\n");
                fprintf(stderr, "VxT Authorization Test: "
		        " Query for guest status failed\n");
		exit(1);
	} else {
		if (query_guest_args.hub) {
		   printf("\n\t\tThe test program is running on a HUB.  \n"
		          "\t\tThis test is not designed to "
		          "run on a hub.\n"
		          "\t\tExiting...\n");
                   fprintf(stderr, "VxT Authorization Test: "
		           " Guest test detected running on a hub, exiting\n");
		   exit(1);
		} else {
		   printf("\t\tThe test program is running on a SPOKE, "
		          "continuing\n\n");
		}
	}

	/*
	 * Test local UUID determination mechanism
	 */
 
        uuid_arg.uuid[0] = 0;
        ret = ioctl(authdev, VXT_AUTH_GET_LOCAL_UUID, (void *)(long)&uuid_arg);
	if (ret) {
		printf("\n\t\t ******WARNING:  Unable to determine local UUID"
		       "**********\n\n");
                fprintf(stderr, "VxT Authorization Test: "
		           " VxT Auth device failed to find local UUID\n");
		exit(1);
	}
	printf("  ****** LOCAL UUID: %s  ******\n", uuid_arg.uuid);
 

	/*
	 * Test basic insert, read, and delete actions
	 */
        insert_arg.record.class_name_length=0;
	strncpy(insert_arg.record.client, test_recs[0].client, MAX_VXT_UUID);
        strncpy(insert_arg.record.server, test_recs[0].server, MAX_VXT_UUID);
        insert_arg.record.priv.valid = 1;
        insert_arg.record.priv.direct = 1;
        insert_arg.record.label = test_recs[0].label;
        strncpy(insert_arg.record.dev_name, test_recs[0].dev, MAX_VXT_UNAME);
        insert_arg.flags = (VXT_DB_INSERT_CLIENT | VXT_DB_INSERT_SERVER);


        ret = ioctl(authdev, VXT_AUTH_DB_INSERT, (void *)(long)&insert_arg);
	if (ret) {
		printf("\n\t\t ******WARNING:  Record Insert FAILED"
		       "**********\n\n");
                fprintf(stderr, "VxT Authorization Test: "
		        "VxT Auth Device VXT_AUTH_DB_INSERT returned an "
		        "error: %d\n", ret);
		exit(1);
	}


	/*
	 * Attempt to read the record we have just inserted
	 */
        strncpy(lookup_arg.requester,
                test_recs[0].client, MAX_VXT_UNAME);
        strncpy(lookup_arg.dev_name, test_recs[0].dev, 20);
        ret = ioctl(authdev, VXT_AUTH_DB_CLIENT_LOOKUP,
	            (void *)(long)&lookup_arg);
	if (ret) {
		printf("\n\t\t ******WARNING:  Record Lookup FAILED"
		       "**********\n\n");
                fprintf(stderr, "VxT Authorization Test: "
		        "Auth Device VXT_AUTH_DB_CLIENT_LOOKUP "
		        "failed, ret = %d\n", ret);
		exit(1);
	}

	printf("\nReading back first record written: show by field\n\n");
	printf("\tClient: expected.. %s   got %s\n",
	       test_recs[0].client, lookup_arg.record.client);
	printf("\tServer: expected.. %s   got %s\n",
	       test_recs[0].server, lookup_arg.record.server);
	printf("\tDevice Name: expected.. %s   got %s\n",
	       test_recs[0].dev, lookup_arg.record.dev_name);
	printf("\tLabel: expected.. %lld   got %lld\n",
	       test_recs[0].label, lookup_arg.record.label);

	if (strncmp(test_recs[0].client, 
	            lookup_arg.record.client, MAX_VXT_UUID) ||
	    strncmp(test_recs[0].server,
	            lookup_arg.record.server, MAX_VXT_UUID) ||
	    strncmp(test_recs[0].dev,
	            lookup_arg.record.dev_name, MAX_VXT_UUID) ||
	    !(test_recs[0].label == lookup_arg.record.label)) {

		printf("\n\t\t ******WARNING:  TEST FAILED**********\n\n");
                fprintf(stderr, "VxT Authorization Test: "
		        "Record Corruption detected on read of simple "
		        "write/read test\n");
		exit(1);
	} else {
		printf("\n\t\tTest Completed\n\n");
	}


	printf("\nDelete test record ...\n\n");
        strncpy(delete_arg.client,
                test_recs[0].client, MAX_VXT_UUID);
        strncpy(delete_arg.server, test_recs[0].server, MAX_VXT_UUID);
        strncpy(delete_arg.dev_name, test_recs[0].dev, 20);
        ret = ioctl(authdev, VXT_AUTH_DB_DELETE_REC, (void *)(long)&delete_arg);
	if (ret) {
		printf("\n\t\t ******WARNING:  Record Delete "
		       "Failed**********\n\n");
                fprintf(stderr, "VxT Authorization Test: "
		        "Auth Device VXT_AUTH_DB_DELETE_REC "
		        "failed, ret = %d\n", ret);
		exit(1);
	} else {
		printf("\n\t\t Delete completed\n\n");
	}

	/*
	 * Now test gang record actions starting with gang client removal
	 */

	printf("\nLoading records for client 7 for move client tests\n\n");

	for (i = 6; i < 10; i++) {
        	insert_arg.record.class_name_length=0;
		strncpy(insert_arg.record.client,
		        test_recs[i].client, MAX_VXT_UUID);
        	strncpy(insert_arg.record.server,
		        test_recs[i].server, MAX_VXT_UUID);
        	insert_arg.record.priv.valid = 1;
        	insert_arg.record.priv.direct = 0;
        	insert_arg.record.label = test_recs[i].label;
        	strncpy(insert_arg.record.dev_name,
		        test_recs[i].dev, MAX_VXT_UNAME);
        	insert_arg.flags = (VXT_DB_INSERT_CLIENT
		                    | VXT_DB_INSERT_SERVER);


        	ret = ioctl(authdev, VXT_AUTH_DB_INSERT,
		            (void *)(long)&insert_arg);
		if (ret) {
			printf("\n\t\t ******WARNING:  Record Insert "
			       "FAILED **********\n\n");
                	fprintf(stderr, "VxT Authorization Test: "
			        "Auth Device VXT_AUTH_DB_INSERT "
			        "failed, ret = %d\n", ret);
			exit(1);
		}
	
	}
	printf("\n\t\t Multiple record insert completed\n\n");
        printf("Guest record removal stage 1: "
	       "Client Hash delete (migrate action)\n");
	printf("\nRead the records with client name = %s and remove "
	       "client side hash lookups\n"
	       "as is done in a migration action\n\n",
	       test_recs[6].client);

	/*
	 * Now attempt to read back the client records and remove them
	 * from the database
	 */

        strncpy((char *)dump_guest_args.guest,
	        test_recs[6].client, MAX_VXT_UUID);
        dump_guest_args.buf_size = 10;
        dump_guest_args.overrun = 0;
        dump_guest_args.delete = 1;
        dump_guest_args.migrate = 1;
        dump_guest_args.record_buffer = (void *)rec_buf;

        ioctl(authdev, VXT_AUTH_DB_DUMP_GUEST, (void *)(long)&dump_guest_args);

        record_list = (vxt_db_file_rec_t *)(dump_guest_args.record_buffer);
        printf("Number of records recovered %d\n", dump_guest_args.buf_size);
        printf("Reading guest records: ...\n\n");
        for (i = 0; i < dump_guest_args.buf_size; i++) {
                printf("\t Record %d\n", i);
                printf("\t\t client: %s\n", record_list[i].client);
                printf("\t\t server: %s\n", record_list[i].server);
                printf("\t\t dev_name: %s\n", record_list[i].dev_name);
                printf("\t\t label 0x%x\n", record_list[i].label);
                printf("\t\t direct %d\n", record_list[i].priv.direct);

		/*
		 * Check for data corruption or lost info
		 * We have read all the records pretaining to
		 * test_recs[6].client.  Therefore, the client
		 * field should match test_recs[6]
		 */
		authtest_readback_compare(test_recs, &(record_list[i]),
		                          &(test_recs[6]));

	/*
 	 * Make sure we actually removed our client hash records
	 * The following client lookup should fail
	 *
	 * In the next test we will attempt to lookup a record
	 * using the client hash.  This should fail as we
	 * used the migrate flag above.
	 *
	 * After this we will attempt to lookup the record using
	 * the server hash.  This should succeed.
	 *
	 */
	authtest_test_record_retrieve(authdev, &(record_list[i]), 0, 1);

        }

        printf("Restore Guest record client hashes\n");
	/*
	 * Now do standard insert on top of partial deleted guest.
	 * i.e. The client hashes are gone.  This will test the
	 * handling of the migrated guest on top of insert.  We
	 * would not expect to see this in a running system but
	 * it is a good test of proper auth system functioning.
	 */
	for (i = 6; i < 10; i++) {
        	insert_arg.record.class_name_length=0;
		strncpy(insert_arg.record.client,
		        test_recs[i].client, MAX_VXT_UUID);
        	strncpy(insert_arg.record.server,
		        test_recs[i].server, MAX_VXT_UUID);
        	insert_arg.record.priv.valid = 1;
        	insert_arg.record.label = test_recs[i].label;
        	strncpy(insert_arg.record.dev_name,
		        test_recs[i].dev, MAX_VXT_UNAME);
        	insert_arg.flags = (VXT_DB_INSERT_CLIENT
		                    | VXT_DB_INSERT_SERVER);


        	ret = ioctl(authdev, VXT_AUTH_DB_INSERT,
		            (void *)(long)&insert_arg);
		if (ret) {
			printf("\n\t\t ******WARNING:  Record Insert on top "
		               "of migrated record FAILED"
			       "******\n\n");
			return;
		}
	
	}

        printf("Guest Restore complete\n\n");

	/*
	 * records should be present and retrievable by either server or
	 * client lookup
	 */
        for (i = 0; i < dump_guest_args.buf_size; i++) {
		authtest_test_record_retrieve(authdev,
		                              &(record_list[i]), 1, 1);
	}

	/*
	 * read back the client records again and remove them
	 * from the database.  This time do a full delete, not
	 * just a migrate
	 */

        strncpy((char *)dump_guest_args.guest,
	        test_recs[6].client, MAX_VXT_UUID);
        dump_guest_args.buf_size = 10;
        dump_guest_args.overrun = 0;
        dump_guest_args.delete = 1;
        dump_guest_args.migrate = 0;
        dump_guest_args.record_buffer = (void *)rec_buf;

        ret = ioctl(authdev, VXT_AUTH_DB_DUMP_GUEST,
	            (void *)(long)&dump_guest_args);
	if (ret) {
		printf("\n\t\t ******WARNING:  Dump Guest action Failed" 
		       "******\n\n");
		return;
	}

        record_list = (vxt_db_file_rec_t *)(dump_guest_args.record_buffer);
        printf("Guest record removal stage 2: Full record delete\n");
	printf("\nRead the records with client name = %s and remove "
	       "client and \n"
	       "server side hash lookups as is done in a "
	       "standard delete action\n\n",
	       test_recs[6].client);

        printf("Number of records recovered %d\n", dump_guest_args.buf_size);
        printf("Reading guest records: ...\n\n");
        for (i = 0; i < dump_guest_args.buf_size; i++) {
                printf("\t Record %d\n", i);
                printf("\t\t client: %s\n", record_list[i].client);
                printf("\t\t server: %s\n", record_list[i].server);
                printf("\t\t dev_name: %s\n", record_list[i].dev_name);
                printf("\t\t label 0x%x\n", record_list[i].label);
                printf("\t\t direct %d\n", record_list[i].priv.direct);

		authtest_readback_compare(test_recs, &(record_list[i]),
		                          &(test_recs[6]));
		authtest_test_record_retrieve(authdev, &(record_list[i]), 0, 0);

        }

        printf("\n\t\t Test Completed\n");


	printf("\nGuest Record Restore\n\n");


        /*
         * restore that which we just took away
         */
        strncpy((char *)restore_guest_args.guest,
                test_recs[6].client, 40);
        restore_guest_args.migrate = 1;
        restore_guest_args.record_buffer =  (void *)rec_buf;
        restore_guest_args.buf_size = dump_guest_args.buf_size;

        ret = ioctl(authdev, VXT_AUTH_DB_RESTORE_GUEST,
                    (void *)(long)&restore_guest_args);

	if (ret) {
		printf("\n\t\t ******WARNING:  Restore Guest action Failed" 
		       "******\n\n");
		return;
	}

	printf("\nCheck Database to see that records have been restored\n");
        for (i = 0; i < dump_guest_args.buf_size; i++) {
                printf("\t Record %d\n", i);
                printf("\t\t client: %s\n", record_list[i].client);
                printf("\t\t server: %s\n", record_list[i].server);
                printf("\t\t dev_name: %s\n", record_list[i].dev_name);
                printf("\t\t label 0x%x\n", record_list[i].label);
                printf("\t\t direct %d\n", record_list[i].priv.direct);

		authtest_test_record_retrieve(authdev, &(record_list[i]), 1, 0);

        }

        printf("\n\t\t Test Completed\n");


	printf("\nClean-up test records ...\n\n");
	for (i = 6; i < 10; i++) {
        	strncpy(delete_arg.client,
		        test_recs[i].client, MAX_VXT_UUID);
        	strncpy(delete_arg.server, test_recs[i].server, MAX_VXT_UUID);
        	strncpy(delete_arg.dev_name, test_recs[i].dev, 20);
        	ret = ioctl(authdev, VXT_AUTH_DB_DELETE_REC,
		            (void *)(long)&delete_arg);
		if (ret) {
			printf("\n\t\t ******WARNING:  Record Delete "
			       "Failed**********\n\n");
		}
	}

	printf("\n\t\t Delete completed\n\n");
	printf("\nTesting completed\n\n");

	
	return;
}


