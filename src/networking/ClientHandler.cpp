#include "ClientHandler.h"
#include "CGIStreamer.h"
#include <sys/time.h>
#include <poll.h>
#include <cerrno>
#include <cstdio>  





/* 

This constant defines the maximum number of state-machine pump 
iterations that ClientHandler::onEvent will perform for a client socket in a single poll event.

Here’s why it matters:

Prevent infinite loops
Some requests (especially CGI or proxy-driven) can cause the connection’s 
phase to change rapidly in succession (PH_READ_HEADERS → PH_ROUTE_SELECT → PH_WRITE → PH_CLOSE, etc.). 
Without a cap, onEvent could spin endlessly in one tick if the state keeps changing.

Fairness across connections
Limiting to 30 iterations ensures one busy connection cannot monopolize the event loop. 
Even if state transitions happen quickly, the server will pause after 30, 
return to the main loop, and give other clients time.

Progress guarantee
30 is high enough to allow normal transitions (most requests complete in fewer than 10 steps), 
but bounded to avoid pathological starvation scenarios. 
This balance keeps latency low for all clients while still 
allowing a single request to make rapid progress.


*/

#define PHASE_PUMP_CAP 30


/* 

void ClientHandler::onEvent(int fd, short revents)

The dispatcher for poll events associated with one client connection. 
It starts with a periodic tick (onTick) to advance deadlines and state. 
If the connection is closing and ready, it removes CGI pipes and socket FDs from the loop, 
then closes. Otherwise, it distinguishes: (1) client socket events, 
pumping CGI output into the socket and updating masks; (2) CGI stdout readiness, 
draining child output and reflecting into the client; (3) CGI stdin readiness, 
feeding request body; (4) closed state cleanup. If no case matches, it resynchronizes interests. 
This function ensures every event is correctly propagated.


*/

void ClientHandler::onEvent(int fd, short revents)
{
    const unsigned long long now = nowMs();

	const int sockfd = clientConnection->fd();

	// 0) Periodic tick drives the connection state
	clientConnection->onTick(now);
	if (clientConnection->getState() == PH_CLOSE &&
		clientConnection->isReadyToClose())
	{
		const int cgiOut = clientConnection->getCGIStreamer().cgiStdoutFD();
		const int cgiIn  = clientConnection->getCGIStreamer().cgiStdinFD();
		if (cgiOut >= 0)
			eventLoop.removeFD(cgiOut);
		if (cgiIn  >= 0)
			eventLoop.removeFD(cgiIn);
		if (sockfd >= 0)
			eventLoop.removeFD(sockfd);
		clientConnection->close();
		return;
	}

	// 1) Client socket
	if (fd == sockfd)
	{
		Phase prev = clientConnection->getState();
		for (int i = 0; i < PHASE_PUMP_CAP; ++i) {
			clientConnection->onTick(now);
			if (clientConnection->getState() == PH_CLOSE &&
				clientConnection->isReadyToClose())
			{
				const int cgiOut = clientConnection->getCGIStreamer().cgiStdoutFD();
				const int cgiIn  = clientConnection->getCGIStreamer().cgiStdinFD();
				if (cgiOut >= 0)
					eventLoop.removeFD(cgiOut);
				if (cgiIn  >= 0)
					eventLoop.removeFD(cgiIn);
				if (sockfd >= 0)
					eventLoop.removeFD(sockfd);
				clientConnection->close();
				return;
			}
			if (clientConnection->getState() == prev)
				break;
			prev = clientConnection->getState();
		}

		(void)clientConnection->pumpCgiToSocket();

		// Refresh client socket mask
		short ev = 0;
		if (clientConnection->wantsRead() &&
			!clientConnection->getFlow().isReadPaused())
			ev |= POLLIN;
		if (clientConnection->hasPendingWrite() ||
			clientConnection->getCGIStreamer().hasOutBytes())
			ev |= POLLOUT;

		if (eventLoop.indexOfFD(sockfd) < 0)
			eventLoop.addFD(sockfd, ev, this);
		else
			eventLoop.modFD(sockfd, ev);

		updateInterests();
		return;
	}

	// 2) CGI stdout
	if (fd == clientConnection->getCGIStreamer().cgiStdoutFD())
	{
		if ((revents & (POLLIN | POLLHUP | POLLERR)) &&
			clientConnection->getCGIStreamer().wantsRead())
		{
			clientConnection->getCGIStreamer().onCgiReadable(fd);
		}

		(void)clientConnection->pumpCgiToSocket();

		if (clientConnection->getCGIStreamer().cgiStdoutFD() < 0)
			eventLoop.removeFD(fd);

		short ev = 0;
		if (clientConnection->wantsRead() &&
			!clientConnection->getFlow().isReadPaused())
			ev |= POLLIN;
		if (clientConnection->hasPendingWrite() ||
			clientConnection->getCGIStreamer().hasOutBytes())
			ev |= POLLOUT;

		const int sockfd = clientConnection->fd();
		if (eventLoop.indexOfFD(sockfd) >= 0)
			eventLoop.modFD(sockfd, ev);

		return;
	}

	// 3) CGI stdin
	if (fd == clientConnection->getCGIStreamer().cgiStdinFD())
	{
		if (revents & POLLOUT)
			clientConnection->getCGIStreamer().onCgiWritable(fd);

		if (revents & (POLLHUP | POLLERR)) {
			clientConnection->getCGIStreamer().closeStdin();
			eventLoop.removeFD(fd);
		} else if (clientConnection->getCGIStreamer().cgiStdinFD() < 0) {
			eventLoop.removeFD(fd);
		}

		(void)clientConnection->pumpCgiToSocket();

		short ev = 0;
		if (clientConnection->wantsRead() &&
			!clientConnection->getFlow().isReadPaused())
			ev |= POLLIN;
		if (clientConnection->hasPendingWrite() ||
			clientConnection->getCGIStreamer().hasOutBytes())
			ev |= POLLOUT;

		if (eventLoop.indexOfFD(sockfd) >= 0)
			eventLoop.modFD(sockfd, ev);

		return;
	}

	// 4) Closed connection
	if (clientConnection->isClosed())
	{
		const int cgiOut = clientConnection->getCGIStreamer().cgiStdoutFD();
		const int cgiIn  = clientConnection->getCGIStreamer().cgiStdinFD();
		if (cgiOut >= 0)
			eventLoop.removeFD(cgiOut);
		if (cgiIn  >= 0)
			eventLoop.removeFD(cgiIn);
		if (sockfd >= 0)
			eventLoop.removeFD(sockfd);
		return;
	}

	// 5) Fallback: resync CGI pipes
	updateInterests();
}


