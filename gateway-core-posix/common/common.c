#include "common.h"

#include <fcntl.h>
#include <mqueue.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <semaphore.h>

#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

mqd_t get_mq(const char *name, int flags, int mode, struct mq_attr *expect_attr)
{
    mqd_t ret = mq_open(name, flags, mode, expect_attr);
    if (ret < 0) {
        if (errno == EEXIST) {
            perror("msq exists.\n");
            ret = mq_open(name, O_RDWR);

            if (ret < 0) {
                perror("mq_open failed.\n");
                if (errno == EINTR) {
                    perror("failed caused by interrupt.\n");
                }
                return -1;
            }

            struct mq_attr stale_attr;
            int r = mq_getattr(ret, &stale_attr);
            if (r == 0) {
                if (stale_attr.mq_maxmsg == expect_attr->mq_maxmsg && stale_attr.mq_msgsize == expect_attr->mq_msgsize) {
                    return ret;
                }
                else {
                    perror("stale queue not match requires.\n");
                    return -1;
                }
            }
        } else{
            perror("Create mq failed.\n");
        }
    }
    printf("Create mq successfull.\n");
    return ret;
}

int send_to_queue(mqd_t mq, const char* ms, size_t ms_len, unsigned int priority, int timeout)
{
    if (ms == NULL || mq < 0) {
        perror("invalid params.\n");
        return -1;
    }

    int ret;
    if (timeout > 0) {
        struct timespec time;

        if (clock_gettime(CLOCK_REALTIME, &time) == -1) {
            perror("get time error.\n");
            return -1;
        }
        time.tv_sec += (long)timeout;

        ret = mq_timedsend(mq, ms, ms_len, priority, &time);
    } else {
        ret = mq_send(mq, ms, ms_len, priority);
    }

    if (ret == -1) {
        if (errno == EAGAIN) {
            perror("msq is full.\n");
        } else if (errno == EINTR) {
            perror("msq was interrupted.\n");
        } else if (errno == ETIMEDOUT) {
            perror("timout.\n");
        } else if (errno == EMSGSIZE) {
            perror("sizeof ms is greater than msq size.\n");
        }

        return -1;
    }

    return 0;
}

ssize_t recv_from_queue(mqd_t mq, char *rec_ms, size_t ms_len, unsigned int priority, int timeout)
{
    if (!rec_ms && mq < 0) {
        perror("invalid params.\n");
        return -1;
    }

    ssize_t ret;

    if (timeout) {
        struct timespec time;

        if (clock_gettime(CLOCK_REALTIME, &time) == -1) {
            perror("get time error.\n");
            return -1;
        }
        time.tv_sec += (long)timeout;

        ret = mq_timedreceive(mq, rec_ms, ms_len, &priority, &time);
    } else {
        ret = mq_receive(mq, rec_ms, ms_len, &priority);
    }

    if (ret == -1) {
        if (errno == EAGAIN) {
            perror("msq is full.\n");
        } else if (errno == EINTR) {
            perror("msq was interrupted.\n");
        } else if (errno == ETIMEDOUT) {
            perror("timout.\n");
        } else if (errno == EMSGSIZE) {
            perror("sizeof ms is greater than msq size.\n");
        }

        return ret;
    }

    return ret;
}

int drain_mq(mqd_t mqd, struct shm_data *data, int shm_fd)
{
    if (data == NULL) {
        perror("shm null..\n");
        return -1;
    }

    struct mq_attr attr;
    if (mq_getattr(mqd, &attr) == -1) {
        perror("mq_getattr");
        return -1;
    }

    int remain = attr.mq_curmsgs;
    struct message_queue msg;

    while (remain > 0) {
        ssize_t n = recv_from_queue(mqd, (char*)&msg, sizeof(msg), 0, 1);
        if (n <= 0) {
            if (errno == ETIMEDOUT || errno == EINTR) {
                break;
            }
            perror("[drain_mq] recv_from_queue failed");
            break;
        }

        printf("[drain_mq] draining: %s (type=%ld)\n", msg.message, msg.msq_type);
        command_handling(msg, data, shm_fd, mqd);

        if (mq_getattr(mqd, &attr) == -1) {
            break;
        }
        remain = attr.mq_curmsgs;
    }

    return 0;
}

