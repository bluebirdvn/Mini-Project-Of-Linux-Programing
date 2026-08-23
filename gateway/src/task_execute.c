#include "task_execute.h"
#include "common.h"
#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int init_aht30_sensor(void* arg) 
{
    if (arg == NULL) {
        LOG_ERROR("null param");
        return -1;
    }

    struct aht30_sensor* dev = (struct aht30_sensor*)arg;

    dev->base.sensor_fd = open(AHT30_DEVICE_PATH, O_RDONLY);
    if (dev->base.sensor_fd < 0) {
        LOG_WARN("can't open %s, use simulation file", AHT30_DEVICE_PATH);
        dev->base.sensor_fd = open("./mock_aht30.txt", O_CREAT | O_RDWR, 0644);
    }

    if (dev->base.sensor_fd < 0) {
        LOG_ERROR("error open AHT30");
        dev->base.state = 0;
        return -1;
    }

    dev->base.state = 1;
    dev->temperature = 0.0f;
    dev->humidity = 0.0f;
    LOG_INFO("init sensor AHT30 success, fd=%d", dev->base.sensor_fd);
    return 0;
}

int get_aht30_sensor_data(void* arg) 
{
    if (arg == NULL) {
        LOG_ERROR("null param");
        return -1;
    }
    
    struct aht30_sensor* dev = (struct aht30_sensor*)arg;
    if (dev->base.sensor_fd < 0) {
        LOG_ERROR("bad fd");
        return -1;
    }

    lseek(dev->base.sensor_fd, 0, SEEK_SET);
    char buf[64];
    ssize_t n = read(dev->base.sensor_fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        LOG_ERROR("read AHT30 failed");
        return -1;
    }
    buf[n] = '\0';

    sscanf(buf, "TEMP=%f,HUMI=%f", &dev->temperature, &dev->humidity);
    dev->base.last_control_ms = now_ms();
    return 0;
}

void destroy_aht30_sensor(void* arg) {
    struct aht30_sensor* dev = (struct aht30_sensor*)arg;
    if (dev && dev->base.sensor_fd >= 0) {
        close(dev->base.sensor_fd);
        dev->base.sensor_fd = -1;
        dev->base.state = 0;
    }
}


int init_bh1750_sensor(void* arg) 
{
    if (arg == NULL) {
        LOG_ERROR("nul param");
        return -1;
    }
    struct bh1750_sensor* dev = (struct bh1750_sensor*)arg;

    dev->base.sensor_fd = open(BH1750_DEVICE_PATH, O_RDONLY);
    if (dev->base.sensor_fd < 0) {
        dev->base.sensor_fd = open("./mock_bh1750.txt", O_CREAT | O_RDWR, 0644);
    }
    if (dev->base.sensor_fd < 0) {
        LOG_ERROR("bad fd");
        return -1;
    }

    dev->base.state = 1;
    dev->lux = 0.0f;
    return 0;
}

int get_bh1750_sensor_data(void* arg) 
{
    if (arg == NULL) {
        LOG_ERROR("nul param");
        return -1;
    }

    struct bh1750_sensor* dev = (struct bh1750_sensor*)arg;
    if (dev->base.sensor_fd < 0) {
        LOG_ERROR("bad fd");
        return -1;
    }

    lseek(dev->base.sensor_fd, 0, SEEK_SET);
    char buf[32];
    ssize_t n = read(dev->base.sensor_fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        LOG_ERROR("read bh1750 failed");
        return -1;
    }
    buf[n] = '\0';

    sscanf(buf, "LUX=%f", &dev->lux);
    return 0;
}

void destroy_bh1750_sensor(void* arg) {
    struct bh1750_sensor* dev = (struct bh1750_sensor*)arg;
    if (dev && dev->base.sensor_fd >= 0) {
        close(dev->base.sensor_fd);
        dev->base.sensor_fd = -1;
        dev->base.state = 0;
    }
}


