/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2012  Ingo Korb <ingo@akana.de>

   Inspired by MMC2IEC by Lars Pontoppidan et al.

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


   fastloader-ll.c: C implementation of low-level fastloader code

*/

#include "config.h"
#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "fastloader.h"
#include "iec-bus.h"
#include "system.h"
#include "timer.h"
#include "fastloader-ll.h"

#define ATNABORT 1
#define NO_ATNABORT 0

#define WAIT 1
#define NO_WAIT 0

#ifdef IEC_OUTPUTS_INVERTED
#  define EMR_LOW  2
#  define EMR_HIGH 1
#else
#  define EMR_LOW  1
#  define EMR_HIGH 2
#endif

typedef struct {
  uint32_t pairtimes[4];
  uint8_t  clockbits[4];
  uint8_t  databits[4];
  uint8_t  eorvalue;
} generic_2bit_t;

static uint32_t reference_time;
static uint32_t timer_a_ccr, timer_b_ccr;

/* ---------- utility functions ---------- */

static void fastloader_setup(void) {
  /*** set up timer A+B to count synchronized 100ns-units ***/

  /* Reset timers */
  BITBAND(IEC_TIMER_A->TCR, 1) = 1;
  BITBAND(IEC_TIMER_B->TCR, 1) = 1;

  /* Move both timers out of reset */
  BITBAND(IEC_TIMER_A->TCR, 1) = 0;
  BITBAND(IEC_TIMER_B->TCR, 1) = 0;

  /* compensate for timer B offset */
  //FIXME: Values are wrong for 10MHz timers
  //LPC_TIM2->PC = 0;
  //LPC_TIM3->PC = 22;

  /* disable IEC interrupts */
  NVIC_DisableIRQ(IEC_TIMER_A_IRQn);
  NVIC_DisableIRQ(IEC_TIMER_B_IRQn);

  /* Clear all capture/match functions except interrupts */
  timer_a_ccr = IEC_TIMER_A->CCR;
  timer_b_ccr = IEC_TIMER_B->CCR;
  IEC_TIMER_A->CCR = 0b100100;
  IEC_TIMER_B->CCR = 0b100100;
  IEC_TIMER_A->MCR = 0b001001001001;
  IEC_TIMER_B->MCR = 0b001001001001;

  /* Enable both timers */
  BITBAND(IEC_TIMER_A->TCR, 0) = 1;
  BITBAND(IEC_TIMER_B->TCR, 0) = 1;
}

static void fastloader_teardown(void) {
  /* Reenable stop-on-match for timer A */
  BITBAND(IEC_TIMER_A->MCR, 2) = 1;

  /* Reset all match conditions */
  IEC_TIMER_A->EMR &= 0b1111;
  IEC_TIMER_B->EMR &= 0b1111;

  /* clear capture/match interrupts */
  IEC_TIMER_A->MCR = 0;
  IEC_TIMER_B->MCR = 0;
  IEC_TIMER_A->CCR = timer_a_ccr;
  IEC_TIMER_B->CCR = timer_b_ccr;
  IEC_TIMER_A->IR  = 0b111111;
  IEC_TIMER_B->IR  = 0b111111;

  /* reenable IEC interrupts */
  NVIC_EnableIRQ(IEC_TIMER_A_IRQn);
  NVIC_EnableIRQ(IEC_TIMER_B_IRQn);
}

/**
 * wait_atn - wait until ATN has the specified level and capture time
 * @state: line level to wait for (0 low, 1 high)
 *
 * This function waits until the ATN line has the specified level and captures
 * the time when its level changed.
 */
