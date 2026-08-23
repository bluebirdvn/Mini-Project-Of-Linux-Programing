
#include "worker_thread.h"
#include "sensor.h"
#include "actuator.h"
#include "task_execute.h"
#include "payload_json.h"
#include "protocol_message.h"
#include "logger.h"
#include "server.h"
#include "common.h"
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>     
#include <sys/epoll.h>
#include <unistd.h>


int notify_main_thread(int event_fd)
{
    if (event_fd < 0) {
        LOG_ERROR("invalid params");
        return -1;
    }

    uint64_t event = 1;
    ssize_t ret = write(event_fd, &event, sizeof(event));
    if (ret < 0) {
        int err = errno;
        if (err == EINTR) {
            LOG_WARN("write was interrupted, thu lai");
            return notify_main_thread(event_fd);
        }
        LOG_ERROR("error writing eventfd: %d", err);
        return -1;
    }

    LOG_DEBUG("wakeup mainthread through eventfd");
    return 0;
}

struct response* make_response(int slot, uint64_t generation, uint16_t type, uint32_t request_id, const void *payload, uint32_t length)
{
    struct response* rp = malloc(sizeof(struct response));
    if (rp == NULL) {
        LOG_ERROR("malloc failed");
        return NULL;
    }

    rp->slot = slot;
    rp->generation = generation;
    rp->type = type;
    rp->request_id = request_id;

    if (length > MAX_PAYLOAD) {
        LOG_WARN("payload response exceed MAX_PAYLOAD, rm from %u to %d", length, MAX_PAYLOAD);
        length = MAX_PAYLOAD;
    }
    rp->length = length;

    if (length > 0 && payload != NULL) {
        memcpy(rp->payload, payload, length);
    }
    rp->next_response = NULL;

    return rp;
}

static struct response* make_error_response(struct task *task, int err_code, const char *message)
{
    char err_buf[128];
    int n = json_build_error_resp(err_buf, sizeof(err_buf), err_code, message);
    if (n < 0) {
        n = json_build_error_resp(err_buf, sizeof(err_buf), err_code, "internal_error");
        if (n < 0) {
            return NULL;
        }
    }
    return make_response(task->slot, task->generation, MSG_ERROR, task->request_id, err_buf, (uint32_t)n);
}

static struct response* process_hardware_task(long worker_id, struct task *task)
{
    if (task == NULL) return NULL;

    LOG_DEBUG("worker %d with task type=0x%04x request_id=%u", worker_id, task->type, task->request_id);

    char resp_buf[256];
    int resp_len;

    switch (task->type) {

    case MSG_ECHO: {
        char msg_in[128];
        if (json_parse_echo_req(task->payload, task->length, msg_in, sizeof(msg_in)) < 0) {
            return make_error_response(task, ERR_BAD_JSON, "missing_or_invalid_field_msg");
        }
        resp_len = json_build_echo_resp(resp_buf, sizeof(resp_buf), msg_in);

        
        if (resp_len < 0) {
            return make_error_response(task, ERR_BAD_JSON, "response_too_large");
        }

        return make_response(task->slot, task->generation, MSG_RESPONSE, task->request_id, resp_buf, (uint32_t)resp_len);
    }

    case MSG_STATUS: {
        resp_len = json_build_status_resp(resp_buf, sizeof(resp_buf), now_ms(), NUM_WORKERS);

        if (resp_len < 0) {
            return make_error_response(task, ERR_BAD_JSON, "response_too_large");
        }

        return make_response(task->slot, task->generation, MSG_RESPONSE, task->request_id, resp_buf, (uint32_t)resp_len);
    }

    case MSG_GET_SENSOR: {
        int sensor_id;
        if (json_parse_get_sensor_req(task->payload, task->length, &sensor_id) < 0) {
            return make_error_response(task, ERR_BAD_JSON, "missing_or_invalid_field_sensor_id");
        }

        if (sensor_id == AHT30_SENSOR_ID) {
            struct aht30_sensor dev; 
            if (init_aht30_sensor(&dev) != 0) {
                return make_error_response(task, ERR_UNKNOWN_TYPE, "aht30_hardware_init_fail");
            }
            get_aht30_sensor_data(&dev);
            destroy_aht30_sensor(&dev);

            resp_len = json_build_sensor_aht30_resp(resp_buf, sizeof(resp_buf), dev.temperature, dev.humidity);

        } else if (sensor_id == BH1750_SENSOR_ID) {

            struct bh1750_sensor dev;
            if (init_bh1750_sensor(&dev) != 0) {
                return make_error_response(task, ERR_UNKNOWN_TYPE, "bh1750_hardware_init_fail");
            }
            get_bh1750_sensor_data(&dev);
            destroy_bh1750_sensor(&dev);

            resp_len = json_build_sensor_bh1750_resp(resp_buf, sizeof(resp_buf), dev.lux);

        } else {
            return make_error_response(task, ERR_UNKNOWN_TYPE, "unknown_sensor_id");
        }

        if (resp_len < 0) {
            return make_error_response(task, ERR_BAD_JSON, "response_too_large");
        }
        return make_response(task->slot, task->generation, MSG_RESPONSE, task->request_id, resp_buf, (uint32_t)resp_len);
    }

    case MSG_SET_ACTUATOR: {
        int device_id, state, value;
        if (json_parse_set_actuator_req(task->payload, task->length, &device_id, &state, &value) < 0) {
            return make_error_response(task, ERR_BAD_JSON, "missing_or_invalid_field_device_id_state_value");
        }

        bool ok = false;

        if (device_id == AC_ACTUATOR_ID) {
            struct air_condition dev; 
            if (init_ac_actuator(&dev) == 0) {
                dev.base.state = state;
                dev.base.current_setpoint = (double)value;
                ok = (set_ac_actuator(&dev) == 0);
                destroy_ac_actuator(&dev);
            }
        } else if (device_id == LED_ACTUATOR_ID) {
            struct led_pwm dev;
            if (init_led_actuator(&dev) == 0) {
                dev.base.state = state;
                dev.brightness_percent = value;
                ok = (set_led_actuator(&dev) == 0);
                destroy_led_actuator(&dev);
            }
        } else if (device_id == FAN_ACTUATOR_ID) {
            struct fan dev;
            if (init_fan_actuator(&dev) == 0) {
                dev.base.state = state;
                dev.speed_rpm = value;
                ok = (set_fan_actuator(&dev) == 0);
                destroy_fan_actuator(&dev);
            }
        } else {
            return make_error_response(task, ERR_UNKNOWN_TYPE, "unknown_device_id");
        }

        if (!ok) {
            return make_error_response(task, ERR_UNKNOWN_TYPE, "actuator_hardware_control_fail");
        }

        resp_len = json_build_actuator_resp(resp_buf, sizeof(resp_buf), "actuator_control_success");
        if (resp_len < 0) {
            return make_error_response(task, ERR_BAD_JSON, "response_too_large");
        }
        return make_response(task->slot, task->generation, MSG_RESPONSE, task->request_id, resp_buf, (uint32_t)resp_len);
    }

    default: {
        return make_error_response(task, ERR_UNKNOWN_TYPE, "unsupported_message_type");
    }
    }
}


