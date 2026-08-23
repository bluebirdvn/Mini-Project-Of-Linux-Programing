#define _GNU_SOURCE   
#include "server.h"
#include "client.h"
#include "worker_thread.h"  
#include "common.h"
#include "logger.h"
#include "rate_limit.h"
#include "protocol_message.h"
#include "crc.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>

static int setup_tcp(void)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        int err = errno;
        if (err == EACCES) {
            LOG_ERROR("Permission to create a socket of the specified type and/or protocol is denied");
        }
        return -1;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server)); 
    server.sin_port = htons(TCP_PORT);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY; 

    int reuse = 1;
    int ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (ret < 0) {
        LOG_ERROR("set option failed");
        close(listen_fd);
        return -1;
    }

    ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    if (ret < 0) {
        LOG_ERROR("set option failed");
        close(listen_fd);
        return -1;
    }

    int opt = 1;
    ret = setsockopt(listen_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
    if (ret < 0) {
        LOG_ERROR("set option failed");
        close(listen_fd);
        return -1;
    }

    ret = bind(listen_fd, (struct sockaddr*)&server, sizeof(server));
    if (ret < 0) {
        LOG_ERROR("bind error.");
        close(listen_fd);
        return -1;
    }

    ret = listen(listen_fd, 128);
    if (ret < 0) {
        LOG_ERROR("listen error.");
        close(listen_fd);
        return -1;
    }

    LOG_DEBUG("create listen_fd successfully");
    return listen_fd;
}

static int setup_udp(void)
{
    int discovery_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (discovery_fd < 0) {
        int err = errno;
        if (err == EACCES) {
            LOG_ERROR("Permission to create a socket of the specified type and/or protocol is denied");
        }
        return -1;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_port = htons(UDP_PORT);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    int reuse = 1;
    int ret = setsockopt(discovery_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (ret < 0) {
        LOG_ERROR("set option failed");
        close(discovery_fd);
        return -1;
    }

    ret = setsockopt(discovery_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    if (ret < 0) {
        LOG_ERROR("set option failed");
        close(discovery_fd);
        return -1;
    }
    ret = bind(discovery_fd, (struct sockaddr*)&server, sizeof(server));
    if (ret < 0) {
        LOG_ERROR("bind error.");
        close(discovery_fd);
        return -1;
    }
    LOG_DEBUG("create discovery_fd successfully");
    return discovery_fd;
}

static int setup_eventfd(void)
{
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd < 0) {
        int err = errno;
        if (err == EMFILE) {
            LOG_ERROR("The per-process limit on the number of open file descriptors has been reached.");
        }
        return -1;
    }
    LOG_DEBUG("create eventfd successfully");
    return efd;
}

static int setup_epoll(void)
{
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        int err = errno;
        if (err == EINVAL) {
            LOG_ERROR("invalid flags");
        } else if (err == EMFILE) {
            LOG_ERROR("not enough fd in system");
        }
        return -1;
    }
    return epoll_fd;
}

int server_init(struct server* sv)
{
    if (sv == NULL) {
        LOG_ERROR("null param");
        return -1;
    }

    memset(sv, 0, sizeof(*sv)); 

    int ret = clients_init(sv->clients, MAX_CLIENTS);
    if (ret < 0) {
        return -1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        rate_limit_init(RATE_LIMIT_CAP, RATE_LIMIT_REFILL_SEC, &sv->clients[i].rate);
    }

    task_queue_init(&sv->tasks, TASK_QUEUE_MAX); 
    response_queue_init(&sv->response, TASK_QUEUE_MAX);
    atomic_store(&sv->shutdown, false);

    pthread_mutex_init(&sv->lock, NULL);   
    pthread_cond_init(&sv->stop, NULL);

    sv->listen_fd = setup_tcp();
    if (sv->listen_fd < 0) {
        return -1;
    }

    sv->broadcast_fd = setup_udp();
    if (sv->broadcast_fd < 0) {
        return -1;
    }

    sv->event_fd = setup_eventfd();
    if (sv->event_fd < 0) {
        return -1;
    }

    sv->epoll_fd = setup_epoll();
    if (sv->epoll_fd < 0) {
        return -1;
    }

    if (epoll_add(sv->epoll_fd, EPOLLIN, sv->listen_fd) < 0) {
        return -1;
    }
    if (epoll_add(sv->epoll_fd, EPOLLIN, sv->broadcast_fd) < 0) {
        return -1;
    }
    if (epoll_add(sv->epoll_fd, EPOLLIN, sv->event_fd) < 0) {
        return -1;
    }

    sigemptyset(&sv->new_set);
    sigaddset(&sv->new_set, SIGINT);
    sigaddset(&sv->new_set, SIGTERM);
    sigaddset(&sv->new_set, SIGHUP);

    pthread_sigmask(SIG_BLOCK, &sv->new_set, &sv->origin_set);

    return 0;
}

int handle_accept_client(struct server* sv)
{
    if (sv == NULL) {
        LOG_ERROR("null param");
        return -1;
    }

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr); 
    int client_fd = accept4(sv->listen_fd, (struct sockaddr*)&client_addr, &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return 0; 
        }
        if (err == EINTR) {
            LOG_WARN("accept was interrupted");
            return 0;
        }
        LOG_ERROR("accept4 failed: %d", err);
        return -1;
    }

    int slot = client_alloc(sv->clients, MAX_CLIENTS, client_fd, &client_addr);
    if (slot < 0) {
        LOG_ERROR("not find empty slot in clients");
        close(client_fd); 
        return -1;
    }

    int ret = epoll_add(sv->epoll_fd, EPOLLIN, client_fd);
    if (ret < 0) {
        LOG_ERROR("epoll add failed.");
        close_client(sv->clients, slot); 
        return -1;
    }

    LOG_INFO("add new client, slot=%d", slot);
    return 0;
}

