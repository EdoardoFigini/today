#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <wininet.h>

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
  const char* name;
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
  STATE_OTHER,
} parse_state_t;

void split(slice_t* s, const char* sep, unsigned int limit, slicearr_t* sa) {
  if (!s || !s->data) return;
  if (!sa) return;
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

// TODO: retrieve calendar name from X-WR-CALNAME, not user-defined.
// this would mean having a calendars file which is just a list of links, no names
int parse_calendar(arena_t* arena, sb_t* cal, const char* filename, calendar_t* calendar) {
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

    if (memcmp(key.data, "X-WR-CALNAME", key.size ) == 0) {
      calendar->name = arena_sprintf(arena, "%.*s", SLICE_FMT(value));
    } else if (memcmp(key.data, "BEGIN", key.size ) == 0) {
      if (memcmp(value.data, "VEVENT", value.size) == 0) {
        // LOG_DEBUG("BEGIN:VEVENT");
        if (state == STATE_EVENT) {
          LOG_ERROR("%s:%zu: Unclosed event", filename, i + 1);
          return -1;
        }
        state = STATE_EVENT;
      } else {
        state = STATE_OTHER;
      }
    } else if (memcmp(key.data, "END", key.size) == 0) {
      if (memcmp(value.data, "VEVENT", value.size) == 0) {
        // LOG_DEBUG("END:VEVENT");
        if (state != STATE_EVENT) {
          LOG_ERROR("%s:%zu: Closing event before BEGIN:VEVENT", filename, i + 1);
          return -1;
        }
        state = STATE_CAL;
        da_append(&calendar->events, e);
        memset(&e, 0, sizeof(e));
      }
    } else if (memcmp(key.data, "SUMMARY", key.size) == 0) {
      // LOG_DEBUG("SUMMARY");
      if (state != STATE_EVENT) {
        LOG_ERROR("%s:%zu: Summary outside of event.", filename, i + 1);
        return -1;
      }
      e.summary = arena_sprintf(arena, "%.*s", SLICE_FMT(value));
    } else if (memcmp(key.data, "DTSTART", key.size) == 0) {
      // LOG_DEBUG("SUMMARY");
      if (state == STATE_OTHER) continue;
      if (state != STATE_EVENT) {
        LOG_ERROR("%s:%zu: Start time outside of event.", filename, i + 1);
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
      if (state == STATE_OTHER) continue;
      if (state != STATE_EVENT) {
        LOG_ERROR("%s:%zu: End time outside of event.", filename, i + 1);
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
  PCSTR dn[] = { "Sunday", "Monday", "Tuesday", 
  "Wednesday", "Thursday", "Friday", "Saturday" };
  // FIXME: only on Windows
  printf("%s - %d/%d/%d", dn[timestamp_to_systime(t).wDayOfWeek], t.d, t.m, t.y);
}

int qsort_event_cmp(const void* e1, const void* e2) {
  return (int)-timestamp_cmp(((event_t*)e1)->dtstart, ((event_t*)e2)->dtstart);
}

int http_get(slice_t* url, sb_t* out) {
  out->count = 0;
  slicearr_t url_structure = { 0 };
  split(url, "//", 1, &url_structure);
  if (url_structure.count < 2) {
    fprintf(stderr, "[ERR ] Failed to parse URL `%.*s`\n", SLICE_FMT(*url));
    return 1;
  }

  int schema = 0; // http = 0, https = 1;
  if (memcmp(url_structure.items[0].data, "http", 4)) {
    fprintf(stderr, "[WARN] No schema specified in url, assuming HTTP\n");
  } else {
    if (memcmp(url_structure.items[0].data, "https", 5) == 0) {
      schema = 1;
    } else if (memcmp(url_structure.items[0].data, "http", 4) == 0) {
      schema = 0;
    } else {
      fprintf(stderr, "Invalid schema %.*s", SLICE_FMT(url_structure.items[0]));
      return 1;
    }
  }

  slice_t url_path = url_structure.items[1];
  url_structure.count = 0;
  split(&url_path, "/", 1, &url_structure);

#ifdef _WIN32
  if (schema != 0) {
    fprintf(stderr, "[ERR ] HTTPS not supported yet\n");
    return 1;
  }

  LPCSTR lpszHost = LocalAlloc(LPTR, url_structure.items[0].size + 1);
  CopyMemory((LPVOID)lpszHost, url_structure.items[0].data, url_structure.items[0].size);

  LPCSTR lpszObj = LocalAlloc(LPTR, url_structure.items[1].size + 1);
  CopyMemory((LPVOID)lpszObj, url_structure.items[1].data, url_structure.items[1].size);

  HINTERNET hInternet = NULL;
  HINTERNET hConnect  = NULL;
  HINTERNET hRequest  = NULL;

  hInternet = InternetOpenA(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36",
    INTERNET_OPEN_TYPE_DIRECT,
    NULL,
    NULL,
    0
  );
  if (hInternet == NULL) return 1;

  hConnect = InternetConnectA(
    hInternet,
    lpszHost,
    schema ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT,
    NULL,
    NULL,
    INTERNET_SERVICE_HTTP,
    INTERNET_FLAG_NO_CACHE_WRITE,
    0
  );
  if (hConnect == NULL) return 1;

  hRequest = HttpOpenRequestA(
    hConnect,
    "GET",
    lpszObj,
    NULL,
    NULL,
    (PCTSTR[]){"text/calendar", NULL},
    INTERNET_FLAG_NO_CACHE_WRITE,
    0
  );
  if (hRequest == NULL) return 1;

  BOOL res = HttpSendRequestA(
    hRequest,
    NULL,
    (DWORD)-1,
    NULL,
    0
  );
  if (!res) return 1;

  BYTE buffer[4096];
  DWORD dwRead = 0;
  do {
    dwRead = 0;
    res = InternetReadFile(
      hRequest,
      buffer,
      sizeof(buffer)/sizeof(*buffer),
      (LPDWORD)&dwRead
    );
    if (!res) return 1;

    sb_n_append(out, (const char*)buffer, dwRead);
    fprintf(stderr, "[INFO] extended output to %zu bytes (%lu read).\n", out->count, dwRead);
  } while (res && dwRead > 0);

  return 0;
#else
  return 1;/
#endif // platform
}

#define shift(argc, argv) (argc-- > 0 ? *(argv++) : NULL);

int main(int argc, char **argv) {
  sb_t cal_file = { 0 };
  arena_t arena = { 0 };

  const char* program = shift(argc, argv);
  fprintf(stderr, "%s\n", program);
  int help = 0;
  int refresh = 0;
  struct {
    int set;
    const char* url;
    const char* name;
  } add = { 0 };

  char* arg = shift(argc, argv);
  while (arg) {
    if (*arg != '-') break;
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
      help = 1;
    else if (strcmp(arg, "--add") == 0 || strcmp(arg, "-a") == 0) {
      add.url = shift(argc, argv);
      if (!add.url) {
        fprintf(stderr, "Expected <url> after --add option.\n");
        return 1;
      }
      add.name = shift(argc, argv);
      if (!add.name) {
        fprintf(stderr, "Expected <name> after --add option.\n");
        return 1;
      }
      add.set = 1;
    } else if (strcmp(arg, "--refresh") == 0 || strcmp(arg, "-r") == 0) {
      refresh = 1;
    } else {
      fprintf(stderr, "Unknown flag %s", arg);
      return 1;
    }
    arg = shift(argc, argv);
  }

  const char* format = arg;

  if (help) {
    fprintf(stdout, "USAGE: %s [OPTION] <format>\n", program);
    fprintf(stdout, "OPTIONS:\n");
    fprintf(stdout, "\t--help     -h               Shows this message and exits with 0.\n");
    fprintf(stdout, "\t--refresh  -r               Refreshes all the calendars.\n");
    fprintf(stdout, "\t--add      -a <url> <name>  Adds <url> to the list of calendars.\n");
    return 0;
  }

  if (refresh) {
    sb_t urls = { 0 };
    const char* filename = "calendars.csv";
    sb_read_file(filename, &urls);
    slice_t cal_slice = { .data = urls.items, .size = urls.count };
    slicearr_t lines = { 0 };
    split(&cal_slice, "\r\n", 0, &lines);
    fprintf(stderr, "%zu\n", lines.count);

    slicearr_t csv_line = { 0 };
    // skip title line
    for (size_t i = 1; i < lines.count; i++) {
      // fprintf(stderr, "line %zu: %*s, %zu\n", i+1, SLICE_FMT(lines.items[i]), lines.items[i].size);
      if (lines.items[i].size == 0) continue;

      csv_line.count = 0;
      cal_file.count = 0;

      split(&lines.items[i], ", ", 0, &csv_line);
      if (csv_line.count < 2 ) {
        fprintf(stderr, "[ERR ] %s:%zu: Could not parse CSV line.\n", filename, i+1);
        continue;
      }
      slice_t name = csv_line.items[0];
      slice_t url  = csv_line.items[1];
      if(http_get(&url, &cal_file)) {
        fprintf(stderr, "[ERR ] HTTP GET `%.*s` failed\n", SLICE_FMT(url));
        continue;
      }
      fprintf(stderr, "[INFO] Writing %zu bytes to cache.\n", cal_file.count);
      sb_write_file(arena_sprintf(&arena, "%.*s.ics", SLICE_FMT(name)), &cal_file);
    }

    return 0;
  }

  if (add.set) {
    // TODO: add to calendars.csv
    return 1;
  }

  calendar_t calendar = { 0 };

  // TODO: foreach entry in calendars.csv
  const char* calname = "iphone.ics";
  sb_read_file(calname, &cal_file);

  if(parse_calendar(&arena, &cal_file, calname, &calendar)) {
    return 1;
  }

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

  printf("Events for today, ");
  timestamp_day_print(now());
  printf("\n");

  if (!today.items || today.count == 0) {
    printf("No events.\n");
    return 0;
  }

  // TODO: handle events spanning multiple days
  qsort(today.items, today.count, sizeof(*today.items), (int(*)(const void*, const void*))qsort_event_cmp);
  if (!arg || strcmp("list", format) == 0) {
    da_foreach(event_t, e, &today) {
      printf("[%02d:%02d - %02d:%02d] %s (%s)\n", e->dtstart.hh, e->dtstart.mm, e->dtend.hh, e->dtend.mm, e->summary, calendar.name);
    }
  } else if (strcmp("table", format) == 0) {

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
