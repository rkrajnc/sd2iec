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


   diskio.c: Generic disk access routines and supporting stuff

*/

#include "config.h"
#include "diskio.h"
#include "ata.h"
#include "dataflash.h"
#include "sdcard.h"

volatile enum diskstates disk_state;

#ifdef NEED_DISKMUX

/* Very stupid and incomplete implementation of the disk mux, */
/* just as a template and for easier dataflash testing.       */

#if defined(HAVE_SD) && defined(HAVE_ATA)
#  define SD_OFFSET 2
#else
#  define SD_OFFSET 0
#endif

void init_disk(void) {
#ifdef HAVE_SD
  init_sd();
#endif
#ifdef HAVE_ATA
  init_ata();
#endif
#ifdef HAVE_DF
  init_df();
#endif
}

DSTATUS disk_status(BYTE drv) {
#ifdef HAVE_DF
  if (drv == MAX_DRIVES-1)
    return df_status(0);
#endif
#ifdef HAVE_ATA
  if (drv < 2)
    return ata_status(drv);
#endif
#ifdef HAVE_SD
  return sd_status(drv-SD_OFFSET);
#endif
  return STA_NOINIT|STA_NODISK;
}

DSTATUS disk_initialize(BYTE drv) {
#ifdef HAVE_DF
  if (drv == MAX_DRIVES-1)
    return df_initialize(0);
#endif
#ifdef HAVE_ATA
  if (drv < 2)
    return ata_initialize(drv);
#endif
#ifdef HAVE_SD
  return sd_initialize(drv-SD_OFFSET);
#endif
  return STA_NOINIT|STA_NODISK;
}
  
DRESULT disk_read(BYTE drv, BYTE *buffer, DWORD sector, BYTE count) {
#ifdef HAVE_DF
  if (drv == MAX_DRIVES-1)
    return df_read(drv,buffer,sector,count);
#endif
#ifdef HAVE_ATA
  if (drv < 2)
    return ata_read(drv,buffer,sector,count);
#endif
#ifdef HAVE_SD
  return sd_read(drv,buffer,sector,count);
#endif
  return RES_ERROR;
}
  
DRESULT disk_write(BYTE drv, const BYTE *buffer, DWORD sector, BYTE count) {
#ifdef HAVE_DF
  if (drv == MAX_DRIVES-1)
    return df_write(drv,buffer,sector,count);
#endif
#ifdef HAVE_ATA
  if (drv < 2)
    return ata_write(drv,buffer,sector,count);
#endif
#ifdef HAVE_SD
  return sd_write(drv,buffer,sector,count);
#endif
  return RES_ERROR;
}
  

#endif
