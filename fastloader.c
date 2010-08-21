/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2010  Ingo Korb <ingo@akana.de>
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

#include <avr/boot.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/atomic.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "diskchange.h"
#include "display.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fastloader-ll.h"
#include "fileops.h"
#include "iec-ll.h"
#include "iec.h"
#include "led.h"
#include "parser.h"
#include "timer.h"
#include "wrapops.h"
#include "fastloader.h"

uint8_t detected_loader;

/* track to load, used as a kind of jobcode */
volatile uint8_t fl_track;

/* sector to load, used as a kind of jobcode */
volatile uint8_t fl_sector;

/* Small helper for fastloaders that need to detect disk changes */
static uint8_t check_keys(void) {
  /* Check for disk changes etc. */
  if (key_pressed(KEY_NEXT | KEY_PREV | KEY_HOME)) {
    change_disk();
  }
  if (key_pressed(KEY_SLEEP)) {
    reset_key(KEY_SLEEP);
    set_busy_led(0);
    set_dirty_led(1);
    return 1;
  }

  return 0;
}


/*
 *
 *  Turbodisk
 *
 */
#ifdef CONFIG_LOADER_TURBODISK
void load_turbodisk(void) {
  uint8_t i,len,firstsector;
  buffer_t *buf;

#if defined __AVR_ATmega644__   || \
    defined __AVR_ATmega644P__  || \
    defined __AVR_ATmega1284P__ || \
    defined __AVR_ATmega1281__
  /* Lock out clock sources that aren't stable enough for this protocol */
  uint8_t tmp = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS) & 0x0f;
  if (tmp == 2) {
    set_error(ERROR_CLOCK_UNSTABLE);
    return;
  }
#endif

  set_clock(0);
  uart_flush();

  /* Copy filename to beginning of buffer */
  len = command_buffer[9];
  for (i=0;i<len;i++)
    command_buffer[i] = command_buffer[10+i];

  command_buffer[len] = 0;
  command_length = len;

  /* Open the file */
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


/*
 *
 *  Final Cartridge 3 / EXOS
 *
 */
#ifdef CONFIG_LOADER_FC3
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
      if (!IEC_ATN)
        goto cleanup;

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

 cleanup:
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


/*
 *
 *  Dreamload
 *
 */
#ifdef CONFIG_LOADER_DREAMLOAD
#ifndef set_clock_irq
#  error "Sorry, DreamLoad is only supported on platforms with a CLK interrupt"
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

  /* Find the start sector of the current directory */
  dh_t dh;
  path_t curpath;

  curpath.part = current_part;
  curpath.dir  = partition[current_part].current_dir;
  opendir(&dh, &curpath);

  for (;;) {

    while (fl_track == 0xff) {
      if (check_keys()) {
        fl_track = 0;
        fl_sector = 0;
        break;
      }
    }

    set_busy_led(1);

    /* Output the track/sector for debugging purposes */
    uart_puthex(fl_track);
    uart_putc('/');
    uart_puthex(fl_sector);
    uart_putcrlf();

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

        read_sector(buf, current_part, dh.dir.d64.track, dh.dir.d64.sector);
        dreamload_send_block(buf->data);
      }
      else {
        // fl_sector == 2 is canonical
        set_busy_led(0);
      }
    } else {
      read_sector(buf, current_part, fl_track, fl_sector);
      dreamload_send_block(buf->data);
    }
    fl_track = 0xff;
  }

error:
  free_buffer(buf);
  set_clock_irq(0);
  set_atn_irq(0);
}
#endif


/*
 *
 * ULoad Model 3
 *
 */
