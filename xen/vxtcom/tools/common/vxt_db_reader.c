/*
 *
 * vxt_db_reader:
 *
 * This module implements a tool to read vxt_authorization_database
 * records files.  During normal execution of the vxt_auth_db, records
 * from a db file are read into a memory image and queries, either 
 * at kernel or application level go through the auth_db driver.  This
 * tools is meant as a diagnostic, allowing inactive files to be tested
 * for integrity and their contents revealed.
 *
 * Because an active file may be undergoing changes while it is being
 * read by this tool, vxt_db_reader is only recommended for inactive
 * files.  However, in its read-only capacity it may be used on
 * active files with the caveat that the results may be udefined
 * due to parallel updates.
 *
 * vxt_db_reader also has the ability to clean database image files
 * of unwanted entries.  These calls should never be done on an
 * active file as they may result in file corruption and execution
 * failures of the vxtauth_db driver.
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



#include <vxt_tools.h>

#include <vxt_system.h>
#include <vxt_auth.h>
#include <vxt_db_reader.h>


char db_file_name[MAX_FILE_STRING];
char db_dev_name[MAX_VXT_UNAME];
char db_dev_name_length[MAX_FILE_STRING];
char db_dev_record[MAX_VXT_UNAME];





/*
 *
 * command_line_error:
 *
 * Called when an error in the format of command line arguments passed by the
 * user is detected.
 *
 *
 */

static int
command_line_error()
{
	printf("\n\n usage ... \n\n");
	printf("\tvxt_db_read OPTIONS[-f <file>]"
	       "[-r <device_name>[-l <name_length>]]\n%s",
	       "\t\t-f Provide database record filename\n"
	       "\t\t-r Provide data record device name for removal\n"
	       "\t\t\t -l optional device name compare length\n"
	       "\t\t\t    allows the removal of a class of devices\n"
	       "\t\t-i Invalidate a single record by number\n\n\n");
	return -1;
}


/*
 * vxtdb_parse_option_parm:
 *
 * vxtdb_parse_option_parm is a helper function for the
 * parse engine switch statement.  It parses the
 * string after the option character.  e.g. in "-fabc"
 * vxtdb_parse_option_parm is responsible for parsing
 * "abc".  It must be noted that "-f abc" is a different
 * case and only the space will be recognized below.
 * 
 */ 

static int
vxtdb_parse_option_parm(char **argv, int argc, char *parse_string,
                        int *option, int *token, int *index)
{
	int i = *token;
	int j = *index;

	if (!(*option)) {
		VXTPRINT(4, "where is \"-\"\n");
		return command_line_error();
	}
	if (*option == 1) {
		j++;
	}
	if (argv[i][j] != '\0') {
		while (argv[i][j] != '\0') {
			if (argv[i][j] == ' ') {
				j++;
				continue;
			} else {
				break;
			}
		}
		strncpy(parse_string, &(argv[i][j]), MAX_FILE_STRING);
		*option = 0;
		parse_string[MAX_FILE_STRING - 1] = 0;
		parse_string = NULL;
		VXTPRINT(4, "finished getting parameter string " "name\n");
		*token = ++i;
		*index = 0;
		return 0;
	}
	VXTPRINT(4, "Null char found\n");
	*option = 2;
	i++;
	if (i >= argc) {
		VXTPRINT(4, "i is larger than argc\n");
		return command_line_error();
	}
	*token = i;
	*index = 0;
	return 0;
}


/*
 * vxtdb_delete_records_for_device:
 *
 * vxtdb_delete_records_for_device takes the open file descriptor
 * passed, translates the global dev_name_length field into an
 * integer, and searches the open database file for records
 * with matching device name fields, (or substring matches
 * in the case of a non-zero dev_name_length field).
 *
 * vxtdb_delete_records_for_device relies on the implicit position
 * of the file pointer in authdev.  It is expected to be pointing
 * at the first record after the VxT authorization database file
 * header.
 *
 * Returns:
 *			0 => Upon successful file traversal
 *
 */

