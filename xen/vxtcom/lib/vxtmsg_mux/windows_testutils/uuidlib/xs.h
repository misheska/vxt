#ifndef _XS_H_
#define _XS_H_

#include <windows.h>

#ifdef XSAPI_EXPORTS
#define XS_API __declspec(dllexport)
#else
#define XS_API __declspec(dllimport)
#endif


#if defined (__cplusplus)
extern "C" {
#endif

//
// Desc: xs_domain_open() get a handle to the xenbus device.
// Return: Valid Windows file handle if successful. NULL otherwise.
//
XS_API HANDLE xs_domain_open();

//
// Desc: Close the handle from xs_domain_open().
// Aborts any pending transaction.
//
XS_API void xs_daemon_close(HANDLE hXS);

//
// Desc: Start a transaction. 
// Parameter: hXS -> handle to xenstore.
//
// Return: True on success, false on error.
// Note that there can only be one transaction outstanding on a given
// handle at any time.
XS_API BOOL xs_transaction_start(HANDLE hXS);
 
//
// Desc: Commit or abort a transaction
// Parameter:
//
// Return: True if successful. False otherwise.
XS_API BOOL xs_transaction_end(HANDLE hXS, BOOL fAbort);
 
//
// Desc: Write the value of a file
// Parameter:
//                                
// Return: True if successful. False otherwise.
XS_API BOOL xs_write( HANDLE hXS, const char *path, const char *data);

//
// Get contents of a directory.
// Returns a malloced array: call free() on it after use.
// Num indicates size.
//
XS_API char **xs_directory(HANDLE hXS, const char *path, unsigned int *num);

//
// Get the value of a single file, nul terminated.
// Returns a malloced value: call free() on it after use.
// len indicates length in bytes, not including terminator.
//
XS_API void *xs_read(HANDLE hXS, const char *path, size_t *len);

//
// Remove a node from the store.
// Return True if successful, False otherwise.
//
XS_API BOOL xs_remove( HANDLE hXS, const char *path);

//
// Create a watch on a xenstore node.  Arranges that the event will
// be signalled whenever the node changes.
// Returns a handle to the watch, or -1 on error.
//
XS_API int xs_watch( HANDLE hXS, const char *path, HANDLE event);

//
// Remove a watch which was previously created with xs_watch.
// Returns TRUE on success or FALSE on error.
//
XS_API BOOL xs_unwatch(HANDLE hXS, int handle);

#if defined (__cplusplus)
};
#endif

#endif // _XS_H_
