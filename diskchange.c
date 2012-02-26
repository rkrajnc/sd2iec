/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2012  Ingo Korb <ingo@akana.de>

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


   diskchange.c: Disk image changer

*/

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "config.h"
#include "buffers.h"
#include "display.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fatops.h"
#include "flags.h"
#include "ff.h"
#include "led.h"
#include "parser.h"
#include "timer.h"
#include "ustring.h"
#include "diskchange.h"

static const char PROGMEM autoswap_name[] = "AUTOSWAP.LST";

static FIL     swaplist;
static path_t  swappath;
static uint8_t linenum;

#define BLINK_BACKWARD 1
#define BLINK_FORWARD  2
#define BLINK_HOME     3

static void confirm_blink(uint8_t type) {
  uint8_t i;

  for (i=0;i<2;i++) {
    tick_t targettime;

#ifdef SINGLE_LED
    DIRTY_LED_ON();
#else
    if (!i || type & 1)
      DIRTY_LED_ON();
    if (!i || type & 2)
      BUSY_LED_ON();
#endif
    targettime = ticks + MS_TO_TICKS(100);
    while (time_before(ticks,targettime)) ;

    DIRTY_LED_OFF();
    BUSY_LED_OFF();
    targettime = ticks + MS_TO_TICKS(100);
    while (time_before(ticks,targettime)) ;
  }
}

static uint8_t mount_line(void) {
  FRESULT res;
  UINT bytesread;
  uint8_t i,*str,*strend;
  uint16_t curpos;

  uint8_t olderror = current_error;
  current_error = ERROR_OK;

  /* Kill all buffers */
  free_multiple_buffers(FMB_USER_CLEAN);

  curpos = 0;
  strend = NULL;

  for (i=0;i<=linenum;i++) {
    str = command_buffer;

    res = f_lseek(&swaplist,curpos);
    if (res != FR_OK) {
      parse_error(res,1);
      return 0;
    }

    res = f_read(&swaplist, str, CONFIG_COMMAND_BUFFER_SIZE, &bytesread);
    if (res != FR_OK) {
      parse_error(res,1);
      return 0;
    }

    /* Terminate string in buffer */
    if (bytesread < CONFIG_COMMAND_BUFFER_SIZE)
      str[bytesread] = 0;

    if (bytesread == 0) {
      if (linenum == 255) {
        /* Last entry requested, found it */
        linenum = i-1;
      } else {
        /* End of file - restart loop to read the first entry */
        linenum = 0;
      }
      i = -1; /* I could've used goto instead... */
      curpos = 0;
      continue;
    }

    /* Skip name */
    while (*str != '\r' && *str != '\n') str++;

    strend = str;

    /* Skip line terminator */
    while (*str == '\r' || *str == '\n') str++;

    curpos += str - command_buffer;
  }

  /* Terminate file name */
  *strend = 0;

  if (partition[swappath.part].fop != &fatops)
    image_unmount(swappath.part);

  /* Parse the path */
  path_t path;

  /* Start in the partition+directory of the swap list */
  current_part = swappath.part;
  display_current_part(current_part);
  partition[current_part].current_dir = swappath.dir;

  if (parse_path(command_buffer, &path, &str, 0)) {
    current_error = olderror;
    return 0;
  }

  /* Mount the disk image */
  cbmdirent_t dent;

  if (!first_match(&path, str, FLAG_HIDDEN, &dent)) {
    chdir(&path, &dent);
    update_current_dir(&path);
  }

  if (current_error != 0 && current_error != ERROR_DOSVERSION) {
    current_error = olderror;
    return 0;
  }

  return 1;
}

void set_changelist(path_t *path, uint8_t *filename) {
  FRESULT res;

  /* Assume this isn't the auto-swap list */
  globalflags &= (uint8_t)~AUTOSWAP_ACTIVE;

  /* Remove the old swaplist */
  if (swaplist.fs != NULL) {
    f_close(&swaplist);
    memset(&swaplist,0,sizeof(swaplist));
  }

  if (ustrlen(filename) == 0)
    return;

  /* Open a new swaplist */
  partition[path->part].fatfs.curr_dir = path->dir.fat;
  res = f_open(&partition[path->part].fatfs, &swaplist, filename, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) {
    parse_error(res,1);
    return;
  }

  /* Remember its directory so relative paths work */
  swappath = *path;

  linenum = 0;
  if (mount_line())
    confirm_blink(BLINK_HOME);
}


void change_disk(void) {
  path_t path;

  if (swaplist.fs == NULL) {
    /* No swaplist active, try using AUTOSWAP.LST */
    /* change_disk is called from the IEC idle loop, so entrybuf is free */
    reset_key(0xff); // <- lazy
    ustrcpy_P(entrybuf, autoswap_name);
    path.dir  = partition[current_part].current_dir;
    path.part = current_part;
    set_changelist(&path, entrybuf);
    if (swaplist.fs == NULL) {
      /* No swap list found, clear error and exit */
      set_error(ERROR_OK);
      return;
    } else {
      /* Autoswaplist found, mark it as active                */
      /* and exit because the first image is already mounted. */
      globalflags |= AUTOSWAP_ACTIVE;
      return;
    }
  }

  /* Mount the next image in the list */
  if (key_pressed(KEY_NEXT)) {
    linenum++;
    reset_key(KEY_NEXT);
    if (mount_line())
      confirm_blink(BLINK_FORWARD);
  } else if (key_pressed(KEY_PREV)) {
    linenum--;
    reset_key(KEY_PREV);
    if (mount_line())
      confirm_blink(BLINK_BACKWARD);
  } else if (key_pressed(KEY_HOME)) {
    linenum = 0;
    reset_key(KEY_HOME);
    if (mount_line())
      confirm_blink(BLINK_HOME);
  }
}

void change_init(void) {
  memset(&swaplist,0,sizeof(swaplist));
  globalflags &= (uint8_t)~AUTOSWAP_ACTIVE;
}
