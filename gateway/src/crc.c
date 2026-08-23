#include "crc.h"
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

static uint32_t crc_table[256];
static int table_rd = 0;

static void build_table()
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) {
            c = 0xEDB88320UL ^ (c >> 1);
            } else {
            c = (c >> 1);
            }
        }

        crc_table[i] = c;
    }

    table_rd = 1;
}

uint32_t crc_calculate(const uint8_t* data, size_t size)
{
    if (table_rd != 1) {
        build_table();
    }

    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < size; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);

    }

    return crc ^ 0xFFFFFFFFUL;

}
