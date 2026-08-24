#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "../include/cJSON.h"
#include "../include/protocol_message.h"
#include "../include/common.h"
#include "../include/crc.h"

#define POLL_INTERVAL_SEC 5
#define RECONNECT_DELAY_SEC 3
#define RECV_TIMEOUT_SEC 3
#define DISCOVERY_MAX_RETRIES 5

#define SENSOR_ID_AHT30 1
#define SENSOR_ID_BH1750 2

#define DEVICE_ID_AC 1
#define DEVICE_ID_LED 2
#define DEVICE_ID_FAN 3

#define TEMP_ON_THRESHOLD_C 28.0f
#define TEMP_OFF_THRESHOLD_C 26.0f
#define AC_SETPOINT_C 24

#define LUX_ON_THRESHOLD 100.0f
#define LUX_OFF_THRESHOLD 150.0f
#define LED_BRIGHTNESS_PERCENT 70

#define HUMI_ON_THRESHOLD 70.0f
#define HUMI_OFF_THRESHOLD 60.0f
#define FAN_SPEED_RPM 1500

#ifndef MSG_DISCOVERY
#define MSG_DISCOVERY 0x0001
#endif
#ifndef MSG_DISCOVERY_RESP
#define MSG_DISCOVERY_RESP 0x0002
#endif

struct client_data {
    uint32_t next_request_id;
    int server_fd;
    int boardcast_fd;

    int ac_state;
    int led_state;
    int fan_state;

    float temperature;
    float humidity;
    float lux;

    char broadcast_addr[64];
    int discovery_port;

    char gateway_host[64];
    int gateway_port;

    SSL *ssl;
    SSL_CTX *tls_ctx;

    pthread_mutex_t lock;
    pthread_cond_t stop;
    volatile bool shutdown;

    sigset_t new_set;
};

static SSL_CTX* init_client_ctx(void)
{
    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        fprintf(stderr, "Unable to create SSL context\n");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }


    return ctx;
}

static SSL* connect_tls(SSL_CTX *ctx, const char *host, int port, int *fd_out)
{
    int fd;
    struct timeval tv;
    int nodelay;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return NULL;
    }

    tv.tv_sec = RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "invalid host: %s\n", host);
        close(fd);
        return NULL;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return NULL;
    }

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);

    if (SSL_connect(ssl) <= 0) {
        fprintf(stderr, "TLS Handshake failed\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(fd);
        return NULL;
    }

    *fd_out = fd;
    return ssl;
}

static int client_config(struct client_data *data, int argc, char **argv);
static int init_client_data(struct client_data *data);
static void client_cleanup(struct client_data *data);
static int setup_boardcast(struct client_data *data);
static int setup_tcp_tls(struct client_data *data);
static void *signal_thread(void *arg);
static bool client_should_stop(struct client_data *data);
static void sleep_interruptible_cd(struct client_data *data, int seconds);
static int do_discovery(struct client_data *data);

