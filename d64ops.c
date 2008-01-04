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

#define DIR_TRACK        18
#define LABEL_OFFSET     0x90
#define ID_OFFSET        0xa2
#define DIR_START_SECTOR 1
#define OFS_FILE_TYPE    2
#define OFS_TRACK        3
#define OFS_SECTOR       4
#define OFS_FILE_NAME    5
#define OFS_SIZE_LOW     0x1e
#define OFS_SIZE_HI      0x1f

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

static uint8_t d64_read(buffer_t *buf) {
  uint32_t sectorofs = sector_offset(buf->data[0],buf->data[1]);

  if (image_read(sectorofs, buf->data, 256))
    return 1;

  buf->position = 2;

  if (buf->data[0] == 0) {
    /* Final sector of the file */
    buf->lastused = buf->data[1];
    buf->sendeoi  = 1;
  } else {
    buf->lastused = 255;
    buf->sendeoi  = 0;
  }

  return 0;
}

/* ------------------------------------------------------------------------- */
/*  fileops-API                                                              */
/* ------------------------------------------------------------------------- */

static uint8_t d64_opendir(dh_t *dh, char *path) {
  dh->d64.track  = DIR_TRACK;
  dh->d64.sector = DIR_START_SECTOR;
  dh->d64.entry  = 0;
  return 0;
}

static int8_t d64_readdir(dh_t *dh, struct cbmdirent *dent) {
  do {
    /* End of directory entries in this sector? */
    if (dh->d64.entry == 8) {
      /* Read link pointer */
      if (image_read(sector_offset(dh->d64.track, dh->d64.sector), entrybuf, 2))
	return 1;
      
      /* Final directory sector? */
      if (entrybuf[0] == 0)
	return -1;

      dh->d64.track  = entrybuf[0];
      dh->d64.sector = entrybuf[1];
      dh->d64.entry   = 0;
    }

    if (image_read(sector_offset(dh->d64.track, dh->d64.sector)+
		   dh->d64.entry*32, entrybuf, 32))
      return 1;

    dh->d64.entry++;

    if (entrybuf[OFS_FILE_TYPE] != 0)
      break;
  } while (1);

  dent->typeflags = entrybuf[OFS_FILE_TYPE] ^ FLAG_SPLAT;
  dent->blocksize = entrybuf[OFS_SIZE_LOW] + 256*entrybuf[OFS_SIZE_HI];
  memcpy(dent->name, entrybuf+OFS_FILE_NAME, CBM_NAME_LENGTH);
  dent->name[16] = 0;

  return 0;
}

static uint8_t d64_getlabel(char *label) {
  if (image_read(sector_offset(DIR_TRACK,0) + LABEL_OFFSET, label, 16))
    return 1;
  
  strnsubst((uint8_t *)label, 16, 0xa0, 0x20);
  return 0;
}

static uint8_t d64_getid(char *id) {
  if (image_read(sector_offset(DIR_TRACK,0) + ID_OFFSET, id, 5))
    return 1;

  strnsubst((uint8_t *)id, 5, 0xa0, 0x20);
  return 0;
}

static uint16_t d64_freeblocks(void) {
  uint16_t blocks = 0;
  uint8_t i;

  for (i=1;i<36;i++) {
    /* Skip directory track */
    if (i == 18)
      continue;
    if (image_read(sector_offset(DIR_TRACK,0) + 4*i, entrybuf, 1))
      return 0;
    blocks += entrybuf[0];
  }
  
  return blocks;
}

static void d64_open_read(char *path, char *name, buffer_t *buf) {
  /* WARNING: Ugly hack used here. The directory entry is still in  */
  /*          entrybuf because of match_entry in fatops.c/file_open */
  buf->data[0] = entrybuf[OFS_TRACK];
  buf->data[1] = entrybuf[OFS_SECTOR];

  // FIXME: Check the file type
  
  buf->read    = 1;
  buf->write   = 0;
  buf->cleanup = generic_cleanup;
  buf->refill  = d64_read;

  buf->refill(buf);
}

static void d64_read_sector(buffer_t *buf, uint8_t track, uint8_t sector) {
  image_read(sector_offset(track,sector), buf->data, 256);
}

static void d64_write_sector(buffer_t *buf, uint8_t track, uint8_t sector) {
  image_write(sector_offset(track,sector), buf->data, 256);
}

const PROGMEM fileops_t d64ops = {
  d64_open_read, // open_read,
  NULL, // open_write,
  NULL, // delete,
  d64_getlabel,
  d64_getid,
  d64_freeblocks,
  d64_read_sector,
  d64_write_sector,
  d64_opendir,
  d64_readdir,
  image_chdir, // mkdir...
  image_chdir
};
