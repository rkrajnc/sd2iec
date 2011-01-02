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


   arch-eeprom.c: EEPROM access functions

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include "i2c.h"
#include "arch-eeprom.h"

// FIXME: Doesn't implement address wrapping for >256byte EEPROMs

uint8_t  eeprom_read_byte(void *addr_) {
  unsigned int addr = (unsigned int)addr_ & 0xffff;

  return i2c_read_register(I2C_EEPROM_ADDRESS, addr);
}

uint16_t eeprom_read_word(void *addr_) {
  unsigned int addr = (unsigned int)addr_ & 0xffff;
  uint16_t result;

  i2c_read_registers(I2C_EEPROM_ADDRESS, addr, 2, &result);
  return result;
}

void eeprom_read_block(void *destptr, void *addr_, unsigned int length) {
  unsigned int addr = (unsigned int)addr_ & 0xffff;

  i2c_read_registers(I2C_EEPROM_ADDRESS, addr, length, destptr);
}

void eeprom_write_byte(void *addr_, uint8_t value) {
  unsigned int addr = (unsigned int)addr_ & 0xffff;

  i2c_write_register(I2C_EEPROM_ADDRESS, addr, value);

  /* Wait until write has finished */
  while (i2c_read_register(I2C_EEPROM_ADDRESS, 0) < 0) ;
}

void eeprom_write_word(void *addr_, uint16_t value) {
  eeprom_write_block(&value, addr_, 2);
}

void eeprom_write_block(void *srcptr, void *addr_, unsigned int length) {
  unsigned int addr = (unsigned int)addr_ & 0xffff;
  uint8_t *data = (uint8_t *)srcptr;
  unsigned int i;

  // FIXME: Add block write!
  for (i=0;i<length;i++) {
    i2c_write_register(I2C_EEPROM_ADDRESS, addr+i, data[i]);
    /* Wait until write has finished */
    while (i2c_read_register(I2C_EEPROM_ADDRESS, 0) < 0) ;
  }
}
