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

#include "autoconf.h"

/* Include avrcompat.h to get the PA0..PD7 macros on 1284P */
#include "avrcompat.h"

#if CONFIG_HARDWARE_VARIANT==1
/* Configure for your own hardware                     */
/* Example values are for the "Shadowolf 1.x" variant. */

/* Name the software should return in the Dos version messate (73) */
/* This should be upper-case because it isn't PETSCII-converted.   */
#  define HW_NAME "SD2IEC"

/*** SD card support ***/
/* If your device supports SD cards by default, define this symbol. */
#  define HAVE_SD

/* CARD_DETECT must return non-zero when card is inserted */
/* This must be a pin capable of generating interrupts.   */
#  define SDCARD_DETECT         (!(PIND & _BV(PD2)))
#  define SDCARD_DETECT_SETUP() do { DDRD &= ~_BV(PD2); PORTD |= _BV(PD2); } while(0)

/* Set up the card detect pin to generate an interrupt on every change */
#  if defined __AVR_ATmega32__
#    define SD_CHANGE_SETUP()  do { MCUCR |= _BV(ISC00); GICR |= _BV(INT0); } while(0)
#  elif defined __AVR_ATmega644__ || defined __AVR_ATmega644P__ || defined __AVR_ATmega1284P__
#    define SD_CHANGE_SETUP()  do { EICRA |= _BV(ISC00); EIMSK |= _BV(INT0); } while(0)
#  else
#    error Unknown chip!
#  endif

/* Name of the interrupt of the card detect pin */
#  define SD_CHANGE_VECT INT0_vect

/* CARD Write Protect must return non-zero when card is write protected */
#  define SDCARD_WP         (PIND & _BV(PD6))
#  define SDCARD_WP_SETUP() do { DDRD &= ~ _BV(PD6); PORTD |= _BV(PD6); } while(0)

/* SD Card supply voltage - choose the one appropiate to your board */
/* #  define SD_SUPPLY_VOLTAGE (1L<<15)  / * 2.7V - 2.8V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<16)  / * 2.8V - 2.9V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<17)  / * 2.9V - 3.0V */
#  define SD_SUPPLY_VOLTAGE (1L<<18)  /* 3.0V - 3.1V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<19)  / * 3.1V - 3.2V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<20)  / * 3.2V - 3.3V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<21)  / * 3.3V - 3.4V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<22)  / * 3.4V - 3.5V */
/* #  define SD_SUPPLY_VOLTAGE (1L<<23)  / * 3.5V - 3.6V */

/* Support for a second SD card - use CONFIG_TWINSD=y in your config file to enable! */
/* The code assumes that detect/select/write-protect lines of the */
/* second card are all on the same port.                          */
#   define SD2_PORT             PORTC
#   define SD2_PIN              PINC
#   define SD2_PRESENT          _BV(PB2)
#   define SD2_CS               _BV(PC6)
#   define SD2_WP               (PINC & _BV(PC7))

/* Same sd SD_DETECT above. */
#   define SD2_DETECT           (!(PINB & SD2_PRESENT))

/* Full setup of the SD2 port, excluding interrupts. */
#   define SD2_SETUP()          do { SD2_PORT |= SD2_CS|SD2_WP; DDRC |= SD2_CS; DDRC &= ~SD2_WP; DDRB &= ~SD2_PRESENT; PORTB |= SD2_PRESENT; } while (0)

/* Interrupt vector for card 2 change detection */
#   define SD2_CHANGE_VECT      INT2_vect

/* Set up and enable the card 2 change interrupt */
#   define SD2_CHANGE_SETUP()   do { EICRA |= _BV(ISC20); EIMSK |= _BV(INT2);  } while (0)


/*** AT45DB161D dataflash support ***/
/* You can use an Atmel AT45DB161D chip as data storage/additional drive */
//#  define HAVE_DF
//#  define DATAFLASH_PORT        PORTD
//#  define DATAFLASH_DDR         DDRD
//#  define DATAFLASH_SELECT      _BV(PD2)


/*** Device address selection ***/
/* DEVICE_SELECT should return the selected device number,   */
/* DEVICE_SELECT_SETUP() is called once to set up the ports. */
#  define DEVICE_SELECT       (8+!(PIND & _BV(PD7))+2*!(PIND & _BV(PD5)))
#  define DEVICE_SELECT_SETUP() do { \
      DDRD  &= ~(_BV(PD7)|_BV(PD5)); \
      PORTD |=   _BV(PD7)|_BV(PD5);  \
   } while (0)


/*** LEDs ***/
/* If your hardware only has a single LED, use only the DIRTY_LED defines */
/* and set SINGLE_LED. See uIEC below for an example.                     */
/* #  define SINGLE_LED */

