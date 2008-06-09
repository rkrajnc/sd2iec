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
#include "utils.h"
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

const PROGMEM uint8_t syspart_line[] = {
    1, 1, /* next line pointer */
    0, 0, /* number of free blocks (to be filled later) */
    ' ',' ',' ',
    '"','S','Y','S','T','E','M','"',
    ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    'S','Y','S',
    0x20, 0x20, 0x00 /* Filler and end marker */
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
  'N','A','T', // 8
};

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * createentry - create a single directory entry in buf
 * @dent: directory entry to be added
 * @buf : buffer to be used
 *
 * This function creates a directory entry for dent in 15x1 compatible format
 * in the given buffer.
 */
static void createentry(struct cbmdirent *dent, buffer_t *buf, dirformat_t format) {
  uint8_t i;
  uint8_t *data = buf->data;

  if(format == DIR_FMT_CMD_LONG)
    i=63;
  else if(format == DIR_FMT_CMD_SHORT)
    i=41;
  else
    i=31;

  buf->lastused  = i;
  /* Clear the line */
  memset(data, ' ', i);
  /* Line end marker */
  data[i] = 0;

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
    if (dent->name[i] == 0x22 || dent->name[i] == 0 || i == 16) {
      data[i] = '"';
      while (i<=CBM_NAME_LENGTH) {
        if (data[i] == 0)
          data[i] = ' ';
        else
          data[i] &= 0x7f;
        i++;
      }
    }

  /* Skip name and final quote */
  data += CBM_NAME_LENGTH+1;

  if (dent->typeflags & FLAG_SPLAT)
    *data = '*';

  /* File type */
  memcpy_P(data+1, filetypes + TYPE_LENGTH * (dent->typeflags & EXT_TYPE_MASK),
           (format & DIR_FMT_CMD_SHORT) ? 1 : TYPE_LENGTH);

  /* RO marker */
  if (dent->typeflags & FLAG_RO)
    data[4] = '<';

  if(format & DIR_FMT_CMD_LONG) {
    data += 7;
    data = appendnumber(data,dent->date.month);
    *data++ = '/';
    data = appendnumber(data,dent->date.day);
    *data++ = '/';
    data = appendnumber(data,dent->date.year % 100) + 3;
    data = appendnumber(data,(dent->date.hour>12?dent->date.hour-12:dent->date.hour));
    *data++ = '.';
    data = appendnumber(data,dent->date.minute) + 1;
    *data++ = (dent->date.hour>11?'P':'A');
    *data++ = 'M';
    while (*data)
      *data++ = 1;
  } else if(format == DIR_FMT_CMD_SHORT) {
    /* Add date/time stamp */
    data+=3;
    data = appendnumber(data,dent->date.month);
    *data++ = '/';
    data = appendnumber(data,dent->date.day) + 1;
    data = appendnumber(data,(dent->date.hour>12?dent->date.hour-12:dent->date.hour));
    *data++ = '.';
    data = appendnumber(data,dent->date.minute) + 1;
    *data++ = (dent->date.hour>11?'P':'A');
    while(*data)
      *data++ = 1;
  } else {
    /* Extension: Hidden marker */
    if (dent->typeflags & FLAG_HIDDEN)
      data[5] = 'H';
  }
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
  buf->lastused = 31;
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
    dent.typeflags = TYPE_NAT;

    /* Parse the name pattern */
    if (buf->pvt.pdir.matchstr &&
        !match_name(buf->pvt.pdir.matchstr, &dent, 0))
      continue;

    createentry(&dent, buf, DIR_FMT_CBM);
    return 0;
  }
  buf->lastused = 1;
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

  switch (next_match(&buf->pvt.dir.dh,
                     buf->pvt.dir.matchstr,
                     buf->pvt.dir.match_start,
                     buf->pvt.dir.match_end,
                     buf->pvt.dir.filetype,
                     &dent)) {
  case 0:
    createentry(&dent, buf, buf->pvt.dir.format);
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
  uint8_t pos=1;

  buf = alloc_buffer();
  if (!buf)
    return;

  uint8_t *name;

  buf->secondary = secondary;
  buf->read      = 1;
  buf->lastused  = 31;

  if (command_length > 2) {
    if(command_buffer[1]=='=') {
      if(command_buffer[2]=='P') {
        /* Parse Partition Directory */

        /* copy static header to start of buffer */
        memcpy_P(buf->data, dirheader, sizeof(dirheader));
        memcpy_P(buf->data + 32, syspart_line, sizeof(syspart_line));
        buf->lastused  = 63;

        /* set partition number */
        buf->data[HEADER_OFFSET_DRIVE] = max_part;

        /* Let the refill callback handle everything else */
        buf->refill = pdir_refill;

        if(command_length>3) {
          /* Parse the name pattern */
          if (parse_path(command_buffer+3, &path, &name, 0)) {
            free_buffer(buf);
            return;
          }
          buf->pvt.pdir.matchstr = name;
        }

        return;
      } else if(command_buffer[2]=='T') {
        buf->pvt.dir.format = DIR_FMT_CMD_SHORT;
        pos=3;
      }
    }
  }

  if (command_buffer[pos]) { /* do we have a path to scan? */
    if (command_length > 2) {
      /* Parse the name pattern */
      if (parse_path(command_buffer+pos, &path, &name, 0)) {
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
        switch (*name) {
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
        }
        if(buf->pvt.dir.filetype) {
          name++;
          if(*name++ != ',') {
            goto scandone;
          }
        }
        while(*name) {
          switch(*name++) {
          case '>':
            if(parse_date(&date_match_start,&name))
              goto scandone;
            if(date_match_start.month && date_match_start.day) // ignore 00/00/00
              buf->pvt.dir.match_start = &date_match_start;
            break;
          case '<':
            if(parse_date(&date_match_end,&name))
              goto scandone;
            if(date_match_end.month && date_match_end.day) // ignore 00/00/00
              buf->pvt.dir.match_end = &date_match_end;
            break;
          case 'L':
            /* don't switch to long format if 'N' has already been sent */
            if(buf->pvt.dir.format != DIR_FMT_CBM)
              buf->pvt.dir.format = DIR_FMT_CMD_LONG;
            break;
          case 'N':
            buf->pvt.dir.format=DIR_FMT_CBM; /* turn off extended listing */
            break;
          default:
            goto scandone;
          }
          if(*name && *name++ != ',') {
            goto scandone;
          }
        }
      }
    } else {
      /* Command string is two characters long, parse the drive */
      if (command_buffer[1] == '0')
        path.part = current_part;
      else
        path.part = command_buffer[1] - '0' - 1;
      if (path.part >= max_part) {
        set_error(ERROR_DRIVE_NOT_READY);
        free_buffer(buf);
        return;
      }
      path.fat  = partition[path.part].current_dir;
      if (opendir(&buf->pvt.dir.dh, &path)) {
        free_buffer(buf);
        return;
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

scandone:
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

  /* Let the refill callback handle everything else */
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
  uint8_t i = 0;

  /* Assume everything will go well unless proven otherwise */
  set_error(ERROR_OK);

  /* Clear the remainder of the command buffer, simplifies parsing */
  memset(command_buffer+command_length, 0, sizeof(command_buffer)-command_length);

  uart_trace(command_buffer,0,command_length);

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

  while(i++ < 2 && *ptr && (ptr = ustrchr(ptr, ','))) {
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
      if((ptr = ustrchr(ptr, ',')))
        ;//recordlen = *(++ptr);
      i = 2;  // stop the scan
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
