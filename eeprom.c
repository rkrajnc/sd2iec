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


   eeprom.c: Persistent configuration storage

*/

#include <avr/eeprom.h>
#include <avr/io.h>
#include "config.h"
#include "iec.h"
#include "eeprom.h"

/**
 * struct storedconfig - in-eeprom data structure
 * @dummy    : EEPROM position 0 is unused
 * @checksum : Checksum over the EEPROM contents
 * @osccal   : stored value of OSCCAL
 * @jiffyflag: stored value of jiffy_enabled
 *
 * This is the data structure for the contents of the EEPROM.
 */
static EEMEM struct {
  uint8_t dummy;
  uint8_t checksum;
  uint8_t osccal;
  uint8_t jiffyflag;
  uint8_t address;
  uint8_t hardaddress;
} storedconfig;

/**
 * read_configuration - reads configuration from EEPROM
 *
 * This function reads the stored configuration values from the EEPROM.
 * If the stored checksum doesn't match the calculated one nothing will
 * be changed.
 */
void read_configuration(void) {
  uint16_t i;
  uint8_t checksum;

  /* Calculate checksum of EEPROM contents */
  checksum = 0;
  for (i=2; i<sizeof(storedconfig); i++)
    checksum += eeprom_read_byte((uint8_t *)i);

  /* Abort if the checksum doesn't match */
  if (checksum != eeprom_read_byte(&storedconfig.checksum))
    return;

  /* Read data from EEPROM */
  OSCCAL = eeprom_read_byte(&storedconfig.osccal);
  iecflags.jiffy_enabled = eeprom_read_byte(&storedconfig.jiffyflag);
  if (eeprom_read_byte(&storedconfig.hardaddress) == DEVICE_SELECT)
    device_address = eeprom_read_byte(&storedconfig.address);

  /* Paranoia: Set EEPROM address register to the dummy entry */
  EEAR = 0;
}

/**
 * write_configuration - stores configuration data to EEPROM
 *
 * This function stores the current configuration values to the EEPROM.
 */
void write_configuration(void) {
  uint16_t i;
  uint8_t checksum;

  /* Write configuration to EEPROM */
  eeprom_write_byte(&storedconfig.osccal, OSCCAL);
  eeprom_write_byte(&storedconfig.jiffyflag, iecflags.jiffy_enabled);
  eeprom_write_byte(&storedconfig.address, device_address);
  eeprom_write_byte(&storedconfig.hardaddress, DEVICE_SELECT);

  /* Calculate checksum over EEPROM contents */
  checksum = 0;
  for (i=2;i<sizeof(storedconfig);i++)
    checksum += eeprom_read_byte((uint8_t *) i);

  /* Store checksum to EEPROM */
  eeprom_write_byte(&storedconfig.checksum, checksum);

  /* Paranoia: Set EEPROM address register to the dummy entry */
  EEAR = 0;
}