static int send_all(SSL *ssl, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        int n = SSL_write(ssl, buf + sent, len - sent);
        if (n <= 0) {
            int err = SSL_get_error(ssl, n);
            fprintf(stderr, "SSL_write failed, err=%d\n", err);
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int recv_one_frame(SSL *ssl, uint16_t *type_out, uint8_t *payload_out, size_t cap, uint32_t *len_out)
{
    uint8_t buf[HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE];
    size_t have = 0;

    for (;;) {
        int n = SSL_read(ssl, buf + have, sizeof(buf) - have);

        if (n <= 0) {
            int err = SSL_get_error(ssl, n);
            if (err == SSL_ERROR_SYSCALL) {
                if (errno == EINTR) {
                    continue;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    fprintf(stderr, "recv timeout after %d seconds, no reply from gateway\n", RECV_TIMEOUT_SEC);
                    return -1;
                }
            }
            if (err == SSL_ERROR_ZERO_RETURN) {
                fprintf(stderr, "gateway closed the TLS connection cleanly\n");
            } else {
                fprintf(stderr, "SSL_read failed, err=%d\n", err);
            }
            return -1;
        }

        have += (size_t)n;

        if (have < HEADER_SIZE) {
            continue;
        }

        uint32_t length = decode_32_bit(buf + 8);
        if (length > MAX_PAYLOAD) {
            fprintf(stderr, "length field exceeds MAX_PAYLOAD, data is corrupted\n");
            return -1;
        }

        size_t total = HEADER_SIZE + length + CRC_SIZE;
        if (have < total) {
            continue;
        }
        uint32_t crc_rev = decode_32_bit(buf + HEADER_SIZE + length);
        uint32_t crc_cal = crc_calculate(buf + HEADER_SIZE, length);

        if (crc_rev != crc_cal) {
            fprintf(stderr, "CRC mismatch, data from gateway is corrupted\n");
            return -1;
        }

        *type_out = decode_16_bit(buf + 6);

        if (length > cap) {
            fprintf(stderr, "response payload exceeds buffer capacity\n");
            return -1;
        }

        memcpy(payload_out, buf + HEADER_SIZE, length);
        *len_out = length;
        return 0;
    }
}

static int do_request(struct client_data *data, uint16_t type, cJSON *req_obj, cJSON **resp_obj_out)
{
    char *json_str = cJSON_PrintUnformatted(req_obj);
    if (json_str == NULL) {
        return -1;
    }

    uint32_t length = (uint32_t)strlen(json_str);
    uint8_t out[HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE];

    int rc = packet_encode(out, sizeof(out), type, data->next_request_id++, json_str, length);
    free(json_str);

    if (rc < 0) {
        fprintf(stderr, "packet_encode failed\n");
        return -1;
    }

    size_t total_len = HEADER_SIZE + length + CRC_SIZE;
    if (send_all(data->ssl, out, total_len) != 0) {
        return -1;
    }

    uint16_t resp_type;
    uint8_t resp_payload[MAX_PAYLOAD + 1];
    uint32_t resp_len;

    if (recv_one_frame(data->ssl, &resp_type, resp_payload, MAX_PAYLOAD, &resp_len) != 0) {
        return -1;
    }

    resp_payload[resp_len] = '\0';

    cJSON *resp = cJSON_Parse((const char *)resp_payload);
    if (resp == NULL) {
        fprintf(stderr, "response is not valid JSON\n");
        return -1;
    }

    if (resp_type != MSG_RESPONSE && resp_type != MSG_ERROR) {
        fprintf(stderr, "unexpected response type: 0x%04x\n", resp_type);
        cJSON_Delete(resp);
        return -1;
    }

    *resp_obj_out = resp;
    return 0;
}

static cJSON *build_get_sensor_req(int sensor_id) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", "get_sensor");
    cJSON_AddNumberToObject(root, "sensor_id", sensor_id);
    return root;
}

static cJSON *build_set_actuator_req(int device_id, int state, int value) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", "set_actuator");
    cJSON_AddNumberToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "state", state);
    cJSON_AddNumberToObject(root, "value", value);
    return root;
}

static int decide_state(float value, float on_threshold, float off_threshold, int current_state) {
    if (value >= on_threshold) {
        return 1;
    }
    if (value <= off_threshold) {
        return 0;
    }
    if (current_state == -1) {
        return 0;
    }
    return current_state;
}

static int decide_state_inverted(float value, float on_threshold_low, float off_threshold_high, int current_state) {
    if (value <= on_threshold_low) {
        return 1;
    }
    if (value >= off_threshold_high) {
        return 0;
    }
    if (current_state == -1) {
        return 0;
    }
    return current_state;
}

static bool fetch_temp_humi(struct client_data *data)
{
    cJSON *req = build_get_sensor_req(SENSOR_ID_AHT30);
    cJSON *resp = NULL;
    int rc = do_request(data, MSG_GET_SENSOR, req, &resp);
    cJSON_Delete(req);

    if (rc != 0) {
        return false;
    }

    bool have_data = false;
    cJSON *status = cJSON_GetObjectItemCaseSensitive(resp, "status");

    if (cJSON_IsString(status) && strcmp(status->valuestring, "ok") == 0) {
        cJSON *jdata = cJSON_GetObjectItemCaseSensitive(resp, "data");
        cJSON *t = cJSON_GetObjectItemCaseSensitive(jdata, "temp");
        cJSON *h = cJSON_GetObjectItemCaseSensitive(jdata, "humi");

        if (cJSON_IsNumber(t) && cJSON_IsNumber(h)) {
            data->temperature = (float)t->valuedouble;
            data->humidity = (float)h->valuedouble;
            have_data = true;
        }
    } else {
        fprintf(stderr, "gateway returned an error for AHT30\n");
    }

    cJSON_Delete(resp);
    return have_data;
}

