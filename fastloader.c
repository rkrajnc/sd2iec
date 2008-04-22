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


   fastloader.c: High level handling of fastloader protocols

*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include "config.h"
#include "buffers.h"
#include "doscmd.h"
#include "fileops.h"
#include "iec-ll.h"
#include "fastloader-ll.h"
#include "fastloader.h"

enum fastloaders detected_loader;

#ifdef CONFIG_TURBODISK
void load_turbodisk(void) {
  uint8_t i,len,firstsector;
  buffer_t *buf;

  set_clock(0);

  /* Copy filename to beginning of buffer */
  // FIXME: Das ist daemlich. fat_open um Zeiger auf Dateinamen erweitern?
  len = command_buffer[9];
  for (i=0;i<len;i++)
    command_buffer[i] = command_buffer[10+i];

  command_buffer[len] = 0;
  command_length = len;

  // FIXME: Rueckgabewert mit Status, evtl. direkt fat_open_read nehmen
  file_open(0);
  buf = find_buffer(0);
  if (!buf) {
    cli();
    turbodisk_byte(0xff);
    IEC_DDR &= ~IEC_BIT_CLOCK;
    IEC_DDR &= ~IEC_BIT_DATA;
    sei();
    return;
  }

  firstsector = 1;

  cli();
  while (1) {
    /* Send the status byte */
    if (buf->sendeoi) {
      turbodisk_byte(0);
    } else {
      turbodisk_byte(1);
    }

    if (firstsector) {
      /* Load address is transferred seperately */
      i = buf->position;
      turbodisk_byte(buf->data[i++]);
      turbodisk_byte(buf->data[i++]);
      buf->position  = i;
      firstsector    = 0;
    }

    if (buf->sendeoi) {
      /* Last sector is sent byte-by-byte */
      turbodisk_byte(buf->lastused - buf->position + 2);

      i = buf->position;
      do {
        turbodisk_byte(buf->data[i]);
      } while (i++ < buf->lastused);

      break;
    } else {
      /* Send the complete 254 byte buffer */
      turbodisk_buffer(buf->data + buf->position, 254);
      if (buf->refill(buf)) {
        /* Some error, abort */
        turbodisk_byte(0xff);
        break;
      }
    }
  }
  sei();
  buf->cleanup(buf);
  free_buffer(buf);

  set_clock(1);
}
#endif
