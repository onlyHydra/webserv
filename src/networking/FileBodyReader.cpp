// src/networking/FileBodyReader.cpp  (std98)

#include "FileBodyReader.h"
#include <cerrno>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h> // mkstemp

// ---- tiny helpers (std98) ----
std::string FileBodyReader::join_path(const std::string &a, const std::string &b)
{
	if (a.empty())
		return b;
	if (a[a.size() - 1] == '/')
		return a + b;
	return a + "/" + b;
}

const std::string& FileBodyReader::get_path() const {
    return path;
}

// Build a mkstemp template like "<dir>/<prefix>.XXXXXX"
std::string FileBodyReader::make_template(const std::string &dir, const std::string &prefix)
{
	std::string pfx = prefix.empty() ? std::string("webserv_body") : prefix;
	return join_path(dir, pfx + ".XXXXXX");
}

FileBodyReader::FileBodyReader(const std::string &tmp_dir)
	: total(0), done(false), fd(), dir(tmp_dir), prefix("webserv_body"),
	  path(), pending(), pending_off(0)
{
}

FileBodyReader::FileBodyReader(const std::string &tmp_dir, const std::string &pfx)
	: total(0), done(false), fd(), dir(tmp_dir),
	  prefix(pfx.empty() ? std::string("webserv_body") : pfx),
	  path(), pending(), pending_off(0)
{
}

FileBodyReader::~FileBodyReader()
{
	// UniqueFD closes automatically on destruction.
}

// FileBodyReader::ensure_open  (replace your current definition)
bool FileBodyReader::ensure_open()
{
    if (fd.get() != -1)
        return true;

    // try configured dir first
    std::string tmpl = make_template(dir, prefix);
    std::vector<char> cbuf; cbuf.reserve(tmpl.size() + 1);
    for (std::size_t i = 0; i < tmpl.size(); ++i)
		cbuf.push_back(tmpl[i]);
    cbuf.push_back('\0');

    int raw = ::mkstemp(&cbuf[0]); // 0600
    if (raw >= 0) {
        path.assign(&cbuf[0]);
        fd.reset(raw);
        return true;
    }

    // fallback to /tmp
    std::string fallbackDir = "/tmp";
    tmpl = make_template(fallbackDir, prefix);
    cbuf.clear();
    for (std::size_t i = 0; i < tmpl.size(); ++i)
		cbuf.push_back(tmpl[i]);
    cbuf.push_back('\0');

    raw = ::mkstemp(&cbuf[0]);
    if (raw < 0)
        return false;

    path.assign(&cbuf[0]);
    fd.reset(raw);
    dir = fallbackDir;
    return true;
}


std::size_t FileBodyReader::consume(const char *data, std::size_t len)
{
	if (data == 0 || len == 0)
		return 0;

	// Stage bytes in memory; the driver will flush them after poll().
	if (pending.empty())
	{
		// fast-path: append entire slice
		pending.insert(pending.end(), data, data + len);
	}
	else
	{
		// compact if we've dropped a lot from the front
		if (pending_off > 0 && pending_off > (pending.size() / 2))
		{
			const std::size_t tail = pending.size() - pending_off;
			if (tail)
				::memmove(&pending[0], &pending[0] + pending_off, tail);
			pending.resize(tail);
			pending_off = 0;
		}
		pending.insert(pending.end(), data, data + len);
	}

	total += len;
	return len;
}

void FileBodyReader::drop_pending(std::size_t n)
{
	const std::size_t avail = pending_size();
	if (n >= avail)
	{
		// dropped everything
		pending.clear();
		pending_off = 0;
		return;
	}
	// drop prefix
	pending_off += n;
}

std::size_t FileBodyReader::flush_to_disk(std::size_t max_bytes)
{
	// Do bounded, single write attempt; no blocking and no errno-based branching.
	if (pending_size() == 0)
		return 0;
	if (!ensure_open())
		return 0;

	const char *p = &pending[0] + pending_off;
	std::size_t n = pending.size() - pending_off;
	if (n > max_bytes)
		n = max_bytes;

	ssize_t w = ::write(fd.get(), p, n);
	if (w > 0)
	{
		drop_pending((std::size_t)w);
		return (std::size_t)w;
	}
	// If w == 0 or w < 0: report no progress; driver will try again next tick.
	return 0;
}
