#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <wininet.h>
#include <userenv.h>

#define OS_SEP "\\"
#else
#define OS_SEP  "/"
#endif

#define SB_IMPLEMENTATION
#include "sb.h"
#define DA_IMPLEMENTATION
#include "da.h"
#define ARENA_IMPLEMENTATION
#include "arena.h"

#include "logging.h"
#include "timestamp.h"
#include "slice.h"

#define TODAY_DIR ".today"
#define MAX_USRDIR_PATH 260

typedef struct {
  size_t dtstamp;
  const char* uid;
  timestamp_t dtstart;
  timestamp_t dtend;
  const char* cat;
  const char* summary;
  const char* location;
  const char* geo;
  const char* cal_name;
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

typedef enum {
  STATE_UNDEF = 0,
  STATE_CAL,
  STATE_EVENT,
  STATE_OTHER,
} parse_state_t;

#ifdef _WIN32
CHAR *helper_win32_error_message(DWORD err) {
  static CHAR szErrMsg[4096] = {0};

  DWORD dwErrMsgSize = FormatMessageA(
    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL,
    err,
    LANG_USER_DEFAULT,
    szErrMsg,
    4096,
    NULL
  );

  if (dwErrMsgSize == 0) {
    if (GetLastError() != ERROR_MR_MID_NOT_FOUND) {
      if (sprintf(szErrMsg, "Could not get error message for 0x%lX", err) > 0) {
        return (CHAR *)&szErrMsg;
      } 
    } else {
      if (sprintf(szErrMsg, "Invalid Windows Error code (0x%lX)", err) > 0) {
        return (CHAR *)&szErrMsg;
      }
    }
    return NULL;
  }

  while (dwErrMsgSize > 1 && isspace(szErrMsg[dwErrMsgSize - 1])) {
    szErrMsg[--dwErrMsgSize] = '\0';
  }

  return szErrMsg;
}
#endif


char* get_full_path(arena_t* arena, const char* filename) {
  char userdir[MAX_USRDIR_PATH] = { 0 };

#ifdef _WIN32
  if (!GetEnvironmentVariableA("USERPROFILE", userdir, sizeof(userdir))) {
    LOG_ERROR("Failed to get USERPROFILE: 0x%lX (%s)", GetLastError(), helper_win32_error_message(GetLastError()));
    return NULL;
  }
#endif

  return arena_sprintf(arena, "%s" OS_SEP "%s" OS_SEP "%s", userdir, TODAY_DIR, filename);
}


int parse_calendar(arena_t* arena, sb_t* cal, const char* filename, calendar_t* calendar) {
  slicearr_t lines = { 0 };

  slice_t cal_slice = { .data = cal->items, .size = cal->count };
  split(&cal_slice, "\n", 0, &lines);

  event_t e = { 0 };
  parse_state_t state = STATE_UNDEF;

  slicearr_t key_value = { 0 };

  for (size_t i = 0; i < lines.count; i++, key_value.count = 0) {
    slice_trim(&lines.items[i]);

    split(lines.items + i, ":", 1, &key_value);
    slice_t key = key_value.items[0];
    slice_t value = key_value.items[1];

    if (slice_eq(&key, "X-WR-CALNAME")) {
      calendar->name = arena_sprintf(arena, "%.*s", SLICE_FMT(value));
    } else if (slice_eq(&key, "BEGIN")) {
      if (slice_eq(&value, "VEVENT")) {
        // LOG_DEBUG("BEGIN:VEVENT");
        if (state == STATE_EVENT) {
          LOG_ERROR("%s:%zu: Unclosed event", filename, i + 1);
          return -1;
        }
        state = STATE_EVENT;
      } else if (slice_eq(&value, "VCALENDAR")) {
        if (state != STATE_UNDEF) {
          LOG_ERROR("%s:%zu: Unexpected start of calendar", filename, i + 1);
          return -1;
        }
        state = STATE_CAL;
      }else {
        state = STATE_OTHER;
      }
    } else if (slice_eq(&key, "END")) {
      if (slice_eq(&value, "VEVENT")) {
        // LOG_DEBUG("END:VEVENT");
        if (state != STATE_EVENT) {
          LOG_ERROR("%s:%zu: Closing event before BEGIN:VEVENT", filename, i + 1);
          return -1;
        }
        e.cal_name = calendar->name;
        da_append(&calendar->events, e); // copies
        memset(&e, 0, sizeof(e));
      } else if (slice_eq(&value, "VCALENDAR")) {
        if (state != STATE_CAL) {
          LOG_ERROR("%s:%zu: Unexpected end of calendar", filename, i + 1);
          return -1;
        }
        return 0;
      }

      state = STATE_CAL;
    } else if (slice_eq(&key, "SUMMARY")) {
      // LOG_DEBUG("SUMMARY");
      if (state != STATE_EVENT) {
        LOG_ERROR("%s:%zu: Summary outside of event.", filename, i + 1);
        return -1;
      }
      e.summary = arena_sprintf(arena, "%.*s", SLICE_FMT(value));
    } else if (slice_eq(&key, "DTSTART")) {
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
    } else if (slice_eq(&key, "DTEND")) {
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

char* get_cal_name(arena_t* arena, sb_t* cal) {
  char* name = NULL;
  slicearr_t lines = { 0 };

  slice_t cal_slice = { .data = cal->items, .size = cal->count };
  split(&cal_slice, "\n", 0, &lines);

  slicearr_t key_value = { 0 };

  for (size_t i = 0; i < lines.count; i++, key_value.count = 0) {
    slice_trim(&lines.items[i]);

    split(lines.items + i, ":", 1, &key_value);
    slice_t key = key_value.items[0];
    slice_t value = key_value.items[1];

    if (slice_eq(&key, "X-WR-CALNAME")) {
      name = arena_sprintf(arena, "%.*s", SLICE_FMT(value));
      break;
    }
  }

  return name;
}

int qsort_event_cmp(const void* e1, const void* e2) {
  return (int)-timestamp_cmp(((event_t*)e1)->dtstart, ((event_t*)e2)->dtstart);
}

int http_get(slice_t* url, sb_t* out) {
  out->count = 0;
  slicearr_t url_structure = { 0 };
  split(url, "//", 1, &url_structure);
  if (url_structure.count < 2) {
    LOG_ERROR("Failed to parse URL `%.*s`", SLICE_FMT(*url));
    return 1;
  }

  int schema = 0; // http = 0, https = 1;
  slice_t *url_schema = url_structure.items;
  if (slice_starts_with(url_schema, "http")) {
    schema = url_schema->size >= 5 && url_schema->data[4] == 's';
  } else {
    LOG_WARN("Unrecognized schema `%.*s`, assuming HTTP", SLICE_FMT(*url_schema));
  }

  slice_t url_path = url_structure.items[1];
  url_structure.count = 0;
  split(&url_path, "/", 1, &url_structure);

#ifdef _WIN32
  // TODO: support HTTPS
  if (schema != 0) {
    LOG_ERROR("HTTPS not supported yet, falling back to HTTP");
    schema = 0;
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
    // LOG_INFO("extended output to %zu bytes (%lu read).", out->count, dwRead);
  } while (res && dwRead > 0);

  return 0;
#else
  return 1;
#endif // platform
}

int refresh(arena_t* arena, const char* cals_path, const char* urls_path) {
  sb_t urls = { 0 };
  sb_t cals = { 0 };

  sb_t calendar = { 0 };

  if(sb_read_file(urls_path, &urls) < 0) {
    LOG_ERROR("Failed to read file `%s`", urls_path);
    return -1;
  }

  slice_t cal_slice = { .data = urls.items, .size = urls.count };
  slicearr_t lines = { 0 };
  split(&cal_slice, "\n", 0, &lines);

  for (size_t i = 0; i < lines.count; i++) {
    if (lines.items[i].size == 0) continue;
    slice_trim(&lines.items[i]);

    calendar.count = 0;

    slice_t url  = lines.items[i];

    LOG_INFO("Fetching %.*s", SLICE_FMT(url));

    if(http_get(&url, &calendar)) {
      LOG_ERROR("HTTP GET `%.*s` failed", SLICE_FMT(url));
      continue;
    }

    char* cal_name = get_cal_name(arena, &calendar);
    if (cal_name == NULL) {
      LOG_WARN("Could not find name for `%.*s`", SLICE_FMT(url));
    }
    char* cal_path = get_full_path(arena, arena_sprintf(arena, "calendars" OS_SEP "%s.ics", cal_name)); 
    sb_appendln(&cals, cal_path);
    if (sb_write_to_file(cal_path, &calendar) < 0) {
      LOG_ERROR("Failed to write to file `%s`.", cal_path);
      continue;
    }
  }

  if (sb_write_to_file(cals_path, &cals) < 0) {
    LOG_ERROR("Failed to write to file `%s`.", cals_path);
    sb_free(&urls);
    sb_free(&cals);
    return 1;
  }

  sb_free(&urls);
  sb_free(&cals);

  return 0;
}

int delete(const char* url, const char* urls_path) {
  sb_t sb = { 0 };
  sb_t urls = { 0 };

  slicearr_t lines = { 0 };
  if (sb_read_file(urls_path, &urls) > 0) {
    slice_t urls_slice = { .data = urls.items, .size = urls.count };
    split(&urls_slice, "\n", 0, &lines);
    da_foreach(slice_t, s, &lines) {
      slice_trim(s);

      if (!slice_eq(s, url)) {
        sb_appendf(&sb, "%.*s\n", SLICE_FMT(*s));
      }
    }
  }

  if (sb_write_to_file(urls_path, &sb) < 0) {
    LOG_ERROR("Failed to write to file `%s`.", urls_path);
    return 1;
  }

  return 0;
}

int add(const char* url, const char* urls_path) {
  sb_t sb = { 0 };
  sb_t urls = { 0 };

  // check for duplicates
  slicearr_t lines = { 0 };
  if (sb_read_file(urls_path, &urls) > 0) {
    slice_t urls_slice = { .data = urls.items, .size = urls.count };
    split(&urls_slice, "\n", 0, &lines);
    da_foreach(slice_t, s, &lines) {
      slice_trim(s);

      if (slice_eq(s, url)) {
        LOG_INFO("URL `%s` already added.", url);
        return 0;
      }
    }
  }

  sb_appendln(&sb, url);
  if (sb_append_to_file(urls_path, &sb) < 0) {
    LOG_ERROR("Failed to append to file `%s`.", urls_path);
    return 1;
  }

  return 0;
}

int create_dir(const char* dirname) {
#ifdef _WIN32
  if(!CreateDirectoryA(dirname, NULL)) {
    DWORD err = GetLastError();
    if (err != ERROR_ALREADY_EXISTS) { 
      LOG_ERROR("Failed to create directory `%s`: 0x%lX (%s)", dirname, err, helper_win32_error_message(err));
      return 1;
    }
  }
#endif
  return 0;
}

#define shift(argc, argv) (argc-- > 0 ? *(argv++) : NULL);

int main(int argc, char **argv) {
  arena_t arena = { 0 };
  
  if(create_dir(get_full_path(&arena, ""))) return 1;
  if(create_dir(get_full_path(&arena, "calendars"))) return 1;

  const char* urls_fn = get_full_path(&arena, "urls");
  const char* cals_fn = get_full_path(&arena, "cals");

  if (!urls_fn || !cals_fn) return 1;

  const char* program = shift(argc, argv);
  // fprintf(stderr, "%s\n", program);
  int f_help = 0;
  int f_refresh = 0;
  struct {
    int set;
    const char* url;
  } f_add = { 0 };
  struct {
    int set;
    const char* url;
  } f_del = { 0 };

  char* arg = shift(argc, argv);
  while (arg) {
    if (*arg != '-') break;
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
      f_help = 1;
    else if (strcmp(arg, "--add") == 0 || strcmp(arg, "-a") == 0) {
      f_add.url = shift(argc, argv);
      if (!f_add.url || *f_add.url == '-') {
        LOG_ERROR("Expected <url> after --add option.");
        return 1;
      }
      f_add.set = 1;
    } else if (strcmp(arg, "--delete") == 0 || strcmp(arg, "-d") == 0) {
      f_del.url = shift(argc, argv);
      if (!f_del.url || *f_del.url == '-') {
        LOG_ERROR("Expected <url> after --delete option.");
        return 1;
      }
      f_del.set = 1;
    } else if (strcmp(arg, "--refresh") == 0 || strcmp(arg, "-r") == 0) {
      f_refresh = 1;
    } else {
      LOG_ERROR("Unknown flag %s", arg);
      return 1;
    }
    arg = shift(argc, argv);
  }

  const char* format = arg;

  if (f_help) {
    fprintf(stdout, "USAGE: %s [OPTIONS] <format>\n", program);
    fprintf(stdout, "OPTIONS:\n");
    fprintf(stdout, "\t--help     -h        Shows this message and exits with 0.\n");
    fprintf(stdout, "\t--refresh  -r        Refreshes all the calendars.\n");
    fprintf(stdout, "\t--add      -a <url>  Adds <url> to the list of calendars.\n");
    fprintf(stdout, "\t--delete   -d <url>  Deletes <url> from the list of calendars.\n");
    return 0;
  }

  if (f_add.set) {
    if(add(f_add.url, urls_fn)) return 1;
  }

  if (f_del.set) {
    if(delete(f_del.url, urls_fn)) return 1;
  }

  if (f_refresh) {
    if(refresh(&arena, cals_fn, urls_fn)) return 1;
  }

  sb_t cals_fnames = { 0 };
  if(sb_read_file(cals_fn, &cals_fnames) < 0) {
   LOG_ERROR("Failed to read file `%s`", cals_fn); 
   return 1;
  }

  slicearr_t cals = { 0 };
  slice_t cals_fnames_slice = { .data = cals_fnames.items, .size = cals_fnames.count }; 
  split(&cals_fnames_slice, "\n", 0, &cals);

  eventarr_t today = { 0 };

  da_foreach(slice_t, cal_fn, &cals) {
    if (!cal_fn->data || *cal_fn->data == '\0') continue;

    sb_t cal_file = { 0 };

    const char* calname = arena_sprintf(&arena, "%.*s", SLICE_FMT(*cal_fn));
    LOG_DEBUG("Reading file %s.", calname);
    if(sb_read_file(calname, &cal_file) < 0) {
      LOG_ERROR("Failed to read file `%s`.", calname); 
      continue;
    }
    if (cal_file.count == 0 || cal_file.items == NULL) {
      LOG_ERROR("Failed to read file `%s`.", calname);
      continue;
    }

    calendar_t calendar = { 0 };

    if(parse_calendar(&arena, &cal_file, calname, &calendar)) { 
      LOG_ERROR("Failed to parse calendar %s.", calname);
      continue;
    }

    LOG_DEBUG("Calendar %s (%s), %zu total events.", calendar.name, calname, calendar.events.count);
    da_foreach(event_t, e, &calendar.events) {
      if (
        ( timestamp_cmp(e->dtstart, today_00()) > 0 &&
          timestamp_cmp(e->dtstart, today_24()) < 0 ) ||
        ( timestamp_cmp(e->dtend, today_00()) > 0 &&
          timestamp_cmp(e->dtend, today_24()) < 0)
      ) {
        da_append(&today, *e);
      }
    }
  }

  printf("Events for today, ");
  timestamp_day_print(now());
  printf(":\n");

  if (!today.items || today.count == 0) {
    printf("No events.\n");
    return 0;
  }

  // TODO: handle events spanning multiple days
  qsort(today.items, today.count, sizeof(*today.items), (int(*)(const void*, const void*))qsort_event_cmp);
  if (!arg || strcmp("list", format) == 0) {
    da_foreach(event_t, e, &today) {
      printf("[%02d:%02d - %02d:%02d] (%s) %s\n", e->dtstart.hh, e->dtstart.mm, e->dtend.hh, e->dtend.mm, e->cal_name, e->summary);
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
      printf(" (%s) %s \n",e->cal_name, e->summary);
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

  arena_free(&arena);

  return 0;
}
