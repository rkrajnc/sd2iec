#include <inttypes.h>
#include "config.h"
#include "time.h"
#include "rtc.h"

uint32_t get_fattime(void) {
  struct tm time;

  read_rtc(&time);
  return ((uint32_t)time.tm_year-80) << 25 |
    ((uint32_t)time.tm_mon+1) << 21 |
    ((uint32_t)time.tm_mday)  << 16 |
    ((uint32_t)time.tm_hour)  << 11 |
    ((uint32_t)time.tm_min)   << 5  |
    ((uint32_t)time.tm_sec)   >> 1;
}
