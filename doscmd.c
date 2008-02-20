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


   doscmd.c: Command channel parser

*/

#include <util/delay.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <util/crc16.h>
#include "config.h"
#include "diskchange.h"
#include "eeprom.h"
#include "errormsg.h"
#include "fastloader.h"
#include "fatops.h"
#include "iec.h"
#include "m2iops.h"
#include "sdcard.h"
#include "uart.h"
#include "wrapops.h"
#include "doscmd.h"

#define CURSOR_RIGHT 0x1d

static void (*restart_call)(void) = 0;

uint8_t command_buffer[CONFIG_COMMAND_BUFFER_SIZE+2];
uint8_t command_length;

uint16_t datacrc = 0xffff;

#ifdef CONFIG_STACK_TRACKING
uint16_t minstack = RAMEND;

void __cyg_profile_func_enter (void *this_fn, void *call_site) __attribute__((no_instrument_function));
void __cyg_profile_func_exit  (void *this_fn, void *call_site) __attribute__((alias("__cyg_profile_func_enter")));

void __cyg_profile_func_enter (void *this_fn, void *call_site) {
  if (SP < minstack) minstack = SP;
}
#endif

/* ------------------------------------------------------------------------- */
/*  Parsing helpers                                                          */
/* ------------------------------------------------------------------------- */

/**
 * parse_path - parse CMD style directory specification
 * @in  : input buffer
 * @out : output buffer
 * @name: pointer to pointer to filename (may be NULL)
 *
 * This function parses a CMD style directory specification in a file name
 * and copies both path and filename to the output buffer, seperated by \0.
 * Both buffers may point to the same address, but must be able to hold one
 * character more than strlen(in)+1. If non-NULL, *name will point to the
 * beginning of the filename in the output buffer.
 */
void parse_path(char *in, char *out, char **name) {
  if (strchr(in, ':')) {
    uint8_t state = 0;

    /* Skip partition number */
    while (*in && isdigit(*in)) in++;

    /* Unoptimized DFA matcher             */
    /* I wonder if this can be simplified? */
    while (state != 5) {
      switch (state) {
      case 0: /* Starting state */
	switch (*in++) {
	case ':':
	  *out++ = 0;
	  state = 5;
	  break;

	case '/':
	  state = 1;
	  break;

	default:
	  state = 2;
	  break;
	}
	break;

      case 1: /* Initial slash found */
	if (*in == ':') {
	  *out++ = 0;
	  in++;
	  state = 5;
	} else {
	  *out++ = *in++;
	  state = 3;
	}
	break;

      case 2: /* Initial non-slash found */
	while (*in++ != ':');
	state = 5;
	break;

      case 3: /* Slash-noncolon found */
	switch (*in) {
	case ':':
	  *out++ = 0;
	  in++;
	  state = 5;
	  break;

	case '/':
	  in++;
	  state = 4;
	  break;

	default:
	  *out++ = *in++;
	  break;
	}
	break;

      case 4: /* Slash-noncolon-slash found */
	if (*in == ':') {
	  *out++ = 0;
	  in++;
	  state = 5;
	} else {
	  *out++ = '/';
	  *out++ = *in++;
	  state = 3;
	}
	break;
      }
    }
  } else {
    /* No colon in name, add a terminator for the path */
    if (in == out) {
      /* Make some space for the new colon */
      memmove(in+1,in,strlen(in)+1);
      in++;
    }
    *out++ = 0;
  }

  if (name)
    *name = out;

  /* Copy remaining string */
  while ((*out++ = *in++));

  return;
}

/* Parse a decimal number at str and return a pointer to the following char */
static uint8_t parse_number(char **str) {
  uint8_t res = 0;

  /* Skip leading spaces */
  while (**str == ' ') (*str)++;

  /* Parse decimal number */
  while (isdigit(**str)) {
    res *= 10;
    res += (*(*str)++) - '0';
  }

  return res;
}

/* Parse parameters of block commands in the command buffer */
/* Returns number of parameters (up to 4) or <0 on error    */
static int8_t parse_blockparam(uint8_t values[]) {
  uint8_t paramcount = 0;
  char *str;

  str = strchr((char *) command_buffer, ':');
  if (!str) {
    if (strlen((char *) command_buffer) < 3)
      return -1;
    str = (char *)command_buffer + 2;
  }

  str++;

  while (*str && paramcount < 4) {
    /* Skip all spaces, cursor-rights and commas - CC7C */
    while (*str && (*str == ' ' || *str == 0x1d || *str == ',')) str++;
    if (!*str)
      return -1;

    values[paramcount++] = parse_number(&str);
  }

  return paramcount;
}

/* ------------------------------------------------------------------------- */
/*  Command handlers                                                         */
/* ------------------------------------------------------------------------- */

static void handle_eeread(uint16_t address, uint8_t length) {
  if (length > CONFIG_ERROR_BUFFER_SIZE) {
    set_error(ERROR_SYNTAX_TOOLONG);
    return;
  }

  buffers[CONFIG_BUFFER_COUNT].position = 0;
  buffers[CONFIG_BUFFER_COUNT].lastused = length-1;

  uint8_t *ptr = error_buffer;
  while (length--)
    *ptr++ = eeprom_read_byte((uint8_t *)(CONFIG_EEPROM_OFFSET + address++));
}