/* BUSY led, recommended color: green */
/* R.Riedel - using PORTC instead of the original PORTA here plus inverse polarity */
#  define BUSY_LED_SETDDR() DDRC  |= _BV(PC0)
#  define BUSY_LED_ON()     PORTC |= _BV(PC0)
#  define BUSY_LED_OFF()    PORTC &= ~_BV(PC0)

/* DIRTY led, recommended color: red */
/* R.Riedel - using PORTC instead of the original PORTA here plus inverse polarity */
#  define DIRTY_LED_SETDDR() DDRC  |= _BV(PC1)
#  define DIRTY_LED_ON()     PORTC |= _BV(PC1)
#  define DIRTY_LED_OFF()    PORTC &= ~_BV(PC1)
#  define DIRTY_LED_PORT     PORTC
#  define DIRTY_LED_BIT()    _BV(PC1)

/* Software power LED */
/* Currently used on uIEC/SD only */
//#  define POWER_LED_DDR         DDRG
//#  define POWER_LED_PORT        PORTG
//#  define POWER_LED_BIT         _BV(PG1)
//#  define POWER_LED_POLARITY    0


/*** IEC signals ***/
/* R.Riedel - using PORTA instead of the original PORTC for the IEC */

#  define IEC_PIN  PINA
#  define IEC_DDR  DDRA
#  define IEC_PORT PORTA

/* Pins assigned for the IEC lines */
#  define IEC_PIN_ATN   PA0
#  define IEC_PIN_DATA  PA1
#  define IEC_PIN_CLOCK PA2
#  define IEC_PIN_SRQ   PA3

/* Use separate input/output lines?                                    */
/* The code assumes that the input is NOT inverted, but the output is. */
//#  define IEC_SEPARATE_OUT
//#  define IEC_OPIN_ATN   PA4
//#  define IEC_OPIN_DATA  PA5
//#  define IEC_OPIN_CLOCK PA6
//#  define IEC_OPIN_SRQ   PA7

/* You can use different ports for input and output bits. The code tries */
/* to not stomp on the unused bits. IEC output is on IEC_PORT.           */
/* Not well-tested yet.                                                  */
//#  define IEC_DDRIN      DDRX
//#  define IEC_DDROUT     DDRY
//#  define IEC_PORTIN     PORTX

/* ATN interrupt (not required) */
/* If this is commented out, the ATN line will be polled by a timer interrupt instead */
//#  define IEC_ATN_INT         PCINT0
//#  define IEC_ATN_INT_VECT    PCINT0_vect
//#  define IEC_ATN_INT_SETUP() do { PCMSK0 = _BV(PCINT0); PCIFR |= _BV(PCIF0); } while (0)

/* CLK interrupt (not required) */
/* Dreamload requires interrupts for both the ATN and CLK lines. If both are served by */
/* the same PCINT vector, define that as ATN interrupt above and define IEC_PCMSK.     */
//#  define IEC_PCMSK             PCMSK0
/* If the CLK line has its own dedicated interrupt, use the following definitions: */
//#  define IEC_CLK_INT           INT5
//#  define IEC_CLK_INT_VECT      INT5_vect
//#  define IEC_CLK_INT_SETUP()   do { EICRB |= _BV(ISC50); } while (0)


/*** User interface ***/
/* Macros for the registers of the port where the buttons are connected */
/* All buttons must be on the same port. */
#  define BUTTON_PIN  PINC
#  define BUTTON_PORT PORTC
#  define BUTTON_DDR  DDRC

/* Mask value to isolate the button bits */
#  define BUTTON_MASK (_BV(PC3)|_BV(PC4))

/* Button NEXT changes to the next disk image and enables sleep mode (held) */
#  define BUTTON_NEXT _BV(PC4)

/* Button PREV changes to the previous disk image */
/* If you don't have/need this button, define it as 0. */
#  define BUTTON_PREV _BV(PC3)

/* Software I2C lines for the RTC */
#  define SOFTI2C_PORT    PORTC
#  define SOFTI2C_PIN     PINC
#  define SOFTI2C_DDR     DDRC
#  define SOFTI2C_BIT_SCL PC4
#  define SOFTI2C_BIT_SDA PC5
#  define SOFTI2C_DELAY   6


/* Pre-configurated hardware variants */

