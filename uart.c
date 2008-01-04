/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>

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

   
   uart.h: UART access routines

*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include "config.h"
#include "avrcompat.h"

#ifdef UART_DEBUG

static char txbuf[UART_BUFFER_SIZE];
static volatile uint16_t read_idx;
static volatile uint16_t write_idx;

ISR(USART_UDRE_vect) {
  if (read_idx == write_idx) return;
  UDR = txbuf[read_idx];
  read_idx = (read_idx+1) & (sizeof(txbuf)-1);
  if (read_idx == write_idx)
    UCSRB &= ~ _BV(UDRIE);
}

void uart_putc(char c) {
  UCSRB &= ~ _BV(UDRIE);
  txbuf[write_idx] = c;
  write_idx = (write_idx+1) & (sizeof(txbuf)-1);
  //if (read_idx == write_idx) PORTD |= _BV(PD7);
  UCSRB |= _BV(UDRIE);
}

void uart_puthex(uint8_t num) {
  uint8_t tmp;
  tmp = (num & 0xf0) >> 4;
  if (tmp < 10)
    uart_putc('0'+tmp);
  else
    uart_putc('a'+tmp-10);

  tmp = num & 0x0f;
  if (tmp < 10)
    uart_putc('0'+tmp);
  else
    uart_putc('a'+tmp-10);
}

static int ioputc(char c, FILE *stream) {
  if (c == '\n') uart_putc('\r');
  uart_putc(c);
  return 0;
}

unsigned char uart_getc(void) {
  loop_until_bit_is_set(UCSRA,RXC);
  return UDR;
}

void uart_flush(void) {
  while (read_idx != write_idx) ;
}

void uart_puts_P(prog_char *text) {
  uint8_t ch;
  
  while ((ch = pgm_read_byte(text++))) {
    uart_putc(ch);
  }
}

void uart_putcrlf(void) {
  uart_putc(13);
  uart_putc(10);
}

static FILE mystdout = FDEV_SETUP_STREAM(ioputc, NULL, _FDEV_SETUP_WRITE);

void init_serial(void) {
  /* Seriellen Port konfigurieren */

  UBRRH = (int)((double)F_CPU/(16.0*UART_BAUDRATE)-1) >> 8;
  UBRRL = (int)((double)F_CPU/(16.0*UART_BAUDRATE)-1) & 0xff;

  UCSRB = _BV(RXEN) | _BV(TXEN);
  // I really don't like random #ifdefs in the code =(
#ifdef __AVR_ATmega644__
  UCSRC = _BV(UCSZ1) | _BV(UCSZ0);
#elif defined __AVR_ATmega32__
  UCSRC = _BV(URSEL) | _BV(UCSZ1) | _BV(UCSZ0);
#else
#  error Unknown chip!
#endif

  stdout = &mystdout;

  //UCSRB |= _BV(UDRIE);
  read_idx  = 0;
  write_idx = 0;
}

#endif
