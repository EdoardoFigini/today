#include "slice.h"

#include <string.h>
#include <ctype.h>

#include "da.h"

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

void slice_trim_start(slice_t* s) {
  int i = 0;
  while(i < s->size && isspace(s->data[i])) {
    s->size--;
    s->data++;
  }
}

void slice_trim_end(slice_t* s) {
  while(s->size > 0 && isspace(s->data[s->size - 1])) {
    s->size--;
  }
}

void slice_trim(slice_t* s) {
  slice_trim_start(s);
  slice_trim_end(s);
}
