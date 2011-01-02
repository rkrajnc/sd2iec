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
#include <avr/io.h>
#include <avr/interrupt.h>

/* Include avrcompat.h to get the PA0..PD7 macros on 1284P */
#include "avrcompat.h"

/* ----- Common definitions for all AVR hardware variants ------ */

/* SPI clock divisors - 250kHz slow, 2MHz fast */
#define SPI_DIVISOR_SLOW 32
#define SPI_DIVISOR_FAST 4

/* Return value of buttons_read() */
typedef uint8_t rawbutton_t;

/* Interrupt handler for system tick */
#define SYSTEM_TICK_HANDLER ISR(TIMER1_COMPA_vect)

#if CONFIG_HARDWARE_VARIANT==1
/* ---------- Hardware configuration: Example ---------- */
/* This is a commented example for most of the available options    */
/* in case someone wants to build Yet Another[tm] hardware variant. */
/* Some of the values are chosen randomly, so this variant is not   */
/* expected to compile successfully.                                */

/* Name the software should return in the Dos version message (73) */
/* This should be upper-case because it isn't PETSCII-converted.   */
/* Change this if you're forking the code!                         */
#  define HW_NAME "SD2IEC"

/*** SD card support ***/
/* If your device supports SD cards by default, define this symbol. */
#  define HAVE_SD

/* Declaration of the interrupt handler for SD card change */
#  define SD_CHANGE_HANDLER ISR(INT0_vect)

/* Declaration of the interrupt handler for SD card 2 change */
#  define SD2_CHANGE_HANDLER ISR(INT9_vect)

/* Initialize all pins and interrupts related to SD - except SPI */
static inline void sdcard_interface_init(void) {
  /* card detect (SD1) */
  DDRD  &= ~_BV(PD2);
  PORTD |=  _BV(PD2);
  /* write protect (SD1) */
  DDRD &= ~_BV(PD6);
  PORTD |= _BV(PD6);
  /* card change interrupt (SD1) */
  EICRA |= _BV(ISC00);
  EIMSK |= _BV(INT0);
  // Note: Wrapping SD2 in CONFIG_TWINSD may be a good idea
  /* chip select (SD2) */
  PORTD |= _BV(PD4);
  DDRD |= _BV(PD4);
  /* card detect (SD2) */
  DDRD &= ~_BV(PD3);
  PORTD |= _BV(PD3);
  /* write protect (SD2) */
  DDRD &= ~_BV(PD7);
  PORTD |= _BV(PD7);
  /* card change interrupt (SD2) */
  EICRA |=  _BV(ISC90); // Change interrupt
  EIMSK |=  _BV(INT9);  // Change interrupt
}

/* sdcard_detect() must return non-zero while a card is inserted */
/* This must be a pin capable of generating interrupts.          */
static inline uint8_t sdcard_detect(void) {
  return !(PIND & _BV(PD2));
}

/* Returns non-zero when the currently inserted card is write-protected */
static inline uint8_t sdcard_wp(void) {
  return PIND & _BV(PD6);
}

/* Support for a second SD card - use CONFIG_TWINSD=y in your config file to enable! */
/* Same as the two functions above, but for card 2 */
static inline uint8_t sdcard2_detect(void) {
  return !(PIND & _BV(PD3));
}
static inline uint8_t sdcard2_wp(void) {
  return PIND & _BV(PD7);
}

/* SD card 1 is assumed to use the standard SS pin   */
/* If that's not true, #define SDCARD_SS_SPECIAL and */
/* implement this function:                          */
//static inline __attribute__((always_inline)) void sdcard_set_ss(uint8_t state) {
//  if (state)
//    PORTZ |= _BV(PZ9);
//  else
//    PORTZ &= ~_BV(PZ9);
//}

/* SD card 2 CS pin */
static inline __attribute__((always_inline)) void sdcard2_set_ss(uint8_t state) {
  if (state)
    PORTD |= _BV(PD4);
  else
    PORTD &= ~_BV(PD4);
}

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


/*** Device address selection ***/
/* device_hw_address() returns the hardware-selected device address */
static inline uint8_t device_hw_address(void) {
  return 8 + !(PIND & _BV(PD7)) + 2*!(PIND & _BV(PD5));
}

/* Configure hardware device address pins */
static inline void device_hw_address_init(void) {
  DDRD  &= ~(_BV(PD7) | _BV(PD5));
  PORTD |=   _BV(PD7) | _BV(PD5);
}


