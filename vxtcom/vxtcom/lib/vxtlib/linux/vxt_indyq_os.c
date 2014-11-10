/*
 *
 * vxt_indyq_os.c
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
 *
 * vxtiq_address_translate:
 *
 * Returns:
 *
 *		Translated address based on input address and
 *		input transform
 *
 */

static uint64_t
vxtiq_address_translate(uint64_t context, uint64_t mapping_id,
                        uint64_t src_addr)
{
	struct task_struct *task;
	struct kvm         *kvm;
	gfn_t              gfn;

	task = (task_struct *)context;
	kvm = (struct kvm *)mapping_id;
	gfn = (gfn_t)(src_addr >> PAGE_SHIFT);

	return (uint64_t)vxtcard_gfn_to_pfn(task, kvm, gfn);
}


/*
 *
 * vxtiq_address_free:
 *
 * It is required that all mapped addresses be freed when the 
 * caller is done with them, this is done via a put after a get
 * on a consume.  On the produce side it is more complicated.
 * The free is done on the put from the consumer.  i.e. the
 * mappings are chained.
 *
 * vxtiq_address_free unwires or otherwise releases resources
 * associated with the mapping which must remain fixed while a
 * translation is in effect.  
 *
 * Returns:
 *
 *		None
 */

static void
vxtiq_address_free(src_addr)
{
	kvm_release_page_clean(src_addr);
}


/*
 *
 * vxtiq_cell_copy_translate:
 *
 * vxtiq_cell_copy_translate is a helper routine.  The destination buffer is
 * actually free form buffer space that is defined by the nature of the data
 * being transferred.  i.e. if the indirect source list is expanded, the
 * destination will contain more cells in the form of a scatter list.  Otherwise
 * the translation is one to one.
 *
 * The number of source cells transferred depends on three independent 
 * variables, the cell count, the dst size, and the source size, with the
 * smallest being the final determinant of the transfer length.
 *
 * 
 * The src_len, dst_len, and cell_cnt are all updated cell_cnt is decremented
 * unless it is set to the streaming value.  If cell count is set to streaming
 * the last cell is set to the streaming value.  If this condition is detected
 * the size field of this cell is returned, otherwise zero is returned.
 *
 * Note: translate_onto is the direct, one for one source to destination
 * version.  Look to translate_expand for the scatter version. 
 */

