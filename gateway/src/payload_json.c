
#include "payload_json.h"
#include "logger.h"
#include <cJSON.h>
#include <string.h>
#include <stdint.h>

static cJSON *parse_payload(const uint8_t *payload, uint32_t len)
{
    if (payload == NULL || len == 0) {
        LOG_WARN("empty payload");
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength((const char *)payload, len);
    if (root == NULL) {
        const char *err = cJSON_GetErrorPtr();
        LOG_ERROR("JSON parse failed: %s", err ? err : "(unkown)");
        return NULL;
    }
    return root;
}

static int print_and_free(cJSON *root, char *buf, size_t cap)
{
    if (root == NULL) {
        LOG_ERROR("cJSON_CreateObject failed");
        return -1;
    }

    cJSON_bool ok = cJSON_PrintPreallocated(root, buf, (int)cap, /*format=*/0);
    cJSON_Delete(root);

    if (!ok) {
        LOG_ERROR("buffer too small JSON response (cap=%zu)", cap);
        return -1;
    }
    return (int)strlen(buf);
}


int json_parse_echo_req(const uint8_t *payload, uint32_t len, char *msg_out, size_t msg_cap)
{
    if (msg_out == NULL || msg_cap == 0) {
        LOG_ERROR("invalid params");
        return -1;
    }

    cJSON *root = parse_payload(payload, len);
    if (root == NULL) return -1;

    cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "msg");
    if (!cJSON_IsString(msg) || msg->valuestring == NULL) {
        LOG_ERROR("get msg failed");
        cJSON_Delete(root);
        return -1;
    }

    snprintf(msg_out, msg_cap, "%s", msg->valuestring);

    cJSON_Delete(root);
    return 0;
}

int json_parse_get_sensor_req(const uint8_t *payload, uint32_t len, int *sensor_id_out)
{
    if (sensor_id_out == NULL) {
        LOG_ERROR("invalid params");
        return -1;
    }

    cJSON *root = parse_payload(payload, len);
    if (root == NULL) return -1;

    cJSON *sensor_id = cJSON_GetObjectItemCaseSensitive(root, "sensor_id");
    if (!cJSON_IsNumber(sensor_id)) {
        LOG_ERROR("sensor id mismatch or mismatch GET_SENSOR request");
        cJSON_Delete(root);
        return -1; 
    }

    *sensor_id_out = sensor_id->valueint;

    cJSON_Delete(root);
    return 0;
}

int json_parse_set_actuator_req(const uint8_t *payload, uint32_t len, int *device_id_out, int *state_out, int *value_out)
{
    if (device_id_out == NULL || state_out == NULL || value_out == NULL) {
        LOG_ERROR("invalid params");
        return -1;
    }

    cJSON *root = parse_payload(payload, len);
    if (root == NULL) {
        return -1;
    }

    cJSON *device_id = cJSON_GetObjectItemCaseSensitive(root, "device_id");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");

    if (!cJSON_IsNumber(device_id) || !cJSON_IsNumber(state) || !cJSON_IsNumber(value)) {
        LOG_ERROR("miss field 'device_id'/'state'/'value' in SET_ACTUATOR request");
        cJSON_Delete(root);
        return -1;
    }

    *device_id_out = device_id->valueint;
    *state_out = state->valueint;
    *value_out = value->valueint;

    cJSON_Delete(root);
    return 0;
}

int json_build_echo_resp(char *buf, size_t cap, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "msg", msg ? msg : "");

    return print_and_free(root, buf, cap);
}

int json_build_status_resp(char *buf, size_t cap, uint64_t uptime_ms, int active_workers)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddNumberToObject(root, "uptime_ms", (double)uptime_ms);
    cJSON_AddNumberToObject(root, "active_workers", active_workers);

    return print_and_free(root, buf, cap);
}

int json_build_sensor_aht30_resp(char *buf, size_t cap, float temperature, float humidity)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "sensor_name", "AHT30");

    cJSON *data = cJSON_CreateObject();
    if (data == NULL) { 
        cJSON_Delete(root); 
        return -1; 
    }
    cJSON_AddNumberToObject(data, "temp", temperature);
    cJSON_AddNumberToObject(data, "humi", humidity);
    cJSON_AddItemToObject(root, "data", data); 

    return print_and_free(root, buf, cap);
}

int json_build_sensor_bh1750_resp(char *buf, size_t cap, float lux)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "sensor_name", "BH1750");

    cJSON *data = cJSON_CreateObject();
    if (data == NULL) { 
        cJSON_Delete(root); 
        return -1; 
    }
    cJSON_AddNumberToObject(data, "lux", lux);
    cJSON_AddItemToObject(root, "data", data);

    return print_and_free(root, buf, cap);
}

int json_build_actuator_resp(char *buf, size_t cap, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "message", message ? message : "");

    return print_and_free(root, buf, cap);
}

int json_build_error_resp(char *buf, size_t cap, int error_code, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddNumberToObject(root, "error_code", error_code);
    cJSON_AddStringToObject(root, "message", message ? message : "");

    return print_and_free(root, buf, cap);
}