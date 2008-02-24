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


   iec.h: Definitions for the IEC handling code

*/

#ifndef IEC_H
#define IEC_H

/**
 * struct iecflags_t - Bitfield of various flags, mostly IEC-related
 * @vc20mode       : Use VC20 timing on IEC bus
 * @eoi_recvd      : Received EOI with the last byte read
 * @command_recvd  : Command or filename received
 * @jiffy_enabled  : JiffyDOS support enabled
 * @jiffy_active   : JiffyDOS-capable master detected
 * @jiffy_load     : JiffyDOS LOAD operation detected
 * @autoswap_active: autoswap.lst in use
 *
 * This is a bitfield for a number of boolean variables used around the code.
 * autoswap_active is the only one not related to IEC stuff, but is still
 * included in here because it saves one byte of ram.
 */

#define VC20MODE        (1<<0)
#define EOI_RECVD       (1<<1)
#define COMMAND_RECVD   (1<<2)
#define JIFFY_ENABLED   (1<<3)
#define JIFFY_ACTIVE    (1<<4)
#define JIFFY_LOAD      (1<<5)
#define AUTOSWAP_ACTIVE (1<<6)

typedef struct iec_s {
  uint8_t iecflags;
  enum { BUS_IDLE = 0, BUS_ATNACTIVE, BUS_FOUNDATN, BUS_FORME, BUS_NOTFORME, BUS_ATNFINISH, BUS_ATNPROCESS, BUS_CLEANUP } bus_state;
  enum { DEVICE_IDLE = 0, DEVICE_LISTEN, DEVICE_TALK } device_state;
  uint8_t device_address;
  uint8_t secondary_address;
} iec_t;


extern iec_t iec_data;

void init_iec(void);
void iec_mainloop(void);

#endif
