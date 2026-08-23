#ifndef _SERVER_H
#define _SERVER_H

#include "common.h"
#include "client.h"
#include "queue.h"
#include <signal.h>
#if ENABLE_TLS
#include "tls.h"
#endif

struct server {
    int listen_fd;
    int broadcast_fd;
    int event_fd;
    int epoll_fd;
    struct client clients[MAX_CLIENTS];

    struct task_queue tasks;
    struct response_queue response;

    sigset_t new_set, origin_set;

    pthread_cond_t stop;
    pthread_mutex_t lock;
    atomic_bool shutdown;
#if ENABLE_TLS
    SSL_CTX *tls_ctx;
#endif

};


/**
 * @brief setup tcp, udp connection, event_fd for get notify for response queue from worker done
 * init clients, queues, create worker threads
 * @param sv pointer to struct server
 * @return int 0 if success, -1 if failed
 */
int server_init(struct server* sv, const char* cert_path, const char* key_path);


/**
 * @brief when a client send connect cmd to server, server will get a signal from listen fd (by wait in epoll), server will step into this function to accept connection and setup 
 * 
 * @param sv pointer to struct server
 * @return int return 0 if success, -1 if failed
 */
int handle_accept_client(struct server* sv);

/**
 * @brief when a client send a request or message and a fd the client will be readable, server will step in to this function to read message 
 * 
 * @param sv pointer to struct server
 * @return int 0 if success, -1 if failed, 1 if get limit request
 */
int handle_request_client(struct server* sv, int client_fd);


/**
 * @brief when a task response was enqueued in respoonse queue, worker will
 * set eventfd to 1 for notifying server enter to this function  
 * @param sv poniter to struct server
 * @return int 0 if success, -1 if failed
 */
int handle_response_to_client(struct server *sv);

/**
 * @brief when response packet was added into queue of client to sent and EPOLLOUT in the fd of client was set, main thread will call this one
 * 
 * @param sv pointer to struct server
 * @param client_fd fd of client 
 * @return int 0 if sucess, -1 if failed
 */
int handle_client_sendable(struct server* sv, int client_fd);


/**
 * @brief reap clients that idle timeout or closed
 * 
 * @param sv pointer to struct server
 * @return int 0 if success, -1 if failed
 */
int reap_client(struct server* sv);

/**
 * @brief when a client want to connect to this server, it will board cast 
 * a message "DISCOVERY" to boardcast ip, server will get this board cast and know 
 * it's addr and port, and server response back to the client with IP and port of server
 * @param sv pointer to struct server
 * @return int 0 if success, -1 if failed
 */
int handle_discovery(struct server* sv);

/**
 * @brief the main loop of system when all initialized successfull. it will handle all accept, message, event and reap unconnected client
 * 
 * @param sv pointer to struct server 
 * @return int 0 if success, -1 if failed
 */
int server_run(struct server* sv);

/**
 * @brief when get a signal (SIGTERM/INT or any signal define to fore shutdown in struct ssrever to true)
 * sever will enter this function to reap resouces safely
 * @param sv pointer to struct server
 * @return int 0 if success, -1 if failed
 */
int server_shutdown(struct server* sv);


/**
 * @brief read message when client_fd has data
 * 
 * @param client pointer to struct client
 * @return int 0 if success, -1 if failed, 1 if client close, 2 if packet error, 3 if EAGAIN or EWOUDBLOCK
 */
int client_read_message(struct server* sv, struct client* client);


#endif