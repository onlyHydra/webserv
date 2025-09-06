#ifndef CGI_STREAMER_H
#define CGI_STREAMER_H

#include <string>
#include <vector>
#include <cstddef> // size_t

class ChainBuf;
class HttpRequest;
class HttpResponse;
struct CgiSpec;

#include "CgiProcess.h"
#include "PhaseDeadline.h"  // for header/total runtime deadlines

// Streams data between the client and a spawned CGI process (non-blocking).
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

    // backpressure threshold for buffered (chunked) bytes waiting to be sent
    static const std::size_t OUT_CAP_BYTES = 256u * 1024u;

    bool wantsRead() const {
        return outBuffer.size() - outOffset < OUT_CAP_BYTES;
    }

    // Move up to max_bytes from internal buffer into the socket ChainBuf.
    // Returns number of bytes appended.
    std::size_t takeOutBytes(ChainBuf &out, std::size_t max_bytes);

    // Proactively close child's stdin (e.g., for GET/HEAD without a body)
    void closeStdin();

    // Called by the connection each tick to enforce timeouts.
    void onTick(unsigned long long now_ms);

    // Call right after a CGI process is successfully started to start header timers.
    void startHeaderPhase(unsigned long long now_ms);

private:
    // ---- Header parsing helpers (implemented in .cpp) ----
    // Returns true when headers complete (and trims them from cgi_buf).
    bool parseCgiHeaders(std::string &buf, int &status, long &content_len);
    void parseOneHeaderLine(const std::string& line);
    void finalizeHeaders();   // map parsed CGI headers → HttpResponse and queue HTTP head

    // Reset internal write deadline after progress to child's stdin
    void resetDeadlineForWrite();

    // Close child's stdout fd (used on HUP/timeout/error)
    void closeStdout();

    // Append bytes to the outgoing buffer for the client socket
    // void enqueueOut(const char* data, std::size_t n);

    // Kill/reap and synthesize 504 if needed
    void timeout504();

    


private:
    // ---- Bound request/response + process wrapper ----
    HttpRequest   &req;
    HttpResponse  &res;
    CgiProcess     proc;

    // ---- CGI pipes / state ----
    bool                cgi_active;        // true after beginCgi() until teardown
    int                 cgi_in_fd;         // write end → child's stdin
    int                 cgi_out_fd;        // read end  ← child's stdout/stderr
    std::size_t         cgi_body_off;      // bytes of in-memory body already sent

    // ---- Raw bytes read from CGI before header split ----
    // (Placed here to match common initializer lists and avoid -Wreorder)
    std::string         cgi_buf;           // buffer (headers + body)

    // ---- Request-body streaming to child's stdin ----
    int                 body_fd_;          // fd of on-disk request body (or -1)
    std::size_t         body_off_;         // read offset within body file
    std::string         body_path_;        // temp body file path (if any)

    // ---- Legacy parsed fields ----
    bool                cgi_headers_done;  // set once HTTP head is built
    int                 cgi_status;        // from "Status:"; default 200
    long                cgi_content_len;   // from "Content-Length:"; -1 if unknown

    // ---- Deadline used for stdin write progress / overall tick ----
    // (Declared before chunked_mode to match ctor initializer order)
    unsigned long long  cgi_deadline;      // write/overall deadline ticks

    // ---- Output framing to client ----
    bool                chunked_mode;      // if streaming to client as chunked
    bool                sent_final_chunk;  // whether "0\r\n\r\n" has been sent

    bool        cgi_preface_stripped_;
    std::string cgi_preface_buf_;

    // ---- Fully-formed HTTP bytes queued to send to the client socket ----
    std::vector<char>   outBuffer;
    std::size_t         outOffset;

 

    // ---- Minimal state flag (kept for compatibility) ----
    enum State { READ, WRITE };
    State               state;

    // ========= Header-phase parsing + timeouts =========
    enum HdrState { HDR_WAITING, HDR_DONE, HDR_FAILED };
    HdrState            hdr_state_;         // start as HDR_WAITING when CGI begins
    std::string         cgi_header_accum_;  // accumulates until blank line
    bool                saw_content_type_;  // track if CGI sent Content-Type
    int                 status_code_;       // from "Status:" (optional)
    std::string         status_reason_;     // from "Status:" (optional)
    std::vector<std::string> set_cookie_;   // collect multiple Set-Cookie

    // Per-phase timers
    PhaseDeadline       hdr_deadline_;      // e.g., 10s to receive header
    PhaseDeadline       total_deadline_;    // e.g., 120s max total runtime

    // Static timeout constants (keep in header for visibility)
    static const unsigned long long WRITE_TIMEOUT_MS = 5000;  // 5s

    void enqueueOut(const char* data, std::size_t len);

};

#endif // CGI_STREAMER_H
