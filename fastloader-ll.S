/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007,2008  Ingo Korb <ingo@akana.de>
   Copyright (C) 2007,2008  M.Kiesel <mayne@users.sourceforge.net>

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


   fastloader.S: Low level fastloader transfer routines.

   Thanks to Jochen Adler for his Jiffy docs at http://www.nlq.de/

*/


#include "config.h"
#include <avr/io.h>

/* Timing offsets for JiffyDos read/write */
/* These values are added to the delayloop counter before the first      */
/* bitpair read/write. They were determined experimentally to center the */
/* range of working OSCCAL values around the default value.              */
#define JIFFY_OFFSET_WRITE 30
#define JIFFY_OFFSET_READ  30

        .section .text

#if defined(CONFIG_JIFFY_ASM) || defined(CONFIG_TURBODISK) || defined(CONFIG_FC3)
        ;; ===================================================================
        ;;  Utility routines
        ;; ===================================================================

        ;; waits the number of cycles given in r18 plus 18, including rcall
        ;; this works for 19 to 255 cycles, r18 will be changed
        ;; Please use the delay_cycles macro instead of calling directly.
cycleloop:
        ; rcall                 ; 3
1:
        cpi     r18, 5          ; 1 - less than one loop left?
        brcs    2f              ; 2/1 - then go to the final part
        subi    r18, 5          ; 1 - minus cycles for one loop
        rjmp    1b              ; 2
2:
        ; r18 is 0..4 now and we have 12..16 cycles to waste, DELTA see above
        bst     r18, 0          ; 1 - take lowest bit
        brts    3f              ; 1/2 - waste 1 cycle more if set
3:
        bst     r18, 1          ; 1 - take bit with value 2
        brtc    5f              ; 1/2 - waste 2 cycles more if set
        rjmp    4f              ; 2
4:
        nop                     ; 1
5:
        bst     r18, 2          ; 1 - take bit with value 4
        brtc    7f              ; 1/2 - waste 4 cycles more if set
        nop                     ; 1
        rjmp    6f              ; 2
6:
        rjmp    7f              ; 2
7:
        ret                     ; 4

        ;; Convenience-wrapper for cycleloop
        .macro delay_cycles num
        ldi     r18, \num-19     ; 1 - minus cycles for ldi, rcall, sbi, cpi, brcc+, DELTA
        rcall   cycleloop       ; 3
        .endm


        ;; Including the rcall this will delay for 8 cycles
delay1us:
        nop                     ; 1
        ret                     ; 4

        ;; Variable-delay macros to compensate for the additional instructions
        ;; when using separate output lines
#ifdef IEC_SEPARATE_OUT
#  define JIFFY_SUB_CYCLES 3
        
        ;; Nibble-swap the given register
        .macro swapwait reg
        swap    \reg            ; 1
        .endm
#else
#  define JIFFY_SUB_CYCLES 0
        
        ;; Wait one cycle
        .macro swapwait reg
        nop                     ; 1
        .endm
#endif
#endif // ifdef any fast loader

#ifdef CONFIG_JIFFY_ASM
        ;; ====================================================================
        ;;  JiffyDOS
        ;; ====================================================================

        ;;
        ;; Receives a single byte using the JiffyDOS protocol
        ;; return uint8_t reveicedbyte (r24)
        ;;
        .global jiffy_receive
jiffy_receive:
        ;; Disable interrupts
        cli

        ;; Move pointer to EOF variable into Z
        movw    r30,r24

        ;; Set clock+data high
        cbi     _SFR_IO_ADDR(IEC_OUT), IEC_OPIN_CLOCK
        cbi     _SFR_IO_ADDR(IEC_OUT), IEC_OPIN_DATA

        ;; Wait until clock is high and emulate ATN-Ack
