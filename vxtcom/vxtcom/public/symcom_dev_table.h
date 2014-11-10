#ifndef VXTCOM_DEV_TYPES_H
#define VXTCOM_DEV_TYPES_H


typedef uint64_t vxtdev_type_t;

/*
 *
 * vxtcom_dev_types.h
 *
 * This file contains the list of device types which
 * have been defined for the vxt communications subsystem
 * types should not be added to this list except through
 * the prescribed Symantec registration service.
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
 * Device Types Enumeration
 */


#define vxtcom_devname(device) \
	((device) >> 32)
#define vxtcom_devtype(device) \
	((device) & 0xffffffff)

#define vxtcom_make_devtag(device, type) \
	(((device) << 32) | ((type & 0xffffffff)))


/*
 * Devices
 */
#define VXT_REMOTE_COM 0x00000001 /* simple com, no indirect data */
                                  /* shared memory based */

#define VXT_UNIVERSAL 0xFFFFFFFF  /* Matches all devices.  Used to */
                                  /* communicate a global device wait */
                                  /* instead of waiting for devices on */
                                  /* a single controller */


/*
 * Device Types
 */

/* VXT_REMOTE_COM */
#define VXT_RCOM_REV1 0x00000000



#endif /* VXTCOM_DEV_TYPES_H */
