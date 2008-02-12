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


   avrcompat.h: Compatibility defines for multiple target chips
*/

#ifndef AVRCOMPAT_H
#define AVRCOMPAT_H

#ifdef __AVR_ATmega644__
#  define RXC   RXC0
#  define RXEN  RXEN0
#  define TXC   TXC0
#  define TXEN  TXEN0
#  define UBRRH UBRR0H
#  define UBRRL UBRR0L
#  define UCSRA UCSR0A
#  define UCSRB UCSR0B
#  define UCSRC UCSR0C
#  define UCSZ0 UCSZ00
#  define UCSZ1 UCSZ01
#  define UDR   UDR0
#  define UDRIE UDRIE0
#  define UDRE  UDRE0
#  define USART_UDRE_vect USART0_UDRE_vect

#elif defined __AVR_ATmega32__
#  define TIMER2_COMPA_vect TIMER2_COMP_vect
#  define TCCR0B TCCR0
#  define TCCR2A TCCR2
#  define TCCR2B TCCR2
#  define TIFR0  TIFR
#  define TIMSK2 TIMSK
#  define OCIE2A OCIE2
#  define OCR2A  OCR2

#elif defined __AVR_ATmega128__
#  define UBRRH  UBRR0H
#  define UBRRL  UBRR0L
#  define UCSRA  UCSR0A
#  define UCSRB  UCSR0B
#  define UCSRC  UCSR0C
#  define UDR    UDR0
#  define USART_UDRE_vect USART0_UDRE_vect
#  define TIMER2_COMPA_vect TIMER2_COMP_vect
#  define TCCR0B TCCR0
#  define TCCR2A TCCR2
#  define TCCR2B TCCR2
#  define TIFR0  TIFR
#  define TIMSK2 TIMSK
#  define OCIE2A OCIE2
#  define OCR2A  OCR2

#else
#  error Unknown chip!
#endif

#endif /* AVRCOMPAT_H */
