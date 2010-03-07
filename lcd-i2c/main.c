/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2010  Ingo Korb <ingo@akana.de>

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


   main.c: Main display code

*/

#include "config.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <util/atomic.h>
#include <util/delay.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "avrcompat.h"
#include "display.h"
#include "encoder.h"
#include "lcd.h"
#include "menu.h"
#include "timer.h"
#include "utils.h"

/* ------------------------------------------------------------------------- */
/*  Global variables                                                         */
/* ------------------------------------------------------------------------- */

/* Status line */
static const PROGMEM char status_line_str[] = "P___                    A__";
#define STATUS_ADDR 25
#define STATUS_PART 1

/* Buffers for the LCD rows */
typedef struct {
  char    contents[CONFIG_I2C_BUFFER_SIZE+1];
  int8_t  scrollofs;
  tick_t  scrolltick;
  int     updated:1;
  int     scrolling:1;
} linebuffer_t;

static linebuffer_t lcdline[LCD_ROWS];

static volatile uint8_t updatedisplay;

/* Menu state */
static enum { MST_INACTIVE = 0, MST_RESET, MST_INIT, MST_ACTIVE } menustate;
static int8_t menuentry;

/* I2C rx buffer and tx pointers */
static uint8_t rxbuffer[CONFIG_I2C_BUFFER_SIZE];
static uint8_t *rxbufptr = rxbuffer;
static uint8_t *txdata;
static uint8_t txlen;

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/* Set INTRQ-pin to low (state == 0) or high (state != 0) */
static inline void set_intrq(uint8_t state) {
  if (state)
    INTRQ_PORT |= INTRQ_BIT;
  else
    INTRQ_PORT &= (uint8_t)~INTRQ_BIT;
}

/**
 * pet2asc - convert string from PETSCII to ASCII
 * @buf: pointer to the string to be converted
 *
 * This function converts the string in the given buffer from PETSCII to
 * ASCII in-place. Modified for LCD use, substitutes _ with 0x01 to
 * use the custom "left arrow" character.
 */
static void pet2asc(uint8_t *buf) {
  uint8_t ch;
  while (*buf) {
    ch = *buf;
    if (ch == 95) // left arrow, use custom LCD char
      ch = 0x01;
    if (ch > (128+64) && ch < (128+91))
      ch -= 128;
    else if (ch > (96-32) && ch < (123-32))
      ch += 32;
    else if (ch > (192-128) && ch < (219-128))
      ch += 128;
    else if (ch == 255)
      ch = '~';
    *buf = ch;
    buf++;
  }
}

/**
 * asc2pet - convert string from ASCII to PETSCII
 * @buf: pointer to the string to be converted
 *
 * This function converts the string in the given buffer from ASCII to
 * PETSCII in-place. Modified for LCD use, substitutes the custom
 * "left arrow" character 0x01 with _.
 */
static void asc2pet(uint8_t *buf) {
  uint8_t ch;
  while (*buf) {
    ch = *buf;
    if (ch == 1) // left arrow
      ch = '_';
    if (ch > 64 && ch < 91)
      ch += 128;
    else if (ch > 96 && ch < 123)
      ch -= 32;
    else if (ch > 192 && ch < 219)
      ch -= 128;
    else if (ch == '~')
      ch = 255;
    *buf = ch;
    buf++;
  }
}

/**
 * set_line - format text data for display line
 * @line   : output in this display line
 * @len    : length of string
 * @string : string to be displayed
 * @convert: do PETSCII conversion
 *
 * This function will copy up to len bytes from string to
 * the lcd-line buffer for line, marking it as updated.
 * If convert is non-zero, PETSCII to ASCII conversion will
 * be applied to string first. Copying stops at the first
 * ASCII character > 126.
 */
static void set_line(uint8_t line, uint8_t len, uint8_t *string, uint8_t convert) {
  char *ptr = lcdline[line].contents;

  memset(&lcdline[line],0,sizeof(linebuffer_t));
  lcdline[line].updated = 1;
  updatedisplay++;

  if (convert)
    pet2asc(string);

  while (*string <= 126 && len-- > 0)
    *ptr++ = *string++;
}

