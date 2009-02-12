/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>
   Final Cartridge III, DreamLoad fastloader support:
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
#include <util/atomic.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "doscmd.h"
#include "fileops.h"
#include "parser.h"
#include "wrapops.h"
#include "iec-ll.h"
#include "fastloader-ll.h"
#include "fastloader.h"
#include "led.h"
#include "timer.h"
#include "diskchange.h"

uint8_t detected_loader;

/* track to load, used as a kind of jobcode */
volatile uint8_t fl_track;

/* sector to load, used as a kind of jobcode */
volatile uint8_t fl_sector;

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
    ATOMIC_BLOCK( ATOMIC_FORCEON ) {
      turbodisk_byte(0xff);
      set_clock(1);
      set_data(1);
    }
    return;
  }

  firstsector = 1;

  ATOMIC_BLOCK( ATOMIC_FORCEON ) {
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
  }
  buf->cleanup(buf);
  free_buffer(buf);

  set_clock(1);
}
#endif

#ifdef CONFIG_FC3
void load_fc3(uint8_t freezed) {
  buffer_t *buf;
  unsigned char step,pos;
  unsigned char sector_counter = 0;
  unsigned char block[4];

  buf = find_buffer(0);

  if (!buf) {
    /* error, abort and pull down CLOCK and DATA to inform the host */
    IEC_OUT |= IEC_OBIT_CLOCK | IEC_OBIT_DATA;
    return;
  }

  /* to make sure the C64 VIC DMA is off */
  _delay_ms(20);

  for(;;) {
    clk_data_handshake();

    /* Starting buffer position */
    /* Rewinds by 2 bytes for the first sector and normal loader */
    pos = 2;

    /* construct first 4-byte block */
    /* The 0x07 in the first byte is never used */
    block[1] = sector_counter++;
    if (buf->sendeoi) {
      /* Last sector, send number of bytes */
      block[2] = buf->lastused;
    } else {
      /* Send 0 for full sector */
      block[2] = 0;
    }
    /* First data byte */
    block[3] = buf->data[pos++];

    if (!freezed)
      _delay_ms(0.19);
    fastloader_fc3_send_block(block);

    /* send the next 64 4-byte-blocks, the last 3 bytes are read behind
       the buffer, good that we don't have an MMU ;) */
    for (step = 0; step < 64; step++) {
      if (freezed)
        clk_data_handshake();
      else
        _delay_ms(0.19);
      fastloader_fc3_send_block(buf->data + pos);
      pos += 4;
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
  if (!buf || !buf->write)
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

  buf->cleanup(buf);
  free_buffer(buf);
}
#endif

#ifdef CONFIG_DREAMLOAD
#ifndef IEC_PCMSK
#  error "Sorry, DreamLoad is only supported on platforms using PCINT IEC"
#endif

static void dreamload_send_block(const uint8_t* p) {
  uint8_t checksum = 0;
  int     n;

  ATOMIC_BLOCK( ATOMIC_FORCEON ) {

    // checksum is EOR of all bytes
    for (n = 0; n < 256; n++)
      checksum ^= p[n];

    // send status, data bytes and checksum
    dreamload_send_byte(0);
    for (n = 0; n < 256; n++) {
      dreamload_send_byte(*p);
      p++;
    }
    dreamload_send_byte(checksum);

    // release CLOCK and DATA
    IEC_OUT &= (uint8_t)~(IEC_OBIT_ATN|IEC_OBIT_DATA|IEC_OBIT_CLOCK|IEC_OBIT_SRQ);
  }
}

void load_dreamload(void) {
  uint16_t n;
  uint8_t  type;
  buffer_t *buf;

  /* disable IRQs while loading the final code, so no jobcodes are read */
  ATOMIC_BLOCK( ATOMIC_FORCEON ) {
    set_clock_irq(0);
    set_atn_irq(0);

    // Release clock and data
    IEC_OUT &= (uint8_t)~(IEC_OBIT_ATN|IEC_OBIT_DATA|IEC_OBIT_CLOCK|IEC_OBIT_SRQ);

    /* load final drive code, fixed length */
    type = 0;
    for (n = 4 * 256; n != 0; --n) {
      type ^= dreamload_get_byte();
    }

    if ((type == 0xac) || (type == 0xdc)) {
      set_atn_irq(1);
      detected_loader = FL_DREAMLOAD_OLD;
    } else {
      set_clock_irq(1);
    }

    /* mark no job waiting, enable IRQs to get job codes */
    fl_track = 0xff;
  }

  buf = alloc_system_buffer();
  if (!buf) {
    /* &§$% :-( */
    goto error;
  }

  for (;;) {

    while (fl_track == 0xff) {
      if (key_pressed(KEY_NEXT | KEY_PREV | KEY_HOME)) {
        change_disk();
      }
      if (key_pressed(KEY_SLEEP)) {
        reset_key(KEY_SLEEP);
        set_busy_led(0);
        set_dirty_led(1);
        fl_track = 0;
        fl_sector = 0;
        break;
      }
    }

    set_busy_led(1);

    if (fl_track == 0) {
      // check special commands first
      if (fl_sector == 0) {
        // end loader
        set_busy_led(0);
        break;
      } else if (fl_sector == 1) {
        // command: load first sector of directory
        // slow down 18/1 loading, so diskswap has a higher chance
        tick_t targettime = ticks + MS_TO_TICKS(1000);
        while (time_before(ticks,targettime)) ;
        read_sector(buf, current_part,
                    partition[current_part].d64data.dir_track,
                    partition[current_part].d64data.dir_start_sector);
        dreamload_send_block(buf->data);
      }
      else {
        set_busy_led(0);
      }
    } else {
      read_sector(buf, current_part, fl_track, fl_sector);
      dreamload_send_block(buf->data);
    }
    fl_track = 0xff;
  }

error:
  set_clock_irq(0);
  set_atn_irq(0);
}
#endif