#ifdef CONFIG_LOADER_ULOAD3
static uint8_t uload3_transferchain(uint8_t track, uint8_t sector, uint8_t saving) {
  buffer_t *buf;
  uint8_t i,bytecount,first;

  first = 1;

  buf = alloc_buffer();
  if (!buf) {
    uload3_send_byte(0xff);
    return 0;
  }

  do {
    /* read current sector */
    read_sector(buf, current_part, track, sector);
    if (current_error != 0) {
      uload3_send_byte(0xff);
      return 0;
    }

    /* send number of bytes in sector */
    if (buf->data[0] == 0) {
      bytecount = buf->data[1]-1;
    } else {
      bytecount = 254;
    }
    uload3_send_byte(bytecount);

    if (saving) {
      if (first) {
        /* send load address */
        first = 0;
        uload3_send_byte(buf->data[2]);
        uload3_send_byte(buf->data[3]);
        i = 2;
      } else
        i = 0;

      /* receive sector contents */
      for (;i<bytecount;i++) {
        int16_t tmp = uload3_get_byte();
        if (tmp < 0)
          return 1;

        buf->data[i+2] = tmp;
      }

      /* write sector */
      write_sector(buf, current_part, track, sector);
      if (current_error != 0) {
        uload3_send_byte(0xff);
        return 0;
      }
    } else {
      /* reading: send sector contents */
      for (i=0;i<bytecount;i++)
        uload3_send_byte(buf->data[i+2]);
    }

    track  = buf->data[0];
    sector = buf->data[1];
  } while (track != 0);

  /* send end marker */
  uload3_send_byte(0);

  free_buffer(buf);
  return 0;
}

void load_uload3(void) {
  int16_t cmd,tmp;
  uint8_t t,s;
  dh_t dh;
  path_t curpath;

  curpath.part = current_part;
  curpath.dir  = partition[current_part].current_dir;
  opendir(&dh, &curpath);

  while (1) {
    /* read command */
    cmd = uload3_get_byte();
    if (cmd < 0) {
      /* ATN received */
      break;
    }

    switch (cmd) {
    case 1: /* load a file */
    case 2: /* save and replace a file */
      tmp = uload3_get_byte();
      if (tmp < 0)
        return;
      t = tmp;

      tmp = uload3_get_byte();
      if (tmp < 0)
        /* ATN received */
        return;
      s = tmp;

      if (uload3_transferchain(t,s, (cmd == 2)))
        return;

      break;

    case '$':
      /* read directory */
      uload3_transferchain(dh.dir.d64.track, dh.dir.d64.sector, 0);
      break;

    default:
      /* unknown command */
      uload3_send_byte(0xff);
      break;
    }
  }
}
#endif


/*
 *
 *  GIJoe/EPYX common code
 *
 */
#if defined(CONFIG_LOADER_GIJOE) || defined(CONFIG_LOADER_EPYXCART)
/* Returns the byte read or <0 if the user aborts */
/* Aborting on ATN is not reliable for at least one version */
static int16_t gijoe_read_byte(void) {
  uint8_t i;
  uint8_t value = 0;

  for (i=0;i<4;i++) {
    while (IEC_CLOCK)
      if (check_keys())
        return -1;

    value >>= 1;

    if (!IEC_DATA)
      value |= 0x80;

    while (!IEC_CLOCK)
      if (check_keys())
        return -1;

    value >>= 1;

    if (!IEC_DATA)
      value |= 0x80;
  }

  return value;
}
#endif


/*
 *
 * GI Joe
 *
 */
#ifdef CONFIG_LOADER_GIJOE
static void gijoe_send_byte(uint8_t value) {
  uint8_t i;

  ATOMIC_BLOCK( ATOMIC_FORCEON ) {
    for (i=0;i<4;i++) {
      /* Wait for clock high */
      while (!IEC_CLOCK) ;

      set_data(value & 1);
      value >>= 1;

      /* Wait for clock low */
      while (IEC_CLOCK) ;

      set_data(value & 1);
      value >>= 1;
    }
  }
}

