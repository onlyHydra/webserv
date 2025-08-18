#include <catch2/catch_all.hpp>
#include <thread>
#include <string>
#include <vector>
#include <unistd.h>

#include "Server.h"
#include "ServerConfig.h"
#include "VirtualServer.h"

#include "helpers/net.hpp"

TEST_CASE("Slowloris: partial headers then FIN => no response", "[server][slowloris]")
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

	int cfd = connect_ipv4(port);
	const char partial[] = "GET / HTTP/1.1\r\nHost: x\r\n"; // no \r\n\r\n
	write_all(cfd, partial, sizeof(partial) - 1);
	::shutdown(cfd, SHUT_WR); // FIN from client
	std::string resp = read_until_eof(cfd);
	::close(cfd);

	s.stop();
	if (loop.joinable())
		loop.join();

	REQUIRE(resp.empty()); // server should not reply without complete headers
}

TEST_CASE("Headers fragmented into tiny writes => still responds", "[server][fragment]")
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

	int cfd = connect_ipv4(port);
	const std::string req = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
	for (size_t i = 0; i < req.size(); ++i)
	{
		write_all(cfd, &req[i], 1);
		// minimal delay is optional; uncomment to stress scheduling
		// std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::string resp = read_until_eof(cfd);
	::close(cfd);

	s.stop();
	if (loop.joinable())
		loop.join();

	REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
	REQUIRE(resp.size() >= resp.find("\r\n\r\n") + 4);
	REQUIRE(resp.substr(resp.size() - 5) == "hello");
}

TEST_CASE("MAX_INBUFFER overflow: >1MB without terminator => drop connection", "[server][overflow]")
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

	int cfd = connect_ipv4(port);

	// Send header start but never \r\n\r\n; then spam >1MB
	const char prefix[] = "GET / HTTP/1.1\r\nHost: x\r\n";
	write_all(cfd, prefix, sizeof(prefix) - 1);

	std::string big(1100000, 'A'); // 1.1MB
	write_all(cfd, big.data(), big.size());

	std::string resp = read_until_eof(cfd); // server should close; no response body
	::close(cfd);

	s.stop();
	if (loop.joinable())
		loop.join();

	REQUIRE(resp.empty());
}

TEST_CASE("Client closes right after complete headers => server handles send error", "[server][rst]")
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

	int cfd = connect_ipv4(port);
	const char req[] = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
	write_all(cfd, req, sizeof(req) - 1);
	// Immediately close; server will attempt send and should survive gracefully
	::close(cfd);

	// Give loop a tick to process
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	s.stop();
	if (loop.joinable())
		loop.join();

	SUCCEED("Server tolerated client close during write without crashing");
}

TEST_CASE("Bind conflict: 0.0.0.0:PORT and 127.0.0.1:PORT => start() throws", "[server][startup][bind]")
{
	const int port = pick_free_port_ipv4();

	ServerConfig cfg;
	VirtualServer a;
	a.listen_host = "0.0.0.0";
	a.listen_port = port;
	VirtualServer b;
	b.listen_host = "127.0.0.1";
	b.listen_port = port;
	cfg.push_back(a);
	cfg.push_back(b);

	Server s(cfg);
	REQUIRE_THROWS_AS(s.start(), std::runtime_error);
}
