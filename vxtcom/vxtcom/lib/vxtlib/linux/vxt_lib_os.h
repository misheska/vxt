
#ifndef VXT_LIB_OS_H
#define VXT_LIB_OS_H
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
 * vxt_lib_os.h:
 *
 * This file makes standard resources and defines available to
 * the O.S. independent layers.  If such defines are not available
 * in the targeted O.S. they are handled here directly.  i.e. if
 * uint64_t does not exist in stdint, we would define it directly
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>


#define vxtarch_word long

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

#define vxt_mutex_t pthread_mutex_t

#define vxt_lock_init(mutex_obj)                                \
	pthread_mutex_init(&mutex_obj, NULL)      


#define vxt_lock_destroy(mutex_obj) pthread_mutex_destroy(&mutex_obj)

#define vxt_lock(mutex_obj) pthread_mutex_lock(&mutex_obj)

#define vxt_unlock(mutex_obj) pthread_mutex_unlock(&mutex_obj)

#endif  /* VXT_LIB_OS_H */
