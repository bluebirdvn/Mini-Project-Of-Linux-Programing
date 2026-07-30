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
            printf("Received signal %d, initiating shutdown...\n", sig);

            pthread_mutex_lock(&cfg->mutex);
            printf("Get mutex.\n");
            cfg->is_running = false;
            pthread_cond_signal(&cfg->shutdown_cond);
            pthread_mutex_unlock(&cfg->mutex);

            pthread_mutex_lock(&cfg->shm->mutex);
            printf("get mutex in shm\n");
            cfg->shm->shutdown_flag = 1;
            pthread_mutex_unlock(&cfg->shm->mutex);

            struct message_queue msg;
            msg.msq_type = SHUTDOWN;
            msg.client_id = getpid();
            strcpy(msg.message, "SHUTDOWN");
            send_to_queue(cfg->msq, (char*)&msg, sizeof(msg), HIGH_PRI, 3);

            struct mq_attr attr;
            int retry = 10; /* timeout 10 secs */
            while (retry-- > 0) {
                mq_getattr(cfg->msq, &attr);
                if (attr.mq_curmsgs == 0) break;
                sleep(1);
            }

            shutdown_ipc(true, cfg->shm, cfg->shm_fd, cfg->msq);

            return NULL;
        }
    }
    return NULL;
}

void *message_thread(void *arg)
{
    struct system_config *cfg = (struct system_config*)arg;
    int count = 0;

    while (1) {
        pthread_mutex_lock(&cfg->mutex);
        if (!cfg->is_running) {
            pthread_mutex_unlock(&cfg->mutex);
            printf("[Worker] Shutdown requested, exiting.\n");
            return NULL;
        }
        pthread_mutex_unlock(&cfg->mutex);

        struct message_queue msg;
        msg.msq_type = (count % 3) + 1; 
        msg.client_id = getpid();
        snprintf(msg.message, SIZE_MESSAGE, "Test message #%d", count);

        int ret = send_to_queue(cfg->msq, (char*)&msg, sizeof(msg), MID_PRI, 0);
        if (ret != 0) {
            fprintf(stderr, "[Worker] send failed\n");
        } else {
            printf("[Worker] Sent: %s (type=%ld)\n", msg.message, msg.msq_type);
        }

        count++;
        sleep(2);
    }
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
        fprintf(stderr, "open mq failed.\n");
        return EXIT_FAILURE;
    }
    printf("[Main] MQ opened: %s\n", PATH_MQ);

    size_t shm_size = offsetof(struct shm_data, data) + BUFFER_CAPACITY * sizeof(struct sensor_data);
    printf("[Main] shm_size = %zu bytes\n", shm_size);
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

    pthread_t sig_tid, worker_tid;
    if (pthread_create(&sig_tid, NULL, sig_thread, &cfg) != 0) {
        perror("pthread_create sig");
        goto cleanup;
    }
    if (pthread_create(&worker_tid, NULL, message_thread, &cfg) != 0) {
        perror("pthread_create worker");
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
    pthread_join(worker_tid, NULL);

    pthread_mutex_destroy(&cfg.mutex);
    pthread_cond_destroy(&cfg.shutdown_cond);

    printf("[Main] Clean exit.\n");
    return EXIT_SUCCESS;

cleanup:
    munmap(shm, shm_size);
    close(shm_fd);
    mq_close(msq);
    shm_unlink(PATH_SHM);
    mq_unlink(PATH_MQ);
    return EXIT_FAILURE;
}