0:      sbis    _SFR_IO_ADDR(IEC_PIN), IEC_PIN_ATN
        sbi     _SFR_IO_ADDR(IEC_OUT), IEC_OPIN_DATA ; Data low if ATN is low
        sbis    _SFR_IO_ADDR(IEC_PIN), IEC_PIN_CLOCK
        rjmp    0b

        ;; Wait for 13us from clock high -> 104 cycles
        delay_cycles 104 + JIFFY_OFFSET_READ ; 104 + Offset

        ;; Read bit 5+4
        in      r0, _SFR_IO_ADDR(IEC_PIN) ; 1

        ;; Move bits to the correct position
        swap    r0              ; 1
        mov     r24, r0         ; 1
        andi    r24, 0x20       ; 1 - mask swapped data bit
        mov     r25, r0         ; 1
        andi    r25, 0x40       ; 1 - mask swapped clock bit
        lsr     r25             ; 1 - shift into position
        lsr     r25             ; 1
        or      r24, r25        ; 1 - merge

        ;; Wait 13us -> 104-9=95 cycles
        delay_cycles 95         ; 95

        ;; Read bit 7+6
        in      r0, _SFR_IO_ADDR(IEC_PIN) ; 1

        ;; Move to corrent position and merge
        swap    r0              ; 1
        mov     r25, r0         ; 1
        andi    r25, 0x40       ; 1 - mask swapped clock bit
        or      r24, r25        ; 1 - merge
        mov     r25, r0         ; 1
        andi    r25, 0x20       ; 1 - mask swapped data bit
        lsl     r25             ; 1 - shift into position
        lsl     r25             ; 1
        or      r24, r25        ; 1 - merge

        ;; Wait 11us -> 88-10=78 cycles
        delay_cycles 78         ; 78

        ;; Read bit 3+1 [sic]
        in      r0, _SFR_IO_ADDR(IEC_PIN) ; 1
        mov     r25, r0         ; 1
        andi    r25, 0x04       ; 1 - mask clock bit
        lsl     r25             ; 1 - shift into position
        or      r24, r25        ; 1 - merge
        mov     r25, r0         ; 1
        andi    r25, 0x02       ; 1 - mask data bit
        or      r24, r25        ; 1 - merge

        ;; Wait 13us -> 104-8=96 cycles
        delay_cycles 96         ; 96

        ;; Read Bit 2+0
        in      r0, _SFR_IO_ADDR(IEC_PIN) ; 1
        mov     r25, r0         ; 1
        andi    r25, 0x04       ; 1 - mask clock bit
        or      r24, r25        ; 1 - merge
        mov     r25, r0         ; 1
        andi    r25, 0x02       ; 1 - mask data bit
        lsr     r25             ; 1 - shift into position
        or      r24, r25        ; 1
        clr     r25             ; 1 - clear high byte of return value

        ;; Wait 13us -> 104-9=95 cycles
        delay_cycles 95         ; 95

        ;; Read EOI mark
        in      r19, _SFR_IO_ADDR(IEC_PIN) ; 1

        ;; Wait 6us -> 48-1=47 cycles
        delay_cycles 47         ; 47

        ;; Data low
        sbi     _SFR_IO_ADDR(IEC_OUT), IEC_OPIN_DATA ; 1

        ;; Post-process data and store last bus state
        ldi     r18, 0xff
        eor     r24, r18        ; invert received data (result)
        st      Z, r19          ; store last bus state
        sei                     ; enable interrupts
        ret



        ;; Shifts the lowest two bytes from r0 to r19
        ;; and sends then on the bus at cycle 10
        ;; Needs 14 cycles including the rcall with combined in/out pins,
        ;; needs 17 cycles with separeted output pins
jiffy_sendbits:
                                ; 1 - Enable pullups on (not yet swapped) input bits
        clr     r19             ; 1 - clear output register
        lsr     r0              ; 1
        rol     r19             ; 1
        lsr     r0              ; 1
        rol     r19             ; 1
        lsl     r19             ; 1
#ifdef IEC_SEPARATE_OUT
        swap    r19             ; 1 - swap nibbles
        ori     r19, IEC_PULLUPS ; 1 - keep pullups on the input lines
        nop                     ; 1 - waste one cycle for easier timing compensation
#endif
        out     _SFR_IO_ADDR(IEC_OUT), r19 ; 1 - output the bit pair
        ret                     ; 4

        ;;
        ;; Sends a single byte using the Jiffy protocol
        ;; uint8_t value (r24), uint8_t eoi (r22), uint8_t loadflags (r20)
        ;; return uint8_t atnactive (r24)
        ;;
        ;; eoi should always be 0 for LOAD because the eoi marker is
        ;; transmitted in a different way there (see C code).
        ;;
        ;; Loadflags has a dual purpose:
        ;;  - if it's != 0, the start condition will be modified for Jiffy-LOAD
        ;;  - bit 7 will be cleared after that
        ;;  - if it's still != 0, the function will exit after sending
        ;;    the last bitpair - required for all but the final byte of a block
        ;;    during LOAD
        .global jiffy_send
jiffy_send:
        cli                     ; Disable interrupts

        ;; Calculate bus wait condition based on current state
        ;; The loop below will spin while ATN and Clock are high and
        ;; - Data is low  (single-byte transfer)
        ;; - Data is high (LOAD)
        ldi     r21, IEC_BIT_CLOCK | IEC_BIT_ATN
        tst     r20             ; loadflag set?
        breq    0f              ; Yes, skip
                ; No: Set data bit
        ori     r21, IEC_BIT_DATA

