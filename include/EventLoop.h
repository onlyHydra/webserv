// include/EventLoop.h
#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <vector>
#include <map>
#include <poll.h>
#include "Debug.h"

class ClientConnection; // forward

class EventLoop
{
public:
	struct Handler
	{
		virtual ~Handler() {}
		virtual void onEvent(int fd, short revents) = 0;
	};

	EventLoop();
	~EventLoop();

	enum
	{
		EV_READ = 1,
		EV_WRITE = 2
	};

	// Register / modify / remove a file descriptor
	bool addFD(int fd, short events);						   // legacy
	bool addFD(int fd, short events, Handler *h);			   // preferred
	bool addFD(int fd, short events, ClientConnection *owner); // optional owner map
	bool modFD(int fd, short events);
	void removeFD(int fd);

	// *** Keep only this declaration ***
	std::vector<std::pair<int, short> > handleEvents(int timeout_ms);

	void run(int timeout_ms);
	void stop();

	// Owner helpers (optional)
	bool setOwner(int fd, ClientConnection *owner);
	void clearOwner(int fd);
	ClientConnection *ownerOf(int fd) const;
	int indexOfFD(int fd) const;
	void removeOwner(ClientConnection *owner);


private:

	std::vector<struct pollfd> _pfds;
	std::vector<Handler *> _hs; // aligned with _pfds
	bool _stop;

	std::map<int, int> watch_mask_;			   // if you track masks
	std::map<int, ClientConnection *> owners_; // fd -> owner
};

#endif // EVENTLOOP_H
