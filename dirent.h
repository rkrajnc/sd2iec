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

   
   dirent.h: Various data structures for directory browsing

*/

#ifndef DIRENT_H
#define DIRENT_H

#include "tff.h"

#define CBM_NAME_LENGTH 16

#define TYPE_LENGTH 3
#define TYPE_MASK 7
#define TYPE_DEL 0
#define TYPE_SEQ 1
#define TYPE_PRG 2
#define TYPE_USR 3
#define TYPE_REL 4
#define TYPE_CBM 5
#define TYPE_DIR 6

/// Hidden is an unused bit on CBM
#define FLAG_HIDDEN (1<<5)
#define FLAG_RO     (1<<6)
#define FLAG_SPLAT  (1<<7)

/**
 * struct cbmdirent - directory entry for CBM names
 * @blocksize: Size in blocks of 254 bytes
 * @remainder: (filesize MOD 254) or 0xff if unknown
 * @typeflags: OR of file type and flags
 * @name     : 0xa0-padded commodore file name
 *
 * This structure holds a CBM filename, its type and its size. The typeflags
 * are almost compatible to the file type byte in a D64 image, but the splat
 * bit is inverted. The name is padded with 0xa0, but holds an extra byte
 * in case it has to be converted to a zero-terminated C string.
 */
struct cbmdirent {
  uint16_t blocksize;
  uint8_t  remainder;
  uint8_t  typeflags;
  uint8_t  name[CBM_NAME_LENGTH+1];
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
 * union dh_t - union of all directory handles
 * @fat: tff directory handle
 * @m2i: m2i directory handle (offset of entry in the file)
 * @d64: d64 directory handle
 *
 * This is a union of directory handles for all supported file types
 * which is used as an opaque type to be passed between openddir and
 * readdir.
 */
typedef union dh_u {
  DIR fat;
  uint16_t m2i;
  struct d64dh d64;
} dh_t;

#endif