#elif CONFIG_HARDWARE_VARIANT==2
/* Hardware configuration: Shadowolf 1 */
#  define HW_NAME "SD2IEC"
#  define HAVE_SD
#  define SDCARD_DETECT         (!(PIND & _BV(PD2)))
#  define SDCARD_DETECT_SETUP() do { DDRD &= ~_BV(PD2); PORTD |= _BV(PD2); } while(0)
#  if defined __AVR_ATmega32__
#    define SD_CHANGE_SETUP()   do { MCUCR |= _BV(ISC00); GICR |= _BV(INT0); } while(0)
#  elif defined __AVR_ATmega644__ || defined __AVR_ATmega644P__ || defined __AVR_ATmega1284P__
#    define SD_CHANGE_SETUP()   do { EICRA |= _BV(ISC00); EIMSK |= _BV(INT0); } while(0)
#  else
#    error Unknown chip!
#  endif
#  define SD_CHANGE_VECT        INT0_vect
#  define SDCARD_WP             (PIND & _BV(PD6))
#  define SDCARD_WP_SETUP()     do { DDRD &= ~ _BV(PD6); PORTD |= _BV(PD6); } while(0)
#  define SD_SUPPLY_VOLTAGE     (1L<<18)
#  define DEVICE_SELECT         (8+!(PIND & _BV(PD7))+2*!(PIND & _BV(PD5)))
#  define DEVICE_SELECT_SETUP() do {        \
             DDRD  &= ~(_BV(PD7)|_BV(PD5)); \
             PORTD |=   _BV(PD7)|_BV(PD5);  \
          } while (0)
#  define BUSY_LED_SETDDR()     DDRC  |= _BV(PC0)
#  define BUSY_LED_ON()         PORTC |= _BV(PC0)
#  define BUSY_LED_OFF()        PORTC &= ~_BV(PC0)
#  define DIRTY_LED_SETDDR()    DDRC  |= _BV(PC1)
#  define DIRTY_LED_ON()        PORTC |= _BV(PC1)
#  define DIRTY_LED_OFF()       PORTC &= ~_BV(PC1)
#  define DIRTY_LED_PORT        PORTC
#  define DIRTY_LED_BIT()       _BV(PC1)
#  define IEC_PIN               PINA
#  define IEC_DDR               DDRA
#  define IEC_PORT              PORTA
#  define IEC_PIN_ATN           PA0
#  define IEC_PIN_DATA          PA1
#  define IEC_PIN_CLOCK         PA2
#  define IEC_PIN_SRQ           PA3
#  define IEC_ATN_INT_VECT      PCINT0_vect
#  define IEC_ATN_INT_SETUP()   do { PCICR |= _BV(PCIE0); PCIFR |= _BV(PCIF0); } while (0)
#  define IEC_PCMSK             PCMSK0
#  define BUTTON_PIN            PINC
#  define BUTTON_PORT           PORTC
#  define BUTTON_DDR            DDRC
#  define BUTTON_MASK           (_BV(PC3)|_BV(PC4))
#  define BUTTON_NEXT           _BV(PC4)
#  define BUTTON_PREV           _BV(PC3)


#elif CONFIG_HARDWARE_VARIANT == 3
/* Hardware configuration: LarsP */
#  define HW_NAME "SD2IEC"
#  define HAVE_SD
#  define SDCARD_DETECT         (!(PIND & _BV(PD2)))
#  define SDCARD_DETECT_SETUP() do { DDRD &= ~_BV(PD2); PORTD |= _BV(PD2); } while(0)
#  if defined __AVR_ATmega32__
#    define SD_CHANGE_SETUP()   do { MCUCR |= _BV(ISC00); GICR |= _BV(INT0); } while(0)
#  elif defined __AVR_ATmega644__ || defined __AVR_ATmega644P__ || defined __AVR_ATmega1284P__
#    define SD_CHANGE_SETUP()   do { EICRA |= _BV(ISC00); EIMSK |= _BV(INT0); } while(0)
#  else
#    error Unknown chip!
#  endif
#  define SD_CHANGE_VECT        INT0_vect
#  define SDCARD_WP             (PIND & _BV(PD6))
#  define SDCARD_WP_SETUP()     do { DDRD &= ~ _BV(PD6); PORTD |= _BV(PD6); } while(0)
#  define SD_CHANGE_ICR         MCUCR
#  define SD_SUPPLY_VOLTAGE     (1L<<21)
#  define DEVICE_SELECT         (8+!(PINA & _BV(PA2))+2*!(PINA & _BV(PA3)))
#  define DEVICE_SELECT_SETUP() do {        \
             DDRA  &= ~(_BV(PA2)|_BV(PA3)); \
             PORTA |=   _BV(PA2)|_BV(PA3);  \
          } while (0)