/*** LEDs ***/
/* Please don't build single-LED hardware anymore... */

/* Initialize ports for all LEDs */
static inline void leds_init(void) {
  DDRC |= _BV(PC0);
  DDRC |= _BV(PC1);
}

/* --- "BUSY" led, recommended color: green (usage similiar to 1541 LED) --- */
static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
  if (state)
    PORTC |= _BV(PC0);
  else
    PORTC &= ~_BV(PC0);
}

/* --- "DIRTY" led, recommended color: red (errors, unwritten data in memory) --- */
static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
  if (state)
    PORTC |= _BV(PC1);
  else
    PORTC &= ~_BV(PC1);
}

/* Toggle function used for error blinking */
static inline void toggle_dirty_led(void) {
  /* Sufficiently new AVR cores have a toggle function */
  PINC |= _BV(PC1);
}

/* --- Software power LED (enabled at startup, not touched after that) --- */
/* Currently used on uIEC/SD only */
//#define HAVE_POWER_LED
//static inline __attribute__((always_inline)) void set_power_led(uint8_t state) {
//  if (state)
//    PORTG |= _BV(PG1);
//  else
//    PORTG &= ~_BV(PG1);
//}


/*** IEC signals ***/
#  define IEC_INPUT PINA
#  define IEC_DDR   DDRA
#  define IEC_PORT  PORTA

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

/* ATN interrupt (required) */
#  define IEC_ATN_INT_VECT    PCINT0_vect
static inline void iec_interrupts_init(void) {
  PCMSK0 = _BV(PCINT0);
  PCIFR |= _BV(PCIF0);
}

/* CLK interrupt (not required) */
/* Dreamload requires interrupts for both the ATN and CLK lines. If both are served by */
/* the same PCINT vector, define that as ATN interrupt above and define IEC_PCMSK.     */
//#  define IEC_PCMSK             PCMSK0
/* If the CLK line has its own dedicated interrupt, use the following definitions: */
//#  define IEC_CLK_INT           INT5
//#  define IEC_CLK_INT_VECT      INT5_vect
//static inline void iec_clock_int_setup(void) {
//  EICRB |= _BV(ISC50);
//}


/*** User interface ***/
/* Button NEXT changes to the next disk image and enables sleep mode (held) */
#  define BUTTON_NEXT _BV(PC4)

/* Button PREV changes to the previous disk image */
/* If you don't have/need this button, define it as 0. */
#  define BUTTON_PREV _BV(PC3)

/* Read the raw button state - a depressed button should read as 0 */
static inline rawbutton_t buttons_read(void) {
  return PINC & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
  DDRC  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
  PORTC |= BUTTON_NEXT | BUTTON_PREV;
}


/* Software I2C lines for the RTC */
#  define SOFTI2C_PORT    PORTC
#  define SOFTI2C_PIN     PINC
#  define SOFTI2C_DDR     DDRC
#  define SOFTI2C_BIT_SCL PC4
#  define SOFTI2C_BIT_SDA PC5
#  define SOFTI2C_DELAY   6


/* Pre-configurated hardware variants */

#elif CONFIG_HARDWARE_VARIANT==2
/* ---------- Hardware configuration: Shadowolf 1 ---------- */
#  define HW_NAME "SD2IEC"
#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(INT0_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<18)

static inline void sdcard_interface_init(void) {
  DDRD &= ~_BV(PD2);
  PORTD |= _BV(PD2);
  DDRD &= ~_BV(PD6);
  PORTD |= _BV(PD6);
  EICRA |= _BV(ISC00);
  EIMSK |= _BV(INT0);
}

static inline uint8_t sdcard_detect(void) {
  return !(PIND & _BV(PD2));
}

static inline uint8_t sdcard_wp(void) {
  return PIND & _BV(PD6);
}

static inline uint8_t device_hw_address(void) {
  return 8 + !(PIND & _BV(PD7)) + 2*!(PIND & _BV(PD5));
}

static inline void device_hw_address_init(void) {
  DDRD  &= ~(_BV(PD7)|_BV(PD5));
  PORTD |=   _BV(PD7)|_BV(PD5);
}

static inline void leds_init(void) {
  DDRC |= _BV(PC0);
  DDRC |= _BV(PC1);
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
  if (state)
    PORTC |= _BV(PC0);
  else
    PORTC &= ~_BV(PC0);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
  if (state)
    PORTC |= _BV(PC1);
  else
    PORTC &= ~_BV(PC1);
}

