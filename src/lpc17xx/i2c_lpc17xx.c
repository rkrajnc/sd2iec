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


   i2c_lpc17xx.c: Hardware I2C bus master for LPC17xx

   Note: Doesn't use repeated start when reading,
         so it's broken on multi-master busses
*/

#include <arm/NXP/LPC17xx/LPC17xx.h>
#include <arm/bits.h>
#include "config.h"
#include "i2c.h"

#include "uart.h"
#include "timer.h"

#if I2C_NUMBER == 0
#  define I2C_REGS    LPC_I2C0
#  define I2C_PCLKREG PCLKSEL0
#  define I2C_PCLKBIT 14
#  define I2C_HANDLER I2C0_IRQHandler
#  define I2C_IRQ     I2C0_IRQn
#elif I2C_NUMBER == 1
#  define I2C_REGS LPC_I2C1
#  define I2C_PCLKREG PCLKSEL1
#  define I2C_PCLKBIT 6
#  define I2C_HANDLER I2C1_IRQHandler
#  define I2C_IRQ     I2C1_IRQn
#elif I2C_NUMBER == 2
#  define I2C_REGS LPC_I2C2
#  define I2C_PCLKREG PCLKSEL1
#  define I2C_PCLKBIT 20
#  define I2C_HANDLER I2C2_IRQHandler
#  define I2C_IRQ     I2C2_IRQn
#else
#  error I2C_NUMBER is not set or has an invalid value!
#endif

#define I2CEN  6
#define I2CSTA 5
#define I2CSTO 4
#define I2CSI  3
#define I2CAA  2

#define RESULT_NONE      0
#define RESULT_ADDR_NACK 1
#define RESULT_DATA_NACK 2
#define RESULT_BUSERROR  3
#define RESULT_DONE      4

static unsigned char address, i2creg, count, read_mode;
static volatile unsigned char *bufferptr;
static volatile char result;

void I2C_HANDLER(void) {
  unsigned int tmp = I2C_REGS->I2STAT;

  switch (tmp) { //I2C_REGS->I2STAT) {
  case 0x08: // START transmitted
  case 0x10: // repeated START transmitted
    /* send address */
    I2C_REGS->I2DAT = address;
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x18: // SLA+W transmitted, ACK received
    /* send register */
    I2C_REGS->I2DAT = i2creg;
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x20: // SLA+W transmitted, no ACK
  case 0x30: // byte transmitted, no ACK
  case 0x48: // SLA+R transmitted, no ACK
    /* send stop */
    result = RESULT_ADDR_NACK;
    I2C_REGS->I2CONSET = BV(I2CSTO);
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x28: // byte transmitted, ACK received
    /* send next data byte or stop */
    if (read_mode) {
      /* switch to master receiver mode */
      address |= 1;
      I2C_REGS->I2CONSET = BV(I2CSTA);
      I2C_REGS->I2CONCLR = BV(I2CSI);
    } else
      if (count) {
        I2C_REGS->I2DAT = *bufferptr++;
        count--;
        I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
      } else {
        I2C_REGS->I2CONSET = BV(I2CSTO);
        I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
        result = RESULT_DONE;
      }
    break;

  case 0x38: // arbitration lost
    /* try to send start again (assumes arbitration was not lost in data transmission */
    I2C_REGS->I2CONSET = BV(I2CSTA);
    I2C_REGS->I2CONCLR = BV(I2CSTO) | BV(I2CSI);
    break;

  case 0x40: // SLA+R transmitted, ACK received
    /* prepare read cycle */
    if (--count)
      I2C_REGS->I2CONSET = BV(I2CAA);
    else
      I2C_REGS->I2CONCLR = BV(I2CAA);

    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x50: // data byte received, ACK sent
    /* decide to ACK/NACK next cycle, read current byte */
    if (--count)
      I2C_REGS->I2CONSET = BV(I2CAA);
    else
      I2C_REGS->I2CONCLR = BV(I2CAA);

    *bufferptr++ = I2C_REGS->I2DAT;
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    break;

  case 0x58: // data byte received, no ACK sent
    /* read last byte, send stop */
    *bufferptr++ = I2C_REGS->I2DAT;
    I2C_REGS->I2CONSET = BV(I2CSTO);
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    result = RESULT_DONE;
    break;

  case 0x00: // bus error
    I2C_REGS->I2CONSET = BV(I2CSTO);
    I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI);
    result = RESULT_BUSERROR;
    break;

  default:
    //printf("i2c:%02x\n",tmp);
    break;
  }
}

void i2c_init(void) {
  /* Set up I2C clock prescaler */
  if (I2C_PCLKDIV == 1) {
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT  ) = 1;
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT+1) = 0;
  } else if (I2C_PCLKDIV == 2) {
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT  ) = 0;
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT+1) = 1;
  } else if (I2C_PCLKDIV == 4) {
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT  ) = 0;
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT+1) = 0;
  } else { // Fallback: Divide by 8
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT  ) = 1;
    BITBAND(LPC_SC->I2C_PCLKREG, I2C_PCLKBIT+1) = 1;
  }

  /* Set I2C clock (symmetric) */
  I2C_REGS->I2SCLH = CONFIG_MCU_FREQ / I2C_CLOCK / I2C_PCLKDIV / 2;
  I2C_REGS->I2SCLL = CONFIG_MCU_FREQ / I2C_CLOCK / I2C_PCLKDIV / 2;

  /* Enable I2C interrupt */
  NVIC_EnableIRQ(I2C_IRQ);

  /* Enable I2C */
  BITBAND(I2C_REGS->I2CONSET, I2CEN) = 1;
  I2C_REGS->I2CONCLR = BV(I2CSTA) | BV(I2CSI) | BV(I2CAA);

  /* connect to I/O pins */
  i2c_pins_connect();
}

uint8_t i2c_write_registers(uint8_t address_, uint8_t startreg, uint8_t count_, const void *data) {
  result    = RESULT_NONE;
  address   = address_;
  i2creg    = startreg;
  count     = count_;
  bufferptr = (void *)data;
  read_mode = 0;

  /* send start condition */
  BITBAND(I2C_REGS->I2CONSET, I2CSTA) = 1;

  /* wait until ISR is done */
  while (result == RESULT_NONE)
    __WFI();

  return (result != RESULT_DONE);
}

uint8_t i2c_write_register(uint8_t address, uint8_t reg, uint8_t val) {
  return i2c_write_registers(address, reg, 1, &val);
}

uint8_t i2c_read_registers(uint8_t address_, uint8_t startreg, uint8_t count_, void *data) {
  result    = RESULT_NONE;
  address   = address_ & 0xfe;
  i2creg    = startreg;
  count     = count_;
  bufferptr = data;
  read_mode = 1;

  /* send start condition */
  BITBAND(I2C_REGS->I2CONSET, I2CSTA) = 1;

  /* wait until ISR is done */
  while (result == RESULT_NONE)
    __WFI();

  /* tell gcc that the contents of data have changed */
  asm volatile ("" : "=m" (*(char *)data));

  return (result != RESULT_DONE);
}

int16_t i2c_read_register(uint8_t address, uint8_t reg) {
  uint8_t tmp = 0;

  if (i2c_read_registers(address, reg, 1, &tmp))
    return -1;
  else
    return tmp;
}

