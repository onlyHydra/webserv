#include "HttpRequest.h"
#include "CGIStreamer.h"
#include "CgiProcess.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <stdlib.h>
#include "ChainBuf.h" // for ChainBuf::push_copy
#include <algorithm>
#include <fcntl.h>           // fcntl, O_NONBLOCK, FD_CLOEXEC
#include <unistd.h>          // close (if not already included)// optional
#include "ResponseFactory.h" // if you want reason phrases; optional
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <cstdio>
#include <sstream>
#include <ctime>
#include <sys/time.h>   // <-- add this

/***---------------------------CGI PROCCESSING AND EXECUTION--------------------------- ***/

// CGIStreamer.cpp
#include <unistd.h> // ::close

/* static std::string http_date_now_gmt()
{
    char buf[64];
    time_t t = time(0);
    struct tm g = *gmtime(&t);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
    return std::string(buf);
}

static const char *reason_for(int code)
{
    switch (code)
    {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 302:
        return "Found";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 500:
        return "Internal Server Error";
    default:
        return "OK";
    }
} */

void CGIStreamer::closeStdin()
{
    if (cgi_in_fd >= 0) {
        ::close(cgi_in_fd);
        cgi_in_fd = -1;
    }
    // Important: Event loop must stop polling this fd once it sees -1.
}


std::size_t CGIStreamer::takeOutBytes(ChainBuf &out, std::size_t max_bytes)
{
    const std::size_t avail = outBuffer.size() - outOffset;
    if (avail == 0) return 0;

    std::size_t n = (avail < max_bytes) ? avail : max_bytes;
    out.push_copy(&outBuffer[outOffset], n);
    outOffset += n;

    if (outOffset >= outBuffer.size()) {
        outBuffer.clear();
        outOffset = 0;
    }
    return n;
}




CGIStreamer::CGIStreamer(HttpRequest &req, HttpResponse &res)
    : req(req),
      res(res),
      proc(),
      cgi_active(false),
      cgi_in_fd(-1),
      cgi_out_fd(-1),
      cgi_body_off(0),
      cgi_buf(),
      body_fd_(-1),
      body_off_(0),
      body_path_(),
      cgi_headers_done(false),
      cgi_status(200),
      cgi_content_len(-1),
      cgi_deadline(0),
      chunked_mode(false),
      sent_final_chunk(false),
      outBuffer(),
      outOffset(0),
      state(READ)
{
    outBuffer.clear();

    if (req.isBodyOnDisk())
        body_path_ = req.getBodyFilePath();
    else
        body_path_.clear();
}



