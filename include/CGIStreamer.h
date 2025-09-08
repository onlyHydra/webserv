#ifndef CGI_STREAMER_H
#define CGI_STREAMER_H

#include <string>
#include <vector>
#include <cstddef> // size_t

class ChainBuf;
class HttpRequest;
class HttpResponse;
class EventLoop;        // <-- forward declare the loop
struct CgiSpec;

#include "CgiProcess.h"
#include "PhaseDeadline.h"  // header/total runtime deadlines



// Streams data between the client socket and a spawned CGI process (non-blocking).
// - Reads child's stdout incrementally, parses the CGI header block once, then
//   serializes an HTTP/1.1 head (respecting Status, Content-Type, Set-Cookie)
//   and streams the CGI payload to the client as chunked transfer encoding.
// - Writes the request body (RAM or temp file) to child's stdin boundedly on POLLOUT.
// - Enforces per-phase timeouts (header phase & total runtime).
class CGIStreamer
{
public:
    // Construct bound to the current request/response
    CGIStreamer(HttpRequest &req, HttpResponse &res);
    ~CGIStreamer();

    // Attach the owning event loop (must be called before beginCgi()).
    void attachLoop(EventLoop* loop) { loop_ = loop; }

    // Start CGI (non-blocking). Returns false if spawn/pipe setup fails.
    bool beginCgi(const CgiSpec &spec,
                  const std::string &script_path,
                  const std::vector<std::string> &envv);

    // EventLoop dispatchers should call these when the corresponding fd is ready.
    void onCgiReadable(int fd);  // drain child's stdout → internal buffers (header/body)
    void onCgiWritable(int fd);  // feed request body → child's stdin (bounded write)

    // Child fds (−1 when closed)
    int  cgiStdoutFD() const { return cgi_out_fd_; }
    int  cgiStdinFD()  const { return cgi_in_fd_;  }

    // Is a CGI exchange in progress?
    bool isActive()     const { return cgi_active_; }

    // Did the CGI exchange fail (timeout/spawn/IO error)?
    bool failed()       const { return failed_; }

    // Is there buffered HTTP data ready for the client socket?
    bool hasOutBytes()  const { return out_off_ < out_buf_.size(); }

    // Back-pressure threshold for buffered (chunked) bytes waiting to be sent.
    static const std::size_t OUT_CAP_BYTES = 256u * 1024u;

    // True if we want more bytes from CGI stdout (respect back-pressure).
    bool wantsRead() const { return (out_buf_.size() - out_off_) < OUT_CAP_BYTES; }

    // Move up to max_bytes from internal buffer into the socket ChainBuf.
    std::size_t takeOutBytes(ChainBuf &out, std::size_t max_bytes);

    // Proactively close child's stdin (e.g., for GET/HEAD without a body)
    void closeStdin();

    // Called by the connection each tick to enforce timeouts and reaping.
    void onTick(unsigned long long now_ms);

    // Begin header-phase timer (3s by default).
    void startHeaderPhase(unsigned long long now_ms);

    // Hint from the connection: current client-socket queued bytes.
    void setClientWaterline(std::size_t bytesQueued) { client_out_bytes_ = bytesQueued; }
    void reset();    

    void pauseStdoutReads();
void resumeStdoutReads();

private:
    // ===== Header parsing helpers (implemented in .cpp) =====
    void parseOneHeaderLine(const std::string& line);
    void finalizeHeaders();

    // Append raw bytes to outgoing buffer for the client socket (chunked-aware)
    void enqueueOut(const char* data, std::size_t len);
    void enqueueFinalChunk();               // "0\r\n\r\n"

    // Reset stdin write deadline after progress
    void resetWriteDeadline();

    // Close child's stdout fd (used on HUP/timeout/error)
    void closeStdout();

    // Timeout handler: kill/reap and mark failure; owner should send 504
    void timeout504();

private:
    // ---- Bound request/response + process wrapper ----
    HttpRequest   &req_;
    HttpResponse  &res_;
    CgiProcess     proc_;

    // ---- Event loop (owner) ----
    EventLoop*          loop_;             // set via attachLoop(); may be NULL until wired

    // ---- CGI pipes / state ----
    bool                cgi_active_;       // true after beginCgi() until teardown
    bool                failed_;           // set on timeout/spawn/IO error
    int                 cgi_in_fd_;        // write end → child's stdin
    int                 cgi_out_fd_;       // read end  ← child's stdout/stderr

    // ---- Request-body streaming to child's stdin ----
    std::size_t         cgi_body_off_;     // bytes already written to child
    int                 body_fd_;          // fd of on-disk request body (or -1)
    std::size_t         body_file_off_;    // file read offset (if disk-backed)
    std::string         body_path_;        // temp body file path (if any)

    // ---- CGI header-phase parsing ----
    enum HdrState { HDR_WAITING, HDR_DONE, HDR_FAILED };
    HdrState            hdr_state_;        // starts as HDR_WAITING on begin
    std::string         cgi_header_accum_; // accumulates until blank line
    bool                saw_content_type_; // track if CGI sent Content-Type
    int                 status_code_;      // from "Status:" (optional; default 200)
    std::string         status_reason_;    // from "Status:" (optional)
    std::vector<std::string> set_cookie_;  // collect multiple Set-Cookie

    // ---- Output framing to client ----
    bool                http_head_queued_; // whether HTTP head has been queued
    bool                chunked_mode_;     // we stream as chunked
    bool                sent_final_chunk_; // whether "0\r\n\r\n" has been queued
    std::vector<char>   out_buf_;          // fully-formed HTTP bytes pending send
    std::size_t         out_off_;          // consumption offset within out_buf_

    // ---- Deadlines / timeouts ----
    PhaseDeadline       hdr_deadline_;     // ~3s to receive header
    PhaseDeadline       total_deadline_;   // ~15s total runtime
    unsigned long long  write_deadline_ms_;// stdin write progress watchdog (absolute ms)

    // Static timeout constants
    static const unsigned long long WRITE_TIMEOUT_MS = 5000;  // 5s without stdin progress

    // ---- Back-pressure hint from the connection ----
    std::size_t         client_out_bytes_; // last known queued bytes on client socket

    bool stdout_paused_; 
};

#endif // CGI_STREAMER_H
