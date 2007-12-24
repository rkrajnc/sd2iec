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

   
   d64ops.c: D64 operations

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
#include "d64ops.h"

#define DIRECTORY_TRACK 18
#define LABEL_OFFSET    0x90
#define ID_OFFSET       0xa2

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/* Transform a track/sector pair into a D64 offset   */
/* I wonder if the code size can be reduced further? */
static uint32_t sector_offset(uint8_t track, const uint8_t sector) {
  track--; /* Track numbers are 1-based */
  if (track < 17)
    return 256L * (track*21 + sector);
  if (track < 24)
    return 256L * (17*21 + (track-17)*19 + sector);
  if (track < 30)
    return 256L * (17*21 + 7*19 + (track-24)*18 + sector);
  return 256L * (17*21 + 7*19 + 6*18 + (track-30)*17 + sector);
}

/* Replace oldchar with newchar in the first len bytes of buffer */
static void strnsubst(uint8_t *buffer, uint8_t len, uint8_t oldchar, uint8_t newchar) {
  uint8_t i = len-1;

  while (i > 0) {
    if (buffer[i] == oldchar)
      buffer[i] = newchar;
    i--;
  }
}
/* ------------------------------------------------------------------------- */
/*  fileops-API                                                              */
/* ------------------------------------------------------------------------- */

static uint8_t d64_opendir(dh_t *dh, char *path) {
  return 0;
}

static int8_t d64_readdir(dh_t *dh, struct cbmdirent *dent) {
  return -1;
}

static uint8_t d64_getlabel(char *label) {
  if (image_read(sector_offset(DIRECTORY_TRACK,0) + LABEL_OFFSET, label, 16))
    return 1;
  
  strnsubst((uint8_t *)label, 16, 0xa0, 0x20);
  return 0;
}

static uint8_t d64_getid(char *id) {
  if (image_read(sector_offset(DIRECTORY_TRACK,0) + ID_OFFSET, id, 5))
    return 1;

  strnsubst((uint8_t *)id, 5, 0xa0, 0x20);
  return 0;
}

static uint16_t d64_freeblocks(void) {
  return 0;
}

const PROGMEM fileops_t d64ops = {
  NULL, // open_read,
  NULL, // open_write,
  NULL, // delete,
  d64_getlabel,
  d64_getid,
  d64_freeblocks,
  d64_opendir,
  d64_readdir,
  image_chdir, // mkdir...
  image_chdir
};
