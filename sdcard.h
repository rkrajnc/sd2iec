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

   
   sdcard.h: Definitions for the SD/MMC access routines

   Based on original code by Lars Pontoppidan et al., see sdcard.c
   for full copyright details.

*/

#ifndef SDCARD_H
#define SDCARD_H

// functions

//! Initialize the card and prepare it for use.
/// Returns zero if successful.
char sdReset(void);

//! Read 512-byte sector from card to buffer
/// Returns zero if successful.
char sdRead(uint32_t sector, uint8_t *buffer);

//! Write 512-byte sector from buffer to card
/// Returns zero if successful.
char sdWrite(uint32_t sector, uint8_t *buffer);


extern uint8_t sdCardOK;

#endif
