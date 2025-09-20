#ifndef CHUNKEDREADER_H
#define CHUNKEDREADER_H

#include "BodyReader.h"
#include <cstddef>
#include <string>
#include <vector>

/**
 * ChunkedReader (std98)
 *  - Parses HTTP/1.1 chunked transfer-encoding.
 *  - Accumulates payload in memory until `spill_threshold_` is exceeded,
 *    then switches to a temp file under `tmp_dir_`.
 *  - Does NOT perform socket IO; driver feeds it via consume().
 *  - For disk-backed mode, no synchronous writes occur in the hot path:
 *    bytes are staged and the driver calls flush_to_disk() after poll.
 */
class ChunkedReader : public IBodyReader
{
public:
	ChunkedReader(std::vector<char> &mem_body,
	              std::size_t spill_threshold,
	              const std::string &tmp_dir);
	~ChunkedReader();

	// Incrementally consume bytes from caller buffer.
	std::size_t consume(const char *p, std::size_t n);

	// IBodyReader API
	bool complete() const { return st_ == S_DONE; }
	std::size_t bytes_received() const { 
		return total_; 
	}

	// Storage queries
	bool        isBodyOnDisk() const { 
		return on_disk_; 
	}
	std::string getBodyFilePath() const { 
		return path_; 
	}
	std::size_t getBodyLength() const { 
		return total_; 
	}

	// Driver-facing disk flushing (bounded, non-blocking)
	std::size_t flush_to_disk(std::size_t max_bytes);

	// Optional helpers
	std::size_t pending_size() const { 
		return pending_.size() - pending_off_; 
	}
	const char* pending_data() const
	{
		return pending_size() ? &pending_[0] + pending_off_ : 0;
	}
	void drop_pending(std::size_t n);

private:
	// Internal helpers implemented in .cpp
	bool ensure_spill_file();
	bool write_payload(const char *p, std::size_t n);
	bool parse_size_line();
	bool parse_trailers();

private:
	// Parser states observed in implementation
	enum State
	{
		S_READING_SIZE,
		S_READING_DATA,
		S_EXPECTING_CRLF,
		S_TRAILERS,
		S_DONE,
		S_ERROR
	};

	State         st_;
	std::size_t   total_;
	std::size_t   chunk_rem_;
	std::string   linebuf_;

	// Storage policy
	std::size_t        spill_threshold_;
	std::string        tmp_dir_;
	std::vector<char> &mem_;

	// Spill-to-disk bookkeeping
	bool          on_disk_;
	int           fd_;
	std::string   path_;

	// Parsed trailers (opaque store)
	std::string   trailers_;

	// Pending bytes to flush to disk (for on-disk mode)
	std::vector<char> pending_;
	std::size_t       pending_off_;
};

#endif // CHUNKEDREADER_H
