#ifndef NOTIFY_SIG_H
#define NOTIFY_SIG_H

#include <stdint.h>

typedef void (*inotify_callback_t)(const char* path, uint32_t mask, void* priv_data);


void config_watch(const char* path, uint32_t mask);

int init_monitor(void);

int add_monitor(int inotify_fd, const char* path, uint32_t mask);

int remove_monitor(int inotify_fd, int watch_fd);

int watch_config_dir(const char* watch_dir);

#endif