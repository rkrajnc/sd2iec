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

   
   doscmd.c: Command channel parser

*/

#include <util/delay.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include "config.h"
#include "errormsg.h"
#include "doscmd.h"
#include "uart.h"
// FIXME: Don't do that. Implement fat_delete instead.
#include "tff.h"

uint8_t command_buffer[COMMAND_BUFFER_SIZE];
uint8_t command_length;

void parse_doscommand() {
  uint8_t i;

  /* FIXME: This parser sucks. */
  if (command_length == COMMAND_BUFFER_SIZE) {
    set_error(ERROR_SYNTAX_TOOLONG,0,0);
    return;
  }

  if (command_buffer[0] == 'U' && command_buffer[1] == 'J') {
    /* Reset the MCU via Watchdog */
    cli();
    wdt_enable(WDTO_15MS);
    while (1);
  } else if (command_buffer[0] == 'M') {
    /* Memory-something - dump the data for later analysis */
    uart_flush();
    for (i=0;i<3;i++)
      uart_putc(command_buffer[i]);
    for (i=3;i<command_length;i++) {
      uart_putc(' ');
      uart_puthex(command_buffer[i]);
      uart_flush(); /* Be nice to people with no ram... */
    }
    uart_putc(13);
    uart_putc(10);
  } else if (command_buffer[0] == 'E') {
    // Testing hack: Generate error messages
    // FIXME: Use a custom number parser that doesn't require \0
    command_buffer[command_length] = 0;
    i = atoi((char *)command_buffer+1);
    set_error(i,1,2);
  } else if (command_buffer[0] == 'S' && command_buffer[1] == ':') {
    // Testing hack II: Single file scratch
    command_buffer[command_length] = 0;
    f_unlink((char *)command_buffer+2);
    set_error(ERROR_SCRATCHED,1,0);
  } else if (command_buffer[0] == 'N') {
    // FIXME: HACK! Sonst bleibt der 64'er Speed-Test mit Division by Zero stehen
    /* mkdir+chdir may be a nice substitute for FAT */
    _delay_ms(100);
    _delay_ms(100);
    _delay_ms(100);
    _delay_ms(100);
    _delay_ms(100);
  } else {
    set_error(ERROR_SYNTAX_UNKNOWN,0,0);
  }
}