int get_sem(const char *name, int flags, mode_t mode, unsigned int value, sem_t **s)
{
    if (!name && !s) {
        perror("not valid params.\n");
        return -1;
    }

    sem_t *tmp = sem_open(name, flags, mode, value);

    if (tmp != SEM_FAILED) {
        *s = tmp;
        return 0;
    }

    int ret_errno = errno;

    if ((flags & O_EXCL) && ret_errno == EEXIST) {
        int new_flags = flags & ~(O_CREAT | O_EXCL);
        tmp = sem_open(name, new_flags);
        if (tmp != SEM_FAILED) {
            *s = tmp;
            printf("use a exist sem.\n");
            return 0;
        }
    }

    if (ret_errno == EINVAL) {
        fprintf(stderr, "invalid sem name or value exceeds max.\n");
    } else if (ret_errno == ENOENT) {
        fprintf(stderr, "sem doesn't exist.\n");
    } else if (ret_errno == EACCES) {
        fprintf(stderr, "permission denied.\n");
    }

    errno = ret_errno;
    return -1;
}

int wait_sem(sem_t* sem, bool is_trywait, int timeout)
{
    if (!sem) {
        perror("invalid params.\n");
        return -1;
    }

    int ret;

    if (is_trywait) {
        ret = sem_trywait(sem);
        if (ret == -1) {
            if (errno == EAGAIN) {
                printf("sem currently has zero value.\n");
                return -1;
            } else if (errno == EINTR) {
                perror("sem_trywait was interrupt.\n");
                return -1;
            } else if (errno == EINVAL) {
                perror("not valid sem.\n");
                return -1;
            }
        }

        return 0;
    }

    if (timeout != 0) {
        struct timespec time;
        if (clock_gettime(CLOCK_REALTIME, &time) != 0) {
            perror("get clock failed.\n");
            return -1;
        }

        time.tv_sec += timeout;

        ret = sem_timedwait(sem, &time);
        if (ret == -1) {
            if (errno == EINVAL) {
                perror("sem_timedwait: not valid params.\n");
                return -1;
            } else if (errno == ETIMEDOUT) {
                perror("semtimedwait timeout.\n");
                return -1;
            }
        }

        return 0;
    }

    ret = sem_wait(sem);
    if (ret == -1) {
        if (errno == EINTR) {
            perror("semwait was interrupted.\n");
            return -1;
        } else if (errno == EINVAL) {
            perror("semwait has not valid params.\n");
            return -1;
        }
    }

    return 0;
}

int post_sem(sem_t *sem)
{
    if (!sem) {
        perror("invalid params.\n");
        return -1;
    }

    int ret = sem_post(sem);
    if (ret == -1) {
        if (errno == EINVAL) {
            perror("not valid sem\n");
            return -1;
        } else if (errno == EOVERFLOW) {
            perror("overflow sem. exeeds max.\n");
            return -1;
        }
    }

    return 0;
}

int mapping_shm(int shm_fd, size_t size, int prot, int flags, void **addr)
{
    if (shm_fd < 0 || size <= 0 || addr == NULL) {
        perror("invalid shm_fd.\n");
        return -1;
    }

    void *map = mmap(NULL, size, prot, flags, shm_fd, 0);

    if (map == MAP_FAILED) {
        perror("map failed.\n");
        return -1;
    }

    *addr = map;

    return 0;
}

