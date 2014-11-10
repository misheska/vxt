/*
 *
 * vxt_trace:
 *
 * vxt_trace logs events associated with a unique controler pair.
 * The trace mechanism itself is agnostic with respect to the
 * scope of its use and the number of instantiations.  The
 * system however, is inititialized and used out of the signal 
 * handling subsystem.  The locking and access are controlled
 * by the caller.
 *
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

/*
 * The following two defines are for UMI logging and they must
 * be correct or the logged serial number will be invalid and
 * misleading.  vxt_trace contains no logging primitives and so
 * has never been assigned component and subsystem designates.  The
 * defines are still needed however to satisfy the compiler.
 */

#define _VXT_COMPONENT_ 0  // use the NULL id
#define _VXT_SUBSYSTEM_ 0  // use the NULL id


#include <public/kernel/os/vxtcom_embedded_os.h>
#include <public/vxt_system.h>
#include <public/vxt_trace_entries.h>


/*
 *
 * vxt_trace_header_t:
 * This structure defines the runtime state
 * of an associated trace table including the present
 * record pointer and whether we are actively recording.
 * The vxt_trace facility uses a fixed buffer and 
 * wraps when the buffer is full, before the buffer
 * wraps for the first time, playback begins at entry
 * zero.  After wrap, playback begins with the entry
 * that sits under the next_record pointer, wrapping
 * around zero and coming up to the record just before
 * next_record.
 */

typedef struct vxt_trace_header {
	uint32_t halted;
	uint32_t wrapped;
	uint32_t next_record;
	uint32_t size;
} vxt_trace_header_t;



/*
 *
 * vxt_trace_create:
 *
 * A trace table is allocated and initialized.
 * The trace table is left in the off position
 * i.e. Any calls to vxt_trace result in NOOP.
 * There are no locks in the trace table itslef
 * syncronization is maintained by the caller.
 * It is expected that multiple trace operations
 * will be ongoing for different subsystems and
 * different instances of the same subsystems.
 * Future, cross analysis will call for the
 * addition of timestamp support.
 *
 * Returns:
 *		VXT_SUCCESS => table allocated, initialized
 *		               and in standby.
 *
 *		VXT_NOMEM => table allocation failed.
 *
 *		VXT_PARM => entry_count out of range
 */

int
vxt_trace_create(uint32_t entry_count, void **table)
{
	vxt_trace_header_t *header;

	if (entry_count > VXT_MAX_TRACE_TABLE_SIZE) {
		return VXT_PARM;
	}

	header = (vxt_trace_header_t *)
	         vxt_kmalloc(sizeof(vxt_trace_header_t) + 
	                     (sizeof(vxt_trace_entry_t) * entry_count),
	                     VXT_KMEM_KERNEL);

	if (header == NULL) {
		return VXT_NOMEM;
	}

	header->halted = 1;
	header->wrapped = 0;
	header->size = entry_count;
	header->next_record = 0;

	*table = (void *)header;

	return VXT_SUCCESS;
	
}


/*
 * 
 * vxt_trace_destroy:
 *
 * Free the resources associated with the
 * table passed.
 *
 * Note: all locking is done by the caller.
 *
 * Returns:
 *
 *		None
 *
 */

void
vxt_trace_destroy(void *table)
{
	vxt_kfree(table);
	return;
}


/*
 *
 * vxt_trace_query:
 *
 * Returns the size and the runtime state of the 
 * targeted trace table.
 *
 * Returns:
 *
 *		VXT_SUCCESS
 * 
 */

int
vxt_trace_query(void *table, vxt_trace_stats_t *info)
{
	vxt_trace_header_t *header;

	header = (vxt_trace_header_t *)table;

	info->halted = header->halted;
	info->wrapped = header->wrapped;
	info->next_record = header->next_record;
	info->table_size = header->size;

	return VXT_SUCCESS;
}


