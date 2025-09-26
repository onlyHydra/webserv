#if !defined(HEADERPROCESSOR_H)
#define HEADERPROCESSOR_H

#include <cstddef>
#include <string>
// #include "FlatHeaders.h"
#include "Headers.h"
#include "HttpRequest.h"
#include "Util.h"


struct HeaderCheck
{
	bool ok;
	int error_status;
	std::string message;
	std::size_t content_length;
	bool chunked;
	bool expect_continue;
	HeaderCheck() : ok(true),error_status(0),message(), content_length(0), chunked(false), expect_continue(false) {}
};

class HeaderProcessor
{
public:
	static HeaderCheck analyze(const HttpRequest &req, const Headers &hdrs, std::size_t max_body_bytes_global);
};

#endif //  HEADERPROCESSOR_H
