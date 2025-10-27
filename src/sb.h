#ifndef SB_H
#define SB_H

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#else

#include <stdlib.h>

#endif

typedef struct {
  char*  items;
  size_t count;
  size_t size;
} sb_t;

#ifndef SB_DEFAULT_SIZE
#define SB_DEFAULT_SIZE  128
#endif

#ifndef SB_GROWTH_FACTOR
#define SB_GROWTH_FACTOR 2
#endif

#define SB_FMT(sb) (int)(sb).count, (sb).items

#define sb_reset(sb) (sb).count = 0

/*
 * Reserves required memory for the sb.
 * @param sb pointer to sb_t structure
 * @param size requesetd size to allocate
 * @return amount of size allocated.
 */
size_t sb_reserve(sb_t *sb, size_t size);
/*
 * Appends n characters to a sb.
 * @param sb pointer to sb_t structure
 * @param str const char* pointing to the string to append
 * @param len amount of chars to append to sb
 * @return If >= 0 the number of chars appended, if < 0 error.
 */
int sb_n_append(sb_t *sb, const char *str, size_t len);
/*
 * Appends a null-terminated string to sb. (null terminator
 * will get discarded)
 * @param sb pointer to sb_t structure
 * @param str const char* pointing to the string to append
 * @return If >= 0 the number of chars appended, if < 0 error.
 */
int sb_append(sb_t *sb, const char *str);
/*
 * Appends a null-terminated string to sb.
 * @param sb pointer to sb_t structure
 * @param str const char* pointing to the string to append
 * @return If >= 0 the number of chars appended, if < 0 error.
 */
int sb_appendz(sb_t *sb, const char *str);
/*
 * Appends a formatted string to sb
 * @param sb pointer to sb_t structure
 * @param fmt const char* pointing to a printf-style format string
 * @param ... variadic arguments
 * @return If >= 0 the number of chars appended, if < 0 error.
 */
int sb_appendf(sb_t *sb, const char* fmt, ...);
/*
 * Reads contents of a file into sb. Resets sb.
 * @param filename path of the file to read
 * @param sb pointer to sb_t structure that will hold
 * the file contents
 * @return If >= 0 the number of chars read, if < 0 error.
 */
int sb_read_file(const char *filename, sb_t* sb);
/*
 * Writes sb to a file
 * @param filename path of the file to write
 * @param sb pointer to sb_t structure
 * @return If >= 0 the number of chars read, if < 0 error.
 */
int sb_write_to_file(const char *filename, sb_t* sb);
/*
 * Appends sb to a file
 * @param filename path of the file to write
 * @param sb pointer to sb_t structure
 * @return If >= 0 the number of chars read, if < 0 error.
 */
int sb_append_to_file(const char *filename, sb_t* sb);
/*
 * Concats the contents of a sb to another.
 * @param a pointer to sb_t structure that will be extended
 * with the contents of b.
 * @param b pointer to sb_t structure. Will not get reset or freed.
 * @return If >= 0 the number of chars appended, if < 0 error.
 */
int sb_concat(sb_t* a, sb_t* b);
/*
 * Frees sb
 * @param sb pointer to sb_t structure
 */
void sb_free(sb_t* sb);

#endif

#ifdef SB_IMPLEMENTATION

size_t sb_reserve(sb_t *sb, size_t size) {
  if (size < sb->size) return 0;
  // ensure nearest power of two
  if (size && (!(size & (size - 1))) == 0) {
    size--;
    for (short i = 1; i <= 32; i <<= 1)
      size |= size >> i;
    size++;
  }

#ifdef _WIN32
    sb->items = sb->items
                ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sb->items, size)
                : HeapAlloc(GetProcessHeap(),   HEAP_ZERO_MEMORY, size);
#else
  sb->items = realloc(sb->items, size);
#endif

  if (!sb->items) return 0;
  sb->size = size;
  return size;
}

int sb_n_append(sb_t *sb, const char *str, size_t len) {
  if (sb->size == 0) {
    sb_reserve(sb, SB_DEFAULT_SIZE);
    if (sb->items == NULL) return -1;
  }
  while (len + sb->count >= sb->size) // = avoids realloc at next call
    sb->size *= SB_GROWTH_FACTOR;
  sb_reserve(sb, sb->size);
  if (sb->items == NULL) return -1;
#ifdef _WIN32
  CopyMemory(sb->items + sb->count, str, len);
#else
  memcpy(sb->items + sb->count, str, len);
#endif

  sb->count += len;
  return (int)len;
}

