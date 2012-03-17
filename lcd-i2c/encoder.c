/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2012  Ingo Korb <ingo@akana.de>

   Inspired by MMC2IEC by Lars Pontoppidan et al.

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


   encoder.c: Encoder and button polling

*/

#include "config.h"
#include <avr/io.h>
#include <util/delay.h>
#include "timer.h"
#include "encoder.h"

volatile int8_t  encoder_position;
volatile uint8_t button_state;
volatile tick_t  last_button_change;

static uint8_t laststate;
static tick_t  debounce_timer;

/* Encoder gets its own on-change ISR, polling would require ~1000Hz */
ISR(ENCODER_INT_VECT) {
  uint8_t curstate;

  curstate = ENCODER_PIN & (ENCODER_A | ENCODER_B);

  // Encoder
  if ((laststate & ENCODER_A) ^ (curstate & ENCODER_A)) {
    if (!!(curstate & ENCODER_A) == !!(curstate & ENCODER_B)) {
      encoder_position--;
    } else {
      encoder_position++;
    }
    laststate = curstate;
  }
}

/* Polling is easier for debouncing a button and works fine at 100Hz */
void encoder_buttonisr(void) {
  uint8_t curstate;

  // Button
  curstate = !(ENCODER_PIN & ENCODER_BUTTON);
  if (curstate != button_state) {
    if (last_button_change != debounce_timer) {
      if (time_after(ticks,debounce_timer + HZ/20)) {
	last_button_change = ticks;
	debounce_timer = ticks;
	button_state = curstate;
      }
    } else {
      debounce_timer = ticks;
    }
  } else {
    // bounced - reset timer
    debounce_timer = last_button_change;
  }
}

void encoder_init(void) {
  ENCODER_DDR  &= (uint8_t)~(ENCODER_A | ENCODER_B | ENCODER_BUTTON);
  ENCODER_PORT |= ENCODER_A | ENCODER_B | ENCODER_BUTTON;
  _delay_ms(5); // long wires...
  laststate = ENCODER_PIN & (ENCODER_A | ENCODER_B);

  encoder_position = 0;
  button_state = !(ENCODER_PIN & ENCODER_BUTTON);
  last_button_change = 0;
  debounce_timer = 0;

  ENCODER_PCMSK |= ENCODER_A; /* No B! */
  ENCODER_INT_SETUP();
}

