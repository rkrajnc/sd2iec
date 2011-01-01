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


   spi.h: Definitions for the low-level SPI routines - AVR version

*/
#ifndef SPI_H
#define SPI_H

#include "config.h"

/* Low speed 400kHz for init, fast speed <=20MHz (MMC limit) */
typedef enum { SPI_SPEED_FAST, SPI_SPEED_SLOW } spi_speed_t;

/* Available SPI devices - special case to select all SD cards during init */
/* Note: SD cards must be 0 and 1 */
/* AVR note: Code assumes that (spi_device_t+1) can be used as bit field of selected cards */
typedef enum { SPIDEV_CARD0    = 0,
               SPIDEV_CARD1    = 1,
               SPIDEV_ALLCARDS = 2 } spi_device_t;

/* AVR only accesses SD cards over SPI, so optimize the single-card case */
#ifdef CONFIG_TWINSD

extern uint8_t spi_current_device;

/* Expose SS pin function for faster sector reads/writes */
static inline __attribute__((always_inline)) void spi_set_ss(uint8_t state) {
  if (spi_current_device & 1) {
    if (state)
      SPI_PORT |= SPI_SS;
    else
      SPI_PORT &= ~SPI_SS;
  }
  if (spi_current_device & 2) {
    if (state)
      SD2_PORT |= SD2_CS;
    else
      SD2_PORT &= ~SD2_CS;
  }
}

static inline void spi_select_device(spi_device_t dev) {
  spi_set_ss(1);
  spi_current_device = dev+1;
}

#else
#  define spi_select_device(foo) do {} while (0)

/* Expose SS pin function for faster sector reads/writes */
static inline void spi_set_ss(uint8_t state) {
  if (state)
    SPI_PORT |= SPI_SS;
  else
    SPI_PORT &= ~SPI_SS;
}

#endif

/* Initialize SPI interface */
void spi_init(spi_speed_t speed);

/* Transmit a single byte */
void spi_tx_byte(uint8_t data);

/* Do a single byte dummy transmission (no device selected) */
void spi_tx_dummy(void);

/* Exchange a data block - internal API only! */
void spi_exchange_block(void *data, unsigned int length, uint8_t write);

/* Receive a data block */
static inline void spi_tx_block(const void *data, unsigned int length) {
  spi_exchange_block((void *)data, length, 0);
}

/* Receive a single byte */
uint8_t spi_rx_byte(void);

/* Receive a data block */
static inline void spi_rx_block(void *data, unsigned int length) {
  spi_exchange_block(data, length, 1);
}

/* Switch speed of SPI interface */
void spi_set_speed(spi_speed_t speed);

#endif
