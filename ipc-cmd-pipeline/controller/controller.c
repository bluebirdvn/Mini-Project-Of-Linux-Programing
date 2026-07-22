#include <asm-generic/errno-base.h>
#include <bits/pthreadtypes.h>
#include <bits/types/error_t.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdbool.h>
#include <pthread.h>

#define  PATH_CMD_FIFO "/tmp/ipc_cmd_pipeline.fifo"
#define  PATH_LOG_FIFO "/tmp/ipc_log_pipeline.fifo"

#define  PATH_CMD_ACK "/tmp/ipc_ack.fifo"
#define  RESEND_NUM 3

#define TIMEOUT 3

struct thread_data {
    pthread_cond_t is_ack;
    
    int last_send;
    int ack_send;

    int send_fd, recv_fd;
    bool is_running;

    pthread_mutex_t mutex;
};

struct ack_data {
    int num_pkt;
    bool ack;
};

typedef enum command {
    PING = 0,
    GET_DATA,
    SEND_CMD,
    CHECK_SEC,
    CHECK_STATUS,
    NONE
};
struct packet {
    int num_pkt;
    enum command cmd;
    char pri_data[20];
};


enum command cmd_rand() {

    int a = rand() % 4;
    switch(a) {
        case 0 :{
            return PING;
        }
        case 1 :{
            return GET_DATA;
        }
        case 2 :{
            return SEND_CMD;
        }
        case 3 :{
            return CHECK_SEC;
        }
        case 4 :{
            return CHECK_STATUS;
        }
        default: {
            return NONE;
        }
    }
};

void convert_to_packet(enum command a, struct packet *pkt) {
    if (!pkt) {
        perror("str is nullptr");
        return;

    }

    pkt->cmd = a;

    switch ((int)a) {
        case 0: {
            strcpy(pkt->pri_data, "CMD 0: PING");
            break;
        }

        case 1: {
            strcpy(pkt->pri_data, "CMD 2: GET_DATA");
            break;
        }

        case 2: {
            strcpy(pkt->pri_data, "CMD 0: SEND_CMD");
            break;
        }

        case 3: {
            strcpy(pkt->pri_data, "CMD 0: CHECK_SEC");
            break;
        }

        case 4: {
            strcpy(pkt->pri_data, "CMD 0: CHECK_STATUS");
            break;
        }

        default:{
            strcpy(pkt->pri_data, "CMD UNDEFINED");
            break;
        }
        
    }

}

void sleep_random(void)
{
    int a = rand() % 10;
    printf("sleep for %d seconds....\n", a);
    sleep(a);
}

void *recv_ack_thread(void *arg)
{
    if (!arg) {
        perror("arg is nullptr.\n");
        return NULL;
    }

    struct thread_data* status = (struct thread_data*)(arg);

    bool is_running = true;
    while (is_running) {
        struct ack_data ack;
        int ret = read(status->recv_fd, &ack, sizeof(struct ack_data));
        if (ret < 0) {
            if (errno == EINTR) {
                printf("err by signal");
            } else {
                pthread_mutex_lock(&status->mutex);
                status->is_running = false;
                pthread_mutex_unlock(&status->mutex);

                pthread_cond_signal(&status->is_ack);
                break;
            }


        } else if (ret == 0) {
            printf("End of fifo.\n");
                pthread_mutex_lock(&status->mutex);
                status->is_running = false;
                pthread_mutex_unlock(&status->mutex);

                pthread_cond_signal(&status->is_ack);
        } else {
            int last_send;

            pthread_mutex_lock(&status->mutex);
            last_send = status->last_send;
            status->ack_send = ack.num_pkt;
            pthread_mutex_unlock(&status->mutex);

            pthread_cond_signal(&status->is_ack);
        }

    }
 
}


void *send_cmd(void* arg)
{
    if (!arg) {
        perror("arg is nullptr.\n");
        return NULL;
    }

    struct thread_data* status = (struct thread_data*)(arg);

    if (!status->is_running) {
        printf("runing is false.\n");
        return NULL;
    }

    bool is_running = true;
    while(is_running) {
        enum command a = cmd_rand();
        struct packet pkt;
        convert_to_packet(a, &pkt);
        int try_resend = 0;

        pthread_mutex_lock(&status->mutex);
        status->last_send++;
        pkt.num_pkt = status->last_send;
        pthread_mutex_unlock(&status->mutex);

        struct timespec time;
        time.tv_sec = TIMEOUT;
        time.tv_nsec = 0;

        while (try_resend < RESEND_NUM && is_running) {
            int ret = write(status->send_fd, &pkt, sizeof(struct packet));
            try_resend++;
            if (ret < 0) {
                perror("send data failed\n");
            }

            if (pthread_cond_timedwait(&status->is_ack, &status->mutex, &time) != 0 && is_running) {
                printf("timeout for ack.\nRESEND.\n");
                continue;
            } else {
                pthread_mutex_lock(&status->mutex);
                if (status->last_send != status->ack_send) {
                    pthread_mutex_unlock(&status->mutex);
                    printf("ACK is not for this packet.\nRESEND");
                    continue;
                } else {
                    pthread_mutex_unlock(&status->mutex);
                    printf("GET ACK.\n");
                    break;

                }
            }

            pthread_mutex_lock(&status->mutex);
            is_running = status->is_running;
            pthread_mutex_lock(&status->mutex);


        }


        sleep_random();
    }
    
    return NULL;
}

int main(int argc, char* argv[])
{

    int send_fd = open(PATH_CMD_FIFO, O_WRONLY | O_CLOEXEC);
    if (send_fd < 0) {
        perror("Open fifo failed.\n");
    }
    int recv_fd = open(PATH_CMD_ACK, O_RDONLY | O_CLOEXEC);
    if (recv_fd < 0) {
        perror("Open fifo failed.\n");
    }    
    struct thread_data thread_data;

    thread_data.send_fd = send_fd;
    thread_data.recv_fd = recv_fd;
    thread_data.is_running = true;
    thread_data.last_send = 0;
    pthread_mutex_init(&thread_data.mutex, NULL);
    pthread_cond_init(&thread_data.is_ack, NULL);
    pthread_t send_cmd_thread, recv_ack;
    pthread_create(&send_cmd_thread, NULL, send_cmd, &thread_data);
    pthread_create(&recv_ack, NULL, recv_ack_thread, &thread_data);

    while (thread_data.is_running) {
        sleep(TIMEOUT);
    }


    pthread_join(send_cmd_thread, NULL);
    pthread_join(recv_ack, NULL);


    pthread_mutex_destroy(&thread_data.mutex);
    pthread_cond_destroy(&thread_data.is_ack);

    return 0;
}