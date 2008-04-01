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


   fileops.c: Generic file operations

*/

#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "dirent.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fatops.h"
#include "ff.h"
#include "m2iops.h"
#include "parser.h"
#include "uart.h"
#include "ustring.h"
#include "wrapops.h"
#include "fileops.h"

/* ------------------------------------------------------------------------- */
/*  Some constants used for directory generation                             */
/* ------------------------------------------------------------------------- */

#define HEADER_OFFSET_DRIVE 4
#define HEADER_OFFSET_NAME  8
#define HEADER_OFFSET_ID   26

/* NOTE: I wonder if RLE-packing would save space in flash? */
const PROGMEM uint8_t dirheader[] = {
  1, 4,                            /* BASIC start address */
  1, 1,                            /* next line pointer */
  0, 0,                            /* line number 0 */
  0x12, 0x22,                      /* Reverse on, quote */
  'S','D','2','I','E','C',' ',' ', /* 16 spaces as the disk name */
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
  'D','I','R', // 6
  '?','?','?', // 7
  'S','Y','S', // 8
  'N','A','T', // 9
  'F','A','T' // 10
};

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * addentry - create a single directory entry in buf
 * @dent: directory entry to be added
 * @buf : buffer to be used
 *
 * This function creates a directory entry for dent in 15x1 compatible format
 * in the given buffer.
 */
static void addentry(struct cbmdirent *dent, uint8_t *buf) {
  uint8_t i;

  /* Clear the line */
  memset(buf, ' ', 31);
  /* Line end marker */
  buf[31] = 0;

  /* Next line pointer, 1571-compatible =) */
  if (dent->remainder != 0xff)
    /* store remainder in low byte of link pointer          */
    /* +2 so it is never 0 (end-marker) or 1 (normal value) */
    *buf++ = dent->remainder+2;
  else
    *buf++ = 1;
  *buf++ = 1;

  *buf++ = dent->blocksize & 0xff;
  *buf++ = dent->blocksize >> 8;

  /* Filler before file name */
  if (dent->blocksize < 1000)
    buf++;
  if (dent->blocksize < 100)
    buf++;
  if (dent->blocksize < 10)
    buf++;
  *buf++ = '"';

  /* copy and adjust the filename - C783 */
  memcpy(buf, dent->name, CBM_NAME_LENGTH);
  for (i=0;i<=CBM_NAME_LENGTH;i++)
    if (dent->name[i] == 0x22 || dent->name[i] == 0 || i == 16) {
      buf[i] = '"';
      while (i<=CBM_NAME_LENGTH) {
        if (buf[i] == 0)
          buf[i] = ' ';
        else
          buf[i] &= 0x7f;
        i++;
      }
    }

  /* Skip name and final quote */
  buf += CBM_NAME_LENGTH+1;

  if (dent->typeflags & FLAG_SPLAT)
    *buf = '*';

  /* File type */
  memcpy_P(buf+1, filetypes + TYPE_LENGTH * (dent->typeflags & EXT_TYPE_MASK), TYPE_LENGTH);

  /* RO marker */
  if (dent->typeflags & FLAG_RO)
    buf[4] = '<';

  /* Extension: Hidden marker */
  if (dent->typeflags & FLAG_HIDDEN)
    buf[5] = 'H';
}

/* ------------------------------------------------------------------------- */
/*  Callbacks                                                                */
/* ------------------------------------------------------------------------- */

/**
 * dir_footer - generate the directory footer
 * @buf: buffer to be used
 *
 * This is the final callback used during directory generation. It generates
 * the "BLOCKS FREE" message and indicates that this is the final buffer to
 * be sent. Always returns 0 for success.
 */
static uint8_t dir_footer(buffer_t *buf) {
  uint16_t blocks;

  /* Copy the "BLOCKS FREE" message */
  memcpy_P(buf->data, dirfooter, sizeof(dirfooter));

  blocks = disk_free(buf->pvt.dir.dh.part);
  buf->data[2] = blocks & 0xff;
  buf->data[3] = blocks >> 8;

  buf->position = 0;
  buf->sendeoi  = 1;

  return 0;
}