int unmap_shm(void *addr, size_t size)
{
    if (addr == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (munmap(addr, size) == -1) {
        perror("munmap");
        return -1;
    }
    return 0;
}

int get_shm(const char *name, int flags, mode_t mode, size_t expect_size)
{
    if (name == NULL || expect_size == 0) {
        errno = EINVAL;
        fprintf(stderr, "get_shm: invalid params (name=%p, size=%zu)\n", name, expect_size);
        return -1;
    }

    printf("[get_shm] Opening/Creating %s with size %zu\n", name, expect_size);

    int ret = shm_open(name, flags, mode);
    if (ret == -1) {
        if (errno == EEXIST) {
            int new_flags = flags & ~(O_CREAT | O_EXCL);
            ret = shm_open(name, new_flags, mode);
            if (ret == -1) {
                perror("shm_open fallback");
                return -1;
            }
            struct stat shmb;
            if (fstat(ret, &shmb) == -1) {
                perror("fstat");
                close(ret);
                return -1;
            }
            printf("[get_shm] Existing SHM size: %ld\n", (long)shmb.st_size);
            if (shmb.st_size == 0) {
                printf("[get_shm] Truncating to %zu\n", expect_size);
                if (ftruncate(ret, (off_t)expect_size) == -1) {
                    perror("ftruncate (existing)");
                    close(ret);
                    return -1;
                }
                printf("[get_shm] Truncated successfully.\n");
            } else if ((size_t)shmb.st_size != expect_size) {
                fprintf(stderr, "[get_shm] Size mismatch: existing %ld, expected %zu\n",
                        (long)shmb.st_size, expect_size);
                close(ret);
                errno = EINVAL;
                return -1;
            }
            return ret;
        } else if (errno == EACCES) {
            perror("Permission denied");
        } else if (errno == EINVAL) {
            perror("Invalid name");
        } else {
            perror("shm_open");
        }
        return -1;
    }

    struct stat shmb;
    if (fstat(ret, &shmb) == -1) {
        perror("fstat");
        close(ret);
        return -1;
    }
    printf("[get_shm] SHM opened, current size: %ld\n", (long)shmb.st_size);
    if (shmb.st_size == 0 && (flags & O_CREAT)) {
        printf("[get_shm] New SHM, truncating to %zu\n", expect_size);
        if (ftruncate(ret, (off_t)expect_size) == -1) {
            perror("ftruncate (new)");
            close(ret);
            return -1;
        }
        printf("[get_shm] Created with size %zu\n", expect_size);
    } else if ((size_t)shmb.st_size != expect_size) {
        fprintf(stderr, "[get_shm] Existing size %ld differs from expected %zu. Using existing.\n",
                (long)shmb.st_size, expect_size);
    }
    return ret;
}

int init_shm(bool pshared, struct shm_data *data)
{
    if (!data) {
        perror("null ptr.\n");
        return -1;
    }

    if (data->hdr.magic == 0) {
        data->hdr.magic = SHM_MAGIC;
        data->hdr.version = SHM_VERSION;
        size_t header_size = offsetof(struct shm_data, data);
        size_t total_size = header_size + data->capacity * sizeof(struct sensor_data);
        data->hdr.total_size = total_size;
        data->hdr.flags = 1;
        data->hdr.reserved = 0;

        data->read_index = 0;
        data->write_index = 0;
        data->dropped = 0;

        if (pshared) {
            pthread_mutexattr_t mattr;
            pthread_mutexattr_init(&mattr);
            pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&data->mutex, &mattr);
            pthread_mutexattr_destroy(&mattr);
        }

        int ret = sem_init(&data->spaces, pshared? 1 : 0, data->capacity);
        if (ret == -1) {
            if (errno == EINVAL) {
                perror("value exceed SEM_VALUE_MAX");
                return -1;
            }
        }

        ret = sem_init(&data->items, pshared? 1 : 0, 0);
        if (ret == -1) {
            if (errno == EINVAL) {
                perror("value exceed SEM_VALUE_MAX");
                return -1;
            }
        }

        printf("init space sem success.\n");

        memset(data->data, 0, data->capacity * sizeof(struct sensor_data));

        printf("init sem successfully.\n");

        return 0;

    } else if (data->hdr.magic == SHM_MAGIC && data->hdr.version == SHM_VERSION) {
        printf("shm has already init.\n");
    }

    return 0;
}

const char* cmd_to_string(int cmd)
{
    switch (cmd) {
        case 1:
            return "GET_DATA";
        case 2:
            return "STORAGE_DATA";
        case 3:
            return "GET_STATUS";
        case 4:
            return "LOW_ENERY";
        case 5:
            return "SHUTDOWN";
        default:
            return "UNKNOWN";
    }
}

void random_update_sensor(struct sensor_data *sensor, int id, struct message_queue data)
{
    time_t now = time(NULL);
    uint64_t timestamp = (uint64_t)now;

    sensor->id_sensor = id;
    sensor->timestamp = timestamp;
    sensor->id_sensor = id;
    sensor->data = data;

    sensor->flux = (rand() % 1000);
    sensor->humidity = rand() % 100;
    sensor->moisture = rand() % 100;
    sensor->motion = true;

    printf("[SENSOR] Random update sensor called.\n");
}

