/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>

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
#include "dirent.h"
#include "errormsg.h"
#include "buffers.h"
#include "uart.h"
#include "doscmd.h"
#include "wrapops.h"
#include "m2iops.h"
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

/// Pointer to the currently active fileops structure
const fileops_t *fop;

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * dent2str - zero terminate the commodore name in dent
 * @dent: dent containing the name to be terminated
 *
 * This function zero-terminates the file name in dent and returns a
 * pointer to that name.
 */
char *dent2str(struct cbmdirent *dent) {
  uint8_t i;

  for (i=0;i<CBM_NAME_LENGTH;i++) {
    if (dent->name[i] == 0xa0) {
      dent->name[i] = 0;
      return (char *)dent->name;
    }
  }
  dent->name[CBM_NAME_LENGTH] = 0;
  return (char *)dent->name;
}

/**
 * addentry - add a single directory entry to the end of buf
 * @dent: directory entry to be added
 * @buf : buffer to be added to
 *
 * This function adds a directory entry for dent in 15x1 compatible format
 * to the end of buf.
 */
static void addentry(struct cbmdirent *dent, buffer_t *buf) {
  uint8_t i;
  uint8_t *data;

  data = buf->data + buf->lastused;

  /* Clear the line */
  memset(data, ' ', 31);
  /* Line end marker */
  data[31] = 0;

  /* Next line pointer, 1571-compatible =) */
  if (dent->remainder != 0xff)
    /* store remainder in low byte of link pointer          */
    /* +2 so it is never 0 (end-marker) or 1 (normal value) */
    *data++ = dent->remainder+2;
  else
    *data++ = 1;
  *data++ = 1;

  *data++ = dent->blocksize & 0xff;
  *data++ = dent->blocksize >> 8;

  /* Filler before file name */
  if (dent->blocksize < 1000)
    data++;
  if (dent->blocksize < 100)
    data++;
  if (dent->blocksize < 10)
    data++;
  *data++ = '"';

  /* copy and adjust the filename - C783 */
  memcpy(data, dent->name, CBM_NAME_LENGTH);
  for (i=0;i<=CBM_NAME_LENGTH;i++)
    if (dent->name[i] == 0x22 || dent->name[i] == 0xa0 || i == 16) {
      data[i] = '"';
      while (i<=CBM_NAME_LENGTH)
	data[i++] &= 0x7f;
    }

  /* Skip name and final quote */
  data += CBM_NAME_LENGTH+1;

  if (dent->typeflags & FLAG_SPLAT)
    *data = '*';

  /* File type */
  memcpy_P(data+1, filetypes + TYPE_LENGTH * (dent->typeflags & TYPE_MASK), TYPE_LENGTH);

  /* RO marker */
  if (dent->typeflags & FLAG_RO)
    data[4] = '<';

  /* Extension: Hidden marker */
  if (dent->typeflags & FLAG_HIDDEN)
    data[5] = 'H';

  buf->lastused += 32;
}

/**
 * match_name - Match a pattern against a file name
 * @matchstr: pattern to be matched
 * @dent    : pointer to the directory entry to be matched against
 *
 * This function tests if matchstr matches name in dent.
 */
static uint8_t match_name(char *matchstr, struct cbmdirent *dent) {
  uint8_t *filename = dent->name;
  uint8_t i = 0;

  while (filename[i] != 0xa0 && i < CBM_NAME_LENGTH) {
    switch (*matchstr) {
    case '?':
      i++;
      matchstr++;
      break;

    case '*':
      return 1;

    default:
      if (filename[i++] != *matchstr++)
	return 0;
      break;
    }
  }
  if (*matchstr && *matchstr != '*')
    return 0;
  else
    return 1;
}

/**
 * next_match - get next matching directory entry
 * @dh      : directory handle
 * @matchstr: pattern to be matched
 * @type    : required file type (0 for any)
 * @dent    : pointer to a directory entry for returning the match
 *
 * This function looks for the next directory entry matching matchstr and
 * type (if != 0) and returns it in dent. Return values of the function are
 * -1 if no match could be found, 1 if an error occured or 0 if a match was
 * found.
 */
int8_t next_match(dh_t *dh, char *matchstr, uint8_t type, struct cbmdirent *dent) {
  int8_t res;
  while (1) {
    res = readdir(dh, dent);
    if (res == 0) {
      /* Skip if the type doesn't match */
      if ((type & TYPE_MASK) &&
	  (dent->typeflags & TYPE_MASK) != (type & TYPE_MASK))
	continue;

      /* Skip hidden files */
      if ((dent->typeflags & FLAG_HIDDEN) &&
	  !(type & FLAG_HIDDEN))
	continue;

      /* Skip if the name doesn't match */
      if (matchstr &&
	  !match_name(matchstr, dent))
	continue;
    }

    return res;
  }
}

