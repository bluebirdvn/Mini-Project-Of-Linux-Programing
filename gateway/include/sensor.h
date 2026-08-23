#ifndef _SENSOR_H
#define _SENSOR_H

#include <stdint.h>
#include <stddef.h>

struct sensor;

typedef int (*init_sensor)(void* arg);
typedef int (*get_sensor)(void* arg);
typedef void (*destructor_sensor)(void* arg);

struct sensor {
    int sensor_id;
    int state;
    uint64_t last_control_ms;
    int sensor_fd;
    uint8_t *data;

    init_sensor init;
    destructor_sensor destructor;
    get_sensor getter;
};

#endif