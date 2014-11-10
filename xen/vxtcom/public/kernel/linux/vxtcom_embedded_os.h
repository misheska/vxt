#ifndef VXT_COM_EMBEDDED_OS_H
#define VXT_COM_EMBEDDED_OS_H

#define VXTPRINT_PRODUCT_LEVEL 0
#define VXTPRINT_PROFILE_LEVEL 2
#define VXTPRINT_BETA_LEVEL 3
#define VXTPRINT_ALPHA_LEVEL 4
#define VXTPRINT_DEBUG_LEVEL   7
#define VXTPRINT_DEBUG2_LEVEL  8

/*
#define VXT_DEBUG_LEVEL VXTPRINT_DEBUG_LEVEL
*/
#define VXT_DEBUG_LEVEL VXTPRINT_PRODUCT_LEVEL

#ifndef ASSERT
#define ASSERT(condition) BUG_ON(!(condition))
#endif


/*
 * vxtcom_embedded_os.h
 *
 * This file contains structures that are embedded in
 * os independent structures.  It must go before
 * the os independent vxtctrlr header files and therefore
 * must be split from vxt_com_os.h which must come after
 * the os independent files.
 *
 * vxtcom_embedded_os.h is a public file in that it is
 * used by library modules as well as the vxt_controller
 * vxt_com_embedded_os contains wrappers for os_specific
 * utilities as well as embbeded structures, allowing
 * for library module os independence.
 */

/*
 * Include only the ".h" files which are needed by
 * this file for O.S. specific fields and therefore
 * cannot be included directoy by O.S. independent
 * "c" files.
 */
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <xen/evtchn.h>


/*
 * Files below are needed for bus header file
 * remove when bus header file is moved into
 * os/hypervisor specific directory
 */
#include <xen/gnttab.h>
#include <xen/driver_util.h>


#define vxtarch_word long

#define vxt_domid_t domid_t




/*
 * VXTPRINT will support debug levels in future through VXT_DEBUG_LEVEL
 */



