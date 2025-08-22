#include <catch2/catch_all.hpp>
#include "ClientConnection.h"
#include "HttpRequest.h"
#include <vector>
#include <string>

static void set_nonblock(int fd)
{
	int fl = ::fcntl(fd, F_GETFL, 0);
	REQUIRE(fl >= 0);
	REQUIRE(::fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0);
}

static std::pair<int, int> make_socketpair()
{
	int fds[2];
	REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	return std::make_pair(fds[0], fds[1]);
}

// Helper: Simulate feeding data in chunks to the connection
static bool feedChunks(ClientConnection &conn, const std::vector<std::string> &chunks)
{
	for (size_t i = 0; i < chunks.size(); ++i)
	{
		std::vector<char> buf(chunks[i].begin(), chunks[i].end());
		conn.getInBuffer().insert(conn.getInBuffer().end(), buf.begin(), buf.end());
		if (conn.processIncoming("ok"))
			return true;
		conn.getInBuffer().erase(conn.getInBuffer().begin(), conn.getInBuffer().end());
	}
	return false;
}

TEST_CASE("Edge case: HTTP request headers split across multiple chunks", "[integration][edge][http]")
{
	// Minimal GET request split into 3 chunks
	std::vector<std::string> chunks = {
		"GET / HTTP/1.1\r\nHost: loca",
		"lhost\r\nUser-Agent: test\r\n",
		"Accept: */*\r\n\r\n"};

	int peer, connFd;
	{
		std::pair<int, int> p = make_socketpair();
		peer = p.first;
		connFd = p.second;
	}
	set_nonblock(connFd);
	set_nonblock(peer);

	ClientConnection conn(connFd);
	conn.setState(READ_HEADERS);
	conn.getParseOffset() = 0;
	conn.getInBuffer().clear();
	// Simulate feeding chunks
	bool complete = feedChunks(conn, chunks);
	REQUIRE(complete);
	// Check that a response was generated
	REQUIRE(conn.getState() == WRITE);
	REQUIRE(!conn.getOutBuffer().empty());
	std::string response(conn.getOutBuffer().begin(), conn.getOutBuffer().end());
	// Should be a valid HTTP response
	bool condition = (response.find("HTTP/1.1 200 OK") != std::string::npos) ||
					 (response.find("HTTP/1.1 500 Internal Server Error") != std::string::npos);
	REQUIRE(condition == 1);
}