// Parse CGI headers from buf. On success, removes headers from buf,
// sets status (default 200), and sets content_len (or -1 if unknown).
// ---- keep this near the top of CGIStreamer.cpp ----
// returns true once headers parsed; removes the header block (incl. CRLFCRLF) from 'buf'.
// fills status (from "Status: NNN Reason") and content_len (from "Content-Length: ...") if present.
// inside CGIStreamer
bool CGIStreamer::parseCgiHeaders(std::string &buf, int &status, long &content_len)
{
    // Look for header/body split (supports CRLF CRLF or NL NL)
    std::string::size_type p = std::string::npos;

    // Try CRLF CRLF
    std::string::size_type pos = buf.find("\r\n\r\n");
    if (pos != std::string::npos)
    {
        p = pos + 4;
    }
    else
    {
        // Try LF LF
        pos = buf.find("\n\n");
        if (pos != std::string::npos)
            p = pos + 2;
    }
    if (p == std::string::npos)
        return false; // need more

    // Parse headers line-by-line
    status = 200;
    content_len = -1;
    std::string headers = buf.substr(0, p);
    std::istringstream is(headers);
    std::string line;

    while (std::getline(is, line))
    {
        // trim trailing CR
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        if (line.empty())
            break; // end of headers

        // Status: 200 OK
        if (line.size() >= 7 && (line[0] == 'S' || line[0] == 's'))
        {
            // starts with 'Status:' ?
            const char *k = "Status:";
            bool match = true;
            for (size_t i = 0; i < 7 && i < line.size(); ++i)
            {
                char a = line[i];
                char b = k[i];
                if (a >= 'A' && a <= 'Z')
                    a = char(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z')
                    b = char(b - 'A' + 'a');
                if (a != b)
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                std::istringstream ls(line.substr(7));
                int s = 0;
                ls >> s;
                if (s >= 100 && s <= 599)
                    status = s;
            }
        }

        // Content-Length:
        if (line.size() >= 15)
        {
            const char *k2 = "Content-Length:";
            bool match2 = true;
            for (size_t i = 0; i < 15 && i < line.size(); ++i)
            {
                char a = line[i];
                char b = k2[i];
                if (a >= 'A' && a <= 'Z')
                    a = char(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z')
                    b = char(b - 'A' + 'a');
                if (a != b)
                {
                    match2 = false;
                    break;
                }
            }
            if (match2)
            {
                const char *s = line.c_str() + 15;
                while (*s == ' ' || *s == '\t')
                    ++s;
                long v = 0;
                while (*s >= '0' && *s <= '9')
                {
                    v = v * 10 + (*s - '0');
                    ++s;
                }
                content_len = v;
            }
        }
    }

    // Remove header part from buffer, leave body (if any) in buf
    buf.erase(0, p);
    return true;
}

void CGIStreamer::onCgiReadable(int fd)
{
    if (!cgi_active || fd != cgi_out_fd) return;

    // 1) Read as much as available into cgi_buf (non-blocking)
    for (;;)
    {
        char tmp[8192];
        ssize_t n = ::read(cgi_out_fd, tmp, sizeof(tmp));
        if (n > 0)
        {
            cgi_buf.append(tmp, n);
        }
        else if (n == 0)
        {
            // EOF from CGI stdout
            // If headers not done, synthesize minimal headers now.
            if (!cgi_headers_done)
            {
                // No headers parsed → default to 200 + text/plain
                cgi_status = (cgi_status ? cgi_status : 200);
                if (cgi_content_len < 0) chunked_mode = true;

                std::string hdr;
                {
                    std::ostringstream oss;
                    oss << "HTTP/1.1 " << cgi_status << " OK\r\n";

                    // If you want to be safe, emit Content-Type if CGI sent none.
                    // We don't track a header list here, so put a sensible default.
                    oss << "Content-Type: text/plain\r\n";

                    if (chunked_mode)
                        oss << "Transfer-Encoding: chunked\r\n";
                    else if (cgi_content_len >= 0)
                        oss << "Content-Length: " << cgi_content_len << "\r\n";

                    oss << "Connection: keep-alive\r\n";
                    oss << "\r\n";
                    hdr = oss.str();
                }
                outBuffer.insert(outBuffer.end(), hdr.begin(), hdr.end());
                cgi_headers_done = true;
            }

            // Flush whatever body remains in cgi_buf, then send final chunk if needed.
            if (chunked_mode)
            {
                // send pending body as one last chunk (if any)
                if (!cgi_buf.empty())
                {
                    std::ostringstream head;
                    head << std::hex << cgi_buf.size() << "\r\n";
                    const std::string h = head.str();
                    outBuffer.insert(outBuffer.end(), h.begin(), h.end());
                    outBuffer.insert(outBuffer.end(), cgi_buf.begin(), cgi_buf.end());
                    outBuffer.push_back('\r'); outBuffer.push_back('\n');
                    cgi_buf.clear();
                }
                // terminating chunk
                if (!sent_final_chunk)
                {
                    static const char end_chunk[] = "0\r\n\r\n";
                    outBuffer.insert(outBuffer.end(), end_chunk, end_chunk + 5);
                    sent_final_chunk = true;
                }
            }
            else
            {
                // non-chunked: just append remaining bytes
                if (!cgi_buf.empty())
                {
                    outBuffer.insert(outBuffer.end(), cgi_buf.begin(), cgi_buf.end());
                    cgi_buf.clear();
                }
            }

            ::close(cgi_out_fd);
            cgi_out_fd = -1;
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; // nothing more for now
            // read error: close stdout and bail
            ::close(cgi_out_fd);
            cgi_out_fd = -1;
            return;
        }
    }

    // 2) If headers aren’t done yet, try to parse them now.
    if (!cgi_headers_done)
    {
        // parseCgiHeaders must remove the header block from cgi_buf when it returns true
        if (!parseCgiHeaders(cgi_buf, cgi_status, cgi_content_len))
            return; // need more bytes

        // ======= THIS IS THE BLOCK YOU ASKED ABOUT =======
        // If CGI didn't give Content-Length, stream with chunked encoding
        if (cgi_content_len < 0) {
            chunked_mode = true;
        }

        // Build HTTP status line + headers once.
        std::string hdr;
        {
            std::ostringstream oss;
            oss << "HTTP/1.1 " << (cgi_status ? cgi_status : 200) << " OK\r\n";

            // NOTE: If parseCgiHeaders extracted CGI-provided headers (e.g., Content-Type),
            // you would emit them here. Since we don't keep a list in this snippet,
            // at minimum ensure there's a Content-Type:
            oss << "Content-Type: text/plain\r\n";

            if (chunked_mode) {
                oss << "Transfer-Encoding: chunked\r\n";
            } else if (cgi_content_len >= 0) {
                oss << "Content-Length: " << cgi_content_len << "\r\n";
            }

            oss << "Connection: keep-alive\r\n";
            oss << "\r\n";
            hdr = oss.str();
        }

        // Append header bytes once
        outBuffer.insert(outBuffer.end(), hdr.begin(), hdr.end());

        cgi_headers_done = true;
    }

    // 3) Move body bytes currently in cgi_buf to outBuffer
    if (chunked_mode)
    {
        while (!cgi_buf.empty())
        {
            const std::size_t chunk_len = cgi_buf.size();
            std::ostringstream head;
            head << std::hex << chunk_len << "\r\n";
            const std::string h = head.str();

            outBuffer.insert(outBuffer.end(), h.begin(), h.end());              // chunk header
            outBuffer.insert(outBuffer.end(), cgi_buf.begin(), cgi_buf.end());  // chunk data
            outBuffer.push_back('\r'); outBuffer.push_back('\n');               // chunk trailer

            cgi_buf.clear();
        }
    }
    else
    {
        if (!cgi_buf.empty())
        {
            outBuffer.insert(outBuffer.end(), cgi_buf.begin(), cgi_buf.end());
            cgi_buf.clear();
        }
    }
}

void CGIStreamer::resetDeadlineForWrite()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    const unsigned long long now_ms =
        static_cast<unsigned long long>(tv.tv_sec) * 1000ULL +
        static_cast<unsigned long long>(tv.tv_usec) / 1000ULL;

    cgi_deadline = now_ms + WRITE_TIMEOUT_MS;
}




void CGIStreamer::onCgiWritable(int fd)
{
    if (!cgi_active || fd != cgi_in_fd) return;

    const std::size_t total = req.getBodyLength();

    std::fprintf(stderr,
        "[CGI] POLLOUT inFD=%d off=%zu total=%zu disk=%d\n",
        cgi_in_fd,
        (size_t)cgi_body_off,
        (size_t)total,
        req.isBodyOnDisk() ? 1 : 0
    );

    // Nothing to send (no body) or already finished → close stdin.
    if (total == 0 || cgi_body_off >= total) {
        closeStdin();
        return;
    }

    // ---- Disk-backed body ----
    if (req.isBodyOnDisk())
    {
        // Lazy-open the body file
        if (body_fd_ < 0) {
            body_fd_ = ::open(body_path_.c_str(), O_RDONLY
#ifdef O_CLOEXEC
                              | O_CLOEXEC
#endif
            );
            if (body_fd_ < 0) {
                std::fprintf(stderr, "[CGI] open(%s) failed: %d\n",
                             body_path_.c_str(), errno);
                closeStdin();
                return;
            }
        }

        char buf[8192];
        const std::size_t remaining = total - cgi_body_off;
        const std::size_t to_read   = remaining < sizeof(buf) ? remaining : sizeof(buf);

        // Read exactly the next slice we still need
        const off_t pos = static_cast<off_t>(cgi_body_off);
        ssize_t r = ::pread(body_fd_, buf, to_read, pos);
        if (r < 0) {
            if (errno == EINTR) return;       // try again next POLLOUT
            std::fprintf(stderr, "[CGI] pread error: %d\n", errno);
            closeStdin();
            return;
        }
        if (r == 0) {
            // Unexpected EOF before total; just close stdin to unblock child.
            std::fprintf(stderr, "[CGI] pread EOF early at %zu/%zu\n",
                         (size_t)cgi_body_off, (size_t)total);
            closeStdin();
            return;
        }

        // Write to child's stdin (NB short writes are normal)
        ssize_t w = ::write(cgi_in_fd, buf, r);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN) return; // try again later
            std::fprintf(stderr, "[CGI] write(disk) error: %d\n", errno);
            closeStdin();
            return;
        }

        if (w > 0) {
            cgi_body_off += static_cast<std::size_t>(w);
            std::fprintf(stderr, "[CGI] wrote(disk)=%zd new_off=%zu\n",
                         w, (size_t)cgi_body_off);
            resetDeadlineForWrite();
            if (cgi_body_off >= total) {
                closeStdin();
            }
        }
        return;
    }

    // ---- In-memory body ----
    const std::vector<char>& b = req.getBody();

    if (cgi_body_off >= b.size()) {
        // Nothing left in RAM buffer (should match total)
        closeStdin();
        return;
    }

    const char* p = &b[0] + cgi_body_off;
    std::size_t len = b.size() - cgi_body_off;
    if (len > 16384u) len = 16384u; // cap per write

    ssize_t w = ::write(cgi_in_fd, p, len);
    if (w < 0) {
        if (errno == EINTR || errno == EAGAIN) return; // try again later
        std::fprintf(stderr, "[CGI] write(ram) error: %d\n", errno);
        closeStdin();
        return;
    }

    if (w > 0) {
        cgi_body_off += static_cast<std::size_t>(w);
        std::fprintf(stderr, "[CGI] wrote(ram)=%zd new_off=%zu\n",
                     w, (size_t)cgi_body_off);
        resetDeadlineForWrite();
        if (cgi_body_off >= total) {
            closeStdin();
        }
    }
}



