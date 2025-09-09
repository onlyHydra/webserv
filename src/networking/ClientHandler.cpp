#include "ClientHandler.h"

#include <sys/time.h>
#include <poll.h>

#define PHASE_PUMP_CAP 30

void ClientHandler::onEvent(int fd, short revents)
{
	const unsigned long long now = nowMs();

	// 1) Socket events and timer ticks drive the HTTP state machine
	if (fd == clientConnection->fd())
	{
		Phase prev = clientConnection->getState();
		clientConnection->onTick(now);
		if (clientConnection->getState() == PH_CLOSE && clientConnection->isReadyToClose())
		{
			clientConnection->close();
			 
			return;
		}

		for (int i = 0; i < PHASE_PUMP_CAP; ++i)
		{
			Phase was = clientConnection->getState();
			if (was == prev)
				break; // no phase change → stop
			prev = was;
			clientConnection->onTick(now);
			if (clientConnection->getState() == PH_CLOSE && clientConnection->isReadyToClose())
			{
				clientConnection->close();
				return;
			}
		}

		// Compute desired interest mask
		short ev = 0;
		if (clientConnection->wantsRead() &&
			!clientConnection->getFlow().isReadPaused())
			ev |= POLLIN;

		if (clientConnection->hasPendingWrite() > 0)
			ev |= POLLOUT;

		const int fd = clientConnection->fd(); // use your accessor

		// First registration vs subsequent updates
		if (eventLoop.indexOfFD(fd) < 0)
		{
			eventLoop.addFD(fd, ev, clientConnection);
			eventLoop.setOwner(fd, clientConnection);
		}
		else
		{
			eventLoop.modFD(fd, ev); // ← keep using this to “steer” interest
		}

		updateInterests();
	}
	// 2) CGI stdout → stream into ChainBuf
	else if (fd == clientConnection->getCGIStreamer().cgiStdoutFD())
	{
		if (revents == 0 || (revents & POLLIN))
			clientConnection->getCGIStreamer().onCgiReadable(fd);
	}
	// 3) CGI stdin → feed request body to child
	else if (fd == clientConnection->getCGIStreamer().cgiStdinFD())
	{
		if (revents == 0 || (revents & POLLOUT))
			clientConnection->getCGIStreamer().onCgiWritable(fd);
	}

	// 4) If the connection closed, drop all its fds
	if (clientConnection->isClosed())
	{
		// drop CGI fds if present
		int cgiOut = clientConnection->getCGIStreamer().cgiStdoutFD();
		int cgiIn = clientConnection->getCGIStreamer().cgiStdinFD();
		if (cgiOut >= 0)
			eventLoop.removeFD(cgiOut);
		if (cgiIn >= 0)
			eventLoop.removeFD(cgiIn);
		eventLoop.removeFD(clientConnection->fd());
		return;
	}

	// 5) Refresh poll interests (socket + CGI pipes)
	updateInterests();
}

void ClientHandler::updateInterests()
{
	// ---- Socket interests ----
	short sock_ev = 0;
	if (clientConnection->wantsRead() && !clientConnection->isReadPaused())
		sock_ev |= POLLIN;
	if (clientConnection->hasPendingWrite())
		sock_ev |= POLLOUT;
	eventLoop.modFD(clientConnection->fd(), sock_ev);

	// ---- CGI stdout readable? ----
	const int cgiOut = clientConnection->getCGIStreamer().cgiStdoutFD();
	if (cgiOut >= 0)
	{
		if (eventLoop.indexOfFD(cgiOut) < 0)
			eventLoop.addFD(cgiOut, POLLIN, this);
		else
			eventLoop.modFD(cgiOut, POLLIN);
	}

	// ---- CGI stdin writable? ----
	const int cgiIn = clientConnection->getCGIStreamer().cgiStdinFD();
	if (cgiIn >= 0)
	{
		// We can always request POLLOUT while stdin is open; the kernel will edge us when writable.
		if (eventLoop.indexOfFD(cgiIn) < 0)
			eventLoop.addFD(cgiIn, POLLOUT, this);
		else
			eventLoop.modFD(cgiIn, POLLOUT);
	}
}