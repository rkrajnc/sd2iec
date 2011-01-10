/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2011  Ingo Korb <ingo@akana.de>
   ASCII/PET conversion Copyright (C) 2008 Jim Brain <brain@jbrain.com>

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


   fatops.c: FAT operations

*/

#include <avr/pgmspace.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include "config.h"
#include "buffers.h"
#include "d64ops.h"
#include "diskchange.h"
#include "diskio.h"
#include "display.h"
#include "doscmd.h"
#include "errormsg.h"
#include "ff.h"
#include "fileops.h"
#include "flags.h"
#include "led.h"
#include "m2iops.h"
#include "parser.h"
#include "uart.h"
#include "ustring.h"
#include "wrapops.h"
#include "fatops.h"

#define P00_HEADER_SIZE       26
#define P00_CBMNAME_OFFSET    8
#define P00_RECORDLEN_OFFSET  25

static const PROGMEM char p00marker[] = "C64File";

typedef enum { EXT_UNKNOWN, EXT_IS_X00, EXT_IS_TYPE } exttype_t;

uint8_t file_extension_mode;

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * parse_error - translates a ff FRESULT into a commodore error message
 * @res     : FRESULT to be translated
 * @readflag: Flags if it was a read operation
 *
 * This function sets the error channel according to the problem given in
 * res. readflag specifies if a READ ERROR or WRITE ERROR should be used
 * if the error is FR_RW_ERROR.
 */
void parse_error(FRESULT res, uint8_t readflag) {
  switch (res) {
  case FR_OK:
    set_error(ERROR_OK);
    break;

  case FR_NO_FILE:
    set_error_ts(ERROR_FILE_NOT_FOUND,res,0);
    break;

  case FR_NO_PATH:
  case FR_NOT_DIRECTORY:
    set_error_ts(ERROR_FILE_NOT_FOUND_39,res,0);
    break;

  case FR_INVALID_NAME:
    set_error_ts(ERROR_SYNTAX_JOKER,res,0);
    break;

  case FR_NOT_READY:
  case FR_INVALID_DRIVE:
  case FR_NOT_ENABLED:
  case FR_NO_FILESYSTEM:
    set_error_ts(ERROR_DRIVE_NOT_READY,res,0);
    break;

  case FR_RW_ERROR:
    /* Just a random READ ERROR */
    if (readflag)
      set_error_ts(ERROR_READ_NOHEADER,res,0);
    else
      set_error_ts(ERROR_WRITE_VERIFY,res,0);
    break;

  case FR_WRITE_PROTECTED:
    set_error_ts(ERROR_WRITE_PROTECT,res,0);
    break;

  case FR_EXIST:
    set_error_ts(ERROR_FILE_EXISTS,res,0);
    break;

  case FR_DIR_NOT_EMPTY:
    // FIXME: What do the CMD drives return when removing a non-empty directory?
    set_error_ts(ERROR_FILE_EXISTS,res,0);
    break;

  case FR_DENIED:
    set_error_ts(ERROR_DISK_FULL,res,0);
    break;

  case FR_IS_READONLY:
  case FR_IS_DIRECTORY:
    set_error_ts(ERROR_FILE_EXISTS,res,0);
    break;

  case FR_INVALID_OBJECT:
    set_error_ts(ERROR_DRIVE_NOT_READY,res,0);
    break;

  default:
    set_error_ts(ERROR_SYNTAX_UNABLE,res,99);
    break;
  }
}

/**
 * check_extension - check for known file-type-based name extensions
 * @name: pointer to the file name
 * @ext : pointer to pointer to the file extension
 *
 * This function checks if the given file name has an extension that
 * indicates a specific file type like PRG/SEQ/P00/S00/... The ext
 * pointer will be set to the first character of the extension if
 * any is present or NULL if not. Returns EXT_IS_X00 for x00,
 * EXT_IS_TYPE for PRG/SEQ/... or EXT_UNKNOWN for an unknown file extension.
 */
static exttype_t check_extension(uint8_t *name, uint8_t **ext) {
  uint8_t f,s,t;

  /* Search for the file extension */
  if ((*ext = ustrrchr(name, '.')) != NULL) {
    f = *(++(*ext));
    s = *(*ext+1);
    t = *(*ext+2);
    if ((f == 'P' || f == 'S' ||
         f == 'U' || f == 'R') &&
        isdigit(s) && isdigit(t))
      return EXT_IS_X00;
    else if ((f=='P' && s == 'R' && t == 'G') ||
             (f=='S' && s == 'E' && t == 'Q') ||
             (f=='R' && s == 'E' && t == 'L') ||
             (f=='U' && s == 'S' && t == 'R'))
      return EXT_IS_TYPE;
  }
  return EXT_UNKNOWN;
}

/**
 * check_imageext - check for a known image file extension
 * @name: pointer to the file name
 *
 * This function checks if the given file name has an extension that
 * indicates a known image file type. Returns IMG_IS_M2I for M2I files,
 * IMG_IS_DISK for D64/D41/D71/D81 files or IMG_UNKNOWN for an unknown
 * file extension.
 */
imgtype_t check_imageext(uint8_t *name) {
  uint8_t f,s,t;
  uint8_t *ext = ustrrchr(name, '.');

  if (ext == NULL)
    return IMG_UNKNOWN;

  f = toupper(*++ext);
  s = toupper(*++ext);
  t = toupper(*++ext);

  if (f == 'M' && s == '2' && t == 'I')
    return IMG_IS_M2I;

  if (f == 'D')
    if ((s == '6' && t == '4') ||
        (s == 'N' && t == 'P') ||
        ((s == '4' || s == '7' || s == '8') &&
         (t == '1')))
      return IMG_IS_DISK;

 return IMG_UNKNOWN;
}

