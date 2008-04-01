/*
    Copyright Jim Brain and Brain Innovations, 2005
  
    This file is part of uIEC.

    uIEC is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    uIEC is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with uIEC; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <inttypes.h>
#include <avr/io.h>
#include "config.h"
#include "ata.h"
#include "diskio.h"
#include "uart.h"

static uint8_t ATA_drv_flags[2];

volatile enum diskstates disk_state;

#define DELAY() { asm volatile ("nop" ::); asm volatile ("nop" ::);}
#define ATA_send_command(cmd) { ATA_write_reg(ATA_REG_COMMAND,cmd); }
#define ATA_INIT_WAIT 0xfff80000

void init_disk(void) {
  disk_state=DISK_OK;
  ATA_drv_flags[0]=STA_NOINIT;
  ATA_drv_flags[1]=STA_NOINIT;
  ATA_PORT_CTRL_OUT=ATA_REG_IDLE;
  ATA_PORT_CTRL_DDR=ATA_REG_IDLE;
  ATA_PORT_RESET_DDR |= ATA_PIN_RESET;
  uint8_t i=0;
  ATA_PORT_RESET_OUT &= (uint8_t)~ATA_PIN_RESET;
  ATA_PORT_CTRL_OUT=ATA_REG_IDLE; // bring active low lines high
  // wait a bit for the drive to reset.
  while((++i))
    DELAY();
  ATA_PORT_RESET_OUT |= ATA_PIN_RESET;
}

uint8_t ATA_read_reg(uint8_t reg) {
  uint8_t data;

  ATA_PORT_CTRL_OUT=reg ;
  ATA_PORT_CTRL_OUT&=(uint8_t)~ATA_PIN_RD;
  DELAY();
  data=ATA_PORT_DATA_LO_IN;
  ATA_PORT_CTRL_OUT|=ATA_PIN_RD;
  return data;
}

void ATA_write_reg(uint8_t reg, unsigned char data) {
  ATA_PORT_DATA_LO_DDR=0xff;  // bring to output
  ATA_PORT_DATA_LO_OUT=data;
  ATA_PORT_CTRL_OUT=reg ;
  ATA_PORT_CTRL_OUT&=(uint8_t)~ATA_PIN_WR;
  DELAY();
  ATA_PORT_CTRL_OUT|=ATA_PIN_WR;
  ATA_PORT_DATA_LO_DDR=0x00;  // bring to input
}

void ATA_read_data(unsigned char* data, uint8_t offset, uint8_t dlen) {
  uint8_t i=0,l,h;
  
  ATA_PORT_CTRL_OUT=ATA_REG_DATA;
  do {
    ATA_PORT_CTRL_OUT&=(uint8_t)~ATA_PIN_RD;
    DELAY();
    l=ATA_PORT_DATA_LO_IN;
    h=ATA_PORT_DATA_HI_IN;
    ATA_PORT_CTRL_OUT|=ATA_PIN_RD;
    if(dlen && i>=offset) {
      *data++=l;
      *data++=h;
      dlen--;
    }
  } while (++i);
}

uint8_t ATA_drq(void) {
  // check for DRQ hi or ERR hi
  uint8_t status;
  do {
      status=ATA_read_reg(ATA_REG_STATUS);
  } while((status&ATA_STATUS_BSY)!= 0 && (status&ATA_STATUS_ERR)==0 && (status & (ATA_STATUS_RDY | ATA_STATUS_DRQ)) != (ATA_STATUS_RDY | ATA_STATUS_DRQ));
  return status;
}

uint8_t ATA_bsy(void) {
  // check for BSY lo
  uint8_t status;
  do {
      status=ATA_read_reg(ATA_REG_STATUS);
  } while((status&ATA_STATUS_BSY)!=0);
  return status;
}

void ATA_select_sector(uint8_t drv, uint32_t sec, uint8_t count) {
  if(ATA_drv_flags[drv]&ATA_FL_48BIT) {
    ATA_write_reg (ATA_REG_SECCNT, 0);
    ATA_write_reg (ATA_REG_SECCNT, count);

    ATA_write_reg (ATA_REG_LBA0, (uint8_t)(sec>>24));
    ATA_write_reg (ATA_REG_LBA0, (uint8_t)sec);
    
    ATA_write_reg (ATA_REG_LBA1,0);
    ATA_write_reg (ATA_REG_LBA1, (uint8_t)(sec>>16));
    
    ATA_write_reg (ATA_REG_LBA2,0);
    ATA_write_reg (ATA_REG_LBA2, (uint8_t)(sec>>8));
    
    ATA_write_reg (ATA_REG_LBA3, 0xe0 | (drv?ATA_DEV_SLAVE:ATA_DEV_MASTER));
  } else {
    ATA_write_reg (ATA_REG_SECCNT, count);
    ATA_write_reg (ATA_REG_LBA0, (uint8_t)sec);
    ATA_write_reg (ATA_REG_LBA1, (uint8_t)(sec>>8));
    ATA_write_reg (ATA_REG_LBA2, (uint8_t)(sec>>16));
    ATA_write_reg (ATA_REG_LBA3, 0xe0 | (drv?ATA_DEV_SLAVE:ATA_DEV_MASTER) | ((sec>>24)&0x0f));
  }
}

DSTATUS disk_initialize (BYTE drv) {
  unsigned char data[(83-49+1)*2];
  uint32_t i=ATA_INIT_WAIT;
  uint8_t status;
  
  if(drv>1) return STA_NOINIT;
  if(ATA_drv_flags[drv]&STA_NODISK)
    return STA_NOINIT; // cannot initialize a drive with no disk in it.
  // we need to set the drive.
  ATA_write_reg (ATA_REG_LBA3, 0xe0 | (drv?ATA_DEV_SLAVE:ATA_DEV_MASTER));
  // we should set a timeout on bsy();
  //ATA_rdy();
  // check for RDY hi.
  do {
      status=ATA_read_reg(ATA_REG_STATUS);
  } while((status&ATA_STATUS_RDY)==0 && (++i));
  if(status&ATA_STATUS_RDY) {
    i=ATA_INIT_WAIT;
    do {
        status=ATA_read_reg(ATA_REG_STATUS);
    } while((status&ATA_STATUS_BSY)!=0 && (++i));
    if(!(status&ATA_STATUS_BSY)) {
      //ATA_write_reg (ATA_REG_FEATURES, 3); /* set PIO mode 0 */ 
      //ATA_write_reg (ATA_REG_SECCNT, 1);
      //ATA_send_command (ATA_CMD_FEATURES);
      ATA_send_command(ATA_CMD_IDENTIFY);
      if(!(ATA_drq()&ATA_STATUS_ERR)) {
        ATA_read_data(data,49,83-49+1);
        if((data[1] & 0x02)) { /* LBA support */
          if(data[(83-49)*2 + 1]&0x04) /* 48 bit addressing... */
            ATA_drv_flags[drv] |= ATA_FL_48BIT;
          ATA_drv_flags[drv]&=(uint8_t)~STA_NOINIT;
          return ATA_drv_flags[drv]&STA_NOINIT;
        }
      }
    }
  }
  ATA_drv_flags[drv]|=STA_NODISK; // no disk in drive
  return ATA_drv_flags[drv]&STA_NOINIT;
}


