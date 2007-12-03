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
#include "config.h"
#include "dirent.h"
#include "buffers.h"
#include "tff.h"
#include "buffers.h"
#include "wrapops.h"
#include "errormsg.h"
#include "fatops.h"
#include "m2iops.h"

#define M2I_ENTRY_LEN 33
#define M2I_ENTRY_OFFSET 18

static uint8_t m2i_opendir(dh_t *dh, char *path) {
  return 0;
}

static int8_t m2i_readdir(dh_t *dh, struct cbmdirent *dent) {
  return -1;
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

