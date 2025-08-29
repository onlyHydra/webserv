/* --- HttpResponse.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */
#include "HttpResponse.h"
#include <sstream>

HttpResponse::HttpResponse()
: http_version("HTTP/1.1")
, session_id("")
, bodyLength(0)
, headers()
, cookies()
, body()
, status(200)
, reason("OK")
{}

HttpResponse::~HttpResponse() {}

void HttpResponse::setStatus(int code, const std::string &r) {
    status = code;
    reason = r;
}

int HttpResponse::getStatusCode() const { return status; }
const std::string& HttpResponse::getReason() const { return reason; }

void HttpResponse::clearBody() {
    body.clear();
    bodyLength = 0;
    headers.set(std::string("Content-Length"), std::string("0"));
}

std::ostream &operator<<(std::ostream &out, const HttpResponse &r) {
    out << r.http_version << " " << r.status << " " << r.reason << "\r\n";
    out << r.headers.serialize();
    out << "(bodyLength=" << r.bodyLength << ")\n";
    return out;
}



