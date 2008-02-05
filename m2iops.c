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


   m2iops.c: M2I operations

*/

#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "dirent.h"
#include "errormsg.h"
#include "fatops.h"
#include "tff.h"
#include "wrapops.h"
#include "m2iops.h"

#define M2I_ENTRY_LEN      33
#define M2I_ENTRY_OFFSET   18
#define M2I_CBMNAME_OFFSET 15
#define M2I_FATNAME_OFFSET 2
#define M2I_FATNAME_LEN    12

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * name_repad - changes CBM name padding in entrybuf
 * @oldchar: char to be substituted
 * @newchar: char to be substituded with
 *
 * This function changes the padding of the CBM name in entrybuf from oldchar
 * to newchar. Please note that strnsubst is not a straigt replacement for
 * this function.
 */
static void name_repad(uint8_t oldchar, uint8_t newchar) {
  uint8_t i = CBM_NAME_LENGTH-1;
  uint8_t *str;

  str = entrybuf+M2I_CBMNAME_OFFSET;
  while (i > 0 && str[i] == oldchar)
    str[i--] = newchar;
}

/**
 * parsetype - change type letter to CBM filetype in entrybuf
 *
 * This function replaces the type letter in the M2I line in entrybuf
 * with a CBM file type. Returns 1 for deleted or unknown type letters,
 * 0 otherwise.
 */
static uint8_t parsetype(void) {
  switch (entrybuf[0] | 0x20) { /* Lowercase the letter */
  case 'd':
    entrybuf[0] = TYPE_DEL;
    return 0;

  case 's':
    entrybuf[0] = TYPE_SEQ;
    return 0;

  case 'p':
    entrybuf[0] = TYPE_PRG;
    return 0;

  case 'u':
    entrybuf[0] = TYPE_USR;
    return 0;

  default:
    return 1;
  }
}

/**
 * load_entry - load M2I entry at offset into entrybuf
 * @offset: offset in M2I file to be loaded
 *
 * This function loads the M2I line at offset into entrybuf and zero-
 * terminates the FAT name within.  Returns 0 if successful, 1 at
 * end of file or 255 on error.
 */
static uint8_t load_entry(uint16_t offset) {
  uint8_t i;

  i = image_read(offset, entrybuf, M2I_ENTRY_LEN);

  if (i > 1)
    return 255;

  if (i == 1)
    /* Incomplete entry read, assuming end of file */
    return 1;

  /* Be nice and zero-terminate the FAT filename */
  i = 0;
  while (entrybuf[M2I_FATNAME_OFFSET+i] != ' ' && i < M2I_FATNAME_LEN) i++;
  entrybuf[M2I_FATNAME_OFFSET+i] = 0;

  return 0;
}

/**
 * find_entry - locate a CBM file name in an M2I file
 * @name: name to be located
 *
 * This function searches for a given CBM file name in the currently
 * mounted M2I File. Returns 0 if not found, 1 on errors or the
 * offset of the M2I line if found.
 */
static uint16_t find_entry(char *name) {
  uint16_t pos = M2I_ENTRY_OFFSET;
  uint8_t i;
  uint8_t *srcname, *dstname;

  while (1) {
    i = load_entry(pos);
    pos += M2I_ENTRY_LEN;

    if (i) {
      if (i == 1)
	return 0;
      else
	return 1;
    }

    /* Skip deleted entries */
    if (entrybuf[0] == '-')
      continue;

    name_repad(' ',0);
    srcname = (uint8_t *)name;
    dstname = entrybuf + M2I_CBMNAME_OFFSET;
    dstname[CBM_NAME_LENGTH] = 0;

    if (strcmp((char *)srcname, (char *)dstname))
      continue;

    return pos - M2I_ENTRY_LEN;
  }
}

