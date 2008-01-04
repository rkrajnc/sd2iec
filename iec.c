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

   
   iec.c: IEC handling code, stateful version

   This code is a close reimplementation of the bus handling in a 1571
   to be as compatible to original drives as possible. Hex addresses in
   comments refer to the part of the 1571 rom that particular section
   is based on.

*/

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "avrcompat.h"
#include "uart.h"
#include "errormsg.h"
#include "doscmd.h"
#include "buffers.h"
#include "fatops.h"
#include "fileops.h"
#include "sdcard.h"
#include "iec-ll.h"
#include "fastloader-ll.h"
#include "diskchange.h"
#include "iec.h"

/* ------------------------------------------------------------------------- */
/*  Global variables                                                         */
/* ------------------------------------------------------------------------- */

uint8_t device_address;
uint8_t secondary_address;

iecflags_t iecflags;

enum { BUS_IDLE = 0, BUS_ATNACTIVE, BUS_FOUNDATN, BUS_FORME, BUS_NOTFORME, BUS_ATNFINISH, BUS_ATNPROCESS, BUS_CLEANUP } bus_state;

enum { DEVICE_IDLE = 0, DEVICE_LISTEN, DEVICE_TALK } device_state;

/* ------------------------------------------------------------------------- */
/*  Utility stuff                                                            */
/* ------------------------------------------------------------------------- */

/* Calculate timer start value for given timeout in microseconds   */
#define TIMEOUT_US(x) (256-((float)F_CPU/8.0*(x/1000000.0)))

/* Set timer0 count to startval and clear overflow flag                    */
/*   Use in conjunction with the above macro to set up overflow after      */
/*   a specified amount of time. Calculating this directly in the function */
/*   would pull in floating point routines -> big and slow.                */
static void start_timeout(uint8_t startval) {
  TCNT0 = startval;
  TIFR0 |= _BV(TOV0);
}

/* Returns true if timer0 has overflown */
static uint8_t has_timed_out(void) {
  return TIFR0 & _BV(TOV0);
}


/* ------------------------------------------------------------------------- */
/*  Very low-level bus handling                                              */
/* ------------------------------------------------------------------------- */

/* Debounce IEC input - see E9C0 */
static uint8_t iec_pin(void) {
  uint8_t tmp;
  
  do {
    tmp = IEC_PIN;
    _delay_us(2); /* 1571 uses LDA/CMP/BNE, approximate by waiting 2us */
  } while (tmp != IEC_PIN);
  return tmp;
}

/* Checks if ATN has changed and changes state to match - see EA59 */
static uint8_t check_atn(void) {
  if (bus_state == BUS_ATNACTIVE)
    if (IEC_ATN) {
      bus_state = BUS_ATNPROCESS; // A9AC
      return 1;
    } else
      return 0;
  else
    if (!IEC_ATN) {
      bus_state = BUS_FOUNDATN; // A7B3
      return 1;
    } else
      return 0;
}

/* Interrupt routine that simulates the hardware-auto-acknowledge of ATN */
/*   This has to run faster than once per millisecond =(                 */
ISR(TIMER2_COMPA_vect) {
  static uint8_t blinktimer;

  if (!IEC_ATN) {
    set_data(0);
  }

  if (error_blink_active) {
    blinktimer++;
    if (blinktimer == 200) {
      DIRTY_LED_PORT ^= DIRTY_LED_BIT();
      blinktimer = 0;
    }
  }

  if (!(DISKCHANGE_PIN & DISKCHANGE_BIT)) {
    if (keycounter < DISKCHANGE_MAX)
      keycounter++;
  } else {
    keycounter = 0;
  }
}

/* ------------------------------------------------------------------------- */
/*  Byte transfer routines                                                   */
/* ------------------------------------------------------------------------- */