/**
 * asc2pet - convert string from ASCII to PETSCII
 * @buf: pointer to the string to be converted
 *
 * This function converts the string in the given buffer from ASCII to
 * PETSCII in-place.
 */
static void asc2pet(uint8_t *buf) {
  uint8_t ch;
  while (*buf) {
    ch = *buf;
    if (ch > 64 && ch < 91)
      ch += 128;
    else if (ch > 96 && ch < 123)
      ch -= 32;
    else if (ch > 192 && ch < 219)
      ch -= 128;
    else if (ch == '~')
      ch = 255;
    *buf = ch;
    buf++;
  }
}

/**
 * pet2asc - convert string from PETSCII to ASCII
 * @buf: pointer to the string to be converted
 *
 * This function converts the string in the given buffer from PETSCII to
 * ASCII in-place.
 */
static void pet2asc(uint8_t *buf) {
  uint8_t ch;
  while (*buf) {
    ch = *buf;
    if (ch > (128+64) && ch < (128+91))
      ch -= 128;
    else if (ch > (96-32) && ch < (123-32))
      ch += 32;
    else if (ch > (192-128) && ch < (219-128))
      ch += 128;
    else if (ch == 255)
      ch = '~';
    *buf = ch;
    buf++;
  }
}

static uint8_t* build_name(uint8_t *name, uint8_t type) {
  uint8_t *x00ext = NULL;

  pet2asc(name);
  if (type != TYPE_RAW && file_extension_mode != 0 &&
      !(type == TYPE_PRG && check_imageext(name) != IMG_UNKNOWN)) {
    if ((file_extension_mode == 1 && type != TYPE_PRG) ||
        (file_extension_mode == 2)
        ) {
      /* Append .[PSUR]00 suffix to the file name */
      while (*name) {
        if (isalnum(*name) || *name == '!' ||
            (*name >= '#' && *name <= ')') ||
            *name == '-') {
          name++;
        } else {
          *name++ = '_';
        }
      }
      *name++ = '.';
      *name++ = pgm_read_byte(filetypes+3*type);
      *name++ = '0';
      x00ext = name;
      *name++ = '0';
      *name   = 0;
    } else if ((file_extension_mode == 3 && type != TYPE_PRG) ||
               (file_extension_mode == 4)) {
      /* Append type suffix to the file name */
      while (*name) name++;
      *name++ = '.';
      memcpy_P(name, filetypes + TYPE_LENGTH * (type & EXT_TYPE_MASK), TYPE_LENGTH);
      *(name+3) = 0;
    }
  }
  return x00ext;
}

/* ------------------------------------------------------------------------- */
/*  Callbacks                                                                */
/* ------------------------------------------------------------------------- */

/**
 * fat_file_read - read the next data block into the buffer
 * @buf: buffer to be worked on
 *
 * This function reads the next block of data from the associated file into
 * the given buffer. Used as a refill-callback when reading files
 */
static uint8_t fat_file_read(buffer_t *buf) {
  FRESULT res;
  UINT bytesread;

  uart_putc('#');

  buf->fptr = buf->pvt.fat.fh.fptr - buf->pvt.fat.headersize;

  res = f_read(&buf->pvt.fat.fh, buf->data+2, (buf->recordlen ? buf->recordlen : 254), &bytesread);
  if (res != FR_OK) {
    parse_error(res,1);
    free_buffer(buf);
    return 1;
  }

  /* The bus protocol can't handle 0-byte-files */
  if (bytesread == 0) {
    bytesread = 1;
    /* Experimental data suggests that this may be correct */
    buf->data[2] = (buf->recordlen ? 255 : 13);
  }

  buf->position = 2;
  buf->lastused = bytesread+1;
  if(buf->recordlen) // strip nulls from end of REL record.
    while(!buf->data[buf->lastused] && --(buf->lastused) > 1);
  if (bytesread < 254
      || (buf->pvt.fat.fh.fsize - buf->pvt.fat.fh.fptr) == 0
      || buf->recordlen
     )
    buf->sendeoi = 1;
  else
    buf->sendeoi = 0;

  return 0;
}

/**
 * write_data - write the current buffer data
 * @buf: buffer to be worked on
 *
 * This function writes the current contents of the given buffer into its
 * associated file.
 */
static uint8_t write_data(buffer_t *buf) {
  FRESULT res;
  UINT byteswritten;

  uart_putc('/');

  if(!buf->mustflush)
    buf->lastused = buf->position - 1;

  if(buf->recordlen > buf->lastused - 1)
    memset(buf->data + buf->lastused + 1,0,buf->recordlen - (buf->lastused - 1));

  if(buf->recordlen)
    buf->lastused = buf->recordlen + 1;

  res = f_write(&buf->pvt.fat.fh, buf->data+2, buf->lastused-1, &byteswritten);
  if (res != FR_OK) {
    uart_putc('r');
    parse_error(res,1);
    f_close(&buf->pvt.fat.fh);
    free_buffer(buf);
    return 1;
  }

  if (byteswritten != buf->lastused-1U) {
    uart_putc('l');
    set_error(ERROR_DISK_FULL);
    f_close(&buf->pvt.fat.fh);
    free_buffer(buf);
    return 1;
  }

  mark_buffer_clean(buf);
  buf->mustflush = 0;
  buf->position  = 2;
  buf->lastused  = 2;
  buf->fptr      = buf->pvt.fat.fh.fptr - buf->pvt.fat.headersize;

  return 0;
}

/**
 * fat_file_write - refill-callback for files opened for writing
 * @buf: target buffer
 *
 * This function writes the contents of buf to the associated file.
 */
