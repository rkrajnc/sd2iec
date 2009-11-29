/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2009  Ingo Korb <ingo@akana.de>

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
#include "parser.h"
#include "wrapops.h"
#include "d64ops.h"

#define D41_ERROR_OFFSET 174848
#define D71_ERROR_OFFSET 349696

#define D41_BAM_TRACK           18
#define D41_BAM_SECTOR          0
#define D41_BAM_BYTES_PER_TRACK 4
#define D41_BAM_BITFIELD_BYTES  3

#define D81_BAM_TRACK           40
#define D81_BAM_SECTOR1         1
#define D81_BAM_SECTOR2         2
#define D81_BAM_OFFSET          10
#define D81_BAM_BYTES_PER_TRACK 6
#define D81_BAM_BITFIELD_BYTES  5

#define D71_BAM2_TRACK  53
#define D71_BAM2_SECTOR 0
#define D71_BAM2_BYTES_PER_TRACK 3
#define D71_BAM_COUNTER2OFFSET   0xdd

/* No errorinfo support for D81 */
#define MAX_SECTORS_PER_TRACK 21

#define D64_TYPE_MASK 3
#define D64_TYPE_NONE 0
#define D64_TYPE_D41  1
#define D64_TYPE_D71  2
#define D64_TYPE_D81  3
#define D64_TYPE_DNP  4

#define D64_HAS_ERRORINFO 128

typedef enum { BAM_BITFIELD, BAM_FREECOUNT } bamdata_t;

struct {
  uint8_t part;
  uint8_t track;
  uint8_t errors[MAX_SECTORS_PER_TRACK];
} errorcache;

buffer_t *bam_buffer;

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

static const PROGMEM struct param_s d41param = {
  18, 1, 35, 0x90, 0xa2, 10, 3
};

static const PROGMEM struct param_s d71param = {
  18, 1, 70, 0x90, 0xa2, 6, 3
};

static const PROGMEM struct param_s d81param = {
  40, 3, 80, 0x04, 0x16, 1, 1
};

/**
 * get_param - get a format-dependent parameter
 * @part : partition number
 * @param: parameter to retrieve
 *
 * Returns a parameter that is format-dependent for the image type mounted
 * on the specified partition.
 */
static uint8_t get_param(uint8_t part, param_t param) {
  return *(((uint8_t *)&partition[part].d64data)+param);
}

/**
 * sector_lba - Transform a track/sector pair into a LBA sector number
 * @part  : partition number
 * @track : Track number
 * @sector: Sector number
 *
 * Calculates an LBA-style sector number for a given track/sector pair.
 */
/* This version used the least code of all tested variants. */
static uint16_t sector_lba(uint8_t part, uint8_t track, const uint8_t sector) {
  uint16_t offset = 0;

  track--; /* Track numbers are 1-based */

  switch (partition[part].imagetype & D64_TYPE_MASK) {
  case D64_TYPE_D41:
  case D64_TYPE_D71:
  default:
    if (track >= 35) {
      offset = 683;
      track -= 35;
    }
    if (track < 17)
      return track*21 + sector + offset;
    if (track < 24)
      return 17*21 + (track-17)*19 + sector + offset;
    if (track < 30)
      return 17*21 + 7*19 + (track-24)*18 + sector + offset;
    return 17*21 + 7*19 + 6*18 + (track-30)*17 + sector + offset;

  case D64_TYPE_D81:
    return track*40 + sector;
  }
}

/**
 * sector_offset - Transform a track/sector pair into a D64 offset
 * @part  : partition number
 * @track : Track number
 * @sector: Sector number
 *
 * Calculates an offset into a D64 file from a track and sector number.
 */
static uint32_t sector_offset(uint8_t part, uint8_t track, const uint8_t sector) {
  return 256L * sector_lba(part,track,sector);
}

/**
 * sectors_per_track - number of sectors on given track
 * @part : partition number
 * @track: Track number
 *
 * This function returns the number of sectors on the given track
 * of a 1541/71/81 disk. Invalid track numbers will return invalid results.
 */
static uint8_t sectors_per_track(uint8_t part, uint8_t track) {
  switch (partition[part].imagetype & D64_TYPE_MASK) {
  case D64_TYPE_D41:
  case D64_TYPE_D71:
  default:
    if (track > 35)
      track -= 35;
    if (track < 18)
      return 21;
    if (track < 25)
      return 19;
    if (track < 31)
      return 18;
    return 17;

  case D64_TYPE_D81:
    return 40;
  }
}

