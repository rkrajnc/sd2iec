/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007  Ingo Korb <ingo@akana.de>

   Inspiration and low-level SD/MMC access based on code from MMC2IEC
     by Lars Pontoppidan et al., see sdcard.c|h and config.h.

   FAT filesystem access based on code from ChaN, see tff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

   
   buffers.h: Data structures for the internal buffers
*/

#ifndef BUFFERS_H
#define BUFFERS_H

#include <stdint.h>
#include "tff.h"

typedef struct buffer_s {
  /* The error channel uses the same data structure for convenience reasons, */
  /* so data must be a pointer. It also allows swapping the buffers around   */
  /* in case I ever add external ram (not XRAM) to the design (which will    */
  /* require locking =( ).                                                   */
  uint8_t *data; 
  uint16_t length;   // Index of the last used byte -> length-1!
  uint16_t position; // Index of the byte that will be sent next
  uint8_t secondary;
  int     allocated:1;
  int     dirty:1;
  int     read:1;
  int     write:1;
  int     sendeoi:1;
  /* Callback routine to refill the buffer after its end has been reached. */
  /*  Returns true if any error occured                                    */
  uint8_t (*refill)(struct buffer_s *buffer);
  /* Cleanup routine that will be called after CLOSE */
  /*  Returns true if any error occured              */
  uint8_t (*cleanup)(struct buffer_s *buffer);
} buffer_t;

/* Simplifies access to the error buffer length */
extern buffer_t buffer[];

/* Initializes the buffer structures */
void init_buffers(void);

/* Allocates a buffer - Returns pointer to buffer or NULL of failure */
buffer_t *alloc_buffer(void);

/* Deallocates a buffer */
void free_buffer(buffer_t *buffer);

/* Finds the buffer corresponding to a secondary address */
/* Returns pointer to buffer on success or NULL on failure */
buffer_t *find_buffer(uint8_t secondary);

#endif
