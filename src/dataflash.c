/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2011  Ingo Korb <ingo@akana.de>

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


   dataflash.c: AT45DB161D dataflash access routines

   The exported functions in this file are weak-aliased to their corresponding
   versions defined in diskio.h so when this file is the only diskio provider
   compiled in they will be automatically used by the linker.

*/

#include <avr/io.h>
#include <util/crc16.h>
#include "config.h"
#include "crc7.h"
#include "diskio.h"
#include "spi.h"
#include "uart.h"
#include "dataflash.h"

#define STATUS_READY       128
#define STATUS_COMPAREFAIL  64
#define STATUS_PROTECTED     2
#define STATUS_PAGES512      1

#define CMD_READ_CONT_LOW      0x03
#define CMD_MEM_TO_BUFFER_1    0x53
#define CMD_MEM_TO_BUFFER_2    0x55
#define CMD_PAGE_ERASE         0x81
#define CMD_BUFFER_ERASE_PGM_1 0x83
#define CMD_BUFFER_WRITE_1     0x84
#define CMD_BUFFER_ERASE_PGM_2 0x86
#define CMD_BUFFER_WRITE_2     0x87
#define CMD_STATUS             0xd7


#define SECTORS_PER_DEVICE 16
#define PAGES_PER_SECTOR   256
#define SECTOR_SHIFT       8
#define SECTOR_MASK        0x0f00
#define MAX_WRITES_BEFORE_REFRESH 10000

/* Per-sector write counters */
static uint16_t sectorwrites[SECTORS_PER_DEVICE];

static inline void select_df(uint8_t state) {
  if (state) {
    DATAFLASH_PORT &= ~DATAFLASH_SELECT;
  } else {
    DATAFLASH_PORT |= DATAFLASH_SELECT;
  }
}

static inline uint8_t wait_until_ready(void) {
  uint8_t res;

  select_df(1);
  spiTransferByte(CMD_STATUS);
  do {
    res = spiTransferByte(0xff);
  } while (!(res & STATUS_READY));
  select_df(0);
  return res;
}

static inline void send_address(uint16_t page, uint16_t byte) {
  spiTransferByte((page >> 6) & 0xff);
  spiTransferByte(((page << 2) & 0xff) + (byte >> 8));
  spiTransferByte(byte & 0xff);
}

void df_init(void) {
  uint8_t  i;
  uint16_t j,maxwrites,maxbelow10k,tmp;

  spiInit();
  DATAFLASH_PORT |= DATAFLASH_SELECT;
  DATAFLASH_DDR  |= DATAFLASH_SELECT;

  for (i=0;i<SECTORS_PER_DEVICE;i++) {
    maxwrites  = 0;
    maxbelow10k = 0;
    for (j=0;j<PAGES_PER_SECTOR;j++) {
      /* Read write counter */
      select_df(1);
      spiTransferByte(CMD_READ_CONT_LOW);
      send_address(i * PAGES_PER_SECTOR + j, 514);
      tmp  = spiTransferByte(0xff);
      tmp += spiTransferByte(0xff) << 8;
      select_df(0);
      if (tmp != 0xffff) {
        if (tmp <= MAX_WRITES_BEFORE_REFRESH && tmp > maxbelow10k)
          maxbelow10k = tmp;
        if (tmp > maxwrites)
          maxwrites = tmp;
      }
    }
    if (maxbelow10k != 0)
      sectorwrites[i] = maxbelow10k;
    else
      sectorwrites[i] = maxwrites;

    uart_puts_P(PSTR("maxw "));
    uart_puthex(i);
    uart_putc(' ');
    uart_puthex(sectorwrites[i] >> 8);
    uart_puthex(sectorwrites[i] & 0xff);
    uart_putcrlf();
  }

  disk_state = DISK_OK;
}
void disk_init(void) __attribute__ ((weak, alias("df_init")));


DSTATUS df_status(BYTE drv) {
  if (drv == 0) {
    if (wait_until_ready() & STATUS_PROTECTED)
      return STA_PROTECT;
    else
      return RES_OK;
  } else
    return STA_NOINIT|STA_NODISK;
}
DSTATUS disk_status(BYTE drv) __attribute__ ((weak, alias("df_status")));


DSTATUS df_initialize(BYTE drv) {
  /* Dataflash initialisation already happened in disk_init */
  if (drv == 0)
    return RES_OK;
  else
    return STA_NOINIT|STA_NODISK;
}
DSTATUS disk_initialize(BYTE drv) __attribute__ ((weak, alias("df_initialize")));


/**
 * df_read - reads sectors from the dataflash to buffer
 * @drv   : drive (unused)
 * @buffer: pointer to the buffer
 * @sector: first sector to be read
 * @count : number of sectors to be read
 *
 * This function reads count sectors from the dataflash starting
 * at sector to buffer. Returns RES_ERROR if an error occured or
 * RES_OK if successful.
 */
