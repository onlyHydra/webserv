#include "ClientHandler.h"

#include <sys/time.h>
#include <poll.h>
#include <cerrno>
#include <cstdio>  

#define PHASE_PUMP_CAP 30

void ClientHandler::onEvent(int fd, short revents)
{
    const unsigned long long now = nowMs();

    // 1) Socket events drive the HTTP state machine
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
            if (was == prev) break;
            prev = was;

            clientConnection->onTick(now);
            if (clientConnection->getState() == PH_CLOSE && clientConnection->isReadyToClose())
            {
                clientConnection->close();
                return;
            }
        }

        // Refresh socket interest mask
        short ev = 0;
        if (clientConnection->wantsRead() && !clientConnection->getFlow().isReadPaused())
            ev |= POLLIN;
        if (clientConnection->hasPendingWrite())
            ev |= POLLOUT;

        const int sockfd = clientConnection->fd();
        if (eventLoop.indexOfFD(sockfd) < 0)
            eventLoop.addFD(sockfd, ev, this);
        else
            eventLoop.modFD(sockfd, ev);

        // Also (re)sync CGI pipe registrations
        updateInterests();
        return;
    }

    // 2) CGI stdout → treat POLLIN | POLLHUP | POLLERR as readable
    if (fd == clientConnection->getCGIStreamer().cgiStdoutFD())
    {
        if (revents & (POLLIN | POLLHUP | POLLERR))
            clientConnection->getCGIStreamer().onCgiReadable(fd);

        // If streamer closed stdout, stop watching it
        if (clientConnection->getCGIStreamer().cgiStdoutFD() < 0)
            eventLoop.removeFD(fd);

        return;
    }

    // 3) CGI stdin → feed request body; close/deregister on HUP/ERR
    if (fd == clientConnection->getCGIStreamer().cgiStdinFD())
    {
        if (revents & POLLOUT)
            clientConnection->getCGIStreamer().onCgiWritable(fd);

        if (revents & (POLLHUP | POLLERR))
        {
            clientConnection->getCGIStreamer().closeStdin();
            eventLoop.removeFD(fd);
            return;
        }

        // If stdin got closed by the streamer, stop watching it
        if (clientConnection->getCGIStreamer().cgiStdinFD() < 0)
            eventLoop.removeFD(fd);

        return;
    }

    // 4) If the connection is closed, drop all its fds
    if (clientConnection->isClosed())
    {
        const int cgiOut = clientConnection->getCGIStreamer().cgiStdoutFD();
        const int cgiIn  = clientConnection->getCGIStreamer().cgiStdinFD();
        if (cgiOut >= 0) eventLoop.removeFD(cgiOut);
        if (cgiIn  >= 0) eventLoop.removeFD(cgiIn);
        eventLoop.removeFD(clientConnection->fd());
        return;
    }

    // 5) Final interest refresh
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

    const int sockfd = clientConnection->fd();
    if (eventLoop.indexOfFD(sockfd) < 0) {
        eventLoop.addFD(sockfd, sock_ev, clientConnection); // keep owner linkage
        eventLoop.setOwner(sockfd, clientConnection);
    } else {
        eventLoop.modFD(sockfd, sock_ev);
    }

    // ---- CGI stdout (child -> server) readable? ----
    const int cgiOut = clientConnection->getCGIStreamer().cgiStdoutFD();
    if (cgiOut >= 0) {
        if (eventLoop.indexOfFD(cgiOut) < 0)
            eventLoop.addFD(cgiOut, POLLIN, this);
        else
            eventLoop.modFD(cgiOut, POLLIN);
    } else {
        // stdout closed: stop polling it if it was registered
        if (eventLoop.indexOfFD(cgiOut) >= 0)
            eventLoop.removeFD(cgiOut);
    }

    // ---- CGI stdin (server -> child) writable? ----
    const int cgiIn = clientConnection->getCGIStreamer().cgiStdinFD();
    if (cgiIn >= 0) {
	
    	fprintf(stderr, "[CGI] register POLLIN for outFD=%d\n", cgiOut);
        // It’s fine to keep POLLOUT requested while stdin is open; kernel signals readiness.
        if (eventLoop.indexOfFD(cgiIn) < 0)
            eventLoop.addFD(cgiIn, POLLOUT, this);
        else
            eventLoop.modFD(cgiIn, POLLOUT);
    } else {
        // stdin closed: stop polling it if it was registered
        if (eventLoop.indexOfFD(cgiIn) >= 0)
            eventLoop.removeFD(cgiIn);
    }
}
