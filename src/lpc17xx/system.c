/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2012  Ingo Korb <ingo@akana.de>

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


   system.c: System-specific initialisation (LPC17xx version)

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "system.h"

/* Early system initialisation */
void system_init_early(void) {
  __disable_irq();
  return;
}

/* Late initialisation, increase CPU clock */
void system_init_late(void) {
  /* Set flash accelerator to 5 CPU cycle access time */
  LPC_SC->FLASHCFG = (LPC_SC->FLASHCFG & 0xffff0fff) | (4 << 12);

  /* Enable main oscillator, range 1-20MHz */
  BITBAND(LPC_SC->SCS, 5) = 1;

  /* Wait until stable */
  while (!BITBAND(LPC_SC->SCS, 6)) ;

  /* Use main oscillator as system clock source */
  LPC_SC->CLKSRCSEL = 1;

  /* Set up PLL0 multiplier and pre-divisor */
  LPC_SC->PLL0CFG  = ((PLL_PREDIV-1) << 16) | (PLL_MULTIPLIER-1);
  LPC_SC->PLL0FEED = 0xaa;
  LPC_SC->PLL0FEED = 0x55;

  /* Enable PLL0 */
  LPC_SC->PLL0CON  = 1;
  LPC_SC->PLL0FEED = 0xaa;
  LPC_SC->PLL0FEED = 0x55;

  /* Increase CPU clock divider */
  LPC_SC->CCLKCFG = PLL_DIVISOR-1;

  /* Wait until PLL locks */
  while (!(LPC_SC->PLL0STAT & BV(26))) ;

  /* Connect PLL0 */
  LPC_SC->PLL0CON  = 3;
  LPC_SC->PLL0FEED = 0xaa;
  LPC_SC->PLL0FEED = 0x55;

  /* Enable GPIO interrupt */
  NVIC_EnableIRQ(EINT3_IRQn);

  // FIXME: Debugging
  //LPC_GPIO0->FIODIR |= BV(11) | BV(10);
  //LPC_GPIO2->FIODIR |= BV(0) | BV(1);
  //set_debugstate(0);
}

/* Put MCU in low-power mode */
void system_sleep(void) {
  __WFI();
}

/* Reset MCU */
void system_reset(void) {
  __disable_irq();

  /* force watchdog reset */
  LPC_WDT->WDTC = 256;            // minimal timeout
  LPC_WDT->WDCLKSEL = BV(31);     // internal RC, lock register
  LPC_WDT->WDMOD = BV(0) | BV(1); // enable watchdog and reset-by-watchdog
  LPC_WDT->WDFEED = 0xaa;
  LPC_WDT->WDFEED = 0x55;         // initial feed to really enable WDT

  while (1) ;
}

/* Disable interrupts */
void disable_interrupts(void) {
  __disable_irq();
}

/* Enable interrupts */
void enable_interrupts(void) {
  __enable_irq();
}

/*** GPIO interrupt demux ***/

/* Declare handler functions */
SD_CHANGE_HANDLER;
IEC_ATN_HANDLER;
IEC_CLOCK_HANDLER;

/* GPIO interrupt handler, shared with EINT3 */
void EINT3_IRQHandler(void) {
  if (BITBAND(LPC_GPIOINT->IO0IntStatF, SD_DETECT_PIN) ||
      BITBAND(LPC_GPIOINT->IO0IntStatR, SD_DETECT_PIN)) {
    BITBAND(LPC_GPIOINT->IO0IntClr, SD_DETECT_PIN) = 1;
    sdcard_change_handler();
  }

  if (BITBAND(LPC_GPIOINT->IO0IntStatF, IEC_PIN_ATN) ||
      BITBAND(LPC_GPIOINT->IO0IntStatR, IEC_PIN_ATN)) {
    BITBAND(LPC_GPIOINT->IO0IntClr, IEC_PIN_ATN) = 1;
    iec_atn_handler();
  }

#ifdef CONFIG_LOADER_DREAMLOAD
  if (BITBAND(LPC_GPIOINT->IO0IntStatF, IEC_PIN_CLOCK) ||
      BITBAND(LPC_GPIOINT->IO0IntStatR, IEC_PIN_CLOCK)) {
    BITBAND(LPC_GPIOINT->IO0IntClr, IEC_PIN_CLOCK) = 1;
    iec_clock_handler();
  }
#endif
}

void HardFault_Handler(void) {
  set_test_led(1);
  while (1) ;
}

void MemManage_Handler(void) {
  set_test_led(1);
  while (1) ;
}

void BusFault_Handler(void) {
  set_test_led(1);
  while (1);
}

void UsageFault_Handler(void) {
  set_test_led(1);
  while (1) ;
}