/**
 * find_empty_entry - returns the offset of the first empty M2I entry
 *
 * This function looks for a deleted entry in an M2I file and returns
 * it offset. Returns 1 on error or an offset if successful. The
 * offset may point to a position beyond the end of file if there
 * were no free entries available.
 */
static uint16_t find_empty_entry(void) {
  uint16_t pos = M2I_ENTRY_OFFSET;
  uint8_t i;

  while (1) {
    i = load_entry(pos);

    if (i) {
      if (i == 255)
	return 1;
      else
	return pos;
    }

    if (entrybuf[0] == '-')
      return pos;

    pos += M2I_ENTRY_LEN;
  }
}

/**
 * open_existing - open an existing file
 * @name      : name of the file
 * @type      : type of the file (not checked)
 * @buf       : buffer to be used
 * @appendflag: Flags if the file should be opened for appending
 *
 * This function searches the file name in an M2I file and opens it
 * either in read or append mode according to appendflag by calling
 * the appropiate fat_* functions.
 */
static void open_existing(char *name, uint8_t type, buffer_t *buf, uint8_t appendflag) {
  uint16_t offset;

  offset = find_entry(name);
  if (offset < M2I_ENTRY_OFFSET) {
    set_error(ERROR_FILE_NOT_FOUND);
    return;
  }

  if (parsetype()) {
    set_error(ERROR_FILE_NOT_FOUND);
    return;
  }

  if (appendflag)
    fat_open_write("", (char *)entrybuf+M2I_FATNAME_OFFSET, type, buf, 1);
  else
    fat_open_read("", (char *)entrybuf+M2I_FATNAME_OFFSET, buf);
}

/* ------------------------------------------------------------------------- */
/*  fileops-API                                                              */
/* ------------------------------------------------------------------------- */

static uint8_t m2i_opendir(dh_t *dh, char *path) {
  dh->m2i = M2I_ENTRY_OFFSET;
  return 0;
}

static int8_t m2i_readdir(dh_t *dh, struct cbmdirent *dent) {
  uint8_t i;

  while (1) {
    i = load_entry(dh->m2i);
    if (i) {
      if (i == 255)
	return 1;
      else
	return -1;
    }

    dh->m2i += M2I_ENTRY_LEN;

    /* Check file type */
    if (parsetype())
      continue;

    dent->typeflags = entrybuf[0];

    /* Copy CBM file name */
    name_repad(' ', 0xa0);
    memset(dent->name, 0xa0, sizeof(dent->name));
    memcpy(dent->name, entrybuf+M2I_CBMNAME_OFFSET, CBM_NAME_LENGTH);

    /* Get file size */
    if (dent->typeflags != TYPE_DEL) {
      /* Sorry, but I have to fake the sizes.                        */
      /* Reading the correct size as above requires a directory scan */
      /* per file which means that the single FAT buffer switches    */
      /* between the M2I file and the directory for every single     */
      /* file -> slooooow (<1 file/s for an M2I with 500 entries)    */
      dent->blocksize = 1;
      dent->remainder = 0xff;
#if 0
      FILINFO finfo;

      res = f_stat((char *)entrybuf+M2I_FATNAME_OFFSET, &finfo);
      if (res != FR_OK) {
	if (res == FR_NO_FILE)
	  continue;
	else {
	  parse_error(res,1);
	  return 1;
	}
      }

      if (finfo.fsize > 16255746)
	/* File too large -> size 63999 blocks */
	dent->blocksize = 63999;
      else
	dent->blocksize = (finfo.fsize+253) / 254;

      dent->remainder = finfo.fsize % 254;
#endif
    } else
      dent->blocksize = 0;

    return 0;
  }
}

static uint8_t m2i_getlabel(char *label) {
  return image_read(0, label, 16);
}

static void m2i_open_read(char *path, char *name, buffer_t *buf) {
  /* The type isn't checked anyway */
  open_existing(name, TYPE_PRG, buf, 0);
}

