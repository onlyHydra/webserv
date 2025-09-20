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
#include <csignal>
#include <set>
#include "UniqueFD.h"
#include "AcceptorHandler.h"


static Server* g_srv = 0;


static volatile sig_atomic_t g_sigflag = 0;


extern "C" void sig_handler(int signo)
{
	g_sigflag = signo; // record which signal arrived (last one wins)
	if (g_srv && g_srv->sigpipe_wr >= 0) {
		char b = 1;
		ssize_t r = write(g_srv->sigpipe_wr, &b, 1);
		(void)r;
	}
}

static void throwErr(const char *what)
{
	throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

EventLoop& Server::getLoop() {
    return loop_;
}

Server::Server(ServerConfig &srvConfig) : srvConfig(srvConfig), shutdown_grace_ms(2000), shutdown_hard_ms(5000),
grace_deadline(0),hard_deadline_ms(0),sigpipe_rd(-1),sigpipe_wr(-1)
{
	this->loop_ = EventLoop();
	serverpipeline = new ServerPipeline();
}


Server::~Server()
{
	// stop();
	delete serverpipeline;
}

void Server::registerListeners()
{
	for (std::vector<Listener *>::const_iterator it = listeners.begin();
		 it != listeners.end(); ++it)
	{
		if (*it && (*it)->getFD() >= 0)
		{
			loop_.addFD((*it)->getFD(), POLLIN, new AcceptorHandler(loop_, *this, *it));
		}
	}
}

void Server::unregisterListeners()
{
	// We must delete the Listener objects and clear the vector,
	// not only remove their FDs from the loop.
	for (std::vector<Listener *>::iterator it = listeners.begin();
		it != listeners.end(); ++it)
	{
		Listener *lst = *it;
		if (!lst)
			continue;
        const int fd = lst->getFD();
        if (fd >= 0) {
            // Also deletes the per-fd handler inside EventLoop, if any.
            loop_.removeFD(fd);
        }
		
		delete lst;  // Listener dtor should close its fd (RAII)
		*it = 0;
	}
	listeners.clear();
}

void Server::closeAll()
{
	// Delete all Listener objects and clear the vector
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
	loop_.stop();
	shutdownAllHandlers();
}

void Server::buildListenerPlan(std::vector<std::pair<std::string, int> > &unique_pairs,
							   std::map<std::pair<std::string, int>, std::vector<int> > &vsIndiciesByPair)
{

	unique_pairs.clear();
	vsIndiciesByPair.clear();
	std::set<std::pair<std::string, int> > uniq;
	const std::vector<VirtualServer>& servers = srvConfig.servers();
	for (std::vector<VirtualServer>::const_iterator it = servers.begin();
		 it != servers.end(); ++it)
	{
		const int idx = int(it - servers.begin());
		const int port = it->listen_port;
		if(port <= 0 || port > 65535)
			continue;
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
		if (!*it)
			continue;
		const int port = (*it)->getPort();
		const std::vector<int>& vs_list = (*it)->virtualServerIndices();
		if (vs_list.empty())
			continue;
		if (default_vs_by_port.find(port) == default_vs_by_port.end())
			default_vs_by_port[port] = vs_list.front();

		std::map<std::string,int>& hmap = host_map_by_port[port];
		for (std::vector<int>::const_iterator vit = vs_list.begin();
				vit != vs_list.end(); ++vit)
		{
			const int vs_idx = *vit;
			const VirtualServer& vs = srvConfig.servers()[vs_idx];
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
	g_srv = this;
	initSignals();
	phase = RUNNING;
	while (phase != STOPPED) 
	{
		loop.runOnce(tick_ms);


		onTickAllConnections(TimeUtil::nowMs());

		if (phase != RUNNING)
		driveShutdownTick();

		drainReap();

		if (phase == DRAINING && server_handlers.empty())
		phase = STOPPED;
	}

// final cleanup
	if (sigpipe_rd >= 0) { loop.removeFD(sigpipe_rd); close(sigpipe_rd); sigpipe_rd = -1; }
	if (sigpipe_wr >= 0) { close(sigpipe_wr); sigpipe_wr = -1; }
	g_srv = 0;
}

static std::string normalize_host(const std::string &h)
{
	std::string s = h;
	for (size_t i = 0; i < s.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (c >= 'A' && c <= 'Z')
			s[i] = char(c - 'A' + 'a');
	}
	if (!s.empty() && s[0] == '[')
	{
		std::string::size_type rb = s.find(']');
		if (rb != std::string::npos)
			return s.substr(0, rb + 1);
		return s;
	}
	std::string::size_type cpos = s.find(':');
	return (cpos == std::string::npos) ? s : s.substr(0, cpos);
}

int Server::resolveVirtualServerByPort(int localPort, const std::string &hostHdr) const
{
	const std::string key = normalize_host(hostHdr);
	std::map<int, std::map<std::string, int> >::const_iterator pm = host_map_by_port.find(localPort);
	if (pm != host_map_by_port.end())
	{
		std::map<std::string, int>::const_iterator it = pm->second.find(key);
		if (it != pm->second.end())
			return it->second;
	}
	std::map<int, int>::const_iterator d = default_vs_by_port.find(localPort);
	return (d != default_vs_by_port.end()) ? d->second : -1;
}

const ServerConfig& Server::getConfig()const
{
	return srvConfig;
}

void Server::initSignals() {
	int p[2];
	if (::pipe(p) == -1) { /* handle fatal error */ }
	sigpipe_rd = p[0]; sigpipe_wr = p[1];

	// set nonblocking
	fcntl(sigpipe_rd, F_SETFL, O_NONBLOCK);
	fcntl(sigpipe_wr, F_SETFL, O_NONBLOCK);

	// register sigpipe_rd in your EventLoop with a trivial handler
	loop.addFD(sigpipe_rd, POLLIN, this /*owner*/, /*userdata*/ SIGPIPE_MAGIC);

	// install handlers
	struct sigaction sa; memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;     // fine for us
	sigaction(SIGINT,  &sa, 0);
	sigaction(SIGTERM, &sa, 0);

// optional: second signal forces hard stop
// you can also add SIGQUIT to force immediate kill
}


void Server::onSignalByte()
{
	int signo = g_sigflag; // last signal observed
	if (phase == RUNNING) {
		beginQuiesce(signo);
	} else {
		// A second signal while shutting down => shorten the deadlines
		hard_deadline_ms = TimeUtil::nowMs() + 200; // e.g. 200ms
		grace_deadline = hard_deadline_ms;
	}
}


void Server::beginQuiesce(int signo)
{
	if (phase != RUNNING) return;
		phase = QUIESCING;

	// 1) Close/unregister all listeners *first*
	unregisterListeners();   // removeFD(listenfd); delete acceptor; delete Listener; clear vectors

	// 2) Tell every active connection: no new requests; close when done
	for (std::set<ClientHandler*>::iterator it = server_handlers.begin();
		it != server_handlers.end(); ++it) {
		ClientConnection* c = (*it)->conn();
		if (!c) continue;
		c->requestCloseSoon();   // you add this: set a flag used by response path
		c->disableReads();       // stop accepting next request on keep-alive
	}

	// enter draining
	phase = DRAINING;
	const unsigned long long now = TimeUtil::nowMs();
	grace_deadline = now + shutdown_grace_ms;
	hard_deadline_ms  = now + shutdown_hard_ms;
}


void Server::driveShutdownTick() {
	const unsigned long long now = TimeUtil::nowMs();

	if (phase == DRAINING) {
		// Optional: nudge idle keep-alive sockets to close now (no response in progress)
		for (std::set<ClientHandler*>::iterator it = server_handlers.begin();
			it != server_handlers.end(); ++it) {
		ClientConnection* c = (*it)->conn();
		if (!c) continue;

		if (c->isIdleWaitingForNextRequest()) {
			// Close immediately (send FIN); nothing to drain
			c->shutdownWrite(); // or c->state(PH_CLOSE) and let handler flush close
		}

		// Kill CGIs that haven't produced headers before grace deadline
		if (now >= grace_deadline)
			c->killCGIIfNotResponding();
		}

		// If we’ve passed grace deadline — force-close remaining
		if (now >= grace_deadline) {
		for (std::set<ClientHandler*>::iterator it = server_handlers.begin();
			it != server_handlers.end(); ++it) {
			ClientConnection* c = (*it)->conn();
			if (!c) continue;
			c->forceClose();  // hard close: shutdown both dirs, mark PH_CLOSE
		}
		phase = REAPING;
		}
	}

	if (phase == REAPING) {
		// If no handlers left, we’re done
		if (server_handlers.empty()) {
		phase = STOPPED;
		} else if (now >= hard_deadline_ms) {
		// Safety: if anything still around, just reap anyway
		for (std::set<ClientHandler*>::iterator it = server_handlers.begin();
			it != server_handlers.end(); ) {
			ClientHandler* h = *it++;
			loop.removeOwner(h->conn());
			if (loop.indexOfFD(h->conn()->fd()) >= 0) loop.removeFD(h->conn()->fd());
			server_handlers.erase(h);
			delete h;
		}
		phase = STOPPED;
		}
	}
}