static bool fetch_lux(struct client_data *data)
{
    cJSON *req = build_get_sensor_req(SENSOR_ID_BH1750);
    cJSON *resp = NULL;
    int rc = do_request(data, MSG_GET_SENSOR, req, &resp);
    cJSON_Delete(req);

    if (rc != 0) {
        return false;
    }

    bool have_data = false;
    cJSON *status = cJSON_GetObjectItemCaseSensitive(resp, "status");

    if (cJSON_IsString(status) && strcmp(status->valuestring, "ok") == 0) {
        cJSON *jdata = cJSON_GetObjectItemCaseSensitive(resp, "data");
        cJSON *l = cJSON_GetObjectItemCaseSensitive(jdata, "lux");

        if (cJSON_IsNumber(l)) {
            data->lux = (float)l->valuedouble;
            have_data = true;
        }
    } else {
        fprintf(stderr, "gateway returned an error for BH1750\n");
    }

    cJSON_Delete(resp);
    return have_data;
}

static void apply_actuator_if_changed(struct client_data *data, int device_id, int want_state, int value, int *current_state, const char *name, float sensor_value)
{
    if (want_state == *current_state) {
        return;
    }

    cJSON *req = build_set_actuator_req(device_id, want_state, value);
    cJSON *resp = NULL;

    if (do_request(data, MSG_SET_ACTUATOR, req, &resp) == 0) {
        printf("%s: %s -> %s (value=%.1f)\n", name, (*current_state == -1) ? "unknown" : (*current_state ? "ON" : "OFF"), want_state ? "ON" : "OFF", sensor_value);
        *current_state = want_state;
        cJSON_Delete(resp);
    } else {
        fprintf(stderr, "failed to send command for %s\n", name);
    }

    cJSON_Delete(req);
}

static int client_config(struct client_data *data, int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <broadcast_addr> <discovery_port>\n", argv[0]);
        return -1;
    }

    strncpy(data->broadcast_addr, argv[1], sizeof(data->broadcast_addr) - 1);
    data->broadcast_addr[sizeof(data->broadcast_addr) - 1] = '\0';

    data->discovery_port = atoi(argv[2]);
    if (data->discovery_port <= 0 || data->discovery_port > 65535) {
        fprintf(stderr, "invalid discovery port: %s\n", argv[2]);
        return -1;
    }

    return 0;
}

static int init_client_data(struct client_data *data)
{
    data->next_request_id = 1;
    data->server_fd = -1;
    data->boardcast_fd = -1;

    data->ac_state = -1;
    data->led_state = -1;
    data->fan_state = -1;

    data->ssl = NULL;

    if (pthread_mutex_init(&data->lock, NULL) != 0) {
        perror("pthread_mutex_init");
        return -1;
    }

    if (pthread_cond_init(&data->stop, NULL) != 0) {
        perror("pthread_cond_init");
        pthread_mutex_destroy(&data->lock);
        return -1;
    }

    data->shutdown = false;

    sigemptyset(&data->new_set);
    sigaddset(&data->new_set, SIGINT);
    sigaddset(&data->new_set, SIGTERM);

    if (pthread_sigmask(SIG_BLOCK, &data->new_set, NULL) != 0) {
        perror("pthread_sigmask");
        pthread_mutex_destroy(&data->lock);
        pthread_cond_destroy(&data->stop);
        return -1;
    }

    data->tls_ctx = init_client_ctx();
    if (data->tls_ctx == NULL) {
        pthread_mutex_destroy(&data->lock);
        pthread_cond_destroy(&data->stop);
        return -1;
    }

    return 0;
}

