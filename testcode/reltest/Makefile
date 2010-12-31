#  reltest - REL file test program
#  Copyright (C) 2010-2011  Ingo Korb <ingo@akana.de>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License only.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

CFLAGS   := -O
ASFLAGS  :=
SYSTEM   := c64
PROGRAM  := reltest.prg
CSRC     := reltest.c
ASRC     :=
DISKTYPE := d81

OBJ := $(CSRC:.c=.o) $(ASRC:.s=.o)

all: $(PROGRAM) disk.$(DISKTYPE)

disk.$(DISKTYPE): $(PROGRAM)
	c1541 -format disk,00 $(DISKTYPE) $@ -write $<

$(PROGRAM): $(OBJ)
	ld65 -t $(SYSTEM) -o $@ $^ $(SYSTEM).lib

clean:
	-rm $(PROGRAM) $(OBJ) disk.$(DISKTYPE)

%.s : %.c
	cc65 -t $(SYSTEM) $(CFLAGS) -o $@ $<

%.o : %.s
	ca65 -t $(SYSTEM) $(ASFLAGS) -o $@ $<

# Kill the implicit .c->.o rule
%.o : %.c
