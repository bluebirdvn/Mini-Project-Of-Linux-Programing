#ifndef _TASK_EXCUTE_H
#define _TASK_EXCUTE_H

#include "sensor.h"
#include "actuator.h"
#include <stdint.h>

#define AHT30_DEVICE_PATH       "/dev/i2c_aht30"
#define BH1750_DEVICE_PATH      "/dev/i2c_bh1750"
#define AC_DEVICE_PATH          "/dev/actuator_ac"
#define LED_PWM_PATH            "/sys/class/pwm/pwmchip0/pwm0/duty_cycle"
#define FAN_DEVICE_PATH         "/dev/actuator_fan"

#define AHT30_SENSOR_ID     1
#define BH1750_SENSOR_ID    2

#define AC_ACTUATOR_ID      1
#define LED_ACTUATOR_ID     2
#define FAN_ACTUATOR_ID     3

struct aht30_sensor {
    struct sensor base;
    float temperature;
    float humidity;
};

struct bh1750_sensor {
    struct sensor base;
    float lux;
};

struct air_condition {
    struct actuator base;
    int swing;
    int fan_level;
};

struct led_pwm {
    struct actuator base;
    int brightness_percent;
};

struct fan {
    struct actuator base;
    int speed_rpm;
};

int  init_aht30_sensor(void* arg);
void destroy_aht30_sensor(void* arg);
int  get_aht30_sensor_data(void* arg);

int  init_bh1750_sensor(void* arg);
void destroy_bh1750_sensor(void* arg);
int  get_bh1750_sensor_data(void* arg);

int  init_ac_actuator(void* arg);
void destroy_ac_actuator(void* arg);
int  set_ac_actuator(void* arg);

int  init_led_actuator(void* arg);
void destroy_led_actuator(void* arg);
int  set_led_actuator(void* arg);

int  init_fan_actuator(void* arg);
void destroy_fan_actuator(void* arg);
int  set_fan_actuator(void* arg);

#endif