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
#include <ctype.h>   // tolower
#include <signal.h>  // SIGKILL (if you still kill by pid)
#include <sys/stat.h>   // struct stat, fstat
#include <unistd.h>     // close, fstat on POSIX





/***---------------------------CGI PROCCESSING AND EXECUTION--------------------------- ***/

// CGIStreamer.cpp
#include <unistd.h> // ::close

/* void CGIStreamer::closeStdin() {
    if (cgi_in_fd >= 0) {
        ::close(cgi_in_fd);
        cgi_in_fd = -1;
    }
} */




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

namespace {
    static bool iequals(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::string::size_type i = 0; i < a.size(); ++i) {
            unsigned char ca = static_cast<unsigned char>(a[i]);
            unsigned char cb = static_cast<unsigned char>(b[i]);
            if (std::tolower(ca) != std::tolower(cb)) return false;
        }
        return true;
    }

    /* static void rstrip_cr(std::string& s) {
        if (!s.empty() && s[s.size() - 1] == '\r')
            s.erase(s.size() - 1);
    } */
}





void CGIStreamer::closeStdin()
{
    if (cgi_in_fd >= 0) {
        ::close(cgi_in_fd);
        cgi_in_fd = -1;
    }
    // Important: Event loop must stop polling this fd once it sees -1.
}
void CGIStreamer::closeStdout() {
    if (cgi_out_fd >= 0) { ::close(cgi_out_fd); cgi_out_fd = -1; }
}


