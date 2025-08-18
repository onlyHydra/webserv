/* --- Location.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef LOCATION_H
#define LOCATION_H

#include <string>
#include <vector>
#include <map>

// --- small helper structs used inside Location ---

struct CgiSpec {
    std::string bin;
    int         timeout_ms;

    CgiSpec();
};

struct RateLimitConfig {
    int  requests_per_minute;
    int  burst;
    bool enabled;

    RateLimitConfig();
};

struct PutPatchConfig {
    std::string root_directory;
    int         max_body_bytes;
    bool        allow_put;
    bool        allow_patch;
    bool        generate_etag;

    PutPatchConfig();
};

// --- Location itself ---

struct Location {
    std::string                  path_prefix;
    bool                         regex;
    std::string                  root;
    bool                         autoindex;
    std::vector<std::string>     index_files;
    std::vector<std::string>     allowed_methods;
    std::string                  upload_dir;
    bool                         is_proxy;
    std::string                  proxy_name;
    std::map<std::string,CgiSpec> cgi_by_ext;

    RateLimitConfig              rate_limit;
    PutPatchConfig               write_conf;

    Location();
};

#endif // LOCATION_H