/**
 * set_line_part - format partition+text data for display line
 * @line   : output in this display line
 * @prefix : prefix character
 * @len    : length of string
 * @data   : data to be displayed
 *
 * This function will copy data to the lcd-line buffer for line,
 * marking it as updated. If prefix is non-zero, it will be
 * the first character in the new line contents. The first
 * byte of data is displayed as a decimal number followed by
 * a colon, followed by the remainder of data converted
 * from PETSCII to ASCII. Copying stops at the first ASCII
 * character > 126.
 */
static void set_line_part(uint8_t line, uint8_t prefix, uint8_t len, uint8_t *data) {
  char *ptr = lcdline[line].contents;

  memset(&lcdline[line],0,sizeof(linebuffer_t));
  lcdline[line].updated = 1;
  updatedisplay++;

  if (prefix)
    *ptr++ = prefix;

  data[0] += 1;
  if (data[0] > 100)
    *ptr++ = '0'+data[0]/100;
  if (data[0] > 10)
    *ptr++ = '0'+(data[0]%100)/10;
  *ptr++ = '0'+(data[0]%10);

  *ptr++ = ':';
  pet2asc(++data);
  while (*data <= 126 && --len > 0)
    *ptr++ = *data++;
}

/**
 * set_part - set the partition number in the status line
 * @part: new partition number
 *
 * This function updates the partition number in the fourth
 * line of the display to show part.
 */
static void set_part(uint8_t part) {
  char *ptr = &lcdline[3].contents[STATUS_PART];

  lcdline[3].contents[STATUS_PART]   = ' ';
  lcdline[3].contents[STATUS_PART+1] = ' ';
  lcdline[3].contents[STATUS_PART+2] = ' ';
  if (part > 100)
    *ptr++ = '0'+part/100;
  if (part > 10)
    *ptr++ = '0'+(part%100)/10;
  *ptr = '0'+(part%10);

  lcdline[3].updated = 1;
  updatedisplay++;
}

/**
 * parse_display - parse display message
 * @length: length of message
 * @data  : message contents
 *
 * This function parses a display message received over
 * I2C and acts upon it.
 */
static void parse_display(uint8_t length, uint8_t *data) {
  uint8_t i;

  switch (data[0]) {
  case DISPLAY_INIT:
    lcd_clrscr();
    menu_resetlines();
    menuentry = -1;
    menustate = MST_RESET;
    for (i=0;i<sizeof(lcdline)/sizeof(linebuffer_t);i++) {
      memset(&lcdline[i],0,sizeof(linebuffer_t));
    }
    for (i=0;i<length-1;i++)
      data[i+1] = tolower(data[i+1]);
    set_line(0, length-1, data+1, 0);
    strcpy_P(lcdline[3].contents, status_line_str);
    break;

  case DISPLAY_ADDRESS:
    lcdline[3].contents[STATUS_ADDR]   = '0'+data[1] / 10;
    lcdline[3].contents[STATUS_ADDR+1] = '0'+data[1] % 10;
    lcdline[3].updated = 1;
    updatedisplay++;
    break;

  case DISPLAY_FILENAME_READ:
    set_line_part(0, 'L', length-1, data+1);
    break;

  case DISPLAY_FILENAME_WRITE:
    set_line_part(0, 'S', length-1, data+1);
    break;

  case DISPLAY_DOSCOMMAND:
    set_line(0, length-1, data+1, 1);
    break;

  case DISPLAY_ERRORCHANNEL:
    set_line(2, length-1, data+1, 1);
    break;

  case DISPLAY_CURRENT_DIR:
    if (length == 2) {
      /* Special case: root dir */
      length = 3;
      data[2] = '/';
    }
    set_line_part(1, 0, length-1, data+1);
    break;

  case DISPLAY_CURRENT_PART:
    set_part(data[1]+1);
    break;

  case DISPLAY_MENU_RESET:
    menu_resetlines();
    menustate = MST_RESET;
    break;

  case DISPLAY_MENU_ADD:
    pet2asc(data+1);
    menu_addline((char *)data+1);
    break;

  case DISPLAY_MENU_SHOW:
    menustate = MST_INIT;
    if (length > 1)
      menuentry = data[1];
    else
      menuentry = 0;
    break;

  case DISPLAY_MENU_GETSELECTION:
    txdata = (uint8_t *)&menuentry;
    txlen = 1;
    break;

  case DISPLAY_MENU_GETENTRY:
    asc2pet((uint8_t *)menulines[menuentry]);
    txdata = (uint8_t *)menulines[menuentry];
    txlen = strlen(menulines[menuentry]);
    break;
  }
}