static inline void toggle_dirty_led(void) {
  PINC |= _BV(PC1);
}

#  define IEC_INPUT             PINA
#  define IEC_DDR               DDRA
#  define IEC_PORT              PORTA
#  define IEC_PIN_ATN           PA0
#  define IEC_PIN_DATA          PA1
#  define IEC_PIN_CLOCK         PA2
#  define IEC_PIN_SRQ           PA3
#  define IEC_ATN_INT_VECT      PCINT0_vect
#  define IEC_PCMSK             PCMSK0

static inline void iec_interrupts_init(void) {
  PCICR |= _BV(PCIE0);
  PCIFR |= _BV(PCIF0);
}

#  define BUTTON_NEXT           _BV(PC4)
#  define BUTTON_PREV           _BV(PC3)

static inline rawbutton_t buttons_read(void) {
  return PINC & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
  DDRC  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
  PORTC |= BUTTON_NEXT | BUTTON_PREV;
}


#elif CONFIG_HARDWARE_VARIANT == 3
/* ---------- Hardware configuration: LarsP ---------- */
#  define HW_NAME "SD2IEC"
#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(INT0_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)

static inline void sdcard_interface_init(void) {
  DDRD  &= ~_BV(PD2);
  PORTD |=  _BV(PD2);
  DDRD  &= ~_BV(PD6);
  PORTD |=  _BV(PD6);
  EICRA |=  _BV(ISC00);
  EIMSK |=  _BV(INT0);
}

static inline uint8_t sdcard_detect(void) {
  return !(PIND & _BV(PD2));
}

static inline uint8_t sdcard_wp(void) {
  return PIND & _BV(PD6);
}

static inline uint8_t device_hw_address(void) {
  return 8 + !(PINA & _BV(PA2)) + 2*!(PINA & _BV(PA3));
}

static inline void device_hw_address_init(void) {
  DDRA  &= ~(_BV(PA2)|_BV(PA3));
  PORTA |=   _BV(PA2)|_BV(PA3);
}

static inline void leds_init(void) {
  DDRA |= _BV(PA0);
  DDRA |= _BV(PA1);
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
  if (state)
    PORTA &= ~_BV(PA0);
  else
    PORTA |= _BV(PA0);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
  if (state)
    PORTA &= ~_BV(PA1);
  else
    PORTA |= _BV(PA1);
}

static inline void toggle_dirty_led(void) {
  PINA |= _BV(PA1);
}

#  define IEC_INPUT             PINC
#  define IEC_DDR               DDRC
#  define IEC_PORT              PORTC
#  define IEC_PIN_ATN           PC0
#  define IEC_PIN_DATA          PC1
#  define IEC_PIN_CLOCK         PC2
#  define IEC_PIN_SRQ           PC3
#  define IEC_ATN_INT_VECT      PCINT2_vect
#  define IEC_PCMSK             PCMSK2

static inline void iec_interrupts_init(void) {
  PCICR |= _BV(PCIE2);
  PCIFR |= _BV(PCIF2);
}

#  define BUTTON_NEXT           _BV(PA4)
#  define BUTTON_PREV           _BV(PA5)

static inline rawbutton_t buttons_read(void) {
  return PINA & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
  DDRA  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
  PORTA |= BUTTON_NEXT | BUTTON_PREV;
}

#  define SOFTI2C_PORT          PORTC
#  define SOFTI2C_PIN           PINC
#  define SOFTI2C_DDR           DDRC
#  define SOFTI2C_BIT_SCL       PC6
#  define SOFTI2C_BIT_SDA       PC5
#  define SOFTI2C_BIT_INTRQ     PC7
#  define SOFTI2C_DELAY         6


#elif CONFIG_HARDWARE_VARIANT == 4
/* ---------- Hardware configuration: uIEC ---------- */
#  define HW_NAME "UIEC"
#  define HAVE_ATA
#  define HAVE_SD
#  define SPI_LATE_INIT
#  define CF_CHANGE_HANDLER     ISR(INT7_vect)
#  define SD_CHANGE_HANDLER     ISR(PCINT0_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)
#  define SINGLE_LED

static inline void cfcard_interface_init(void) {
  DDRE  &= ~_BV(PE7);
  PORTE |=  _BV(PE7);
  EICRB |=  _BV(ISC70);
  EIMSK |=  _BV(INT7);
}

static inline uint8_t cfcard_detect(void) {
  return !(PINE & _BV(PE7));
}