static uint8_t fat_file_write(buffer_t *buf) {
  FRESULT res = FR_OK;
  uint32_t fptr;
  uint32_t i = 0;

  fptr = buf->pvt.fat.fh.fsize - buf->pvt.fat.headersize;

  // on a REL file, the fptr will be be at the end of the record we just read.  Reposition.
  if (buf->fptr != fptr) {
    res = f_lseek(&buf->pvt.fat.fh, buf->pvt.fat.headersize + buf->fptr);
    if (res != FR_OK) {
      parse_error(res,1);
      f_close(&buf->pvt.fat.fh);
      free_buffer(buf);
      return 1;
    }
  }

  if(buf->fptr > fptr)
    i = buf->fptr - fptr;

  if (res == FR_OK) {
    if (write_data(buf))
      return 1;
  }

  if(i) {
    // we need to fill bytes.
    // position to old end of file.
    res = f_lseek(&buf->pvt.fat.fh, buf->pvt.fat.headersize + fptr);
    buf->mustflush = 0;
    buf->fptr = fptr;
    buf->data[2] = (buf->recordlen?255:0);
    memset(buf->data + 3,0,253);
    while(res == FR_OK && i) {
      if (buf->recordlen)
        buf->lastused = buf->recordlen;
      else
        buf->lastused = (i>254 ? 254 : (uint8_t) i);

      i -= buf->lastused;
      buf->position = buf->lastused + 2;

      if(write_data(buf))
        return 1;
    }
    res = f_lseek(&buf->pvt.fat.fh, buf->pvt.fat.fh.fsize);
    if (res != FR_OK) {
      uart_putc('r');
      parse_error(res,1);
      f_close(&buf->pvt.fat.fh);
      free_buffer(buf);
      return 1;
    }
    buf->fptr = buf->pvt.fat.fh.fptr - buf->pvt.fat.headersize;
  }

  return 0;
}

/**
 * fat_file_seek - callback for seek
 * @buf     : buffer to be worked on
 * @position: offset to seek to
 * @index   : offset within the record to seek to
 *
 * This function seeks to the offset position in the file associated
 * with the given buffer and sets the read pointer to the byte given
 * in index, effectively seeking to (position+index) for normal files.
 * Returns 1 if an error occured, 0 otherwise.
 */
uint8_t fat_file_seek(buffer_t *buf, uint32_t position, uint8_t index) {
  uint32_t pos = position + buf->pvt.fat.headersize;

  if (buf->dirty)
    if (fat_file_write(buf))
      return 1;

  if (buf->pvt.fat.fh.fsize >= pos) {
    FRESULT res = f_lseek(&buf->pvt.fat.fh, pos);
    if (res != FR_OK) {
      parse_error(res,0);
      f_close(&buf->pvt.fat.fh);
      free_buffer(buf);
      return 1;
    }

    if (fat_file_read(buf))
      return 1;
  } else {
    buf->data[2]  = (buf->recordlen ? 255:13);
    buf->lastused = 2;
    buf->fptr     = position;
    set_error(ERROR_RECORD_MISSING);
  }

  buf->position = index + 2;
  if(index + 2 > buf->lastused)
    buf->position = buf->lastused;

  return 0;
}

/**
 * fat_file_sync - synchronize the current REL file.
 * @buf: buffer to be worked on
 *
 */
static uint8_t fat_file_sync(buffer_t *buf) {
  return fat_file_seek(buf,buf->fptr + buf->recordlen,0);
}

/**
 * fat_file_close - close the file associated with a buffer
 * @buf: buffer to be worked on
 *
 * This function closes the file associated with the given buffer. If the buffer
 * was opened for writing the data contents will be stored if required.
 * Additionally the buffer will be marked as free.
 * Used as a cleanup-callback for reading and writing.
 */
static uint8_t fat_file_close(buffer_t *buf) {
  FRESULT res;

  if (!buf->allocated) return 0;

  if (buf->write) {
    /* Write the remaining data using the callback */
    buf->refill(buf);
  }

  res = f_close(&buf->pvt.fat.fh);
  parse_error(res,1);
  buf->cleanup = callback_dummy;

  if (res != FR_OK)
    return 1;
  else
    return 0;
}

/* ------------------------------------------------------------------------- */
/*  Internal handlers for the various operations                             */
/* ------------------------------------------------------------------------- */

/**
 * fat_open_read - opens a file for reading
 * @path: path of the file
 * @dent: pointer to cbmdirent with name of the file
 * @buf : buffer to be used
 *
 * This functions opens a file in the FAT filesystem for reading and sets up
 * buf to access it.
 */
void fat_open_read(path_t *path, cbmdirent_t *dent, buffer_t *buf) {
  FRESULT res;
  uint8_t *name;

  pet2asc(dent->name);
  if (dent->pvt.fat.realname[0])
    name = dent->pvt.fat.realname;
  else
    name = dent->name;

  partition[path->part].fatfs.curr_dir = path->dir.fat;
  res = f_open(&partition[path->part].fatfs,&buf->pvt.fat.fh, name, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    parse_error(res,1);
    return;
  }

  if (dent->opstype == OPSTYPE_FAT_X00) {
    /* It's a [PSUR]00 file, skip the header */
    /* If anything goes wrong here, refill will notice too */
    f_lseek(&buf->pvt.fat.fh, P00_HEADER_SIZE);
    buf->pvt.fat.headersize = P00_HEADER_SIZE;
  }

  buf->read      = 1;
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_read;
  buf->seek      = fat_file_seek;

  stick_buffer(buf);

  /* Call the refill once for the first block of data */
  buf->refill(buf);
}

/**
 * create_file - creates a file
 * @path     : path of the file
 * @dent     : name of the file
 * @type     : type of the file
 * @buf      : buffer to be used
 * @recordlen: length of record, if REL file.
 *
 * This function opens a file in the FAT filesystem for writing and sets up
 * buf to access it. type is ignored here because FAT has no equivalent of
 * file types.
 */
