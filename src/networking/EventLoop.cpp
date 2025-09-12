// src/networking/EventLoop.cpp
#include "EventLoop.h"
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdio.h>


EventLoop::EventLoop() : _stop(false) {}
EventLoop::~EventLoop()
{
}

int EventLoop::indexOfFD(int fd) const
{
	for (size_t i = 0; i < _pfds.size(); ++i)
		if (_pfds[i].fd == fd)
			return static_cast<int>(i);
	return -1;
}

bool EventLoop::addFD(int fd, short events)
{
	// disambiguate to the Handler* overload
	return addFD(fd, events, static_cast<Handler *>(0));
}

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

bool EventLoop::addFD(int fd, short events, ClientConnection *owner)
{
	if (!addFD(fd, events, static_cast<Handler *>(0)))
		return false;
	owners_[fd] = owner;
	return true;
}

bool EventLoop::modFD(int fd, short events)
{
	int idx = indexOfFD(fd);
	if (idx < 0)
		return false;
	_pfds[idx].events = events;
	return true;
}

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
				continue; // may have been removed
			Handler *h = _hs[idx];
			if (h)
				h->onEvent(fd, rev);
		}
	}
}

void EventLoop::stop() { _stop = true; }

bool EventLoop::setOwner(int fd, ClientConnection *owner)
{
	if (indexOfFD(fd) < 0)
		return false;
	owners_[fd] = owner;
	return true;
}
void EventLoop::clearOwner(int fd) { owners_.erase(fd); }
ClientConnection *EventLoop::ownerOf(int fd) const
{
	std::map<int, ClientConnection *>::const_iterator it = owners_.find(fd);
	return (it == owners_.end() ? 0 : it->second);
}

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
