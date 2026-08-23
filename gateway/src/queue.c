#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include "logger.h"
#include "queue.h"
#include <stdlib.h>
void task_queue_init(struct task_queue* queue, size_t size)
{
    if (queue == NULL || size == 0) {
        LOG_ERROR("init failed");
        return;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    queue->max_size = size;
    
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);

    queue->shutdown = false;

    LOG_DEBUG("init task quueue success");
    return;
}



int push(struct task_queue* queue, struct task* task)
{
    if (queue == NULL || task == NULL) {
        LOG_ERROR("null params");
        return -1;
    }

    pthread_mutex_lock(&queue->lock);

    if (queue->shutdown == true) {
        pthread_mutex_unlock(&queue->lock);
        LOG_DEBUG("shutdown in queue");
        return -1;
    }

    if (queue->size >= queue->max_size) {
        pthread_mutex_unlock(&queue->lock);
        return -EAGAIN;
    }

    if (queue->tail == NULL) {
        queue->tail = task;
        queue->head = task;
    } else {
        queue->tail->next_task = task;
    }

    task->next_task = NULL;
    queue->size++;

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);

    return 0;
}


struct task* pop(struct task_queue* queue)
{
    if (queue == NULL) {
        LOG_ERROR("null params");
        return NULL;
    }

    pthread_mutex_lock(&queue->lock);
    while(queue->size == 0 && queue->shutdown == false) {
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }

    if (queue->shutdown == true && queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        LOG_DEBUG("shutdown in queue");
        return NULL;
    }

    struct task* return_task =  queue->head;
    queue->head = queue->head->next_task;

    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    queue->size--;

    return_task->next_task = NULL;

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);

    return return_task;
}


int task_queue_shutdown(struct task_queue* queue)
{
    pthread_mutex_lock(&queue->lock);
    atomic_store(&queue->shutdown, true);
    pthread_cond_broadcast(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);

    return 0;
}

int task_queue_destroy(struct task_queue* queue)
{
    if (queue == NULL) {
        LOG_ERROR("null params");
        return -1;
    }

    pthread_mutex_lock(&queue->lock);
    struct task* task = queue->head;

    while(task) {
        struct task* next = task->next_task;
        task->next_task = NULL;
        free(task);

        task = next;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->size  = 0;

    pthread_mutex_unlock(&queue->lock);
    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);

    return 0;
}

/**
 * @brief 
 * 
 * @param queue 
 * @param size 
 */
void response_queue_init(struct response_queue* queue, size_t size)
{
    if (queue == NULL) {
        LOG_ERROR("null params");
        return;
    }

    if (size <= 0) {
        LOG_ERROR("size must be positive");
        return;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    queue->max_size = size;
    pthread_mutex_init(&queue->lock, NULL);

    LOG_DEBUG("init response queue success");
    return;
}

/**
 * @brief 
 * 
 * @param queue 
 */
void response_queue_destroy(struct response_queue* queue)
{

}

/**
 * @brief 
 * 
 * @param queue 
 * @param response 
 * @return int 
 */
int response_queue_push(struct response_queue* queue, struct response* response)
{
    if (queue == NULL || response == NULL) {
        LOG_ERROR("null parms");
        return -1;
    }

    pthread_mutex_lock(&queue->lock);
    if (queue->size >= queue->max_size) {
        pthread_mutex_unlock(&queue->lock);
        return -EAGAIN;
    }

    if (queue->tail == NULL) {
        queue->head = response;
        queue->tail = response;

    } else {
        queue->tail->next_response = response;
        queue->tail = response;
    }
    queue->size++;

    queue->tail->next_response = NULL;

    pthread_mutex_unlock(&queue->lock);

    return 0;
}

/**
 * @brief 
 * 
 * @param queue 
 * @return struct response* 
 */
struct response* response_queue_pop(struct response_queue* queue)
{
    if (queue == NULL) {
        LOG_ERROR("null params");
        return NULL;
    }

    pthread_mutex_lock(&queue->lock);
    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        LOG_WARN("empty queue");
        return NULL;
    }

    struct response* ret_res = queue->head;
    queue->head = ret_res->next_response;

    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    
    ret_res->next_response = NULL;
    queue->size--;
    pthread_mutex_unlock(&queue->lock);
    return ret_res;
}
