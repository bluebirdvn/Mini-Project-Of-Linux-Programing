#ifndef _RATE_LIMIT_H
#define _RATE_LIMIT_H

#include <stdint.h>
#include <stdbool.h>

#define CAPACITY_OF_BUCKET 20
#define REQUEST_PER_SECOND 5

struct rate_limit {
    double capacity;
    double number_request_available;
    double request_per_second;
    uint64_t last_request_ms;
};

/**
 * @brief init limit processing rate
 * 
 * @param capacity number request system can process current
 * @param request_per_second limit of request persecond
 * @param rate pointer to struct rate_limit
 * @return int 0 if success, 0 if failed
 */
int rate_limit_init(double capacity, double request_per_second, struct rate_limit* rate);


/**
 * @brief check if system is available for request
 * 
 * @param rate pointer to struct rate_limit
 * @return true if system can process
 * @return false if reach limit cap
 */
bool request_allow(struct rate_limit* rate);

#endif