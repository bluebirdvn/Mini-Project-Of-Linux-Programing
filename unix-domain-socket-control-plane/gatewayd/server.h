#ifndef _SERVER_H
#define _SERVER_H

#include "common.h"
#include "logger.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>

#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>


#define USING_EPOLL  1
#define USING_POLL   0
#define USING_SELECT 0

#if (USING_SELECT + USING_EPOLL + USING_POLL) != 1
#error "exactly one IO must be enabled"
#endif

#if USING_EPOLL
#include <sys/epoll.h>
#define MAX_EVENTS 1024
#endif

#if USING_POLL
#include <poll.h>
#endif

#if USING_SELECT
#include <sys/select.h>
#endif



#define CAPACITY 1024

#define NUM_HIGH_WORKERS 2
#define NUM_MID_WORKERS  1
#define NUM_LOW_WORKERS  1
#define SELECT_TIMEOUT_SEC 1

#define MAX_SESSIONS 256

#define MAX_FRAMES_PER_CLIENT_PER_LOOP 32

#define NUM_PACKET_TO_SEND 16


struct client_session {
    bool in_use;
    char token[SESSION_TOKEN_LEN];

    uint64_t client_id;
    uint64_t packets_in_total;
    uint64_t packets_out_total;
    uint64_t first_seen_ms;
    uint64_t last_seen_ms;

    int reconnect_count;
};


struct session_table {
    struct client_session sessions[MAX_SESSIONS];
    uint64_t next_persistent_id;
    pthread_mutex_t lock;
};

enum read_stage {
    READ_HEADER = 0,
    READ_PAYLOAD,
    READ_CHECKSUM
};

struct outgoing {
    struct message_control msg;
    char wire_buf[sizeof(struct header_message) + MAX_PAYLOAD_SIZE + sizeof(uint32_t)];
    size_t bytes_sent;
};

struct client {
    int fd;

    bool connected;

    uint64_t connection_id;
    uint64_t generation;

    struct client_session* session;
    enum read_stage stage;
    
    struct message_control message;

    size_t bytes_read;

    bool pending_close;
    int pending_task;

    uint64_t last_active;
    uint64_t connected_at;

    pthread_mutex_t client_lock;

    uint64_t packet_sent;
    uint64_t packet_received;

    struct outgoing send_queue[NUM_PACKET_TO_SEND];
    int send_head, send_tail, send_count;
};

struct task {
    int fd;
    uint64_t client_generation;
    uint64_t connection_id;
    struct message_control message;
};

struct message_queue {
    struct task tasks[CAPACITY];
    int head, tail;
    int count;
};

struct server {
    int listen_fd;
#if USING_SELECT
    fd_set all_fds;
    fd_set read_fds;
#endif

#if USING_POLL
    struct pollfd fds[FD_SETSIZE];
#endif

#if USING_EPOLL
    int epollfd, nfds;
    struct epoll_event events[MAX_EVENTS];
#endif
    int max_fd;
    struct client clients[FD_SETSIZE];

    struct session_table table;

    uint64_t next_client_id;

    int num_clients;

    pthread_mutex_t queue_lock;
    pthread_cond_t queue_wakeup;

    pthread_mutex_t worker_lock;       
    pthread_cond_t worker_wakeup;      

    struct message_queue high_priority_queue;
    struct message_queue medium_priority_queue;
    struct message_queue low_priority_queue;

    pthread_mutex_t server_lock;    
    volatile bool shutdown;
    pthread_cond_t stop;

    sigset_t system_set;
    sigset_t origin_set;
};



uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);   
    return ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void init_session_table(struct session_table *t)
{
    memset(t, 0, sizeof(*t));
    pthread_mutex_init(&t->lock, NULL);
}


static struct client_session *session_lookup_or_create(struct session_table* table, const char* token, bool *out_was_resumed)
{
    pthread_mutex_lock(&table->lock);
    struct client_session *free_slot = NULL;

    for (int i = 0; i < MAX_SESSIONS; ++i) {
        struct client_session* t = &table->sessions[i];
        if (t->in_use == false) {
            if (free_slot == NULL) {
                free_slot = t;
            }
            continue;
        }
        if (strncmp(token, t->token, SESSION_TOKEN_LEN) == 0) {
            t->last_seen_ms = now_ms();
            t->reconnect_count++;
            *out_was_resumed = true;
            pthread_mutex_unlock(&table->lock);
            return t;
        }
    }

    if (free_slot == NULL) {
        pthread_mutex_unlock(&table->lock);
        return NULL;
    }

