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
	// bool headers_parsed = false;
	// size_t content_length = 0;
	// bool has_content_length = false;
	// bool connection_close = false;

	// // helper: try to parse headers if we have them
	// auto try_parse_headers = [&]() {
	// 	if (headers_parsed) return;
	// 	std::string s = out;
	// 	size_t pos = s.find("\r\n\r\n");
	// 	if (pos == std::string::npos) return;
	// 	headers_parsed = true;
	// 	std::string hdrs = s.substr(0, pos + 4);
	// 	// simple header search for Content-Length and Connection
	// 	size_t cl = hdrs.find("Content-Length:");
	// 	if (cl != std::string::npos) {
	// 		cl += strlen("Content-Length:");
	// 		while (cl < hdrs.size() && (hdrs[cl] == ' ' || hdrs[cl] == '\t')) ++cl;
	// 		size_t eol = hdrs.find('\r', cl);
	// 		if (eol != std::string::npos) {
	// 			std::string v = hdrs.substr(cl, eol - cl);
	// 			content_length = (size_t)atoi(v.c_str());
	// 			has_content_length = true;
	// 		}
	// 	}
	// 	size_t cc = hdrs.find("Connection:");
	// 	if (cc != std::string::npos) {
	// 		cc += strlen("Connection:");
	// 		while (cc < hdrs.size() && (hdrs[cc] == ' ' || hdrs[cc] == '\t')) ++cc;
	// 		size_t eol = hdrs.find('\r', cc);
	// 		if (eol != std::string::npos) {
	// 			std::string v = hdrs.substr(cc, eol - cc);
	// 			if (v.find("close") != std::string::npos || v.find("Close") != std::string::npos)
	// 				connection_close = true;
	// 		}
	// 	}
	// };

	// // Use poll-based short inactivity timeout when we have no content-length
	// struct pollfd pfd;
	// pfd.fd = fd;
	// pfd.events = POLLIN;

	// // set a recv timeout so we never block forever
	// struct timeval oldtv;
	// socklen_t optlen = sizeof(oldtv);
	// ::getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &oldtv, &optlen); // best-effort
	// struct timeval tv;
	// tv.tv_sec = 0;
	// tv.tv_usec = 500000; // 500ms
	// ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	for (;;)
	{
		ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
		if (r == 0)
			break; // peer sent FIN: clean EOF
		if (r < 0)
		{
			if (errno == EINTR)
				continue; // retry
			// if (errno == EAGAIN || errno == EWOULDBLOCK) {
			// 	// no data available now
			// 	if (headers_parsed && has_content_length) {
			// 		// wait until body arrives
			// 		int rc = ::poll(&pfd, 1, 200); // 200ms
			// 		if (rc <= 0) break;
			// 		continue;
			// 	}
			// 	// if no content-length, wait a short while for more data then return
			// 	int rc = ::poll(&pfd, 1, 200);
			// 	if (rc <= 0) break;
			// 	continue;
			// }
			if (errno == ECONNRESET)
				break; // peer reset is acceptable in some flows
			// if (errno == EPIPE) break;
			REQUIRE(r >= 0); // other errors -> fail
		}
		else
		{
			out.append(buf, buf + r);
			// try_parse_headers();
			// if (has_content_length)
			// {
			// 	// check if we received full body
			// 	size_t hdr_end = out.find("\r\n\r\n");
			// 	if (hdr_end != std::string::npos)
			// 	{
			// 		size_t body_bytes = out.size() - (hdr_end + 4);
			// 		if (body_bytes >= content_length) break;
			// 	}
			// }
			// else if (connection_close)
			// {
			// 	// server intends to close connection; wait a short while for EOF
			// 	int rc = ::poll(&pfd, 1, 200); // 200ms
			// 	if (rc <= 0) break; // no activity -> assume server finished
			// 	continue;
			// }
			// else
			// {
			// 	// no content-length and no connection: close — use short inactivity
			// 	int rc = ::poll(&pfd, 1, 50); // 50ms
			// 	if (rc <= 0) break;
			// }

			if (out.size() > guard)
				break; // safety
		}
	}
	// // restore previous timeout
	// ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &oldtv, sizeof(oldtv));
	return out;
}