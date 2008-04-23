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
#include "ustring.h"
#include "parser.h"

partition_t partition[CONFIG_MAX_PARTITIONS];
uint8_t current_part;
uint8_t max_part;


/**
 * check_invalid_name - check for invalid characters in a file name
 * @name: pointer to the file name
 *
 * This function checks if the passed name contains any characters
 * that are not valid in a CBM file name. Returns 1 if invalid
 * characters are found, 0 if not.
 */
uint8_t check_invalid_name(uint8_t *name) {
  while (*name) {
    if (*name == '=' || *name == '"' ||
        *name == '*' || *name == '?' ||
        *name == ',')
      return 1;
    name++;
  }
  return 0;
}

/**
 * parse_partition - parse a partition number from a file name
 * @buf     : pointer to pointer to filename
 *
 * This function parses the partition number from the file name
 * specified in buf and advances buf to point at the first character
 * that isn't part of the number. Returns a 0-based partition number.
 */
uint8_t parse_partition(uint8_t **buf) {
  uint8_t part=0;
  while (isdigit(**buf) || **buf == ' ') {
    if(isdigit(**buf))
      part=part*10+(**buf-'0');
    (*buf)++;
  }
  if (part == 0)
    return current_part;
  else
    return part-1;
}


/**
 * match_name - Match a pattern against a file name
 * @matchstr: pattern to be matched
 * @dent    : pointer to the directory entry to be matched against
 *
 * This function tests if matchstr matches name in dent.
 * Returns 1 for a match, 0 otherwise.
 */
uint8_t match_name(uint8_t *matchstr, struct cbmdirent *dent) {
  uint8_t *filename = dent->name;
  uint8_t *starpos;

  /* Shortcut for chaining fastloaders ("!*file") */
  if (*filename == *matchstr && matchstr[1] == '*')
    return 1;

  while (*filename) {
    switch (*matchstr) {
    case '?':
      filename++;
      matchstr++;
      break;

    case '*':
      starpos = matchstr;
      matchstr += ustrlen(matchstr)-1;
      filename += ustrlen(filename)-1;
      while (matchstr != starpos) {
        if (*matchstr != *filename && *matchstr != '?')
          return 0;
        filename--;
        matchstr--;
      }
      return 1;

    default:
      if (*filename++ != *matchstr++)
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
int8_t next_match(dh_t *dh, uint8_t *matchstr, uint8_t type, struct cbmdirent *dent) {
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
int8_t first_match(path_t *path, uint8_t *matchstr, uint8_t type, struct cbmdirent *dent) {
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
uint8_t parse_path(uint8_t *in, path_t *path, uint8_t **name, uint8_t parse_always) {
  struct cbmdirent dent;
  uint8_t *end;
  uint8_t saved;
  uint8_t part;

  if (parse_always || ustrchr(in, ':')) {
    /* Skip partition number */
    part=parse_partition(&in);
    if(part>=max_part) {
      set_error(ERROR_DRIVE_NOT_READY);
      return 1;
    }

    path->part = part;
    path->fat  = partition[part].current_dir;

    if (*in != '/') {
      *name = ustrchr(in, ':');
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
          /* This will break for image files with TYPE_DIR entries */
          path->fat = dent.fatcluster;
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
  } else {
    /* No :, use current dir/path */
    path->part = current_part;
    path->fat  = partition[current_part].current_dir;
  }

  *name = in;
  return 0;
}