FRESULT create_file(path_t *path, cbmdirent_t *dent, uint8_t type, buffer_t *buf, uint8_t recordlen) {
  FRESULT res;
  uint8_t *name, *x00ext;

  x00ext = NULL;

  /* FIXME: Add a comment why the "true" part of this if is required */
  if (dent->pvt.fat.realname[0])
    name = dent->pvt.fat.realname;
  else {
    ustrcpy(entrybuf, dent->name);
    x00ext = build_name(entrybuf,type);
    name = entrybuf;
  }

  partition[path->part].fatfs.curr_dir = path->dir.fat;
  do {
    res = f_open(&partition[path->part].fatfs, &buf->pvt.fat.fh, name,FA_WRITE | FA_CREATE_NEW | (recordlen?FA_READ:0));
    if (res == FR_EXIST && x00ext != NULL) {
      /* File exists, increment extension */
      *x00ext += 1;
      if (*x00ext == '9'+1) {
        *x00ext = '0';
        *(x00ext-1) += 1;
        if (*(x00ext-1) == '9'+1)
          break;
      }
    }
  } while (res == FR_EXIST);

  if (res != FR_OK)
    return res;

  if (x00ext != NULL || recordlen) {
    UINT byteswritten;

    if(x00ext != NULL) {
      /* Write a [PSUR]00 header */

      memset(entrybuf, 0, P00_HEADER_SIZE);
      ustrcpy_P(entrybuf, p00marker);
      memcpy(entrybuf+P00_CBMNAME_OFFSET, dent->name, CBM_NAME_LENGTH);
      if(recordlen)
        entrybuf[P00_RECORDLEN_OFFSET] = recordlen;
      buf->pvt.fat.headersize = P00_HEADER_SIZE;
    } else if(recordlen) {
      entrybuf[0] = recordlen;
      buf->pvt.fat.headersize = 1;
    }
    res = f_write(&buf->pvt.fat.fh, entrybuf, buf->pvt.fat.headersize, &byteswritten);
    if (res != FR_OK || byteswritten != buf->pvt.fat.headersize) {
      return res;
    }
  }

  return FR_OK;
}

/**
 * fat_open_write - opens a file for writing
 * @path  : path of the file
 * @dent  : name of the file
 * @type  : type of the file
 * @buf   : buffer to be used
 * @append: Flags if the new data should be appended to the end of file
 *
 * This function opens a file in the FAT filesystem for writing and sets up
 * buf to access it. type is ignored here because FAT has no equivalent of
 * file types.
 */
void fat_open_write(path_t *path, cbmdirent_t *dent, uint8_t type, buffer_t *buf, uint8_t append) {
  FRESULT res;

  if (append) {
    partition[path->part].fatfs.curr_dir = path->dir.fat;
    res = f_open(&partition[path->part].fatfs, &buf->pvt.fat.fh, dent->pvt.fat.realname, FA_WRITE | FA_OPEN_EXISTING);
    if (dent->opstype == OPSTYPE_FAT_X00)
      /* It's a [PSUR]00 file */
      buf->pvt.fat.headersize = P00_HEADER_SIZE;
    if (res == FR_OK)
      res = f_lseek(&buf->pvt.fat.fh, buf->pvt.fat.fh.fsize);
    buf->fptr = buf->pvt.fat.fh.fsize - buf->pvt.fat.headersize;
  } else
    res = create_file(path, dent, type, buf, 0);

  if (res != FR_OK) {
    parse_error(res,0);
    return;
  }

  mark_write_buffer(buf);
  buf->position  = 2;
  buf->lastused  = 2;
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_write;
  buf->seek      = fat_file_seek;

  /* If no data is written the file should end up with a single 0x0d byte */
  buf->data[2] = 13;

  stick_buffer(buf);
}

/**
 * fat_open_rel - creates a rel file.
 * @path  : path of the file
 * @dent  : name of the file
 * @buf   : buffer to be used
 * @length: record length
 * @mode  : select between new or existing file
 *
 * This function opens a rel file and prepares it for access.
 * If the mode parameter is 0, create a new file. If it is != 0,
 * open an existing file.
 */
void fat_open_rel(path_t *path, cbmdirent_t *dent, buffer_t *buf, uint8_t length, uint8_t mode) {
  FRESULT res;
  UINT bytesread;

  if(!mode) {
    res = create_file(path, dent, TYPE_REL, buf, length);
    bytesread = 1;
    entrybuf[0] = length;
  } else {
    partition[path->part].fatfs.curr_dir = path->dir.fat;
    res = f_open(&partition[path->part].fatfs, &buf->pvt.fat.fh, dent->pvt.fat.realname, FA_WRITE | FA_READ | FA_OPEN_EXISTING);
    if (res == FR_OK) {
      if (dent->opstype == OPSTYPE_FAT_X00) {
        res = f_lseek(&buf->pvt.fat.fh, P00_RECORDLEN_OFFSET);
      }
      if(res == FR_OK)
        /* read record length */
        res = f_read(&buf->pvt.fat.fh, entrybuf, 1, &bytesread);
      if(!length)
        length = entrybuf[0];
    }
  }

  if (res != FR_OK || bytesread != 1) {
    parse_error(res,0);
    return;
  }

  buf->pvt.fat.headersize = (uint8_t)buf->pvt.fat.fh.fptr;
  buf->recordlen  = length;
  mark_write_buffer(buf);
  buf->read      = 1;
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_sync;
  buf->seek      = fat_file_seek;

  stick_buffer(buf);

  /* read the first record */
  if(!fat_file_read(buf) && length != entrybuf[0])
    set_error(ERROR_RECORD_MISSING);
}

/* ------------------------------------------------------------------------- */
/*  External interface for the various operations                            */
/* ------------------------------------------------------------------------- */

