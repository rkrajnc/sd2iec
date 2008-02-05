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


   main.c: Lots of init calls for the submodules

*/

#include <stdio.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <avr/boot.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include "config.h"
#include "buffers.h"
#include "diskchange.h"
#include "eeprom.h"
#include "fatops.h"
#include "iec.h"
#include "sdcard.h"
#include "ff.h"
#include "uart.h"

#ifdef CONFIG_BOOTLOADER
typedef struct
{
	unsigned long dev_id;
	unsigned short app_version;
	unsigned short crc;
} bootloaderinfo_t;
/* R.Riedel - bootloader-support */

const bootloaderinfo_t bootloaderinfo BOOTLOADER_SECTION = {CONFIG_BOOT_DEVID, CONFIG_BOOT_MAJOR << 8 | CONFIG_BOOT_MINOR, 0x0000};
#endif

/* Make sure the watchdog is disabled as soon as possible    */
/* Copy this code to your bootloader if you use one and your */
/* MCU doesn't disable the WDT after reset!                  */
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
  asm volatile("in  r24, %0\n"
	       "ori r24, 0x80\n"
	       "out %0, r24\n"
	       "out %0, r24\n"
	       :
	       : "I" (_SFR_IO_ADDR(MCUCR))
	       : "r24"
	       );
#elif defined __AVR_ATmega32__
  asm volatile ("in  r24, %0\n"
		"ori r24, 0x80\n"
		"out %0, r24\n"
		"out %0, r24\n"
		:
		: "I" (_SFR_IO_ADDR(MCUCSR))
		: "r24"
		);
#elif defined __AVR_ATmega128__
  /* Just assume that JTAG doesn't hurt us on the m128 */
#else
#  error Unknown chip!
#endif

  BUSY_LED_SETDDR();
  DIRTY_LED_SETDDR();

  BUSY_LED_ON();
  DIRTY_LED_OFF();

  init_serial();
  init_buffers();
  init_iec();
  init_sdcard();
  init_fatops();

  init_change();

  read_configuration();

  BUSY_LED_OFF();

  iec_mainloop();

  while (1);
}
