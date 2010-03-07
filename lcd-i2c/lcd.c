/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2010  Ingo Korb <ingo@akana.de>

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


   lcd.c: HD44780-compatible display access

*/

#include "config.h"
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdio.h>
#include "lcd.h"

#define LCD_UPPER 1
#define LCD_LOWER 2
#define LCD_BOTH  3

#define DISPLAY_ON   0x0c

#define SET_DD_ADDRESS 0x80

uint8_t cursor_x;
uint8_t cursor_y;
uint8_t cursor_mode;

#ifdef USE_LCD_STDOUT
static FILE lcd_stdout = FDEV_SETUP_STREAM(lcd_putc, NULL, _FDEV_SETUP_WRITE);
#endif

static const PROGMEM uint8_t lcd_rowaddr[] = LCD_ROWADDR;
static uint8_t cursor_controller;

static void inline lcd1_strobe(void) {
  LCD_PORT |=  LCD_E1;
  _delay_us(1);
  LCD_PORT &= (uint8_t)~LCD_E1;
}

static void inline lcd2_strobe(void) {
  LCD_PORT |=  LCD_E2;
  _delay_us(1);
  LCD_PORT &= (uint8_t)~LCD_E2;
}

static void lcd_strobe(uint8_t controller) {
  if (controller & LCD_UPPER)
    lcd1_strobe();
  if ((LCD_ROWS_BOTTOM > 0) && (controller & LCD_LOWER))
    lcd2_strobe();
}


static void lcd_wait(uint8_t controller) {
  uint8_t tmp;

  LCD_DDR = LCD_RS | LCD_RW | LCD_E1 | LCD_E2;
  
  LCD_PORT |=  LCD_RW;
  LCD_PORT &= (uint8_t)~LCD_RS;

  if (controller & LCD_UPPER) {
    do {
      LCD_PORT |= LCD_E1;
      _delay_us(5);
      tmp = (LCD_PIN >> LCD_DATA_SHIFT) & 0x08;
      LCD_PORT &= (uint8_t)~LCD_E1;
      _delay_us(5);
      LCD_PORT |= LCD_E1;
      _delay_us(5);
      LCD_PORT &= (uint8_t)~LCD_E1;
    } while (tmp);
  }
  if ((LCD_ROWS_BOTTOM > 0) && (controller & LCD_LOWER)) {
    do {
      LCD_PORT |= LCD_E2;
      _delay_us(5);
      tmp = (LCD_PIN >> LCD_DATA_SHIFT) & 0x08;
      LCD_PORT &= (uint8_t)~LCD_E2;
      _delay_us(5);
      LCD_PORT |= LCD_E2;
      _delay_us(5);
      LCD_PORT &= (uint8_t)~LCD_E2;
    } while (tmp);
  }
  LCD_PORT = 0;
  LCD_DDR  = 0xff;
}

static void lcd_write(uint8_t controller, uint8_t rs, uint8_t value) {
  uint8_t tmp;

  if (rs)
    tmp = LCD_RS;
  else
    tmp = 0;

  lcd_wait(controller);

  LCD_PORT = tmp | (uint8_t)((value >> 4) << LCD_DATA_SHIFT);
  lcd_strobe(controller);
  LCD_PORT = tmp | (uint8_t)((value & 0x0f) << LCD_DATA_SHIFT);
  lcd_strobe(controller);
}

void lcd_init(void) {
  LCD_PORT = 0;
  LCD_DDR  = 0xff;
  _delay_ms(15);

  LCD_PORT = 0x03 << LCD_DATA_SHIFT;
  lcd_strobe(LCD_BOTH);
  _delay_ms(5);
  lcd_strobe(LCD_BOTH);
  _delay_ms(0.100);
  lcd_strobe(LCD_BOTH);
  lcd_wait(LCD_BOTH);

  LCD_PORT = 0x02 << LCD_DATA_SHIFT;
  lcd_strobe(LCD_BOTH);
  lcd_wait(LCD_BOTH);

  lcd_write(LCD_BOTH,0,0x28); // System Set: 4 Bit, 2 Zeilen, 5x7
  // FIXME: Einmal sollte eigentlich reichen?
  lcd_write(LCD_BOTH,0,0x28); // System Set: 4 Bit, 2 Zeilen, 5x7
  lcd_write(LCD_BOTH,0,0x08); // Display aus
  lcd_write(LCD_BOTH,0,0x0c); // Display an, kein Cursor
  lcd_write(LCD_BOTH,0,0x06); // Cursor beim Schreiben vorwaerts
  lcd_write(LCD_BOTH,0,0x01); // Anzeige loeschen

  cursor_x = 0;
  cursor_y = 0;
  cursor_mode = LCD_CURSOR_NONE;
  cursor_controller = LCD_UPPER;

#ifdef USE_LCD_STDOUT
  stdout = &lcd_stdout;
#endif
}