static __attribute__((unused)) void wait_atn(unsigned int state) {
  /* set up capture */
  BITBAND(IEC_TIMER_ATN->CCR, 3*IEC_CAPTURE_ATN + IEC_IN_COND_INV(!state)) = 1;

  /* clear interrupt flag */
  IEC_TIMER_ATN->IR = BV(4 + IEC_CAPTURE_ATN);

  /* wait until interrupt flag is set */
  while (!BITBAND(IEC_TIMER_ATN->IR, 4+IEC_CAPTURE_ATN)) ;

  /* read event time */
  if (IEC_CAPTURE_ATN == 0) {
    reference_time = IEC_TIMER_ATN->CR0;
  } else {
    reference_time = IEC_TIMER_ATN->CR1;
  }

  /* reset capture mode */
  IEC_TIMER_ATN->CCR = 0b100100;
}

/* wait_clock - see wait_atn, aborts on ATN low if atnabort is true */
static __attribute__((unused)) void wait_clock(unsigned int state, unsigned int atnabort) {
  /* set up capture */
  BITBAND(IEC_TIMER_CLOCK->CCR, 3*IEC_CAPTURE_CLOCK + IEC_IN_COND_INV(!state)) = 1;

  /* clear interrupt flag */
  IEC_TIMER_CLOCK->IR = BV(4 + IEC_CAPTURE_CLOCK);

  /* wait until interrupt flag is set */
  while (!BITBAND(IEC_TIMER_CLOCK->IR, 4+IEC_CAPTURE_CLOCK))
    if (atnabort && !IEC_ATN)
      break;

  if (atnabort && !IEC_ATN) {
    /* read current time */
    reference_time = IEC_TIMER_CLOCK->TC;
  } else {
    /* read event time */
    if (IEC_CAPTURE_CLOCK == 0) {
      reference_time = IEC_TIMER_CLOCK->CR0;
    } else {
      reference_time = IEC_TIMER_CLOCK->CR1;
    }
  }

  /* reset capture mode */
  IEC_TIMER_CLOCK->CCR = 0b100100;
}

/* wait_data - see wait_atn */
static __attribute__((unused)) void wait_data(unsigned int state, unsigned int atnabort) {
  /* set up capture */
  BITBAND(IEC_TIMER_DATA->CCR, 3*IEC_CAPTURE_DATA + IEC_IN_COND_INV(!state)) = 1;

  /* clear interrupt flag */
  IEC_TIMER_DATA->IR = BV(4 + IEC_CAPTURE_DATA);

  /* wait until interrupt flag is set */
  while (!BITBAND(IEC_TIMER_DATA->IR, 4+IEC_CAPTURE_DATA))
    if (atnabort && !IEC_ATN)
      break;

  if (atnabort && !IEC_ATN) {
    /* read current time */
    reference_time = IEC_TIMER_DATA->TC;
  } else {
    /* read event time */
    if (IEC_CAPTURE_DATA == 0) {
      reference_time = IEC_TIMER_DATA->CR0;
    } else {
      reference_time = IEC_TIMER_DATA->CR1;
    }
  }

  /* reset capture mode */
  IEC_TIMER_CLOCK->CCR = 0b100100;
}

/**
 * set_clock_at - sets clock line at a specified time offset
 * @time : change time in 100ns after reference_time
 * @state: new line state (0 low, 1 high)
 * @wait : wait until change happened if 1
 *
 * This function sets the clock line to a specified state at a defined time
 * after the reference_time set by a previous wait_* function.
 */
static __attribute__((unused)) void set_clock_at(uint32_t time, unsigned int state, unsigned int wait) {
  /* check if requested time is possible */
  // FIXME: Wrap in debugging macro?
  if (IEC_MTIMER_CLOCK->TC > reference_time+time)
    set_test_led(1);

  /* set match time */
  IEC_MTIMER_CLOCK->IEC_MATCH_CLOCK = reference_time + time;

  /* reset match interrupt flag */
  IEC_MTIMER_CLOCK->IR = BV(IEC_OPIN_CLOCK);

  /* set match action */
  if (state) {
    IEC_MTIMER_CLOCK->EMR = (IEC_MTIMER_CLOCK->EMR & ~(3 << (4+IEC_OPIN_CLOCK*2))) |
                             EMR_HIGH << (4+IEC_OPIN_CLOCK*2);
  } else {
    IEC_MTIMER_CLOCK->EMR = (IEC_MTIMER_CLOCK->EMR & ~(3 << (4+IEC_OPIN_CLOCK*2))) |
                             EMR_LOW << (4+IEC_OPIN_CLOCK*2);
  }

  /* optional: wait for match */
  if (wait)
    while (!BITBAND(IEC_MTIMER_CLOCK->IR, IEC_OPIN_CLOCK)) ;
}