/*
 *
 * vxt_trace_reset:
 *
 * Set the next_record pointer back to zero, reset
 * the wrapped condition.
 *
 * vxt_trace_reset can only run when the trace buffer
 * is in the halted state.
 *
 * Returns:
 *
 *		VXT_SUCCESS => upon successful invocation
 *		VXT_BUSY - Trace Table is running
 *
 *
 */

int
vxt_trace_reset(void *table)
{
	vxt_trace_header_t *header;

	header = (vxt_trace_header_t *)table;

	if (header->halted == 0) {
		return VXT_BUSY;
	}

	header->halted = 1;
	header->next_record = 0;
	header->wrapped = 0;

	return VXT_SUCCESS;
}


/*
 *
 * vxt_trace_start:
 *
 * Enable the recording of trace requests
 *
 * Returns:
 *
 *		VXT_SUCCESS
 *
 *
 */

int
vxt_trace_start(void *table)
{
	vxt_trace_header_t *header;

	header = (vxt_trace_header_t *)table;

	header->halted = 0;

	return VXT_SUCCESS;

}


/* 
 *
 * vxt_trace_stop:
 *
 * Disable the recording of trace requests on the
 * targeted trace table.
 *
 * Returns:
 *
 *		VXT_SUCCESS
 *
 */

int
vxt_trace_stop(void *table)
{
	vxt_trace_header_t *header;

	header = (vxt_trace_header_t *)table;

	header->halted = 1;

	return VXT_SUCCESS;

}


/*
 * 
 * vxt_trace_log:
 *
 * vxt_trace_log records the information passed by the
 * caller in the next entry of the table targeted.  The
 * call is subject to the halted bit.  If halted is set
 * vxt_trace_log returns without taking action
 *
 * Returns:
 *
 *		NONE
 *
 */

void
vxt_trace_log(void *table, uint64_t cmd, uint64_t parm1, 
              uint64_t parm2, uint64_t parm3) 
{
	vxt_trace_entry_t  *entries;
	vxt_trace_header_t *header;

	header = (vxt_trace_header_t *)table;
	entries = (vxt_trace_entry_t *)(header + 1);
	
	if (header->halted != 0) {
		return;
	}

	entries[header->next_record].entry = cmd;
	entries[header->next_record].parm1 = parm1;
	entries[header->next_record].parm2 = parm2;
	entries[header->next_record].parm3 = parm3;

	header->next_record++;
	if (header->next_record == header->size) {
		header->wrapped = 1;
		header->next_record = 0;
	}

	return;
}


/*
 *
 * vxt_trace_playback:
 *
 * vxt_trace_playback copies the entries found in the targeted
 * trace table from the oldest to the youngest.  The number of
 * entries returned is either, all of those contained in the
 * trace table, or all of those that will fit in the callers
 * buffer, whichever is smaller.
 *
 *
 * Returns
 *
 *		VXT_SUCCESS
 */

int
vxt_trace_playback(void *table, 
                   vxt_trace_entry_t *buffer,
                   uint32_t *caller_count)
{
	uint32_t           count;
	uint32_t           playback_ptr;
	vxt_trace_entry_t  *entries;
	vxt_trace_header_t *header;

	header = (vxt_trace_header_t *)table;
	entries = (vxt_trace_entry_t *)(header + 1);

	if (header->wrapped == 1) {
		count = header->size;
	} else {
		count = header->next_record;
	}

	if (*caller_count < count) {
		count = *caller_count;
	} else {
		*caller_count = count;
	}

	if (header->next_record == 0) {
		playback_ptr = header->size - 1;
	} else {
		playback_ptr = header->next_record - 1;
	}

	if (count == 0) {
		return VXT_SUCCESS;
	}


	/*
	 * recording the results from back to front
	 * simplifies the initial placement of the playback
	 * pointer.
 	 */

	while (count > 0) {
		buffer[count - 1] =  entries[playback_ptr];
		if (playback_ptr != 0) {
			playback_ptr--;
		} else {
			playback_ptr = header->size - 1;
		}
		
		count--;
	}

	return VXT_SUCCESS;
}


