/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>

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


   d64ops.c: D64 operations

*/

#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "dirent.h"
#include "errormsg.h"
#include "fatops.h"
#include "ff.h"
#include "wrapops.h"
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
 * checked_read - read a specified sector after range-checking
 * @drive : drive number
 * @track : track number to be read
 * @sector: sector number to be read
 * @buf   : pointer to where the data should be read to
 * @len   : number of bytes to be read
 * @error : error number to be flagged if the range check fails
 *
 * This function checks if the track and sector are within the
 * limits for the image format and calls image_read to read
 * the data if they are. Returns the result of image_read or
 * 2 if the range check failed.
 */
static uint8_t checked_read(uint8_t drive, uint8_t track, uint8_t sector, uint8_t *buf, uint16_t len, uint8_t error) {
  if (track < 1 || track > LAST_TRACK ||
      sector >= sectors_per_track(track)) {
    set_error_ts(error,track,sector);
    return 2;
  }
  return image_read(drive, sector_offset(track,sector), buf, len);
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
 * @drive: drive number
 *
 * This function returns a pointer to a buffer containing the
 * BAM of the D64 image. Returns NULL on failure.
 */
static buffer_t* read_bam(uint8_t drive) {
  buffer_t *buf;

  buf = alloc_buffer();
  if (!buf)
    return NULL;;

  mark_write_buffer(buf);

  if (image_read(drive, sector_offset(BAM_TRACK,BAM_SECTOR), buf->data, 256)) {
    free_buffer(buf);
    return NULL;
  }

  buf->pvt.d64.drive = drive;

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

  res = image_write(buf->pvt.d64.drive, sector_offset(BAM_TRACK,BAM_SECTOR), buf->data, 256, 1);
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

    trackmap[1+(sector>>3)] &= (uint8_t)~(1<<(sector&7));
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
  if (dh->dir.d64.entry == 8) {
    /* Read link pointer */
    if (checked_read(dh->drive, dh->dir.d64.track, dh->dir.d64.sector, entrybuf, 2, ERROR_ILLEGAL_TS_LINK))
      return 1;

    /* Final directory sector? */
    if (entrybuf[0] == 0)
      return -1;

    dh->dir.d64.track  = entrybuf[0];
    dh->dir.d64.sector = entrybuf[1];
    dh->dir.d64.entry  = 0;
  }

  if (image_read(dh->drive, sector_offset(dh->dir.d64.track, dh->dir.d64.sector)+
                 dh->dir.d64.entry*32, entrybuf, 32))
    return 1;

  dh->dir.d64.entry++;

  return 0;
}

static uint8_t d64_read(buffer_t *buf) {
  /* Store the current sector, used for append */
  buf->pvt.d64.track  = buf->data[0];
  buf->pvt.d64.sector = buf->data[1];

  if (checked_read(buf->pvt.d64.drive, buf->data[0], buf->data[1], buf->data, 256, ERROR_ILLEGAL_TS_LINK))
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

static uint8_t d64_write(buffer_t *buf) {
  uint8_t t,s,savederror;
  buffer_t *bambuf;

  savederror = 0;
  t = buf->pvt.d64.track;
  s = buf->pvt.d64.sector;

  buf->pvt.d64.blocks++;

  /* Mark as last sector in case something below fails */
  buf->data[0] = 0;
  buf->data[1] = buf->lastused;

  /* Read BAM */
  /* Yes, this means writing can suddenly fail with NO CHANNEL. */
  bambuf = read_bam(buf->pvt.d64.drive);
  if (!bambuf) {
    savederror = current_error;
    goto storedata;
  }

  /* Find another free sector */
  if (get_next_sector(&t, &s, bambuf)) {
    t = 0;
    savederror = current_error;
    goto storedata;
  }

  buf->data[0] = t;
  buf->data[1] = s;

  /* Allocate it */
  allocate_sector(t,s,bambuf);
  if (write_bam(bambuf))
    return 1;

 storedata:
  /* Free BAM buffer - free will accept an already-freed buffer */
  free_buffer(bambuf);

  /* Store data in the already-reserved sector */
  if (image_write(buf->pvt.d64.drive, sector_offset(buf->pvt.d64.track,buf->pvt.d64.sector),
                  buf->data, 256, 1))
    return 1;

  buf->pvt.d64.track  = t;
  buf->pvt.d64.sector = s;
  buf->position  = 2;
  buf->lastused  = 1;
  buf->mustflush = 0;

  if (savederror) {
    set_error(savederror);
    return 1;
  } else
    return 0;
}

static uint8_t d64_write_cleanup(buffer_t *buf) {
  uint8_t t,s;

  buf->data[0] = 0;
  buf->data[1] = buf->lastused;

  t = buf->pvt.d64.track;
  s = buf->pvt.d64.sector;
  buf->pvt.d64.blocks++;

  /* Track=0 means that there was an error somewhere earlier */
  if (t == 0)
    return 1;

  /* Store data */
  if (image_write(buf->pvt.d64.drive, sector_offset(t,s), buf->data, 256, 1))
    return 1;

  /* Update directory entry */
  t = buf->pvt.d64.dh.track;
  s = buf->pvt.d64.dh.sector;
  if (image_read(buf->pvt.d64.drive, sector_offset(t,s)+32*buf->pvt.d64.dh.entry, entrybuf, 32))
    return 1;

  entrybuf[OFS_FILE_TYPE] |= FLAG_SPLAT;
  entrybuf[OFS_SIZE_LOW]   = buf->pvt.d64.blocks & 0xff;
  entrybuf[OFS_SIZE_HI]    = buf->pvt.d64.blocks >> 8;

  if (image_write(buf->pvt.d64.drive, sector_offset(t,s)+32*buf->pvt.d64.dh.entry, entrybuf, 32, 1))
    return 1;

  buf->cleanup = NULL;
  free_buffer(buf);

  return 0;
}


/* ------------------------------------------------------------------------- */
/*  fileops-API                                                              */
/* ------------------------------------------------------------------------- */

static uint8_t d64_opendir(dh_t *dh, path_t *path) {
  dh->drive = path->drive;
  dh->dir.d64.track  = DIR_TRACK;
  dh->dir.d64.sector = DIR_START_SECTOR;
  dh->dir.d64.entry  = 0;
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

  if ((dent->typeflags & TYPE_MASK) >= TYPE_DIR)
    /* Change invalid types (includes DIR for now) to DEL */
    dent->typeflags &= (uint8_t)~TYPE_MASK;

  dent->blocksize = entrybuf[OFS_SIZE_LOW] + 256*entrybuf[OFS_SIZE_HI];
  dent->remainder = 0xff;
  memcpy(dent->name, entrybuf+OFS_FILE_NAME, CBM_NAME_LENGTH);
  strnsubst(dent->name, 16, 0xa0, 0);
  dent->name[16] = 0;

  return 0;
}

static uint8_t d64_getlabel(path_t *path, uint8_t *label) {
  if (image_read(path->drive, sector_offset(DIR_TRACK,0) + LABEL_OFFSET, label, 16))
    return 1;

  strnsubst(label, 16, 0xa0, 0x20);
  return 0;
}

static uint8_t d64_getid(uint8_t drive, uint8_t *id) {
  if (image_read(drive, sector_offset(DIR_TRACK,0) + ID_OFFSET, id, 5))
    return 1;

  strnsubst(id, 5, 0xa0, 0x20);
  return 0;
}

static uint16_t d64_freeblocks(uint8_t drive) {
  uint16_t blocks = 0;
  uint8_t i;

  for (i=1;i<36;i++) {
    /* Skip directory track */
    if (i == 18)
      continue;
    if (image_read(drive, sector_offset(DIR_TRACK,0) + 4*i, entrybuf, 1))
      return 0;
    blocks += entrybuf[0];
  }

  return blocks;
}

static void d64_open_read(path_t *path, uint8_t *name, buffer_t *buf) {
  /* WARNING: Ugly hack used here. The directory entry is still in  */
  /*          entrybuf because of match_entry in fatops.c/file_open */
  buf->data[0] = entrybuf[OFS_TRACK];
  buf->data[1] = entrybuf[OFS_SECTOR];

  buf->pvt.d64.drive = path->drive;

  // FIXME: Check the file type

  buf->read    = 1;
  buf->refill  = d64_read;

  buf->refill(buf);
}

static void d64_open_write(path_t *path, uint8_t *name, uint8_t type, buffer_t *buf, uint8_t append) {
  dh_t dh;
  int8_t res;
  uint8_t t,s;
  uint8_t *ptr;
  buffer_t *bambuf;

  if (append) {
    /* Append case: Open the file and read the last sector */
    d64_open_read(path,name,buf);
    while (!current_error && buf->data[0])
      buf->refill(buf);

    if (current_error)
      return;

    /* Modify the buffer for writing */
    buf->pvt.d64.dh.track  = matchdh.dir.d64.track;
    buf->pvt.d64.dh.sector = matchdh.dir.d64.sector;
    buf->pvt.d64.dh.entry  = matchdh.dir.d64.entry-1;
    buf->pvt.d64.blocks    = entrybuf[OFS_SIZE_LOW] + 256*entrybuf[OFS_SIZE_HI]-1;
    buf->read       = 0;
    buf->position   = buf->lastused+1;
    if (buf->position == 0)
      buf->mustflush = 1;
    else
      buf->mustflush = 0;
    buf->refill     = d64_write;
    buf->cleanup    = d64_write_cleanup;
    mark_write_buffer(buf);

    return;
  }

  /* Non-append case */

  /* Search for an empty directotry entry */
  d64_opendir(&dh, path);
  do {
    res = nextdirentry(&dh);
    if (res > 0)
      return;
  } while (res == 0 && entrybuf[OFS_FILE_TYPE] != 0);

  /* Load the BAM */
  bambuf = read_bam(path->drive);
  if (!bambuf)
    return;

  /* Allocate a new directory sector if no empty entries were found */
  if (res < 0) {
    t = dh.dir.d64.track;
    s = dh.dir.d64.sector;

    if (get_next_sector(&dh.dir.d64.track, &dh.dir.d64.sector, bambuf)) {
      free_buffer(bambuf);
      return;
    }

    /* Link the old sector to the new */
    entrybuf[0] = dh.dir.d64.track;
    entrybuf[1] = dh.dir.d64.sector;
    if (image_write(path->drive, sector_offset(t,s), entrybuf, 2, 0)) {
      free_buffer(bambuf);
      return;
    }

    allocate_sector(dh.dir.d64.track, dh.dir.d64.sector, bambuf);

    /* Clear the new directory sector */
    memset(entrybuf, 0, 32);
    entrybuf[1] = 0xff;
    for (uint8_t i=0;i<256/32;i++) {
      if (image_write(path->drive, sector_offset(dh.dir.d64.track, dh.dir.d64.sector)+32*i,
                      entrybuf, 32, 0)) {
        free_buffer(bambuf);
        return;
      }
      entrybuf[1] = 0;
    }

    /* Mark full sector as used */
    entrybuf[1] = 0xff;
    dh.dir.d64.entry = 0;
  } else {
    /* Fix gcc 4.2 "uninitialized" warning */
    s = t = 0;
    /* nextdirentry has already incremented this variable, undo it */
    dh.dir.d64.entry--;
  }

  /* Create directory entry in entrybuf */
  memset(entrybuf+2, 0, sizeof(entrybuf)-2);  /* Don't overwrite the link pointer! */
  memset(entrybuf+OFS_FILE_NAME, 0xa0, CBM_NAME_LENGTH);
  ptr = entrybuf+OFS_FILE_NAME;
  while (*name) *ptr++ = *name++;
  entrybuf[OFS_FILE_TYPE] = type;

  /* Find a free sector and allocate it */
  if (get_first_sector(&t,&s,bambuf)) {
    free_buffer(bambuf);
    return;
  }
  entrybuf[OFS_TRACK]  = t;
  entrybuf[OFS_SECTOR] = s;
  allocate_sector(t,s,bambuf);
  if (write_bam(bambuf))
    return;

  /* Write the directory entry */
  if (image_write(path->drive, sector_offset(dh.dir.d64.track,dh.dir.d64.sector)+
                  dh.dir.d64.entry*32, entrybuf, 32, 1))
    return;

  /* Prepare the data buffer */
  mark_write_buffer(buf);
  buf->position       = 2;
  buf->lastused       = 2;
  buf->cleanup        = d64_write_cleanup;
  buf->refill         = d64_write;
  buf->data[2]        = 13; /* Verified on VICE */
  buf->pvt.d64.dh     = dh.dir.d64;
  buf->pvt.d64.drive  = path->drive;
  buf->pvt.d64.track  = t;
  buf->pvt.d64.sector = s;
}

static uint8_t d64_delete(path_t *path, uint8_t *name) {
  /* At this point entrybuf will contain the directory entry and    */
  /* matchdh will almost point to it (entry incremented in readdir) */
  buffer_t *buf;
  uint8_t linkbuf[2];

  /* Read BAM */
  buf = read_bam(path->drive);
  if (!buf)
    return 255;

  /* Free the sector chain in the BAM */
  linkbuf[0] = entrybuf[OFS_TRACK];
  linkbuf[1] = entrybuf[OFS_SECTOR];
  do {
    free_sector(linkbuf[0], linkbuf[1], buf);

    if (checked_read(path->drive, linkbuf[0], linkbuf[1], linkbuf, 2, ERROR_ILLEGAL_TS_LINK)) {
      free_buffer(buf);
      return 255;
    }
  } while (linkbuf[0]);

  /* Clear directory entry */
  entrybuf[OFS_FILE_TYPE] = 0;
  if (image_write(path->drive, sector_offset(matchdh.dir.d64.track,matchdh.dir.d64.sector)
                 +32*(matchdh.dir.d64.entry-1), entrybuf, 32, 1)) {
    free_buffer(buf);
    return 255;
  }

  /* Write new BAM */
  if (write_bam(buf))
    return 255;
  else
    return 1;
}

static void d64_read_sector(buffer_t *buf, uint8_t drive, uint8_t track, uint8_t sector) {
  checked_read(drive, track, sector, buf->data, 256, ERROR_ILLEGAL_TS_COMMAND);
}

static void d64_write_sector(buffer_t *buf, uint8_t drive, uint8_t track, uint8_t sector) {
  if (track < 1 || track > LAST_TRACK ||
      sector >= sectors_per_track(track)) {
    set_error_ts(ERROR_ILLEGAL_TS_COMMAND,track,sector);
  } else
    image_write(drive, sector_offset(track,sector), buf->data, 256, 1);
}

static void d64_rename(path_t *path, uint8_t *oldname, uint8_t *newname) {
  uint8_t *ptr;

  /* We're assuming that the caller has looked up the old name just   */
  /* before calling us  so entrybuf holds the correct directory entry */
  /* and matchdh has the track/sector/offset we need.                 */
  memset(entrybuf+OFS_FILE_NAME, 0xa0, CBM_NAME_LENGTH);
  ptr = entrybuf+OFS_FILE_NAME;
  while (*newname) *ptr++ = *newname++;

  image_write(path->drive,
              sector_offset(matchdh.dir.d64.track,matchdh.dir.d64.sector)+
              (matchdh.dir.d64.entry-1)*32, entrybuf, 32, 1);
}

const PROGMEM fileops_t d64ops = {
  d64_open_read,
  d64_open_write,
  d64_delete,
  d64_getlabel,
  d64_getid,
  d64_freeblocks,
  d64_read_sector,
  d64_write_sector,
  d64_opendir,
  d64_readdir,
  image_mkdir,
  image_chdir,
  d64_rename
};
