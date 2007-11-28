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

   
   fatops.h: Definitions for the FAT operations

*/

#ifndef FATOPS_H
#define FATOPS_H

#include "buffers.h"
#include "fileops.h"

void     init_fatops(void);
uint8_t  fat_delete(char *path, char *filename);
void     fat_chdir(char *dirname);
void     fat_mkdir(char *dirname);
void     fat_open_read(char *path, char *filename, buffer_t *buf);
void     fat_open_write(char *path, char *filename, buffer_t *buf, uint8_t append);
uint8_t  fat_getlabel(char *label);
uint8_t  fat_getid(char *id);
uint16_t fat_freeblocks(void);
uint8_t  fat_opendir(DIR *dh, char *dir);
int8_t   fat_readdir(DIR *dh, struct cbmdirent *dent);

#endif
