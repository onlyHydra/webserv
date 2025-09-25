#if !defined(CLIENTCONNECTION_H)
#define CLIENTCONNECTION_H

#include <string>
#include <vector>       // for std::vector
#include <cstddef>      // for size_t
#include <sys/types.h>  // for off_t

#include "ConnectionIO.h"
#include "HeaderProcessor.h"
#include "RequestGuards.h"
#include "PhaseDeadline.h"
#include "CGIStreamer.h"
#include "BodyReader.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "ResponseFactory.h"
#include "RequestContext.h"
#include "RouteResolver.h"
#include "Phase.h"
#include "RoutePlan.h"
#include "ReturnHandler.h"
// #include "RequestDecoder.h"

class Server;

class ClientConnection
{
public:
    ClientConnection(int fd, Server *s, unsigned long long nowMs);
    ~ClientConnection();

    bool beginProxyTunnel(int upstream_fd,
                          const std::string& host,
                          const std::string& port,
                          int connect_timeout_ms,
                          int io_idle_timeout_ms,
                          const HttpRequest& req,
                          const std::string& target_path);
    bool beginProxyTunnel(int upstream_fd,
                          const std::string& host,
                          const std::string& port,
                          int connect_timeout_ms,
                          int io_idle_timeout_ms,
                          const HttpRequest& req);

    void onTick(unsigned long long now_ms);
    bool isClosed() const;
    bool wantsRead();
    bool isReadPaused();
    bool hasPendingWrite() const;
    int fd() const { return io.getFD(); }

    CGIStreamer &getCGIStreamer() { return cgi; }

    Phase getState() { return state; }
    FlowControl &getFlow() { return io.getFlow(); }
    void resetDeadline(int ms){ dl.reset(now_cached_ms, ms); };

    bool isReadyToClose() { return ready_to_close; }

    void close();

    int getFD() { return io.getFD(); }
    
    bool pumpCgiToSocket(std::size_t max_bytes = 128u * 1024u);
    void drainRingIntoBody();
    
private:
    struct ProxyState {
    bool active;
    bool connect_done;
    int  ufd;

    unsigned long long connect_deadline_ms;
    unsigned long long io_idle_deadline_ms;

    // Upstream host/port (as strings)
    std::string uh;  // upstream host
    std::string up;  // upstream port

    // Upstream send buffers
    std::string       to_upstream;  // request head
    std::size_t       to_up_off;

    // Body source selection
    enum { BODY_NONE, BODY_MEM, BODY_FILE } body_src;
    std::vector<char> body_mem;     // in-memory body buffer
    std::size_t       body_off;

    // File-backed body (when request body is on disk)
    std::string body_file; // path
    int         body_fd;   // read-only fd
    off_t       body_pos;  // current read offset

    // NEW: configured idle timeout window (ms)
    int io_idle_window_ms;

    ProxyState()
    : active(false)
    , connect_done(false)
    , ufd(-1)
    , connect_deadline_ms(0)
    , io_idle_deadline_ms(0)
    , uh()
    , up()
    , to_upstream()
    , to_up_off(0)
    , body_src(BODY_NONE)
    , body_mem()
    , body_off(0)
    , body_file()
    , body_fd(-1)
    , body_pos(0)
    , io_idle_window_ms(0)   // <- this now matches the declaration above
    {}
} proxy_;


    void parseHeaders();
    void selectRouteOnce();
    void runPreflight();
    void decideBodyReader();
    void decideBodyReader(std::size_t content_lenght);
    void readBody();
    void routeAndBuild();
    void finishWriteOrNext();
    void fail(int code, const char *reason);
    void serviceProxyTunnel();

    Phase state;
    Server *server;
    ConnectionIO io;
    HttpRequest req;
    HttpResponse res;
    IBodyReader *body;
    CGIStreamer cgi;
    PhaseDeadline dl;
    std::size_t hdr_bytes;
    std::size_t max_hdr_bytes;
    std::size_t max_body_bytes;
    bool should_close;
    bool route_selected;
    RoutePlan plan;
    int local_port;
    int vs_idx;

    RouteDecision *ctx;
    Preflight pr;

    std::size_t body_bytes_prev; // last observed body->bytes_received()
    int body_no_progress_ticks;
    int flush_no_progress_ticks;
    unsigned long long now_cached_ms;
    bool ready_to_close;
    std::size_t fixed_body_target_;  

    size_t        body_expected_;     // from Content-Length
    size_t        body_received_;     // progresses to body_expected_
    int           body_fd_;           // -1 if using RAM
    std::string   body_path_;         // temp file path if body_fd_ >= 0

    bool cgi_error_latched;

    static const size_t MEM_BODY_LIMIT = 32u * 1024u; // spill threshold

    enum
    {
        BODY_STALL_TICK_LIMIT = 30,
        FLUSH_STALL_TICK_LIMIT = 30
    };
};

#endif //  CLIENTCONNECTION_H
