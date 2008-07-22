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


   jiffy.c: Pin-agnostic JiffyDos implementation in C

   This file is not used when CONFIG_JIFFY_ASM is set to "y".

*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "config.h"
#include "avrcompat.h"
#include "iec-ll.h"
#include "fastloader-ll.h"
#include "timer.h"

/* Ugly hack. Use the assembler version instead. */
#define IEC_PULLUPS (IEC_PORT & (uint8_t)~(IEC_ATN|IEC_DATA|IEC_CLOCK|IEC_SRQ))

#define JIFFY_OFFSET_SEND 3
#define JIFFY_OFFSET_RECV 3

uint8_t jiffy_receive(uint8_t *busstate) {
  uint8_t data,tmp;

  data = 0;
  cli();

  /* Set clock+data high */
  set_clock(1);
  set_data(1);

  /* Wait until clock is high and emulate ATN-Ack */
  while (!IEC_CLOCK) {
    if (!IEC_ATN)
      set_data(0);
  }

  /* Wait for 13us from clock high (plus offset to center sampling window) */
  start_timeout(TIMEOUT_US(13 + JIFFY_OFFSET_RECV));
  while (!has_timed_out());

  /* Start the next timeout */
  start_timeout(TIMEOUT_US(13));

  /* Calculate data values */
  tmp = IEC_PIN;
  if (tmp & IEC_BIT_DATA)
    data |= _BV(5);
  if (tmp & IEC_BIT_CLOCK)
    data |= _BV(4);

  /* Wait for the timeout */
  while (!has_timed_out()) ;

  /* Bits 7+6 */
  start_timeout(TIMEOUT_US(11));

  tmp = IEC_PIN;
  if (tmp & IEC_BIT_DATA)
    data |= _BV(7);
  if (tmp & IEC_BIT_CLOCK)
    data |= _BV(6);

  while (!has_timed_out()) ;

  /* Bits 1+3 */
  start_timeout(TIMEOUT_US(13));

  tmp = IEC_PIN;
  if (tmp & IEC_BIT_DATA)
    data |= _BV(1);
  if (tmp & IEC_BIT_CLOCK)
    data |= _BV(3);

  while (!has_timed_out()) ;

  /* Bits 0+2 */
  start_timeout(TIMEOUT_US(13));

  tmp = IEC_PIN;
  if (tmp & IEC_BIT_DATA)
    data |= _BV(0);
  if (tmp & IEC_BIT_CLOCK)
    data |= _BV(2);

  while (!has_timed_out()) ;

  /* Read EOI mark */
  start_timeout(TIMEOUT_US(6));
  *busstate = IEC_PIN;

  while (!has_timed_out()) ;

  /* Data low */
  set_data(0);

  sei();

  return data ^ 0xff;
}


uint8_t jiffy_send(uint8_t value, uint8_t eoi, uint8_t loadflags) {
  uint8_t waitcond, eoimark, tmp;

  cli();
  if (loadflags)
    waitcond = IEC_BIT_ATN | IEC_BIT_CLOCK | IEC_BIT_DATA;
  else
    waitcond = IEC_BIT_ATN | IEC_BIT_CLOCK;

  loadflags &= 0x7f;

  if (eoi)
    eoimark = IEC_PULLUPS | IEC_OBIT_DATA;
  else
    eoimark = IEC_PULLUPS | IEC_OBIT_CLOCK;

  set_data(1);
  set_clock(1);
  _delay_us(1); // let the bus settle

  value ^= 0xff;

  while ((IEC_PIN & (IEC_BIT_ATN | IEC_BIT_CLOCK | IEC_BIT_DATA)) == waitcond) ;

  /* first bitpair */
  start_timeout(TIMEOUT_US(6 + JIFFY_OFFSET_SEND));

  /* Calculate next output value */
  tmp = IEC_PULLUPS;
  if (value & _BV(0))
    tmp |= IEC_OBIT_CLOCK;
  if (value & _BV(1))
    tmp |= IEC_OBIT_DATA;

  value >>= 2;

  /* Wait until the timeout is reached */
  while (!has_timed_out()) ;

  /* Send the data */
  IEC_OUT = tmp;

  /* 10 microseconds until the next transmission */
  start_timeout(TIMEOUT_US(10));

  /* For some reason gcc generates much smaller code here when using copy&paste. */
  /* A version using a for loop used ~640 bytes more flash than this.            */

  /* second bitpair */
  tmp = IEC_PULLUPS;
  if (value & _BV(0))
    tmp |= IEC_OBIT_CLOCK;
  if (value & _BV(1))
    tmp |= IEC_OBIT_DATA;

  value >>= 2;
  while (!has_timed_out()) ;
  IEC_OUT = tmp;

  start_timeout(TIMEOUT_US(11));

  /* third bitpair */
  tmp = IEC_PULLUPS;
  if (value & _BV(0))
    tmp |= IEC_OBIT_CLOCK;
  if (value & _BV(1))
    tmp |= IEC_OBIT_DATA;

  value >>= 2;
  while (!has_timed_out()) ;
  IEC_OUT = tmp;

  start_timeout(TIMEOUT_US(10));

  /* fourth bitpair */
  tmp = IEC_PULLUPS;
  if (value & _BV(0))
    tmp |= IEC_OBIT_CLOCK;
  if (value & _BV(1))
    tmp |= IEC_OBIT_DATA;

  while (!has_timed_out()) ;
  IEC_OUT = tmp;

  start_timeout(TIMEOUT_US(11));

  /* EOI marker */
  if (!loadflags) {
    while (!has_timed_out()) ;
    IEC_OUT = eoimark;
    _delay_us(1);
    /* Wait until Data and/or ATN are low */
    while ((IEC_PIN & (IEC_BIT_ATN | IEC_BIT_DATA)) == (IEC_BIT_ATN | IEC_BIT_DATA)) ;
    sei();
    return !IEC_ATN;
  }
  // FIXME: Check original roms
  sei();
  return 0;
}