static void client_cleanup(struct client_data *data)
{
    if (data->ssl != NULL) {
        SSL_shutdown(data->ssl);
        SSL_free(data->ssl);
        data->ssl = NULL;
    }

    if (data->server_fd >= 0) {
        close(data->server_fd);
        data->server_fd = -1;
    }

    if (data->boardcast_fd >= 0) {
        close(data->boardcast_fd);
        data->boardcast_fd = -1;
    }

    if (data->tls_ctx != NULL) {
        SSL_CTX_free(data->tls_ctx);
        data->tls_ctx = NULL;
    }

    pthread_mutex_destroy(&data->lock);
    pthread_cond_destroy(&data->stop);
}

static bool client_should_stop(struct client_data *data)
{
    bool stop;
    pthread_mutex_lock(&data->lock);
    stop = data->shutdown;
    pthread_mutex_unlock(&data->lock);
    return stop;
}

static void sleep_interruptible_cd(struct client_data *data, int seconds)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;

    pthread_mutex_lock(&data->lock);
    while (!data->shutdown) {
        int rc = pthread_cond_timedwait(&data->stop, &data->lock, &ts);
        if (rc == ETIMEDOUT || rc != 0) {
            break;
        }
    }
    pthread_mutex_unlock(&data->lock);
}

static void *signal_thread(void *arg)
{
    struct client_data *data = (struct client_data *)arg;
    int sig = 0;

    int rc = sigwait(&data->new_set, &sig);
    if (rc == 0) {
        printf("\nsignal %d received, initiating graceful shutdown...\n", sig);
    } else {
        fprintf(stderr, "sigwait failed: %s\n", strerror(rc));
    }

    pthread_mutex_lock(&data->lock);
    data->shutdown = true;
    pthread_cond_broadcast(&data->stop);
    pthread_mutex_unlock(&data->lock);

    return NULL;
}


static int setup_boardcast(struct client_data *data)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket (broadcast)");
        return -1;
    }

    int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) < 0) {
        perror("setsockopt SO_BROADCAST");
        close(fd);
        return -1;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct timeval tv;
    tv.tv_sec = RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    data->boardcast_fd = fd;
    return 0;
}


static int do_discovery(struct client_data *data)
{
    const char *msg = "DISCOVERY";

    struct sockaddr_in baddr;
    memset(&baddr, 0, sizeof(baddr));
    baddr.sin_family = AF_INET;
    baddr.sin_port = htons(data->discovery_port);

    if (inet_pton(AF_INET, data->broadcast_addr, &baddr.sin_addr) != 1) {
        fprintf(stderr, "invalid broadcast address: %s\n", data->broadcast_addr);
        return -1;
    }

    for (int attempt = 1; attempt <= DISCOVERY_MAX_RETRIES; attempt++) {
        if (client_should_stop(data)) {
            return -1;
        }

        if (sendto(data->boardcast_fd, msg, strlen(msg), 0, (struct sockaddr *)&baddr, sizeof(baddr)) < 0) {
            perror("sendto (discovery)");
            continue;
        }

        printf("discovery broadcast sent to %s:%d (attempt %d/%d), waiting for gateway...\n", data->broadcast_addr, data->discovery_port, attempt, DISCOVERY_MAX_RETRIES);

        char buf[256];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        int n = recvfrom(data->boardcast_fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from_addr, &from_len);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "discovery timeout, retrying...\n");
            } else {
                perror("recvfrom (discovery)");
            }
            continue;
        }

        buf[n] = '\0';

        cJSON *resp = cJSON_Parse(buf);
        if (resp == NULL) {
            fprintf(stderr, "discovery response is not valid JSON, ignoring\n");
            continue;
        }

        cJSON *port = cJSON_GetObjectItemCaseSensitive(resp, "tcp_port");
        
        if (cJSON_IsNumber(port)) {
            strncpy(data->gateway_host, inet_ntoa(from_addr.sin_addr), sizeof(data->gateway_host) - 1);
            data->gateway_host[sizeof(data->gateway_host) - 1] = '\0';
            
            data->gateway_port = port->valueint;
            
            printf("gateway discovered: %s:%d\n", data->gateway_host, data->gateway_port);
            cJSON_Delete(resp);
            return 0; // Thành công!
        } else {
            fprintf(stderr, "discovery response missing tcp_port field\n");
        }

        cJSON_Delete(resp);
    }

    fprintf(stderr, "discovery failed after %d attempts\n", DISCOVERY_MAX_RETRIES);
    return -1;
}

