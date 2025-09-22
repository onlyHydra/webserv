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
#include <unistd.h> // mkstemp, write
#include <stdlib.h>
#include <limits> // for std::numeric_limits
#include <cstdio>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include <iostream>
#include <sstream>
#include "ChunkedReader.h"   // for dynamic_cast to ChunkedReader
#include <iostream>          // for std::cerr (debug)







static const std::size_t INMEM_BODY_LIMIT = 256 * 1024;

/*

ClientConnection::ClientConnection(int fd, Server s, unsigned long long nowMs)*

Initializes a per-client state machine: records server pointer, wraps socket in ConnectionIO,
constructs request/response objects, wires a CGIStreamer to the server’s EventLoop,
allocates a RouteDecision, and sets initial deadlines for header parsing.
It also discovers the local port via getsockname for virtual-server selection.
Default caps (header/body), keep-alive flags, progress counters, and a special
fixed_body_target_ sentinel are initialized. This constructor ensures the connection
starts in PH_READ_HEADERS, fully ready for the non-blocking pipeline, with all
invariants defined so later phases can safely assume consistent, initialized state.

*/

ClientConnection::ClientConnection(int fd, Server *s, unsigned long long nowMs)
	: state(PH_READ_HEADERS),
	  server(s),
	  io(fd, 64 * 1024),
	  req(),
	  res(req),
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
	  fixed_body_target_((std::size_t)-1) // <— add this

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




/*

void ClientConnection::drainRingIntoBody()

Moves bytes already read into the input ring into the request body sink
(RAM or temp-file). If a temp file is active, it writes in a loop handling
EINTR and unlikely regular-file EAGAIN; on hard error, it fails the request.
Updates body_received_, advances the ring with consumed, and stops when the known length is satisfied.
This helper is useful when body bytes arrived together with headers or when scheduling prefers staged draining.
Keeping this logic separate avoids mixing ring manipulation with parser code and ensures the server
honors Content-Length precisely without blocking.


*/

void ClientConnection::drainRingIntoBody()
{
	IoRing &ring = io.getInputRing();

	while (true)
	{
		std::size_t avail = ring.readAvail();
		if (avail == 0)
			break;

		const char *p = ring.readPtr();
		if (!p)
			break; // nothing contiguous to read right now

		std::size_t take = avail;

		if (body_fd_ >= 0)
		{
			std::size_t off = 0;
			while (off < take)
			{
				ssize_t w = ::write(body_fd_, p + off, take - off);
				if (w > 0)
				{
					off += static_cast<std::size_t>(w);
				}
				else if (w < 0 && errno == EINTR)
				{
					continue;
				}
				else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				{
					// Unlikely for regular files; stop this tick.
					break;
				}
				else
				{
					// Hard error writing body to disk.
					fail(500, "Failed to buffer request body");
					return;
				}
			}
			take = off; // actually consumed from ring
		}
		else
		{
			// RAM path (optional): e.g. body_vec_.insert(body_vec_.end(), p, p + take);
		}

		body_received_ += take;
		ring.consumed(take); // NOTE: 'consumed', not 'consume'

		// If we know the expected length, stop when done
		if (body_expected_ != static_cast<std::size_t>(-1) &&
			body_received_ >= body_expected_)
		{
			break;
		}
	}
}

/*

ClientConnection::~ClientConnection()

Cleans up per-connection resources: deletes the routing context and body reader if allocated,
resets server pointer, and asks the HttpRequest to remove any temporary body file.
Destruction is designed to be safe from any state, including error or partially handled requests,
guaranteeing file descriptors and temporary files are not leaked.
Because connections may terminate from many paths (timeouts, client closes, errors),
consolidating cleanup here enforces RAII discipline and keeps the event-driven loop robust under stress,
preventing gradual resource loss across many concurrent clients.


*/

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

// small helper

/*


static int get_so_error(int fd)

Queries SO_ERROR with getsockopt, returning the pending error (or errno if getsockopt fails).
Used to complete a non-blocking connect() during reverse-proxy setup. Rather than branching on connect’s immediate result,
the code defers connection success/failure checks to this helper—aligned with poll-driven architectures.
It’s crucial for correctness because non-blocking connects typically yield EINPROGRESS; the socket becomes writable later,
and SO_ERROR indicates final outcome. Centralizing this avoids duplicated boilerplate and
keeps connection completion logic straightforward inside the proxy tunneling pump.

*/

static int get_so_error(int fd)
{
	int err = 0;
	socklen_t len = sizeof(err);
	if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
		return errno;
	return err;
}

/*

bool ClientConnection::beginProxyTunnel(…, const HttpRequest& req) (no target path)

Convenience overload that delegates to the full version, passing an empty target path so the
function derives it from the request (path plus optional query). This keeps handler code simple
when the upstream target equals the client’s path. Introducing a small wrapper avoids duplicating derivation logic,
ensures consistent header construction for upstream requests, and preserves a single place for timeout and buffer initialization.
It returns true to indicate the tunnel has been staged and will be driven asynchronously by the per-tick pump (serviceProxyTunnel).


*/

bool ClientConnection::beginProxyTunnel(int upstream_fd,
										const std::string &host,
										const std::string &port,
										int connect_timeout_ms,
										int io_idle_timeout_ms,
										const HttpRequest &req)
{
	// Delegate to the overload that accepts a target path; empty means derive from req.
	return beginProxyTunnel(upstream_fd, host, port,
							connect_timeout_ms, io_idle_timeout_ms,
							req, std::string());
}

/*

bool ClientConnection::beginProxyTunnel(…, const HttpRequest& req, const std::string& target_path)

Initializes reverse-proxy state: marks proxy active, sets connect and idle deadlines, stores upstream fd/host/port,
and builds the upstream HTTP/1.1 request head (request line, Host, and a pass-through allowlist of safe headers).
Copies any already-buffered client body into a memory buffer for forwarding. It deliberately requests Connection:
close upstream for simplicity. No blocking occurs here; writing/reading is deferred to the main tick.
Returning true means the tunnel will progress via serviceProxyTunnel, which handles connect completion,
body forwarding, response streaming, backpressure, and timeouts.

*/

