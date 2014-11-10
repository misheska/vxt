#ifndef _XS_H_
#define _XS_H_

#include <windows.h>

#ifdef XSAPI_EXPORTS
    #ifdef XSAPI_STATIC_LIB
    #define XS_API
    #else
    #define XS_API __declspec(dllexport)
    #endif
#else
    #ifdef XSAPI_STATIC_LIB
    #define XS_API  extern
    #else
    #define XS_API __declspec(dllimport)
    #endif
#endif


#if defined (__cplusplus)
extern "C" {
#endif

//
// Desc: xs_domain_open() get a handle to the xenbus device.
// Return: Valid Windows file handle if successful. NULL otherwise.
//
XS_API HANDLE __cdecl xs_domain_open(void);

//
// Desc: Close the handle from xs_domain_open().
// Aborts any pending transaction.
//
XS_API void __cdecl xs_daemon_close(HANDLE hXS);

//
// Desc: Start a transaction. 
// Parameter: hXS -> handle to xenstore.
//
// Return: A hint as to whether the transaction is likely to succeed
// or fail.  If this function returns FALSE, the transaction is likely
// to fail.  GetLastError() can be called to give an indication of
// why the transaction is likely to fail.
//
// xs_transaction_end() must always be called to end the transaction,
// regardless of the return value of this function.
//
// Only one transaction can be open on a given handle at any given
// time.
//
// Transactions apply to all data operations (read, write, remove,
// etc.), but not to non-data operations like watch and unwatch.
//
XS_API BOOL __cdecl xs_transaction_start(HANDLE hXS);
 
//
// Desc: Commit or abort a transaction
// Parameter:
//
// Return: True if successful. False otherwise.
//
// It is an error to call xs_transaction_end() without first calling
// xs_transaction_start(), and to call xs_transaction_end() twice
// without an intervening call to xs_transaction_start().
//
XS_API BOOL __cdecl xs_transaction_end(HANDLE hXS, BOOL fAbort);
 
//
// Desc: Write the value of a file.  The data is nul-terminated.
// Parameter:
//                                
// Return: True if successful. False otherwise.
XS_API BOOL __cdecl xs_write( HANDLE hXS, const char *path, const char *data);

//
// Desc: Write some data to a xenstore key.  The data may contain
//       embedded nuls.
// Parameter:
//                                
// Return: True if successful. False otherwise.
XS_API BOOL __cdecl xs_write_bin( HANDLE hXS, const char *path,
                                const void *data, size_t size);


//
// Get contents of a directory.
// Returns a malloced array: call xs_free() on it after use.
// Num indicates size.
//
XS_API char ** __cdecl xs_directory(HANDLE hXS, const char *path,
                                  unsigned int *num);

//
// Get the value of a single file, nul terminated.
// Returns a malloced value: call xs_free() on it after use.
// len indicates length in bytes, not including terminator.
//
XS_API void * __cdecl xs_read(HANDLE hXS, const char *path, size_t *len);

//
// Remove a node from the store.
// Return True if successful, False otherwise.
//
XS_API BOOL __cdecl xs_remove( HANDLE hXS, const char *path);

//
// Create a watch on a xenstore node.  Arranges that the event will
// be signalled whenever the node changes.
// Returns a handle to the watch, or -1 on error.
//
XS_API int __cdecl xs_watch( HANDLE hXS, const char *path, HANDLE event);

//
// Remove a watch which was previously created with xs_watch.
// Returns TRUE on success or FALSE on error.
//
// This will only fail if @handle is invalid.
//
XS_API BOOL __cdecl xs_unwatch(HANDLE hXS, int handle);

//
// Release memory allocated by xs_directory() or xs_read().  The
// semantics are similar to the C standard library free() function:
// the memory at @mem must have been previously allocated, and not
// already freed, and once xs_free() has been called any access to
// @mem is invalid.
//
XS_API VOID __cdecl xs_free(void *mem);

#if defined (__cplusplus)
};
#endif

#endif // _XS_H_