void get_data(struct shm_data *shared_mem)
{
    if (shared_mem == NULL) {
        perror("Null params.\n");
        return;
    }

    if (wait_sem(&shared_mem->items, true, 0) != 0) {
        printf("[get_data] Buffer is empty, nothing to read.\n");
        return;
    }

    pthread_mutex_lock(&shared_mem->mutex);
    struct sensor_data data = shared_mem->data[shared_mem->read_index];
    shared_mem->read_index = (shared_mem->read_index + 1) % shared_mem->capacity;
    pthread_mutex_unlock(&shared_mem->mutex);

    if (post_sem(&shared_mem->spaces) != 0) {
        perror("[get_data] post_sem failed.\n");
    }

    printf("[get_data] Read sensor id=%d, humidity=%d, moisture=%d\n",
           data.id_sensor, data.humidity, data.moisture);
    return;
}

void storage_data(struct shm_data *shared_mem, const struct sensor_data *new_data)
{
    if (shared_mem == NULL || new_data == NULL) {
        perror("null params.\n");
        return;
    }
    if (wait_sem(&shared_mem->spaces, true, 0) != 0) {
        pthread_mutex_lock(&shared_mem->mutex);
        shared_mem->dropped++;
        pthread_mutex_unlock(&shared_mem->mutex);
        printf("[storage_data] Buffer full, dropping data (dropped=%u).\n", shared_mem->dropped);
        return;
    }

    pthread_mutex_lock(&shared_mem->mutex);
    shared_mem->data[shared_mem->write_index] = *new_data;
    shared_mem->write_index = (shared_mem->write_index + 1) % shared_mem->capacity;
    pthread_mutex_unlock(&shared_mem->mutex);

    if (post_sem(&shared_mem->items) != 0) {
        perror("[storage_data] post_sem failed.\n");
    }
    return;
}

void print_status(struct shm_data *shared_mem){
    if (shared_mem == NULL) {
        perror("null param.\n");
        return;
    }

    pthread_mutex_lock(&shared_mem->mutex);
    printf("Status of shared mem: \n");
    printf("capacity: %d.\n", shared_mem->capacity);
    printf("read index: %d\n", shared_mem->read_index);
    printf("write index: %d.\n", shared_mem->write_index);
    printf("dropped items: %d.\n", shared_mem->dropped);

    pthread_mutex_unlock(&shared_mem->mutex);

    return;
}

void command_handling(const struct message_queue ctrl, struct shm_data *shared_mem, int shm_fd, mqd_t mq_fd)
{
    (void)shm_fd;
    (void)mq_fd;

    if (shared_mem == NULL) {
        perror("null param.\n");
        return;
    }
    switch (ctrl.msq_type) {
        case 1:
            get_data(shared_mem);
            break;
        case 2: {
            struct sensor_data dummy_data;
            random_update_sensor(&dummy_data, 101, ctrl);
            storage_data(shared_mem, &dummy_data);
            break;
        }
        case 3:
            print_status(shared_mem);
            break;
        case 4:
            printf("[Handle command] Switching to low energy mode...\n");
            break;
        case 5:
            printf("[Handle command] SHUTDOWN command received, forwarding to signal handler...\n");
            kill(getpid(), SIGTERM);
            break;
        default:
            printf("[Handle command] Invalid command.\n");
            break;
    }
}

void shutdown_ipc(bool is_creator, struct shm_data *shm, int shm_fd, mqd_t mq_fd)
{
    if (mq_fd != -1) {
        mq_close(mq_fd);
        if (is_creator) {
            if (mq_unlink(PATH_MQ) == -1) {
                perror("mq_unlink");
            } else {
                printf("[Shutdown] Unlinked mq: %s\n", PATH_MQ);
            }
        }
    }

    if (shm != NULL && shm->hdr.total_size > 0) {
        pthread_mutex_destroy(&shm->mutex);

        if (munmap(shm, shm->hdr.total_size) == -1) {
            perror("munmap");
        } else {
            printf("[Shutdown] Unmapped shm\n");
        }
    }
    if (shm_fd != -1) {
        close(shm_fd);
        if (is_creator) {
            if (shm_unlink(PATH_SHM) == -1) {
                perror("shm_unlink");
            } else {
                printf("[Shutdown] Unlinked shm: %s\n", PATH_SHM);
            }
        }
    }

    return;
}