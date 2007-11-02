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

   
   diag.c: Quick-and-dirty SD card diagnostics

*/

#include <avr/pgmspace.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "config.h"
#include "tff.h"
#include "buffers.h"
#include "doscmd.h"
#include "sdcard.h"
#include "diag.h"

FATFS fatfs;
char volumelabel[12];

static buffer_t *buf;

extern uint8_t bufferdata[BUFFER_COUNT*1024];

static int ioputc(char c, FILE *stream) {
  if (c == '\n') {
    buf->data[buf->length++] = '"';
    buf->data[buf->length++] = 0;
    buf->data[buf->length++] = 1;
    buf->data[buf->length++] = 1;
    buf->data[buf->length++] = 0;
    buf->data[buf->length++] = 0;
    buf->data[buf->length++] = '"';
  } else {
    buf->data[buf->length++] = (uint8_t)c;
  }
  return 0;
}

static FILE mystdout = FDEV_SETUP_STREAM(ioputc, NULL, _FDEV_SETUP_WRITE);

static uint8_t generic_cleanup(buffer_t *buf) {
  free_buffer(buf);
  buf->cleanup = NULL;
  return 0;
}

void sdcard_diags(uint8_t secondary) {
  DIR dh;
  FILINFO finfo;
  uint8_t i;

  stdout = &mystdout;

  buf = alloc_buffer();
  buf->secondary = secondary;
  buf->read      = 1;
  buf->position  = 0;
  buf->length    = 7;
  buf->sendeoi   = 1;
  buf->cleanup   = generic_cleanup;

  bufferdata[0] = 1;
  bufferdata[1] = 8;
  bufferdata[2] = 1;
  bufferdata[3] = 1;
  bufferdata[4] = 0;
  bufferdata[5] = 0;
  bufferdata[6] = '"';

  f_mount(0, &fatfs);

  /* Read volume label */
  if (f_opendir(&dh, "") == FR_OK) {
    while (f_readdir(&dh, &finfo) == FR_OK) {
      if (!finfo.fname[0]) break;
      if ((finfo.fattrib & (AM_VOL|AM_SYS|AM_HID)) == AM_VOL) {
	i=0;
	while (finfo.fname[i]) {
	  volumelabel[i] = finfo.fname[i];
	  i++;
	}
      }
    }
  }
  
  /* Remove dot from volume label. Assumes a single dot. */
  for (i=0; i<sizeof(volumelabel); i++) {
    if (volumelabel[i] == '.') {
      while (i < sizeof(volumelabel)-1) {
	volumelabel[i] = volumelabel[i+1];
	i++;
      }
      volumelabel[sizeof(volumelabel)-1] = ' ';
    }
  }

  buf->data[buf->length++] = 0;
  buf->data[buf->length++] = 0;
}
