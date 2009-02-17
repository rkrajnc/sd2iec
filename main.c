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


   main.c: Lots of init calls for the submodules

*/

#include <stdio.h>
#include <string.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include "config.h"
#include "buffers.h"
#include "diskchange.h"
#include "diskio.h"
#include "eeprom.h"
#include "errormsg.h"
#include "fatops.h"
#include "iec.h"
#include "ff.h"
#include "led.h"
#include "time.h"
#include "rtc.h"
#include "timer.h"
#include "uart.h"

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

#ifdef CONFIG_MEMPOISON
void poison_memory(void) \
  __attribute__((naked)) \
  __attribute__((section(".init1")));
void poison_memory(void) {
  register uint16_t i;
  register uint8_t  *ptr;

  asm("clr r1\n");
  /* There is no RAMSTARt variable =( */
  if (RAMEND > 2048 && RAMEND < 4096) {
    /* 2K memory */
    ptr = (void *)RAMEND-2047;
    for (i=0;i<2048;i++)
      ptr[i] = 0x55;
  } else if (RAMEND > 4096 && RAMEND < 8192) {
    /* 4K memory */
    ptr = (void *)RAMEND-4095;
    for (i=0;i<4096;i++)
      ptr[i] = 0x55;
  } else {
    /* Assume 8K memory */
    ptr = (void *)RAMEND-8191;
    for (i=0;i<8192;i++)
      ptr[i] = 0x55;
  }
}
#endif

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 1)
int main(void) __attribute__((OS_main));
#endif
int main(void) {
#if defined __AVR_ATmega644__ || defined __AVR_ATmega644P__ || defined __AVR_ATmega2561__
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
#elif defined __AVR_ATmega128__ || defined __AVR_ATmega1281__
  /* Just assume that JTAG doesn't hurt us on the m128 */
#else
#  error Unknown chip!
#endif

#ifdef CLOCK_PRESCALE
  clock_prescale_set(CLOCK_PRESCALE);
#endif

  BUSY_LED_SETDDR();
  DIRTY_LED_SETDDR();
  AUX_LED_SETDDR();

  AUX_LED_OFF();
  set_busy_led(1);
  set_dirty_led(0);

  init_serial();
  sei();
  init_buffers();
  init_timer();
  init_iec();
  init_rtc();
  init_disk();
  read_configuration();

  init_fatops(0);
  init_change();

  uart_puts_P(PSTR("\nsd2iec " VERSION " #"));
  uart_puthex(device_address);
  uart_putcrlf();

  set_busy_led(0);

  iec_mainloop();

  while (1);
}
