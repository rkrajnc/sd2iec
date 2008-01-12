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
#define LAST_TRACK       35
#define LABEL_OFFSET     0x90
#define ID_OFFSET        0xa2
#define DIR_START_SECTOR 1
#define OFS_FILE_TYPE    2
#define OFS_TRACK        3
#define OFS_SECTOR       4
#define OFS_FILE_NAME    5
#define OFS_SIZE_LOW     0x1e
#define OFS_SIZE_HI      0x1f

#define BAM_TRACK 18
#define BAM_SECTOR 0
#define BAM_BYTES_PER_TRACK 4
#define FILE_INTERLEAVE 10
#define DIR_INTERLEAVE 3

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * sector_offset - Transform a track/sector pair into a D64 offset
 * @track : Track number
 * @sector: Sector number
 *
 * Calculates an offset into a D64 file from a track and sector number.
 */
/* This version used the least code of all tested variants. */
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

/**
 * sectors_per_track - number of sectors on given track
 * @track: Track number
 *
 * This function returns the number of sectors on the given track
 * of a 1541 disk. Invalid track numbers will return invalid results.
 */
static uint8_t sectors_per_track(uint8_t track) {
  if (track < 18)
    return 21;
  if (track < 25)
    return 19;
  if (track < 31)
    return 18;
  return 17;
}

/**
 * strnsubst - substitute one character with another in a buffer
 * @buffer : pointer to the buffer
 * @len    : length of buffer
 * @oldchar: character to be replaced
 * @newchar: character to be used as a replacement
 *
 * This functions changes all occurences of oldchar in the first len
 * byte of buffer with newchar. Although it is named str*, it doesn't
 * care about zero bytes in any way.
 */
static void strnsubst(uint8_t *buffer, uint8_t len, uint8_t oldchar, uint8_t newchar) {
  uint8_t i=len-1;

  do {
    if (buffer[i] == oldchar)
      buffer[i] = newchar;
  } while (i--);
}

/**
 * read_bam - return a pointer to a buffer with the current BAM
 *
 * This function returns a pointer to a buffer containing the
 * BAM of the D64 image. Returns NULL on failure.
 */
static buffer_t* read_bam(void) {
  buffer_t *buf;

  buf = alloc_buffer();
  if (!buf)
    return NULL;;

  DIRTY_LED_ON();
  active_buffers += 16;
  buf->write      = 1;

  if (image_read(sector_offset(BAM_TRACK,BAM_SECTOR), buf->data, 256)) {
    free_buffer(buf);
    return NULL;
  }

  return buf;
}

/**
 * write_bam - write BAM buffer to disk
 * @buf: pointer to the BAM buffer
 *
 * This function writes the contents of the BAM buffer to the disk image.
 * Returns 0 if successful, != 0 otherwise.
 */
static uint8_t write_bam(buffer_t *buf) {
  uint8_t res;

  res = image_write(sector_offset(BAM_TRACK,BAM_SECTOR), buf->data, 256, 1);
  free_buffer(buf);
  return res;
}

/**
 * is_free - checks if the given sector is marked as free
 * @track : track number
 * @sector: sector number
 * @bambuf: pointer to a buffer holding the current BAM
 *
 * This function checks if the given sector is marked as free in
 * the supplied BAM buffer. Returns 0 if allocated, != 0 if free.
 */
static uint8_t is_free(uint8_t track, uint8_t sector, buffer_t *bambuf) {
  return bambuf->data[4*track+1+(sector>>3)] & (1<<(sector&7));
}

/**
 * sectors_free - returns the number of free sectors on a given track
 * @track : track number
 * @bambuf: pointer to a buffer holding the current BAM
 *
 * This function returns the number of free sectors on the given track
 * as stored in the supplied BAM buffer.
 */
static uint8_t sectors_free(uint8_t track, buffer_t *bambuf) {
  if (track < 1 || track > LAST_TRACK)
    return 0;
  return bambuf->data[4*track];
}

/**
 * allocate_sector - mark a sector as used
 * @track : track number
 * @sector: sector number
 * @bambuf: pointer to a buffer holding the current BAM
 *
 * This function marks the given sector as used in the supplied BAM
 * buffer. If the sector was already marked as used nothing is changed.
 */
static void allocate_sector(uint8_t track, uint8_t sector, buffer_t *bambuf) {
  uint8_t *trackmap = bambuf->data+4*track;

  if (is_free(track,sector,bambuf)) {
    if (trackmap[0] > 0)
      trackmap[0]--;

    trackmap[1+(sector>>3)] &= ~(1<<(sector&7));
  }
}

/**
 * free_sector - mark a sector as free
 * @track : track number
 * @sector: sector number
 * @bambuf: pointer to a buffer holding the current BAM
 *
 * This function marks the given sector as free in the supplied BAM
 * buffer. If the sector was already marked as free nothing is changed.
 */
static void free_sector(uint8_t track, uint8_t sector, buffer_t *bambuf) {
  uint8_t *trackmap = bambuf->data+4*track;

  if (!is_free(track,sector,bambuf)) {
    trackmap[0]++;

    trackmap[1+(sector>>3)] |= 1<<(sector&7);
  }
}

