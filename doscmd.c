/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>

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
#include "dirent.h"
#include "diskchange.h"
#include "eeprom.h"
#include "errormsg.h"
#include "fastloader.h"
#include "fatops.h"
#include "flags.h"
#include "iec.h"
#include "m2iops.h"
#include "parser.h"
#include "sdcard.h"
#include "uart.h"
#include "ustring.h"
#include "wrapops.h"
#include "doscmd.h"

#define CURSOR_RIGHT 0x1d

static void (*restart_call)(void) = 0;

typedef struct magic_value_s {
  uint16_t address;
  uint8_t  val[2];
}
magic_value_t;

/* These are address/value pairs used by some programs to detect a 1541. */
/* Currently we remember two bytes per address since that's the longest  */
/* block required. */
static const PROGMEM magic_value_t c1541_magics[] = {
  { 0xfea0, { 0x0d, 0xed } }, /* used by DreamLoad */
  { 0xe5c6, { 0x34, 0xb1 } }, /* used by DreamLoad */
  { 0xfffe, { 0x00, 0x00 } }, /* Disable AR6 fastloader */
  { 0,      { 0, 0 } }        /* end mark */
};

uint8_t globalflags;

uint8_t command_buffer[CONFIG_COMMAND_BUFFER_SIZE+2];
uint8_t command_length;

date_t date_match_start;
date_t date_match_end;

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

/* Parse parameters of block commands in the command buffer */
/* Returns number of parameters (up to 4) or <0 on error    */
static int8_t parse_blockparam(uint8_t values[]) {
  uint8_t paramcount = 0;
  uint8_t *str;

  str = ustrchr(command_buffer, ':');
  if (!str) {
    if (ustrlen(command_buffer) < 3)
      return -1;
    str = command_buffer + 2;
  }

  str++;

  while (*str && paramcount < 4) {
    /* Skip all spaces, cursor-rights and commas - CC7C */
    while (*str == ' ' || *str == 0x1d || *str == ',') str++;
    if (!*str)
      break;

    values[paramcount++] = parse_number(&str);
  }

  return paramcount;
}

static uint8_t parse_bool(void) {
  switch (command_buffer[2]) {
  case '+':
    return 1;

  case '-':
    return 0;

  default:
    set_error(ERROR_SYNTAX_UNKNOWN);
    return 255;
  }
}

/* ------------------------------------------------------------------------- */
/*  Command handlers                                                         */
/* ------------------------------------------------------------------------- */

/* ------------ */
/*  E commands  */
/* ------------ */

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

/* ------------ */
/*  M commands  */
/* ------------ */

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
#ifdef CONFIG_FC3
  if (detected_loader == FL_FC3_LOAD && address == 0x059a) {
    detected_loader = FL_NONE;
    load_fc3();
  }
  if (detected_loader == FL_FC3_SAVE && address == 0x059c) {
    detected_loader = FL_NONE;
    save_fc3();
  }
#endif
#ifdef CONFIG_DREAMLOAD
  if (detected_loader == FL_DREAMLOAD && address == 0x0700) {
    detected_loader = FL_NONE;
    load_dreamload();
  }
#endif
}

static void handle_memread(void) {
  uint16_t address, check;
  magic_value_t *p;

  if (command_length < 6)
    return;

  address = command_buffer[3] + (command_buffer[4]<<8);

  /* Check some special addresses used for drive detection. */
  p = (magic_value_t*) c1541_magics;
  while ( (check = pgm_read_word(&p->address)) ) {
    if (check == address) {
      error_buffer[0] = pgm_read_byte(p->val);
      error_buffer[1] = pgm_read_byte(p->val + 1);
      break;
    }
    p++;
  }

  /* possibly the host wants to read more bytes than error_buffer size */
  /* we ignore this knowing that we return nonsense in this case       */
  buffers[CONFIG_BUFFER_COUNT].data     = error_buffer;
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

#ifdef CONFIG_FC3
  if (datacrc == 0xf1bd) {
    detected_loader = FL_FC3_LOAD;
  }
  else if (datacrc == 0xbe56) {
    detected_loader = FL_FC3_SAVE;
  }
#endif
#ifdef CONFIG_DREAMLOAD
  if (datacrc == 0x1f5b) {
    detected_loader = FL_DREAMLOAD;
  }
#endif

  if (detected_loader == FL_NONE) {
    uart_puts_P(PSTR("M-W CRC result: "));
    uart_puthex(datacrc >> 8);
    uart_puthex(datacrc & 0xff);
    uart_putcrlf();
  }
}

/* ------------ */
/*  X commands  */
/* ------------ */

