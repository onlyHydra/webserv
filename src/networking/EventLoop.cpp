// src/networking/EventLoop.cpp
#include "EventLoop.h"
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdio.h>




/* 

EventLoop::EventLoop() / ~EventLoop()

Constructor initializes the loop in a stopped state. 
Destructor performs no extra work; cleanup of FDs is explicit elsewhere. 
Keeping construction and destruction lightweight is important because Server 
may create an EventLoop even before listeners exist, 
and destruction may occur during shutdown while FDs are already removed. 
This guarantees no accidental syscalls or races during construction/teardown. 
The design intentionally leaves ownership of resources (handlers, client connections) 
to higher-level objects, so the event loop stays as a thin poll() 
wrapper with no heavy resource management responsibilities.


*/


EventLoop::EventLoop() : _stop(false) {}
EventLoop::~EventLoop()
{
}

/* 

int EventLoop::indexOfFD(int fd) const

Searches the _pfds vector for the index of a given file descriptor, 
returning -1 if not found. It’s a small utility heavily used by addFD, modFD, 
and removeFD. Because poll is managed via an array, this linear search provides 
mapping between a descriptor and its handler slot. Even though O(n), it’s acceptable 
because poll itself scales linearly, and the number of active descriptors is bounded. 
By isolating lookup logic, later modifications remain centralized and less error-prone. 
This helper underpins consistent updates when descriptors are registered or removed from the loop.


*/


int EventLoop::indexOfFD(int fd) const
{
	for (size_t i = 0; i < _pfds.size(); ++i)
		if (_pfds[i].fd == fd)
			return static_cast<int>(i);
	return -1;
}


/* 

bool EventLoop::addFD(int fd, short events)

Convenience overload forwarding to addFD(fd, events, static_cast<Handler*>(0)). 
It simplifies cases where no handler is attached, just event interest. 
By centralizing FD addition, it enforces consistent initialization of pollfd 
structures and prevents duplicate registrations. Returning a bool gives callers quick 
feedback on success/failure. This version is mostly syntactic sugar but avoids 
repeating NULL arguments in call sites, improving readability for basic cases like 
registering listening sockets or temporary descriptors without associated handlers. 
It’s part of the loop’s usability layer.


*/

bool EventLoop::addFD(int fd, short events)
{
	// disambiguate to the Handler* overload
	return addFD(fd, events, static_cast<Handler *>(0));
}

/* 

bool EventLoop::addFD(int fd, short events, Handler h)*

Adds a descriptor to _pfds if valid and not already present. 
Creates a pollfd struct, sets requested events, resets revents, 
and pushes it to the vector. Also records the handler pointer in _hs, aligned by index. 
This is the backbone registration path: almost all descriptors (client sockets, CGI pipes) 
are registered here, often with a handler dispatcher. The checks avoid double-registration errors. 
Pairing Handler* with pollfd allows callback-style event dispatch: once poll returns, 
onEvent can be invoked on the right handler. This function is central to multiplexing work.

*/

bool EventLoop::addFD(int fd, short events, Handler *h)
{
	if (fd < 0)
		return false;
	if (indexOfFD(fd) != -1)
		return false;
	struct pollfd p;
	p.fd = fd;
	p.events = events;
	p.revents = 0;
	_pfds.push_back(p);
	_hs.push_back(h);
	return true;
}

/* 

bool EventLoop::addFD(int fd, short events, ClientConnection owner)*

Variation that attaches ownership instead of a handler. 
It still calls the Handler* version to register with poll, then maps fd → ClientConnection* in owners_. 
This mapping is useful when multiple descriptors (like CGI stdin/stdout) 
belong to the same client connection; later removal or cleanup can operate by owner rather than fd alone. 
This design enables higher-level cleanup (removeOwner) to safely sweep all descriptors associated with a given connection, 
preventing leaks when connections close unexpectedly. 
It extends the loop’s API for lifecycle management while keeping base poll operations consistent.

*/

bool EventLoop::addFD(int fd, short events, ClientConnection *owner)
{
	if (!addFD(fd, events, static_cast<Handler *>(0)))
		return false;
	owners_[fd] = owner;
	return true;
}

