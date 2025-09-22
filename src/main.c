#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>

#define TO_SECS(x) (x) / ( 1000 * 1000 * 10 )
#endif

#define SB_IMPLEMENTATION
#include "sb.h"
#define DA_IMPLEMENTATION
#include "da.h"
#define ARENA_IMPLEMENTATION
#include "arena.h"

#include "logging.h"

typedef struct {
  int y;
  int m;
  int d;
  int hh;
  int mm;
  int ss;
  char tz;
} timestamp_t;

typedef struct {
  size_t dtstamp;
  const char* uid;
  timestamp_t dtstart;
  timestamp_t dtend;
  const char* cat;
  const char* summary;
  const char* location;
  const char* geo;
} event_t;

typedef struct {
  event_t *items;
  size_t count;
  size_t capacity;
} eventarr_t;

typedef struct {
  // const char* version;
  eventarr_t events;
} calendar_t;

typedef struct {
  char* data;
  size_t size;
} slice_t;

#define SLICE_FMT(s) (int)(s).size, (s).data

typedef struct {
  slice_t* items;
  size_t count;
  size_t capacity;
} slicearr_t;

typedef enum {
  STATE_CAL = 0,
  STATE_EVENT,
} parse_state_t;

void split(slice_t* s, const char* sep, unsigned int limit, slicearr_t* sa) {
  size_t start = 0;
  size_t sep_len = strlen(sep);
  for (size_t i=0; i < s->size && (limit == 0 || sa->count < limit); i++) {
    if (s->size - i >= sep_len && memcmp(s->data + i, sep, sep_len) == 0) {
      da_append(sa, ((slice_t){ .data = s->data + start, .size = i - start }));
      i += sep_len - 1;
      start = i + 1;
    }
  }
  da_append(sa, ((slice_t){ .data = s->data + start, .size = s->size - start }));
}

int sized_atoi(const char* data, size_t size) {
  int n = 0;
  int sign = 1;
  if (size == 0) return 0;
  for (size_t i=0; i < size; i++, data++) {
    if (i==0 && *data == '-') { sign = -1; continue; }
    if (!isdigit(*data)) return 0;
    n = (n * 10) + (*data - '0');
  }
  return n * sign;
}

int slice_atoi(slice_t *s) {
  return sized_atoi(s->data, s->size);
}

#define slice_starts_with(s, str) \
  (strlen(str) <= s->size && memcmp(s->data, str, strlen(str)) == 0)

int parse_calendar(arena_t* arena, sb_t* cal, calendar_t* calendar) {
  slicearr_t lines = { 0 };

  slice_t cal_slice = { .data = cal->items, .size = cal->count };
  split(&cal_slice, "\r\n", 0, &lines);

  event_t e = { 0 };
  parse_state_t state = STATE_CAL;

  slicearr_t key_value = { 0 };

  for (size_t i = 0; i < lines.count; i++, key_value.count = 0) {
  // for (size_t i = 0; i < 25; i++, key_value.count = 0) {
    split(lines.items + i, ":", 1, &key_value);
    slice_t key = key_value.items[0];
    slice_t value = key_value.items[1];
    // LOG_DEBUG("-%.*s -> %.*s-", SLICE_FMT(key), SLICE_FMT(value));

    if (memcmp(key.data, "BEGIN", key.size ) == 0) {
      if (memcmp(value.data, "VEVENT", value.size) == 0) {
        // LOG_DEBUG("BEGIN:VEVENT");
        if (state == STATE_EVENT) {
          LOG_ERROR("Unclosed event");
          return -1;
        }
        state = STATE_EVENT;
      }
    } else if (memcmp(key.data, "END", key.size) == 0) {
      if (memcmp(value.data, "VEVENT", value.size) == 0) {
        // LOG_DEBUG("END:VEVENT");
        if (state != STATE_EVENT) {
          LOG_ERROR("Closing event before BEGIN:VEVENT");
          return -1;
        }
        state = STATE_CAL;
        da_append(&calendar->events, e);
        memset(&e, 0, sizeof(e));
      }
    } else if (memcmp(key.data, "SUMMARY", key.size) == 0) {
      // LOG_DEBUG("SUMMARY");
      if (state != STATE_EVENT) {
        LOG_ERROR("Summary outside of event.");
        return -1;
      }
      e.summary = arena_sprintf(arena, "%.*s", SLICE_FMT(value));
    } else if (memcmp(key.data, "DTSTART", key.size) == 0) {
      // LOG_DEBUG("SUMMARY");
      if (state != STATE_EVENT) {
        LOG_ERROR("Start time outside of event.");
        return -1;
      }
      e.dtstart = (timestamp_t){
        .y  = sized_atoi(value.data,      4),
        .m  = sized_atoi(value.data + 4,  2),
        .d  = sized_atoi(value.data + 6,  2),
        .hh = sized_atoi(value.data + 9,  2),
        .mm = sized_atoi(value.data + 11, 2),
        .ss = sized_atoi(value.data + 13, 2),
      };
    } else if (memcmp(key.data, "DTEND", key.size) == 0) {
      // LOG_DEBUG("SUMMARY");
      if (state != STATE_EVENT) {
        LOG_ERROR("End time outside of event.");
        return -1;
      }
      e.dtend = (timestamp_t){
        .y  = sized_atoi(value.data,      4),
        .m  = sized_atoi(value.data + 4,  2),
        .d  = sized_atoi(value.data + 6,  2),
        .hh = sized_atoi(value.data + 9,  2),
        .mm = sized_atoi(value.data + 11, 2),
        .ss = sized_atoi(value.data + 13, 2),
      };
    }
  }

  return 0;
}

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
  return (SYSTEMTIME) {
    .wYear = (WORD)t.y,
    .wMonth = (WORD)t.m,
    .wDay = (WORD)t.d,
    .wHour = (WORD)t.hh,
    .wMinute = (WORD)t.mm,
    .wSecond = (WORD)t.ss,
    .wMilliseconds = 0,
  };
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

