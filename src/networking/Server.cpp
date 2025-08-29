/* --- Server.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */
/* --- src/networking/Server.cpp --- */

/* --- src/networking/Server.cpp --- */
// src/networking/Server.cpp
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

Server::Server(ServerConfig &srvConfig)
    : srvConfig(srvConfig),
      serverpipeline(new ServerPipeline())
{
    // loop_ default-constructs; do not assign through accessor.
}

Server::~Server()
{
    // stop(); // optional: your tests may rely on explicit stop() elsewhere
    delete serverpipeline;
}

EventLoop &Server::loop() { return loop_; }
const EventLoop &Server::loop() const { return loop_; }

void Server::registerListeners()
{
    for (std::vector<Listener *>::const_iterator it = listeners.begin();
         it != listeners.end(); ++it)
    {
        if (*it && (*it)->getFD() >= 0)
        {
            // AcceptorHandler takes (EventLoop&, Server&, Listener*)
            loop_.addFD((*it)->getFD(), POLLIN, new AcceptorHandler(loop_, *this, *it));
        }
    }
}

void Server::unregisterListeners()
{
    // Remove from loop and delete the Listener objects
    for (std::vector<Listener *>::iterator it = listeners.begin();
         it != listeners.end(); ++it)
    {
        Listener *lst = *it;
        if (!lst)
            continue;

        const int fd = lst->getFD();
        if (fd >= 0)
        {
            loop_.removeFD(fd); // also deletes per-fd handler inside EventLoop
        }
        delete lst; // Listener dtor should close its fd (RAII)
        *it = 0;
    }
    listeners.clear();
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
    loop_.stop();
}

void Server::buildListenerPlan(
    std::vector<std::pair<std::string, int> > &unique_pairs,
    std::map<std::pair<std::string, int>, std::vector<int> > &vsIndiciesByPair // keep your spelling if header matches
)
{
    unique_pairs.clear();
    vsIndiciesByPair.clear();
    std::set<std::pair<std::string, int> > uniq;

    const std::vector<VirtualServer> &servers = srvConfig.servers();
    for (std::vector<VirtualServer>::const_iterator it = servers.begin();
         it != servers.end(); ++it)
    {
        const int idx = int(it - servers.begin());
        const int port = it->listen_port;
        if (port <= 0 || port > 65535)
            continue;

        const std::string host = it->listen_host.empty()
                                     ? std::string("0.0.0.0")
                                     : it->listen_host;
        const std::pair<std::string, int> key(host, port);
        if (uniq.insert(key).second)
            unique_pairs.push_back(key);
        vsIndiciesByPair[key].push_back(idx);
    }
}

int Server::createListenSocketRaw(const std::string &host, int port, bool &out_is_ipv6)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::ostringstream oss;
    oss << port;
    const std::string portstr = oss.str();

    struct addrinfo *res = 0;
    int rc = ::getaddrinfo(host.c_str(), portstr.c_str(), &hints, &res);
    if (rc != 0)
    {
        throw std::runtime_error(
            std::string("getaddrinfo(") + host + ":" + portstr + "): " + ::gai_strerror(rc));
    }

    UniqueFD guard;
    out_is_ipv6 = false;
    int last_errno = 0;

    for (struct addrinfo *ai = res; ai; ai = ai->ai_next)
    {
        int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == -1)
        {
            last_errno = errno;
            continue;
        }
        guard.reset(fd);

        int yes = 1;
        if (::setsockopt(guard.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
        {
            last_errno = errno;
            continue;
        }

        try
        {
            setNonBlocking(guard.get());
            setCloseOnExec(guard.get());
        }
        catch (...)
        {
            last_errno = errno;
            continue;
        }

        if (::bind(guard.get(), ai->ai_addr, ai->ai_addrlen) == -1)
        {
            last_errno = errno;
            continue;
        }
        if (::listen(guard.get(), SOMAXCONN) == -1)
        {
            last_errno = errno;
            continue;
        }

        out_is_ipv6 = (ai->ai_family == AF_INET6);
        int ok = guard.release();
        ::freeaddrinfo(res);
        return ok;
    }

    ::freeaddrinfo(res);
    std::ostringstream emsg;
    emsg << "bind/listen failed for " << host << ":" << port
         << (last_errno ? std::string(": ") + std::strerror(last_errno) : std::string(""));
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

    for (std::vector<Listener *>::const_iterator it = listeners.begin();
         it != listeners.end(); ++it)
    {
        if (!*it)
            continue;

        const int port = (*it)->getPort();
        const std::vector<int> &vs_list = (*it)->virtualServerIndices();
        if (vs_list.empty())
            continue;

        if (default_vs_by_port.find(port) == default_vs_by_port.end())
            default_vs_by_port[port] = vs_list.front();

        std::map<std::string, int> &hmap = host_map_by_port[port];
        for (std::vector<int>::const_iterator vit = vs_list.begin();
             vit != vs_list.end(); ++vit)
        {
            const int vs_idx = *vit;
            const VirtualServer &vs = srvConfig.servers()[vs_idx];
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
    std::vector<std::pair<std::string, int> > hostPort;
    std::map<std::pair<std::string, int>, std::vector<int> > vsIndicesByPair;
    buildListenerPlan(hostPort, vsIndicesByPair);

    // 2) Create temporary set
    std::vector<Listener *> tmp;
    tmp.reserve(hostPort.size());

    try
    {
        for (std::vector<std::pair<std::string, int> >::const_iterator it = hostPort.begin();
             it != hostPort.end(); ++it)
        {
            const std::string &host = it->first;
            const int port = it->second;

            bool is6 = false;
            int fd = createListenSocketRaw(host, port, is6); // may throw

            Listener *L = new Listener(fd, host, port, is6);

            std::map<std::pair<std::string, int>, std::vector<int> >::const_iterator vit =
                vsIndicesByPair.find(*it);
            if (vit != vsIndicesByPair.end())
            {
                L->setVirtualServerIndices(vit->second);
            }

            tmp.push_back(L);
        }
    }
    catch (...)
    {
        for (std::vector<Listener *>::iterator dit = tmp.begin(); dit != tmp.end(); ++dit)
        {
            delete *dit;
        }
        throw;
    }

    // 3) Commit & register
    listeners.swap(tmp);
    registerListeners();

    // 4) Build routing maps
    buildHostMaps();

    // old listeners (if any) are now in tmp (after swap); delete them
    for (std::vector<Listener *>::iterator dit = tmp.begin(); dit != tmp.end(); ++dit)
    {
        delete *dit;
    }
}

void Server::run(int poll_timeout_ms)
{
    if (listeners.empty())
        start();
    loop_.run(poll_timeout_ms);
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

const ServerConfig &Server::getConfig() const
{
    return srvConfig;
}
