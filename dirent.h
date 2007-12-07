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

/* Hidden is an unused bit on CBM */
#define FLAG_HIDDEN (1<<5)
#define FLAG_RO     (1<<6)
#define FLAG_SPLAT  (1<<7)

struct cbmdirent {
  uint16_t blocksize;               /* file size in blocks      */
  uint8_t  typeflags;               /* OR of filetype and flags */
  uint8_t  name[CBM_NAME_LENGTH+1]; /* padded with 0xa0         */
};

/* Generic directory handle */
typedef union {
  DIR fat;
  uint16_t m2i;
} dh_t;

#endif

