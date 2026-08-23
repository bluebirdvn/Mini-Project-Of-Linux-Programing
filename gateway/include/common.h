#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TCP_PORT 5555
#define UDP_PORT 5556
#define NUM_WORKERS 4
#define MAX_CLIENTS 128
#define TASK_QUEUE_MAX 1024
#define MAX_PAYLOAD 4096
#define RX_BUF_CAP 8192
#define MAX_EVENTS 64
#define IP_BOARD_CAST "255.255.255.255"
#define IP_LISTEN "0.0.0.0"

#define REQUEST_TIMEOUT_MS 5000
#define IDLE_TIMEOUT_SEC 60
#define RATE_LIMIT_CAP 100.0
#define RATE_LIMIT_REFILL_SEC 50.0
#define EPOLL_TIMEOUT_MS 1000


static uint64_t inline now_ms(void)
{
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    uint64_t t = spec.tv_sec * 1000ULL + spec.tv_nsec / 1000000UL;
    return t;
}

/**
 * @brief add a fd to event poll for monitoring
 * 
 * @param epoll_fd epoll file descriptor monitor others fd
 * @param event evnet was monitored
 * @param fd a fd was monitored
 * @return int 0 if success,  -1 if failed
 */
int epoll_add(int epoll_fd, int event, int fd);

/**
 * @brief modify event monitored of fd int epoll_fd
 * 
 * @param epoll_fd epoll file descriptor monitor others fd
 * @param event evnet was monitored
 * @param fd a fd was monitored
 * @return int 0 if success,  -1 if failed
 */
int epoll_mod(int epoll_fd, int event, int fd);

/**
 * @brief delete fd from epoll-fd
 * 
 * @param epoll_fd  epoll file descriptor monitor others fd
 * @param fd fd that deleted
 * @return int 0 if success, -1 if failed
 */
int epoll_del(int epoll_fd, int fd);



#endif