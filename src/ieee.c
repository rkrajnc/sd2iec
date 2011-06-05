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


   ieee.c: IEEE-488 handling code by Nils Eilers <nils.eilers@gmx.de>

*/

#include "config.h"
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/atomic.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "timer.h"
#include "uart.h"
#include "buffers.h"
#include "diskchange.h"
#include "diskio.h"
#include "doscmd.h"
#include "fileops.h"
#include "fatops.h"
#include "led.h"
#include "ieee.h"
#include "fastloader.h"
#include "errormsg.h"
#include "ctype.h"

/*
  Debug output:

  AXX   : ATN 0xXX
  c     : listen_handler cancelled
  C     : CLOSE
  l     : UNLISTEN
  L     : LISTEN
  D     : DATA 0x60
  O     : OPEN 0xfX
  ?XX   : unknown cmd 0xXX
  .     : timeout after ATN

*/

#define FLAG_EOI 256
#define FLAG_ATN 512

#define uart_puts_p(__s) uart_puts_P(PSTR(__s))
#define EOI_RECVD       (1<<0)
#define COMMAND_RECVD   (1<<1)
#define ATN_POLLED      -3      // 0xfd
#define TIMEOUT_ABORT   -4      // 0xfc

/* ------------------------------------------------------------------------- */
/*  Global variables                                                         */
/* ------------------------------------------------------------------------- */

uint8_t detected_loader = FL_NONE;      /* Workaround serial fastloader */
uint8_t device_address;                 /* Current device address */
static tick_t timeout;                  /* timeout on getticks()=timeout */

/**
 * struct ieeeflags_t - Bitfield of various flags, mostly IEEE-related
 * @eoi_recvd      : Received EOI with the last byte read
 * @command_recvd  : Command or filename received
 *
 * This is a bitfield for a number of boolean variables used
 */

struct {
  uint8_t ieeeflags;
  enum {    BUS_IDLE = 0,
            BUS_FOUNDATN,
            BUS_ATNPROCESS,
//          BUS_SLEEP                   TODO: add sleep mode
  } bus_state;

  enum {    DEVICE_IDLE = 0,
            DEVICE_LISTEN,
            DEVICE_TALK
  } device_state;
  uint8_t secondary_address;
} ieee_data;

/* ------------------------------------------------------------------------- */
/*  Initialization and very low-level bus handling                           */
/* ------------------------------------------------------------------------- */

static inline void set_eoi_state(uint8_t x)
{
  if(x) {                                           // Set EOI high
    IEEE_C_DDR &= (uint8_t)~_BV(IEEE_PIN_EOI);      // EOI as input
    IEEE_C_PORT |= (uint8_t)_BV(IEEE_PIN_EOI);      // Enable pull-up
  } else {                                          // Set EOI low
    IEEE_C_PORT &= (uint8_t)~_BV(IEEE_PIN_EOI);     // EOI low
    IEEE_C_DDR |= (uint8_t) _BV(IEEE_PIN_EOI);      // EOI as output
  }
}

/* Read port bits */
# define IEEE_ATN        (IEEE_C_ATN_PIN & _BV(IEEE_PIN_ATN))
# define IEEE_NDAC       (IEEE_C_PIN & _BV(IEEE_PIN_NDAC))
# define IEEE_NRFD       (IEEE_C_PIN & _BV(IEEE_PIN_NRFD))
# define IEEE_DAV        (IEEE_C_PIN & _BV(IEEE_PIN_DAV))
# define IEEE_EOI        (IEEE_C_PIN & _BV(IEEE_PIN_EOI))

