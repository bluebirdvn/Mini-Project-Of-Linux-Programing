#ifndef _PROTOCOL_MESSAGE_H
#define _PROTOCOL_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#define PROTOCOL_MAGIC 0xFFAABBCCu
#define PROTOCOL_VERSION 1

#define CRC_SIZE 4
#define HEADER_SIZE 16

enum read_stage {
    READ_HEADER = 1,
    READ_PAYLOAD,
    READ_CRC
};

struct header_packet {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t length;
    uint32_t request_id;
};

enum err_code {
    ERR_UNKNOWN_TYPE = 1,
    ERR_BAD_MAGIC,
    ERR_BAD_VERSION,    
    ERR_PAYLOAD_TOO_LARGE,
    ERR_CRC_MISMATCH,
    ERR_RATE_LIMIT,
    ERR_SERVER_BUSY,
    ERR_SERVER_FULL,
    ERR_TIMEOUT,
    ERR_BAD_JSON,
};

enum message_type {
    MSG_ECHO = 1,
    MSG_STATUS,
    MSG_SET_DEVICE,
    MSG_GET_SENSOR,
    MSG_SET_ACTUATOR = 0x10,
    MSG_GET_ACTUATOR = 0x11,
    MSG_READ_SENSOR_ALL = 0x20,
    MSG_RESPONSE = 0x8001,
    MSG_ERROR = 0x8002
};

/**
 * @brief encode a interger 16 bit into byte order of network
 * 
 * @param buffer pointer to buffer contains var encoded
 * @param var_to_encode interger to encode
 */
void encode_16_bit(uint8_t *buffer, uint16_t var_to_encode);

/**
 * @brief encode a interger 32 bit into byte order of network
 * 
 * @param buffer  pointer to buffer contains var encoded
 * @param var_to_encode interger_to_encode
 */
void encode_32_bit(uint8_t *buffer, uint32_t var_to_encode);

/**
 * @brief decode byte order of network to machine order byte
 * 
 * @param buffer_to_decode buffer contain byte to decode
 * @return uint16_t return interger decoded by machine order
 */
uint16_t decode_16_bit(const uint8_t* buffer_to_decode);

/**
 * @brief decode byte order of network to machine order byte
 * 
 * @param buffer_to_decode  buffer contain byte to decode
 * @return uint32_t return interger decoded by machine order
 */
uint32_t decode_32_bit(const uint8_t* buffer_to_decode);

/**
 * @brief 
 * 
 * @param buffer pointer to data encoded
 * @param size sizeof paket to check
 * @param type type of packet
 * @param request_id number packet
 * @param payload pointer to data
 * @param length sizeof data in payload
 * @return int 0 if success, -1 if error
 */
int packet_encode(uint8_t* buffer, size_t size, uint16_t type, uint32_t request_id, const void* payload, uint32_t length);

/**
 * @brief build a error packet
 * 
 * @param request_id 
 * @param err_code 
 * @param out_size 
 * @return int 
 */
uint8_t* build_error_packet(uint32_t request_id, uint8_t err_code, size_t *out_size);

#endif