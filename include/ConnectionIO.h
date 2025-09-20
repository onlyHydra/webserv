	/* --- ConnectionIO.h --- */

	/* ------------------------------------------
	Author: Hydra
	Date: 9/1/2025
	------------------------------------------ */

#ifndef CONNECTIONIO_H
#define CONNECTIONIO_H

#include "UniqueFD.h"
#include "FlowControl.h"
#include "IoRing.h"
#include "ChainBuf.h"

#include <unistd.h>
#include <vector>
#include <cstddef>
#include <sys/types.h>

class ConnectionIO
	{
	private:
		ConnectionIO(const ConnectionIO &);
		ConnectionIO &operator=(const ConnectionIO &);

	public:
		// explicit ConnectionIO(int fd = -1, std::size_t inCap = 64 * 1024)
		explicit ConnectionIO(int fd = -1, std::size_t inCap = 256 * 1024)
			: socket(fd), in(inCap), out(), flow(), tx_scratch(), tmp()
		{
			tx_scratch.reserve(128 * 1024); // one-time reserve
		}

		~ConnectionIO() {}

		void close() { socket.reset(); }

		int getFD() const { return socket.get(); }

		IoRing &getInputRing() { return in; }
		const IoRing &getInputRing() const { return in; }

		// Non-const accessor for writers (push_copy, dropFront, etc.)
		ChainBuf &getChainBuf() { return out; }

		// Const accessor for queries (e.g., getByteSize()) in const contexts
		const ChainBuf &getChainBuf() const { return out; }

		FlowControl &getFlow() { return flow; }
		const FlowControl &getFlow() const { return flow; }

		// Non-blocking read/write to socket
		ssize_t nb_read(std::size_t max_bytes);
		ssize_t nb_write();

		// Scratch buffer (e.g., for assembling a write from ChainBuf)
		std::vector<char> &getTmp() { return tmp; }
		const std::vector<char> &getTmp() const { return tmp; }

	private:
		UniqueFD          socket;
		IoRing            in;         // inbound ring buffer
		ChainBuf          out;        // outbound chain buffer (to socket)
		FlowControl       flow;       // read flow-control (pause/resume)
		std::vector<char> tx_scratch; // internal staging for writes
		std::vector<char> tmp;        // general-purpose scratch
};

#endif // CONNECTIONIO_H
