#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <map>
#include <string>

struct RateBucketCfg {
    // requests per second and burst size (tokens)
    double rps;          // e.g. 5.0
    double burst;        // e.g. 10.0
    // store sizing
    std::size_t max_entries; // LRU-ish cap
    RateBucketCfg() : rps(0.0), burst(0.0), max_entries(1024) {}
    bool enabled() const { return rps > 0.0 && burst > 0.0; }
};

// Result of allow()
struct RateDecision {
    bool allowed;
    int  retry_after_seconds; // if !allowed, >= 1
    RateDecision(bool a=true, int ra=0) : allowed(a), retry_after_seconds(ra) {}
};

class RateLimiter {
public:
    RateLimiter();
    explicit RateLimiter(const RateBucketCfg& cfg);

    void setConfig(const RateBucketCfg& cfg);
    const RateBucketCfg& getConfig() const;

    // now_ms: monotonic-ish ms
    // Returns decision. If !allowed, also returns Retry-After seconds.
    RateDecision allow(const std::string& ip, unsigned long long now_ms);

    // Periodic housekeeping to keep map small.
    void maybeCleanup(unsigned long long now_ms);

private:
    struct Node {
        double tokens;                 // current tokens
        unsigned long long last_ms;    // last refill timestamp
        unsigned long long seen_ms;    // for LRU-ish cleanup
        Node() : tokens(0.0), last_ms(0), seen_ms(0) {}
        Node(double t, unsigned long long ms)
        : tokens(t), last_ms(ms), seen_ms(ms) {}
    };

    RateBucketCfg cfg_;
    std::map<std::string, Node> table_;
    unsigned long long last_cleanup_ms_;

    void evictIfNeeded();
};

#endif // RATE_LIMITER_H
