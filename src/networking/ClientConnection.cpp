#include "ClientConnection.h"
#include "ExpectContinue.h"
#include "RequestContext.h"
#include "ResponseFactory.h"
#include "ChunkedReader.h"
#include "CGIStreamer.h"
#include "FileBodyReader.h"
#include "ContentLenghtReader.h"
#include "ServerPipeline.h"
#include "Server.h"
#include <sstream>
#include <netinet/in.h>
#include <unistd.h>   // mkstemp, write
#include <stdlib.h> 
#include <limits>   // for std::numeric_limits
#include <cstdio>

static const std::size_t INMEM_BODY_LIMIT = 256 * 1024;


ClientConnection::ClientConnection(int fd, Server* s, unsigned long long nowMs)
    : state(PH_READ_HEADERS),
      server(s),
      io(fd, 64 * 1024),
      req(),
      res(),
      body(0),
      cgi(req, res),
      dl(),
      hdr_bytes(0),
      max_hdr_bytes(64 * 1024),
      max_body_bytes(0),
      should_close(false),
      route_selected(false),
      plan(),
      pr(),
      body_bytes_prev(0),
      body_no_progress_ticks(0),
      flush_no_progress_ticks(0),
      now_cached_ms(nowMs),
      ready_to_close(false),
      fixed_body_target_((std::size_t)-1)   // <— add this


{
     // Wire the loop once
    cgi.attachLoop(&server->getLoop()); 

	ctx = new RouteDecision;
	resetDeadline(HDR_TIMEOUT_MS);
	struct sockaddr_storage ss;
	socklen_t sl = sizeof(ss);
	local_port = -1;
	if (getsockname(fd, (struct sockaddr *)&ss, &sl) == 0)
	{
		if (ss.ss_family == AF_INET)
		{
			local_port = (int)ntohs(((sockaddr_in *)&ss)->sin_port);
		}
		else if (ss.ss_family == AF_INET6)
		{
			local_port = (int)ntohs(((sockaddr_in6 *)&ss)->sin6_port);
		}
	}
}

void ClientConnection::drainRingIntoBody()
{
    IoRing &ring = io.getInputRing();

    while (true) {
        std::size_t avail = ring.readAvail();
        if (avail == 0) break;

        const char *p = ring.readPtr();
        if (!p) break;  // nothing contiguous to read right now

        std::size_t take = avail;

       
        if (body_fd_ >= 0) {
            std::size_t off = 0;
            while (off < take) {
                ssize_t w = ::write(body_fd_, p + off, take - off);
                if (w > 0) {
                    off += static_cast<std::size_t>(w);
                } else if (w < 0 && errno == EINTR) {
                    continue;
                } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // Unlikely for regular files; stop this tick.
                    break;
                } else {
                    // Hard error writing body to disk.
                    fail(500, "Failed to buffer request body");
                    return;
                }
            }
            take = off; // actually consumed from ring
        } else {
            // RAM path (optional): e.g. body_vec_.insert(body_vec_.end(), p, p + take);
        }

        body_received_ += take;
        ring.consumed(take);  // NOTE: 'consumed', not 'consume'

        // If we know the expected length, stop when done
        if (body_expected_ != static_cast<std::size_t>(-1) &&
            body_received_ >= body_expected_) {
            break;
        }
    }
}


ClientConnection::~ClientConnection()
{
	if (server)
		server = 0;
	if (ctx)
	{
		delete ctx;
		ctx = 0;
	}
	if (body)
	{
		delete body;
		body = 0;
	}
	req.cleanupBodyFile();
}

bool ClientConnection::isClosed() const { return state == PH_CLOSE; }

bool ClientConnection::hasPendingWrite() const {
    return io.getChainBuf().getByteSize() > 0 || cgi.hasOutBytes();
}

bool ClientConnection::isReadPaused() { return io.getFlow().isReadPaused(); }
void ClientConnection::close()
{
	server->releaseConnection(this);
}


