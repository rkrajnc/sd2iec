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

   
   m2iops.c: M2I operations

*/

#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "dirent.h"
#include "buffers.h"
#include "tff.h"
#include "buffers.h"
#include "wrapops.h"
#include "errormsg.h"
#include "fatops.h"
#include "m2iops.h"

#define M2I_ENTRY_LEN      33
#define M2I_ENTRY_OFFSET   18
#define M2I_CBMNAME_OFFSET 15
#define M2I_FATNAME_OFFSET 2
#define M2I_FATNAME_LEN    12

/* Changes the CBM name padding in entrybuf */
static void name_repad(uint8_t oldchar, uint8_t newchar) {
  uint8_t i = CBM_NAME_LENGTH-1;
  uint8_t *str;

  str = entrybuf+M2I_CBMNAME_OFFSET;
  while (i > 0 && str[i] == oldchar)
    str[i--] = newchar;
}

/* Changes the type letter in entrybuf to a CBM type
 * Returns 1 if deleted or unknown entry
 *         0 otherwise
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

/* Load the M2I entry at offset
 * Returns 0 if successful
 *         1 if end-of-file
 *       255 on errors
 */
static uint8_t load_entry(uint16_t offset) {
  UINT bytesread;
  FRESULT res;
  uint8_t i;

  res = f_lseek(&imagehandle, offset);
  if (res != FR_OK) {
    parse_error(res,1);
    return 255;
  }

  res = f_read(&imagehandle, entrybuf, M2I_ENTRY_LEN, &bytesread);
  if (res != FR_OK) {
    parse_error(res,1);
    return 255;
  }
  
  if (bytesread != M2I_ENTRY_LEN)
    /* Incomplete entry read, assuming end of file */
    return 1;

  /* Be nice and zero-terminate the FAT filename */
  i = 0;
  while (entrybuf[M2I_FATNAME_OFFSET+i] != ' ' && i < M2I_FATNAME_LEN) i++;
  entrybuf[M2I_FATNAME_OFFSET+i] = 0;

  return 0;
}

static uint8_t m2i_opendir(dh_t *dh, char *path) {
  dh->m2i = M2I_ENTRY_OFFSET;
  return 0;
}

static int8_t m2i_readdir(dh_t *dh, struct cbmdirent *dent) {
  uint8_t i;
  uint8_t *str;

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
#endif
    } else
      dent->blocksize = 0;

    return 0;
  }
}

static uint8_t m2i_getlabel(char *label) {
  FRESULT res;
  UINT bytesread;

  res = f_lseek(&imagehandle, 0);
  if (res != FR_OK) {
    parse_error(res,1);
    return 1;
  }

  res = f_read(&imagehandle, label, 16, &bytesread);
  if (res != FR_OK || bytesread != 16) {
    parse_error(res,1);
    return 1;
  }
  return 0;
}

static void m2i_umount(void) {
  FRESULT res;

  fop = &fatops;
  res = f_close(&imagehandle);
  if (res != FR_OK)
    parse_error(res,0);
}

static void m2i_open_read(char *path, char *name, buffer_t *buf) {
  return;
}

static void m2i_open_write(char *path, char *name, uint8_t type, buffer_t *buf, uint8_t append) {
  return;
}

static uint8_t m2i_delete(char *path, char *name) {
  return 0;
}

static void m2i_chdir(char *dirname) {
  if (!strcmp_P(dirname, PSTR("_"))) {
    /* Unmount request */
    free_all_buffers(1);
    m2i_umount();
  }
  return;
}

void m2i_mount(char *filename) {
  FRESULT res;

  res = f_open(&imagehandle, filename, FA_OPEN_EXISTING|FA_READ|FA_WRITE);
  if (res != FR_OK) {
    parse_error(res,1);
    return;
  }

  fop = &m2iops;
}

const PROGMEM fileops_t m2iops = {
  m2i_open_read,
  m2i_open_write,
  m2i_delete,
  m2i_getlabel,
  fat_getid,
  fat_freeblocks,
  m2i_opendir,
  m2i_readdir,
  m2i_chdir, /* Technically mkdir, but who'll notice? */
  m2i_chdir
};
