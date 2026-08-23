
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <cjson/cJSON.h>

#include "../include/protocol_message.h"
#include "../include/common.h"
#include "../include/crc.h"

#define POLL_INTERVAL_SEC 5
#define RECONNECT_DELAY_SEC 3
#define RECV_TIMEOUT_SEC 3

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

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int connect_tcp(const char *host, int port)
{
    int fd;
    struct timeval tv;
    int nodelay;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
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
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

static int send_all(int fd, const uint8_t *buf, size_t len)
{
    size_t sent;
    ssize_t n;

    sent = 0;
    while (sent < len)
    {
        n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)n;
    }

    return 0;
}

static int recv_one_frame(int fd, uint16_t *type_out, uint8_t *payload_out, size_t cap, uint32_t *len_out)
{
    uint8_t buf[HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE];
    size_t have;
    ssize_t n;
    uint32_t length;
    size_t total;
    uint32_t crc_rev;
    uint32_t crc_cal;

    have = 0;

    for (;;) {
        n = recv(fd, buf + have, sizeof(buf) - have, 0);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "recv timeout after %d seconds, no reply from gateway\n", RECV_TIMEOUT_SEC);
            }
            else {
                perror("recv");
            }

            return -1;
        }

        if (n == 0) {
            fprintf(stderr, "gateway closed the connection\n");
            return -1;
        }

        have += (size_t)n;

        if (have < HEADER_SIZE) {
            continue;
        }

        length = decode_32_bit(buf + 8);

        if (length > MAX_PAYLOAD) {
            fprintf(stderr, "length field exceeds MAX_PAYLOAD, data is corrupted\n");
            return -1;
        }

        total = HEADER_SIZE + length + CRC_SIZE;

        if (have < total) {
            continue;
        }

        crc_rev = decode_32_bit(buf + HEADER_SIZE + length);
        crc_cal = crc_calculate(buf + HEADER_SIZE, length);

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

static int do_request(int fd, uint16_t type, cJSON *req_obj, uint32_t request_id, cJSON **resp_obj_out)
{
    char *json_str;
    uint32_t length;
    uint8_t out[HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE];
    int rc;
    size_t total_len;
    uint16_t resp_type;
    uint8_t resp_payload[MAX_PAYLOAD + 1];
    uint32_t resp_len;
    cJSON *resp;

    json_str = cJSON_PrintUnformatted(req_obj);
    if (json_str == NULL) {
        return -1;
    }

    length = (uint32_t)strlen(json_str);

    rc = packet_encode(out, sizeof(out), type, request_id, json_str, length);
    free(json_str);

    if (rc < 0) {
        fprintf(stderr, "packet_encode failed\n");
        return -1;
    }

    total_len = HEADER_SIZE + length + CRC_SIZE;

    if (send_all(fd, out, total_len) != 0) {
        return -1;
    }

    if (recv_one_frame(fd, &resp_type, resp_payload, MAX_PAYLOAD, &resp_len) != 0) {
        return -1;
    }

    resp_payload[resp_len] = '\0';

    resp = cJSON_Parse((const char *)resp_payload);
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

static cJSON *build_get_sensor_req(int sensor_id)
{
    cJSON *root;

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", "get_sensor");
    cJSON_AddNumberToObject(root, "sensor_id", sensor_id);

    return root;
}

static cJSON *build_set_actuator_req(int device_id, int state, int value)
{
    cJSON *root;

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", "set_actuator");
    cJSON_AddNumberToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "state", state);
    cJSON_AddNumberToObject(root, "value", value);

    return root;
}