/* ------------------------------------------------------------------------- */
/*  Callbacks                                                                */
/* ------------------------------------------------------------------------- */

/**
 * generic_cleanup - generic cleanup-callback that just frees the buffer
 * @buf: buffer to be used
 *
 * This function just frees the given buffer and returns 0 for success.
 * It can be used if no special cleanup for a buffer is required.
 */
uint8_t generic_cleanup(buffer_t *buf) {
  free_buffer(buf);
  return 0;
}

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

  blocks = disk_free();
  buf->data[2] = blocks & 0xff;
  buf->data[3] = blocks >> 8;

  buf->position = 0;
  buf->lastused = 31;
  buf->sendeoi  = 1;

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
  buf->lastused = 0;

  switch (next_match(&buf->pvt.dir.dh, buf->pvt.dir.matchstr,
		     buf->pvt.dir.filetype, &dent)) {
  case 0:
    addentry(&dent, buf);
    buf->lastused--;
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

  buf = alloc_buffer();
  if (!buf)
    return;

  buf->pvt.dir.filetype = 0;
  if (command_length > 2) {
    /* Parse the name pattern */
    char *name;

    parse_path((char *) command_buffer+1, (char *) command_buffer, &name);

    if (opendir(&buf->pvt.dir.dh, (char *) command_buffer)) {
      free_buffer(buf);
      return;
    }

    buf->pvt.dir.matchstr = name;

    /* Check for a filetype match */
    name = strchr(name, '=');
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
    if (opendir(&buf->pvt.dir.dh,"")) {
      free_buffer(buf);
      return;
    }
    buf->pvt.dir.matchstr = NULL;
  }

  buf->secondary = secondary;
  buf->read      = 1;
  buf->write     = 0;
  buf->cleanup   = generic_cleanup;
  buf->position  = 0;
  buf->lastused  = 31;
  buf->sendeoi   = 0;

  /* copy static header to start of buffer */
  memcpy_P(buf->data, dirheader, sizeof(dirheader));

  /* read volume name */
  if (disk_label((char *) (buf->data+8))) {
    free_buffer(buf);
    return;
  }

  /* read id */
  if (disk_id((char *) (buf->data+HEADER_OFFSET_ID))) {
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

  /* Empty name? */
  if (command_length == 0) {
    set_error(ERROR_SYNTAX_NONAME);
    return;
  }

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
    buf->write     = 1;
    buf->position  = 1;  /* Sic! */
    buf->lastused  = 255;
    buf->sendeoi   = 1;
    buf->mustflush = 0;
    buf->cleanup   = NULL; // FIXME: free_buffer? Der erste Patch verschob die free-Pflicht zum Aufrufer
    buf->refill    = NULL;
    active_buffers += 16;
    DIRTY_LED_ON();
    return;
  }

  /* Parse type+mode suffixes */
  char *ptr = (char *) command_buffer;
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

  /* Parse path+drive numbers */
  char *cbuf = (char *) command_buffer;
  char *fname;
  int8_t res;
  struct cbmdirent dent;

  /* Check for rewrite marker */
  if (command_buffer[0] == '@')
    cbuf = (char *)command_buffer+1;
  else
    cbuf = (char *)command_buffer;

  parse_path(cbuf, (char *) command_buffer, &fname);

  /* For M2I only: Remove trailing spaces from name */
  if (fop == &m2iops) {
    res = strlen(fname);
    while (--res && fname[res] == ' ')
      fname[res] = 0;
  }

  /* Filename matching */
  if (opendir(&matchdh, (char *) command_buffer))
    return;

  res = next_match(&matchdh, fname, FLAG_HIDDEN, &dent);
  if (res > 0)
    return;

  if (mode == OPEN_WRITE) {
    if (res == 0) {
      /* Match found */
      if (cbuf != (char *) command_buffer) {
	/* Rewrite existing file: Delete the old one */
	/* This is safe. If there is no buffer available, delete will fail. */
	if (file_delete((char *) command_buffer, fname) == 255)
	  return;
      } else {
	/* Write existing file without replacement: Raise error */
	set_error(ERROR_FILE_EXISTS);
	return;
      }
    } else {
      /* Normal write or non-existing rewrite */
      /* Doesn't exist: Copy name to dent */
      strcpy((char *)dent.name, fname);
    }
  } else
    if (res != 0) {
      /* File not found */
      set_error(ERROR_FILE_NOT_FOUND);
      return;
    }

  fname = dent2str(&dent);

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
    open_read((char *) command_buffer, fname, buf);
    break;

  case OPEN_WRITE:
  case OPEN_APPEND:
    open_write((char *) command_buffer, fname, filetype, buf, (mode == OPEN_APPEND));
    break;
  }
}
