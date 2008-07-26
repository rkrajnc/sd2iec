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


   diskio.h: Definitions for the disk access routines

   Based on diskio.h from FatFS by ChaN, see ff.c for
   for full copyright details.

*/

#ifndef DISKIO_H
#define DISKIO_H

#include "integer.h"

/* Status of Disk Functions */
typedef BYTE    DSTATUS;

/* Disk Status Bits (DSTATUS) */
#define STA_NOINIT              0x01    /* Drive not initialized */
#define STA_NODISK              0x02    /* No medium in the drive */
#define STA_PROTECT             0x04    /* Write protected */

/* Results of Disk Functions */
typedef enum {
        RES_OK = 0,             /* 0: Successful */
        RES_ERROR,              /* 1: R/W Error */
        RES_WRPRT,              /* 2: Write Protected */
        RES_NOTRDY,             /* 3: Not Ready */
        RES_PARERR              /* 4: Invalid Parameter */
} DRESULT;


/*---------------------------------------*/
/* Prototypes for disk control functions */

DSTATUS disk_initialize (BYTE);
DSTATUS disk_status (BYTE);
DRESULT disk_read (BYTE, BYTE*, DWORD, BYTE);
DRESULT disk_write (BYTE, const BYTE*, DWORD, BYTE);
#define disk_ioctl(a,b,c) RES_OK

void init_disk(void);

/* Will be set to DISK_ERROR if any access on the card fails */
enum diskstates { DISK_CHANGED = 0, DISK_REMOVED, DISK_OK, DISK_ERROR };

extern volatile enum diskstates disk_state;

#endif
