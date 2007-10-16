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

   
   config.h: User-configurable options to simplify hardware changes and/or
             reduce the code/ram requirements of the code.


   Based on MMC2IEC, original copyright header follows:

//
// Title        : MMC2IEC - Configuration
// Author       : Lars Pontoppidan 
// Date         : Jan. 2007
// Version      : 0.7
// Target MCU   : AtMega32(L) at 8 MHz
//
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

#ifndef CONFIG_H
#define CONFIG_H

/* R.Riedel - bootloader-support */
#define DEVID 0x49454321
#define SWVERSIONMAJOR 0
#define SWVERSIONMINOR 0

typedef struct
{
	unsigned long dev_id;
	unsigned short app_version;
	unsigned short crc;
} bootloaderinfo_t;
/* R.Riedel - bootloader-support */


// Enable UART debugging here by uncommenting UART_DEBUG
#define UART_DEBUG
#define UART_BAUDRATE 19200
// Must be a power of 2
#define UART_BUFFER_SIZE 128

// CARD_DETECT must return non-zero when card is inserted
// If no card detect signal is available, comment the defines
#define SDCARD_DETECT         (!(PIND & _BV(PD2)))
#define SDCARD_DETECT_SETUP() do { DDRD &= ~_BV(PD2); PORTD |= _BV(PD2); } while(0)

// CARD Write Protect must return non-zero when card is write protected
// If no card detect signal is available, comment the defines
#define SDCARD_WP         (PIND & _BV(PD6))
#define SDCARD_WP_SETUP() do { DDRD &= ~ _BV(PD6); PORTD |= _BV(PD6); } while(0)

// DEV9 jumper. If jumped, IEC device number is 9, otherwise 8
// If no DEV9 jumper is available, comment the defines
/* R.Riedel uses PORTD.7 instead of PA2 */
#define DEV9_JUMPER         (!(PIND & _BV(PD7)))
#define DEV9_JUMPER_SETUP() do { DDRD &= ~_BV(PD7); PORTD |= _BV(PD7); } while(0)

// If DEV10_JUMPER is non-zero, IEC device number is 10, otherwise 8
// If no DEV10 jumper is available, comment the defines
/* R.Riedel uses PORTD.5 instead of PA3 */
#define DEV10_JUMPER         (!(PIND & _BV(PD5)))
#define DEV10_JUMPER_SETUP() do { DDRD &= ~_BV(PD5); PORTD |= _BV(PD5); } while(0)

// BUSY led, recommended color: green
/* R.Riedel - using PORTC instead of the original PORTA here plus inverse polarity */
#define BUSY_LED_SETDDR() DDRC  |= _BV(PC0)
#define BUSY_LED_ON()     PORTC |= _BV(PC0)
#define BUSY_LED_OFF()    PORTC &= ~_BV(PC0)


// DIRTY led, recommended color: red
/* R.Riedel - using PORTC instead of the original PORTA here plus inverse polarity */
#define DIRTY_LED_SETDDR() DDRC  |= _BV(PC1)
#define DIRTY_LED_ON()     PORTC |= _BV(PC1)
#define DIRTY_LED_OFF()    PORTC &= ~_BV(PC1)
#define DIRTY_LED_PORT     PORTC
#define DIRTY_LED_BIT()    _BV(PC1)

/* Auxiliary LED for debugging */
#define AUX_LED_SETDDR()   DDRD  |= _BV(PC2)
#define AUX_LED_ON()       PORTD |= _BV(PC2)
#define AUX_LED_OFF()      PORTD &= ~_BV(PC2)

// IEC signals
/* R.Riedel - using PORTA instead of the original PORTC for the IEC */

#define IEC_PIN  PINA
#define IEC_DDR  DDRA
#define IEC_PORT PORTA

#define IEC_BIT_ATN   _BV(PA0)
#define IEC_BIT_DATA  _BV(PA1)
#define IEC_BIT_CLOCK _BV(PA2)
#define IEC_BIT_SRQ   _BV(PA3)

// SD Card supply voltage - choose the one appropiate to your board
//#define SD_SUPPLY_VOLTAGE (1L<<15)  // 2.7V - 2.8V
//#define SD_SUPPLY_VOLTAGE (1L<<16)  // 2.8V - 2.9V
//#define SD_SUPPLY_VOLTAGE (1L<<17)  // 2.9V - 3.0V
#define SD_SUPPLY_VOLTAGE (1L<<18)  // 3.0V - 3.1V
//#define SD_SUPPLY_VOLTAGE (1L<<19)  // 3.1V - 3.2V
//#define SD_SUPPLY_VOLTAGE (1L<<20)  // 3.2V - 3.3V
//#define SD_SUPPLY_VOLTAGE (1L<<21)  // 3.3V - 3.4V
//#define SD_SUPPLY_VOLTAGE (1L<<22)  // 3.4V - 3.5V
//#define SD_SUPPLY_VOLTAGE (1L<<23)  // 3.5V - 3.6V

// Support SDHC - disabling it saves ~220 bytes flash
#define SDHC_SUPPORT

/* Length of error message buffer - 1571 uses 36 bytes */
#define ERROR_BUFFER_SIZE 36

/* Length of command buffer - 1571 uses 42 bytes */
#define COMMAND_BUFFER_SIZE 42

/* Number of sector buffers (256 byte+a bit of overhead)          */
/*  Reading a directory from a d64 image requires two buffers.    */
/*  In general: More buffers -> More open files at the same time  */
#define BUFFER_COUNT 2

#endif
