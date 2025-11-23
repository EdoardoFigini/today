#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define __LOG_SEP "\\"
#else
#define __LOG_SEP "/"
#endif

#define __FILENAME__ strrchr(__LOG_SEP __FILE__, __LOG_SEP[0]) + 1

#define __LOG_LVL_FATAL 4
#define __LOG_LVL_ERROR 3
#define __LOG_LVL_WARN  2
#define __LOG_LVL_INFO  1
#define __LOG_LVL_DEBUG 0

#define __LOG_ICON_FATAL "-"
#define __LOG_ICON_ERROR "-"
#define __LOG_ICON_WARN  "!"
#define __LOG_ICON_INFO  "i"
#define __LOG_ICON_DEBUG "*"

#ifdef LOG_NOCOLOR
#define __LOG_COLOR_RESET
#define __LOG_COLOR_FATAL
#define __LOG_COLOR_ERROR
#define __LOG_COLOR_WARN
#define __LOG_COLOR_INFO
#define __LOG_COLOR_DEBUG
#else
#define __LOG_COLOR_RESET "\033[0m"
#define __LOG_COLOR_FATAL "\033[41m"
#define __LOG_COLOR_ERROR "\033[31m"
#define __LOG_COLOR_WARN  "\033[33m"
#define __LOG_COLOR_INFO  "\033[36m"
#define __LOG_COLOR_DEBUG "\033[37m"
#endif

#ifdef LOG_FILE_LOC
#define __LOG(lvl, fmt, ...) fprintf(__LOG_LVL_ ## lvl <= __LOG_LVL_INFO ? stdout : stderr, __LOG_COLOR_ ## lvl "[" __LOG_ICON_ ## lvl "] " fmt "\n    at %s:%d" __LOG_COLOR_RESET "\n", ##__VA_ARGS__, __FILENAME__, __LINE__);
#else
#define __LOG(lvl, fmt, ...) fprintf(__LOG_LVL_ ## lvl <= __LOG_LVL_INFO ? stdout : stderr, __LOG_COLOR_ ## lvl "[" __LOG_ICON_ ## lvl "] " fmt __LOG_COLOR_RESET "\n", ##__VA_ARGS__);
#endif

#define LOG_FATAL(fmt, ...) __LOG(FATAL, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) __LOG(ERROR, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  __LOG(WARN, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  __LOG(INFO,  fmt, ##__VA_ARGS__)
#ifdef NDEBUG
#define LOG_DEBUG(fmt, ...)
#else
#define LOG_DEBUG(fmt, ...) __LOG(DEBUG, fmt, ##__VA_ARGS__)
#endif

#endif // LOGGING_H