#ifdef HAVE_7516X
// FIXME: Translate and move to documentation!
/*
   Device should use 75160/75161 bus drivers
   =========================================

   They are specially designed for the IEEE 488 bus and offer sufficiant sink
   and source current, bus termination, signal raise times etc. according the
   standard.

    Signale des SN75161:
    ====================

    DC muss immer high sein, da petSD Device ist und kein Controller.
    Damit ist folgende Datenrichtung (aus Sicht des AVR) festgelegt:
    - ATN ist immer Eingang
    - SRQ ist immer Ausgang
    - REN und IFC sind immer Eingaenge
      (nicht an AVR angeschlossen)


    TE regelt die Flussrichtung der Signale:

    - TE high (petSD ist Talker):
        - DAV ist Ausgang
        - NDAC und NRFD sind Eingaenge
        - EOI ist Ausgang (AVR --> 75161 --> Bus) solange ATN high ist.
          Wenn der Controller ATN setzt (dem Device also "in's Wort faellt"),
          wird EOI zum Eingang (AVR --> <-- 75161 <-- Bus) umgeschaltet!
          Da der 75161 offene (=unbeschaltete) Eingaenge als high erkennt,
          kann der Flussrichtungskonflikt wie folgt umgangen werden:
          Bei EOI=1 wird der Port als Eingang mit Pull-up geschaltet.
          Bei EOI=0 wird der Port als Ausgang beschaltet. Hier gibt es
          keinen Konflikt, da sowohl EOI vom AVR als auch EOI vom 75161 auf
          low liegen.

    - TE low (petSD ist Listener):
        - DAV ist Eingang
        - EOI ist Eingang
        - NDAD und NRFD sind Ausgaenge
*/

# define ddr_change_by_atn() \
    if(ieee_data.device_state == DEVICE_TALK) IEEE_PORTS_LISTEN()
# define set_ieee_data(data) IEEE_D_PORT = (uint8_t) ~ data

  static inline void set_te_state(uint8_t x)
  {
    if(x) IEEE_C_PORT |= _BV(IEEE_PIN_TE);
    else  IEEE_C_PORT &= ~_BV(IEEE_PIN_TE);
  }

  static inline void set_ndac_state(uint8_t x)
  {
    if (x) IEEE_C_PORT |= _BV(IEEE_PIN_NDAC);
    else   IEEE_C_PORT &= ~_BV(IEEE_PIN_NDAC);
  }

  static inline void set_nrfd_state(uint8_t x)
  {
    if(x) IEEE_C_PORT |= _BV(IEEE_PIN_NRFD);
    else  IEEE_C_PORT &= ~_BV(IEEE_PIN_NRFD);
  }

  static inline void set_dav_state(uint8_t x)
  {
    if(x) IEEE_C_PORT |= _BV(IEEE_PIN_DAV);
    else  IEEE_C_PORT &= ~_BV(IEEE_PIN_DAV);
  }

  /* Configure bus to passive/listen or talk */
  /* Toogle direction of I/O pins and safely avoid connecting two outputs */

  static inline void IEEE_PORTS_LISTEN (void)
  {
    IEEE_D_DDR=0;           /* data ports as inputs */
    IEEE_D_PORT=0xff;       /* enable pull-ups for data lines  */
    IEEE_C_DDR &=(uint8_t) ~ (_BV(IEEE_PIN_DAV)   /* DAV as input */
                            | _BV(IEEE_PIN_EOI)); /* EOI as input */
    set_te_state(0);                              /* 7516x listen */
    IEEE_C_DDR |= (uint8_t) (_BV(IEEE_PIN_NDAC)   /* NDAC as output */
                          | _BV(IEEE_PIN_NRFD));  /* NRFD as output */
    /* Enable pull-ups for DAV, EOI */
    IEEE_C_PORT |= (uint8_t) ( _BV(IEEE_PIN_DAV) | _BV(IEEE_PIN_EOI) );
  }

  static inline void IEEE_PORTS_TALK (void)
  {
    IEEE_C_DDR &= (uint8_t)~_BV(IEEE_PIN_NDAC); // NDAC as input
    IEEE_C_DDR &= (uint8_t)~_BV(IEEE_PIN_NRFD); // NRFD as input
    /* Enable pull-ups for NDAC, NRFD */
    IEEE_C_PORT |= (uint8_t) ( _BV(IEEE_PIN_NDAC) | _BV(IEEE_PIN_NRFD) );
    set_te_state(1);                            // 7516x talk enable
    IEEE_D_PORT=0xff;                           // all data lines high
    IEEE_D_DDR=0xff;                            // data ports as outputs
    set_dav_state(1);                           // Set DAV high
    IEEE_C_DDR |= (uint8_t)_BV(IEEE_PIN_DAV);   // DAV as output
    set_eoi_state(1);                           // Set EOI high
    IEEE_C_DDR |= (uint8_t) _BV(IEEE_PIN_EOI);  // EOI as output
  }

  static void inline IEEE_BUS_IDLE (void)
  {
    IEEE_PORTS_LISTEN();
    set_ndac_state(1);
    set_nrfd_state(1);
  }

