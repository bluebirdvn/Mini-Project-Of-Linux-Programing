

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

struct thread_data {
    pthread_cond_t has_data;
    int send_fd, recv_fd, send_log_fd;
    bool is_running;
    struct packet data;
    pthread_mutex_t mutex;
};

int get_data_task(struct packet *pkt, int fd)
{
    printf("Task get data execute.\n");
    int ret = write(fd, pkt->pri_data, sizeof(pkt->pri_data));
    
    if (ret < 0) {
        if (errno == EINTR) {
            printf("erorr because of interrupt.\n");
            return -1;
        }
    } else if (ret == 0) {
        printf("end of read fifo.\n");
        return -2;
    } else {
        printf("send successful to log.\n");
    }

    return 0;
}

int send_cmd_task(struct packet *pkt, int fd)
{
    printf("Task send cmd execute.\n");
    int ret = write(fd, pkt->pri_data, sizeof(pkt->pri_data));
    
    if (ret < 0) {
        if (errno == EINTR) {
            printf("erorr because of interrupt.\n");
            return -1;
        }
    } else if (ret == 0) {
        printf("end of read fifo.\n");
        return -2;
    } else {
        printf("send successful to log.\n");
    }

    return 0;
}
int check_sec_task(struct packet *pkt, int fd)
{
    printf("Task check sec execute.\n");
    int ret = write(fd, pkt->pri_data, sizeof(pkt->pri_data));
    
    if (ret < 0) {
        if (errno == EINTR) {
            printf("erorr because of interrupt.\n");
            return -1;
        }
    } else if (ret == 0) {
        printf("end of read fifo.\n");
        return -2;
    } else {
        printf("send successful to log.\n");
    }

    return 0;
}
int check_status_task(struct packet *pkt, int fd)
{
    printf("Task check status execute.\n");
    int ret = write(fd, pkt->pri_data, sizeof(pkt->pri_data));
    
    if (ret < 0) {
        if (errno == EINTR) {
            printf("erorr because of interrupt.\n");
            return -1;
        }
    } else if (ret == 0) {
        printf("end of read fifo.\n");
        return -2;
    } else {
        printf("send successful to log.\n");
    }

    return 0;
}

int ping_task(struct packet *pkt, int fd)
{
    printf("Task ping execute.\n");
    int ret = write(fd, pkt->pri_data, sizeof(pkt->pri_data));
    
    if (ret < 0) {
        if (errno == EINTR) {
            printf("erorr because of interrupt.\n");
            return -1;
        }
    } else if (ret == 0) {
        printf("end of read fifo.\n");
        return -2;
    } else {
        printf("send successful to log.\n");
    }

    return 0;
}


void *worker_thread(void *arg)
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
        pthread_mutex_lock(&status->mutex);
        if (pthread_cond_wait(&status->has_data, &status->mutex) != 0 && is_running) {
            perror("Wait error");
            if (is_running != 1) {
                pthread_mutex_unlock(&status->mutex);
                break;
            } 
            if (errno == EBUSY) {
                printf("another thread is waitting .\n");
                pthread_mutex_unlock(&status->mutex);

                continue;
            }
        } else {
            struct packet pkt = status->data;
            pthread_mutex_unlock(&status->mutex);

            switch ((int)pkt.cmd) {
                case 0: {
                    ping_task(&pkt, status->send_log_fd);
                    
                    break;
                }
                case 1: {
                    get_data_task(&pkt, status->send_log_fd);
                    
                    break;
                }
                case 2: {
                    send_cmd_task(&pkt, status->send_log_fd);
                    
                    break;
                }
                case 3: {
                    check_sec_task(&pkt, status->send_log_fd);
                
                    break;
                }
                case 4: {
                    check_status_task(&pkt, status->send_log_fd);
                    
                    break;
                }
                default: {
                    printf("not a situation.\n");
                    break;
                }
                   
            }
        }
        

    }
    
    return NULL;
}

void *recv_data_thread(void *arg)
{
    if (!arg) {
        perror("arg is nullptr.\n");
        return NULL;
    }

    struct thread_data* status = (struct thread_data*)(arg);

    bool is_running = true;
    while (is_running) {
        struct packet pkt;
        int ret = read(status->recv_fd, &pkt, sizeof(struct packet));
        if (ret < 0) {
            if (errno == EINTR) {
                printf("err by signal");
            } else {
                pthread_mutex_lock(&status->mutex);
                status->is_running = false;
                is_running = status->is_running;
                pthread_cond_signal(&status->has_data);
                pthread_mutex_unlock(&status->mutex);

                break;
            }


        } else if (ret == 0) {
            printf("End of fifo.\n");
            pthread_mutex_lock(&status->mutex);
            status->is_running = false;
            is_running = status->is_running;
            pthread_mutex_unlock(&status->mutex);

            pthread_cond_signal(&status->has_data);
        } else {
            printf("GET data from fifo command.\n");
            struct ack_data ack;
            ack.num_pkt = pkt.num_pkt;
            ack.ack = true;
            
            int ret = write(status->send_fd, &ack, sizeof(ack));
            if (ret < 0) {
                if (errno == EINTR) {
                    printf("write error by interrupt.\n");
                } else {
                    perror("erorr in write");

                }
            } else if (ret == 0) {
                perror("end of write.\n");
                continue;
            } else {
                printf("Send ack successfull.\n");
                pthread_mutex_lock(&status->mutex);
                status->data = pkt;
                pthread_mutex_unlock(&status->mutex);

                pthread_cond_signal(&status->has_data);
            }
        }

    }
 
}



int main(int argc, char* argv[])
{


    int send_ack = open(PATH_CMD_ACK, O_WRONLY | O_CLOEXEC);
    if (send_ack < 0) {
        perror("Open fifo failed.\n");
    }
    int recv_fd = open(PATH_CMD_FIFO, O_RDONLY | O_CLOEXEC);
    if (recv_fd < 0) {
        perror("Open fifo failed.\n");
    }    
    int send_log_fd = open(PATH_LOG_FIFO, O_WRONLY | O_CLOEXEC);
    if (recv_fd < 0) {
        perror("Open fifo failed.\n");
    }      

    struct thread_data thread_data;

    thread_data.send_log_fd = send_log_fd;
    thread_data.send_fd = send_ack;
    thread_data.recv_fd = recv_fd;
    thread_data.is_running = true;


    pthread_mutex_init(&thread_data.mutex, NULL);
    pthread_cond_init(&thread_data.has_data, NULL);
    pthread_t send_cmd_thread, recv_ack;
    pthread_create(&send_cmd_thread, NULL, worker_thread, &thread_data);
    pthread_create(&recv_ack, NULL, recv_data_thread, &thread_data);

    while (thread_data.is_running) {
        sleep(TIMEOUT);
    }


    pthread_join(send_cmd_thread, NULL);
    pthread_join(recv_ack, NULL);


    pthread_mutex_destroy(&thread_data.mutex);
    pthread_cond_destroy(&thread_data.has_data);

    return 0;
}