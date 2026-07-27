#include "common.h"

#include <asm-generic/errno-base.h>
#include <endian.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>


key_t create_key(char* path, int id)
{
    if (!path || id == 0) {
        perror("create_key: param is not valid.\n");
        return -1;
    }

    key_t key = ftok(path, id);
    if (key == -1) {
        perror("create_key failed.\n");
        return key;
    } else {
        return key;
    }
}

bool is_pid_alive(pid_t pid)
{
    if (pid <= 0) {
        return false;
    }
    if (kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

int get_msq(key_t key, int msgflag, bool *exist, bool force_reset)
{
    if (key == 0 || msgflag == 0) {
        perror("get_msq failed.\n");
        return -1;
    }

    if (force_reset) {
        int old_id = msgget(key, 0);
        if (old_id >= 0) {
            msgctl(old_id, IPC_RMID, NULL);
        }
    }

    int id = msgget(key, IPC_CREAT | IPC_EXCL | msgflag);
    if (id < 0) {
        if (errno == EEXIST) {
            int old_id = msgget(key, 0);
            struct msqid_ds infor;
            msgctl(old_id, IPC_STAT, &infor);
            id = old_id;
            fprintf(stderr, "[get_msq] queue exist, owner: %d, ctime: %ld, qnum: %ld.\n",
                    infor.msg_perm.uid, (long)infor.msg_ctime, (long)infor.msg_qnum);
            *exist = true;
            return id;
        } else {
            perror("[get_msq] msgget");
            exit(EXIT_FAILURE);
        }
    }

    return id;
}

static bool sem_is_stale(int semid)
{
    int val = semctl(semid, 0, GETVAL);
    if (val < 0) {
        return true;
    }
    if (val != 0) {
        return false;
    }

    pid_t last_pid = semctl(semid, 0, GETPID);
    if (last_pid > 0 && is_pid_alive(last_pid)) {
        return false;
    }

    fprintf(stderr,
            "[get_sem] semaphore looks stale (locked, last pid %d not alive) -> recreating.\n",
            (int)last_pid);
    return true;
}

int get_sem(key_t key, int msgflag, int num_sem, unsigned short *sem_array, bool force_reset)
{
    if (key == 0 || msgflag == 0) {
        perror("params are empty.\n");
        exit(EXIT_FAILURE);
    }

    if (force_reset) {
        int old = semget(key, 0, 0);
        if (old >= 0) {
            semctl(old, 0, IPC_RMID);
        }
    } else {
        int old = semget(key, 0, 0);
        if (old >= 0 && sem_is_stale(old)) {
            semctl(old, 0, IPC_RMID);
        }
    }

    int semid = semget(key, num_sem , IPC_CREAT | IPC_EXCL | msgflag);

    if (semid != -1) {
        union semun arg;

        if (sem_array) {
            arg.array = sem_array;

            if (semctl(semid, 0, SETALL, arg) == -1) {
                perror("semctl setall error.\n");
                return -1;
            }
        } else {
            arg.val = 1;

            for (int i = 0; i < num_sem; ++i) {
                if (semctl(semid, i, SETVAL, arg) == -1) {
                    perror("semctl setval error");
                    return -1;
                }
            }
        }

        struct sembuf buf[2];
        buf[0].sem_num = 0;
        buf[0].sem_op = -1;
        buf[0].sem_flg = SEM_UNDO;

        buf[1].sem_num = 0;
        buf[1].sem_op = 1;
        buf[1].sem_flg = SEM_UNDO;

        if (semop(semid, buf, 2) == -1) {
            perror("semop init dummy error.\n");
            return -1;
        }

    } else {
        const int MAX_ENTRIES = 10;
        union semun arg;
        struct semid_ds ds;

        if (errno != EEXIST) {
            perror("[get_sem] semget.\n");
            exit(EXIT_FAILURE);
        } else {
            semid = semget(key, 0, msgflag);

            arg.buf = &ds;

            for (int i = 0; i < MAX_ENTRIES; ++i) {
                semctl(semid, 0, IPC_STAT, arg);
                if (ds.sem_otime != 0) {
                    break;
                }
                sleep(1);
            }

            if (ds.sem_otime == 0) {
                perror("Existing sem not initialized.\n");
                exit(EXIT_FAILURE);
            }

            return semid;
        }
    }

    return semid;
}

static bool shm_is_stale(int shmid)
{
    struct shmid_ds ds;
    if (shmctl(shmid, IPC_STAT, &ds) == -1) {
        return true;
    }

    if (ds.shm_nattch > 0) {
        return false;
    }

    pid_t owner = ds.shm_lpid > 0 ? ds.shm_lpid : ds.shm_cpid;
    if (is_pid_alive(owner)) {
        return false;
    }

    fprintf(stderr,
            "[get_shmem] shm id %d looks stale (nattch=0, owner pid %d not alive) -> recreating.\n",
            shmid, (int)owner);
    return true;
}

int get_shmem(key_t key, int shmflag, size_t size, bool *exist, bool force_reset)
{
    if (key <= 0 || shmflag <= 0 || size <= 0) {
        perror("params not valid.\n");
        return -1;
    }

    {
        int old = shmget(key, size, 0);
        if (old >= 0) {
            if (force_reset || shm_is_stale(old)) {
                shmctl(old, IPC_RMID, NULL);
            }
        }
    }

    int shmid = shmget(key, size, IPC_CREAT | IPC_EXCL | shmflag);

    if (shmid == -1) {
        if (errno == EEXIST) {
            int old = shmget(key, size, shmflag);
            if (old > 0) {
                struct shmid_ds ds;
                shmctl(old, IPC_STAT, &ds);
                *exist = true;
                printf("share mem exist: id: %d, size: %ld, uid: %d\n", old, size, ds.shm_perm.uid);
                return old;
            }
        } else {
            perror("create shm failed.\n");
            exit(EXIT_FAILURE);
        }
    }

    return shmid;
}

int send_to_queue(int msq_id, struct message_control *msg, size_t size, int flag)
{
    int ret = msgsnd(msq_id, msg, size, flag);
    if (ret == -1) {
        if (errno == EINTR) {
            perror("error by interrupt.\n");
            return -1;
        } else {
            return -1;
        }
    }

    return ret;
}

int recv_from_queue(int msq_id, struct message_control *msg, size_t size, int flag, long type_message)
{
    int ret = msgrcv(msq_id, msg, size, type_message, flag);
    if (ret == -1) {
        if (errno  == EINTR) {
            perror("error by interrupt.\n");
            return -1;
        } else {
            return -1;
        }
    }

    return ret;
}

int attach_share_mem(int shmid, int shmflag, void **ret)
{
    *ret = shmat(shmid, NULL, shmflag);
    if (*ret == (void*)-1) {
        if (errno == EIDRM) {
            perror("shmid point to a removed identity");
            return -1;
        }
        return -1;
    }

    return 0;
}

int detach_share_mem(const void *shmaddr)
{
    int ret = shmdt(shmaddr);

    if (ret == -1) {
        perror("detach failed.\n");
        return ret;
    }
    return ret;
}

int sem_lock(int semid, int num_sem, struct sembuf *buf)
{
    if (buf) {
        if (semop(semid, buf, num_sem) == -1) {
            perror("semop in sem_lock failed.\n");
            return -1;
        }
        return 0;
    } else {
        return -1;
    }
}

int sem_unlock(int semid, int sem_num, struct sembuf *buf)
{
    if (buf) {
        if (semop(semid, buf, sem_num) == -1) {
            perror("semop in sem unlock failed.\n");
            return -1;
        }
        return 0;
    } else {
        return -1;
    }
}

int init_shmem(int semid, struct shm_data *data, int capacity)
{
    if (data == NULL) {
        perror("data is nullptr.\n");
        return -1;
    }

    struct sembuf buf;

    if (semid >= 0) {
        buf.sem_num = 0;
        buf.sem_op = -1;
        buf.sem_flg = SEM_UNDO;

        if (sem_lock(semid, 1, &buf) != 0) {
            perror("limit sem lock.\n");
            return -1;
        }
    }

    if (data->hdr.magic != SHM_MAGIC) {
        data->hdr.magic = SHM_MAGIC;
        data->hdr.version = SHM_VERSION;
        data->hdr.header_size = sizeof(struct shm_header);
        data->hdr.flags = 0;
        data->hdr.reserved = 0;

        if (data->capacity == 0) {
            if (capacity) {
                data->capacity = capacity;
            } else {
                data->capacity = CAP_DEFAULT;
            }
        }

        data->hdr.total_size = sizeof(struct shm_data) + (data->capacity * sizeof(struct sensor_data));
        data->read_index = 0;
        data->write_index = 0;
        data->dropped = 0;

        memset(data->data, 0, data->capacity * sizeof(struct sensor_data));
        printf("initialize data sucessfully.\n");
    } else {
        printf("data existed.\n");
    }

    if (semid >= 0) {
        buf.sem_num = 0;
        buf.sem_op  = 1;
        buf.sem_flg = SEM_UNDO;
        if (sem_unlock(semid, 1, &buf) == -1) {
            perror("[init_shmem] sem_unlock failed");
            return -1;
        }
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

void get_data(struct shm_data *shared_mem, int semid)
{
    if (!shared_mem || semid < 0) {
        perror("invalid params.\n");
        return;
    }

    struct sembuf buf;
    buf.sem_num = 0;
    buf.sem_op = -1;
    buf.sem_flg = SEM_UNDO;

    if(sem_lock(semid, 1, &buf) == 0) {
        if (shared_mem->read_index != shared_mem->write_index) {
            struct sensor_data data = shared_mem->data[shared_mem->read_index];
            printf("[GET_DATA] Fetched successfully - Sensor ID: %d\n", data.id_sensor);
            shared_mem->read_index = (shared_mem->read_index + 1) % shared_mem->capacity;
        } else {
            printf("[GET_DATA] Buffer is empty.\n");
        }

        buf.sem_op = 1;
        sem_unlock(semid, 1, &buf);
    }
}

void storage_data(struct shm_data *shared_mem, int semid, struct sensor_data *new_data)
{
    if (!shared_mem || !new_data || semid < 0) {
        perror("invalid params.\n");
        return;
    }

    struct sembuf buf = {0, -1, SEM_UNDO};

    if (sem_lock(semid, 1, &buf) == 0) {
        uint32_t next_write = (shared_mem->write_index + 1) % shared_mem->capacity;

        if (next_write != shared_mem->read_index) {
            shared_mem->data[shared_mem->write_index] = *new_data;
            printf("[STORAGE_DATA] Data stored at index %d\n", shared_mem->write_index);
            shared_mem->write_index = next_write;
        } else {
            shared_mem->dropped++;
            printf("[STORAGE_DATA] Buffer is full. Dropping data. (total dropped: %u)\n",
                   shared_mem->dropped);
        }

        buf.sem_op = 1;
        sem_unlock(semid, 1, &buf);
    }
}

void print_status(const struct shm_data *shared_mem, int semid)
{
    if (!shared_mem || semid < 0) {
        return;
    }

    struct sembuf buf = {0, -1, SEM_UNDO};

    if (sem_lock(semid, 1, &buf) == 0) {
        printf("\nShare mem status\n");
        printf("Capacity:    %d\n", shared_mem->capacity);
        printf("Read Index:  %d\n", shared_mem->read_index);
        printf("Write Index: %d\n", shared_mem->write_index);
        printf("Dropped:     %u\n", shared_mem->dropped);
        printf("\n");

        buf.sem_op = 1;
        sem_unlock(semid, 1, &buf);
    }
}

void handle_command(const struct message_control ctrl, struct shm_data *shared_mem, int semid)
{
    if (!shared_mem) {
        return;
    }

    printf("\n [Handle command] Received command: %s\n", cmd_to_string(ctrl.mtype));

    switch (ctrl.mtype) {
        case 1:
            get_data(shared_mem, semid);
            break;
        case 2: {
            struct sensor_data dummy_data;
            random_update_sensor(&dummy_data, 101);
            storage_data(shared_mem, semid, &dummy_data);
            break;
        }
        case 3:
            print_status(shared_mem, semid);
            break;
        case 4:
            printf("[Handle commadn] Switching to low energy mode...\n");
            break;
        case 5:
            printf("[Handle command] Preparing to shutdown...\n");
            break;
        default:
            printf("[Hndle command] Invalid command.\n");
            break;
    }
}

void random_update_sensor(struct sensor_data *sensor, int id)
{
    time_t now = time(NULL);
    uint64_t timestamp_spec = (uint64_t)now;

    sensor->timestamp = timestamp_spec;
    sensor->id_sensor = id;
    sensor->data.id = id;
    char buffer[128];

    snprintf(buffer, sizeof(buffer), "Message from sensor id: %d, pid: %d", sensor->id_sensor, getpid());

    strcpy(sensor->data.message, buffer);

    sensor->flux = (rand() % 1000);
    sensor->humidity = rand() % 100;
    sensor->moisture = rand() % 100;
    sensor->motion = true;

    printf("[SENSOR] Random update sensor called.\n");
}

int cleaning(int shm_id, int msq_id, int sem_id)
{
    int ret = shmctl(shm_id, IPC_RMID, NULL);
    if (ret != 0) {
        perror("Cleaning sharemem failed.\n");
        return -1;
    }

    ret = msgctl(msq_id, IPC_RMID, NULL);
    if (ret != 0) {
        perror("Cleaning message queue failed.\n");
        return -1;
    }

    ret = semctl(sem_id, 0, IPC_RMID, NULL);
    if (ret != 0) {
        perror("Cleaning semaphore failed.\n");
        return -1;
    }

    return 0;
}

int create_key_all(key_t *shm, key_t *msq, key_t *sem) 
{
    *shm = create_key(PATH_SYS_V, SHMEM);

    if (*shm < 0) {
        perror("create failed.\n");
        return -1;
    }

    *msq = create_key(PATH_SYS_V, QUEUE);

    if (*msq < 0) {
        perror("create failed.\n");
        return -1;
    }

    *sem = create_key(PATH_SYS_V, SEM);

    if (*sem < 0) {
        perror("create failed.\n");
        return -1;
    }

    return 0;
}

int get_id_all(int *msq_id, int *shm_id, int *sem_id, key_t *shm, key_t *msq, key_t *sem, bool force_reset)
{
    bool exist = false;

    *msq_id = get_msq(*msq, PERMS, &exist, force_reset);

    if (*msq_id < 0 && exist == false) {
        perror("create msq failed.\n");
        return -1;
    }   
    ssize_t size = sizeof(struct shm_data) + (CAP_SHM* sizeof(struct sensor_data));

    *shm_id = get_shmem(*shm, PERMS, size, &exist, force_reset);
    if (*shm_id < 0 && exist == false) {
        perror("create shm_id failed.\n");
        return -1;
    } else if (*shm_id > 0 && exist == true) {
        printf("reusign shm\n");

    }

    *sem_id = get_sem(*sem, PERMS, 1, NULL, force_reset);

    if (*sem_id < 0) {
        perror("create or use sem failed.\n");
        return -1;
    }

    return 0;
}