    memset(free_slot, 0, sizeof(struct client_session));
    free_slot->in_use = true;
    free_slot->first_seen_ms = now_ms();
    free_slot->last_seen_ms = free_slot->first_seen_ms;
    free_slot->reconnect_count = 1;
    free_slot->client_id = ++table->next_persistent_id;

    size_t token_len = strnlen(token, SESSION_TOKEN_LEN - 1);
    memcpy(free_slot->token, token, token_len);
    free_slot->token[token_len] = '\0';

    *out_was_resumed = false;
    pthread_mutex_unlock(&table->lock);
    return free_slot;
}



int queue_client(struct client* client, struct message_control* send_message)
{
    if (client == NULL || send_message == NULL) {
        return -1;
    }

    pthread_mutex_lock(&client->client_lock);

    if (client->send_count == 16) {
        LOG_WARN("queue full: drop oldest message");
        client->send_tail = (client->send_tail + 1) % 16;
        client->send_count--;
    }

    client->send_queue[client->send_head].msg = *send_message;
    client->send_queue[client->send_head].bytes_sent = 0;
    client->send_head = (client->send_head + 1) % 16;
    client->send_count++;
    pthread_mutex_unlock(&client->client_lock);
    return 0;
}

static void advance_send_queue(struct client *client)
{
    client->send_tail = (client->send_tail + 1) % NUM_PACKET_TO_SEND;
    client->send_count--;
}


int dequeue_client(struct client* client, struct message_control* send_message)
{
    if (client == NULL || send_message == NULL) {
        return -1;
    }

    pthread_mutex_lock(&client->client_lock);
    if (client->send_count == 0) {
        pthread_mutex_unlock(&client->client_lock);
        return -1;
    }

    *send_message = client->send_queue[client->send_tail].msg;
    advance_send_queue(client);
    pthread_mutex_unlock(&client->client_lock);
    return 0;
}



