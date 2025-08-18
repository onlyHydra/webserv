#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <vector>
#include <poll.h>

class EventLoop
{
	public:
		// Per-fd callback
		struct Handler
		{
			virtual ~Handler() {}
			virtual void onEvent(int fd, short revents) = 0;
		};

	public:
		EventLoop();
		~EventLoop();

		// Register / modify / remove a file descriptor
		bool addFD(int fd, short events);			  // legacy
		bool addFD(int fd, short events, Handler *h); // preferred
		bool modFD(int fd, short events);
		void removeFD(int fd); // deletes its handler

		// Main loop: poll + call handlers
		void run(int timeout_ms);
		void stop();

		// Legacy helper (optional)
		std::vector<std::pair<int, short> > handleEvents(int timeout_ms);

	private:
		std::vector<struct pollfd> _pfds;
		std::vector<Handler *> _hs; // aligned with _pfds
		bool _stop;

		int indexOfFD(int fd) const;
};

#endif // EVENTLOOP_H