uint8_t fat_opendir(dh_t *dh, path_t *path) {
  FRESULT res;

  res = l_opendir(&partition[path->part].fatfs, path->dir.fat, &dh->dir.fat);
  dh->part = path->part;
  if (res != FR_OK) {
    parse_error(res,1);
    return 1;
  }
  return 0;
}

/**
 * fat_readdir - readdir wrapper for FAT
 * @dh  : directory handle as set up by opendir
 * @dent: CBM directory entry for returning data
 *
 * This function reads the next directory entry into dent.
 * Returns 1 if an error occured, -1 if there are no more
 * directory entries and 0 if successful.
 */
int8_t fat_readdir(dh_t *dh, cbmdirent_t *dent) {
  FRESULT res;
  FILINFO finfo;
  uint8_t *ptr,*nameptr;
  uint8_t typechar;

  finfo.lfn = entrybuf;

  do {
    res = f_readdir(&dh->dir.fat, &finfo);
    if (res != FR_OK) {
      if (res == FR_INVALID_OBJECT)
        set_error(ERROR_DIR_ERROR);
      else
        parse_error(res,1);
      return 1;
    }
  } while ((finfo.fname[0] && (finfo.fattrib & AM_VOL)) ||
           (finfo.fname[0] == '.' && finfo.fname[1] == 0) ||
           (finfo.fname[0] == '.' && finfo.fname[1] == '.' && finfo.fname[2] == 0));

  memset(dent, 0, sizeof(cbmdirent_t));

  if (!finfo.fname[0])
    return -1;

  dent->opstype = OPSTYPE_FAT;

  /* Copy name */
  ustrcpy(dent->pvt.fat.realname, finfo.fname);

  if (!finfo.lfn[0] || ustrlen(finfo.lfn) > CBM_NAME_LENGTH+4) {
    nameptr = finfo.fname;
  } else {
    /* Convert only LFNs to PETSCII, 8.3 are always upper-case */
    nameptr = finfo.lfn;
    asc2pet(nameptr);
  }

  /* File type */
  if (finfo.fattrib & AM_DIR) {
    dent->typeflags = TYPE_DIR;
    /* Hide directories starting with . */
    if (*nameptr == '.')
      dent->typeflags |= FLAG_HIDDEN;

  } else {
    /* Search for the file extension */
    exttype_t ext = check_extension(finfo.fname, &ptr);
    if (ext == EXT_IS_X00) {
      /* [PSRU]00 file - try to read the internal name */
      UINT bytesread;

      typechar = *ptr;
      res = l_opencluster(&partition[dh->part].fatfs, &partition[dh->part].imagehandle, finfo.clust);
      if (res != FR_OK)
        goto notp00;

      res = f_read(&partition[dh->part].imagehandle, entrybuf, P00_HEADER_SIZE, &bytesread);
      if (res != FR_OK)
        goto notp00;

      if (ustrcmp_P(entrybuf, p00marker))
        goto notp00;

      /* Copy the internal name - dent->name is still zeroed */
      ustrcpy(dent->name, entrybuf+P00_CBMNAME_OFFSET);

      /* Some programs pad the name with 0xa0 instead of 0 */
      ptr = dent->name;
      for (uint8_t i=0;i<16;i++,ptr++)
        if (*ptr == 0xa0)
          *ptr = 0;

      finfo.fsize -= P00_HEADER_SIZE;
      dent->opstype = OPSTYPE_FAT_X00;

    } else if (ext == EXT_IS_TYPE && (globalflags & EXTENSION_HIDING)) {
      /* Type extension */
      typechar = *ptr;
      uint8_t i = ustrlen(nameptr)-4;
      nameptr[i] = 0;

    } else { /* ext == EXT_UNKNOWN or EXT_IS_TYPE but hiding disabled */
      /* Unknown extension: PRG */
      typechar = 'P';
    }

  notp00:
    /* Set the file type */
    switch (typechar) {
    case 'P':
      dent->typeflags = TYPE_PRG;
      break;

    case 'S':
      dent->typeflags = TYPE_SEQ;
      break;

    case 'U':
      dent->typeflags = TYPE_USR;
      break;

    case 'R':
      dent->typeflags = TYPE_REL;
      break;
    }
  }

  /* Copy file name into dirent if it fits */
  if (dent->opstype != OPSTYPE_FAT_X00) {
    if (ustrlen(nameptr) > CBM_NAME_LENGTH) {
      ustrcpy(dent->name, finfo.fname);
    } else {
      ustrcpy(dent->name, nameptr);
    }

    ptr = dent->name;
    while (*ptr) {
      if (*ptr == '~') *ptr = 0xff;
      ptr++;
    }
  }

  if (finfo.fsize > 16255746)
    /* File too large -> size 63999 blocks */
    dent->blocksize = 63999;
  else
    dent->blocksize = (finfo.fsize+253) / 254;

  dent->remainder = finfo.fsize % 254;

  /* Read-Only and hidden flags */
  if (finfo.fattrib & AM_RDO)
    dent->typeflags |= FLAG_RO;

  if (finfo.fattrib & (AM_HID|AM_SYS))
    dent->typeflags |= FLAG_HIDDEN;

  /* Cluster number */
  dent->pvt.fat.cluster = finfo.clust;

  /* Date/Time */
  dent->date.year  = (finfo.fdate >> 9) + 80;
  dent->date.month = (finfo.fdate >> 5) & 0x0f;
  dent->date.day   = finfo.fdate & 0x1f;

  dent->date.hour   = finfo.ftime >> 11;
  dent->date.minute = (finfo.ftime >> 5) & 0x3f;
  dent->date.second = (finfo.ftime & 0x1f) << 1;

  return 0;
}