bool ClientConnection::pumpCgiToSocket(std::size_t max_bytes)
{
    bool progressed = false;

    // 1) Pull from CGI → connection out buffer (chunked framing happens in CGIStreamer)
    if (cgi.isActive()) {
        const std::size_t pulled = cgi.takeOutBytes(io.getChainBuf(), max_bytes);
        if (pulled > 0) progressed = true;
    }

    // 2) Try to write whatever we have to the client socket
    if (io.getChainBuf().getByteSize() > 0) {
        const ssize_t n = io.nb_write();  // non-blocking send
        if (n > 0) progressed = true;
        // Do NOT branch on errno here per project rules if n <= 0
    }

    // 3) If progress was made, refresh the write deadline (if you track one)
    if (progressed) {
        // keep the connection’s write phase alive
        state = PH_WRITE;
        // only call this if you have such a helper:
        resetDeadline(WR_TIMEOUT_MS);
    }

    // 4) Finalize: only when CGI is done AND socket buffer is empty
    if (!cgi.isActive() && io.getChainBuf().getByteSize() == 0) {
        if (should_close) {
            state = PH_CLOSE;
        } else {
            // Ready for the next request on this keep-alive connection
            // Reinitialize request/response to defaults
            req = HttpRequest();
            res = HttpResponse();

            state = PH_READ_HEADERS; // wait for the next request
        }
    }

    return progressed || cgi.isActive() || (io.getChainBuf().getByteSize() > 0);
}





bool ClientConnection::wantsRead()
{
	// We want read in header/body phases; can also allow lingering read in PH_CLOSE if you implement it
	return state == PH_READ_HEADERS || state == PH_ROUTE_SELECT || state == PH_PRECHECK || state == PH_READ_BODY;
}

void ClientConnection::onTick(unsigned long long now_ms)
{
    now_cached_ms = now_ms;

    // Let CGI enforce its own deadlines (header + total runtime)
    cgi.onTick(now_ms);
    if (!cgi.isActive() && cgi.failed() && !cgi_error_latched) {
        fail(504, "Gateway Timeout");
        cgi_error_latched = true;
        state = PH_WRITE;
        (void)io.nb_write();
        resetDeadline(WR_TIMEOUT_MS);
        return;
    }

    // 0) Pull any CGI bytes into the socket buffer BEFORE we attempt a write.
    if (cgi.hasOutBytes())
        (void)cgi.takeOutBytes(io.getChainBuf(), 128u * 1024u);

    // 1) Flow control based on queued output
    const std::size_t outbytes = io.getChainBuf().getByteSize();
    cgi.setClientWaterline(outbytes);

    // and resume reading as soon as the socket drains enough.
    const std::size_t HIGH_WATER = 192u * 1024u;  // pause when >= 192 KiB queued
    const std::size_t LOW_WATER  = 64u  * 1024u;  // resume when <= 64 KiB queued

	if (cgi.isActive()) {
		if (outbytes >= HIGH_WATER) {
			std::fprintf(stderr, "[CGI][BP] pause stdout (queued=%zu)\n", outbytes);
			cgi.pauseStdoutReads();
		} else if (outbytes <= LOW_WATER) {
			std::fprintf(stderr, "[CGI][BP] resume stdout (queued=%zu)\n", outbytes);
			cgi.resumeStdoutReads();
		}
	}

    // -------------------------------------------------------------------------

    if (io.getFlow().shouldPauseRead(outbytes))
        io.getFlow().setReadPaused(true);
    if (io.getFlow().shouldResumeRead(outbytes))
        io.getFlow().setReadPaused(false);

    // 2) Try to flush whatever is queued (static or CGI)
    (void)io.nb_write();

    // After flushing, we may have dropped below LOW_WATER; re-check to resume quickly.
    if (cgi.isActive()) {
        const std::size_t out_after = io.getChainBuf().getByteSize();
        if (out_after <= LOW_WATER) cgi.resumeStdoutReads();
    }

    // 3) Deadline enforcement
    if (dl.expired(now_ms) && state != PH_CLOSE) {
        if (state == PH_READ_HEADERS || state == PH_READ_BODY ||
            state == PH_PRECHECK     || state == PH_ROUTE_SELECT)
        {
            fail(408, "Request Timeout");
        } else {
            state = PH_CLOSE;
        }
        return;
    }

    // 4) Non-blocking read only in header/body phases and when not paused
    if (!io.getFlow().isReadPaused() &&
        (state == PH_READ_HEADERS || state == PH_READ_BODY))
    {
        const std::size_t kAllYouCanEat = std::numeric_limits<std::size_t>::max();
        ssize_t r = io.nb_read(kAllYouCanEat);
        if (r == 0) { state = PH_CLOSE; return; }
        // r < 0 => EAGAIN/other; ignore here.
    }

    // 5) Drive the HTTP/CGI state machine
    switch (state)
    {
    case PH_READ_HEADERS:  parseHeaders();   break;
    case PH_ROUTE_SELECT:  selectRouteOnce();break;
    case PH_PRECHECK:      runPreflight();   break;
    case PH_READ_BODY:     readBody();       break;
    case PH_ROUTE:         routeAndBuild();  break;
    case PH_WRITE:         finishWriteOrNext(); break;
    case PH_CLOSE:
        io.getFlow().setReadPaused(true);
        (void)io.nb_write();
        if (io.getChainBuf().getByteSize() == 0) {
            if (body) { delete body; body = 0; }
            req.cleanupBodyFile();
            ready_to_close = true;
        }
        return;
    }
}



 bool pumpCgiToSocket(std::size_t max_bytes = 128u * 1024u);

