#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "timestamp.h"

timestamp_t now() {
#ifdef _WIN32
  SYSTEMTIME st = { 0 };
  GetLocalTime(&st);
  return (timestamp_t) {
    .y = st.wYear,
    .m = st.wMonth,
    .d = st.wDay,
    .hh = st.wHour,
    .mm = st.wMinute,
    .ss = st.wSecond,
  };
#else
  time_t t;
  time(&t);
  struct tm *tm = localtime(&t);
  return (timestamp_t) {
    .y = tm->tm_year + 1900,
    .m = tm->tm_mon + 1,
    .d = tm->tm_mday,
    .hh = tm->tm_hour,
    .mm = tm->tm_min,
    .ss = tm->tm_sec,
  };
#endif
}

timestamp_t today_00() {
#ifdef _WIN32
  SYSTEMTIME st = { 0 };
  GetLocalTime(&st);
  return (timestamp_t) {
    .y = st.wYear,
    .m = st.wMonth,
    .d = st.wDay,
  };
#else
  time_t t;
  time(&t);
  struct tm *tm = localtime(&t);
  return (timestamp_t) {
    .y = tm->tm_year + 1900,
    .m = tm->tm_mon + 1,
    .d = tm->tm_mday,
  };
#endif
}

timestamp_t today_24() {
#ifdef _WIN32
  SYSTEMTIME st = { 0 };
  GetLocalTime(&st);
  return (timestamp_t) {
    .y = st.wYear,
    .m = st.wMonth,
    .d = st.wDay,
    .hh = 23,
    .mm = 59,
    .ss = 59
  };
#else
  time_t t;
  time(&t);
  struct tm *tm = localtime(&t);
  return (timestamp_t) {
    .y = tm->tm_year + 1900,
    .m = tm->tm_mon + 1,
    .d = tm->tm_mday,
    .hh = 23,
    .mm = 59,
    .ss = 59
  };
#endif
}

#ifdef _WIN32
SYSTEMTIME timestamp_to_systime(timestamp_t t) {
  SYSTEMTIME st = (SYSTEMTIME) {
    .wYear = (WORD)t.y,
    .wMonth = (WORD)t.m,
    .wDay = (WORD)t.d,
    .wHour = (WORD)t.hh,
    .wMinute = (WORD)t.mm,
    .wSecond = (WORD)t.ss,
    .wMilliseconds = 0,
  };
  // force normalization
  FILETIME ft = { 0 };
  SYSTEMTIME local;
  SystemTimeToFileTime(&st, &ft);
  FileTimeToSystemTime(&ft, &local);
  return local;
}
#else
time_t timestamp_to_systime(timestamp_t t) {
  struct tm tm = { 0 };
  tm.tm_year = t.y - 1900;
  tm.tm_mon  = t.m - 1;
  tm.tm_mday = t.d;
  tm.tm_hour = t.hh;
  tm.tm_min  = t.mm;
  tm.tm_sec  = t.ss;
  tm.tm_isdst = -1;

  return mktime(&tm);
}
#endif

int64_t timestamp_cmp(timestamp_t a, timestamp_t b) {
#ifdef _WIN32
  SYSTEMTIME sta = timestamp_to_systime(a);
  SYSTEMTIME stb = timestamp_to_systime(b);

  FILETIME fta = { 0 };
  FILETIME ftb = { 0 };

  if (!SystemTimeToFileTime(&sta, &fta)) return -INT_MAX;
  if (!SystemTimeToFileTime(&stb, &ftb)) return -INT_MAX;

  LARGE_INTEGER lia = {
    .LowPart  = fta.dwLowDateTime,
    .HighPart = fta.dwHighDateTime,
  };
  LARGE_INTEGER lib = {
    .LowPart  = ftb.dwLowDateTime,
    .HighPart = ftb.dwHighDateTime,
  };

  return lia.QuadPart - lib.QuadPart;
#else
  time_t sta = timestamp_to_systime(a);
  time_t stb = timestamp_to_systime(b);

  if (sta == (time_t)-1 || stb == (time_t)-1) return -INT64_MAX;

  return (int64_t)sta - (int64_t)stb;
#endif
}

void timestamp_day_print(timestamp_t t) {
#ifdef _WIN32
  PCSTR dn[] = { "Sunday", "Monday", "Tuesday", 
  "Wednesday", "Thursday", "Friday", "Saturday" };
  printf("%s - %d/%d/%d", dn[timestamp_to_systime(t).wDayOfWeek], t.d, t.m, t.y);
#else
  time_t stt = timestamp_to_systime(t);
  struct tm *tm = localtime(&stt);

  char buf[256];
  size_t len = strftime(buf, sizeof(buf), "%A - %d/%m/%Y", tm);
  if(len > 0)
    printf("%s", buf);
#endif
}

