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


   i2c.h: Definitions for I2C transfers

   There is no i2c.c, the functions defined here are currently implemented
   by softi2c.c. An additional implementation using the hardware I2C/TWI
   peripheral should implement the same functions so either implementation
   can be used.

*/

#ifndef I2C_H
#define I2C_H

void i2c_init(void);
uint8_t i2c_write_register(uint8_t address, uint8_t reg, uint8_t val);
uint8_t i2c_write_registers(uint8_t address, uint8_t startreg, uint8_t count, void *data);
int16_t i2c_read_register(uint8_t address, uint8_t reg);
uint8_t i2c_read_registers(uint8_t address, uint8_t startreg, uint8_t count, void *data);

#endif
