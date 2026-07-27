#include "common.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <sys/types.h>
#include <signal.h>
#include <string.h>

#define CAP_SHM       20
#define QUEUE_SIZE    64
#define NUMBER_WORKER 4

struct thread_data {
    int head, tail;
    int count;

    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    void *addr;
    int shm_id, sem_id, msq_id;

    struct message_control ctrl[QUEUE_SIZE];
    bool is_running;
    sigset_t system_set;
};


void *worker_thread(void *arg)
{
    if (arg == NULL) {
        perror("null param.\n");
        return NULL;
    }

    struct thread_data* data = (struct thread_data*)arg;

    while (1) {

        pthread_mutex_lock(&data->mutex);

        while (data->count == 0 && data->is_running) {
            pthread_cond_wait(&data->not_empty, &data->mutex);
        }

        if (data->is_running == false && data->count == 0) {
            pthread_mutex_unlock(&data->mutex);
            break;
        }

        struct message_control ctrl = data->ctrl[data->head];
        data->count--;
        data->head = (data->head + 1) % QUEUE_SIZE;
        pthread_cond_signal(&data->not_full);
        int sem_id = data->sem_id;
        pthread_mutex_unlock(&data->mutex);
        handle_command(ctrl, data->addr, sem_id);

    }

    return NULL;

}


void *sig_thread(void *arg)
{

    if (arg == NULL) {
        perror("arg of signal thread is NULL.\n");
        return NULL;
    }

    struct thread_data *state = (struct thread_data*)(arg);

    sigset_t *set = NULL;
    if (state) {
        set = &state->system_set;
    }
    siginfo_t siginfo;

    while(1) {
        if (sigwaitinfo(set, &siginfo) == -1) {
            perror("sigwaitinfo failed");
            continue;
        }

        int sig_num = siginfo.si_signo;
        switch (sig_num) {
            case SIGTERM:
            case SIGINT: {
                pthread_mutex_lock(&state->mutex);
                state->is_running = false;
                pthread_mutex_unlock(&state->mutex);
                pthread_cond_broadcast(&state->not_empty);
                return NULL;
            }

            case SIGUSR1: {
                printf("sig user 1.\n");
                break;
            }

            default: {
                printf("Sig : %d", sig_num);
                break;
            }
        }

        fflush(stdout);

    }
    return NULL;

}



int system_configuration(struct thread_data *data)
{
    if (data == NULL) {
        perror("system config error: input params.\n");
        return -1;
    }

    data->is_running = true;

    sigset_t origin_set;

    sigemptyset(&data->system_set);

    sigaddset(&data->system_set, SIGUSR1);
    sigaddset(&data->system_set, SIGHUP);
    sigaddset(&data->system_set, SIGTERM);
    sigaddset(&data->system_set, SIGINT);

    pthread_sigmask(SIG_BLOCK, &data->system_set, &origin_set);

    data->head = 0;
    data->tail = 0;
    data->count = 0;

    pthread_mutex_init(&data->mutex, NULL);
    pthread_cond_init(&data->not_empty, NULL);
    pthread_cond_init(&data->not_full, NULL);


    return 0;
}



int main(int argc, char* argv[])
{
    bool force_reset = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--reset") == 0) {
            force_reset = true;
        }
    }

    key_t shm_key, msq_key, sem_key;
    int shm_id, msq_id, sem_id;

    if (create_key_all(&shm_key, &msq_key, &sem_key) != 0) {
        return -1;
    }
    if (get_id_all(&msq_id, &shm_id, &sem_id, &shm_key, &msq_key, &sem_key, force_reset) != 0) {
        return -1;
    }

    void* addr;

    int ret = attach_share_mem(shm_id, 0, &addr);

    if (ret != 0 || addr == NULL) {
        return -1;
    }


    struct shm_data *data = (struct shm_data*)addr;

    if (init_shmem(sem_id, data, CAP_SHM) != 0) {
        detach_share_mem(data);
        return -1;
    }

    printf("System initialized. Waiting for commands on Message Queue...\n");

    struct thread_data thr_data;
    thr_data.addr = data;
    thr_data.is_running = true;
    thr_data.msq_id = msq_id;
    thr_data.sem_id = sem_id;
    thr_data.shm_id = shm_id;



    if (system_configuration(&thr_data) != 0) {
        return -1;
    }

    struct message_control ctrl;

    pthread_t workers[NUMBER_WORKER];
    pthread_t sig;
    pthread_create(&sig, NULL, sig_thread, &thr_data);

    for (int i = 0; i< NUMBER_WORKER; ++i) {
        pthread_create(&workers[i], NULL, worker_thread, &thr_data);
    }

    while (1) {
        pthread_mutex_lock(&thr_data.mutex);
        bool running = thr_data.is_running;
        pthread_mutex_unlock(&thr_data.mutex);
        if (!running) {
            break;
        }

        if (recv_from_queue(msq_id, &ctrl, sizeof(struct message_control) - sizeof(long), 0, 0) != -1) {
            if (ctrl.mtype == SHUTDOWN) {
                pthread_mutex_lock(&thr_data.mutex);
                thr_data.is_running = false;
                pthread_mutex_unlock(&thr_data.mutex);
                break;
            }

            pthread_mutex_lock(&thr_data.mutex);

            while (thr_data.count == QUEUE_SIZE && thr_data.is_running) {
                pthread_cond_wait(&thr_data.not_full, &thr_data.mutex);
            }

            thr_data.ctrl[thr_data.tail] = ctrl;
            thr_data.tail = (thr_data.tail + 1) % QUEUE_SIZE;
            thr_data.count++;

            pthread_cond_signal(&thr_data.not_empty);

            pthread_mutex_unlock(&thr_data.mutex);


        } else {
            perror("Failed to receive message from queue");
        }
    }

    pthread_mutex_lock(&thr_data.mutex);
    thr_data.is_running = false;

    pthread_cond_broadcast(&thr_data.not_empty);
    pthread_mutex_unlock(&thr_data.mutex);

    for (int i = 0; i < NUMBER_WORKER; i++) {
        pthread_join(workers[i], NULL);
    }

    pthread_join(sig, NULL);
    pthread_mutex_destroy(&thr_data.mutex);
    pthread_cond_destroy(&thr_data.not_empty);
    pthread_cond_destroy(&thr_data.not_full);

    printf("Detaching shared memory...\n");
    detach_share_mem(data);

    return 0;
}