/* Receive a byte from the CBM serial bus - see E9C9 */
/*   If this returns -1 the device state has changed, return to the main loop */
static int16_t _iec_getc(void) {
  uint8_t i,val,tmp;

  val = 0;

  do {                                                 // E9CD-E9D5
    if (check_atn()) return -1;
  } while (!(iec_pin() & IEC_BIT_CLOCK));

  set_data(1);                                         // E9D7
  /* Wait until all other devices released the data line    */
  while (!IEC_DATA) ;                                  // FF20

  /* Timer for EOI detection */
  start_timeout(TIMEOUT_US(256));
  
  do {
    if (check_atn()) return -2;                        // E9DF
    tmp = has_timed_out();                             // E9EE
  } while ((iec_pin() & IEC_BIT_CLOCK) && !tmp);

  /* See if timeout happened -> EOI */
  if (tmp) {
    set_data(0);                                       // E9F2
    _delay_us(73);                      // E9F5-E9F8, delay calculated from all
    set_data(1);                        //   instructions between IO accesses

    uart_putc('E');

    do {
      if (check_atn())                                 // E9FD
	return -3;
    } while (iec_pin() & IEC_BIT_CLOCK);
    
    iecflags.eoi_recvd = 1;                            // EA07
  }

  for (i=0;i<8;i++) {
    /* Check for JiffyDOS                                       */
    /*   Source: http://home.arcor.de/jochen.adler/ajnjil-t.htm */
    if (bus_state == BUS_ATNACTIVE && iecflags.jiffy_enabled && i == 7) {
      start_timeout(TIMEOUT_US(218));
      
      do {
	tmp = IEC_PIN;

	/* If there is a delay before the last bit, the controller uses JiffyDOS */
	if (!iecflags.jiffy_active && has_timed_out()) {
	  if (val < 0x60 && ((val>>1) & 0x1f) == device_address) {
	    /* If it's for us, notify controller that we support Jiffy too */
	    set_data(0);
	    _delay_us(101); // nlq says 405us, but the code shows only 101
	    set_data(1);
	    iecflags.jiffy_active = 1;
	  }
	}
      } while (!(tmp & IEC_BIT_CLOCK));
    } else {
      /* Capture data on rising edge */
      do {                                             // EA0B
	tmp = IEC_PIN;
      } while (!(tmp & IEC_BIT_CLOCK));
    }

    val = (val >> 1) | (!!(tmp & IEC_BIT_DATA) << 7);  // EA18

    do {                                               // EA1A
      if (check_atn()) return -4;
    } while (iec_pin() & IEC_BIT_CLOCK);
  }

  _delay_us(5); // Test
  set_data(0);                                         // EA28
  _delay_us(50);  /* Slow down a little bit, may or may not fix some problems */
  return val;
}

/* Wrap cli/sei around iec_getc. The compiler inlines _iec_getc into this. */
static int16_t iec_getc(void) {
  int16_t val;

  cli();
  val = _iec_getc();
  sei();
  return val;
}
  

/* Send a byte over the CBM serial bus - see E916 */
/*   If this returns -1 the device state has changed, return to the main loop */
static uint8_t iec_putc(uint8_t data, const uint8_t with_eoi) {
  uint8_t i;

  if (check_atn()) return -1;                          // E916
  i = iec_pin();

  _delay_us(60); // Fudged delay
  set_clock(1);

  if (i & IEC_BIT_DATA) { // E923
    /* The 1571 jumps to E937 at this point, but I think            */
    /* this is not necessary - the following loop will fall through */
  }

  do {
    if (check_atn()) return -1;                        // E925
  } while (!(iec_pin() & IEC_BIT_DATA));

  if (with_eoi || (i & IEC_BIT_DATA)) {
    do {
      if (check_atn()) return -1;                      // E937
    } while (!(iec_pin() & IEC_BIT_DATA));
  
    do {
      if (check_atn()) return -1;                      // E941
    } while (iec_pin() & IEC_BIT_DATA);
  }

  set_clock(0);                                        // E94B
  _delay_us(60); // Yet another "looked at the bus trace and guessed until it worked" delay
  do {
    if (check_atn()) return -1;
  } while (!(iec_pin() & IEC_BIT_DATA));

  for (i=0;i<8;i++) {
    if (!(iec_pin() & IEC_BIT_DATA)) {
      bus_state = BUS_CLEANUP;
      return -1;
    }

    set_data(data & 1<<i);
    _delay_us(70);    // Implicid delay, fudged
    set_clock(1);
    if (iecflags.vc20mode)
      _delay_us(34);  // Calculated delay
    else
      _delay_us(69);  // Calculated delay

    set_clock(0);
    set_data(1);
    _delay_us(5);     // Settle time
  }

  do {
    if (check_atn()) return -1;
  } while (iec_pin() & IEC_BIT_DATA);

  return 0;
}


