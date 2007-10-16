/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2007        */
/*-----------------------------------------------------------------------*/
/* This is a stub disk I/O module that acts as front end of the existing */
/* disk I/O modules and attach it to FatFs module with common interface. */
/*-----------------------------------------------------------------------*/

/* 2007-09-14: Hacked up interface to sdcard.c from MMC2IEC              */
/*             FIXME: Integrate this into sdcard.c or change tff to      */
/*                    sdcard.c interface                                 */

#include <avr/io.h>
#include "config.h"
#include "diskio.h"
#include "sdcard.h"


/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */

DSTATUS disk_initialize (
	BYTE drv				/* Physical drive nmuber (0..) */
)
{
  if (sdReset())
    return disk_status(drv);
  else
    return STA_NOINIT | STA_NODISK;
}

/*-----------------------------------------------------------------------*/
/* Return Disk Status                                                    */

DSTATUS disk_status (
	BYTE drv		/* Physical drive nmuber (0..) */
)
{
  if (SDCARD_DETECT)
    if (SDCARD_WP)
      return STA_PROTECT;
    else
      return RES_OK;
  else
    // FIXME: Is this correct?
    return STA_NOINIT|STA_NODISK;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */

DRESULT disk_read (
	BYTE drv,		/* Physical drive nmuber (0..) */
	BYTE *buff,		/* Data buffer to store read data */
	DWORD sector,	/* Sector number (LBA) */
	BYTE count		/* Sector count (1..255) */
)
{
  uint8_t i;

  for (i=0;i<count;i++) {
    if (!sdRead(sector+i, buff+512*i))
      return RES_ERROR;
  }
  return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */

#if _READONLY == 0
DRESULT disk_write (
	BYTE drv,			/* Physical drive nmuber (0..) */
	const BYTE *buff,	/* Data to be written */
	DWORD sector,		/* Sector number (LBA) */
	BYTE count			/* Sector count (1..255) */
)
{
  uint8_t i;

  for (i=0;i<count;i++) {
    if (!sdWrite(sector+i, (uint8_t*)buff+512*i))
      return RES_ERROR;
  }
  return RES_OK;
}
#endif /* _READONLY */
