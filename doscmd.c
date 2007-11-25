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
#include <ctype.h>
#include <stdlib.h>
#include <util/crc16.h>
#include "config.h"
#include "errormsg.h"
#include "doscmd.h"
#include "uart.h"
#include "fatops.h"
#include "sdcard.h"
#include "fastloader.h"
#include "iec.h"

#define CURSOR_RIGHT 0x1d

static void (*restart_call)(void) = 0;

uint8_t command_buffer[COMMAND_BUFFER_SIZE+1];
uint8_t command_length;

uint16_t datacrc = 0xffff;

static void handle_memexec(void) {
  uint16_t address;

  if (command_length < 5)
    return;

  if (detected_loader == FL_NONE) { 
    uart_puts_P(PSTR("M-E at "));
    uart_puthex(command_buffer[4]);
    uart_puthex(command_buffer[3]);
    uart_puts_P(PSTR(", CRC "));
    uart_puthex(datacrc >> 8);
    uart_puthex(datacrc & 0xff);
    uart_putcrlf();
  }
  datacrc = 0xffff;

  address = command_buffer[3] + (command_buffer[4]<<8);
  if (detected_loader == FL_TURBODISK && address == 0x0303) {
    /* Looks like Turbodisk */
    detected_loader = FL_NONE;
    load_turbodisk();
  }
}

static void handle_memread(void) {
  if (command_length < 6)
    return;

 // FIXME: M-R should return data even if it's just junk
}

static void handle_memwrite(void) {
  uint16_t address;
  uint8_t  length,i;

  if (command_length < 6)
    return;

  address = command_buffer[3] + (command_buffer[4]<<8);
  length  = command_buffer[5];

  if (address == 119) {
    /* Change device address, 1541 style */
    device_address = command_buffer[6] & 0x1f;
    return;
  }

  if (address == 0x1c06 || address == 0x1c07) {
    /* Ignore attempts to increase the VIA timer frequency */
    return;
  }

  /* Turbodisk sends the filename in the last M-W */
  if (datacrc == 0xe1cb) {
    detected_loader = FL_TURBODISK;
  } else {
    detected_loader = FL_NONE;
  }
  
  for (i=0;i<command_length;i++)
    datacrc = _crc16_update(datacrc, command_buffer[i]);

  if (detected_loader == FL_NONE) {
    uart_puts_P(PSTR("M-W CRC result: "));
    uart_puthex(datacrc >> 8);
    uart_puthex(datacrc & 0xff);
    uart_putcrlf();
  }
}


/* Parses CMD-style directory specification  */
/* Returns 1 if any errors found             */
/* in and out may point to the same address, */
/* the string will never increase in length  */
/* Warning: Don't call for random filenames that don't have a : ! */
uint8_t parse_path(char *in, char *out, uint8_t skipcolon) {
  /* Skip partition number */
  while (*in && isdigit(*in)) in++;
  if (!*in) return 1;

  /* Handle slashed path immediate after command */
  if (*in == '/') {
    /* Copy path before colon */
    while (*++in != ':' && (*out++ = *in));

    /* CD doesn't require a colon, let's extend that to everything */
    if (!*in)
      return 0;

    /* If there is a :, there must be a /: */
    if (*(in-1) != '/') return 1;
  }

  /* Skip the colon and abort if it's the last character */
  if (skipcolon && (*in++ != ':' || *in == 0)) return 1;
    
  /* Copy remaining string */
  while ((*out++ = *in++));

  return 0;
}


