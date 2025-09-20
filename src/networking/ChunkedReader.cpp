#include "ChunkedReader.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <vector>

// ---- tiny helpers ----
static int hexval(int c)
{
	if (c >= '0' && c <= '9') 
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/**
 * bac
 */
static int open_tmpfile(std::string &out_path, const std::string &dir_hint)
{
	std::string tmpl;
	if (!dir_hint.empty())
	{
		tmpl = dir_hint;
		if (tmpl[tmpl.size() - 1] != '/')
			tmpl += "/";
		tmpl += "webserv-body-XXXXXX";
	}
	else
	{
		tmpl = std::string("/tmp/webserv-body-XXXXXX");
	}
	// mkstemp modifies the string; make a mutable buffer
	std::vector<char> buf(tmpl.begin(), tmpl.end());
	buf.push_back('\0');
	int fd = ::mkstemp(&buf[0]);
	if (fd >= 0)
	{
		out_path.assign(&buf[0]);
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	}
	return fd;
}

ChunkedReader::ChunkedReader(std::vector<char> &mem_body,
							std::size_t spill_threshold,
							const std::string &tmp_dir)
	: st_(S_READING_SIZE), total_(0), chunk_rem_(0), linebuf_(),
	spill_threshold_(spill_threshold), tmp_dir_(tmp_dir), mem_(mem_body),
	on_disk_(false), fd_(-1), path_(), trailers_(),
	pending_(), pending_off_(0)
{
	if (!mem_.empty())
		mem_.clear();
	mem_.reserve(8 * 1024);
	// ensure pending is initialized (std98-safe)
	pending_.clear();
	pending_off_ = 0;
}

ChunkedReader::~ChunkedReader()
{
	if (fd_ >= 0)
		::close(fd_);
}


bool ChunkedReader::ensure_spill_file()
{
	if (on_disk_)
		return true;
	// open temp file
	fd_ = open_tmpfile(path_, tmp_dir_);
	if (fd_ < 0)
		return false;

	// switch to disk mode; do NOT write here (driver will flush pending later)
	on_disk_ = true;

	// Move any already-buffered memory body into pending_ for later flush
	if (!mem_.empty())
	{
		pending_.insert(pending_.end(), &mem_[0], &mem_[0] + mem_.size());
		mem_.clear();
		mem_.reserve(0);
	}
	return true;
}

bool ChunkedReader::write_payload(const char *p, std::size_t n)
{
	// Decide if we need to spill
	if (!on_disk_ && spill_threshold_ && (total_ + n) > spill_threshold_)
	{
		if (!ensure_spill_file())
			return false;
	}

	if (!on_disk_)
	{
		// in-memory append
		const std::size_t old_sz = mem_.size();
		mem_.resize(old_sz + n);
		if (n)
			std::memcpy(&mem_[0] + old_sz, p, n);
	}
	else
	{
		// disk-backed: stage; actual write occurs via flush_to_disk() by driver
		pending_.insert(pending_.end(), p, p + n);
	}

	total_ += n;
	return true;
}

bool ChunkedReader::parse_size_line()
{
	// Expect: <hex>[;extensions...]<CRLF>
	// Accumulate into linebuf_ until CRLF seen
	// Return true when size parsed and state updated, false on error
	std::size_t i = 0;
	while (i < linebuf_.size())
	{
		if (linebuf_[i] == '\r')
		{
			if (i + 1 >= linebuf_.size() || linebuf_[i + 1] != '\n')
				return false; // incomplete CRLF; caller will feed more
			// parse hex size up to ';' or \r
			std::size_t hexlen = 0;
			while (hexlen < i && linebuf_[hexlen] != ';')
				++hexlen;

			std::size_t sz = 0;
			for (std::size_t k = 0; k < hexlen; ++k)
			{
				int v = hexval(linebuf_[k]);
				if (v < 0)
					return false;
				sz = (sz << 4) + (std::size_t)v;
			}

			chunk_rem_ = sz;
			linebuf_.erase(0, i + 2); // remove "<line>\r\n"

			if (chunk_rem_ == 0)
				st_ = S_TRAILERS;
			else
				st_ = S_READING_DATA;
			return true;
		}
		++i;
	}

	// no CRLF yet; need more data
	return true;
}

bool ChunkedReader::parse_trailers()
{
	// Read until empty line (CRLF CRLF)
	while (!linebuf_.empty())
	{
		// looking for CRLF CRLF
		for (std::size_t i = 0; i + 3 < linebuf_.size(); ++i)
		{
			if (linebuf_[i] == '\r' && linebuf_[i + 1] == '\n' &&
				linebuf_[i + 2] == '\r' && linebuf_[i + 3] == '\n')
			{
				// keep trailers if you want
				trailers_ = linebuf_.substr(0, i + 2);
				linebuf_.erase(0, i + 4);
				st_ = S_DONE;
				return true;
			}
		}
		// need more data to finish trailers
		break;
	}
	return true;
}

std::size_t ChunkedReader::consume(const char *p, std::size_t n)
{
	if (!p || n == 0)
		return 0;
	if (st_ == S_DONE || st_ == S_ERROR)
		return 0;

	const char *cur = p;
	std::size_t left = n;

	while (left > 0)
	{
		switch (st_)
		{
		case S_READING_SIZE:
		{
			// accumulate until CRLF
			const char *nl = 0;
			for (std::size_t i = 0; i < left; ++i)
			{
				if (cur[i] == '\n')
				{
					nl = cur + i;
					break;
				}
			}
			if (!nl)
			{
				// no full line yet
				linebuf_.append(cur, left);
				cur += left;
				left = 0;
				return n - left;
			}
			// append through NL inclusive
			std::size_t take = (std::size_t)(nl - cur) + 1;
			linebuf_.append(cur, take);
			cur += take;
			left -= take;

			if (!parse_size_line())
			{
				st_ = S_ERROR;
				return n - left;
			}
			break;
		}

		case S_READING_DATA:
		{
			if (chunk_rem_ == 0)
			{
				st_ = S_EXPECTING_CRLF;
				break;
			}
			std::size_t to_take = left;
			if (to_take > chunk_rem_) to_take = chunk_rem_;

			if (!write_payload(cur, to_take))
			{
				st_ = S_ERROR;
				return n - left;
			}

			cur += to_take;
			left -= to_take;
			chunk_rem_ -= to_take;

			if (chunk_rem_ == 0)
				st_ = S_EXPECTING_CRLF;

			break;
		}

		case S_EXPECTING_CRLF:
		{
			if (left < 2)
			{
				// Need more to validate CRLF
				linebuf_.append(cur, left);
				cur += left;
				left = 0;

				// Handle the case where part of CRLF already buffered
				if (linebuf_.size() >= 2)
				{
					if (linebuf_[0] == '\r' && linebuf_[1] == '\n')
					{
						linebuf_.erase(0, 2);
						st_ = S_READING_SIZE;
					}
					else
					{
						st_ = S_ERROR;
					}
				}
				return n - left;
			}

			if (cur[0] != '\r' || cur[1] != '\n')
			{
				st_ = S_ERROR;
				return n - left;
			}

			cur += 2;
			left -= 2;
			st_ = S_READING_SIZE;
			break;
		}

		case S_TRAILERS:
		{
			// copy all to linebuf_ and try to parse
			linebuf_.append(cur, left);
			cur += left;
			left = 0;
			if (!parse_trailers())
				return n - left;
			break;
		}

		case S_DONE:
		case S_ERROR:
			return n - left;
		}
	}

	return n;
}

std::size_t ChunkedReader::flush_to_disk(std::size_t max_bytes)
{
	if (!on_disk_ || pending_size() == 0)
		return 0;
	if (fd_ < 0 && !ensure_spill_file())
		return 0;

	const char *p = &pending_[0] + pending_off_;
	std::size_t n = pending_.size() - pending_off_;
	if (n > max_bytes) n = max_bytes;

	ssize_t w = ::write(fd_, p, n);
	if (w > 0)
	{
		drop_pending((std::size_t)w);
		return (std::size_t)w;
	}
	// w == 0 or w < 0: no progress; driver tries next tick
	return 0;
}

void ChunkedReader::drop_pending(std::size_t n)
{
	const std::size_t avail = pending_size();
	if (n >= avail)
	{
		pending_.clear();
		pending_off_ = 0;
		return;
	}
	pending_off_ += n;

	// compact if front gap grew large
	if (pending_off_ > 0 && pending_off_ > (pending_.size() / 2))
	{
		const std::size_t tail = pending_.size() - pending_off_;
		if (tail)
			std::memmove(&pending_[0], &pending_[0] + pending_off_, tail);
		pending_.resize(tail);
		pending_off_ = 0;
	}
}
