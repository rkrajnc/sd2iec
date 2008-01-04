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

   
   iec-ll.h: A few wrappers around the port definitions

*/

#ifndef IEC_LL_H
#define IEC_LL_H

#define set_atn(state)   do { if (state) IEC_DDR &= ~IEC_BIT_ATN;   else IEC_DDR |= IEC_BIT_ATN;   } while(0)
#define set_data(state)  do { if (state) IEC_DDR &= ~IEC_BIT_DATA;  else IEC_DDR |= IEC_BIT_DATA;  } while(0)
#define set_clock(state) do { if (state) IEC_DDR &= ~IEC_BIT_CLOCK; else IEC_DDR |= IEC_BIT_CLOCK; } while(0)
#define set_srq(state)   do { if (state) IEC_DDR &= ~IEC_BIT_SRQ;   else IEC_DDR |= IEC_BIT_SRQ;   } while(0)

#define toggle_srq() IEC_PIN |= IEC_BIT_SRQ

#define IEC_ATN   (IEC_PIN & IEC_BIT_ATN)
#define IEC_DATA  (IEC_PIN & IEC_BIT_DATA)
#define IEC_CLOCK (IEC_PIN & IEC_BIT_CLOCK)
#define IEC_SRQ   (IEC_PIN & IEC_BIT_SRQ)

#define set_atnack(state) do { if (state) TIMSK2 |= _BV(OCIE2A); else TIMSK2 &= ~_BV(OCIE2A); } while(0)

#endif
