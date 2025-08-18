#pragma once
#include <catch2/catch_all.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <string>

inline int pick_free_port_ipv4()
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

inline int connect_ipv4(int port)
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

inline void write_all(int fd, const char *p, size_t n)
{
	size_t off = 0;
	while (off < n)
	{
		ssize_t w = ::send(fd, p + off, n - off, MSG_NOSIGNAL);
		REQUIRE(w >= 0);
		off += (size_t)w;
	}
}

inline std::string read_until_eof(int fd, size_t guard = (1u << 20))
{
	std::string out;
	char buf[4096];
	for (;;)
	{
		ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
		if (r == 0)
			break; // peer sent FIN: clean EOF
		if (r < 0)
		{
			if (errno == EINTR)
				continue; // retry
			if (errno == ECONNRESET)
				break; // peer reset is acceptable in some flows
			// If you also want to tolerate EPIPE here on some platforms:
			// if (errno == EPIPE) break;
			REQUIRE(r >= 0); // other errors -> fail
		}
		else
		{
			out.append(buf, buf + r);
			if (out.size() > guard)
				break; // safety
		}
	}
	return out;
}