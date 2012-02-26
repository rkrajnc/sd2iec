/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2012  Ingo Korb <ingo@akana.de>

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

   
   menu.c: Generic menu code

*/

#include "config.h"
#include <avr/io.h>
#include <string.h>
#include "encoder.h"
#include "lcd.h"
#include "timer.h"
#include "utils.h"
#include "menu.h"


char *menulines[CONFIG_MAX_MENU_ENTRIES];
static uint8_t entrycount;
static char *menuheap;
extern char __heap_start;

static void show_menu(char **entries, uint8_t count, uint8_t start) {
  uint8_t i,j;

  lcd_gotoxy(0,0);
  lcd_putch(' ');

  /* Output menu entries */
  for (i=0;i<min(count-start,LCD_ROWS);i++) {
    for (j=0;j<LCD_COLUMNS-1 && entries[i+start][j];j++)
      lcd_putch(entries[i+start][j]);
    for (;j<LCD_COLUMNS;j++)
      lcd_putch(' ');
  }

  /* Clear remaining rows */
  for (;i<LCD_ROWS;i++)
    for (j=0;j<LCD_COLUMNS;j++)
      lcd_putch(' ');
}

void menu_init(void) {
  /* Arrow for highlighing an entry */
  lcd_customchar(3,0x08,0x0c,0x0e,0x0f,0x0e,0x0c,0x08,0x00);
  menu_resetlines();
}

void menu_resetlines(void) {
  menuheap = &__heap_start;
  entrycount = 0;
}
  
uint8_t menu_addline(char *line) {
  /* Check remaining heap space */
  if (((uint16_t)menuheap+strlen(line)+1) > SP-32 || entrycount >= CONFIG_MAX_MENU_ENTRIES) {
    return 1;
  }

  strcpy(menuheap,line);
  menulines[entrycount++] = menuheap;
  menuheap += strlen(line)+1;

  return 0;
}


uint8_t menu_display(uint8_t init, uint8_t startentry) {
  static int8_t  prevoffset,curoffset,preventry,scrollofs;
  static int8_t  curencoder,prevencoder,curentry;
  static uint8_t entrylen;
  static tick_t  scrolltime;

  if (init) {
    lcd_setcursormode(LCD_CURSOR_NONE);
    lcd_clrscr();

    prevencoder = encoder_position;
    prevoffset  = -1;
    preventry   = curentry;
    scrollofs   = -1;
    scrolltime  = 0;
    entrylen    = strlen(menulines[curentry]);
    if (startentry < entrycount)
      curentry = startentry;
    else
      curentry = 0;

    curoffset = curentry-LCD_ROWS/2+1;
    if (curoffset+LCD_ROWS-1 >= entrycount) curoffset = entrycount-LCD_ROWS;
    if (curoffset < 0) curoffset = 0;
  }

  curencoder = encoder_position;

  /* Encoder position changed */
  if (curencoder != prevencoder) {
    curentry += (curencoder-prevencoder);
    if (curentry >= entrycount) curentry = 0;
    if (curentry < 0) curentry = entrycount-1;
  }

  /* Move offset until the marker is back into visible area */
  while (curentry-curoffset < 1 && curoffset > 0) curoffset--;
  while (curentry-curoffset >= LCD_ROWS-1 && curentry < entrycount-1) curoffset++;
  if (curentry-curoffset >= LCD_ROWS) {
    /* Special case for wrapping to the last entry */
    curoffset = curentry-LCD_ROWS+1;
  }

  /* Redraw when offset changed */
  if (curoffset != prevoffset) {
    show_menu(menulines,entrycount,curoffset);
    prevoffset = curoffset;
    scrollofs  = -1;

    lcd_gotoxy(0,curentry-curoffset);
    lcd_putch(3);

    entrylen = strlen(menulines[curentry]);
    preventry = curentry;
  } else if (preventry != curentry) {
    /* Move marker if only the entry changed */
    lcd_gotoxy(0,preventry-curoffset);
    lcd_putch(' ');

    if (scrollofs != -1) {
      uint8_t i;

      lcd_gotoxy(1,preventry-curoffset);
      for (i=0;i<LCD_COLUMNS-1;i++)
        lcd_putch(menulines[preventry][i]);
      scrollofs = -1;
    }

    lcd_gotoxy(0,curentry-curoffset);
    lcd_putch(3);

    entrylen = strlen(menulines[curentry]);

    preventry = curentry;
  }

  /* Current entry is too long -> scroll */
  if (entrylen >= LCD_COLUMNS) {
    if (scrollofs == -1) {
      /* Haven't scrolled yet, start in a few ticks */
      scrollofs  = 0;
      scrolltime = getticks() + TICKS_BEFORE_SCROLLING;
    } else if (time_after(getticks(),scrolltime)) {
      /* Scrolling */
      uint8_t i;

      scrollofs++;
      if (scrollofs > entrylen+2) scrollofs = 0;
      scrolltime += TICKS_PER_CHARACTER;

      lcd_gotoxy(1,curentry-curoffset);
      for (i=0;i<LCD_COLUMNS-1;i++) {
        if (scrollofs+i < entrylen) {
          lcd_putch(menulines[curentry][scrollofs+i]);
        } else if (scrollofs+i < entrylen+3) {
          lcd_putch(' ');
        } else
          lcd_putch(menulines[curentry][scrollofs+i-entrylen-3]);
      }
    }
  }

  prevencoder = curencoder;

  return curentry;
}
