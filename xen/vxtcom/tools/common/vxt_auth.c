/*
 *
 * vxt_auth:
 *
 * vxt_auth provides the caller with the ability to create and
 * remove authorization records from the VxT authorization/routing
 * database.
 *
 * vxt_auth also makes use of the migration primatives to conveniently
 * and efficiently move authorization records for migrating guests.
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



#include <vxt_tools.h>
#include <vxt_auth_tool.h>

#include <vxt_system.h>
#include <vxt_auth.h>
#include <vxt_db_reader.h>


char *db_client;
char *db_server;
char *db_dev_name;
char *db_dev_name_length;
char *db_record_label;
char *db_option_flags;





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
	printf("\tvxt_auth -a [-s <server_uuid>][-c <client_uuid>]\n");
	printf("\t            [-d <device_name>][-l <name_match_length>]\n");
	printf("\t            [-o [client,server,both]]\n");
	printf("\tor: vxt_auth -r [-s <server_uuid>][-c <client_uuid>][-d <device_name>]\n");
	printf("\tor: vxt_auth -m  >>>> NOT IMPLEMENTED YET <<<<\n\n");
	printf("\tThere are default values for server_uuid, client_uuid,\n");
	printf("\toptions, and name match length.\n");
	printf("\tIf the hub is server or client this entry can be omited.\n"); 
	printf("\tName_match_length is zero by default, matching the "
	       "entire device name\n");
	printf("\tThe options flags are set to \"both\" by "
	       "default hashing both\n");
	printf("\tclient and server in the lookup trees.\n\n");
	printf("\tCommands:\n\n\t\t-a, --add a new authdb record\n");
	printf("\t\t-r, --remove an authdb record\n");
	printf("\t\t-m, --migrate a guests non-default records\n\n\n");


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


typedef enum vxt_auth_command {
	NONE,
	ADD,
	REMOVE,
	MIGRATE,
} vxt_auth_command_t;


/*
 *
 * main:
 * 
 * Parse the incoming parameter strings and call
 * the requested service routines.
 *
 */

