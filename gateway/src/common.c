#include "common.h"
#include "logger.h"
#include <sys/epoll.h>


int epoll_add(int epoll_fd, int event, int fd)
{
    struct epoll_event ev;
    ev.events = op;
    ev.data.fd = fd;

    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    if (ret < 0) {
        LOG_ERROR("epoll ctl failed");
        return -1;
    }
    return 0;
}


int epoll_mod(int epoll_fd, int event, int fd)
{
    struct epoll_event ev;
    ev.events = op;
    ev.data.fd = fd;

    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
    if (ret < 0) {
        LOG_ERROR("epoll ctl failed");
        return -1;
    }
    return 0;
}


int epoll_del(int epoll_fd, int fd)
{
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    if (ret < 0) {
        LOG_ERROR("epoll ctl failed");
        return -1;
    }
    return 0;
}
