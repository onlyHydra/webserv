#include "ChunkedReader.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>


// ---- small helpers ---------------------------------------------------------

static inline std::size_t min_sz(std::size_t a, std::size_t b) {
	return a < b ? a : b;
}


// ---- ctor / dtor -----------------------------------------------------------

ChunkedReader::ChunkedReader(std::vector<char> &mem_body,
							std::size_t spill_threshold,
							const std::string &tmp_dir)
: st_(S_READING_SIZE)
, total_(0)
, chunk_rem_(0)
, linebuf_()
, spill_threshold_(spill_threshold)
, tmp_dir_(tmp_dir)
, mem_(mem_body)
, on_disk_(false)
, fd_(-1)
, path_()
, trailers_()
, pending_()
, pending_off_(0)
, hard_limit_(0)
, over_limit_(false)
{
	// pre-reserve a little for size lines/trailers
	linebuf_.reserve(64);
	pending_.reserve(8192);
}

ChunkedReader::~ChunkedReader() {
	if (fd_ >= 0) {
		::close(fd_);
		fd_ = -1;
	}
	// do not unlink path_: the driver owns the produced file if any
}

// ---- disk helpers ----------------------------------------------------------

bool ChunkedReader::ensure_spill_file() {
	if (on_disk_) return true;

	// Build mkstemp template: <tmp_dir_>/chunk-XXXXXX
	std::string templ = tmp_dir_;
	if (!templ.empty() && templ[templ.size() - 1] != '/')
		templ += '/';
	templ += "chunk-XXXXXX";

	// mkstemp modifies the string in place
	std::vector<char> buf(templ.begin(), templ.end());
	buf.push_back('\0');

	int fd = ::mkstemp(&buf[0]);
	if (fd < 0) {
		st_ = S_ERROR;
		return false;
	}

	// Best effort: user/group read-write
	::fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

	fd_ = fd;
	path_.assign(&buf[0]);
	on_disk_ = true;
	pending_.clear();
	pending_off_ = 0;
	return true;
}

std::size_t ChunkedReader::flush_to_disk(std::size_t max_bytes) {
	if (!on_disk_ || fd_ < 0) return 0;
	std::size_t written_total = 0;

	while (pending_off_ < pending_.size() && written_total < max_bytes) {
		std::size_t avail = pending_.size() - pending_off_;
		std::size_t to_write = min_sz(avail, max_bytes - written_total);

		ssize_t w = ::write(fd_, &pending_[0] + pending_off_, to_write);
		if (w < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break; // try later
			// hard IO error; mark reader error
			st_ = S_ERROR;
			break;
		}
		if (w == 0) 
			break; // shouldn't happen; avoid tight loop
		pending_off_ += static_cast<std::size_t>(w);
		written_total += static_cast<std::size_t>(w);
	}

	// Drop fully-flushed prefix
	if (pending_off_ && pending_off_ == pending_.size()) {
		pending_.clear();
		pending_off_ = 0;
	}
	return written_total;
}

void ChunkedReader::drop_pending(std::size_t n) {
	// helper if your driver wants to forcibly drop staged bytes;
	// generally not needed because flush_to_disk advances offsets.
	n = min_sz(n, pending_size());
	pending_off_ += n;
	if (pending_off_ == pending_.size()) {
		pending_off_ = 0;
		pending_.clear();
	}
}

// ---- parsing helpers -------------------------------------------------------

bool ChunkedReader::parse_size_line() {
	// 'linebuf_' currently holds the line (without CRLF).
	// Format: <hex> [; extensions...]
	// We stop at ';' if present.
	std::size_t i = 0;
	// skip leading spaces (robustness)
	while (i < linebuf_.size() && (linebuf_[i] == ' ' || linebuf_[i] == '\t'))
		++i;

	// read hex number
	std::size_t value = 0;
	bool saw_digit = false;
	for (; i < linebuf_.size(); ++i) {
		const char c = linebuf_[i];
		if (c == ';') break;
		if (c == ' ' || c == '\t') break; // allow spaces before extensions
		unsigned v;
		if (c >= '0' && c <= '9')  { 
			v = static_cast<unsigned>(c - '0'); 
		}
		else if (c >= 'a' && c <= 'f')  { 
			v = 10u + static_cast<unsigned>(c - 'a'); 
		}
		else if (c >= 'A' && c <= 'F')  { 
			v = 10u + static_cast<unsigned>(c - 'A'); 
		}
		else { 
			
			return false; }
		saw_digit = true;
		// guard overflow: clamp if it would wrap (extremely unlikely here)
		if (value > (static_cast<std::size_t>(-1) >> 4))
			value = static_cast<std::size_t>(-1);
		else
			value = (value << 4) | v;
	}
	if (!saw_digit)
		return false;

	chunk_rem_ = value;
	if (chunk_rem_ == 0) {
		st_ = S_TRAILERS;
	} else {
		st_ = S_READING_DATA;
	}
	return true;
}

bool ChunkedReader::parse_trailers() {
	// We’re called when we have an empty line buffered (CRLF already seen),
	// but to be robust we’ll keep using linebuf_ to collect lines until we
	// hit an empty line.
	// Nothing to do here; trailers are accepted but ignored (stored in trailers_).
	// The actual scanning is done in consume().
	return true;
}

// ---- storage (mem vs disk) -------------------------------------------------

// ChunkedReader.cpp
#include <cstring>  // for std::memcpy

