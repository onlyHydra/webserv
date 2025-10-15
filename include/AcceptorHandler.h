#ifndef ACCEPTORHANDLER_H
#define ACCEPTORHANDLER_H

#include "EventLoop.h"
#include "Server.h"
#include "Listener.h"
#include "ClientConnection.h"
#include "ClientHandler.h"
#include "IpList.h"
#include "TimeUtil.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>   // TCP_NODELAY
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <unistd.h>        // close

// Small helper: one-shot nonblocking writer for immediate 403 replies on denied IPs.
// All socket writes are driven by poll(); we never branch on errno after send().
class OneShotWriter : public EventLoop::Handler {
public:
    OneShotWriter(EventLoop& loop, const std::string& payload)
    : loop_(loop), buf_(payload), off_(0) {}

    virtual void onEvent(int fd, short revents) {
        // On error/hup/nval → stop and close.
        if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
            loop_.removeFD(fd);
            ::close(fd);
            delete this;
            return;
        }

        if (revents & POLLOUT) {
            // Non-blocking send; no errno branching. poll() will wake us again.
            ssize_t n = ::send(fd, &buf_[0] + off_, (int)(buf_.size() - off_), MSG_DONTWAIT);
            if (n > 0) {
                off_ += (size_t)n;
                if (off_ == buf_.size()) {
                    loop_.removeFD(fd);
                    ::close(fd);
                    delete this;
                    return;
                }
                // Still have bytes → keep POLLOUT; we'll be called again.
            } else if (n == 0) {
                // Peer closed; cleanup.
                loop_.removeFD(fd);
                ::close(fd);
                delete this;
                return;
            } else {
                // n < 0: made no progress. Do NOT check errno; just return and wait.
                return;
            }
        }
    }

private:
    EventLoop&   loop_;
    std::string  buf_;
    size_t       off_;
};

class AcceptorHandler : public EventLoop::Handler {
public:
    AcceptorHandler(EventLoop& loop, Server& srv, Listener* L)
    : eventLoop(loop), _srv(srv), listener(L) {}

    virtual void onEvent(int fd, short revents) {
        if (!listener || listener->getFD() != fd) return;

        // Listener gone/broken: stop accepting on it.
        if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
            eventLoop.removeFD(fd);
            return;
        }

        if (!(revents & POLLIN)) return;

        // Accept as many as available (non-blocking).
        for (;;) {
            struct sockaddr_storage ss;
            socklen_t sl = sizeof(ss);
            int cfd = ::accept(fd, (struct sockaddr*)&ss, &sl);
            if (cfd < 0) {
                if (errno == EINTR) continue;                 // retry accept
                if (errno == EAGAIN || errno == EWOULDBLOCK)   // no more clients
                    break;
                // Other accept errors: give up this turn.
                break;
            }

            // Optional: disable Nagle for low-latency small writes.
            int one = 1;
            (void)::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            // IP ACL check
            std::string ip = IpList::getIpFromSocket(&ss);
            if (!_srv.getConfig().ip_list.checkIp(ip)) {
                // Build 403 response and attach a one-shot POLLOUT writer.
                std::string out = IpList::ipDeniedResponse();
                OneShotWriter* w = new OneShotWriter(eventLoop, out);
                // Register only for POLLOUT; writer will close when done.
                eventLoop.addFD(cfd, POLLOUT, w);
                continue; // accept next client
            }

            // Normal client path: create connection & handler; register for POLLIN.
            ClientConnection* c = new ClientConnection(cfd, &_srv, TimeUtil::nowMs());
            c->setIp(ip);
            ClientHandler* h = new ClientHandler(eventLoop, c);
            _srv.trackHandler(h);
            eventLoop.addFD(cfd, POLLIN, h);
            // From here on, all I/O for this client flows through the single poll loop.
        }
    }

private:
    EventLoop&  eventLoop;
    Server&     _srv;
    Listener*   listener; // not owned
};

#endif // ACCEPTORHANDLER_H