/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2010  Ingo Korb <ingo@akana.de>

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


   fastloader-ll.h: Definitions for low-level fastloader routines

*/

#ifndef FASTLOADERLL_H
#define FASTLOADERLL_H

void turbodisk_byte(uint8_t value);
void turbodisk_buffer(uint8_t *data, uint8_t length);

uint8_t jiffy_receive(uint8_t *busstate);
uint8_t jiffy_send(uint8_t value, uint8_t eoi, uint8_t loadflags);

void clk_data_handshake(void);
void fastloader_fc3_send_block(uint8_t *data);
uint8_t fc3_get_byte(void);

uint8_t dreamload_get_byte(void);
void dreamload_send_byte(uint8_t byte);

int16_t uload3_get_byte(void);
void uload3_send_byte(uint8_t byte);

uint8_t epyxcart_send_byte(uint8_t byte);

uint8_t geos_get_byte_1mhz(void);
uint8_t geos_get_byte_2mhz(void);
void geos_send_byte_20(uint8_t byte);
void geos_send_byte_1581_21(uint8_t byte);

void wheels_send_byte_1mhz(uint8_t byte);
uint8_t wheels_get_byte_1mhz(void);

uint8_t wheels44_get_byte_1mhz(void);
uint8_t wheels44_get_byte_2mhz(void);
void wheels44_send_byte_2mhz(uint8_t byte);

void ar6_1581_send_byte(uint8_t byte);

#endif
