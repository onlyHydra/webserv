#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <map>
#include <string>
#include <cstddef>

struct RateBucketCfg {
    double      rps;          // tokens per second
    double      burst;        // bucket capacity
    std::size_t max_entries;  // LRU-ish cap
    RateBucketCfg() : rps(0.0), burst(0.0), max_entries(1024) {}
    bool enabled() const { return rps > 0.0 && burst > 0.0; }
};

struct RateDecision {
    bool allowed;
    int  retry_after_seconds;   // if !allowed
    int  remaining_tokens_int;  // floor(tokens after this request), clamped >= 0
    int  limit_rpm;             // for X-RateLimit-Limit (requests per minute)

    RateDecision(bool a=true, int ra=0, int rem=0, int lim=0)
        : allowed(a), retry_after_seconds(ra),
          remaining_tokens_int(rem), limit_rpm(lim) {}
};

class RateLimiter {
public:
    RateLimiter();
    explicit RateLimiter(const RateBucketCfg& cfg);

    void setConfig(const RateBucketCfg& cfg);
    const RateBucketCfg& getConfig() const;

    // now_ms: milliseconds since epoch (coarse ok)
    RateDecision allow(const std::string& ip, unsigned long long now_ms);

    void maybeCleanup(unsigned long long now_ms);

private:
    struct Node {
        double               tokens;
        unsigned long long   last_ms;
        unsigned long long   seen_ms;
        Node() : tokens(0.0), last_ms(0), seen_ms(0) {}
        Node(double t, unsigned long long ms) : tokens(t), last_ms(ms), seen_ms(ms) {}
    };

    RateBucketCfg                cfg_;
    std::map<std::string, Node>  table_;
    unsigned long long           last_cleanup_ms_;

    void evictIfNeeded();
};

#endif

