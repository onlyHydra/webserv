/* --- CgiRegistry.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

/* --- CgiRegistry.cpp --- */
#include "CgiRegistry.h"

CgiRegistry::CgiRegistry() : local_(0), global_(0) {}
CgiRegistry::~CgiRegistry() {}

void CgiRegistry::setSources(const std::map<std::string, CgiSpec>* local,
                             const std::map<std::string, CgiSpec>* global) {
    local_  = local;
    global_ = global;
}

static std::string normalizeExt(const std::string& e) {
    if (e.empty())
        return e;
    return (e[0] == '.') ? e : std::string(".") + e;
}

const CgiSpec* CgiRegistry::findByExtension(const std::string& raw) const {
    const std::string key = normalizeExt(raw);
    if (local_) {
        std::map<std::string, CgiSpec>::const_iterator it = local_->find(key);
        if (it != local_->end())
            return &it->second;
    }
    if (global_) {
        std::map<std::string, CgiSpec>::const_iterator it = global_->find(key);
        if (it != global_->end())
            return &it->second;
    }
    return 0;
}
