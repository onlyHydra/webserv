/* --- ConnectionIO.h --- */

/* ------------------------------------------
Author: Hydra
Date: 9/1/2025
------------------------------------------ */

#ifndef CONNECTIONIO_H
#define CONNECTIONIO_H

#include "UniqueFD.h"
#include "unistd.h"
#include "FlowControl.h"
#include "IoRing.h"
#include "ChainBuf.h"
#include <vector>
#include <cstddef>
#include <sys/types.h>

class ConnectionIO
{

private:
	ConnectionIO(const ConnectionIO &);
	ConnectionIO &operator=(const ConnectionIO &);

public:
	explicit ConnectionIO(int fd = -1, std::size_t inCap = 64 * 1024)
		: socket(fd), in(inCap), out(), flow(), tx_scratch()
	{
		tx_scratch.reserve(128 * 1024); // one-time reserve
	}
	~ConnectionIO(){};

	/**
	 * have to add :
	 * readfromSocket
	 * onReadable()
	 * onWritable()
	 * setReadPaused()/isReadPaused()	via flow class.
	 */

	void close() { socket.reset(); }

	int getFD() const { 
		return socket.get(); 
	}
	IoRing &getInputRing()  { 
		return in; 
	}
	ChainBuf &getChainBuf() { 
		return out; 
	}
	FlowControl &getFlow() { 
		return flow; 
	}
	const FlowControl &getFlow() const { 
		return flow; 
	}

	ssize_t nb_read(std::size_t max_bytes);
	ssize_t nb_write();

	std::vector<char>& getTmp(){return tmp;}

private:
	UniqueFD socket;
	IoRing in;
	ChainBuf out;
	FlowControl flow;
	std::vector<char> tx_scratch;
	std::vector<char> tmp;
};

#endif // CONNECTIONIO_H
