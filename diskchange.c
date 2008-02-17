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


   diskchange.c: Disk image changer

*/

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "errormsg.h"
#include "fatops.h"
#include "iec.h"
#include "ff.h"
#include "diskchange.h"

static const char PROGMEM autoswap_name[] = "AUTOSWAP.LST";

volatile uint8_t keycounter;

static FIL     swaplist;
static uint8_t linenum;

/* Timer-polling delay */
#define TICKS_PER_MS (F_CPU/(1000*1024.0))
static void tdelay(uint16_t ticks) {
  TCNT1 = 0;
  while (TCNT1 < ticks) ;
}

static void mount_line(void) {
  FRESULT res;
  UINT bytesread;
  buffer_t *buf;
  uint8_t i,*str,*strend;
  uint16_t curpos;

  /* Kill all buffers */
  free_all_buffers(0);

  /* Grab some scratch memory - this won't fail */
  buf = alloc_buffer();

  curpos = 0;
  strend = NULL;

  for (i=0;i<=linenum;i++) {
    str = buf->data;

    res = f_lseek(&swaplist,curpos);
    if (res != FR_OK) {
      parse_error(res,1);
      free_buffer(buf);
      return;
    }

    res = f_read(&swaplist, str, 256, &bytesread);
    if (res != FR_OK) {
      parse_error(res,1);
      free_buffer(buf);
      return;
    }

    /* Terminate string in buffer */
    if (bytesread < 256)
      str[bytesread] = 0;

    if (bytesread == 0) {
      /* End of file - restart loop to read the first entry */
      i = -1; /* I could've used goto instead... */
      linenum = 0;
      curpos = 0;
      continue;
    }

    /* Skip name */
    while (*str != '\r' && *str != '\n') str++;

    strend = str;

    /* Skip line terminator */
    while (*str == '\r' || *str == '\n') str++;

    curpos += str-buf->data;
  }

  /* Terminate file name */
  *strend = 0;

  if (fop != &fatops)
    image_unmount();

  /* Mount the disk image */
  fat_chdir((char *)buf->data);

  free_buffer(buf);

  if (current_error != 0)
    return;

  /* Confirmation blink */
  AUX_LED_ON();
  for (i=0;i<2;i++) {
    DIRTY_LED_ON();
    BUSY_LED_ON();
    tdelay(100 * TICKS_PER_MS);
    DIRTY_LED_OFF();
    BUSY_LED_OFF();
    tdelay(100 * TICKS_PER_MS);
  }
  while (keycounter == DISKCHANGE_MAX) ;
}

void set_changelist(char *filename) {
  FRESULT res;

  /* Assume this isn't the auto-swap list */
  iecflags.autoswap_active = 0;

  /* Remove the old swaplist */
  if (linenum != 255) {
    f_close(&swaplist);
    memset(&swaplist,0,sizeof(swaplist));
    linenum = 255;
  }

  /* Open a new swaplist */
  res = f_open(&swaplist, filename, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    parse_error(res,1);
    return;
  }

  linenum = 0;
  mount_line();
}


void change_disk(void) {
  if (linenum == 255) {
    /* No swaplist active, try using AUTOSWAP.LST */
    /* change_disk is called from the IEC idle loop, so entrybuf is free */
    strcpy_P((char *)entrybuf, autoswap_name);
    set_changelist((char *)entrybuf);
    if (linenum == 255) {
      /* No swap list found, clear error and exit */
      set_error(ERROR_OK);
      return;
    } else {
      /* Autoswaplist found, mark it as active                */
      /* and exit because the first image is already mounted. */
      iecflags.autoswap_active = 1;
      return;
    }
  }

  /* Mount the next image in the list */
  linenum++;
  mount_line();
}

void init_change(void) {
  /* Timer 1 counts F_CPU/1024, i.e. 1/8ths of a millisecond */
  TCCR1B = _BV(CS12) | _BV(CS10);

  memset(&swaplist,0,sizeof(swaplist));
  linenum = 255;
  iecflags.autoswap_active = 0;
}
