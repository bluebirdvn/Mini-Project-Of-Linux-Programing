




#ifndef _PAYLOAD_JSON_H
#define _PAYLOAD_JSON_H

#include <stdint.h>
#include <stddef.h>

// #define TYPE "type"
// #define ECHO "echo"
// #define STATUS "status"
// #define SENSOR_ID "sensor_id"
// #define DEVICE_ID "device_id"
// #define TEMP "temp"
// #define HUMI "humi"
// #define LUX "lux"
// #define STATE "state"
// #define SET_POINT "set_point"

/**
 * @brief parse echo packet received
 * 
 * @param payload payload received in uint8_t type
 * @param len length of payload
 * @param msg_out pointer to message after parsed
 * @param msg_cap sizeof of msg_out
 * @return int 0 if success, -1 if failed
 */
int json_parse_echo_req(const uint8_t *payload, uint32_t len, char *msg_out, size_t msg_cap);

/**
 * @brief parse requesting data senor packet received
 * 
 * @param payload payload received in uint8_t type
 * @param len length of payload
 * @param sensor_id_out id of sensor that parsed from payload
 * @return  int 0 if success, -1 if failed
 */
int json_parse_get_sensor_req(const uint8_t *payload, uint32_t len, int *sensor_id_out);

/**
 * @brief parse setting actuator packet received
 * 
 * @param payload payload received in uint8_t type
 * @param len length of payload
 * @param device_id_out id of actuator that parsed from payload
 * @param state_out state of actuator that parsed from payload
 * @param value_out setpoint of actuator that parsed from payload
 * @return int int 0 if success, -1 if failed
 */
int json_parse_set_actuator_req(const uint8_t *payload, uint32_t len, int *device_id_out, int *state_out, int *value_out);

/**
 * @brief build echo packet for responsding echo meesasge
 * 
 * @param buf buffer that contains the generated JSON string
 * @param cap maximum capacity of the buffer 
 * @param msg message want to sent in string
 * @return int length of the generated JSON string, or -1 if failed
 */
int json_build_echo_resp(char *buf, size_t cap, const char *msg);


/**
 * @brief build response containing gateway status
 * 
 * @param buf buffer that contains the generated JSON string
 * @param cap maximum capacity of the buffer
 * @param uptime_ms gateway uptime in milliseconds
 * @param active_workers number of currently active worker threads
 * @return int length of the generated JSON string, or -1 if failed
 */
int json_build_status_resp(char *buf, size_t cap, uint64_t uptime_ms, int active_workers);


/**
 * @brief build response for AHT30 sensor data
 * 
 * @param buf buffer that contains the generated JSON string
 * @param cap maximum capacity of the buffer
 * @param temperature temperature measured temperature value
 * @param humidity humidity measured humidity value
 * @return int length of the generated JSON string, or -1 if failed
 */
int json_build_sensor_aht30_resp(char *buf, size_t cap, float temperature, float humidity);

/**
 * @brief 
 * 
 * @param buf 
 * @param cap 
 * @param lux 
 * @return int 
 */
int json_build_sensor_bh1750_resp(char *buf, size_t cap, float lux);

/**
 * @brief 
 * 
 * @param buf 
 * @param cap 
 * @param message 
 * @return int 
 */
int json_build_actuator_resp(char *buf, size_t cap, const char *message);

/**
 * @brief 
 * 
 * @param buf 
 * @param cap 
 * @param error_code 
 * @param message 
 * @return int 
 */
int json_build_error_resp(char *buf, size_t cap, int error_code, const char *message);

#endif  