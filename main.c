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

   
   main.c: Lots of calls init calls for the submodules

*/

#include <stdio.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <avr/boot.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include "config.h"
#include "uart.h"
#include "spi.h"
#include "tff.h"
#include "iec.h"
#include "buffers.h"
#include "fatops.h"

#ifdef __AVR_ATmega32__
typedef struct
{
	unsigned long dev_id;
	unsigned short app_version;
	unsigned short crc;
} bootloaderinfo_t;
/* R.Riedel - bootloader-support */

const bootloaderinfo_t bootloaderinfo BOOTLOADER_SECTION = {DEVID, SWVERSIONMAJOR << 8 | SWVERSIONMINOR, 0x0000};
#endif

/* Make sure the watchdog is disabled as soon as possible */
/* Copy this code to your bootloader if you use one and your */
/* MCU doesn't disable the WDT after reset! */
void get_mcusr(void) \
      __attribute__((naked)) \
      __attribute__((section(".init3")));
void get_mcusr(void)
{
  MCUSR = 0;
  wdt_disable();
}

int main(void) {
#ifdef __AVR_ATmega644__
  // My test board uses a 16MHz xtal
  CLKPR = _BV(CLKPCE);
  CLKPR = 1;
#endif

  MCUCR |= _BV(JTD);
  MCUCR |= _BV(JTD);

  BUSY_LED_SETDDR();
  DIRTY_LED_SETDDR();

  BUSY_LED_ON();
  DIRTY_LED_OFF();

  init_serial();
  init_buffers();
  init_iec();
  spiInit();
  init_fatops();

  BUSY_LED_OFF();

  iec_mainloop();

  while (1);
}
