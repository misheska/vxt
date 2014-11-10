#ifndef VXT_MSG_OS_H
#define VXT_MSG_OS_H

/*
 *
 * vxt_msg_os.h:
 *
 * This file implements the os specific elements of the
 * VXT system message device library interface.  The message
 * device, (vxt_msg) is comprised of a driver and device
 * emulation as well as a user level interface.  The 
 * device emulation layer provides devices that reside
 * on the VXTcom controller bus.  To effect this, a kernel
 * level library exists that is registered with the vxt
 * controller.  Subsequent calls to device create on the
 * controller may invoke the library, creating or connecting to
 * vxt_msg devices.
 *
 * The purpose of this file is to supplement the user level
 * library that talks to the VXT controller and communicates
 * with the vxt_msg library through VXTcom system interfaces.
 *
 */

#include <linux/kdev_t.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
/*
#include <xenctrl.h>
*/

#include <sys/socket.h>
#include <assert.h>

#ifndef mb

#ifdef __i386__
#define mb()    __asm__ __volatile__ ("lock; addl $0,0(%%esp)": : :"memory")
#else
#define mb()    asm volatile("mfence":::"memory")
#endif

#endif


#define vxt_mb() mb()

/*
 * VXT_INVALID_DEV_OBJ:
 *
 * In the linux port this value is used to denote an
 * invalid file descriptor
 *
 */

#define VXT_INVALID_DEV_OBJ -1

/*
 *
 * VXT_DEV_DIR:
 *
 * The root directory that serves as a repository
 * for VXT controller and device files
 */

#define VXT_DEV_DIR "/dev/"


/*
 *
 * VXT_COM_DEV_NAME:
 * 
 * The device path and name of the first VXT
 * controller
 */

#define VXT_COM_DEV_NAME  "/dev/vxtctrlr0"


/*
 * VXT_COM_BUS_NAME
 *
 * The device path and name of the VXT
 * system bus in which the VXT controllers
 * are plugged.
 *
 */

#define VXT_COM_BUS_NAME  "/dev/vxtctrlr-bus"



/*
 * 
 * VXT_NO_TIMEOUT:
 *
 * The value provided to disable timeouts
 *
 */

#define VXT_NO_TIMEOUT -1



#define VXT_MSG_SIGNAL       0x1
#define VXT_MSG_WAIT         0x2
#define VXT_MSG_AUTO_STREAM  0x4


#define VXT_DEBUG_LEVEL 0

#define VXTPRINT_PRODUCT_LEVEL 0
#define VXTPRINT_DEBUG_LEVEL   1
#define VXTPRINT_LOG_LEVEL     2

/*
 * VXTPRINT will support debug levels in future through VXT_DEBUG_LEVEL
 */