static const unsigned long long READ_TIMEOUT_MS = 15000;

// (same helpers as before if you still need them)
static inline bool ci_equal(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = char(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = char(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}
static inline long long parse_content_length(const std::string &s) {
    if (s.empty()) return -1;
    long long v = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c < '0' || c > '9') return -1;
        v = v * 10 + (c - '0');
        if (v < 0) return -1;
    }
    return v;
}

// Feed bytes from ring into your HttpRequest parser until it reports headers done
void ClientConnection::parseHeaders()
{
    IoRing &ring = io.getInputRing();

    for (;;) {
        const char *p = ring.readPtr();
        std::size_t n = ring.readAvail();
        if (!p || n == 0) break;

        const char *end = 0;

        // Look for "\r\n\r\n"
        for (std::size_t i = 0; !end && i + 3 < n; ++i) {
            if (p[i] == '\r' && p[i+1] == '\n' && p[i+2] == '\r' && p[i+3] == '\n')
                end = p + i + 4;
        }
        // Or "\n\n"
        if (!end) {
            for (std::size_t i = 0; !end && i + 1 < n; ++i) {
                if (p[i] == '\n' && p[i+1] == '\n')
                    end = p + i + 2;
            }
        }

        std::size_t to_feed;
        if (end) {
            // Found a complete header block
            to_feed = static_cast<std::size_t>(end - p);
        } else {
            // Not yet complete → consume all but the last 3 bytes
            to_feed = (n > 3) ? (n - 3) : 0;
            if (to_feed == 0) break; // need more data
        }

        if (hdr_bytes + to_feed > max_hdr_bytes) {
            fail(431, "Request Header Fields Too Large");
            return;
        }

        if (!req.parse(p, to_feed)) {
            fail(400, "Bad Request");
            return;
        }

        ring.consumed(to_feed);
        hdr_bytes += to_feed;
        resetDeadline(HDR_TIMEOUT_MS);

        if (end) {
            route_selected = false;
            state = PH_ROUTE_SELECT;
            resetDeadline(BODY_TIMEOUT_MS);
            return;
        }
        // else: continue the loop
    }

    resetDeadline(HDR_TIMEOUT_MS);
}


void ClientConnection::selectRouteOnce()
{
	if (route_selected)
	{
		state = PH_PRECHECK;
		return;
	}

	plan.needs_body = false;
	plan.max_body_bytes = 0;
	// replace 'localhost' with HttpRequest actual domainName
	vs_idx = server->resolveVirtualServerByPort(local_port, "localhost");

	pr = RequestGuards::preflight(server->getConfig(),
								  vs_idx,
								  req.getMethod(),
								  req.getPath(),
								  req.getHeaders(),
								  ctx);
	route_selected = true;
	state = PH_PRECHECK;
	resetDeadline(BODY_TIMEOUT_MS);
}