void load_gijoe(void) {
  buffer_t *buf;

  set_data(1);
  set_clock(1);
  set_atn_irq(0);

  /* Wait until the bus has settled */
  _delay_ms(10);
  while (!IEC_DATA || !IEC_CLOCK) ;

  while (1) {
    /* Handshake */
    set_clock(0);

    while (IEC_DATA)
      if (check_keys())
        return;

    set_clock(1);
    uart_flush();

    /* First byte is ignored */
    if (gijoe_read_byte() < 0)
      return;

    /* Read two file name characters */
    command_buffer[0] = gijoe_read_byte();
    command_buffer[1] = gijoe_read_byte();

    set_clock(0);

    command_buffer[2] = '*';
    command_buffer[3] = 0;
    command_length = 3;

    /* Open the file */
    file_open(0);
    uart_flush();
    buf = find_buffer(0);
    if (!buf) {
      set_clock(1);
      gijoe_send_byte(0xfe);
      gijoe_send_byte(0xfe);
      gijoe_send_byte(0xac);
      gijoe_send_byte(0xf7);
      continue;
    }

    /* file is open, transfer */
    while (1) {
      uint8_t i = buf->position;

      set_clock(1);
      _delay_us(2);

      do {
        if (buf->data[i] == 0xac)
          gijoe_send_byte(0xac);

        gijoe_send_byte(buf->data[i]);
      } while (i++ < buf->lastused);

      /* Send end marker and wait for the next name */
      if (buf->sendeoi) {
        gijoe_send_byte(0xac);
        gijoe_send_byte(0xff);

        buf->cleanup(buf);
        free_buffer(buf);
        break;
      }

      /* Send "another sector following" marker */
      gijoe_send_byte(0xac);
      gijoe_send_byte(0xc3);
      _delay_us(50);
      set_clock(0);

      /* Read next block */
      if (buf->refill(buf)) {
        /* Send error marker */
        gijoe_send_byte(0xfe);
        gijoe_send_byte(0xfe);
        gijoe_send_byte(0xac);
        gijoe_send_byte(0xf7);

        buf->cleanup(buf);
        free_buffer(buf);
        break;
      }
    }
  }
}
#endif


/*
 *
 * Epyx Fast Load Cartridge
 *
 */
#ifdef CONFIG_LOADER_EPYXCART
void load_epyxcart(void) {
  uint8_t checksum = 0;
  int16_t b,i;

  uart_flush(); // Pending output can mess up our timing

  /* Initial handshake */
  set_data(1);
  set_clock(0);
  set_atn_irq(0);

  while (IEC_DATA)
    if (!IEC_ATN)
      return;

  set_clock(1);

  /* Receive and checksum stage 2 */
  for (i=0;i<256;i++) {
    b = gijoe_read_byte();

    if (b < 0)
      return;

    if (i < 238)
      /* Stage 2 has some junk bytes at the end, ignore them */
      checksum ^= b;
  }

  /* Check for known stage2 loader */
  if (checksum != 0x50) {
    return;
  }

  /* Receive file name */
  i = gijoe_read_byte();
  if (i < 0) {
    return;
  }

  command_length = i;

  do {
    b = gijoe_read_byte();
    if (b < 0)
      return;

    command_buffer[--i] = b;
  } while (i > 0);

  set_clock(0);

  /* Open the file */
  file_open(0);

  buffer_t *buf = find_buffer(0);
  if (buf == NULL) {
    set_clock(1);
    return;
  }

  /* Transfer data */
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    while (1) {
      set_clock(1);
      set_data(1);

      /* send number of bytes in sector */
      if (epyxcart_send_byte(buf->lastused-1)) {
        break;
      }

      /* send data */
      for (i=2;i<=buf->lastused;i++) {
        if (epyxcart_send_byte(buf->data[i])) {
          break;
        }
      }

      if (!IEC_ATN)
        break;

      /* exit after final sector */
      if (buf->sendeoi)
        break;

      /* read next sector */
      set_clock(0);
      if (buf->refill(buf))
        break;
    }
  }

  set_clock(1);
  set_data(1);
  buf->cleanup(buf);
  free_buffer(buf);
}
#endif


/*
 *
 *  GEOS
 *
 */
#ifdef CONFIG_LOADER_GEOS

/* Function pointer to the current byte transmit function */
void (*geos_send_byte)(uint8_t byte);

/* Receive a fixed-length data block */
static void geos_receive_datablock(uint8_t *data, uint16_t length) {
  data += length-1;

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    while (!IEC_CLOCK);
    set_data(1);
    while (length--)
      *data-- = geos_get_byte();
  }
  set_data(0);
}

/* Receive a data block from the computer */
static void geos_receive_block(uint8_t *data) {
  uint8_t exitflag = 0;
  uint16_t length;

  /* Receive data length */
  while (!IEC_CLOCK && IEC_ATN)
    if (check_keys()) {
      /* User-requested exit */
      exitflag = 1;
      break;
    }

  /* Exit if ATN is low */
  if (!IEC_ATN || exitflag) {
    *data++ = 0;
    *data   = 0;
    return;
  }

  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    set_data(1);
    length = geos_get_byte();
    set_data(0);
  }

  if (length == 0)
    length = 256;

  geos_receive_datablock(data, length);
}

