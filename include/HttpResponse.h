/* --- HttpResponse.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

# include <string>
# include <vector>
# include <errno.h>
# include "Headers.h"
# include "CookieJar.h"

using namespace std;

/**
 * @brief Class that represents a full Http Response
 * ---------------------------------------------------
 */
class HttpResponse
{
	public:
		string http_version;
		string session_id;
		size_t	bodyLength;
		Headers headers;
		CookieJar cookies;
		vector<char> body;

		HttpResponse();
		~HttpResponse();
};

/**
 * DANGER
 * PLACEHOLDER
 * USES THE ostream.write external function
 * which isnt allowed
 * DELETE LATER
 * ...or keep it for debugging
 */
std::ostream &operator<<(std::ostream &out, const HttpResponse &target);

#endif // HTTPRESPONSE_H