// Analyze parsed headers and set internal markers for future enforcement.
// We keep this minimal: detect Content-Length, Transfer-Encoding: chunked,
// and Expect: 100-continue. Do not change state here; callers use these
// markers to decide next actions.

bool CGIStreamer::beginCgi(const CgiSpec &spec,
                           const std::string &script_path,
                           const std::vector<std::string> &envv)
{
    cgi_active = false;
    cgi_headers_done = false;
    cgi_status = 200;
    cgi_content_len = -1;
    cgi_buf.clear();
    outBuffer.clear();
    outOffset = 0;
    cgi_body_off = 0;
    chunked_mode     = false;
    sent_final_chunk = false;

    std::fprintf(stderr,
    "[CGI] begin: inFD=%d outFD=%d len=%zu disk=%d path=%s\n",
    cgi_in_fd, cgi_out_fd,
    (size_t)req.getBodyLength(),
    req.isBodyOnDisk() ? 1 : 0,
    req.isBodyOnDisk() ? req.getBodyFilePath().c_str() : "");


    if (!proc.spawn(spec, script_path, envv))
        return false;

    cgi_in_fd = proc.inFD();
    cgi_out_fd = proc.outFD();

    // mark nonblocking, close-on-exec
    if (cgi_in_fd >= 0)
    {
        int fl = fcntl(cgi_in_fd, F_GETFL, 0);
        if (fl >= 0)
            fcntl(cgi_in_fd, F_SETFL, fl | O_NONBLOCK);
        fl = fcntl(cgi_in_fd, F_GETFD, 0);
        if (fl >= 0)
            fcntl(cgi_in_fd, F_SETFD, fl | FD_CLOEXEC);
    }
    if (cgi_out_fd >= 0)
    {
        int fl = fcntl(cgi_out_fd, F_GETFL, 0);
        if (fl >= 0)
            fcntl(cgi_out_fd, F_SETFL, fl | O_NONBLOCK);
        fl = fcntl(cgi_out_fd, F_GETFD, 0);
        if (fl >= 0)
            fcntl(cgi_out_fd, F_SETFD, fl | FD_CLOEXEC);
    }

    cgi_active = (cgi_out_fd >= 0);
    return true;
}