#  define BUSY_LED_SETDDR()     DDRA  |= _BV(PA0)
#  define BUSY_LED_ON()         PORTA &= ~_BV(PA0)
#  define BUSY_LED_OFF()        PORTA |= _BV(PA0)
#  define DIRTY_LED_SETDDR()    DDRA  |= _BV(PA1)
#  define DIRTY_LED_ON()        PORTA &= ~_BV(PA1)
#  define DIRTY_LED_OFF()       PORTA |= _BV(PA1)
#  define DIRTY_LED_PORT        PORTA
#  define DIRTY_LED_BIT()       _BV(PA1)
#  define IEC_PIN               PINC
#  define IEC_DDR               DDRC
#  define IEC_PORT              PORTC
#  define IEC_PIN_ATN           PC0
#  define IEC_PIN_DATA          PC1
#  define IEC_PIN_CLOCK         PC2
#  define IEC_PIN_SRQ           PC3
#  define IEC_ATN_INT_VECT      PCINT2_vect
#  define IEC_ATN_INT_SETUP()   do { PCICR |= _BV(PCIE2); PCIFR |= _BV(PCIF2); } while (0)
#  define IEC_PCMSK             PCMSK2
#  define BUTTON_PIN            PINA
#  define BUTTON_PORT           PORTA
#  define BUTTON_DDR            DDRA
#  define BUTTON_MASK           (_BV(PA4)|_BV(PA5))
#  define BUTTON_NEXT           _BV(PA4)
#  define BUTTON_PREV           _BV(PA5)
#  define SOFTI2C_PORT          PORTC
#  define SOFTI2C_PIN           PINC
#  define SOFTI2C_DDR           DDRC
#  define SOFTI2C_BIT_SCL       PC6
#  define SOFTI2C_BIT_SDA       PC5
#  define SOFTI2C_BIT_INTRQ     PC7
#  define SOFTI2C_DELAY         6


#elif CONFIG_HARDWARE_VARIANT == 4
/* Hardware configuration: uIEC */
#  define HW_NAME "UIEC"
#  define HAVE_ATA
#  define CFCARD_DETECT         (!(PINE & _BV(PE7)))
#  define CFCARD_DETECT_SETUP() do { DDRE &= ~_BV(PE7); PORTE |= _BV(PE7); } while(0)
#  define CF_CHANGE_SETUP()     do { EICRB |= _BV(ISC70); EIMSK |= _BV(INT7); } while(0)
#  define CF_CHANGE_VECT        INT7_vect
#  define HAVE_SD
#  define SDCARD_DETECT         (!(PINB & _BV(PB7)))
#  define SDCARD_DETECT_SETUP() do { DDRB &= ~_BV(PB7); PORTB |= _BV(PB7); } while(0)
#  define SD_CHANGE_SETUP()     do { PCMSK0 |= _BV(PCINT7); PCICR |= _BV(PCIE0); PCIFR |= _BV(PCIF0); } while (0)
#  define SD_CHANGE_VECT        PCINT0_vect
#  define SDCARD_WP             (PINB & _BV(PB6))
#  define SDCARD_WP_SETUP()     do { DDRB &= ~ _BV(PB6); PORTB |= _BV(PB6); } while(0)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)
/* No device jumpers on uIEC */
#  define DEVICE_SELECT         10
#  define DEVICE_SELECT_SETUP() do {} while (0)
#  define SINGLE_LED
#  define DIRTY_LED_SETDDR()    DDRE  |= _BV(PE3)
#  define DIRTY_LED_ON()        PORTE |= _BV(PE3)
#  define DIRTY_LED_OFF()       PORTE &= ~_BV(PE3)
#  define DIRTY_LED_PORT        PORTE
#  define DIRTY_LED_BIT()       _BV(PE3)
#  define IEC_PIN               PINE
#  define IEC_DDR               DDRE
#  define IEC_PORT              PORTE
#  define IEC_PIN_ATN           PE6
#  define IEC_PIN_DATA          PE4
#  define IEC_PIN_CLOCK         PE5
#  define IEC_PIN_SRQ           PE2
#  define IEC_ATN_INT           INT6
#  define IEC_ATN_INT_VECT      INT6_vect
#  define IEC_CLK_INT           INT5
#  define IEC_CLK_INT_VECT      INT5_vect
#  define IEC_ATN_INT_SETUP()   do { EICRB |= _BV(ISC60); } while (0)
#  define IEC_CLK_INT_SETUP()   do { EICRB |= _BV(ISC50); } while (0)
#  undef  IEC_PCMSK
#  define BUTTON_PIN            PING
#  define BUTTON_PORT           PORTG
#  define BUTTON_DDR            DDRG
#  define BUTTON_MASK           (_BV(PG3)|_BV(PG4))
#  define BUTTON_NEXT           _BV(PG4)
#  define BUTTON_PREV           _BV(PG3)
#  define SOFTI2C_PORT          PORTD
#  define SOFTI2C_PIN           PIND
#  define SOFTI2C_DDR           DDRD
#  define SOFTI2C_BIT_SCL       PD0
#  define SOFTI2C_BIT_SDA       PD1
#  define SOFTI2C_BIT_INTRQ     PD2
#  define SOFTI2C_DELAY         6

