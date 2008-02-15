/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>
   ASCII/PET conversion Copyright (C) 2008 Jim Brain <brain@jbrain.com>

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
#include "buffers.h"
#include "d64ops.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fileops.h"
#include "m2iops.h"
#include "ff.h"
#include "uart.h"
#include "wrapops.h"
#include "fatops.h"

FATFS fatfs;

const PROGMEM fileops_t fatops = {
  &fat_open_read,
  &fat_open_write,
  &fat_delete,
  &fat_getlabel,
  &fat_getid,
  &fat_freeblocks,
  &fat_sectordummy,
  &fat_sectordummy,
  &fat_opendir,
  &fat_readdir,
  &fat_mkdir,
  &fat_chdir
};

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * parse_error - translates a tff FRESULT into a commodore error message
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
    // FIXME: Change tff to be more precise
    set_error_ts(ERROR_DISK_FULL,res,0);
    break;

  case FR_IS_READONLY:
  case FR_IS_DIRECTORY:
    set_error_ts(ERROR_FILE_EXISTS,res,0);
    break;

  default:
    set_error_ts(ERROR_SYNTAX_UNABLE,res,99);
    break;
  }
}

/**
 * asc2pet - convert string from ASCII to PETSCII
 * @buf - pointer to the string to be converted
 *
 * This function converts the string in the given buffer from ASCII to
 * PETSCII in-place.
 */
