#include "ClientHandler.h"
#include "CGIStreamer.h"
#include <sys/time.h>
#include <poll.h>
#include <cerrno>
#include <cstdio>  

#define PHASE_PUMP_CAP 30

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
        if (cgiOut >= 0) eventLoop.removeFD(cgiOut);
        if (cgiIn  >= 0) eventLoop.removeFD(cgiIn);
        if (sockfd >= 0) eventLoop.removeFD(sockfd);
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
                if (cgiOut >= 0) eventLoop.removeFD(cgiOut);
                if (cgiIn  >= 0) eventLoop.removeFD(cgiIn);
                if (sockfd >= 0) eventLoop.removeFD(sockfd);
                clientConnection->close();
                return;
            }
            if (clientConnection->getState() == prev) break;
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
        if (cgiOut >= 0) eventLoop.removeFD(cgiOut);
        if (cgiIn  >= 0) eventLoop.removeFD(cgiIn);
        if (sockfd >= 0) eventLoop.removeFD(sockfd);
        return;
    }

    // 5) Fallback: resync CGI pipes
    updateInterests();
}





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


