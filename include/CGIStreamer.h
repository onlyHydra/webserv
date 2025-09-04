#ifndef CGISTREAMER_H
#define CGISTREAMER_H

#include <string>
#include <vector>
#include <cstddef>

#include "CgiProcess.h"    // owns fork/exec + pipe fds
#include "HttpRequest.h"
#include "HttpResponse.h"

// Forward-declared; we only pass it by reference.
class ChainBuf;

// Forward-declare CGI spec if not in CgiProcess.h
struct CgiSpec;

/**
 * CGIStreamer
 * ----------
 * Runs a CGI child process asynchronously and buffers its output as
 * fully-formed HTTP bytes. ClientConnection pulls those bytes via
 * takeOutBytes() and writes them to the socket.
 */
class CGIStreamer
{
public:
    // Construct bound to the current request/response
    CGIStreamer(HttpRequest &req, HttpResponse &res);

    // Start CGI (non-blocking). Returns false if spawn/pipe setup fails.
    bool beginCgi(const CgiSpec &spec,
                  const std::string &script_path,
                  const std::vector<std::string> &envv);

    // EventLoop dispatchers should call these when the corresponding fd is ready.
    void onCgiReadable(int fd);  // drain child's stdout → internal buffer
    void onCgiWritable(int fd);  // feed request body → child's stdin

    // Child fds (−1 when closed)
    int  cgiStdoutFD() const { return cgi_out_fd; }
    int  cgiStdinFD()  const { return cgi_in_fd;  }

    // Is a CGI exchange in progress?
    bool isActive()     const { return cgi_active; }

    // Is there buffered HTTP data ready for the socket?
    bool hasOutBytes()  const { return outOffset < outBuffer.size(); }

    // Move up to max_bytes from internal buffer into the socket ChainBuf.
    // Returns number of bytes appended.
    std::size_t takeOutBytes(ChainBuf &out, std::size_t max_bytes);

    // Proactively close child's stdin (e.g., for GET/HEAD without a body)
    void closeStdin();

private:
    // Parse CGI response headers from cgi_buf; set status & content_len.
    // Returns true when headers complete (and trims them from cgi_buf).
    bool parseCgiHeaders(std::string &buf, int &status, long &content_len);

    // Reset internal write deadline after progress
    void resetDeadlineForWrite();

    // ---- State ----
    HttpRequest   &req;
    HttpResponse  &res;
    CgiProcess     proc;

    bool                cgi_active;        // true after beginCgi() until teardown
    int                 cgi_in_fd;         // write end → child's stdin
    int                 cgi_out_fd;        // read end  ← child's stdout/stderr
    std::size_t         cgi_body_off;      // how much of request body we've fed
    std::string         cgi_buf;           // scratch for header parsing / overflow
    bool                cgi_headers_done;  // set once we built HTTP head
    int                 cgi_status;        // from "Status:"; default 200
    long                cgi_content_len;   // from "Content-Length:"; -1 if unknown
    unsigned long long  cgi_deadline;      // write/overall deadline ticks

    // Fully-formed HTTP bytes to send to the client socket
    std::vector<char>   outBuffer;
    std::size_t         outOffset;

    enum State { READ, WRITE };
    State               state;

    // Timeouts
    static const unsigned long long WRITE_TIMEOUT_MS = 5000; // 5s
};

#endif // CGISTREAMER_H