/* Send a single byte to the computer after waiting for CLOCK high */
static void geos_transmit_byte_wait(uint8_t byte) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    /* Wait until clock is high */
    while (!IEC_CLOCK) ;
    set_data(1);

    /* Send byte */
    geos_send_byte(byte);
    set_clock(1);
    set_data(0);
  }

  _delay_us(25); // educated guess
}

/* Send data block to computer */
static void geos_transmit_buffer_s3(uint8_t *data, uint16_t len) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    /* Wait until clock is high */
    while (!IEC_CLOCK) ;
    set_data(1);

    /* Send data block */
    uint16_t i = len;
    data += len - 1;

    while (i--) {
      geos_send_byte(*data--);
    }

    set_clock(1);
    set_data(0);

    _delay_us(15); // guessed
  }
}

static void geos_transmit_buffer_s2(uint8_t *data, uint16_t len) {
  /* Send length byte */
  geos_transmit_byte_wait(len);

  /* the rest is the same as in stage 3 */
  geos_transmit_buffer_s3(data, len);
}

/* Send job status to computer */
static void geos_transmit_status(void) {
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    /* Send a single 1 byte as length indicator */
    geos_transmit_byte_wait(1);

    /* Send (faked) job result code */
    if (current_error == 0)
      geos_transmit_byte_wait(1);
    else
      if (current_error == ERROR_WRITE_PROTECT)
        geos_transmit_byte_wait(8);
      else
        geos_transmit_byte_wait(2); // random non-ok job status
  }
}

/* GEOS READ operation */
static void geos_read_sector(uint8_t track, uint8_t sector, buffer_t *buf) {
  uart_putc('R');
  uart_puthex(track);
  uart_putc('/');
  uart_puthex(sector);
  uart_putcrlf();

  read_sector(buf, current_part, track, sector);
}

/* GEOS WRITE operation */
static void geos_write_sector_41(uint8_t track, uint8_t sector, buffer_t *buf) {
  uart_putc('W');
  uart_puthex(track);
  uart_putc('/');
  uart_puthex(sector);
  uart_putcrlf();

  /* Provide "unwritten data present" feedback */
  mark_buffer_dirty(buf);

  /* Receive data */
  geos_receive_block(buf->data);

  /* Write to image */
  write_sector(buf, current_part, track, sector);

  /* Reset "unwritten data" feedback */
  mark_buffer_clean(buf);
}

/* GEOS WRITE_71 operation */
static void geos_write_sector_71(uint8_t track, uint8_t sector, buffer_t *buf) {
  uart_putc('W');
  uart_puthex(track);
  uart_putc('/');
  uart_puthex(sector);
  uart_putcrlf();

  /* Provide "unwritten data present" feedback */
  mark_buffer_dirty(buf);

  /* Receive data */
  geos_receive_datablock(buf->data, 256);

  /* Write to image */
  write_sector(buf, current_part, track, sector);

  /* Send status */
  geos_transmit_status();

  /* Reset "unwritten data" feedback */
  mark_buffer_clean(buf);
}