/**
 * fat_delete - Delete a file/directory on FAT
 * @path: path to the file/directory
 * @dent: pointer to cbmdirent with name of the file/directory to be deleted
 *
 * This function deletes the file filename in path and returns
 * 0 if not found, 1 if deleted or 255 if an error occured.
 */
uint8_t fat_delete(path_t *path, cbmdirent_t *dent) {
  FRESULT res;
  uint8_t *name;

  set_dirty_led(1);
  if (dent->pvt.fat.realname[0]) {
    name = dent->pvt.fat.realname;
  } else {
    name = dent->name;
    pet2asc(name);
  }
  partition[path->part].fatfs.curr_dir = path->dir.fat;
  res = f_unlink(&partition[path->part].fatfs, name);

  update_leds();

  parse_error(res,0);
  if (res == FR_OK)
    return 1;
  else if (res == FR_NO_FILE)
    return 0;
  else
    return 255;
}

/**
 * fat_chdir - change directory in FAT and/or mount image
 * @path: path object for the location of dirname
 * @dent: Name of the directory/image to be changed into
 *
 * This function changes the directory of the path object to dirname.
 * If dirname specifies a file with a known extension (e.g. M2I or D64), the
 * current(!) directory will be changed to the directory of the file and
 * it will be mounted as an image file. Returns 0 if successful,
 * 1 otherwise.
 */
uint8_t fat_chdir(path_t *path, cbmdirent_t *dent) {
  FRESULT res;

  partition[path->part].fatfs.curr_dir = path->dir.fat;

  /* Left arrow moves one directory up */
  if (dent->name[0] == '_' && dent->name[1] == 0) {
    FILINFO finfo;

    entrybuf[0] = '.';
    entrybuf[1] = '.';
    entrybuf[2] = 0;

    res = f_stat(&partition[path->part].fatfs, entrybuf, &finfo);
    if (res != FR_OK) {
      parse_error(res,1);
      return 1;
    }

    dent->pvt.fat.cluster = finfo.clust;
    dent->typeflags = TYPE_DIR;
  } else if (dent->name[0] == 0) {
    /* Empty string moves to the root dir */
    path->dir.fat = 0;
    return 0;
  }

  if ((dent->typeflags & TYPE_MASK) == TYPE_DIR) {
    /* It's a directory, change to its cluster */
    path->dir.fat = dent->pvt.fat.cluster;
  } else {
    /* Changing into a file, could be a mount request */
    if (check_imageext(dent->pvt.fat.realname) != IMG_UNKNOWN) {
      /* D64/M2I mount request */
      free_multiple_buffers(FMB_USER_CLEAN);
      /* Open image file */
      res = f_open(&partition[path->part].fatfs,
                   &partition[path->part].imagehandle,
                   dent->pvt.fat.realname, FA_OPEN_EXISTING|FA_READ|FA_WRITE);

      /* Try to open read-only if medium or file is read-only */
      if (res == FR_DENIED || res == FR_WRITE_PROTECTED)
        res = f_open(&partition[path->part].fatfs,
                     &partition[path->part].imagehandle,
                     dent->pvt.fat.realname, FA_OPEN_EXISTING|FA_READ);

      if (res != FR_OK) {
        parse_error(res,1);
        return 1;
      }

      if (check_imageext(dent->pvt.fat.realname) == IMG_IS_M2I)
        partition[path->part].fop = &m2iops;
      else {
        if (d64_mount(path))
          return 1;
        partition[path->part].fop = &d64ops;
      }

      return 0;
    }
  }
  return 0;
}

/* Create a new directory */
void fat_mkdir(path_t *path, uint8_t *dirname) {
  FRESULT res;

  partition[path->part].fatfs.curr_dir = path->dir.fat;
  pet2asc(dirname);
  res = f_mkdir(&partition[path->part].fatfs, dirname);
  parse_error(res,0);
}

/**
 * fat_getvolumename - Get the volume label
 * @part : partition to request
 * @label: pointer to the buffer for the label (16 characters+zero-termination)
 *
 * This function reads the FAT volume label and stores it zero-terminated
 * in label. Returns 0 if successfull, != 0 if an error occured.
 */
static uint8_t fat_getvolumename(uint8_t part, uint8_t *label) {
  DIR dh;
  FILINFO finfo;
  FRESULT res;
  uint8_t i,j;

  finfo.lfn = NULL;
  memset(label, 0, CBM_NAME_LENGTH+1);

  res = l_opendir(&partition[part].fatfs, 0, &dh);

  if (res != FR_OK) {
    parse_error(res,0);
    return 1;
  }

  while ((res = f_readdir(&dh, &finfo)) == FR_OK) {
    if (!finfo.fname[0]) break;
    if ((finfo.fattrib & (AM_VOL|AM_SYS|AM_HID)) == AM_VOL) {
      i=0;
      j=0;
      while (finfo.fname[i]) {
        /* Skip dots */
        if (finfo.fname[i] == '.') {
          i++;
          continue;
        }
        label[j++] = finfo.fname[i++];
      }
      return 0;
    }
  }
  return 0;
}

/**
 * fat_getdirlabel - Get the directory label
 * @path : path object of the directory
 * @label: pointer to the buffer for the label (16 characters)
 *
 * This function reads the FAT volume label (if in root directory) or FAT
 * directory name (if not) and stored it space-padded
 * in the first 16 bytes of label.
 * Returns 0 if successfull, != 0 if an error occured.
 */
