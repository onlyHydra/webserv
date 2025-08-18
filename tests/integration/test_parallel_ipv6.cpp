#include <catch2/catch_all.hpp>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>

#include "Server.h"
#include "ServerConfig.h"
#include "VirtualServer.h"

#include "helpers/net.hpp"

TEST_CASE("Parallel clients: 16 requests in parallel", "[server][parallel]")
{
	const int port = pick_free_port_ipv4();
	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.push_back(vs);
	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	std::thread loop([&]
					 { s.run(25); });

	const int N = 16;
	std::vector<std::thread> threads;
	threads.reserve(N);
	std::vector<std::string> results(N);

	for (int i = 0; i < N; ++i)
	{
		threads.push_back(std::thread([&, i]
									  {
            int fd = connect_ipv4(port);
            const char req[] = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
            write_all(fd, req, sizeof(req)-1);
            results[i] = read_until_eof(fd);
            ::close(fd); }));
	}

	for (size_t i = 0; i < threads.size(); ++i)
		threads[i].join();

	s.stop();
	if (loop.joinable())
		loop.join();

	for (size_t i = 0; i < results.size(); ++i)
	{
		REQUIRE(results[i].find("HTTP/1.1 200") != std::string::npos);
		REQUIRE(results[i].substr(results[i].size() - 5) == "hello");
	}
}

#ifdef AF_INET6
#include <netdb.h>
static bool ipv6_loopback_available()
{
	int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0)
		return false;
	sockaddr_in6 sa6;
	std::memset(&sa6, 0, sizeof(sa6));
	sa6.sin6_family = AF_INET6;
	sa6.sin6_addr = in6addr_loopback; // ::1
	sa6.sin6_port = htons(0);
	bool ok = (::bind(fd, (sockaddr *)&sa6, sizeof(sa6)) == 0) && (::listen(fd, 1) == 0);
	::close(fd);
	return ok;
}

TEST_CASE("IPv6 (::1) optional request", "[server][ipv6][optional]")
{
	if (!ipv6_loopback_available())
	{
		SUCCEED("IPv6 loopback not available on this host");
		return;
	}
	// pick an ephemeral port by letting kernel choose via a probe socket
	int probe = ::socket(AF_INET6, SOCK_STREAM, 0);
	REQUIRE(probe != -1);
	sockaddr_in6 sa6;
	std::memset(&sa6, 0, sizeof(sa6));
	sa6.sin6_family = AF_INET6;
	sa6.sin6_addr = in6addr_loopback;
	sa6.sin6_port = htons(0);
	REQUIRE(::bind(probe, (sockaddr *)&sa6, sizeof(sa6)) == 0);
	REQUIRE(::listen(probe, 1) == 0);
	socklen_t sl = sizeof(sa6);
	REQUIRE(::getsockname(probe, (sockaddr *)&sa6, &sl) == 0);
	int port = ntohs(sa6.sin6_port);
	::close(probe);

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "::1";
	vs.listen_port = port;
	cfg.push_back(vs);
	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	std::thread loop([&]
					 { s.run(25); });

	int cfd = ::socket(AF_INET6, SOCK_STREAM, 0);
	REQUIRE(cfd != -1);
	sockaddr_in6 d;
	std::memset(&d, 0, sizeof(d));
	d.sin6_family = AF_INET6;
	d.sin6_addr = in6addr_loopback;
	d.sin6_port = htons(port);
	REQUIRE(::connect(cfd, (sockaddr *)&d, sizeof(d)) == 0);
	const char req[] = "GET / HTTP/1.1\r\nHost: v6\r\n\r\n";
	write_all(cfd, req, sizeof(req) - 1);
	std::string resp = read_until_eof(cfd);
	::close(cfd);

	s.stop();
	if (loop.joinable())
		loop.join();

	REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
	REQUIRE(resp.substr(resp.size() - 5) == "hello");
}
#endif
