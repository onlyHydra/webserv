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
#include <poll.h>	  // POLLIN, POLLOUT
#include <fcntl.h>	  // fcntl, O_NONBLOCK, FD_CLOEXEC
#include <signal.h>	  // SIGKILL
#include <sys/stat.h> // fstat
#include <sys/time.h> // gettimeofday
#include <unistd.h>	  // read, write, pread, close
#include <sys/time.h> // gettimeofday

// void CGIStreamer::attachLoop(EventLoop* L) { loop_ = L; }

// --- tiny helper: RFC1123 Date string (GMT) ---

/* 

static std::string http_date_now_gmt()

Formats the current time as an RFC-1123 Date header in GMT. CGI responses may omit standard HTTP metadata; 
adding a correct Date improves caching and client behavior. Centralizing the formatter prevents inconsistent strings or 
locale surprises. It uses std::time, std::gmtime, and std::strftime into a fixed buffer, returning a compact string. 
Because it’s small and allocation-light, it’s safe on hot paths when headers are finalized. 
This helper is called by finalizeHeaders() to ensure every CGI response has a Date, even when the CGI itself didn’t emit one.

*/


static std::string http_date_now_gmt()
{
	char buf[64];
	std::time_t t = std::time(0);
	std::tm g = *std::gmtime(&t);
	std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
	return std::string(buf);
}

// --- case-insensitive equals for ASCII keys ---

/* 

static bool iequals_ascii(const std::string& a, const std::string& b)

ASCII-only case-insensitive equality used while parsing CGI-emitted headers. 
HTTP header names are case-insensitive; many CGIs also vary capitalization of keys like Status, 
Content-Type, or Set-Cookie. Implementing a tight loop (manual A–Z downcase) avoids 
locale costs and keeps parsing deterministic. Returning false on size mismatch short-circuits quickly. 
Using this function inside parseOneHeaderLine ensures robust behavior regardless of header casing, 
without pulling heavier utilities. The strict ASCII scope matches HTTP token space, 
eliminating edge-case surprises and keeping CPU overhead minimal when processing many short header lines.

*/


