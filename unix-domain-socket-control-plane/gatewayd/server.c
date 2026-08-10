#include "common.h"
#include "logger.h"
#include "server.h"

#include <pthread.h>
#include <signal.h>


static bool all_queues_empty_locked(const struct server *sv)
{
    return sv->high_priority_queue.count == 0 &&
           sv->medium_priority_queue.count == 0 &&
           sv->low_priority_queue.count == 0;
}

void *sig_thread(void* arg);
void *IO_thread(void *arg);
void *high_pri_worker_thread(void *arg);
void *mid_pri_worker_thread(void *arg);
void *low_pri_worker_thread(void *arg);


int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    log_init("../log/serverlog");
    struct server *sv = (struct server*)calloc(1, sizeof(struct server));
    init_server(sv);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd == -1) {
        perror("socket");
        return -1;
    }
    sv->listen_fd = listen_fd;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, PATH_SOCKET, sizeof(addr.sun_path) - 1);

    unlink(PATH_SOCKET);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        if (errno == EADDRINUSE) {
            LOG_ERROR("bind: address already in use");
        } else if (errno == ENAMETOOLONG) {
            LOG_ERROR("bind: pathname too long");
        } else {
            LOG_ERROR("bind");
        }
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 5) == -1) {
        LOG_ERROR("listen");
        close(listen_fd);
        return -1;
    }

#if USING_SELECT
    FD_SET(listen_fd, &sv->all_fds);
    sv->max_fd = listen_fd;
#endif
#if USING_POLL
    for (int i = 0; i < FD_SETSIZE; i++) {
        sv->fds[i].fd = -1;
        sv->fds[i].events = 0;
        sv->fds[i].revents = 0;
    }
    sv->fds[listen_fd].fd = listen_fd;
    sv->fds[listen_fd].events = POLLIN;
    sv->max_fd = listen_fd;
#endif

#if USING_EPOLL
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(sv->epollfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        LOG_ERROR("epoll_ctl failed");
        return -1;
    }
#endif

    pthread_t sig_tid;
    pthread_t io_tid;
    pthread_t high_tids[NUM_HIGH_WORKERS];
    pthread_t mid_tids[NUM_MID_WORKERS];
    pthread_t low_tids[NUM_LOW_WORKERS];

    pthread_create(&sig_tid, NULL, sig_thread, sv);
    pthread_create(&io_tid, NULL, IO_thread, sv);

    for (int i = 0; i < NUM_HIGH_WORKERS; i++) {
        pthread_create(&high_tids[i], NULL, high_pri_worker_thread, sv);
    }
    for (int i = 0; i < NUM_MID_WORKERS; i++) {
        pthread_create(&mid_tids[i], NULL, mid_pri_worker_thread, sv);
    }
    for (int i = 0; i < NUM_LOW_WORKERS; i++) {
        pthread_create(&low_tids[i], NULL, low_pri_worker_thread, sv);
    }

    LOG_INFO("Server listening on %s (Ctrl+C to stop)", PATH_SOCKET);

    pthread_mutex_lock(&sv->server_lock);
    while (!sv->shutdown) {
        pthread_cond_wait(&sv->stop, &sv->server_lock);
    }

    pthread_mutex_unlock(&sv->server_lock);

    pthread_join(sig_tid, NULL);
    pthread_join(io_tid, NULL);
    for (int i = 0; i < NUM_HIGH_WORKERS; i++) pthread_join(high_tids[i], NULL);
    for (int i = 0; i < NUM_MID_WORKERS; i++) pthread_join(mid_tids[i], NULL);
    for (int i = 0; i < NUM_LOW_WORKERS; i++) pthread_join(low_tids[i], NULL);


    close(listen_fd);
    unlink(PATH_SOCKET);

    log_close();
    return 0;
}


