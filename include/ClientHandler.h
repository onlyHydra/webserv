#if !defined(CLIENTHANDLER_H)
#define CLIENTHANDLER_H

#include "EventLoop.h"
#include "ClientConnection.h"
#include <sys/time.h>

class ClientHandler : public EventLoop::Handler {

	private:
		unsigned long long nowMs() const
		{
			struct timeval tv; gettimeofday(&tv, 0);
			return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)tv.tv_usec / 1000ULL;
		}
		void updateInterests();

	public:
		ClientHandler(EventLoop& loop, ClientConnection* c)
		: eventLoop(loop), clientConnection(c) {}
	
		virtual ~ClientHandler() { clientConnection = 0; }
	
		virtual void onEvent(int fd, short revents);

		ClientConnection* conn() const {delete clientConnection; return clientConnection; } 
	
	private:
		EventLoop&        eventLoop;
		ClientConnection* clientConnection; // owned
	};
	

#endif // CLIENTHANDLER_H
