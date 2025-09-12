#ifndef CONTENTLENGHTREADER_H
#define CONTENTLENGHTREADER_H

#include "BodyReader.h"
#include "HttpRequest.h"
#include <cstddef>

class ContentLenghtReader : public IBodyReader {
public:
    ContentLenghtReader(std::size_t total_len, HttpRequest &req); // declaration only

    // feed bytes into the request body
    std::size_t consume(const char *data, std::size_t len);

    bool complete() const { return done; }
    std::size_t bytes_received() const { return got; }

private:
    std::size_t  need;   // expected total bytes (Content-Length)
    std::size_t  got;    // bytes appended so far
    bool         done;   // completion flag
    HttpRequest &req;    // destination (uses appendBody)
};

#endif // CONTENTLENGHTREADER_H
