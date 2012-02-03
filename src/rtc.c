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


   rtc.c: FatFS support function

*/

#include <inttypes.h>
#include "config.h"
#include "progmem.h"
#include "time.h"
#include "rtc.h"

/* Default date/time if the RTC isn't present or not set: 1982-08-31 00:00:00 */
const PROGMEM struct tm rtc_default_date = {
  0, 0, 0, 31, 8-1, 82, 2
};

/* Return current time in a FAT-compatible format */
uint32_t get_fattime(void) {
  struct tm time;

  read_rtc(&time);
  return ((uint32_t)time.tm_year-80) << 25 |
    ((uint32_t)time.tm_mon+1) << 21 |
    ((uint32_t)time.tm_mday)  << 16 |
    ((uint32_t)time.tm_hour)  << 11 |
    ((uint32_t)time.tm_min)   << 5  |
    ((uint32_t)time.tm_sec)   >> 1;
}
