/* --- Server.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef SERVER_H
#define SERVER_H

#ifdef USE_STUBS
  #include "stubs/EventLoop.h"
  #include "stubs/ServerConfig.h"
#else
  #include "EventLoop.h"
  #include "ServerConfig.h"
#endif

#include "Listener.h"
#include "ClientConnection.h"
#include "ServerPipeline.h"
#include <vector>
#include <string>
#include <poll.h>
#include <map>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <netinet/in.h>




#pragma once
#include <map>
#include <vector>
#include <string>
#include "EventLoop.h"
#include "ServerConfig.h"
#include "Listener.h"
#include "ServerPipeline.h"
#include "ClientConnection.h"   // if this creates a cycle, forward-declare and move ClientHandler dtor to .cpp

class Server
{
private:
    ServerConfig &srvConfig;
    std::vector<Listener*> listeners;
    std::map<int, std::map<std::string, int> > host_map_by_port; // port -> (host -> vs_index)
    std::map<int, int> default_vs_by_port;                       // port -> default vs_index
    ServerPipeline *serverpipeline;

    EventLoop loop_;  // renamed from 'loop' to avoid clash with accessor names

    int  createListenSocketRaw(const std::string &host, int port, bool &out_is_ipv6);
    void registerListeners();
    void unregisterListeners();
    void closeAll();

    void buildListenerPlan(std::vector<std::pair<std::string, int> > &unique_pairs,
                           std::map<std::pair<std::string, int>, std::vector<int> > &vs_indices_by_pair);

    void buildHostMaps();

public:
    explicit Server(ServerConfig &srvConfig);
    ~Server();

    // Accessors for the event loop
    EventLoop&       loop();        // implemented in Server.cpp: `return loop_;`
    const EventLoop& loop() const;  // implemented in Server.cpp: `return loop_;`

    void start();
    void stop();
    void run(int poll_timeout_ms);

    void setNonBlocking(int fd);
    void setCloseOnExec(int fd);

    int  resolveVirtualServerByPort(int localPort, const std::string& hostHdr) const;

    const ServerConfig& getConfig() const;
    ServerPipeline*     getPipeline() const { return serverpipeline; }

#ifdef UNIT_TEST
public:
    size_t listenerCount() const { return listeners.size(); }
    int    listenerFD(size_t i) const { return (i < listeners.size() && listeners[i]) ? listeners[i]->getFD() : -1; }
    int    listenerPortAt(size_t i) const { return (i < listeners.size() && listeners[i]) ? listeners[i]->getPort() : -1; }
#endif
};

// If including ClientConnection.h here causes an include cycle, forward-declare:
// class ClientConnection; and move the destructor definition to a .cpp file.

class ClientHandler : public EventLoop::Handler {
public:
    ClientHandler(EventLoop& loop, ClientConnection* c)
    : eventLoop(loop), clientConnection(c) {}

    virtual ~ClientHandler() { delete clientConnection; }

    virtual void onEvent(int fd, short revents) {
        // Poll timeout “tick”
        if (revents == 0) {
            clientConnection->onTick();
            if (clientConnection->isClosed()) { eventLoop.removeFD(fd); return; }

            short want = 0;
            if (clientConnection->wantsRead() && !clientConnection->isReadPaused()) want |= POLLIN;
            if (clientConnection->hasPendingWrite())                                 want |= POLLOUT;
            if (want == 0 && clientConnection->wantsRead())                          want  = POLLIN;
            eventLoop.modFD(fd, want);
            return;
        }

        // Error / hangup
        if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
            clientConnection->close();
            eventLoop.removeFD(fd);
            return;
        }

        // Readable
        if (revents & POLLIN) {
            clientConnection->onReadable();
            if (clientConnection->isClosed()) { eventLoop.removeFD(fd); return; }
        }

        // Writable
        if (revents & POLLOUT) {
            clientConnection->onWritable();
            if (clientConnection->isClosed()) { eventLoop.removeFD(fd); return; }
        }

        // Recompute interest (backpressure-aware)
        short want = 0;
        if (clientConnection->wantsRead() && !clientConnection->isReadPaused()) want |= POLLIN;
        if (clientConnection->hasPendingWrite())                                 want |= POLLOUT;

        // Keep POLLOUT one extra tick after flush so the connection can finalize/close
        if (!clientConnection->hasPendingWrite() && clientConnection->wantsWriteLinger())
            want |= POLLOUT;

        if (want == 0 && clientConnection->wantsRead()) want = POLLIN;
        eventLoop.modFD(fd, want);
    }

private:
    EventLoop&        eventLoop;
    ClientConnection* clientConnection; // owned
};






class AcceptorHandler : public EventLoop::Handler {
	public:
		AcceptorHandler(EventLoop& loop, Server& srv, Listener* L)
		: eventLoop(loop), _srv(srv), listener(L) {}

		virtual void onEvent(int fd, short revents) {
			if (!listener || listener->getFD() != fd) return;

			if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
				eventLoop.removeFD(fd); // stop accepting on this listener
				return;
			}

			if (revents & POLLIN) {
				for (;;) {
					struct sockaddr_storage ss;
					socklen_t sl = sizeof(ss);
					int cfd = ::accept(fd, (struct sockaddr*)&ss, &sl);
					if (cfd == -1) {
						if (errno == EINTR) continue;
						if (errno == EAGAIN || errno == EWOULDBLOCK) break;
						break; // other errors: give up this turn
					}
					// configure client
					try {

						// disable Nagle to reduce small-write latency for tests
						int one = 1;
						::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
					}
					catch (...) { ::close(cfd); continue; }

					// register client handler
					ClientConnection* c = new ClientConnection(cfd,&_srv);
					eventLoop.addFD(cfd, POLLIN, new ClientHandler(eventLoop, c));
					// If the client has already sent data (common in tests that write
					// immediately after connect), process it right away instead of
					// waiting for the next poll cycle. Adjust poll registration if
					// the connection now wants to write.
				}
			}
		}
	private:
		EventLoop& eventLoop;
		Server& _srv;
		Listener* listener; // not owned
};




#endif // SERVER_H