0:      andi    r20, 0x7f       ; Clear bit 7 of loadflags

        ;; Calculate bus state for EOI/not-EOI mark
        tst     r22             ; EOI flag set?
        breq    0f              ; No, branch

        ldi     r22, IEC_PULLUPS | IEC_OBIT_DATA  ; Data low, Clock hi on EOI
        rjmp    1f

0:      ldi     r22, IEC_PULLUPS | IEC_OBIT_CLOCK ; Data hi, Clock low on non-EOI

1:
        ;; Set clock and data high/inactive - FFB5
        cbi     _SFR_IO_ADDR(IEC_OUT), IEC_OPIN_DATA
                ; this is the actual ready signal for the C64
        cbi     _SFR_IO_ADDR(IEC_OUT), IEC_OPIN_CLOCK

        ;; Wait 1us to allow the bus to settle (J1541 needs 4us here)
        rcall   delay1us

        ;; Set up a few registers
        mov     r0, r24         ; move data byte to r0
        ldi     r23, 0xff       ; constant for inverting bytes
        eor     r0, r23         ; invert data byte

        ;; Wait for start condition (calculated above) from C64 - FFB8
0:      in      r24, _SFR_IO_ADDR(IEC_PIN)
        andi    r24, IEC_BIT_CLOCK | IEC_BIT_DATA | IEC_BIT_ATN ; 1
        cp      r24, r21        ; 1
        breq    0b              ; 1

        ;; Check for ATN (J1541 doesn't do this, but I'm paranoid)
        ;; Doesn't work, further analysis required
        andi    r24, IEC_BIT_ATN ; 1
        breq    js_finish        ; 1 - jump if ATN is low

        ;; Output the first bitpair 6us (48 cycles) after the start
        ;; 6us is the best-case time from bus check to transmission in J1541
        delay_cycles 28 + JIFFY_OFFSET_WRITE - JIFFY_SUB_CYCLES
                                ; 28 + 2 = 30 = 48-17-1 (NOP omitted)
        rcall   jiffy_sendbits  ; 10+4 - [FFBD]

        ;; Output the second bitpair 10us (80 cycles) after the first
        delay_cycles 63 - JIFFY_SUB_CYCLES
                                ; 63 = 80-15-2
        rcall   jiffy_sendbits  ; 10+4 - [FFC4]

        ;; Output the third bitpair 11us (88 cycles) after the second
        delay_cycles 72 - JIFFY_SUB_CYCLES
                                ; 72 = 88-15-1
        rcall   jiffy_sendbits  ; 10+4 - [FFCC]

        ;; Output the fourth bitpair 10us (80 cycles) after the third
        delay_cycles 63 - JIFFY_SUB_CYCLES
                                ; 63 = 80-15-2
        rcall   jiffy_sendbits  ; 10+4 - [FFD3]

        ;; Skip sending EOI for LOAD code path
        tst     r20             ; 1
        brne    js_finish       ; 1

        ;; Output EOI marker 11us (88 cycles) after the last bitpair
        delay_cycles 78         ; 78 = 88-8-2

        out     _SFR_IO_ADDR(IEC_OUT), r22 ; 1 - output EOI marker [FFDB]

        ;; Wait 1us to allow the bus to settle (J1541 needs 4us here)
        rcall   delay1us

        ;; Wait until data is low, check ATN [FFDE]
        ;; This loop won't wait when EOI is signalled.
0:      in      r24, _SFR_IO_ADDR(IEC_PIN) ; Read bus
        sbrs    r24, IEC_PIN_ATN           ; Skip if ATN is high
        rjmp    js_finish                  ; ATN low, exit loop
        sbrc    r24, IEC_PIN_DATA          ; Skip if Data is low
        rjmp    0b                         ; No Data, no ATN -> loop

js_finish:
        eor     r24, r23         ; invert port state (ATN low returns true)
        andi    r24, IEC_BIT_ATN ; single out ATN bit

#if IEC_PULLUPS != 0
        in      r22, _SFR_IO_ADDR(IEC_OUT) ; Read output register
        ori     r22, IEC_PULLUPS ; Set whatever bits are is required
        out     _SFR_IO_ADDR(IEC_OUT), r22 ; Write output register
#endif
        
        sei                     ; re-enable interrupts
        ret

#endif  // CONFIG_JIFFY_ASM

#ifdef CONFIG_TURBODISK
        ;; ====================================================================
        ;;  Turbodisk
        ;; ====================================================================

        ;;
        ;; Sends a single byte in r24 using the Turbodisk protocol
        ;;
        .global turbodisk_byte
