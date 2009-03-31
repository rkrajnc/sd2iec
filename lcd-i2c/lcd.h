/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2009  Ingo Korb <ingo@akana.de>

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


   lcd.h: HD44780-compatible display access

*/

#ifndef LCD_H
#define LCD_H

#include <avr/pgmspace.h>

#define LCD_CURSOR_NONE  0x00
#define LCD_CURSOR_LINE  0x02
#define LCD_CURSOR_BLOCK 0x01

#define LCD_ROWS    (LCD_ROWS_TOP+LCD_ROWS_BOTTOM)

void lcd_init(void);
void lcd_clrscr(void);
void lcd_putxy_P(uint8_t xpos, uint8_t ypos, prog_char *text);
void lcd_putxy(uint8_t xpos, uint8_t ypos, char *text);
void lcd_puts_P(prog_char *text);
void lcd_puts(char *text);
void lcd_setcursormode(uint8_t mode);
void lcd_gotoxy(uint8_t x, uint8_t y);
void lcd_putch(char c);
void lcd_customchar(uint8_t num, uint8_t b0, uint8_t b1,
		    uint8_t b2,  uint8_t b3, uint8_t b4,
		    uint8_t b5,  uint8_t b6, uint8_t b7);

#ifdef USE_LCD_STDOUT
# include <stdio.h>
int lcd_putc(char c, FILE *stream);
#endif

extern uint8_t cursor_x;
extern uint8_t cursor_y;
extern uint8_t cursor_mode;

#endif
