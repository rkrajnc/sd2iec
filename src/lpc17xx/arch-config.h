/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2012  Ingo Korb <ingo@akana.de>

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


   arch-config.h: The main architecture-specific config header

*/

#ifndef ARCH_CONFIG_H
#define ARCH_CONFIG_H

#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>

/* ----- Common definitions for all LPC17xx hardware variants ------ */

/* PLL settings for 100MHz from a 12MHz crystal */
#define PLL_MULTIPLIER 25
#define PLL_PREDIV     2
#define PLL_DIVISOR    3

/* Return value of buttons_read() */
typedef unsigned int rawbutton_t;

/* Interrupt handler for system tick */
#define SYSTEM_TICK_HANDLER void SysTick_Handler(void)

/* SSP clock divisors (must be even) - 400kHz slow, 16.7MHz fast */
#define SSP_CLK_DIVISOR_FAST 6
#define SSP_CLK_DIVISOR_SLOW 250

/* IEC in/out are always seperate */
#define IEC_SEPARATE_OUT

/* The GPIO interrupt is demuxed in system.c, function names are fixed */
//FIXME: Code currently assumes everything is on GPIO0
#define SD_CHANGE_HANDLER  void sdcard_change_handler(void)
#define IEC_ATN_HANDLER    void iec_atn_handler(void)
#define IEC_CLOCK_HANDLER  void iec_clock_handler(void)

static inline void device_hw_address_init(void) {
  // Nothing, pins are input+pullups by default
}

static inline void iec_interrupts_init(void) {
  // Noting - GPIO-Interrupt demux is in system.c
}

/* P00 name cache is in AHB ram */
#define P00CACHE_ATTRIB __attribute__((section(".ahbram")))

#if CONFIG_HARDWARE_VARIANT == 100
/* ---------- Hardware configuration: mbed LPC1768 ---------- */
#  define HW_NAME "SD2IEC"
#  define HAVE_SD
#  define SD_SUPPLY_VOLTAGE (1L<<21)
#  define SPI_ON_SSP 0

#  define SD_DETECT_PIN  25

/* SSP 0, SD0-CS P0.16 */
/* SD1: Detect P0.25, WP P0.26 */
/* SD2: Not defined yet        */
static inline void sdcard_interface_init(void) {
  /* configure SD0-CS as output, high */
  LPC_GPIO0->FIOSET  = BV(16);
  LPC_GPIO0->FIODIR |= BV(16);

  /* Connect SSP0 */
  LPC_PINCON->PINSEL0 |= BV(31);        // SCK
  LPC_PINCON->PINSEL1 |= BV(3) | BV(5); // MISO/MOSI

  /* GPIOs are input-with-pullup by default, so Detect and WP "just work" */

  /* Enable both rising and falling interrupt for change line */
  LPC_GPIOINT->IO0IntEnR |= BV(SD_DETECT_PIN);
  LPC_GPIOINT->IO0IntEnF |= BV(SD_DETECT_PIN);

  /* Note: The GPIO interrupt is enabled in system_init_late */
}

static inline void sdcard_set_ss(int state) {
  if (state)
    LPC_GPIO0->FIOSET = BV(16);
  else
    LPC_GPIO0->FIOCLR = BV(16);
}

static inline uint8_t sdcard_detect(void) {
  return !BITBAND(LPC_GPIO0->FIOPIN, SD_DETECT_PIN);
}

static inline uint8_t sdcard_wp(void) {
  return BITBAND(LPC_GPIO0->FIOPIN, 26);
}

static inline uint8_t device_hw_address(void) {
  return 8 + BITBAND(LPC_GPIO2->FIOPIN, 5) + 2*BITBAND(LPC_GPIO2->FIOPIN, 4);
}

#define HAVE_SD_LED

static inline void leds_init(void) {
  LPC_GPIO1->FIODIR |= BV(18) | BV(20) | BV(21) | BV(23);
  LPC_GPIO1->FIOPIN &= ~(BV(18) | BV(20) | BV(21) | BV(23));
}

static inline __attribute__((always_inline)) void set_busy_led(uint8_t state) {
  if (state)
    BITBAND(LPC_GPIO1->FIOSET, 18) = 1;
  else
    BITBAND(LPC_GPIO1->FIOCLR, 18) = 1;
}

static inline __attribute__((always_inline)) void set_dirty_led(uint8_t state) {
  if (state)
    BITBAND(LPC_GPIO1->FIOSET, 20) = 1;
  else
    BITBAND(LPC_GPIO1->FIOCLR, 20) = 1;
}

static inline __attribute__((always_inline)) void set_test_led(uint8_t state) {
  if (state)
    BITBAND(LPC_GPIO1->FIOSET, 21) = 1;
  else
    BITBAND(LPC_GPIO1->FIOCLR, 21) = 1;
}

