#ifndef ARENA_H
#define ARENA_H


#define DEFAULT_BLOCK_SIZE 8*1024

#define BLOCK_CAN_ALLOC(block_ptr, bytes) (block_ptr != NULL && block_ptr->size + bytes < block_ptr->capacity)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define platform_alloc(size) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)
#define platform_free(ptr) HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, ptr)
#define platform_memcopy(dest, src, size) CopyMemory(dest, src, size)
#else
#include <stdlib.h>

#define platform_alloc(size) malloc(size)
#define platform_free(ptr) free(ptr)
#define platform_memcopy(dest, src, size) memcpy(dest, src, size)
#endif

#include <stdarg.h>

typedef struct _block {
  unsigned char data[DEFAULT_BLOCK_SIZE];
  size_t size;
  size_t capacity;
  struct _block *next;
} block_t;

typedef struct {
  block_t *head;
  block_t *current;
  size_t block_count;
} arena_t;

void* arena_alloc(arena_t*, size_t);
void arena_free(arena_t*);
char* arena_strdup(arena_t*, const char*);

#ifdef ARENA_IMPLEMENTATION

void* arena_alloc(arena_t* a, size_t bytes) {
  if (!a) return NULL;
  if (bytes > DEFAULT_BLOCK_SIZE) return NULL; 

  // new block
  if (a->current == NULL || !BLOCK_CAN_ALLOC(a->current, bytes)) {
    block_t *prev = a->current; 
    a->current = platform_alloc(sizeof(block_t));

#if defined(ZERO_MEM) && !defined(_WIN32) // windows defaults to HEAP_ZERO_MEMORY
    memset(a->current, 0, sizeof(block_t));
#endif

    a->current->size = 0;
    a->current->capacity = DEFAULT_BLOCK_SIZE;
    a->current->next = NULL;
    if (a->head == NULL) 
      a->head = a->current;
    else
      prev->next = a->current;

    a->block_count++;
  }

  size_t free_idx = a->current->size;

  a->current->size += bytes;
  
  return (void*)(a->current->data + free_idx);
}

void arena_free(arena_t *a) {
  if (!a) return;
  if (a->head == NULL) return;

  block_t* tmp;
  block_t* next;

  for (tmp = a->head; tmp != NULL; tmp = next) {
    next = tmp->next;
    platform_free(tmp);
  }

  a->head = NULL;
  a->current = NULL;
  a->block_count = 0;
}

char* arena_strdup(arena_t* a, const char* str) {
  size_t len = strlen(str) + 1;
  char* copy = (char*)arena_alloc(a, len);
  if (!copy) return NULL;

  platform_memcopy(copy, str, len);
  
  return copy;
}

char* arena_sprintf(arena_t* a, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  va_list args_copy;
  va_copy(args_copy, args);
  int len = vsnprintf(NULL, 0, fmt, args_copy);
  va_end(args_copy);

  if (len < 0) {
    va_end(args);
    return NULL;
  }

  char* buffer = (char*)arena_alloc(a, (size_t)len + 1);
  if (!buffer) {
    va_end(args);
    return NULL;
  }

  vsnprintf(buffer, len + 1, fmt, args);
  va_end(args);

  return buffer;
}

#endif // !ARENA_IMPLEMENTATION
#endif // !ARENA_H
