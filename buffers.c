/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>

   Inspiration and low-level SD/MMC access based on code from MMC2IEC
     by Lars Pontoppidan et al., see sdcard.c|h and config.h.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

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


   buffers.c: Internal buffer management
*/

#include <avr/io.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "dirent.h"
#include "errormsg.h"
#include "ff.h"
#include "buffers.h"

dh_t    matchdh;
uint8_t entrybuf[33];

/// One additional buffer structure for channel 15
buffer_t buffers[CONFIG_BUFFER_COUNT+1];

/// The actual data buffers
static uint8_t bufferdata[CONFIG_BUFFER_COUNT*256];

/// Number of active data buffers + 16 * number of write buffers
uint8_t active_buffers;

/**
 * init_buffers - initializes the buffer data structures
 *
 * This function initialized all the buffer-related data structures.
 */
void init_buffers(void) {
  uint8_t i;

  memset(buffers,0,sizeof(buffers));
  for (i=0;i<CONFIG_BUFFER_COUNT;i++)
    buffers[i].data = bufferdata + 256*i;

  buffers[CONFIG_BUFFER_COUNT].data      = error_buffer;
  buffers[CONFIG_BUFFER_COUNT].secondary = 15;
  buffers[CONFIG_BUFFER_COUNT].allocated = 1;
  buffers[CONFIG_BUFFER_COUNT].read      = 1;
  buffers[CONFIG_BUFFER_COUNT].write     = 1;
  buffers[CONFIG_BUFFER_COUNT].sendeoi   = 1;
  buffers[CONFIG_BUFFER_COUNT].refill    = set_ok_message;
}

/**
 * alloc_system_buffer - allocate a buffer for system use
 *
 * This function allocates a buffer and marks it as used. Returns a
 * pointer to the buffer structure or NULL of no buffer is free.
 */
buffer_t *alloc_system_buffer(void) {
  uint8_t i;

  for (i=0;i<CONFIG_BUFFER_COUNT;i++) {
    if (!buffers[i].allocated) {
      /* Clear everything except the data pointer */
      memset(sizeof(uint8_t *)+(char *)&(buffers[i]),0,sizeof(buffer_t)-sizeof(uint8_t *));
      buffers[i].allocated = 1;
      buffers[i].secondary = BUFFER_SEC_SYSTEM;
      return &buffers[i];
    }
  }

  set_error(ERROR_NO_CHANNEL);
  return NULL;
}

/**
 * alloc_buffer - allocates a buffer
 *
 * This function allocates a buffer and marks it as used. It will also
 * turn on the busy LED to notify the user. Returns a pointer to the
 * buffer structure or NULL if no buffer is free.
 */
buffer_t *alloc_buffer(void) {
  buffer_t *buf = alloc_system_buffer();
  if (buf != NULL) {
    buf->secondary = 0;
    active_buffers++;
    BUSY_LED_ON();
  }
  return buf;
}

/**
 * free_buffer - deallocate a buffer
 * @buffer: pointer to the buffer structure to mark as free
 *
 * This function deallocates the given buffer. If the pointer is NULL,
 * the buffer is already freed or the buffer is assigned to secondary
 * address 15 nothing will happen. This function will also update the
 * two LEDs accordings to the remaining number of open and writeable
 * buffers.
 */
void free_buffer(buffer_t *buffer) {
  if (buffer == NULL) return;
  if (buffer->secondary == 15) return;
  if (!buffer->allocated) return;

  buffer->allocated = 0;

  if (buffer->write)
    active_buffers -= 16;
  if (!(active_buffers & 0xf0))
    DIRTY_LED_OFF();

  if (buffer->secondary < BUFFER_SEC_SYSTEM && ! --active_buffers)
    BUSY_LED_OFF();
}

/**
 * free_all_buffers - deallocates all buffers
 * @cleanup: Flags if the cleanup callback should be called
 *
 * This function calls free_buffer on all allocated buffers, optionally
 * calling the cleanup callback if desired. Returns 0 if all cleanup
 * calls were successful (or no cleanup call was performed), non-zero
 * otherwise.
 */
uint8_t free_all_buffers(uint8_t cleanup) {
  uint8_t i,res;

  res = 0;

  for (i=0;i<CONFIG_BUFFER_COUNT;i++)
    if (buffers[i].allocated) {
      if (buffers[i].secondary >= BUFFER_SEC_SYSTEM ||
          (cleanup && buffers[i].cleanup))
        res = res || buffers[i].cleanup(&buffers[i]);
      free_buffer(&buffers[i]);
    }

  return res;
}

/**
 * find_buffer - find the buffer corresponding to a secondary address
 * @secondary: secondary address to look for
 *
 * This function returns a pointer to the first buffer structure whose
 * secondary address is the same as the one given. Returns NULL if
 * no matching buffer was found.
 */
buffer_t *find_buffer(uint8_t secondary) {
  uint8_t i;

  for (i=0;i<CONFIG_BUFFER_COUNT+1;i++) {
    if (buffers[i].allocated && buffers[i].secondary == secondary)
      return &buffers[i];
  }
  return NULL;
}

/**
 * mark_write_buffer - mark a buffer as used for writing
 * @buf: pointer to the buffer
 *
 * This function marks the given buffer as used for writing, tracks
 * this in active_buffers and turns on the dirty LED.
 */
void mark_write_buffer(buffer_t *buf) {
  buf->write = 1;
  active_buffers += 16;
  DIRTY_LED_ON();
}
