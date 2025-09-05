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
    if (cgi_in_fd >= 0)
    {
        ::close(cgi_in_fd);
        cgi_in_fd = -1;
    }
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
    : req(req), res(res), cgi_active(false), cgi_in_fd(-1), cgi_out_fd(-1),
      cgi_body_off(0), cgi_headers_done(false), cgi_status(200), cgi_content_len(-1),
      cgi_deadline(0), outOffset(0), state(READ)
{
    // Initialize the output buffer
    outBuffer.clear();
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



void CGIStreamer::onCgiWritable(int fd)
{
    fprintf(stderr, "[CGI] onCgiWritable fd=%d active=%d in_fd=%d method=%s body_len=%lu off=%lu\n",
            fd, (int)cgi_active, cgi_in_fd, req.getMethod().c_str(),
            (unsigned long)req.getBodyLength(), (unsigned long)cgi_body_off);
    if (!cgi_active || fd != cgi_in_fd)
        return;
    if (cgi_in_fd < 0)
        return; // already closed

    // No request body? Close stdin so the script doesn't wait forever.
    if (req.getMethod() != "POST" && req.getBodyLength() == 0)
    {
        closeStdin();
        return;
    }

    // Stream in-memory body (adjust later if you add temp-file streaming)
    const std::vector<char> &b = req.getBody();
    const std::size_t kMaxPerWrite = 64u * 1024u;

    while (cgi_body_off < b.size())
    {
        std::size_t to_send = b.size() - cgi_body_off;
        if (to_send > kMaxPerWrite)
            to_send = kMaxPerWrite;

        ssize_t n = ::write(cgi_in_fd, &b[cgi_body_off], to_send);
        if (n > 0)
        {
            cgi_body_off += static_cast<std::size_t>(n);
        }
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            // Backpressure: try again when POLLOUT fires next time
            break;
        }
        else
        {
            // Hard error (EPIPE, etc.) — stop feeding the child
            closeStdin();
            fprintf(stderr, "[CGI] closeStdin()\n");
            return;
        }
    }

    // Body fully sent → close stdin so child sees EOF and starts responding
    if (cgi_body_off >= b.size())
    {
        closeStdin();
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
