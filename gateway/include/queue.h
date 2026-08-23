#ifndef QUEUE_H
#define QUEUE_H


#include "common.h"
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

struct task {
    int slot;
    /* task_id (by gateway define) != request_id (by client define)*/
    uint64_t task_id;
    uint64_t generation;
    uint16_t type;
    uint32_t request_id;
    unsigned char payload[MAX_PAYLOAD];
    uint64_t timeout_ms;
    struct task* next_task;
    size_t length;
};

struct response {
    int slot;
    uint64_t generation;
    uint16_t type;
    uint32_t request_id;
    uint32_t length;
    uint8_t payload[MAX_PAYLOAD];
    struct response* next_response;
};

struct task_queue {
    struct task* head;
    struct task* tail;

    size_t size;
    size_t max_size;

    pthread_mutex_t lock;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;

    atomic_bool shutdown;
};

struct response_queue {
    struct response* head;
    struct response* tail;

    size_t size;
    size_t max_size;

    pthread_mutex_t lock;
};


/**
 * @brief init task queue 
 * 
 * @param queue : pointer to task queue
 * @param size : maximum number of queue
 */
void task_queue_init(struct task_queue* queue, size_t size);


/**
 * @brief add a task to queue
 * 
 * @param queue pointer to task queue
 * @param task task need to enqueue
 * @return int 0 on success, -1 failed, EAGAIN if full
 */
int push(struct task_queue* queue, struct task* task);

/**
 * @brief take a task from queue
 * blocking untils getting a task or queue shutdown
 * @param queue pointer to task queue 
 * @return struct task* get task if success, NULL if queue empty and shutdown
 */
struct task* pop(struct task_queue* queue);

/**
 * @brief shutdown taskqueue
 * 
 * @param queue pointer to queue want to shutdown
 * @return int 0 if success, -1 if failed
 */
int task_queue_shutdown(struct task_queue* queue);

/**
 * @brief 
 * 
 * @param queue 
 * @return int 
 */
int task_queue_destroy(struct task_queue* queue);


/**
 * @brief 
 * 
 * @param queue 
 * @param size 
 */
void response_queue_init(struct response_queue* queue, size_t size);


/**
 * @brief 
 * 
 * @param queue 
 */
void response_queue_destroy(struct response_queue* queue);

/**
 * @brief 
 * 
 * @param queue 
 * @param response 
 * @return int 
 */
int response_queue_push(struct response_queue* queue, struct response* response);

/**
 * @brief 
 * 
 * @param queue 
 * @return struct response* 
 */
struct response* response_queue_pop(struct response_queue* queue);


#endif