void *sig_thread(void *arg)
{
    if (arg == NULL) {
        perror("arg of signal thread is NULL.\n");
        return NULL;
    }

    struct server *sv = (struct server*)(arg);
    sigset_t *set = &sv->system_set;
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
                pthread_mutex_lock(&sv->server_lock);
                for (int fd = 0; fd < FD_SETSIZE; fd++) {
                    struct client *c = &sv->clients[fd];
                    if (c->connected && c->fd == fd) {
                        struct message_control msg;
                        build_message("server shutting down", SHUTDOWN_PEER, HIGH, &msg, 0);
                        send_framed_message(c->fd, &msg, MSG_NOSIGNAL); 
                    }
                }
                sv->shutdown = true;
                pthread_cond_signal(&sv->stop);
                pthread_mutex_unlock(&sv->server_lock);

                pthread_mutex_lock(&sv->queue_lock);
                pthread_cond_broadcast(&sv->queue_wakeup);
                pthread_mutex_unlock(&sv->queue_lock);
                
                return NULL;
            }
            case SIGUSR1: {
                printf("sig user 1.\n");
                break;
            }
            default: {
                printf("Sig : %d", sig_num);
                break;
            }
        }
        fflush(stdout);
    }
    return NULL;
}


void *IO_thread(void *arg)
{
    if (arg == NULL) {
        LOG_ERROR("null param");
        return NULL;
    }

    struct server* sv = (struct server*)arg;
    int ret;

#if USING_EPOLL
#endif


    while(1) {
        pthread_mutex_lock(&sv->server_lock);
        bool shutdown = sv->shutdown;
        pthread_mutex_unlock(&sv->server_lock);
        if (shutdown) {
            break;
        }

#if USING_POLL
        for (int i = 0; i <= sv->max_fd; i++) {
            if (i == sv->listen_fd) {
                continue;
            }
            if (sv->clients[i].connected) {
                sv->fds[i].fd = sv->clients[i].fd;
                sv->fds[i].events = POLLIN | POLLHUP;
                if (sv->clients[i].send_count > 0)
                    sv->fds[i].events |= POLLOUT;
            } else {
                sv->fds[i].fd = -1;
            }
        }
        ret = poll(sv->fds, (nfds_t)sv->max_fd + 1, 5);
        if (ret == -1) {
            if (errno == EINTR) continue;
            LOG_ERROR("poll failed");
            break;
        }
        if (ret == 0) {
            reap_closed_clients(sv);
            continue;
        }

        if (sv->fds[sv->listen_fd].revents & POLLIN) {
            handle_accept(sv);
        }

        for (int i = 0; i <= sv->max_fd; i++) {
            if (sv->clients[i].fd != i || !sv->clients[i].connected) {
                continue;
            }
            if (sv->fds[i].revents & (POLLHUP | POLLERR)) {
                pthread_mutex_lock(&sv->clients[i].client_lock);
                sv->clients[i].pending_close = true;
                pthread_mutex_unlock(&sv->clients[i].client_lock);
            }
            if (sv->fds[i].revents & POLLOUT) {
                handle_client_writable(sv, &sv->clients[i]);
            }
        }
        handle_client_readable(sv);
#endif

#if USING_SELECT
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        read_fds = sv->all_fds;
        int maxfd = sv->max_fd;

        for (int i = 0; i < FD_SETSIZE; i++) {
            if (sv->clients[i].connected && sv->clients[i].fd == i) {
                FD_SET(i, &read_fds);
                if (sv->clients[i].send_count > 0)
                    FD_SET(i, &write_fds);
                if (i > maxfd) maxfd = i;
            }
        }
        struct timeval tv;
        tv.tv_sec = SELECT_TIMEOUT_SEC;
        tv.tv_usec = 0;
        ret = select(maxfd + 1, &read_fds, &write_fds, NULL, &tv);
        sv->read_fds = read_fds;
        if (ret == -1) {
            if (errno == EINTR) continue;
            LOG_ERROR("select failed");
            break;
        }
        if (ret == 0) {
            reap_closed_clients(sv);
            continue;
        }
        if (FD_ISSET(sv->listen_fd, &read_fds)) {
            handle_accept(sv);
            maxfd = sv->max_fd;
        }
        for (int i = 0; i <= maxfd; i++) {
            if (sv->clients[i].connected && sv->clients[i].fd == i) {
                if (FD_ISSET(i, &write_fds)){
                    handle_client_writable(sv, &sv->clients[i]);
                }
            }
        }
        handle_client_readable(sv);
#endif

#if USING_EPOLL
        ret = epoll_wait(sv->epollfd, sv->events, MAX_EVENTS, 5);
        if (ret == -1) {
            if (errno == EINTR) continue;
            LOG_ERROR("epoll_wait failed");
            break;
        }
        if (ret == 0) {
            reap_closed_clients(sv);
            continue;
        }
        pthread_mutex_lock(&sv->server_lock);
        sv->nfds = ret;
        pthread_mutex_unlock(&sv->server_lock);
        for (int i = 0; i < ret; i++) {
            int fd = sv->events[i].data.fd;
            uint32_t events = sv->events[i].events;

            if (fd == sv->listen_fd) {
                if (events & EPOLLIN) {
                    handle_accept(sv);
                }
                continue;
            }
            if (sv->clients[fd].fd != fd || !sv->clients[fd].connected) {
                continue;
            }
            if (events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                pthread_mutex_lock(&sv->clients[fd].client_lock);
                sv->clients[fd].pending_close = true;
                pthread_mutex_unlock(&sv->clients[fd].client_lock);
            }
            if (events & EPOLLOUT) {
                handle_client_writable(sv, &sv->clients[fd]);
            }
        }
        handle_client_readable(sv);
#endif
        reap_closed_clients(sv);
    }

    return NULL;
}