/**
 * checked_read - read a specified sector after range-checking
 * @part  : partition number
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
static uint8_t checked_read(uint8_t part, uint8_t track, uint8_t sector, uint8_t *buf, uint16_t len, uint8_t error) {
  if (track < 1 || track > get_param(part, LAST_TRACK) ||
      sector >= sectors_per_track(part, track)) {
    set_error_ts(error,track,sector);
    return 2;
  }

  if (partition[part].imagetype & D64_HAS_ERRORINFO) {
    /* Check if the sector is marked as bad */
    if (errorcache.part != part || errorcache.track != track) {
      /* Read the error info for this track */
      memset(errorcache.errors, 1, sizeof(errorcache.errors));
      /* Needs fix for errorinfo on anything but D64/D71! */
      if ((partition[part].imagetype & D64_TYPE_MASK) == D64_TYPE_D41) {
        if (image_read(part, D41_ERROR_OFFSET+sector_lba(part,track,0),
                       errorcache.errors, sectors_per_track(part, track)) >= 2)
          return 2;
      } else {
        if (image_read(part, D71_ERROR_OFFSET+sector_lba(part,track,0),
                       errorcache.errors, sectors_per_track(part, track)) >= 2)
          return 2;
      }
      errorcache.part  = part;
      errorcache.track = track;
    }

    /* Calculate error message from the code */
    if (errorcache.errors[sector] >= 2 && errorcache.errors[sector] <= 11) {
      /* Most codes can be mapped directly */
      set_error_ts(errorcache.errors[sector]-2+20,track,sector);
      return 2;
    }
    if (errorcache.errors[sector] == 15) {
      /* Drive not ready */
      set_error(74);
      return 2;
    }
    /* 1 is OK, unknown values are accepted too */
  }

  return image_read(part, sector_offset(part,track,sector), buf, len);
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
 * d64_bam_flush - write BAM buffer to disk
 * @buf: pointer to the BAM buffer
 *
 * This function writes the contents of the BAM buffer to the disk image.
 * Returns 0 if successful, != 0 otherwise.
 */
static uint8_t d64_bam_flush(buffer_t *buf) {
  uint8_t res;

  if (buf->mustflush && buf->pvt.bam.part < max_part) {
    res = image_write(buf->pvt.bam.part,
                      sector_offset(buf->pvt.bam.part,
                                    buf->pvt.bam.track,
                                    buf->pvt.bam.sector),
                      buf->data, 256, 1);
    buf->mustflush = 0;
    return res;
  } else
    return 0;
}

/**
 * move_bam_window - read correct BAM sector into window.
 * @part  : partition
 * @track : track number
 * @type  : type of pointer requested
 * @ptr   : pointer to track information in BAM sector
 *
 * This function reads the correct BAM sector into memory for the requested
 * track, flushing an existing BAM sector to disk if needed.  It also
 * calculates the correct pointer into the BAM sector for the appropriate
 * track.  Since the BAM contains both sector counts and sector allocation
 * bitmaps, type is used to signal which reference is desired.
 * Returns 0 if successful, != 0 otherwise.
 */
static uint8_t move_bam_window(uint8_t part, uint8_t track, bamdata_t type, uint8_t **ptr) {
  uint8_t res;
  uint8_t t,s, pos;

  switch(partition[part].imagetype & D64_TYPE_MASK) {
    case D64_TYPE_D41:
    default:
      t   = D41_BAM_TRACK;
      s   = D41_BAM_SECTOR;
      pos = D41_BAM_BYTES_PER_TRACK * track + (type == BAM_BITFIELD ? 1:0);
      break;

    case D64_TYPE_D71:
      if (track > 35 && type == BAM_BITFIELD) {
        t   = D71_BAM2_TRACK;
        s   = D71_BAM2_SECTOR;
        pos = (track - 36) * D71_BAM2_BYTES_PER_TRACK;
      } else {
        t = D41_BAM_TRACK;
        s = D41_BAM_SECTOR;
        if (track > 35) {
          pos = (track - 36) + D71_BAM_COUNTER2OFFSET;
        } else {
          pos = D41_BAM_BYTES_PER_TRACK * track + (type == BAM_BITFIELD ? 1:0);
        }
      }
      break;

    case D64_TYPE_D81:
      t   = D81_BAM_TRACK;
      s   = (track < 41 ? D81_BAM_SECTOR1 : D81_BAM_SECTOR2);
      if (track > 40)
        track -= 40;
      pos = D81_BAM_OFFSET + track * D81_BAM_BYTES_PER_TRACK + (type == BAM_BITFIELD ? 1:0);
      break;
/*
    case D64_TYPE_DNP:
      t   = DNP_BAM_TRACK;
      s   = DNP_BAM_SECTOR + 1 + (track >> 3);
      pos = (track & 0x07) * 32;
      break;
*/
    }

    if (bam_buffer->pvt.bam.part != part
        || bam_buffer->pvt.bam.track != t
        || bam_buffer->pvt.bam.sector != s) {
      /* Need to read the BAM sector */
      if((res = bam_buffer->cleanup(bam_buffer)))
        return res;

      res = image_read(part, sector_offset(part, t, s), bam_buffer->data, 256);
      if(res)
        return res;

      bam_buffer->pvt.bam.part   = part;
      bam_buffer->pvt.bam.track  = t;
      bam_buffer->pvt.bam.sector = s;
    }

    *ptr = bam_buffer->data + pos;
    return 0;
}