#define VXTPRINT(level,a,b...) \
	do { if ((level) <= VXT_DEBUG_LEVEL)                       \
	     printk(KERN_ALERT "%s: " a, __FUNCTION__ , ## b);     \
	} while (0)


/*
 *
 * see ~public/vxt_system.h for designations
 */

#ifndef _VXT_COMPONENT_
#error "VxT component designation is missing\n"
#endif
#ifndef _VXT_SUBSYSTEM_
#error "VxT subsystem designation is missing\n"
#endif

#define UMI_LOG(umi,level,a,b...)                                             \
	do { if ((level) <= VXT_DEBUG_LEVEL)                                  \
	     printk(KERN_ALERT "VxT Info V-320-%d-%d-%d %s: " a,              \
	     _VXT_COMPONENT_, _VXT_SUBSYSTEM_, umi, __FUNCTION__ , ## b);     \
	} while (0)
#define UMI_WARN(umi,level,a,b...)                                            \
	do { if ((level) <= VXT_DEBUG_LEVEL)                                  \
	     printk(KERN_ALERT "VxT WARNING V-320-%d-%d-%d %s: " a,           \
	     _VXT_COMPONENT_, _VXT_SUBSYSTEM_, umi, __FUNCTION__ , ## b);     \
	} while (0)
#define UMI_ERROR(umi,level,a,b...)                                           \
	do { if ((level) <= VXT_DEBUG_LEVEL)                                  \
	     printk(KERN_ALERT "VxT ERROR V-320-%d-%d-%d %s: " a,             \
	     _VXT_COMPONENT_, _VXT_SUBSYSTEM_, umi, __FUNCTION__ , ## b);     \
	} while (0)

/*
 *
 * vxtcom_wait_queue_t
 *
 * Encapsulate the os dependent
 * structure for thread wait
 */

struct vxtcom_wait_queue {
	 wait_queue_head_t wq;
};


#define VXT_SPIN_LOCK_UNLOCKED SPIN_LOCK_UNLOCKED

#define VXT_DEFINE_SPINLOCK(x)			\
	vxt_slock_t x = { VXT_SPIN_LOCK_UNLOCKED };

typedef struct vxt_slock {
	spinlock_t lock;	
} vxt_slock_t;

typedef unsigned long vxt_sflags_t;


/*
 * vxt_slock:
 *
 * Wrapper for os specific
 * mutex lock routine.
 *
 * Note: we pass the flags field by reference however
 * the underlying linux support takes a by-value parameter
 * in a macro call.  The by-reference is better suited to
 * other implementations that might not rely on macros.
 *
 * Returns:
 *
 *	None
 *
 */

static inline void
vxt_slock(vxt_slock_t *lock, vxt_sflags_t *flags)
{
	spin_lock_irqsave(&(lock->lock), *flags);
}


/*
 * vxt_sunlock:
 *
 * Wrapper for os specific
 * mutex unlock routine.
 *
 * Returns:
 *
 *	None
 *
 */

static inline void
vxt_sunlock(vxt_slock_t *lock, vxt_sflags_t flags)
{
	spin_unlock_irqrestore(&(lock->lock), flags);
}


/*
 * vxt_slock_init:
 *
 * Wrapper for os specific
 * mutex lock init routine.
 *
 * Returns:
 *
 *	None
 *
 */

static inline void
vxt_slock_init(vxt_slock_t *lock)
{
	lock->lock = VXT_SPIN_LOCK_UNLOCKED;
}






typedef struct vxt_mlock {
	struct mutex os_mlock;
} vxt_mlock_t;


/*
 * vxt_mlock_init:
 *
 * Wrapper for os specific
 * mutex lock init routine.
 *
 * Returns:
 *
 *	None
 *
 */

static inline void
vxt_mlock_init(vxt_mlock_t *lock)
{
	mutex_init(&(lock->os_mlock));
}


/*
 * vxt_mlock:
 *
 * Wrapper for os specific
 * mutex lock routine.
 *
 * Returns:
 *
 *	None
 *
 */

static inline void
vxt_mlock(vxt_mlock_t *lock)
{
	mutex_lock(&(lock->os_mlock));
}


/*
 * vxt_munlock:
 *
 * Wrapper for os specific
 * mutex unlock routine.
 *
 * Returns:
 *
 *	None
 *
 */

static inline void
vxt_munlock(vxt_mlock_t *lock)
{
	mutex_unlock(&(lock->os_mlock));
}

#define VXT_KMEM_KERNEL GFP_KERNEL

/*
 *
 * vxt_kmalloc:
 *
 * vxt_kmalloc is a wrapper for the OS
 * specific kernel memory allocation
 * routine.
 *
 * Returns:
 *
 *	A "void *" pointer to the region
 * 	of memory allocated/provided to
 *	the caller.  The value returned
 *	is NULL if the memory request
 *	cannot be accomodated.
 *
 */

static inline void *
vxt_kmalloc(uint64_t size, uint32_t flags)
{
	return kmalloc((size_t)size, (gfp_t)flags);
}


/*
 *
 * vxt_kzalloc:
 *
 * vxt_kzalloc is a wrapper for the OS
 * specific kernel memory allocation
 * routine, that includes an initialization
 * of the memory space returned.  The
 * space is initialized with zeros.
 *
 * Returns:
 *
 *	A "void *" pointer to the region
 * 	of memory allocated/provided to
 *	the caller.  The value returned
 *	is NULL if the memory request
 *	cannot be accomodated.  The
 *	memory returned is initialized
 *	to zero.
 *
 */

static inline void *
vxt_kzalloc(uint64_t size, uint32_t flags)
{
	return kzalloc((size_t)size, (gfp_t)flags);
}

/*
 *
 * vxt_kfree:
 *
 * vxt_kfree is a wrapper for the OS
 * specific kernel memory allocation
 * free routine
 *
 * Returns:
 *
 *	None
 *
 */

static inline void
vxt_kfree(void *buf)
{
	kfree(buf);
}

static inline void
vxtcom_event_signal(unsigned int signal)
{
	notify_remote_via_evtchn(signal);
}


/*
 *
 * vxtcom_strncpy:
 *
 * Modifies the behavior of strncpy so that
 * a NULL/0 is always placed in the last
 * position of the destination string, regardless
 * of the length of the source string.
 *
 */

static inline char *
vxtcom_strncpy(char *s1, char *s2, size_t cnt)
{
	char *ret;
	ret = strncpy(s1, s2, cnt);
	s1[cnt-1] = 0;
	return ret;
}


extern int
vxtcom_mmap(struct file *file, struct vm_area_struct *vma);

/*
 *
 * vxtdev_queues:
 *
 * Tracks the queue resource abstraction
 * on an internal device structure.  The
 * structure is used to retrieve a queue
 * for cross domain sharing, task level
 * page mapping, and device resource
 * reclaim.
 *
 */

typedef struct vxtdev_queues {
	void *refs;
	void *vm_area;
	void *qaddr;
	int pg_cnt;
	uint32_t active_mapping;
	struct vxtdev_queues *next;
} vxtdev_queues_t;



#endif /* VXT_COM_EMBEDDED_OS_H */