static int
vxtiq_cell_copy_translate(vxt_indyq_cell_t *src, vxt_indyq_cell_t *dst,
                          uint64_t mapping_id, uint64_t context_id,
                          uint32_t page_size, uint32_t *src_len,
                          uint32_t *dst_len, uint64_t *cell_cnt)
{

	vxt_indyq_cell_t  *dst_ptr;
	vxt_indyq_cell_t  *src_ptr;
	uint32_t dst_off;
	uint32_t src_off;

	uint64_t total_cells;
	int follow_on_data;
	int streaming;
	int terminator;

	follow_on_data = 0;
	terminator = 0;
	total_cells = 0;
	dst_off = sizeof(vxt_indyq_cell_t);
	src_off = sizeof(vxt_indyq_cell_t);
	dst_ptr = dst;
	src_ptr = src;



	if (cell_cnt == VXTIQ_STREAMING_MSG) {
		streaming = 1;
	}


	while ((((void *)dst_off) <= *dst_len)
	       && ((void *)(src_off <= *src_len))) {
		if (streaming) {
			if (src_ptr->address == VXTIQ_STREAMING_MSG) {
				follow_on_data = src_ptr->size;
				dst_off += sizeof(vxt_indyq_cell_t);
				src_off += sizeof(vxt_indyq_cell_t);
				terminator = 1;
				break;
			}
		} else {
			if (*cell_cnt <= total_cells)  {
				break;
			}
			total_cells++;
		}

		if (page_size == 0) {
			dst_ptr->size = src_ptr->size;
			dst_ptr->address = 
		   	   vxtiq_address_translate(context, mapping_id,
			                           src_ptr->address);
		} else {
			uint32_t cell_size;
			uint64_t cell_address;
			cell_size = dst_ptr->size;
			if (cell_size/page_size >  
			    ((dst_len - dst_off) + sizeof(vxt_indyq_cell_t))) {
				/*
				 * "All or nothing at all"
				 * We don't want to track partial cells.
				 * This adds a caveat to scatter transfers.
				 * The destination buffer must be at least
				 * large enough to hold the scatter list of
				 * the largest contiguous cell transfer.
				 *
				 * Note: We could upgrade this by tracking
				 * partial transfers in the current_transfer
				 * complex.  But the only artifact is that
				 * any single untranslated transfer cannot
				 * have more scatter elements than the
				 * destination buffer size will hold.  
				 * Its probably not worth the extra complexity
				 * to remove the artifact.
				 */
				break;
			}
			cell_address = src_ptr->address;
			while (cell_size > 0) {
				/*
				 * It is also assumed that cell_address
				 * falls on a page boundary and the
				 * length is modulo page size
				 */
				
				dst_ptr->size = page_size;
				dst_ptr->address =
		   		   vxtiq_address_translate(context, mapping_id,
			                                   cell_address);
				cell_address += page_size;
				cell_size -= page_size;
				dst_off += sizeof(vxt_indyq_cell_t);
				dst_ptr++;
				
			}
			src_ptr++;
			src_off += sizeof(vxt_indyq_cell_t);
			continue;
		}


		dst_ptr++;
		src_ptr++;

		dst_off += sizeof(vxt_indyq_cell_t);
		src_off += sizeof(vxt_indyq_cell_t);
	}




	/*
	 * We need to back up one for our transfer lengths
	 * Case 1: failed the while
	 * Case 2: found a valid terminator cell but added increment explicitly
	 * Case 3: cell count exhausted
	 * All cases are advanced beyond end of transfer
	 */
	 */
	dst_off -= sizeof(vxt_indyq_cell_t);
	src_off -= sizeof(vxt_indyq_cell_t);
	*src_len = src_off;
	*dst_len = dst_off;

	if (cell_cnt != VXTIQ_STREAMING_MSG) {
		*cell_cnt -= total_cells;
	} else {
		if (terminator == 1) {
			/*
			 * We are finished with indirect scatter/gather
			 * list.  Indicate completion to caller
			 */
			*cell_cnt = 0;
		}
	}

	return follow_on_data;
	
}


/*
 *
 * vxt_indyq_translate:
 *
 * linux host version:  (hypervisor linux translate)
 *
 * vxt_indyq_translate copies a list of vxt_indyq_cells from the
 * source buffer to the destination buffer.  In moving them accross
 * the address is translated from guest physical to machine
 * phyaical based on the default map function. Any message provided
 * map function is ignored.  
 *
 *
 * No lock held on entry
 * 
 * Returns:
 *
 *		Number of bytes written to dest_buf
 *
 */