static int
vxtdb_delete_records_for_device(int authdev)
{
	int               record_count;
	int               read_cnt;
	int               name_length;
	vxt_db_file_rec_t db_record;
	uint64_t          offset;

	record_count = 0;
	if (db_dev_name_length[0] != '\0') {
		name_length = atoi(db_dev_name_length);
		VXTPRINT(3, "Class level name removal for prefix "
		         "of length %d\n", name_length);
	} else {
		name_length = MAX_VXT_UNAME;
	}
	while(TRUE) {
		read_cnt = vxt_file_read(authdev, &db_record, 
		                         sizeof(vxt_db_file_rec_t));

		if (read_cnt != sizeof(vxt_db_file_rec_t)) {
			VXTPRINT(3, "Reached end of database exiting ...\n");
			printf("\tCompleted scan and clean of records for "
			       "device %s\n", db_dev_name);
			return 0;

		}
		VXTPRINT(3, "Found Record, checking status, valid=%d\n",
		         db_record.priv.valid);

		/*
		 * Check the record entry device name field and
		 * invalidate the record if there is a match
		 */
		if ((db_record.priv.valid) &&
		    !strncmp(db_dev_name, db_record.dev_name, name_length)) {

			/*
			 * Clear the valid field and write the record
			 * back.
			 */
			offset = sizeof(vxt_db_file_rec_t) * record_count;
			offset+= sizeof(vxt_db_header_t);
			db_record.priv.valid = 0;
			vxt_file_pwrite(authdev, &db_record, 
	                                sizeof(vxt_db_file_rec_t), offset);

			printf("\tInvalidated record %d\n", record_count);
		}
		record_count++;

	}
}


/*
 *
 * vxtdb_delete_single_record:
 *
 * vxtdb_delete_single_record jumps to the offset of the record
 * indicated in the global variable db_dev_record.  db_dev_record
 * is a numeric string.  The offset in the file is found
 * by adding the file header size to the size of the records
 * preceeding the target.
 *
 * The record is deleted by clearing its "valid" field.
 *
 * Returns:
 *
 *		0 => Upon Successful invocation
 *		-1:  Unable to read target record at prescribed offset
 *	             or Null db_dev_record string variable
 *
 */

static int
vxtdb_delete_single_record(int authdev)
{
	int               record_count;
	int               read_cnt;
	vxt_db_file_rec_t db_record;
	uint64_t          offset;

	if (db_dev_record[0] != '\0') {
		record_count = atoi(db_dev_record);
		offset = sizeof(vxt_db_file_rec_t) * record_count;
		offset += sizeof(vxt_db_header_t);

		read_cnt =
		   vxt_file_pread(authdev, &db_record, 
		                  sizeof(vxt_db_file_rec_t), offset);

		if (read_cnt != sizeof(vxt_db_file_rec_t)) {
			VXTPRINT(3, "Record not in database, exiting ...\n");
			return -1;

		}
		VXTPRINT(3, "Found Record, checking status, valid=%d\n",
		         db_record.priv.valid);
		db_record.priv.valid = 0;

		vxt_file_pwrite(authdev, &db_record, 
                                sizeof(vxt_db_file_rec_t), offset);

		printf("\tInvalidated record %d\n", record_count);

		return 0;	
	}
	printf("\tInvalid record count parameter\n\n");
	return -1;
}


/*
 *
 * main:
 * 
 * Parse the incoming parameter strings and scan and print
 * the records in the targeted database if one of the record
 * remove options is not requested.
 *
 */