static int decide_state(float value, float on_threshold, float off_threshold, int current_state)
{
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

static int decide_state_inverted(float value, float on_threshold_low, float off_threshold_high, int current_state)
{
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

static bool fetch_temp_humi(int fd, uint32_t *request_id, float *temperature_out, float *humidity_out)
{
    cJSON *req;
    cJSON *resp;
    int rc;
    bool have_data;
    cJSON *status;

    have_data = false;

    req = build_get_sensor_req(SENSOR_ID_AHT30);
    resp = NULL;
    rc = do_request(fd, MSG_GET_SENSOR, req, (*request_id)++, &resp);
    cJSON_Delete(req);

    if (rc != 0) {
        return false;
    }

    status = cJSON_GetObjectItemCaseSensitive(resp, "status");

    if (cJSON_IsString(status) && strcmp(status->valuestring, "ok") == 0) {
        cJSON *data;
        cJSON *t;
        cJSON *h;

        data = cJSON_GetObjectItemCaseSensitive(resp, "data");
        t = cJSON_GetObjectItemCaseSensitive(data, "temp");
        h = cJSON_GetObjectItemCaseSensitive(data, "humi");

        if (cJSON_IsNumber(t) && cJSON_IsNumber(h)) {
            *temperature_out = (float)t->valuedouble;
            *humidity_out = (float)h->valuedouble;
            have_data = true;
        }
    }
    else {
        fprintf(stderr, "gateway returned an error for AHT30\n");
    }

    cJSON_Delete(resp);

    return have_data;
}

static bool fetch_lux(int fd, uint32_t *request_id, float *lux_out)
{
    cJSON *req;
    cJSON *resp;
    int rc;
    bool have_data;
    cJSON *status;

    have_data = false;

    req = build_get_sensor_req(SENSOR_ID_BH1750);
    resp = NULL;
    rc = do_request(fd, MSG_GET_SENSOR, req, (*request_id)++, &resp);
    cJSON_Delete(req);

    if (rc != 0) {
        return false;
    }

    status = cJSON_GetObjectItemCaseSensitive(resp, "status");

    if (cJSON_IsString(status) && strcmp(status->valuestring, "ok") == 0) {
        cJSON *data;
        cJSON *l;

        data = cJSON_GetObjectItemCaseSensitive(resp, "data");
        l = cJSON_GetObjectItemCaseSensitive(data, "lux");

        if (cJSON_IsNumber(l)) {
            *lux_out = (float)l->valuedouble;
            have_data = true;
        }
    } else {
        fprintf(stderr, "gateway returned an error for BH1750\n");
    }

    cJSON_Delete(resp);

    return have_data;
}

static void apply_actuator_if_changed(int fd, uint32_t *request_id, int device_id, int want_state, int value, int *current_state, const char *name, float sensor_value)
{
    cJSON *req;
    cJSON *resp;

    if (want_state == *current_state) {
        return;
    }

    req = build_set_actuator_req(device_id, want_state, value);
    resp = NULL;

    if (do_request(fd, MSG_SET_ACTUATOR, req, (*request_id)++, &resp) == 0) {
        printf("%s: %s -> %s (value=%.1f)\n",
               name,
               (*current_state == -1) ? "unknown" : (*current_state ? "ON" : "OFF"),
               want_state ? "ON" : "OFF",
               sensor_value);

        *current_state = want_state;
        cJSON_Delete(resp);
    } else {
        fprintf(stderr, "failed to send command for %s\n", name);
    }

    cJSON_Delete(req);
}

static void sleep_interruptible(int seconds)
{
    int i;

    for (i = 0; i < seconds; i++) {
        if (g_stop) {
            break;
        }
        sleep(1);
    }
}

int main(int argc, char **argv)
{
    const char *host;
    int port;
    struct sigaction sa;
    int fd;
    uint32_t next_request_id;
    int ac_state;
    int led_state;
    int fan_state;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        return 1;
    }

    host = argv[1];
    port = atoi(argv[2]);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    fd = -1;
    next_request_id = 1;
    ac_state = -1;
    led_state = -1;
    fan_state = -1;

    printf("starting, polling every %d seconds, gateway=%s:%d\n", POLL_INTERVAL_SEC, host, port);

    while (!g_stop) {
        float temperature;
        float humidity;
        float lux;
        bool have_temp_humi;
        bool have_lux;

        if (fd < 0) {
            fd = connect_tcp(host, port);

            if (fd < 0) {
                fprintf(stderr, "connection failed, retrying in %d seconds\n", RECONNECT_DELAY_SEC);
                sleep_interruptible(RECONNECT_DELAY_SEC);
                continue;
            }

            printf("connected to gateway\n");
            ac_state = -1;
            led_state = -1;
            fan_state = -1;
        }

        temperature = 0.0f;
        humidity = 0.0f;
        lux = 0.0f;

        have_temp_humi = fetch_temp_humi(fd, &next_request_id, &temperature, &humidity);

        if (!have_temp_humi) {
            fprintf(stderr, "communication error, closing connection and retrying\n");
            close(fd);
            fd = -1;
            sleep_interruptible(RECONNECT_DELAY_SEC);
            continue;
        }

        have_lux = fetch_lux(fd, &next_request_id, &lux);

        if (!have_lux) {
            fprintf(stderr, "communication error, closing connection and retrying\n");
            close(fd);
            fd = -1;
            sleep_interruptible(RECONNECT_DELAY_SEC);
            continue;
        }

        printf("sensors: temp=%.1fC humi=%.1f%% lux=%.1f\n", temperature, humidity, lux);

        {
            int want_ac;
            int want_fan;
            int want_led;

            want_ac = decide_state(temperature, TEMP_ON_THRESHOLD_C, TEMP_OFF_THRESHOLD_C, ac_state);
            apply_actuator_if_changed(fd, &next_request_id, DEVICE_ID_AC, want_ac, AC_SETPOINT_C, &ac_state, "AC", temperature);

            want_fan = decide_state(humidity, HUMI_ON_THRESHOLD, HUMI_OFF_THRESHOLD, fan_state);
            apply_actuator_if_changed(fd, &next_request_id, DEVICE_ID_FAN, want_fan, FAN_SPEED_RPM, &fan_state, "FAN", humidity);

            want_led = decide_state_inverted(lux, LUX_ON_THRESHOLD, LUX_OFF_THRESHOLD, led_state);
            apply_actuator_if_changed(fd, &next_request_id, DEVICE_ID_LED, want_led, LED_BRIGHTNESS_PERCENT, &led_state, "LED", lux);
        }

        sleep_interruptible(POLL_INTERVAL_SEC);
    }

    printf("received stop signal, closing connection and exiting\n");

    if (fd >= 0) {
        close(fd);
    }

    return 0;
}