vxt_main(int argc, char **argv)
{
	vxt_auth_command_t  command = NONE;
	vxt_db_insert_arg_t insert_arg;
	vxt_db_delete_rec_arg_t delete_arg;



	vxtctrlr_handle_t authdev;
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

	printf("\n\n\tVXT_AUTH version %d.%d\n",
	        VXT_AUTH_VERSION_MAJOR, VXT_AUTH_VERSION_MINOR);
	printf("\tSupport for active VxT Authorization Record Update\n\n");

	/*
	 * Allocate our parsed string buffers
	 */
	db_client = vxt_malloc(MAX_FILE_STRING);
	if (db_client == NULL) {
		printf("Unable to create internal parsing buffers\n");
		return -1;
	}
	db_server = vxt_malloc(MAX_FILE_STRING);
	if (db_server == NULL) {
		printf("Unable to create internal parsing buffers\n");
		return -1;
	}
	db_dev_name = vxt_malloc(MAX_FILE_STRING);
	if (db_dev_name == NULL) {
		printf("Unable to create internal parsing buffers\n");
		return -1;
	}
	db_dev_name_length = vxt_malloc(MAX_FILE_STRING);
	if (db_dev_name_length == NULL) {
		printf("Unable to create internal parsing buffers\n");
		return -1;
	}
	db_record_label = vxt_malloc(MAX_FILE_STRING);
	if (db_record_label == NULL) {
		printf("Unable to create internal parsing buffers\n");
		return -1;
	}
	db_option_flags = vxt_malloc(MAX_FILE_STRING);
	if (db_option_flags == NULL) {
		printf("Unable to create internal parsing buffers\n");
		return -1;
	}

	if (argc == 1) {
		VXTPRINT(2, "No caller options are present\n", argc);
	} else {
		VXTPRINT(2, "Caller options present %d parse tokens\n", argc);
	}


	/*
	 * Set default client and server names.  These are
	 * the endpoints that will be used for the new record
	 * unless the caller supplies one.
	 */
	strncpy(db_client, VXTDB_DOM0_ENDPOINT, MAX_VXT_UNAME);
	strncpy(db_server, VXTDB_DOM0_ENDPOINT, MAX_VXT_UNAME);
	/*
	 * Set the device name to NULL, the user must supply
	 * a device name
	 */
	db_dev_name[0] = '\0';
	db_dev_name_length[0] = '\0';
	db_record_label[0] = '\0';
	db_option_flags[0] = '\0';

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
			case 'a': {
				if (!option) {
					VXTPRINT(4, "where is the \"-\"\n");
					return command_line_error();
				}
				if (option == 2) {
					/*
					 * We just picked up the first
					 * char of an option string not
					 * an option, go to the option
					 * string reader.
					 */
					ret = 
					   vxtdb_parse_option_parm(argv, argc, 
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
				command = ADD;
				option = 0;
				i++;
				j=0;
				if (i > argc) {
					VXTPRINT(4, "i is larger than argc\n");
					return command_line_error();
				}
				break;
			}
			case 'r': {
				if (!option) {
					VXTPRINT(4, "where is the \"-\"\n");
					return command_line_error();
				}
				if (option == 2) {
					/*
					 * We just picked up the first
					 * char of an option string not
					 * an option, go to the option
					 * string reader.
					 */
					ret = 
					   vxtdb_parse_option_parm(argv, argc, 
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
				command = REMOVE;
				option = 0;
				i++;
				j=0;
				if (i > argc) {
					VXTPRINT(4, "i is larger than argc\n");
					return command_line_error();
				}
				break;
			}
			case 'm': {
				if (!option) {
					VXTPRINT(4, "where is the \"-\"\n");
					return command_line_error();
				}
				if (option == 2) {
					/*
					 * We just picked up the first
					 * char of an option string not
					 * an option, go to the option
					 * string reader.
					 */
					ret = 
					   vxtdb_parse_option_parm(argv, argc, 
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
				command = MIGRATE;
				option = 0;
				i++;
				j=0;
				if (i > argc) {
					VXTPRINT(4, "i is larger than argc\n");
					return command_line_error();
				}
				break;
			}
			case 'c': {
				/*
				 * client string follows
				 */
				if (option == 1) {
					parse_string = db_client;
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
			case 's': {
				/*
				 * server string follows
				 */
				if (option == 1) {
					parse_string = db_server;
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
			case 'd': {
				/*
				 * server string follows
				 */
				if (option == 1) {
					parse_string = db_dev_name;
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
				if (option == 0) {
					parse_string = NULL;
				}
				break;
			}

			case 'n': {
				if (option == 1) {
					VXTPRINT(4, "case n, "
					         "record name or label\n");
					parse_string = db_record_label;
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

			case 'o': {
				if (option == 1) {
					VXTPRINT(4, "case o, "
					         "option flags\n");
					parse_string = db_option_flags;
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
					if ((parse_string == db_client) ||
					    (parse_string == db_dev_name_length)
					    || (parse_string == db_server)
					    || (parse_string == db_record_label)
					    || (parse_string == db_option_flags)
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

	VXTPRINT(2, "Completed Parsing\n");

	/*
	 * Open the authorization database device
	 */
	authdev = vxt_dev_open(VXT_AUTH_DEVICE_NAME, O_RDWR);
	if(authdev < 0) {
		printf("Failed to open the Vxt authorization "
		       "database device, return = %p\n", 
		       (void *)(vxtarch_word)authdev);
	}


	switch (command) {
	case ADD: {
		int ret;

		VXTPRINT(2, "Attempting to add new record: client - %s, "
		         "server - %s, dev_name - %s, "
		         "dev_string_matching_length - %s\n",
		         db_client, db_server, db_dev_name, db_dev_name_length);
		if (!strncmp(db_client, db_server, MAX_VXT_UNAME)) {
			printf("\n\n\t The client and server endpoints "
			       "cannot be the same\n\n");
			return -1;
		}
		if (db_dev_name[0] == '\0') {
			printf("\n\n\t A device name must be specified\n\n");
		}
		strncpy(insert_arg.record.client, db_client, MAX_VXT_UNAME);
		insert_arg.record.class_name_length=atoi(db_dev_name_length);
		insert_arg.record.priv.valid = 1;
		insert_arg.record.label = vxt_atoll(db_record_label);
		strncpy(insert_arg.record.server, db_server, MAX_VXT_UNAME);
		strncpy(insert_arg.record.dev_name, db_dev_name, MAX_VXT_UNAME);
		if ((db_option_flags[0] == '\0') ||
		    !strncmp(db_option_flags, "both", MAX_FILE_STRING)) {
        		insert_arg.flags = 
			   (VXT_DB_INSERT_CLIENT | VXT_DB_INSERT_SERVER);
		} else if (!strncmp(db_option_flags, 
		           "server", MAX_FILE_STRING)) {
        		insert_arg.flags = VXT_DB_INSERT_SERVER;
		} else if (!strncmp(db_option_flags, 
		           "client", MAX_FILE_STRING)) {
        		insert_arg.flags = VXT_DB_INSERT_CLIENT;
		} else {
			printf("\n\n\tUnsupported Hash Option\n\n\n");
		}

		ret = vxt_dev_ioctl(authdev, VXT_AUTH_DB_INSERT,
		                    (void *)(vxtarch_word)&insert_arg);

		if (ret) {
			printf("\tRecord insert failed\n\n\n");
		} else {
			printf("\tRecord insert completed\n\n\n");
		}


		break;
	}
	case REMOVE: {
		int ret;
		VXTPRINT(2, "Attempting to remove a record: client - %s, "
		         "server - %s, dev_name - %s\n",
		         db_client, db_server, db_dev_name);

		strncpy(delete_arg.client, db_client, MAX_VXT_UNAME);
		strncpy(delete_arg.server, db_server, MAX_VXT_UNAME);
		strncpy(delete_arg.dev_name, db_dev_name, MAX_VXT_UNAME);

		ret = vxt_dev_ioctl(authdev, VXT_AUTH_DB_DELETE_REC, 
		                    (void *)(vxtarch_word)&delete_arg);
		if (ret) {
			printf("\tRecord removal failed\n\n\n");
		} else {
			printf("\tRecord removal completed\n\n\n");
		}
		break;
	}
	case MIGRATE: {
		printf("\t\t Migrate Command Not Implemented\n\n\n");
		break;
	}
	case NONE: {
		printf("\n\n\tMust specify a command: a,r, or m\n\n");
		break;
	}
	default: {
		printf("\n\n\tUnknown command request\n\n");
	}
	}

	

return 0;




}