/* 

bool EventLoop::modFD(int fd, short events)

Modifies interest flags for a registered fd by finding its index and updating events. 
If not found, returns false. This is crucial for flow control: when output buffers are empty, 
POLLOUT interest is removed; when data arrives or backpressure clears, POLLOUT is re-enabled. 
By exposing a small API to adjust events, the loop allows handlers to fine-tune responsiveness 
and avoid unnecessary wakeups. This dynamic updating keeps the server efficient under load, 
ensuring poll only wakes for actionable I/O rather than constantly firing.


*/

bool EventLoop::modFD(int fd, short events)
{
	int idx = indexOfFD(fd);
	if (idx < 0)
		return false;
	_pfds[idx].events = events;
	return true;
}


/* 

void EventLoop::removeFD(int fd)

Removes an fd from _pfds, _hs, owners_, and watch_mask_. 
It swaps the last element into the removed slot for O(1) deletion, 
then shrinks the vectors. Clearing handler pointers ensures no dangling callbacks remain. 
This function is called whenever a connection closes, 
a CGI process ends, or a listener is stopped. Proper removal prevents poll from reporting 
stale fds and avoids leaks in owner maps. It’s a critical hygiene step that ensures correctness in long-running 
servers where thousands of connections may be opened and closed over time.

*/

void EventLoop::removeFD(int fd)
{
	int idx = indexOfFD(fd);
	if (idx >= 0)
	{
		if (_hs[idx])
		{
			_hs[idx] = 0;
		}
		_pfds[idx] = _pfds.back();
		_pfds.pop_back();
		_hs[idx] = _hs.back();
		_hs.pop_back();
		owners_.erase(fd);
		watch_mask_.erase(fd);
	}
}


/* 

std::vector<std::pair<int,short>> EventLoop::handleEvents(int timeout_ms)

Wrapper for poll that waits up to timeout_ms, then builds a vector of 
(fd, revents) pairs for ready descriptors. Filters out POLLNVAL invalid results. 
Returning a vector decouples event collection from dispatching, letting higher-level 
code choose how to iterate and handle them. This separation supports cases like unit 
testing or manual multiplexing without full loop control. It’s lighter than the full run() loop, 
often used when external scheduling drives the event loop or when integrating into a 
larger system that already has its own control loop.


*/

std::vector<std::pair<int, short> > EventLoop::handleEvents(int timeout_ms)
{
	std::vector<std::pair<int, short> > ev;
	if (_pfds.empty())
		return ev;

	int rc = ::poll(&_pfds[0], _pfds.size(), timeout_ms);
	if (rc <= 0)
		return ev;

	ev.reserve(_pfds.size());
	for (size_t i = 0; i < _pfds.size(); ++i)
	{
		if (_pfds[i].revents && !(_pfds[i].revents & POLLNVAL))
			ev.push_back(std::make_pair(_pfds[i].fd, _pfds[i].revents));
	}
	return ev;
}


/* 

void EventLoop::run(int timeout_ms)

The main loop: resets _stop, then repeatedly polls all registered descriptors 
with timeout_ms. If poll returns <0 with EINTR, it retries; other errors break. 
On timeout (rc==0), it calls onEvent(fd,0) on all handlers—this provides “tick” 
behavior for deadlines. On activity, it collects ready fds into a dispatch list, 
then calls the associated Handler::onEvent(fd, revents). This implements the project 
requirement of a single poll() managing all client sockets and CGI pipes simultaneously. 
It centralizes multiplexing, ensures fairness, and gives handlers control to enforce per-phase deadlines.


*/

void EventLoop::run(int timeout_ms)
{
	_stop = false;
	while (!_stop)
	{
		if (_pfds.empty())
			break;

		int rc = ::poll(&_pfds[0], _pfds.size(), timeout_ms);
		if (rc < 0)
		{
			if (errno == EINTR)
				continue;
			break;
		}

		if (rc == 0)
		{
			// timer tick: let handlers enforce deadlines
			for (size_t i = 0; i < _pfds.size(); ++i)
			{
				int idx = static_cast<int>(i);
				Handler *h = _hs[idx];
				if (h)
					h->onEvent(_pfds[i].fd, 0);
			}
			continue;
		}

		// normal dispatch
		std::vector<std::pair<int, short> > dispatch;
		dispatch.reserve(_pfds.size());
		for (size_t i = 0; i < _pfds.size(); ++i)
		{
			short rev = _pfds[i].revents;
			if (rev && !(rev & POLLNVAL))
				dispatch.push_back(std::make_pair(_pfds[i].fd, rev));
		}

		for (size_t i = 0; i < dispatch.size(); ++i)
		{
			const int fd = dispatch[i].first;
			const short rev = dispatch[i].second;
			if (DEBUG_CGI) // Defined in Debug.h, changed so now final version doesnt output
			fprintf(stderr, "[EV] fd=%d revents=0x%x%s%s%s%s%s\n",
							fd, rev,
							(rev & POLLIN) ? " POLLIN" : "",
							(rev & POLLOUT)? " POLLOUT":"",
							(rev & POLLERR)? " POLLERR":"",
							(rev & POLLHUP)? " POLLHUP":"",
							(rev & POLLNVAL)?" POLLNVAL":"");

			int idx = indexOfFD(fd);
			if (idx < 0)
				continue;
			Handler *h = _hs[idx];
			if (h)
				h->onEvent(fd, rev);
		}
	}
}


