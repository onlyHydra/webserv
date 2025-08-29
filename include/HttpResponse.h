/* --- HttpResponse.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#include <string>
#include <vector>
#include <ostream>
#include "Headers.h"
#include "CookieJar.h"

class HttpResponse {
public:
    std::string  http_version;
    std::string  session_id;
    size_t       bodyLength;
    Headers      headers;
    CookieJar    cookies;
    std::vector<char> body;

    // >>> Keep legacy public names <<<
    int          status;       // e.g. 200
    std::string  reason;       // e.g. "OK"

    HttpResponse();
    ~HttpResponse();

    // Optional helpers (used by newer code paths)
    void setStatus(int code, const std::string& r);
    int  getStatusCode() const;
    const std::string& getReason() const;

    // Handy helper some code uses
    void clearBody();
};

// Debug/trace helper (safe)
std::ostream& operator<<(std::ostream& out, const HttpResponse& r);

#endif // HTTPRESPONSE_H


