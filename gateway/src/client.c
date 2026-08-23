

#include <linux/limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#if ENABLE_TLS
#include <openssl/ssl.h>
#endif

#include "common.h"
#include "client.h"
#include "logger.h"
#include "rate_limit.h"
#include "protocol_message.h"

int clients_init(struct client* client, int max_clients)
{
    if (client == NULL || max_clients < 1) {
        LOG_ERROR("null params");
        return -1;
    }

    for (int i = 0; i < max_clients; i++) {
        client[i].fd = -1;
        client[i].generation = 0;
        client[i].tail = NULL;
        client[i].head = NULL;
        client[i].alive = false;
        client[i].bytes_need_to_send = 0;
        client[i].rx_len = 0;
        client[i].stage = READ_HEADER;
        pthread_mutex_init(&client[i].lock, NULL);
#if ENABLE_TLS
        client[i].ssl = NULL;
        client[i].tls_handshake_done = false;
        client[i].tls_read_pending = false;
#endif


    }

    return 0;
}

int client_alloc(struct client* client, int max_clients, int fd, struct sockaddr_in* addr)
{
    if (client == NULL || max_clients < 1 || fd < 0 || addr == NULL) {
        LOG_ERROR("invalid params");
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < max_clients; i++) {
        if (client[i].fd == -1) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        LOG_WARN("number client reach maximum");
        return -1;
    }

    LOG_DEBUG("find slot number %d in clients", slot);

    client[slot].fd = fd;
    client[slot].addr = *addr;
    client[slot].last_active_ms = now_ms();
    client[slot].generation++;
    client[slot].rx_len = 0;
    client[slot].stage = READ_HEADER;
    client[slot].tail = NULL;
    client[slot].head = NULL;
    client[slot].bytes_need_to_send = 0;
#if ENABLE_TLS
    client[slot].ssl = NULL;
    client[slot].tls_handshake_done = false;
    client[slot].tls_read_pending = false;
#endif    
    rate_limit_init(RATE_LIMIT_CAP, RATE_LIMIT_REFILL_SEC, &client[slot].rate);

    atomic_store(&client[slot].alive, true);
    LOG_DEBUG("add client with fd %d into slot %d", fd, slot);

    return slot;
}

int fd_to_slot(struct client* clients, int client_fd, int max_client)
{
    if (clients == NULL || client_fd < 0) {
        LOG_ERROR("invalid params");
        return -1;
    }

    for (int i = 0; i < max_client; i++) {
        if (clients[i].alive == true && clients[i].fd == client_fd) {
            return i;
        }
    }

    return -1;
}

int enqueue_message(struct client* client, uint8_t *data, size_t len)
{
    if (client == NULL || data == NULL) {
        LOG_ERROR("null params");
        return -1;
    }

    pthread_mutex_lock(&client->lock);
    struct data_transmit* data_send = malloc(sizeof(struct data_transmit));
    if (data_send == NULL) {
        pthread_mutex_unlock(&client->lock);
        LOG_ERROR("can't alloc data transmit");
        return -1;
    }
    data_send->data = data;
    data_send->len = len;
    data_send->offset = 0;
    data_send->next_data = NULL;
    if (client->tail == NULL) {
        client->head = data_send;

    } else {
        client->tail->next_data = data_send;
    }
    client->tail = data_send;

    client->bytes_need_to_send += len;

    pthread_mutex_unlock(&client->lock);

    return 0;
}

struct data_transmit* dequeue_message(struct client* client)
{
    if (client == NULL) {
        LOG_ERROR("null param");
        return NULL;
    }

    if (client->head == NULL) {
        return NULL;
    }

    struct data_transmit* data = client->head;
    client->head = data->next_data;
    if (client->head == NULL) {
        client->tail = NULL;
    }
    data->next_data = NULL;

    return data;
}


