/* --- HttpRequest.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef HTTPREQUEST_H
 #define HTTPREQUEST_H

# include <string>
# include <vector>
# include "Headers.h"
# include "CookieJar.h"

using namespace std;

class HttpRequest
{
	public:
		HttpRequest();
		~HttpRequest();
		/**
		 * @returns wether session is still alive
		 */
		bool	keepAlive(void) const;
		/**
		 * For Later
		 * PLACEHOLDER
		 */
		bool	headerAsSize(string k, size_t &v) const;
		/**
		 * For Later
		 * PLACEHOLDER
		 */
		string	extension() const;
		/**
		 * Incremental parser
		 * Parse information from Client to build Header and Request
		 * @param data Data stream to parse from
		 * @param n Amount of bytes data is big
		 * @returns false if parsing failed
		 * @returns true if it worked
		 */
		bool	parse(const char* data, size_t n);
		/**
		 * @returns method, meaning the full Start line of the Request
		 */
		string	getMethod(void);
		/**
		 * For Later
		 * PLACEHOLDER
		 */
		string	getUri(void);
		/**
		 * @returns path, part of the method
		 */
		string	getPath(void);
		/**
		 * For Later
		 * PLACEHOLDER
		 */
		string	getQuery(void);
		/**
		 * @returns Sesion ID
		 */
		string	getSessId(void);
		/**
		 * @returns http version, part of the method
		 */
		string	getHttpVer(void);
		/**
		 * @returns Headers from this response
		 */
		string	getHeaders(void);
		/**
		 * @returns Cookies from this response
		 */
		string	getCookies(void);
		/**
		 * @returns Body of the response as vector
		 */
		string	getBody(void);
	private:
		string method;
		string uir;
		string path;
		string query;
		string session_id;
		string http_version;
		Headers headers;
		CookieJar cookies;
		vector<char> body;
};

#endif // HTTPREQUEST_H