static inline __attribute__((always_inline)) void set_sd_led(uint8_t state) {
  if (state)
    BITBAND(LPC_GPIO1->FIOSET, 23) = 1;
  else
    BITBAND(LPC_GPIO1->FIOCLR, 23) = 1;
}

static inline void toggle_dirty_led(void) {
  BITBAND(LPC_GPIO1->FIOPIN, 20) = !BITBAND(LPC_GPIO1->FIOPIN, 20);
}

/* IEC input bits */
#  define IEC_INPUT             LPC_GPIO0->FIOPIN
#  define IEC_INPUTS_INVERTED
#  define IEC_PIN_ATN           23
#  define IEC_PIN_CLOCK         4
#  define IEC_PIN_DATA          5
#  define IEC_PIN_SRQ           24
#  define IEC_TIMER_ATN         LPC_TIM3
#  define IEC_TIMER_CLOCK       LPC_TIM2
#  define IEC_TIMER_DATA        LPC_TIM2
#  define IEC_TIMER_SRQ         LPC_TIM3
#  define IEC_CAPTURE_ATN       0
#  define IEC_CAPTURE_CLOCK     0
#  define IEC_CAPTURE_DATA      1
#  define IEC_CAPTURE_SRQ       1

/* IEC output bits - must be EMR of the same timer used for input */
/* optionally all lines can be on timer 2 (4 match registers) */
#  define IEC_OUTPUTS_INVERTED
#  define IEC_ALL_MATCHES_ON_TIMER2
#  define IEC_OPIN_ATN          2
#  define IEC_OPIN_CLOCK        0
#  define IEC_OPIN_DATA         1
#  define IEC_OPIN_SRQ          3
#  define IEC_MATCH_ATN         MR2
#  define IEC_MATCH_CLOCK       MR0
#  define IEC_MATCH_DATA        MR1
#  define IEC_MATCH_SRQ         MR3
/* name the two IEC timers as A and B (simplifies initialisation), A is the main reference */
/* Note: timer A is also used for timeouts */
#  define IEC_TIMER_A           LPC_TIM2
#  define IEC_TIMER_B           LPC_TIM3
/* LPC_SC->PCONP bits */
#  define IEC_TIMER_A_PCONBIT   22
#  define IEC_TIMER_B_PCONBIT   23
/* PCLKSELx registers */
#  define IEC_TIMER_A_PCLKREG   PCLKSEL1
#  define IEC_TIMER_B_PCLKREG   PCLKSEL1
/* PCLKSELx bits for 1:1 prescaler */
#  define IEC_TIMER_A_PCLKBIT   12
#  define IEC_TIMER_B_PCLKBIT   14

static inline void iec_pins_connect(void) {
  /* Enable all capture and match pins of timer 2 */
  LPC_PINCON->PINSEL0 |= 0b111111111111 << 8;

  /* Enable capture pins of timer 3 */
  LPC_PINCON->PINSEL1 |= 0b1111 << 14;
}

#  define BUTTON_NEXT           BV(3)
#  define BUTTON_PREV           BV(2)

static inline rawbutton_t buttons_read(void) {
  return LPC_GPIO2->FIOPIN & (BUTTON_NEXT | BUTTON_PREV);
}

static inline void buttons_init(void) {
  // None
}

#  define I2C_NUMBER         1
#  define I2C_PCLKDIV        1
#  define I2C_CLOCK          100000
#  define I2C_EEPROM_ADDRESS 0xa0
#  define I2C_EEPROM_SIZE    256

static inline __attribute__((always_inline)) void i2c_pins_connect(void) {
  /* I2C pins open drain */
  LPC_PINCON->PINMODE_OD0 |= BV(0) | BV(1);
  LPC_PINCON->PINSEL0     |= BV(0) | BV(1) | // SDA1
                             BV(2) | BV(3);  // SCL1
}

#  define UART_NUMBER 0

static inline __attribute__((always_inline)) void uart_pins_connect(void) {
  /* use internal debug port on mbed */
  LPC_PINCON->PINSEL0 |= BV(4) | BV(6);
}

#else
#  error "CONFIG_HARDWARE_VARIANT is unset or set to an unknown value."
#endif


/* ---------------- End of user-configurable options ---------------- */

/* Bit number to bit value, used in iec_bus_read() */
#define IEC_BIT_ATN      BV(IEC_PIN_ATN)
#define IEC_BIT_DATA     BV(IEC_PIN_DATA)
#define IEC_BIT_CLOCK    BV(IEC_PIN_CLOCK)
#define IEC_BIT_SRQ      BV(IEC_PIN_SRQ)