static inline void sdcard_interface_init(void) {
  DDRB   &= ~_BV(PB7);
  PORTB  |=  _BV(PB7);
  DDRB   &= ~_BV(PB6);
  PORTB  |=  _BV(PB6);
  PCMSK0 |=  _BV(PCINT7);
  PCICR  |=  _BV(PCIE0);
  PCIFR  |=  _BV(PCIF0);
}

static inline uint8_t sdcard_detect(void) {
  return !(PINB & _BV(PB7));
}

static inline uint8_t sdcard_wp(void) {
  return PINB & _BV(PB6);
}

static inline uint8_t device_hw_address(void) {
  /* No device jumpers on uIEC */
  return 10;
}

static inline void device_hw_address_init(void) {
  return;
}

static inline void leds_init(void) {
  DDRE |= _BV(PE3);
}

static inline __attribute__((always_inline)) void set_led(uint8_t state) {
  if (state)
    PORTE |= _BV(PE3);
  else
    PORTE &= ~_BV(PE3);
}

static inline void toggle_led(void) {
  PINE |= _BV(PE3);
}

#  define IEC_INPUT             PINE
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
#  undef  IEC_PCMSK

static inline void iec_interrupts_init(void) {
  EICRB |= _BV(ISC60);
  EICRB |= _BV(ISC50);
}

#  define BUTTON_NEXT           _BV(PG4)
#  define BUTTON_PREV           _BV(PG3)

static inline rawbutton_t buttons_read(void) {
  return PING & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
  DDRG  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
  PORTG |= BUTTON_NEXT | BUTTON_PREV;
}

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
/* ---------- Hardware configuration: Shadowolf 2 aka sd2iec 1.x ---------- */
#  define HW_NAME "SD2IEC"
#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(INT0_vect)
#  define SD2_CHANGE_HANDLER    ISR(INT2_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<18)

static inline void sdcard_interface_init(void) {
  DDRD  &= ~_BV(PD2);
  PORTD |=  _BV(PD2);
  DDRD  &= ~_BV(PD6);
  PORTD |=  _BV(PD6);
  EICRA |=  _BV(ISC00);
  EIMSK |=  _BV(INT0);
#ifdef CONFIG_TWINSD
  PORTD |=  _BV(PD3); // CS
  DDRD  |=  _BV(PD3); // CS
  DDRC  &= ~_BV(PC7); // WP
  PORTC |=  _BV(PC7); // WP
  DDRB  &= ~_BV(PB2); // Detect
  PORTB |=  _BV(PB2); // Detect
  EICRA |=  _BV(ISC20); // Change interrupt
  EIMSK |=  _BV(INT2);  // Change interrupt
#endif
}

static inline uint8_t sdcard_detect(void) {
  return !(PIND & _BV(PD2));
}

static inline uint8_t sdcard_wp(void) {
  return PIND & _BV(PD6);
}

static inline uint8_t sdcard2_detect(void) {
  return !(PINB & _BV(PB2));
}

static inline uint8_t sdcard2_wp(void) {
  return PINC & _BV(PC7);
}

static inline __attribute__((always_inline)) void sdcard2_set_ss(uint8_t state) {
  if (state)
    PORTD |= _BV(PD3);
  else
    PORTD &= ~_BV(PD3);
}

static inline uint8_t device_hw_address(void) {
  return 8 + !(PIND & _BV(PD7)) + 2*!(PIND & _BV(PD5));
}

static inline void device_hw_address_init(void) {
  DDRD  &= ~(_BV(PD7)|_BV(PD5));
  PORTD |=   _BV(PD7)|_BV(PD5);
}

static inline void leds_init(void) {
  DDRC |= _BV(PC0);
  DDRC |= _BV(PC1);
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
  if (state)
    PORTC |= _BV(PC0);
  else
    PORTC &= ~_BV(PC0);
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
  if (state)
    PORTC |= _BV(PC1);
  else
    PORTC &= ~_BV(PC1);
}

static inline void toggle_dirty_led(void) {
  PINC |= _BV(PC1);
}

#  define IEC_INPUT             PINA
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
#  define IEC_PCMSK             PCMSK0

static inline void iec_interrupts_init(void) {
  PCICR |= _BV(PCIE0);
  PCIFR |= _BV(PCIF0);
}

#  define BUTTON_NEXT           _BV(PC3)
#  define BUTTON_PREV           _BV(PC2)

static inline rawbutton_t buttons_read(void) {
  return PINC & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
  DDRC  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
  PORTC |= BUTTON_NEXT | BUTTON_PREV;
}

