#if !defined(FILEBODYREADER_H)
#define FILEBODYREADER_H

#include "BodyReader.h"
#include "UniqueFD.h"
#include <cstddef>
#include <string>
#include <vector>

class FileBodyReader : public IBodyReader
{
public:
	explicit FileBodyReader(const std::string &tmp_dir);
	FileBodyReader(const std::string &tmp_dir, const std::string &prefix);
	~FileBodyReader();

	// Feed data from ConnectionIO's IoRing. Returns bytes consumed (staged).
	std::size_t consume(const char *data, std::size_t len);

	// Mark protocol end-of-body (e.g., after chunked trailers or fixed CL).
	void set_done(bool v) { 
		done = v; 
	}

	// Reader is complete when done && pending_size()==0.
	bool complete() const { 
		return done && pending_size() == 0; 
	}

	// Total payload bytes accepted so far (staged + already flushed).
	std::size_t bytes_received() const { 
		return total; 
	}

	// Ensure the body file exists; returns false on failure.
	bool ensure_open();
	const std::string& get_path() const;

	// FD of the temp body file (>=0 when open, else -1).
	int getFd() const { 
		return fd.get(); 
	}

	// Absolute path to the temp file (valid after ensure_open()).
	const std::string &getBodyFilePath() const { 
		return path; 
	}

	// Buffer that still needs to be flushed to disk by the event loop:
	std::size_t pending_size() const { 
		return pending.size() - pending_off; 
	}
	const char *pending_data() const
	{
		return pending_size() ? &pending[0] + pending_off : 0;
	}
	// Drop n bytes from the front of pending after a successful write().
	void drop_pending(std::size_t n);
	// Try to flush up to max_bytes from the pending buffer to disk.
	// Returns bytes actually written (0 on no progress).
	std::size_t flush_to_disk(std::size_t max_bytes);

private:
	// Helpers (std98):
	static std::string join_path(const std::string &a, const std::string &b);
	static std::string make_template(const std::string &dir, const std::string &prefix);

private:
	// Accounting
	std::size_t total; // total payload bytes accepted
	bool done;		   // set by driver when protocol signals EOF

	// Temp file
	UniqueFD fd;		// RAII around the temp file
	std::string dir;	// tmp_dir
	std::string prefix; // filename prefix (optional)
	std::string path;	// absolute path (set on ensure_open)

	// Staging buffer (bytes not yet flushed to disk)
	std::vector<char> pending;
	std::size_t pending_off;
};

#endif // FILEBODYREADER_H
