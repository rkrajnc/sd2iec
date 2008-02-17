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


   errormsg.c: Generates Commodore-compatible error messages

*/

#include <avr/pgmspace.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "iec.h"
#include "errormsg.h"

uint8_t current_error;
uint8_t error_buffer[CONFIG_ERROR_BUFFER_SIZE];
volatile uint8_t error_blink_active;

#define EC(x) x+0x80

/// Abbreviations used in the main error strings
static const prog_uint8_t abbrevs[] = {
  EC(0), 'F','I','L','E',
  EC(1), 'R','E','A','D',
  EC(2), 'W','R','I','T','E',
  EC(3), ' ','E','R','R','O','R',
  EC(4), ' ','N','O','T',' ',
  EC(5), 'D','I','S','K',' ',
  EC(6), 'O','P','E','N',
  EC(7), 'R','E','C','O','R','D',
  EC(127)
};

/// Error string table
static const prog_uint8_t messages[] = {
  EC(00),
    ' ','O','K',
  EC(01),
    0,'S',' ','S','C','R','A','T','C','H','E','D',
  EC(20), EC(21), EC(22), EC(23), EC(24), EC(27),
    1,3,
  EC(25), EC(28),
    2,3,
  EC(26),
    2,' ','P','R','O','T','E','C','T',' ','O','N',
  EC(29),
    5,'I','D',' ','M','I','S','M','A','T','C','H',
  EC(30), EC(31), EC(32), EC(33), EC(34),
    'S','Y','N','T','A','X',3,
  EC(39), EC(62),
    0,4,'F','O','U','N','D',
  EC(50),
    7,4,'P','R','E','S','E','N','T',
  EC(51),
    'O','V','E','R','F','L','O','W',' ','I','N',' ',7,
  EC(52),
    0,' ','T','O','O',' ','L','A','R','G','E',
  EC(60),
    2,' ',0,' ',6,
  EC(61),
    0,4,6,
  EC(63),
    0,' ','E','X','I','S','T','S',
  EC(64),
    0,' ','T','Y','P','E',' ','M','I','S','M','A','T','C','H',
  EC(65),
    'N','O',' ','B','L','O','C','K',
  EC(66), EC(67),
    'I','L','L','E','G','A','L',' ','T','R','A','C','K',' ','O','R',' ','S','E','C','T','O','R',
  EC(70),
    'N','O',' ','C','H','A','N','N','E','L',
  EC(71),
    'D','I','R',3,
  EC(72),
    5,'F','U','L','L',
  EC(73),
    'S','D','2','I','E','C',' ','V',
  EC(74),
    'D','R','I','V','E',4,1,'Y',
  EC(127)
};

/* Workaround for the make-challenged */
#ifndef VERSION
#  warning "VERSION not defined, using dummy"
#  define VERSION "X.X"
#endif

/// Version number string, will be added to message 73
static const prog_uint8_t versionstr[] = VERSION;

static char *appendmsg(char *msg, const prog_uint8_t *table, const uint8_t entry) {
  uint8_t i,tmp;

  i = 0;
  do {
    tmp = pgm_read_byte(table+i++);
    if (tmp == EC(entry) || tmp == EC(127))
      break;
  } while (1);

  if (tmp == EC(127)) {
    /* Unknown error */
    *msg++ = '?';
  } else {
    /* Skip over remaining error numbers */
    while (pgm_read_byte(table+i) >= EC(0)) i++;

    /* Copy error string to buffer */
    do {
      tmp = pgm_read_byte(table+i++);

      if (tmp < 32) {
	/* Abbreviation found, handle by recursion */
	msg = appendmsg(msg,abbrevs,tmp);
	continue;
      }

      if (tmp < EC(0))
	/* Don't copy error numbers */
	*msg++ = tmp;
    } while (tmp < EC(0));
  }

  return msg;
}

static char *appendnumber(char *msg, uint8_t value) {
  if (value >= 100) {
    *msg++ = '0' + value/100;
    value %= 100;
  }

  *msg++ = '0' + value/10;
  *msg++ = '0' + value%10;

  return msg;
}


void set_error(uint8_t errornum) {
  set_error_ts(errornum,0,0);
}

void set_error_ts(uint8_t errornum, uint8_t track, uint8_t sector) {
  char *msg = (char *) error_buffer;

  current_error = errornum;
  buffers[CONFIG_BUFFER_COUNT].lastused = 0;
  buffers[CONFIG_BUFFER_COUNT].position = 0;
  memset(error_buffer,0,sizeof(error_buffer));

  msg = appendnumber(msg,errornum);
  *msg++ = ',';

  if (errornum == ERROR_STATUS) {
    *msg++ = 'J';
    if (iecflags.jiffy_enabled)
      *msg++ = '+';
    else
      *msg++ = '-';
    *msg++ = ':';
    *msg++ = 'C';
    msg = appendnumber(msg, OSCCAL);
  } else {
    msg = appendmsg(msg,messages,errornum);
    if (errornum == ERROR_DOSVERSION) {
      /* Append the version string */
      uint8_t i = 0;
      while ((*msg++ = pgm_read_byte(versionstr+i++))) ;
      msg--;
    }
  }
  *msg++ = ',';

  msg = appendnumber(msg,track);
  *msg++ = ',';

  msg = appendnumber(msg,sector);
  *msg = 13;

  if (errornum >= 20 && errornum != ERROR_DOSVERSION) {
    // FIXME: Compare to E648
    // NOTE: 1571 doesn't write the BAM and closes some buffers if an error occured
    error_blink_active = 1;
  } else {
    error_blink_active = 0;
    if (active_buffers & 0xf0)
      DIRTY_LED_ON();
    else
      DIRTY_LED_OFF();
  }
  buffers[CONFIG_BUFFER_COUNT].lastused = msg - (char *)error_buffer;
}

/* Callback for the error channel buffer */
uint8_t set_ok_message(buffer_t *buf) {
  /* Reset data pointer */
  buf->data = error_buffer;
  set_error(0);
  return 0;
}
