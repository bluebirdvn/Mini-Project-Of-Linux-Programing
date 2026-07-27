#ifndef _COMMON_H
#define _COMMON_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
#endif
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short  *array;
    struct seminfo  *__buf;
};


#define PATH_SYS_V "/tmp"

#define QUEUE 'Q'
#define SHMEM 'M'
#define SEM   'S'

#define PERMS 0666

#define MAX_LEN_MESS 256

#define SHM_VERSION 1u
#define SHM_MAGIC 0x20213026

#define CAP_DEFAULT 10
#define CAP_SHM 20
#define FILTER_MSQ 0

enum type_message {
    GET_DATA = 1,
    STORAGE_DATA,
    GET_STATUS,
    LOW_ENERY,
    SHUTDOWN,
};

struct message {
    int id;
    char message[MAX_LEN_MESS];
};

struct message_control {
    long mtype;
    struct message data;
};

struct sensor_data {
    int id_sensor;
    struct message data;
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
    struct sensor_data data[];
};



key_t create_key(char* path, int id);

bool is_pid_alive(pid_t pid);

int get_msq(key_t key, int msgflag, bool *exist, bool force_reset);

int get_shmem(key_t key, int shmflag, size_t size, bool *exist, bool force_reset);

int get_sem(key_t key, int msgflag, int num_sem, unsigned short *sem_array, bool force_reset);

int send_to_queue(int msq_id, struct message_control *msg, size_t size, int flag);

int recv_from_queue(int msq_id, struct message_control *msg, size_t size, int flag, long type_message);


int attach_share_mem(int shmid, int shmflag, void **ret);

int detach_share_mem(const void *shmaddr);


int sem_lock(int semid, int sem_num, struct sembuf *buf);

int sem_unlock(int semid, int sem_num, struct sembuf *buf);

int init_shmem(int semid, struct shm_data *data, int capacity);

const char* cmd_to_string(int cmd);

void random_update_sensor(struct sensor_data *sensor, int id);

int cleaning(int shm_id, int msq_id, int sem_id);

void get_data(struct shm_data *shared_mem, int semid);
void storage_data(struct shm_data *shared_mem, int semid, struct sensor_data *new_data);
void print_status(const struct shm_data *shared_mem, int semid);
void handle_command(const struct message_control ctrl, struct shm_data *shared_mem, int semid);

void shutdown();

int create_key_all(key_t *shm, key_t *msq, key_t *sem);

int get_id_all(int *msq_id, int *shm_id, int *sem_id, key_t *shm, key_t *msq, key_t *sem, bool force_reset);


#endif