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

   
   fastloader.S: Low level fastloader transfer routines

*/

#include "config.h"
#include <avr/io.h>
	
	.section .text

	;; ===================================================================
	;;  Utility routines
	;; ===================================================================
	
	;; Counts r18 down to zero
	;; Takes (r18-1)*3 + 6 cycles, not including the rcall
delayloop:
	dec	r18		; 1
	brne	delayloop	; 2/1
	ret			; 4

	;; Including the rcall this will delay for 8 cycles
delay1us:
	nop			; 1
	ret			; 4


	
	;; ====================================================================
	;;  Turbodisk
	;; ====================================================================

	;; 
	;; Sends a single byte in r24 using the Turbodisk protocol
	;; 
	.global turbodisk_byte
turbodisk_byte:
	;; Wait until data is low
	sbic	_SFR_IO_ADDR(IEC_PIN), IEC_PIN_DATA
	rjmp	turbodisk_byte

	;; Byte preparation
	ldi	r18, 0xff
	eor	r24, r18	; Invert it

	;; All bus lines high
	in	r18, IEC_DDR
	andi	r18, ~(IEC_BIT_ATN | IEC_BIT_DATA | IEC_BIT_CLOCK | IEC_BIT_SRQ)
	out	_SFR_IO_ADDR(IEC_DDR), r18
	
	;; Wait until data is high
0:	sbis	_SFR_IO_ADDR(IEC_PIN), IEC_PIN_DATA ; 2
	rjmp	0b

	rcall	delay1us	; Move the timing window slightly
		
	ldi	r20, 4		; 1 - Bitpair counter

tdbitloop:	
	clr	r19		; 1 - Output register
	lsl	r24		; 1 - grab top bit
	rol	r19		; 1 - move to bottom
	lsl	r24		; 1 - grab top bit
	rol	r19		; 1 - move to bottom
	lsl	r19		; 1 - move pair to correct position
	
	;; Wait for 28us from data high -> 224-8=216 cycles
	ldi	r18,69		; 1
	rcall	delayloop	; 213

	nop			; 1
	
	out	_SFR_IO_ADDR(IEC_DDR), r19	; 1 - output the bit pair
	
	rcall	delay1us	; 8 - additional us for loops 2-4

	dec	r20		; 1 - Decrement bitpair counter
	brne	tdbitloop	; 2/1 - loop until done

	;; Wait for 26us from last bitpair -> 208-12=196 cycles
	ldi	r18,63		; 1
	rcall	delayloop	; 195

	;; Data high, Clock low
	cbi	_SFR_IO_ADDR(IEC_DDR), IEC_PIN_DATA
	sbi	_SFR_IO_ADDR(IEC_DDR), IEC_PIN_CLOCK

	ret 
	

	;; 
	;; Sends a complete buffer of r22 bytes at r24/r25 using the Turbodisk protocol
	;; 
	.global turbodisk_buffer
turbodisk_buffer:
	;; Wait until data is low
	sbic	_SFR_IO_ADDR(IEC_PIN), IEC_PIN_DATA
	rjmp	turbodisk_buffer

	;; All bus lines high
	in	r18, IEC_DDR
	andi	r18, ~(IEC_BIT_ATN | IEC_BIT_DATA | IEC_BIT_CLOCK | IEC_BIT_SRQ)
	out	_SFR_IO_ADDR(IEC_DDR), r18

	movw	r30,r24		; output pointer
	ldi	r21, 0xff	; constant for inverting bytes
		
	;; Wait until data is high
0:	sbis	_SFR_IO_ADDR(IEC_PIN), IEC_PIN_DATA ; 2
	rjmp	0b

	rcall	delay1us	; Move the timing window slightly
				
	;; Initial delay of 4 us
	ldi	r18,8		; 1
	rcall	delayloop	; 30
	
tdbyteloop:
	ld	r0, Z+		; 2 - load byte
	ldi	r20, 4		; 1 - bitpair counter
	eor	r0,r21		; 1 - invert byte

	;; Waste another 12 us -> 96-4=92 cycles
	ldi	r18,28		; 1
	rcall	delayloop	; 90
	nop			; 1
	
tdbitloop2:	
	clr	r19		; 1 - clear output register
	lsl	r0		; 1 - grab top bit
	rol	r19		; 1 - move to bottom
	lsl	r0		; 1 - grab top bit
	rol	r19		; 1 - move to bottom
	lsl	r19		; 1 - move pair to correct position
	
	;; Wait another 24 us -> 192-7=185 cycles
	ldi	r18,59		; 1
	rcall	delayloop	; 183
	nop			; 1
	out	_SFR_IO_ADDR(IEC_DDR), r19	; 1 - output the bit pair

	;; 5 us -> 40-3=37 cycles
	ldi	r18,10		; 1
	rcall	delayloop	; 36
		
	dec	r20		; 1 - Decrement bitpair counter
	brne	tdbitloop2	; 2/1 - loop until done

	;; Wait 10 us -> 80-3+1=78 cycles
	ldi	r18,23		; 1
	rcall	delayloop	; 75
	rjmp	1f		; 2
1:	

	dec	r22		; 1 - Decrement byte counter
	brne	tdbyteloop	; 2/1 - loop until done
	
	;; Final delay:	11 us -> 88-2+1=87 cycles
	ldi	r18,27		; 1
	rcall	delayloop	; 87
	
	;; Data high, Clock low
	cbi	_SFR_IO_ADDR(IEC_DDR), IEC_PIN_DATA
	sbi	_SFR_IO_ADDR(IEC_DDR), IEC_PIN_CLOCK

	ret 

	.end
