    #ifndef _CLIENT_H
    #define _CLIENT_H

    #include "common.h"
    #include "logger.h"


    #include <bits/pthreadtypes.h>
    #include <bits/types/sigset_t.h>
    #include <cstdint>
    #include <stddef.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <signal.h>
    #include <pthread.h>
    #include <stdlib.h>
    #include <string.h>


    #define CLIENT_QUEUE 32

    struct client_state {
        pthread_mutex_t lock;
        pthread_cond_t stop;

        bool shutdown;

        int server_fd;

        uint64_t num_packet_sent;
        uint64_t num_packet_recv;

        pthread_mutex_t sent_lock;
        struct message_control sent_queue[CLIENT_QUEUE];
        int send_head, send_tail, send_count;

        pthread_mutex_t recv_lock;
        struct message_control recv_queue[CLIENT_QUEUE];
        int recv_head, recv_tail, recv_count;

        sigset_t system_set, origin_set;
        
        enum read_stage stage;
        struct message_control message;
        size_t bytes_read;
    };

    int enqueue_message(struct message_control *mes, struct message_control *queue, struct client_state* client)
    {   
        if (client == NULL || mes == NULL || queue == NULL) {
            LOG_ERROR("null params.");
            return -1;
        }
        
        if (queue == client->sent_queue) {
            if (client->send_count == CLIENT_QUEUE) {
                LOG_WARN("send queue full");
                return 1;
            } 

            queue[client->send_head] = *mes;
            client->send_head = (client->send_head + 1) % CLIENT_QUEUE;
            client->send_count++;
            
        }

        if (queue == client->recv_queue) {
            if (client->recv_count == CLIENT_QUEUE) {
                LOG_WARN("recv queue full");
                return 1;
            }
            queue[client->recv_head] = *mes;
            client->recv_head = (client->recv_head + 1) % CLIENT_QUEUE;
            client->recv_count++;        
        }

        return 0;

    }

    int dequeue_message(struct message_control *mes, struct message_control *queue, struct client_state* client)
    {
        if (client == NULL || mes == NULL || queue == NULL) {
            LOG_ERROR("null params.");
            return -1;
        }
        
        if (queue == client->sent_queue) {
            if (client->send_count == 0) {
                LOG_WARN("empty queue");
                return 1;
            } 

            *mes = queue[client->send_tail];
            client->send_tail = (client->send_tail + 1) % CLIENT_QUEUE;
            client->send_count--;
            
        }

        if (queue == client->recv_queue) {
            if (client->recv_count == 0) {
                LOG_WARN("empty queue");
                return 1;
            }

            *mes = queue[client->recv_tail];
            client->recv_tail = (client->recv_tail + 1) % CLIENT_QUEUE;
            client->recv_count--;       
        }

        return 0;
    }

    int create_mesage_random(struct message_control* mess, uint64_t num_packet, client_state* state)
    {
        int i = rand() % 5;
        char payload[MAX_PAYLOAD_SIZE];
        enum priority pri;
        switch (i) {
            case 0: {
                snprintf(payload, MAX_PAYLOAD_SIZE, "GET_STATUS: {sensor:%d, device:%d}", i + rand() % 10, i + rand() % 40);
                pri = LOW;
                break;
            }

            case 1: {
                snprintf(payload, MAX_PAYLOAD_SIZE, "SET_STATUS: {sensor:%d, deivce:%d, status:%d}", i + rand() % 10, i + rand() % 40, (i+rand()%2) % 2 == 1 ? 1 : 0);
                pri = HIGH;
                break;
            }

            case 2: {
                snprintf(payload, MAX_PAYLOAD_SIZE, "GET_DATA: {sensor:%d, deivce:%d}", i + rand() % 10, i + rand() % 40);
                pri = MEDIUM;
                break;
            }

            case 3: {
                snprintf(payload, MAX_PAYLOAD_SIZE, "GET_CONFIG: {sensor:%d, deivce:%d}", i + rand() % 10, i + rand() % 40);
                pri = LOW;
                break;
            }

            case 4: {
                snprintf(payload, MAX_PAYLOAD_SIZE, "SET_CONFIG: {sensor:%d, deivce:%d, config:{frequency:100000, set_lowpower:%d}}", i + rand() % 10, i + rand() % 40, (i+rand()%2) % 2 == 1 ? 1 : 0);
                pri = HIGH;
                break;
            }
            
            case 5: {
                snprintf(payload, MAX_PAYLOAD_SIZE, "SEND_CONTROL: {actuator:%d, deivce:%d, control: %d}", i + rand() % 10, i + rand() % 40, (i+rand()%2) % 2 == 1 ? 1 : 0);
                pri = HIGH;
                break;
            }

            default: {
                LOG_ERROR("not a situation");
                return -1;
            }

        }

        int ret = build_message(payload, (enum command)i, pri, mess, num_packet);
        if (ret == -1) {
            LOG_ERROR("build message failed.");
            return -1;
        }

        LOG_INFO("build message number %d success.", num_packet);

        pthread_mutex_lock(&state->sent_lock);
        enqueue_message(mess, state->sent_queue, state);
        pthread_mutex_unlock(&state->sent_lock);

        return 0;
    }


    int read_stage(struct client_state* client)
    {

        while (1) {
            pthread_mutex_lock(&client->lock);
            if (client->shutdown) {
                pthread_mutex_unlock(&client->lock);
                return -1;
            }
            enum read_stage stage = client->stage;
            size_t bytes_read = client->bytes_read;
            struct message_control *msg = &client->message;
            int fd = client->server_fd;
            pthread_mutex_unlock(&client->lock);

            switch (stage) {
                case READ_HEADER: {
                    char *dst = (char*)&msg->header + bytes_read;
                    size_t remaining = sizeof(msg->header) - bytes_read;
                    ssize_t n = recv(fd, dst, remaining, 0);
                    if (n == -1) {
                        if (errno == EINTR) continue;
                        if (errno == EWOULDBLOCK || errno == EAGAIN) return 0;
                        pthread_mutex_lock(&client->lock);
                        client->shutdown = true;
                        pthread_mutex_unlock(&client->lock);
                        return -1;
                    }
                    if (n == 0) {
                        pthread_mutex_lock(&client->lock);
                        client->shutdown = true;
                        pthread_mutex_unlock(&client->lock);
                        return -1;
                    }
                    pthread_mutex_lock(&client->lock);
                    client->bytes_read += (size_t)n;
                    if (client->bytes_read < sizeof(msg->header)) {
                        pthread_mutex_unlock(&client->lock);
                        return 0;
                    }
                    if (msg->header.length < 0 || msg->header.length > MAX_PAYLOAD_SIZE) {
                        LOG_ERROR("invalid length in header");
                        pthread_mutex_unlock(&client->lock);
                        continue;;
                    }
                    client->bytes_read = 0;
                    client->stage = (msg->header.length == 0) ? READ_CHECKSUM : READ_PAYLOAD;
                    pthread_mutex_unlock(&client->lock);
                    break;
                }
                case READ_PAYLOAD: {
                    char *dst = msg->message + bytes_read;
                    size_t remaining = (size_t)msg->header.length - bytes_read;
                    ssize_t n = recv(fd, dst, remaining, 0);
                    if (n == -1) {
                        if (errno == EINTR) continue;
                        if (errno == EWOULDBLOCK || errno == EAGAIN) return 0;
                        pthread_mutex_lock(&client->lock);
                        client->shutdown = true;
                        pthread_mutex_unlock(&client->lock);
                        return -1;
                    }
                    if (n == 0) {
                        pthread_mutex_lock(&client->lock);
                        client->shutdown = true;
                        pthread_mutex_unlock(&client->lock);
                        return -1;
                    }
                    pthread_mutex_lock(&client->lock);
                    client->bytes_read += (size_t)n;
                    if (client->bytes_read < (size_t)msg->header.length) {
                        pthread_mutex_unlock(&client->lock);
                        return 0;
                    }
                    client->bytes_read = 0;
                    client->stage = READ_CHECKSUM;
                    pthread_mutex_unlock(&client->lock);
                    break;
                }
                case READ_CHECKSUM: {
                    char *dst = (char*)&msg->checksum + bytes_read;
                    size_t remaining = sizeof(msg->checksum) - bytes_read;
                    ssize_t n = recv(fd, dst, remaining, 0);
                    if (n == -1) {
                        if (errno == EINTR) continue;
                        if (errno == EWOULDBLOCK || errno == EAGAIN) return 0;
                        pthread_mutex_lock(&client->lock);
                        client->shutdown = true;
                        pthread_mutex_unlock(&client->lock);
                        return -1;
                    }
                    if (n == 0) {
                        pthread_mutex_lock(&client->lock);
                        client->shutdown = true;
                        pthread_mutex_unlock(&client->lock);
                        return -1;
                    }
                    pthread_mutex_lock(&client->lock);
                    client->bytes_read += (size_t)n;
                    if (client->bytes_read < sizeof(msg->checksum)) {
                        pthread_mutex_unlock(&client->lock);
                        return 0;
                    }
                    uint32_t calc = calculate_checksum(msg);
                    bool ok = (calc == msg->checksum);
                    client->bytes_read = 0;
                    client->stage = READ_HEADER;
                    if (ok) {
                        client->num_packet_recv++;
                    }
                    pthread_mutex_unlock(&client->lock);
                    if (!ok) {
                        return 2;   
                    }
                    return 1;
                }
                default:
                    LOG_ERROR("invalid read stage");
                    return -1;
            }
        }
        return 0; 
    }





    #endif 