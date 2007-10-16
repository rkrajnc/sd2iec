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

   
   errormsg.h: Definitions for the error message generator

*/

#ifndef ERRORMSG_H
#define ERRORMSG_H

#include <stdint.h>

extern uint8_t error_buffer[ERROR_BUFFER_SIZE];
extern volatile uint8_t error_blink_active;


void set_error(uint8_t errornum, uint8_t track, uint8_t sector);

// Commodore DOS error codes
#define ERROR_OK                  0
#define ERROR_SCRATCHED           1
#define ERROR_READ_NOHEADER      20
#define ERROR_READ_NOSYNC        21
#define ERROR_READ_NODATA        22
#define ERROR_READ_CHECKSUM      23
#define ERROR_WRITE_VERIFY       25
#define ERROR_WRITE_PROTECT      26
#define ERROR_READ_HDRCHECKSUM   27
#define ERROR_DISK_ID_MISMATCH   29
#define ERROR_SYNTAX_UNKNOWN     30
#define ERROR_SYNTAX_UNABLE      31
#define ERROR_SYNTAX_TOOLONG     32
#define ERROR_SYNTAX_JOKER       33
#define ERROR_SYNTAX_NONAME      34
#define ERROR_FILE_NOT_FOUND_AS  39
#define ERROR_RECORD_MISSING     50
#define ERROR_RECORD_OVERFLOW    51
#define ERROR_FILE_TOO_LARGE     52
#define ERROR_WRITE_FILE_OPEN    60
#define ERROR_FILE_NOT_OPEN      61
#define ERROR_FILE_NOT_FOUND     62
#define ERROR_FILE_EXISTS        63
#define ERROR_FILE_TYPE_MISMATCH 64
#define ERROR_NO_BLOCK           65
#define ERROR_ILLEGAL_TS_COMMAND 66
#define ERROR_ILLEGAL_TS_LINK    67
#define ERROR_NO_CHANNEL         70
#define ERROR_DIR_ERROR          71
#define ERROR_DISK_FULL          72
#define ERROR_DOSVERSION         73
#define ERROR_DRIVE_NOT_READY    74

#endif
