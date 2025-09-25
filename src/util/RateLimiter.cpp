/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RateLimiter.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vvelikov <vvelikov@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/09/25 12:45:58 by vvelikov          #+#    #+#             */
/*   Updated: 2025/09/25 14:32:36 by vvelikov         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "RateLimiter.h"
#include <cmath>

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

RateLimiter::RateLimiter()
: cfg_(), table_(), last_cleanup_ms_(0) {}

RateLimiter::RateLimiter(const RateBucketCfg& cfg)
: cfg_(cfg), table_(), last_cleanup_ms_(0) {}

void RateLimiter::setConfig(const RateBucketCfg& cfg) { cfg_ = cfg; }
const RateBucketCfg& RateLimiter::getConfig() const { return cfg_; }

RateDecision RateLimiter::allow(const std::string& ip, unsigned long long now_ms) {
    if (!cfg_.enabled()) {
        return RateDecision(true, 0, 0, 0);
    }

    std::map<std::string, Node>::iterator it = table_.find(ip);
    if (it == table_.end()) {
        Node n(cfg_.burst, now_ms); // start full to allow initial burst
        it = table_.insert(std::make_pair(ip, n)).first;
        evictIfNeeded();
    }

    Node& node = it->second;

    // Refill
    if (now_ms > node.last_ms) {
        unsigned long long dm = now_ms - node.last_ms;
        double add = (cfg_.rps * (double)dm) / 1000.0;
        node.tokens = clampd(node.tokens + add, 0.0, cfg_.burst);
        node.last_ms = now_ms;
    }
    node.seen_ms = now_ms;

    // Consume or compute retry
    if (node.tokens >= 1.0) {
        node.tokens -= 1.0;
        int rem = (int)std::floor(node.tokens);
        if (rem < 0) rem = 0;
        int lim = (int)std::floor(cfg_.rps * 60.0 + 0.5); // rpm
        return RateDecision(true, 0, rem, lim);
    } else {
        double needed = 1.0 - node.tokens;
        double secs   = (cfg_.rps > 0.0) ? (needed / cfg_.rps) : 1.0;
        int retry     = (int)std::ceil(secs);
        if (retry < 1) retry = 1;
        int rem = 0;
        int lim = (int)std::floor(cfg_.rps * 60.0 + 0.5);
        return RateDecision(false, retry, rem, lim);
    }
}

void RateLimiter::maybeCleanup(unsigned long long now_ms) {
    if (table_.empty()) return;
    if (last_cleanup_ms_ != 0) {
        unsigned long long delta = (now_ms > last_cleanup_ms_) ? (now_ms - last_cleanup_ms_) : 0;
        if (delta < 30000ULL) return;
    }
    const unsigned long long STALE_MS = 5ULL * 60ULL * 1000ULL;
    for (std::map<std::string, Node>::iterator it = table_.begin(); it != table_.end(); ) {
        if ((now_ms - it->second.seen_ms) > STALE_MS) {
            std::map<std::string, Node>::iterator del = it++;
            table_.erase(del);
        } else {
            ++it;
        }
    }
    while (table_.size() > cfg_.max_entries) {
        std::map<std::string, Node>::iterator victim = table_.begin();
        unsigned long long oldest = victim->second.seen_ms;
        for (std::map<std::string, Node>::iterator jt = table_.begin(); jt != table_.end(); ++jt) {
            if (jt->second.seen_ms < oldest) {
                oldest = jt->second.seen_ms;
                victim = jt;
            }
        }
        table_.erase(victim);
    }
    last_cleanup_ms_ = now_ms;
}

void RateLimiter::evictIfNeeded() {
    if (cfg_.max_entries == 0) return;
    if (table_.size() <= cfg_.max_entries) return;
    std::map<std::string, Node>::iterator victim = table_.begin();
    unsigned long long oldest = victim->second.seen_ms;
    for (std::map<std::string, Node>::iterator jt = table_.begin(); jt != table_.end(); ++jt) {
        if (jt->second.seen_ms < oldest) {
            oldest = jt->second.seen_ms;
            victim = jt;
        }
    }
    table_.erase(victim);
}
