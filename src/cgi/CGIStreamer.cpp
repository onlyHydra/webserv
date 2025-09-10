#include "CGIStreamer.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "ChainBuf.h"
#include "CgiProcess.h"
#include "EventLoop.h"  
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <vector>
#include <poll.h>   // POLLIN, POLLOUT
#include <fcntl.h>      // fcntl, O_NONBLOCK, FD_CLOEXEC
#include <signal.h>     // SIGKILL
#include <sys/stat.h>   // fstat
#include <sys/time.h>   // gettimeofday
#include <unistd.h>     // read, write, pread, close
#include <sys/time.h> // gettimeofday

// void CGIStreamer::attachLoop(EventLoop* L) { loop_ = L; }

// --- tiny helper: RFC1123 Date string (GMT) ---
static std::string http_date_now_gmt()
{
    char buf[64];
    std::time_t t = std::time(0);
    std::tm g = *std::gmtime(&t);
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
    return std::string(buf);
}

// --- case-insensitive equals for ASCII keys ---
static bool iequals_ascii(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (std::string::size_type i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'A' && ca <= 'Z') ca = char(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = char(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

CGIStreamer::CGIStreamer(HttpRequest &req, HttpResponse &res)
    : req_(req)
    , res_(res)
    , proc_()
    // ---- Event loop ----
    , loop_(NULL)                 // will be set via attachLoop(&eventLoop)
    // ---- CGI pipes / state ----
    , cgi_active_(false)
    , failed_(false)
    , cgi_in_fd_(-1)
    , cgi_out_fd_(-1)
    // ---- Request-body streaming ----
    , cgi_body_off_(0)
    , body_fd_(-1)
    , body_file_off_(0)
    , body_path_()
    // ---- Header parsing ----
    , hdr_state_(HDR_WAITING)
    , cgi_header_accum_()
    , saw_content_type_(false)
    , status_code_(0)             // keep 0 if your code interprets 0 as “unset”
    , status_reason_()
    , set_cookie_()
    // ---- Output framing ----
    , http_head_queued_(false)
    , chunked_mode_(false)
    , sent_final_chunk_(false)
    , out_buf_()
    , out_off_(0)
    // ---- Deadlines / timeouts ----
    , hdr_deadline_()
    , total_deadline_()
    , write_deadline_ms_(0)
    // ---- Back-pressure hint ----
    , client_out_bytes_(0)
    // ---- Read throttling ----
    , stdout_paused_(false)
{
    // nothing else to do
}


CGIStreamer::~CGIStreamer()
{
    closeStdin();
    closeStdout();
}

// --- enqueue helpers (chunked-aware) ---

void CGIStreamer::enqueueOut(const char* data, std::size_t len)
{
    if (len == 0) return;

    if (chunked_mode_) {
        // <hex>\r\n
        char hex[32];
        std::sprintf(hex, "%lx\r\n", (unsigned long)len);
        out_buf_.insert(out_buf_.end(), hex, hex + std::strlen(hex));
        // payload
        out_buf_.insert(out_buf_.end(), data, data + len);
        // \r\n
        out_buf_.push_back('\r');
        out_buf_.push_back('\n');
    } else {
        out_buf_.insert(out_buf_.end(), data, data + len);
    }
}

void CGIStreamer::enqueueFinalChunk()
{
    if (sent_final_chunk_) return;
    static const char fin[] = "0\r\n\r\n";
    out_buf_.insert(out_buf_.end(), fin, fin + sizeof(fin) - 1);
    sent_final_chunk_ = true;
}

void CGIStreamer::pauseStdoutReads() {
    if (stdout_paused_) return;
    if (loop_ && cgi_out_fd_ >= 0) loop_->modFD(cgi_out_fd_, 0);
    stdout_paused_ = true;
}

void CGIStreamer::resumeStdoutReads() {
    if (!stdout_paused_) return;
    if (loop_ && cgi_out_fd_ >= 0) loop_->modFD(cgi_out_fd_, POLLIN);
    stdout_paused_ = false;
}


// --- header parsing ---

void CGIStreamer::parseOneHeaderLine(const std::string& line)
{
    // Expect "Key: Value"
    std::string::size_type col = line.find(':');
    if (col == std::string::npos) return;

    std::string key = line.substr(0, col);
    std::string val = (col + 1 < line.size() && line[col + 1] == ' ')
                    ? line.substr(col + 2)
                    : line.substr(col + 1);

    if (iequals_ascii(key, "Status")) {
        // "Status: 404 Not Found"
        int code = 0;
        std::string::size_type i = 0;
        while (i < val.size() && val[i] >= '0' && val[i] <= '9') {
            code = code * 10 + (val[i] - '0');
            ++i;
        }
        while (i < val.size() && val[i] == ' ') ++i;
        status_code_   = code;
        status_reason_ = (i < val.size()) ? val.substr(i) : std::string();
        return;
    }

    if (iequals_ascii(key, "Content-Type")) {
        saw_content_type_ = true;
        res_.headers.set("Content-Type", val);
        return;
    }

    if (iequals_ascii(key, "Set-Cookie")) {
        set_cookie_.push_back(val); // keep multiple
        return;
    }

    // Other headers can be forwarded if desired:
    res_.headers.set(key, val);
}

// Build and queue the HTTP head once (after CGI headers parsed).
void CGIStreamer::finalizeHeaders()
{
    if (http_head_queued_) return;

    const int status = status_code_ ? status_code_ : 200;
    const std::string reason = status_reason_.empty() ? "OK" : status_reason_;

    // Ensure Content-Type exists
    if (!saw_content_type_)
        res_.headers.set("Content-Type", "text/plain");

    // We stream as chunked
    chunked_mode_ = true;
    res_.headers.set("Transfer-Encoding", "chunked");
    res_.headers.set("Connection", "keep-alive");

    std::string head;
    head.reserve(512);
    head += "HTTP/1.1 ";
    {
        char tmp[16];
        std::sprintf(tmp, "%d", status);
        head += tmp;
    }
    head += " ";
    head += reason;
    head += "\r\n";

    // Minimal header set (Date/Server/CT/TE/Conn + multi Set-Cookie)
    head += "Date: " + http_date_now_gmt() + "\r\n";
    head += "Server: webserv/1.0\r\n";

    // Content-Type
    head += "Content-Type: " + res_.headers.get("Content-Type") + "\r\n";
    // Chunked
    head += "Transfer-Encoding: chunked\r\n";
    // Connection
    {
        const std::string conn = res_.headers.keyExists("Connection")
                               ? res_.headers.get("Connection")
                               : std::string("keep-alive");
        head += "Connection: " + conn + "\r\n";
    }
    // Multiple Set-Cookie
    for (std::vector<std::string>::size_type i = 0; i < set_cookie_.size(); ++i)
        head += "Set-Cookie: " + set_cookie_[i] + "\r\n";

    head += "\r\n"; // end of header block

    // Queue head as-is (not chunked)
    const bool saved = chunked_mode_;
    chunked_mode_ = false;
    enqueueOut(head.data(), head.size());
    chunked_mode_ = saved;

    http_head_queued_ = true;
}

// --- stdin/stdout helpers ---

#include <poll.h>  // POLLIN, POLLOUT

void CGIStreamer::closeStdin() {
#if defined(DEBUG)
    std::fprintf(stderr, "[CGI][FD] closeStdin fd=%d\n", cgi_in_fd_);
#endif
    if (cgi_in_fd_ >= 0) {
        if (loop_) loop_->removeFD(cgi_in_fd_);   // unregister from poller
        ::close(cgi_in_fd_);
        cgi_in_fd_ = -1;
    }
}

void CGIStreamer::closeStdout() {
#if defined(DEBUG)
    std::fprintf(stderr, "[CGI][FD] closeStdout fd=%d\n", cgi_out_fd_);
#endif
    if (cgi_out_fd_ >= 0) {
        if (loop_) loop_->removeFD(cgi_out_fd_);  // unregister from poller
        ::close(cgi_out_fd_);
        cgi_out_fd_ = -1;
    }
}


void CGIStreamer::reset() {
    // stop timers/mark inactive AFTER fds are gone
    closeStdin();
    closeStdout();

    failed_           = false;
    cgi_active_       = false;

    cgi_body_off_     = 0;
    if (body_fd_ >= 0) { ::close(body_fd_); body_fd_ = -1; }
    body_file_off_    = 0;
    body_path_.clear();

    hdr_state_        = HDR_WAITING;
    cgi_header_accum_.clear();
    saw_content_type_ = false;
    status_code_      = 0;
    status_reason_.clear();
    set_cookie_.clear();

    http_head_queued_ = false;
    chunked_mode_     = true;
    sent_final_chunk_ = false;
    out_buf_.clear();
    out_off_          = 0;

    client_out_bytes_ = 0;
    write_deadline_ms_= 0;
    hdr_deadline_.clear();
    total_deadline_.clear();
}


void CGIStreamer::resetWriteDeadline()
{
    struct timeval tv; ::gettimeofday(&tv, 0);
    const unsigned long long now_ms =
        (unsigned long long)tv.tv_sec * 1000ULL +
        (unsigned long long)tv.tv_usec / 1000ULL;
    write_deadline_ms_ = now_ms + WRITE_TIMEOUT_MS;
}

// --- public API ---

bool CGIStreamer::beginCgi(const CgiSpec &spec,
                           const std::string &script_path,
                           const std::vector<std::string> &envv)
{
    // ---- reset runtime state ----
    failed_            = false;
    cgi_active_        = false;

    cgi_in_fd_         = -1;   // server → child stdin (we write here)
    cgi_out_fd_        = -1;   // child  → server stdout (we read here)

    cgi_body_off_      = 0;
    if (body_fd_ >= 0) { ::close(body_fd_); body_fd_ = -1; }
    body_file_off_     = 0;
    body_path_.clear();
    if (req_.isBodyOnDisk()) body_path_ = req_.getBodyFilePath();

    hdr_state_         = HDR_WAITING;
    cgi_header_accum_.clear();
    saw_content_type_  = false;
    status_code_       = 0;
    status_reason_.clear();
    set_cookie_.clear();

    http_head_queued_  = false;
    chunked_mode_      = true;
    sent_final_chunk_  = false;
    out_buf_.clear();
    out_off_           = 0;
    stdout_paused_ = false;


    // ---- spawn & set fds non-blocking ----
    if (!proc_.spawn(spec, script_path, envv))
        return false;

    cgi_in_fd_  = proc_.inFD();
    cgi_out_fd_ = proc_.outFD();

#ifdef F_GETFL
    if (cgi_in_fd_ >= 0) {
        int fl = ::fcntl(cgi_in_fd_, F_GETFL, 0);
        if (fl >= 0) ::fcntl(cgi_in_fd_, F_SETFL, fl | O_NONBLOCK);
#ifdef F_GETFD
        fl = ::fcntl(cgi_in_fd_, F_GETFD, 0);
        if (fl >= 0) ::fcntl(cgi_in_fd_, F_SETFD, fl | FD_CLOEXEC);
#endif
    }
    if (cgi_out_fd_ >= 0) {
        int fl = ::fcntl(cgi_out_fd_, F_GETFL, 0);
        if (fl >= 0) ::fcntl(cgi_out_fd_, F_SETFL, fl | O_NONBLOCK);
#ifdef F_GETFD
        fl = ::fcntl(cgi_out_fd_, F_GETFD, 0);
        if (fl >= 0) ::fcntl(cgi_out_fd_, F_SETFD, fl | FD_CLOEXEC);
#endif
    }
#endif

    if (cgi_out_fd_ < 0) { failed_ = true; return false; }

    // ---- initial event-loop interest (right after O_NONBLOCK) ----
    if (loop_) {
        // always read child's stdout
        loop_->modFD(cgi_out_fd_, POLLIN);
        // be ready to feed child's stdin; if we later decide there's no body,
        // closeStdin() will clear this and close the fd.
        if (cgi_in_fd_ >= 0)
            loop_->modFD(cgi_in_fd_, POLLOUT);
    }

    // If no body at all, close stdin now so the CGI won’t block on read()
    if (req_.getBodyLength() == 0 && !req_.isBodyOnDisk())
        closeStdin(); // this also clears POLLOUT via loop_->modFD(fd, 0)

    // ---- deadlines ----
    struct timeval tv; ::gettimeofday(&tv, 0);
    const unsigned long long now_ms =
        (unsigned long long)tv.tv_sec * 1000ULL +
        (unsigned long long)tv.tv_usec / 1000ULL;

    const int HDR_WAIT_MS    = 3000;   // header-phase timeout
    const int TOTAL_LIMIT_MS = 15000;  // total runtime timeout

    hdr_deadline_.reset(now_ms, HDR_WAIT_MS);
    total_deadline_.reset(now_ms, TOTAL_LIMIT_MS);
    resetWriteDeadline();   // arms WRITE_TIMEOUT_MS from "now"

    cgi_active_ = true;
    return true;
}


void CGIStreamer::startHeaderPhase(unsigned long long now_ms)
{
    // Re-arm header deadline with default 3s (same as beginCgi()).
    hdr_deadline_.reset(now_ms, 10000);
}

void CGIStreamer::onTick(unsigned long long now_ms)
{
    if (hdr_state_ == HDR_WAITING && hdr_deadline_.expired(now_ms)) {
        timeout504();
        return;
    }
    if (total_deadline_.expired(now_ms)) {
        timeout504();
        return;
    }
    if (write_deadline_ms_ && now_ms > write_deadline_ms_) {
        // Child not draining stdin in time → close stdin to unblock it.
        closeStdin();
        write_deadline_ms_ = 0; // avoid repeated triggers
    }
}

std::size_t CGIStreamer::takeOutBytes(ChainBuf &out, std::size_t max_bytes) {
    if (out_off_ >= out_buf_.size()) {
        out_buf_.clear();
        out_off_ = 0;
        return 0;
    }
    std::size_t avail = out_buf_.size() - out_off_;
    std::size_t n = (max_bytes < avail) ? max_bytes : avail;

    if (n == 0) return 0;

    out.push_copy(&out_buf_[out_off_], n);
    out_off_ += n;

    if (out_off_ == out_buf_.size()) {
        out_buf_.clear();
        out_off_ = 0;
    }
    return n;
}


// --- I/O driving ---

void CGIStreamer::onCgiReadable(int fd)
{
    if (!cgi_active_ || fd != cgi_out_fd_ || cgi_out_fd_ < 0) return;

    // Back-pressure gate
    if (!wantsRead()) {
        std::fprintf(stderr, "[CGI][RD] outFD=%d wantsRead=0 → defer\n", cgi_out_fd_);
        return;
    }

    for (;;)
    {
        char buf[16384];
        ssize_t r = ::read(cgi_out_fd_, buf, sizeof(buf));

        if (r > 0)
        {
            const char* p = buf;
            std::size_t n = static_cast<std::size_t>(r);
            std::fprintf(stderr, "[CGI][RD] outFD=%d got=%zu bytes (state=%s)\n",
                         cgi_out_fd_, n, (hdr_state_ == HDR_WAITING ? "HEADERS" : "BODY"));

            if (hdr_state_ == HDR_WAITING)
            {
                // Accumulate until CRLFCRLF or LFLF
                cgi_header_accum_.append(p, n);
                std::fprintf(stderr, "[CGI][RD] accum=%zu bytes\n",
                             cgi_header_accum_.size());

                // Find header/body cut
                std::string::size_type cut = std::string::npos;
                std::string::size_type k = cgi_header_accum_.find("\r\n\r\n");
                if (k != std::string::npos) cut = k + 4;
                else {
                    k = cgi_header_accum_.find("\n\n");
                    if (k != std::string::npos) cut = k + 2;
                }

                if (cut == std::string::npos)
                {
                    // Heuristic: if first line lacks ":" or headers grow too large,
                    // treat the whole thing as body.
                    std::string::size_type eol = cgi_header_accum_.find_first_of("\r\n");
                    std::string first = (eol == std::string::npos)
                        ? cgi_header_accum_ : cgi_header_accum_.substr(0, eol);
                    const bool first_has_colon = (first.find(':') != std::string::npos);

                    if (!first_has_colon || cgi_header_accum_.size() > 8192u) {
                        std::fprintf(stderr,
                            "[CGI][RD] no header terminator (first_has_colon=%d, size=%zu) → treat as body\n",
                            (int)first_has_colon, cgi_header_accum_.size());

                        if (!http_head_queued_) {
                            std::fprintf(stderr, "[CGI][RD] finalizeHeaders (synth)\n");
                            finalizeHeaders(); // synthesize safe head
                        }
                        if (!cgi_header_accum_.empty()) {
                            enqueueOut(cgi_header_accum_.data(), cgi_header_accum_.size());
                            std::fprintf(stderr, "[CGI][RD] queued body spill=%zu\n",
                                         cgi_header_accum_.size());
                        }
                        cgi_header_accum_.clear();
                        hdr_state_ = HDR_DONE;
                    }
                    // else: keep waiting for header terminator
                    continue;
                }

                // We have a complete header block → parse it line-by-line
                const std::string hdr = cgi_header_accum_.substr(0, cut);
                std::fprintf(stderr, "[CGI][RD] header block complete len=%zu (cut at %zu)\n",
                             hdr.size(), cut);

                // Quick counters for debug
                int set_cookie_count = 0;
                int parsed_lines = 0;

                std::string line;
                for (std::string::size_type i = 0; i < hdr.size(); )
                {
                    std::string::size_type j = i;
                    while (j < hdr.size() && hdr[j] != '\r' && hdr[j] != '\n') ++j;
                    line.assign(hdr, i, j - i);
                    if (j < hdr.size() && hdr[j] == '\r') ++j;
                    if (j < hdr.size() && hdr[j] == '\n') ++j;
                    i = j;

                    if (!line.empty()) {
                        // Count Set-Cookie for visibility
                        std::string lower = line;
                        for (size_t q = 0; q < lower.size(); ++q) {
                            char c = lower[q];
                            if (c >= 'A' && c <= 'Z') lower[q] = char(c - 'A' + 'a');
                        }
                        if (lower.size() >= 11 && lower.compare(0, 11, "set-cookie:") == 0)
                            ++set_cookie_count;

                        parseOneHeaderLine(line);
                        ++parsed_lines;
                    }
                }

                std::fprintf(stderr, "[CGI][RD] parsed_lines=%d set-cookie=%d\n",
                             parsed_lines, set_cookie_count);

                // Anything after the terminator is body
                if (!http_head_queued_) {
                    std::fprintf(stderr, "[CGI][RD] finalizeHeaders\n");
                    finalizeHeaders();
                }
                if (cut < cgi_header_accum_.size()) {
                    const char* bp = cgi_header_accum_.data() + cut;
                    const std::size_t bn = cgi_header_accum_.size() - cut;
                    if (bn) {
                        enqueueOut(bp, bn);
                        std::fprintf(stderr, "[CGI][RD] queued post-header body=%zu\n", bn);
                    }
                }

                cgi_header_accum_.clear();
                hdr_state_ = HDR_DONE;
                continue;
            }

            // Normal body streaming (headers already finalized)
            if (!http_head_queued_) {
                std::fprintf(stderr, "[CGI][RD] finalizeHeaders (late safety)\n");
                finalizeHeaders(); // safety net
            }
            enqueueOut(p, n);
            std::fprintf(stderr, "[CGI][RD] queued body=%zu\n", n);
            continue;
        }

        if (r == 0)  // EOF from CGI
        {
            std::fprintf(stderr, "[CGI][RD] EOF on outFD=%d\n", cgi_out_fd_);

            // Make sure we have emitted an HTTP head even if CGI sent no headers
            if (!http_head_queued_) {
                std::fprintf(stderr, "[CGI][RD] finalizeHeaders at EOF (no CGI headers)\n");
                finalizeHeaders();
            }

            // Close stdout and enqueue the terminating chunk if chunked
            closeStdout();
            if (chunked_mode_ && !sent_final_chunk_) {
                enqueueFinalChunk();     // "0\r\n\r\n"
                sent_final_chunk_ = true;
                std::fprintf(stderr, "[CGI][RD] queued final chunk\n");
            }

            cgi_active_ = false;         // streamer done producing
            return;
        }

        // r < 0
        if (errno == EINTR) {
            std::fprintf(stderr, "[CGI][RD] read EINTR → retry\n");
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::fprintf(stderr, "[CGI][RD] read EAGAIN → pause loop\n");
            break;
        }

        // Hard error → stop this CGI cleanly
        std::fprintf(stderr, "[CGI][RD] read error errno=%d (%s) → fail\n",
                     errno, std::strerror(errno));
        closeStdout();
        cgi_active_ = false;
        failed_ = true;
        return;
    }
}


void CGIStreamer::onCgiWritable(int fd)
{
    if (!cgi_active_ || fd != cgi_in_fd_) return;

    // Enter + basic state
    const std::size_t total = req_.getBodyLength();
    std::fprintf(stderr, "[CGI][WR] enter inFD=%d off=%zu total=%zu disk=%d\n",
                 cgi_in_fd_, cgi_body_off_, total, (int)req_.isBodyOnDisk());

    // If no stdin expected, close immediately.
    if (total == 0) {
        std::fprintf(stderr, "[CGI][WR] total=0 → closeStdin\n");
        closeStdin();
        return;
    }

    if (cgi_body_off_ >= total) {
        std::fprintf(stderr, "[CGI][WR] already sent all (%zu/%zu) → closeStdin\n",
                     cgi_body_off_, total);
        closeStdin();
        return;
    }

    // Write at most this many bytes per syscall to avoid hogging the loop.
    const std::size_t CHUNK = 16384u;

    // =========================
    // DISK-BACKED BODY
    // =========================
    if (req_.isBodyOnDisk())
    {
        if (body_fd_ < 0) {
            body_fd_ = ::open(body_path_.c_str(),
                              O_RDONLY
#ifdef O_CLOEXEC
                              | O_CLOEXEC
#endif
            );
            if (body_fd_ < 0) {
                std::fprintf(stderr, "[CGI][WR] open(%s) failed: errno=%d (%s) → closeStdin\n",
                             body_path_.c_str(), errno, std::strerror(errno));
                closeStdin();
                return;
            }
            std::fprintf(stderr, "[CGI][WR] opened body file %s fd=%d\n",
                         body_path_.c_str(), body_fd_);
        }

        const std::size_t remaining = total - cgi_body_off_;
        const std::size_t to_read   = (remaining < CHUNK) ? remaining : CHUNK;
        if (to_read == 0) {
            std::fprintf(stderr, "[CGI][WR] to_read=0 (remaining=%zu) → closeStdin\n", remaining);
            closeStdin();
            return;
        }

        char small[16384];
        // pread() from the *logical* offset (stateless; safe on partial writes)
        ssize_t r;
        do {
            r = ::pread(body_fd_, small, to_read,
                        static_cast<off_t>(cgi_body_off_));
        } while (r < 0 && errno == EINTR);

        if (r < 0) {
            if (errno == EAGAIN) {
                std::fprintf(stderr, "[CGI][WR] pread EAGAIN → retry later\n");
                return;
            }
            std::fprintf(stderr, "[CGI][WR] pread error errno=%d (%s) → closeStdin\n",
                         errno, std::strerror(errno));
            closeStdin();
            return;
        }
        if (r == 0) {
            // Producer (request reader) hasn’t flushed these bytes to disk yet; retry later.
            std::fprintf(stderr, "[CGI][WR] pread=0 (not flushed yet) → retry later\n");
            return;
        }

        std::size_t want = static_cast<std::size_t>(r);
        std::size_t sent = 0;
        std::fprintf(stderr, "[CGI][WR] disk chunk ready want=%zu\n", want);

        while (sent < want) {
            ssize_t w = ::write(cgi_in_fd_, small + sent, want - sent);
            if (w > 0) {
                sent += static_cast<std::size_t>(w);
                cgi_body_off_ += static_cast<std::size_t>(w);
                std::fprintf(stderr, "[CGI][WR] wrote=%zd new_off=%zu/%zu\n",
                             w, cgi_body_off_, total);
                resetWriteDeadline();
                if (cgi_body_off_ >= total) {
                    std::fprintf(stderr, "[CGI][WR] done sending body → closeStdin\n");
                    closeStdin();
                    return;
                }
            } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Pipe full; we’ll retry from the same logical offset on next POLLOUT
                std::fprintf(stderr, "[CGI][WR] write EAGAIN with %zu/%zu sent → retry later\n",
                             sent, want);
                break;
            } else if (w < 0 && errno == EINTR) {
                std::fprintf(stderr, "[CGI][WR] write EINTR → continue\n");
                continue; // retry same write
            } else {
                // Broken pipe / other error
                std::fprintf(stderr, "[CGI][WR] write error errno=%d (%s) → closeStdin\n",
                             errno, std::strerror(errno));
                closeStdin();
                return;
            }
        }

        return;
    }

    // =========================
    // IN-MEMORY BODY
    // =========================
    std::vector<char> tmp = req_.getBody();
    std::fprintf(stderr, "[CGI][WR] mem tmp.size=%zu off=%zu total=%zu\n",
                 tmp.size(), cgi_body_off_, total);

    // Guard empty/not-yet-available bytes to avoid UB on &tmp[0].
    if (tmp.size() <= cgi_body_off_) {
        std::fprintf(stderr, "[CGI][WR] no bytes available yet (tmp.size=%zu, off=%zu) → retry later\n",
                     tmp.size(), cgi_body_off_);
        return;
    }

    std::size_t remaining = total - cgi_body_off_;
    std::size_t avail     = tmp.size() - cgi_body_off_;
    std::size_t len       = (remaining < avail) ? remaining : avail;
    if (len == 0) {
        std::fprintf(stderr, "[CGI][WR] len=0 after calc → retry later\n");
        return;
    }
    if (len > CHUNK) len = CHUNK;

    char small[16384];
    std::memcpy(small, &tmp[0] + cgi_body_off_, len);
    std::fprintf(stderr, "[CGI][WR] mem chunk write attempt len=%zu\n", len);

    std::size_t sent = 0;
    while (sent < len) {
        ssize_t w = ::write(cgi_in_fd_, small + sent, len - sent);
        if (w > 0) {
            sent += static_cast<std::size_t>(w);
            cgi_body_off_ += static_cast<std::size_t>(w);
            std::fprintf(stderr, "[CGI][WR] wrote=%zd new_off=%zu/%zu\n",
                         w, cgi_body_off_, total);
            resetWriteDeadline();
            if (cgi_body_off_ >= total) {
                std::fprintf(stderr, "[CGI][WR] done sending body → closeStdin\n");
                closeStdin();
                return;
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::fprintf(stderr, "[CGI][WR] write EAGAIN after sent=%zu/%zu → retry later\n",
                             sent, len);
                return;
            }
            if (errno == EINTR) {
                std::fprintf(stderr, "[CGI][WR] write EINTR → continue\n");
                continue;
            }
            if (errno == EPIPE) {
                std::fprintf(stderr, "[CGI][WR] write EPIPE → closeStdin\n");
                closeStdin();
                return;
            }
            std::fprintf(stderr, "[CGI][WR] write error errno=%d (%s) → closeStdin\n",
                         errno, std::strerror(errno));
            closeStdin();
            return;
        }
    }
}


// --- timeout handling ---

void CGIStreamer::timeout504()
{
    closeStdin();
    closeStdout();
    proc_.terminate();
    cgi_active_ = false;
    failed_ = true;
}