/* I2C interrupt function, calls parse_display */
ISR(TWI_vect) {
  //uart_puthex(TWSR & 0b11111000);uart_putcrlf();

  /* gcc produces 16-bit comparisons with masking instead of shifts */
  switch(TWSR >> 3) {

    /* ----- Slave receiver ----- */

  case (0x60 >> 3): // Own SLA+W received
    rxbufptr = rxbuffer;
    TWCR = _BV(TWEN) | _BV(TWIE) | _BV(TWINT) | _BV(TWEA); // Return ACK for data byte
    break;

  case (0x80 >> 3): // Own SLA+W active, data received and ACKed
    *rxbufptr = TWDR;
    rxbufptr++;
    if (rxbufptr == rxbuffer+sizeof(rxbuffer))
      TWCR = _BV(TWEN) | _BV(TWIE) | _BV(TWINT); // Buffer full, stop ACKing
    else
      TWCR = _BV(TWEN) | _BV(TWIE) | _BV(TWINT) | _BV(TWEA);
    break;

  case (0x88 >> 3): // Own SLA+W active, data received and NAKed
  case (0xa0 >> 3): // STOP condition received while we were addressed
    parse_display(rxbufptr-rxbuffer, rxbuffer);

    rxbufptr = rxbuffer;
    /* Return ACK for address */
    TWCR = _BV(TWEN) | _BV(TWIE) |  _BV(TWINT) | _BV(TWEA);
    break;

    /* ----- Slave transmitter ----- */

  case (0xa8 >> 3): // SLA+R received, ACK returned
  case (0xb8 >> 3): // Data byte transmitted in slave tx mode, ACK received
    /* Transmit a byte and expect ACK */
    set_intrq(1);
    if (txlen > 0) {
      TWDR = *txdata++;
      txlen--;
    } else
      TWDR = 0;
    TWCR = _BV(TWEN) | _BV(TWIE) | _BV(TWINT) | _BV(TWEA);
    break;

  case (0xc0 >> 3): // Data byte transmitted in slave tx mode, NACK received
  case (0xc8 >> 3): // Data byte transmitted in slave tx mode, ACK received whick NACK expected
    /* Switch to not-addressed slave mode */
    /* Return ACK for address */
    TWCR = _BV(TWEN) | _BV(TWIE) |  _BV(TWINT) | _BV(TWEA);
    break;


    /* ----- General status codes ----- */

  case (0xf8 >> 3): // No relevant state information available
    break;

  case (0x00 >> 3): // Illegal bus condition
    TWCR = _BV(TWEN) | _BV(TWIE) | _BV(TWSTO) | _BV(TWINT) | _BV(TWEA);
    break;

  default:
    //uart_puthex(TWSR & 0b11111000); uart_putcrlf();
    TWCR = _BV(TWEN) | _BV(TWIE) | _BV(TWINT) | _BV(TWEA);
    break;
  }
}

/**
 * update_display - update display contents based on lcdline array
 *
 * This function reads the contents of the lcdline array and
 * updates and/or scrolls lines as neccessary.
 */