DSTATUS disk_status (BYTE drv) {
  if(drv>1)
     return STA_NOINIT;
  return ATA_drv_flags[drv]&STA_NOINIT;
}


DRESULT disk_read (BYTE drv, BYTE* data, DWORD sec, BYTE count) {
  uint8_t i=0;
  
  if (drv>1 || !count) return RES_PARERR;
  if (ATA_drv_flags[drv] & STA_NOINIT) return RES_NOTRDY;
  
  ATA_bsy();
  ATA_select_sector(drv, sec,count);
  ATA_send_command((ATA_drv_flags[drv]&ATA_FL_48BIT?ATA_CMD_READ_EXT:ATA_CMD_READ));
  ATA_drq();
  ATA_PORT_CTRL_OUT=ATA_REG_DATA;
  do {
    ATA_PORT_CTRL_OUT&=(uint8_t)~ATA_PIN_RD;
    DELAY();
    *data++=ATA_PORT_DATA_LO_IN;
    *data++=ATA_PORT_DATA_HI_IN;
    ATA_PORT_CTRL_OUT|=ATA_PIN_RD;
  } while (++i);
  return RES_OK;
}

#if _READONLY == 0
DRESULT disk_write (BYTE drv, const BYTE* data, DWORD sec, BYTE count) {
  uint8_t i=0;
  
  ATA_bsy();
  ATA_select_sector(drv, sec, count);
  ATA_send_command((ATA_drv_flags[drv]&ATA_FL_48BIT?ATA_CMD_WRITE_EXT:ATA_CMD_WRITE));
  ATA_drq();
  ATA_PORT_CTRL_OUT=ATA_REG_DATA;
  ATA_PORT_DATA_LO_DDR=0xff;  // bring to output
  ATA_PORT_DATA_HI_DDR=0xff;  // bring to output
  do {
  ATA_PORT_DATA_LO_OUT=*data++;
  ATA_PORT_DATA_HI_OUT=*data++;
  ATA_PORT_CTRL_OUT&=(uint8_t)~ATA_PIN_WR;
  DELAY();
  ATA_PORT_CTRL_OUT|=ATA_PIN_WR;
  } while (++i);
  ATA_PORT_DATA_LO_DDR=0x00;  // bring to input
  ATA_PORT_DATA_HI_DDR=0x00;  // bring to input
  return RES_OK;
}
#endif


#if _USE_IOCTL != 0
DRESULT disk_ioctl (
  BYTE drv,   /* Physical drive nmuber (0) */
  BYTE ctrl,    /* Control code */
  void *buff    /* Buffer to send/receive data block */
)
{
  BYTE n, dl, dh, ofs, w, *ptr = buff;


  if (drv>1) return RES_PARERR;
  if (ATA_drv_flags[drv] & STA_NOINIT) return RES_NOTRDY;

  switch (ctrl) {
    case GET_SECTOR_COUNT : /* Get number of sectors on the disk (DWORD) */
      ofs = 60; w = 2; n = 0;
      break;

    case GET_SECTOR_SIZE :  /* Get sectors on the disk (WORD) */
      *(WORD*)buff = 512;
      return RES_OK;

    case GET_BLOCK_SIZE : /* Get erase block size in sectors (DWORD) */
      *(DWORD*)buff = 1;
      return RES_OK;

    case CTRL_SYNC :  /* Nothing to do */
      return RES_OK;

    case ATA_GET_REV :  /* Get firmware revision (8 chars) */
      ofs = 23; w = 4; n = 4;
      break;

    case ATA_GET_MODEL :  /* Get model name (40 chars) */
      ofs = 27; w = 20; n = 20;
      break;

    case ATA_GET_SN : /* Get serial number (20 chars) */
      ofs = 10; w = 10; n = 10;
      break;

    default:
      return RES_PARERR;
  }

  ATA_send_command(ATA_CMD_IDENTIFY);
  ATA_drq();
  //if (!wait_data()) return RES_ERROR;
  ATA_read_data(ptr, ofs, w);
  while (n--) {
    dl = *ptr; dh = *(ptr+1);
    *ptr++ = dh; *ptr++ = dl; 
  }

  return RES_OK;
}
#endif /*  _USE_IOCTL != 0 */

