#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>

#define PATH_SOCKET      "/tmp/myapp.sock"
#define MAX_PAYLOAD_SIZE 2048
#define SESSION_TOKEN_LEN 64


enum command {
    GET_STATUS = 0,
    SET_STATUS,
    GET_DATA,
    GET_CONFIG,
    SET_CONFIG,
    SEND_CONTROL,
    SHUTDOWN_PEER,
    ACK,
    NACK,
    IDENTIFY   
}; 

enum priority {
    LOW = 0,
    MEDIUM,
    HIGH,
};

struct header_message {
    enum command cmd;
    enum priority pri;
    uint64_t number_packet;
    int length;             
};

struct message_control {
    struct header_message header;
    char message[MAX_PAYLOAD_SIZE];
    uint32_t checksum;
};

static inline uint32_t calculate_checksum(struct message_control *m)
{
    uint32_t sum = 0;
    const uint8_t *b = (const uint8_t *)&m->header;

    for (size_t i = 0; i < sizeof(m->header); i++) {
        sum += b[i];
    }
    for (int i = 0; i < m->header.length && i < MAX_PAYLOAD_SIZE; i++) {
        sum += (uint8_t)m->message[i];
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}

static inline int build_message(const char *payload, enum command cmd, enum priority pri,
                                 struct message_control *message, uint64_t num_packet)
{
    if (message == NULL || payload == NULL) {
        return -1;
    }

    size_t len = strlen(payload);
    if (len >= MAX_PAYLOAD_SIZE) {
        len = MAX_PAYLOAD_SIZE - 1;
    }

    memset(message, 0, sizeof(*message));
    message->header.cmd = cmd;
    message->header.pri = pri;
    message->header.number_packet = num_packet;

    memcpy(message->message, payload, len);
    message->message[len] = '\0';
    message->header.length = (int)len;

    message->checksum = calculate_checksum(message);
    return 0;
}

static inline int build_ack(uint64_t number_ack, struct message_control *message)
{
    return build_message("ACK", ACK, LOW, message, number_ack);
}

static inline int build_nack(uint64_t number_nack, struct message_control *message)
{
    return build_message("NACK", NACK, LOW, message, number_nack);
}

static inline int send_framed_message(int fd, const struct message_control *m, int flags)
{
    int payload_len = m->header.length;
    if (payload_len < 0 || payload_len > MAX_PAYLOAD_SIZE) {
        payload_len = 0;
    }

    size_t wire_size = sizeof(m->header) + (size_t)payload_len + sizeof(m->checksum);
    char buf[sizeof(struct header_message) + MAX_PAYLOAD_SIZE + sizeof(uint32_t)];

    size_t off = 0;
    memcpy(buf + off, &m->header, sizeof(m->header)); off += sizeof(m->header);
    memcpy(buf + off, m->message, (size_t)payload_len); off += (size_t)payload_len;
    memcpy(buf + off, &m->checksum, sizeof(m->checksum)); off += sizeof(m->checksum);

    size_t sent_total = 0;
    while (sent_total < wire_size) {
        ssize_t ret = send(fd, buf + sent_total, wire_size - sent_total, flags);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            } 
            return -1; 
        } else if (ret == 0) {
            return 1;
        }
        sent_total += (size_t)ret;
    }
    return 0;
}

static inline int recv_framed_message(int fd, struct message_control *m)
{
    memset(m, 0, sizeof(*m));

    ssize_t n = recv(fd, &m->header, sizeof(m->header), MSG_WAITALL);
    if (n <= 0) {
        return (n == 0) ? 1 : -1;
    }

    if (n != (ssize_t)sizeof(m->header)) {
        return -1;
    }

    if (m->header.length < 0 || m->header.length > MAX_PAYLOAD_SIZE) {
        return -1;
    }

    if (m->header.length > 0) {
        n = recv(fd, m->message, (size_t)m->header.length, MSG_WAITALL);
        if (n <= 0) {
            return (n == 0) ? 1 : -1;
        }

        if (n != m->header.length) {
            return -1;
        }
    }

    n = recv(fd, &m->checksum, sizeof(m->checksum), MSG_WAITALL);
    if (n <= 0) {
        return (n == 0) ? 1 : -1;
    }
    if (n != (ssize_t)sizeof(m->checksum)) {
        return -1;
    }

    return 0;
}

static inline int connect_to_server(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        close(fd);
        return -1;
    }
    return fd;
}

#endif 