static void parse_xcommand(void) {
  uint8_t num;
  uint8_t *str;
  path_t path; // FIXME: use global? Dupe in parse_command

  switch (command_buffer[1]) {
  case 'B':
    /* Free-block count faking */
    num = parse_bool();
    if (num != 255) {
      if (num)
        globalflags |= FAT32_FREEBLOCKS;
      else
        globalflags &= (uint8_t)~FAT32_FREEBLOCKS;

      set_error_ts(ERROR_STATUS,device_address,0);
    }
    break;

  case 'E':
    /* Change file extension mode */
    str = command_buffer+2;
    if (*str == '+') {
      globalflags |= EXTENSION_HIDING;
    } else if (*str == '-') {
      globalflags &= (uint8_t)~EXTENSION_HIDING;
    } else {
      num = parse_number(&str);
      if (num > 4) {
        set_error(ERROR_SYNTAX_UNKNOWN);
      } else {
        file_extension_mode = num;
        if (num >= 3)
          globalflags |= EXTENSION_HIDING;
      }
    }
    set_error_ts(ERROR_STATUS,device_address,0);
    break;

  case 'J':
    /* Jiffy enable/disable */
    num = parse_bool();
    if (num != 255) {
      if (num)
        globalflags |= JIFFY_ENABLED;
      else
        globalflags &= (uint8_t)~JIFFY_ENABLED;

      set_error_ts(ERROR_STATUS,device_address,0);
    }
    break;

  case 'C':
    /* Calibration */
    str = command_buffer+2;
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
    if (parse_path(command_buffer+2, &path, &str, 0))
      return;

    set_changelist(&path, str);
    break;

  case '*':
    /* Post-* matching */
    num = parse_bool();
    if (num != 255) {
      if (num)
        globalflags |= POSTMATCH;
      else
        globalflags &= (uint8_t)~POSTMATCH;

      set_error_ts(ERROR_STATUS,device_address,0);
    }
    break;

#ifdef CONFIG_STACK_TRACKING
  case '?':
    /* Output the largest stack size seen */
    set_error_ts(ERROR_LONGVERSION,(RAMEND-minstack)>>8,(RAMEND-minstack)&0xff);
    break;
#else
  case '?':
    /* Output the long version string */
    set_error(ERROR_LONGVERSION);
    break;
#endif

  default:
    /* Unknown command, just show the status */
    set_error_ts(ERROR_STATUS,device_address,0);
    break;
  }
}

/* ------------ */
/*  B commands  */
/* ------------ */