/**
 * get_first_sector - calculate the first sector for a new file
 * @track : pointer to a variable holding the track
 * @sector: pointer to a variable holding the sector
 * @bambuf: pointer to a buffer holding the current BAM
 *
 * This function calculates the first sector to be allocated for a new
 * file. The algorithm is based on the description found at
 * http://ist.uwaterloo.ca/~schepers/formats/DISK.TXT
 * Returns 0 if successful or 1 if any error occured.
 */
static uint8_t get_first_sector(uint8_t *track, uint8_t *sector, buffer_t *bambuf) {
  int8_t distance = 1;

  /* Look for a track with free sectors close to the directory */
  while (distance < LAST_TRACK) {
    if (sectors_free(DIR_TRACK-distance, bambuf))
      break;

    /* Invert sign */
    distance = -distance;

    /* Increase distance every second try */
    if (distance > 0)
      distance++;
  }

  if (distance == LAST_TRACK) {
    set_error(ERROR_DISK_FULL);
    return 1;
  }

  /* Search for the first free sector on this track */
  *track = DIR_TRACK-distance;
  for (*sector = 0;*sector < sectors_per_track(*track); *sector += 1)
    if (is_free(*track, *sector, bambuf))
      return 0;

  /* If we're here the BAM is invalid */
  set_error(ERROR_DISK_FULL);
  return 1;
}

/**
 * get_next_sector - calculate the next sector for a file
 * @track : pointer to a variable holding the track
 * @sector: pointer to a variable holding the sector
 * @bambuf: pointer to a buffer holding the current BAM
 *
 * This function calculates the next sector to be allocated for a file
 * based on the current sector in the variables pointed to by track/sector.
 * The algorithm is based on the description found at
 * http://ist.uwaterloo.ca/~schepers/formats/DISK.TXT
 * Returns 0 if successful or 1 if any error occured.
 */
static uint8_t get_next_sector(uint8_t *track, uint8_t *sector, buffer_t *bambuf) {
  uint8_t interleave,tries;

  if (*track == DIR_TRACK) {
    if (sectors_free(DIR_TRACK,bambuf) == 0) {
      set_error(ERROR_DISK_FULL);
      return 1;
    }
    interleave = DIR_INTERLEAVE;
  } else
    interleave = FILE_INTERLEAVE;

  /* Look for a track with free sectors */
  tries = 0;
  while (tries < 3 && !sectors_free(*track,bambuf)) {
    /* No more space on current track, try another */
    if (*track < DIR_TRACK)
      *track -= 1;
    else
      *track += 1;

    if (*track < 1) {
      *track = DIR_TRACK + 1;
      *sector = 0;
      tries++;
    }
    if (*track > LAST_TRACK) {
      *track = DIR_TRACK - 1;
      *sector = 0;
      tries++;
    }
  }

  if (tries == 3) {
    set_error(ERROR_DISK_FULL);
    return 1;
  }

  /* Look for a sector at interleave distance */
  *sector += interleave;
  if (*sector >= sectors_per_track(*track)) {
    *sector -= sectors_per_track(*track);
    if (*sector != 0)
      *sector -= 1;
  }

  /* Increase distance until an empty sector is found */
  tries = 99;
  while (!is_free(*track,*sector,bambuf) && tries--) {
    *sector += 1;
    if (*sector >= sectors_per_track(*track))
      *sector = 0;
  }

  if (tries)
    return 0;

  set_error(ERROR_DISK_FULL);
  return 1;
}

/**
 * nextdirentry - read the next dir entry to entrybuf
 * @dh: directory handle
 *
 * This function reads the next directory entry from the disk
 * into entrybuf. Returns 1 if an error occured, -1 if there
 * are no more directory entries and 0 if successful. This
 * function will return every entry, even deleted ones.
 */
static int8_t nextdirentry(dh_t *dh) {
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
    dh->d64.entry  = 0;
  }

  if (image_read(sector_offset(dh->d64.track, dh->d64.sector)+
		 dh->d64.entry*32, entrybuf, 32))
    return 1;

  dh->d64.entry++;

  return 0;
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
  int8_t res;

  do {
    res = nextdirentry(dh);
    if (res)
      return res;

    if (entrybuf[OFS_FILE_TYPE] != 0)
      break;
  } while (1);

  dent->typeflags = entrybuf[OFS_FILE_TYPE] ^ FLAG_SPLAT;
  dent->blocksize = entrybuf[OFS_SIZE_LOW] + 256*entrybuf[OFS_SIZE_HI];
  dent->remainder = 0xff;
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
  buf->cleanup = NULL;
  buf->refill  = d64_read;

  buf->refill(buf);
}

static void d64_read_sector(buffer_t *buf, uint8_t track, uint8_t sector) {
  image_read(sector_offset(track,sector), buf->data, 256);
}

static void d64_write_sector(buffer_t *buf, uint8_t track, uint8_t sector) {
  image_write(sector_offset(track,sector), buf->data, 256, 1);
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
