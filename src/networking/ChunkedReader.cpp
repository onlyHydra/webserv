#include "ChunkedReader.h"

#include <cerrno>
#include <cstring>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>

// ===== small helpers (C++98-safe) ===========================================
namespace {
	inline bool is_hex(char c) {
		return (c >= '0' && c <= '9') ||
			(c >= 'a' && c <= 'f') ||
			(c >= 'A' && c <= 'F');
	}
	inline unsigned hex_val(char c) {
		if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
		if (c >= 'a' && c <= 'f') return 10u + static_cast<unsigned>(c - 'a');
		return 10u + static_cast<unsigned>(c - 'A');
	}
}

// ===== ctor / dtor ===========================================================

ChunkedReader::ChunkedReader(std::vector<char> &mem_body,
							std::size_t spill_threshold,
							const std::string &tmp_dir)
: st_(S_READING_SIZE),
total_(0),
chunk_rem_(0),
linebuf_(),
spill_threshold_(spill_threshold),
tmp_dir_(tmp_dir),
mem_(mem_body),
on_disk_(false),
fd_(-1),
path_(),
trailers_(),
pending_(),
pending_off_(0),
hard_limit_(0),
over_limit_(false),
bytes_received_(0)
{
	linebuf_.clear();
	trailers_.clear();
	pending_.clear();
}

ChunkedReader::~ChunkedReader() {
	if (fd_ >= 0) ::close(fd_);
	if (on_disk_ && !path_.empty()) ::unlink(path_.c_str());
}

// ===== internal I/O buffers ==================================================

void ChunkedReader::drop_pending(std::size_t n) {
	if (n == 0) return;
	if (n > pending_size()) n = pending_size();
	pending_off_ += n;

	// compact occasionally to avoid growth
	if (pending_off_ > 65536 && pending_off_ * 2 > pending_.size()) {
		std::vector<char> tmp(pending_.begin() + pending_off_, pending_.end());
		pending_.swap(tmp);
		pending_off_ = 0;
	}
}

bool ChunkedReader::ensure_spill_file() {
	if (on_disk_ && fd_ >= 0) return true;

	// make "tmp_dir_/chunk-XXXXXX"
	std::string tpl = tmp_dir_;
	if (!tpl.empty() && tpl[tpl.size() - 1] != '/') tpl += "/";
	tpl += "chunk-XXXXXX";

	std::vector<char> c(tpl.begin(), tpl.end());
	c.push_back('\0');

	int fd = ::mkstemp(&c[0]);
	if (fd < 0) return false;

	path_.assign(&c[0]);
	fd_ = fd;
	on_disk_ = true;
	return true;
}

bool ChunkedReader::write_payload(const char *p, std::size_t n) {
	// track decoded size regardless of storage
	total_          += n;
	bytes_received_ += n;

	if (hard_limit_ && bytes_received_ > hard_limit_) {
		over_limit_ = true; // caller can turn this into 413
	}

	if (!on_disk_) {
		// Decide to spill?
		if (spill_threshold_ == 0 || (mem_.size() + n) > spill_threshold_) {
			// move current mem_ to pending_ then switch to file
			if (!mem_.empty()) {
				std::size_t old = pending_.size();
				pending_.resize(old + mem_.size());
				std::memcpy(&pending_[0] + old, &mem_[0], mem_.size());
				mem_.clear();
				pending_off_ = 0;
			}
			if (!ensure_spill_file()) return false; // backpressure; caller may 507 later
		}
	}

	if (on_disk_) {
		std::size_t old = pending_.size();
		pending_.resize(old + n);
		std::memcpy(&pending_[0] + old, p, n);
		return true; // flushing is separate
	} else {
		std::size_t old = mem_.size();
		mem_.resize(old + n);
		std::memcpy(&mem_[0] + old, p, n);
		return true;
	}
}

std::size_t ChunkedReader::flush_to_disk(std::size_t max_bytes) {
	if (!on_disk_ || fd_ < 0) return 0;
	std::size_t avail = pending_size();
	if (avail == 0) return 0;

	if (max_bytes == 0 || max_bytes > avail) max_bytes = avail;

	ssize_t wr = ::write(fd_, pending_data(), (int)max_bytes);
	if (wr <= 0) {
		// EAGAIN/EINTR => just try again later; other errors -> keep returning 0
		return 0;
	}
	drop_pending((std::size_t)wr);
	return (std::size_t)wr;
}

// ===== parsing helpers =======================================================