#  define SOFTI2C_PORT          PORTC
#  define SOFTI2C_PIN           PINC
#  define SOFTI2C_DDR           DDRC
#  define SOFTI2C_BIT_SCL       PC4
#  define SOFTI2C_BIT_SDA       PC5
#  define SOFTI2C_BIT_INTRQ     PC6
#  define SOFTI2C_DELAY         6


/* Hardware configuration 6 was old NKC MMC2IEC */


#elif CONFIG_HARDWARE_VARIANT == 7
/* ---------- Hardware configuration: uIEC v3 ---------- */
#  define HW_NAME "UIEC"
#  define HAVE_SD
#  define SD_CHANGE_HANDLER     ISR(INT6_vect)
#  define SD_SUPPLY_VOLTAGE     (1L<<21)
#  define SINGLE_LED
#  define HAVE_POWER_LED

static inline void sdcard_interface_init(void) {
  DDRE  &= ~_BV(PE6);
  PORTE |=  _BV(PE6);
  DDRE  &= ~_BV(PE2);
  PORTE |=  _BV(PE2);
  EICRB |=  _BV(ISC60);
  EIMSK |=  _BV(INT6);
}

static inline uint8_t sdcard_detect(void) {
  return !(PINE & _BV(PE6));
}

static inline uint8_t sdcard_wp(void) {
  return PINE & _BV(PE2);
}

static inline uint8_t device_hw_address(void) {
  /* No device jumpers on uIEC */
  return 10;
}

static inline void device_hw_address_init(void) {
  return;
}

static inline void leds_init(void) {
  DDRG |= _BV(PG0);
  DDRG |= _BV(PG1);
}

static inline __attribute__((always_inline)) void set_led(uint8_t state) {
  if (state)
    PORTG |= _BV(PG0);
  else
    PORTG &= ~_BV(PG0);
}

static inline void toggle_led(void) {
  PING |= _BV(PG0);
}

static inline __attribute__((always_inline)) void set_power_led(uint8_t state) {
  if (state)
    PORTG |= _BV(PG1);
  else
    PORTG &= ~_BV(PG1);
}

#  define IEC_INPUT             PINB
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
#  define IEC_PCMSK             PCMSK0

static inline void iec_interrupts_init(void) {
  PCICR |= _BV(PCIE0);
  PCIFR |= _BV(PCIF0);
}

#  define BUTTON_NEXT           _BV(PG4)
#  define BUTTON_PREV           _BV(PG3)

static inline rawbutton_t buttons_read(void) {
  return PING & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
  DDRG  &= (uint8_t)~(BUTTON_NEXT | BUTTON_PREV);
  PORTG |= BUTTON_NEXT | BUTTON_PREV;
}


#else
#  error "CONFIG_HARDWARE_VARIANT is unset or set to an unknown value."
#endif


/* ---------------- End of user-configurable options ---------------- */

#define IEC_BIT_ATN      _BV(IEC_PIN_ATN)
#define IEC_BIT_DATA     _BV(IEC_PIN_DATA)
#define IEC_BIT_CLOCK    _BV(IEC_PIN_CLOCK)
#define IEC_BIT_SRQ      _BV(IEC_PIN_SRQ)

/* Return type of iec_bus_read() */
typedef uint8_t iec_bus_t;

#ifdef IEC_SEPARATE_OUT
#  define IEC_OBIT_ATN   _BV(IEC_OPIN_ATN)
#  define IEC_OBIT_DATA  _BV(IEC_OPIN_DATA)
#  define IEC_OBIT_CLOCK _BV(IEC_OPIN_CLOCK)
#  define IEC_OBIT_SRQ   _BV(IEC_OPIN_SRQ)
#  define IEC_OUTPUT     IEC_PORT
#else
#  define IEC_OPIN_ATN   IEC_PIN_ATN
#  define IEC_OPIN_DATA  IEC_PIN_DATA
#  define IEC_OPIN_CLOCK IEC_PIN_CLOCK
#  define IEC_OPIN_SRQ   IEC_PIN_SRQ
#  define IEC_OBIT_ATN   IEC_BIT_ATN
#  define IEC_OBIT_DATA  IEC_BIT_DATA
#  define IEC_OBIT_CLOCK IEC_BIT_CLOCK
#  define IEC_OBIT_SRQ   IEC_BIT_SRQ
#  define IEC_OUTPUT     IEC_DDR
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