bool ClientConnection::beginProxyTunnel(int upstream_fd,
										const std::string &host,
										const std::string &port,
										int connect_timeout_ms,
										int io_idle_timeout_ms,
										const HttpRequest &req,
										const std::string &target_path)
{
	if (proxy_.active)
		return false; // already proxying this connection

	proxy_.active = true;
	proxy_.connect_done = false;
	proxy_.ufd = upstream_fd;
	proxy_.uh = host;
	proxy_.up = port;

	proxy_.connect_deadline_ms = now_cached_ms + (unsigned long long)connect_timeout_ms;
	proxy_.io_idle_deadline_ms = now_cached_ms + (unsigned long long)io_idle_timeout_ms;

	// ---- Build upstream request head -------------------------------------------------
	std::string head;
	head.reserve(1024);

	// Request line: METHOD SP target SP HTTP/1.1 CRLF
	head += req.getMethod();
	head += " ";

	// Target path: use override if provided, else derive from req (path[?query])
	std::string target;
	if (!target_path.empty())
	{
		target = target_path;
	}
	else
	{
		target = req.getPath();
		const std::string q = req.getQuery();
		if (!q.empty())
		{
			target += "?";
			target += q;
		}
	}
	head += target;

	head += " ";
	head += req.getHttpVer(); // e.g., "HTTP/1.1"
	head += "\r\n";

	// Headers: use your public Headers API; no iteration support, so copy a small allowlist
	const Headers &H = req.getHeaders();

	// Host
	if (!H.keyExists("Host"))
	{
		head += "Host: ";
		head += proxy_.uh;
		if (!proxy_.up.empty() && proxy_.up != "80")
		{
			head += ":";
			head += proxy_.up;
		}
		head += "\r\n";
	}
	else
	{
		const std::string hv = H.get("Host");
		if (!hv.empty())
		{
			head += "Host: ";
			head += hv;
			head += "\r\n";
		}
	}

	// Pass-through a few safe headers if present (no hop-by-hop here)
	const char *pass_list[] = {
		"Content-Type",
		"Content-Length",
		"User-Agent",
		"Accept",
		"Accept-Encoding",
		"Accept-Language",
		"Cookie"};
	for (size_t i = 0; i < sizeof(pass_list) / sizeof(pass_list[0]); ++i)
	{
		const std::string v = H.get(pass_list[i]);
		if (!v.empty())
		{
			head += pass_list[i];
			head += ": ";
			head += v;
			head += "\r\n";
		}
	}

	// Simplify lifecycle upstream (no keep-alive upstream for first cut)
	head += "Connection: close\r\n";
	head += "\r\n";

	// Save head into upstream-send buffer
	proxy_.to_upstream = head;
	proxy_.to_up_off = 0;

	// Optional: include already-buffered body (if any) — small bodies only
	std::vector<char> mem = req.readBodyToVector();
	if (!mem.empty())
	{
		proxy_.body_mem.swap(mem);
		proxy_.body_off = 0;
	}
	else
	{
		proxy_.body_off = 0;
	}

	// serviceProxyTunnel() will be called from onTick() to drive connect/IO
	return true;
}

/*

void ClientConnection::serviceProxyTunnel()

Drives the upstream tunnel each tick.
First completes the non-blocking connect by checking SO_ERROR and enforcing a connect
deadline. Then writes the staged upstream request head and any memory-buffered body in bounded
loops handling EAGAIN/EINTR. Next, it reads upstream response bytes into the client’s outbound ChainBuf,
closing and marking should_close when upstream closes.
Implements output-based backpressure (pause/resume reads) and an IO idle timeout: if no progress by deadline,
fails with 504. Importantly, this path must be integrated under the single poll loop to satisfy the project’s
“one poll for all I/O” rule.

*/

