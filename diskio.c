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
uint32_t drive_config;

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
  switch(drv & 0xe) {
#ifdef HAVE_DF
  case DRIVE_CONFIG_DF_MASK:
    return df_status(drv & 1);
#endif

#ifdef HAVE_ATA
  case DRIVE_CONFIG_ATA1_MASK:
    return ata_status(drv & 1);

  case DRIVE_CONFIG_ATA2_MASK:
    return ata_status((drv & 1) + 2);
#endif

#ifdef HAVE_SD
  case DRIVE_CONFIG_SD_MASK:
    return sd_status(drv & 1);
#endif

  default:
    return STA_NOINIT|STA_NODISK;
  }
}

DSTATUS disk_initialize(BYTE drv) {
  switch(drv & 0xe) {
#ifdef HAVE_DF
  case DRIVE_CONFIG_DF_MASK:
    return df_initialize(drv & 1);
#endif

#ifdef HAVE_ATA
  case DRIVE_CONFIG_ATA1_MASK:
    return ata_initialize(drv & 1);

  case DRIVE_CONFIG_ATA2_MASK:
    return ata_initialize((drv & 1) + 2);
#endif

#ifdef HAVE_SD
  case DRIVE_CONFIG_SD_MASK:
    return sd_initialize(drv & 1);
#endif

  default:
    return STA_NOINIT|STA_NODISK;
  }
}
  
DRESULT disk_read(BYTE drv, BYTE *buffer, DWORD sector, BYTE count) {
  switch(drv & 0xe) {
#ifdef HAVE_DF
  case DRIVE_CONFIG_DF_MASK:
    return df_read(drv & 1,buffer,sector,count);
#endif

#ifdef HAVE_ATA
  case DRIVE_CONFIG_ATA1_MASK:
    return ata_read(drv & 1,buffer,sector,count);

  case DRIVE_CONFIG_ATA2_MASK:
    return ata_read((drv & 1) + 2,buffer,sector,count);
#endif

#ifdef HAVE_SD
  case DRIVE_CONFIG_SD_MASK:
    return sd_read(drv & 1,buffer,sector,count);
#endif

  default:
    return RES_ERROR;
  }
}
  
DRESULT disk_write(BYTE drv, const BYTE *buffer, DWORD sector, BYTE count) {
  switch(drv & 0xe) {
#ifdef HAVE_DF
  case DRIVE_CONFIG_DF_MASK:
    return df_write(drv & 1,buffer,sector,count);
#endif

#ifdef HAVE_ATA
  case DRIVE_CONFIG_ATA1_MASK:
    return ata_write(drv & 1,buffer,sector,count);

  case DRIVE_CONFIG_ATA2_MASK:
    return ata_write((drv & 1) + 2,buffer,sector,count);
#endif

#ifdef HAVE_SD
  case DRIVE_CONFIG_SD_MASK:
    return sd_write(drv & 1,buffer,sector,count);
#endif

  default:
    return RES_ERROR;
  }
}