void *worker_thread(void* arg)
{
    if (arg == NULL) {
        return NULL;
    }

    struct work_task *work = (struct work_task *)arg;
    long worker_id = (long)work->id;

    LOG_INFO("[worker-%ld] started", worker_id);

    for (;;) {
        struct task* t = pop(work->task_queue);
        if (t == NULL) {
            break;
        }

        if (now_ms() > t->timeout_ms) {
            LOG_WARN("[worker-%ld] task request_id=%u timeout in queue, delete", worker_id, t->request_id);
            free(t);
            continue;
        }

        struct response* resp = process_hardware_task(worker_id, t);

        if (now_ms() > t->timeout_ms) {
            LOG_WARN("[worker-%ld] task request_id=%u timeout when proccessing", worker_id, t->request_id);
            free(resp);
            resp = make_error_response(t, ERR_TIMEOUT, "processing_timeout");
        }

        if (resp != NULL) {
            if (response_queue_push(work->response_queue, resp) != 0) {
                LOG_ERROR("[worker-%ld] response queue full, drop response request_id=%u", worker_id, t->request_id);
                free(resp);
            } else {
                notify_main_thread(work->event_fd);
            }
        }

        free(t);
    }

    LOG_INFO("[worker-%ld] stopped", worker_id);
    return NULL;
}


void *main_thread(void *arg)
{
    if (arg == NULL) {
        return NULL;
    }
    struct server* sv = (struct server*)arg;
    struct epoll_event events[MAX_EVENTS];

    for (;;) {
        int n = epoll_wait(sv->epoll_fd, events, MAX_EVENTS, EPOLL_TIMEOUT_MS);

        if (n < 0) {
            int err = errno;
            if (err == EINTR) {
                LOG_WARN("epoll wait was interrupted");
            } else {
                LOG_ERROR("epoll_wait failed: %d, stop main_thread", err);
                break;
            }
        } else if (n > 0) {
            for (int i = 0; i < n; i++) {
                int fd = events[i].data.fd;
                uint32_t ev = events[i].events;

                if (sv->listen_fd == fd && (ev & EPOLLIN)) {
                    handle_accept_client(sv);
                    continue;
                }
                if (sv->broadcast_fd == fd && (ev & EPOLLIN)) {
                    handle_discovery(sv);
                    continue;
                }
                if (sv->event_fd == fd && (ev & EPOLLIN)) {
                    handle_response_to_client(sv);
                    continue;
                }

                int slot = fd_to_slot(sv->clients, fd, MAX_CLIENTS);
                if (slot < 0) continue;

                if (fd == sv->clients[slot].fd && (ev & (EPOLLHUP | EPOLLERR))) {
                    close_client(sv->clients, slot);
                    continue;
                }
                if (fd == sv->clients[slot].fd && (ev & EPOLLIN)) {
                    handle_request_client(sv, fd);
                }
                if (fd == sv->clients[slot].fd && (ev & EPOLLOUT) && sv->clients[slot].fd >= 0) {
                    handle_client_sendable(sv, fd);
                }
            }
        }
        reap_client(sv);

        if (atomic_load(&sv->shutdown) == true) {
            break;
        }
    }

    LOG_INFO("main_thread stop");
    return NULL;
}

void *signal_thread(void* arg)
{
    if (arg == NULL) return NULL;

    struct server* sv = (struct server*)arg;
    sigset_t *s = &sv->new_set;
    siginfo_t info;

    for (;;) {
        if (sigwaitinfo(s, &info) == -1) {
            if (errno == EINTR) continue;
            LOG_ERROR("sigwaitinfo failed: %d", errno);
            continue;
        }

        int sig_num = info.si_signo;
        if (sig_num == SIGTERM || sig_num == SIGINT) {
            LOG_INFO("get signal %d, shutdown", sig_num);
            server_shutdown(sv);
            break;
        }
    }

    return NULL;
}