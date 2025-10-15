#include <stdint.h>

#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>

#define TO_SECS(x) (x) / ( 1000 * 1000 * 10 )
#endif

typedef struct {
  int y;
  int m;
  int d;
  int hh;
  int mm;
  int ss;
  char tz;
} timestamp_t;

timestamp_t now();
timestamp_t today_00();
timestamp_t today_24();

#ifdef _WIN32
SYSTEMTIME timestamp_to_systime(timestamp_t t);
#endif

int64_t timestamp_cmp(timestamp_t a, timestamp_t b); 

void timestamp_day_print(timestamp_t t);
