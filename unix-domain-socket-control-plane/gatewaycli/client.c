#include "common.h"
#include "logger.h"
#include "server.h"
#include "client.h"


#include <asm-generic/errno-base.h>
#include <bits/pthreadtypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

void *sig_thread(void *arg);
void *recv_thread(void *arg);
void *send_thread(void *arg);
void *worker_thread(void *arg);
void *task_create_thread(void *arg);

int init_client(struct client_state* client)
{
    if (client == NULL) {
        LOG_ERROR("null param");
        return -1;
    }
    client->num_packet_recv = 0;
    client->num_packet_sent = 0;

    pthread_mutex_init(&client->lock, NULL);
    pthread_cond_init(&client->stop, NULL);
    pthread_mutex_init(&client->sent_lock, NULL);
    pthread_mutex_init(&client->recv_lock, NULL);
    client->shutdown = false;
    signal(SIGPIPE, SIG_IGN);

    sigemptyset(&client->system_set);
    sigaddset(&client->system_set, SIGINT);
    sigaddset(&client->system_set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &client->system_set, &client->origin_set);

    client->server_fd = connect_to_server(PATH_SOCKET);
    if (client->server_fd == -1) {
        LOG_ERROR("Failed to connect to server");
        free(client);
        return -1;
    }
    LOG_INFO("Connected to server successfully!\n");



    struct message_control token_msg;
    const char* fixed_token = "MY_FIXED_SECRET_TOKEN_123456789"; 
    
    build_message(fixed_token, IDENTIFY, HIGH, &token_msg, client->num_packet_sent);
    
    if (send_framed_message(client->server_fd, &token_msg, 0) != 0) {
        LOG_ERROR("Failed to send initial token");
        close(client->server_fd);
        free(client);
        return -1;
    }
    LOG_INFO("Initial token sent successfully.\n");

    return 0;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    log_init("../log/clientlog");

    struct client_state *client = (struct client_state *)calloc(1, sizeof(struct client_state));
    if (!client) {
        perror("Failed to allocate client_state");
        return EXIT_FAILURE;
    }

    int ret = init_client(client);

    if (ret == -1) {
        return -1;
    }
    pthread_t tid_sig, tid_recv, tid_send, tid_worker, tid_task;

    pthread_create(&tid_sig, NULL, sig_thread, client);
    pthread_create(&tid_recv, NULL, recv_thread, client);
    pthread_create(&tid_send, NULL, send_thread, client);
    pthread_create(&tid_worker, NULL, worker_thread, client);
    pthread_create(&tid_task, NULL, task_create_thread, client);

    pthread_mutex_lock(&client->lock);
    while (!client->shutdown) {
        pthread_cond_wait(&client->stop, &client->lock);
    }
    pthread_mutex_unlock(&client->lock);

    pthread_join(tid_sig, NULL);
    pthread_join(tid_recv, NULL);
    pthread_join(tid_send, NULL);
    pthread_join(tid_worker, NULL);
    pthread_join(tid_task, NULL);

    LOG_INFO("Shutting down client...\n");

    close(client->server_fd);
    free(client);

    return 0;
}


void *sig_thread(void *arg)
{
    if (arg == NULL) {
        perror("arg of signal thread is NULL.\n");
        return NULL;
    }

    struct client_state *client = (struct client_state*)(arg);
    sigset_t *set = &client->system_set;
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
                struct message_control msg;
                build_message("client shutting down", SHUTDOWN_PEER, HIGH, &msg, client->num_packet_sent);
                send_framed_message(client->server_fd, &msg, MSG_NOSIGNAL);                
                pthread_mutex_lock(&client->lock);
                client->shutdown = true;
                pthread_cond_broadcast(&client->stop);
                pthread_mutex_unlock(&client->lock);

                return NULL;
            }
            default: {
                printf("Sig : %d", sig_num);
                break;
            }
        }
        fflush(stdout);
    }
    log_close();
    return NULL;
}