static bool iequals_ascii(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	for (std::string::size_type i = 0; i < a.size(); ++i)
	{
		unsigned char ca = static_cast<unsigned char>(a[i]);
		unsigned char cb = static_cast<unsigned char>(b[i]);
		if (ca >= 'A' && ca <= 'Z')
			ca = char(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z')
			cb = char(cb - 'A' + 'a');
		if (ca != cb)
			return false;
	}
	return true;
}



/* 

CGIStreamer::CGIStreamer(HttpRequest& req, HttpResponse& res)

Constructor wiring the streamer to the current request and response. 
It initializes event-loop pointer to NULL (attached elsewhere), resets CGI activity flags, 
fds, body streaming offsets (in-memory or disk), header parser state, Set-Cookie accumulator, 
output framing (chunked mode off until finalized), and timeout trackers. No OS work occurs here—spawning 
the CGI and registering fds happens in beginCgi. The careful zeroing ensures reuse across keep-alive requests without stale state. 
This separation keeps responsibilities clear: configuration and process management happen later, 
while the streamer simply starts in a predictable, safe baseline.


*/

CGIStreamer::CGIStreamer(HttpRequest &req, HttpResponse &res)
	: req_(req), res_(res), proc_()
	// ---- Event loop ----
	,
	loop_(NULL) // will be set via attachLoop(&eventLoop)
	// ---- CGI pipes / state ----
	,
	cgi_active_(false), failed_(false), cgi_in_fd_(-1), cgi_out_fd_(-1)
	// ---- Request-body streaming ----
	,
	cgi_body_off_(0), body_fd_(-1), body_file_off_(0), body_path_()
	// ---- Header parsing ----
	,
	hdr_state_(HDR_WAITING), cgi_header_accum_(), saw_content_type_(false), status_code_(0) // keep 0 if your code interprets 0 as “unset”
	,
	status_reason_(), set_cookie_()
	// ---- Output framing ----
	,
	http_head_queued_(false), chunked_mode_(false), sent_final_chunk_(false), out_buf_(), out_off_(0)
	// ---- Deadlines / timeouts ----
	,
	hdr_deadline_(), total_deadline_(), write_deadline_ms_(0)
	// ---- Back-pressure hint ----
	,
	client_out_bytes_(0)
	// ---- Read throttling ----
	,
	stdout_paused_(false),

	// in CGIStreamer.cpp ctor init list
	mem_body_(), 
	mem_off_(0), 
	using_mem_(false),

	in_mem_body_(), 
	in_mem_off_(0), 
	using_mem_body_(false)

{
	// nothing else to do
}


/* 

CGIStreamer::~CGIStreamer()

Destructor ensures descriptor hygiene by calling closeStdin() and closeStdout(). 
The actual child process lifecycle is owned by CgiProcess, 
but the streamer owns registrations in the EventLoop and must unregister/close its pipe fds. 
Idempotent closing prevents dangling poll registrations, spurious wakeups, 
or resource leaks after request teardown. Keeping destructor small and side-effect-free beyond 
cleanup makes error paths and early exits safe: even if multiple subsystems try to tidy up, 
repeated closes won’t crash or double-free. This aligns with RAII expectations in a long-running, 
event-driven server.


*/

CGIStreamer::~CGIStreamer()
{
	closeStdin();
	closeStdout();
}

// --- enqueue helpers (chunked-aware) ---


/* 

void enqueueOut(const char* data, std::size_t len)

Queues bytes for the client, handling HTTP/1.1 chunked framing when chunked_mode_ is enabled. For chunked mode, 
it writes <hex>\r\npayload\r\n; otherwise, it appends raw payload. 
Centralizing this logic lets higher-level code just “write body” and the streamer guarantees the correct wire format. 
It minimizes copying by appending directly into out_buf_, which the connection drains via takeOutBytes. 
This function is performance-sensitive: small, allocation-aware operations are critical when streaming large CGI outputs. 
Correct framing enables persistent connections without knowing Content-Length.

*/

// In CGIStreamer.cpp
void CGIStreamer::enqueueOut(const char* data, std::size_t len)
{
	if (!data || len == 0) 
		return;

	// Allow the head, suppress any body for HEAD after the head is queued
	if (req_.getMethod() == "HEAD" && http_head_queued_) {
		return;
	}

	if (!chunked_mode_) {
		out_buf_.insert(out_buf_.end(), data, data + len);
		return;
	}

	// --- chunked framing: <hex>\r\n<data>\r\n ---
	// format len in hex (C++98-safe)
	char hexbuf_rev[32];
	int h = 0;
	unsigned long long v = (unsigned long long)len;
	if (v == 0) hexbuf_rev[h++] = '0';
	else {
		while (v) {
			unsigned d = (unsigned)(v & 0xF);
			hexbuf_rev[h++] = (char)(d < 10 ? '0'+d : 'a'+(d-10));
			v >>= 4;
		}
	}
	// write hex digits in forward order + CRLF
	char sz[36];
	int k = 0;
	for (int i = h - 1; i >= 0; --i) sz[k++] = hexbuf_rev[i];
	sz[k++] = '\r'; sz[k++] = '\n';

	// size line
	out_buf_.insert(out_buf_.end(), sz, sz + k);
	// data
	out_buf_.insert(out_buf_.end(), data, data + len);
	// CRLF
	out_buf_.push_back('\r');
	out_buf_.push_back('\n');
}



/* 

void enqueueFinalChunk()

Appends the terminating chunk "0\r\n\r\n" exactly once when chunked transfer encoding is active. 
This marks end-of-message to clients if no Content-Length was provided, allowing keep-alive reuse of the connection. 
The sent_final_chunk_ guard prevents duplicate terminators when multiple code paths converge 
(EOF, error, or finalization). Centralizing termination logic ensures consistent behavior across 
normal completion and error paths, improving robustness with intermediaries 
and browsers that depend on unambiguous message boundaries.

*/

void CGIStreamer::enqueueFinalChunk()
{
	if (sent_final_chunk_)
		return;
	sent_final_chunk_ = true;

	if (chunked_mode_) {
		static const char kFinal[] = "0\r\n\r\n";
		out_buf_.insert(out_buf_.end(), kFinal, kFinal + sizeof(kFinal) - 1);
	}
}






/* 

void pauseStdoutReads() / void resumeStdoutReads()

Flow-control hooks that toggle POLLIN interest for the CGI stdout fd in the event loop. 
When downstream (client) buffers are high, pauseStdoutReads() suspends reads to prevent unbounded memory growth; 
resumeStdoutReads() re-enables reads after pressure subsides. 
These are essential for fairness across connections and for avoiding memory bloat when clients are slow. 
They integrate with hints from ClientConnection (client_out_bytes_) and are idempotent to simplify callers. 
Managing readiness interest instead of reading and discarding bytes 
keeps the system lossless and cooperative in a single-poll architecture.


*/

void CGIStreamer::pauseStdoutReads()
{
	if (stdout_paused_)
		return;
	if (loop_ && cgi_out_fd_ >= 0)
		loop_->modFD(cgi_out_fd_, 0);
	stdout_paused_ = true;
}


void CGIStreamer::resumeStdoutReads()
{
	if (!stdout_paused_)
		return;
	if (loop_ && cgi_out_fd_ >= 0)
		loop_->modFD(cgi_out_fd_, POLLIN);
	stdout_paused_ = false;
}

// --- header parsing ---


/* 

void parseOneHeaderLine(const std::string& line)

Parses one Key: Value header line from the CGI. Special-cases Status: <code> <reason> to override the response’s 
status line. Recognizes Content-Type to set an explicit content type and tracks presence, 
and accumulates multiple Set-Cookie values. All other headers are forwarded onto res_.headers 
for potential propagation. This incremental approach builds a complete outgoing head based on the 
CGI’s choices, while leaving finalizeHeaders() responsible for defaults and required fields. 
Handling Status correctly is critical because many CGIs (e.g., PHP) use it to signal application-level errors.


*/

void CGIStreamer::parseOneHeaderLine(const std::string &line)
{
	// Expect "Key: Value"
	std::string::size_type col = line.find(':');
	if (col == std::string::npos)
		return;

	std::string key = line.substr(0, col);
	std::string val = (col + 1 < line.size() && line[col + 1] == ' ')
						? line.substr(col + 2)
						: line.substr(col + 1);

	if (iequals_ascii(key, "Status"))
	{
		// "Status: 404 Not Found"
		int code = 0;
		std::string::size_type i = 0;
		while (i < val.size() && val[i] >= '0' && val[i] <= '9')
		{
			code = code * 10 + (val[i] - '0');
			++i;
		}
		while (i < val.size() && val[i] == ' ')
			++i;
		status_code_ = code;
		status_reason_ = (i < val.size()) ? val.substr(i) : std::string();
		return;
	}

	if (iequals_ascii(key, "Content-Type"))
	{
		saw_content_type_ = true;
		res_.headers.set("Content-Type", val);
		return;
	}

	if (iequals_ascii(key, "Set-Cookie"))
	{
		set_cookie_.push_back(val); // keep multiple
		return;
	}

	// Other headers can be forwarded if desired:
	res_.headers.set(key, val);
}


/* 

void finalizeHeaders()

Builds and queues the HTTP status line and minimal essential headers exactly once. 
If CGI didn’t provide Content-Type, defaults to text/plain. It forces Transfer-Encoding: 
chunked and sets Connection, Date, Server, and emits all collected Set-Cookie headers. 
The function temporarily disables chunking to enqueue the head un-chunked (correct framing), 
then restores mode for the body. A one-time guard prevents duplicate heads. 
This guarantees well-formed responses even if the CGI never emitted headers or closed prematurely, 
keeping browser and proxy behavior correct.

*/

// Build and queue the HTTP head once (after CGI headers parsed).
void CGIStreamer::finalizeHeaders()
{
	if (http_head_queued_)
		return;

	// --- status line pieces ---
	const int status = status_code_ ? status_code_ : 200;
	const std::string reason = status_reason_.empty() ? "OK" : status_reason_;

	// --- ensure basic headers (but don't stomp CGI-provided ones) ---
	if (!saw_content_type_ && !res_.headers.keyExists("Content-Type"))
		res_.headers.set("Content-Type", "text/plain");

	if (!res_.headers.keyExists("Date"))
		res_.headers.set("Date", http_date_now_gmt());

	if (!res_.headers.keyExists("Server"))
		res_.headers.set("Server", "webserv/1.0");

	// --- decide Connection policy unless CGI already set it ---
	if (!res_.headers.keyExists("Connection"))
	{
		bool want_close = false;

		// Honor client Connection: close (case-insensitive) or HTTP/1.0 default-close
		const Headers &rq = req_.getHeaders();
		std::string c = rq.get("Connection");
		// simple ASCII case-insensitive compare to "close"
		bool rq_says_close = false;
		if (!c.empty()) {
			if (c.size() == 5) { // "close"
				char a='c',b='l',d='o',e='s',f='e';
				rq_says_close =
					( (c[0]|32)==a && (c[1]|32)==b && (c[2]|32)==d && (c[3]|32)==e && (c[4]|32)==f );
			}
		}
		if (rq_says_close)
			want_close = true;
		else if (req_.getHttpVer() == "HTTP/1.0")
			want_close = true;

		res_.headers.set("Connection", want_close ? "close" : "keep-alive");
	}

	// --- choose body framing: prefer CGI's Content-Length; else chunked ---
	const bool have_cl = res_.headers.keyExists("Content-Length");
	if (!have_cl)
	{
		// enable chunked framing for the message body that follows
		chunked_mode_ = true;
		res_.headers.set("Transfer-Encoding", "chunked");
	}
	else
	{
		// if CGI supplied Content-Length, do NOT set chunked
		chunked_mode_ = false;
		// ensure Transfer-Encoding isn't lingering from earlier parsing
		if (res_.headers.keyExists("Transfer-Encoding"))
			res_.headers.erase("Transfer-Encoding");
	}

	// --- build and queue the response head (status line + headers) ---
	std::string head;
	head.reserve(512);

	// Status line
	head += "HTTP/1.1 ";
	{
		char buf[16];
		std::sprintf(buf, "%d", status);
		head += buf;
	}
	head += " ";
	head += reason;
	head += "\r\n";

	// Required/standard headers (use values from res_.headers)
	head += "Date: " + res_.headers.get("Date") + "\r\n";
	head += "Server: " + res_.headers.get("Server") + "\r\n";

	// Content-Type
	if (res_.headers.keyExists("Content-Type"))
		head += "Content-Type: " + res_.headers.get("Content-Type") + "\r\n";

	// Either Content-Length or Transfer-Encoding (but not both)
	if (have_cl)
		head += "Content-Length: " + res_.headers.get("Content-Length") + "\r\n";
	else
		head += "Transfer-Encoding: chunked\r\n";

	// Connection
	head += "Connection: " + res_.headers.get("Connection") + "\r\n";

	// Multiple Set-Cookie (if any)
	for (std::vector<std::string>::size_type i = 0; i < set_cookie_.size(); ++i)
		head += "Set-Cookie: " + set_cookie_[i] + "\r\n";

	head += "\r\n"; // end of headers

	// Queue head bytes unchunked
	const bool saved = chunked_mode_;
	chunked_mode_ = false;
	enqueueOut(head.data(), head.size());
	chunked_mode_ = saved;

	http_head_queued_ = true;
}


// --- stdin/stdout helpers ---

#include <poll.h> // POLLIN, POLLOUT




/* 

void closeStdin() / void closeStdout()

Unregister the CGI pipe fd from the event loop and close it, resetting the fd to -1. 
Closing stdin early (no body or finished sending) unblocks CGIs that wait for EOF before producing output. 
Closing stdout at EOF or after errors prevents further reads and ensures the event loop won’t watch stale descriptors. 
Encapsulating this logic avoids duplicated poll modifications and makes 
cleanup idempotent—critical during timeouts, disconnects, or crashes.

*/

void CGIStreamer::closeStdin()
{
#if defined(DEBUG)
	std::fprintf(stderr, "[CGI][FD] closeStdin fd=%d\n", cgi_in_fd_);
#endif
	if (cgi_in_fd_ >= 0)
	{
		if (loop_)
			loop_->removeFD(cgi_in_fd_);
		::close(cgi_in_fd_);
		cgi_in_fd_ = -1;
	}
}

void CGIStreamer::closeStdout()
{
#if defined(DEBUG)
	std::fprintf(stderr, "[CGI][FD] closeStdout fd=%d\n", cgi_out_fd_);
#endif
	if (cgi_out_fd_ >= 0)
	{
		if (loop_)
			loop_->removeFD(cgi_out_fd_);
		::close(cgi_out_fd_);
		cgi_out_fd_ = -1;
	}
}


/* 

void reset()

Returns the streamer to a pristine state: closes both pipe fds, 
clears failure/active flags, resets body offsets and temp file handle/path, 
wipes header parser state, cookies, output buffers, chunked flags, deadlines, 
and waterline hints. Designed for reuse across keep-alive requests or after errors, 
it ensures no cross-request contamination. Clearing everything centrally simplifies 
higher-level code and prevents subtle bugs where previous 
CGI state leaks into a new request.

*/

void CGIStreamer::reset()
{
	// stop timers/mark inactive AFTER fds are gone
	closeStdin();
	closeStdout();

	failed_ = false;
	cgi_active_ = false;

	cgi_body_off_ = 0;
	if (body_fd_ >= 0)
	{
		::close(body_fd_);
		body_fd_ = -1;
	}
	body_file_off_ = 0;
	body_path_.clear();

	hdr_state_ = HDR_WAITING;
	cgi_header_accum_.clear();
	saw_content_type_ = false;
	status_code_ = 0;
	status_reason_.clear();
	set_cookie_.clear();

	http_head_queued_ = false;
	chunked_mode_ = true;
	sent_final_chunk_ = false;
	out_buf_.clear();
	out_off_ = 0;

	client_out_bytes_ = 0;
	write_deadline_ms_ = 0;
	hdr_deadline_.clear();
	total_deadline_.clear();
}

/* 

void resetWriteDeadline()

Arms/refreshes a write-progress watchdog (write_deadline_ms_ = now + WRITE_TIMEOUT_MS). 
If stdin writes don’t progress by the deadline, onTick() will close stdin to unblock the child. 
This protects the server from scripts that read slowly (or stop reading) while still 
allowing normal streaming. Implemented with gettimeofday for lightweight millisecond precision, 
it’s called after successful writes in onCgiWritable.

*/

void CGIStreamer::resetWriteDeadline()
{
	struct timeval tv;
	::gettimeofday(&tv, 0);
	const unsigned long long now_ms =
		(unsigned long long)tv.tv_sec * 1000ULL +
		(unsigned long long)tv.tv_usec / 1000ULL;
	write_deadline_ms_ = now_ms + WRITE_TIMEOUT_MS;
}

// --- public API ---


/* 

bool beginCgi(const CgiSpec& spec, const std::string& script_path, const std::vector<std::string>& envv)

Resets runtime state, spawns the CGI via proc_.spawn, retrieves stdin/stdout fds, 
and applies O_NONBLOCK and FD_CLOEXEC. Registers POLLIN for stdout and POLLOUT for stdin with the event loop, 
unless the request has no body—in which case it proactively closes stdin to unblock the child. 
It also discovers the body source (disk path or memory) from HttpRequest, initializes header parsing variables, 
clears output buffers, and sets header/total runtime deadlines plus the write watchdog. 
Returns true when the streamer is active and ready for polling; otherwise failed_ is set.

*/

bool CGIStreamer::beginCgi(const CgiSpec &spec,
						const std::string &script_path,
						const std::vector<std::string> &envv)
{
	// ---- reset runtime state ----
	failed_         = false;
	cgi_active_     = false;

	cgi_in_fd_      = -1;   // server → child stdin (we write here)
	cgi_out_fd_     = -1;   // child  → server stdout (we read here)

	cgi_body_off_   = 0;    // generic progress counter (kept for logs)
	if (body_fd_ >= 0) { ::close(body_fd_); body_fd_ = -1; }
	body_file_off_  = 0;
	body_path_.clear();
	if (req_.isBodyOnDisk()) body_path_ = req_.getBodyFilePath();

	// stdout parsing / response framing
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
	stdout_paused_     = false;

	// ---- spawn child ----
	if (!proc_.spawn(spec, script_path, envv)) {
		failed_ = true;
		return false;
	}
	cgi_in_fd_  = proc_.inFD();
	cgi_out_fd_ = proc_.outFD();

	// ---- set pipes non-blocking + CLOEXEC ----
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

	// ---- prime stdin feed (the missing piece that fixes big-body hangs) ----
	// If body is kept in memory, take a stable snapshot ONCE and write it across ticks.
	in_mem_body_.clear();
	in_mem_off_     = 0;
	using_mem_body_ = false;

	if (!req_.isBodyOnDisk()) {
		const std::size_t blen = req_.getBodyLength();
		if (blen > 0) {
			in_mem_body_ = req_.readBodyToVector();   // snapshot
			using_mem_body_ = !in_mem_body_.empty();
		}
	} else {
		// for on-disk bodies we’ll open body_fd_ lazily on first POLLOUT in pump
		body_file_off_ = 0;
	}

	// ---- register fds with event loop right after O_NONBLOCK ----
	if (loop_) {
		loop_->modFD(cgi_out_fd_, POLLIN);           // always read child's stdout
		if (cgi_in_fd_ >= 0) loop_->modFD(cgi_in_fd_, POLLOUT); // to feed stdin progressively
	}

	// If there is truly no body, close stdin now so the CGI doesn’t block on read()
	if (!using_mem_body_ && !req_.isBodyOnDisk() && req_.getBodyLength() == 0) {
		closeStdin(); // also clears POLLOUT via loop_->modFD(fd, 0)
	}

	// ---- arm deadlines ----
	struct timeval tv; ::gettimeofday(&tv, 0);
	const unsigned long long now_ms =
		(unsigned long long)tv.tv_sec * 1000ULL +
		(unsigned long long)tv.tv_usec / 1000ULL;

	const int HDR_WAIT_MS    = 3000;   // time to see CGI headers
	const int TOTAL_LIMIT_MS = 15000;  // total runtime cap

	hdr_deadline_.reset(now_ms, HDR_WAIT_MS);
	total_deadline_.reset(now_ms, TOTAL_LIMIT_MS);
	resetWriteDeadline();

	cgi_active_ = true;
	return true;
}


/* 


void startHeaderPhase(unsigned long long now_ms)

Re-arms the header-phase deadline from now_ms. 
Useful if the pipeline wants to allow fresh time for header arrival 
(e.g., after initial setup). If headers still don’t arrive before the new deadline, onTick() 
will time out and fail safely. Keeping this separate from 
beginCgi provides flexible timeout policy without respawning.

*/

void CGIStreamer::startHeaderPhase(unsigned long long now_ms)
{
	// Re-arm header deadline with default 3s (same as beginCgi()).
	hdr_deadline_.reset(now_ms, 10000);
}


/* 


void onTick(unsigned long long now_ms)

Periodic timer hook enforcing timeouts. If headers haven’t arrived by hdr_deadline_ or total runtime exceeds total_deadline_, 
it calls timeout504() to terminate the CGI and mark failure. It also checks the stdin write watchdog: 
if the child fails to read in time, it closes stdin, avoiding indefinite stalls. 
This makes CGI processing cooperative and bounded under the single-poll 
architecture, guaranteeing requests never hang forever.

*/

void CGIStreamer::onTick(unsigned long long now_ms)
{
	if (hdr_state_ == HDR_WAITING && hdr_deadline_.expired(now_ms))
	{
		timeout504();
		return;
	}
	if (total_deadline_.expired(now_ms))
	{
		timeout504();
		return;
	}
	if (write_deadline_ms_ && now_ms > write_deadline_ms_)
	{
		// Child not draining stdin in time → close stdin to unblock it.
		closeStdin();
		write_deadline_ms_ = 0; // avoid repeated triggers
	}
}


/* 

std::size_t takeOutBytes(ChainBuf& out, std::size_t max_bytes)

Transfers up to max_bytes from the internal out_buf_ into the connection’s ChainBuf, 
advancing out_off_ and compacting when drained. Returning the number moved informs 
the caller whether to keep POLLOUT enabled for the client socket. 
This function decouples CGI framing from actual socket writes, letting ClientConnection control 
fairness and per-tick throughput while the streamer simply provides ready bytes.

*/

std::size_t CGIStreamer::takeOutBytes(ChainBuf &out, std::size_t max_bytes)
{
	if (out_off_ >= out_buf_.size())
	{
		out_buf_.clear();
		out_off_ = 0;
		return 0;
	}
	std::size_t avail = out_buf_.size() - out_off_;
	std::size_t n = (max_bytes < avail) ? max_bytes : avail;

	if (n == 0)
		return 0;

	out.push_copy(&out_buf_[out_off_], n);
	out_off_ += n;

	if (out_off_ == out_buf_.size())
	{
		out_buf_.clear();
		out_off_ = 0;
	}
	return n;
}

// --- I/O driving ---


/* 

void onCgiReadable(int fd)

Handles POLLIN on CGI stdout. During header phase, it accumulates bytes until \r\n\r\n or \n\n, 
then parses headers line-by-line via parseOneHeaderLine and calls finalizeHeaders(). 
If no header terminator appears and heuristics indicate “not headers” (no colon on first line, or too big), 
it synthesizes a safe head and treats data as body. After headers, 
it streams body chunks with backpressure checks (wantsRead()), 
and on EOF closes stdout, enqueues the final chunk (if chunked), 
and marks inactive. Errors set failed_. This is the heart of CGI output handling

*/

void CGIStreamer::onCgiReadable(int fd)
{
	if (!cgi_active_ || fd != cgi_out_fd_ || cgi_out_fd_ < 0)
		return;

	// Back-pressure gate
	if (!wantsRead())
	{
		#if defined(DEBUG)
		std::fprintf(stderr, "[CGI][RD] outFD=%d wantsRead=0 → defer\n", cgi_out_fd_);
		#endif
		return;
	}

	for (;;)
	{
		char buf[16384];
		ssize_t r = ::read(cgi_out_fd_, buf, sizeof(buf));

		if (r > 0)
		{
			const char *p = buf;
			std::size_t n = static_cast<std::size_t>(r);
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][RD] outFD=%d got=%zu bytes (state=%s)\n",
				cgi_out_fd_, n, (hdr_state_ == HDR_WAITING ? "HEADERS" : "BODY"));
			#endif
				
				if (hdr_state_ == HDR_WAITING)
			{
				// Accumulate until CRLFCRLF or LFLF
				cgi_header_accum_.append(p, n);
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][RD] accum=%zu bytes\n",
					cgi_header_accum_.size());
				#endif

				// Find header/body cut
				std::string::size_type cut = std::string::npos;
				std::string::size_type k = cgi_header_accum_.find("\r\n\r\n");
				if (k != std::string::npos)
					cut = k + 4;
				else
				{
					k = cgi_header_accum_.find("\n\n");
					if (k != std::string::npos)
						cut = k + 2;
				}

				if (cut == std::string::npos)
				{
					// Heuristic: if first line lacks ":" or headers grow too large,
					// treat the whole thing as body.
					std::string::size_type eol = cgi_header_accum_.find_first_of("\r\n");
					std::string first = (eol == std::string::npos)
										? cgi_header_accum_
										: cgi_header_accum_.substr(0, eol);
					const bool first_has_colon = (first.find(':') != std::string::npos);

					if (!first_has_colon || cgi_header_accum_.size() > 8192u)
					{
						#if defined(DEBUG)
						std::fprintf(stderr,
							"[CGI][RD] no header terminator (first_has_colon=%d, size=%zu) → treat as body\n",
							(int)first_has_colon, cgi_header_accum_.size());
						#endif

						if (!http_head_queued_)
						{
							#if defined(DEBUG)
							std::fprintf(stderr, "[CGI][RD] finalizeHeaders (synth)\n");
							#endif
							finalizeHeaders(); // synthesize safe head
						}
						if (!cgi_header_accum_.empty())
						{
							// enqueueOut will suppress if this is HEAD and the head was already queued
							enqueueOut(cgi_header_accum_.data(), cgi_header_accum_.size());
							#if defined(DEBUG)
							std::fprintf(stderr, "[CGI][RD] queued body spill=%zu\n",
								cgi_header_accum_.size());
							#endif
						}
						cgi_header_accum_.clear();
						hdr_state_ = HDR_DONE;
					}
					// else: keep waiting for header terminator
					continue;
				}

				// We have a complete header block → parse it line-by-line
				const std::string hdr = cgi_header_accum_.substr(0, cut);
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][RD] header block complete len=%zu (cut at %zu)\n",
					hdr.size(), cut);
				#endif

				int set_cookie_count = 0;
				int parsed_lines = 0;

				std::string line;
				for (std::string::size_type i = 0; i < hdr.size();)
				{
					std::string::size_type j = i;
					while (j < hdr.size() && hdr[j] != '\r' && hdr[j] != '\n')
						++j;
					line.assign(hdr, i, j - i);
					if (j < hdr.size() && hdr[j] == '\r')
						++j;
					if (j < hdr.size() && hdr[j] == '\n')
						++j;
					i = j;

					if (!line.empty())
					{
						std::string lower = line;
						for (size_t q = 0; q < lower.size(); ++q)
						{
							char c = lower[q];
							if (c >= 'A' && c <= 'Z')
								lower[q] = char(c - 'A' + 'a');
						}
						if (lower.size() >= 11 && lower.compare(0, 11, "set-cookie:") == 0)
							++set_cookie_count;

						parseOneHeaderLine(line);
						++parsed_lines;
					}
				}
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][RD] parsed_lines=%d set-cookie=%d\n",
					parsed_lines, set_cookie_count);
				#endif

				// Anything after the terminator is body
				if (!http_head_queued_)
				{
					#if defined(DEBUG)
					std::fprintf(stderr, "[CGI][RD] finalizeHeaders\n");
					#endif
					finalizeHeaders();
				}
				if (cut < cgi_header_accum_.size())
				{
					const char *bp = cgi_header_accum_.data() + cut;
					const std::size_t bn = cgi_header_accum_.size() - cut;
					if (bn)
					{
						// enqueueOut will suppress post-head body for HEAD
						enqueueOut(bp, bn);
						#if defined(DEBUG)
						std::fprintf(stderr, "[CGI][RD] queued post-header body=%zu\n", bn);
						#endif
					}
				}

				cgi_header_accum_.clear();
				hdr_state_ = HDR_DONE;
				continue;
			}

			// Normal body streaming (headers already finalized)
			if (!http_head_queued_)
			{
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][RD] finalizeHeaders (late safety)\n");
				#endif
				finalizeHeaders(); // safety net
			}
			// enqueueOut will suppress for HEAD when head is already queued
			enqueueOut(p, n);
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][RD] queued body=%zu\n", n);
			#endif
			continue;
		}

		if (r == 0) // EOF from CGI
		{
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][RD] EOF on outFD=%d\n", cgi_out_fd_);
			#endif

			// Ensure we emitted an HTTP head even if CGI sent no headers
			if (!http_head_queued_)
			{
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][RD] finalizeHeaders at EOF (no CGI headers)\n");
				#endif
				finalizeHeaders();
			}

			// Terminate chunked stream (if applicable); skip for HEAD inside helper
			enqueueFinalChunk();    

			// Stop watching/using stdout and mark producer done
			closeStdout();
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][RD] queued final chunk\n");
			#endif

			cgi_active_ = false;       // streamer done producing
			return;
		}

		// r < 0 (non-blocking read)
		if (errno == EINTR)
		{
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][RD] read EINTR → retry\n");
			#endif
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][RD] read EAGAIN → pause loop\n");
			#endif
			break;
		}

		// Hard error → stop this CGI cleanly
		#if defined(DEBUG)
		std::fprintf(stderr, "[CGI][RD] read error errno=%d (%s) → fail\n",
			errno, std::strerror(errno));
		#endif
		closeStdout();
		cgi_active_ = false;
		failed_ = true;
		return;
	}
}



