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

   
   fileops.c: Generic file operations

*/

#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "errormsg.h"
#include "buffers.h"
#include "uart.h"
#include "doscmd.h"
#include "fatops.h"
#include "fileops.h"

/* ------------------------------------------------------------------------- */
/*  Some constants used for directory generation                             */
/* ------------------------------------------------------------------------- */

#define HEADER_OFFSET_NAME 
#define HEADER_OFFSET_ID   26

/* NOTE: I wonder if RLE-packing would save space in flash? */
const PROGMEM uint8_t dirheader[] = {
  1, 4,                            /* BASIC start address */
  1, 1,                            /* next line pointer */
  0, 0,                            /* line number 0 */
  0x12, 0x22,                      /* Reverse on, quote */
  ' ',' ',' ',' ',' ',' ',' ',' ', /* 16 spaces as the disk name */
  ' ',' ',' ',' ',' ',' ',' ',' ', /* will be overwritten if needed */
  0x22,0x20,                       /* quote, space */
  'I','K',' ','2','A',             /* id IK, shift-space, dosmarker 2A */
  00                               /* line-end marker */
};

const PROGMEM uint8_t dirfooter[] = {
  1, 1, /* next line pointer */
  0, 0, /* number of free blocks (to be filled later */
  'B','L','O','C','K','S',' ','F','R','E','E','.',
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, /* Filler and end marker */
  0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00
};

const PROGMEM uint8_t filetypes[] = {
  'D','E','L', // 0
  'S','E','Q', // 1
  'P','R','G', // 2
  'U','S','R', // 3
  'R','E','L', // 4
  'C','B','M', // 5
  'D','I','R'  // 6
};

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/* Add a single directory entry in 15x1 format to the end of buf */
static void addentry(struct cbmdirent *dent, buffer_t *buf) {
  uint8_t i;
  uint8_t *data;

  data = buf->data + buf->length;

  /* Clear the line */
  memset(data, ' ', 31);
  /* Line end marker */
  data[31] = 0;

  /* Next line pointer, 1571-compatible =) */
  *data++ = 1;
  *data++ = 1;
  
  *data++ = dent->blocksize & 0xff;
  *data++ = dent->blocksize >> 8;
  
  /* Filler before file name */
  if (dent->blocksize < 1000)
    *data++;
  if (dent->blocksize < 100)
    *data++;
  if (dent->blocksize < 10)
    *data++;
  *data++ = '"';

  /* copy and adjust the filename - C783 */
  memcpy(data, dent->name, CBM_NAME_LENGTH);
  for (i=0;i<17;i++)
    if (dent->name[i] == 0x22 || dent->name[i] == 0xa0 || i == 16) {
      data[i] = '"';
      while (i<17)
	data[i++] &= 0x7f;
    }

  /* Skip name and final quote */
  data += 17;

  if (dent->typeflags & FLAG_SPLAT)
    *data = '*';

  /* File type */
  memcpy_P(data+1, filetypes + TYPE_LENGTH * (dent->typeflags & 0x07), TYPE_LENGTH);

  if (dent->typeflags & FLAG_RO)
    data[5] = '<';

  buf->length += 32;
}

/* ------------------------------------------------------------------------- */
/*  Callbacks                                                                */
/* ------------------------------------------------------------------------- */

/* Generic cleanup-callback that just frees the buffer */
uint8_t generic_cleanup(buffer_t *buf) {
  free_buffer(buf);
  return 0;
}

/* Generate the final directory buffer with the BLOCKS FREE message */
static uint8_t dir_footer(buffer_t *buf) {
  uint16_t blocks;

  /* Copy the "BLOCKS FREE" message */
  memcpy_P(buf->data, dirfooter, sizeof(dirfooter));

  blocks = fat_freeblocks();
  buf->data[2] = blocks & 0xff;
  buf->data[3] = blocks >> 8;

  buf->position = 0;
  buf->length   = 31;
  buf->sendeoi  = 1;

  return 0;
}

/* Fill the buffer with one new directory entry */
static uint8_t dir_refill(buffer_t *buf) {
  struct cbmdirent dent;

  uart_putc('+');

  buf->position = 0;
  buf->length   = 0;

  switch (fat_readdir(buf, &dent)) {
  case 0:
    addentry(&dent, buf);
    buf->length--;
    return 0;

  case -1:
    return dir_footer(buf);

  default:
    free_buffer(buf);
    return 1;
  }
}

