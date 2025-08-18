/* --- Server.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */
/* --- src/networking/Server.cpp --- */

/* --- src/networking/Server.cpp --- */
#include "Server.h"

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <set>
#include "UniqueFD.h"

static void throwErr(const char *what)
{
	throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

Server::Server(ServerConfig &srvConfig) : srvConfig(srvConfig)
{
	this->loop = EventLoop();
}

Server::~Server()
{
	stop();
}

void Server::registerListeners()
{
	for (std::vector<Listener *>::const_iterator it = listeners.begin();
		 it != listeners.end(); ++it)
	{
		if (*it && (*it)->getFD() >= 0)
		{
			loop.addFD((*it)->getFD(), POLLIN, new AcceptorHandler(loop, *this, *it));
		}
	}
}

void Server::unregisterListeners()
{
	for (std::vector<Listener *>::const_iterator it = listeners.begin();
		 it != listeners.end(); ++it)
	{
		if (*it && (*it)->getFD() >= 0)
		{
			loop.removeFD((*it)->getFD());
		}
	}
}

void Server::closeAll()
{
	for (std::vector<Listener *>::iterator it = listeners.begin();
		 it != listeners.end(); ++it)
	{
		delete *it; 
	}
	listeners.clear();
}

void Server::setNonBlocking(int fd)
{
	int flags = ::fcntl(fd, F_GETFL, 0);
	if (flags == -1)
		throwErr("fcntl(F_GETFL)");
	if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		throwErr("fcntl(F_SETFL O_NONBLOCK)");
}

void Server::setCloseOnExec(int fd)
{
	int flags = ::fcntl(fd, F_GETFD, 0);
	if (flags == -1)
		throwErr("fcntl(F_GETFD)");
	if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
		throwErr("fcntl(F_SETFD FD_CLOEXEC)");
}

void Server::stop()
{
	unregisterListeners();
	closeAll();
}

void Server::buildListenerPlan(std::vector<std::pair<std::string, int> > &unique_pairs,
							   std::map<std::pair<std::string, int>, std::vector<int> > &vsIndiciesByPair)
{

	unique_pairs.clear();
	vsIndiciesByPair.clear();
	std::set<std::pair<std::string, int> > uniq;
	const std::vector<VirtualServer>& servers = srvConfig.servers;
	for (std::vector<VirtualServer>::const_iterator it = servers.begin();
		 it != servers.end(); ++it)
	{
		const int idx = int(it - servers.begin());
		const int port = it->listen_port;
		if(port <= 0 || port > 65535) continue;
		const std::string host = it->listen_host.empty() ? std::string("0.0.0.0") : it->listen_host;
		const std::pair<std::string, int> key(host, port);
		if (uniq.insert(key).second)
			unique_pairs.push_back(key);
		vsIndiciesByPair[key].push_back(idx);
	}
}

int Server::createListenSocketRaw(const std::string &host, int port, bool &out_is_ipv6)
{

	struct addrinfo hints;
	// zero-init without memset (stays within std98 & allowed calls)
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_addrlen = 0;
	hints.ai_addr = 0;
	hints.ai_canonname = 0;
	hints.ai_next = 0;

	std::ostringstream oss;
	oss << port;
	const std::string portstr = oss.str();

	struct addrinfo *res = 0;
	int rc = getaddrinfo(host.c_str(), portstr.c_str(), &hints, &res);
	if (rc != 0)
	{
		throw std::runtime_error(
			std::string("getaddrinfo(") + host + ":" + portstr + "): " + gai_strerror(rc));
	}

	// ----- 2) Try each candidate until one binds & listens -----
	UniqueFD guard; // closes any candidate fd on scope exit
	out_is_ipv6 = false;
	int last_errno = 0;

	for (struct addrinfo *ai = res; ai; ai = ai->ai_next)
	{
		int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd == -1)
		{
			last_errno = errno;
			continue;
		}

		guard.reset(fd); // RAII takes ownership of this candidate

		// make robust restarts
		int yes = 1;
		if (setsockopt(guard.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
		{
			last_errno = errno;
			continue;
		}

		// non-blocking + close-on-exec before bind/listen (fails safe)
		try
		{
			setNonBlocking(guard.get());
			setCloseOnExec(guard.get());
		}
		catch (const std::exception &)
		{
			last_errno = errno; // errno set by fcntl
			// try next address; guard will close fd
			continue;
		}

		if (bind(guard.get(), ai->ai_addr, ai->ai_addrlen) == -1)
		{
			last_errno = errno;
			continue;
		}
		if (listen(guard.get(), SOMAXCONN) == -1)
		{
			last_errno = errno;
			continue;
		}

		// success
		out_is_ipv6 = (ai->ai_family == AF_INET6);
		int ok = guard.release();
		freeaddrinfo(res);
		return ok;
	}

	// ----- 3) None succeeded -----
	freeaddrinfo(res);
	// prefer a helpful message with the last errno we saw during attempts
	std::ostringstream emsg;
	emsg << "bind/listen failed for " << host << ":" << port
		 << (last_errno ? std::string(": ") + strerror(last_errno) : std::string(""));
	throw std::runtime_error(emsg.str());
}

static std::string lower_str(const std::string &s)
{
	std::string t = s;
	for (std::string::size_type i = 0; i < t.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(t[i]);
		if (c >= 'A' && c <= 'Z')
			t[i] = char(c - 'A' + 'a');
	}
	return t;
}

void Server::buildHostMaps()
{
    host_map_by_port.clear();
    default_vs_by_port.clear();

	for (std::vector<Listener*>::const_iterator it = listeners.begin();
			it != listeners.end(); ++it)
	{
		if (!*it) continue;

		const int port = (*it)->getPort();
		const std::vector<int>& vs_list = (*it)->virtualServerIndices();
		if (vs_list.empty()) continue;

		if (default_vs_by_port.find(port) == default_vs_by_port.end())
			default_vs_by_port[port] = vs_list.front();

		std::map<std::string,int>& hmap = host_map_by_port[port];
		for (std::vector<int>::const_iterator vit = vs_list.begin();
				vit != vs_list.end(); ++vit)
		{
			const int vs_idx = *vit;
			const VirtualServer& vs = srvConfig.servers[vs_idx];
			for (std::vector<std::string>::const_iterator sn = vs.server_names.begin();
					sn != vs.server_names.end(); ++sn)
			{
				const std::string key = lower_str(*sn);
				if (hmap.find(key) == hmap.end())
					hmap[key] = vs_idx;
			}
		}
	}
}


void Server::start()
{
	// 1) Plan listeners
	std::vector<std::pair<std::string,int> > hostPort;
	std::map<std::pair<std::string,int>, std::vector<int> > vsIndicesByPair;
	buildListenerPlan(hostPort, vsIndicesByPair);

	// 2) Create into a temporary vector
	std::vector<Listener*> tmp;
	tmp.reserve(hostPort.size());

	try {
		for (std::vector<std::pair<std::string,int> >::const_iterator it = hostPort.begin();
				it != hostPort.end(); ++it)
		{
			const std::string& host = it->first;
			const int          port = it->second;

			bool is6 = false;
			int fd = createListenSocketRaw(host, port, is6); // may throw

			Listener* L = new Listener(fd, host, port, is6);

			// attach VS indices for this (host,port)
			std::map<std::pair<std::string,int>, std::vector<int> >::const_iterator vit =
				vsIndicesByPair.find(*it);
			if (vit != vsIndicesByPair.end()) {
				L->setVirtualServerIndices(vit->second);
			}

			tmp.push_back(L); 
		}
	} catch (...) {
		// cleanup any partially built listeners
		for (std::vector<Listener*>::iterator dit = tmp.begin(); dit != tmp.end(); ++dit) {
			delete *dit;
		}
		throw;
	}

	// 3) Commit & register
	listeners.swap(tmp);              // take ownership
	registerListeners();

	// 4) Build routing maps
	buildHostMaps();

	for (std::vector<Listener*>::iterator dit = tmp.begin(); dit != tmp.end(); ++dit) {
		delete *dit;
	}
}


void Server::run(int poll_timeout_ms) {
	if (listeners.empty()) start();
	loop.run(poll_timeout_ms);
}