int handle_request_client(struct server *sv, int client_fd)
{
    if (sv == NULL || client_fd < 0) {
        LOG_ERROR("invalid params");
        return -1;
    }
    int slot = fd_to_slot(sv->clients, client_fd, MAX_CLIENTS);
    if (slot < 0) {
        return -1;
    }
    struct client* c = &sv->clients[slot];
    if (atomic_load(&c->alive) == false) {
        return -1;
    }

    bool allow = request_allow(&c->rate);
    if (allow == false) {
        LOG_WARN("reach limit request per second, slot=%d", slot);
        char discard[512];
        while (recv(client_fd, discard, sizeof(discard), MSG_DONTWAIT) > 0) { 

         }
        return 1;
    }

    int ret = client_read_message(sv, c); 
    if (ret == -1) {
        LOG_ERROR("read failed, closing slot=%d", slot);
        close_client(sv->clients, slot);
        return -1;
    } else if (ret == 1) {
        LOG_INFO("client close, slot=%d", slot);
        close_client(sv->clients, slot); 
        return -1;
    } else if (ret == 2) {
        LOG_WARN("packet error, response err queued, slot=%d", slot);
        return 0;
    } else if (ret == 3) {
        return 0;
    }

    return 0;
}

int handle_response_to_client(struct server *sv)
{
    if (sv == NULL) {
        LOG_ERROR("invalid params");
        return -1;
    }

    uint64_t val;
    ssize_t rn = read(sv->event_fd, &val, sizeof(val));
    (void)rn; 

    struct response* rp = response_queue_pop(&sv->response);
    while (rp != NULL) {
        int slot = rp->slot;
        if (slot < 0 || slot >= MAX_CLIENTS) {
            free(rp); 
            rp = response_queue_pop(&sv->response);
            continue;
        }

        struct client* c = &sv->clients[slot];
        bool is_valid = atomic_load(&c->alive) && check_response_message(rp, c);

        if (is_valid == false) {
            free(rp);
            rp = response_queue_pop(&sv->response);
            continue;
        }

        uint8_t *data = packet_response_message(rp);
        if (data == NULL) {
            LOG_ERROR("packet message failed, slot=%d", slot);
            free(rp);
            rp = response_queue_pop(&sv->response);
            continue;
        }

        int ret = enqueue_message(c, data, rp->length + CRC_SIZE + HEADER_SIZE);
        if (ret != 0) {
            LOG_ERROR("enqueue failed, slot=%d", slot);
            free(data);
            free(rp);
            rp = response_queue_pop(&sv->response);
            continue;
        }

        ret = epoll_mod(sv->epoll_fd, EPOLLIN | EPOLLOUT, c->fd);
        if (ret < 0) {
            LOG_ERROR("epoll mod failed, slot=%d", slot);
        }

        free(rp); 

        rp = response_queue_pop(&sv->response);
    }

    return 0;
}