/**
 * is_free - checks if the given sector is marked as free
 * @part  : partition
 * @track : track number
 * @sector: sector number
 *
 * This function checks if the given sector is marked as free in
 * the BAM of drive "part". Returns 0 if allocated, >0 if free, <0 on error.
 */
static int8_t is_free(uint8_t part, uint8_t track, uint8_t sector) {
  uint8_t res;
  uint8_t *ptr = NULL;

  res = move_bam_window(part,track,BAM_BITFIELD,&ptr);
  if(res)
    return -1;

  return (ptr[sector>>3] & (1<<(sector&7))) != 0;
}

/**
 * sectors_free - returns the number of free sectors on a given track
 * @part  : partition
 * @track : track number
 *
 * This function returns the number of free sectors on the given track
 * of partition part.
 */
static uint8_t sectors_free(uint8_t part, uint8_t track) {
  uint8_t *trackmap = NULL;
  //uint8_t i, b;
  //uint8_t blocks = 0;

  if (track < 1 || track > get_param(part, LAST_TRACK))
    return 0;

  switch (partition[part].imagetype & D64_TYPE_MASK) {
/*
  case D64_TYPE_DNP:
    if(move_bam_window(part,track,BAM_FREECOUNT,&trackmap))
      return 0;
    for(i=0;i < BAM_BITFIELD_BYTES_PER_TRACK;i++) {
      // From http://everything2.com/title/counting%25201%2520bits
      b = (trackmap[i] & 0x55) + (trackmap[i]>>1 & 0x55);
      b = (b & 0x33) + (b >> 2 & 0x33);
      b = (b & 0x0f) + (b >> 4 & 0x0f);
      blocks += b;
    }
    return blocks;
*/

  case D64_TYPE_D71:
  case D64_TYPE_D81:
  case D64_TYPE_D41:
  default:
    if(move_bam_window(part,track,BAM_FREECOUNT,&trackmap))
      return 0;
    return *trackmap;
  }
}

/**
 * allocate_sector - mark a sector as used
 * @part  : partitoin
 * @track : track number
 * @sector: sector number
 *
 * This function marks the given sector as used in the BAM of drive part.
 * If the sector was already marked as used nothing is changed.
 * Returns 0 if successful, 1 on error.
 */
static uint8_t allocate_sector(uint8_t part, uint8_t track, uint8_t sector) {
  uint8_t *trackmap;
  int8_t res = is_free(part,track,sector);

  if (res < 0)
    return 1;

  if (res != 0) {
    /* do the bitfield first, since is_free already set it up for us. */
    if(move_bam_window(part,track,BAM_BITFIELD,&trackmap))
      return 1;

    trackmap[sector>>3] &= (uint8_t)~(1<<(sector&7));
    bam_buffer->mustflush = 1;

    if(move_bam_window(part,track,BAM_FREECOUNT,&trackmap))
      return 1;
    if (trackmap[0] > 0) {
      trackmap[0]--;
      bam_buffer->mustflush = 1;
    }
  }
  return 0;
}

/**
 * free_sector - mark a sector as free
 * @part  : partition
 * @track : track number
 * @sector: sector number
 *
 * This function marks the given sector as free in the BAM of partition
 * part. If the sector was already marked as free nothing is changed.
 * Returns 0 if successful, 1 on error.
 */
static uint8_t free_sector(uint8_t part, uint8_t track, uint8_t sector) {
  uint8_t *trackmap;
  int8_t res = is_free(part,track,sector);

  if (res < 0)
    return 1;

  if (res == 0) {
    /* do the bitfield first, since is_free already set it up for us. */
    if(move_bam_window(part,track,BAM_BITFIELD,&trackmap))
      return 1;

    trackmap[sector>>3] |= 1<<(sector&7);
    bam_buffer->mustflush = 1;

    if(move_bam_window(part,track,BAM_FREECOUNT,&trackmap))
      return 1;

    if(trackmap[0] < sectors_per_track(part, track)) {
      trackmap[0]++;
      bam_buffer->mustflush = 1;
    }
  }
  return 0;
}

/**
 * get_first_sector - calculate the first sector for a new file
 * @part  : partition
 * @track : pointer to a variable holding the track
 * @sector: pointer to a variable holding the sector
 *
 * This function calculates the first sector to be allocated for a new
 * file. The algorithm is based on the description found at
 * http://ist.uwaterloo.ca/~schepers/formats/DISK.TXT
 * Returns 0 if successful or 1 if any error occured.
 *
 * This code will not skip track 53 of a D71 if there are any free
 * sectors on it - this behaviour is consistent with that of a 1571
 * with a revision 3.0 ROM.
 */