/* Return type of iec_bus_read() */
typedef uint32_t iec_bus_t;

/* Shortcut input macros using the CM3 bitband feature */
#ifdef IEC_INPUTS_INVERTED
#  define IEC_IN_COND_INV(x) (!(x))
#else
#  define IEC_IN_COND_INV(x) (x)
#endif

#define IEC_ATN   IEC_IN_COND_INV(BITBAND(IEC_INPUT, IEC_PIN_ATN))
#define IEC_CLOCK IEC_IN_COND_INV(BITBAND(IEC_INPUT, IEC_PIN_CLOCK))
#define IEC_DATA  IEC_IN_COND_INV(BITBAND(IEC_INPUT, IEC_PIN_DATA))
#define IEC_SRQ   IEC_IN_COND_INV(BITBAND(IEC_INPUT, IEC_PIN_SRQ))


/* Generate match timer macros */
#ifdef IEC_ALL_MATCHES_ON_TIMER2
#  define IEC_MTIMER_ATN   LPC_TIM2
#  define IEC_MTIMER_CLOCK LPC_TIM2
#  define IEC_MTIMER_DATA  LPC_TIM2
#  define IEC_MTIMER_SRQ   LPC_TIM2
#else
#  define IEC_MTIMER_ATN   IEC_TIMER_ATN
#  define IEC_MTIMER_CLOCK IEC_TIMER_CLOCK
#  define IEC_MTIMER_DATA  IEC_TIMER_DATA
#  define IEC_MTIMER_SRQ   IEC_TIMER_SRQ
#endif

/* IEC output with bitband access */
#ifdef IEC_OUTPUTS_INVERTED
#  define COND_INV(x) (!(x))
#else
#  define COND_INV(x) (x)
#endif

static inline __attribute__((always_inline)) void set_atn(unsigned int state) {
  if (COND_INV(state))
    BITBAND(IEC_MTIMER_ATN->EMR, IEC_OPIN_ATN) = 1;
  else
    BITBAND(IEC_MTIMER_ATN->EMR, IEC_OPIN_ATN) = 0;
}

static inline __attribute__((always_inline)) void set_clock(unsigned int state) {
  if (COND_INV(state))
    BITBAND(IEC_MTIMER_CLOCK->EMR, IEC_OPIN_CLOCK) = 1;
  else
    BITBAND(IEC_MTIMER_CLOCK->EMR, IEC_OPIN_CLOCK) = 0;
}

static inline __attribute__((always_inline)) void set_data(unsigned int state) {
  if (COND_INV(state))
    BITBAND(IEC_MTIMER_DATA->EMR, IEC_OPIN_DATA) = 1;
  else
    BITBAND(IEC_MTIMER_DATA->EMR, IEC_OPIN_DATA) = 0;
}

static inline __attribute__((always_inline)) void set_srq(unsigned int state) {
  if (COND_INV(state))
    BITBAND(IEC_MTIMER_SRQ->EMR, IEC_OPIN_SRQ) = 1;
  else
    BITBAND(IEC_MTIMER_SRQ->EMR, IEC_OPIN_SRQ) = 0;
}

/* Enable/disable ATN interrupt */
static inline __attribute__((always_inline)) void set_atn_irq(uint8_t state) {
  if (state) {
    BITBAND(LPC_GPIOINT->IO0IntEnR, IEC_PIN_ATN) = 1;
    BITBAND(LPC_GPIOINT->IO0IntEnF, IEC_PIN_ATN) = 1;
  } else {
    BITBAND(LPC_GPIOINT->IO0IntEnR, IEC_PIN_ATN) = 0;
    BITBAND(LPC_GPIOINT->IO0IntEnF, IEC_PIN_ATN) = 0;
  }
}

/* Enable/disable CLOCK interrupt */
static inline __attribute__((always_inline)) void set_clock_irq(uint8_t state) {
  if (state) {
    BITBAND(LPC_GPIOINT->IO0IntEnR, IEC_PIN_CLOCK) = 1;
    BITBAND(LPC_GPIOINT->IO0IntEnF, IEC_PIN_CLOCK) = 1;
  } else {
    BITBAND(LPC_GPIOINT->IO0IntEnR, IEC_PIN_CLOCK) = 0;
    BITBAND(LPC_GPIOINT->IO0IntEnF, IEC_PIN_CLOCK) = 0;
  }
}
#define HAVE_CLOCK_IRQ

#undef COND_INV

/* Display interrupt request line */
// FIXME2: Init function!
static inline void display_intrq_init(void) {
}

// FIXME: Define and add!
static inline unsigned int display_intrq_active(void) {
  return 0;
}

#endif
