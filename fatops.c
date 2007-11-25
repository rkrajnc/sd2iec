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

   
   fatops.c: FAT operations

*/

#include <avr/pgmspace.h>
#include <string.h>
#include <stdint.h>
#include "config.h"
#include "tff.h"
#include "buffers.h"
#include "doscmd.h"
#include "errormsg.h"
#include "uart.h"
#include "tff.h"
#include "fileops.h"
#include "fatops.h"

FATFS fatfs;

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/* Translate a tff FRESULT into a commodore error message */
static void parse_error(FRESULT res, uint8_t readflag) {
  switch (res) {
  case FR_OK:
    set_error(ERROR_OK,0,0);
    break;

  case FR_NO_FILE:
    set_error(ERROR_FILE_NOT_FOUND,res,0);
    break;
    
  case FR_NO_PATH:
    set_error(ERROR_FILE_NOT_FOUND_39,res,0);
    break;

  case FR_INVALID_NAME:
    set_error(ERROR_SYNTAX_JOKER,res,0);
    break;
    
  case FR_NOT_READY:
  case FR_INVALID_DRIVE:
  case FR_NOT_ENABLED:
  case FR_NO_FILESYSTEM:
    set_error(ERROR_DRIVE_NOT_READY,res,0);
    break;
    
  case FR_RW_ERROR:
    /* Just a random READ ERROR */
    if (readflag)
      set_error(ERROR_READ_NOHEADER,res,0);
    else
      set_error(ERROR_WRITE_VERIFY,res,0);
    break;
    
  case FR_WRITE_PROTECTED:
    set_error(ERROR_WRITE_PROTECT,res,0);
    break;
    
  case FR_EXIST:
    set_error(ERROR_FILE_EXISTS,res,0);
    break;

  case FR_DIR_NOT_EMPTY:
    // FIXME: What do the CMD drives return when removing a non-empty directory?
    set_error(ERROR_FILE_EXISTS,res,0);
    break;
    
  case FR_DENIED:
    // FIXME: Change tff to be more precise
    set_error(ERROR_DISK_FULL,res,0);
    break;

  case FR_IS_READONLY:
  case FR_IS_DIRECTORY:
    set_error(ERROR_FILE_EXISTS,res,0);
    break;
    
  default:
    set_error(ERROR_SYNTAX_UNABLE,res,99);
    break;
  }
}

static void fix_name(char *str) {
  /* The C64 displays ~ as pi, but sends pi as 0xff */
  while (*str) {
    if (*str == 0xff)
      *str = '~';
    str++;
  }
}

/* ------------------------------------------------------------------------- */
/*  Callbacks                                                                */
/* ------------------------------------------------------------------------- */

/* Read the next data block from a file into the buffer */
static uint8_t fat_file_read(buffer_t *buf) {
  FRESULT res;
  UINT bytesread;

  uart_putc('#');

  res = f_read(&buf->pvt.fh, buf->data, 254, &bytesread);
  if (res != FR_OK) {
    parse_error(res,1);
    free_buffer(buf);
    return 1;
  }

  /* The bus protocol can't handle 0-byte-files */
  if (bytesread == 0) {
    bytesread = 1;
    /* Experimental data suggests that this may be correct */
    buf->data[0] = 13;
  }

  buf->position = 0;
  buf->length = bytesread-1;
  if (bytesread < 254 ||
      (buf->pvt.fh.fsize - buf->pvt.fh.fptr) == 0)
    buf->sendeoi = 1;
  else
    buf->sendeoi = 0;

  return 0;
}

/* Writes the data block to a file */
static uint8_t fat_file_write(buffer_t *buf) {
  FRESULT res;
  UINT byteswritten;

  uart_putc('/');

  res = f_write(&buf->pvt.fh, buf->data, buf->length+1, &byteswritten);
  if (res != FR_OK) {
    uart_putc('r');
    parse_error(res,1);
    f_close(&buf->pvt.fh);
    free_buffer(buf);
    return 1;
  }

  if (byteswritten != buf->length+1) {
    uart_putc('l');
    set_error(ERROR_DISK_FULL,0,0);
    f_close(&buf->pvt.fh);
    free_buffer(buf);
    return 1;
  }

  buf->dirty    = 0;
  buf->position = 0;
  buf->length   = -1;

  return 0;
}

/* Close the file corresponding to the buffer, writing remaining data if any */
static uint8_t fat_file_close(buffer_t *buf) {
  FRESULT res;
  
  if (!buf->allocated) return 0;

  if (buf->write && buf->dirty) {
    /* Write the remaining data using the callback */
    DIRTY_LED_OFF(); // FIXME: Mehr als eine Schreibdatei offen?
    if (fat_file_write(buf))
      return 1;
  }

  res = f_close(&buf->pvt.fh);
  free_buffer(buf);
  parse_error(res,1);

  if (res != FR_OK)
    return 1;
  else
    return 0;
}

/* ------------------------------------------------------------------------- */
/*  Internal handlers for the various operations                             */
/* ------------------------------------------------------------------------- */

