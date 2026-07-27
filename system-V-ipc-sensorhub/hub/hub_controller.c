#include "common.h"


#include <bits/pthreadtypes.h>
#include <bits/types/sigset_t.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>


struct system_config {
    sigset_t system_set;

    int shm_id;
    int sem_id;
    int msq_id;
    void *addr;

    pthread_mutex_t mutex;
    pthread_cond_t shut_down;

    bool is_running;
};




int system_configuration(struct system_config *config)
{   
    if (config == NULL) {
        perror("system config error: input params.\n");
        return -1;
    }

    config->is_running = true;

    sigset_t origin_set;

    sigemptyset(&config->system_set);

    sigaddset(&config->system_set, SIGUSR1);
    sigaddset(&config->system_set, SIGHUP);
    sigaddset(&config->system_set, SIGTERM);
    sigaddset(&config->system_set, SIGINT);

    pthread_mutex_init(&config->mutex, NULL);
    pthread_cond_init(&config->shut_down, NULL);

    pthread_sigmask(SIG_BLOCK, &config->system_set, &origin_set);

    return 0;
}   



void *sig_thread(void *arg)
{

    if (arg == NULL) {
        perror("arg of signal thread is NULL.\n");
        return NULL;
    }

    struct system_config *state = (struct system_config*)(arg);

    sigset_t *set = NULL;
    if (state) {
        set = &state->system_set;
    }
    siginfo_t siginfo;

    for (;;) {
        if (sigwaitinfo(set, &siginfo) == -1) {
            perror("sigwaitinfo failed");
            continue;
        }

        int sig_num = siginfo.si_signo;
        switch (sig_num) {
            case SIGTERM:
            case SIGINT: {
                struct message_control mes;
                mes.mtype = SHUTDOWN;
                mes.data.id = getpid();
                strcpy(mes.data.message, cmd_to_string(mes.mtype));

                int ret = send_to_queue(state->msq_id, &mes, sizeof(struct message_control) - sizeof(long), 0);
                if (ret != 0) {
                      perror("send failed.\n");
                }
                pthread_mutex_lock(&state->mutex);
                state->is_running = false;
                pthread_cond_signal(&state->shut_down);
                pthread_mutex_unlock(&state->mutex);
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


void *controller_thread(void* arg)
{
    if (arg == NULL) {
        perror("arg of signal thread is NULL.\n");
        return NULL;
    }

    struct system_config *state = (struct system_config*)(arg);

    int count = 1;

    while(1) {
        pthread_mutex_lock(&state->mutex);
        if (!state->is_running) {
            pthread_mutex_unlock(&state->mutex);

            break;
        }
        pthread_mutex_unlock(&state->mutex);

        struct message_control mes;
        mes.mtype = (enum type_message)count%5;
        mes.data.id = getpid();
        strcpy(mes.data.message, cmd_to_string(mes.mtype));

        int ret = send_to_queue(state->msq_id, &mes, sizeof(struct message_control) - sizeof(long), 0);
        if (ret < 0) {
            perror("send to queue failed.\n");
            continue;
        }
        ++count;

        if (count == 5) {
            count = 1;
        }

        printf("send to queue successfully\n");
        fflush(stdout);
        sleep(1);

    }

    return NULL;
}



void *cleaning_thread(void* arg)
{
    if (arg == NULL) {
        perror("arg of signal thread is NULL.\n");
        return NULL;
    }

    struct system_config *state = (struct system_config*)(arg);
    while(1) {
        pthread_mutex_lock(&state->mutex);
        while(state->is_running) {
            pthread_cond_wait(&state->shut_down, &state->mutex);
        }
        pthread_mutex_unlock(&state->mutex);
        sleep(2);
        detach_share_mem(state->addr);
        cleaning(state->shm_id, state->msq_id, state->sem_id);
        break;
        
    }
    
    return NULL;    
}


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    key_t shm_key, msq_key, sem_key;
    int shm_id, msq_id, sem_id;

    int ret = create_key_all(&shm_key, &msq_key, &sem_key);

    if (ret != 0) {
        return -1;
    }

    ret = get_id_all(&msq_id, &shm_id, &sem_id, &shm_key, &msq_key, &sem_key, false);

    if (ret != 0) {
        return -1;
    }
    
    void* addr;

    ret = attach_share_mem(shm_id, 0, &addr);

    if (ret != 0 || addr == NULL) {
        return -1;
    }


    struct shm_data *data = (struct shm_data*)addr; 

    ret = init_shmem(sem_id, data, CAP_SHM);

    if (ret != 0) {
        return -1;
    }

    struct system_config config;
    config.msq_id = msq_id;
    config.sem_id = sem_id;
    config.shm_id = shm_id;
    config.addr = data;

    system_configuration(&config);
    pthread_t sig_tid, controller_tid, cleaner_tid;

    pthread_create(&sig_tid, NULL, sig_thread, &config);
    pthread_create(&controller_tid, NULL, controller_thread, &config);
    pthread_create(&cleaner_tid, NULL, cleaning_thread, &config);

    pthread_join(sig_tid, NULL);
    pthread_join(controller_tid, NULL);
    pthread_join(cleaner_tid, NULL);


    
    return 0;
}