/* IEC lines initialisation */
static inline void iec_interface_init(void) {
#ifdef IEC_SEPARATE_OUT
  /* Set up the input port - pullups on all lines */
  IEC_DDRIN  &= (uint8_t)~(IEC_BIT_ATN  | IEC_BIT_CLOCK  | IEC_BIT_DATA  | IEC_BIT_SRQ);
  IEC_PORTIN |= IEC_BIT_ATN | IEC_BIT_CLOCK | IEC_BIT_DATA | IEC_BIT_SRQ;
  /* Set up the output port - all lines high */
  IEC_DDROUT |=            IEC_OBIT_ATN | IEC_OBIT_CLOCK | IEC_OBIT_DATA | IEC_OBIT_SRQ;
  IEC_PORT   &= (uint8_t)~(IEC_OBIT_ATN | IEC_OBIT_CLOCK | IEC_OBIT_DATA | IEC_OBIT_SRQ);
#else
  /* Pullups would be nice, but AVR can't switch from */
  /* low output to hi-z input directly                */
  IEC_DDR  &= (uint8_t)~(IEC_BIT_ATN | IEC_BIT_CLOCK | IEC_BIT_DATA | IEC_BIT_SRQ);
  IEC_PORT &= (uint8_t)~(IEC_BIT_ATN | IEC_BIT_CLOCK | IEC_BIT_DATA | IEC_BIT_SRQ);
#endif
}

/* The assembler module needs the vector names, */
/* so the _HANDLER macros are created here.     */
#define IEC_ATN_HANDLER   ISR(IEC_ATN_INT_VECT)
#define IEC_CLOCK_HANDLER ISR(IEC_CLK_INT_VECT)

/* SD SS pin default implementation */
#ifndef SDCARD_SS_SPECIAL
static inline __attribute__((always_inline)) void sdcard_set_ss(uint8_t state) {
  if (state)
    SPI_PORT |= SPI_SS;
  else
    SPI_PORT &= ~SPI_SS;
}
#endif

/* Display interrupt pin */
#ifdef CONFIG_REMOTE_DISPLAY
static inline void display_intrq_init(void) {
  /* Enable pullup on the interrupt line */
  SOFTI2C_PORT |= _BV(SOFTI2C_BIT_INTRQ);
}

static inline uint8_t display_intrq_active(void) {
  return !(SOFTI2C_PIN & _BV(SOFTI2C_BIT_INTRQ));
}
#endif

/* Disable COMMAND_CHANNEL_DUMP if UART_DEBUG is disabled */
#ifndef CONFIG_UART_DEBUG
#  undef CONFIG_COMMAND_CHANNEL_DUMP
#endif

/* An interrupt for detecting card changes implies hotplugging capability */
#if defined(SD_CHANGE_HANDLER) || defined (CF_CHANGE_HANDLER)
#  define HAVE_HOTPLUG
#endif

/* Generate a dummy function for the Power-LED if required */
#ifndef HAVE_POWER_LED
static inline void set_power_led(uint8_t state) {
  return;
}
#endif

/* ----- Translate CONFIG_ADD symbols to HAVE symbols ----- */
/* By using two symbols for this purpose it's easier to determine if */
/* support was enabled by default or added in the config file.       */
#if defined(CONFIG_ADD_SD) && !defined(HAVE_SD)
#  define HAVE_SD
#endif

#if defined(CONFIG_ADD_ATA) && !defined(HAVE_ATA)
#  define HAVE_ATA
#endif

/* Create some temporary symbols so we can calculate the number of */
/* enabled storage devices.                                        */
#ifdef HAVE_SD
#  define TMP_SD 1
#endif
#ifdef HAVE_ATA
#  define TMP_ATA 1
#endif

/* Enable the diskmux if more than one storage device is enabled. */
#if !defined(NEED_DISKMUX) && (TMP_SD + TMP_ATA) > 1
#  define NEED_DISKMUX
#endif

/* Remove the temporary symbols */
#undef TMP_SD
#undef TMP_ATA

/* Hardcoded maximum - reducing this won't save any ram */
#define MAX_DRIVES 8

/* SD access LED dummy */
#ifndef HAVE_SD_LED
# define set_sd_led(x) do {} while (0)
#endif

/* Sanity check */
#if defined(CONFIG_LOADER_WHEELS) && !defined(CONFIG_LOADER_GEOS)
#  error "CONFIG_LOADER_GEOS must be enabled for Wheels support!"
#endif

#endif