void ClientConnection::runPreflight()
{
    const Headers &H = req.getHeaders();

    HeaderCheck hc = HeaderProcessor::analyze(req, H, max_body_bytes);
    if (!hc.ok) { fail(hc.error_status, 0); return; }

    if (!pr.ok) { fail(pr.reject_status ? pr.reject_status : 400, pr.reject_reason.c_str()); return; }

    max_body_bytes = pr.max_body_bytes;

    if (hc.expect_continue && ExpectContinue::needed(req.getHeaders())) {
        ExpectContinue::write100(io.getChainBuf());
    }

    if (!pr.needs_body) {
        state = PH_ROUTE;
        return;
    }

    if (hc.chunked) {
        decideBodyReader(/* chunked */);
    } else if (hc.content_length > 0) {
        if (max_body_bytes && hc.content_length > max_body_bytes) {
            fail(413, "Payload Too Large");
            return;
        }
        decideBodyReader(hc.content_length);
    } else {
        fail(411, "Length Required");
        return;
    }

    // ---- INSERT THIS SAFETY NET *after* decideBodyReader(...) ----
    if (body) {
        std::string tail = req.takeBuffer();   // may contain body bytes that arrived with headers
        if (!tail.empty())
            (void)body->consume(tail.data(), tail.size());
    }
    // --------------------------------------------------------------

    body_bytes_prev         = 0;
    body_no_progress_ticks  = 0;
    flush_no_progress_ticks = 0;

    state = PH_READ_BODY;
    resetDeadline(BODY_TIMEOUT_MS);
}



// --- in ClientConnection.cpp ---
void ClientConnection::readBody()
{
    if (!body) { fail(400, "Bad Request"); return; }

    IoRing &ring = io.getInputRing();

    // Progress baseline
    const std::size_t before_bytes = body->bytes_received();

    // 1) Drain as much as the reader will accept
    for (;;) {
        const char *buf   = ring.readPtr();
        std::size_t avail = ring.readAvail();
        if (!buf || avail == 0) break;

        const std::size_t used = body->consume(buf, avail);
        if (used == 0) break;              // reader can’t take more right now

        ring.consumed(used);
        resetDeadline(BODY_TIMEOUT_MS);
    }

    const std::size_t after_bytes = body->bytes_received();

    // 2) Enforce total body cap (post-read, on de-chunked length)
    if (max_body_bytes && after_bytes > max_body_bytes) {
        fail(413, "Payload Too Large");
        return;
    }

    // 3) Watchdogs: read progress + staging flush progress
    if (after_bytes == before_bytes && ring.readAvail() == 0)
        ++body_no_progress_ticks;
    else
        body_no_progress_ticks = 0;

    std::size_t pending_before = 0, flushed = 0;
    if (FileBodyReader *fr = dynamic_cast<FileBodyReader *>(body)) {
        pending_before = fr->pending_size();
        if (pending_before) flushed = fr->flush_to_disk(64 * 1024);
    } else if (ChunkedReader *cr = dynamic_cast<ChunkedReader *>(body)) {
        pending_before = cr->pending_size();
        if (pending_before) flushed = cr->flush_to_disk(64 * 1024);
    }

    if (pending_before > 0 && flushed == 0) ++flush_no_progress_ticks;
    else                                    flush_no_progress_ticks = 0;

    if (body_no_progress_ticks  >= BODY_STALL_TICK_LIMIT) { fail(408, "Request Timeout");      return; }
    if (flush_no_progress_ticks >= FLUSH_STALL_TICK_LIMIT){ fail(507, "Insufficient Storage"); return; }

    // 4) Completion check
    bool done = false;
    if (fixed_body_target_ != (std::size_t)-1) {
        if (body->bytes_received() >= fixed_body_target_) {
            if (FileBodyReader *fr2 = dynamic_cast<FileBodyReader *>(body))
                done = (fr2->pending_size() == 0);
            else
                done = true;
        }
    } else {
        if (ChunkedReader *cr2 = dynamic_cast<ChunkedReader *>(body))
            done = cr2->complete();
    }

    if (!done) return;

    // 5) Body complete → advance pipeline
    state = PH_ROUTE;                 // or PH_ROUTE_SELECT if that’s your next step
    resetDeadline(WR_TIMEOUT_MS);
}