#define VXTPRINT(level,a,b...)                                   \
        do { if ((level) <= VXT_DEBUG_LEVEL)                     \
             printf("%s: " a, __FUNCTION__ , ## b);              \
        } while (0)


#define VXTPRINT_ERROR 0


#define VXT_PERROR(a...)                                        \
	VXTPRINT(VXTPRINT_ERROR, a)


/*
 * UMI variant support
 */

#ifndef _VXT_COMPONENT_
#error "VxT component designation is missing\n"
#endif
#ifndef _VXT_SUBSYSTEM_
#error "VxT subsystem designation is missing\n"
#endif

#define UMI_LOG(umi,level,a,b...)                                             \
	do { if ((level) <= VXT_DEBUG_LEVEL)                                  \
	     printf("VxT Info V-320-%d-%d-%d %s: " a,                         \
	     _VXT_COMPONENT_, _VXT_SUBSYSTEM_, umi, __FUNCTION__ , ## b);     \
	} while (0)
#define UMI_WARN(umi,level,a,b...)                                            \
	do { if ((level) <= VXT_DEBUG_LEVEL)                                  \
	     printf("VxT WARNING V-320-%d-%d-%d %s: " a,                      \
	     _VXT_COMPONENT_, _VXT_SUBSYSTEM_, umi, __FUNCTION__ , ## b);     \
	} while (0)
#define UMI_ERROR(umi,level,a,b...)                                           \
	do { if ((level) <= VXT_DEBUG_LEVEL)                                  \
	     printf("VxT ERROR V-320-%d-%d-%d %s: " a,                        \
	     _VXT_COMPONENT_, _VXT_SUBSYSTEM_, umi, __FUNCTION__ , ## b);     \
	} while (0)

/*
 * File and object mapping protections
 */

#define VXT_PROT_READ  PROT_READ
#define VXT_PROT_WRITE PROT_WRITE

/*
 * Shared memory mapping flags
 */

#define VXT_MAP_SHARED MAP_SHARED

/*
 * object/file access and write request flags
 */

#define VXT_RDONLY O_RDONLY 
#define VXT_WRONLY O_WRONLY 
#define VXT_RDWR   O_RDWR

/*
 * object/file handle for VXTcom controllers
 * and the VXT system bus
 */

typedef int vxtctrlr_handle_t;

/*
 * 
 * vxt_msg_open_com
 *
 * OS dependent wrapper for object/file
 * open support
 *
 */

static inline vxtctrlr_handle_t
vxt_msg_open_com(char *name, unsigned int permissions)
{
   return (vxtctrlr_handle_t)open(name, permissions);
}

/*
 *
 * vxt_msg_open_dev
 *
 * OS dependent wrapper for device object open.
 * vxt_msg_open_dev differs from vxt_msg_open_com
 * in that an extra handle parameter is passed.
 * The handle represents an alternate way to 
 * establish connection.  The file name works
 * if there is a traditional file presence.  In
 * the case of some unix systems, the device file
 * may not be present at the time of this request
 * in this case, the handle is used to transmit
 * major and minor numbers, and a mknod
 * is invoked to create the device file.
 *
 */


static inline vxtctrlr_handle_t
vxt_msg_open_dev(char *name, unsigned int permissions, uint64_t handle)
{
	int ret;
	int major, minor;

   	ret = open(name, permissions);

	if (ret == VXT_INVALID_DEV_OBJ) {
		major = VXT_DEV_HANDLE_GET_PRIMARY(handle);
		minor = VXT_DEV_HANDLE_GET_SECONDARY(handle);
		ret = mknod (name, S_IFCHR | 600, MKDEV(major, minor));
		UMI_LOG(103, VXTPRINT_DEBUG_LEVEL,
		        "Failed to open device file %s, attempting "
		        "to create device file for major %d, minor %d, "
		        "ret = %d\n", name, major, minor, ret);
		ret = open(name, permissions);
		return (vxtctrlr_handle_t)ret;
	}

	return (vxtctrlr_handle_t)ret;
}


/*
 * 
 * vxt_msg_open_close
 *
 * OS dependent wrapper for object/file
 * close support
 *
 */

static inline void
vxt_msg_close_com(vxtctrlr_handle_t fd)
{
	close(fd);
}


/*
 * 
 * vxt_msg_ioctl
 *
 * OS dependent wrapper for user level
 * commands on an open object/file
 *
 */

static inline int
vxt_msg_ioctl(vxtctrlr_handle_t handle, uint64_t command, void *data)
{
   return ioctl(handle, command, data);
}


/*
 *
 * vxt_malloc:
 *
 * OS independent wrapper for space/buffer
 * allocation 
 *
 */

static inline void *
vxt_malloc(unsigned int size)
{
	return malloc(size);
}


/*
 *
 * vxt_free:
 *
 * OS independent wrapper for space/buffer
 * release 
 *
 */

static inline void 
vxt_free(void *buffer)
{
	free(buffer);
	return;
}

/*
 * vxt_mmap:
 *
 * This routine is provided as an os independent wrapper for the
 * insertion of vxt device memory a.k.a vxt shared memory into
 * the callers address space.  It may be implemented as an
 * mmap call where the O.S. supports the driver mmap mechanism
 * it may be implemented as a synchronous call into the kernel
 * if this is more appropriate.
 *
 */

static inline void *
vxt_mmap(void *starting_addr, uint32_t len, 
         int prot, int flags, vxtctrlr_handle_t fd, int64_t offset)
{
	return mmap(starting_addr, len, prot, flags, fd, offset);
}


/*
 * vxt_munmap:
 *
 * This routine is provided as an os independent wrapper for the
 * unmapping of VXT com shared memory space.  The shared memory
 * space may be ordinary ram or physical device memory.  It is 
 * provided by a messaging device through the abstraction of a
 * memory token.  The token is used on the mapping side of the
 * call.  The map routine associates the user address with
 * the underlying shared memory resource and the unmap call
 * takes the starting address of the region the shared memory
 * is found at.  This is in line with the semantics of the
 * linux/unix mmap capability.  It is also sufficient for systems
 * where there is no explicit built-in capaiblity and an
 * ioctl must be used.
 */

static inline void
vxt_munmap(void *addr, vxtctrlr_handle_t fd, uint64_t token, uint32_t len)
{
	munmap(addr, len);
	return;
}


static inline int
vxt_msg_event_wait(vxtctrlr_handle_t fd, int timeout, int *revent)
{
	struct pollfd fds[2];
	nfds_t nfds;

	fds[0].fd = fd;
	fds[0].events = 0xff;
	fds[0].revents = 0;
	nfds = 1;
	if (poll(fds, nfds, timeout) < 0) {
		if (errno == EBADF) {
			return (VXT_PARM);
		}
		if (errno == EFAULT) {
			return (VXT_PARM);
		}
		if (errno == EINTR) {
			return (VXT_ABORT);
		}
		if (errno == EINVAL) {
			return (VXT_PARM);
		}
		if (errno == ENOMEM) {
			return (VXT_NOMEM);
		}

	}
	*revent = fds[0].revents;
	return VXT_SUCCESS;
}


static inline void *
vxt_malloc_poll_obj(int object_count)
{
	return vxt_malloc(sizeof(vxt_poll_obj_t) * object_count);
}

static inline void
vxt_free_poll_obj(void *poll_object, int object_count)
{
	free(poll_object);
}

static inline void
vxt_write_event_wait(void *poll_obj_handle, int count,
                     uint64_t object, uint32_t events, uint32_t revents)
{
	struct pollfd *poll_obj = &((struct pollfd *)poll_obj_handle)[count];
	poll_obj->fd = object;
	poll_obj->events = events;
	poll_obj->revents = revents;
}


static inline void
vxt_read_event_wait(void *poll_obj_handle, int count,
                    uint64_t *object, uint32_t *events, uint32_t *revents)
{
	struct pollfd *poll_obj = &((struct pollfd *)poll_obj_handle)[count];
	*object = poll_obj->fd;
	*events = poll_obj->events;
	*revents = poll_obj->revents;
	
}

/*
 *
 * vxt_msg_cookie_check:
 *
 * This call is made on a vxt_msg_connect attempt.  It checks a field
 * at the end of the header to make certain that the device being connected
 * is indeed a vxt_msg device.  It also serves as a guarantor of queue
 * initialization.  A connecting guest should not read or write from a
 * device until the guest creating the device has written data and signaled
 * data available.  This protects against the race of connecting before
 * the queues are initialized.  Should this be violated, we may see a
 * transitory failure here.  To make the interface more robust in the face
 * of connecting guest transgression, we will wait for 1 second and try
 * again when a cookie mismatch occurs.
 *
 * Returns:
 *
 *	VXT_SUCCESS => Upon succesful cookie match
 *
 *	VXT_FAIL: ERRNO = EINVAL,  on cookie mismatch and timeout.
 *
 */

static inline int
vxt_msg_cookie_check (char *cookie, char *id, uint32_t namelen)
{
        /*
         * Check the queue initialization status
         * We have a vxt_msg level cookie at the
         * end of the header which must match
         */
        if (strncmp(cookie, id, namelen)) {
                VXTPRINT(0, "waiting for send queue initialization\n");
                sleep(1);
                if (strncmp(cookie, id, namelen)) {
                        VXTPRINT(0, "timeout waiting for send queue "
                                 "initialization, connect failed\n");
			errno = EINVAL;
                        return VXT_FAIL;
                }
        }
	return VXT_SUCCESS;
}


#endif  /* VXT_MSG_OS_H */