/**
 * @brief send data from queue to client (data_transmit)
 * 
 * @param client_fd file descriptor of client socket
 * @param data data need to send
 * @param len size of data
 * @param flags flags of send function
 * @return int return number bytes sent, -1 if failed, -2 if buffer send is full (retry)
 */
static int send_data_to_client(int client_fd, uint8_t* data, size_t len, int flags)
{
    if (client_fd < 0 || data == NULL) {
        LOG_ERROR("invalid params");
        return -1;
    }
 
    size_t sent_total = 0;
    while (sent_total < len) {
        ssize_t ret = send(client_fd, data + sent_total, len - sent_total, flags | MSG_NOSIGNAL);
        if (ret < 0) {
            int err = errno;
            if (err == EINTR) {
                continue;
            }
            if (err == EAGAIN || err == EWOULDBLOCK) {
                return (int)sent_total > 0 ? (int)sent_total : -2;
            }
            return -1; 
        }
        if (ret == 0) {
            LOG_WARN("client close");
            return 1;
        }
        sent_total += (size_t)ret;
    }
 
    return (int)sent_total; 
}



int flush_message_from_queue(struct client* client)
{
    if (client == NULL) {
        LOG_ERROR("null param");
        return -1;
    }


    pthread_mutex_lock(&client->lock);
    
    if (client->head == NULL) {
        pthread_mutex_unlock(&client->lock);
        LOG_WARN("buffer empty");
        return 1;
    }

    struct data_transmit* send_data = dequeue_message(client);
    
    
    while(send_data != NULL) {
        
        int ret = send_data_to_client(client->fd, send_data->data, send_data->len, 0);

        if (ret < 0) {
            free(send_data->data);
            free(send_data);
            pthread_mutex_unlock(&client->lock);
            return -1;
        } else if (ret == 1) {
            atomic_store(&client->alive, false);
            free(send_data->data);
            free(send_data);
            
            pthread_mutex_unlock(&client->lock);
            return 2;
        } else if (ret == -2) {
            send_data->next_data = client->head;
            client->head = send_data;

            if (client->tail == NULL) {
                client->tail = send_data;
            }

            pthread_mutex_unlock(&client->lock);

            return 1;
        }
        client->last_active_ms = now_ms();
        client->bytes_need_to_send -= send_data->len;
        free(send_data->data);
        free(send_data);
        send_data = dequeue_message(client);
    }

    client->last_active_ms = now_ms();
    pthread_mutex_unlock(&client->lock);
    
    return 0;
}



bool check_response_message(struct response* response, struct client* client)
{
    if (response == NULL || client == NULL) {
        LOG_ERROR("null params");
        return false;
    }   

    if (client->generation != response->generation) {
        return false;
    } 
    
    return true;
}


uint8_t* packet_response_message(struct response* response)
{
    if (response == NULL) {
        LOG_ERROR("null param");
        return NULL;
    }


    size_t total_len = HEADER_SIZE + response->length + CRC_SIZE;
    uint8_t *buffer = malloc(total_len);
    if (buffer == NULL) {
        LOG_ERROR("malloc failed");
        return NULL;
    }

    int ret = packet_encode(buffer, total_len, response->type, response->request_id, response->payload, response->length);
    if (ret < 0) {
        LOG_ERROR("failed to encode");
        free(buffer);
        return NULL;    
    }

    return buffer;
}

int close_client(struct client* clients, int slot)
{
    if (clients == NULL || slot < 0) {
        LOG_ERROR("invalistd params");
        return -1;
    }

    struct client* client = &clients[slot];
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
#if ENABLE_TLS
    if (client->ssl) {
        SSL_shutdown(client->ssl);
        SSL_free(client->ssl);
        client->ssl = NULL;
    }
#endif
    atomic_store(&client->alive, false);

    struct data_transmit* data = client->head;
    while(data != NULL) {
        struct data_transmit* next_data = data->next_data;
        free(data->data);
        free(data);
        data = next_data;
    }

    client->head = NULL;
    client->tail = NULL;
    client->bytes_need_to_send = 0;
    client->rx_len = 0;

    return 0;
}