// NOTE: discards the null terminator
int sb_append(sb_t *sb, const char *str) {
  return sb_n_append(sb, str, strlen(str));
}

int sb_appendln(sb_t *sb, const char *str) {
  int l = sb_n_append(sb, str, strlen(str) + 1);
  sb->items[sb->count-1] = '\n';
  return l;
}

int sb_appendz(sb_t *sb, const char *str) {
  return sb_n_append(sb, str, strlen(str) + 1);
}

int sb_appendf(sb_t *sb, const char* fmt, ...) {
  va_list args, args_copy = { 0 };

  va_start(args, fmt);
  va_copy(args_copy, args);

#ifdef _WIN32
  int n = _vscprintf(fmt, args);
#else
  int n = vsnprintf(NULL, 0, fmt, args);
#endif
  va_end(args);

  if (n < 0) {
    va_end(args_copy);
    return n;
  }

  sb_reserve(sb, sb->count + n + 1);
  char *dest = sb->items + sb->count;

  int len = vsnprintf(dest, n + 1, fmt, args_copy);
  va_end(args_copy);

  if (len > 0) sb->count += len;
  return len;
}

// NOTE: resets the sb and overwrites items
int sb_read_file(const char *filename, sb_t* sb) {
  int read = 0;

#ifdef _WIN32
  ZeroMemory(sb, sizeof(*sb));
#else
  memset(sb, 0, sizeof(*sb));
#endif

#ifdef _WIN32
  HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (hFile == INVALID_HANDLE_VALUE) return -1;

  LARGE_INTEGER liFileSize = { 0 };
  if (!GetFileSizeEx(hFile, &liFileSize)) return -1;

  if (sb_reserve(sb, liFileSize.QuadPart * sizeof(*sb->items)) == 0) return -1;
  if (!ReadFile(hFile, (LPVOID)sb->items, (DWORD)sb->size, (LPDWORD)&read, NULL)) {
    sb_free(sb);
    return -1;
  }

  CloseHandle(hFile);
#else
  FILE* fp = fopen(filename, "r");
  if (!fp) return -1;

  fseek(fp , 0 , SEEK_END);
  size_t size = ftell(fp);
  rewind(fp);

  sb_reserve(sb, size);
  read = fread(sb->items, sizeof(*sb->items), size, fp);

  fclose(fp);
#endif

  sb->count = read;
  return read;
}

int sb_write_to_file(const char *filename, sb_t* sb) {
  int written = 0;

#ifdef _WIN32
  DeleteFileA(filename);
  HANDLE hFile = CreateFileA(filename, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0, NULL);
  if (hFile == INVALID_HANDLE_VALUE) return -1;

  if (!WriteFile(hFile, (LPCVOID)sb->items, (DWORD)sb->count, (LPDWORD)&written, NULL)) return -1;

  CloseHandle(hFile);
#else
  FILE* fp = NULL;
  fp = fopen(filename, "w");
  if (!fp) return -1;

  written = fprintf(fp, "%.*s", SB_FMT(*sb));

  fclose(fp);
#endif
  return written;
}


int sb_append_to_file(const char *filename, sb_t* sb) {
  int written = 0;

#ifdef _WIN32
  HANDLE hFile = CreateFileA(filename, FILE_GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) return -1;

  if (!WriteFile(hFile, (LPCVOID)sb->items, (DWORD)sb->count, (LPDWORD)&written, NULL)) return -1;

  CloseHandle(hFile);
#else
  FILE* fp = NULL;
  fp = fopen(filename, "a");
  if (!fp) return -1;

  written = fprintf(fp, "%.*s", SB_FMT(*sb));

  fclose(fp);
#endif
  return written;
}

int sb_concat(sb_t* a, sb_t* b) {
  return sb_n_append(a, b->items, b->count);
}

void sb_free(sb_t* sb) {
#ifdef _WIN32
  HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, sb->items);
  ZeroMemory(sb, sizeof(*sb));
#else
  free(sb->items);
  memset(sb, 0, sizeof(*sb));
#endif
}

#endif