int qsort_event_cmp(const void* e1, const void* e2) {
  return (int)-timestamp_cmp(((event_t*)e1)->dtstart, ((event_t*)e2)->dtstart);
}

int main(int argc, char **argv) {
  sb_t cal_file = { 0 };
  arena_t arena = { 0 };

  calendar_t calendar = { 0 };

  sb_read_file("test.ics", &cal_file);

  parse_calendar(&arena, &cal_file, &calendar);

  eventarr_t today = { 0 };

  da_foreach(event_t, e, &calendar.events) {
    // LOG_INFO("%s -> %lld", e->summary, timestamp_cmp(e->dtstart, today_00()));
    // LOG_INFO("%02d/%02d/%04d", e->dtstart.d, e->dtstart.m, e->dtstart.y);
    if (
      ( timestamp_cmp(e->dtstart, today_00()) > 0 &&
        timestamp_cmp(e->dtstart, today_24()) < 0 ) ||
      ( timestamp_cmp(e->dtend, today_00()) > 0 &&
        timestamp_cmp(e->dtend, today_24()) < 0)
    ) {
      da_append(&today, *e);
      // LOG_INFO("%s", e->summary);
    }
  }

  // TODO: handle events spanning multiple days
  qsort(today.items, today.count, sizeof(*today.items), (int(*)(const void*, const void*))qsort_event_cmp);
  if (!argc || strcmp("list", argv[1]) == 0) {
    da_foreach(event_t, e, &today) {
      printf("[%02d:%02d - %02d:%02d] %s\n", e->dtstart.hh, e->dtstart.mm, e->dtend.hh, e->dtend.mm, e->summary);
    }
  } else if (strcmp("table", argv[1]) == 0) {

    event_t *first = today.items;
    for (;; first++) {
      if (timestamp_cmp(first->dtstart, today_00()) > 0) break;
    }
    event_t *last = today.items;
    da_foreach(event_t, ev, &today) {
      if (timestamp_cmp(today_24(), ev->dtend) > 0 && timestamp_cmp(ev->dtend, last->dtend) > 0) last = ev;
    }

    size_t h_start = first->dtstart.hh;
    size_t h_end   = last->dtend.hh + (last->dtend.mm > 0);

    size_t h_diff = h_end - h_start;
    size_t space = 100 / h_diff;

    timestamp_t n = now();
    uint64_t now_h = (n.hh * space)   + (n.mm / (60 / space));

    for (size_t i = h_start * space; i <= h_end * space; i++ ) {
      if (i == now_h) {
        printf("|");
      } else if (i % space == 0) {
        printf("%02zu", i / space);
      } else if (i % space == 1) {
        (void)0;
      } else {
        printf("-");
      }
    }
    printf("\n");

    da_foreach(event_t, e, &today) {
      for (size_t i = h_start * space; i <= h_end * space; i++ ) {
        uint64_t start = (e->dtstart.hh * space) + (e->dtstart.mm / (60 / space));
        uint64_t end   = (e->dtend.hh * space)   + (e->dtend.mm / (60 / space));

        if (i == now_h) {
          printf("|");
        } else if (i == start) {
          printf("[");
        } else if (i > start && i < end) {
          printf("x");
        } else if (i == end) {
          printf("]");
        } else {
            if (i % space == 0) {
              printf("|");
          } else {
            printf(".");
          }
        }
      }
      printf(" %s\n", e->summary);
    }

    for (size_t i = h_start * space; i <= h_end * space; i++ ) {
      if (i == now_h) {
        printf("|");
      } else if (i % space == 0) {
        printf("%02zu", i / space);
      } else if (i % space == 1) {
        (void)0;
      } else {
        printf("-");
      }
    }
  }

  return 0;
}