/* Prepare for directory reading and create the header */
static void load_directory(uint8_t secondary) {
  buffer_t *buf;
  uint8_t i;

  buf = alloc_buffer();
  if (buf == 0) {
    set_error(ERROR_NO_CHANNEL,0,0);
    return;
  }

  if (fat_opendir(buf)) {
    free_buffer(buf);
    return;
  }

  buf->secondary = secondary;
  buf->read      = 1;
  buf->write     = 0;
  buf->cleanup   = generic_cleanup;
  buf->position  = 0;
  buf->length    = 31;
  buf->sendeoi   = 0;

  /* copy static header to start of buffer */
  memcpy_P(buf->data, dirheader, sizeof(dirheader));

  /* read volume name */
  if (fat_getlabel((char *) (buf->data+8))) {
    free_buffer(buf);
    return;
  }

  /* read id */
  if (fat_getid((char *) (buf->data+HEADER_OFFSET_ID))) {
    free_buffer(buf);
    return;
  }

  /* Let the refill callback handly everything else */
  buf->refill = dir_refill;

  return;
}

/* ------------------------------------------------------------------------- */
/*  External interface for the various operations                            */
/* ------------------------------------------------------------------------- */

/* Open something */
void file_open(uint8_t secondary) {
  char *fname;
  buffer_t *buf;
  uint8_t i;

  /* Assume everything will go well unless proven otherwise */
  set_error(ERROR_OK,0,0);

  /* Empty name? */
  if (command_length == 0) {
    set_error(ERROR_SYNTAX_NONAME,0,0);
    return;
  }

  /* Load directory? */
  if (command_buffer[0] == '$') {
    // FIXME: Secondary != 0? Compare D7F7-D7FF
    load_directory(secondary);
    return;
  }

  command_buffer[command_length] = 0;

  /* Parse path+drive numbers */
  fname = strchr((char *)command_buffer, ':');
  if (fname != NULL) {
    if (*(fname-1) == '/') {
      /* CMD-style path, rewrite it */
      if (parse_path((char *) command_buffer,(char *) command_buffer)) {
	set_error(ERROR_SYNTAX_NONAME,0,0);
	return;
      }
      fname = (char *) command_buffer;
    } else {
      /* Ignore everything before the : */
      fname++;
    }
  } else
    fname = (char *) command_buffer;

  /* Parse type+mode suffixes */
  char *ptr = fname;
  enum open_modes mode = OPEN_READ;
  uint8_t filetype = TYPE_DEL;

  while (*ptr && (ptr = strchr(ptr, ','))) {
    *ptr = 0;
    ptr++;
    switch (*ptr) {
    case 0:
      break;

    case 'R': /* Read */
      mode = OPEN_READ;
      break;

    case 'W': /* Write */
      mode = OPEN_WRITE;
      break;

    case 'A': /* Append */
      mode = OPEN_APPEND;
      break;

    case 'M': /* Modify */
      mode = OPEN_MODIFY;
      break;

    case 'D': /* DEL */
      filetype = TYPE_DEL;
      break;

    case 'S': /* SEQ */
      filetype = TYPE_SEQ;
      break;

    case 'P': /* PRG */
      filetype = TYPE_PRG;
      break;

    case 'U': /* USR */
      filetype = TYPE_USR;
      break;

    case 'L': /* REL */
      filetype = TYPE_REL;
      // FIXME: REL wird nach dem L anders geparst!
      break;
    }
  }

  /* Force mode for secondaries 0/1 */
  switch (secondary) {
  case 0:
    mode = OPEN_READ;
    if (filetype == TYPE_DEL)
      filetype = TYPE_PRG;
    break;

  case 1:
    mode = OPEN_WRITE;
    if (filetype == TYPE_DEL)
      filetype = TYPE_PRG;
    break;

  default:
    if (filetype == TYPE_DEL)
      filetype = TYPE_SEQ;
  }

  /* Grab a buffer */
  buf = alloc_buffer();
  if (buf == NULL) {
    set_error(ERROR_NO_CHANNEL,0,0);
    return;
  }
  buf->secondary = secondary;

  switch (mode) {
  case OPEN_MODIFY:
  case OPEN_READ:
    /* Modify is the same as read, but allows reading *ed files.        */
    /* FAT doesn't have anything equivalent, so both are mapped to READ */
    fat_open_read(fname, buf);
    break;

  case OPEN_WRITE:
  case OPEN_APPEND:
    fat_open_write(fname, buf, (mode == OPEN_APPEND));
    break;
  }
}
