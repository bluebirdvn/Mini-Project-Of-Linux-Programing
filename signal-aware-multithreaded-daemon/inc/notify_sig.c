#include <asm-generic/errno-base.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <sys/types.h>
#include <unistd.h>

#include "notify_sig.h"


int init_monitor(void)
{
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("init inotify_fd failed.\n");
        exit(EXIT_FAILURE);
    }

    return inotify_fd;
}


int add_monitor(int inotify_fd, const char* path, uint32_t mask)
{
    if (path == NULL) {
        perror("param null\n");
        exit(EXIT_FAILURE);
    }

    int watch_fd = inotify_add_watch(inotify_fd, path, mask);

    if(watch_fd < 0) {
        perror("Add watch inotify failed.\n");
        switch (errno) {
            case EEXIST:
                perror("mask contains IN_MASK_CREATE and path refers to a file already being watched by the same fd");
                break;
            case EFAULT:
                perror("path points outside of the process's accessible address space");
                break;
            default:
                break;
        }
    }

    return watch_fd;
}



int remove_monitor(int inotify_fd, int watch_fd)
{
    if (inotify_fd != -1 && watch_fd != -1) {
        inotify_rm_watch(inotify_fd, watch_fd);
        close(inotify_fd);
    }

    return 0;
}



int watch_config_dir(const char* watch_dir)
{
    if (watch_dir == NULL) {
        perror("Directory is invalid\n");
        return -1;
    }
    int inofify_fd = init_monitor();
    fflush(stdout);
    uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;

    int ret = add_monitor(inofify_fd, watch_dir, mask);
    fflush(stdout);
    if (ret < 0) {
        perror("add monitor dir failed.\n");
        return -1;
    }

    return inofify_fd;
}