#else   /* HAVE_7516X */
  /* ----------------------------------------------------------------------- */
  /*  Poor men's variant without IEEE bus drivers                            */
  /* ----------------------------------------------------------------------- */

  static inline void set_ndac_state (uint8_t x)
  {
    if(x) {                                         // Set NDAC high
      IEEE_C_DDR &= (uint8_t)~_BV(IEEE_PIN_NDAC);   // NDAC as input
      IEEE_C_PORT |= (uint8_t)_BV(IEEE_PIN_NDAC);   // Enable pull-up
    } else {                                        // Set NDAC low
      IEEE_C_PORT &= (uint8_t)~_BV(IEEE_PIN_NDAC);  // NDAC low
      IEEE_C_DDR |= (uint8_t) _BV(IEEE_PIN_NDAC);   // NDAC as output
    }
  }

  static inline void set_nrfd_state (uint8_t x)
  {
    if(x) {                                         // Set NRFD high
      IEEE_C_DDR &= (uint8_t)~_BV(IEEE_PIN_NRFD);   // NRFD as input
      IEEE_C_PORT |= (uint8_t)_BV(IEEE_PIN_NRFD);   // Enable pull-up
    } else {                                        // Set NRFD low
      IEEE_C_PORT &= (uint8_t)~_BV(IEEE_PIN_NRFD);  // NRFD low
      IEEE_C_DDR |= (uint8_t) _BV(IEEE_PIN_NRFD);   // NRFD as output
    }
  }

  static inline void set_dav_state (uint8_t x)
  {
    if(x) {                                         // Set DAV high
      IEEE_C_DDR &= (uint8_t)~_BV(IEEE_PIN_DAV);    // DAV as input
      IEEE_C_PORT |= (uint8_t)_BV(IEEE_PIN_DAV);    // Enable pull-up
    } else {                                        // Set DAV low
      IEEE_C_PORT &= (uint8_t)~_BV(IEEE_PIN_DAV);   // DAV low
      IEEE_C_DDR |= (uint8_t) _BV(IEEE_PIN_DAV);    // DAV as output
    }
  }

# define set_te_state(dummy) do { } while (0)       // ignore TE

  static inline void set_ieee_data (uint8_t data)
  {
    IEEE_D_DDR = data;
    IEEE_D_PORT = (uint8_t) ~ data;
  }

  static inline void IEEE_BUS_IDLE (void)
  {
    IEEE_D_DDR = 0;                 // data ports as input
    IEEE_D_PORT = 0xff;             // enable pull-ups for data lines
    /* Define DAV, EOI, NDAC, NRFD as inputs */
    IEEE_C_DDR &= (uint8_t) ~ ( _BV(IEEE_PIN_DAV) | _BV(IEEE_PIN_EOI)
      | _BV(IEEE_PIN_NDAC) | _BV(IEEE_PIN_NRFD) );
    /* Enable pull-ups for DAV, EOI, NDAC, NRFD */
    IEEE_C_PORT |= _BV(IEEE_PIN_DAV) | _BV(IEEE_PIN_EOI)
      | _BV(IEEE_PIN_NDAC) | _BV(IEEE_PIN_NRFD);
  }

# define IEEE_PORTS_LISTEN() do { IEEE_BUS_IDLE(); } while (0)
# define IEEE_PORTS_TALK()   do { IEEE_BUS_IDLE(); } while (0)
# define ddr_change_by_atn() do { } while (0)

#endif  /* HAVE_7516X */

/* Init IEEE bus */
void ieee_init(void) {
  IEEE_BUS_IDLE();

  /* Prepare IEEE interrupts */
  ieee_interrupts_init();

  /* Read the hardware-set device address */
  device_hw_address_init();
  delay_ms(1);
  device_address = device_hw_address();

  /* Init vars and flags */
  command_length = 0;
  ieee_data.ieeeflags &= (uint8_t) ~ (COMMAND_RECVD || EOI_RECVD);
}
void bus_init(void) __attribute__((weak, alias("ieee_init")));

/* Interrupt routine that simulates the hardware-auto-acknowledge of ATN */
IEEE_ATN_HANDLER {
  if(!IEEE_ATN) {               /* Run code only at falling edge of ATN */
    ddr_change_by_atn();        /* Switch NDAC+NRFD to outputs */
    set_ndac_state(0);          /* Poll NDAC and NRFD low */
    set_nrfd_state(0);
  }
}

