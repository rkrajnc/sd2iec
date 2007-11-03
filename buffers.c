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

   
   buffers.c: Internal buffer management
*/

#include <avr/io.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "errormsg.h"

/* One additional buffer for channel 15 */
buffer_t buffer[BUFFER_COUNT+1];

/* The actual data buffers */
static uint8_t bufferdata[BUFFER_COUNT*256];

static uint8_t active_buffers;

void init_buffers(void) {
  uint8_t i;

  memset(buffer,0,sizeof(buffer));
  for (i=0;i<BUFFER_COUNT;i++)
    buffer[i].data = bufferdata + 256*i;
  
  buffer[BUFFER_COUNT].data      = error_buffer;
  buffer[BUFFER_COUNT].secondary = 15;
  buffer[BUFFER_COUNT].allocated = 1;
  buffer[BUFFER_COUNT].read      = 1;
  buffer[BUFFER_COUNT].write     = 1;
  buffer[BUFFER_COUNT].sendeoi   = 1;
}

buffer_t *alloc_buffer(void) {
  uint8_t i;

  for (i=0;i<BUFFER_COUNT;i++) {
    if (!buffer[i].allocated) {
      buffer[i].allocated = 1;
      active_buffers++;
      BUSY_LED_ON();
      return &buffer[i];
    }
  }

  return NULL;
}

void free_buffer(buffer_t *buffer) {
  if (buffer == NULL) return;
  if (buffer->secondary == 15) return;

  buffer->allocated = 0;
  if (! --active_buffers)
    BUSY_LED_OFF();
}

buffer_t *find_buffer(uint8_t secondary) {
  uint8_t i;

  for (i=0;i<BUFFER_COUNT+1;i++) {
    if (buffer[i].allocated && buffer[i].secondary == secondary)
      return &buffer[i];
  }
  return NULL;
}
