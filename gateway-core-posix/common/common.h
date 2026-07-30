#ifndef _COMMON_H
#define _COMMON_H

#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <time.h>
#include <semaphore.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

enum type_message {
    GET_DATA = 1,
    STORAGE_DATA,
    GET_STATUS,
    LOW_ENERY,
    SHUTDOWN,
};

#define SHM_VERSION 1u
#define SHM_MAGIC 0x20213026

#define PATH_MQ "/gateway_queue"
#define PATH_SHM "/gateway_shm"
#define PATH_SEM_ITEM "/gateway_semitem"
#define PATH_SEM_SPACE "/gateway_semspace"

#define MAX_MSQ  10
#define SIZE_MESSAGE 128

#define BUFFER_CAPACITY 128

#define DEVICE_READ "/read"
#define DEVICE_WRITE "write"

enum priority_msq {
    LOW_PRI = 10,
    MID_PRI = 20,
    HIGH_PRI = 30,
};

struct message_queue {
    long msq_type;
    int client_id;
    char message[SIZE_MESSAGE];
};

#define SIZE_MSQ sizeof(struct message_queue)

struct sensor_data {
    int id_sensor;
    struct message_queue data;
    int humidity;
    int flux;
    int moisture;
    bool motion;
    uint64_t timestamp;
};

struct shm_header {
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t header_size;
    uint32_t flags;
    uint32_t reserved;
};

struct shm_data {
    struct shm_header hdr;
    uint32_t capacity;
    uint32_t read_index;
    uint32_t write_index;
    uint32_t dropped;
    pthread_mutex_t mutex;

    sem_t items;
    sem_t spaces;
    volatile uint8_t shutdown_flag;
    struct sensor_data data[];
};

mqd_t get_mq(const char *name, int flags, int mode, struct mq_attr *expect_attr);

int send_to_queue(mqd_t mq, const char* ms, size_t ms_len, unsigned int priority, int timeout);

ssize_t recv_from_queue(mqd_t mq, char *rec_ms, size_t ms_len, unsigned int priority, int timeout);

int drain_mq(mqd_t mqd, struct shm_data *data, int shm_fd);

int get_sem(const char *name, int flags, mode_t mode, unsigned int value, sem_t **s);

int wait_sem(sem_t* sem, bool is_trywait, int timeout);

int post_sem(sem_t *sem);

int get_shm(const char *name, int flags, mode_t mode, size_t expect_size);

int mapping_shm(int shm_fd, size_t size, int prot, int flags, void **addr);

int unmap_shm(void *addr, size_t size);

int init_shm(bool pshared, struct shm_data *data);

const char* cmd_to_string(int cmd);

void random_update_sensor(struct sensor_data *sensor, int id, struct message_queue data);

void get_data(struct shm_data *shared_mem);
void storage_data(struct shm_data *shared_mem, const struct sensor_data *new_data);
void print_status(struct shm_data *shared_mem);
void command_handling(const struct message_queue ctrl, struct shm_data *shared_mem, int shm_fd, mqd_t mq_fd);
void shutdown_ipc(bool is_creator, struct shm_data *shm, int shm_fd, mqd_t mq_fd);

#endif