static void m2i_open_write(char *path, char *name, uint8_t type, buffer_t *buf, uint8_t append) {
  uint16_t offset;
  uint8_t *str;
  char *nameptr;
  uint8_t i;
  FRESULT res;

  if (append) {
    open_existing(name, type, buf, 1);
  } else {
    // FIXME: Sind das alle zu verbietenden Zeichen?
    nameptr = name;
    while (*nameptr) {
      if (*nameptr == '=' || *nameptr == '"' ||
	  *nameptr == '*' || *nameptr == '?') {
	set_error(ERROR_SYNTAX_JOKER);
	return;
      }
      nameptr++;
    }

    /* Find an empty entry */
    offset = find_empty_entry();
    if (offset < M2I_ENTRY_OFFSET)
      return;

    memset(entrybuf, ' ', sizeof(entrybuf));
    str = entrybuf;

    switch (type & TYPE_MASK) {
    case TYPE_DEL:
      *str++ = 'D';
      break;

    case TYPE_SEQ:
      *str++ = 'S';
      break;

    case TYPE_PRG:
      *str++ = 'P';
      break;

    case TYPE_USR:
      *str++ = 'U';
      break;

    default:
      /* Unknown type - play it safe, don't create a file */
      return;
    }

    *str++ = ':';

    /* Generate a FAT name */
    for (i=0;i<8;i++) {
      *str++ = '0';
    }
    *str = 0;

    do {
      FILINFO finfo;

      /* See if it's already there */
      res = f_stat((char *)entrybuf+M2I_FATNAME_OFFSET, &finfo);
      if (res == FR_OK) {
	str = entrybuf+M2I_FATNAME_OFFSET+7;
	/* Increment name */
	while (1) {
	  if (++(*str) > '9') {
	    *str-- = '0';
	    continue;
	  }
	  break;
	}
      }
    } while (res == FR_OK);

    if (res != FR_NO_FILE)
      return;

    /* Copy the CBM file name */
    nameptr = name;
    str = entrybuf+M2I_CBMNAME_OFFSET;
    while (*nameptr)
      *str++ = *nameptr++;

    /* Overwrite the original name */
    strcpy(name, (char *) entrybuf+M2I_FATNAME_OFFSET);

    /* Finish creating the M2I entry */
    entrybuf[M2I_FATNAME_OFFSET+8]  = ' ';
    entrybuf[M2I_FATNAME_OFFSET+12] = ':';
    entrybuf[M2I_CBMNAME_OFFSET+CBM_NAME_LENGTH] = 13;
    entrybuf[M2I_CBMNAME_OFFSET+CBM_NAME_LENGTH+1] = 10;

    /* Write it */
    if (image_write(offset, entrybuf, M2I_ENTRY_LEN, 1))
      return;

    /* Write the actual file */
    fat_open_write("", name, type, buf, append);

    /* Abort on error */
    if (current_error) {
      /* No error checking here. Either it works or everything has failed. */
      entrybuf[0] = '-';
      image_write(offset, entrybuf, 1, 1);
    }
  }
}

static uint8_t m2i_delete(char *path, char *name) {
  uint16_t offset;

  offset = find_entry(name);
  if (offset == 1)
    return 255;

  if (offset == 0)
    return 0;

  /* Ignore the result, we'll have to delete the entry anyway */
  fat_delete("", (char *)entrybuf+M2I_FATNAME_OFFSET);

  entrybuf[0] = '-';
  if (image_write(offset, entrybuf, 1, 1))
    return 0;
  else
    return 1;
}

const PROGMEM fileops_t m2iops = {
  m2i_open_read,
  m2i_open_write,
  m2i_delete,
  m2i_getlabel,
  fat_getid,
  fat_freeblocks,
  fat_sectordummy,
  fat_sectordummy,
  m2i_opendir,
  m2i_readdir,
  image_chdir, /* Technically mkdir, but who'll notice? */
  image_chdir
};
