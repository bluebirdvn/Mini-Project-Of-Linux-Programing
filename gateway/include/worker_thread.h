#ifndef _WORKER_THREAD_H
#define _WORKER_THREAD_H

#include "queue.h"
#include <stddef.h>
#include <stdint.h>

#include "sensor.h"
#include "actuator.h"
#include "task_execute.h"

struct work_task {
    uint64_t id;
    struct task_queue* task_queue;
    struct response_queue* response_queue;
    int event_fd;
    // size_t length;
};


/**
 * @brief this function will wakeup main thread through eventfd by writing 1 to eventfd
 * 
 * @param event_fd event was monitored by epoll_fd in structserver
 * @return int 0 if success, -1 if failed
 */
int notify_main_thread(int event_fd);

/**
 * @brief build response packet when a task was done
 * 
 * @param slot index in clients array in server
 * @param generation if of client when it was accepted connection
 * @param type type of packet in hreader
 * @param request_id number of request id
 * @param payload payload of packet
 * @param length sizeof payload
 * @return struct response* poiniter to resoinse if successs, NULL if failed
 */
struct response* make_response(int slot, uint64_t generation, uint16_t type, uint32_t request_id, const void *payload, uint32_t length);

/**
 * @brief this thread have IO function. it will process epoll event to accept connections, read message,
 * send message, ...
 * @param arg pointer to struct sever
 * @return void* NULL
 */
void *main_thread(void *arg);

/**
 * @brief this thread run task, it will process task popped from task queue and return the response
 * 
 * @param arg pointer to work_task struct
 * @return void* NULL
 */
void *worker_thread(void* arg);

/**
 * @brief this thread will capture some signal that was add to sigset_t to monitor, avoiding othrer thread capture signal (unwanted)
 * and this will help process shutdown safely
 * @param arg pointer to struct server
 * @return void* NULL
 */
void *signal_thread(void* arg);

#endif