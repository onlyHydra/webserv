// tests/integration/server_end_to_end.cpp
#include <catch2/catch_all.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

#include <thread> // C++14 tests only
#include <string>
#include <vector>

#include "Server.h" // pulls real EventLoop/ServerConfig/VirtualServer (no stubs)

// ====== helpers ======

static int pick_free_port()
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	REQUIRE(fd != -1);
	int yes = 1;
	::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	sockaddr_in sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(0);
	REQUIRE(::bind(fd, (sockaddr *)&sa, sizeof(sa)) == 0);
	REQUIRE(::listen(fd, 16) == 0);
	socklen_t len = sizeof(sa);
	REQUIRE(::getsockname(fd, (sockaddr *)&sa, &len) == 0);
	int port = ntohs(sa.sin_port);
	::close(fd);
	return port;
}

static int connect_to_local(int port)
{
	int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
	REQUIRE(cfd != -1);
	sockaddr_in sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(port);
	int rc = ::connect(cfd, (sockaddr *)&sa, sizeof(sa));
	REQUIRE(rc == 0);
	return cfd;
}

static void write_all(int fd, const char *p, size_t n)
{
	size_t off = 0;
	while (off < n)
	{
		ssize_t w = ::send(fd, p + off, n - off, MSG_NOSIGNAL);
		REQUIRE(w >= 0);
		off += static_cast<size_t>(w);
	}
}

static std::string read_until_eof(int fd)
{
	std::string out;
	char buf[4096];
	for (;;)
	{
		ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
		if (r == 0)
			break;		 // EOF
		REQUIRE(r >= 0); // not an error
		out.append(buf, buf + r);
		if (out.size() > (1u << 20))
			break; // safety guard (1MB)
	}
	return out;
}

// ====== tests ======

TEST_CASE("End-to-end: single request yields hello response", "[server][e2e]")
{
	const int port = pick_free_port();

	// minimal config (real types via Server.h includes)
	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.servers.push_back(vs);

	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	REQUIRE(s.listenerCount() == 1);
	REQUIRE(s.listenerPortAt(0) == port);

	// run the event loop in background
	std::thread loopThread([&]()
						   {
							   s.run(50); // 50ms poll timeout
						   });

	// client: connect, send a minimal HTTP/1.1 request, read response
	const int cfd = connect_to_local(port);
	const char req[] =
		"GET / HTTP/1.1\r\n"
		"Host: test.local\r\n"
		"\r\n";
	write_all(cfd, req, sizeof(req) - 1);
	std::string resp = read_until_eof(cfd);
	::close(cfd);

	// stop loop and join
	s.stop();
	if (loopThread.joinable())
		loopThread.join();

	// basic assertions on the placeholder response your ClientConnection builds
	REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
	REQUIRE(resp.find("Content-Length: 5") != std::string::npos);
	REQUIRE(resp.size() >= resp.find("\r\n\r\n") + 4);
	REQUIRE(resp.substr(resp.size() - 5) == "hello");
}

TEST_CASE("End-to-end: multiple sequential connections", "[server][e2e][seq]")
{
	const int port = pick_free_port();

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.servers.push_back(vs);

	Server s(cfg);
	REQUIRE_NOTHROW(s.start());

	std::thread loopThread([&]()
						   { s.run(50); });

	for (int i = 0; i < 3; ++i)
	{
		int cfd = connect_to_local(port);
		const char req[] =
			"GET /ping HTTP/1.1\r\n"
			"Host: t.local\r\n"
			"\r\n";
		write_all(cfd, req, sizeof(req) - 1);
		std::string resp = read_until_eof(cfd);
		::close(cfd);

		REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
		REQUIRE(resp.substr(resp.size() - 5) == "hello");
	}

	s.stop();
	if (loopThread.joinable())
		loopThread.join();
}

TEST_CASE("Startup: bind failure surfaces cleanly (EADDRINUSE)", "[server][startup][errors]")
{
	// occupy a port
	const int busy = ::socket(AF_INET, SOCK_STREAM, 0);
	REQUIRE(busy != -1);
	int yes = 1;
	::setsockopt(busy, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	sockaddr_in sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(0);
	REQUIRE(::bind(busy, (sockaddr *)&sa, sizeof(sa)) == 0);
	REQUIRE(::listen(busy, 8) == 0);
	socklen_t len = sizeof(sa);
	REQUIRE(::getsockname(busy, (sockaddr *)&sa, &len) == 0);
	const int port = ntohs(sa.sin_port);

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.servers.push_back(vs);

	Server s(cfg);
	REQUIRE_THROWS_AS(s.start(), std::runtime_error);

	::close(busy);
}

TEST_CASE("Graceful shutdown closes listeners", "[server][shutdown]")
{
	const int port = pick_free_port();

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.servers.push_back(vs);

	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	REQUIRE(s.listenerCount() == 1);

	s.stop();
	REQUIRE(s.listenerCount() == 0);
}

TEST_CASE("End-to-end: client half-closes after request", "[server][e2e]")
{
	const int port = pick_free_port();

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.servers.push_back(vs);

	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	std::thread loopThread([&]
						   { s.run(50); });

	int cfd = connect_to_local(port);
	const char req[] =
		"GET / HTTP/1.1\r\n"
		"Host: x\r\n"
		"\r\n";
	write_all(cfd, req, sizeof(req) - 1);

	// Half-close write side immediately (peer sends FIN)
	::shutdown(cfd, SHUT_WR);

	std::string resp = read_until_eof(cfd);
	::close(cfd);

	s.stop();
	if (loopThread.joinable())
		loopThread.join();

	REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
	REQUIRE(resp.size() >= resp.find("\r\n\r\n") + 4);
	REQUIRE(resp.substr(resp.size() - 5) == "hello");
}

TEST_CASE("End-to-end: headers split across two TCP reads", "[server][e2e][split]")
{
	const int port = pick_free_port();

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.servers.push_back(vs);

	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	std::thread loopThread([&]
						   { s.run(50); });

	int cfd = connect_to_local(port);

	const char part1[] = "GET / HTTP/1.1\r\nHost: x\r\n";
	const char part2[] = "\r\n"; // completes CRLFCRLF

	write_all(cfd, part1, sizeof(part1) - 1);

	// small pause so the server gets woken and sees a partial header
	// (use usleep to avoid adding <chrono> in some environments)
	::usleep(5000);

	write_all(cfd, part2, sizeof(part2) - 1);

	std::string resp = read_until_eof(cfd);
	::close(cfd);

	s.stop();
	if (loopThread.joinable())
		loopThread.join();

	REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
	REQUIRE(resp.substr(resp.size() - 5) == "hello");
}