/* set_data_at - see set_clock_at */
static __attribute__((unused)) void set_data_at(uint32_t time, unsigned int state, unsigned int wait) {
  /* check if requested time is possible */
  // FIXME: Wrap in debugging macro?
  if (IEC_MTIMER_DATA->TC > reference_time+time)
    set_test_led(1);

  /* set match time */
  IEC_MTIMER_DATA->IEC_MATCH_DATA = reference_time + time;

  /* reset match interrupt flag */
  IEC_MTIMER_DATA->IR = BV(IEC_OPIN_DATA);

  /* set match action */
  if (state) {
    IEC_MTIMER_DATA->EMR = (IEC_MTIMER_DATA->EMR & ~(3 << (4+IEC_OPIN_DATA*2))) |
                            EMR_HIGH << (4+IEC_OPIN_DATA*2);
  } else {
    IEC_MTIMER_DATA->EMR = (IEC_MTIMER_DATA->EMR & ~(3 << (4+IEC_OPIN_DATA*2))) |
                            EMR_LOW << (4+IEC_OPIN_DATA*2);
  }

  /* optional: wait for match */
  if (wait)
    while (!BITBAND(IEC_MTIMER_DATA->IR, IEC_OPIN_DATA)) ;
}

/* set_srq_at - see set_clock_at (nice for debugging) */
static __attribute__((unused)) void set_srq_at(uint32_t time, unsigned int state, unsigned int wait) {
  /* check if requested time is possible */
  // FIXME: Wrap in debugging macro?
  if (IEC_MTIMER_SRQ->TC > reference_time+time)
    set_test_led(1);

  /* set match time */
  IEC_MTIMER_SRQ->IEC_MATCH_SRQ = reference_time + time;

  /* reset match interrupt flag */
  IEC_MTIMER_SRQ->IR = BV(IEC_OPIN_SRQ);

  /* set match action */
  if (state) {
    IEC_MTIMER_SRQ->EMR = (IEC_MTIMER_SRQ->EMR & ~(3 << (4+IEC_OPIN_SRQ*2))) |
                           EMR_HIGH << (4+IEC_OPIN_SRQ*2);
  } else {
    IEC_MTIMER_SRQ->EMR = (IEC_MTIMER_SRQ->EMR & ~(3 << (4+IEC_OPIN_SRQ*2))) |
                           EMR_LOW << (4+IEC_OPIN_SRQ*2);
  }

  /* optional: wait for match */
  if (wait)
    while (!BITBAND(IEC_MTIMER_SRQ->IR, IEC_OPIN_SRQ)) ;
}

/**
 * read_bus_at - reads the IEC bus at a certain time
 * @time: read time in 100ns after reference_time
 *
 * This function returns the current IEC bus state at a certain time
 * after the reference_time set by a previous wait_* function.
 */
static __attribute__((unused)) uint32_t read_bus_at(uint32_t time) {
  /* check if requested time is possible */
  // FIXME: Wrap in debugging macro?
  if (IEC_TIMER_A->TC >= reference_time+time)
    set_test_led(1);

  // FIXME: This could be done in hardware using DMA
  /* Wait until specified time */
  while (IEC_TIMER_A->TC < reference_time+time) ;

  return iec_bus_read();
}

/**
 * now - returns current timer count
 *
 * This function returns the current timer value
 */
static __attribute__((unused)) uint32_t now(void) {
  return IEC_TIMER_A->TC;
}

