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
*/

#ifndef WRAPOPS_H
#define WRAPOPS_H

#include <avr/pgmspace.h>
#include "buffers.h"
#include "fileops.h"
#include "tff.h"

typedef void (*WRAP_READ)(char *, char *, buffer_t *);
typedef void (*WRAP_WRITE)(char *, char *, buffer_t *, uint8_t);
typedef uint8_t (*WRAP_DELETE)(char *, char *);
typedef uint8_t (*WRAP_LABEL)(char *);
typedef uint8_t (*WRAP_OPENDIR)(DIR *, char *);
typedef int8_t  (*WRAP_READDIR)(DIR *, struct cbmdirent *);
typedef uint16_t (*WRAP_FREEBLOCKS)(void);

typedef struct {
  WRAP_READ       open_read;
  WRAP_WRITE      open_write;
  WRAP_DELETE     file_delete;
  WRAP_LABEL      disk_label;
  WRAP_LABEL      disk_id;
  WRAP_FREEBLOCKS disk_free;
  WRAP_OPENDIR    opendir;
  WRAP_READDIR    readdir;
} fileops_t;

/* Pointer to the current fileops struct */
extern const fileops_t *fop;

/* Please note: This is still C, even though it may look like Lisp */
#define open_read(dir,name,buf) (((WRAP_READ)(pgm_read_word(&(fop->open_read))))(dir,name,buf))
#define open_write(dir,name,buf,app) (((WRAP_WRITE)(pgm_read_word(&(fop->open_write))))(dir,name,buf,app))
#define file_delete(dir,name) (((WRAP_DELETE)(pgm_read_word(&(fop->file_delete))))(dir,name))
#define disk_label(label) (((WRAP_LABEL)(pgm_read_word(&(fop->disk_label))))(label))
#define disk_id(id) (((WRAP_LABEL)(pgm_read_word(&(fop->disk_id))))(id))
#define disk_free() (((WRAP_FREEBLOCKS)(pgm_read_word(&(fop->disk_free))))())
#define opendir(dh,dir) (((WRAP_OPENDIR)(pgm_read_word(&(fop->opendir))))(dh,dir))
#define readdir(dh,dent) (((WRAP_READDIR)(pgm_read_word(&(fop->readdir))))(dh,dent))

#endif