/* 

void onCgiWritable(int fd)

Handles POLLOUT on CGI stdin. Determines body source: disk-backed (open once, pread from logical offset) 
or in-memory (req_.getBody()). Writes in bounded chunks (16KB), handles EAGAIN/EINTR/EPIPE, updates cgi_body_off_, 
and refreshes the write watchdog on progress. When all bytes are sent, it closes stdin to signal EOF, 
unblocking CGIs that wait for end-of-input before producing output. This function ensures large uploads 
are streamed safely without blocking or monopolizing the event loop, respecting fairness across connections.

*/

void CGIStreamer::onCgiWritable(int fd)
{
	if (!cgi_active_ || fd != cgi_in_fd_)
		return;

	// Enter + basic state
	const std::size_t total = req_.getBodyLength();
	#if defined(DEBUG)
	std::fprintf(stderr, "[CGI][WR] enter inFD=%d off=%zu total=%zu disk=%d\n",
		cgi_in_fd_, cgi_body_off_, total, (int)req_.isBodyOnDisk());
	#endif

	// If no stdin expected, close immediately.
	if (total == 0)
	{
		#if defined(DEBUG)
		std::fprintf(stderr, "[CGI][WR] total=0 → closeStdin\n");
		#endif
		closeStdin();
		return;
	}

	if (cgi_body_off_ >= total)
	{
		#if defined(DEBUG)
		std::fprintf(stderr, "[CGI][WR] already sent all (%zu/%zu) → closeStdin\n",
			cgi_body_off_, total);
		#endif
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
		if (body_fd_ < 0)
		{
			body_fd_ = ::open(body_path_.c_str(),
							O_RDONLY
#ifdef O_CLOEXEC
								| O_CLOEXEC
#endif
			);
			if (body_fd_ < 0)
			{
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] open(%s) failed: errno=%d (%s) → closeStdin\n",
					body_path_.c_str(), errno, std::strerror(errno));
				#endif
				closeStdin();
				return;
			}
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][WR] opened body file %s fd=%d\n",
				body_path_.c_str(), body_fd_);
			#endif
		}

		const std::size_t remaining = total - cgi_body_off_;
		const std::size_t to_read = (remaining < CHUNK) ? remaining : CHUNK;
		if (to_read == 0)
		{
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][WR] to_read=0 (remaining=%zu) → closeStdin\n", remaining);
			#endif
			closeStdin();
			return;
		}

		char small[16384];
		// pread() from the *logical* offset (stateless; safe on partial writes)
		ssize_t r;
		do
		{
			r = ::pread(body_fd_, small, to_read,
						static_cast<off_t>(cgi_body_off_));
		} while (r < 0 && errno == EINTR);

		if (r < 0)
		{
			if (errno == EAGAIN)
			{
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] pread EAGAIN → retry later\n");
				#endif
				return;
			}
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][WR] pread error errno=%d (%s) → closeStdin\n",
				errno, std::strerror(errno));
			#endif
			closeStdin();
			return;
		}
		if (r == 0)
		{
			// Producer (request reader) hasn’t flushed these bytes to disk yet; retry later.
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][WR] pread=0 (not flushed yet) → retry later\n");
			#endif
			return;
		}

		std::size_t want = static_cast<std::size_t>(r);
		std::size_t sent = 0;
		#if defined(DEBUG)
		std::fprintf(stderr, "[CGI][WR] disk chunk ready want=%zu\n", want);
		#endif

		while (sent < want)
		{
			ssize_t w = ::write(cgi_in_fd_, small + sent, want - sent);
			if (w > 0)
			{
				sent += static_cast<std::size_t>(w);
				cgi_body_off_ += static_cast<std::size_t>(w);
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] wrote=%zd new_off=%zu/%zu\n",
					w, cgi_body_off_, total);
				#endif
				resetWriteDeadline();
				if (cgi_body_off_ >= total)
				{
					#if defined(DEBUG)
					std::fprintf(stderr, "[CGI][WR] done sending body → closeStdin\n");
					#endif
					closeStdin();
					return;
				}
			}
			else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			{
				// Pipe full; we’ll retry from the same logical offset on next POLLOUT
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] write EAGAIN with %zu/%zu sent → retry later\n",
					sent, want);
				#endif
				break;
			}
			else if (w < 0 && errno == EINTR)
			{
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] write EINTR → continue\n");
				#endif
				continue; // retry same write
			}
			else
			{
				// Broken pipe / other error
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] write error errno=%d (%s) → closeStdin\n",
					errno, std::strerror(errno));
				#endif
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
	#if defined(DEBUG)
	std::fprintf(stderr, "[CGI][WR] mem tmp.size=%zu off=%zu total=%zu\n",
		tmp.size(), cgi_body_off_, total);
	#endif

	// Guard empty/not-yet-available bytes to avoid UB on &tmp[0].
	if (tmp.size() <= cgi_body_off_)
	{
		#if defined(DEBUG)
		std::fprintf(stderr, "[CGI][WR] no bytes available yet (tmp.size=%zu, off=%zu) → retry later\n",
			tmp.size(), cgi_body_off_);
		#endif
		return;
	}

	std::size_t remaining = total - cgi_body_off_;
	std::size_t avail = tmp.size() - cgi_body_off_;
	std::size_t len = (remaining < avail) ? remaining : avail;
	if (len == 0)
	{
		#if defined(DEBUG)
		std::fprintf(stderr, "[CGI][WR] len=0 after calc → retry later\n");
		#endif
		return;
	}
	if (len > CHUNK)
		len = CHUNK;

	char small[16384];
	std::memcpy(small, &tmp[0] + cgi_body_off_, len);
	#if defined(DEBUG)
	std::fprintf(stderr, "[CGI][WR] mem chunk write attempt len=%zu\n", len);
	#endif

	std::size_t sent = 0;
	while (sent < len)
	{
		ssize_t w = ::write(cgi_in_fd_, small + sent, len - sent);
		if (w > 0)
		{
			sent += static_cast<std::size_t>(w);
			cgi_body_off_ += static_cast<std::size_t>(w);
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][WR] wrote=%zd new_off=%zu/%zu\n",
				w, cgi_body_off_, total);
			#endif
			resetWriteDeadline();
			if (cgi_body_off_ >= total)
			{
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] done sending body → closeStdin\n");
				#endif
				closeStdin();
				return;
			}
		}
		else
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] write EAGAIN after sent=%zu/%zu → retry later\n",
					sent, len);
				#endif
				return;
			}
			if (errno == EINTR)
			{
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] write EINTR → continue\n");
				#endif
				continue;
			}
			if (errno == EPIPE)
			{
				#if defined(DEBUG)
				std::fprintf(stderr, "[CGI][WR] write EPIPE → closeStdin\n");
				#endif
				closeStdin();
				return;
			}
			#if defined(DEBUG)
			std::fprintf(stderr, "[CGI][WR] write error errno=%d (%s) → closeStdin\n",
				errno, std::strerror(errno));
			#endif
			closeStdin();
			return;
		}
	}
}

/* 

void timeout504()

Uniform timeout handler: closes stdin/stdout, calls proc_.terminate() 
to kill/reap the child, marks inactive and failed. 
Higher layers then generate a 504 Gateway Timeout (or mapped error page). 
Centralizing teardown guarantees descriptors are gone, prevents partial states, 
and leaves the connection ready to deliver an error response or close. 
This meets the requirement that no request can hang indefinitely

*/

// --- timeout handling ---

void CGIStreamer::timeout504()
{
	closeStdin();
	closeStdout();
	proc_.terminate();
	cgi_active_ = false;
	failed_ = true;
}