/* ------------------------------------------------------------------------- */
/*  Listen+Talk-Handling                                                     */
/* ------------------------------------------------------------------------- */

/* Handle an incoming LISTEN request - see EA2E */
static uint8_t iec_listen_handler(const uint8_t cmd) {
  int16_t c;
  buffer_t *buf;
  enum { DATA_COMMAND, DATA_BUFFER } data_state;

  uart_putc('L');

  buf = find_buffer(cmd & 0x0f);
    
  /* Abort if there is no buffer or it's not open for writing */
  /* and it isn't an OPEN command                             */
  if ((buf == NULL || !buf->write) && (cmd & 0xf0) != 0xf0) {
    uart_putc('c');
    bus_state = BUS_CLEANUP;
    return 1;
  }

  if ((cmd & 0x0f) == 0x0f || (cmd & 0xf0) == 0xf0) {
    data_state = DATA_COMMAND;
  } else {
    data_state = DATA_BUFFER;
  }

  while (1) {
    if (iecflags.jiffy_active) {
      uint8_t flags;
      set_atnack(1);
      _delay_us(50); /* Slow down or we'll see garbage from the C64 */
                     /* The time was guessed from bus traces.       */
      c = jiffy_receive(&flags);
      if (!(flags & IEC_BIT_ATN))
	/* ATN was active at the end of the transfer */
	c = iec_getc();
      else
	iecflags.eoi_recvd = !!(flags & IEC_BIT_CLOCK);
    } else
      c = iec_getc();
    if (c < 0) return 1;

    if (data_state == DATA_COMMAND) {
      if (command_length < COMMAND_BUFFER_SIZE)
	command_buffer[command_length++] = c;
      if (iecflags.eoi_recvd)
	// Filenames are just a special type of command =)
	iecflags.command_recvd = 1;
    } else {
      buf->dirty = 1;
      buf->data[buf->position] = c;

      if (buf->lastused < buf->position)
	buf->lastused = buf->position;
      buf->position++;

      /* Check if the position has wrapped */
      if (buf->position == 0 && buf->refill)
	if (buf->refill(buf))
	  return 1;
    }
  }    
}

/* Handle an incoming TALK request - see E909 */
static uint8_t iec_talk_handler(uint8_t cmd) {
  buffer_t *buf;

  uart_putc('T');

  buf = find_buffer(cmd & 0x0f);
  if (buf == NULL)
    return 0; /* 0 because we didn't change the state here */
    
  while (buf->read) {
    do {
      if (buf->sendeoi &&
	  buf->position == buf->lastused) {
	/* Send with EOI */
	if (iec_putc(buf->data[buf->position],1)) {
	  uart_putc('Q');
	  return 1;
	}
      } else {
	/* Send without EOI */
	if (iec_putc(buf->data[buf->position],0)) {
	  uart_putc('V');
	  return 1;
	}
      }
    } while (buf->position++ < buf->lastused);

    if (buf->sendeoi)
      break;

    if (buf->refill)
      if (buf->refill(buf)) {
	bus_state = BUS_CLEANUP;
	return 1;
      }
  }

  /* If the error channel was read, create a new OK message */
  if ((cmd & 0x0f) == 0x0f)
    set_error(ERROR_OK);

  return 0;
}