std::size_t CGIStreamer::takeOutBytes(ChainBuf &out, std::size_t max_bytes)
{
    std::size_t moved = 0;
    while (outOffset < outBuffer.size() && moved < max_bytes)
    {
        const std::size_t avail = outBuffer.size() - outOffset;
        const std::size_t chunk  = (avail < (max_bytes - moved)) ? avail : (max_bytes - moved);
        if (chunk == 0) break;

        out.push_copy(&outBuffer[outOffset], chunk);
        outOffset += chunk;
        moved     += chunk;

        // compact if we’ve drained a lot
        if (outOffset > 0 && outOffset == outBuffer.size()) {
            outBuffer.clear();
            outOffset = 0;
            break;
        }
    }
    return moved;
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

void CGIStreamer::parseOneHeaderLine(const std::string& line)
{
    // Split "Key: value"
    std::string::size_type pos = line.find(':');
    if (pos == std::string::npos) return;

    std::string key = line.substr(0, pos);
    std::string val;
    if (pos + 1 < line.size() && line[pos + 1] == ' ')
        val = line.substr(pos + 2);
    else
        val = line.substr(pos + 1);

    // Special cases
    if (iequals(key, "Status")) {
        // "Status: 404 Not Found"
        int code = 0;
        std::string reason;
        std::string::size_type sp = val.find(' ');
        if (sp == std::string::npos) { code = Atoi::atoiHttpReq(val.c_str(), *(new bool(false))); }
        else {
            code = Atoi::atoiHttpReq(val.substr(0, sp).c_str(), *(new bool(false)));
            reason = val.substr(sp + 1);
        }
        status_code_   = code;
        status_reason_ = reason;
        return;
    }

    if (iequals(key, "Set-Cookie")) {
        set_cookie_.push_back(val); // keep multiple
        return;
    }

    if (iequals(key, "Content-Type")) {
        saw_content_type_ = true;
        res.headers.set("Content-Type", val);
        return;
    }

    // Default: copy through to response headers
    res.headers.set(key, val);
}


void CGIStreamer::finalizeHeaders()
{
    // Default status
    int status = status_code_ ? status_code_ : 200;
    std::string reason = status_reason_.empty() ? "OK" : status_reason_;

    if (!saw_content_type_) {
        res.headers.set("Content-Type", "text/plain");
    }

    // Stream CGI output as chunked to client
    chunked_mode = true;
    res.headers.set("Transfer-Encoding", "chunked");

    // Keep-alive by default (your server/pipeline may override)
    res.headers.set("Connection", "keep-alive");

    // Build minimal start line + headers
    std::string head;
    head.reserve(256);
    head += "HTTP/1.1 ";
    {
        char tmp[16];
        std::sprintf(tmp, "%d", status);
        head += tmp;
    }
    head += " ";
    head += reason;
    head += "\r\n";

    // Serialize headers (your Headers class supports printing via operator<< usually,
    // but since we’re in minimal mode, call toString() if you have one; else iterate).
    // If you don’t have an easy serializer here, you can at least write the fields you set:
    head += "Content-Type: " + res.headers.get("Content-Type") + "\r\n";
    head += "Transfer-Encoding: chunked\r\n";
    head += "Connection: " + res.headers.get("Connection") + "\r\n";

    // Multi Set-Cookie
    for (std::vector<std::string>::size_type i = 0; i < set_cookie_.size(); ++i) {
        head += "Set-Cookie: " + set_cookie_[i] + "\r\n";
    }

    head += "\r\n"; // end of header block

    enqueueOut(head.data(), head.size());
    cgi_headers_done = true;
    hdr_state_ = HDR_DONE;
}


void CGIStreamer::timeout504()
{
    // Stop talking to the child
    closeStdin();
    closeStdout();

     // Stop and reap the child; also closes its pipe fds
    proc.terminate();

    // Don’t try to synthesize a response here if your connection owns that logic.
    // Just mark inactive; the owner connection/timer should notice and fail 504.
    cgi_active = false;
}




void CGIStreamer::enqueueOut(const char* data, std::size_t len)
{
    if (len == 0) return;

    if (chunked_mode) {
        // write: <hex>\r\n
        char hex[32];
        // C++98 safe: use %zx alternative via casting to unsigned long
        // but %lx expects unsigned long, so convert len.
        std::sprintf(hex, "%lx\r\n", static_cast<unsigned long>(len));
        outBuffer.insert(outBuffer.end(), hex, hex + std::strlen(hex));

        // write: data
        outBuffer.insert(outBuffer.end(), data, data + len);

        // write: \r\n
        outBuffer.push_back('\r');
        outBuffer.push_back('\n');
    } else {
        outBuffer.insert(outBuffer.end(), data, data + len);
    }
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
    if (fd != cgi_out_fd || cgi_out_fd < 0) return;

    // Optional extra guard (onEvent usually gates this already)
    if (!wantsRead()) return;

    char buf[8192];
    for (;;)
    {
        ssize_t r = ::read(cgi_out_fd, buf, sizeof(buf));
        if (r > 0)
        {
            const char* p = buf;
            std::size_t n = static_cast<std::size_t>(r);

            // -------- Header preface handling (optional per CGI) --------
            if (hdr_state_ == HDR_WAITING)
            {
                // Accumulate until blank line (CRLFCRLF or LFLF)
                cgi_header_accum_.append(p, n);

                // Try to find end-of-headers
                std::string::size_type cut = std::string::npos;
                std::string::size_type k = cgi_header_accum_.find("\r\n\r\n");
                if (k != std::string::npos) cut = k + 4;
                else {
                    k = cgi_header_accum_.find("\n\n");
                    if (k != std::string::npos) cut = k + 2;
                }

                if (cut == std::string::npos)
                {
                    // Heuristic fallback:
                    //  - if the first line has no ":", or
                    //  - headers grow too large without terminator
                    // → treat everything as body (some scripts print only body).
                    bool first_line_has_colon = false;
                    {
                        std::string::size_type eol = cgi_header_accum_.find_first_of("\r\n");
                        std::string first = (eol == std::string::npos)
                          ? cgi_header_accum_ : cgi_header_accum_.substr(0, eol);
                        first_line_has_colon = (first.find(':') != std::string::npos);
                    }
                    if (!first_line_has_colon || cgi_header_accum_.size() > 8192u)
                    {
                        // No recognizable header preface → stream as body
                        if (!cgi_header_accum_.empty())
                            enqueueOut(cgi_header_accum_.data(), cgi_header_accum_.size());
                        cgi_header_accum_.clear();
                        hdr_state_ = HDR_DONE;
                    }

                    // Still waiting for more header bytes (or just switched to DONE)
                    continue;
                }

                // We have a header block → parse it
                const std::string hdr = cgi_header_accum_.substr(0, cut);
                // Minimal line parser: split on CRLF or LF
                std::string line;
                for (std::string::size_type i = 0; i < hdr.size(); )
                {
                    // consume a line
                    std::string::size_type j = i;
                    while (j < hdr.size() && hdr[j] != '\r' && hdr[j] != '\n') ++j;
                    line.assign(hdr, i, j - i);

                    // skip line break
                    if (j < hdr.size() && hdr[j] == '\r') ++j;
                    if (j < hdr.size() && hdr[j] == '\n') ++j;
                    i = j;

                    if (line.empty()) continue; // blank line (separator)

                    // key: value
                    std::string::size_type col = line.find(':');
                    if (col == std::string::npos) continue;

                    std::string key = line.substr(0, col);
                    std::string val = line.substr(col + 1);

                    // trim leading spaces in value
                    while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
                        val.erase(0, 1);

                    // lowercase key for comparisons
                    for (std::size_t qi = 0; qi < key.size(); ++qi) {
                        unsigned char c = static_cast<unsigned char>(key[qi]);
                        if (c >= 'A' && c <= 'Z') key[qi] = char(c - 'A' + 'a');
                    }

                    if (key == "status") {
                        // format: "NNN Reason"
                        int code = 0;
                        std::size_t pos = 0;
                        while (pos < val.size() && val[pos] >= '0' && val[pos] <= '9') {
                            code = code * 10 + (val[pos] - '0');
                            ++pos;
                        }
                        while (pos < val.size() && val[pos] == ' ') ++pos;
                        status_code_   = code;
                        status_reason_ = (pos < val.size()) ? val.substr(pos) : std::string();
                    } else if (key == "content-type") {
                        saw_content_type_ = true;
                        // (If you want to forward/track, store val in a member)
                    } else if (key == "set-cookie") {
                        set_cookie_.push_back(val);
                    }
                    // ignore other headers; the HTTP head is already sent by the connection
                }

                // Anything after the header terminator is body – stream it
                if (cut < cgi_header_accum_.size()) {
                    const char* bodyp = cgi_header_accum_.data() + cut;
                    const std::size_t bodyn = cgi_header_accum_.size() - cut;
                    enqueueOut(bodyp, bodyn);
                }

                cgi_header_accum_.clear();
                hdr_state_ = HDR_DONE;
                continue;
            }

            // -------- Normal body streaming (headers already handled/omitted) --------
            enqueueOut(p, n);
            continue;
        }

        if (r == 0)  // EOF from CGI
        {
            closeStdout();

            // Finish chunked stream once
            if (chunked_mode && !sent_final_chunk) {
                static const char fin[] = "0\r\n\r\n";
                outBuffer.insert(outBuffer.end(), fin, fin + sizeof(fin) - 1);
                sent_final_chunk = true;
            }
            cgi_active = false;
            break;
        }

        // r < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            break;

        // Hard error
        closeStdout();
        cgi_active = false;
        break;
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

    // ---- DISK-BACKED BODY ----
    if (req.isBodyOnDisk())
    {
        // Open the temp body file lazily
        if (body_fd_ < 0) {
            body_fd_ = ::open(body_path_.c_str(), O_RDONLY
#ifdef O_CLOEXEC
                              | O_CLOEXEC
#endif
            );
            if (body_fd_ < 0) {
                std::fprintf(stderr, "[CGI] open(%s) failed: %d\n",
                             body_path_.c_str(), errno);
                closeStdin();        // unblock child even on error
                return;
            }
        }

        // Determine total size from the file (don’t rely on req.getBodyLength())
        struct stat st;
        if (::fstat(body_fd_, &st) != 0) {
            std::fprintf(stderr, "[CGI] fstat(%s) failed: %d\n",
                         body_path_.c_str(), errno);
            closeStdin();
            return;
        }
        const std::size_t total = static_cast<std::size_t>(st.st_size);

        std::fprintf(stderr,
            "[CGI] POLLOUT inFD=%d off=%zu total=%zu disk=1\n",
            cgi_in_fd, (size_t)cgi_body_off, (size_t)total);

        // Nothing to send? (empty file) → close stdin
        if (total == 0 || cgi_body_off >= total) {
            closeStdin();
            return;
        }

        // Read the next slice we still need and write it to the child
        char buf[8192];
        const std::size_t remaining = total - cgi_body_off;
        const std::size_t to_read   = remaining < sizeof(buf) ? remaining : sizeof(buf);

        const off_t pos = static_cast<off_t>(cgi_body_off);
        ssize_t r = ::pread(body_fd_, buf, to_read, pos);
        if (r < 0) {
            if (errno == EINTR) return;       // try again later
            std::fprintf(stderr, "[CGI] pread error: %d\n", errno);
            closeStdin();
            return;
        }
        if (r == 0) {
            // Unexpected EOF before total; still signal EOF to child
            std::fprintf(stderr, "[CGI] pread EOF early at %zu/%zu\n",
                         (size_t)cgi_body_off, (size_t)total);
            closeStdin();
            return;
        }

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
                closeStdin(); // signal EOF so CGI can proceed
            }
        }
        return;
    }

    // ---- IN-MEMORY BODY ----
    const std::size_t total = req.getBodyLength();
    const std::vector<char>& b = req.getBody();

    std::fprintf(stderr,
        "[CGI] POLLOUT inFD=%d off=%zu total=%zu disk=0\n",
        cgi_in_fd, (size_t)cgi_body_off, (size_t)total);

    // Nothing to send? (no body) or already finished
    if (total == 0 || cgi_body_off >= total || cgi_body_off >= b.size()) {
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
        if (cgi_body_off >= total || cgi_body_off >= b.size()) {
            closeStdin(); // signal EOF so CGI can proceed
        }
    }
}





