#include "logger.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

static FILE *g_log_file = NULL;

static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char* level_names[] = { "DEBUG", "INFO ", "WARN ", "ERROR" };

int log_init(const char *path_file_log)
{
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file != NULL) {
        pthread_mutex_unlock(&g_log_mutex);

        return 0;
    }

    g_log_file = fopen(path_file_log, "a");
    if (g_log_file == NULL) {
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }

    pthread_mutex_unlock(&g_log_mutex);
    
    return 0;
}

void log_close(void)
{
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file != NULL) {
        fclose(g_log_file);
    }
    pthread_mutex_unlock(&g_log_mutex);
}



void log_message(log_level level, const char* file, int line, const char* func, const char* fmt, ...)
{
    struct tm t;

    time_t now = time(NULL);
    localtime_r(&now, &t);

    const char *short_file = strrchr(file, '/');
    short_file = (short_file != NULL) ? short_file + 1 : file;

    pthread_mutex_lock(&g_log_mutex);

    fprintf(stderr, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s:%d -> %s()] ",
            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
            t.tm_hour, t.tm_min, t.tm_sec,
            level_names[level],
            short_file, line, func);
    va_list args_console;
    va_start(args_console, fmt);
    vfprintf(stderr, fmt, args_console);
    va_end(args_console);
    fprintf(stderr, "\n");

    if (g_log_file != NULL) {
        fprintf(g_log_file, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] [%s:%d -> %s()] ",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec,
                level_names[level],
                short_file, line, func);
        
        va_list args_file;
        va_start(args_file, fmt);
        vfprintf(g_log_file, fmt, args_file);
        va_end(args_file);
        fprintf(g_log_file, "\n");
        
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_log_mutex);    
}
