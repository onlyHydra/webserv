/* --- IoRing.h --- */

/* ------------------------------------------
Author: Hydra
Date: 9/1/2025
------------------------------------------ */

#ifndef IORING_H
#define IORING_H

#include <vector>
#include <string.h>
#include <cstddef>

class IoRing
{
public:
	explicit IoRing(std::size_t cap = 64 * 1024)
		: buf(), r(0), w(0)
	{
		if (cap == 0)
			cap = 1;
		buf.resize(cap); // IMPORTANT: real storage (not just reserve)
	}

	// --- write side ---
	char *writePtr() { 
		return &buf[0] + w; 
	}

	std::size_t writeAvail() const
	{
		return buf.size() - w;
	}

	// Ensure there is at least 'need' bytes contiguous; compacts if helpful.
	// Returns the contiguous writable space after compaction attempt.
	std::size_t ensureWrite(std::size_t need)
	{
		if (writeAvail() >= need)
			return writeAvail();
		compactIfNeeded(); // pulls unread data to front if we advanced far
		return writeAvail();
	}

	void wrote(std::size_t n)
	{
		if (n == 0)
			return;
		if (n > writeAvail())
			n = writeAvail(); // or assert in debug
		w += n;
	}

	// --- read side ---
	const char *readPtr() const
	{
		return &buf[0] + r;
	}
	char *readPtr() { 
		return &buf[0] + r; 
	}

	std::size_t readAvail() const { 
		return w - r; 
	}

	void consumed(std::size_t n)
	{
		if (n == 0)
			return;
		if (n > readAvail())
			n = readAvail(); // or assert in debug
		r += n;

		// fast reset when empty
		if (r == w)
		{
			r = 0;
			w = 0;
			return;
		}

		// optional: compact when we've slid past half the buffer
		if (r > buf.size() / 2)
			compactIfNeeded();
	}

	// Pull unread bytes to the front; keeps capacity fixed
	void compactIfNeeded()
	{
		if (r == 0)
			return;
		const std::size_t n = w - r;
		if (n)
			::memmove(&buf[0], &buf[0] + r, n);
		r = 0;
		w = n;
	}

	// misc helpers
	std::size_t capacity() const { 
		return buf.size(); 
	}
	void clear()
	{
		r = 0;
		w = 0;
	} // does not shrink

private:
	std::vector<char> buf; // fixed-capacity backing store
	std::size_t r;		   // read index (0..w)
	std::size_t w;		   // write index (r..size)
};

#endif // IORING_H
