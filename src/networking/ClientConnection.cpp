#include "ClientConnection.h"
#include "ExpectContinue.h"
#include "RequestContext.h"
#include "ResponseFactory.h"
#include "ChunkedReader.h"
#include "FileBodyReader.h"
#include "ContentLenghtReader.h"
#include "ServerPipeline.h"
#include "Server.h"
#include <sstream>
#include <netinet/in.h>

static const std::size_t INMEM_BODY_LIMIT = 256 * 1024;

ClientConnection::ClientConnection(int fd, Server *s, unsigned long long nowMs)
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
	  ready_to_close(false)
{
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
bool ClientConnection::hasPendingWrite() { return io.getChainBuf().getByteSize() != 0; }
bool ClientConnection::isReadPaused() { return io.getFlow().isReadPaused(); }
void ClientConnection::close()

{
	server->releaseConnection(this);
}

bool ClientConnection::wantsRead()
{
	// We want read in header/body phases; can also allow lingering read in PH_CLOSE if you implement it
	return state == PH_READ_HEADERS || state == PH_ROUTE_SELECT || state == PH_PRECHECK || state == PH_READ_BODY;
}

void ClientConnection::onTick(unsigned long long now_ms)
{
	const std::size_t outbytes = io.getChainBuf().getByteSize();
	now_cached_ms = now_ms;

	if (io.getFlow().shouldPauseRead(outbytes))
		io.getFlow().setReadPaused(true);
	if (io.getFlow().shouldResumeRead(outbytes))
		io.getFlow().setReadPaused(false);

	(void)io.nb_write();

	if (dl.expired(now_ms) && state != PH_CLOSE)
	{
		if (state == PH_READ_HEADERS || state == PH_READ_BODY || state == PH_PRECHECK || state == PH_ROUTE_SELECT)
		{
			fail(408, "Request Timeout");
		}
		else
		{
			state = PH_CLOSE;
		}
		return;
	}

	if (!io.getFlow().isReadPaused() && (state == PH_READ_HEADERS || state == PH_READ_BODY))
		if (io.nb_read(32 * 1024) == 0)
		{
			state = PH_CLOSE;
			return;
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
		if (io.getChainBuf().getByteSize() != 0)

			if (body)
			{
				delete body;
				body = 0;
			}
		req.cleanupBodyFile();
		ready_to_close = true;
		// server->releaseConnection(this);
		return;
	}
}

void ClientConnection::parseHeaders()
{
	const char *buf = io.getInputRing().readPtr();
	std::size_t avail = io.getInputRing().readAvail();
	if (avail == 0)
		return;

	if (!req.parse(buf, avail))
	{
		if (errno == HTTP_BAD_REQUEST || errno == HTTP_HEADER_TOO_BIG)
			fail(errno, "");
	}

	std::size_t used = avail;

	if (used > 0)
	{
		io.getInputRing().consumed(used);
		hdr_bytes += used;
		if (hdr_bytes > max_hdr_bytes)
		{
			fail(431, "Request Header Fields to Larger");
			return;
		}
		resetDeadline(HDR_TIMEOUT_MS);
	}

	if (!req.headersDone())
		return;

	state = PH_ROUTE_SELECT;
	resetDeadline(BODY_TIMEOUT_MS);
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

	if (!hc.ok)
	{
		fail(hc.error_status, 0);
		return;
	}

	if (!pr.ok)
	{
		fail(pr.reject_status ? pr.reject_status : 400, pr.reject_reason.c_str());
		return;
	}

	max_body_bytes = pr.max_body_bytes;

	if (hc.expect_continue && ExpectContinue::needed(req.getHeaders()))
		ExpectContinue::write100(io.getChainBuf()); // queued; nb_write() will flush it

	if (!pr.needs_body)
	{
		state = PH_ROUTE;
		return;
	}

	if (hc.chunked)
	{
		decideBodyReader(/*chunked*/);
	}
	else if (hc.content_length > 0)
	{

		if (max_body_bytes && hc.content_length > max_body_bytes)
		{
			fail(413, "Payload Too Large");
			return;
		}
		decideBodyReader(hc.content_length);
	}
	else
	{
		fail(411, "Length Required");
		return;
	}

	body_bytes_prev = 0;
	body_no_progress_ticks = 0;
	flush_no_progress_ticks = 0;
	state = PH_READ_BODY;
	resetDeadline(BODY_TIMEOUT_MS);
}

void ClientConnection::readBody()
{
	const char *buf = io.getInputRing().readPtr();
	const std::size_t avail = io.getInputRing().readAvail();

	if (!body)
	{
		fail(400, "Bad Request");
		return;
	}

	const std::size_t before = body->bytes_received();

	// 1) Try to consume whatever arrived this tick
	if (avail != 0)
	{
		const std::size_t used = body->consume(buf, avail);
		if (used > 0)
		{
			io.getInputRing().consumed(used);
			resetDeadline(BODY_TIMEOUT_MS);
		}
	}

	const std::size_t after = body->bytes_received();

	// 2) Enforce max-body guard (runtime, affects chunked too)
	if (max_body_bytes && after > max_body_bytes)
	{
		fail(413, "Payload Too Large");
		return; // do not attempt to flush
	}

	// 3) Progress watchdog for reading (no advance across ticks)
	if (after == before && avail == 0)
	{
		// nothing new consumed, and no new bytes visible in ring
		++body_no_progress_ticks;
	}
	else
	{
		body_no_progress_ticks = 0;
	}
	body_bytes_prev = after;

	// 4) If we’re file-backed, attempt a bounded flush to disk each tick
	//    and watch for persistent no-progress when there IS pending data.
	std::size_t pending_before = 0;
	std::size_t flushed = 0;

	if (FileBodyReader *fr = dynamic_cast<FileBodyReader *>(body))
	{
		pending_before = fr->pending_size();
		if (pending_before)
			flushed = fr->flush_to_disk(64 * 1024);
	}
	else if (ChunkedReader *cr = dynamic_cast<ChunkedReader *>(body))
	{
		pending_before = cr->pending_size();
		if (pending_before)
			flushed = cr->flush_to_disk(64 * 1024);
	}

	if (pending_before > 0 && flushed == 0)
	{
		++flush_no_progress_ticks;
	}
	else
	{
		flush_no_progress_ticks = 0;
	}

	// 5) Stall policies
	if (body_no_progress_ticks >= BODY_STALL_TICK_LIMIT)
	{
		// We’ve been called many times with no read progress → timeout
		fail(408, "Request Timeout");
		return;
	}
	if (flush_no_progress_ticks >= FLUSH_STALL_TICK_LIMIT)
	{
		// We had bytes staged for disk but couldn’t make any progress
		// (don’t branch on errno; treat as storage/IO exhaustion class)
		fail(507, "Insufficient Storage");
		return;
	}

	// 6) Completion check
	if (!body->complete() && req.getState() != OVER)
		return;

	// Body done → continue pipeline
	state = PH_ROUTE;
	resetDeadline(WR_TIMEOUT_MS);
}

void ClientConnection::routeAndBuild()
{
	// Build the response for the current request
	(void)ServerPipeline::processRequest(server->getConfig(), vs_idx, req, res, *ctx);

	// Decide keep-alive vs close (HTTP/1.1 keeps alive by default unless client asks to close)
	const std::string connHdr = req.getHeaders().get("Connection");
	should_close = (req.getHttpVer() == "HTTP/1.0") ||
				   (connHdr == "close" || connHdr == "Close");

	// Advertise the chosen policy in the response
	if (should_close)
		res.headers.set("Connection", "close");
	else
		res.headers.set("Connection", "keep-alive");

	// Fill in defaults (Date, Server, Content-Length if applicable, etc.)
	res.ensureDefaultHeaders();

	// Serialize → output buffer
	std::ostringstream os;
	os << res;
	const std::string s = os.str();
	io.getChainBuf().push_copy(s.data(), s.size());

	state = PH_WRITE;
	resetDeadline(WR_TIMEOUT_MS);
}

void ClientConnection::finishWriteOrNext()
{
	if (io.getChainBuf().getByteSize() != 0)
		return;

	if (should_close)
	{
		state = PH_CLOSE;
		return;
	}

	req.cleanupBodyFile();
	req.reset();
	// Reset req now
	hdr_bytes = 0;
	max_body_bytes = 0;
	if (body)
	{
		delete body;
		body = 0;
	}
	plan = RoutePlan();
	ctx->reset();
	req = HttpRequest();
	res = HttpResponse();
	route_selected = false;

	state = PH_READ_HEADERS;
	resetDeadline(IDLE_TIMEOUT_MS);
	if (io.getInputRing().readAvail() > 0)
	{
		parseHeaders();
	}
}

void ClientConnection::fail(int code, const char *reason)
{

	res = ResponseFactory::makeText(code, "", reason ? reason : "", /*close*/ true);

	std::ostringstream os;
	os << res;
	const std::string s = os.str();
	io.getChainBuf().push_copy(s.data(), s.size()); // ChainBuf

	should_close = true;
	state = PH_WRITE;
	resetDeadline(WR_TIMEOUT_MS);
	finishWriteOrNext();
}

void ClientConnection::decideBodyReader()
{
	if (body)
	{
		delete body;
		body = 0;
	}
	body = new ChunkedReader(io.getTmp(), plan.max_body_bytes, server->getConfig().servers()[vs_idx].client_body_temp_path);
}

void ClientConnection::decideBodyReader(std::size_t content_length)
{
	if (body)
	{
		delete body;
		body = 0;
	}

	const std::size_t cap = (max_body_bytes != 0) ? max_body_bytes : (std::size_t)(~0);

	const bool use_inmem =
		(content_length > 0) &&
		(content_length <= INMEM_BODY_LIMIT) &&
		(content_length <= cap);

	if (use_inmem)
	{
		body = new ContentLenghtReader(content_length, req);
	}
	else
	{
		FileBodyReader *fr = new FileBodyReader(
			server->getConfig().servers()[vs_idx].client_body_temp_path);
		body = fr;
	}
}