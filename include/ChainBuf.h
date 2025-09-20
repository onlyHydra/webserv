#ifndef CHAINBUF_H
#define CHAINBUF_H

#include <vector>
#include <cstddef>
#include <cstring>   // std::memcpy

class ChainBuf {
public:
ChainBuf()
	: byteSize_(0),
		max_bytes_(2u * 1024u * 1024u),
		max_blocks_(1024u) {}

~ChainBuf() {}

// copy-in (owned)
bool push_copy(const void* p, std::size_t n) {
	if (n == 0) return true;
	if (byteSize_ + n > max_bytes_ || blocks_.size() >= max_blocks_)
		return false;
	const std::size_t off = storage_.size();
	storage_.resize(off + n);
	std::memcpy(&storage_[0] + off, p, n);
	blocks_.push_back(Block(&storage_[0] + off, n, true));
	byteSize_ += n;
	return true;
}

// reference (caller guarantees lifetime)
bool push_ref(const void* p, std::size_t n) {
	if (n == 0) return true;
	if (byteSize_ + n > max_bytes_ || blocks_.size() >= max_blocks_)
		return false;
	blocks_.push_back(Block(reinterpret_cast<const char*>(p), n, false));
	byteSize_ += n;
	return true;
}

std::size_t getByteSize() const { return byteSize_; }
bool        empty()       const { return blocks_.empty(); }

// Copy up to max_bytes from the front into 'scratch'
std::size_t copy_front_into(std::vector<char>& scratch, std::size_t max_bytes_req) const {
	scratch.clear();
	if (blocks_.empty() || max_bytes_req == 0) return 0;

	std::size_t want = max_bytes_req;
	if (want > byteSize_) want = byteSize_;
	scratch.resize(want);

	std::size_t copied = 0;
	for (std::size_t i = 0; i < blocks_.size() && copied < want; ++i) {
		const Block& b = blocks_[i];
		std::size_t take = b.len;
		if (take > (want - copied)) take = want - copied;
		std::memcpy(&scratch[0] + copied, b.data, take);
		copied += take;
	}
	if (copied < scratch.size()) scratch.resize(copied);
	return copied;
}

// Drop n bytes from the front
	void dropFront(std::size_t n) {
		while (n && !blocks_.empty()) {
			if (n >= blocks_.front().len) {
				n        -= blocks_.front().len;
				byteSize_ -= blocks_.front().len;
				blocks_.erase(blocks_.begin());
			} else {
				// advance pointer inside the first block
				blocks_.front().data += n;
				blocks_.front().len  -= n;
				byteSize_            -= n;
				n = 0;
			}
		}
		// Optional: if all blocks drained, free owned storage to release memory burst
		if (blocks_.empty() && !storage_.empty()) {
			std::vector<char>().swap(storage_);
		}
	}

	void setCaps(std::size_t new_max_bytes, std::size_t new_max_blocks) {
		max_bytes_  = new_max_bytes;
		max_blocks_ = new_max_blocks;
	}

	private:
	struct Block {
		const char*  data;
		std::size_t  len;
		bool         owned;
		Block() : data(0), len(0), owned(false) {}
		Block(const char* d, std::size_t n, bool o) : data(d), len(n), owned(o) {}
	};

	std::vector<Block> blocks_;
	std::vector<char>  storage_;
	std::size_t        byteSize_;
	std::size_t        max_bytes_;
	std::size_t        max_blocks_;
};

#endif // CHAINBUF_H
