/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>
   Final Cartridge III fastloader support:
   Copyright (C) 2008  Thomas Giesel <skoe@directbox.com>

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
#include <util/delay.h>
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
  if (buf->cleanup)
    buf->cleanup(buf);
  free_buffer(buf);

  set_clock(1);
}
#endif

#ifdef CONFIG_FC3
void load_fc3(void) {
  buffer_t *buf;
  unsigned char step;
  unsigned char sector_counter = 0;
  unsigned char block[4];

  buf = find_buffer(0);

  if (!buf) {
    /* error, abort and pull down CLOCK and DATA to inform the host */
    IEC_OUT |= IEC_OBIT_CLOCK | IEC_OBIT_DATA;
    return;
  }

  /* FC3 reads the first two bytes before starting the speeder, rewind */
  buf->position -= 2;

  /* to make sure the C64 VIC DMA is off */
  _delay_ms(20);

  for(;;) {
    set_clock(0);
    while (IEC_DATA) ;
    set_clock(1);
    while (!IEC_DATA) ;

    /* construct first 4-byte block */
    block[0] = 7;          /* any meaning? Why not 42? */
    block[1] = sector_counter++;
    if (buf->sendeoi) {
      /* Last sector, send number of bytes */
      block[2] = buf->lastused;
    } else {
      /* Send 0 for full sector */
      block[2] = 0;
    }
    block[3] = buf->data[buf->position++];

    _delay_ms(0.15);
    fastloader_fc3_send_block(block);

    /* send the next 64 4-byte-blocks, the last 3 bytes are read behind
       the buffer, good that we don't have an MMU ;) */
    for (step = 0; step < 64; step++) {
      _delay_ms(0.15);
      fastloader_fc3_send_block(buf->data + buf->position);
      buf->position += 4;
    }

    if (buf->sendeoi) {
      /* pull down DATA to inform the host about the last sector */
      set_data(0);
      break;
    } else {
      if (buf->refill(buf)) {
        /* error, abort and pull down CLOCK and DATA to inform the host */
        IEC_OUT |= IEC_OBIT_CLOCK | IEC_OBIT_DATA;
        break;
      }
    }
  }

  if (buf->cleanup)
    buf->cleanup(buf);

  free_buffer(buf);
}

void save_fc3(void) {
  unsigned char n;
  unsigned char size;
  unsigned char eof = 0;
  buffer_t *buf;

  buf = find_buffer(1);
  /* Check if this is a writable file */
  if (!buf || !buf->allocated || !buf->write)
      return;

  /* to make sure the host pulled DATA low and is ready */
  _delay_ms(5);

  do {
    set_data(0);

    size = fc3_get_byte();

    if (size == 0) {
      /* a full block is coming, no EOF */
      size = 254;
    }
    else {
      /* this will be the last block */
      size--;
      eof = 1;
    }

    for (n = 0; n < size; n++) {
      /* Flush buffer if full */
      if (buf->mustflush) {
        buf->refill(buf);
        /* the FC3 just ignores things like "disk full", so do we */
      }

      buf->data[buf->position] = fc3_get_byte();

      if (buf->lastused < buf->position)
        buf->lastused = buf->position;
      buf->position++;

      /* Mark buffer for flushing if position wrapped */
      if (buf->position == 0)
        buf->mustflush = 1;
    }
  }
  while (!eof);

  if (buf->cleanup)
    buf->cleanup(buf);
  free_buffer(buf);
}
#endif