int handle_client_sendable(struct server* sv, int client_fd)
{
    if (sv == NULL || client_fd < 0) {
        LOG_ERROR("invalid params");
        return -1;
    }
    int slot = fd_to_slot(sv->clients, client_fd, MAX_CLIENTS);
    if (slot < 0) {
        return -1;
    }
    struct client* c = &sv->clients[slot];
    if (atomic_load(&c->alive) == false) {
        return -1;
    }

    int ret = flush_message_from_queue(c);
    if (ret < 0) {
        LOG_ERROR("flush client message failed, slot=%d", slot);
        close_client(sv->clients, slot); 
        return -1;
    } else if (ret == 2) {
        LOG_WARN("client close, slot=%d", slot);
        close_client(sv->clients, slot); 
        return -1;
    } else if (ret == 1) {
        return 0;
    }

    ret = epoll_mod(sv->epoll_fd, EPOLLIN, client_fd);
    if (ret < 0) {
        LOG_ERROR("epoll mod failed, slot=%d", slot);
        return -1;
    }
    return 0;
}

int handle_discovery(struct server* sv)
{
    if (sv == NULL) {
        LOG_ERROR("null params");
        return -1;
    }

    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr); 
    char buffer[16]; 

    int ret = recvfrom(sv->broadcast_fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT, (struct sockaddr*)&client_addr, &len);
    if (ret < 0) {
        int err = errno;
        if (err == EWOULDBLOCK || err == EAGAIN) {
            return 0; 
        }
        if (err == EINTR) {
            return 0;
        }
        LOG_ERROR("recvfrom failed: %d", err);
        return -1;
    }

    buffer[ret] = '\0'; 

    bool is_discovery = (strcmp(buffer, "DISCOVERY") == 0);
    if (!is_discovery) {
        LOG_WARN("not a discovery packet, ignore");
        return 0;
    }

    char send_buf[128];
    int slen = snprintf(send_buf, sizeof(send_buf), "{\"gateway\":\"iot-gw\",\"tcp_port\":%d,\"udp_port\":%d}", TCP_PORT, UDP_PORT);

    ret = sendto(sv->broadcast_fd, send_buf, (size_t)slen, MSG_DONTWAIT, (struct sockaddr*)&client_addr, len); 

    if (ret != slen) {
        LOG_ERROR("send broadcast packet failed");
        return -1;
    }
    LOG_DEBUG("send broadcast packet successfully");

    return 0;
}

int reap_client(struct server* sv)
{
    if (sv == NULL) {
        LOG_ERROR("null param");
        return -1;
    }

    uint64_t now = now_ms();

    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct client* c = &sv->clients[i];

        if (c->fd >= 0 && atomic_load(&c->alive) == false) {
            close_client(sv->clients, i);
            continue; 
        }

        if (c->fd < 0) {
            continue; 
        }

        uint64_t interval_sec = (now - c->last_active_ms) / 1000;
        if (interval_sec > IDLE_TIMEOUT_SEC) {
            LOG_INFO("slot=%d idle timeout, closing", i);
            close_client(sv->clients, i);
        }
    }

    return 0;
}

int server_shutdown(struct server* sv)
{
    if (sv == NULL) {
        LOG_ERROR("null param");
        return -1;
    }

    LOG_INFO("shutdown duoc yeu cau, bao hieu cho cac thread dung lai...");

    atomic_store(&sv->shutdown, true);

    task_queue_shutdown(&sv->tasks);

    pthread_mutex_lock(&sv->lock);
    pthread_cond_signal(&sv->stop);
    pthread_mutex_unlock(&sv->lock);

    return 0;
}

