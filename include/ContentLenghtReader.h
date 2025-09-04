#if !defined(CONTENTLENGHTREADER_H)
#define CONTENTLENGHTREADER_H

#include "BodyReader.h"
#include "HttpRequest.h"
#include <cstddef>
class ContentLenghtReader : public IBodyReader
{
	public:
	ContentLenghtReader(std::size_t total_len, HttpRequest &req)
	: need(total_len), got(0), done(total_len == 0), req(req) {}

	std::size_t consume(const char *data, std::size_t len)
	{
		if (done || need == got) { 
			done = true; 
			return 0; 
		}
		const std::size_t remaining = need - got;
		const std::size_t take = (len < remaining) ? len : remaining;

		req.parse(data, take);

		got += take;
		if (got == need) done = true;
		return take;
	}

	bool complete() const { 
		return done; 
	}
	std::size_t bytes_received() const { 
		return got; 
	}

private:
	std::size_t need;  // expected total bytes
	std::size_t got;   // bytes appended so far
	bool        done;  // completion flag
	HttpRequest &req;  // in-memory sink owner
};

#endif //  CONTENTLENGHTREADER_H