static inline int send_message_chunk(struct client *client, int client_fd, int flags)
{
    if (client->send_count == 0)
        return 1;

    struct outgoing *out = &client->send_queue[client->send_tail];
    struct message_control *msg = &out->msg;
    size_t payload_len = (size_t)(msg->header.length >= 0 ? msg->header.length : 0);
    size_t total = sizeof(msg->header) + payload_len + sizeof(msg->checksum);

    if (out->bytes_sent == 0) {
        size_t off = 0;
        memcpy(out->wire_buf + off, &msg->header, sizeof(msg->header));
        off += sizeof(msg->header);
        memcpy(out->wire_buf + off, msg->message, payload_len);
        off += payload_len;
        memcpy(out->wire_buf + off, &msg->checksum, sizeof(msg->checksum));
    }

    size_t offset = out->bytes_sent;
    while (offset < total) {
        const char *buf = out->wire_buf + offset;
        size_t remaining = total - offset;
        ssize_t sent = send(client_fd, buf, remaining, flags);
        if (sent == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                out->bytes_sent = offset;
                return 0;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        offset += (size_t)sent;
    }

    advance_send_queue(client);
    return 1;
}

bool try_enqueue_task(struct server *sv, struct message_queue *queue, const struct task *task)
{
    pthread_mutex_lock(&sv->queue_lock);

    if (sv->shutdown || queue->count >= CAPACITY) {
        pthread_mutex_unlock(&sv->queue_lock);
        return false;
    }

    queue->tasks[queue->head] = *task;
    queue->head = (queue->head + 1) % CAPACITY;
    queue->count++;

    pthread_cond_signal(&sv->queue_wakeup);
    pthread_mutex_unlock(&sv->queue_lock);
    return true;
}

bool try_dequeue_task_locked(struct message_queue *queue, struct task *task)
{
    if (queue->count == 0) return false;
    *task = queue->tasks[queue->tail];
    queue->tail = (queue->tail + 1) % CAPACITY;
    queue->count--;
    return true;
}


void init_message_queue(struct message_queue* queue)
{
    memset(queue, 0, sizeof(*queue));
}


void init_server(struct server* server)
{
    if (server == NULL) {
        LOG_ERROR("null param.");
        return;
    }

    pthread_mutex_init(&server->server_lock, NULL);
    pthread_mutex_init(&server->worker_lock, NULL);   
    pthread_cond_init(&server->worker_wakeup, NULL);
    pthread_cond_init(&server->stop, NULL);

    sigemptyset(&server->system_set);
    sigaddset(&server->system_set, SIGUSR1);
    sigaddset(&server->system_set, SIGHUP);
    sigaddset(&server->system_set, SIGTERM);
    sigaddset(&server->system_set, SIGINT);

    pthread_sigmask(SIG_BLOCK, &server->system_set, &server->origin_set);

    init_session_table(&server->table);
    pthread_mutex_init(&server->queue_lock, NULL);
    pthread_cond_init(&server->queue_wakeup, NULL);

    server->next_client_id = 0;
    server->num_clients = 0;
    server->shutdown = false;
    server->max_fd = -1;
#if USING_SELECT
    FD_ZERO(&server->all_fds);
#endif
#if USING_EPOLL
    server->epollfd = epoll_create1(0);
    if (server->epollfd == -1) {
        LOG_ERROR("create epollfd failed");
    }
    server->nfds = 0;
#endif
    init_message_queue(&server->high_priority_queue);
    init_message_queue(&server->medium_priority_queue);
    init_message_queue(&server->low_priority_queue);
}

void init_client(struct client* client, uint64_t connection_id, uint64_t generation)
{
    if (client == NULL) {
        return;
    }

    memset(client, 0, sizeof(struct client));
    pthread_mutex_init(&client->client_lock, NULL);

    client->connection_id = connection_id;
    client->generation = generation;
    client->connected_at = now_ms();
    client->last_active = now_ms();
    client->stage = READ_HEADER;

    client->bytes_read = 0;
    client->packet_received = 0;
    client->packet_sent = 0;
    client->send_count = 0;
    client->send_head = 0;
    client->send_tail = 0;
}

int handle_accept(struct server *sv)
{
    while (1) {
        int new_fd = accept4(sv->listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (new_fd == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
                return 0;
            }
            LOG_ERROR("accept error");
            return -1;
        }
        if (new_fd >= FD_SETSIZE) {
            LOG_ERROR("fd %d exceeds limit, reject", new_fd);
            close(new_fd);
            continue;
        }

        struct client *slot = &sv->clients[new_fd];
        uint64_t next_generation = slot->generation + 1;
        uint64_t connection_id = ++sv->next_client_id;

        init_client(slot, connection_id, next_generation);
        slot->fd = new_fd;
        slot->connected = true;

        sv->num_clients++;

#if USING_SELECT
        FD_SET(new_fd, &sv->all_fds);
#endif
#if USING_POLL
        sv->fds[new_fd].fd = new_fd;
        sv->fds[new_fd].events = POLLIN;
#endif
#if USING_EPOLL
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;  
        ev.data.fd = new_fd;
        if (epoll_ctl(sv->epollfd, EPOLL_CTL_ADD, new_fd, &ev) == -1) {
            LOG_ERROR("epoll_ctl add failed");
            close(new_fd);
            continue;
        }
#endif
        if (new_fd > sv->max_fd) {
            sv->max_fd = new_fd;
        }

        LOG_INFO("connection id: %" PRIu64 " on fd %d", connection_id, new_fd);

    }
    return 0;
}


int calculate_length_packet(struct message_control* message)
{
    if (message == NULL) return -1;
    if (message->header.length < 0 || message->header.length > MAX_PAYLOAD_SIZE)
        return -1;
    return (int)(sizeof(message->header) + (size_t)message->header.length + sizeof(message->checksum));
}

int read_stage(struct client* client)
{
    int frames_processed = 0;

    while (frames_processed < MAX_FRAMES_PER_CLIENT_PER_LOOP) {
        pthread_mutex_lock(&client->client_lock);
        if (client->pending_close) {
            pthread_mutex_unlock(&client->client_lock);
            return -1;
        }
        enum read_stage stage = client->stage;
        size_t bytes_read = client->bytes_read;
        struct message_control *msg = &client->message;
        int fd = client->fd;
        pthread_mutex_unlock(&client->client_lock);

        switch (stage) {
            case READ_HEADER: {
                char *dst = (char*)&msg->header + bytes_read;
                size_t remaining = sizeof(msg->header) - bytes_read;
                ssize_t n = recv(fd, dst, remaining, 0);
                if (n == -1) {
                    if (errno == EINTR) {
                        continue;
                    }

                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        return 0;
                    }
                    pthread_mutex_lock(&client->client_lock);
                    client->pending_close = true;
                    pthread_mutex_unlock(&client->client_lock);
                    return -1;
                }
                if (n == 0) {
                    pthread_mutex_lock(&client->client_lock);
                    client->pending_close = true;
                    pthread_mutex_unlock(&client->client_lock);
                    return -1;
                }
                pthread_mutex_lock(&client->client_lock);
                client->bytes_read += (size_t)n;
                if (client->bytes_read < sizeof(msg->header)) {
                    pthread_mutex_unlock(&client->client_lock);
                    return 0;
                }
                if (msg->header.length < 0 || msg->header.length > MAX_PAYLOAD_SIZE) {
                    LOG_ERROR("invalid length in header");
                    client->pending_close = true;
                    pthread_mutex_unlock(&client->client_lock);
                    return -1;
                }
                client->bytes_read = 0;
                client->stage = (msg->header.length == 0) ? READ_CHECKSUM : READ_PAYLOAD;
                pthread_mutex_unlock(&client->client_lock);
                break;
            }
            case READ_PAYLOAD: {
                char *dst = msg->message + bytes_read;
                size_t remaining = (size_t)msg->header.length - bytes_read;
                ssize_t n = recv(fd, dst, remaining, 0);
                if (n == -1) {
                    if (errno == EINTR) continue;
                    if (errno == EWOULDBLOCK || errno == EAGAIN) return 0;
                    pthread_mutex_lock(&client->client_lock);
                    client->pending_close = true;
                    pthread_mutex_unlock(&client->client_lock);
                    return -1;
                }
                if (n == 0) {
                    pthread_mutex_lock(&client->client_lock);
                    client->pending_close = true;
                    pthread_mutex_unlock(&client->client_lock);
                    return -1;
                }
                pthread_mutex_lock(&client->client_lock);
                client->bytes_read += (size_t)n;
                if (client->bytes_read < (size_t)msg->header.length) {
                    pthread_mutex_unlock(&client->client_lock);
                    return 0;
                }
                client->bytes_read = 0;
                client->stage = READ_CHECKSUM;
                pthread_mutex_unlock(&client->client_lock);
                break;
            }
            case READ_CHECKSUM: {
                char *dst = (char*)&msg->checksum + bytes_read;
                size_t remaining = sizeof(msg->checksum) - bytes_read;
                ssize_t n = recv(fd, dst, remaining, 0);
                if (n == -1) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        return 0;
                    }

                    pthread_mutex_lock(&client->client_lock);
                    client->pending_close = true;
                    pthread_mutex_unlock(&client->client_lock);
                    return -1;
                }
                if (n == 0) {
                    pthread_mutex_lock(&client->client_lock);
                    client->pending_close = true;
                    pthread_mutex_unlock(&client->client_lock);
                    return -1;
                }
                pthread_mutex_lock(&client->client_lock);
                client->bytes_read += (size_t)n;
                if (client->bytes_read < sizeof(msg->checksum)) {
                    pthread_mutex_unlock(&client->client_lock);
                    return 0;
                }
                uint32_t calc = calculate_checksum(msg);
                bool ok = (calc == msg->checksum);
                client->bytes_read = 0;
                client->stage = READ_HEADER;
                if (ok) {
                    client->pending_task++;
                    client->packet_received++;
                }
                pthread_mutex_unlock(&client->client_lock);
                frames_processed++;
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


static void archive_client(struct client *c)
{
    LOG_INFO("archive client fd %d", c->fd);
    (void)c;
}


int handle_client_readable(struct server *sv)
{
    pthread_mutex_lock(&sv->server_lock);

#if USING_EPOLL
    for (int i = 0; i < sv->nfds; i++) {
        int fd = sv->events[i].data.fd; 
        struct client* c = &sv->clients[fd];
        
        if (!c->connected || c->fd != fd) {
            continue;
        }        
#else
    for (int fd = 0; fd < FD_SETSIZE; fd++) {
        struct client *c = &sv->clients[fd];
        if (!c->connected || c->fd != fd) {
            continue;
        }
#endif

#if USING_SELECT
        if (!FD_ISSET(fd, &sv->read_fds)) {
            continue;
        }
#endif

#if USING_POLL
        if (!(sv->fds[fd].revents & POLLIN)) {
            continue;
        }
#endif

#if USING_EPOLL
        if (!(sv->events[i].events & EPOLLIN)) {
            continue;
        }
#endif 
        int ret = read_stage(c);
        if (ret == 0) continue;
        if (ret == -1) continue;

        if (ret == 2) {   
            struct message_control nack;
            build_nack(c->message.header.number_packet, &nack);
            queue_client(c, &nack);
            continue;
        }

        if (ret == 1) {   
            struct task t;
            t.fd = fd;
            t.client_generation = c->generation;
            t.connection_id = c->connection_id;
            t.message = c->message;

            struct message_queue *q = NULL;
            switch (t.message.header.pri) {
                case HIGH:   {
                    q = &sv->high_priority_queue; 
                    break;
                }
                case MEDIUM: {
                    q = &sv->medium_priority_queue; 
                    break;
                }
                case LOW:    {
                    q = &sv->low_priority_queue; 
                    break;
                }
                default:     {
                    LOG_WARN("not a specific case."); 
                    break;
                }
            }

            if (!try_enqueue_task(sv, q, &t)) {
                pthread_mutex_lock(&c->client_lock);
                if (c->pending_task > 0) c->pending_task--;
                pthread_mutex_unlock(&c->client_lock);                
                struct message_control reply;
                build_message("SERVER BUSY", NACK, LOW, &reply, t.message.header.number_packet);
                queue_client(c, &reply);
            }
        }
    }

    pthread_mutex_unlock(&sv->server_lock);
    return 0;
}

int handle_client_writable(struct server *sv, struct client* client)
{
    (void)sv;
    if (client == NULL) return -1;

    pthread_mutex_lock(&client->client_lock);
    if (client->send_count == 0) {
        pthread_mutex_unlock(&client->client_lock);
        return 0;
    }

    int res = send_message_chunk(client, client->fd, MSG_DONTWAIT);
    if (res == -1) {
        client->pending_close = true;
        advance_send_queue(client);
    } else if (res == 1) {
        struct client_session *sess = client->session;
        if (sess) {
            pthread_mutex_lock(&sv->table.lock);
            sess->packets_out_total++;
            pthread_mutex_unlock(&sv->table.lock);
        }
        client->packet_sent++;
    }
    pthread_mutex_unlock(&client->client_lock);
    return 0;
}



void reap_closed_clients(struct server *sv)
{
    pthread_mutex_lock(&sv->server_lock);

    for (int fd = 0; fd < FD_SETSIZE; fd++) {
        struct client* c = &sv->clients[fd];
        if (!c->pending_close) {
            continue;
        }

        pthread_mutex_lock(&c->client_lock);
        bool can_reap = (c->pending_task == 0 && c->send_count == 0);
        pthread_mutex_unlock(&c->client_lock);

        if (!can_reap) {
            continue;
        }

        archive_client(c);

        if (c->fd >= 0) {
            close(c->fd);
        }

#if USING_SELECT
        FD_CLR(c->fd, &sv->all_fds);
#endif
#if USING_POLL
        if (c->fd >= 0) {
            sv->fds[c->fd].fd = -1;
            sv->fds[c->fd].events = 0;
        }
#endif
#if USING_EPOLL
        if (c->fd >= 0) {
            epoll_ctl(sv->epollfd, EPOLL_CTL_DEL, c->fd, NULL);
        }
#endif
        pthread_mutex_lock(&c->client_lock);
        c->fd = -1;
        c->connected = false;
        c->pending_close = false;
        c->session = NULL;
        pthread_mutex_unlock(&c->client_lock);
        if (sv->num_clients > 0) {
            sv->num_clients--;
        }
    }

    pthread_mutex_unlock(&sv->server_lock);
}



int get_status_task(struct client* client, struct message_control* message)
{
    struct message_control reply;
    build_message("STATUS OK", ACK, LOW, &reply, message->header.number_packet);
    queue_client(client, &reply);
    return 0;
}

int set_status_task(struct client* client, struct message_control* message)
{
    struct message_control reply;
    build_ack(message->header.number_packet, &reply);
    queue_client(client, &reply);
    return 0;
}

int get_data_task(struct client* client, struct message_control* message)
{
    struct message_control reply;
    build_message("DATA", ACK, LOW, &reply, message->header.number_packet);
    queue_client(client, &reply);
    return 0;
}

int get_config_task(struct client* client, struct message_control* message)
{
    struct message_control reply;
    build_message("CONFIG", ACK, LOW, &reply, message->header.number_packet);
    queue_client(client, &reply);
    return 0;
}

int set_config_task(struct client* client, struct message_control* message)
{
    struct message_control reply;
    build_ack(message->header.number_packet, &reply);
    queue_client(client, &reply);
    return 0;
}

int send_control_task(struct client* client, struct message_control* message)
{
    struct message_control reply;
    build_ack(message->header.number_packet, &reply);
    queue_client(client, &reply);
    return 0;
}

int ack_task(struct client* client, struct message_control* message)
{
    (void)client; (void)message;
    return 0;
}

int nack_task(struct client* client, struct message_control* message)
{
    (void)client; (void)message;
    return 0;
}

int identify_task(struct server *sv, struct client* client, struct message_control* message)
{
    char token[SESSION_TOKEN_LEN];
    int len = message->header.length;
    if (len < 0) len = 0;
    if (len >= SESSION_TOKEN_LEN) len = SESSION_TOKEN_LEN - 1;

    memcpy(token, message->message, len);
    token[len] = '\0';

    if (len == 0) {
        struct message_control rep;
        build_message("IDENTITY requires a non-empty token", NACK, LOW, &rep,
                      message->header.number_packet);
        queue_client(client, &rep);
        return -1;
    }

    bool was_resumed = false;
    struct client_session* s = session_lookup_or_create(&sv->table, token, &was_resumed);

    if (s == NULL) {
        struct message_control rep;
        build_message("session table full", NACK, LOW, &rep, message->header.number_packet);
        queue_client(client, &rep);
        return -1;
    }

    pthread_mutex_lock(&client->client_lock);
    client->session = s;
    pthread_mutex_unlock(&client->client_lock);

    char rep_text[128];
    snprintf(rep_text, sizeof(rep_text), "%s client_id=%" PRIu64 " reconnect=%d", was_resumed ? "RESUMED" : "NEW", s->client_id, s->reconnect_count);
    struct message_control rep;
    build_message(rep_text, ACK, LOW, &rep, message->header.number_packet);
    queue_client(client, &rep);

    LOG_INFO("%s client_id=%" PRIu64 " reconnect=%d", was_resumed ? "RESUMED" : "NEW", s->client_id, s->reconnect_count);
    return 0;
}

static void finish_task(struct server *sv, struct task *t)
{
    if (t == NULL) return;
    int fd = t->fd;
    if (fd < 0 || fd >= FD_SETSIZE) return;

    struct client *c = &sv->clients[fd];
    pthread_mutex_lock(&c->client_lock);
    if (c->fd == fd && c->generation == t->client_generation) {
        if (c->pending_task > 0) {
            c->pending_task--;
        }
    }
    pthread_mutex_unlock(&c->client_lock);
}


void process_task(struct server* sv, struct task *t)
{
    if (sv == NULL || t == NULL) return;

    int fd = t->fd;
    if (fd < 0 || fd >= FD_SETSIZE) {
        finish_task(sv, t);
        return;
    }

    struct client *client = &sv->clients[fd];

    pthread_mutex_lock(&client->client_lock);
    bool valid = (client->fd == fd && client->generation == t->client_generation && client->connected);
    pthread_mutex_unlock(&client->client_lock);

    if (!valid) {
        finish_task(sv, t);
        return;
    }

    switch (t->message.header.cmd) {
        case GET_STATUS:{
               get_status_task(client, &t->message); 
               break;
        }

        case SET_STATUS:{
                set_status_task(client, &t->message); 
               break;
        }

        case GET_DATA:{
                get_data_task(client, &t->message); 
                break;
        }

        case GET_CONFIG:{
                get_config_task(client, &t->message); 
                break;
        }

        case SET_CONFIG:{
                set_config_task(client, &t->message); 
                break;
        }

        case SEND_CONTROL:{
                send_control_task(client, &t->message); 
                break;
        }

        case ACK:{
                ack_task(client, &t->message); 
                break;
        }

        case NACK:{
                nack_task(client, &t->message); 
                break;
        }

        case IDENTIFY:{
                identify_task(sv, client, &t->message); 
                break;
        }

        case SHUTDOWN_PEER: {
            LOG_INFO("client fd=%d requested graceful shutdown", client->fd);
            pthread_mutex_lock(&client->client_lock);
            client->pending_close = true;  
            pthread_mutex_unlock(&client->client_lock);
            break;
        }
        default: {
            LOG_ERROR("unknown command"); 
            break;
        }
    }
    LOG_DEBUG("processed cmd=%d packet=%" PRIu64 " from client_id=%" PRIu64, t->message.header.cmd, t->message.header.number_packet, t->connection_id);

    pthread_mutex_lock(&client->client_lock);
    struct client_session* session = client->session;
    pthread_mutex_unlock(&client->client_lock);
    if (session != NULL) {

        pthread_mutex_lock(&sv->table.lock);
        session->packets_in_total++;
        session->last_seen_ms = now_ms();
        pthread_mutex_unlock(&sv->table.lock);
    }

    finish_task(sv, t);
}


#endif