/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2009  Ingo Korb <ingo@akana.de>

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


   config.h: User-configurable options to simplify hardware changes and/or
             reduce the code/ram requirements of the code.

   This is the display-side config.h.
*/

#ifndef CONFIG_H
#define CONFIG_H

#include "autoconf.h"

/* LCD pin assignments. The code assumes that the data lines are */
/* continuous and in ascending order and the control lines are   */
/* connected to the same port as the data lines.                 */
#define LCD_PORT         PORTA
#define LCD_PIN          PINA
#define LCD_DDR          DDRA

#define LCD_RS           _BV(PA0)
#define LCD_RW           _BV(PA1)
#define LCD_E1           _BV(PA2)

/* Number of bits to left-shift data nibbles,         */
/* e.g. 4 if the data lines are connected to PA4-PA7. */
#define LCD_DATA_SHIFT   4

/* E2 is used for dual-controller displays that split the displayed */
/* lines between two LCD controllers. Define to 0 for a single-     */
/* controller display.                                              */
#define LCD_E2           _BV(PA3)

/* Number of lines and columns of the display.              */
/* Set LCD_ROWS_BOTTOM to 0 for single-controller displays. */
#define LCD_COLUMNS     27
#define LCD_ROWS_TOP    2
#define LCD_ROWS_BOTTOM 2

/* Start address of the display lines in the controller ram. For dual- */
/* controller displays it is assumed that both use the same addresses. */
#define LCD_ROWADDR {0x00, 0x40}

/* Example for a single-controller 4 row display: */
//#define LCD_ROWADDR {0x00, 0x40, 0x14, 0x54}

/* Encoder port assignments */
#define ENCODER_PIN         PINC
#define ENCODER_PORT        PORTC
#define ENCODER_DDR         DDRC
#define ENCODER_PCMSK       PCMSK2
#define ENCODER_INT_VECT    PCINT2_vect
#define ENCODER_INT_SETUP() do { PCICR |= _BV(PCIE2); PCIFR |= _BV(PCIF2); } while (0)

/* Encoder pins - A is the line that toggles between "clicks", */
/*                B is the one that toggles at "clicks".       */
/* If in doubt: Try it, if the menu arrow "flickers" swap the lines */
#define ENCODER_A      _BV(PC2)
#define ENCODER_B      _BV(PC3)
#define ENCODER_BUTTON _BV(PC4)

/* Interrupt request line from the display to sd2iec */
#define INTRQ_PORT    PORTD
#define INTRQ_BIT     _BV(PD6)
#define INTRQ_SETUP() do { PORTD |= _BV(PD6); DDRD |= _BV(PD6); } while (0)

/* Scroll-timing */
#define TICKS_BEFORE_SCROLLING (1*HZ)
#define TICKS_PER_CHARACTER (HZ/5)

#endif
