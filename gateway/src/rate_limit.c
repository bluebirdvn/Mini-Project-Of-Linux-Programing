#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "rate_limit.h"
#include "logger.h"
#include "common.h"


int rate_limit_init(double capacity, double request_per_second, struct rate_limit* rate)
{
    if (rate == NULL) {
        LOG_ERROR("null params");
        return -1;
    }

    rate->capacity = capacity;
    rate->number_request_available = capacity;
    rate->request_per_second = request_per_second;
    rate->last_request_ms = now_ms();

    return 0;
}


bool request_allow(struct rate_limit* rate)
{
    if (rate == NULL) {
        LOG_ERROR("null params");
        return false;
    }

    uint64_t now = now_ms();
    double time_eslaped = (double)(now - rate->last_request_ms) / 1000.0;

    double number_request_could_process = time_eslaped * rate->request_per_second;

    rate->number_request_available += number_request_could_process;

    if(rate->number_request_available > rate->capacity) {
        rate->number_request_available = rate->capacity;
    }

    rate->last_request_ms = now_ms();

    if (rate->number_request_available < 1.0) {
        return false;
    }
    rate->number_request_available -= 1.0;
    
    return true;
}