/* ------------------------------------------------------------------------- */
/*  Initialization and main loop                                             */
/* ------------------------------------------------------------------------- */


void init_iec(void) {
  /* Pullups would be nice, but AVR can't switch from */
  /* low output to hi-z input directly                */
  IEC_DDR  &= ~(IEC_BIT_ATN | IEC_BIT_CLOCK | IEC_BIT_DATA);
  IEC_PORT &= ~(IEC_BIT_ATN | IEC_BIT_CLOCK | IEC_BIT_DATA);

  /* Count F_CPU/8 in timer 0 */
  TCCR0B = _BV(CS01);

  /* Issue an interrupt every 500us with timer 2 for ATN-Acknowledge.    */
  /* The exact timing isn't critical, it just has to be faster than 1ms. */
  /* Every 800us was too slow in rare situations.                        */
  OCR2A = 125;
  TCNT2 = 0;
  /* On the mega32 both registers are the same, so OR those bits in */
  TCCR2B = 0;
  TCCR2A |= _BV(WGM21); // CTC mode
  TCCR2B |= _BV(CS20) | _BV(CS21); // prescaler /32

  /* We're device 8 by default */
  DEV9_JUMPER_SETUP();
  DEV10_JUMPER_SETUP();
  _delay_ms(1);
  device_address = 8 + !!DEV9_JUMPER + 2*(!!DEV10_JUMPER);

  /* Set up disk change key */
  DISKCHANGE_DDR  &= ~DISKCHANGE_BIT;
  DISKCHANGE_PORT |=  DISKCHANGE_BIT;

  set_error(ERROR_DOSVERSION);
}