static void update_display(void) {
  static uint8_t scrolling;
  char    tmpbuf[LCD_COLUMNS+1];
  char   *ptr;
  uint8_t i,j,flag;
  int8_t  scrollofs;

  flag = 0;
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    if (updatedisplay) {
      flag = 1;
      updatedisplay--;
    }
  }
  if (!flag && !scrolling)
    return;

  scrolling = 0;
  tmpbuf[LCD_COLUMNS] = 0;
  for (i=0;i<LCD_ROWS;i++) {
    ptr = tmpbuf;
    flag = 0;
    ATOMIC_BLOCK(ATOMIC_FORCEON) {
      if (lcdline[i].updated ||
          (lcdline[i].scrolling && time_after(ticks,lcdline[i].scrolltick))) {

        if (!lcdline[i].scrolling) {
          scrollofs = 0;
          if (strlen(lcdline[i].contents) > LCD_COLUMNS) {
            /* Initialize scrolling for this line */
            lcdline[i].scrolling = 1;
            lcdline[i].scrollofs = -1;
            lcdline[i].scrolltick = ticks + 2*TICKS_BEFORE_SCROLLING;
          }
        } else {
          /* Line is scrolling (scrolling gets reset when updated is set) */
          scrollofs = lcdline[i].scrollofs + 1;
          if (scrollofs == strlen(lcdline[i].contents)-LCD_COLUMNS) {
            /* Scrolled to the end: Wait longer */
            lcdline[i].scrolltick = ticks + 2*TICKS_BEFORE_SCROLLING;
          } else if (scrollofs > strlen(lcdline[i].contents)-LCD_COLUMNS) {
            /* Beyond the end: Reset, wait longer */
            scrollofs = 0;
            lcdline[i].scrolltick = ticks + 2*TICKS_BEFORE_SCROLLING;
          } else {
            lcdline[i].scrolltick = ticks + TICKS_PER_CHARACTER;
          }
          lcdline[i].scrollofs = scrollofs;
        }

        for (j=0;j<LCD_COLUMNS && lcdline[i].contents[j+scrollofs];j++)
          *ptr++ = lcdline[i].contents[j+scrollofs];
        for (;j<LCD_COLUMNS;j++)
          *ptr++ = ' ';

        lcdline[i].updated = 0;
        flag = 1;
      }
    }
    if (lcdline[i].scrolling)
        scrolling = 1;

    if (flag) {
      lcd_gotoxy(0,i);
      lcd_puts(tmpbuf);
    }
  }
}


int main(void) {
  // Disable JTAG
#if defined __AVR_ATmega644__ || defined __AVR_ATmega644P__ || defined __AVR_ATmega2561__
  asm volatile("in  r24, %0\n"
               "ori r24, 0x80\n"
               "out %0, r24\n"
               "out %0, r24\n"
               :
               : "I" (_SFR_IO_ADDR(MCUCR))
               : "r24"
               );
#else
#  error Unknown chip!
#endif

  /* I2C initialisation, no need to set TWBR because we don't act as master */
  HWI2C_PORT |= HWI2C_SDA | HWI2C_SCL;
  TWAR = DISPLAY_I2C_ADDR;
  TWCR = _BV(TWEA) | _BV(TWEN) | _BV(TWIE);

  INTRQ_SETUP();

  lcd_init();
  lcd_customchar(1,0,4,8,31,8,4,0,0); // define left arrow
  menu_init();
  encoder_init();
  timer_init();

  strcpy_P((char *)rxbuffer,PSTR("Waiting for data..."));
  set_line(0,strlen((char *)rxbuffer),rxbuffer,0);

  sei();

  uint8_t prevkey,curkey;

  prevkey = 0;
  while (1) {
    if (menustate == MST_INIT) {
      menuentry = menu_display(1,menuentry);
      menustate = MST_ACTIVE;
    } else if (menustate == MST_ACTIVE) {
      menuentry = menu_display(0,menuentry);
    } else if (menustate == MST_RESET) {
      lcd_clrscr();
      ATOMIC_BLOCK(ATOMIC_FORCEON) {
        updatedisplay++;
        for (uint8_t i=0;i<LCD_ROWS;i++)
          lcdline[i].updated = 1;
        menustate = MST_INACTIVE;
      }
    } else {
      update_display();
    }

    curkey = button_state;
    if (!prevkey && curkey) {
      if (menustate == MST_ACTIVE) {
        menustate = MST_RESET;
      }
      set_intrq(0);
    }

    prevkey = curkey;
  }
}