int
vxt_indyq_translate(vxt_indyq_header_t *current_transfer,
                    uint32_t *current_xfer_offset,
                    uint64_t mapping_id,
                    uint8_t *source_buf, uint32_t sbuf_size)
{
	vxt_indyq_header_t *msg_head;
	vxt_indyq_cell_t   *source_ele;
	vxt_indyq_cell_t   *smsg_cell;
	uint32_t           smsg_offset;
	uint32_t           sbuf_remaining;
	uint32_t           cell_cnt;
	uint8_t            *data_ptr;
	void               *map;
	int                byte_cnt;
	int                i;


	map = (void *)(vxtarch_word)mapping_id;

	translated_length = 0;
	sbuf_remaining = sbuf_size;
	data_ptr = source_buf;
	msg_head = current_transfer;
	while (sbuf_remaining) {
		byte_cnt = 0;
		/*
		 * We could support this but no reason for the extra logic
		 * User knows the header and should be able to request buffers
		 * large enough to accommodate it.
		 */
		if ((sbuf_remaining < sizeof(vxt_indyq_header_t))
		     && (*current_xfer_offset == 0)) {
			if (translated_length == 0)) {
			buffer too small for header!
			warning message needed here if translated length == 0;
			}
			return translated_length;
		}
		/*
		 * First thing, lets figure out where our header is
		 */
		if (*current_xfer_offset != 0) {
			/*
			 * Only happens at the top of a buffer on a continuing
			 * message:  ie when byte_cnt was 0 anyway
			 * We keep this accurate for xlate_copy case where the
			 * count will not match the buffer sent back by the
			 * underlying direct shared memory queue
			 */
			translated_length = *current_xfer_offset;
			if (sbuf_remaining >= msg_head->size) {
				translated_length += msg_head->size;
				*current_xfer_offset = 0;
			} else {
				translated_length += sbuf_remaining;
				*current_xfer_offset = sbuf_remaining;
			}
			/*
			 * We'll use this if data is indirect
			 */
			smsg_cell = (vxt_indyq_cell_t *)data_ptr;
		} else {
			*current_transfer = *(vxt_indyq_header_t *)data_ptr;
			if (msg_head->size >= sbuf_remaining) {
				translated_length += sbuf_remaining;
				*current_xfer_offset = sbuf_remaining;
			} else {
				*current_xfer_offset = 0;
				translated_length += msg_head->size;
			}
			/*
			 * We'll use this if data is indirect
			 */
			data_ptr += sizeof(vxt_indyq_header_t);
			smsg_cell = (vxt_indyq_cell_t *)data_ptr;
			msg_head->size -= sizeof(vxt_indyq_header_t);
			sbuf_remaining -= sizeof(vxt_indyq_header_t);
		}
		if (msg_head->flags.indirect) {
			cell_cnt = sbuf_remaining/sizeof(vxt_indyq_cell_t); 
			if (cell_cnt < msg_head->cell_count) {
				/*
				 * Partial buffer, save rest for later
				 */
				msg_head->cell_count -= cell_cnt;
			} else {
				/*
				 * Translate them all
				 */
				cell_cnt = msg_head->cell_count;
				msg_head->cell_count = 0;
			}
	
			if (msg_head->flags.explicit) {
				/*
				 * Explicit pointers are offsets into the
				 * buffer do not translate, leave checks to
				 * the caller.
				 * Since we do not necessarily have the 
				 * entire buffer here, some of the offsets
				 * may legitimately extend outside of our
				 * ranges, just copy
				 * Note: we are expecting msg_cells then data.
				 */
/*
while(cell_cnt != 0) {
	byte_cnt+=sizeof(vxt_indyq_cell_t);
	cell_cnt--;
}
*/
				byte_cnt += 
				   sizeof(vxt_indyq_cell_t) * cell_cnt;
			} else {
				/*
				 * Standard case, s/g list, translate to local
				 * physical addresses
				 */
	
				while(cell_cnt != 0) {
					smsg_cell->address =
					   gfn_to_pfn(map,
					              (smsg_cell->address
					               >> PAGE_SHIFT));
					smsg_cell++;
					byte_cnt+=sizeof(vxt_indyq_cell_t);
					cell_cnt--;
				}
	
			}
			msg_head->size -= byte_cnt;
			sbuf_remaining -= byte_cnt;
			data_ptr += byte_cnt;
		
			if ((msg_head->size && (byte_cnt == 0)) {
				/*
				 * Inline data to pick up, no intermediate
				 * buffer here so nothing to copy, just 
				 * update the pointers
				 */
	
				if (sbuf_remaining >= msg_head->size) {
					sbuf_remaining -= msg_head->size;
					data_ptr += msg_head->size;
				} else {
					msg_head->size -= sbuf_remaining;
					data_ptr += sbuf_remaining;
					sbuf_remaining = 0;
				}
			
			}
		} else {
			/*
			 * Direct data, explict ie local to the buffer
			 * No intermediate buffers in this case so no
			 * data copy, just update the pointers
			 */
			if (sbuf_remaining >= msg_head->size) {
				sbuf_remaining -= msg_head->size;
				byte_cnt += msg_head_size;
				data_ptr += msg_head_size;
			} else {
				msg_head->size -= sbuf_remaining;
				sbuf_remaining = 0;
				byte_cnt += sbuf_remaining;
				data_ptr += sbuf_remaining;
			}
	
		}
	}
	
	return translated_length;

}


/*
 *
 * vxt_indyq_copy_translate:
 *
 *
 */

