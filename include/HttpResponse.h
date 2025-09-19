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
#include "HttpRequest.h"
#include "CookieJar.h"

class HttpResponse {
public:
		// Wire metadata
		std::string       http_version;   // e.g. "HTTP/1.1"
		std::string       session_id;

		// Body bookkeeping
		size_t            bodyLength;     // if 0, serializer should use body.size()
		std::vector<char> body;

		// Headers & cookies
		Headers           headers;
		CookieJar         *cookies;

		// Legacy names for compatibility
		int               status;         // e.g. 200
		std::string       reason;         // e.g. "OK"

		/**
		 * @warning Legacy support, you should realistically be giving 
		 * this a Request, to steal its CookieJar from
		 */
		HttpResponse();
		/**
		 * Use this Constructor for Responses that may need Cookies
		 */
		HttpResponse(HttpRequest &req);
		~HttpResponse();

		// Helpers
		void setStatus(int code);                       // infers reason
		void setStatus(int code, const std::string& r); // explicit reason
		int  getStatusCode() const;
		const std::string& getReason() const;

		void setBody(const std::string& s);
		void clearBody();

		Headers& headersRef() { return headers; }
		const Headers& headersRef() const { return headers; }

		// Ensures Date, Server, Content-Length, Connection defaults.
		void ensureDefaultHeaders();
};

	// Debug/trace helper (does NOT print the actual body bytes)
	std::ostream& operator<<(std::ostream& out, const HttpResponse& r);

#endif // HTTPRESPONSE_H





