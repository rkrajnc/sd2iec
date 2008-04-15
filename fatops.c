/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>
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
#include "doscmd.h"
#include "errormsg.h"
#include "ff.h"
#include "fileops.h"
#include "m2iops.h"
#include "parser.h"
#include "uart.h"
#include "ustring.h"
#include "wrapops.h"
#include "fatops.h"

#define P00_HEADER_SIZE    26
#define P00_CBMNAME_OFFSET 8
static const PROGMEM char p00marker[] = "C64File";

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
 * @path: path of the file
 * @dent: pointer to cbmdirent with name of the file
 * @buf : buffer to be used
 *
 * This functions opens a file in the FAT filesystem for reading and sets up
 * buf to access it.
 */
void fat_open_read(path_t *path, struct cbmdirent *dent, buffer_t *buf) {
  FRESULT res;
  uint8_t *name;

  pet2asc(dent->name);
  if (dent->realname[0])
    name = dent->realname;
  else
    name = dent->name;

  partition[path->part].fatfs.curr_dir = path->fat;
  res = f_open(&partition[path->part].fatfs,&buf->pvt.fh, name, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    parse_error(res,1);
    free_buffer(buf);
    return;
  }

  if (dent->realname[0]) {
    /* It's a [PSUR]00 file, skip the header */
    /* If anything goes wrong here, refill will notice too */
    f_lseek(&buf->pvt.fh, P00_HEADER_SIZE);
  }

  buf->read      = 1;
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_read;

  /* Call the refill once for the first block of data */
  buf->refill(buf);
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
void fat_open_write(path_t *path, struct cbmdirent *dent, uint8_t type, buffer_t *buf, uint8_t append) {
  FRESULT res;
  uint8_t *name, *x00ext;

  x00ext = NULL;

  if (dent->realname[0])
    name = dent->realname;
  else {
    ustrcpy(entrybuf, dent->name);
    pet2asc(entrybuf);
    if (type != TYPE_RAW && file_extension_mode != 0 && (
         (file_extension_mode == 1 && type != TYPE_PRG) ||
         (file_extension_mode == 2)
        )) {
      /* Append .[PSUR]00 suffix to the file name */
      name = entrybuf;
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
    }
    name = entrybuf;
  }

  partition[path->part].fatfs.curr_dir = path->fat;
  if (append) {
    res = f_open(&partition[path->part].fatfs, &buf->pvt.fh, name, FA_WRITE | FA_OPEN_EXISTING);
    if (res == FR_OK)
      res = f_lseek(&buf->pvt.fh, buf->pvt.fh.fsize);
  } else {
    do {
      res = f_open(&partition[path->part].fatfs, &buf->pvt.fh, name, FA_WRITE | FA_CREATE_NEW);
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
  }

  if (res != FR_OK) {
    parse_error(res,0);
    free_buffer(buf);
    return;
  }

  if (!append && x00ext != NULL) {
    /* Write a [PSUR]00 header */
    UINT byteswritten;

    memset(entrybuf, 0, P00_HEADER_SIZE);
    ustrcpy_P(entrybuf, p00marker);
    memcpy(entrybuf+P00_CBMNAME_OFFSET, dent->name, CBM_NAME_LENGTH);
    res = f_write(&buf->pvt.fh, entrybuf, P00_HEADER_SIZE, &byteswritten);
    if (res != FR_OK || byteswritten != P00_HEADER_SIZE) {
      parse_error(res,0);
      free_buffer(buf);
      return;
    }
  }

  mark_write_buffer(buf);
  buf->cleanup   = fat_file_close;
  buf->refill    = fat_file_write;
}

/* ------------------------------------------------------------------------- */
/*  External interface for the various operations                            */
/* ------------------------------------------------------------------------- */

uint8_t fat_opendir(dh_t *dh, path_t *path) {
  FRESULT res;

  res = l_opendir(&partition[path->part].fatfs, path->fat, &dh->dir.fat);
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
int8_t fat_readdir(dh_t *dh, struct cbmdirent *dent) {
  FRESULT res;
  FILINFO finfo;
  uint8_t *ptr;
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
  } while (finfo.fname[0] && (finfo.fattrib & AM_VOL));

  if (finfo.fname[0]) {
    /* Copy name */
    memset(dent->name, 0, sizeof(dent->name));
    memset(dent->realname, 0, sizeof(dent->realname));

    if (!finfo.lfn[0] || ustrlen(finfo.lfn) > CBM_NAME_LENGTH) {
      ustrcpy(dent->name, finfo.fname);

      ptr = dent->name;
      while (*ptr) {
        if (*ptr == '~') *ptr = 0xff;
        ptr++;
      }
    } else {
      /* Convert only LFNs to PETSCII, 8.3 are always upper-case */
      ustrcpy(dent->name, finfo.lfn);
      asc2pet(dent->name);
    }

    /* File type */
    if (finfo.fattrib & AM_DIR) {
      dent->typeflags = TYPE_DIR;
      /* Hide directories starting with . */
      if (dent->name[0] == '.')
        dent->typeflags |= FLAG_HIDDEN;
    } else
      dent->typeflags = TYPE_PRG;

    /* Search for the file extension */
    ptr = ustrrchr(finfo.fname, '.');
    if (ptr++ != NULL) {
      typechar = *ptr++;

      if (!(finfo.fattrib & AM_DIR) &&
          (typechar == 'P' || typechar == 'S' ||
           typechar == 'U' || typechar == 'R') &&
          isdigit(*ptr++) && isdigit(*ptr++)) {
        /* [PSRU]00 file - try to read the internal name */
        UINT bytesread;

        res = l_opencluster(&partition[dh->part].fatfs, &partition[dh->part].imagehandle, finfo.clust);
        if (res != FR_OK)
          goto notp00;

        res = f_read(&partition[dh->part].imagehandle, entrybuf, P00_HEADER_SIZE, &bytesread);
        if (res != FR_OK)
          goto notp00;

        if (ustrcmp_P(entrybuf, p00marker))
          goto notp00;

        /* Copy the internal name */
        memset(dent->name, 0, sizeof(dent->name));
        ustrcpy(dent->name, entrybuf+P00_CBMNAME_OFFSET);

        /* Remember the real file name */
        ustrcpy(dent->realname, finfo.fname);

        /* Some programs pad the name with 0xa0 instead of 0 */
        ptr = dent->name;
        for (uint8_t i=0;i<16;i++,ptr++)
          if (*ptr == 0xa0)
            *ptr = 0;

        finfo.fsize -= P00_HEADER_SIZE;

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
    }

  notp00:

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
    dent->fatcluster = finfo.clust;

    return 0;
  } else
    return -1;
}

/**
 * fat_delete - Delete a file/directory on FAT
 * @path: path to the file/directory
 * @dent: pointer to cbmdirent with name of the file/directory to be deleted
 *
 * This function deletes the file filename in path and returns
 * 0 if not found, 1 if deleted or 255 if an error occured.
 */
uint8_t fat_delete(path_t *path, struct cbmdirent *dent) {
  FRESULT res;
  uint8_t *name;

  DIRTY_LED_ON();
  if (dent->realname[0]) {
    name = dent->realname;
  } else {
    name = dent->name;
    pet2asc(name);
  }
  partition[path->part].fatfs.curr_dir = path->fat;
  res = f_unlink(&partition[path->part].fatfs, name);

  if (check_write_buf_count())
    DIRTY_LED_OFF();

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
 * @path   : path object for the location of dirname
 * @dirname: Name of the directory/image to be changed into
 *
 * This function changes the current FAT directory to dirname.
 * If dirname specifies a file with a known extension (e.g. M2I or D64), the
 * current directory will be changed to the directory of the file and
 * it will be mounted as an image file. Returns 0 if successful,
 * 1 otherwise.
 */
uint8_t fat_chdir(path_t *path, uint8_t *dirname) {
  FRESULT res;
  FILINFO finfo;

  partition[path->part].fatfs.curr_dir = path->fat;

  /* Left arrow moves one directory up */
  if (dirname[0] == '_' && dirname[1] == 0) {
    command_buffer[0] = '.';
    command_buffer[1] = '.';
    command_buffer[2] = 0;
    dirname = command_buffer;
  }

  pet2asc(dirname);
  res = f_stat(&partition[path->part].fatfs, dirname, &finfo);
  if (res != FR_OK) {
    parse_error(res,1);
    return 1;
  }

  if (finfo.fattrib & AM_DIR) {
    /* It's a directory, change to its cluster */
    partition[path->part].current_dir= finfo.clust;
  } else {
    /* Changing into a file, could be a mount request */
    uint8_t *ext = ustrrchr(dirname, '.');

    if (ext && (!ustrcasecmp_P(ext, PSTR(".m2i")) ||
                !ustrcasecmp_P(ext, PSTR(".d64")) ||
                !ustrcasecmp_P(ext, PSTR(".d71")) )) {
      /* D64/M2I mount request */
      free_all_buffers(1);
      /* Open image file */
      res = f_open(&partition[path->part].fatfs, &partition[path->part].imagehandle, dirname, FA_OPEN_EXISTING|FA_READ|FA_WRITE);
      if (res != FR_OK) {
        parse_error(res,1);
        return 1;
      }

      if (!ustrcasecmp_P(ext, PSTR(".m2i")))
        partition[path->part].fop = &m2iops;
      else {
        if (d64_mount(path->part))
          return 1;
        partition[path->part].fop = &d64ops;
      }

      partition[path->part].current_dir = partition[path->part].fatfs.curr_dir;
      return 0;
    }
  }
  return 0;
}

/* Create a new directory */
void fat_mkdir(path_t *path, uint8_t *dirname) {
  FRESULT res;

  partition[path->part].fatfs.curr_dir = path->fat;
  pet2asc(dirname);
  res = f_mkdir(&partition[path->part].fatfs, dirname);
  parse_error(res,0);
}

/**
 * fat_getvolumename - Get the volume label
 * @part : partition to request
 * @label: pointer to the buffer for the label (16 characters)
 *
 * This function reads the FAT volume label and stores it zero-terminated
 * in label. Returns 0 if successfull, != 0 if an error occured.
 */
uint8_t fat_getvolumename(uint8_t part, uint8_t *label) {
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
 * fat_getlabel - Get the directory label
 * @path : path object of the directory
 * @label: pointer to the buffer for the label (16 characters)
 *
 * This function reads the FAT volume label (if in root directory) or FAT
 * directory name (if not) and stores it space-padded
 * in the first 16 bytes of label. Returns 0 if successfull, != 0 if
 * an error occured.
 */
uint8_t fat_getlabel(path_t *path, uint8_t *label) {
  DIR dh;
  FILINFO finfo;
  FRESULT res;
  uint8_t *name = entrybuf;

  finfo.lfn = name;
  memset(label, ' ', CBM_NAME_LENGTH);

  if((res = l_opendir(&partition[path->part].fatfs,path->fat,&dh)) != FR_OK)
    goto gl_error;
  while ((res = f_readdir(&dh, &finfo)) == FR_OK) {
    if(finfo.fname[0] == '\0' || finfo.fname[0] != '.') {
      if((res = fat_getvolumename(path->part,name)) != FR_OK)
        return res;
      break;
    }
    if(finfo.fname[0] == '.' && finfo.fname[1] == '.' && finfo.fname[2] == 0) {
      if((res = l_opendir(&partition[path->part].fatfs,finfo.clust,&dh)) != FR_OK) // open .. dir.
        break;
      while ((res = f_readdir(&dh, &finfo)) == FR_OK) {
        if(finfo.fname[0] == '\0')
          break;
        if(finfo.clust == path->fat) {
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

  if(*name)
    memcpy(label,name,ustrlen(name));
  return 0;

gl_error:
  parse_error(res,0);
  return 1;
}

/**
 * fat_getid - Create a disk id
 * @part: partition number
 * @id  : pointer to the buffer for the id (5 characters)
 *
 * This function creates a disk ID from the FAT type (12/16/32)
 * and the usual " 2A" of a 1541 in the first 5 bytes of id.
 * Always returns 0 for success.
 */
uint8_t fat_getid(uint8_t part, uint8_t *id) {
  switch (partition[part].fatfs.fs_type) {
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

  if (f_getfree(fs, NULLSTRING, &clusters) == FR_OK) {
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
void fat_rename(path_t *path, struct cbmdirent *dent, uint8_t *newname) {
  FRESULT res;

  partition[path->part].fatfs.curr_dir = path->fat;
  if (dent->realname[0]) {
    /* [PSUR]00 rename, just change the internal file name */
    UINT byteswritten;

    res = f_open(&partition[path->part].fatfs, &partition[path->part].imagehandle, dent->realname, FA_WRITE|FA_OPEN_EXISTING);
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

    res = f_write(&partition[path->part].imagehandle, newname, CBM_NAME_LENGTH, &byteswritten);
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
    /* Normal rename */
    pet2asc(dent->name);
    pet2asc(newname);
    res = f_rename(&partition[path->part].fatfs, dent->name, newname);
    if (res != FR_OK)
      parse_error(res, 0);
  }
}

/**
 * init_fatops - Initialize fatops module
 * @preserve_path: Preserve the current directory if non-zero
 *
 * This function will initialize the fatops module and force
 * mounting of the card. It can safely be called again if re-mounting
 * is required.
 */
void init_fatops(uint8_t preserve_path) {
  FRESULT res;
  uint8_t drive,part;

  max_part = 0;
  drive = 0;
  part = 0;
  while (max_part < CONFIG_MAX_PARTITIONS && drive < MAX_DRIVES) {
    partition[max_part].fop = &fatops;
    res=f_mount((drive * 16) + part, &partition[max_part].fatfs);

    if (!preserve_path)
      partition[max_part].current_dir = 0;

    if (res == FR_OK)
      max_part++;

    if (res != FR_INVALID_OBJECT && part < 15 &&
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
    set_changelist(NULL, NULLSTRING);
  }

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

  buf = find_buffer(BUFFER_SEC_SYSTEM + part);
  if (buf) {
    if (buf->cleanup)
      buf->cleanup(buf);
    free_buffer(buf);
  }

  free_all_buffers(1);
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
 * @path   : path object of the location of dirname
 * @dirname: directory to be changed into
 *
 * This function will ignore any dirnames except _ (left arrow)
 * and unmount the image if that is found. It can be used as
 * chdir/mkdir for all image types that don't support subdirectories
 * themselves. Returns 0 if successful, 1 otherwise.
 */
uint8_t image_chdir(path_t *path, uint8_t *dirname) {
  if (dirname[0] == '_' && dirname[1] == 0) {
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

const PROGMEM fileops_t fatops = {  // These should be at bottom, to be consistent with d64ops and m2iops
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
  &fat_chdir,
  &fat_rename
};
