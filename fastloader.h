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


   fastloader.h: Definitions for the high level fast loader handling

*/

#ifndef FASTLOADER_H
#define FASTLOADER_H

#define FL_NONE              0
#define FL_TURBODISK         1
#define FL_FC3_LOAD          2
#define FL_FC3_SAVE          3
#define FL_DREAMLOAD         4
#define FL_DREAMLOAD_OLD     5
#define FL_FC3_FREEZED       6
#define FL_ULOAD3            7
#define FL_GI_JOE            8
#define FL_EPYXCART          9
#define FL_GEOS_S1          10
#define FL_GEOS_S1_KEY      11
#define FL_GEOS_S23_1541    12
#define FL_GEOS_S23_1571    13
#define FL_GEOS_S23_1581    14
#define FL_WHEELS_S1_64     15
#define FL_WHEELS_S1_128    16
#define FL_WHEELS_S2        17
#define FL_WHEELS44_S2      18
#define FL_WHEELS44_S2_1581 19
#define FL_NIPPON           20
#define FL_AR6_1581         21

#ifndef __ASSEMBLER__

extern uint8_t detected_loader;
extern volatile uint8_t fl_track;
extern volatile uint8_t fl_sector;
extern void (*geos_send_byte)(uint8_t byte);
extern uint8_t (*geos_get_byte)(void);

void load_turbodisk(void);
void load_fc3(uint8_t freezed);
void save_fc3(void);
void load_dreamload(void);
void load_uload3(void);
void load_gijoe(void);
void load_epyxcart(void);
void load_geos(void);
void load_geos64_s1(void);
void load_geos128_s1(void);
void load_wheels_s1(const char *filename);
void load_wheels_s2(void);
void load_nippon(void);
void load_ar6_1581(void);

#endif
#endif
