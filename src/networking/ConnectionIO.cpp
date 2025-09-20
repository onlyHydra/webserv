#include "ConnectionIO.h"

#include <errno.h>
#include <string.h>     // std::memmove (used by IoRing::compactIfNeeded)
#include <sys/socket.h> // recv, send
#include <unistd.h>     // close

ssize_t ConnectionIO::nb_read(std::size_t maxBytes)
{
    if (!socket.valid())
	{
		return -1;
	}

	in.compactIfNeeded();

	std::size_t space = in.writeAvail();
	if (space == 0)
		return -1;
	if (maxBytes < space)
		space = maxBytes;

    char *dst = in.writePtr();
    ssize_t n = ::recv(socket.get(), dst, (int)space, MSG_DONTWAIT);

	if (n > 0) {
		in.wrote((std::size_t)n);
		return n;
	}
	// n == 0 (peer closed) or n < 0 (EAGAIN/other) — just return it; caller decides.
	return n;
}

ssize_t ConnectionIO::nb_write()
{
    if (!socket.valid())
        return -1;
    if (out.empty())
        return 0;

    // Copy a bounded window from the front of the chain into the scratch buffer
    const std::size_t kMaxPerSend = 128u * 1024u;
    std::size_t n_to_send = out.copy_front_into(tx_scratch, kMaxPerSend);
    if (n_to_send == 0)
        return 0;

    ssize_t n = ::send(socket.get(), &tx_scratch[0], (int)n_to_send, MSG_DONTWAIT);

	if (n > 0) {
		out.dropFront((std::size_t)n);
		return n;
	}
	// n == 0 or n < 0 — do NOT branch on errno here; let caller/loop logic decide.
	return n;
}