/**
 * generic_load_2bit - generic 2-bit fastloader transmit
 * @def : pointer to fastloader definition struct
 * @byte: data byte
 *
 * This function implements generic 2-bit fastloader
 * transmission based on a generic_2bit_t struct.
 */
static __attribute__((unused)) void generic_load_2bit(const generic_2bit_t *def, uint8_t byte) {
  unsigned int i;

  byte ^= def->eorvalue;

  for (i=0;i<4;i++) {
    set_clock_at(def->pairtimes[i], byte & (1 << def->clockbits[i]), NO_WAIT);
    set_data_at (def->pairtimes[i], byte & (1 << def->databits[i]),  WAIT);
  }
}

/**
 * generic_save_2bit - generic 2-bit fastsaver receive
 * @def: pointer to fastloader definition struct
 *
 * This function implements genereic 2-bit fastsaver reception
 * based on a generic_2bit_t struct.
 */
static __attribute__((unused)) uint8_t generic_save_2bit(const generic_2bit_t *def) {
  unsigned int i;
  uint8_t result = 0;

  for (i=0;i<4;i++) {
    uint32_t bus = read_bus_at(def->pairtimes[i]);

    result |= (!!(bus & IEC_BIT_CLOCK)) << def->clockbits[i];
    result |= (!!(bus & IEC_BIT_DATA))  << def->databits[i];
  }

  return result ^ def->eorvalue;
}

/* --------- low level fastloader implementations ---------- */

/* ---------------- */
/* --- JiffyDos --- */
/* ---------------- */

static const generic_2bit_t jiffy_receive_def = {
  .pairtimes = {170, 300, 410, 540},
  .clockbits = {4, 6, 3, 2},
  .databits  = {5, 7, 1, 0},
  .eorvalue  = 0xff
};

static const generic_2bit_t jiffy_send_def = {
  .pairtimes = {100, 200, 310, 410},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0
};

uint8_t jiffy_receive(iec_bus_t *busstate) {
  uint8_t result;

  fastloader_setup();
  disable_interrupts();

  /* Initial handshake - wait for rising clock, but emulate ATN-ACK */
  set_clock(1);
  set_data(1);
  do {
    wait_clock(1, ATNABORT);
    if (!IEC_ATN)
      set_data(0);
  } while (!IEC_CLOCK);

  /* receive byte */
  result = generic_save_2bit(&jiffy_receive_def);

  /* read EOI info */
  *busstate = read_bus_at(670);

  /* exit with data low */
  set_data_at(730, 0, WAIT);
  delay_us(10);

  enable_interrupts();
  fastloader_teardown();
  return result;
}

uint8_t jiffy_send(uint8_t value, uint8_t eoi, uint8_t loadflags) {
  unsigned int loadmode = loadflags & 0x80;
  unsigned int skipeoi  = loadflags & 0x7f;

  fastloader_setup();
  disable_interrupts();

  /* Initial handshake */
  set_data(1);
  set_clock(1);
  delay_us(3);

  if (loadmode) {
    /* LOAD mode: start marker is data low */
    while (!IEC_DATA) ; // wait until data actually is high again
    wait_data(0, ATNABORT);
  } else {
    /* single byte mode: start marker is data high */
    wait_data(1, ATNABORT);
  }

  /* transmit data */
  generic_load_2bit(&jiffy_send_def, value);

  /* Send EOI info */
  if (!skipeoi) {
    if (eoi) {
      set_clock_at(520, 1, NO_WAIT);
      set_data_at (520, 0, WAIT);
    } else {
      /* LOAD mode also uses this for the final byte of a block */
      set_clock_at(520, 0, NO_WAIT);
      set_data_at (520, 1, WAIT);
    }

    /* wait until data is low */
    delay_us(3); // allow for slow rise time
    while (IEC_DATA && IEC_ATN) ;
  }

  /* hold time */
  delay_us(10);

  enable_interrupts();
  fastloader_teardown();
  return !IEC_ATN;
}