/* Use diskmux code to optionally turn off second IDE drive */
#  define NEED_DISKMUX

#elif CONFIG_HARDWARE_VARIANT==5
/* Hardware configuration: Shadowolf 2 aka sd2iec 1.x */
#  define HW_NAME "SD2IEC"
#  define HAVE_SD
#  define SDCARD_DETECT         (!(PIND & _BV(PD2)))
#  define SDCARD_DETECT_SETUP() do { DDRD &= ~_BV(PD2); PORTD |= _BV(PD2); } while(0)
#  if defined __AVR_ATmega32__
#    define SD_CHANGE_SETUP()   do { MCUCR |= _BV(ISC00); GICR |= _BV(INT0); } while(0)
#  elif defined __AVR_ATmega644__ || defined __AVR_ATmega644P__ || defined __AVR_ATmega1284P__
#    define SD_CHANGE_SETUP()   do { EICRA |= _BV(ISC00); EIMSK |= _BV(INT0); } while(0)
#  else
#    error Unknown chip!
#  endif
#  define SD_CHANGE_VECT        INT0_vect
#  define SDCARD_WP             (PIND & _BV(PD6))
#  define SDCARD_WP_SETUP()     do { DDRD &= ~ _BV(PD6); PORTD |= _BV(PD6); } while(0)
#  define SD_SUPPLY_VOLTAGE     (1L<<18)
#  define DEVICE_SELECT         (8+!(PIND & _BV(PD7))+2*!(PIND & _BV(PD5)))
#  define DEVICE_SELECT_SETUP() do {        \
             DDRD  &= ~(_BV(PD7)|_BV(PD5)); \
             PORTD |=   _BV(PD7)|_BV(PD5);  \
          } while (0)
#  define BUSY_LED_SETDDR()     DDRC  |= _BV(PC0)
#  define BUSY_LED_ON()         PORTC |= _BV(PC0)
#  define BUSY_LED_OFF()        PORTC &= ~_BV(PC0)
#  define DIRTY_LED_SETDDR()    DDRC  |= _BV(PC1)
#  define DIRTY_LED_ON()        PORTC |= _BV(PC1)
#  define DIRTY_LED_OFF()       PORTC &= ~_BV(PC1)
#  define DIRTY_LED_PORT        PORTC
#  define DIRTY_LED_BIT()       _BV(PC1)
#  define IEC_PIN               PINA
#  define IEC_DDR               DDRA
#  define IEC_PORT              PORTA
#  define IEC_PIN_ATN           PA0
#  define IEC_PIN_DATA          PA1
#  define IEC_PIN_CLOCK         PA2
#  define IEC_PIN_SRQ           PA3
#  define IEC_SEPARATE_OUT
#  define IEC_OPIN_ATN          PA4
#  define IEC_OPIN_DATA         PA5
#  define IEC_OPIN_CLOCK        PA6
#  define IEC_OPIN_SRQ          PA7
#  define IEC_ATN_INT_VECT      PCINT0_vect
#  define IEC_ATN_INT_SETUP()   do { PCICR |= _BV(PCIE0); PCIFR |= _BV(PCIF0); } while (0)
#  define IEC_PCMSK             PCMSK0
#  define BUTTON_PIN            PINC
#  define BUTTON_PORT           PORTC
#  define BUTTON_DDR            DDRC
#  define BUTTON_MASK           (_BV(PC2)|_BV(PC3))
#  define BUTTON_NEXT           _BV(PC3)
#  define BUTTON_PREV           _BV(PC2)
#  define SOFTI2C_PORT          PORTC
#  define SOFTI2C_PIN           PINC
#  define SOFTI2C_DDR           DDRC
#  define SOFTI2C_BIT_SCL       PC4
#  define SOFTI2C_BIT_SDA       PC5
#  define SOFTI2C_BIT_INTRQ     PC6
#  define SOFTI2C_DELAY         6

#  ifdef CONFIG_TWINSD
/* Support for multiple SD cards */
#   define SD2_PORT             PORTD
#   define SD2_PIN              PIND
#   define SD2_PRESENT          _BV(PB2)
#   define SD2_CS               _BV(PD3)
#   define SD2_WP               (PINC & _BV(PC7))
#   define SD2_DETECT           (!(PINB & SD2_PRESENT))
#   define SD2_SETUP()          do { SD2_PORT |= SD2_CS; PORTC |= SD2_WP; DDRD |= SD2_CS; DDRC &= ~SD2_WP; DDRB &= ~SD2_PRESENT; PORTB |= SD2_PRESENT; } while (0)
#   define SD2_CHANGE_VECT      INT2_vect
#   define SD2_CHANGE_SETUP()   do { EICRA |= _BV(ISC20); EIMSK |= _BV(INT2);  } while (0)
#  endif


