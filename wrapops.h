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

/**
 * struct fileops_t - function pointers to file operations
 * @open_read   : open a file for reading
 * @open_write  : open a file for writing/appending
 * @file_delete : delete a file
 * @disk_label  : read disk label
 * @disk_id     : read disk id
 * @disk_free   : read free space of disk
 * @read_sector : read a sector from the disk
 * @write_sector: write a sector to the disk
 * @opendir     : open a directory
 * @readdir     : read an entry from a directory
 * @mkdir       : create a directory
 * @chdir       : change current directory
 *
 * This structure holds function pointers for the various
 * abstracted operations on the supported file systems/images.
 * Instances of this structure must always be allocated in flash
 * and no field may be set to NULL.
 */
typedef struct {
  void     (*open_read)(char *path, char *name, buffer_t *buf);
  void     (*open_write)(char *path, char *name, uint8_t type, buffer_t *buf, uint8_t append);
  uint8_t  (*file_delete)(char *path, char *name);
  uint8_t  (*disk_label)(char *label);
  uint8_t  (*disk_id)(char *id);
  uint16_t (*disk_free)(void);
  void     (*read_sector)(buffer_t *buf, uint8_t track, uint8_t sector);
  void     (*write_sector)(buffer_t *buf, uint8_t track, uint8_t sector);
  uint8_t  (*opendir)(dh_t *dh, char *path);
  int8_t   (*readdir)(dh_t *dh, struct cbmdirent *dent);
  void     (*mkdir)(char *dirname);
  void     (*chdir)(char *dirname);
} fileops_t;

/* Pointer to the current fileops struct */
extern const fileops_t *fop;

/* Helper-Define to avoid lots of typedefs */
#define pgmcall(x) ((typeof(x))pgm_read_word(&(x)))

/* Wrappers to make the indirect calls look like normal functions */
#define open_read(dir,name,buf) ((pgmcall(fop->open_read))(dir,name,buf))
#define open_write(dir,name,type,buf,app) ((pgmcall(fop->open_write))(dir,name,type,buf,app))
#define file_delete(dir,name) ((pgmcall(fop->file_delete))(dir,name))
#define disk_label(label) ((pgmcall(fop->disk_label))(label))
#define disk_id(id) ((pgmcall(fop->disk_id))(id))
#define disk_free() ((pgmcall(fop->disk_free))())
#define read_sector(buf,t,s) ((pgmcall(fop->read_sector))(buf,t,s))
#define write_sector(buf,t,s) ((pgmcall(fop->write_sector))(buf,t,s))
#define opendir(dh,dir) ((pgmcall(fop->opendir))(dh,dir))
#define readdir(dh,dent) ((pgmcall(fop->readdir))(dh,dent))
#define mkdir(dir) ((pgmcall(fop->mkdir))(dir))
#define chdir(dir) ((pgmcall(fop->chdir))(dir))

#endif
