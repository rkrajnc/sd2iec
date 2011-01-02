/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2011  Ingo Korb <ingo@akana.de>

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


   iec-bus.h: A few wrappers around the port definitions

*/

#ifndef IEC_BUS_H
#define IEC_BUS_H

/*** Output functions (generic versions) ***/

/* A little trick */
#ifdef IEC_OUTPUTS_INVERTED
#  define COND_INV(x) (!(x))
#else
#  define COND_INV(x) (x)
#endif

#ifndef IEC_OUTPUTFUNC_SPECIAL
static inline __attribute__((always_inline)) void set_atn(uint8_t state) {
  if (COND_INV(state))
    IEC_OUTPUT |= IEC_OBIT_ATN;
  else
    IEC_OUTPUT &= ~IEC_OBIT_ATN;
}

static inline __attribute__((always_inline)) void set_data(uint8_t state) {
  if (COND_INV(state))
    IEC_OUTPUT |= IEC_OBIT_DATA;
  else
    IEC_OUTPUT &= ~IEC_OBIT_DATA;
}

static inline __attribute__((always_inline)) void set_clock(uint8_t state) {
  if (COND_INV(state))
    IEC_OUTPUT |= IEC_OBIT_CLOCK;
  else
    IEC_OUTPUT &= ~IEC_OBIT_CLOCK;
}

static inline __attribute__((always_inline)) void set_srq(uint8_t state) {
  if (COND_INV(state))
    IEC_OUTPUT |= IEC_OBIT_SRQ;
  else
    IEC_OUTPUT &= ~IEC_OBIT_SRQ;
}

//FIXME: AVR only
# define toggle_srq()     IEC_INPUT |= IEC_OBIT_SRQ
#endif

#undef COND_INV

/*** Input definitions (generic versions) ***/
#ifndef IEC_ATN
#  define IEC_ATN   (IEC_INPUT & IEC_BIT_ATN)
#  define IEC_DATA  (IEC_INPUT & IEC_BIT_DATA)
#  define IEC_CLOCK (IEC_INPUT & IEC_BIT_CLOCK)
#  define IEC_SRQ   (IEC_INPUT & IEC_BIT_SRQ)
#endif

#ifdef IEC_INPUTS_INVERTED
static inline iec_bus_t iec_bus_read(void) {
  return (~IEC_INPUT) & (IEC_BIT_ATN | IEC_BIT_DATA | IEC_BIT_CLOCK | IEC_BIT_SRQ);
}
#else
static inline iec_bus_t iec_bus_read(void) {
  return IEC_INPUT & (IEC_BIT_ATN | IEC_BIT_DATA | IEC_BIT_CLOCK | IEC_BIT_SRQ);
}
#endif

void iec_interface_init(void);

#endif