#elif CONFIG_HARDWARE_VARIANT == 6
/* Hardware configuration: NKC MMC2IEC */
#  define HW_NAME "SD2IEC"
#  define HAVE_SD
#  define SDCARD_DETECT         (!(PIND & _BV(PD2)))
#  define SDCARD_DETECT_SETUP() do { DDRD &= ~_BV(PD2); PORTD |= _BV(PD2); } while(0)
#  if defined __AVR_ATmega32__
#    define SD_CHANGE_SETUP()   do { MCUCR |= _BV(ISC00); GICR |= _BV(INT0); } while(0)
#  elif defined __AVR_ATmega644__ || defined __AVR_ATmega644P__ || defined __AVR_ATmega1284P__
#    define SD_CHANGE_SETUP()   do { EICRA |= _BV(ISC00); EIMSK |= _BV(INT0); } while(0)
#  else
#    error Unknown chip!
#  endif
#  define SD_CHANGE_VECT        INT0_vect
#  define SDCARD_WP             (PIND & _BV(PD6))
#  define SDCARD_WP_SETUP()     do { DDRD &= ~ _BV(PD6); PORTD |= _BV(PD6); } while(0)
#  define SD_CHANGE_ICR         MCUCR
#  define SD_SUPPLY_VOLTAGE     (1L<<21)
#  define DEVICE_SELECT         (8+!(PINA & _BV(PA2))+2*!(PINA & _BV(PA3)))
#  define DEVICE_SELECT_SETUP() do {        \
             DDRA  &= ~(_BV(PA2)|_BV(PA3)); \
             PORTA |=   _BV(PA2)|_BV(PA3);  \
          } while (0)
#  define BUSY_LED_SETDDR()     DDRA  |= _BV(PA0)
#  define BUSY_LED_ON()         PORTA &= ~_BV(PA0)
#  define BUSY_LED_OFF()        PORTA |= _BV(PA0)
#  define DIRTY_LED_SETDDR()    DDRA  |= _BV(PA1)
#  define DIRTY_LED_ON()        PORTA &= ~_BV(PA1)
#  define DIRTY_LED_OFF()       PORTA |= _BV(PA1)
#  define DIRTY_LED_PORT        PORTA
#  define DIRTY_LED_BIT()       _BV(PA1)
#  define IEC_PIN               PINB
#  define IEC_DDR               DDRB
#  define IEC_PORT              PORTB
#  define IEC_PIN_ATN           PB0
#  define IEC_PIN_DATA          PB1
#  define IEC_PIN_CLOCK         PB2
#  define IEC_PIN_SRQ           PB3
#  define IEC_ATN_INT_VECT      PCINT1_vect
#  define IEC_ATN_INT_SETUP()   do { PCICR |= _BV(PCIE1); PCIFR |= _BV(PCIF1); } while (0)
#  define IEC_PCMSK             PCMSK1
#  define BUTTON_PIN            PINA
#  define BUTTON_PORT           PORTA
#  define BUTTON_DDR            DDRA
#  define BUTTON_MASK           (_BV(PA4)|_BV(PA5))
#  define BUTTON_NEXT           _BV(PA4)
#  define BUTTON_PREV           _BV(PA5)


