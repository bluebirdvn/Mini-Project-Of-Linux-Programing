#ifndef _CLIENT_H
#define _CLIENT_H

#include "protocol_message.h"
#include "rate_limit.h"
#include "queue.h"
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct data_transmit {
    uint8_t *data;
    size_t len;
    size_t offset;
    struct data_transmit* next_data;
};

struct client {
    int fd;
    uint64_t generation;
    atomic_bool alive;

    struct rate_limit rate;

    enum read_stage stage;
    uint8_t data_receive[RX_BUF_CAP];
    size_t rx_len;

    struct data_transmit *tail;
    struct data_transmit *head;
    size_t bytes_need_to_send;

    pthread_mutex_t lock;
    struct sockaddr_in addr;

    uint64_t last_active_ms;
};


/**
 * @brief init all client in server with fd = -1
 * 
 * @param client pointer to array of client
 * @param max_clients number of client need to be initialized
 * @return int 0 if success, -1 if failed
 */
int clients_init(struct client* client, int max_clients);

/**
 * @brief init client when server accept new client fd with sockadd_in
 * 
 * @param client pointer to client array
 * @param max_clients max client in client array
 * @param fd new client fd accepted by server
 * @param addr pointer to sockaddr contained info about client : addr, port
 * @return int : number of slot in struct client if success, -1 if can't find a empty slot in clients
 */
int client_alloc(struct client* client, int max_clients, int fd, struct sockaddr_in* addr);


/**
 * @brief close client and all resource of it
 * 
 * @param clients pointer to client array
 * @param slot index of client in array
 * @return int 0 if success, -1 if failed
 */
int close_client(struct client* clients, int slot);

/**
 * @brief get the index of client fd int struct clients
 * 
 * @param clients pointer to array of struct clients
 * @param client_fd client_fd in slot 
 * @param max_client number of client in array
 * @return int slot if success, -1 if not find
 */
int fd_to_slot(struct client* clients, int client_fd, int max_client);

/**
 * @brief enqueue a message to tx buffer of cthe client
 * 
 * @param client pointer to client
 * @param data data need to enqueue
 * @param len sizeof data
 * @return int 0 if success, -1 if failed, -EAGAIN if queue full
 */
int enqueue_message(struct client* client, uint8_t *data, size_t len);


/**
 * @brief get data from queue 
 * when calling this function, ensure lock of client must be locked before call this func
 * @param client pointer to client
 * @return struct data_transmit* pointer to struct need to be sent if success, NULL if failed
 */
struct data_transmit* dequeue_message(struct client* client);

/**
 * @brief try to send all message in client buffer
 *
 * @param client pointer to client 
 * @return int 0 if success flush all data in buffer, 1 if buffer of client is empty or buffer of socket full, -1 if failed, 2 if client closed
 */
int flush_message_from_queue(struct client* client);


/**
 * @brief check if response of client is valid or not
 * check if current response is match with current client in clients
 * 
 * @param response poniter to response of client
 * @param client pointer to struct client
 * @return uint8_t 
 */
bool check_response_message(struct response* response, struct client* client);


/**
 * @brief packet response message to network data
 * 
 * @param response pointer to response message
 * @return uint8_t* pointer to data if success, NULL if failed
 */
uint8_t* packet_response_message(struct response* response);

/**
 * @brief build task from packet received from socket
 * 
 * @param client pointer struct client - owner the task
 * @param header header of packet received
 * @param slot index in clients array
 * @return struct task* pointer to struct built
 */
struct task* build_task(struct client* client, struct header_packet *header, int slot);


#endif