void *recv_thread(void *arg)
{
    if (arg == NULL) {
        LOG_ERROR("null params.");
        return NULL;
    }

    struct client_state* client = (struct client_state* )arg;
    struct epoll_event ev;
    ev.data.fd = client->server_fd;
    ev.events = EPOLLIN | EPOLLHUP;

    int epoll_fd;
    epoll_fd = epoll_create(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        if (errno == EINTR) {
            LOG_ERROR("interrupted.");
            return NULL;
        }

        return NULL;
    }

    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client->server_fd, &ev);
    if (ret == -1) {
        LOG_ERROR("errno: %d", errno);
    }

    struct epoll_event events[5];
    while (1)
    {
        pthread_mutex_lock(&client->lock);
        bool should_stop = client->shutdown;
        pthread_mutex_unlock(&client->lock);

        if (should_stop){
             break;
        }

        pthread_mutex_unlock(&client->lock);
        int ret;
        int num_ev = epoll_wait(epoll_fd, events, 5, 5);
        if (num_ev == -1) {
            if (errno == EINTR) {
                LOG_ERROR("epoll was interrupted");
                continue;
            }
            LOG_ERROR("other err %d", errno);
            break;
        }   

        if (num_ev == 0) {
            continue;
        }
        for (int i = 0; i < num_ev; i++) {
            pthread_mutex_unlock(&client->lock);
            if (events[i].data.fd == client->server_fd) {
                ret = read_stage(client);

                if (ret == -1) {
                    return NULL;
                }

                if (ret == 0) {
                    continue;
                }

                if (ret == 2) {
                    LOG_WARN("checksum mismatch on received frame, sending NACK");
                    struct message_control message;
                    build_nack(client->message.header.number_packet, &message);
                    pthread_mutex_lock(&client->sent_lock);
                    ret = enqueue_message(&message, client->sent_queue, client);
                    pthread_mutex_unlock(&client->sent_lock);
                    if (ret == -1) {
                        LOG_ERROR("enqueue failed.");
                        return NULL;
                    }
                    if (ret == 1) {
                        pthread_mutex_lock(&client->sent_lock);
                        enqueue_message(&message, client->sent_queue, client);
                        pthread_mutex_unlock(&client->sent_lock);
                    }
                    continue;
                }

                pthread_mutex_lock(&client->lock);
                client->num_packet_recv++;
                pthread_mutex_unlock(&client->lock);


                if (client->message.header.cmd == SHUTDOWN_PEER) {
                    LOG_INFO("server requested graceful shutdown");
                    pthread_mutex_lock(&client->lock);
                    client->shutdown = true;
                    pthread_cond_broadcast(&client->stop);
                    pthread_mutex_unlock(&client->lock);
                    return NULL;   
                } 

                if (client->message.header.cmd == ACK) {
                    LOG_INFO("server ack for packet number %d", client->message.header.number_packet);
                    continue;
                } else if (client->message.header.cmd == NACK) {
                    LOG_INFO("server ack for packet number %d", client->message.header.number_packet);
                    continue;
                }

                struct message_control message;
                build_ack(client->message.header.number_packet, &message);
                pthread_mutex_lock(&client->sent_lock);
                ret = enqueue_message(&message, client->sent_queue, client);
                pthread_mutex_unlock(&client->sent_lock);

                pthread_mutex_lock(&client->recv_lock);
                ret = enqueue_message(&client->message, client->recv_queue, client);
                pthread_mutex_unlock(&client->recv_lock);

                if (ret == -1) {
                    LOG_ERROR("enqueue failed.");
                    return NULL;
                }
                if (ret == 1) {
                    pthread_mutex_lock(&client->sent_lock);
                    enqueue_message(&message, client->sent_queue, client);
                    pthread_mutex_unlock(&client->sent_lock);
                }
                continue;
            }


        }

    }

    return NULL;
}

void *send_thread(void *arg)
{
    struct client_state* client = (struct client_state*)arg;

    while (1) {
        pthread_mutex_lock(&client->lock);
        bool should_stop = client->shutdown;
        pthread_mutex_unlock(&client->lock);
        if (should_stop) break;

        pthread_mutex_lock(&client->sent_lock);
        if (client->send_count > 0) {
            struct message_control send_message;
            int ret = dequeue_message(&send_message, client->sent_queue, client);
            pthread_mutex_unlock(&client->sent_lock);

            if (ret != 0) continue;

            ret = send_framed_message(client->server_fd, &send_message, MSG_NOSIGNAL);
            if (ret == 1) {
                pthread_mutex_lock(&client->lock);
                client->shutdown = true;
                pthread_cond_broadcast(&client->stop);
                pthread_mutex_unlock(&client->lock);
                LOG_ERROR("server closed.");
                break;
            }
            LOG_INFO("send successfully");

            pthread_mutex_lock(&client->lock);
            client->num_packet_sent++;
            pthread_mutex_unlock(&client->lock);
        } else {
            pthread_mutex_unlock(&client->sent_lock);
        }
        usleep(10000);
    }
    return NULL;
}


void *worker_thread(void *arg)
{
    if (arg == NULL) return NULL;
    struct client_state* client = (struct client_state*)arg;

    while (1) {
        pthread_mutex_lock(&client->lock);
        if (client->shutdown) {
            pthread_mutex_unlock(&client->lock);
            break;
        }
        pthread_mutex_unlock(&client->lock);

        struct message_control msg;
        int ret = -1;

        pthread_mutex_lock(&client->recv_lock);
        if (client->recv_count > 0) {
            ret = dequeue_message(&msg, client->recv_queue, client);
        }
        pthread_mutex_unlock(&client->recv_lock);

        if (ret == 0) {
            LOG_INFO("[Worker] Received message from server - CMD: %d, Payload: %s\n", msg.header.cmd, msg.message);
        } else {
            usleep(10000); 
        }

    }
    return NULL;
}

void *task_create_thread(void *arg)
{
    if (arg == NULL) return NULL;
    struct client_state* client = (struct client_state*)arg;
    
    while (1) {
        
        pthread_mutex_lock(&client->lock);
        uint64_t packet_num = client->num_packet_sent; 
        if (client->shutdown) {
            pthread_mutex_unlock(&client->lock);
            break;
        }
        pthread_mutex_unlock(&client->lock);

        struct message_control msg;
        
        pthread_mutex_lock(&client->sent_lock);
        bool queue_full = (client->send_count >= CLIENT_QUEUE);
        pthread_mutex_unlock(&client->sent_lock);

        if (!queue_full) {
            int ret = create_mesage_random(&msg, packet_num, client);
            if (ret == 0) {
                LOG_INFO("send message to queue");
            }
        }
        
        sleep(1); 
    }
    return NULL;
}