bool ChunkedReader::write_payload(const char *p, std::size_t n) {
	// Hard-limit enforcement
	if (hard_limit_ && total_ >= hard_limit_) {
		over_limit_ = true;
		st_ = S_ERROR;
		return false;
	}
	if (hard_limit_) {
		const std::size_t room = hard_limit_ - total_;
		if (n > room) {
			n = room;
			over_limit_ = true;
		}
	}

	//  Use memory only if threshold > 0 AND we would not exceed it.
	if (!on_disk_ && (spill_threshold_ > 0) && (total_ + n) <= spill_threshold_) {
		mem_.insert(mem_.end(), p, p + n);
	} else {
		if (!on_disk_) {
			if (!ensure_spill_file()) return false;   // st_ set to S_ERROR on failure
		}
		const std::size_t old_size = pending_.size();
		pending_.resize(old_size + n);
		std::memcpy(&pending_[0] + old_size, p, n);
	}

	total_ += n;

	if (hard_limit_ && total_ == hard_limit_) {
		over_limit_ = true;
		// still return true: the bytes we accepted are valid, driver will stop
	}
	return true;
}



// ---- main state machine ----------------------------------------------------

std::size_t ChunkedReader::consume(const char* p, std::size_t n)
{
	if (!p || n == 0)
		return 0;
	if (st_ == S_ERROR || st_ == S_DONE)
		return 0;

	std::size_t taken = 0;

	while (taken < n) {
		switch (st_) {
		case S_READING_SIZE: {
			// Look for '\n' to finish the size line; if not present,
			// buffer everything we got and report it as consumed.
			const char* nl = (const char*)std::memchr(p + taken, '\n', n - taken);
			if (!nl) {
				linebuf_.append(p + taken, n - taken);
				taken = n;
				return taken; // consumed all input this call
			}

			// We have a full line segment ending at nl (inclusive).
			const std::size_t seg = (std::size_t)(nl - (p + taken)) + 1;
			linebuf_.append(p + taken, seg);
			taken += seg;

			// Expect CRLF in the assembled line
			if (linebuf_.size() < 2 ||
				linebuf_[linebuf_.size() - 2] != '\r' ||
				linebuf_[linebuf_.size() - 1] != '\n') {
				st_ = S_ERROR;
				return taken;
			}

			// Strip CRLF and parse hex size (ignore optional ";ext")
			linebuf_.resize(linebuf_.size() - 2);
			std::string::size_type semi = linebuf_.find(';');
			const std::string hex = (semi == std::string::npos)
				? linebuf_
				: linebuf_.substr(0, semi);

			std::istringstream is(hex);
			std::size_t sz = 0;
			is >> std::hex >> sz;
			if (!is) {
				st_ = S_ERROR;
				return taken;
			}

			chunk_rem_ = sz;
			linebuf_.clear();

			if (chunk_rem_ == 0) {
				// Final chunk — next come trailers (possibly empty)
				st_ = S_TRAILERS;
			} else {
				st_ = S_READING_DATA;
			}
			break;
		}

		case S_READING_DATA: {
			// Take as much as we can for this chunk’s data
			std::size_t want = n - taken;
			if (want > chunk_rem_) 
				want = chunk_rem_;
			if (want == 0) 
				return taken;

			// Stage payload (mem or pending_ if on-disk)
			// Note: write_payload() may return false to hint “don’t read more”,
			// but we *did* accept 'want' bytes, so we still advance 'taken'.
			(void)write_payload(p + taken, want);
			taken     += want;
			chunk_rem_ -= want;

			if (chunk_rem_ == 0) {
				st_ = S_EXPECTING_CRLF; // expect data CRLF
			}
			break;
		}

		case S_EXPECTING_CRLF: {
			// After data chunk, we must see "\r\n".
			const std::size_t rem = n - taken;

			if (rem == 0)
				return taken;

			if (rem == 1) {
				// If it's just '\r', buffer it and return 1 consumed
				if (p[taken] == '\r') {
					linebuf_.push_back('\r');
					++taken;
					return taken; // consume what we got; finish on next call
				}
				st_ = S_ERROR;
				return taken;
			}

			// We have at least 2 bytes available
			if (linebuf_.empty()) {
				if (p[taken] == '\r' && p[taken + 1] == '\n') {
					taken += 2;
					st_ = S_READING_SIZE;
				} else {
					st_ = S_ERROR;
				}
			} else {
				// We buffered a lone '\r' previously; expect '\n' now
				if (linebuf_.size() == 1 && linebuf_[0] == '\r' && p[taken] == '\n') {
					linebuf_.clear();
					++taken;
					st_ = S_READING_SIZE;
				} else {
					st_ = S_ERROR;
				}
			}
			break;
		}

		case S_TRAILERS: {
		// Collect trailer lines until we hit an empty line ("\r\n").
		// Empty trailers => immediate "\r\n" -> DONE.

		// Append whatever we have this call
		linebuf_.append(p + taken, n - taken);
		taken = n;

		// Process any complete lines in linebuf_
		for (;;) {
			std::string::size_type eol = linebuf_.find("\r\n");
			if (eol == std::string::npos) {
				// need more bytes to complete a line
				break;
			}

			if (eol == 0) {
				// empty line: end of trailers -> DONE
				// consume the CRLF and finish
				linebuf_.erase(0, 2);
				st_ = S_DONE;
				break;
			}

			// non-empty trailer line: store/ignore it, then drop it
			if (trailers_.empty())
				trailers_.reserve(trailers_.size() + eol + 2);
			trailers_.append(linebuf_, 0, eol + 2);
			linebuf_.erase(0, eol + 2);

			// keep looping: there might be multiple trailer lines in buffer
		}
		return taken;
	}


		case S_DONE:
		case S_ERROR:
			return taken;
		}
	}

	return taken;
}