static uint8_t get_first_sector(uint8_t part, uint8_t *track, uint8_t *sector) {
  int8_t distance = 1;

  /* Look for a track with free sectors close to the directory */
  while (distance < get_param(part, LAST_TRACK)) {
    if (sectors_free(part, get_param(part, DIR_TRACK)-distance))
      break;

    /* Invert sign */
    distance = -distance;

    /* Increase distance every second try */
    if (distance > 0)
      distance++;
  }

  if (distance == get_param(part, LAST_TRACK)) {
    if (current_error == ERROR_OK)
      set_error(ERROR_DISK_FULL);
    return 1;
  }

  /* Search for the first free sector on this track */
  *track = get_param(part, DIR_TRACK)-distance;
  for (*sector = 0;*sector < sectors_per_track(part, *track); *sector += 1)
    if (is_free(part, *track, *sector) > 0)
      return 0;

  /* If we're here the BAM is invalid or couldn't be read */
  if (current_error == ERROR_OK)
    set_error(ERROR_DISK_FULL);
  return 1;
}

/**
 * get_next_sector - calculate the next sector for a file
 * @part  : partition
 * @track : pointer to a variable holding the track
 * @sector: pointer to a variable holding the sector
 *
 * This function calculates the next sector to be allocated for a file
 * based on the current sector in the variables pointed to by track/sector.
 * The algorithm is based on the description found at
 * http://ist.uwaterloo.ca/~schepers/formats/DISK.TXT
 * Returns 0 if successful or 1 if any error occured.
 */
static uint8_t get_next_sector(uint8_t part, uint8_t *track, uint8_t *sector) {
  uint8_t interleave,tries;

  if (*track == get_param(part, DIR_TRACK)) {
    if (sectors_free(part, get_param(part, DIR_TRACK)) == 0) {
      if (current_error == ERROR_OK)
        set_error(ERROR_DISK_FULL);
      return 1;
    }
    interleave = get_param(part, DIR_INTERLEAVE);
  } else
    interleave = get_param(part, FILE_INTERLEAVE);

  /* Look for a track with free sectors */
  tries = 0;
  while (tries < 3 && !sectors_free(part, *track)) {
    /* No more space on current track, try another */
    if (*track < get_param(part, DIR_TRACK))
      *track -= 1;
    else {
      *track += 1;
      /* Skip track 53 on D71 images */
      if ((partition[part].imagetype & D64_TYPE_MASK) == D64_TYPE_D71 &&
          *track == D71_BAM2_TRACK)
        *track += 1;
    }

    if (*track < 1) {
      *track = get_param(part, DIR_TRACK) + 1;
      *sector = 0;
      tries++;
    }
    if (*track > get_param(part, LAST_TRACK)) {
      *track = get_param(part, DIR_TRACK) - 1;
      *sector = 0;
      tries++;
    }
  }

  if (tries == 3) {
    if (current_error == ERROR_OK)
      set_error(ERROR_DISK_FULL);
    return 1;
  }

  /* Look for a sector at interleave distance */
  *sector += interleave;
  if (*sector >= sectors_per_track(part, *track)) {
    *sector -= sectors_per_track(part, *track);
    if (*sector != 0)
      *sector -= 1;
  }

  /* Increase distance until an empty sector is found */
  tries = 99;
  while (is_free(part,*track,*sector) <= 0 && tries--) {
    *sector += 1;
    if (*sector >= sectors_per_track(part, *track))
      *sector = 0;
  }

  if (tries)
    return 0;

  if (current_error == ERROR_OK)
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
    if (checked_read(dh->part, dh->dir.d64.track, dh->dir.d64.sector, entrybuf, 2, ERROR_ILLEGAL_TS_LINK))
      return 1;

    /* Final directory sector? */
    if (entrybuf[0] == 0)
      return -1;

    dh->dir.d64.track  = entrybuf[0];
    dh->dir.d64.sector = entrybuf[1];
    dh->dir.d64.entry  = 0;
  }

  if (dh->dir.d64.track < 1 || dh->dir.d64.track > get_param(dh->part, LAST_TRACK) ||
      dh->dir.d64.sector >= sectors_per_track(dh->part, dh->dir.d64.track)) {
    set_error_ts(ERROR_ILLEGAL_TS_LINK,dh->dir.d64.track,dh->dir.d64.sector);
    return 1;
  }

  if (image_read(dh->part, sector_offset(dh->part, dh->dir.d64.track, dh->dir.d64.sector)+
                 dh->dir.d64.entry*32, entrybuf, 32))
    return 1;

  dh->dir.d64.entry++;

  return 0;
}

/**
 * d64_read - refill-callback used for reading
 * @buf: target buffer
 *
 * This is the callback used as refill for files opened for reading.
 */