static void handle_eewrite(uint16_t address, uint8_t length) {
  uint8_t *ptr = command_buffer+6;
  while (length--)
    eeprom_write_byte((uint8_t *)(CONFIG_EEPROM_OFFSET + address++), *ptr++);
}

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
#ifdef CONFIG_TURBODISK
  if (detected_loader == FL_TURBODISK && address == 0x0303) {
    /* Looks like Turbodisk */
    detected_loader = FL_NONE;
    load_turbodisk();
  }
#endif
}

static void handle_memread(void) {
  if (command_length < 6)
    return;

  /* Return the contents of the first buffer for now.     */
  /* Simply reading the requested address in AVR ram here */
  /* could cause problems with some IO registers.         */
  /* FIXME: Check for signature addresses and return      */
  /*        something fixed there.                        */
  buffers[CONFIG_BUFFER_COUNT].data = buffers[0].data;
  buffers[CONFIG_BUFFER_COUNT].position = 0;
  buffers[CONFIG_BUFFER_COUNT].lastused = command_buffer[5]-1;
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

#ifdef CONFIG_TURBODISK
  /* Turbodisk sends the filename in the last M-W, check the previous CRC */
  if (datacrc == 0xe1cb) {
    detected_loader = FL_TURBODISK;
  } else
#endif
    detected_loader = FL_NONE;


  for (i=0;i<command_length;i++)
    datacrc = _crc16_update(datacrc, command_buffer[i]);

  if (detected_loader == FL_NONE) {
    uart_puts_P(PSTR("M-W CRC result: "));
    uart_puthex(datacrc >> 8);
    uart_puthex(datacrc & 0xff);
    uart_putcrlf();
  }
}

static void parse_xcommand(void) {
  char *str;

  switch (command_buffer[1]) {
  case 'J':
    /* Jiffy enable/disable */
    switch (command_buffer[2]) {
    case '+':
      iecflags.jiffy_enabled = 1;
      break;

    case '-':
      iecflags.jiffy_enabled = 0;
      break;

    default:
      set_error(ERROR_SYNTAX_UNKNOWN);
    }
    set_error_ts(ERROR_STATUS,device_address,0);
    break;

  case 'C':
    /* Calibration */
    str = (char *)command_buffer+2;
    OSCCAL = parse_number(&str);
    set_error_ts(ERROR_STATUS,device_address,0);
    break;

  case 'W':
    /* Write configuration */
    write_configuration();
    set_error_ts(ERROR_STATUS,device_address,0);
    break;

  case 'S':
    /* Swaplist */
    set_changelist((char *)command_buffer+3);
    break;

#ifdef CONFIG_STACK_TRACKING
  case '?':
    /* Output the largest stack size seen */
    set_error_ts(ERROR_OK,(RAMEND-minstack)>>8,(RAMEND-minstack)&0xff);
    break;
#endif

  default:
    /* Unknown command, just show the status */
    set_error_ts(ERROR_STATUS,device_address,0);
    break;
  }
}

static void parse_block(void) {
  char *str;
  buffer_t *buf;
  uint8_t params[4];
  int8_t  pcount;

  str = strchr((char *) command_buffer, '-');
  if (!str) {
    set_error(ERROR_SYNTAX_UNABLE);
    return;
  }

  memset(params,0,sizeof(params));
  pcount = parse_blockparam(params);
  if (pcount < 0)
    return;

  str++;
  switch (*str) {
  case 'R':
  case 'W':
    /* Block-Read  - CD56 */
    /* Block-Write - CD73 */
    /* Does not include the bug/misfeature of the original */
    buf = find_buffer(params[0]);
    if (!buf) {
      set_error(ERROR_NO_CHANNEL);
      return;
    }

    if (*str == 'R') {
      read_sector(buf,params[2],params[3]);
      buf->position = 1;
      buf->lastused = buf->data[0];
    } else {
      buf->data[0] = buf->position-1; // FIXME: Untested, verify!
      write_sector(buf,params[2],params[3]);
    }
    break;

  case 'P':
    /* Buffer-Position - CDBD */
    buf = find_buffer(params[0]);
    if (!buf) {
      set_error(ERROR_NO_CHANNEL);
      return;
    }
    buf->position = params[1];
    break;

  default:
    set_error(ERROR_SYNTAX_UNABLE);
    return;
  }
}

/* ------------------------------------------------------------------------- */
/*  Main command parser function                                             */
/* ------------------------------------------------------------------------- */