/* ------------------------------------------------------------------------- */
/*  Byte transfer routines                                                   */
/* ------------------------------------------------------------------------- */


/**
 * ieee_getc - receive one byte from the IEEE-488 bus
 *
 * This function tries receives one byte from the IEEE-488 bus and returns it
 * if successful. Flags (EOI, ATN) are passed in the more significant byte.
 * Returns TIMEOUT_ABORT if a timeout occured
 */

int ieee_getc(void) {
  int c = 0;

  /* PET waits for NRFD high */
  set_ndac_state(0);            /* data not yet accepted */
  set_nrfd_state(1);            /* ready for new data */

  /* Wait for DAV low, check timeout */
  timeout = getticks() + MS_TO_TICKS(64);
  do {                          /* wait for data valid */
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (IEEE_DAV);

  set_nrfd_state(0);    /* not ready for new data, data not yet read */

  c = (uint8_t) ~ IEEE_D_PIN;   /* read data */
  if(!IEEE_EOI) c |= FLAG_EOI;  /* end of transmission? */
  if(!IEEE_ATN) c |= FLAG_ATN;  /* data or command? */

  set_ndac_state(1);            /* data accepted, read complete */

  /* Wait for DAV high, check timeout */
  timeout = getticks() + MS_TO_TICKS(64);
  do {              /* wait for controller to remove data from bus */
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (!IEEE_DAV);
  set_ndac_state(0);            /* next data not yet accepted */

  return c;
}


/**
 * ieee_putc - send a byte
 * @data    : byte to be sent
 * @with_eoi: Flags if the byte should be send with an EOI condition
 *
 * This function sends the byte data over the IEEE-488 bus and pulls
 * EOI if it is the last byte.
 * Returns
 *  0 normally,
 * ATN_POLLED if ATN was set or
 * TIMEOUT_ABORT if a timeout occured
 * On negative returns, the caller should return to the IEEE main loop.
 */

static uint8_t ieee_putc(uint8_t data, const uint8_t with_eoi) {
  IEEE_PORTS_TALK();
  set_eoi_state (!with_eoi);
  set_ieee_data (data);
  if(!IEEE_ATN) return ATN_POLLED;
  _delay_us(11);    /* Allow data to settle */
  if(!IEEE_ATN) return ATN_POLLED;

  /* Wait for NRFD high , check timeout */
  timeout = getticks() + MS_TO_TICKS(64);
  do {
    if(!IEEE_ATN) return ATN_POLLED;
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (!IEEE_NRFD);
  set_dav_state(0);

  /* Wait for NRFD low, check timeout */
  timeout = getticks() + MS_TO_TICKS(64);
  do {
    if(!IEEE_ATN) return ATN_POLLED;
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (IEEE_NRFD);

  /* Wait for NDAC high , check timeout */
  timeout = getticks() + MS_TO_TICKS(64);
  do {
    if(!IEEE_ATN) return ATN_POLLED;
    if(time_after(getticks(), timeout)) return TIMEOUT_ABORT;
  } while (!IEEE_NDAC);
  set_dav_state(1);
  return 0;
}

/* ------------------------------------------------------------------------- */
/*  Listen+Talk-Handling                                                     */
/* ------------------------------------------------------------------------- */

static int16_t ieee_listen_handler (uint8_t cmd)
/* Receive characters from IEEE-bus and write them to the
   listen buffer adressed by ieee_data.secondary_address.
   If a new command is received (ATN set), return it
*/
{
  buffer_t *buf;
  int16_t c;

  ieee_data.secondary_address = cmd & 0x0f;
  buf = find_buffer(ieee_data.secondary_address);

  /* Abort if there is no buffer or it's not open for writing */
  /* and it isn't an OPEN command                             */
  if ((buf == NULL || !buf->write) && (cmd & 0xf0) != 0xf0) {
    uart_putc('c');
    return -1;
  }

  switch(cmd & 0xf0) {
    case 0x60:
      uart_puts_p("DATA L ");
      break;
    case 0xf0:
      uart_puts_p("OPEN ");
      break;
    default:
      uart_puts_p("Unknown LH! ");
      break;
  }
  uart_puthex(ieee_data.secondary_address);
  uart_putcrlf();

  c = -1;
  for(;;) {
    /* Get a character ignoring timeout but but watching ATN */
    while((c = ieee_getc()) < 0);
    if (c  & FLAG_ATN) return c;

    uart_putc('<');
    if (c & FLAG_EOI) {
      uart_puts_p("EOI ");
      ieee_data.ieeeflags |= EOI_RECVD;
    } else ieee_data.ieeeflags &= ~EOI_RECVD;

    uart_puthex(c); uart_putc(' ');
    c &= 0xff; /* needed for isprint */
    if(isprint(c)) uart_putc(c); else uart_putc('?');
    uart_putcrlf();

    if((cmd & 0x0f) == 0x0f || (cmd & 0xf0) == 0xf0) {
      if (command_length < CONFIG_COMMAND_BUFFER_SIZE)
        command_buffer[command_length++] = c;
      if (ieee_data.ieeeflags & EOI_RECVD)
        /* Filenames are just a special type of command =) */
        ieee_data.ieeeflags |= COMMAND_RECVD;
    } else {
      /* Flush buffer if full */
      if (buf->mustflush) {
        if (buf->refill(buf)) return -2;
        /* Search the buffer again,                     */
        /* it can change when using large buffers       */
        buf = find_buffer(ieee_data.secondary_address);
      }

      buf->data[buf->position] = c;
      mark_buffer_dirty(buf);

      if (buf->lastused < buf->position) buf->lastused = buf->position;
      buf->position++;

      /* Mark buffer for flushing if position wrapped */
      if (buf->position == 0) buf->mustflush = 1;

      /* REL files must be syncronized on EOI */
      if(buf->recordlen && (ieee_data.ieeeflags & EOI_RECVD)) {
        if (buf->refill(buf)) return -2;
      }
    }   /* else-buffer */
  }     /* for(;;) */
}

static uint8_t ieee_talk_handler (void)
{
  buffer_t *buf;
  uint8_t finalbyte;
  uint8_t c;
  uint8_t res;

  buf = find_buffer(ieee_data.secondary_address);
  if(buf == NULL) return -1;

  while (buf->read) {
    do {
      finalbyte = (buf->position == buf->lastused);
      c = buf->data[buf->position];
      if (finalbyte && buf->sendeoi) {
        /* Send with EOI */
        res = ieee_putc(c, 1);
        if(!res) uart_puts_p("EOI: ");
      } else {
        /* Send without EOI */
        res = ieee_putc(c, 0);
      }
      if(res) {
        uart_putc('c'); uart_puthex(res);
        return 1;
      } else {
        uart_putc('>');
        uart_puthex(c); uart_putc(' ');
        if(isprint(c)) uart_putc(c); else uart_putc('?');
        uart_putcrlf();
      }
    } while (buf->position++ < buf->lastused);

    if(buf->sendeoi && ieee_data.secondary_address != 0x0f &&
      !buf->recordlen && buf->refill != directbuffer_refill) {
      buf->read = 0;
      break;
    }

    if (buf->refill(buf)) {
      return -1;
    }

    /* Search the buffer again, it can change when using large buffers */
    buf = find_buffer(ieee_data.secondary_address);
  }
  return 0;
}

static void cmd_handler (void)
{
  /* Handle commands and filenames */
  if (ieee_data.ieeeflags & COMMAND_RECVD) {
# ifdef HAVE_HOTPLUG
    /* This seems to be a nice point to handle card changes */
    if (disk_state != DISK_OK) {
      set_busy_led(1);
      /* If the disk was changed the buffer contents are useless */
      if (disk_state == DISK_CHANGED || disk_state == DISK_REMOVED) {
        free_multiple_buffers(FMB_ALL);
        change_init();
        fatops_init(0);
      } else {
        /* Disk state indicated an error, try to recover by initialising */
        fatops_init(1);
      }
      update_leds();
    }
# endif
    if (ieee_data.secondary_address == 0x0f) {
      parse_doscommand();                   /* Command channel */
    } else {
      datacrc = 0xffff;                     /* Filename in command buffer */
      file_open(ieee_data.secondary_address);
    }
    command_length = 0;
    ieee_data.ieeeflags &= (uint8_t) ~COMMAND_RECVD;
  } /* COMMAND_RECVD */

  /* We're done, clean up unused buffers */
  free_multiple_buffers(FMB_UNSTICKY);
}

/* ------------------------------------------------------------------------- */
/*  Main loop                                                                */
/* ------------------------------------------------------------------------- */

void ieee_mainloop(void) {
  int16_t cmd = 0;

  set_error(ERROR_DOSVERSION);

  uart_puts_p("IEEE main loop entered as device ");
  uart_puthex(device_address); uart_putcrlf();

  ieee_data.bus_state = BUS_IDLE;
  ieee_data.device_state = DEVICE_IDLE;
  for(;;) {
    switch(ieee_data.bus_state) {
      case BUS_IDLE:                                /* BUS_IDLE */
        IEEE_BUS_IDLE();
        while(IEEE_ATN);    /* wait for ATN */
        ieee_data.bus_state = BUS_FOUNDATN;
      break;

      case BUS_FOUNDATN:                            /* BUS_FOUNDATN */
        ieee_data.bus_state     = BUS_ATNPROCESS;
        cmd = ieee_getc();
      break;

      case BUS_ATNPROCESS:                          /* BUS_ATNPROCESS */
        if(cmd < 0) {
          uart_putc('c');
          ieee_data.bus_state   = BUS_IDLE;
          break;
        } else cmd &= 0xFF;
        uart_puts_p("ATN "); uart_puthex(cmd);
        uart_putcrlf();

        if (cmd == 0x3f) {                                  /* UNLISTEN */
          if(ieee_data.device_state == DEVICE_LISTEN) {
            ieee_data.device_state = DEVICE_IDLE;
            uart_puts_p("UNLISTEN\r\n");
          }
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if (cmd == 0x5f) {                           /* UNTALK */
          if(ieee_data.device_state == DEVICE_TALK) {
            ieee_data.device_state = DEVICE_IDLE;
            uart_puts_p("UNTALK\r\n");
          }
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if (cmd == (0x40 + device_address)) {        /* TALK */
          uart_puts_p("TALK ");
          uart_puthex(device_address); uart_putcrlf();
          ieee_data.device_state = DEVICE_TALK;
          /* disk drives never talk immediatly after TALK, so stay idle
             and wait for a secondary address given by 0x60-0x6f DATA */
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if (cmd == (0x20 + device_address)) {        /* LISTEN */
          ieee_data.device_state = DEVICE_LISTEN;
          uart_puts_p("LISTEN ");
          uart_puthex(device_address); uart_putcrlf();
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if ((cmd & 0xf0) == 0x60) {                  /* DATA */
          /* 8250LP sends data while ATN is still active, so wait
             for bus controller to release ATN or we will misinterpret
             data as a command */
          while(!IEEE_ATN);
          if(ieee_data.device_state == DEVICE_LISTEN) {
            cmd = ieee_listen_handler(cmd);
            cmd_handler();
            break;
          } else if (ieee_data.device_state == DEVICE_TALK) {
            ieee_data.secondary_address = cmd & 0x0f;
            uart_puts_p("DATA T ");
            uart_puthex(ieee_data.secondary_address);
            uart_putcrlf();
            if(ieee_talk_handler() == TIMEOUT_ABORT) {
              ieee_data.device_state = DEVICE_IDLE;
            }
            ieee_data.bus_state = BUS_IDLE;
            break;
          } else {
            ieee_data.bus_state = BUS_IDLE;
            break;
          }
        } else if (ieee_data.device_state == DEVICE_IDLE) {
          ieee_data.bus_state = BUS_IDLE;
          break;
          /* ----- if we reach this, we're LISTENer or TALKer ----- */
        } else if ((cmd & 0xf0) == 0xe0) {                  /* CLOSE */
          ieee_data.secondary_address = cmd & 0x0f;
          uart_puts_p("CLOSE ");
          uart_puthex(ieee_data.secondary_address);
          uart_putcrlf();
          /* Close all buffers if sec. 15 is closed */
          if(ieee_data.secondary_address == 15) {
            free_multiple_buffers(FMB_USER_CLEAN);
          } else {
            /* Close a single buffer */
            buffer_t *buf;
            buf = find_buffer (ieee_data.secondary_address);
            if (buf != NULL) {
              buf->cleanup(buf);
              free_buffer(buf);
            }
          }
          ieee_data.bus_state = BUS_IDLE;
          break;
        } else if ((cmd & 0xf0) == 0xf0) {                  /* OPEN */
          cmd = ieee_listen_handler(cmd);
          cmd_handler();
          break;
        } else {
          /* Command for other device or unknown command */
          ieee_data.bus_state = BUS_IDLE;
        }
      break;
    }   /* switch   */
  }     /* for()    */
}
void bus_mainloop(void) __attribute__ ((weak, alias("ieee_mainloop")));
