#include "EventLoop.h"
#include <cerrno>
#include <cstring>

EventLoop::EventLoop() : _stop(false) {}

EventLoop::~EventLoop()
{
	for (size_t i = 0; i < _hs.size(); ++i)
		delete _hs[i];
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
	return addFD(fd, events, 0);
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
			delete _hs[idx];
			_hs[idx] = 0;
		}
		_pfds[idx] = _pfds.back();
		_pfds.pop_back();
		_hs[idx] = _hs.back();
		_hs.pop_back();
	}
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

        // --- Timer tick on poll timeout ---
        if (rc == 0)
        {
            // Build a dispatch list with revents == 0 (tick)
            std::vector< std::pair<int, short> > dispatch;
            dispatch.reserve(_pfds.size());
            for (size_t i = 0; i < _pfds.size(); ++i)
                dispatch.push_back(std::make_pair(_pfds[i].fd, (short)0));

            for (size_t i = 0; i < dispatch.size(); ++i)
            {
                const int   fd  = dispatch[i].first;
                const short rev = 0; // tick
                int idx = indexOfFD(fd);
                if (idx < 0)
                    continue; // maybe removed by a prior handler
                Handler *h = _hs[idx];
                if (h)
                    h->onEvent(fd, rev); // tick: lets connections enforce deadlines
            }
            continue; // next poll
        }

        // --- Normal event dispatch ---
        std::vector< std::pair<int, short> > dispatch;
        dispatch.reserve(_pfds.size());
        for (size_t i = 0; i < _pfds.size(); ++i)
        {
            short rev = _pfds[i].revents;
            if (rev && !(rev & POLLNVAL))
                dispatch.push_back(std::make_pair(_pfds[i].fd, rev));
        }

        for (size_t i = 0; i < dispatch.size(); ++i)
        {
            const int   fd  = dispatch[i].first;
            const short rev = dispatch[i].second;
            int idx = indexOfFD(fd);
            if (idx < 0)
                continue; // maybe removed by prior handler
            Handler *h = _hs[idx];
            if (h)
                h->onEvent(fd, rev); // may call removeFD(fd)
        }
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

void EventLoop::stop() { _stop = true; }
