/* --- HttpResponse.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */
#include "HttpResponse.h"
#include <sstream>
#include <ctime>

class HttpRequest;

// --- tiny internal helpers --------------------------------------------------

namespace
{
	const char *reasonFor(int code)
	{
		switch (code)
		{
		case 100:
			return "Continue";
		case 101:
			return "Switching Protocols";
		case 200:
			return "OK";
		case 201:
			return "Created";
		case 202:
			return "Accepted";
		case 204:
			return "No Content";
		case 301:
			return "Moved Permanently";
		case 302:
			return "Found";
		case 304:
			return "Not Modified";
		case 307:
			return "Temporary Redirect";
		case 308:
			return "Permanent Redirect";
		case 400:
			return "Bad Request";
		case 401:
			return "Unauthorized";
		case 403:
			return "Forbidden";
		case 404:
			return "Not Found";
		case 405:
			return "Method Not Allowed";
		case 412:
			return "Precondition Failed";
		case 413:
			return "Payload Too Large";
		case 500:
			return "Internal Server Error";
		case 502:
			return "Bad Gateway";
		case 503:
			return "Service Unavailable";
		default:
			return "";
		}
	}

	std::string rfc1123Now()
	{
		char buf[64];
		std::time_t t = std::time(0);
		std::tm gmt;
#if defined(_WIN32)
		gmtime_s(&gmt, &t);
#else
		gmt = *std::gmtime(&t);
#endif
		std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
		return std::string(buf);
	}

	std::string toStringUL(unsigned long v)
	{
		std::ostringstream oss;
		oss << v;
		return oss.str();
	}
}

// --- HttpResponse implementation -------------------------------------------

HttpResponse::HttpResponse(void)
	: http_version("HTTP/1.1"), session_id(""), bodyLength(0), body(), headers(), cookies(NULL), status(200), reason("OK")
{
}

HttpResponse::HttpResponse(HttpRequest &req)
	: http_version("HTTP/1.1"), session_id(""), bodyLength(0), body(), headers(), cookies(&req.cookies), status(200), reason("OK")
{
}

HttpResponse::~HttpResponse() {}

void HttpResponse::setStatus(int code)
{
	status = code;
	const char *r = reasonFor(code);
	reason = (r && *r) ? std::string(r) : std::string();
}

void HttpResponse::setStatus(int code, const std::string &r)
{
	status = code;
	reason = r;
}

int HttpResponse::getStatusCode() const
{
	return status;
}

const std::string &HttpResponse::getReason() const
{
	return reason;
}

void HttpResponse::setBody(const std::string &s)
{
	body.assign(s.begin(), s.end());
	bodyLength = body.size();
	headers.set("Content-Length", toStringUL((unsigned long)bodyLength));
}

void HttpResponse::clearBody()
{
	body.clear();
	bodyLength = 0;
	headers.set("Content-Length", "0");
}

void HttpResponse::ensureDefaultHeaders()
{
	const size_t effective_len = bodyLength ? bodyLength : body.size();
	headers.set("Content-Length", toStringUL((unsigned long)effective_len));

	if (!headers.keyExists("Date"))
		headers.set("Date", rfc1123Now());

	if (!headers.keyExists("Server"))
		headers.set("Server", "webserv");

	if (!headers.keyExists("Connection"))
		headers.set("Connection", "close");
}

// HttpResponse.cpp
std::ostream& operator<<(std::ostream& out, const HttpResponse& r)
{
	// Starting Line
    out << r.http_version << " " << r.status << " " << r.reason << "\r\n";
	
	// Cookiejar
	if (r.cookies != NULL)
		out << r.cookies->serialize();

	// Headers
    out << r.headers.serialize();

	// Body
    if (!r.body.empty())
        out.write(&r.body[0], std::streamsize(r.body.size())); // ✅ write exact bytes

    return out;
}