/* Callback for the partition directory */
static uint8_t pdir_refill(buffer_t* buf) {
  struct cbmdirent dent;

  buf->position = 0;
  /* read volume name */
  while(buf->pvt.pdir.part < max_part) {
    if (fat_getvolumename(buf->pvt.pdir.part, dent.name)) {
      free_buffer(buf);
      return 0;
    }
    dent.blocksize=++buf->pvt.pdir.part;
    dent.typeflags = TYPE_FAT;

    /* Parse the name pattern */
    if (buf->pvt.pdir.matchstr &&
        !match_name(buf->pvt.pdir.matchstr, &dent))
      continue;

    addentry(&dent, buf->data);
    return 0;
  }
  buf->lastused = 2;
  buf->sendeoi = 1;
  memset(buf->data,0,2);
  return 0;
}

/**
 * dir_refill - generate the next directory entry
 * @buf: buffer to be used
 *
 * This function generates a single directory entry with the next matching
 * file. If there is no more matching file the footer will be generated
 * instead. Used as a callback during directory generation.
 */
static uint8_t dir_refill(buffer_t *buf) {
  struct cbmdirent dent;

  uart_putc('+');

  buf->position = 0;

  switch (next_match(&buf->pvt.dir.dh, buf->pvt.dir.matchstr,
                     buf->pvt.dir.filetype, &dent)) {
  case 0:
    addentry(&dent, buf->data);
    return 0;

  case -1:
    return dir_footer(buf);

  default:
    free_buffer(buf);
    return 1;
  }
}

/**
 * load_directory - Prepare directory generation and create header
 * @secondary: secondary address used for reading the directory
 *
 * This function prepeares directory reading and fills the buffer
 * with the header line of the directory listing.
 * BUG: There is a not-well-known feature in the 1541/1571 disk
 * drives (and possibly others) that returns unparsed directory
 * sectors if $ is opened with a secondary address != 0. This
 * is not emulated here.
 */