void *high_pri_worker_thread(void *arg)
{

    struct server *sv = (struct server*)arg;
    struct task t;

    while (1) {
        pthread_mutex_lock(&sv->queue_lock);
        while (!sv->shutdown && all_queues_empty_locked(sv)) {
            pthread_cond_wait(&sv->queue_wakeup, &sv->queue_lock);
        }
        if (sv->shutdown) {
            pthread_mutex_unlock(&sv->queue_lock);
            break;
        }

        bool got = try_dequeue_task_locked(&sv->high_priority_queue, &t);
        
        if (!got) {
            got = try_dequeue_task_locked(&sv->medium_priority_queue, &t);
        }

        if (!got) {
            got = try_dequeue_task_locked(&sv->low_priority_queue, &t);
        }
        
        pthread_mutex_unlock(&sv->queue_lock);

        if (got) {
            process_task(sv, &t);
        }
    }
    return NULL;


}

void *mid_pri_worker_thread(void *arg)
{
    struct server *sv = (struct server*)arg;
    struct task t;

    while (1) {
        pthread_mutex_lock(&sv->queue_lock);
        while (!sv->shutdown && all_queues_empty_locked(sv)) {
            pthread_cond_wait(&sv->queue_wakeup, &sv->queue_lock);
        }
        if (sv->shutdown) {
            pthread_mutex_unlock(&sv->queue_lock);
            break;
        }

        bool got = try_dequeue_task_locked(&sv->medium_priority_queue, &t);

        if (!got) {
            got = try_dequeue_task_locked(&sv->low_priority_queue, &t);
        }

        pthread_mutex_unlock(&sv->queue_lock);

        if (got) {
            process_task(sv, &t);
        }
    }
    return NULL;
}

void *low_pri_worker_thread(void *arg)
{
    struct server *sv = (struct server*)arg;
    struct task t;

    while (1) {
        pthread_mutex_lock(&sv->queue_lock);
        while (!sv->shutdown && all_queues_empty_locked(sv)) {
            pthread_cond_wait(&sv->queue_wakeup, &sv->queue_lock);
        }
        if (sv->shutdown) {
            pthread_mutex_unlock(&sv->queue_lock);
            break;
        }

        bool got = try_dequeue_task_locked(&sv->low_priority_queue, &t);

        pthread_mutex_unlock(&sv->queue_lock);

        if (got) {
            process_task(sv, &t);
        }
    }
    return NULL;
}