vxt_main(int argc, char **argv)
{
	int               authdev;
	int               option;
	char              *parse_string;
	vxt_db_header_t   db_header;
	vxt_db_file_rec_t db_record;
	int               read_cnt;
	uint64_t          version_major;
	uint32_t          version_minor;
	uint32_t          version_subminor;
	uint32_t          record_count;
	uint32_t          parse_length;
	int               ret;
	int i,j;

	printf("VXT_DB_READER version %d.%d\n",
	        VXT_DB_READER_VERSION_MAJOR, VXT_DB_READER_VERSION_MINOR);

	if (argc == 1) {
		VXTPRINT(2, "No caller options are present\n", argc);
	} else {
		VXTPRINT(2, "Caller options present %d parse tokens\n", argc);
	}


	/*
	 * Set default file name
	 */
	sprintf(db_file_name, "%s%s", VXT_DB_PATH, VXT_DB_DEFAULT_FILE_NAME);

	db_dev_name[0] = '\0';
	db_dev_record[0] = '\0';
	db_dev_name_length[0] = '\0';

	/*
	 * Scan the caller's options
	 */
	i = 1;
	j = 0;

	option = 0;
	while (i < argc) {
		while ((i < argc) && (argv[i][j] != '\0')) {
			VXTPRINT(4, "switch on, %c\n", argv[i][j]);
			switch(argv[i][j]) {
			case '-': {
				if (option) {
					VXTPRINT(4, "double option \"-\"\n");
					return command_line_error();
				}
				option = 1;
				j++;
				break;
			}
			case 'r': {
				/*
				 * Remove all records matching dev_name
				 */
				if (option == 1) {
					VXTPRINT(4, "case r\n");
					parse_string = db_dev_name;
				}
				ret = vxtdb_parse_option_parm(argv, argc, 
				                              parse_string,
				                              &option,
				                              &i, &j);
				if (ret != 0) {
					return ret;
				}
				if (option == '\0') {
					parse_string = NULL;
				}
				break;
			}
			case 'l': {
				if (option == 1) {
					VXTPRINT(4, "case l\n");
					parse_string = db_dev_name_length;
				}
				ret = vxtdb_parse_option_parm(argv, argc, 
				                              parse_string,
				                              &option,
				                              &i, &j);
				if (ret != 0) {
					return ret;
				}
				if (option == '\0') {
					parse_string = NULL;
				}
				break;
			}
			case 'i': {
				if (option == 1) {
					VXTPRINT(4,
					         "case i, invalidate record\n");
					parse_string = db_dev_record;
				}
				ret = vxtdb_parse_option_parm(argv, argc, 
				                              parse_string,
				                              &option,
				                              &i, &j);
				if (ret != 0) {
					return ret;
				}
				if (option == 0) {
					parse_string = NULL;
				}
				break;
			}
			case 'f': {
				if (option == 1) {
					VXTPRINT(4, "case f\n");
					parse_string = db_file_name;
				}
				ret = vxtdb_parse_option_parm(argv, argc, 
				                              parse_string,
				                              &option,
				                              &i, &j);
				if (ret != 0) {
					return ret;
				}
				if (option == 0) {
					parse_string = NULL;
				}
				break;
			}
			default:
			{
				VXTPRINT(4, "default, i %d, j %d\n",i,j);
				if (option) {
					VXTPRINT(4, "in option parsing\n");
					if ((parse_string == db_file_name) ||
					    (parse_string == db_dev_name_length)
					    || (parse_string == db_dev_record)
					    || (parse_string == db_dev_name)) {
						VXTPRINT(4,
						         "in filestring "
						         "aquisition\n");
						strncpy(parse_string,
						        &(argv[i][j]),
						        MAX_FILE_STRING);
						option = 0;
						j=0;
						i++;
						parse_string
						   [MAX_FILE_STRING - 1] = 0;
						parse_string = NULL;
						VXTPRINT(4, "finished getting "
						         "filename after "
							 "space\n");
						break;
					}
				}
				return command_line_error();
			}
			if (i >= argc) {
				break;
			}
			}
		}
		option = 0;
			j = 0;
			i++;
	}


	/*
	 * Open database file
	 */
	authdev = 0;
	if ((db_dev_name[0] != '\0') || (db_dev_record[0] != '\0')) {
		vxt_ssize_t read_size;
		vxt_ssize_t len;
		char        *line = NULL;

		printf("\n\n\tWarning!\n");
		printf("\tYou are attempting to update a "
		       "VxT authorization database file.\n");
		printf("\tIf this file is currently being used by the "
		       "VxT authorization\n");
		printf("\tdriver, please abort.\n");
		printf("\n\tAre you sure you wish to continue?\n"
		       "\tPress Y to proceed:  ");
		read_size = getline(&line, &len, stdin);
		if ((line[0] != 'Y') && (line[0] != 'y')) {
			return 0;
		}
		vxt_free(line);
		/*
		 * We are attempting to update the database
		 * open it only EXCLUSIVE and for read/write
		 */
		VXTPRINT(2, "Attempt to open database %s for read and write\n",
		         db_file_name);
		authdev = vxt_file_open(db_file_name, O_EXCL | O_RDWR);
		if(authdev <= 0) {
        		VXTPRINT(0, "Failed to open the Vxt authorization "
        			 "database file %s\n", db_file_name);
			return -1;
		}
	} else {
		VXTPRINT(2, "Attempt to open database %s\n",  db_file_name);
		authdev = vxt_file_open(db_file_name, O_RDONLY);
	}
	if(authdev <= 0) {
        	VXTPRINT(0, "Failed to open the Vxt authorization "
        		 "database file %s\n", db_file_name);
	} else {
        	VXTPRINT(2, "Opening the Vxt authorization "
        		 "database file %s for reading, fid = %d\n",
		         db_file_name, authdev);
	}


	/*
	 * Read the database header
	 */

	read_cnt = vxt_file_read(authdev, &db_header, 
	                         sizeof(vxt_db_header_t));

	if (read_cnt != sizeof(vxt_db_header_t)) {
		VXTPRINT(0, "Database header read failed, exiting ...\n");
		return -1;
	}



	version_major = GET_VXT_DB_VER(db_header.version);
	version_minor = GET_VXT_DB_REL(db_header.version);
	version_subminor = GET_VXT_DB_MINOR(db_header.version);
	printf("\n\n\tDatabase file header\n\t\t%s\n", db_header.banner);
	printf("\t\tExtended Version field %d, \n\t\tVersion Field",
	       db_header.extended_version);
	printf("\n\t\t\tVersion %d\n\t\t\tRelease %d\n\t\t\tSubRelease %d\n",
	       version_major, version_minor, version_subminor);

	/*
	 * Check for compatible database file version
	 */
	if (version_major != GET_VXT_DB_VER(VXT_DB_VERSION)) {
		printf("\n\n\tThis instance of vxt_db_reader does not "
		       "support this version\n\tof the vxt authorization "
		       "database file.\n\n\tExiting ...\n\n\n");
		return -1;
	}


	if (db_dev_name[0] != '\0') {
		return vxtdb_delete_records_for_device(authdev);
	}

	if (db_dev_record[0] != '\0') {
		return vxtdb_delete_single_record(authdev);
	}


	printf("\n\n\tDatabase Records:\n\n");

	record_count = 0;
	while(TRUE) {
		read_cnt = vxt_file_read(authdev, &db_record, 
		                         sizeof(vxt_db_file_rec_t));

		if (read_cnt != sizeof(vxt_db_file_rec_t)) {
			VXTPRINT(3, "Reached end of database exiting ...\n");
			return 0;

		}

		printf("\t\tDatabase record number %d\n", record_count);
		if (!db_record.priv.valid) {
			printf("\t\t\tRecord invalid, skipping ...\n\n\n");
			record_count++;
			continue;
		}
		printf("\t\t\tDatabase record valid\n");
		if (db_record.priv.direct) {
			printf("\t\t\tDirect Device\n");
		} else {
			printf("\t\t\tIndirect, Proxied\n");
		}
		printf("\t\t\tClient Endpoint %s\n", db_record.client);
		printf("\t\t\tServer Endpoint %s\n", db_record.server);
		printf("\t\t\tDevice Name %s\n", db_record.dev_name);
		if (db_record.class_name_length) {
			printf("\t\t\tPrefix Class device match string "
			       "length: %lld\n", db_record.class_name_length);
		} else {
			printf("\t\t\tAbsolute Name Match\n");
		}
		printf("\t\t\tLabel 0x%llx\n", db_record.label);
	
		printf("\n");
		record_count++;
	}



}
