#include "CGIStreamer.h"
#include "CgiProcess.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <stdlib.h>
#include "ChainBuf.h"   // for ChainBuf::push_copy
#include <algorithm>    
#include <fcntl.h>   // fcntl, O_NONBLOCK, FD_CLOEXEC
#include <unistd.h>  // close (if not already included)// optional
#include "ResponseFactory.h"  // if you want reason phrases; optional
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <cstdio>
#include <sstream>
#include <ctime>

/***---------------------------CGI PROCCESSING AND EXECUTION--------------------------- ***/


// CGIStreamer.cpp
#include <unistd.h>   // ::close


static std::string http_date_now_gmt() {
    char buf[64];
    time_t t = time(0);
    struct tm g = *gmtime(&t);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
    return std::string(buf);
}

static const char* reason_for(int code) {
    switch (code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}



void CGIStreamer::closeStdin()
{
    if (cgi_in_fd >= 0) {
        ::close(cgi_in_fd);
        cgi_in_fd = -1;
    }
}




std::size_t CGIStreamer::takeOutBytes(ChainBuf &out, std::size_t max_bytes)
{
    const size_t avail = outBuffer.size() - outOffset;
    if (avail == 0) return 0;

    std::size_t n = (avail < max_bytes) ? avail : max_bytes;

    if (n > 0) {
        out.push_copy(&outBuffer[outOffset], n);
        outOffset += n;
        fprintf(stderr, "[CGI] takeOutBytes moved=%lu remain=%lu\n",
                (unsigned long)n,
                (unsigned long)(outBuffer.size() - outOffset));
        if (outOffset == outBuffer.size()) {
            outBuffer.clear();
            outOffset = 0;
        }
    }
    return n;
}





CGIStreamer::CGIStreamer( HttpRequest& req, HttpResponse& res)
    : req(req), res(res), cgi_active(false), cgi_in_fd(-1), cgi_out_fd(-1),
      cgi_body_off(0), cgi_headers_done(false), cgi_status(200), cgi_content_len(-1),
      cgi_deadline(0), outOffset(0), state(READ) {
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
    if (pos != std::string::npos) {
        p = pos + 4;
    } else {
        // Try LF LF
        pos = buf.find("\n\n");
        if (pos != std::string::npos) p = pos + 2;
    }
    if (p == std::string::npos) return false; // need more

    // Parse headers line-by-line
    status = 200;
    content_len = -1;
    std::string headers = buf.substr(0, p);
    std::istringstream is(headers);
    std::string line;

    while (std::getline(is, line)) {
        // trim trailing CR
        if (!line.empty() && line[line.size()-1] == '\r')
            line.erase(line.size()-1);

        if (line.empty()) break; // end of headers

        // Status: 200 OK
        if (line.size() >= 7 && (line[0]=='S'||line[0]=='s')) {
            // starts with 'Status:' ?
            const char *k = "Status:";
            bool match = true;
            for (size_t i=0;i<7 && i<line.size();++i) {
                char a = line[i];
                char b = k[i];
                if (a>='A'&&a<='Z') a = char(a - 'A' + 'a');
                if (b>='A'&&b<='Z') b = char(b - 'A' + 'a');
                if (a!=b) { match=false; break; }
            }
            if (match) {
                std::istringstream ls(line.substr(7));
                int s = 0; ls >> s;
                if (s >= 100 && s <= 599) status = s;
            }
        }

        // Content-Length:
        if (line.size() >= 15) {
            const char *k2 = "Content-Length:";
            bool match2 = true;
            for (size_t i=0;i<15 && i<line.size();++i) {
                char a = line[i];
                char b = k2[i];
                if (a>='A'&&a<='Z') a = char(a - 'A' + 'a');
                if (b>='A'&&b<='Z') b = char(b - 'A' + 'a');
                if (a!=b) { match2=false; break; }
            }
            if (match2) {
                const char* s = line.c_str()+15;
                while (*s==' '||*s=='\t') ++s;
                long v = 0;
                while (*s>='0'&&*s<='9') { v = v*10 + (*s - '0'); ++s; }
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
    if (!cgi_active || fd != cgi_out_fd)
        return;

    // 1) Pull bytes from CGI stdout into our scratch buffer.
    for (;;)
    {
        char buf[8192];
        ssize_t n = ::read(cgi_out_fd, buf, sizeof(buf));
        if (n > 0)
        {
            cgi_buf.append(buf, static_cast<std::string::size_type>(n));
            // Debug
            std::fprintf(stderr, "[CGI] read %ld bytes from outFD=%d (buf=%lu)\n",
                         (long)n, cgi_out_fd, (unsigned long)cgi_buf.size());
            continue; // try to drain more this tick
        }
        if (n == 0)
        {
            // EOF from child stdout: close and mark.
            ::close(cgi_out_fd);
            cgi_out_fd = -1;
            std::fprintf(stderr, "[CGI] EOF on stdout, closing outFD\n");
            break;
        }
        // n < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break; // nothing more right now
        // Hard error: close and stop reading
        std::fprintf(stderr, "[CGI] read error on stdout: %s\n", std::strerror(errno));
        ::close(cgi_out_fd);
        cgi_out_fd = -1;
        break;
    }

    // 2) If we haven't sent the HTTP headers yet, try to parse the CGI header block.
    if (!cgi_headers_done)
    {
        // Look for header terminator (support \r\n\r\n and \n\n)
        std::string::size_type pos = std::string::npos;
        std::string::size_type rn  = cgi_buf.find("\r\n\r\n");
        std::string::size_type nn  = cgi_buf.find("\n\n");
        if (rn != std::string::npos) pos = rn + 4;
        else if (nn != std::string::npos) pos = nn + 2;

        if (pos != std::string::npos)
        {
            // Split header block and the first body chunk
            const std::string header_block = cgi_buf.substr(0, pos);
            std::string       rest         = cgi_buf.substr(pos);
            cgi_buf.clear(); // we’re going to move 'rest' into outBuffer below

            // Parse CGI headers
            int  status = 200;
            long clen   = -1;

            // We will forward all headers except "Status:"
            std::vector<std::string> forward_headers;

            // Iterate lines (handle CRLF or LF)
            std::string line;
            for (std::string::size_type i = 0; i < header_block.size(); )
            {
                // find end of line
                std::string::size_type j = i;
                while (j < header_block.size() && header_block[j] != '\r' && header_block[j] != '\n')
                    ++j;
                line.assign(header_block, i, j - i);

                // advance past CRLF / LF
                if (j < header_block.size() && header_block[j] == '\r') ++j;
                if (j < header_block.size() && header_block[j] == '\n') ++j;
                i = j;

                // stop at the blank line (header terminator)
                if (line.empty())
                    continue;

                // Parse "Status:" specially
                // Format can be: Status: 200 OK  (we accept "Status: 200" too)
                if (line.size() >= 7 && (line[0] == 'S' || line[0] == 's'))
                {
                    // case-insensitive compare for "status:"
                    const char *p = line.c_str();
                    if ((p[0] == 'S' || p[0] == 's') &&
                        (p[1] == 't' || p[1] == 'T') &&
                        (p[2] == 'a' || p[2] == 'A') &&
                        (p[3] == 't' || p[3] == 'T') &&
                        (p[4] == 'u' || p[4] == 'U') &&
                        (p[5] == 's' || p[5] == 'S') &&
                        p[6] == ':')
                    {
                        // extract code
                        int code = 0;
                        std::size_t k = 7;
                        while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
                        while (k < line.size() && std::isdigit(static_cast<unsigned char>(line[k])))
                        {
                            code = code * 10 + (line[k] - '0');
                            ++k;
                        }
                        if (code >= 100 && code <= 599) status = code;
                        // do NOT forward "Status:" to the client; HTTP status line will encode it.
                        continue;
                    }
                }

                // Track Content-Length (optional)
                if (line.size() >= 15)
                {
                    // case-insensitive compare for "content-length:"
                    const char* p = line.c_str();
                    if ((p[0] == 'C' || p[0] == 'c') &&
                        (p[1] == 'o' || p[1] == 'O') &&
                        (p[2] == 'n' || p[2] == 'N') &&
                        (p[3] == 't' || p[3] == 'T') &&
                        (p[4] == 'e' || p[4] == 'E') &&
                        (p[5] == 'n' || p[5] == 'N') &&
                        (p[6] == 't' || p[6] == 'T') &&
                        p[7] == '-' &&
                        (p[8] == 'L' || p[8] == 'l') &&
                        (p[9] == 'e' || p[9] == 'E') &&
                        (p[10] == 'n' || p[10] == 'N') &&
                        (p[11] == 'g' || p[11] == 'G') &&
                        (p[12] == 't' || p[12] == 'T') &&
                        (p[13] == 'h' || p[13] == 'H') &&
                        p[14] == ':')
                    {
                        // parse integer after colon
                        long v = 0;
                        std::size_t k = 15;
                        while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
                        while (k < line.size() && std::isdigit(static_cast<unsigned char>(line[k])))
                        {
                            v = v * 10 + (line[k] - '0');
                            ++k;
                        }
                        if (v >= 0) clen = v;
                    }
                }

                // Forward the header line as-is
                forward_headers.push_back(line);
            }

            // Store status/content-length in our state
            cgi_status      = status;
            cgi_content_len = clen;

            // 3) Emit the HTTP status line + Date/Server, then all CGI headers (except Status:), then CRLF.
            {
                std::ostringstream os;
                const int out_status = (cgi_status ? cgi_status : 200);
                os << "HTTP/1.1 " << out_status << " " << reason_for(out_status) << "\r\n";
                os << "Date: "   << http_date_now_gmt() << "\r\n";
                os << "Server: webserv\r\n";
                for (std::size_t i = 0; i < forward_headers.size(); ++i)
                    os << forward_headers[i] << "\r\n";
                os << "\r\n"; // end of header block

                const std::string head = os.str();
                outBuffer.insert(outBuffer.end(), head.begin(), head.end());
            }

            // 4) Append any body bytes that followed the header block in the same read burst
            if (!rest.empty())
                outBuffer.insert(outBuffer.end(), rest.begin(), rest.end());

            cgi_headers_done = true;
            std::fprintf(stderr, "[CGI] headers parsed, status=%d, clen=%ld, outBuffer=%lu\n",
                         cgi_status, cgi_content_len, (unsigned long)outBuffer.size());
        }
        // If header terminator not found yet, we simply wait for more bytes.
    }
    else
    {
        // Headers already emitted; move any newly read bytes to outBuffer.
        if (!cgi_buf.empty())
        {
            outBuffer.insert(outBuffer.end(), cgi_buf.begin(), cgi_buf.end());
            std::fprintf(stderr, "[CGI] appended %lu body bytes (total out=%lu)\n",
                         (unsigned long)cgi_buf.size(), (unsigned long)outBuffer.size());
            cgi_buf.clear();
        }
    }

    // 5) If both ends are closed and we have no more buffered bytes, mark CGI inactive.
    if (cgi_out_fd < 0 && cgi_in_fd < 0)
    {
        if (outOffset >= outBuffer.size())
        {
            cgi_active = false;
            std::fprintf(stderr, "[CGI] done (inactive)\n");
        }
    }
}





void CGIStreamer::onCgiWritable(int fd)
{
     fprintf(stderr, "[CGI] onCgiWritable fd=%d active=%d in_fd=%d method=%s body_len=%lu off=%lu\n",
            fd, (int)cgi_active, cgi_in_fd, req.getMethod().c_str(),
            (unsigned long)req.getBodyLength(), (unsigned long)cgi_body_off);
    if (!cgi_active || fd != cgi_in_fd) return;
    if (cgi_in_fd < 0) return; // already closed

    // No request body? Close stdin so the script doesn't wait forever.
    if (req.getMethod() != "POST" && req.getBodyLength() == 0) {
        closeStdin();
        return;
    }

    // Stream in-memory body (adjust later if you add temp-file streaming)
    const std::vector<char> &b = req.getBody();
    const std::size_t kMaxPerWrite = 64u * 1024u;

    while (cgi_body_off < b.size()) {
        std::size_t to_send = b.size() - cgi_body_off;
        if (to_send > kMaxPerWrite) to_send = kMaxPerWrite;

        ssize_t n = ::write(cgi_in_fd, &b[cgi_body_off], to_send);
        if (n > 0) {
            cgi_body_off += static_cast<std::size_t>(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Backpressure: try again when POLLOUT fires next time
            break;
        } else {
            // Hard error (EPIPE, etc.) — stop feeding the child
            closeStdin();
            fprintf(stderr, "[CGI] closeStdin()\n");
            return;
        }
    }

    // Body fully sent → close stdin so child sees EOF and starts responding
    if (cgi_body_off >= b.size()) {
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

    if (!proc.spawn(spec, script_path, envv))
        return false;

    cgi_in_fd  = proc.inFD();
    cgi_out_fd = proc.outFD();

    // mark nonblocking, close-on-exec
    if (cgi_in_fd >= 0) {
        int fl = fcntl(cgi_in_fd, F_GETFL, 0); if (fl >= 0) fcntl(cgi_in_fd, F_SETFL,  fl | O_NONBLOCK);
        fl = fcntl(cgi_in_fd, F_GETFD, 0);     if (fl >= 0) fcntl(cgi_in_fd, F_SETFD, fl | FD_CLOEXEC);
    }
    if (cgi_out_fd >= 0) {
        int fl = fcntl(cgi_out_fd, F_GETFL, 0); if (fl >= 0) fcntl(cgi_out_fd, F_SETFL,  fl | O_NONBLOCK);
        fl = fcntl(cgi_out_fd, F_GETFD, 0);     if (fl >= 0) fcntl(cgi_out_fd, F_SETFD, fl | FD_CLOEXEC);
    }

    cgi_active = (cgi_out_fd >= 0);
    return true;
}
