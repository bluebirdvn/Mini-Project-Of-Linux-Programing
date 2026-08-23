#ifndef _CRC_H
#define _CRC_H
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

uint32_t crc_calculate(const uint8_t* data, size_t size);

#endif