int server_cleanup(struct server* sv)
{
    if (sv == NULL) {
        LOG_ERROR("null param");
        return -1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        close_client(sv->clients, i);
        pthread_mutex_destroy(&sv->clients[i].lock);
    }

    task_queue_destroy(&sv->tasks);
    response_queue_destroy(&sv->response); 

    close(sv->listen_fd);
    close(sv->broadcast_fd);
    close(sv->epoll_fd);
    close(sv->event_fd);

    pthread_mutex_destroy(&sv->lock);
    pthread_cond_destroy(&sv->stop);

    LOG_INFO("cleanup rsc");
    return 0;
}

int server_run(struct server* sv)
{
    if (sv == NULL) {
        LOG_ERROR("null param");
        return -1;
    }
    pthread_t workers[NUM_WORKERS];
    struct work_task work_tasks[NUM_WORKERS]; 

    for (int i = 0; i < NUM_WORKERS; i++) {
        work_tasks[i].id = (uint64_t)i;
        work_tasks[i].task_queue = &sv->tasks;
        work_tasks[i].response_queue = &sv->response;
        work_tasks[i].event_fd = sv->event_fd;
        if (pthread_create(&workers[i], NULL, worker_thread, &work_tasks[i]) != 0) {
            LOG_ERROR("pthread_create worker %d failed", i);
            return -1;
        }
    }

    pthread_t signal_tid, main_tid;
    if (pthread_create(&signal_tid, NULL, signal_thread, sv) != 0) {
        LOG_ERROR("pthread_create signal_thread failed");
        return -1;
    }
    if (pthread_create(&main_tid, NULL, main_thread, sv) != 0) {
        LOG_ERROR("pthread_create main_thread failed");
        return -1;
    }

    pthread_mutex_lock(&sv->lock);
    while (atomic_load(&sv->shutdown) == false) {
        pthread_cond_wait(&sv->stop, &sv->lock);
    }
    pthread_mutex_unlock(&sv->lock);

    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(workers[i], NULL);
    }
    pthread_join(main_tid, NULL);
    pthread_join(signal_tid, NULL);

    server_cleanup(sv);

    return 0;
}

static int check_header(struct header_packet* header) 
{
    if (header == NULL) {
        LOG_ERROR("null param");
        return -1;
    }

    if (header->magic != PROTOCOL_MAGIC) {
        LOG_ERROR("magic not  match");
        return ERR_BAD_MAGIC;
    }
    if (header->version != PROTOCOL_VERSION) {
        LOG_ERROR("version not match");
        return ERR_BAD_VERSION;
    }

    if (header->length > MAX_PAYLOAD) {
        LOG_ERROR("payload exeed max payload size");
        return ERR_PAYLOAD_TOO_LARGE;
    }

    if (header->type != MSG_ECHO &&
        header->type != MSG_STATUS &&
        header->type != MSG_GET_SENSOR &&
        header->type != MSG_SET_ACTUATOR) {
        LOG_ERROR("unknown message type");
        return ERR_UNKNOWN_TYPE;
    }

    return 0;
}


