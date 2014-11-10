#ifndef VXT_TRACE_ENTRIES_H
#define VXT_TRACE_ENTRIES_H

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
 *
 * vxt_trace_entries.h:
 *
 * This file is holds the definitions for signal trace commands.
 * The definitions include ascii strings, and parmeter counts to
 * impose on the trace log entries retrieved from the controller
 * playback ioctl.
 *
 * This is to be used by the application level log parser as well
 * as the trace subsystem.
 *
 */


#define VXT_TRACE_CNT 14



#define DECLARE_VXT_TRACE_ENTRIES					\
static char *vxt_trace_entries[VXT_TRACE_CNT] = {			\
	"vxtsig_controller_init, hash_obj %llx, ctrlr %llx\n",		\
	"vxtsig_hash_lookup called,  hash_obj %llx, handle %llx\n",	\
	"vxtsig_object_release called hash %llx, sigobj %llx\n",	\
	"vxtsig_signal called handle %llx, sig_cnt %llx\n",		\
	"vxtsig_check_pending_signals called, hashobj %llx\n",		\
	"vxtsig_interrupt called, hash_obj %llx, buffer %llx, int_cnt %llx\n", \
	"vxtsig_interrupt: wakeup sigobj %llx\n",			\
	"vxtsig_wait: hash_obj %llx, handle %llx\n",			\
	"vxtsig_wait: returning from wait: hash_obj %llx, sig_obj %llx, handle %llx\n", 	\
	"vxtsig_poll: hash_obj %llx, handle %llx\n",			\
	"vxtsig_signal: Block waiting to signal: hash_obj 0x%llx\n",	\
	"vxtsig_signal: Wakeup blocked and signal: hash_obj 0x%llx\n",	\
	"vxtsig_poll: Poll Return, sigobj 0x%llx, revents 0x%llx\n",	\
	"vxtcom_event_signal called, signaling ctrlr_obj %llx, sig_cnt %llx\n" \
	};

#define DECLARE_VXT_TRACE_PARMS						\
static int vxt_trace_parm_cnt[VXT_TRACE_CNT] = {			\
	2,								\
	2,								\
	2,								\
	2,								\
	1,								\
	3,								\
	1,								\
	2,								\
	3,								\
	2,								\
	1,								\
	1,								\
	2,								\
	2								\
	};



#define VXT_TRACE(table, cmd, a, b, c)  \
	vxt_trace_log(table, cmd, (uint64_t)a, (uint64_t)b, (uint64_t)c)


/*
 * Size measured in the number of
 * vxt_trace entries
 *
 */
#define VXT_MAX_TRACE_TABLE_SIZE 1024

typedef struct vxt_trace_stats {
	uint32_t halted;
	uint32_t wrapped;
	uint32_t next_record;
	uint32_t table_size;  /* in entries */
} vxt_trace_stats_t;

typedef struct vxt_trace_entry {
	uint64_t entry;
	uint64_t parm1;
	uint64_t parm2;
	uint64_t parm3;
} vxt_trace_entry_t;

/*
 * Note: vxt_trace entries are traditionally against
 * controller pairs.  These are accessed via a shared
 * device.
 */
typedef struct vxt_trace_playback_parms {
	void     *buffer;
	uint32_t device_handle;
	uint32_t entry_count;
} vxt_trace_playback_parms_t;



#endif /* VXT_TRACE_ENTRIES_H */
