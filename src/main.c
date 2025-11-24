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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

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
#ifdef _WIN32
  char userdir[MAX_USRDIR_PATH] = { 0 };

  if (!GetEnvironmentVariableA("USERPROFILE", userdir, sizeof(userdir))) {
    LOG_ERROR("Failed to get USERPROFILE: 0x%lX (%s)", GetLastError(), helper_win32_error_message(GetLastError()));
    return NULL;
  }
#else
  char* userdir = getenv("HOME");
  if(!userdir) return NULL;
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
#ifdef _WIN32
  // FIXME: shouldn't invert
  return (int)-timestamp_cmp(((event_t*)e1)->dtstart, ((event_t*)e2)->dtstart);
#else
  return (int) timestamp_cmp(((event_t*)e1)->dtstart, ((event_t*)e2)->dtstart);
#endif
}

int http_get(slice_t* url, sb_t* out) {
  arena_t arena = { 0 };
    
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

  const char* agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36";

  char buffer[4096];

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
    agent,
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

#else
  char* host = arena_sprintf(&arena, "%.*s", SLICE_FMT(url_structure.items[0]));
  char* obj = arena_sprintf(&arena, "%.*s", SLICE_FMT(url_structure.items[1]));

  struct sockaddr_in servaddr = { 0 };
  
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) return 1; 

  int pton_result = inet_pton(AF_INET, host, &servaddr.sin_addr);

  if (pton_result == 0) {
    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
      close(sockfd);
      LOG_ERROR("Failed to resolve host `%s`.", host);
      return 1; 
    }
    memcpy(&servaddr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
  }

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(schema ? 443 : 80);

  if (connect(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) return 1;
  SSL* ssl = NULL;
  if (schema) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
    if (!ctx) return 1;
    ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); return 1;}

    if (!SSL_set_tlsext_host_name(ssl, host)) {
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return 1;
    }

    if (!SSL_set_fd(ssl, sockfd)) return 1;
    if (!SSL_connect(ssl)) return 1;
  }

  const char* req_fmt = 
    "GET /%s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "User-Agent: %s\r\n"
    "Accept: text/calendar\r\n"
    "Cache-Control: no-cache\r\n"
    "Pragma: no-cache\r\n"
    "Connection: close\r\n"
    "\r\n";

  const char* req = arena_sprintf(&arena, req_fmt, obj, host, agent);

  switch (schema){
    case 0:
      if(send(sockfd, req, strlen(req), 0) < 0) return 1;
      break;
    case 1:
      if(SSL_write(ssl, req, strlen(req)) < 0) return 1;
      break;
  }

  // parse headers
  sb_t headers = { 0 };
  char* terminator = NULL;
  size_t headers_length = 0;
  size_t n = 0;
  do {
    switch (schema){
      case 0:
        n = recv(sockfd, buffer, sizeof(buffer), 0);
        break;
      case 1:
        n = SSL_read(ssl, buffer, sizeof(buffer));
        break;
    }
    if (n <= 0) break;

    terminator = strstr(buffer, "\r\n\r\n");
    if (terminator) {
      headers_length = (uintptr_t)terminator - (uintptr_t)buffer;
      sb_n_append(&headers, (const char*)buffer, headers_length);
    } else 
      sb_n_append(&headers, (const char*)buffer, n);
  } while (terminator == NULL);

  int content_length = 0;

  slicearr_t h_lines = { 0 };
  slice_t h_slice = { .data = headers.items, .size = headers.size };
  split(&h_slice, "\r\n", 0, &h_lines);

  slicearr_t status_line = { 0 };
  split(&h_lines.items[0], " ", 2, &status_line);
  if (status_line.count < 3) return 1;

  int status = slice_atoi(&status_line.items[1]);
  slice_t msg = status_line.items[2];

  if (status != 200) {
    LOG_ERROR("HTTP request returned: %d %.*s", status, SLICE_FMT(msg));
    return 1;
  }

  for(size_t i = 1; i < h_lines.count; i++) { 
    slice_t* l = h_lines.items + i;

    slice_trim(l);
    slicearr_t kv_pair = { 0 };
    split(l, ":", 1, &kv_pair);

    if (kv_pair.count < 2) continue;
  
    slice_trim(&kv_pair.items[0]);
    slice_trim(&kv_pair.items[1]);

    if(slice_eq(&kv_pair.items[0], "Content-Length"))
      content_length = slice_atoi(&kv_pair.items[1]);
  }

  if (content_length == 0) {
    LOG_ERROR("Could not read body");
    return 1;
  }

  sb_n_append(out, buffer + (headers_length % sizeof(buffer)), sizeof(buffer) - (headers_length % sizeof(buffer)));

  size_t to_read = content_length;
  while(to_read > 0) {
    switch (schema) {
      case 0:
        n = recv(sockfd, buffer, sizeof(buffer), 0);
        break;
      case 1:
        n = SSL_read(ssl, buffer, sizeof(buffer));
        break;
    }
    if (n <= 0) break;

    sb_n_append(out, buffer, n);

    to_read = to_read > n ? to_read - n : 0;
  }
#endif

  arena_free(&arena);

  return 0;
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
#else
  struct stat st = { 0 };

  if (!stat(dirname, &st)) return 0;
  if (mkdir(dirname, 0777)) {
    LOG_ERROR("Failed to create directory `%s`: %d (%s)", dirname, errno, strerror(errno));
    return 1;
  }
#endif
  return 0;
}

int create_file_if_not_exists(const char* filepath) {
#ifdef _WIN32
#else
  struct stat st = { 0 };

  if (!stat(filepath, &st)) return 0;

  FILE* fp = fopen(filepath, "w");
  if (!fp) {
    LOG_ERROR("Failed to create file `%s`: %d (%s)", filepath, errno, strerror(errno));
    return 1;
  }
  fclose(fp);
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

  if(create_file_if_not_exists(urls_fn)) return 1;
  if(create_file_if_not_exists(cals_fn)) return 1;

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
    // force refresh
    if(refresh(&arena, cals_fn, urls_fn)) return 1;
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