// Analyze parsed headers and set internal markers for future enforcement.
// We keep this minimal: detect Content-Length, Transfer-Encoding: chunked,
// and Expect: 100-continue. Do not change state here; callers use these
// markers to decide next actions.

// CGIStreamer.cpp  — replace your beginCgi body with this version
bool CGIStreamer::beginCgi(const CgiSpec &spec,
                           const std::string &script_path,
                           const std::vector<std::string> &envv)
{
    // ---- Reset streaming state ----
    cgi_active        = false;
    cgi_headers_done  = false;      // we’ll parse CGI preface
    cgi_status        = 200;
    cgi_content_len   = -1;
    cgi_buf.clear();

    outBuffer.clear();
    outOffset         = 0;

    cgi_body_off      = 0;
    body_fd_          = -1;
    body_off_         = 0;
    body_path_        = req.isBodyOnDisk() ? req.getBodyFilePath() : std::string();

    // Header-phase parsing state
    hdr_state_            = HDR_WAITING;   // wait for CGI header preface
    cgi_header_accum_.clear();
    saw_content_type_     = false;
    status_code_          = 0;
    status_reason_.clear();
    set_cookie_.clear();

    // Output framing to client
    chunked_mode          = true;
    sent_final_chunk      = false;
    cgi_preface_stripped_ = false;
    cgi_preface_buf_.clear();

    // ---- Spawn the CGI process ----
    if (!proc.spawn(spec, script_path, envv))
        return false;

    cgi_in_fd  = proc.inFD();   // server writes → child stdin
    cgi_out_fd = proc.outFD();  // server reads  ← child stdout

#ifdef F_GETFL
    if (cgi_in_fd >= 0) {
        int fl = ::fcntl(cgi_in_fd, F_GETFL, 0);
        if (fl >= 0) ::fcntl(cgi_in_fd, F_SETFL, fl | O_NONBLOCK);
        fl = ::fcntl(cgi_in_fd, F_GETFD, 0);
        if (fl >= 0) ::fcntl(cgi_in_fd, F_SETFD, fl | FD_CLOEXEC);
    }
    if (cgi_out_fd >= 0) {
        int fl = ::fcntl(cgi_out_fd, F_GETFL, 0);
        if (fl >= 0) ::fcntl(cgi_out_fd, F_SETFL, fl | O_NONBLOCK);
        fl = ::fcntl(cgi_out_fd, F_GETFD, 0);
        if (fl >= 0) ::fcntl(cgi_out_fd, F_SETFD, fl | FD_CLOEXEC);
    }
#endif

    // If no request body in RAM or on disk, close child's stdin immediately.
    if (req.getBodyLength() == 0 && !req.isBodyOnDisk())
        closeStdin();

    // ---- Arm deadlines (use PhaseDeadline::reset(now_ms, timeout_ms)) ----
    struct timeval tv; ::gettimeofday(&tv, 0);
    unsigned long long now_ms =
        (unsigned long long)tv.tv_sec * 1000ULL +
        (unsigned long long)tv.tv_usec / 1000ULL;

    const int HDR_WAIT_MS     = 3000;   // 3s to receive CGI headers
    const int TOTAL_LIMIT_MS  = 15000;  // 15s total CGI lifetime

    hdr_deadline_.reset(now_ms, HDR_WAIT_MS);
    total_deadline_.reset(now_ms, TOTAL_LIMIT_MS);

    // Log (optional)
    std::fprintf(stderr,
        "[CGI] begin: inFD=%d outFD=%d len=%zu disk=%d path=%s\n",
        cgi_in_fd, cgi_out_fd,
        (size_t)req.getBodyLength(),
        req.isBodyOnDisk() ? 1 : 0,
        req.isBodyOnDisk() ? body_path_.c_str() : "");

    cgi_active = (cgi_out_fd >= 0);
    return cgi_active;
}






void CGIStreamer::onTick(unsigned long long now_ms) {
    // header deadline while still waiting for headers
    if (hdr_state_ == HDR_WAITING && hdr_deadline_.expired(now_ms)) {
        timeout504();
        return;
    }
    // total runtime deadline always applies
    if (total_deadline_.expired(now_ms)) {
        timeout504();
        return;
    }
}