void iec_mainloop(void) {
  int16_t cmd = 0; // make gcc happy...
  
  uart_puts_P(PSTR("\nIn iec_mainloop listening on "));
  uart_puthex(device_address);
  uart_putcrlf();

  sei();

  iecflags.jiffy_active = 0;
  iecflags.vc20mode     = 0;

  bus_state = BUS_IDLE;

  while (1) {
    switch (bus_state) {
    case BUS_IDLE:  // EBFF
      /* Wait for ATN */
      set_atnack(1);
      while (IEC_ATN) {
	if (keycounter == DISKCHANGE_MAX) {
	  change_disk();
	}
      }
      
      bus_state = BUS_FOUNDATN;
      break;

    case BUS_FOUNDATN: // E85B
      /* Pull data low to say we're here */
      set_clock(1);
      set_data(0);
      set_atnack(0);

      device_state = DEVICE_IDLE;
      bus_state    = BUS_ATNACTIVE;
      iecflags.eoi_recvd    = 0;
      iecflags.jiffy_active = 0;

      /* Slight protocol violation:                        */
      /*   Wait until clock is low or 100us have passed    */
      /*   The C64 doesn't always pull down the clock line */
      /*   before ATN, this loop should keep us in sync.   */

      start_timeout(TIMEOUT_US(100));
      while (IEC_CLOCK && !has_timed_out())
	if (IEC_ATN)
	  bus_state = BUS_ATNPROCESS;

      while (!IEC_CLOCK)
	if (IEC_ATN)
	  bus_state = BUS_ATNPROCESS;

      break;

    case BUS_ATNACTIVE: // E884
      cmd = iec_getc();

      if (cmd < 0) {
	/* check_atn changed our state */
	uart_putc('C');
	break;
      }
      
      uart_putc('A');
      uart_puthex(cmd);
      uart_putc(13);
      uart_putc(10);

      if (cmd == 0x3f) { /* Unlisten */
	if (device_state == DEVICE_LISTEN)
	  device_state = DEVICE_IDLE;
	bus_state = BUS_ATNFINISH;
      } else if (cmd == 0x5f) { /* Untalk */
	if (device_state == DEVICE_TALK)
	  device_state = DEVICE_IDLE;
	bus_state = BUS_ATNFINISH;
      } else if (cmd == 0x40+device_address) { /* Talk */
	device_state = DEVICE_TALK;
	bus_state = BUS_FORME;
      } else if (cmd == 0x20+device_address) { /* Listen */
	device_state = DEVICE_LISTEN;
	bus_state = BUS_FORME;
      } else if ((cmd & 0x60) == 0x60) {
	secondary_address = cmd & 0x0f;
	/* 1571 handles close (0xe0-0xef) here, so we do that too. */
	if ((cmd & 0xf0) == 0xe0) {
	  if (cmd == 0xef) {
	    /* Close all buffers if sec. 15 is closed */
	    if (free_all_buffers(1)) {
	      /* The 1571 error generator/handler always jumps to BUS_CLEANUP */
	      bus_state = BUS_CLEANUP;
	      break;
	    }
	  } else {
	    /* Close a single buffer */
	    buffer_t *buf;
	    buf = find_buffer(secondary_address);
	    if (buf != NULL) {
	      if (buf->cleanup) {
		if (buf->cleanup(buf)) {
		  bus_state = BUS_CLEANUP;
		  break;
		}
	      } else
		/* No cleanup: Free the buffer here */
		// FIXME: Kann man das free_buffer überall rausziehen?
		free_buffer(buf);
	    }
	  }
	  bus_state = BUS_FORME;
	} else {
	  bus_state = BUS_ATNFINISH;
	}
      } else {
	// Not me
	bus_state = BUS_NOTFORME;
      }
      break;

    case BUS_FORME: // E8D2
      if (!IEC_ATN)
	bus_state = BUS_ATNACTIVE;
      else
	bus_state = BUS_ATNPROCESS;
      break;

    case BUS_NOTFORME: // E8FD
      set_atnack(0);
      set_clock(1);
      set_data(1);
      bus_state = BUS_ATNFINISH;
      break;

    case BUS_ATNFINISH: // E902
      while (!IEC_ATN) ;
      bus_state = BUS_ATNPROCESS;
      break;

    case BUS_ATNPROCESS: // E8D7
      set_atnack(1);
      
      if (device_state == DEVICE_LISTEN) {
	if (iec_listen_handler(cmd))
	  break;
      } else if (device_state == DEVICE_TALK) {
	set_data(1);
	_delay_us(50);   // Implicit delay, fudged
	set_clock(0);
	_delay_us(70);   // Implicit delay, estimated

	if (iec_talk_handler(cmd))
	  break;

      }
      bus_state = BUS_CLEANUP;
      break;

    case BUS_CLEANUP:
      set_atnack(1);
      // 836B
      set_clock(1);
      set_data(1);

      /* This seems to be a nice point to handle card changes */
      if (card_state != CARD_OK && card_state != CARD_REMOVED) {
	BUSY_LED_ON();
	/* If the card was changed the buffer contents are useless */
	if (card_state == CARD_CHANGED) {
	  free_all_buffers(0);
	  init_change();
	}
	// FIXME: Preserve current directory if state was CARD_ERROR
	init_fatops();
	if (!active_buffers) {
	  BUSY_LED_OFF();
	  DIRTY_LED_OFF();
	}
      }

      //   0x255 -> A61C
      /* Handle commands and filenames */
      if (iecflags.command_recvd) {
	if (secondary_address == 0x0f) {
	  /* Command channel */
	  parse_doscommand();
	} else {
	  /* Filename in command buffer */
	  datacrc = 0xffff;
	  file_open(secondary_address);
	}
	command_length = 0;
	iecflags.command_recvd = 0;
      }
      
      bus_state = BUS_IDLE;
      break;
    }
  }
}
