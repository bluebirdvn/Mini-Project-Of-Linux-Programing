#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include "common.h"

struct system_config {
    sigset_t system_set;            
    struct shm_data *shm;           
    mqd_t msq;                     
    int shm_fd;                     
    pthread_mutex_t mutex;          
    pthread_cond_t shutdown_cond;   
    bool is_running;                
};

void *sig_thread(void *arg)
{
    struct system_config *cfg = (struct system_config*)arg;
    siginfo_t siginfo;

    for (;;) {
        if (sigwaitinfo(&cfg->system_set, &siginfo) == -1) {
            perror("sigwaitinfo");
            continue;
        }

        int sig = siginfo.si_signo;
        if (sig == SIGINT || sig == SIGTERM) {
            printf("[SigThread] Received signal %d, initiating shutdown...\n", sig);

            // 1. Đánh dấu dừng
            pthread_mutex_lock(&cfg->mutex);
            cfg->is_running = false;
            pthread_cond_signal(&cfg->shutdown_cond);
            pthread_mutex_unlock(&cfg->mutex);

            pthread_mutex_lock(&cfg->shm->mutex);
            cfg->shm->shutdown_flag = 1;
            pthread_mutex_unlock(&cfg->shm->mutex);

            struct message_queue msg;
            msg.msq_type = SHUTDOWN;
            msg.client_id = getpid();
            strcpy(msg.message, "SHUTDOWN");
            send_to_queue(cfg->msq, (char*)&msg, sizeof(msg), HIGH_PRI, 0);

            struct mq_attr attr;
            int retry = 10;
            while (retry-- > 0) {
                mq_getattr(cfg->msq, &attr);
                if (attr.mq_curmsgs == 0) break;
                sleep(1);
            }

            shutdown_ipc(false, cfg->shm, cfg->shm_fd, cfg->msq);

            return NULL;
        }
    }
    return NULL;
}

void *monitor_thread(void *arg)
{
    struct system_config *cfg = (struct system_config*)arg;
    struct shm_data *shm = cfg->shm;
    struct mq_attr mq_attr;

    while (1) {
        pthread_mutex_lock(&cfg->mutex);
        if (!cfg->is_running) {
            pthread_mutex_unlock(&cfg->mutex);
            break;
        }
        pthread_mutex_unlock(&cfg->mutex);

        pthread_mutex_lock(&shm->mutex);
        if (shm->read_index != shm->write_index) {
            uint32_t last = (shm->write_index == 0) ? shm->capacity - 1 : shm->write_index - 1;
            struct sensor_data latest = shm->data[last];
            printf("[Monitor] Latest sensor: id=%d, h=%d, m=%d, motion=%d, ts=%lu\n",
                   latest.id_sensor, latest.humidity, latest.moisture, latest.motion, latest.timestamp);
        } else {
            printf("[Monitor] Buffer empty.\n");
        }

        uint32_t count = (shm->write_index >= shm->read_index) ?
                         (shm->write_index - shm->read_index) :
                         (shm->capacity - shm->read_index + shm->write_index);
        printf("[Monitor] Buffer usage: %u/%u slots, dropped=%u\n",
               count, shm->capacity, shm->dropped);
        if (count > shm->capacity * 0.9) {
            fprintf(stderr, "[Monitor] WARNING: Buffer nearly full!\n");
        }
        pthread_mutex_unlock(&shm->mutex);

        if (mq_getattr(cfg->msq, &mq_attr) == 0) {
            printf("[Monitor] MQ: %ld messages waiting\n", mq_attr.mq_curmsgs);
            if (mq_attr.mq_curmsgs > 200) {
                fprintf(stderr, "[Monitor] WARNING: MQ has too many messages!\n");
            }
        }

        pthread_mutex_lock(&cfg->mutex);
        if (!cfg->is_running) {
            pthread_mutex_unlock(&cfg->mutex);
            break;
        }
        pthread_mutex_unlock(&cfg->mutex);

        sleep(2); 
    }
    printf("[Monitor] Exiting.\n");
    return NULL;
}

int system_configuration(struct system_config *cfg)
{
    if (!cfg) return -1;

    cfg->is_running = true;
    pthread_mutex_init(&cfg->mutex, NULL);
    pthread_cond_init(&cfg->shutdown_cond, NULL);

    sigemptyset(&cfg->system_set);
    sigaddset(&cfg->system_set, SIGINT);
    sigaddset(&cfg->system_set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &cfg->system_set, NULL);

    return 0;
}

int main()
{
    struct mq_attr attr = {
        .mq_maxmsg = MAX_MSQ,
        .mq_msgsize = SIZE_MSQ,
        .mq_flags = 0
    };
    mqd_t msq = get_mq(PATH_MQ, O_CREAT | O_RDWR, 0666, &attr);
    if (msq == -1) {
        fprintf(stderr, "Failed to open MQ\n");
        return EXIT_FAILURE;
    }
    printf("[Main] MQ opened: %s\n", PATH_MQ);

    size_t shm_size = offsetof(struct shm_data, data) + BUFFER_CAPACITY * sizeof(struct sensor_data);
    int shm_fd = get_shm(PATH_SHM, O_CREAT | O_EXCL | O_RDWR, 0666, shm_size);
    if (shm_fd == -1) {
        perror("get_shm");
        mq_close(msq);
        return EXIT_FAILURE;
    }

    struct shm_data *shm;
    if (mapping_shm(shm_fd, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, (void**)&shm) == -1) {
        perror("mapping_shm");
        close(shm_fd);
        mq_close(msq);
        return EXIT_FAILURE;
    }

    if (shm->hdr.magic != SHM_MAGIC) {
        shm->capacity = BUFFER_CAPACITY;
        shm->shutdown_flag = 0;
        if (init_shm(true, shm) == -1) {
            perror("init_shm");
            munmap(shm, shm_size);
            close(shm_fd);
            mq_close(msq);
            return EXIT_FAILURE;
        }
        printf("[Main] SHM initialized.\n");
    } else {
        printf("[Main] SHM already exists.\n");
    }

    struct system_config cfg = {
        .shm = shm,
        .msq = msq,
        .shm_fd = shm_fd,
        .is_running = true
    };
    system_configuration(&cfg);

    pthread_t sig_tid, mon_tid;
    if (pthread_create(&sig_tid, NULL, sig_thread, &cfg) != 0) {
        perror("pthread_create sig");
        goto cleanup;
    }
    if (pthread_create(&mon_tid, NULL, monitor_thread, &cfg) != 0) {
        perror("pthread_create monitor");
        pthread_cancel(sig_tid);
        pthread_join(sig_tid, NULL);
        goto cleanup;
    }

    pthread_mutex_lock(&cfg.mutex);
    while (cfg.is_running) {
        pthread_cond_wait(&cfg.shutdown_cond, &cfg.mutex);
    }
    pthread_mutex_unlock(&cfg.mutex);

    pthread_join(sig_tid, NULL);
    pthread_join(mon_tid, NULL);

    pthread_mutex_destroy(&cfg.mutex);
    pthread_cond_destroy(&cfg.shutdown_cond);

    printf("[Main] Clean exit.\n");
    return EXIT_SUCCESS;

cleanup:
    if (shm) munmap(shm, shm_size);
    if (shm_fd != -1) close(shm_fd);
    if (msq != -1) mq_close(msq);
    shm_unlink(PATH_SHM);
    mq_unlink(PATH_MQ);
    return EXIT_FAILURE;
}