struct task* build_task(struct client *client, struct header_packet *header, int slot)
{
    if (client == NULL || header == NULL || slot < 0) {
        LOG_ERROR("invalid params for building task");
        return NULL;
    }

    struct task *t = malloc(sizeof(struct task));
    if (t == NULL) {
        LOG_ERROR("can't alloc memory for task");
        return NULL;
    }

    t->slot = slot;
    t->generation = client->generation; 
    t->type = header->type;
    t->request_id = header->request_id;
    t->timeout_ms = now_ms() + REQUEST_TIMEOUT_MS; 
    t->length = header->length;
    t->next_task = NULL;

    if (header->length > 0 && header->length <= MAX_PAYLOAD) {
        memcpy(t->payload, client->data_receive + HEADER_SIZE, header->length);
    }

    return t;
}

#if ENABLE_TLS


static int send_data_to_client_tls(struct client* client, uint8_t* data, size_t len, int flags)
{
    if (client == NULL || data == NULL) {
        LOG_ERROR("invalid params");
        return -1;
    }
 
    size_t sent_total = 0;
    while (sent_total < len) {
        ssize_t ret = SSL_write(client->ssl, data + sent_total, len - sent_total);
        if (ret < 0) {
            int err = SSL_get_error(client->ssl, ret);

            if (sent_total > 0) {
                return (int)sent_total;
            }

            if (err == SSL_ERROR_WANT_WRITE) {
                return -2;
            }
            if (err == SSL_ERROR_WANT_READ) {
                return -3;
            }

            if (err == SSL_ERROR_ZERO_RETURN) {
                return 0;
            }
            return -1; 
        }
        if (ret == 0) {
            LOG_WARN("client close");
            return 1;
        }
        sent_total += (size_t)ret;
    }
 
    return (int)sent_total; 
}



int client_flush_tx_tls(struct client* client, int epoll_fd)
{
    if (client == NULL) {
        LOG_ERROR("null param");
        return -1;
    }


    pthread_mutex_lock(&client->lock);
    
    if (client->head == NULL) {
        pthread_mutex_unlock(&client->lock);
        LOG_WARN("buffer empty");
        return 0;
    }

    struct data_transmit* send_data = dequeue_message(client);
    
    
    while(send_data != NULL) {
        uint8_t *data = send_data->data + send_data->offset;
        size_t remain = send_data->len - send_data->offset;

        int ret = send_data_to_client_tls(client, data, remain, 0);
        if (ret == -1 || ret == 0) {
            free(send_data->data);
            free(send_data);
            pthread_mutex_unlock(&client->lock);
            return (ret == 0) ? 2 : -1;
        } else if (ret == -2 || ret == -3) {

            if (ret == -2) {
                epoll_mod(epoll_fd, EPOLLIN | EPOLLOUT, client->fd);
            } else {
                epoll_mod(epoll_fd, EPOLLIN, client->fd);
            }
            pthread_mutex_unlock(&client->lock);
            
            return 1;

        } else if (ret > 0) {
            send_data->offset += ret;
            client->bytes_need_to_send -= ret;
            client->last_active_ms = now_ms();   
            
            if (send_data->offset < send_data->len) {
                epoll_mod(epoll_fd, EPOLLIN | EPOLLOUT, client->fd);
                pthread_mutex_unlock(&client->lock);
                return 1;
            } else {
                client->head = send_data->next_data;
                if (client->head == NULL) {
                    client->tail = NULL;
                }
                free(send_data->data);
                free(send_data);
                
                send_data = client->head;
            }
        }
    }

    epoll_mod(epoll_fd, EPOLLIN, client->fd);
    client->last_active_ms = now_ms();
    pthread_mutex_unlock(&client->lock);
    
    return 0;
}

#endif
