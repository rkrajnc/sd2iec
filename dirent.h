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


   dirent.h: Various data structures for directory browsing

*/

#ifndef DIRENT_H
#define DIRENT_H

#include "ff.h"

#define CBM_NAME_LENGTH 16

#define TYPE_LENGTH 3
#define TYPE_MASK 7
#define EXT_TYPE_MASK 15

/* Standard file types */
#define TYPE_DEL 0
#define TYPE_SEQ 1
#define TYPE_PRG 2
#define TYPE_USR 3
#define TYPE_REL 4
#define TYPE_CBM 5
#define TYPE_DIR 6

/* Internal file types used for the partition directory */
#define TYPE_SYS 8
#define TYPE_NAT 9
#define TYPE_FAT 10

/* Internal file type used to force files without header on FAT */
#define TYPE_RAW 15

/// Hidden is an unused bit on CBM
#define FLAG_HIDDEN (1<<5)
#define FLAG_RO     (1<<6)
#define FLAG_SPLAT  (1<<7)

/**
 * struct path_t - struct to reference a directory
 * @part: partition number (0-based)
 * @fat : cluster number of the directory start
 *
 * This is just a wrapper around the FAT cluster number
 * until there is anything else that support subdirectories.
 */
typedef struct {
  uint8_t  part;
  uint32_t fat;
} path_t;

/**
 * struct cbmdirent - directory entry for CBM names
 * @blocksize : Size in blocks of 254 bytes
 * @remainder : (filesize MOD 254) or 0xff if unknown
 * @typeflags : OR of file type and flags
 * @fatcluster: Start cluster of the entry (if on FAT)
 * @name      : 0-padded commodore file name
 * @realname  : Actual 8.3 name of the file (if on FAT and different from name)
 * @year      : 1900-based year
 * @month     : month
 * @day       : day
 * @hour      : hour (24 hours, 0-based)
 * @minute    : minute
 * @second    : second
 *
 * This structure holds a CBM filename, its type and its size. The typeflags
 * are almost compatible to the file type byte in a D64 image, but the splat
 * bit is inverted. The name is padded with 0-bytes and will always be
 * zero-terminated. If it was read from a D64 file, it may contain valid
 * characters beyond the first 0-byte which should be displayed in the
 * directory, but ignored for name matching purposes.
 * realname is either an empty string or the actual ASCII 8.3 file name
 * of the file if the data in the name field was modified compared
 * to the on-disk name (e.g. extension hiding or x00).
 * year/month/day/hour/minute/second specify the time stamp of the file.
 * It is recommended to set it to 1982-08-31 00:00:00 if unknown because
 * this is currently the value used by FatFs for new files.
 */
struct cbmdirent {
  uint16_t blocksize;
  uint8_t  remainder;
  uint8_t  typeflags;
  uint32_t fatcluster;
  uint8_t  name[CBM_NAME_LENGTH+1];
  uint8_t  realname[8+3+1+1];
  uint8_t  year;
  uint8_t  month;
  uint8_t  day;
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  second;
};

/**
 * struct d64dh - D64 directory handle
 * @track : track of the current directory sector
 * @sector: sector of the current directory sector
 * @entry : number of the current directory entry in its sector
 *
 * This structure addresses an entry in a D64 directory by its track,
 * sector and entry (8 entries per sector).
 */
struct d64dh {
  uint8_t track;
  uint8_t sector;
  uint8_t entry;
};

/**
 * struct d64fh - D64 file handle
 * @dh    : d64dh pointing to the directory entry
 * @part  : partition
 * @track : current track
 * @sector: current sector
 * @blocks: number of sectors allocated before the current
 *
 * This structure holds the information required to write to a file
 * in a D64 image and update its directory entry upon close.
 */
typedef struct d64fh {
  struct d64dh dh;
  uint8_t part;
  uint8_t track;
  uint8_t sector;
  uint16_t blocks;
} d64fh_t;

/**
 * struct dh_t - union of all directory handles
 * @part: partition number for the handle
 * @fat : fat directory handle
 * @m2i : m2i directory handle (offset of entry in the file)
 * @d64 : d64 directory handle
 *
 * This is a union of directory handles for all supported file types
 * which is used as an opaque type to be passed between openddir and
 * readdir.
 */
typedef struct dh_s {
  uint8_t part;
  union {
    DIR fat;
    uint16_t m2i;
    struct d64dh d64;
  } dir;
} dh_t;

/**
 * struct partition_t - per-partition data
 * @fatfs      : FatFs per-drive/partition structure
 * @current_dir: current directory on FAT as seen by sd2iec
 * @fop        : pointer to the fileops structure for this partition
 * @imagehandle: file handle of a mounted image file on this partition
 * @imagetype  : disk image type mounted on this partition
 *
 * This data structure holds per-partition data.
 */
typedef struct partition_s {
  FATFS                  fatfs;
  uint32_t               current_dir;
  const struct fileops_s *fop;
  FIL                    imagehandle;
  uint8_t                imagetype;
} partition_t;

#endif
