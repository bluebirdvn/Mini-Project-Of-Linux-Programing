#include "protocol_message.h"
#include "crc.h"
#include "common.h"
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "logger.h"
#include <stdlib.h>
void encode_16_bit(uint8_t *buffer, uint16_t var_to_encode)
{
    if (buffer == NULL) {
        LOG_ERROR("null params");
        return;
    }

    buffer[0] = var_to_encode >> 8;
    buffer[1] = var_to_encode & 0xFF;

    LOG_DEBUG("encode success");
    return;
}

/**
 * @brief encode a interger 32 bit into byte order of network
 * 
 * @param buffer  pointer to buffer contains var encoded
 * @param var_to_encode interger_to_encode
 */
void encode_32_bit(uint8_t *buffer, uint32_t var_to_encode)
{
    if (buffer == NULL) {
        LOG_ERROR("null params");
        return;
    }

    buffer[0] = var_to_encode >> 24;
    buffer[1] = var_to_encode >> 16;
    buffer[2] = var_to_encode >> 8;
    buffer[3] = var_to_encode;

    LOG_DEBUG("encode success");
    return;
}

/**
 * @brief decode byte order of network to machine order byte
 * 
 * @param buffer_to_decode buffer contain byte to decode
 * @return uint16_t return interger decoded by machine order
 */
uint16_t decode_16_bit(const uint8_t* buffer_to_decode)
{
    uint16_t number_decode;

    number_decode = (uint16_t)(buffer_to_decode[0] << 8) | (uint16_t)buffer_to_decode[1];

    return number_decode;
}

/**
 * @brief decode byte order of network to machine order byte
 * 
 * @param buffer_to_decode  buffer contain byte to decode
 * @return uint32_t return interger decoded by machine order
 */
uint32_t decode_32_bit(const uint8_t* buffer_to_decode)
{
    uint32_t number_decode;

    number_decode = (uint32_t)(buffer_to_decode[0] << 24) | (uint32_t)(buffer_to_decode[1] << 16) | (uint32_t)(buffer_to_decode[2] << 8) | (uint32_t)(buffer_to_decode[3]);

    return number_decode;
}

int packet_encode(uint8_t* buffer, size_t size, uint16_t type, uint32_t request_id, const void* payload, uint32_t length)
{
    if (buffer == NULL) {
        LOG_ERROR("null params.");
        return -1;
    }  

    size_t total_len = length + HEADER_SIZE + CRC_SIZE;
    if (total_len > size || length >MAX_PAYLOAD) {
        LOG_ERROR("len not match in packet");
        return -1;
    }

    encode_32_bit(buffer, PROTOCOL_MAGIC);
    encode_16_bit(buffer + 4, PROTOCOL_VERSION);
    encode_16_bit(buffer + 6, type);
    encode_32_bit(buffer + 8, length);
    encode_32_bit(buffer + 12, request_id);

    if (length > 0) {
        memcpy(buffer + HEADER_SIZE, payload, length);
    }

    uint32_t crc = crc_calculate(buffer + HEADER_SIZE, length);
    encode_32_bit(buffer + HEADER_SIZE + length, crc);

    return 0;

}


uint8_t* build_error_packet(uint32_t request_id, uint8_t err_code, size_t *out_size)
{
    uint32_t payload_len = 1;
    size_t total_size = HEADER_SIZE + payload_len + CRC_SIZE;

    uint8_t *packet_buf = malloc(total_size);
    if (packet_buf == NULL) {
        LOG_ERROR("can't alloc memory for error packet");
        return NULL;
    }

    int ret = packet_encode(packet_buf, total_size, MSG_ERROR, request_id, &err_code, payload_len);
    if (ret < 0) {
        LOG_ERROR("failed to encode error packet");
        free(packet_buf);
        return NULL;
    }

    if (out_size != NULL) {
        *out_size = total_size;
    }

    return packet_buf;
}