// Anzeige des Cursors auf richtigem Display aktivieren
void static updatecursor(void) {
  if (LCD_ROWS_BOTTOM == 0 || cursor_y < LCD_ROWS_TOP) {
    lcd_write(LCD_UPPER,0,DISPLAY_ON | cursor_mode);
    lcd_write(LCD_LOWER,0,DISPLAY_ON);
  } else {
    lcd_write(LCD_UPPER,0,DISPLAY_ON);
    lcd_write(LCD_LOWER,0,DISPLAY_ON | cursor_mode);
  }
}

void lcd_clrscr(void) {
  lcd_write(LCD_BOTH,0,0x01);
  cursor_x = 0;
  cursor_y = 0;
  cursor_controller = LCD_UPPER;
  updatecursor();
}

void lcd_setcursormode(uint8_t mode) {
  cursor_mode = mode;
  lcd_gotoxy(cursor_x,cursor_y);
}

void lcd_gotoxy(uint8_t x, uint8_t y) {
  cursor_x = x;
  cursor_y = y;
  if (LCD_ROWS_BOTTOM == 0 || y < LCD_ROWS_TOP) {
    lcd_write(LCD_UPPER,0,SET_DD_ADDRESS + x +
              pgm_read_byte(lcd_rowaddr + y));
    cursor_controller = LCD_UPPER;
  } else {
    lcd_write(LCD_LOWER,0,SET_DD_ADDRESS + x +
              pgm_read_byte(lcd_rowaddr + y - LCD_ROWS_TOP));
    cursor_controller = LCD_LOWER;
  }
}

void lcd_putch(char c) {
  lcd_write(cursor_controller,1,c);
  if (++cursor_x > LCD_COLUMNS-1)
    lcd_gotoxy(0,(cursor_y+1) % LCD_ROWS);
}

#ifdef USE_LCD_STDOUT
int lcd_putc(char c, FILE *stream) {
  if (c == '\n') {
    lcd_gotoxy(0,(cursor_y+1) % LCD_ROWS);
  } else {
    lcd_putch(c);
  }
  return 0;
}
#endif

void lcd_puts_P(prog_char *text) {
  uint8_t ch;

  while ((ch = pgm_read_byte(text++))) {
    if (ch == '\n') {
      lcd_gotoxy(0,(cursor_y+1) % LCD_ROWS);
    } else {
      lcd_putch(ch);
    }
  }
  updatecursor();
}

void lcd_putxy_P(uint8_t xpos, uint8_t ypos, prog_char *text) {
  if (xpos > (LCD_COLUMNS-1) || ypos > (LCD_ROWS-1)) return;

  lcd_gotoxy(xpos,ypos);
  lcd_puts_P(text);
}

void lcd_puts(char *text) {
  while (*text) {
    lcd_putch(*text++);
  }
  updatecursor();
}

void lcd_putxy(uint8_t xpos, uint8_t ypos, char *text) {
  if (xpos > (LCD_COLUMNS-1) || ypos > (LCD_ROWS-1)) return;

  lcd_gotoxy(xpos,ypos);
  lcd_puts(text);
}

void lcd_customchar(uint8_t num, uint8_t b0, uint8_t b1,
		    uint8_t b2,  uint8_t b3, uint8_t b4,
		    uint8_t b5,  uint8_t b6, uint8_t b7) {
  if (num > 7) return;
  lcd_write(LCD_BOTH,0,0x40+8*num);
  lcd_write(LCD_BOTH,1,b0);
  lcd_write(LCD_BOTH,1,b1);
  lcd_write(LCD_BOTH,1,b2);
  lcd_write(LCD_BOTH,1,b3);
  lcd_write(LCD_BOTH,1,b4);
  lcd_write(LCD_BOTH,1,b5);
  lcd_write(LCD_BOTH,1,b6);
  lcd_write(LCD_BOTH,1,b7);
  lcd_gotoxy(cursor_x,cursor_y);
}