DRESULT df_read(BYTE drv, BYTE *buffer, DWORD sector, BYTE count) {
  uint16_t i,crc,recvcrc,wcount;
  uint8_t tmp,sec;

  if (drv != 0)
    return RES_PARERR;

  for (sec=0;sec<count;sec++) {
    if (sector >= SECTORS_PER_DEVICE * PAGES_PER_SECTOR)
      return RES_ERROR;

    select_df(1);
    spiTransferByte(CMD_READ_CONT_LOW);
    send_address(sector,0);

    // Get data
    crc = 0;
    // Initiate data exchange over SPI
    SPDR = 0xff;

    for (i=0; i<512; i++) {
      // Wait until data has been received
      loop_until_bit_is_set(SPSR, SPIF);
      tmp = SPDR;
      // Transmit the next byte while we store the current one
      SPDR = 0xff;

      *(buffer++) = tmp;
      crc = _crc_xmodem_update(crc, tmp);
    }
    // Wait until the first CRC byte is received
    loop_until_bit_is_set(SPSR, SPIF);

    // Check CRC
    recvcrc = (SPDR << 8) + spiTransferByte(0xff);

    // Read write counter
    wcount = (spiTransferByte(0xff) << 8) + spiTransferByte(0xff);

    select_df(0);

    // Compare CRC - skip if sector was never written to
    if (wcount != 0xffff && recvcrc != crc)
      return RES_ERROR;

    sector++;
  }

  return RES_OK;
}
DRESULT disk_read(BYTE drv, BYTE *buffer, DWORD sector, BYTE count) __attribute__ ((weak, alias("df_read")));


/**
 * df_write - writes sectors from buffer to the dataflash
 * @drv   : drive (unused)
 * @buffer: pointer to the buffer
 * @sector: first sector to be written
 * @count : number of sectors to be written
 *
 * This function writes count sectors from buffer to the SD card
 * starting at sector. Returns RES_ERROR if an error occured or
 * RES_OK if successful. This function will also handle rewriting
 * pages when the write count gets close to the specified maximum.
 */
DRESULT df_write(BYTE drv, const BYTE *buffer, DWORD sector, BYTE count) {
  uint8_t sec,j;
  uint16_t crc,i,wcount;

  if (drv != 0)
    return RES_PARERR;

  for (sec=0;sec<count;sec++) {
    select_df(1);

    /* Write data into chip buffer */
    spiTransferByte(CMD_BUFFER_WRITE_1);
    spiTransferByte(0);
    spiTransferByte(0);
    spiTransferByte(0);

    /* Transfer data */
    crc = 0;
    for (i=0; i<512; i++) {
      crc = _crc_xmodem_update(crc, *buffer);
      spiTransferByte(*(buffer++));
    }

    /* Transfer CRC */
    spiTransferByte(crc >> 8);
    spiTransferByte(crc & 0xff);

    /* Transfer write counter */
    wcount = ++sectorwrites[sector >> SECTOR_SHIFT];
    if (wcount >= 2*MAX_WRITES_BEFORE_REFRESH) {
      /* Wrap counter */
      wcount = 1;
      sectorwrites[sector >> SECTOR_SHIFT] = 1;
    }
    spiTransferByte(wcount & 0xff);
    spiTransferByte(wcount >> 8);

    /* Fill remainder of page */
    for (j=0;j<12;j++)
      spiTransferByte(0xff);

    select_df(0);

    /* Write buffer to memory with erase */
    select_df(1);
    spiTransferByte(CMD_BUFFER_ERASE_PGM_1);
    send_address(sector,0);
    select_df(0);

    wait_until_ready();

    /* Refresh a page if required */
    if ((wcount >= MAX_WRITES_BEFORE_REFRESH-(2*PAGES_PER_SECTOR-1) &&
         wcount <= MAX_WRITES_BEFORE_REFRESH) ||
        (wcount >= 2*MAX_WRITES_BEFORE_REFRESH-(2*PAGES_PER_SECTOR-1) &&
         wcount <= 2*MAX_WRITES_BEFORE_REFRESH)) {
      /* Calculate page to refresh */
      uint16_t refreshpage = wcount;

      if (wcount > MAX_WRITES_BEFORE_REFRESH)
        refreshpage -= MAX_WRITES_BEFORE_REFRESH;

      refreshpage = (refreshpage - (MAX_WRITES_BEFORE_REFRESH-(2*PAGES_PER_SECTOR-1))) / 2;
      uart_puts_P(PSTR("refresh page "));
      uart_puthex(refreshpage >> 8);
      uart_puthex(refreshpage & 0xff);
      uart_puts_P(PSTR(" wcount "));
      uart_puthex(wcount >> 8);
      uart_puthex(wcount & 0xff);
      uart_putcrlf();
      refreshpage += sector & SECTOR_MASK;

      /* Read page into the buffer */
      select_df(1);
      spiTransferByte(CMD_MEM_TO_BUFFER_1);
      send_address(refreshpage, 0);
      select_df(0);
      wait_until_ready();

      /* Update write counter of page */
      wcount++;
      sectorwrites[sector >> SECTOR_SHIFT] = wcount;
      select_df(1);
      spiTransferByte(CMD_BUFFER_WRITE_1);
      send_address(0,512);
      spiTransferByte(wcount & 0xff);
      spiTransferByte(wcount >> 8);
      select_df(0);

      /* Write buffer to memory */
      select_df(1);
      spiTransferByte(CMD_BUFFER_ERASE_PGM_1);
      send_address(refreshpage,0);
      select_df(0);

      wait_until_ready();
    }

    sector++;
  }

  return RES_OK;
}
DRESULT disk_write(BYTE drv, const BYTE *buffer, DWORD sector, BYTE count) __attribute__ ((weak, alias("df_write")));

DRESULT df_getinfo(BYTE drv, BYTE page, void *buffer) {
  diskinfo0_t *di = buffer;

  if (page != 0)
    return RES_ERROR;

  di->validbytes  = sizeof(diskinfo0_t);
  di->disktype    = DISK_TYPE_DF;
  di->sectorsize  = 2;
  di->sectorcount = SECTORS_PER_DEVICE * PAGES_PER_SECTOR;

  return RES_OK;
}
DRESULT disk_getinfo(BYTE drv, BYTE page, void *buffer) __attribute__ ((weak, alias("df_getinfo")));
