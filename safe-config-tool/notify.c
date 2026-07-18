#include <asm-generic/errno-base.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <unistd.h>
#include <syslog.h>


typedef void (*inotify_callback_t)(const char* path, uint32_t mask);

void config_watch(const char* path, uint32_t mask)
{
    const char* event_type = "";
    if (mask & IN_MODIFY) {
        event_type = "MODIFY";
    } else if (mask & IN_CREATE) {
        event_type = "CREATE";
    } else if (mask & IN_DELETE) {
        event_type = "DELETE";
    } else if (mask & IN_MOVED_TO) {
        event_type = "MOVE_TO";
    } else if (mask & IN_MOVED_FROM) {
        event_type = "MOVE_FROM";
    }
    syslog(LOG_INFO, "%s: %s\n", event_type, path);
    printf("%s: %s\n", event_type, path);
    fflush(stdout);

}

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


int watch_config_dir(const char* watch_dir, inotify_callback_t callback)
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

    char buffer_event[2048];

    while(1) {
        ssize_t len = read(inofify_fd, buffer_event, sizeof(buffer_event));
        if (len == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("read inotify failed.\n");
                break;
            }

        }
        for (char *ptr = buffer_event; ptr < buffer_event + len; ) {
            struct inotify_event *event = (struct inotify_event*)(ptr);

            if (event->len > 0 && callback) {
                callback(event->name, event->mask);


            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
        fflush(stdout);
    }

    close(inofify_fd);
    closelog();
    return -1;
}



int main(int argc, char* argv[])
{   
    if (argc < 2) {
        perror("No argc\n");
        exit(EXIT_FAILURE);
    }

    watch_config_dir(argv[1], config_watch);

    return 0;
}