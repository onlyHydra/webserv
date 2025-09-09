/* --- ChainBuf.h --- */

/* ------------------------------------------
Author: Hydra
Date: 9/1/2025
------------------------------------------ */

#ifndef CHAINBUF_H
#define CHAINBUF_H

#include <vector>
#include <cstddef>
#include <string.h>

class ChainBuf
{
	public:
		ChainBuf() : blocks(), storage(), byteSize(0), max_bytes(2u * 1024u * 1024u), max_blocks(1024u) {}
		~ChainBuf(){};

		// copy-in (owned)
		bool push_copy(const void *p, std::size_t n)
		{
			if (n == 0)
				return true;
			if (byteSize + n > max_bytes || blocks.size() >= max_blocks)
				return false;
			const std::size_t off = storage.size();
			storage.resize(off + n);
			memcpy(&storage[0] + off, p, n);
			blocks.push_back(Block(&storage[0] + off, n, true));
			byteSize += n;
			return true;
		}

		// reference (caller guarantees lifetime)
		bool push_ref(const void *p, std::size_t n)
		{
			if (n == 0)
				return true;
			if (byteSize + n > max_bytes || blocks.size() >= max_blocks)
				return false;
			blocks.push_back(Block(reinterpret_cast<const char *>(p), n, false));
			byteSize += n;
			return true;
		}

		std::size_t getByteSize() const { return byteSize; };
		bool empty() const { return blocks.empty(); };

		std::size_t copy_front_into(std::vector<char> &scratch, std::size_t max_bytes) const
		{
			scratch.clear();
			if (blocks.empty() || max_bytes == 0)
				return 0;

			// Reserve once to avoid multiple reallocs
			std::size_t want = max_bytes;
			if (want > byteSize)
				want = byteSize;
			scratch.resize(want);

			std::size_t copied = 0;
			for (std::size_t i = 0; i < blocks.size() && copied < want; ++i)
			{
				const Block &b = blocks[i];
				std::size_t take = b.len;
				if (take > (want - copied))
					take = want - copied;
				::memcpy(&scratch[0] + copied, b.data, take);
				copied += take;
			}
			// shrink scratch if we reserved more than we actually copied
			if (copied < scratch.size())
				scratch.resize(copied);
			return copied;
		};

		void dropFront(std::size_t n)
		{
			while (n && !blocks.empty())
			{
				if (n >= blocks.front().len)
				{
					n -= blocks.front().len;
					byteSize -= blocks.front().len;
					blocks.erase(blocks.begin());
				}
				else
				{
					blocks.front().data += n;
					blocks.front().data -= n;
					byteSize -= n;
					n = 0;
				}
			}
		};

		void setCaps(std::size_t max_bytes, std::size_t max_blocks)
		{
			this->max_bytes = max_bytes;
			this->max_blocks = max_blocks;
		}

		// void attachFile(int fd, off_t off, std::size_t len);

	private:
		struct Block
		{
			const char *data;
			std::size_t len;
			bool owned;
			Block() : data(0), len(0), owned(false) {};
			Block(const char *d, std::size_t n, bool o) : data(d), len(n), owned(o) {};
		};
		std::vector<Block> blocks;
		std::vector<char> storage;
		std::size_t byteSize;
		std::size_t max_bytes;
		std::size_t max_blocks;
};

#endif // CHAINBUF_H
