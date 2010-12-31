/* reltest - REL file test program
   Copyright (C) 2010-2011  Ingo Korb <ingo@akana.de>

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
*/

#include <c64.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Add a workaround for a 1541 rom bug that occasionally writes data */
/* into the wrong sector if the record spans more than one sector.   */
/* #define SEEK_TWICE */

/* Static for now */
#define RECORD_LENGTH 47
#define TESTFILE_NAME "reltest"
#define TEST_RECORDS 258

unsigned char buffer[256];
unsigned char errorbuffer[128];
unsigned char reference[256];
int size,errorsize;

/* Show a hexdump of length bytes starting at data */
void hexdump(void *data, unsigned int length) {
  unsigned char *ptr = data;
  unsigned int i = 0;

  printf("%04x: ",i);
  while (length--) {
    printf("%02x ",*ptr++);
    ++i;
    if ((i & 7) == 0) {
      printf("\n%04x: ",i);
    }
  }
  puts("");
}

/* Print out an OK string */
void message_ok(void) {
  gotox(35);
  puts("OK");
}

/* Print out an ERROR string */
void message_error(void) {
  gotox(35);
  puts("ERROR\n");
}

/* Compare two values, complain and return 1 if differing */
char expect_value(char *message, int expected, int actual) {
  if (expected == actual)
    return 0;

  message_error();
  printf("%s: Expected %d, got %d\n", message, expected, actual);

  return 1;
}

/* Expect a certain error on the error channel, return 0 if it's  */
/* found or complain and return 1 if something different is seen. */
char expect_error(char *message, unsigned char error) {
  errorsize = cbm_read(15, errorbuffer, sizeof(errorbuffer));
  if (errorsize < 0) {
    printf("OSError %d\n",_oserror);
    return 1;
  }

  errorbuffer[errorsize] = 0;

  if ((errorbuffer[0] != '0' + (error / 10)) ||
      (errorbuffer[1] != '0' + (error % 10))) {
    message_error();
    printf("%s: expected %d, got %s\n", message, error, errorbuffer);
    return 1;
  }

  return 0;
}

/* Send a seek command */
void rel_seek(unsigned int record, unsigned int offset) {
  buffer[0] = 'p';
  buffer[1] = 96+3;
  buffer[2] = record & 0xff;
  buffer[3] = (record >> 8) & 0xff;
  buffer[4] = offset;

  size = cbm_write(15, buffer, 5);
  if (size != 5) {
    printf("Error seeking: Wrote %d byte, oserror %d\n",size,_oserror);
  }

#ifdef SEEK_TWICE
  /* Once more, with feeling (1541 bug workaround) */
  size = cbm_write(15, buffer, 5);
  if (size != 5) {
    printf("Error seeking: Wrote %d byte, oserror %d\n",size,_oserror);
  }
#endif
}

/* Calculate the expected length of a record */
int data_length(unsigned int record_length, unsigned int record, unsigned char offset, unsigned char mode) {
  int len;

  if (mode == 0) {
    /* First pass: full-length records */
    len = record_length;
  } else {
    /* Second pass: three types of records, depending on (record % 3) */
    /* 0: record unmodified from first pass */
    /* 1: record overwritten at len/2       */
    /* 2: record completely rewritten       */
    switch (record % 3) {
    case 0:
      len = record_length;
      break;

    case 1:
      len = (record_length / 2) + (record_length / 4);
      break;

    case 2:
      len = record_length / 4;
      break;
    }
  }

  if (offset)
    offset--;

  return len - offset;
}

/* Calculate a single data byte based on record number, offset and mode */
/* Note: This function is called to calculate buffer contents, so */
/* the offset is 0-based! */
unsigned char data_function(unsigned char record_length, unsigned int record, unsigned char offset, unsigned char mode) {
  unsigned char tmp = (record + offset) & 0xff;

  if (mode == 2) {
    /* Second pass: Writing data */
    tmp = tmp ^ 0xff;
  } else if (mode == 1) {
    /* Second pass: Reading data */
    switch (record % 3) {
    case 0:
      /* Identical to first pass */
      break;

    case 1:
      if (offset < record_length / 2)
        /* Initial part identical to first pass */
        break;

      tmp = ((record + offset - record_length/2) & 0xff) ^ 0xff;
      break;

    case 2:
      tmp = tmp ^ 0xff;
      break;
    }
  }

  if (tmp == 0)
    return 1;
  else
    return tmp;
}