void parse_doscommand(void) {
  uint8_t i,count;
  char *fname;
  struct cbmdirent dent;

  /* Set default message: Everything ok */
  set_error(ERROR_OK);

  /* Abort if the command is too long */
  if (command_length == CONFIG_COMMAND_BUFFER_SIZE) {
    set_error(ERROR_SYNTAX_TOOLONG);
    return;
  }

#ifdef CONFIG_COMMAND_CHANNEL_DUMP
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
    set_error(ERROR_SYNTAX_UNABLE);
    return;
  }

  command_buffer[command_length]   = 0;
  /* Requires less space than checks in the parsers */
  command_buffer[command_length+1] = 0;

  /* MD/CD/RD clash with other commands, so they're checked first */
  if (command_buffer[1] == 'D') {
    char *name;
    switch (command_buffer[0]) {
    case 'M':
      /* MD requires a colon */
      if (!strchr((char *)command_buffer, ':')) {
	set_error(ERROR_SYNTAX_NONAME);
	break;
      }
      /* Fall-through */

    case 'C':
      i = command_buffer[0];
      parse_path((char *) command_buffer+2, (char *) command_buffer, &name);
      if (strlen((char *) command_buffer) != 0) {
	/* Join path and name */
	name[-1] = '/';
	name = (char *) command_buffer;
      } else
	/* Yay, special case: CD/name/ means ./name   */
	/* Technically the terminating / is required, */
	/* but who'll notice if we don't check?       */
	if (name[0] == '/')
	  name++;

      if (iecflags.autoswap_active)
	set_changelist(NULLSTRING);

      if (i == 'C')
	chdir(name);
      else
	mkdir(name);

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
	set_error(ERROR_SYNTAX_NONAME);
	break;
      }

      /* Skip drive number */
      i = 2;
      while (isdigit(command_buffer[i])) i++;
      if (command_buffer[i] != ':') {
	set_error(ERROR_SYNTAX_NONAME);
      } else {
	i = file_delete(NULL, (char *)command_buffer+i+1);
	if (i != 255)
	  set_error_ts(ERROR_SCRATCHED,i,0);
      }
      break;

    default:
      set_error(ERROR_SYNTAX_UNKNOWN);
      break;;
    }

    return;
  }

  switch (command_buffer[0]) {
  case 'I':
    /* Initialize */
    if (disk_state != DISK_OK)
      set_error_ts(ERROR_READ_NOSYNC,18,0);
    else
      free_all_buffers(1);
    break;

  case 'B':
    /* Block-Something */
    parse_block();
    break;

  case 'U':
    switch (command_buffer[1]) {
    case 'A':
    case '1':
      /* Tiny little hack: Rewrite as (B)-R and call that                */
      /* This will always work because there is either a : in the string */
      /* or the drive will start parsing at buf[3].                      */
      command_buffer[0] = '-';
      command_buffer[1] = 'R';
      parse_block();
      break;

    case 'B':
    case '2':
      /* Tiny little hack: see above case for rationale */
      command_buffer[0] = '-';
      command_buffer[1] = 'W';
      parse_block();
      break;

    case 'I':
    case '9':
      switch (command_buffer[2]) {
      case 0:
	/* Soft-reset - just return the dos version */
	set_error(ERROR_DOSVERSION);
	break;

      case '+':
	iecflags.vc20mode = 0;
	break;

      case '-':
	iecflags.vc20mode = 1;
	break;

      default:
	set_error(ERROR_SYNTAX_UNKNOWN);
	break;
      }
      break;

    case 'J':
    case ':':
      /* Reset - technically hard-reset */
      cli();
      restart_call();
      break;

    case '0':
      /* U0 - only device address changes for now */
      if ((command_buffer[2] & 0x1f) == 0x1e &&
	  command_buffer[3] >= 4 &&
	  command_buffer[3] <= 30) {
	device_address = command_buffer[3];
	break;
      }
      /* Fall through */

    default:
      set_error(ERROR_SYNTAX_UNKNOWN);
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
      set_error(ERROR_SYNTAX_UNKNOWN);
    break;

  case 'E':
    /* EEPROM-something */
    do { /* Create a block to get local variables */
      uint16_t address = command_buffer[3] + (command_buffer[4] << 8);
      uint8_t  length  = command_buffer[5];
      
      if (command_length < 6)
	break;
      
      if (command_buffer[1] != '-' || (command_buffer[2] != 'W' && command_buffer[2] != 'R'))
	set_error(ERROR_SYNTAX_UNKNOWN);
      
      if (address > CONFIG_EEPROM_SIZE || address+length > CONFIG_EEPROM_SIZE) {
	set_error(ERROR_SYNTAX_TOOLONG);
	break;;
      }
      
      if (command_buffer[2] == 'W')
	handle_eewrite(address, length);
      else
	handle_eeread(address, length);
    } while (0);
    break;

  case 'S':
    /* Scratch */
    parse_path((char *) command_buffer+1, (char *) command_buffer, &fname);

    if (opendir(&matchdh, (char *) command_buffer))
      return;

    i = 255;
    count = 0;
    while (!next_match(&matchdh, fname, FLAG_HIDDEN, &dent)) {
      /* Skip directories */
      if ((dent.typeflags & TYPE_MASK) == TYPE_DIR)
	continue;
      i = file_delete(NULL, (char *)dent.name);
      if (i != 255)
	count += i;
      else
	break;
    }
    if (i != 255)
      set_error_ts(ERROR_SCRATCHED,count,0);

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

  case 'X':
    parse_xcommand();
    break;

  default:
    set_error(ERROR_SYNTAX_UNKNOWN);
    break;
  }
}
