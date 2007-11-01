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

FATFS fatfs;
char volumelabel[12];

/* ------------------------------------------------------------------------- */
/*  Some constants used for directory generation                             */
/* ------------------------------------------------------------------------- */

static const PROGMEM uint8_t dirheader[] = {
  1, 4, /* BASIC start address */
  1, 1, /* next line pointer */
  0, 0,    /* line number 0 */
  0x12, 0x22, /* Reverse on, quote */
  ' ',' ',' ',' ',' ',' ',' ',' ', /* 16 spaces as the disk name */
  ' ',' ',' ',' ',' ',' ',' ',' ', /* will be overwritten if needed */
  0x22,0x20, /* quote, space */
  'I','K',' ','2','A', /* id IK, shift-space, dosmarker 2A */
  00 /* line-end marker */
};

// FIXME: Evtl. nach fileops.c verschieben
static const PROGMEM uint8_t dirfooter[] = {
  1, 1, /* next line pointer */
  0, 0, /* number of free blocks (to be filled later */
  'B','L','O','C','K','S',' ','F','R','E','E','.',
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, /* Filler and end marker */
  0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00
};

// FIXME: Sollte in fileops.c stehen damit d64ops darauf zugreifen kann.
#define TYPE_LENGTH 3
#define TYPE_DEL 0
#define TYPE_SEQ 1
#define TYPE_PRG 2
#define TYPE_USR 3
#define TYPE_REL 4
#define TYPE_CBM 5
#define TYPE_DIR 6

