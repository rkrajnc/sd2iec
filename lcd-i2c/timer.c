/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2009  Ingo Korb <ingo@akana.de>

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


   timer.c: System timer

*/

#include "config.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include "avrcompat.h"
#include "encoder.h"
#include "timer.h"

volatile tick_t ticks;

/* The main timer interrupt */
ISR(TIMER1_COMPA_vect) {
  ticks++;
  encoder_buttonisr();
}

void timer_init(void) {
  /* Count F_CPU/8 in timer 0 */
  TCCR0B = _BV(CS01);

  /* Set up a 100Hz interrupt using timer 1 */
  OCR1A  = 1250*((float)F_CPU/8000000.0) -1;
  TCNT1  = 0;
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS10) | _BV(CS11);
  TIMSK1 |= _BV(OCIE1A);
}
