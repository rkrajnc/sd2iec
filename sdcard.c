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

   
   sdcard.c: SD/MMC access routines

   Extended, optimized and cleaned version of code from MMC2IEC,
   original copyright header follows:

//
// Title        : SD/MMC Card driver
// Author       : Lars Pontoppidan, Aske Olsson, Pascal Dufour,
// Date         : Jan. 2006
// Version      : 0.42
// Target MCU   : Atmel AVR Series
//
// CREDITS:
// This module is developed as part of a project at the technical univerisity of 
// Denmark, DTU.
//
// DESCRIPTION:
// This SD card driver implements the fundamental communication with a SD card. 
// The driver is confirmed working on 8 MHz and 14.7456 MHz AtMega32 and has
// been tested successfully with a large number of different SD and MMC cards.
//
// DISCLAIMER:
// The author is in no way responsible for any problems or damage caused by
// using this code. Use at your own risk.
//
// LICENSE:
// This code is distributed under the GNU Public License
// which can be found at http://www.gnu.org/licenses/gpl.txt
//

*/

#include <avr/io.h>
#include <avr/pgmspace.h>
#include "config.h"
#include "spi.h"
#include "sdcard.h"


#ifndef TRUE
#define TRUE -1
#endif
#ifndef FALSE
#define FALSE 0
#endif


// SD/MMC commands
#define GO_IDLE_STATE           0
#define SEND_OP_COND            1
#define SWITCH_FUNC             6
#define SEND_IF_COND            8
#define SEND_CSD                9
#define SEND_CID               10
#define STOP_TRANSMISSION      12
#define SEND_STATUS            13
#define SET_BLOCKLEN           16
#define READ_SINGLE_BLOCK      17
#define READ_MULTIPLE_BLOCK    18
#define WRITE_BLOCK            24
#define WRITE_MULTIPLE_BLOCK   25
#define PROGRAM_CSD            27
#define SET_WRITE_PROT         28
#define CLR_WRITE_PROT         29
#define SEND_WRITE_PROT        30
#define ERASE_WR_BLK_STAR_ADDR 32
#define ERASE_WR_BLK_END_ADDR  33
#define ERASE                  38
#define LOCK_UNLOCK            42
#define APP_CMD                55
#define GEN_CMD                56
#define READ_OCR               58
#define CRC_ON_OFF             59

// SD ACMDs
#define SD_STATUS                 13
#define SD_SEND_NUM_WR_BLOCKS     22
#define SD_SET_WR_BLK_ERASE_COUNT 23
#define SD_SEND_OP_COND           41
#define SD_SET_CLR_CARD_DETECT    42
#define SD_SEND_SCR               51


uint8_t sdCardOK = FALSE;
#ifdef SDHC_SUPPORT
static uint8_t isSDHC;
#else
#define isSDHC 0
#endif


static char sdResponse(uint8_t expected)
{
  unsigned short count = 0x0FFF;
  
  while ((spiTransferByte(0xFF) != expected) && count )
    count--;
  
  // If count didn't run out, return success
  return (count != 0);  
}

static char sdWaitWriteFinish(void)
{
  unsigned short count = 0xFFFF; // wait for quite some time
  
  while ((spiTransferByte(0xFF) == 0) && count )
    count--;
  
  // If count didn't run out, return success
  return (count != 0);  
}

static void deselectCard(void) {
  // Send 8 clock cycles
  SPI_SS_HIGH();
  spiTransferByte(0xff);
}

// Send a command to the SD card
// Deselect it afterwards if requested
// FIXME: Send calculated instead of fixed CRC
static int sendCommand(const uint8_t  command,
		       const uint32_t parameter,
		       const uint8_t  crc,
		       const uint8_t  deselect) {
  uint8_t  i;
  uint16_t counter;

  // Select card
  SPI_SS_LOW();

  // Transfer command
  spiTransferByte(0x40+command);
  spiTransferLong(parameter);
  spiTransferByte(crc);

  // Wait for a valid response
  counter = 0;
  do {
    i = spiTransferByte(0xff);
    counter++;
  } while (i & 0x80 && counter < 0x1000);

  if (deselect) deselectCard();

  return i;
}

#ifdef SDHC_SUPPORT
// Extended init sequence for SDHC support
static char extendedInit(void) __attribute__((unused));
static char extendedInit(void) {
  uint8_t  i;
  uint16_t counter;
  uint32_t answer;

  // Send CMD8: SEND_IF_COND
  //   0b000110101010 == 2.7-3.6V supply, check pattern 0xAA
  //   CRC manually calculated, must be correct!
  i = sendCommand(SEND_IF_COND, 0b000110101010, 0x87, 0);
  if (i > 1) {
    // Card returned an error, ok (MMC oder SD1.x) but not SDHC
    deselectCard();
    return TRUE;
  }

  // No error, continue SDHC initialization
  answer = spiTransferLong(0);
  deselectCard();
  
  if (((answer >> 8) & 0x0f) != 0b0001) {
    // Card didn't accept our voltage specification
    return FALSE;
  }
  
  // Verify echo-back of check pattern
  if ((answer & 0xff) != 0b10101010) {
    // Check pattern mismatch, working but not SD2.0 compliant
    return TRUE;
  }
  
  counter = 0xff;
  do {
    // Prepare for ACMD, send CMD55: APP_CMD
    i = sendCommand(APP_CMD, 0, 0xff, 1);
    if (i > 1) {
      // Command not accepted, could be MMC
      return TRUE;
    }

    // Send ACMD41: SD_SEND_OP_COND
    //   1L<<30 == Host has High Capacity Support
    i = sendCommand(SD_SEND_OP_COND, 1L<<30, 0xff, 1);
    // Repeat while card card accepts command but isn't ready
  } while (i == 1 && --counter > 0);

  // If ACMD41 fails something strange happened...
  if (i != 0)
    return FALSE;
  else
    return TRUE;
}
#endif

