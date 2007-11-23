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

   
   fileops.h: Definitions for file operations

*/

#ifndef FILEOPS_H
#define FILEOPS_H

#include "buffers.h"

#define CBM_NAME_LENGTH 16

#define TYPE_LENGTH 3
#define TYPE_DEL 0
#define TYPE_SEQ 1
#define TYPE_PRG 2
#define TYPE_USR 3
#define TYPE_REL 4
#define TYPE_CBM 5
#define TYPE_DIR 6

#define FLAG_RO    (1<<6)
#define FLAG_SPLAT (1<<7)

enum open_modes { OPEN_READ, OPEN_WRITE, OPEN_APPEND, OPEN_MODIFY };

struct cbmdirent {
  uint16_t blocksize;             /* file size in blocks      */
  uint8_t  typeflags;             /* OR of filetype and flags */
  char     name[CBM_NAME_LENGTH]; /* padded with 0xa0         */
};
  
uint8_t generic_cleanup(buffer_t *buf);

/* Parses a filename in command_buffer and opens that file */
void file_open(uint8_t secondary);

#endif