static uint8_t d64_read(buffer_t *buf) {
  /* Store the current sector, used for append */
  buf->pvt.d64.track  = buf->data[0];
  buf->pvt.d64.sector = buf->data[1];

  if (checked_read(buf->pvt.d64.part, buf->data[0], buf->data[1], buf->data, 256, ERROR_ILLEGAL_TS_LINK)) {
    free_buffer(buf);
    return 1;
  }

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

/**
 * d64_seek - seek-callback
 * @buf     : target buffer
 * @position: offset to seek to
 * @index   : offset within the record to seek to
 *
 * This is the function used as the seek callback. Since seeking
 * isn't supported for D64 files it just sets an error message
 * and returns 1.
 */
static uint8_t d64_seek(buffer_t *buf, uint32_t position, uint8_t index) {
  set_error(ERROR_SYNTAX_UNABLE);
  return 1;
}

/**
 * d64_write - refill-callback used for writing
 * @buf: target buffer
 *
 * This is the callback used as refill for files opened for writing.
 */
static uint8_t d64_write(buffer_t *buf) {
  uint8_t t,s,savederror;

  savederror = 0;
  t = buf->pvt.d64.track;
  s = buf->pvt.d64.sector;

  buf->pvt.d64.blocks++;

  /* Mark as last sector in case something below fails */
  buf->data[0] = 0;
  buf->data[1] = buf->lastused;

  /* Find another free sector */
  if (get_next_sector(buf->pvt.d64.part, &t, &s)) {
    t = 0;
    savederror = current_error;
    goto storedata;
  }

  buf->data[0] = t;
  buf->data[1] = s;

  /* Allocate it */
  if (allocate_sector(buf->pvt.d64.part, t, s)) {
    free_buffer(buf);
    return 1;
  }
  if (bam_buffer->cleanup(bam_buffer)) {
    free_buffer(buf);
    return 1;
  }

 storedata:
  /* Store data in the already-reserved sector */
  if (image_write(buf->pvt.d64.part,
                  sector_offset(buf->pvt.d64.part,
                                buf->pvt.d64.track,
                                buf->pvt.d64.sector),
                  buf->data, 256, 1)) {
    free_buffer(buf);
    return 1;
  }

  buf->pvt.d64.track  = t;
  buf->pvt.d64.sector = s;
  buf->position  = 2;
  buf->lastused  = 1;
  buf->mustflush = 0;
  mark_buffer_clean(buf);

  if (savederror) {
    set_error(savederror);
    free_buffer(buf);
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
  if (image_write(buf->pvt.d64.part, sector_offset(buf->pvt.d64.part,t,s), buf->data, 256, 1))
    return 1;

  /* Update directory entry */
  t = buf->pvt.d64.dh.track;
  s = buf->pvt.d64.dh.sector;
  if (image_read(buf->pvt.d64.part, sector_offset(buf->pvt.d64.part,t,s)+32*buf->pvt.d64.dh.entry, entrybuf, 32))
    return 1;

  entrybuf[DIR_OFS_FILE_TYPE] |= FLAG_SPLAT;
  entrybuf[DIR_OFS_SIZE_LOW]   = buf->pvt.d64.blocks & 0xff;
  entrybuf[DIR_OFS_SIZE_HI]    = buf->pvt.d64.blocks >> 8;

  if (image_write(buf->pvt.d64.part, sector_offset(buf->pvt.d64.part,t,s)+32*buf->pvt.d64.dh.entry, entrybuf, 32, 1))
    return 1;

  buf->cleanup = callback_dummy;
  free_buffer(buf);

  return 0;
}


/* ------------------------------------------------------------------------- */
/*  fileops-API                                                              */
/* ------------------------------------------------------------------------- */

uint8_t d64_mount(uint8_t part) {
  uint8_t imagetype;
  uint32_t fsize = partition[part].imagehandle.fsize;

  switch (fsize) {
  case 174848:
    imagetype = D64_TYPE_D41;
    memcpy_P(&partition[part].d64data, &d41param, sizeof(struct param_s));
    break;

  case 175531:
    imagetype = D64_TYPE_D41 | D64_HAS_ERRORINFO;
    memcpy_P(&partition[part].d64data, &d41param, sizeof(struct param_s));
    break;

  case 349696:
    imagetype = D64_TYPE_D71;
    memcpy_P(&partition[part].d64data, &d71param, sizeof(struct param_s));
    break;

  case 351062:
    imagetype = D64_TYPE_D71 | D64_HAS_ERRORINFO;
    memcpy_P(&partition[part].d64data, &d71param, sizeof(struct param_s));
    break;

  case 819200:
    imagetype = D64_TYPE_D81;
    memcpy_P(&partition[part].d64data, &d81param, sizeof(struct param_s));
    break;

    /* Warning: If you enable this, increase the MAX_SECTORS_PER_TRACK #define
                and the offset calculation in checked_read!
  case 822400:
    imagetype = D64_TYPE_D81 | D64_HAS_ERRORINFO;
    memcpy_P(&partition[part].d64data, &d81param, sizeof(struct param_s));
    break;
    */

  default:
    set_error(ERROR_IMAGE_INVALID);
    return 1;
  }

  partition[part].imagetype = imagetype;

  /* Allocate a BAM buffer if required */
  if (bam_buffer == NULL) {
    bam_buffer = alloc_system_buffer();
    if (!bam_buffer)
      return 1;

    bam_buffer->secondary    = BUFFER_SYS_BAM;
    bam_buffer->pvt.bam.part = 255;
    bam_buffer->cleanup      = d64_bam_flush;
    stick_buffer(bam_buffer);
  }

  bam_buffer->pvt.bam.refcount++;

  if (imagetype & D64_HAS_ERRORINFO)
    /* Invalidate error cache */
    errorcache.part = 255;

  return 0;
}

static uint8_t d64_opendir(dh_t *dh, path_t *path) {
  dh->part = path->part;
  dh->dir.d64.track  = get_param(path->part, DIR_TRACK);
  dh->dir.d64.sector = get_param(path->part, DIR_START_SECTOR);
  dh->dir.d64.entry  = 0;
  return 0;
}

static int8_t d64_readdir(dh_t *dh, struct cbmdirent *dent) {
  int8_t res;

  do {
    res = nextdirentry(dh);
    if (res)
      return res;

    if (entrybuf[DIR_OFS_FILE_TYPE] != 0)
      break;
  } while (1);

  memset(dent, 0, sizeof(struct cbmdirent));

  dent->typeflags = entrybuf[DIR_OFS_FILE_TYPE] ^ FLAG_SPLAT;

  if ((dent->typeflags & TYPE_MASK) >= TYPE_DIR)
    /* Change invalid types (includes DIR for now) to DEL */
    dent->typeflags &= (uint8_t)~TYPE_MASK;

  dent->blocksize = entrybuf[DIR_OFS_SIZE_LOW] + 256*entrybuf[DIR_OFS_SIZE_HI];
  dent->remainder = 0xff;
  memcpy(dent->name, entrybuf+DIR_OFS_FILE_NAME, CBM_NAME_LENGTH);
  strnsubst(dent->name, 16, 0xa0, 0);

  /* Fake Date/Time */
  dent->date.year  = 82;
  dent->date.month = 8;
  dent->date.day   = 31;

  return 0;
}

static uint8_t d64_getlabel(path_t *path, uint8_t *label) {
  if (image_read(path->part,
                 sector_offset(path->part, get_param(path->part, DIR_TRACK),0)
                 + get_param(path->part, LABEL_OFFSET),
                 label, 16))
    return 1;

  strnsubst(label, 16, 0xa0, 0x20);
  return 0;
}

static uint8_t d64_getid(uint8_t part, uint8_t *id) {
  if (image_read(part,
                 sector_offset(part, get_param(part, DIR_TRACK),0)
                 + get_param(part, ID_OFFSET),
                 id, 5))
    return 1;

  strnsubst(id, 5, 0xa0, 0x20);
  return 0;
}

static uint16_t d64_freeblocks(uint8_t part) {
  uint16_t blocks = 0;
  uint8_t i;

  for (i=1;i<=get_param(part, LAST_TRACK);i++) {
    /* Skip directory track */
    switch (partition[part].imagetype & D64_TYPE_MASK) {
    case D64_TYPE_D41:
    case D64_TYPE_D71:
    default:
      if (i == D41_BAM_TRACK || i == D71_BAM2_TRACK)
        continue; // continue the for loop
      break;      // break out of the switch

    case D64_TYPE_D81:
      if (i == D81_BAM_TRACK)
        continue;
      break;
    }

    blocks += sectors_free(part,i);
  }

  return blocks;
}

static void d64_open_read(path_t *path, struct cbmdirent *dent, buffer_t *buf) {
  /* WARNING: Ugly hack used here. The directory entry is still in  */
  /*          entrybuf because of match_entry in fatops.c/file_open */
  buf->data[0] = entrybuf[DIR_OFS_TRACK];
  buf->data[1] = entrybuf[DIR_OFS_SECTOR];

  buf->pvt.d64.part = path->part;

  buf->read    = 1;
  buf->refill  = d64_read;
  buf->seek    = d64_seek;
  stick_buffer(buf);

  buf->refill(buf);
}

static void d64_open_write(path_t *path, struct cbmdirent *dent, uint8_t type, buffer_t *buf, uint8_t append) {
  dh_t dh;
  int8_t res;
  uint8_t t,s;
  uint8_t *ptr;

  /* Check for read-only image file */
  if (!(partition[path->part].imagehandle.flag & FA_WRITE)) {
    set_error(ERROR_WRITE_PROTECT);
    return;
  }

  if (append) {
    /* Append case: Open the file and read the last sector */
    d64_open_read(path, dent, buf);
    while (!current_error && buf->data[0])
      buf->refill(buf);

    if (current_error)
      return;

    /* Modify the buffer for writing */
    buf->pvt.d64.dh.track  = matchdh.dir.d64.track;
    buf->pvt.d64.dh.sector = matchdh.dir.d64.sector;
    buf->pvt.d64.dh.entry  = matchdh.dir.d64.entry-1;
    buf->pvt.d64.blocks    = entrybuf[DIR_OFS_SIZE_LOW] + 256*entrybuf[DIR_OFS_SIZE_HI]-1;
    buf->read       = 0;
    buf->position   = buf->lastused+1;
    if (buf->position == 0)
      buf->mustflush = 1;
    else
      buf->mustflush = 0;
    buf->refill     = d64_write;
    buf->cleanup    = d64_write_cleanup;
    buf->seek       = d64_seek;
    mark_write_buffer(buf);
    stick_buffer(buf);

    return;
  }

  /* Non-append case */

  /* Search for an empty directotry entry */
  d64_opendir(&dh, path);
  do {
    res = nextdirentry(&dh);
    if (res > 0)
      return;
  } while (res == 0 && entrybuf[DIR_OFS_FILE_TYPE] != 0);

  /* Allocate a new directory sector if no empty entries were found */
  if (res < 0) {
    t = dh.dir.d64.track;
    s = dh.dir.d64.sector;

    if (get_next_sector(path->part, &dh.dir.d64.track, &dh.dir.d64.sector))
      return;

    /* Link the old sector to the new */
    entrybuf[0] = dh.dir.d64.track;
    entrybuf[1] = dh.dir.d64.sector;
    if (image_write(path->part, sector_offset(path->part,t,s), entrybuf, 2, 0))
      return;

    if (allocate_sector(path->part, dh.dir.d64.track, dh.dir.d64.sector))
      return;

    /* Clear the new directory sector */
    memset(entrybuf, 0, 32);
    entrybuf[1] = 0xff;
    for (uint8_t i=0;i<256/32;i++) {
      if (image_write(path->part,
                      sector_offset(path->part,
                                    dh.dir.d64.track,
                                    dh.dir.d64.sector)+32*i,
                      entrybuf, 32, 0))
        return;

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
  uint8_t *name = dent->name;
  memset(entrybuf+2, 0, sizeof(entrybuf)-2);  /* Don't overwrite the link pointer! */
  memset(entrybuf+DIR_OFS_FILE_NAME, 0xa0, CBM_NAME_LENGTH);
  ptr = entrybuf+DIR_OFS_FILE_NAME;
  while (*name) *ptr++ = *name++;
  entrybuf[DIR_OFS_FILE_TYPE] = type;

  /* Find a free sector and allocate it */
  if (get_first_sector(path->part,&t,&s))
    return;

  entrybuf[DIR_OFS_TRACK]  = t;
  entrybuf[DIR_OFS_SECTOR] = s;

  if (allocate_sector(path->part,t,s))
    return;

  if (bam_buffer->cleanup(bam_buffer))
    return;

  /* Write the directory entry */
  if (image_write(path->part, sector_offset(path->part, dh.dir.d64.track,dh.dir.d64.sector)+
                  dh.dir.d64.entry*32, entrybuf, 32, 1))
    return;

  /* Prepare the data buffer */
  mark_write_buffer(buf);
  buf->position       = 2;
  buf->lastused       = 2;
  buf->cleanup        = d64_write_cleanup;
  buf->refill         = d64_write;
  buf->seek           = d64_seek;
  buf->data[2]        = 13; /* Verified on VICE */
  buf->pvt.d64.dh     = dh.dir.d64;
  buf->pvt.d64.part   = path->part;
  buf->pvt.d64.track  = t;
  buf->pvt.d64.sector = s;
  stick_buffer(buf);
}

static void d64_open_rel(path_t *path, struct cbmdirent *dent, buffer_t *buf, uint8_t length, uint8_t mode) {
  set_error(ERROR_SYNTAX_UNABLE);
}

static uint8_t d64_delete(path_t *path, struct cbmdirent *dent) {
  /* At this point entrybuf will contain the directory entry and    */
  /* matchdh will almost point to it (entry incremented in readdir) */
  uint8_t linkbuf[2];

  /* Free the sector chain in the BAM */
  linkbuf[0] = entrybuf[DIR_OFS_TRACK];
  linkbuf[1] = entrybuf[DIR_OFS_SECTOR];
  do {
    free_sector(path->part, linkbuf[0], linkbuf[1]);

    if (checked_read(path->part, linkbuf[0], linkbuf[1], linkbuf, 2, ERROR_ILLEGAL_TS_LINK))
      return 255;
  } while (linkbuf[0]);

  /* Clear directory entry */
  entrybuf[DIR_OFS_FILE_TYPE] = 0;
  if (image_write(path->part, sector_offset(path->part,matchdh.dir.d64.track,matchdh.dir.d64.sector)
                 +32*(matchdh.dir.d64.entry-1), entrybuf, 32, 1))
    return 255;

  /* Write new BAM */
  if (bam_buffer->cleanup(bam_buffer))
    return 255;
  else
    return 1;
}

static void d64_read_sector(buffer_t *buf, uint8_t part, uint8_t track, uint8_t sector) {
  checked_read(part, track, sector, buf->data, 256, ERROR_ILLEGAL_TS_COMMAND);
}

static void d64_write_sector(buffer_t *buf, uint8_t part, uint8_t track, uint8_t sector) {
  if (track < 1 || track > get_param(part, LAST_TRACK) ||
      sector >= sectors_per_track(part, track)) {
    set_error_ts(ERROR_ILLEGAL_TS_COMMAND,track,sector);
  } else
    image_write(part, sector_offset(part,track,sector), buf->data, 256, 1);
}

static void d64_rename(path_t *path, struct cbmdirent *dent, uint8_t *newname) {
  uint8_t *ptr;

  /* We're assuming that the caller has looked up the old name just   */
  /* before calling us  so entrybuf holds the correct directory entry */
  /* and matchdh has the track/sector/offset we need.                 */
  memset(entrybuf+DIR_OFS_FILE_NAME, 0xa0, CBM_NAME_LENGTH);
  ptr = entrybuf+DIR_OFS_FILE_NAME;
  while (*newname) *ptr++ = *newname++;

  image_write(path->part,
              sector_offset(path->part,matchdh.dir.d64.track,matchdh.dir.d64.sector)+
              (matchdh.dir.d64.entry-1)*32, entrybuf, 32, 1);
}

static void d64_format(uint8_t part, uint8_t *name, uint8_t *id) {
  buffer_t *buf;
  uint8_t  *ptr;
  uint8_t  idbuf[2];
  uint8_t  t,s;

  /* Limit to D64 until fixed */
  if (partition[part].imagetype != D64_TYPE_D41) {
    set_error(ERROR_SYNTAX_UNABLE);
    return;
  }

  buf = alloc_buffer();
  if (buf == NULL)
    return;

  mark_write_buffer(buf);
  memset(buf->data, 0, 256);

  /* Flush BAM buffer and mark its contents as invalid */
  bam_buffer->cleanup(bam_buffer);
  bam_buffer->pvt.bam.part = 0xff;

  if (id != NULL) {
    uint16_t i;

    /* Clear the data area of the disk image */
    for (i=0;i<683 /* FIXME: TOTAL_SECTORS */ ;i++)
      if (image_write(part, 256L * i, buf->data, 256, 0))
        return;

    /* Copy the new ID into the buffer */
    idbuf[0] = id[0];
    idbuf[1] = id[1];
  } else {
    /* Read the old ID into the buffer */
    if (image_read(part, sector_offset(part, get_param(part, DIR_TRACK),0) + get_param(part, ID_OFFSET), idbuf, 2))
      return;
  }

  /* Clear the first directory sector */
  buf->data[1] = 0xff;
  if (image_write(part,
                  sector_offset(part,
                                get_param(part, DIR_TRACK),
                                get_param(part, DIR_START_SECTOR)),
                  buf->data, 256, 0))
    return;

  /* Mark all sectors as free */
  for (t=1; t<=get_param(part, LAST_TRACK); t++)
    for (s=0; s<sectors_per_track(part, t); s++)
      if (t != 18 || s > 1)
        free_sector(part,t,s);

  ptr = bam_buffer->data;
  ptr[0] = get_param(part, DIR_TRACK);
  ptr[1] = get_param(part, DIR_START_SECTOR);
  ptr[2] = 0x41;
  memset(ptr+0x90, 0xa0, 0xaa-0x90+1);

  /* Copy the disk label */
  ptr += 0x90;
  t = 16;
  while (*name && t--)
    *ptr++ = *name++;

  /* Set the ID */
  bam_buffer->data[get_param(part, ID_OFFSET)  ] = idbuf[0];
  bam_buffer->data[get_param(part, ID_OFFSET)+1] = idbuf[1];
  bam_buffer->data[get_param(part, ID_OFFSET)+3] = '2';
  bam_buffer->data[get_param(part, ID_OFFSET)+4] = 'A';

  /* Write the new BAM - mustflush is set because free_sector was used */
  bam_buffer->cleanup(bam_buffer);

  /* FIXME: Clear the error info block */
}

/**
 * d64_raw_directory - open directory track as file
 * @buf: target buffer
 *
 * This function opens the directory track as a file on the
 * buffer passed in buf. Used when $ is opened on a secondary
 * address > 0.
 */
void d64_raw_directory(path_t *path, buffer_t *buf) {
  /* Let's pile one hack on the other and use d64_open_read */
  entrybuf[DIR_OFS_TRACK]  = get_param(path->part, DIR_TRACK);
  entrybuf[DIR_OFS_SECTOR] = 0;

  d64_open_read(path, NULL, buf);
}

const PROGMEM fileops_t d64ops = {
  d64_open_read,
  d64_open_write,
  d64_open_rel,
  d64_delete,
  d64_getlabel,
  d64_getid,
  d64_freeblocks,
  d64_read_sector,
  d64_write_sector,
  d64_format,
  d64_opendir,
  d64_readdir,
  image_mkdir,
  image_chdir,
  d64_rename
};