void ClientConnection::routeAndBuild()
{
    // Let the pipeline decide and (possibly) start CGI.
    const bool done = ServerPipeline::processRequest(
        server->getConfig(),
        vs_idx,
        req,
        res,
        *ctx,
        &cgi
    );

    // One keep-alive decision used by both paths
    const std::string connHdr = req.getHeaders().get("Connection");
    should_close =
        (req.getHttpVer() == "HTTP/1.0") ||
        (connHdr == "close" || connHdr == "Close");

    if (!done) {
      
        state = PH_WRITE;
        resetDeadline(WR_TIMEOUT_MS);
        return;
    }

    // -------- Synchronous response (static/proxy/put/patch) --------
    // Reflect the keep-alive policy.
    if (should_close)
        res.headers.set("Connection", "close");
    else
        res.headers.set("Connection", "keep-alive");

    // Safe here: will add Content-Length/Date/Server if missing.
    res.ensureDefaultHeaders();

    // Serialize and queue.
    std::ostringstream os;
    os << res;
    const std::string s = os.str();
    io.getChainBuf().push_copy(s.data(), s.size());

	state = PH_WRITE;
	resetDeadline(WR_TIMEOUT_MS);
	state = PH_WRITE;
	resetDeadline(WR_TIMEOUT_MS);
}

void ClientConnection::finishWriteOrNext()
{
    if (io.getChainBuf().getByteSize() != 0)
        return;

    if (cgi.isActive() || cgi.cgiStdoutFD() >= 0 || cgi.hasOutBytes())
        return;

    if (should_close) { state = PH_CLOSE; return; }

    // reset for next request…
    hdr_bytes = 0;
    max_body_bytes = 0;
    if (body) { delete body; body = 0; }
    plan = RoutePlan();
    ctx->reset();
    req = HttpRequest();
    res = HttpResponse();
    route_selected = false;

    fixed_body_target_ = (std::size_t)-1;   // <— reset here too
    // in ClientConnection::finishWriteOrNext(), before switching back to PH_READ_HEADERS
    cgi_error_latched = false;

    state = PH_READ_HEADERS;
    resetDeadline(IDLE_TIMEOUT_MS);
    if (io.getInputRing().readAvail() > 0) parseHeaders();
}


void ClientConnection::fail(int code, const char* reason)
{
    res = ResponseFactory::makeText(code, "", reason ? reason : "", true);

    std::ostringstream os;
    os << res;
    const std::string s = os.str();
    io.getChainBuf().push_copy(s.data(), s.size());

    should_close = true;
    state = PH_WRITE;
    resetDeadline(WR_TIMEOUT_MS);

    fixed_body_target_ = (std::size_t)-1;   // <— optional reset

    finishWriteOrNext();
}


void ClientConnection::decideBodyReader()  // chunked
{
    if (body) { delete body; body = 0; }
    body = new ChunkedReader(io.getTmp(), plan.max_body_bytes,
                             server->getConfig().servers()[vs_idx].client_body_temp_path);
    fixed_body_target_ = (std::size_t)-1;   // unknown/chunked
}

void ClientConnection::decideBodyReader(std::size_t content_length)
{
    if (body) { delete body; body = 0; }

    const std::size_t cap = (max_body_bytes != 0) ? max_body_bytes : static_cast<std::size_t>(~0);

    fixed_body_target_ = content_length;    // <— set it right here

    if (content_length <= INMEM_BODY_LIMIT && content_length <= cap) {
        body = new ContentLenghtReader(content_length, req);
        return;
    }

    const std::string tmpdir = server->getConfig().servers()[vs_idx].client_body_temp_path;
    FileBodyReader* fr = new FileBodyReader(tmpdir);
    if (!fr->ensure_open()) {
        delete fr;
        fail(507, "Insufficient Storage");
        return;
    }
    req.enableBodyOnDisk(fr->get_path());
    body = fr;
}



