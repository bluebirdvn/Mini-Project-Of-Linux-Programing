#ifndef _ACTUATOR_H
#define _ACTUATOR_H

#include <stdint.h>
#include <stddef.h>

struct actuator; 

typedef int (*set_actuator)(void* arg);
typedef int (*get_actuator)(void* arg);
typedef int (*init_actuator)(void* arg);
typedef void (*destructor_actuator)(void* arg);

struct actuator {
    int actuator_id;
    int state;
    double current_setpoint;
    uint64_t last_control_ms;
    int actuator_fd;

    init_actuator init;
    destructor_actuator destructor;
    set_actuator setter;
    get_actuator getter;
};

#endif