static const PROGMEM uint8_t filetypes[] = {
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

/* Add a single directory entry in 15x1 format to the end of buf */
static void addentry(FILINFO *finfo, buffer_t *buf) {
  uint8_t i;
  uint8_t *data;

  data = buf->data + buf->length;

  /* Next line pointer, 1571-compatible =) */
  *data++ = 1;
  *data++ = 1;
  
  if (finfo->fsize > 16255746) {
    /* File too large -> size 63999 blocks */
    *data++ = 255;
    *data++ = 249;
  } else {
    /* File size in 254-byte-blocks as line number*/
    finfo->fsize += 253; /* Force ceil() */
    *data++ = (finfo->fsize/254) & 0xff;
    *data++ = (finfo->fsize/254) >> 8;
  }
  
  /* Filler before file name */
  if (finfo->fsize < 254*1000L)
    *data++ = ' ';
  if (finfo->fsize < 254*100)
    *data++ = ' ';
  if (finfo->fsize < 254*10)
    *data++ = ' ';
  *data++ = '"';

  /* copy the filename */
  memcpy(data, finfo->fname, strlen(finfo->fname));
  data += strlen(finfo->fname);
  
  /* Filler after the name */
  *data++ = '"';
  for (i=strlen(finfo->fname);i<17;i++) /* One additional space */
    *data++ = ' ';

  /* File type (fixed to PRG for now) */
  if (finfo->fattrib & AM_DIR) {
    memcpy_P(data, filetypes + TYPE_LENGTH * TYPE_DIR, TYPE_LENGTH);
  } else {
    memcpy_P(data, filetypes + TYPE_LENGTH * TYPE_PRG, TYPE_LENGTH);
  }
  data += 3;

  /* Filler at end of line */
  if (finfo->fattrib & AM_RDO)
    *data++ = '<';
  else
    *data++ = ' ';

  *data++ = ' ';

  /* Add the spaces we left out before */
  if (finfo->fsize >= 254*10)
    *data++ = ' ';
  if (finfo->fsize >= 254*100)
    *data++ = ' ';
  if (finfo->fsize >= 256*1000L)
    *data++ = ' ';

  /* Line end marker */
  *data++ = 0;

  buf->length += 32;
}

/* ------------------------------------------------------------------------- */
/*  Callbacks                                                                */
/* ------------------------------------------------------------------------- */

/* Generic cleanup-callback that just frees the buffer */
static uint8_t fat_generic_cleanup(buffer_t *buf) {
  free_buffer(buf);
  buf->cleanup = NULL;
  return 0;
}

/* Generate the final directory buffer with the BLOCKS FREE message */
static uint8_t fat_dir_footer(buffer_t *buf) {
  DWORD clusters;
  FATFS *fs;

  fs = &fatfs;

  /* Copy the "BLOCKS FREE" message */
  memcpy_P(buf->data, dirfooter, sizeof(dirfooter));

  if (f_getfree("", &clusters, &fs) == FR_OK) {
    if (clusters < 64000) {
      /* Cheat here: Report free clusters instead of CBM blocks */
      buf->data[2] = clusters & 0xff;
      buf->data[3] = clusters >> 8;
    } else {
      /* 63999 Blocks free... */
      buf->data[2] = 255;
      buf->data[3] = 249;
    }
  }

  buf->position = 0;
  buf->length   = 31;
  buf->sendeoi  = 1;

  return 0;
}

/* Fill the buffer with up to 8 directory entries */
static uint8_t fat_dir_refill(buffer_t *buf) {
  uint8_t i;
  FILINFO finfo;
  FRESULT res;

  uart_putc('+');

  buf->position = 0;
  buf->length   = 0;

  /* Add up to eight directory entries */
  for (i = 0; i < 8; i++) {
    do {
      res = f_readdir(&buf->pvt.dh, &finfo);
      if (res != FR_OK) {
	if (res == FR_INVALID_OBJECT)
	  set_error(ERROR_DIR_ERROR,0,0);
	else
	  parse_error(res,1);
	free_buffer(buf);
	return 1;
      }
    } while (finfo.fname[0] && (finfo.fattrib & (AM_VOL|AM_SYS|AM_HID)));
    
    if (finfo.fname[0])
      addentry(&finfo,buf);
    else
      /* Last entry, exit */
      break;
  }

  /* Fix the buffer length */
  buf->length--;

  if (!finfo.fname[0])
    buf->refill = fat_dir_footer;

  return 0;
}

/* Read the next data block from a file into the buffer */
static uint8_t fat_file_read(buffer_t *buf) {
  FRESULT res;
  UINT bytesread;

  uart_putc('#');

  res = f_read(&buf->pvt.fh, buf->data, 256, &bytesread);
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
  if (bytesread < 256 ||
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

  if (buf->write) {
    /* Write the remaining data using the callback */
    DIRTY_LED_OFF(); // FIXME: Mehr als eine Schreibdatei offen?
    if (fat_file_write(buf))
      return 1;
  }

  res = f_close(&buf->pvt.fh);
  free_buffer(buf);
  if (res != FR_OK) {
    parse_error(res,1);
    return 1;
  }
  set_error(ERROR_OK,0,0);
  return 0;
}

/* ------------------------------------------------------------------------- */
/*  Internal handlers for the various operations                             */
/* ------------------------------------------------------------------------- */

/* Prepare for directory reading and create the header */
static void fat_load_directory(uint8_t secondary) {
  buffer_t *buf;
  uint8_t i;
  FRESULT res;

  buf = alloc_buffer();
  if (buf == 0) {
    set_error(ERROR_NO_CHANNEL,0,0);
    return;
  }

  res = f_opendir(&buf->pvt.dh, "");
  if (res != FR_OK) {
    parse_error(res,1);
    free_buffer(buf);
    return;
  }

  buf->secondary = secondary;
  buf->read      = 1;
  buf->write     = 0;
  buf->cleanup   = fat_generic_cleanup;
  buf->position  = 0;
  buf->length    = 31;
  buf->sendeoi   = 0;

  /* copy static header to start of buffer */
  memcpy_P(buf->data, dirheader, sizeof(dirheader));

  /* Put FAT type into ID field */
  if (fatfs.fs_type == FS_FAT12) {
    buf->data[26] = '1';
    buf->data[27] = '2';
  } else if (fatfs.fs_type == FS_FAT16) {
    buf->data[26] = '1';
    buf->data[27] = '6';
  } else if (fatfs.fs_type == FS_FAT32) {
    buf->data[26] = '3';
    buf->data[27] = '2';
  }

  /* copy volume name */
  memcpy(buf->data+8, volumelabel, 12);

  /* Let the refill callback handly everything else */
  buf->refill = fat_dir_refill;

  return;
}

/* Open a file for reading */
static void fat_open_read(uint8_t secondary, buffer_t *buf) {
  FRESULT res;

  res = f_open(&buf->pvt.fh, (char *) command_buffer, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    parse_error(res,1);
    free_buffer(buf);
    return;
  }

  buf->secondary = secondary;
  buf->read      = 1;
  buf->write     = 0;
  buf->position  = 0;
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_read;
  
  /* Call the refill once for the first block of data */
  buf->refill(buf);
}

/* Open a file for writing */
static void fat_open_write(uint8_t secondary, buffer_t *buf) {
  FRESULT res;

  res = f_open(&buf->pvt.fh, (char *) command_buffer, FA_WRITE | FA_OPEN_ALWAYS);
  if (res != FR_OK) {
    parse_error(res,0);
    free_buffer(buf);
    return;
  }

  DIRTY_LED_ON();

  buf->secondary = secondary;
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

/* Open something */
void fat_open(uint8_t secondary) {
  buffer_t *buf;

  /* Empty name? */
  if (command_length == 0) {
    set_error(ERROR_SYNTAX_NONAME,0,0);
    return;
  }

  /* Load directory? */
  if (command_buffer[0] == '$') {
    /* FIXME: Secondary != 0? Compare D7F7-D7FF */
    fat_load_directory(secondary);
    return;
  }

  buf = alloc_buffer();
  if (buf == NULL) {
    set_error(ERROR_NO_CHANNEL,0,0);
    return;
  }

  // FIXME: Unterverzeichnissupport - evtl. Filenamen hinter current_path kopieren
  command_buffer[command_length] = 0;

  // FIXME: Parse filename for type+operation suffixes
  if (secondary == 0) {
    fat_open_read(secondary, buf);
  } else {
    fat_open_write(secondary, buf);
  }
}

/* Delete a file/directory                         */
/* Returns number of files deleted or 255 on error */
uint8_t fat_delete(uint8_t *filename) {
  FRESULT res;

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
void fat_chdir(uint8_t *dirname) {
  FRESULT res;

  res = f_chdir((char *) dirname);
  parse_error(res,0);
}

/* Create a new directory */
void fat_mkdir(uint8_t *dirname) {
  FRESULT res;

  res = f_mkdir((char *) dirname);
  parse_error(res,0);
}

/* Mount the card and read its label (if any) */
void init_fatops(void) {
  DIR dh;
  FILINFO finfo;
  uint8_t i;

  memset(volumelabel, ' ', sizeof(volumelabel));

  f_mount(0, &fatfs);

  /* Read volume label */
  if (f_opendir(&dh, "") == FR_OK) {
    while (f_readdir(&dh, &finfo) == FR_OK) {
      if (!finfo.fname[0]) break;
      if ((finfo.fattrib & (AM_VOL|AM_SYS|AM_HID)) == AM_VOL) {
	i=0;
	while (finfo.fname[i]) {
	  volumelabel[i] = finfo.fname[i];
	  i++;
	}
      }
    }
  }

  /* Remove dot from volume label. Assumes a single dot. */
  for (i=0; i<sizeof(volumelabel); i++) {
    if (volumelabel[i] == '.') {
      while (i < sizeof(volumelabel)-1) {
	volumelabel[i] = volumelabel[i+1];
	i++;
      }
      volumelabel[sizeof(volumelabel)-1] = ' ';
    }
  }
}
