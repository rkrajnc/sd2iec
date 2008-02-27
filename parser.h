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


   parser.h: Definitions for the common file name parsers

*/

#ifndef PARSER_H
#define PARSER_H

#include "dirent.h"

extern path_t current_dir;

/* Returns the next matching dirent */
int8_t next_match(dh_t *dh, uint8_t *matchstr, uint8_t type, struct cbmdirent *dent);

/* Returns the first matching dirent */
int8_t first_match(path_t *path, uint8_t *matchstr, uint8_t type, struct cbmdirent *dent);

/* Parses CMD-style directory specifications */
uint8_t parse_path(uint8_t *in, path_t *path, uint8_t **name, uint8_t parse_always);

#endif