/* GEOS stage 2/3 loader */
void load_geos(void) {
  buffer_t *cmdbuf = alloc_system_buffer();
  buffer_t *databuf = alloc_system_buffer();
  uint8_t *cmddata;
  uint16_t cmd;

  if (!cmdbuf || !databuf)
    return;

  cmddata = cmdbuf->data;

  /* Initial handshake */
  uart_flush();
  _delay_ms(1);
  set_data(0);
  while (IEC_CLOCK) ;

  while (1) {
    /* Receive command block */
    update_leds();
    geos_receive_block(cmddata);
    set_busy_led(1);

    //uart_trace(cmddata, 0, 4);

    cmd = cmddata[0] | (cmddata[1] << 8);

    switch (cmd) {
    case 0x0320: // 1541 stage 3 transmit
      geos_transmit_buffer_s3(databuf->data, 256);
      geos_transmit_status();
      break;

    case 0x031f: // 1571; 1541 stage 2 status (only seen in GEOS 1.3)
                 // 1581 transmit
      if (detected_loader == FL_GEOS_S23_1581) {
        if (cmddata[2] & 0x80) {
          geos_transmit_buffer_s3(databuf->data, 2);
        } else {
          geos_transmit_buffer_s3(databuf->data, 256);
        }
      }
      geos_transmit_status();
      break;

    case 0x0325: // 1541 stage 3 status
    case 0x032b: // 1581 status
      geos_transmit_status();
      break;

    case 0x0000: // internal QUIT
    case 0x0412: // 1541 stage 2 quit
    case 0x0420: // 1541 stage 3 quit
    case 0x0457: // 1581 quit
    case 0x0475: // 1571 stage 3 quit
      while (!IEC_CLOCK) ;
      set_data(1);
      return;

    case 0x0432: // 1541 stage 2 transmit
      if (current_error != 0) {
        geos_transmit_status();
      } else {
        geos_transmit_buffer_s2(databuf->data, 256); // FIXME: Need to reduce after copy protection check
      }
      break;

    case 0x0439: // 1541 stage 3 set address
    case 0x04a5: // 1571 stage 3 set address
      // Note: identical in stage 2, address 0428, probably unused
      device_address = cmddata[2] & 0x1f;
      display_address(device_address);
      break;

    case 0x049b: // 1581 initialize
    case 0x04b9: // 1581 flush
    case 0x04dc: // 1541 stage 3 initialize
    case 0x0504: // 1541 stage 2 initialize - only seen in GEOS 1.3
    case 0x057e: // 1571 initialize
      /* Doesn't do anything that needs to be reimplemented */
      break;

    case 0x057c: // 1541 stage 2/3 write
      geos_write_sector_41(cmddata[2], cmddata[3], databuf);
      break;

    case 0x058e: // 1541 stage 2/3 read
    case 0x04cc: // 1581 read
      geos_read_sector(cmddata[2], cmddata[3], databuf);
      break;

    case 0x04af: // 1571 read_and_send
      geos_read_sector(cmddata[2], cmddata[3], databuf);
      geos_transmit_buffer_s3(databuf->data, 256);
      geos_transmit_status();
      break;

    case 0x047c: // 1581 write
    case 0x05fe: // 1571 write
      geos_write_sector_71(cmddata[2], cmddata[3], databuf);
      break;

    default:
      uart_puts_P(PSTR("unknown:\r\n"));
      uart_trace(cmddata, 0, 4);
      return;
    }
  }
}

/* Stage 1 only - send a sector chain to the computer */
static void geos_send_chain(uint8_t track, uint8_t sector,
                            buffer_t *buf, uint8_t *key) {
  uint8_t bytes;
  uint8_t *keyptr,*dataptr;

  do {
    /* Read sector - no error recovery on computer side */
    read_sector(buf, current_part, track, sector);

    /* Decrypt contents if we have a key */
    if (key != NULL) {
      keyptr = key;
      dataptr = buf->data + 2;
      bytes = 254;
      while (bytes--)
        *dataptr++ ^= *keyptr++;
    }

    /* Read link pointer */
    track = buf->data[0];
    sector = buf->data[1];

    if (track == 0) {
      bytes = sector - 1;
    } else {
      bytes = 254;
    }

    /* Send buffer contents */
    geos_transmit_buffer_s2(buf->data + 2, bytes);
  } while (track != 0);

  geos_transmit_byte_wait(0);
}

static const PROGMEM uint8_t geos64_chains[] = {
  19, 13,
  20, 15,
  20, 17,
  0
};

static const PROGMEM uint8_t geos128_chains[] = {
  19, 12,
  20, 15,
  23, 6,
  24, 4,
  0
};

/* GEOS 64 stage 1 loader */
static void load_geos_s1(const prog_uint8_t *chainptr) {
  buffer_t *encrbuf = find_buffer(BUFFER_SYS_GEOSKEY);
  buffer_t *databuf = alloc_buffer();
  uint8_t *encdata = NULL;
  uint8_t track, sector;

  if (!encrbuf || !databuf)
    return;

  /* Initial handshake */
  uart_flush();
  _delay_ms(1);
  set_data(0);
  while (IEC_CLOCK) ;

  /* Send sector chains */
  while (1) {
    track = pgm_read_byte(chainptr++);

    if (track == 0)
      break;

    sector = pgm_read_byte(chainptr++);

    /* Transfer sector chain */
    geos_send_chain(track, sector, databuf, encdata);

    /* Turn on decryption after the first chain */
    encdata = encrbuf->data;
  }

  /* Done! */
  free_buffer(encrbuf);
  set_data(1);
}

void load_geos64_s1(void) {
  load_geos_s1(geos64_chains);
}

void load_geos128_s1(void) {
  load_geos_s1(geos128_chains);
}

#endif
