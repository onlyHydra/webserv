#include <catch2/catch_all.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

#include "Server.h"
#include "ServerConfig.h" // stubs
#include "EventLoop.h"	  // stub

// Helper: pick a free TCP port on 127.0.0.1 (race is acceptable in tests)
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


TEST_CASE("Server starts one listener for multiple VS on same (host,port)", "[server][startup]")
{
	int port = pick_free_port();

	ServerConfig cfg;
	VirtualServer vs1;
	vs1.listen_host = "127.0.0.1";
	vs1.listen_port = port;
	vs1.server_names.push_back("a.local");
	VirtualServer vs2;
	vs2.listen_host = "127.0.0.1";
	vs2.listen_port = port;
	vs2.server_names.push_back("b.local");
	cfg.push_back(vs1);
	cfg.push_back(vs2);

	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	REQUIRE(s.listenerCount() == 1);
	REQUIRE(s.listenerPortAt(0) == port);

	// EventLoop stub recorded added fds? (optional — if you expose it)
	s.stop();
}

TEST_CASE("Server starts multiple listeners for different (host, port)", "[server][startup]")
{
	int port1 = pick_free_port();
	int port2 = pick_free_port();

	ServerConfig cfg;
	VirtualServer vs1;
	vs1.listen_host = "127.0.0.1";
	vs1.listen_port = port1;
	VirtualServer vs2;
	vs2.listen_host = "127.0.0.1";
	vs2.listen_port = port2;
	cfg.push_back(vs1);
	cfg.push_back(vs2);

	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	REQUIRE(s.listenerCount() == 2);
	REQUIRE(s.listenerPortAt(0) == port1);
	REQUIRE(s.listenerPortAt(1) == port2);
	s.stop();
}

TEST_CASE("Server surfaces bind failure cleanly", "[server][errors]")
{
	// Pre-bind to force EADDRINUSE
	int busy = ::socket(AF_INET, SOCK_STREAM, 0);
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
	int port = ntohs(sa.sin_port);

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.push_back(vs);

	Server s(cfg);
	REQUIRE_THROWS_AS(s.start(), std::runtime_error);

	::close(busy);
}

TEST_CASE("Server is actually listening (connect succeeds)", "[server][integration]")
{
	int port = pick_free_port();

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.push_back(vs);

	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	REQUIRE(s.listenerCount() == 1);

	// Try a real connect()
	int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
	REQUIRE(cfd != -1);
	sockaddr_in sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(port);
	int rc = ::connect(cfd, (sockaddr *)&sa, sizeof(sa));
	REQUIRE(rc == 0); // TCP handshake reached your listener

	::close(cfd);
	s.stop();
}

TEST_CASE("Server handles empty configuration", "[server][config]")
{
	ServerConfig cfg; // No virtual
	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	REQUIRE(s.listenerCount() == 0);
	s.stop();
}

TEST_CASE("Server skips invalid port and continues running", "[server][config]") {
    ServerConfig cfg;

    // Valid virtual server
    VirtualServer vs1;
    vs1.listen_host = "127.0.0.1";
    vs1.listen_port = 8080; // Valid port
    cfg.push_back(vs1);

    // Invalid virtual server
    VirtualServer vs2;
    vs2.listen_host = "127.0.0.1";
    vs2.listen_port = -1; // Invalid port
    cfg.push_back(vs2);

    Server s(cfg);

    // Server should start without throwing
    REQUIRE_NOTHROW(s.start());

    // Only the valid listener should be created
    REQUIRE(s.listenerCount() == 1);
    REQUIRE(s.listenerPortAt(0) == 8080);

    s.stop();
    REQUIRE(s.listenerCount() == 0); // Ensure all listeners are closed
}
TEST_CASE("Server shuts down gracefully", "[server][shutdown]")
{
	int port = pick_free_port();

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.push_back(vs);

	Server s(cfg);
	REQUIRE_NOTHROW(s.start());
	REQUIRE(s.listenerCount() == 1);

	s.stop();
	REQUIRE(s.listenerCount() == 0); // Ensure all listeners are closed
}