int init_ac_actuator(void* arg) {

    struct air_condition* ac = (struct air_condition*)arg;
    if (!ac) return -1;

    ac->base.actuator_fd = open(AC_DEVICE_PATH, O_WRONLY);
    if (ac->base.actuator_fd < 0) {
        ac->base.actuator_fd = open("./mock_ac.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    }
    if (ac->base.actuator_fd < 0) return -1;

    ac->base.state = 0; // Tắt mặc định
    ac->base.current_setpoint = 26.0; // 26 độ C
    ac->swing = 1;
    ac->fan_level = 2;
    return 0;
}

int set_ac_actuator(void* arg) {
    struct air_condition* ac = (struct air_condition*)arg;
    if (!ac || ac->base.actuator_fd < 0) return -1;

    char cmd[128];
    int len = snprintf(cmd, sizeof(cmd), "STATE=%d,TEMP=%.1f,SWING=%d,FAN=%d\n", 
                       ac->base.state, ac->base.current_setpoint, ac->swing, ac->fan_level);
    
    ssize_t written = write(ac->base.actuator_fd, cmd, len);
    if (written < 0) return -1;
    fsync(ac->base.actuator_fd);
    return 0;
}

void destroy_ac_actuator(void* arg) {
    struct air_condition* ac = (struct air_condition*)arg;
    if (ac && ac->base.actuator_fd >= 0) {
        close(ac->base.actuator_fd);
        ac->base.actuator_fd = -1;
        ac->base.state = 0;
    }
}


int init_led_actuator(void* arg) {
    struct led_pwm* led = (struct led_pwm*)arg;
    if (!led) return -1;

    // Ghi trực tiếp vào file PWM Duty Cycle của Linux Kernel Sysfs
    led->base.actuator_fd = open(LED_PWM_PATH, O_WRONLY);
    if (led->base.actuator_fd < 0) {
        led->base.actuator_fd = open("./mock_led_pwm.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    }
    if (led->base.actuator_fd < 0) return -1;

    led->base.state = 1;
    led->brightness_percent = 50; // 50% độ sáng mặc định
    return 0;
}

int set_led_actuator(void* arg) {
    struct led_pwm* led = (struct led_pwm*)arg;
    if (!led || led->base.actuator_fd < 0) return -1;

    // Quy đổi % độ sáng ra chu kỳ xung PWM (giả sử max là 1000000 ns)
    int duty_ns = (led->brightness_percent * 1000000) / 100;
    char val_str[32];
    int len = snprintf(val_str, sizeof(val_str), "%d\n", duty_ns);

    ssize_t written = write(led->base.actuator_fd, val_str, len);
    if (written < 0) return -1;
    fsync(led->base.actuator_fd);
    return 0;
}

void destroy_led_actuator(void* arg) {
    struct led_pwm* led = (struct led_pwm*)arg;
    if (led && led->base.actuator_fd >= 0) {
        close(led->base.actuator_fd);
        led->base.actuator_fd = -1;
    }
}


int init_fan_actuator(void* arg) {
    struct fan* f = (struct fan*)arg;
    if (!f) return -1;

    f->base.actuator_fd = open(FAN_DEVICE_PATH, O_WRONLY);
    if (f->base.actuator_fd < 0) {
        f->base.actuator_fd = open("./mock_fan.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    }
    if (f->base.actuator_fd < 0) return -1;

    f->base.state = 0;
    f->speed_rpm = 0;
    return 0;
}

int set_fan_actuator(void* arg) {
    struct fan* f = (struct fan*)arg;
    if (!f || f->base.actuator_fd < 0) return -1;

    char cmd[64];
    int len = snprintf(cmd, sizeof(cmd), "STATE=%d,RPM=%d\n", f->base.state, f->speed_rpm);
    
    ssize_t written = write(f->base.actuator_fd, cmd, len);
    if (written < 0) return -1;
    fsync(f->base.actuator_fd);
    return 0;
}

void destroy_fan_actuator(void* arg) {
    struct fan* f = (struct fan*)arg;
    if (f && f->base.actuator_fd >= 0) {
        close(f->base.actuator_fd);
        f->base.actuator_fd = -1;
    }
}