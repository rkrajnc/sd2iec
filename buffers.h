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


   buffers.h: Data structures for the internal buffers
*/

#ifndef BUFFERS_H
#define BUFFERS_H

#include <stdint.h>
#include "dirent.h"

#define BUFFER_SEC_SYSTEM 100

typedef enum { DIR_FMT_CBM, DIR_FMT_CMD_SHORT, DIR_FMT_CMD_LONG } dirformat_t;

/**
 * struct buffer_s - buffer handling structire
 * @data     : Pointer to the data area of the buffer, MUST be the first field
 * @lastused : Index to the last used byted
 * @position : Index of the byte that will be read/written next
 * @seconday : Secondary address the buffer is associated with
 * @allocated: Flags if the buffer is allocated or not
 * @mustflush: Flags if the buffer must be flushed before adding characters
 * @read     : Flags if the buffer was opened for reading
 * @write    : Flags if the buffer was opened for writing
 * @sendeoi  : Flags if the last byte should be sent with EOI
 * @refill   : Callback to refill/write out the buffer, returns true on error
 * @cleanup  : Callback to clean up and save remaining data, returns true on error
 *
 * Most allocated buffers point into the same bufferdata array, but
 * the error channel uses the same structure to avoid special-casing it
 * everywhere.
 */
typedef struct buffer_s {
  /* The error channel uses the same data structure for convenience reasons, */
  /* so data must be a pointer. It also allows swapping the buffers around   */
  /* in case I ever add external ram (not XRAM) to the design (which will    */
  /* require locking =( ).                                                   */
  uint8_t *data;
  uint8_t lastused;
  uint8_t position;
  uint8_t secondary;
  int     allocated:1;
  int     mustflush:1;
  int     read:1;
  int     write:1;
  int     sendeoi:1;
  uint8_t (*refill)(struct buffer_s *buffer);
  uint8_t (*cleanup)(struct buffer_s *buffer);

  /* private: */
  union {
    struct {
      dh_t dh;             /* Directory handle */
      uint8_t filetype;    /* File type */
      dirformat_t format;  /* Dir format */
      uint8_t *matchstr;   /* Pointer to filename pattern */
      date_t *match_start; /* Start matching date */
      date_t *match_end;   /* End matching date */
    } dir;
    FIL fh;                /* File access via FAT */
    d64fh_t d64;           /* File access on D64  */
    struct {
      uint8_t part;        /* partition number for $=P */
      uint8_t *matchstr;   /* Pointer to filename pattern */
    } pdir;
    struct {
      uint8_t refcount;    /* Reference counter, buffer is free if == 0 */
      uint8_t part;        /* partition number where the BAM came from */
      uint8_t sector;      /* BAM-sector (if more than one) */
    } bam;
  } pvt;
} buffer_t;

extern dh_t matchdh;         /// Directory handle used in file matching
extern buffer_t buffers[];   /// Simplifies access to the error buffer length

extern uint8_t entrybuf[33]; /// Buffer for directory entries to be parsed

/* Initializes the buffer structures */
void init_buffers(void);

/* Dummy callback */
uint8_t callback_dummy(buffer_t *buf);

/* Allocates a buffer for internal use */
buffer_t *alloc_system_buffer(void);

/* Allocates a buffer - returns pointer to buffer or NULL if failure */
buffer_t *alloc_buffer(void);

/* Deallocates a buffer */
void free_buffer(buffer_t *buffer);

/* Deallocates all user buffers, cleanup optional */
uint8_t free_all_user_buffers(uint8_t cleanup);

/* Deallocates all buffers, cleanup optional */
uint8_t free_all_buffers(uint8_t cleanup);

/* Finds the buffer corresponding to a secondary address */
/* Returns pointer to buffer on success or NULL on failure */
buffer_t *find_buffer(uint8_t secondary);

/* Number of currently allocated buffers + 16 * number of write buffers */
uint8_t active_buffers;

/* Check if any buffers are free */
#define check_free_buffers() ((active_buffers & 0x0f) < CONFIG_BUFFER_COUNT)

/* Check if any buffers are open for writing */
#define check_write_buf_count() ((active_buffers & 0xf0) != 0)

/* Mark a buffer as write-buffer */
void mark_write_buffer(buffer_t *buf);

/* AVR-specific hack: Address 1 is r1 which is always zero in C code */
#define NULLSTRING ((uint8_t *)1)

#endif
