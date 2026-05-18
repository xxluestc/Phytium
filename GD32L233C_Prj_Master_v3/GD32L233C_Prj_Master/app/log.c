#include "log.h"

#ifndef LOG_DISABLE

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

static LogLevel_t g_log_level = LOG_LEVEL_DEBUG;

static const char *log_level_str[] = {
    "[DEBUG] ",
    "[INFO]  ",
    "[WARN]  ",
    "[ERROR] "
};

static void log_print(LogLevel_t level, const char *format, va_list args)
{
    if (level < g_log_level) {
        return;
    }

    uint32_t timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t ms = timestamp % 1000;
    uint32_t sec = (timestamp / 1000) % 60;
    uint32_t min = (timestamp / 60000) % 60;
    uint32_t hour = (timestamp / 3600000) % 24;

    printf("[%02u:%02u:%02u.%03u] %s", hour, min, sec, ms, log_level_str[level]);
    vprintf(format, args);
    printf("\r\n");
}

void log_init(LogLevel_t level)
{
    g_log_level = level;
}

void log_set_level(LogLevel_t level)
{
    g_log_level = level;
}

void log_debug(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_print(LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

void log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_print(LOG_LEVEL_INFO, format, args);
    va_end(args);
}

void log_warn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_print(LOG_LEVEL_WARN, format, args);
    va_end(args);
}

void log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_print(LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

#endif /* LOG_DISABLE */