// Call from your per-connection tick (where you already pump IO)
void ClientConnection::serviceProxyTunnel()
{
	if (!proxy_.active)
		return;

	bool progressed = false;

	// 1) finish non-blocking connect
	if (!proxy_.connect_done)
	{
		int err = get_so_error(proxy_.ufd);
		if (err == 0)
		{
			proxy_.connect_done = true;
		}
		else if (now_cached_ms >= proxy_.connect_deadline_ms)
		{
			fail(504, "Gateway Timeout");
			::close(proxy_.ufd);
			proxy_.ufd = -1;
			proxy_.active = false;
			return;
		} // else: still connecting
	}

	// 2) write pending request head/body to upstream
	if (proxy_.connect_done && proxy_.ufd >= 0)
	{
		// write head
		while (proxy_.to_up_off < proxy_.to_upstream.size())
		{
			const char *p = proxy_.to_upstream.data() + proxy_.to_up_off;
			size_t nleft = proxy_.to_upstream.size() - proxy_.to_up_off;
			ssize_t n = ::write(proxy_.ufd, p, (int)nleft);
			if (n > 0)
			{
				proxy_.to_up_off += (size_t)n;
				progressed = true;
			}
			else
			{
				if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
					break;
				if (n < 0 && errno == EINTR)
					continue;
				fail(502, "Bad Gateway");
				::close(proxy_.ufd);
				proxy_.ufd = -1;
				proxy_.active = false;
				return;
			}
		}
		// write body (if any)
		while (proxy_.to_up_off >= proxy_.to_upstream.size() &&
			   proxy_.body_off < proxy_.body_mem.size())
		{
			const char *p = &proxy_.body_mem[0] + proxy_.body_off;
			size_t nleft = proxy_.body_mem.size() - proxy_.body_off;
			ssize_t n = ::write(proxy_.ufd, p, (int)nleft);
			if (n > 0)
			{
				proxy_.body_off += (size_t)n;
				progressed = true;
			}
			else
			{
				if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
					break;
				if (n < 0 && errno == EINTR)
					continue;
				fail(502, "Bad Gateway");
				::close(proxy_.ufd);
				proxy_.ufd = -1;
				proxy_.active = false;
				return;
			}
		}
	}

	// 3) read from upstream → queue to client outbound buffer
	if (proxy_.ufd >= 0)
	{
		std::vector<char> &tmp = io.getTmp();
		if (tmp.size() < 64 * 1024)
			tmp.resize(64 * 1024);

		for (;;)
		{
			ssize_t r = ::read(proxy_.ufd, &tmp[0], (int)tmp.size());
			if (r > 0)
			{
				io.getChainBuf().push_copy(&tmp[0], (size_t)r);
				progressed = true;
			}
			else if (r == 0)
			{
				::close(proxy_.ufd);
				proxy_.ufd = -1;
				proxy_.active = false;
				should_close = true; // simplest: close after upstream closes
				break;
			}
			else
			{
				if (errno == EWOULDBLOCK || errno == EAGAIN)
					break;
				if (errno == EINTR)
					continue;
				fail(502, "Bad Gateway");
				::close(proxy_.ufd);
				proxy_.ufd = -1;
				proxy_.active = false;
				return;
			}
		}
	}

	// 4) back-pressure on client reads based on queued outbound size
	const size_t HI = 512 * 1024, LO = 128 * 1024;
	const size_t out_sz = io.getChainBuf().getByteSize();
	if (out_sz > HI)
	{
		io.getFlow().setReadPaused(true);
	}
	else if (out_sz < LO)
	{
		io.getFlow().setReadPaused(false);
	}

	// 5) idle timeout
	if (progressed)
	{
		proxy_.io_idle_deadline_ms = now_cached_ms + 15000; // or your configured value
	}
	else if (now_cached_ms >= proxy_.io_idle_deadline_ms)
	{
		fail(504, "Gateway Timeout");
		if (proxy_.ufd >= 0)
		{
			::close(proxy_.ufd);
			proxy_.ufd = -1;
		}
		proxy_.active = false;
		return;
	}
}

/*

bool ClientConnection::isClosed() const

Returns whether the connection’s state machine is in PH_CLOSE.
This is a simple predicate used by server lifecycle code to decide whether to release the connection object,
stop scheduling reads, or finalize teardown. Keeping a named query improves readability
where decisions depend on the terminal state versus intermediary write-drain states.

*/

bool ClientConnection::isClosed() const
{
	return state == PH_CLOSE;
}

/*

bool ClientConnection::hasPendingWrite() const

Indicates if there are bytes queued to send to the client: either in the connection’s
ChainBuf or still pending from the CGI streamer. The event loop uses this to decide whether
to monitor POLLOUT for the client socket, preventing unnecessary wakeups and maintaining fairness across many connections.
It’s a cheap check that feeds into readiness updates.

*/

bool ClientConnection::hasPendingWrite() const
{
	return io.getChainBuf().getByteSize() > 0 || cgi.hasOutBytes();
}

/*

bool ClientConnection::isReadPaused()

Returns the current input-flow pause flag from FlowControl.
Useful for higher layers (e.g., handlers) to adjust behavior under backpressure and for unit tests verifying waterline logic.
Keeping the accessor here avoids exposing internal flow-control structures directly


*/

bool ClientConnection::isReadPaused()
{
	return io.getFlow().isReadPaused();
}

/*

void ClientConnection::close()

Asks the server to release this connection (server->releaseConnection(this)),
centralizing ownership transfer and actual socket close. This indirection keeps ClientConnection focused on protocol and state,
while Server owns object pooling or deletion policies. It maintains architectural separation,
important for graceful shutdowns and reuse strategies.

*/

void ClientConnection::close()
{
	server->releaseConnection(this);
}

/*

bool ClientConnection::pumpCgiToSocket(std::size_t max_bytes)

Moves up to max_bytes already-framed CGI bytes into the outbound buffer,
then attempts a non-blocking write to the client. If progress occurs, it refreshes the write
deadline and keeps state in PH_WRITE. When the CGI is finished and the socket buffer empties,
it either closes (if should_close) or resets request/response for keep-alive.
This pump isolates “CGI → socket” concerns, decoupling them from the rest of the tick logic.
It ensures chunked framing correctness (handled inside CGIStreamer)
while preserving fairness and responsiveness.

*/

bool ClientConnection::pumpCgiToSocket(std::size_t max_bytes)
{
	bool progressed = false;

	// 1) Pull from CGI → connection out buffer (chunked framing happens in CGIStreamer)
	if (cgi.isActive())
	{
		const std::size_t pulled = cgi.takeOutBytes(io.getChainBuf(), max_bytes);
		if (pulled > 0)
			progressed = true;
	}

	// 2) Try to write whatever we have to the client socket
	if (io.getChainBuf().getByteSize() > 0)
	{
		const ssize_t n = io.nb_write(); // non-blocking send
		if (n > 0)
			progressed = true;
		// Do NOT branch on errno here per project rules if n <= 0
	}

	// 3) If progress was made, refresh the write deadline (if you track one)
	if (progressed)
	{
		// keep the connection’s write phase alive
		state = PH_WRITE;
		// only call this if you have such a helper:
		resetDeadline(WR_TIMEOUT_MS);
	}

	// 4) Finalize: only when CGI is done AND socket buffer is empty
	if (!cgi.isActive() && io.getChainBuf().getByteSize() == 0)
	{
		if (should_close)
		{
			state = PH_CLOSE;
		}
		else
		{
			// Ready for the next request on this keep-alive connection
			// Reinitialize request/response to defaults
			req = HttpRequest();
			res = HttpResponse(req);

			state = PH_READ_HEADERS; // wait for the next request
		}
	}

	return progressed || cgi.isActive() || (io.getChainBuf().getByteSize() > 0);
}