/* ----------------- */
/* --- Turbodisk --- */
/* ----------------- */

static const generic_2bit_t turbodisk_byte_def = {
  .pairtimes = {310, 600, 890, 1180},
  .clockbits = {7, 5, 3, 1},
  .databits  = {6, 4, 2, 0},
  .eorvalue  = 0
};

void turbodisk_byte(uint8_t value) {
  fastloader_setup();

  /* wait for handshake */
  while (IEC_DATA) ;
  set_clock(1);
  wait_data(1, NO_ATNABORT);

  /* transmit data */
  generic_load_2bit(&turbodisk_byte_def, value);

  /* exit with clock low, data high */
  set_clock_at(1470, 0, NO_WAIT);
  set_data_at( 1470, 1, WAIT);
  delay_us(5);

  fastloader_teardown();
}

void turbodisk_buffer(uint8_t *data, uint8_t length) {
  unsigned int pair;
  uint32_t ticks;

  fastloader_setup();

  /* wait for handshake */
  while (IEC_DATA) ;
  set_clock(1);
  wait_data(1, NO_ATNABORT);

  ticks = 70;
  while (length--) {
    uint8_t byte = *data++;

    ticks += 120;
    for (pair = 0; pair < 4; pair++) {
      ticks += 240;
      set_clock_at(ticks, byte & 0x80, NO_WAIT);
      set_data_at (ticks, byte & 0x40, WAIT);
      ticks += 50;
      byte <<= 2;
    }
    ticks += 100;
  }

  ticks += 110;

  set_clock_at(ticks, 0, NO_WAIT);
  set_data_at( ticks, 1, WAIT);
  delay_us(5);

  fastloader_teardown();
}

/* ------------------------- */
/* --- Final Cartridge 3 --- */
/* ------------------------- */

/* Used by the FC3 C part */
void clk_data_handshake(void) {
  set_clock(0);
  while (IEC_DATA && IEC_ATN) ;

  if (!IEC_ATN)
    return;

  set_clock(1);
  while (!IEC_DATA && IEC_ATN) ;
}

void fastloader_fc3_send_block(uint8_t *data) {
  uint32_t ticks;
  unsigned int byte, pair;

  fastloader_setup();
  disable_interrupts();

  /* start in one microsecond */
  reference_time = now() + 10;
  set_clock_at(0, 0, WAIT);

  /* Transmit data */
  ticks = 120;
  for (byte = 0; byte < 4; byte++) {
    uint8_t value = *data++;

    for (pair = 0; pair < 4; pair++) {
      set_clock_at(ticks, value & 1, NO_WAIT);
      set_data_at (ticks, value & 2, WAIT);
      value >>= 2;
      ticks += 120;
    }
    ticks += 20;
  }

  set_clock_at(ticks, 1, NO_WAIT);
  set_data_at (ticks, 1, WAIT);
  // Note: Hold time is in the C part

  enable_interrupts();
  fastloader_teardown();
}

