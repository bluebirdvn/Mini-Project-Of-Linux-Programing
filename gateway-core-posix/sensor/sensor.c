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
            printf("[sig_thread] Received signal %d, initiating shutdown...\n", sig);

            pthread_mutex_lock(&cfg->mutex);
            cfg->is_running = false;
            pthread_cond_signal(&cfg->shutdown_cond);
            pthread_mutex_unlock(&cfg->mutex);

            /* Bao cho message_thread biet la dang shutdown, de no
             * chay drain_mq() truoc khi thoat. */
            pthread_mutex_lock(&cfg->shm->mutex);
            cfg->shm->shutdown_flag = 1;
            pthread_mutex_unlock(&cfg->shm->mutex);

            return NULL;
        }
    }
}

/*
 * message_thread: nhan message tu queue va goi command_handling().
 * Khi is_running == false, drain het cac message con ton dong
 * (neu shutdown_flag da duoc bat) roi thoat vong lap. KHONG tu
 * giai phong tai nguyen (shm/mq) o day.
 */
void *message_thread(void *arg)
{
    struct system_config *cfg = (struct system_config*)arg;
    struct message_queue msg;

    while (1) {
        pthread_mutex_lock(&cfg->mutex);
        if (!cfg->is_running) {
            pthread_mutex_unlock(&cfg->mutex);

            bool need_drain = false;
            pthread_mutex_lock(&cfg->shm->mutex);
            if (cfg->shm->shutdown_flag) {
                need_drain = true;
            }
            pthread_mutex_unlock(&cfg->shm->mutex);

            if (need_drain) {
                if (drain_mq(cfg->msq, cfg->shm, cfg->shm_fd) != 0) {
                    fprintf(stderr, "[message_thread] drain_mq failed.\n");
                } else {
                    printf("[message_thread] drain_mq completed.\n");
                }
            }

            printf("[message_thread] Exiting (shutdown requested).\n");
            return NULL;
        }
        pthread_mutex_unlock(&cfg->mutex);

        ssize_t n = recv_from_queue(cfg->msq, (char*)&msg, sizeof(msg), 0, 1);
        if (n > 0) {
            printf("[message_thread] Command: %s (type=%ld)\n", msg.message, msg.msq_type);
            command_handling(msg, cfg->shm, cfg->shm_fd, cfg->msq);
        } else if (n == -1 && errno != ETIMEDOUT && errno != EINTR) {
            perror("recv_from_queue");
            break;
        }
    }
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
        printf("[main] SHM initialized.\n");
    } else {
        printf("[main] SHM already exists.\n");
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

    shutdown_ipc(false, cfg.shm, cfg.shm_fd, cfg.msq);

    pthread_mutex_destroy(&cfg.mutex);
    pthread_cond_destroy(&cfg.shutdown_cond);

    printf("[main] Clean exit.\n");
    return EXIT_SUCCESS;

cleanup:
    if (shm) munmap(shm, shm_size);
    if (shm_fd != -1) close(shm_fd);
    if (msq != -1) mq_close(msq);
    shm_unlink(PATH_SHM);
    mq_unlink(PATH_MQ);
    return EXIT_FAILURE;
}