/* Open a file for reading */
void fat_open_read(char *filename, buffer_t *buf) {
  FRESULT res;

  fix_name(filename);
  res = f_open(&buf->pvt.fh, filename, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    parse_error(res,1);
    free_buffer(buf);
    return;
  }

  buf->read      = 1;
  buf->write     = 0;
  buf->position  = 0;
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_read;
  
  /* Call the refill once for the first block of data */
  buf->refill(buf);
}

/* Open a file for writing */
void fat_open_write(char *filename, buffer_t *buf, uint8_t append) {
  FRESULT res;

  fix_name(filename);
  if (append) {
    res = f_open(&buf->pvt.fh, filename, FA_WRITE | FA_OPEN_EXISTING);
    if (res == FR_OK)
      res = f_lseek(&buf->pvt.fh, buf->pvt.fh.fsize);
  } else
    res = f_open(&buf->pvt.fh, filename, FA_WRITE | FA_CREATE_NEW);

  if (res != FR_OK) {
    parse_error(res,0);
    free_buffer(buf);
    return;
  }

  DIRTY_LED_ON();

  buf->dirty     = 0;
  buf->read      = 0;
  buf->write     = 1;
  buf->position  = 0;
  buf->length    = -1;
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_write;
}

/* ------------------------------------------------------------------------- */
/*  External interface for the various operations                            */
/* ------------------------------------------------------------------------- */

uint8_t fat_opendir(buffer_t *buf, char *dir) {
  FRESULT res;

  res = f_opendir(&buf->pvt.dir.dh, dir);
  if (res != FR_OK) {
    parse_error(res,1);
    return 1;
  }
  return 0;
}

/* readdir wrapper for FAT files                       */
/* Returns 1 on error, 0 if successful, -1 if finished */
int8_t fat_readdir(buffer_t *buf, struct cbmdirent *dent) {
  FRESULT res;
  FILINFO finfo;
  uint8_t i;

  do {
    res = f_readdir(&buf->pvt.dir.dh, &finfo);
    if (res != FR_OK) {
      if (res == FR_INVALID_OBJECT)
	set_error(ERROR_DIR_ERROR,0,0);
      else
	parse_error(res,1);
      return 1;
    }
  } while (finfo.fname[0] && (finfo.fattrib & (AM_VOL|AM_SYS|AM_HID)));

  if (finfo.fname[0]) {
    if (finfo.fsize > 16255746)
      /* File too large -> size 63999 blocks */
      dent->blocksize = 63999;
    else
      dent->blocksize = (finfo.fsize+253) / 254;

    /* Copy name */
    memset(dent->name, 0xa0, CBM_NAME_LENGTH);
    for (i=0; finfo.fname[i]; i++)
      dent->name[i] = finfo.fname[i];

    /* Type+Flags */
    if (finfo.fattrib & AM_DIR)
      dent->typeflags = TYPE_DIR;
    else
      dent->typeflags = TYPE_PRG;

    if (finfo.fattrib & AM_RDO)
      dent->typeflags |= FLAG_RO;

    return 0;
  } else
    return -1;
}

/* Delete a file/directory                         */
/* Returns number of files deleted or 255 on error */
uint8_t fat_delete(char *filename) {
  FRESULT res;

  fix_name(filename);
  res = f_unlink((char *)filename);
  parse_error(res,0);
  if (res == FR_OK)
    return 1;
  else if (res == FR_NO_FILE)
    return 0;
  else
    return 255;
}

/* Change the current directory */
void fat_chdir(char *dirname) {
  FRESULT res;

  fix_name(dirname);
  res = f_chdir(dirname);
  parse_error(res,0);
}

/* Create a new directory */
void fat_mkdir(char *dirname) {
  FRESULT res;

  fix_name(dirname);
  res = f_mkdir(dirname);
  parse_error(res,0);
}

/* Get the volume label                                               */
/* Returns 0 if successful.                                           */
/* Will write a space-padded, 16 char long unterminated name to label */
uint8_t fat_getlabel(char *label) {
  DIR dh;
  FILINFO finfo;
  CLUST olddir;
  FRESULT res;
  uint8_t i,j;

  memset(label, ' ', 16);

  /* Trade off a bit of flash and stack usage instead of */
  /* permanently wasting two bytes for "/".              */
  olddir = current_dir;
  current_dir = 0;

  res = f_opendir(&dh, "");
  current_dir = olddir;

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
    }
  }

  if (res != FR_OK) {
    parse_error(res,0);
    return 1;
  } else
    return 0;
}

/* Get the volume id (all 5 characters)                         */
/* Returns 0 if successful.                                     */
/* Will write a space-padded, 5 char long unterminated id to id */
uint8_t fat_getid(char *id) {
  switch (fatfs.fs_type) {
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
uint16_t fat_freeblocks(void) {
  FATFS *fs = &fatfs;
  DWORD clusters;

  if (f_getfree("", &clusters, &fs) == FR_OK) {
    if (clusters < 64000)
      return clusters;
    else
      return 63999;
  } else
    return 0;
}

/* Mount the card */
void init_fatops(void) {
  f_mount(0, &fatfs);

  /* Dummy operation to force the actual mounting */
  f_chdir("");
}