/*

bool ClientConnection::wantsRead()

Returns whether the state machine currently desires socket reads: header,
route-select, precheck, or body phases. Helps the event dispatcher decide POLLIN interest without examining
internal state details elsewhere.
This improves clarity and avoids accidental reads during write-drain or close phases.


*/

bool ClientConnection::wantsRead()
{
	// We want read in header/body phases; can also allow lingering read in PH_CLOSE if you implement it
	return state == PH_READ_HEADERS || state == PH_ROUTE_SELECT || state == PH_PRECHECK || state == PH_READ_BODY;
}

/*

void ClientConnection::onTick(unsigned long long now_ms)

Heart of the per-connection scheduler. Updates cached time,
lets the CGI streamer enforce its deadlines, pulls CGI bytes into the outbound buffer,
applies backpressure hints to CGI stdout, services the reverse-proxy pump,
enforces connection-level deadlines (header/body/write/idle), performs non-blocking reads in eligible phases,
and advances the state machine (parse headers, route selection, prechecks, body read, route/build, write/next, close).
It also ensures fairness by bounding work, avoiding any errno-based branching after nb_read/nb_write,
honoring pause/resume policies, and quick-returning on terminal states.
This function ensures progress and responsiveness across thousands of concurrent connections.


*/

void ClientConnection::onTick(unsigned long long now_ms)
{
    now_cached_ms = now_ms;

    // Let CGI enforce its own deadlines (header + total runtime)
    cgi.onTick(now_ms);
    if (!cgi.isActive() && cgi.failed() && !cgi_error_latched)
    {
        std::cerr << "[tick] CGI failed -> 504, state=" << (int)state << std::endl;
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

    const std::size_t HIGH_WATER = 192u * 1024u; // pause when >= 192 KiB queued
    const std::size_t LOW_WATER  =  64u * 1024u; // resume when <= 64 KiB queued

    if (cgi.isActive())
    {
        if (outbytes >= HIGH_WATER)
        {
            std::fprintf(stderr, "[CGI][BP] pause stdout (queued=%zu)\n", outbytes);
            cgi.pauseStdoutReads();
        }
        else if (outbytes <= LOW_WATER)
        {
            std::fprintf(stderr, "[CGI][BP] resume stdout (queued=%zu)\n", outbytes);
            cgi.resumeStdoutReads();
        }
    }

    if (io.getFlow().shouldPauseRead(outbytes))
    {
        io.getFlow().setReadPaused(true);
        std::cerr << "[tick] pause read (queued=" << outbytes << ")\n";
    }
    if (io.getFlow().shouldResumeRead(outbytes))
    {
        io.getFlow().setReadPaused(false);
        std::cerr << "[tick] resume read (queued=" << outbytes << ")\n";
    }

    // 2) Try to flush whatever is queued (static or CGI)
    (void)io.nb_write();

    // After flushing, we may have dropped below LOW_WATER; re-check to resume quickly.
    if (cgi.isActive())
    {
        const std::size_t out_after = io.getChainBuf().getByteSize();
        if (out_after <= LOW_WATER)
            cgi.resumeStdoutReads();
    }

    // 2.5) Reverse-proxy tunnel pump
    if (proxy_.active)
        serviceProxyTunnel();

    // 3) Deadline enforcement
    if (dl.expired(now_ms) && state != PH_CLOSE)
    {
        std::cerr << "[tick] deadline expired in state=" << (int)state << std::endl;
        if (state == PH_READ_HEADERS || state == PH_READ_BODY ||
            state == PH_PRECHECK   || state == PH_ROUTE_SELECT)
        {
            fail(408, "Request Timeout");
        }
        else
        {
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
        if (r == 0)
        {
            std::cerr << "[tick] peer closed (nb_read==0) -> PH_CLOSE\n";
            state = PH_CLOSE;
            return;
        }
        // r < 0 => EAGAIN/other; ignore here.
    }

    // 5) Drive the HTTP/CGI state machine
    std::cerr << "[tick] dispatch state=" << (int)state << std::endl;

// 4.9) Safety guard: if a body reader exists and it's not finished yet,
//      force the state back to PH_READ_BODY (someone reset it too early).
if (state == PH_READ_HEADERS && body) {
    bool in_progress = false;

    if (fixed_body_target_ != (std::size_t)-1) {
        // Fixed-length body: not done until we've received target bytes
        in_progress = (body->bytes_received() < fixed_body_target_);
    } else {
        // Chunked body: not done until ChunkedReader says complete()
        ChunkedReader* cr = dynamic_cast<ChunkedReader*>(body);
        if (cr) in_progress = !cr->complete();
    }

    if (in_progress) {
        std::fprintf(stderr,
            "[guard] restoring PH_READ_BODY (bytes=%zu, fixed_target=%zu)\n",
            (size_t)(body ? body->bytes_received() : 0),
            (size_t)fixed_body_target_);
        state = PH_READ_BODY;
    }
}



    switch (state)
    {
    case PH_READ_HEADERS:
        parseHeaders();
        break;
    case PH_ROUTE_SELECT:
        selectRouteOnce();
        break;
    case PH_PRECHECK:
        runPreflight();
        break;
    case PH_READ_BODY:
        readBody();
        break;
    case PH_ROUTE:
        routeAndBuild();
        break;
    case PH_WRITE:
        finishWriteOrNext();
        break;
    case PH_CLOSE:
        io.getFlow().setReadPaused(true);
        (void)io.nb_write();
        if (io.getChainBuf().getByteSize() == 0)
        {
            if (body) { delete body; body = 0; }
            req.cleanupBodyFile();
            ready_to_close = true;
            std::cerr << "[tick] socket drained -> ready_to_close\n";
        }
        return;
    }
}


/*

Moves already-framed CGI bytes (from CGIStreamer) into the connection’s outbound buffer and performs a bounded
non-blocking write to the client socket. It returns whether progress was made and advances timeouts.
The max_bytes cap enforces fairness so one response can’t monopolize the loop.

Do we call it again?
Yes—repeatedly, every tick while:

CGIStreamer still has bytes, and/or

the client socket is writable, and

the connection is in a write/streaming phase.

It’s not recursive; it’s driven by the event loop (on onTick() and/or client POLLOUT).
You keep calling it until either the outbound buffer drains, CGI ends, or backpressure says stop.

Typical flow each tick:

Pull new CGI bytes (CGIStreamer::takeOutBytes)

If outbound not empty and socket writable → call pumpCgiToSocket()

If drained and CGI finished → close or prepare next request (keep-alive)

static const unsigned long long READ_TIMEOUT_MS = 15000;

A read-progress deadline (15s). It’s compared against “last read progress” timestamps in
onTick() to fail stalled phases (header read, body read, or idle reads).
This guarantees no request can hang the connection, satisfying the project’s
“never hang indefinitely” rule. When exceeded, the connection typically fail()s with 408/504
(depending on context), flushes the error, then closes.

 */

bool pumpCgiToSocket(std::size_t max_bytes = 128u * 1024u);

/*

void ClientConnection::parseHeaders()

Feeds bytes from the input ring to the HTTP parser until a full header block is found (\r\n\r\n or \n\n).
Enforces a maximum header size to prevent abuse. On parse failure, calls fail with the parser-provided status.
When headers complete, transitions to PH_ROUTE_SELECT and arms the body timeout.
This code purposely avoids blocking: it only consumes available bytes, leaves the remainder for future ticks,
and resets deadlines as progress occurs. Careful ring management (consumed vs. contiguous pointer)
avoids copying and preserves performance under heavy load.

*/

// Feed bytes from ring into your HttpRequest parser until it reports headers done
void ClientConnection::parseHeaders()
{
	IoRing &ring = io.getInputRing();

	for (;;)
	{
		const char *p = ring.readPtr();
		std::size_t n = ring.readAvail();
		if (!p || n == 0)
			break;

		const char *end = 0;

		// Look for "\r\n\r\n"
		for (std::size_t i = 0; !end && i + 3 < n; ++i)
		{
			if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n')
				end = p + i + 4;
		}
		// Or "\n\n"
		if (!end)
		{
			for (std::size_t i = 0; !end && i + 1 < n; ++i)
			{
				if (p[i] == '\n' && p[i + 1] == '\n')
					end = p + i + 2;
			}
		}

		std::size_t to_feed;
		if (end)
		{
			// Found a complete header block
			to_feed = static_cast<std::size_t>(end - p);
		}
		else
		{
			// Not yet complete → consume all but the last 3 bytes
			to_feed = (n > 3) ? (n - 3) : 0;
			if (to_feed == 0)
				break; // need more data
		}

		if (hdr_bytes + to_feed > max_hdr_bytes)
		{
			fail(431, "Request Header Fields Too Large");
			return;
		}

		if (!req.parse(p, to_feed))
		{
			int code = errno ? errno : 400; // parse() sets errno to the HTTP status
			fail(code, 0);
			return;
		}

		// ✅ Debug: dump Content-Type after headers parsed
		std::string ct = req.getHeaders().get("Content-Type");
		std::fprintf(stderr, "[PARSE][HDR] CT='%s'\n", ct.c_str());

		ring.consumed(to_feed);
		hdr_bytes += to_feed;
		resetDeadline(HDR_TIMEOUT_MS);

		if (end)
		{
			route_selected = false;
			state = PH_ROUTE_SELECT;
			resetDeadline(BODY_TIMEOUT_MS);
			return;
		}
		// else: continue the loop
	}

	resetDeadline(HDR_TIMEOUT_MS);
}

/*

void ClientConnection::selectRouteOnce()

Resolves the virtual server (by local port; placeholder “localhost” host),
runs preflight rules (RequestGuards::preflight), and records a RoutePlan skeleton.
It performs this exactly once per request and then moves to PH_PRECHECK.
Separating this step keeps routing concerns distinct from header parsing and body handling,
and ensures configuration (allowed methods, limits, return directives) are evaluated before heavy work.
It also caches the chosen virtual-server index for subsequent decisions, including temp-file directory or root resolution.

*/


void ClientConnection::selectRouteOnce()
{
	if (route_selected)
	{
		state = PH_PRECHECK;
		return;
	}

	plan.needs_body = false;
	plan.max_body_bytes = 0;

	// Use actual Host header (strip :port; lowercase; keep [v6] brackets)
	std::string host = req.getHeaders().get("Host");
	if (!host.empty())
	{
		if (host[0] == '[')
		{
			std::string::size_type rb = host.find(']');
			if (rb != std::string::npos)
			{
				if (rb + 1 < host.size() && host[rb + 1] == ':')
					host = host.substr(0, rb + 1);
				else
					host = host.substr(0, rb + 1);
			}
		}
		else
		{
			std::string::size_type c = host.find(':');
			if (c != std::string::npos)
				host = host.substr(0, c);
		}
		for (size_t i = 0; i < host.size(); ++i)
		{
			char &ch = host[i];
			if (ch >= 'A' && ch <= 'Z')
				ch = char(ch - 'A' + 'a');
		}
	}
	else
	{
		host = "localhost";
	}

	vs_idx = server->resolveVirtualServerByPort(local_port, host);

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

/*

void ClientConnection::runPreflight()

Analyzes headers via HeaderProcessor to decide body strategy (chunked vs. fixed length) and validate limits/semantics
(Expect: 100-continue, connection policy). Applies RequestGuards results, possibly failing early with accurate status
(413, 411, etc.). If a body is required, calls decideBodyReader (chunked or sized); then consumes any tail bytes that arrived with headers.
Initializes stall watchdog counters, transitions to PH_READ_BODY, and resets the body deadline.
This step centralizes correctness checks and ensures that the actual body phase never starts with inconsistent state or exceeded limits.


*/

void ClientConnection::runPreflight()
{
    const Headers &H = req.getHeaders();

	//  Debug: dump raw Content-Type header before analyze()
    std::string ct_raw = H.get("Content-Type");
    std::cerr << "[preflight] CT(raw)='" << ct_raw << "'" << std::endl;


    // Parse/validate headers first
    HeaderCheck hc = HeaderProcessor::analyze(req, H, max_body_bytes);
    if (!hc.ok)
    {
        std::cerr << "[preflight] header analyze failed: " << hc.error_status << std::endl;
        fail(hc.error_status, 0);
        return;
    }

    // Guards (method allowed, etc.)
    if (!pr.ok)
    {
        int st = pr.reject_status ? pr.reject_status : 400;
        std::cerr << "[preflight] guard reject " << st << " reason=" << pr.reject_reason << std::endl;
        fail(st, pr.reject_reason.c_str());
        return;
    }

    // Adopt stricter cap from guards if provided
    if (pr.max_body_bytes && (!max_body_bytes || pr.max_body_bytes < max_body_bytes))
        max_body_bytes = pr.max_body_bytes;

    std::cerr << "[preflight] needs_body=" << (pr.needs_body ? 1 : 0)
              << " chunked=" << (hc.chunked ? 1 : 0)
              << " CL=" << (unsigned long)hc.content_length
              << " cap=" << (unsigned long)max_body_bytes
              << std::endl;

    // If this request doesn't need a body, route immediately.
    if (!pr.needs_body)
    {
        state = PH_ROUTE;
        std::cerr << "[preflight] no body needed -> PH_ROUTE\n";
        return;
    }

    // If Content-Length is known and exceeds the cap, reject early.
    if (!hc.chunked && hc.content_length > 0 &&
        max_body_bytes && hc.content_length > max_body_bytes)
    {
        std::cerr << "[preflight] CL exceeds cap -> 413\n";
        fail(413, "Payload Too Large");
        return;
    }

    // Only now consider Expect: 100-continue
    if (hc.expect_continue && ExpectContinue::needed(H))
    {
        ExpectContinue::write100(io.getChainBuf());
        std::cerr << "[preflight] wrote 100-continue\n";
    }

    // Decide how we'll read the body
    if (hc.chunked)
    {
        std::cerr << "[preflight] choose ChunkedReader\n";
        decideBodyReader(); // chunked overload (writes into req.bodyRef())
    }
    else if (hc.content_length > 0)
    {
        std::cerr << "[preflight] choose ContentLenght/FileBodyReader, CL=" 
                  << (unsigned long)hc.content_length << std::endl;
        decideBodyReader(hc.content_length);
    }
    else
    {
        std::cerr << "[preflight] neither CL nor chunked -> 411\n";
        fail(411, "Length Required");
        return;
    }

    // Feed any tail bytes that arrived with headers to the body reader
    if (body)
    {
        std::string tail = req.takeBuffer();
        if (!tail.empty())
        {
            (void)body->consume(tail.data(), tail.size());
            std::cerr << "[preflight] fed tail bytes=" << (unsigned long)tail.size() << std::endl;
        }
    }

    // Reset progress/timeout bookkeeping and switch state
    body_bytes_prev = 0;
    body_no_progress_ticks = 0;
    flush_no_progress_ticks = 0;

    state = PH_READ_BODY;
    resetDeadline(BODY_TIMEOUT_MS);
    std::cerr << "[preflight] -> PH_READ_BODY\n";
}


/*
// --- in ClientConnection.cpp ---
void ClientConnection::readBody()

Streams request body from the ring into the selected reader (chunked decoder, in-RAM fixed, or temp-file writer).
Enforces total post-decode cap, runs two watchdogs (body progress and flush progress), and errors on stalls (408)
or insufficient storage (507). When complete—either reaching the fixed length and flushing pending,
or chunked reader reporting done—it advances to PH_ROUTE and arms write deadlines.
By handling backpressure and disk flushing incrementally, it allows arbitrarily large uploads without blocking,
honoring server limits configured per location or server block.


*/

// --- in ClientConnection.cpp ---
// in your state machine:
// case PH_READ_BODY: readBody(); break;

// --- ClientConnection.cpp ---
void ClientConnection::readBody()
{
    if (!body) { fail(400, "Bad Request"); return; }

    IoRing &ring = io.getInputRing();

    // Baseline for progress
    const std::size_t before = body->bytes_received();

    // 1) Drain from socket ring into the selected reader
    for (;;)
    {
        const char *buf = ring.readPtr();
        std::size_t avail = ring.readAvail();
        if (!buf || avail == 0) break;

        const std::size_t used = body->consume(buf, avail);
        if (used == 0) break;            // reader can’t take more now

        ring.consumed(used);

		// after 'ring.consumed(used);' and deadline reset:
		std::cerr << "[readBody] used=" << used
				<< " total=" << body->bytes_received() << std::endl;

        resetDeadline(BODY_TIMEOUT_MS);

        // Enforce post-decode cap
        if (max_body_bytes && body->bytes_received() > max_body_bytes) {
            fail(413, "Payload Too Large");
            return;
        }
    }

    // 2) Flush any staged bytes to disk for disk-backed readers
    std::size_t pending_before = 0, flushed = 0;
    if (FileBodyReader *fr = dynamic_cast<FileBodyReader *>(body)) {
        pending_before = fr->pending_size();
        if (pending_before) flushed = fr->flush_to_disk(64 * 1024);
    } else if (ChunkedReader *cr = dynamic_cast<ChunkedReader *>(body)) {
        pending_before = cr->pending_size();
        if (pending_before) flushed = cr->flush_to_disk(64 * 1024);
    }

    // Cap again in case totals advanced during flush
    if (max_body_bytes && body->bytes_received() > max_body_bytes) {
        fail(413, "Payload Too Large");
        return;
    }

    // 3) Watchdogs: read progress + flush progress
    const std::size_t after = body->bytes_received();

    if (after == before && ring.readAvail() == 0)
        ++body_no_progress_ticks;
    else
        body_no_progress_ticks = 0;

    if (pending_before > 0 && flushed == 0)
        ++flush_no_progress_ticks;
    else
        flush_no_progress_ticks = 0;

    if (body_no_progress_ticks >= BODY_STALL_TICK_LIMIT) {
        fail(408, "Request Timeout");
        return;
    }
    if (flush_no_progress_ticks >= FLUSH_STALL_TICK_LIMIT) {
        fail(507, "Insufficient Storage");
        return;
    }

    // 4) Completion check
    bool done = false;
    if (fixed_body_target_ != (std::size_t)-1) {
        // Fixed-length: require bytes_received >= target and (if disk) pending flushed
        if (after >= fixed_body_target_) {
            if (FileBodyReader *fr2 = dynamic_cast<FileBodyReader *>(body))
                done = (fr2->pending_size() == 0);
            else
                done = true;
        }
    } else {
        // Chunked: reader decides
        if (ChunkedReader *cr2 = dynamic_cast<ChunkedReader *>(body))
            done = cr2->complete();
    }

    if (!done) {
		
		std::cerr << "[readBody] not done yet; total=" 
              << body->bytes_received() << std::endl;
		return;
	}

    // 5) Body complete — publish disk path for handlers if chunked spilled
    
	if (ChunkedReader* cr2 = dynamic_cast<ChunkedReader*>(body)) {
		if (cr2->isBodyOnDisk()) {
			req.enableBodyOnDisk(cr2->getBodyFilePath());
			std::cerr << "[readBody] chunked complete: disk="
					<< cr2->getBodyFilePath() << std::endl;
		} else {
			std::ostringstream oss;
			oss << req.getBodyLength();
			std::cerr << "[readBody] chunked complete: mem size="
					<< oss.str() << std::endl;
		}
	}

    // Advance pipeline
    state = PH_ROUTE;
    resetDeadline(WR_TIMEOUT_MS);
}


/*


void ClientConnection::routeAndBuild()

Delegates to ServerPipeline::processRequest to choose and run a handler
(static, upload, CGI, proxy, put/patch, return).
Sets keep-alive policy from request version and Connection header, ensures default response headers,
serializes the response into the outbound buffer, and transitions to PH_WRITE. If the handler is asynchronous (e.g., CGI/proxy),
it still moves to write phase where streaming progress occurs while the loop continues pumping.
Centralizing serialization avoids duplication and guarantees
Date/Server/Connection and Content-Length are correct or synthesized as needed.

*/

void ClientConnection::routeAndBuild()
{
    // --- Snapshot a few bits before routing (for debugging) ---
    const std::string m  = req.getMethod();
    const std::string p  = req.getPath();
    const std::string ct_before = req.getHeaders().get("Content-Type");
    const std::string te_before = req.getHeaders().get("Transfer-Encoding");
    const std::string cl_before = req.getHeaders().get("Content-Length");
    const std::string ex_before = req.getHeaders().get("Expect");
    const std::string ho_before = req.getHeaders().get("Host");

    std::fprintf(stderr,
        "[ROUTE][enter] method=%s path=%s disk=%d bodyLen=%lu\n",
        m.c_str(), p.c_str(),
        req.isBodyOnDisk()?1:0,
        (unsigned long)req.getBodyLength());

    std::fprintf(stderr,
        "[ROUTE][HDR] CT(before)='%s' TE='%s' CL='%s' Expect='%s' Host='%s'\n",
        ct_before.c_str(), te_before.c_str(), cl_before.c_str(),
        ex_before.c_str(), ho_before.c_str());

    // --- Let the pipeline decide and (possibly) start CGI. ---
    const bool done = ServerPipeline::processRequest(
        server->getConfig(),
        vs_idx,
        req,
        res,
        *ctx,
        &cgi);

    // After pipeline: did it mutate headers or request?
    const std::string ct_after = req.getHeaders().get("Content-Type");
    std::fprintf(stderr,
        "[ROUTE][exit ] done=%d cgi.active=%d res.status=%d CT(after)='%s'\n",
        done?1:0, cgi.isActive()?1:0, res.status, ct_after.c_str());

    // One keep-alive decision used by both paths
    const std::string connHdr = req.getHeaders().get("Connection");
    should_close =
        (req.getHttpVer() == "HTTP/1.0") ||
        (connHdr == "close" || connHdr == "Close");

    if (!done)
    {
        // Asynchronous path (e.g., CGI) — write whatever is queued so far
        state = PH_WRITE;
        resetDeadline(WR_TIMEOUT_MS);
        std::fprintf(stderr,
            "[ROUTE] async path -> PH_WRITE keep-alive=%d queued=%lu\n",
            should_close?0:1,
            (unsigned long)io.getChainBuf().getByteSize());
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

    std::fprintf(stderr,
        "[ROUTE] sync path -> PH_WRITE status=%d reason='%s' bytes_queued=%lu\n",
        res.status, res.reason.c_str(),
        (unsigned long)io.getChainBuf().getByteSize());

    state = PH_WRITE;
    resetDeadline(WR_TIMEOUT_MS);
}


/*

void ClientConnection::finishWriteOrNext()

If outbound bytes remain, keep writing later.
When CGI isn’t active and no bytes remain, either close (for non-keep-alive) or reset all per-request state
(headers/body counters, plan, context, request/response objects, CGI error latch, and fixed_body_target_)
and transition back to PH_READ_HEADERS. If unread bytes are already in the ring,
immediately call parseHeaders to pipeline the next request on the same connection.
This function implements HTTP/1.1 keep-alive efficiently while ensuring clean state between requests.


*/

void ClientConnection::finishWriteOrNext()
{
    // 0) Try to flush anything left (defensive; onTick() also does this)
    (void)io.nb_write();

    // 1) If there is still data queued, keep the write phase alive
    if (io.getChainBuf().getByteSize() != 0)
        return;

    // 2) If CGI is still active or has bytes pending, keep write phase alive
    if (cgi.isActive() || cgi.cgiStdoutFD() >= 0 || cgi.hasOutBytes())
        return;

    // 3) Per-request cleanup (runs for BOTH keep-alive and close)
    //    a) dispose of any body reader
    if (body) {
        delete body;
        body = 0;
    }

    //    b) remove any temp body file (e.g., from chunked spill / upload)
    req.cleanupBodyFile();

    //    c) reset per-request counters/flags
    hdr_bytes                 = 0;
    fixed_body_target_        = (std::size_t)-1;
    body_bytes_prev           = 0;
    body_no_progress_ticks    = 0;
    flush_no_progress_ticks   = 0;
    route_selected            = false;
    cgi_error_latched         = false;

    // If you lowered max_body_bytes per request in runPreflight(),
    // restore the default (0 means "no per-request override" in your code).
    max_body_bytes            = 0;

    //    d) reset routing plan/context and message objects
    plan = RoutePlan();
    if (ctx) ctx->reset();

    // Re-init request/response for next message on this connection
    req = HttpRequest();
    res = HttpResponse(req);

    // 4) Decide connection fate
    if (should_close)
    {
        // Close path: nothing left to write, so advance to PH_CLOSE.
        // (onTick() will perform the actual close when drained)
        state = PH_CLOSE;
        // one more opportunistic write (usually nothing) and set deadline
        (void)io.nb_write();
        resetDeadline(WR_TIMEOUT_MS);
        std::fprintf(stderr, "[write] done -> PH_CLOSE (KA=0)\n");
        return;
    }

    // 5) Keep-alive: go back to reading next request headers
    io.getFlow().setReadPaused(false);
    state = PH_READ_HEADERS;
    resetDeadline(IDLE_TIMEOUT_MS);

    std::fprintf(stderr, "[write] done -> PH_READ_HEADERS (KA=1)\n");

    // If the peer has already pipelined the next request, parse it now
    if (io.getInputRing().readAvail() > 0)
        parseHeaders();
}


/*

void ClientConnection::fail(int code, const char reason)*

Builds a plain-text error response via ResponseFactory::makeText,
queues it, marks should_close, switches to PH_WRITE, resets write deadlines,
and then calls finishWriteOrNext to progress teardown. Centralizing failure handling ensures consistent headers,
connection policy, and serialization across all error sites (parsing,
timeouts, storage errors, proxy/CGI failures). It also resets body-length bookkeeping to avoid inconsistent post-error state.
This uniform path keeps the server’s behavior predictable for clients and simplifies reasoning about termination semantics.

*/

void ClientConnection::fail(int code, const char *reason)
{


	std::cerr << "[fail] code=" << code
              << " reason=" << (reason ? reason : "")
              << " state=" << (int)state
              << " bytes_received=" << (body ? body->bytes_received() : 0)
              << std::endl;

	res = ResponseFactory::makeText(code, "", reason ? reason : "", true);

	std::ostringstream os;
	os << res;
	const std::string s = os.str();
	io.getChainBuf().push_copy(s.data(), s.size());

	should_close = true;
	state = PH_WRITE;
	resetDeadline(WR_TIMEOUT_MS);

	fixed_body_target_ = (std::size_t)-1; // <— optional reset

	finishWriteOrNext();
}

/*

void ClientConnection::decideBodyReader() (chunked)

Chooses ChunkedReader for Transfer-Encoding: chunked, passing temp-buffer and a
configured temp directory. Marks fixed_body_target_ unknown.
The chunked reader supports disk spilling to avoid memory blowup and exposes completion and
pending-flush checks used by watchdog logic. This selection ensures the
server properly de-chunks before handing data to handlers (e.g., CGI or upload),
satisfying the specification that CGIs expect EOF-terminated bodies, not raw chunked framing.



*/



// --- ClientConnection.cpp ---
void ClientConnection::decideBodyReader() {
    if (body) { delete body; body = 0; }

    std::cerr << "[decideBodyReader] using ChunkedReader -> req.bodyRef()"
              << std::endl;

    const std::string tmpdir =
        server->getConfig().servers()[vs_idx].client_body_temp_path;

    body = new ChunkedReader(req.bodyRef(), plan.max_body_bytes, tmpdir);

    if (max_body_bytes)
        static_cast<ChunkedReader*>(body)->set_hard_limit(max_body_bytes);

    fixed_body_target_ = (std::size_t)-1;
}



/*

void ClientConnection::decideBodyReader(std::size_t content_length)

For fixed-length bodies, sets fixed_body_target_,
then selects in-memory accumulation if length is small (INMEM_BODY_LIMIT) and within configured cap,
else opens a FileBodyReader under the configured temp directory. If file open fails, replies 507.
This decision balances memory efficiency with performance: small bodies stay fast in RAM;
large ones spill to disk safely. It integrates tightly with later stall/flush watchdogs and the final completion checks before routing.


*/

// --- keep ONLY this definition for the CHUNKED overload ---

void ClientConnection::decideBodyReader(std::size_t content_length)
{

	std::cerr << "[decideBodyReader] using ContentLenghtReader, length="
          << content_length << std::endl;

	if (body)
	{
		delete body;
		body = 0;
	}
	const std::size_t cap = (max_body_bytes != 0) ? max_body_bytes : static_cast<std::size_t>(~0);
	fixed_body_target_ = content_length; // <— set it right here
	if (content_length <= INMEM_BODY_LIMIT && content_length <= cap)
	{
		body = new ContentLenghtReader(content_length, req);
		return;
	}
	const std::string tmpdir = server->getConfig().servers()[vs_idx].client_body_temp_path;
	FileBodyReader *fr = new FileBodyReader(tmpdir);
	if (!fr->ensure_open())
	{
		delete fr;
		fail(507, "Insufficient Storage");
		return;
	}
	req.enableBodyOnDisk(fr->get_path());
	body = fr;
}