/* 

void EventLoop::stop()

Sets _stop to true, breaking the run() loop at the next iteration. 
This provides a clean shutdown mechanism: the server can signal the 
loop to exit gracefully after all current events are handled, 
rather than abruptly terminating. By making it a simple flag, 
it avoids synchronization complexity in an otherwise single-threaded event 
loop design. It’s essential for stopping the server cleanly on signals 
like SIGINT or during controlled restarts, ensuring all resources 
get cleaned before process termination.


*/

void EventLoop::stop() { _stop = true; }


/* 

bool EventLoop::setOwner(int fd, ClientConnection owner)*

Associates a connection object with a registered fd. If fd isn’t registered, 
returns false. This supports mapping multiple fds (e.g., CGI stdin, stdout) 
back to a single client connection for lifecycle tracking. It’s used when 
handlers want to identify which connection owns a descriptor without duplicating 
ownership logic elsewhere. This adds another indirection layer that helps cleanup and 
error handling: when the connection dies, the loop can drop all owned descriptors at once. 
It’s an optional but helpful bookkeeping feature to maintain robust connection management.

*/

bool EventLoop::setOwner(int fd, ClientConnection *owner)
{
	if (indexOfFD(fd) < 0)
		return false;
	owners_[fd] = owner;
	return true;
}


/* 

void EventLoop::clearOwner(int fd)

Removes the fd→connection mapping without removing the fd from the poll set. 
This is used when the fd is still monitored but no longer considered logically 
owned by a connection, e.g., during shutdown or after handoff. 
It avoids confusion during teardown and ensures the server doesn’t mis-associate descriptors 
with destroyed objects. Keeping explicit owner clearing helps prevent use-after-free bugs where a 
lingering mapping might later be dereferenced incorrectly.

*/


void EventLoop::clearOwner(int fd) { 
	owners_.erase(fd); 
}

/* 

ClientConnection EventLoop::ownerOf(int fd) const*

Returns the owning ClientConnection* for a given fd, 
or NULL if none. This is a quick lookup tool when a descriptor activity 
needs to be attributed back to its higher-level connection object. 
It’s essential when cleanup must close multiple fds for a connection or 
when propagating errors upwards. Centralizing it in the loop avoids duplicating 
maps across handlers and keeps ownership bookkeeping consistent.

*/

ClientConnection *EventLoop::ownerOf(int fd) const
{
	std::map<int, ClientConnection *>::const_iterator it = owners_.find(fd);
	return (it == owners_.end() ? 0 : it->second);
}


/* 

void EventLoop::removeOwner(ClientConnection owner)*

Iterates through all fd→owner mappings, collects those belonging to the specified connection, 
and calls removeFD on each. This is how a full connection teardown is performed: 
kill all its fds from the loop in one sweep. It’s essential for proper 
cleanup when a client disconnects or times out, preventing dangling sockets. 
The design ensures that even if a connection registered multiple descriptors (socket, CGI pipes), 
all get unregistered safely. It guarantees the poll set remains consistent 
and that resources don’t leak across keep-alive cycles or error paths.

*/

void EventLoop::removeOwner(ClientConnection *owner)
{
	if (!owner)
		return;
	std::vector<int> fds;
	for (std::map<int, ClientConnection *>::iterator it = owners_.begin();
		 it != owners_.end(); ++it)
	{
		if (it->second == owner)
			fds.push_back(it->first);
	}
	for (size_t i = 0; i < fds.size(); ++i)
		removeFD(fds[i]);
}