void parse_doscommand() {
  uint8_t i;

  /* Set default message: Everything ok */
  set_error(ERROR_OK,0,0);

  /* Abort if the command is too long */
  if (command_length == COMMAND_BUFFER_SIZE) {
    set_error(ERROR_SYNTAX_TOOLONG,0,0);
    return;
  }

#ifdef COMMAND_CHANNEL_DUMP
  /* Debugging aid: Dump the whole command via serial */
  if (detected_loader == FL_NONE) {
    /* Dump only if no loader was detected because it may ruin the timing */
    uart_flush();
    uart_putc('>');
    
    for (i=0;i<command_length;i++) {
      uart_puthex(command_buffer[i]);
      uart_putc(' ');
      if ((i & 0x0f) == 0x0f) {
	uart_putcrlf();
	uart_putc('>');
      }
      uart_flush();
    }
    uart_putcrlf();
  }
#endif

  /* Remove CRs at end of command */
  while (command_length > 0 && command_buffer[command_length-1] == 0x0d)
    command_length--;

  /* Abort if there is no command */
  if (command_length == 0) {
    set_error(ERROR_SYNTAX_UNABLE,0,0);
    return;
  }

  command_buffer[command_length] = 0;

  /* MD/CD/RD clash with other commands, so they're checked first */
  if (command_buffer[1] == 'D') {
    switch (command_buffer[0]) {
    case 'C':
    case 'M':
      i = command_buffer[0];
      if (parse_path((char *) command_buffer+2, (char *) command_buffer, 1)) {
	set_error(ERROR_SYNTAX_NONAME,0,0);
      } else {
	if (i == 'C') {
	  /* Left arrow moves one directory up */
	  if (command_buffer[0] == '_') {
	    command_buffer[0] = '.';
	    command_buffer[1] = '.';
	    command_buffer[2] = 0;
	  }
	  fat_chdir((char *)command_buffer);
	} else
	  fat_mkdir((char *)command_buffer);
      }
      break;

    case 'R':
      /* No deletion across subdirectories */
      for (i=0;command_buffer[i];i++) {
	if (command_buffer[i] == '/') {
	  /* Hack around a missing 2-level-break */
	  i = 255;
	  break;
	}
      }
      if (i == 255) {
	set_error(ERROR_SYNTAX_NONAME,0,0);
	break;
      }
	  
      /* Skip drive number */
      i = 2;
      while (isdigit(command_buffer[i])) i++;
      if (command_buffer[i] != ':') {
	set_error(ERROR_SYNTAX_NONAME,0,0);
      } else {
	i = fat_delete((char *)command_buffer+i+1);
	if (i != 255)
	  set_error(ERROR_SCRATCHED,i,0);
      }
      break;
      
    default:
      set_error(ERROR_SYNTAX_UNKNOWN,0,0);
      break;;
    }
    
    return;
  }

  switch (command_buffer[0]) {
  case 'I':
    /* Initialize is a no-op for now */
    if (card_state != CARD_OK)
      set_error(ERROR_READ_NOSYNC,18,0);
    break;

  case 'U':
    switch (command_buffer[1]) {
    case 'I':
    case '9':
      switch (command_buffer[2]) {
      case 0:
	/* Soft-reset - just return the dos version */
	set_error(ERROR_DOSVERSION,0,0);
	break;

      case '+':
	iecflags.vc20mode = 0;
	break;
	
      case '-':
	iecflags.vc20mode = 1;
	break;
	
      default:
	set_error(ERROR_SYNTAX_UNKNOWN,0,0);
	break;
      }
      break;

    case 'J':
    case ':':
      /* Reset - technically hard-reset */
      cli();
      restart_call();
      break;

    default:
      set_error(ERROR_SYNTAX_UNKNOWN,0,0);
      break;
    }
    break;

  case 'M':
    /* Memory-something - just dump for later analysis */
#ifndef COMMAND_CHANNEL_DUMP
    if (detected_loader == FL_NONE) {
      uart_flush();
      for (i=0;i<3;i++)
	uart_putc(command_buffer[i]);
      for (i=3;i<command_length;i++) {
	uart_putc(' ');
	uart_puthex(command_buffer[i]);
	uart_flush();
      }
      uart_putc(13);
      uart_putc(10);
    }
#endif

    if (command_buffer[2] == 'W')
      handle_memwrite();
    else if (command_buffer[2] == 'E')
      handle_memexec();
    else if (command_buffer[2] == 'R')
      handle_memread();
    else
      set_error(ERROR_SYNTAX_UNKNOWN,0,0);
    break;

  case 'S':
    /* Scratch */
    i = fat_delete((char *)command_buffer+2);
    if (i != 255)
      set_error(ERROR_SCRATCHED,i,0);
    break;

  case 'N':
    // FIXME: HACK! Sonst bleibt der 64'er Speed-Test mit Division by Zero stehen
    /* mkdir+chdir may be a nice substitute for FAT */
    _delay_ms(100);
    _delay_ms(100);
    _delay_ms(100);
    _delay_ms(100);
    _delay_ms(100);
    break;

  default:
    set_error(ERROR_SYNTAX_UNKNOWN,0,0);
    break;
  }
}
