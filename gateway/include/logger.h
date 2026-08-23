#ifndef _LOGGER_H
#define _LOGGER_H

#include <stdio.h>
#include <stdarg.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level;

#define CURRENT_LOG_LEVEL LOG_LEVEL_DEBUG


int log_init(const char *path_file_log);

void log_close(void);

void log_message(log_level level, const char* file, int line, const char* func, const char* fmt, ...);

#define LOG_DEBUG(fmt, ...) \
    do { if (CURRENT_LOG_LEVEL <= LOG_LEVEL_DEBUG) \
            log_message(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); } while (0)

#define LOG_INFO(fmt, ...) \
    do { if (CURRENT_LOG_LEVEL <= LOG_LEVEL_INFO) \
            log_message(LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); } while(0)

#define LOG_WARN(fmt, ...) \
    do { if (CURRENT_LOG_LEVEL <= LOG_LEVEL_WARN) \
            log_message(LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); } while (0)


#define LOG_ERROR(fmt, ...) \
    do { if (CURRENT_LOG_LEVEL <= LOG_LEVEL_ERROR) \
            log_message(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); } while(0)



#endif 