//
// Public functions
//
char sdReset(void)
{
  uint8_t  i;
  uint16_t counter;
  uint32_t answer;
  
#ifdef SDCARD_WP_SETUP
  SDCARD_WP_SETUP();
#endif
  
  sdCardOK = FALSE;
#ifdef SDHC_SUPPORT  
  isSDHC   = FALSE;
#endif
  
  SPI_SS_HIGH();

  // Send 80 clks
  for (i=0; i<10; i++) {
    spiTransferByte(0xFF);
  }
  
  // Reset card
  i = sendCommand(GO_IDLE_STATE, 0, 0x95, 1);
  if (i != 1) {
    return FALSE;
  }

#ifdef SDHC_SUPPORT
  if (!extendedInit())
    return FALSE;
#endif

  counter = 0xff;
  // According to the spec READ_OCR should work at this point
  // without retries. One of my Sandisk-cards thinks otherwise.
  do {
    // Send CMD58: READ_OCR
    i = sendCommand(READ_OCR, 0, 0xff, 0);
    if (i > 1) {
      // kills my Sandisk 1G which requires the retries in the first place
      // deselectCard();
    }
  } while (i > 1 && counter > 0);
  
  if (counter > 0) {
    answer = spiTransferLong(0);
    
    // See if the card likes our supply voltage
    if (!(answer & SD_SUPPLY_VOLTAGE)) {
      // The code isn't set up to completely ignore the card,
      // but at least report it as nonworking
      deselectCard();
      return FALSE;
    }
    
#ifdef SDHC_SUPPORT
    // See what card we've got
    if (answer & 0x40000000) {
      isSDHC = TRUE;
    }
#endif
  }
  
  // Keep sending CMD1 (SEND_OP_COND) command until zero response
  counter = 0xfff;
  do {
    i = sendCommand(SEND_OP_COND, 1L<<30, 0xff, 1);
    counter--;
  } while (i != 0 && counter > 0);
  
  if (counter==0) {
    return FALSE;
  }
  
  // Send MMC CMD16(SET_BLOCKLEN) to 512 bytes
  i = sendCommand(SET_BLOCKLEN, 512, 0xff, 1);
  if (i != 0) {
    return FALSE;
  }
    
  // Thats it!
  sdCardOK = TRUE;
  return TRUE;
}



// Reads sector to buffer
char sdRead(uint32_t sector, uint8_t *buffer)
{  
  uint8_t res;

  if (isSDHC)
    res = sendCommand(READ_SINGLE_BLOCK, sector, 0xff, 0);
  else
    res = sendCommand(READ_SINGLE_BLOCK, sector << 9, 0xff, 0);
  
  if (res != 0) {
    SPI_SS_HIGH();
    sdCardOK = FALSE;
    return FALSE;
  }
  
  // Wait for data token
  if (!sdResponse(0xFE)) {
    SPI_SS_HIGH();
    sdCardOK = FALSE;
    return FALSE;
  }
  
  uint16_t i;
  
  // Get data
  for (i=0; i<512; i++) {
    *(buffer++) = spiTransferByte(0xFF);
  }
  
  // Discard chksum
  // FIXME: Check CRC16
  spiTransferByte(0xFF);
  spiTransferByte(0xFF);
  
  deselectCard();
  
  return TRUE;
}



// Writes sector to buffer
char sdWrite(uint32_t sector, uint8_t *buffer)
{ 
  uint8_t res;

#ifdef SDCARD_WP
  if (SDCARD_WP) return FALSE; 
#endif

  if (isSDHC)
    res = sendCommand(WRITE_BLOCK, sector, 0xff, 0);
  else
    res = sendCommand(WRITE_BLOCK, sector<<9, 0xff, 0);

  if (res != 0) {
    SPI_SS_HIGH();
    sdCardOK = FALSE;
    return FALSE;
  }
  
  // Send data token
  spiTransferByte(0xFE);
  
  uint16_t i;
  
  // Send data
  for (i=0; i<512; i++) {
    spiTransferByte(*(buffer++)); 
  }
  
  // Send bogus chksum
  // FIXME: Send real CRC16
  spiTransferByte(0xFF);
  spiTransferByte(0xFF);
  
  // Get and check status feedback
  uint8_t status;
  status = spiTransferByte(0xFF);
  
  if ((status & 0x0F) != 0x05) {
    SPI_SS_HIGH();
    sdCardOK = FALSE;
    return FALSE;
  }
  
  // Wait for write finish
  if (!sdWaitWriteFinish()) {
    SPI_SS_HIGH();
    sdCardOK = FALSE;
    return FALSE;
  }
  
  deselectCard();
  
  return TRUE;
}
