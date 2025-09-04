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

/***---------------------------CGI PROCCESSING AND EXECUTION--------------------------- ***/


void CGIStreamer::closeStdin() {
    if (cgi_in_fd >= 0) { ::close(cgi_in_fd); cgi_in_fd = -1; }
}


std::size_t CGIStreamer::takeOutBytes(ChainBuf &out, std::size_t max_bytes)
{
    if (outOffset >= outBuffer.size())
        return 0;
    std::size_t avail = outBuffer.size() - outOffset;
    if (max_bytes && max_bytes < avail) avail = max_bytes;
    if (avail) {
        out.push_copy(&outBuffer[0] + outOffset, avail);
        outOffset += avail;
    }
    return avail;
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
bool CGIStreamer::parseCgiHeaders(std::string &buf, int &status, long &content_len)
{
    // Look for header terminator
    std::string::size_type p = buf.find("\r\n\r\n");
    if (p == std::string::npos)
        return false;

    // Extract header block (without the empty line)
    std::string hdrs = buf.substr(0, p + 2);
    buf.erase(0, p + 4); // remove "hdrs\r\n\r\n"

    status = 0;          // 0 means "not set"; default to 200 later
    content_len = -1;    // -1 means unknown

    // Parse a few standard fields (Status, Content-Length)
    std::string::size_type start = 0;
    while (start < hdrs.size()) {
        std::string::size_type end = hdrs.find("\r\n", start);
        if (end == std::string::npos) break;
        std::string line = hdrs.substr(start, end - start);
        start = end + 2;
        if (line.empty()) break;

        if (line.size() >= 7 && strncasecmp(line.c_str(), "Status:", 7) == 0) {
            const char *pnum = line.c_str() + 7;
            while (*pnum==' '||*pnum=='\t') ++pnum;
            int code = 0;
            while (*pnum>='0'&&*pnum<='9') { code = code*10 + (*pnum-'0'); ++pnum; }
            if (code >= 100 && code <= 599) status = code;
        } else if (line.size() >= 15 && strncasecmp(line.c_str(), "Content-Length:", 15) == 0) {
            const char *pnum = line.c_str() + 15;
            while (*pnum==' '||*pnum=='\t') ++pnum;
            long cl = 0;
            while (*pnum>='0'&&*pnum<='9') { cl = cl*10 + (*pnum-'0'); ++pnum; }
            content_len = cl;
        }
        // (Optional) forward Content-Type and other headers if you want to preserve them.
    }
    return true;
}

void CGIStreamer::onCgiReadable(int fd)
{
    if (fd != cgi_out_fd || !cgi_active) return;

    char buf[8192];
    for (;;) {
        ssize_t n = ::read(cgi_out_fd, buf, sizeof(buf));
        if (n > 0) {
            cgi_buf.append(buf, n);

            if (!cgi_headers_done) {
                if (parseCgiHeaders(cgi_buf, cgi_status, cgi_content_len)) {
                    cgi_headers_done = true;

                    int st = (cgi_status > 0) ? cgi_status : 200;
                    std::ostringstream oss;
                    oss << "HTTP/1.1 " << st << " OK\r\n\r\n";
                    const std::string hdr = oss.str();
                    outBuffer.insert(outBuffer.end(), hdr.data(), hdr.data()+hdr.size());

                    if (!cgi_buf.empty()) {
                        outBuffer.insert(outBuffer.end(), cgi_buf.data(), cgi_buf.data()+cgi_buf.size());
                        cgi_buf.clear();
                    }
                }
            } else {
                outBuffer.insert(outBuffer.end(), buf, buf + n);
            }
            continue;
        }
        if (n == 0) {
            ::close(cgi_out_fd);
            cgi_out_fd = -1;
            cgi_active = false;
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;

        ::close(cgi_out_fd);
        cgi_out_fd = -1;
        cgi_active = false;
        break;
    }
}

void CGIStreamer::onCgiWritable(int fd)
{
    if (fd != cgi_in_fd || !cgi_active) return;

    const std::vector<char> &body = req.getBody();
    while (cgi_body_off < body.size()) {
        ssize_t n = ::write(cgi_in_fd, &body[0] + cgi_body_off, body.size() - cgi_body_off);
        if (n > 0) {
            cgi_body_off += (std::size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        break;
    }
    if (cgi_in_fd >= 0) { ::close(cgi_in_fd); cgi_in_fd = -1; }
}


// Analyze parsed headers and set internal markers for future enforcement.
// We keep this minimal: detect Content-Length, Transfer-Encoding: chunked,
// and Expect: 100-continue. Do not change state here; callers use these
// markers to decide next actions.

bool CGIStreamer::beginCgi(const CgiSpec &spec,
                           const std::string &script_path,
                           const std::vector<std::string> &envv)
{
    // Reset streamer state
    cgi_active        = false;
    cgi_headers_done  = false;
    cgi_status        = 0;
    cgi_content_len   = -1;
    cgi_buf.clear();
    outBuffer.clear();
    outOffset         = 0;
    cgi_body_off      = 0;
    cgi_in_fd         = -1;
    cgi_out_fd        = -1;

    // Spawn the CGI child
    if (!proc.spawn(spec, script_path, envv))
        return false;

    // Parent ends:
    //  - inFD():  parent writes  → child's stdin
    //  - outFD(): parent reads   ← child's stdout
    cgi_in_fd  = proc.inFD();
	cgi_out_fd = proc.outFD();

	// set O_NONBLOCK + FD_CLOEXEC on both
	int fl = ::fcntl(cgi_out_fd, F_GETFL, 0);
	if (fl >= 0) ::fcntl(cgi_out_fd, F_SETFL, fl | O_NONBLOCK);
	fl = ::fcntl(cgi_out_fd, F_GETFD, 0);
	if (fl >= 0) ::fcntl(cgi_out_fd, F_SETFD, fl | FD_CLOEXEC);

	if (cgi_in_fd >= 0) {
		fl = ::fcntl(cgi_in_fd, F_GETFL, 0);
		if (fl >= 0) ::fcntl(cgi_in_fd, F_SETFL, fl | O_NONBLOCK);
		fl = ::fcntl(cgi_in_fd, F_GETFD, 0);
		if (fl >= 0) ::fcntl(cgi_in_fd, F_SETFD, fl | FD_CLOEXEC);
	}

    // We’re “active” if we can read the child's stdout
    cgi_active = (cgi_out_fd >= 0);

    // If no request body to feed (typical GET/HEAD), close stdin now so the script doesn’t block on read()
    const std::string m = req.getMethod();
    const bool has_body = (req.getBodyLength() > 0);
    if (!has_body && m != "POST") {
        if (cgi_in_fd >= 0) {
            ::close(cgi_in_fd);
            cgi_in_fd = -1;
        }
    }

    return true;
}