turbodisk_byte:
        ;; Wait until data is low
        sbic    _SFR_IO_ADDR(IEC_PIN), IEC_PIN_DATA
        rjmp    turbodisk_byte

        ;; Byte preparation
        ldi     r18, 0xff
        eor     r24, r18        ; Invert it

        ;; All bus lines high
        ldi     r18, IEC_PULLUPS
        out     _SFR_IO_ADDR(IEC_OUT), r18

        ;; Wait until data is high
0:      sbis    _SFR_IO_ADDR(IEC_PIN), IEC_PIN_DATA ; 2
        rjmp    0b

        rcall   delay1us        ; Move the timing window slightly

        ldi     r20, 4          ; 1 - Bitpair counter

tdbitloop:
        clr     r19             ; 1 - Clear output register
        lsl     r24             ; 1 - grab top bit
        rol     r19             ; 1 - move to bottom
        lsl     r24             ; 1 - grab top bit
        rol     r19             ; 1 - move to bottom
#if CONFIG_HARDWARE_VARIANT == 4
        swap    r18             ; 1 - swap the bits to the upper nibble (uIEC)
#else
        lsl     r19             ; 1 - move pair to correct position
#endif
        
        ;; Wait for 28us from data high -> 224-8=216 cycles
        delay_cycles 214        ; 214
        swapwait r19            ; 1

        out     _SFR_IO_ADDR(IEC_OUT), r19      ; 1 - output the bit pair

        rcall   delay1us        ; 8 - additional us for loops 2-4

        dec     r20             ; 1 - Decrement bitpair counter
        brne    tdbitloop       ; 2/1 - loop until done

        ;; Wait for 26us from last bitpair -> 208-12=196 cycles
        delay_cycles 196        ; 196

        ;; Data high, Clock low
        ldi     r18, IEC_PULLUPS | IEC_OBIT_CLOCK
        out     _SFR_IO_ADDR(IEC_OUT), r18

        ret


        ;;
        ;; Sends a complete buffer of r22 bytes at r24/r25 using the Turbodisk protocol
        ;;
        .global turbodisk_buffer
turbodisk_buffer:
        ;; Wait until data is low
        sbic    _SFR_IO_ADDR(IEC_PIN), IEC_PIN_DATA
        rjmp    turbodisk_buffer

        ;; All bus lines high
        ldi     r18, IEC_PULLUPS
        out     _SFR_IO_ADDR(IEC_OUT), r18

        movw    r30,r24         ; output pointer
        ldi     r21, 0xff       ; constant for inverting bytes

        ;; Wait until data is high
0:      sbis    _SFR_IO_ADDR(IEC_PIN), IEC_PIN_DATA ; 2
        rjmp    0b

        ;; Initial delay of 4 us + move the timing window by 1 us for stability
        delay_cycles (4+1)*8    ; 40

        ;; 287 AVR cycles from here to first LDA $DD00 on C64 side ($F822)
tdbyteloop:
        ld      r0, Z+          ; 2 - load byte
        ldi     r20, 4          ; 1 - bitpair counter
        eor     r0,r21          ; 1 - invert byte

        ;; Waste another 12 us -> 96-4=92 cycles
        delay_cycles 92         ; 92

tdbitloop2:
        clr     r19             ; 1 - Clear output register
        lsl     r0              ; 1 - grab top bit
        rol     r19             ; 1 - move to bottom
        lsl     r0              ; 1 - grab top bit
        rol     r19             ; 1 - move to bottom
#if CONFIG_HARDWARE_VARIANT == 4
        swap    r19             ; 1 - swap bits to upper nibble (uIEC)
#else
        lsl     r19             ; 1 - move pair to correct position
#endif

        ;; Wait another 24 us -> 192-7=185 cycles
        delay_cycles 184        ; 184
        swapwait r19            ; 1
        out     _SFR_IO_ADDR(IEC_OUT), r19      ; 1 - output the bit pair

        ;; 5 us -> 40-3=37 cycles
        delay_cycles 37         ; 37

        dec     r20             ; 1 - Decrement bitpair counter
        brne    tdbitloop2      ; 2/1 - loop until done

        ;; Wait 10 us -> 80-3+1=78 cycles
        delay_cycles 78         ; 78

        dec     r22             ; 1 - Decrement byte counter
        brne    tdbyteloop      ; 2/1 - loop until done

        ;; Final delay: 11 us -> 88 cycles
        delay_cycles 88         ; 88

        ;; Data high, Clock low
        ldi     r18, IEC_PULLUPS | IEC_OBIT_CLOCK
        out     _SFR_IO_ADDR(IEC_OUT), r18

        ret
#endif

        .end
