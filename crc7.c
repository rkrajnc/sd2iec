/* This file is a modified version of the output of pycrc.
 *
 * Licensing terms of pycrc:
 *
 *   Copyright (c) 2006-2007, Thomas Pircher <tehpeh@gmx.net>
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy
 *   of this software and associated documentation files (the "Software"), to deal
 *   in the Software without restriction, including without limitation the rights
 *   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *   copies of the Software, and to permit persons to whom the Software is
 *   furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *   THE SOFTWARE.
 *
 *
 * crc7.c: Calculate CRC7 for SD card commands
 *
 */

/*
 * Functions and types for CRC checks.
 *
 * Generated on Thu Nov  8 13:52:05 2007,
 * by pycrc v0.6.3, http://www.tty1.net/pycrc/
 * using the configuration:
 *    Width        = 7
 *    Poly         = 0x09
 *    XorIn        = 0x00
 *    ReflectIn    = False
 *    XorOut       = 0x00
 *    ReflectOut   = False
 *    Algorithm    = bit-by-bit-fast
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

/**
 * crc7update - Update the crc value with a new data byte.
 * @crc : The current crc value.
 * @data: The data byte to be added into crc.
 *
 * Adds data into crc and returns the updated crc value.
 */
uint8_t crc7update(uint8_t crc, const uint8_t data) {
    uint8_t i;
    bool bit;
    uint8_t c;

    c = data;
    for (i = 0x80; i > 0; i >>= 1) {
      bit = crc & 0x40;
      if (c & i) {
        bit = !bit;
      }
      crc <<= 1;
      if (bit) {
        crc ^= 0x09;
      }
    }
    crc &= 0x7f;
    return crc & 0x7f;
}
