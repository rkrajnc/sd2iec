/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007  Ingo Korb <ingo@akana.de>

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

   
   wrapops.h: Lots of ugly defines to allow switchable file operations

   The structure-of-pgmspace-function-pointers access scheme used here
   was inspired by code from MMC2IEC by Lars Pontoppidan.

   typeof-usage suggested by T. Diedrich.
*/

#ifndef WRAPOPS_H
#define WRAPOPS_H

#include <avr/pgmspace.h>
#include "buffers.h"
#include "fileops.h"
#include "tff.h"

typedef struct {
  void     (*open_read)(char *path, char *name, buffer_t *buf);
  void     (*open_write)(char *path, char *name, buffer_t *buf, uint8_t append);
  uint8_t  (*file_delete)(char *path, char *name);
  uint8_t  (*disk_label)(char *label);
  uint8_t  (*disk_id)(char *id);
  uint16_t (*disk_free)(void);
  uint8_t  (*opendir)(dh_t *dh, char *path);
  int8_t   (*readdir)(dh_t *dh, struct cbmdirent *dent);
} fileops_t;

/* Pointer to the current fileops struct */
extern const fileops_t *fop;

/* Helper-Define to avoid lots of typedefs */
#define pgmcall(x) ((typeof(x))pgm_read_word(&(x)))

/* Wrappers to make the indirect calls look like normal functions */
#define open_read(dir,name,buf) ((pgmcall(fop->open_read))(dir,name,buf))
#define open_write(dir,name,buf,app) ((pgmcall(fop->open_write))(dir,name,buf,app))
#define file_delete(dir,name) ((pgmcall(fop->file_delete))(dir,name))
#define disk_label(label) ((pgmcall(fop->disk_label))(label))
#define disk_id(id) ((pgmcall(fop->disk_id))(id))
#define disk_free() ((pgmcall(fop->disk_free))())
#define opendir(dh,dir) ((pgmcall(fop->opendir))(dh,dir))
#define readdir(dh,dent) ((pgmcall(fop->readdir))(dh,dent))

#endif