uint8_t fat_getdirlabel(path_t *path, uint8_t *label) {
  DIR dh;
  FILINFO finfo;
  FRESULT res;
  uint8_t *name = entrybuf;

  finfo.lfn = entrybuf;
  memset(label, ' ', CBM_NAME_LENGTH);

  res = l_opendir(&partition[path->part].fatfs, path->dir.fat, &dh);
  if (res != FR_OK)
    goto gl_error;

  while ((res = f_readdir(&dh, &finfo)) == FR_OK) {
    if(finfo.fname[0] == '\0' || finfo.fname[0] != '.') {
      res = fat_getvolumename(path->part, name);
      break;
    }
    if(finfo.fname[0] == '.' && finfo.fname[1] == '.' && finfo.fname[2] == 0) {
      if((res = l_opendir(&partition[path->part].fatfs,finfo.clust,&dh)) != FR_OK) // open .. dir.
        break;
      while ((res = f_readdir(&dh, &finfo)) == FR_OK) {
        if(finfo.fname[0] == '\0')
          break;
        if (finfo.clust == path->dir.fat) {
          if(!*name)
            name = finfo.fname;
          else
            asc2pet(name);
          break;
        }
      }
      break;
    }
  }

  if (*name)
    memcpy(label, name, ustrlen(name));

  if (res == FR_OK)
    return 0;

gl_error:
  parse_error(res,0);
  return 1;
}

/**
 * fat_getid - "Read" a disk id
 * @path: path object
 * @id  : pointer to the buffer for the id (5 characters)
 *
 * This function creates a disk ID from the FAT type (12/16/32)
 * and the usual " 2A" of a 1541 in the first 5 bytes of id.
 * Always returns 0 for success.
 */
uint8_t fat_getid(path_t *path, uint8_t *id) {
  switch (partition[path->part].fatfs.fs_type) {
  case FS_FAT12:
    *id++ = '1';
    *id++ = '2';
    break;

  case FS_FAT16:
    *id++ = '1';
    *id++ = '6';
    break;

  case FS_FAT32:
    *id++ = '3';
    *id++ = '2';
    break;
  }

  *id++ = ' ';
  *id++ = '2';
  *id++ = 'A';
  return 0;
}

/* Returns the number of free blocks */
uint16_t fat_freeblocks(uint8_t part) {
  FATFS *fs = &partition[part].fatfs;
  DWORD clusters;

  if (!(globalflags & FAT32_FREEBLOCKS) &&
      fs->fs_type == FS_FAT32)
    return 1;

  if (l_getfree(fs, NULLSTRING, &clusters, 65535) == FR_OK) {
    if (clusters > 65535)
      return 65535;
    else
      return clusters;
  } else
    return 0;
}

/* Dummy function for direct sector access */
/* FIXME: Read/Write a file "BOOT.BIN" in the currect directory */
/*        (e.g. for the C128 boot sector)                       */
void fat_sectordummy(buffer_t *buf, uint8_t part, uint8_t track, uint8_t sector) {
  set_error_ts(ERROR_READ_NOHEADER,track,sector);
}

/**
 * fat_rename - rename a file
 * @path   : path object
 * @dent   : pointer to cbmdirent with old file name
 * @newname: new file name
 *
 * This function renames the file in dent in the directory referenced by
 * path to newname.
 */
void fat_rename(path_t *path, cbmdirent_t *dent, uint8_t *newname) {
  uint8_t *ext;
  FRESULT res;
  UINT byteswritten;

  partition[path->part].fatfs.curr_dir = path->dir.fat;

  if (dent->opstype == OPSTYPE_FAT_X00) {
    /* [PSUR]00 rename, just change the internal file name */
    res = f_open(&partition[path->part].fatfs, &partition[path->part].imagehandle, dent->pvt.fat.realname, FA_WRITE|FA_OPEN_EXISTING);
    if (res != FR_OK) {
      parse_error(res,0);
      return;
    }

    res = f_lseek(&partition[path->part].imagehandle, P00_CBMNAME_OFFSET);
    if (res != FR_OK) {
      parse_error(res,0);
      return;
    }

    /* Copy the new name into dent->name so we can overwrite all 16 bytes */
    memset(dent->name, 0, CBM_NAME_LENGTH);
    ustrcpy(dent->name, newname);

    res = f_write(&partition[path->part].imagehandle, dent->name, CBM_NAME_LENGTH, &byteswritten);
    if (res != FR_OK || byteswritten != CBM_NAME_LENGTH) {
      parse_error(res,0);
      return;
    }

    res = f_close(&partition[path->part].imagehandle);
    if (res != FR_OK) {
      parse_error(res,0);
      return;
    }
  } else {
    switch (check_extension(dent->pvt.fat.realname, &ext)) {
    case EXT_IS_TYPE:
      /* Keep type extension */
      ustrcpy(entrybuf, newname);
      build_name(entrybuf, dent->typeflags & TYPE_MASK);
      res = f_rename(&partition[path->part].fatfs, dent->pvt.fat.realname, entrybuf);
      if (res != FR_OK)
        parse_error(res, 0);
      break;

    default:
      /* Normal rename */
      pet2asc(dent->name);
      pet2asc(newname);
      res = f_rename(&partition[path->part].fatfs, dent->name, newname);
      if (res != FR_OK)
        parse_error(res, 0);
      break;
    }
  }
}

/**
 * fatops_init - Initialize fatops module
 * @preserve_path: Preserve the current directory if non-zero
 *
 * This function will initialize the fatops module and force
 * mounting of the card. It can safely be called again if re-mounting
 * is required.
 */