int client_read_message(struct server* sv, struct client* client)
{
    if (client == NULL || sv == NULL) {
        LOG_ERROR("null param");
        return -1;
    }

    pthread_mutex_lock(&client->lock);

    for(;;) {
        switch(client->stage) {
            case READ_HEADER: {
                size_t remain = HEADER_SIZE - client->rx_len;
                ssize_t n = recv(client->fd, client->data_receive + client->rx_len, remain, MSG_DONTWAIT);

                if (n < 0) {
                    int err = errno;
                    if (err == EINTR) {
                        LOG_WARN("read was interrupted");
                        continue;
                    }
                    if (err == EAGAIN || err == EWOULDBLOCK) {
                        pthread_mutex_unlock(&client->lock);
                        return 3;
                    }

                    pthread_mutex_unlock(&client->lock);
                    return -1;
                }

                if (n == 0) {
                    LOG_WARN("client close");
                    atomic_store(&client->alive, false);
                    pthread_mutex_unlock(&client->lock);
                    return 1;
                }

                client->rx_len += (size_t)n;

                if (client->rx_len < HEADER_SIZE) {
                    continue;
                }  

                {
                    struct header_packet header;
                    header.magic = decode_32_bit(client->data_receive);
                    header.version = decode_16_bit(client->data_receive + 4);
                    header.type = decode_16_bit(client->data_receive + 6);
                    header.length = decode_32_bit(client->data_receive + 8);
                    header.request_id = decode_32_bit(client->data_receive + 12);

                    int ret = check_header(&header);

                    if (ret < 0) {
                        pthread_mutex_unlock(&client->lock);
                        return -1;
                    }

                    if (ret > 0) {
                        size_t size;
                        uint8_t *err_packet = build_error_packet(header.request_id, ret, &size);
                        if (err_packet == NULL) {
                            pthread_mutex_unlock(&client->lock);
                            return -1;
                        }

                        client->stage = READ_HEADER;
                        client->rx_len = 0;

                        pthread_mutex_unlock(&client->lock);
                        enqueue_message(client, err_packet, size);
                        epoll_mod(sv->epoll_fd, EPOLLIN | EPOLLOUT, client->fd);
                        return 2;
                    }
                    client->stage = READ_PAYLOAD;
                }

                continue;
            }

            case READ_PAYLOAD: {
                uint32_t length = decode_32_bit(client->data_receive + 8);
                size_t total_needed = (size_t)length + HEADER_SIZE + CRC_SIZE;
                size_t remain = length + HEADER_SIZE + CRC_SIZE - client->rx_len;
                size_t n = recv(client->fd, client->data_receive + client->rx_len, remain, MSG_DONTWAIT);

                if (n < 0) {
                    int err = errno;
                    if (err == EINTR) {
                        continue;
                    }
                    if (err == EAGAIN || err == EWOULDBLOCK) {
                        pthread_mutex_unlock(&client->lock);
                        return 3;
                    }

                    pthread_mutex_unlock(&client->lock);
                    return -1;

                }

                if (n == 0) {
                    LOG_WARN("client close");
                    atomic_store(&client->alive, false);
                    pthread_mutex_unlock(&client->lock);
                    return 1;
                }
                client->rx_len += n;

                if (client->rx_len < total_needed) {
                    continue;
                } 

                {
                    uint32_t crc_rev = decode_32_bit(client->data_receive + HEADER_SIZE + length);
                    uint32_t crc_cal = crc_calculate(client->data_receive + HEADER_SIZE, length);

                    if (crc_rev != crc_cal) {
                        LOG_ERROR("crc not match");
                        size_t size;
                        uint32_t request_id = decode_32_bit(client->data_receive + 12);
                        uint8_t *err_packet = build_error_packet(request_id, ERR_CRC_MISMATCH, &size);

                        client->stage = READ_HEADER;
                        client->rx_len = 0;
                        pthread_mutex_unlock(&client->lock);

                        if (err_packet!= NULL) {
                            enqueue_message(client, err_packet, size);
                            epoll_mod(sv->epoll_fd, EPOLLIN | EPOLLOUT, client->fd);
                        }

                        return 2;
                    }

                    struct header_packet header;
                    header.type = decode_16_bit(client->data_receive + 6);
                    header.length = decode_32_bit(client->data_receive + 8);
                    header.request_id = decode_32_bit(client->data_receive + 12);

                    int slot = fd_to_slot(sv->clients, client->fd, MAX_CLIENTS);

                    struct task *t = build_task(client, &header, slot);
                    if (t != NULL) {
                        if (push(&sv->tasks, t) < 0) {
                            LOG_ERROR("task queue is full, dropping task");
                            free(t); 
                        }
                    }
                    client->stage = READ_HEADER;
                    client->rx_len = 0;
                    client->last_active_ms = now_ms();
                }

                pthread_mutex_unlock(&client->lock);
                return 0;

            }

            default: {
                pthread_mutex_unlock(&client->lock);
                return -1;
            }
        }
    }

    client->last_active_ms =now_ms();
    pthread_mutex_unlock(&client->lock);
    return 0;
}
