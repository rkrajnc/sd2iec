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


   flags.h: Definitions for some global flags

*/

#ifndef FLAGS_H
#define FLAGS_H

#ifdef __AVR__
/* GPIOR0 is a bit-addressable register reserved for user data */
#  define globalflags (GPIOR0)
#else
/* Global flags, variable defined in doscmd.c */
extern uint8_t globalflags;
#endif

/* Values for those flags */
#define VC20MODE         (1<<0)
#define JIFFY_ENABLED    (1<<1)
#define AUTOSWAP_ACTIVE  (1<<2)
#define EXTENSION_HIDING (1<<3)
#define POSTMATCH        (1<<4)
#define FAT32_FREEBLOCKS (1<<5)

#endif
