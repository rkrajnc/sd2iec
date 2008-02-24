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


   parser.c: Common file name parsers

*/

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "dirent.h"
#include "fatops.h"
#include "parser.h"

/**
 * match_name - Match a pattern against a file name
 * @matchstr: pattern to be matched
 * @dent    : pointer to the directory entry to be matched against
 *
 * This function tests if matchstr matches name in dent.
 */
static uint8_t match_name(char *matchstr, struct cbmdirent *dent) {
  uint8_t *filename = dent->name;
  uint8_t i = 0;

  while (filename[i] && i < CBM_NAME_LENGTH) {
    switch (*matchstr) {
    case '?':
      i++;
      matchstr++;
      break;

    case '*':
      return 1;

    default:
      if (filename[i++] != *matchstr++)
	return 0;
      break;
    }
  }
  if (*matchstr && *matchstr != '*')
    return 0;
  else
    return 1;
}

/**
 * next_match - get next matching directory entry
 * @dh      : directory handle
 * @matchstr: pattern to be matched
 * @type    : required file type (0 for any)
 * @dent    : pointer to a directory entry for returning the match
 *
 * This function looks for the next directory entry matching matchstr and
 * type (if != 0) and returns it in dent. Return values of the function are
 * -1 if no match could be found, 1 if an error occured or 0 if a match was
 * found.
 */
int8_t next_match(dh_t *dh, char *matchstr, uint8_t type, struct cbmdirent *dent) {
  int8_t res;

  while (1) {
    res = readdir(dh, dent);
    if (res == 0) {
      /* Skip if the type doesn't match */
      if ((type & TYPE_MASK) &&
	  (dent->typeflags & TYPE_MASK) != (type & TYPE_MASK))
	continue;

      /* Skip hidden files */
      if ((dent->typeflags & FLAG_HIDDEN) &&
	  !(type & FLAG_HIDDEN))
	continue;

      /* Skip if the name doesn't match */
      if (matchstr &&
	  !match_name(matchstr, dent))
	continue;
    }

    return res;
  }
}

/**
 * parse_path - parse CMD style directory specification
 * @in  : input buffer
 * @out : output buffer
 * @name: pointer to pointer to filename (may be NULL)
 *
 * This function parses a CMD style directory specification in a file name
 * and copies both path and filename to the output buffer, seperated by \0.
 * Both buffers may point to the same address, but must be able to hold one
 * character more than strlen(in)+1. If non-NULL, *name will point to the
 * beginning of the filename in the output buffer.
 */
void parse_path(char *in, char *out, char **name) {
  if (strchr(in, ':')) {
    uint8_t state = 0;

    /* Skip partition number */
    while (*in && isdigit(*in)) in++;

    /* Unoptimized DFA matcher             */
    /* I wonder if this can be simplified? */
    while (state != 5) {
      switch (state) {
      case 0: /* Starting state */
	switch (*in++) {
	case ':':
	  *out++ = 0;
	  state = 5;
	  break;

	case '/':
	  state = 1;
	  break;

	default:
	  state = 2;
	  break;
	}
	break;

      case 1: /* Initial slash found */
	if (*in == ':') {
	  *out++ = 0;
	  in++;
	  state = 5;
	} else {
	  *out++ = *in++;
	  state = 3;
	}
	break;

      case 2: /* Initial non-slash found */
	while (*in++ != ':');
	state = 5;
	break;

      case 3: /* Slash-noncolon found */
	switch (*in) {
	case ':':
	  *out++ = 0;
	  in++;
	  state = 5;
	  break;

	case '/':
	  in++;
	  state = 4;
	  break;

	default:
	  *out++ = *in++;
	  break;
	}
	break;

      case 4: /* Slash-noncolon-slash found */
	if (*in == ':') {
	  *out++ = 0;
	  in++;
	  state = 5;
	} else {
	  *out++ = '/';
	  *out++ = *in++;
	  state = 3;
	}
	break;
      }
    }
  } else {
    /* No colon in name, add a terminator for the path */
    if (in == out) {
      /* Make some space for the new colon */
      memmove(in+1,in,strlen(in)+1);
      in++;
    }
    *out++ = 0;
  }

  if (name)
    *name = out;

  /* Copy remaining string */
  while ((*out++ = *in++));

  return;
}