int
vxt_indyq_copy_translate(vxt_indyq_header_t *current_transfer,
                         uint32_t *current_xfer_offset,
                         uint64_t mapping_id, uint64_t context_id,
                         uint32_t page_size,
                         uint8_t *source_buf, uint32_t sbuf_size,
                         uint8_t *dest_buf, uint32_t dest_buf_size)
{
	vxt_indyq_header_t *msg_head;
	vxt_indyq_header_t *dmsg_head;
	vxt_indyq_cell_t   *source_ele;
	vxt_indyq_cell_t   *smsg_cell;
	vxt_indyq_cell_t   *dmsg_cell;
	uint32_t           smsg_offset;
	uint32_t           dbuf_offset;
	uint32_t           sbuf_remaining;
	uint32_t           cell_cnt;
	uint8_t            *data_ptr;
	void               *map;
	int                byte_cnt;
	int                i;


	sbuf_remaining = sbuf_size;
	data_ptr = source_buf;
	dest_ptr = dest_buf;
	msg_head = current_transfer;
	dmsg_cell = NULL;
	smsg_cell = NULL;
	dbuf_offset = 0;
	smsg_offset = 0;

	/*
	 * Before we start the loop, check to see if we have only a
	 * partial header.  Non-guaranteed alignment between shared and
	 * local buffers makes this necessary.
	 * Note:  We have picked up the xfer_offset == 0 case here as
	 * well.
 	 * Important: We do not write to the destination buffer until
	 * we have the entire header.
	 */
	if (*current_xfer_offset < sizeof(vxt_indyq_header_t)) {
		dbuf_offset = 
		   (sizeof(vxt_indyq_header_t) - *current_xfer_offset);

		if((dbuf_offset > sbuf_size) || 
		   (dbuf_offset > dest_buf_size)) {
warning message here
			return 0;
		}

		/*
		 * Copy the rest of the header into our internal parsing
		 * state apparatus.
		 */
		memcpy(*current_transfer + *current_xfer_offset,
		       source_buf, dbuf_offset);
		/*
		 * Give the entire of the header to the caller
		 */
		memcpy(dest_buf,
		       *current_transfer,
		       sizeof(vxt_indyq_header_t);

		sbuf_remaining -= dbuf_offset;
		*current_xfer_offset += dbuf_offset;
		dest_ptr += sizeof(vxt_indyq_header_t);
		if ((page_size) && !(msg->head->explicit) &&
		    !(msg_head->flags.indirect)) {
			/*
			 * We will be expanding, set dmsg_head->size to
			 * streaming.
			 */
			dmsg_header = (vxt_indyq_header_t *)data_ptr;
			dmsg_header->size = VXTIQ_STREAMING_MSG;
			dmsg_head->cell_count = VXTIQ_STREAMING_MSG;
		}

		data_ptr += sizeof(vxt_indyq_header_t);
	}
	dmsg_head = NULL;


IMPORTANT  NEED to check for appropriate fields  (cell count, and 
message size, if here, they need to be set appropriatly, if not ignore)
	while (sbuf_remaining) {
		smsg_offset = 0;
		/*
		 * First thing, lets figure out where our header is
		 */
		if (*current_xfer_offset != 0) {
			/*
			 * Only happens at the top of a buffer on a continuing
			 * message:  ie when byte_cnt was 0 anyway
			 * We keep this accurate for xlate_copy case where the
			 * count will not match the buffer sent back by the
			 * underlying direct shared memory queue
			 */
/*  irrelevent in copy case
			if (sbuf_remaining >= msg_head->size) {
				*current_xfer_offset = 0;
			} else {
				*current_xfer_offset = sbuf_remaining;
			}
*/
			/*
			 * We'll use this if data is indirect
			 */
			smsg_cell = (vxt_indyq_cell_t *)data_ptr;
			dmsg_cell = (vxt_indyq_cell_t *)dest_ptr;
		} else {
			if (sbuf_remaining < sizeof(vxt_indyq_header_t)) {
				memcpy(*current_transfer,
				       source_buf + smg_offset,
				       sbuf_remaining);
				/*
				 * We used every byte of the source
				 * No need to update sbuf_size
				 */
				*dest_buf_size = *dest_buf_size - dbuf_offset;
				*current_xfer_offset += sbuf_remaining;
				smsg_offset += sbuf_remaining;
				break;
			} else if ((dest_buf_size - dbuf_offset)
			           < sizeof(vxt_indyq_header_t)) {
				/*
				 * Pick up from here on the next
				 * local buffer, (dest buffer) run
				 */
				*sbuf_size = *sbuf_size - sbuf_remaining;
				*dest_buf_size = *dest_buf_size - dbuf_offset;
				break;
			} else {
				/*
				 * Copy the next message header into
				 * our transfer apparatus state control
				 */
				*current_transfer =
				   *(vxt_indyq_header_t *)data_ptr;
				dmsg_head = (vxt_indyq_header_t *)
				            (dest_buf + dbuf_offset);
				*dmsg_head = *(vxt_indyq_header_t *)data_ptr;
				if ((page_size) && !(msg->head->explicit) &&
				    !(msg_head->flags.indirect)) {
					/*
					 * We will be expanding, set
					 * dmsg_head->size to streaming.
					 */
					dmsg_head->size = VXTIQ_STREAMING_MSG;
					dmsg_head->cell_count =
					   VXTIQ_STREAMING_MSG;
				}
CDY FIX up cell count and message size
				*current_xfer_offset += 
				   sizeof(vxt_indyq_header_t);
				dbuf_offset += sizeof(vxt_indyq_header_t);
				smsg_offset += sizeof(vxt_indyq_header_t);
				dest_ptr += sizeof(vxt_indyq_header_t);
				data_ptr += sizeof(vxt_indyq_header_t);
				sbuf_remaining -= sizeof(vxt_indyq_header_t);
			}
			/*
			 * We'll use this if data is indirect
			 */
			smsg_cell = (vxt_indyq_cell_t *)data_ptr;
			dmsg_cell = (vxt_indyq_cell_t *)dest_ptr;
		}
		if (msg_head->flags.indirect) {
			/*
			 * we update our local indication of cell_count so
			 * we don't have to calculate from translate_length,
			 * (current_xfer_offset) each time.
			 */
			cell_cnt = msg_head->cell_count;
	

			if (msg_head->flags.explicit) {
				uint32_t dbs;
			
				/*
				 * Explicit pointers are offsets into
				 * the buffer do not translate, leave 
				 * checks to the caller.
				 * Since we do not necessarily have the
				 * entire buffer here, some of the 
				 * offsets may legitimately extend
				 * outside of our ranges, just copy
				 *
				 * Note: we are expecting msg_cells
				 * then data.
				 */
				if (sizeof(vxt_indyq_cell_t) > dest_buf_size) {
					dbs = 0;
				} else {
					dbs = dest_buf_size
					      - sizeof(vxt_indyq_cell_t);
				}
				while ((cell_cnt != 0) &&
				       (sbuf_remaining != 0) && 
				       (dbuf_offset <= dbs)) {

					*dmsg_cell = *smsg_cell;

					dmsg_cell++;
					smsg_cell++
					dbuf_offset += sizeof(vxt_indyq_cell_t);
					sbuf_remaining -=
					   sizeof(vxt_indyq_cell_t);
					*current_xfer_offset +=
					   sizeof(vxt_indyq_cell_t);
					cell_cnt--;
				}
				msg_head->cell_count = cell_cnt;
			} else {
				/*
				 * Standard case, s/g list, translate to local
				 * physical addresses
				 */
NOTE: If we are streaming we will need the follow-on field as a substitute
for msg->size
If cell_cnt is 0 on return and we are streaming, follow_on_data is valid and substitutes for msg->size

Send cell_cnt = streaming if no valid msg->size
follow_on_data = 



				if (msg_head->size == VXTIQ_STREAMING_MSG) {
					uint32_t smsg_size;
					uint32_t dmsg_size;
				
					cell_cnt = VXTIQ_STREAMING_MSG
					smsg_size = sbuf_remaining;
					dmsg_size = dest_buf_size - dbuf_offset;
					follow_on_data = 
					   vxtiq_cell_copy_translate(
					      smsg_cell, dmsg_cell,
					      mapping_id, context_id,
					      page_size, &smsg_size,
					      &dmsg_size, &cell_cnt);
					/*
					 * smsg and dmsg size are updated
					 * and return the amount
					 * of buffer used.
					 */
					sbuf_remaining -= smsg_size;
					*current_xfer_offset += smsg_size;
					dbuf_offset += dmsg_size;
					data_ptr += smsg_size
					dest_ptr += dmsg_size;
				
					if (cell_cnt == 0) {
						/*
						 * Put terminator cell in
						 * destination.
						 */
						dmsg_size = 
						   dest_buf_size - dbuf_offset;
			
						if (dmsg_size >= 
						    sizeof(vxt_indyq_cell_t)) {
						   dmsg_cell = 
						      (vxt_indyq_cell_t *)
						      dest_ptr;
						   dest_ptr->address =
						      VXTIQ_STREAMING_MSG;
						   dest_ptr->size =
						      follow_on_data;
							
						   dbuf_offset +=
						      sizeof(vxt_indy_cell_t);
						   dest_ptr +=
						      sizeof(vxt_indy_cell_t);
						   /*
						    * We have finished the
						    * scatter/gather list
						    * translation
						    * Set the remaining size 
						    * in the message header for
						    * the copy of any remaining
						    * direct data
						    * Note: we measure remaining
						    * message against the
						    * current_xfer_offset
						    */
						   msg_head->size =
						      *current_xfer_offset
						         + follow_on_data;
						   msg_head->cell_count = 0;
						} else {
						   /*
						    * Unroll to the terminator
						    * we will need to see this
						    * again on the next dest
						    * message.
						    */
						   sbuf_remaining += 
						      sizeof(vxt_indyq_cell_t);
						   *current_xfer_offset -=
						      sizeof(vxt_indyq_cell_t);
						   *sbuf_size -= sbuf_remaining;
						   *dest_buf_size -= 
						      dbuf_offset;
						   break;
							
						}
						/*
						 * We have finished the
						 * scatter/gather list
						 * translation
						 * Set the remaining size 
						 * in the message header for
						 * the copy of any remaining
						 * direct data
						 * Note: we measure remaining
						 * message against the
						 * current_xfer_offset
						 */
						msg_head->size =
						   *current_xfer_offset
						      + follow_on_data;
						msg_head->cell_count = 0;
					} else {
						msg_head->cell_count = cell_cnt;
					}

				} else {
					uint32_t smsg_size;
					uint32_t dmsg_size;
				
					smsg_size = sbuf_remaining;
					dmsg_size = dest_buf_size - dbuf_offset;
					follow_on_data = 
					   vxtiq_cell_copy_translate(
					      smsg_cell, dmsg_cell,
					      mapping_id, context_id,
					      page_size, &smsg_size,
					      &dmsg_size, &cell_cnt);
					/*
					 * smsg and dmsg size are updated
					 * and return the amount of buffer
					 * used.
					 */
					dmsg_offset += dmsg_size;
					sbuf_remaining -= smsg_size;
					*current_xfer_offset += smsg_size;
				
					msg_head->cell_count = cell_cnt;
					dbuf_offset += dmsg_size;
					data_ptr += smsg_size
				}


				/*
				 * We are through the scatter/gather list
				 * any embedded data?
				 */

NEED FINAL CELL in destination for STREAMING
				if (((msg_head->size > *current_xfer_offset) &&
				    (msg_head->cell_count == 0)) {
					uint32_t direct_copy_size;
					/*
					 * Direct data, explict ie local 
					 * to the buffer
					 */
					direct_copy_size = sbuf_remaining;
					if (direct_copy_size > 
					    (dest_buf_size - dbuf_offset)) {
						direct_copy_size =
						   (dest_buf_size-dbuf_offset);
					}
					if (direct_copy_size > 
					    (msg_head->size -
					     *current_xfer_offset)) {
						direct_copy_size =
						   msg_head->size - 
						      *current_xfer_offset;
					}
					memcpy(dest_ptr, data_ptr,
					       direct_copy_size);
					sbuf_remaining -= direct_copy_size;
					dbuf_offset += direct_copy_size;
					data_ptr += direct_copy_size;
					dest_ptr += direct_copy_size;
					*current_xfer_offset +=
					   direct_copy_size;
				
				}
			}
			
		} else {
			uint32_t direct_copy_size;
			/*
			 * Direct data, explict ie local to the buffer
			 */
			direct_copy_size = sbuf_remaining;
			if (direct_copy_size > 
			    (dest_buf_size - dbuf_offset)) {
				direct_copy_size =
				   (dest_buf_size - dbuf_offset);
			}
			if (direct_copy_size >
			    (msg_head->size - *current_xfer_offset)) {
				direct_copy_size =
				   msg_head->size - *current_xfer_offset;
			}
			memcpy(dest_ptr, data_ptr, direct_copy_size);
			sbuf_remaining -= direct_copy_size;
			dbuf_offset += direct_copy_size;
			data_ptr += direct_copy_size;
			dest_ptr += direct_copy_size;
			*current_xfer_offset += direct_copy_size;
	
		}

		ASSERT(*current_xfer_offset <= msg_head->size);
		if (*current_xfer_offset  = msg_head->size) {
			*current_xfer_offset = 0;
		}
	}
	
	return smsg_offset;

}
