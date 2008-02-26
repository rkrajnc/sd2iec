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
#include "errormsg.h"
#include "fatops.h"
#include "parser.h"

path_t current_dir;

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
 * first_match - get the first matching directory entry
 * @path    : pointer to a path object
 * @matchstr: pattern to be matched
 * @type    : required file type (0 for any)
 * @dent    : pointer to a directory entry for returning the match
 *
 * This function looks for the first directory entry matching matchstr and
 * type (if != 0) in path and returns it in dent. Uses matchdh for matching
 * and returns the same values as next_match. This function is just a
 * convenience wrapper around opendir+next_match, it is not required to call
 * it before using next_match.
 */
int8_t first_match(path_t *path, char *matchstr, uint8_t type, struct cbmdirent *dent) {
  int8_t res;

  if (opendir(&matchdh, path))
    return 1;

  res = next_match(&matchdh, matchstr, type, dent);
  if (res < 0)
    set_error(ERROR_FILE_NOT_FOUND);
  return res;
}

/**
 * parse_path - parse CMD style directory specification
 * @in          : input buffer
 * @path        : pointer to path object
 * @name        : pointer to pointer to filename (may be NULL)
 * @parse_always: force parsing even if no : is present in the input string
 *
 * This function parses a CMD style directory specification in the input
 * buffer. If successful, the path object will be set up for accessing
 * the path named in the input buffer. Returns 0 if successful or 1 if an
 * error occured.
 */
uint8_t parse_path(char *in, path_t *path, char **name, uint8_t parse_always) {
  struct cbmdirent dent;
  char *end;
  char saved;

  *path = current_dir;

  if (parse_always || strchr(in, ':')) {
    /* Skip partition number */
    while (*in && (isdigit(*in) || *in == ' ')) in++;

    if (*in != '/') {
      *name = strchr(in, ':');
      if (*name == NULL)
        *name = in;
      else
        *name += 1;
      return 0;
    }

    while (*in) {
      switch (*in++) {
      case '/':
        switch (*in) {
        case '/':
          /* Double slash -> root */
          path->fat = 0;
          break;

        case 0:
          /* End of path found, no name */
          *name = in;
          return 0;

        case ':':
          /* End of path found */
          *name = in+1;
          return 0;

        default:
          /* Extract path component and match it */
          end = in;
          while (*end && *end != '/' && *end != ':') end++;
          saved = *end;
          *end = 0;
          if (first_match(path, in, FLAG_HIDDEN, &dent)) {
            /* first_match has set an error already */
            if (current_error == ERROR_FILE_NOT_FOUND)
              set_error(ERROR_FILE_NOT_FOUND_39);
            return 1;
          }

          if ((dent.typeflags & TYPE_MASK) != TYPE_DIR) {
            /* Not a directory */
            /* FIXME: Try to mount as image here so they can be accessed like a directory */
            set_error(ERROR_FILE_NOT_FOUND_39);
            return 1;
          }

          /* Match found, move path */
          *path = dent.path;
          *end = saved;
          in = end;
          break;
        }
        break;

      case 0:
        /* End of path found, no name */
        *name = in-1;  // -1 to fix the ++ in switch
        return 0;

      case ':':
        /* End of path found */
        *name = in;
        return 0;
      }
    }
  }
  *name = in;
  return 0;
}