static void load_directory(uint8_t secondary) {
  buffer_t *buf;
  path_t path;

  buf = alloc_buffer();
  if (!buf)
    return;

  uint8_t *name;
  if (command_length > 2) {
    if(command_buffer[1]=='=' && command_buffer[2]=='P') {
      /* Parse Partition Directory */
      buf->secondary = secondary;
      buf->read      = 1;
      buf->lastused  = 31;

      /* copy static header to start of buffer */
      memcpy_P(buf->data, dirheader, sizeof(dirheader));

      /* set partition number */
      buf->data[HEADER_OFFSET_DRIVE] = max_part;

      /* Let the refill callback handly everything else */
      buf->refill = pdir_refill;

      /* Parse the name pattern */
      if (parse_path(command_buffer+3, &path, &name, 0)) {
        free_buffer(buf);
        return;
      }

      if (ustrlen(name) != 0)
        buf->pvt.pdir.matchstr = name;

      return;
    }

    /* Parse the name pattern */
    if (parse_path(command_buffer+1, &path, &name, 0)) {
      free_buffer(buf);
      return;
    }

    if (opendir(&buf->pvt.dir.dh, &path)) {
      free_buffer(buf);
      return;
    }

    buf->pvt.dir.matchstr = name;

    /* Check for a filetype match */
    name = ustrchr(name, '=');
    if (name != NULL) {
      *name++ = 0;
      switch (*name++) {
      case 'S':
        buf->pvt.dir.filetype = TYPE_SEQ;
        break;

      case 'P':
        buf->pvt.dir.filetype = TYPE_PRG;
        break;

      case 'U':
        buf->pvt.dir.filetype = TYPE_USR;
        break;

      case 'R':
        buf->pvt.dir.filetype = TYPE_REL;
        break;

      case 'C': /* This is guessed, not verified */
        buf->pvt.dir.filetype = TYPE_CBM;
        break;

      case 'B': /* CMD compatibility */
      case 'D': /* Specifying DEL matches everything anyway */
        buf->pvt.dir.filetype = TYPE_DIR;
        break;

      case 'H': /* Extension: Also show hidden files */
        buf->pvt.dir.filetype = FLAG_HIDDEN;
        break;

      default:
        buf->pvt.dir.filetype = 0;
        break;
      }
    }
  } else {
    path.part = current_part;
    path.fat=partition[path.part].current_dir;  // if you do not do this, get_label will fail below.
    if (opendir(&buf->pvt.dir.dh, &path)) {
      free_buffer(buf);
      return;
    }
  }

  buf->secondary = secondary;
  buf->read      = 1;
  buf->lastused  = 31;

  /* copy static header to start of buffer */
  memcpy_P(buf->data, dirheader, sizeof(dirheader));

  /* set partition number */
  buf->data[HEADER_OFFSET_DRIVE] = path.part+1;

  /* read volume name */
  if (disk_label(&path, buf->data+HEADER_OFFSET_NAME)) {
    free_buffer(buf);
    return;
  }

  /* read id */
  if (disk_id(path.part,buf->data+HEADER_OFFSET_ID)) {
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

/**
 * file_open - open a file on given secondary
 * @secondary: secondary address used in OPEN call
 *
 * This function opens the file named in command_buffer on the given
 * secondary address. All special names and prefixes/suffixed are handled
 * here, e.g. $/#/@/,S,W
 */
void file_open(uint8_t secondary) {
  buffer_t *buf;

  /* Assume everything will go well unless proven otherwise */
  set_error(ERROR_OK);

  command_buffer[command_length] = 0;

  /* Load directory? */
  if (command_buffer[0] == '$') {
    // FIXME: Secondary != 0? Compare D7F7-D7FF
    load_directory(secondary);
    return;
  }

  /* Direct access? */
  if (command_buffer[0] == '#') {
    // FIXME: This command can specify a specific buffer number.
    buf = alloc_buffer();
    if (!buf)
      return;

    buf->secondary = secondary;
    buf->read      = 1;
    buf->position  = 1;  /* Sic! */
    buf->lastused  = 255;
    buf->sendeoi   = 1;
    mark_write_buffer(buf);
    return;
  }

  /* Parse type+mode suffixes */
  uint8_t *ptr = command_buffer;
  enum open_modes mode = OPEN_READ;
  uint8_t filetype = TYPE_DEL;

  while (*ptr && (ptr = ustrchr(ptr, ','))) {
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

  /* Parse path+partition numbers */
  uint8_t *cbuf = command_buffer;
  uint8_t *fname;
  int8_t res;
  struct cbmdirent dent;
  path_t path;

  /* Check for rewrite marker */
  if (command_buffer[0] == '@')
    cbuf = command_buffer+1;
  else
    cbuf = command_buffer;

  if (parse_path(cbuf, &path, &fname, 0))
    return;

  /* For M2I only: Remove trailing spaces from name */
  if (partition[path.part].fop == &m2iops) {
    res = ustrlen(fname);
    while (--res && fname[res] == ' ')
      fname[res] = 0;
  }

  /* Filename matching */
  res = first_match(&path, fname, FLAG_HIDDEN, &dent);
  if (res > 0)
    return;

  if (mode == OPEN_WRITE) {
    if (res == 0) {
      /* Match found */
      if (cbuf != command_buffer) {
        /* Make sure there is a free buffer to open the new file later */
        if (!check_free_buffers()) {
          set_error(ERROR_NO_CHANNEL);
          return;
        }

        /* Rewrite existing file: Delete the old one */
        if (file_delete(&path, &dent) == 255)
          return;
      } else {
        /* Write existing file without replacement: Raise error */
        set_error(ERROR_FILE_EXISTS);
        return;
      }
    } else {
      /* Normal write or non-existing rewrite */
      /* Doesn't exist: Copy name to dent */
      ustrcpy(dent.name, fname);
      dent.realname[0] = 0;
      set_error(ERROR_OK); // because first_match has set FNF
    }
  } else
    if (res != 0) {
      /* File not found */
      set_error(ERROR_FILE_NOT_FOUND);
      return;
    }

  /* Grab a buffer */
  buf = alloc_buffer();
  if (!buf)
    return;

  buf->secondary = secondary;

  switch (mode) {
  case OPEN_MODIFY:
  case OPEN_READ:
    /* Modify is the same as read, but allows reading *ed files.        */
    /* FAT doesn't have anything equivalent, so both are mapped to READ */
    open_read(&path, &dent, buf);
    break;

  case OPEN_WRITE:
  case OPEN_APPEND:
    open_write(&path, &dent, filetype, buf, (mode == OPEN_APPEND));
    break;
  }
}
