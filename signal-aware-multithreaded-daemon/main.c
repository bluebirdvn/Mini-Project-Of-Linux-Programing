
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/queue.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <stdbool.h>

/*For inotyfy:>>*/

#include <sys/inotify.h>


#include "notify_sig.h"

#define QUEUE_CAP 32
#define MAX_WORKER 5

#define BUFFER_SIZE 256

#define SAMPLE_RATE "sample_rate"
#define WORKER_DELAY "delay"
#define LOG_LEVEL    "log_level"
typedef enum {
    JOB_NONE = 0,
    JOB_RELOAD_CONFIG,   // Reload config (SIGHUP)
    JOB_PRINT_STATUS,    // status(SIGUSR1)
    JOB_DO_WORK,         // normal work
    JOB_SHUTDOWN         // (SIGTERM/SIGINT)
} work_type;

struct system_config {
    sigset_t system_set;
};

struct workqueue {
    work_type type;
    int data;
};

struct data {
    struct workqueue queue[QUEUE_CAP];
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;

    int head, tail, count;
};

struct config {
    int sample_rate;
    int woker_delay;
    char log_level[32];
};

struct daemon_state {
    struct data daemon_data;
    struct config daemon_config;

    char path_config[256];
    int reload_config_count;
    int work_done_count;

    int shutting_down;
    pthread_mutex_t state_mutex;

    struct system_config config;
};

struct file_notify {
    void* priv_data;
    int inotify_fd;

};



void watch_handler(const char* path, uint32_t mask, void* priv_data)
{

    if (path == NULL || priv_data == NULL) {
        perror("path and private data are NULL");
        return;
    }
    struct daemon_state* state = (struct daemon_state*)priv_data;


    const char* event_type = "";
    if (mask & IN_MODIFY) {
        event_type = "MODIFY";
    } else if (mask & IN_CREATE) {
        event_type = "CREATE";
    } else if (mask & IN_DELETE) {
        event_type = "DELETE";
    } else if (mask & IN_MOVED_TO) {
        event_type = "MOVE_TO";
    } else if (mask & IN_MOVED_FROM) {
        event_type = "MOVE_FROM";
    }
    
    pthread_mutex_lock(&state->state_mutex);
    if (state->shutting_down != 1) {
        kill(getpid(), SIGHUP);
    }
    pthread_mutex_unlock(&state->state_mutex);

    printf("%s: %s\n", event_type, path);
    fflush(stdout);

}

