#ifndef _VXT_DB_READER_
#define _VXT_DB_READER_

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



#define VXT_DB_READER_VERSION_MAJOR 1
#define VXT_DB_READER_VERSION_MINOR 0


static inline uint32_t
pread(int fd, void *buf, size_t count, uint64_t offset)
{
	uint32_t               read_cnt;
	
	_lseeki64(fd, offset, SEEK_SET);
	read_cnt = (uint32_t) _read(fd, buf, count);
  return read_cnt;
}

static inline uint32_t
pwrite(int fd, void *buf, size_t count, uint64_t offset)
{
	uint32_t               write_cnt;
	
	_lseeki64(fd, offset, SEEK_SET);
	write_cnt = (uint32_t) _write(fd, buf, count);
  return write_cnt;
}

#define LINE_WIDTH 80

static inline vxt_ssize_t
getline (char **linebuf, size_t *size, FILE *fp)
{
  size_t i = 0;

  if (linebuf == NULL || size == NULL || fp == NULL){
      return -1;
  }
  if (*linebuf == NULL || *size == 0){
      *size = LINE_WIDTH;
      if (NULL == (*linebuf = (char *) malloc (*size))){
	        return -1;
	    }
  }
  while(1){
      for(; i<*size-2 && '\n'!= (*linebuf[i]=getc(fp)); i++);
      if('\n' == *linebuf[i]){
          break;
      }
      *size += LINE_WIDTH;
      if( NULL == (*linebuf = (char *)realloc( *linebuf, *size ))){
          return -1;
      }       
  }
  *linebuf[i+1] = 0;
  return i;
}

#endif /* _VXT_DB_READER_ */