/* 

Refreshes the poll interest set for a connection’s socket and CGI pipes. 
For the client socket, it enables POLLIN when the connection wants reads and isn’t paused, 
and POLLOUT if writes are pending or CGI has data. It ensures the socket is registered, 
tying the handler for dispatch and the connection as owner for sweeping. For CGI stdout, 
it registers POLLIN to capture child output, also setting the connection as owner. 
For CGI stdin, it registers POLLOUT to feed request body into the child. 
This bookkeeping guarantees the event loop monitors exactly the right FDs.

*/

void ClientHandler::updateInterests()
{
	// ---- Socket interests ----
	short sock_ev = 0;
	if (clientConnection->wantsRead() && !clientConnection->isReadPaused())
		sock_ev |= POLLIN;
	if (clientConnection->hasPendingWrite() ||
		clientConnection->getCGIStreamer().hasOutBytes())
		sock_ev |= POLLOUT;

	const int sockfd = clientConnection->fd();
	if (eventLoop.indexOfFD(sockfd) < 0) {
		// Dispatcher is the handler (this), owner (for sweeping) is the connection
		eventLoop.addFD(sockfd, sock_ev, this);
		eventLoop.setOwner(sockfd, clientConnection);
	} else {
		eventLoop.modFD(sockfd, sock_ev);
	}

	// ---- CGI stdout (child -> server) ----
	const int cgiOut = clientConnection->getCGIStreamer().cgiStdoutFD();
	if (cgiOut >= 0) {
		if (eventLoop.indexOfFD(cgiOut) < 0) {
			// Dispatcher must be the handler so we actually read!
			eventLoop.addFD(cgiOut, POLLIN, this);
			// But tie sweeping to the connection so removeOwner(conn) cleans it up
			eventLoop.setOwner(cgiOut, clientConnection);
		} else {
			eventLoop.modFD(cgiOut, POLLIN);
		}
	}

	// ---- CGI stdin (server -> child) ----
	const int cgiIn = clientConnection->getCGIStreamer().cgiStdinFD();
	if (cgiIn >= 0) {
		if (eventLoop.indexOfFD(cgiIn) < 0) {
			eventLoop.addFD(cgiIn, POLLOUT, this);
			eventLoop.setOwner(cgiIn, clientConnection);
		} else {
			eventLoop.modFD(cgiIn, POLLOUT);
		}
	}

}