static void parse_block(void) {
  uint8_t *str;
  buffer_t *buf;
  uint8_t params[4];
  int8_t  pcount;

  str = ustrchr(command_buffer, '-');
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
    buf = find_buffer(params[0]);
    if (!buf) {
      set_error(ERROR_NO_CHANNEL);
      return;
    }

    /* Use current partition for 0 */
    if (params[1] == 0)
      params[1] = current_part;

    if (*str == 'R') {
      read_sector(buf,params[1],params[2],params[3]);
      if (command_buffer[0] == 'B') {
        buf->position = 1;
        buf->lastused = buf->data[0];
      } else {
        buf->position = 0;
        buf->lastused = 255;
      }
    } else {
      if (command_buffer[0] == 'B')
        buf->data[0] = buf->position-1; // FIXME: Untested, verify!
      write_sector(buf,params[1],params[2],params[3]);
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

/* ------------ */
/*  U commands  */
/* ------------ */

void parse_user(void) {
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
      globalflags &= (uint8_t)~VC20MODE;
      break;
      
    case '-':
      globalflags |= VC20MODE;
      break;
      
    default:
      set_error(ERROR_SYNTAX_UNKNOWN);
      break;
    }
    break;
    
  case 'J':
  case ':':
    /* Reset - technically hard-reset */
    /* Faked because Ultima 5 sends UJ. */
    free_all_user_buffers(0);
    set_error(ERROR_DOSVERSION);
    break;

  case 202: /* Shift-J */
    /* The real hard reset command */
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
}

/* --------- */
/*  N - New  */
/* --------- */
void parse_new(void) {
  uint8_t *name, *id;
  uint8_t part;

  name = command_buffer+1;
  part = parse_partition(&name);
  name = ustrchr(command_buffer, ':');
  if (name++ == NULL) {
    set_error(ERROR_SYNTAX_NONAME);
    return;
  }

  id = ustrchr(name, ',');
  if (id != NULL) {
    *id = 0;
    id++;
  }

  format(part, name, id);
}

/* ------------ */
/*  R - Rename  */
/* ------------ */
void parse_rename(void) {
  path_t oldpath,newpath;
  uint8_t *oldname,*newname;
  struct cbmdirent dent;
  int8_t res;

  /* Find the boundary between the names */
  oldname = ustrchr(command_buffer,'=');
  if (oldname == NULL) {
    set_error(ERROR_SYNTAX_UNKNOWN);
    return;
  }
  *oldname++ = 0;

  /* Parse both names */
  if (parse_path(command_buffer+1, &newpath, &newname, 0))
    return;

  if (parse_path(oldname, &oldpath, &oldname, 0))
    return;

  /* Rename can't move files across directories */
  if (oldpath.fat != newpath.fat) {
    set_error(ERROR_FILE_NOT_FOUND);
    return;
  }

  /* Check for invalid characters in the new name */
  if (check_invalid_name(newname)) {
    /* This isn't correct for all cases, but for most. */
    set_error(ERROR_SYNTAX_UNKNOWN);
    return;
  }

  /* Don't allow an empty new name */
  /* The 1541 renames the file to "=" in this case, but I consider that a bug. */
  if (ustrlen(newname) == 0) {
    set_error(ERROR_SYNTAX_NONAME);
    return;
  }

  /* Check if the new name already exists */
  res = first_match(&newpath, newname, FLAG_HIDDEN, &dent);
  if (res == 0) {
    set_error(ERROR_FILE_EXISTS);
    return;
  }

  if (res > 0)
    /* first_match generated an error other than File Not Found, abort */
    return;

  /* Clear the FNF */
  set_error(ERROR_OK);

  /* Check if the old name exists */
  if (first_match(&oldpath, oldname, FLAG_HIDDEN, &dent))
    return;

  rename(&oldpath, &dent, newname);
}


/* ------------------------------------------------------------------------- */
/*  Main command parser function                                             */
/* ------------------------------------------------------------------------- */

void parse_doscommand(void) {
  uint8_t i,count;
  uint8_t *buf;
  struct cbmdirent dent;
  path_t path;
  uint8_t part;

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
    uart_trace(command_buffer,0,command_length);
  }
#endif

  /* Remove one CR at end of command */
  if (command_length > 0 && command_buffer[command_length-1] == 0x0d)
    command_length--;

  /* Abort if there is no command */
  if (command_length == 0) {
    set_error(ERROR_SYNTAX_UNABLE);
    return;
  }

#ifdef CONFIG_TURBODISK
  if (detected_loader == FL_TURBODISK) {
    /* Don't overwrite the file name in the final M-W command of Turbodisk */
    command_buffer[command_length] = 0;
  } else
#endif
  {
    /* Clear the remainder of the command buffer, simplifies parsing */
    memset(command_buffer+command_length, 0, sizeof(command_buffer)-command_length);
  }

  /* MD/CD/RD clash with other commands, so they're checked first */
  if (command_buffer[1] == 'D') {
    uint8_t *name;
    switch (command_buffer[0]) {
    case 'M':
      /* MD requires a colon */
      if (!ustrchr(command_buffer, ':')) {
        set_error(ERROR_SYNTAX_NONAME);
        break;
      }
      if (parse_path(command_buffer+2, &path, &name, 0))
        break;
      mkdir(&path,name);
      break;

    case 'C':
      if (parse_path(command_buffer+2, &path, &name, 1))
        break;

      if (ustrlen(name) != 0) {
        /* Path component after the : */
        if (name[0] == '_') {
          /* Going up a level - let chdir handle it. */
          if (chdir(&path,name))
            break;
        } else {
          /* A directory name - try to match it */
          if (first_match(&path, name, FLAG_HIDDEN, &dent))
            break;

          /* Move into it if it's a directory, use chdir if it's a file. */
          if ((dent.typeflags & TYPE_MASK) != TYPE_DIR) {
            if (chdir(&path, dent.name))
              break;
          } else
            partition[path.part].current_dir = dent.fatcluster;
        }
      } else {
        if (ustrchr(command_buffer, '/')) {
          partition[path.part].current_dir = path.fat;
        } else {
          set_error(ERROR_FILE_NOT_FOUND_39);
          break;
        }
      }

      if (globalflags & AUTOSWAP_ACTIVE)
        set_changelist(NULL, NULLSTRING);

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

      /* Parse partition number */
      buf=command_buffer+2;
      part=parse_partition(&buf);
      if (*buf != ':') {
        set_error(ERROR_SYNTAX_NONAME);
      } else {
        path.part = part;
        path.fat  = partition[part].current_dir;
        ustrcpy(dent.name, command_buffer+i+1);
        i = file_delete(&path, &dent);
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
  case 'B':
    /* Block-Something */
    parse_block();
    break;

  case 'C':
    /* Copy or Change Partition */
    switch(command_buffer[1]) {
    case 'P':
      /* Change Partition */
      buf=command_buffer+2;
      part=parse_partition(&buf);
      if(part>=max_part) {
        set_error_ts(ERROR_PARTITION_ILLEGAL,part+1,0);
        return;
      }
      
      current_part=part;
      if (globalflags & AUTOSWAP_ACTIVE)
        set_changelist(NULL, NULLSTRING);
      
      set_error_ts(ERROR_PARTITION_SELECTED, part+1, 0);
      break;
      
    case 0xd0: /* Shift-P */
      /* Change Partition, binary version */
      if (command_buffer[2] > max_part) {
        set_error_ts(ERROR_PARTITION_ILLEGAL, command_buffer[2], 0);
        return;
      }
      
      if (command_buffer[2]) {
        current_part = command_buffer[2]-1;
        if (globalflags & AUTOSWAP_ACTIVE)
          set_changelist(NULL, NULLSTRING);
      }
      set_error_ts(ERROR_PARTITION_SELECTED, command_buffer[2], 0);
      break;

    default:
      /* Throw an error because we don't handle copy yet */
      set_error(ERROR_SYNTAX_UNKNOWN);
      break;
    }
    break;

  case 'E':
    /* EEPROM-something */
    do { /* Create a block to get local variables */
      uint16_t address = command_buffer[3] + (command_buffer[4] << 8);
      uint8_t  length  = command_buffer[5];
      
      if (command_length < 6) {
        set_error(ERROR_SYNTAX_UNKNOWN);
        break;
      }
      
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

  case 'G':
    /* Get-Partition */
    if (command_length < 3) /* FIXME: should this set an error? */
      break;

    if (command_buffer[1] != '-' || command_buffer[2] != 'P') {
      set_error(ERROR_SYNTAX_UNKNOWN);
      break;
    }

    if (command_length == 3)
      path.part = current_part+1;
    else
      path.part = command_buffer[3];

    /* Valid partition number? */
    if (path.part >= max_part) {
      set_error(ERROR_PARTITION_ILLEGAL);
      break;
    }

    buffers[CONFIG_BUFFER_COUNT].position = 0;
    buffers[CONFIG_BUFFER_COUNT].lastused = 31;
    buf=buffers[CONFIG_BUFFER_COUNT].data;
    memset(buf,0,32);
    *(buf++) = 1; /* Partition type: native */
    buf++;

    *(buf++) = path.part+1;
    path.fat=0;
    if (disk_label(&path,buf))
      break;
    buf+= 16;
    *(buf++) = partition[path.part].fatfs.fatbase>>16;
    *(buf++) = partition[path.part].fatfs.fatbase>>8;
    *(buf++) = (partition[path.part].fatfs.fatbase & 0xff);
    buf++;

    uint32_t size = (partition[path.part].fatfs.max_clust - 1) * partition[path.part].fatfs.csize;
    *(buf++) = size>>16;
    *(buf++) = size>>8;
    *(buf++) = size;
    *buf = 13;
    break;

  case 'I':
    /* Initialize */
    if (disk_state != DISK_OK)
      set_error_ts(ERROR_READ_NOSYNC,18,0);
    else
      free_all_user_buffers(1);
    break;

  case 'M':
    /* Memory-something - just dump for later analysis */
#ifndef CONFIG_COMMAND_CHANNEL_DUMP
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

  case 'N':
    /* New */
    parse_new();
    break;

  case 'R':
    /* Rename */
    parse_rename();
    break;

  case 'S':
    if(command_length == 3 && command_buffer[1] == '-') {
      /* Swap drive number */
      set_error(ERROR_SYNTAX_UNABLE);
      break;
    }
    /* Scratch */
    parse_path(command_buffer+1, &path, &buf, 0);

    if (opendir(&matchdh, &path))
      return;

    i = 255;
    count = 0;
    while (!next_match(&matchdh, buf, NULL, NULL, FLAG_HIDDEN, &dent)) {
      /* Skip directories */
      if ((dent.typeflags & TYPE_MASK) == TYPE_DIR)
        continue;
      i = file_delete(&path, &dent);
      if (i != 255)
        count += i;
      else
        break;
    }
    if (i != 255)
      set_error_ts(ERROR_SCRATCHED,count,0);

    break;

  case 'U':
    parse_user();
    break;

  case 'X':
    parse_xcommand();
    break;

  default:
    set_error(ERROR_SYNTAX_UNKNOWN);
    break;
  }
}