#elif CONFIG_HARDWARE_VARIANT == 7
/* Hardware configuration: uIEC v3 */
#  define HW_NAME "UIEC"
#  define HAVE_SD
#  define SDCARD_DETECT         (!(PINE & _BV(PE6)))
#  define SDCARD_DETECT_SETUP() do { DDRE &= ~_BV(PE6); PORTE |= _BV(PE6); } while(0)
#  define SD_CHANGE_SETUP()     do { EICRB |= _BV(ISC60); EIMSK |= _BV(INT6); } while(0)
#  define SD_CHANGE_VECT        INT6_vect
#  define SDCARD_WP             (PINE & _BV(PE2))
#  define SDCARD_WP_SETUP()     do { DDRE &= ~ _BV(PE2); PORTE |= _BV(PE2); } while(0)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)
/* No device jumpers on uIEC */
#  define DEVICE_SELECT         10
#  define DEVICE_SELECT_SETUP() do {} while (0)
#  define SINGLE_LED
#  define DIRTY_LED_SETDDR()    DDRG  |= _BV(PG0)
#  define DIRTY_LED_ON()        PORTG |= _BV(PG0)
#  define DIRTY_LED_OFF()       PORTG &= ~_BV(PG0)
#  define DIRTY_LED_PORT        PORTG
#  define DIRTY_LED_BIT()       _BV(PG0)
#  define POWER_LED_DDR         DDRG
#  define POWER_LED_PORT        PORTG
#  define POWER_LED_BIT         _BV(PG1)
#  define POWER_LED_POLARITY    0
#  define IEC_PIN               PINB
#  define IEC_DDRIN             DDRB
#  define IEC_PORTIN            PORTB
#  define IEC_PIN_ATN           PB4
#  define IEC_PIN_DATA          PB5
#  define IEC_PIN_CLOCK         PB6
#  define IEC_PIN_SRQ           PB7
#  define IEC_SEPARATE_OUT
#  define IEC_PORT              PORTD
#  define IEC_DDROUT            DDRD
#  define IEC_OPIN_ATN          PD4
#  define IEC_OPIN_DATA         PD5
#  define IEC_OPIN_CLOCK        PD6
#  define IEC_OPIN_SRQ          PD7
#  define IEC_ATN_INT_VECT      PCINT0_vect
#  define IEC_ATN_INT_SETUP()   do { PCICR |= _BV(PCIE0); PCIFR |= _BV(PCIF0); } while (0)
#  define IEC_PCMSK             PCMSK0
#  define BUTTON_PIN            PING
#  define BUTTON_PORT           PORTG
#  define BUTTON_DDR            DDRG
#  define BUTTON_MASK           (_BV(PG3)|_BV(PG4))
#  define BUTTON_NEXT           _BV(PG4)
#  define BUTTON_PREV           _BV(PG3)

#elif CONFIG_HARDWARE_VARIANT == 99
/* Hardware configuration: sdlite - not for production use */
#  define HW_NAME "SD2IEC"
#  define HAVE_SD
#  define SDCARD_DETECT         1
#  define SDCARD_DETECT_SETUP() do { } while(0)
#  define SD_CHANGE_SETUP()     do { } while(0)
#  define SDCARD_WP             0
#  define SDCARD_WP_SETUP()     do { } while(0)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)
#  define DATAFLASH_PORT        PORTD
#  define DATAFLASH_DDR         DDRD
#  define DATAFLASH_SELECT      _BV(PD2)
#  define DEVICE_SELECT         8
#  define DEVICE_SELECT_SETUP() do {} while(0)
#  define SINGLE_LED
#  define DIRTY_LED_SETDDR()    DDRD  |= _BV(PD7)
#  define DIRTY_LED_ON()        PORTD |= _BV(PD7)
#  define DIRTY_LED_OFF()       PORTD &= ~_BV(PD7)
#  define DIRTY_LED_PORT        PORTD
#  define DIRTY_LED_BIT()       _BV(PD7)
#  define IEC_PIN               PINB
#  define IEC_DDR               DDRB
#  define IEC_PORT              PORTB
#  define IEC_PIN_ATN           PB0
#  define IEC_PIN_DATA          PB2
#  define IEC_PIN_CLOCK         PB1
#  define IEC_PIN_SRQ           PB3
#  define IEC_ATN_INT_VECT      PCINT1_vect
#  define IEC_ATN_INT_SETUP()   do { PCICR |= _BV(PCIE1); PCIFR |= _BV(PCIF1); } while (0)
#  define IEC_PCMSK             PCMSK1
#  define BUTTON_PIN            PIND
#  define BUTTON_PORT           PORTD
#  define BUTTON_DDR            DDRD
#  define BUTTON_MASK           (_BV(PD5)|_BV(PD6))
#  define BUTTON_NEXT           _BV(PD6)
#  define BUTTON_PREV           _BV(PD5)


#else
#  error "CONFIG_HARDWARE_VARIANT is unset or set to an unknown value."
#endif


/* ---------------- End of user-configurable options ---------------- */

#define IEC_BIT_ATN      _BV(IEC_PIN_ATN)
#define IEC_BIT_DATA     _BV(IEC_PIN_DATA)
#define IEC_BIT_CLOCK    _BV(IEC_PIN_CLOCK)
#define IEC_BIT_SRQ      _BV(IEC_PIN_SRQ)

