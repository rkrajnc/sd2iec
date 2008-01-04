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

   
   iec.h: Definitions for the IEC handling code

*/

#ifndef IEC_H
#define IEC_H

/* Some fields don't need to be public, but this way saves a bit of ram */
typedef struct {
  int vc20mode:1;
  int eoi_recvd:1;
  int command_recvd:1;
  int jiffy_enabled:1;
  int jiffy_active:1;
  int jiffy_load:1;
} iecflags_t;

extern iecflags_t iecflags;

extern uint8_t device_address;

void init_iec(void);
void iec_mainloop(void);

#endif