void *inotify_handler(void* param)
{
    if (!param) {
        perror("private data is nullptr");
        return NULL;
    }

    struct file_notify* notify = (struct file_notify*)param;
    struct daemon_state* state = (struct daemon_state*)(notify->priv_data);
    char buffer_event[2048];

    while(1) {
        ssize_t len = read(notify->inotify_fd, buffer_event, sizeof(buffer_event));
        if (len == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("read inotify failed.\n");
                break;
            }

        }
        for (char *ptr = buffer_event; ptr < buffer_event + len; ) {
            struct inotify_event *event = (struct inotify_event*)(ptr);

            if (event->len > 0) {
                watch_handler(event->name, event->mask, state);


            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
        fflush(stdout);
    }

    close(notify->inotify_fd);
    
    EXIT_SUCCESS;
}    

int convert_signal_to_string(int sig_num, char *str, work_type* type) {
    if (!str) {
        return -1;
    }
    if (sig_num == SIGTERM) {
        strcpy(str, "SIG_TERM");
        *type = JOB_SHUTDOWN;
    } else if (sig_num == SIGUSR1) {
        *type = JOB_DO_WORK;
        strcpy(str, "SIGUSR1");
    } else if (sig_num == SIGHUP) {
        *type = JOB_RELOAD_CONFIG;
        strcpy(str, "SIGHUP");
    } else if (sig_num == SIGINT) {
        *type = JOB_SHUTDOWN;
        strcpy(str, "SIGINT");
    } else {
        *type = JOB_NONE;
        strcpy(str, "UNDEFINED SIG");
    }

    return 0;
}

int enqueue_work(struct workqueue *work, struct daemon_state *state) {
    if (!work || !state) {
        perror("work is NULL");
        return -1;
    }
    // pthread_mutex_lock(&state->state_mutex);
    pthread_mutex_lock(&state->daemon_data.mutex);

    while(state->daemon_data.count == QUEUE_CAP) {
        pthread_cond_wait(&state->daemon_data.not_full, &state->daemon_data.mutex);
    }

    if (state->daemon_data.count == QUEUE_CAP) {
        pthread_cond_signal(&state->daemon_data.not_empty);
        perror("Queue is full.\n");
        return -1;
    }
    state->daemon_data.queue[state->daemon_data.tail] = *work;

    state->daemon_data.tail = (state->daemon_data.tail + 1)% QUEUE_CAP;
    state->daemon_data.count = (state->daemon_data.count) + 1;
    pthread_cond_signal(&state->daemon_data.not_empty);

    pthread_mutex_unlock(&state->daemon_data.mutex);
    // pthread_mutex_unlock(&state->state_mutex);
    fflush(stdout);
    return 0;
};

int dequeueu_work(struct workqueue *work, struct daemon_state *state) {
    if (!work && !state) {
        perror("work is NULL");
        return -1;
    }
    // pthread_mutex_lock(&state->state_mutex);
    pthread_mutex_lock(&state->daemon_data.mutex);
    while (state->daemon_data.count == 0 && !state->shutting_down) {
        pthread_cond_wait(&state->daemon_data.not_empty, &state->daemon_data.mutex);
    }

    if (state->shutting_down && state->daemon_data.count == 0) {
        pthread_mutex_unlock(&state->daemon_data.mutex);
        return -1;
    }


    if (state->daemon_data.count == 0) {
        pthread_cond_signal(&state->daemon_data.not_full);
        perror("Queue is empty.\n");
        return -1;
    }

    *work = state->daemon_data.queue[state->daemon_data.head];
    state->daemon_data.head = (state->daemon_data.head + 1) % (QUEUE_CAP);
    state->daemon_data.count--;

    pthread_cond_signal(&state->daemon_data.not_full);

    pthread_mutex_unlock(&state->daemon_data.mutex);
    // pthread_mutex_unlock(&state->state_mutex);
    fflush(stdout);
    return 0;
}




int system_config(struct system_config* config)
{
    if (!config) {
        perror("system config is nullptr.\n");
        return -1;
    }
    sigset_t orin_set;

    sigemptyset(&config->system_set);

    sigaddset(&config->system_set, SIGUSR1);
    sigaddset(&config->system_set, SIGHUP);
    sigaddset(&config->system_set, SIGTERM);
    sigaddset(&config->system_set, SIGINT);

    pthread_sigmask(SIG_BLOCK, &config->system_set, &orin_set);

    return 0;
}

int read_param_config(char *path, const char *param, char *out_value, size_t max_len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("Open file config failed.\n");
        return -1; 
    }
    
    int ret = flock(fd, LOCK_SH | LOCK_NB);
    if (ret != 0) {
        close(fd);
        return -1;
    }

    char buf[BUFFER_SIZE];
    ssize_t read_byte = read(fd, buf, sizeof(buf)-1);
    
    if (read_byte <= 0) {
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    buf[read_byte] = '\0';
    
    char *state;
    char *p = strtok_r(buf, "\n", &state);
    int found = 0;

    while (p != NULL) {
        size_t len = strlen(p);
        if (len > 0 && p[len-1] == '\r') p[len-1] = '\0';

        if (p[0] != '\0' && p[0] != '#') {
            char* delimiter = strchr(p, '=');
            if (delimiter != NULL) {
                *delimiter = '\0';
                if (strcmp(param, p) == 0) {
                    char *output = delimiter + 1;
                    strncpy(out_value, output, max_len);
                    found = 1;
                    break;
                }
            }
        }
        p = strtok_r(NULL, "\n", &state);
    }
    fflush(stdout);
    flock(fd, LOCK_UN);
    close(fd);
    return found;
}


int get_file_config(struct daemon_state* state)
{
    if (!state) return -1;
    
    char val[64];
    
    state->daemon_config.sample_rate = 1000;
    state->daemon_config.woker_delay = 500;
    strcpy(state->daemon_config.log_level, "INFO");

    if (read_param_config(state->path_config, SAMPLE_RATE, val, sizeof(val)) == 1) {
        state->daemon_config.sample_rate = atoi(val);
    }
    if (read_param_config(state->path_config, WORKER_DELAY, val, sizeof(val)) == 1) {
        state->daemon_config.woker_delay = atoi(val);
    }
    if (read_param_config(state->path_config, LOG_LEVEL, val, sizeof(val)) == 1) {
        strncpy(state->daemon_config.log_level, val, sizeof(state->daemon_config.log_level) - 1);
        state->daemon_config.log_level[sizeof(state->daemon_config.log_level) - 1] = '\0';
    }    
    fflush(stdout);
    return 0;
}


int data_config(struct data *data) {
    if (!data) {
        perror("struct data is nullptr.\n");
        return -1;
    }

    data->head = 0;
    data->tail = 0;
    data->count = 0;

    int ret;
    ret = pthread_cond_init(&data->not_empty, NULL);

    if (ret < 0) {
        perror("initialize condition var failed.n\n");
        return ret;
    }

    ret = pthread_cond_init(&data->not_full, NULL);
    if (ret < 0) {
        perror("initialize condition var failed.n\n");
        return ret;
    }

    ret = pthread_mutex_init(&data->mutex, NULL);
    if (ret < 0) {
        perror("initialize mutex failed.n\n");
        return ret;
    }

    memset(data->queue, 0, sizeof(data->queue));

    return 0;
}

int daemon_config(struct daemon_state *daemon)
{
    if (!daemon) {
        perror("daemon is nullptr.\n");
        return -1;
    }

    daemon->reload_config_count = 0;
    daemon->work_done_count = 0;
    daemon->shutting_down = 0;

    pthread_mutex_init(&daemon->state_mutex, NULL);
    int ret = data_config(&daemon->daemon_data);
    if (ret != 0) {
        perror("init data failed.\n");
        return -1;
    }

    ret = system_config(&daemon->config);
    if (ret != 0) {
        perror("init system config failed.\n");
        return -1;
    }
    return 0;
}

int set_path_config(struct daemon_state *state, char* path) {
    if (!state && !path) {
        perror("args are nullptr.\n");
        return -1;
    }

    strcpy(state->path_config, path);

    return 0;
}




void *sig_thread(void *arg)
{

    if (arg == NULL) {
        perror("arg of signal thread is NULL.\n");
        return NULL;
    }

    struct daemon_state *state = (struct daemon_state*)(arg);

    sigset_t *set = NULL;
    if (state) {
        set = &state->config.system_set;
    }
    siginfo_t siginfo;
    for (;;) {
        if (sigwaitinfo(set, &siginfo) == -1) {
            perror("sigwaitinfo failed");
            continue;
        }

        int sig_num = siginfo.si_signo;
        char event[128];
        work_type type;
        int ret = convert_signal_to_string(sig_num, event, &type);
        if (ret != 0) {
            perror("sig_num undefined or str is nullptr.\n");
            continue;
        }

        struct workqueue work;
        work.type = type;
        work.data = sig_num;

        ret = enqueue_work(&work, state);

        if (type == JOB_SHUTDOWN) {
            pthread_mutex_lock(&state->state_mutex);
            state->shutting_down = 1;

            pthread_mutex_unlock(&state->state_mutex);

            pthread_mutex_lock(&state->daemon_data.mutex);
            pthread_cond_broadcast(&state->daemon_data.not_empty); 
            pthread_mutex_unlock(&state->daemon_data.mutex);

            break;
        }

        if (ret < 0) {
            perror("enqueue failed.\n");
            continue;
        }
        fflush(stdout);

    }
    return NULL;

}

void *worker_thread(void* arg)
{
    if (arg == NULL) return NULL;

    struct daemon_state *state = (struct daemon_state*)(arg);
    struct workqueue work;
    
    while (1) {
        if (dequeueu_work(&work, state) != 0) {
            pthread_mutex_lock(&state->state_mutex);
            int is_shutting_down = state->shutting_down;
            pthread_mutex_unlock(&state->state_mutex);

            if (is_shutting_down) {
                break;
            }

            continue;
        }    
        pthread_mutex_lock(&state->state_mutex);
        int is_shutting_down = state->shutting_down;
        pthread_mutex_unlock(&state->state_mutex);
        
        if (is_shutting_down) break;

 

        switch (work.type) {
            case JOB_DO_WORK: {
                pthread_mutex_lock(&state->state_mutex);
                int delay_ms = state->daemon_config.woker_delay;
                pthread_mutex_unlock(&state->state_mutex);
                
                printf("[Worker %lu] Doing work... (delay %d ms)\n", pthread_self(), delay_ms);
                
                usleep(delay_ms * 1000); 
                
                pthread_mutex_lock(&state->state_mutex);
                state->work_done_count++;
                pthread_mutex_unlock(&state->state_mutex);
                break;
            }
            case JOB_PRINT_STATUS: {
                pthread_mutex_lock(&state->state_mutex);
                printf("\n================ DAEMON STATUS ================\n");
                printf("  => Handled by Worker: %lu\n", pthread_self());
                printf("  => Work done        : %d tasks\n", state->work_done_count);
                printf("  => Config reloaded  : %d times\n", state->reload_config_count);
                printf("  => Current Config   : SampleRate=%d, Delay=%dms, Log=%s\n", state->daemon_config.sample_rate, state->daemon_config.woker_delay, state->daemon_config.log_level);
                
                pthread_mutex_lock(&state->daemon_data.mutex);
                printf("  => Queue Status     : %d/%d items\n", state->daemon_data.count, QUEUE_CAP);
                pthread_mutex_unlock(&state->daemon_data.mutex);
                printf("===============================================\n\n");
                pthread_mutex_unlock(&state->state_mutex);
                break;
            }
            case JOB_RELOAD_CONFIG: {
                printf("[Worker %lu] Reloading configuration file...\n", pthread_self());
                
                pthread_mutex_lock(&state->state_mutex);
                get_file_config(state);
                state->reload_config_count++;
                pthread_mutex_unlock(&state->state_mutex);
                
                printf("[Worker %lu] Config reloaded successfully!\n\n", pthread_self());
                break;
            }
            case JOB_SHUTDOWN: {
                printf("[Worker %lu] Received shutdown task. Exiting...\n\n", pthread_self());
                return NULL;
            }
            default: { 
                printf("[Worker %lu] Job undefined.\n\n", pthread_self());
                break;
            }
            fflush(stdout);
        }
    }
    return NULL;
}

int thread_create(pthread_t *thread, int num_workers, struct daemon_state *state) {

    if (!thread) {
        perror("thread array is null.\n");
        return -1;
    }

    if (num_workers < 1) {
        perror("number of worker is zero.\n");
        return -1;
    }

    for (int i = 0; i < num_workers; ++i) {
        pthread_create(&thread[i], NULL, worker_thread, (void*)state);
    }

    pthread_create(&thread[num_workers], NULL, sig_thread, (void*)state);
  
    return 0;
}

int die_thread(pthread_t *thread, int num_workers) {
    if (!thread || num_workers < 1) {
        return -1;
    }
    for (int i = 0; i < num_workers + 1; ++i) {
        pthread_join(thread[i], NULL);
    }
    return 0;
}

int free_resource(struct daemon_state *state) {
    if (!state) {
        return -1;
    }
    pthread_mutex_destroy(&state.state_mutex);
    pthread_mutex_destroy(&state.daemon_data.mutex);
    pthread_cond_destroy(&state.daemon_data.not_empty);
    pthread_cond_destroy(&state.daemon_data.not_full);

    return 0;
}


int main(int argc, char* argv[])
{
    pthread_t threads[MAX_WORKER + 1]; 
    struct daemon_state state;

    if (daemon_config(&state) != 0) {
        fprintf(stderr, "Failed to initialize daemon configuration.\n");
        return EXIT_FAILURE;
    }

    char *config_file = (argc > 1) ? argv[1] : "./config/config.txt";
    set_path_config(&state, config_file);

    get_file_config(&state);

    printf("Daemon is starting with %d workers...\n", MAX_WORKER);

    struct file_notify notify;
    notify.priv_data = &state;
    int  ret = watch_config_dir(state.path_config);
    if (ret < 0) {
        perror("watch dir config failed.\n");
        return -1;
    }

    notify.inotify_fd = ret;

    if (thread_create(threads, MAX_WORKER, &state) != 0) {
        free_resource(&state);
        return -1;
    }



    pthread_t inotify_thr;
    pthread_create(&inotify_thr, NULL, inotify_handler, &notify);
    
    if (die_thread(threads, MAX_WORKER) != 0) {
        free_resource(&state);
        return -1;
    }

    pthread_join(inotify_thr, NULL);

    free_resource(&state);

    printf("Daemon shut down gracefully.\n");
    return EXIT_SUCCESS;
}