static const generic_2bit_t fc3_get_def = {
  .pairtimes = {170, 300, 420, 520},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

uint8_t fc3_get_byte(void) {
  uint8_t result;

  fastloader_setup();
  disable_interrupts();

  /* delay (guessed, see AVR version) */
  delay_us(10);

  /* wait for handshake */
  set_data(1);
  wait_clock(1, NO_ATNABORT);

  /* receive data */
  result = generic_save_2bit(&fc3_get_def);

  /* exit with data low */
  set_data(0);

  enable_interrupts();
  fastloader_teardown();
  return result;
}


/* ---------------------------- */
/* --- Action Replay 6 1581 --- */
/* ---------------------------- */

static const generic_2bit_t ar6_1581_send_def = {
  .pairtimes = {50, 130, 210, 290},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0
};

static const generic_2bit_t ar6_1581p_get_def = {
  .pairtimes = {120, 220, 380, 480},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

void ar6_1581_send_byte(uint8_t byte) {
  fastloader_setup();
  disable_interrupts();

  /* wait for handshake */
  set_clock(1);
  wait_data(1, NO_ATNABORT);

  /* transmit data */
  generic_load_2bit(&ar6_1581_send_def, byte);

  /* exit with clock low, data high */
  set_clock_at(375, 0, NO_WAIT);
  set_data_at( 375, 1, WAIT);

  /* short delay to make sure bus has settled */
  delay_us(10);

  enable_interrupts();
  fastloader_teardown();
}

uint8_t ar6_1581p_get_byte(void) {
  uint8_t result;

  fastloader_setup();
  disable_interrupts();

  set_clock(1);

  /* wait for handshake */
  while (IEC_DATA) ;
  wait_data(1, NO_ATNABORT);

  /* receive data */
  result = generic_save_2bit(&ar6_1581p_get_def);

  /* exit with clock low */
  set_clock_at(530, 0, WAIT);

  enable_interrupts();
  fastloader_teardown();
  return result;
}


/* ----------------- */
/* --- Dreamload --- */
/* ----------------- */

#ifdef CONFIG_LOADER_DREAMLOAD

void dreamload_send_byte(uint8_t byte) {
  unsigned int i;

  for (i=0; i<2; i++) {
    /* send bits 0,1 to bus */
    set_clock(byte & 1);
    set_data (byte & 2);

    /* wait until ATN is low */
    while (IEC_ATN) ;

    /* send bits 2,3 to bus */
    set_clock(byte & 4);
    set_data (byte & 8);

    /* wait until ATN is high */
    while (!IEC_ATN) ;

    /* move upper nibble down */
    byte >>= 4;
  }
}

uint8_t dreamload_get_byte(void) {
  unsigned int i;
  uint8_t result = 0;

  for (i=0;i<4;i++) {
    /* wait until clock is low */
    while (IEC_CLOCK) ;

    /* read data bit a moment later */
    delay_us(3);
    result = (result << 1) | !IEC_DATA;

    /* wait until clock is high */
    while (!IEC_CLOCK) ;

    /* read data bit a moment later */
    delay_us(3);
    result = (result << 1) | !IEC_DATA;
  }

  return result;
}

static uint8_t dreamload_get_byte_old(void) {
  unsigned int i;
  iec_bus_t tmp;
  uint8_t result = 0;

  for (i=0; i<2; i++) {
    /* shift bits to upper nibble */
    result <<= 4;

    /* wait until ATN is low */
    while (IEC_ATN) ;

    /* read two bits a moment later */
    delay_us(3);
    tmp = iec_bus_read();
    result |= (!(tmp & IEC_BIT_CLOCK)) << 3;
    result |= (!(tmp & IEC_BIT_DATA))  << 1;

    /* wait until ATN is high */
    while (!IEC_ATN) ;

    /* read two bits a moment later */
    delay_us(3);
    tmp = iec_bus_read();
    result |= (!(tmp & IEC_BIT_CLOCK)) << 2;
    result |= (!(tmp & IEC_BIT_DATA))  << 0;
  }

  return result;
}

IEC_ATN_HANDLER {
  /* just return if ATN is high */
  if (IEC_ATN)
    return;

  if (detected_loader == FL_DREAMLOAD_OLD) {
    /* handle Dreamload (old) if it's the current fast loader */
    fl_track  = dreamload_get_byte_old();
    fl_sector = dreamload_get_byte_old();
  } else {
    /* standard ATN acknowledge */
    set_data(0);
  }
}

IEC_CLOCK_HANDLER {
  if (detected_loader == FL_DREAMLOAD && !IEC_CLOCK) {
    /* handle Dreamload if it's the current fast loader and clock is low */
    fl_track  = dreamload_get_byte();
    fl_sector = dreamload_get_byte();
  }
}

#endif

/* --------------------- */
/* --- ULoad Model 3 --- */
/* --------------------- */
static const generic_2bit_t uload3_get_def = {
  .pairtimes = {140, 240, 380, 480},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

static const generic_2bit_t uload3_send_def = {
  .pairtimes = {140, 220, 300, 380},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0
};

int16_t uload3_get_byte(void) {
  uint8_t result;

  /* initial handshake */
  set_clock(0);
  while (IEC_DATA && IEC_ATN) ;
  if (!IEC_ATN)
    return -1;

  fastloader_setup();
  disable_interrupts();

  /* wait for start signal */
  set_clock(1);
  wait_data(1, NO_ATNABORT);

  /* receive data */
  result = generic_save_2bit(&uload3_get_def);

  /* wait until the C64 releases the bus */
  delay_us(20);

  enable_interrupts();
  fastloader_teardown();
  return result;
}

void uload3_send_byte(uint8_t byte) {
  fastloader_setup();
  disable_interrupts();

  /* initial handshake */
  set_data(0);
  while (IEC_CLOCK && IEC_ATN) ;
  if (!IEC_ATN)
    goto exit;

  /* wait for start signal */
  set_data(1);
  wait_clock(1, ATNABORT);
  if (!IEC_ATN)
    goto exit;

  /* transmit data */
  generic_load_2bit(&uload3_send_def, byte);

  /* exit with clock+data high */
  set_clock_at(480, 1, NO_WAIT);
  set_data_at (480, 1, WAIT);

 exit:
  enable_interrupts();
  fastloader_teardown();
}


/* --------------------- */
/* --- Epyx FastLoad --- */
/* --------------------- */

static const generic_2bit_t epyxcart_send_def = {
  .pairtimes = {100, 200, 300, 400},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

uint8_t epyxcart_send_byte(uint8_t byte) {
  uint8_t result = 0;

  fastloader_setup();
  disable_interrupts();

  /* clear bus */
  set_data(1);
  set_clock(1);
  delay_us(3);

  /* wait for start signal */
  wait_data(1, ATNABORT);
  if (!IEC_ATN) {
    result = 1;
    goto exit;
  }

  /* transmit data */
  generic_load_2bit(&epyxcart_send_def, byte);

  /* data hold time */
  delay_us(20);

 exit:
  disable_interrupts();
  fastloader_teardown();
  return result;
}


/* ------------ */
/* --- GEOS --- */
/* ------------ */

/* --- reception --- */
static const generic_2bit_t geos_1mhz_get_def = {
  .pairtimes = {150, 290, 430, 590},
  .clockbits = {4, 6, 3, 2},
  .databits  = {5, 7, 1, 0},
  .eorvalue  = 0xff
};

static const generic_2bit_t geos_2mhz_get_def = {
  .pairtimes = {150, 290, 395, 505},
  .clockbits = {4, 6, 3, 2},
  .databits  = {5, 7, 1, 0},
  .eorvalue  = 0xff
};

static uint8_t geos_get_generic(const generic_2bit_t *timingdef, uint8_t holdtime_us) {
  uint8_t result;

  fastloader_setup();

  /* initial handshake */
  /* wait_* expects edges, so waiting for clock high isn't needed */
  wait_clock(0, NO_ATNABORT);

  /* receive data */
  result = generic_save_2bit(timingdef);
  delay_us(holdtime_us);

  fastloader_teardown();
  return result;
}

uint8_t geos_get_byte_1mhz(void) {
  return geos_get_generic(&geos_1mhz_get_def, 12);
}

uint8_t geos_get_byte_2mhz(void) {
  return geos_get_generic(&geos_2mhz_get_def, 12);
}

/* --- transmission --- */
static const generic_2bit_t geos_1mhz_send_def = {
  .pairtimes = {180, 280, 390, 510},
  .clockbits = {3, 2, 4, 6},
  .databits  = {1, 0, 5, 7},
  .eorvalue  = 0x0f
};

static const generic_2bit_t geos_2mhz_send_def = {
  .pairtimes = {90, 200, 320, 440},
  .clockbits = {3, 2, 4, 6},
  .databits  = {1, 0, 5, 7},
  .eorvalue  = 0x0f
};

static const generic_2bit_t geos_1581_21_send_def = {
  .pairtimes = {70, 140, 240, 330},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0
};

static void geos_send_generic(uint8_t byte, const generic_2bit_t *timingdef, unsigned int holdtime_us) {
  fastloader_setup();

  /* initial handshake */
  set_clock(1);
  set_data(1);
  wait_clock(0, NO_ATNABORT);

  /* send data */
  generic_load_2bit(timingdef, byte);

  /* hold time */
  delay_us(holdtime_us);

  fastloader_teardown();
}

void geos_send_byte_1mhz(uint8_t byte) {
  geos_send_generic(byte, &geos_1mhz_send_def, 19);
}

void geos_send_byte_2mhz(uint8_t byte) {
  geos_send_generic(byte, &geos_2mhz_send_def, 22);
}

void geos_send_byte_1581_21(uint8_t byte) {
  geos_send_generic(byte, &geos_1581_21_send_def, 12);
}


/* -------------- */
/* --- Wheels --- */
/* -------------- */

/* --- reception --- */
static const generic_2bit_t wheels_1mhz_get_def = {
  .pairtimes = {160, 260, 410, 540},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

static const generic_2bit_t wheels44_1mhz_get_def = {
  .pairtimes = {170, 280, 450, 610},
  .clockbits = {7, 6, 3, 2},
  .databits  = {5, 4, 1, 0},
  .eorvalue  = 0xff
};

static const generic_2bit_t wheels44_2mhz_get_def = {
  .pairtimes = {150, 260, 370, 480},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0xff
};

uint8_t wheels_get_byte_1mhz(void) {
  return geos_get_generic(&wheels_1mhz_get_def, 20);
}

uint8_t wheels44_get_byte_1mhz(void) {
  return geos_get_generic(&wheels44_1mhz_get_def, 20);
}

uint8_t wheels44_get_byte_2mhz(void) {
  return geos_get_generic(&wheels44_2mhz_get_def, 12);
}

/* --- transmission --- */
static const generic_2bit_t wheels_1mhz_send_def = {
  .pairtimes = {90, 230, 370, 510},
  .clockbits = {3, 2, 7, 6},
  .databits  = {1, 0, 5, 4},
  .eorvalue  = 0xff
};

static const generic_2bit_t wheels44_2mhz_send_def = {
  .pairtimes = {70, 150, 260, 370},
  .clockbits = {0, 2, 4, 6},
  .databits  = {1, 3, 5, 7},
  .eorvalue  = 0
};

void wheels_send_byte_1mhz(uint8_t byte) {
  geos_send_generic(byte, &wheels_1mhz_send_def, 22);
}

void wheels44_send_byte_2mhz(uint8_t byte) {
  geos_send_generic(byte, &wheels44_2mhz_send_def, 15);
}


/* ---------------- */
/* --- Parallel --- */
/* ---------------- */

uint8_t parallel_read(void) {
  return (PARALLEL_PGPIO->FIOPIN >> PARALLEL_PSTARTBIT) & 0xff;
}

void parallel_write(uint8_t value) {
  PARALLEL_PGPIO->FIOPIN =
    (PARALLEL_PGPIO->FIOPIN & ~(0xff << PARALLEL_PSTARTBIT)) |
    (value << PARALLEL_PSTARTBIT);
  delay_us(1);
}

void parallel_set_dir(parallel_dir_t direction) {
  if (direction == PARALLEL_DIR_IN) {
    /* set all lines high - FIODIR is not used in open drain mode */
    PARALLEL_PGPIO->FIOSET |= 0xff << PARALLEL_PSTARTBIT;
  }
}

void parallel_send_handshake(void) {
  PARALLEL_HGPIO->FIOCLR = BV(PARALLEL_HSK_OUT_BIT);
  delay_us(2);
  PARALLEL_HGPIO->FIOSET = BV(PARALLEL_HSK_OUT_BIT);
}