/* Compare a single record */
char compare_record(unsigned int record, unsigned char record_length, unsigned char offset, unsigned char mode) {
  unsigned char i;
  int len = data_length(record_length, record, offset, mode);

  rel_seek(record, offset);

  if (len < 0) {
    /* Offset is beyond the record length */
    if (expect_error("seek", 51))
      return 1;
    else
      return 0;
  }

  if (expect_error("seek", 0))
    return 1;

  size = cbm_read(1, buffer, sizeof(buffer));
  if (expect_error("read", 0)) {
    printf("failed in record %d\n", record);
    return 1;
  }

  if (expect_value("size", len, size)) {
    printf("failed in record %d\n",record);
    hexdump(buffer, size);
    printf("ref:\n");
    hexdump(reference+offset,len);
    return 1;
  }

  if (offset)
    offset--;

  for (i=0; i<len; ++i) {
    if (buffer[i] != reference[i+offset]) {
      message_error();
      printf("datalen %d\n",len);
      printf("Mismatch in %d at %d: expected %d, got %d\n", record, i, reference[i+offset], buffer[i]);
      hexdump(buffer,size);
      printf("ref:\n");
      hexdump(reference+offset,len);
      return 1;
    }
  }

  return 0;
}

/* Verify all test records, seeking to the specified offset in the record */
char read_at_offset(unsigned char record_length, unsigned char offset, unsigned char mode) {
  unsigned int i;
  unsigned char j;

  printf("Reading data at offset %d...", offset);

  for (i=1;i<=TEST_RECORDS;++i) {
    memset(reference, 0x55, sizeof(reference));
    for (j=0;j<record_length;++j)
      reference[j] = data_function(record_length,i,j,mode);

    if (compare_record(i, record_length, offset, mode))
      return 1;
  }
  message_ok();
  return 0;
}

void run_tests(unsigned char record_length) {
  unsigned int i;
  unsigned char j,ofs;

  /* Seek to a record that shouldn't exist yet */
  printf("Seeking to nonexisting record...");
  rel_seek(125*254/record_length, 1);

  if (expect_error("seek", 50))
    return;

  message_ok();

  /* Write a single byte */
  rel_seek(TEST_RECORDS/2, 1);
  printf("Writing one byte...");
  buffer[0] = 0xff;
  cbm_write(1, buffer, 1);
  if (expect_error("write", 50))
    return;

  message_ok();

  /* Return to the first record */
  printf("Seeking to start of file...");
  rel_seek(1,1);
  if (expect_error("seek", 0))
    return;

  message_ok();

  /* Fill the file with predictable test data, without seeking */
  printf("Writing full-length data...");
  memset(buffer,0,sizeof(buffer));

  for (i=1;i<=TEST_RECORDS;++i) {
    for (j=0;j<record_length;++j) {
      buffer[j] = data_function(record_length,i,j,0);
    }
    cbm_write(1, buffer, record_length);
    if (expect_error("write", 0)) {
      printf("while writing item %d\n",i+1);
      return;
    }
  }

  message_ok();

#if 1
  /* Read data with offsets 0 to 3 */
  for (ofs = 0; ofs <= 3; ++ofs) {
    if (read_at_offset(record_length, ofs, 0))
      return;
  }

  /* Read data with offset 13 - tests for incorrect CR stripping */
  if (read_at_offset(record_length, 13, 0))
    return;

  /* Read at record length-1 */
  if (read_at_offset(record_length, record_length-1, 0))
    return;

  /* Read just the last character */
  if (read_at_offset(record_length, record_length, 0))
    return;
#endif

  /* Seek to a position beyond the record length */
  printf("Seeking beyond the record size...");
  rel_seek(1,record_length+1);
  if (expect_error("seek", 51))
    return;

  message_ok();

  /* Return to start of file */
  rel_seek(1,1);
  if (expect_error("seek 0", 0))
    return;

  /* Partial rewriting */
  printf("Rewriting data...");
  for (i=1; i<=TEST_RECORDS; i++) {
    switch (i % 3) {
    case 0:
      continue;

    case 1:
      rel_seek(i, 1+record_length / 2);
      if (expect_error("seek", 0))
        return;
      break;

    case 2:
      /* No additional seek */
      break;
    }

    for (j=0;j<record_length/4;j++)
      buffer[j] = data_function(record_length,i,j,2);

    cbm_write(1, buffer, record_length/4);
    if (expect_error("write", 0)) {
      printf("while writing record %d\n", i+1);
      return;
    }
  }

  message_ok();

  /* Check rewritten data */
  if (read_at_offset(record_length, 0, 1))
    return;
}

int main(void) {
  clrscr();
  textcolor(COLOR_WHITE);
  puts("REL-Test 0.1\n");

  cbm_open(15,8,15,"ui");
  size = cbm_read(15, buffer, sizeof(buffer));
  buffer[size] = 0;
  printf("Drive: %s\n", buffer);

  /* Delete test file */
  strcpy(buffer, "s:" TESTFILE_NAME);
  cbm_write(15,buffer,strlen(buffer));

  strcpy(buffer, TESTFILE_NAME ",l,_");

  /* Set record length */
  buffer[strlen(buffer)-1] = RECORD_LENGTH;

  printf("Creating File...");
  cbm_open(1,8,3,buffer);

  message_ok();

  /* Run tests */
  if (!expect_error("open", 0))
    run_tests(RECORD_LENGTH);

  cbm_close(1);
  cbm_close(15);

  return 0;
}
