/*
 * This Function Gets the UUID of the local domain 9windows guest.
 *
 * input parameters: Pointer to a buffer of size BUFSIZ. The memory for the
 *                   buffer is allocated by the caller.
 * Return value    : 0 for success; -1 for failure.
 */

int get_my_uuid(char *inBuf);