bool ChunkedReader::parse_size_line() {
	// need CRLF at the end
	if (linebuf_.size() < 2) return false;
	const std::size_t L = linebuf_.size();
	if (linebuf_[L - 2] != '\r' || linebuf_[L - 1] != '\n') return false;

	// copy without CRLF
	std::string line = linebuf_.substr(0, L - 2);
	linebuf_.clear();

	// strip optional chunk extensions (from first ';')
	std::string::size_type sc = line.find(';');
	if (sc != std::string::npos) line.erase(sc);

	// trim optional whitespace around the size
	std::size_t b = 0;
	while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) ++b;
	std::size_t e = line.size();
	while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
	if (b >= e) return false;

	// hex parse into size_t (via unsigned long)
	unsigned long val = 0;
	bool any = false;
	for (std::size_t i = b; i < e; ++i) {
		const char c = line[i];
		if (!is_hex(c)) return false;
		val = (val << 4) + hex_val(c);
		any = true;
	}
	if (!any) return false;

	chunk_rem_ = static_cast<std::size_t>(val);
	return true;
}

bool ChunkedReader::parse_trailers() {
	// read CRLF-terminated lines until an empty line
	for (;;) {
		std::string::size_type eol = linebuf_.find("\r\n");
		if (eol == std::string::npos) return false; // need more data
		if (eol == 0) {
			// end of trailers
			linebuf_.erase(0, 2);
			st_ = S_DONE;
			return true;
		}
		// keep trailers if desired; otherwise could drop them
		trailers_.append(linebuf_, 0, eol + 2);
		linebuf_.erase(0, eol + 2);
	}
}

// ===== main state machine ====================================================

std::size_t ChunkedReader::consume(const char *p, std::size_t n) {
	if (!p || n == 0) return 0;
	if (st_ == S_DONE)  return n;   // upper layers may discard
	if (st_ == S_ERROR) return 0;

	std::size_t taken = 0;

	while (taken < n) {
		switch (st_) {
		case S_READING_SIZE: {
			const char *base  = p + taken;
			std::size_t avail = n - taken;

			const void *nlv = std::memchr(base, '\n', avail);
			if (!nlv) {
				// no LF in this chunk; buffer and report progress
				linebuf_.append(base, avail);
				taken = n;
				return taken;
			}

			const char *nl = static_cast<const char*>(nlv);
			std::size_t seg = static_cast<std::size_t>(nl - base) + 1; // include LF
			linebuf_.append(base, seg);
			taken += seg;

			if (!parse_size_line()) {
				// malformed size line; consume what we saw to avoid spin and error out
				st_ = S_ERROR;
				return taken;
			}

			st_ = (chunk_rem_ == 0) ? S_TRAILERS : S_READING_DATA;
			break;
		}

		case S_READING_DATA: {
			std::size_t want = n - taken;
			if (want > chunk_rem_) want = chunk_rem_;

			if (want == 0) {
				// finished the current chunk payload, expect CRLF next
				st_ = S_EXPECTING_CRLF;
				continue;
			}

			// write (to memory or pending spill); always count bytes to ensure progress
			if (!write_payload(p + taken, want)) {
				// backpressure path — still advance to avoid spin; caller can retry later
				taken += want;
				return taken;
			}

			taken      += want;
			chunk_rem_ -= want;

			if (chunk_rem_ == 0)
				st_ = S_EXPECTING_CRLF;

			break;
		}

		case S_EXPECTING_CRLF: {
			// must see "\r\n" (or at least '\n' leniently) after a data chunk
			std::size_t rem = n - taken;
			if (rem == 0) return taken;

			if (linebuf_.empty()) {
				if (rem >= 2) {
					const char c0 = p[taken];
					const char c1 = p[taken + 1];
					if (c0 == '\r' && c1 == '\n') {
						taken += 2;
						st_ = S_READING_SIZE;
					} else if (c0 == '\n') {
						// tolerate bare LF
						++taken;
						st_ = S_READING_SIZE;
					} else {
						// consume a byte to break loops, then error
						++taken;
						st_ = S_ERROR;
					}
				} else {
					// only one byte available
					const char c0 = p[taken];
					if (c0 == '\r') {
						linebuf_.push_back('\r');
						++taken;
						return taken; // need the following '\n'
					}
					if (c0 == '\n') {
						++taken;
						st_ = S_READING_SIZE;
					} else {
						++taken;
						st_ = S_ERROR;
					}
				}
			} else {
				// we had a buffered '\r'
				if (linebuf_.size() == 1 && linebuf_[0] == '\r') {
					if (p[taken] == '\n') {
						++taken;
						linebuf_.clear();
						st_ = S_READING_SIZE;
					} else {
						++taken;         // consume to avoid spin
						linebuf_.clear();
						st_ = S_ERROR;
					}
				} else {
					st_ = S_ERROR;
				}
			}
			break;
		}

		case S_TRAILERS: {
			// Buffer everything we got and peel CRLF-terminated lines
			linebuf_.append(p + taken, n - taken);
			taken = n;

			for (;;) {
				std::string::size_type eol = linebuf_.find("\r\n");
				if (eol == std::string::npos) break;

				if (eol == 0) {
					// empty line => end of trailers
					linebuf_.erase(0, 2);
					st_ = S_DONE;
					break;
				}
				// keep trailers if you want them later
				trailers_.append(linebuf_, 0, eol + 2);
				linebuf_.erase(0, eol + 2);
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