static void asc2pet(char *buf) {
  uint8_t ch;
  while (*buf) {
    ch = *(uint8_t *)buf;
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
 * @buf - pointer to the string to be converted
 *
 * This function converts the string in the given buffer from PETSCII to
 * ASCII in-place.
 */
static void pet2asc(char *buf) {
  uint8_t ch;
  while (*buf) {
    ch = *(uint8_t *)buf;
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

static char* build_name(char *path, char *name, buffer_t *buf) {
  char *str;

  if (path && strlen(path)) {
    str = (char *) buf->data;
    strcpy(str, path);
    strcat_P(str, PSTR("/"));
    strcat(str, name);
  } else
    str = name;

  pet2asc(str);
  return str;
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

  res = f_read(&buf->pvt.fh, buf->data+2, 254, &bytesread);
  if (res != FR_OK) {
    parse_error(res,1);
    free_buffer(buf);
    return 1;
  }

  /* The bus protocol can't handle 0-byte-files */
  if (bytesread == 0) {
    bytesread = 1;
    /* Experimental data suggests that this may be correct */
    buf->data[2] = 13;
  }

  buf->position = 2;
  buf->lastused = bytesread+1;
  if (bytesread < 254 ||
      (buf->pvt.fh.fsize - buf->pvt.fh.fptr) == 0)
    buf->sendeoi = 1;
  else
    buf->sendeoi = 0;

  return 0;
}

/**
 * fat_file_write - write the current buffer data
 * @buf: buffer to be worked on
 *
 * This function writes the current contents of the given buffer into its
 * associated file. Used as a refill-callback when writing files.
 */
static uint8_t fat_file_write(buffer_t *buf) {
  FRESULT res;
  UINT byteswritten;

  uart_putc('/');

  res = f_write(&buf->pvt.fh, buf->data, buf->lastused+1, &byteswritten);
  if (res != FR_OK) {
    uart_putc('r');
    parse_error(res,1);
    f_close(&buf->pvt.fh);
    free_buffer(buf);
    return 1;
  }

  if (byteswritten != buf->lastused+1) {
    uart_putc('l');
    set_error(ERROR_DISK_FULL);
    f_close(&buf->pvt.fh);
    free_buffer(buf);
    return 1;
  }

  buf->mustflush = 0;
  buf->position  = 0;
  buf->lastused  = 0;

  return 0;
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
    if (fat_file_write(buf))
      return 1;
  }

  res = f_close(&buf->pvt.fh);
  parse_error(res,1);

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
 * @path    : path of the file
 * @filename: name of the file
 * @buf     : buffer to be used
 *
 * This functions opens a file in the FAT filesystem for reading and sets up
 * buf to access it.
 */
void fat_open_read(char *path, char *filename, buffer_t *buf) {
  FRESULT res;

  filename = build_name(path, filename, buf);
  res = f_open(&buf->pvt.fh, filename, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    parse_error(res,1);
    free_buffer(buf);
    return;
  }

  buf->read      = 1;
  buf->write     = 0;
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_read;

  /* Call the refill once for the first block of data */
  buf->refill(buf);
}

/**
 * fat_open_write - opens a file for writing
 * @path    : path of the file
 * @filename: name of the file
 * @type    : type of the file
 * @buf     : buffer to be used
 * @append  : Flags if the new data should be appended to the end of file
 *
 * This function opens a file in the FAT filesystem for writing and sets up
 * buf to access it. type is ignored here because FAT has no equivalent of
 * file types.
 */
void fat_open_write(char *path, char *filename, uint8_t type, buffer_t *buf, uint8_t append) {
  FRESULT res;

  filename = build_name(path, filename, buf);
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

  active_buffers += 16;
  DIRTY_LED_ON();

  buf->mustflush = 0;
  buf->read      = 0;
  buf->write     = 1;
  buf->position  = 0;
  buf->lastused  = 0;
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_write;
}

/* ------------------------------------------------------------------------- */
/*  External interface for the various operations                            */
/* ------------------------------------------------------------------------- */

uint8_t fat_opendir(dh_t *dh, char *dir) {
  FRESULT res;

  res = f_opendir(&dh->fat, dir);
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
int8_t fat_readdir(dh_t *dh, struct cbmdirent *dent) {
  FRESULT res;
  FILINFO finfo;
  uint8_t *ptr;

  finfo.lfn = entrybuf;

  do {
    res = f_readdir(&dh->fat, &finfo);
    if (res != FR_OK) {
      if (res == FR_INVALID_OBJECT)
	set_error(ERROR_DIR_ERROR);
      else
	parse_error(res,1);
      return 1;
    }
  } while (finfo.fname[0] && (finfo.fattrib & AM_VOL));

  if (finfo.fname[0]) {
    if (finfo.fsize > 16255746)
      /* File too large -> size 63999 blocks */
      dent->blocksize = 63999;
    else
      dent->blocksize = (finfo.fsize+253) / 254;

    dent->remainder = finfo.fsize % 254;

    /* Copy name */
    memset(dent->name, 0, sizeof(dent->name));

    if (!finfo.lfn[0]) {
      ptr = finfo.lfn = (unsigned char *)finfo.fname;
      while (*ptr) {
	if (*ptr == '~') *ptr = 0xff;
	ptr++;
      }
    } else
      /* Convert only LFNs to PETSCII, 8.3 are always upper-case */
      asc2pet((char *)finfo.lfn);

    strcpy((char *)dent->name, (char *)finfo.lfn);

    /* Type+Flags */
    if (finfo.fattrib & AM_DIR) {
      dent->typeflags = TYPE_DIR;
      /* Hide directories starting with . */
      if (finfo.lfn[0] == '.')
	dent->typeflags |= FLAG_HIDDEN;
    } else
      dent->typeflags = TYPE_PRG;

    if (finfo.fattrib & AM_RDO)
      dent->typeflags |= FLAG_RO;

    if (finfo.fattrib & (AM_HID|AM_SYS))
      dent->typeflags |= FLAG_HIDDEN;

    return 0;
  } else
    return -1;
}

/**
 * fat_delete - Delete a file/directory on FAT
 * @path    : path to the file/directory
 * @filename: name of the file/directory to be deleted
 *
 * This function deletes the file filename in path and returns
 * 0 if not found, 1 if deleted or 255 if an error occured.
 */
uint8_t fat_delete(char *path, char *filename) {
  buffer_t *buf;
  FRESULT res;

  buf = alloc_buffer();
  if (!buf)
    return 255;

  DIRTY_LED_ON();
  filename = build_name(path, filename, buf);
  res = f_unlink(filename);
  /* free_buffer will turn off the LED for us */
  free_buffer(buf);

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
 * @dirname: Name of the directory/image to be changed into
 *
 * This function changes the current FAT directory to dirname.
 * If dirname specifies a file with a known extension (e.g. M2I or D64), the
 * current directory will be changed to the directory of the file and
 * it will be mounted as an image file.
 */
void fat_chdir(char *dirname) {
  FRESULT res;

  /* Left arrow moves one directory up */
  if (dirname[0] == '_' && dirname[1] == 0) {
    dirname[0] = '.';
    dirname[1] = '.';
    dirname[2] = 0;
  } else {
    pet2asc(dirname);
    if (dirname[strlen(dirname)-1] == '/')
      dirname[strlen(dirname)-1] = 0;
  }
  res = f_chdir(dirname);
  if (res == FR_NOT_DIRECTORY) {
    /* Changing into a file, could be a mount request */
    char *ext;
    char *fname = strrchr(dirname, '/');

    if (fname)
      *fname++ = 0;
    else
      fname = dirname;

    ext = strrchr(fname, '.');

    if (ext && (!strcmp_P(ext, PSTR(".m2i")) ||
		!strcmp_P(ext, PSTR(".d64")))) {
      /* D64/M2I mount request */
      if (fname != dirname) {
	res = f_chdir(dirname);
	if (res != FR_OK) {
	  parse_error(res,1);
	  return;
	}
      }

      free_all_buffers(1);
      /* Open image file */
      res = f_open(&imagehandle, fname, FA_OPEN_EXISTING|FA_READ|FA_WRITE);
      if (res != FR_OK) {
	parse_error(res,1);
	return;
      }

      if (!strcmp_P(ext, PSTR(".m2i")))
	fop = &m2iops;
      else
	fop = &d64ops;

      return;
    }
  }
  parse_error(res,1);
}

/* Create a new directory */
void fat_mkdir(char *dirname) {
  FRESULT res;

  pet2asc(dirname);
  res = f_mkdir(dirname);
  parse_error(res,0);
}

/**
 * fat_getlabel - Get the volume label
 * @label: pointer to the buffer for the label (16 characters)
 *
 * This function reads the FAT volume label and stores it space-padded
 * in the first 16 bytes of label. Returns 0 if successfull, != 0 if
 * an error occured.
 */
uint8_t fat_getlabel(char *label) {
  DIR dh;
  FILINFO finfo;
  FRESULT res;
  uint8_t i,j;
  char rootdir[2];

  memset(label, ' ', 16);

  rootdir[0] = '/';
  rootdir[1] = 0;
  res = f_opendir(&dh, rootdir);

  if (res != FR_OK) {
    parse_error(res,0);
    return 1;
  }

  finfo.lfn = NULL;

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

/**
 * fat_getid - Create a disk id
 * @id: pointer to the buffer for the id (5 characters)
 *
 * This function creates a disk ID from the FAT type (12/16/32)
 * and the usual " 2A" of a 1541 in the first 5 bytes of id.
 * Always returns 0 for success.
 */
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

  if (f_getfree(NULLSTRING, &clusters, &fs) == FR_OK) {
    if (clusters < 64000)
      return clusters;
    else
      return 63999;
  } else
    return 0;
}

/* Dummy function for direct sector access */
/* FIXME: Read/Write a file "BOOT.BIN" in the currect directory */
/*        (e.g. for the C128 boot sector)                       */
void fat_sectordummy(buffer_t *buf, uint8_t track, uint8_t sector) {
  set_error_ts(ERROR_READ_NOHEADER,track,sector);
}

/**
 * init_fatops - Initialize fatops module
 *
 * This function will initialize the fatops module and force
 * mounting of the card. It can safely be called again if re-mounting
 * is required.
 */
void init_fatops(void) {
  fop = &fatops;
  f_mount(0, &fatfs);

  /* Dummy operation to force the actual mounting */
  f_chdir(NULLSTRING);
}

/**
 * image_unmount - generic unmounting function for images
 *
 * This function will clear all buffers, close the image file and
 * restore file operations to fatops. It can be used for unmounting
 * any image file types that don't require special cleanups.
 */
void image_unmount(void) {
  FRESULT res;

  free_all_buffers(1);
  fop = &fatops;
  res = f_close(&imagehandle);
  if (res != FR_OK)
    parse_error(res,0);
}

/**
 * image_chdir - generic chdir for image files
 * @dirname: directory to be changed into
 *
 * This function will ignore any dirnames except _ (left arrow)
 * and unmount the image if that is found. It can be used as
 * chdir/mkdir for all image types that don't support subdirectories
 * themselves.
 */
void image_chdir(char *dirname) {
  if (dirname[0] == '_' && dirname[1] == 0) {
    /* Unmount request */
    image_unmount();
  }
  return;
}

/**
 * image_read - Seek to a specified image offset and read data
 * @offset: offset to be seeked to
 * @buffer: pointer to where the data should be read to
 * @bytes : number of bytes to read from the image file
 *
 * This function seeks to offset in the image file and reads bytes
 * byte into buffer. It returns 0 on success, 1 if less than
 * bytes byte could be read and 2 on failure.
 */
uint8_t image_read(DWORD offset, void *buffer, uint16_t bytes) {
  FRESULT res;
  UINT bytesread;

  if (offset != -1) {
    res = f_lseek(&imagehandle, offset);
    if (res != FR_OK) {
      parse_error(res,1);
      return 2;
    }
  }

  res = f_read(&imagehandle, buffer, bytes, &bytesread);
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
 * @offset: offset to be seeked to
 * @buffer: pointer to the data to be written
 * @bytes : number of bytes to read from the image file
 * @flush : Flags if written data should be flushed to disk immediately
 *
 * This function seeks to offset in the image file and writes bytes
 * byte into buffer. It returns 0 on success, 1 if less than
 * bytes byte could be written and 2 on failure.
 */
uint8_t image_write(DWORD offset, void *buffer, uint16_t bytes, uint8_t flush) {
  FRESULT res;
  UINT byteswritten;

  if (offset != -1) {
    res = f_lseek(&imagehandle, offset);
    if (res != FR_OK) {
      parse_error(res,0);
      return 2;
    }
  }

  res = f_write(&imagehandle, buffer, bytes, &byteswritten);
  if (res != FR_OK) {
    parse_error(res,1);
    return 2;
  }

  if (byteswritten != bytes)
    return 1;

  if (flush)
    f_sync(&imagehandle);

  return 0;
}