#ifdef IEC_SEPARATE_OUT
#  define IEC_OBIT_ATN   _BV(IEC_OPIN_ATN)
#  define IEC_OBIT_DATA  _BV(IEC_OPIN_DATA)
#  define IEC_OBIT_CLOCK _BV(IEC_OPIN_CLOCK)
#  define IEC_OBIT_SRQ   _BV(IEC_OPIN_SRQ)
#  define IEC_OUT        IEC_PORT
#else
#  define IEC_OPIN_ATN   IEC_PIN_ATN
#  define IEC_OPIN_DATA  IEC_PIN_DATA
#  define IEC_OPIN_CLOCK IEC_PIN_CLOCK
#  define IEC_OPIN_SRQ   IEC_PIN_SRQ
#  define IEC_OBIT_ATN   IEC_BIT_ATN
#  define IEC_OBIT_DATA  IEC_BIT_DATA
#  define IEC_OBIT_CLOCK IEC_BIT_CLOCK
#  define IEC_OBIT_SRQ   IEC_BIT_SRQ
#  define IEC_OUT        IEC_DDR
#endif

#ifndef IEC_PORTIN
#  define IEC_PORTIN IEC_PORT
#endif

#ifndef IEC_DDRIN
#  define IEC_DDRIN  IEC_DDR
#  define IEC_DDROUT IEC_DDR
#endif

#ifdef IEC_PCMSK
   /* For hardware configurations using PCINT for IEC IRQs */
#  define set_atn_irq(x) \
     if (x) { IEC_PCMSK |= _BV(IEC_PIN_ATN); } \
     else { IEC_PCMSK &= (uint8_t)~_BV(IEC_PIN_ATN); }
#  define set_clock_irq(x) \
     if (x) { IEC_PCMSK |= _BV(IEC_PIN_CLOCK); } \
     else { IEC_PCMSK &= (uint8_t)~_BV(IEC_PIN_CLOCK); }
#else
#  ifdef IEC_ATN_INT
     /* Hardware has an ATN interrupt */
#    define set_atn_irq(x) \
       if (x) { EIMSK |= _BV(IEC_ATN_INT); } \
       else { EIMSK &= (uint8_t)~_BV(IEC_ATN_INT); }
#  else
     /* Polling: Yuck. */
#    define set_atn_irq(x) \
       if (x) { TIMSK2 |= _BV(OCIE2A); } \
       else { TIMSK2 &= (uint8_t)~_BV(OCIE2A); }
#  endif // IEC_ATN_INT

#  ifdef IEC_CLK_INT
     /* Hardware has a CLK interrupt */
#    define set_clock_irq(x) \
       if (x) { EIMSK |= _BV(IEC_CLK_INT); } \
       else { EIMSK &= (uint8_t)~_BV(IEC_CLK_INT); }
#  endif
#endif

/* Create no-op interrupt setups if they are undefined */
#ifndef IEC_ATN_INT_SETUP
#  define IEC_ATN_INT_SETUP() do {} while (0)
#endif
#ifndef IEC_CLK_INT_SETUP
#  define IEC_CLK_INT_SETUP() do {} while (0)
#endif

/* Disable COMMAND_CHANNEL_DUMP if UART_DEBUG is disabled */
#ifndef CONFIG_UART_DEBUG
#  undef CONFIG_COMMAND_CHANNEL_DUMP
#endif

/* An interrupt for detecting card changes implies hotplugging capability */
#if defined(SD_CHANGE_VECT) || defined (CF_CHANGE_VECT)
#  define HAVE_HOTPLUG
#endif

/* Generate dummy functions for the BUSY LED if required */
#ifdef SINGLE_LED
#  define BUSY_LED_SETDDR() do {} while(0)
#  define BUSY_LED_ON()     do {} while(0)
#  define BUSY_LED_OFF()    do {} while(0)
#endif

/* Translate CONFIG_ADD symbols to HAVE symbols */
/* By using two symbols for this purpose it's easier to determine if */
/* support was enabled by default or added in the config file.       */
#if defined(CONFIG_ADD_SD) && !defined(HAVE_SD)
#  define HAVE_SD
#endif

#if defined(CONFIG_ADD_ATA) && !defined(HAVE_ATA)
#  define HAVE_ATA
#endif

#if defined(CONFIG_ADD_DF) && !defined(HAVE_DF)
#  define HAVE_DF
#endif

/* Create some temporary symbols so we can calculate the number of */
/* enabled storage devices.                                        */
#ifdef HAVE_SD
#  define TMP_SD 1
#endif
#ifdef HAVE_ATA
#  define TMP_ATA 1
#endif
#ifdef HAVE_DF
#  define TMP_DF 1
#endif

/* Enable the diskmux if more than one storage device is enabled. */
#if !defined(NEED_DISKMUX) && (TMP_SD + TMP_ATA + TMP_DF) > 1
#  define NEED_DISKMUX
#endif

/* Remove the temporary symbols */
#undef TMP_SD
#undef TMP_ATA
#undef TMP_DF

/* Hardcoded maximum - reducing this won't save any ram */
#define MAX_DRIVES 8

#endif
