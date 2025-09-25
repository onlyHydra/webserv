/* --- RateLimitConfig.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef RATE_LIMIT_CONFIG_H
#define RATE_LIMIT_CONFIG_H
#include <cstddef>

struct RateLimitConfig {
    double rps;             // tokens per second
    double burst;           // bucket size
    std::size_t max_entries;
    RateLimitConfig() : rps(0.0), burst(0.0), max_entries(1024) {}
    bool enabled() const { return rps > 0.0 && burst > 0.0; }
};
#endif