void fatops_init(uint8_t preserve_path) {
  FRESULT res;
  uint8_t realdrive,drive,part;

  max_part = 0;
  drive = 0;
  part = 0;
  while (max_part < CONFIG_MAX_PARTITIONS && drive < MAX_DRIVES) {
    partition[max_part].fop = &fatops;

    /* Map drive numbers in just one place */
    realdrive = map_drive(drive);
    res=f_mount((realdrive * 16) + part, &partition[max_part].fatfs);

    if (!preserve_path)
      partition[max_part].current_dir.fat = 0;

    if (res == FR_OK)
      max_part++;

    if (res != FR_NOT_READY && res != FR_INVALID_OBJECT && part < 15 &&
        /* Don't try to mount partitions on an unpartitioned medium */
        !(res == FR_OK && part == 0))
      /* Try all partitions */
      part++;
    else {
      /* End of extended partition chain, try next drive */
      part = 0;
      drive++;
    }
  }

  if (!preserve_path) {
    current_part = 0;
    display_current_part(0);
    set_changelist(NULL, NULLSTRING);
  }

  /* Remove BAM buffer */
  free_buffer(bam_buffer);
  bam_buffer = NULL;

#ifndef HAVE_HOTPLUG
  if (!max_part) {
    set_error_ts(ERROR_DRIVE_NOT_READY,0,0);
    return;
  }
#endif
}

/**
 * image_unmount - generic unmounting function for images
 * @part: partition number
 *
 * This function will clear all buffers, close the image file and
 * restore file operations to fatops. It can be used for unmounting
 * any image file types that don't require special cleanups.
 * Returns 0 if successful, 1 otherwise.
 */
uint8_t image_unmount(uint8_t part) {
  FRESULT res;
  buffer_t *buf;

  free_multiple_buffers(FMB_USER_CLEAN);

  /* Free the BAM buffer if this was the last D64 */
  // FIXME: Move to d64ops.c/d64_unmount
  if (partition[part].fop == &d64ops) {
    buf = find_buffer(BUFFER_SYS_BAM);
    if (buf) {
      buf->cleanup(buf);
      if (--buf->pvt.bam.refcount) {
        /* Invalidate the BAM buffer contents */
        buf->pvt.bam.part = 255;
      } else {
        /* Last image unmounted */
        free_buffer(buf);
        bam_buffer = NULL;
      }
    }
  }

  if (display_found) {
    /* Send current path to display */
    path_t path;

    path.part    = part;
    path.dir.fat = partition[part].current_dir.fat;
    fat_getdirlabel(&path, entrybuf);
    display_current_directory(part,entrybuf);
  }

  partition[part].fop = &fatops;
  res = f_close(&partition[part].imagehandle);
  if (res != FR_OK) {
    parse_error(res,0);
    return 1;
  }
  return 0;
}

/**
 * image_chdir - generic chdir for image files
 * @path: path object of the location of dirname
 * @dent: directory to be changed into
 *
 * This function will ignore any names except _ (left arrow)
 * and unmount the image if that is found. It can be used as
 * chdir for all image types that don't support subdirectories
 * themselves. Returns 0 if successful, 1 otherwise.
 */
uint8_t image_chdir(path_t *path, cbmdirent_t *dent) {
  if (dent->name[0] == '_' && dent->name[1] == 0) {
    /* Unmount request */
    return image_unmount(path->part);
  }
  return 1;
}

/**
 * image_mkdir - generic mkdir for image files
 * @path   : path of the directory
 * @dirname: name of the directory to be created
 *
 * This function does nothing.
 */
void image_mkdir(path_t *path, uint8_t *dirname) {
  set_error(ERROR_SYNTAX_UNABLE);
  return;
}

/**
 * image_read - Seek to a specified image offset and read data
 * @part  : partition number
 * @offset: offset to be seeked to
 * @buffer: pointer to where the data should be read to
 * @bytes : number of bytes to read from the image file
 *
 * This function seeks to offset in the image file and reads bytes
 * byte into buffer. It returns 0 on success, 1 if less than
 * bytes byte could be read and 2 on failure.
 */
uint8_t image_read(uint8_t part, DWORD offset, void *buffer, uint16_t bytes) {
  FRESULT res;
  UINT bytesread;

  if (offset != -1) {
    res = f_lseek(&partition[part].imagehandle, offset);
    if (res != FR_OK) {
      parse_error(res,1);
      return 2;
    }
  }

  res = f_read(&partition[part].imagehandle, buffer, bytes, &bytesread);
  if (res != FR_OK) {
    parse_error(res,1);
    return 2;
  }

  if (bytesread != bytes)
    return 1;

  return 0;
}

/**
 * image_write - Seek to a specified image offset and write data
 * @part  : partition number
 * @offset: offset to be seeked to
 * @buffer: pointer to the data to be written
 * @bytes : number of bytes to read from the image file
 * @flush : Flags if written data should be flushed to disk immediately
 *
 * This function seeks to offset in the image file and writes bytes
 * byte into buffer. It returns 0 on success, 1 if less than
 * bytes byte could be written and 2 on failure.
 */
uint8_t image_write(uint8_t part, DWORD offset, void *buffer, uint16_t bytes, uint8_t flush) {
  FRESULT res;
  UINT byteswritten;

  if (offset != -1) {
    res = f_lseek(&partition[part].imagehandle, offset);
    if (res != FR_OK) {
      parse_error(res,0);
      return 2;
    }
  }

  res = f_write(&partition[part].imagehandle, buffer, bytes, &byteswritten);
  if (res != FR_OK) {
    parse_error(res,1);
    return 2;
  }

  if (byteswritten != bytes)
    return 1;

  if (flush)
    f_sync(&partition[part].imagehandle);

  return 0;
}

/* Dummy function for format */
void format_dummy(uint8_t drive, uint8_t *name, uint8_t *id) {
  set_error(ERROR_SYNTAX_UNKNOWN);
}

const PROGMEM fileops_t fatops = {  // These should be at bottom, to be consistent with d64ops and m2iops
  &fat_open_read,
  &fat_open_write,
  &fat_open_rel,
  &fat_delete,
  &fat_getvolumename,
  &fat_getdirlabel,
  &fat_getid,
  &fat_freeblocks,
  &fat_sectordummy,
  &fat_sectordummy,
  &format_dummy,
  &fat_opendir,
  &fat_readdir,
  &fat_mkdir,
  &fat_chdir,
  &fat_rename
};
