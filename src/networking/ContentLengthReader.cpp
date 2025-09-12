#include "ContentLenghtReader.h"

ContentLenghtReader::ContentLenghtReader(std::size_t total_len, HttpRequest &req)
: need(total_len), got(0), done(total_len == 0), req(req)
{
    // nothing else to do
}

std::size_t ContentLenghtReader::consume(const char *data, std::size_t len)
{
    if (done || got >= need) { done = true; return 0; }

    const std::size_t remaining = need - got;
    const std::size_t take = (len < remaining) ? len : remaining;

    if (take) {
        req.appendBody(data, take);  // write straight into HttpRequest::body
        got += take;
    }
    if (got >= need) done = true;
    return take;
}