static int setup_tcp_tls(struct client_data *data)
{
    data->ssl = connect_tls(data->tls_ctx, data->gateway_host, data->gateway_port, &data->server_fd);
    return (data->ssl != NULL) ? 0 : -1;
}

int main(int argc, char **argv)
{
    struct client_data data;
    memset(&data, 0, sizeof(data));

    if (client_config(&data, argc, argv) != 0) {
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    if (init_client_data(&data) != 0) {
        fprintf(stderr, "failed to initialize client data\n");
        return 1;
    }

    pthread_t sig_tid;
    if (pthread_create(&sig_tid, NULL, signal_thread, &data) != 0) {
        perror("pthread_create (signal_thread)");
        client_cleanup(&data);
        return 1;
    }

    printf("starting client: discovery broadcast=%s:%d, poll interval=%ds\n",
           data.broadcast_addr, data.discovery_port, POLL_INTERVAL_SEC);

    bool connected = false;

    while (!client_should_stop(&data)) {

        if (!connected) {
            if (setup_boardcast(&data) != 0) {
                fprintf(stderr, "failed to set up discovery socket, retrying in %d seconds\n", RECONNECT_DELAY_SEC);
                sleep_interruptible_cd(&data, RECONNECT_DELAY_SEC);
                continue;
            }

            int discover_rc = do_discovery(&data);

            close(data.boardcast_fd);
            data.boardcast_fd = -1;

            if (discover_rc != 0) {
                sleep_interruptible_cd(&data, RECONNECT_DELAY_SEC);
                continue;
            }

            if (setup_tcp_tls(&data) != 0) {
                fprintf(stderr, "connection/handshake failed, retrying in %d seconds\n", RECONNECT_DELAY_SEC);
                sleep_interruptible_cd(&data, RECONNECT_DELAY_SEC);
                continue;
            }

            printf("connected to gateway %s:%d securely with %s\n",
                   data.gateway_host, data.gateway_port, SSL_get_version(data.ssl));

            data.ac_state = -1;
            data.led_state = -1;
            data.fan_state = -1;
            connected = true;
        }

        bool have_temp_humi = fetch_temp_humi(&data);
        if (have_temp_humi == false) {
            goto communication_error;
        }

        bool have_lux = fetch_lux(&data);
        if (have_lux == false) {
            goto communication_error;
        }

        printf("sensors: temp=%.1fC humi=%.1f%% lux=%.1f\n", data.temperature, data.humidity, data.lux);

        int want_ac = decide_state(data.temperature, TEMP_ON_THRESHOLD_C, TEMP_OFF_THRESHOLD_C, data.ac_state);
        apply_actuator_if_changed(&data, DEVICE_ID_AC, want_ac, AC_SETPOINT_C, &data.ac_state, "AC", data.temperature);

        int want_fan = decide_state(data.humidity, HUMI_ON_THRESHOLD, HUMI_OFF_THRESHOLD, data.fan_state);
        apply_actuator_if_changed(&data, DEVICE_ID_FAN, want_fan, FAN_SPEED_RPM, &data.fan_state, "FAN", data.humidity);

        int want_led = decide_state_inverted(data.lux, LUX_ON_THRESHOLD, LUX_OFF_THRESHOLD, data.led_state);
        apply_actuator_if_changed(&data, DEVICE_ID_LED, want_led, LED_BRIGHTNESS_PERCENT, &data.led_state, "LED", data.lux);

        sleep_interruptible_cd(&data, POLL_INTERVAL_SEC);
        continue;

    communication_error:
        fprintf(stderr, "communication error, closing connection and retrying\n");
        if (data.ssl != NULL) {
            SSL_shutdown(data.ssl);
            SSL_free(data.ssl);
            data.ssl = NULL;
        }
        if (data.server_fd >= 0) {
            close(data.server_fd);
            data.server_fd = -1;
        }
        connected = false;
        sleep_interruptible_cd(&data, RECONNECT_DELAY_SEC);
    }

    printf("received stop signal, closing connection and exiting\n");

    pthread_join(sig_tid, NULL);
    client_cleanup(&data);

    return 0;
}