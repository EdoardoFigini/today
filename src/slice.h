#ifndef _SLICE_H
#define _SLICE_H

#include <stddef.h>

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

#define slice_starts_with(s, str) \
  (strlen(str) <= (s)->size && memcmp((s)->data, str, strlen(str)) == 0)

#define slice_eq(s, str) \
  ((s)->size > 0 && memcmp((s)->data, str, (s)->size) == 0)

void split(slice_t* s, const char* sep, unsigned int limit, slicearr_t* sa);
int sized_atoi(const char* data, size_t size);
int slice_atoi(slice_t *s);
void slice_trim_start(slice_t* s);
void slice_trim_end(slice_t* s);
void slice_trim(slice_t* s);

#endif
