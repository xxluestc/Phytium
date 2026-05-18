#ifndef __LOG_H
#define __LOG_H

#include <stdint.h>

//#define LOG_DISABLE                          /* 注释此行以启用日志 */

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE
} LogLevel_t;

#ifdef LOG_DISABLE
#define log_init(level)         ((void)0)
#define log_set_level(level)    ((void)0)
#define log_debug(...)          ((void)0)
#define log_info(...)           ((void)0)
#define log_warn(...)           ((void)0)
#define log_error(...)          ((void)0)
#else
void log_init(LogLevel_t level);
void log_set_level(LogLevel_t level);
void log_debug(const char *format, ...);
void log_info(const char *format, ...);
void log_warn(const char *format, ...);
void log_error(const char *format, ...);
#endif

#endif
