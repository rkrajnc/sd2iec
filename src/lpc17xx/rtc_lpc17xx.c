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


   rtc_lpc17xx.c: RTC support for the LPC17xx internal RTC

*/

#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "config.h"
#include "rtc.h"

#define SIGNATURE_GPREG0 0xdeadbeef
#define SIGNATURE_GPREG1 0xfce2ea31

rtcstate_t rtc_state;

#define CLKEN  0
#define CTCRST 1

void rtc_init(void) {
  if (LPC_RTC->CCR & BV(CLKEN)) {
    /* Check for signature in battery-backed bytes to determine if RTC was set*/
    if (LPC_RTC->GPREG0 == SIGNATURE_GPREG0 &&
        LPC_RTC->GPREG1 == SIGNATURE_GPREG1) {
      rtc_state = RTC_OK;
    } else {
      rtc_state = RTC_INVALID;
    }
  } else {
    rtc_state = RTC_INVALID;
  }
}

void read_rtc(struct tm *time) {
  do {
    time->tm_sec  = LPC_RTC->SEC;
    time->tm_min  = LPC_RTC->MIN;
    time->tm_hour = LPC_RTC->HOUR;
    time->tm_mday = LPC_RTC->DOM;
    time->tm_mon  = LPC_RTC->MONTH;
    time->tm_year = LPC_RTC->YEAR - 1900;
    time->tm_wday = LPC_RTC->DOW;
  } while (time->tm_sec != LPC_RTC->SEC);
}

void set_rtc(struct tm *time) {
  LPC_RTC->CCR    = BV(CTCRST);
  LPC_RTC->SEC    = time->tm_sec;
  LPC_RTC->MIN    = time->tm_min;
  LPC_RTC->HOUR   = time->tm_hour;
  LPC_RTC->DOM    = time->tm_mday;
  LPC_RTC->MONTH  = time->tm_mon;
  LPC_RTC->YEAR   = time->tm_year + 1900;
  LPC_RTC->DOW    = time->tm_wday;
  LPC_RTC->CCR    = BV(CLKEN);
  LPC_RTC->GPREG0 = SIGNATURE_GPREG0;
  LPC_RTC->GPREG1 = SIGNATURE_GPREG1;
  rtc_state       = RTC_OK;
}

