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
#endif
}

void timestamp_day_print(timestamp_t t) {
#ifdef _WIN32
  PCSTR dn[] = { "Sunday", "Monday", "Tuesday", 
  "Wednesday", "Thursday", "Friday", "Saturday" };
  // FIXME: only on Windows
  printf("%s - %d/%d/%d", dn[timestamp_to_systime(t).wDayOfWeek], t.d, t.m, t.y);
#endif
}

