/* --- ClientConnection.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "ClientConnection.h"
#include "HttpRequest.h"
#include "Router.h"
#include "Server.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>

static int get_local_port(int fd)
{
	struct sockaddr_storage ss = sockaddr_storage();
	socklen_t sl = sizeof(ss);
	if (::getsockname(fd, (struct sockaddr *)&ss, &sl) != 0)
		return -1;
	if (ss.ss_family == AF_INET)
		return (int)ntohs(((sockaddr_in *)&ss)->sin_port);
	if (ss.ss_family == AF_INET6)
		return (int)ntohs(((sockaddr_in6 *)&ss)->sin6_port);
	return -1;
}

void ClientConnection::changeState(State state)
{
	this->state = state;
}

/**
 * Make sure you remove ClientConnection after call
 * @warning Call destructor after Close so the Client Connection dies
 */
void ClientConnection::close()
{
	if (fd)
		this->fd.reset();
	this->state = CLOSE;
}

/**
 * @brief PLEASE DELETE ME I AM A PLACEHOLDER
 */
static std::string makeHelloResponse()
{
	const char *body = "hello";
	std::string resp;
	resp.reserve(128);
	resp += "HTTP/1.1 200 OK\r\n";
	resp += "Content-Length: 5\r\n";
	resp += "Connection: close\r\n";
	resp += "Content-Type: text/plain\r\n";
	resp += "\r\n";
	resp += body;
	return resp;
}

/**
 *  Just a placeholder until he have a proper Parser
 *  Look for \r\n\r\n using parseOffset to avoid rescanning.
 */
static bool headersComplete(const std::vector<char> &buf, size_t &parseOffset , HttpRequest & request)
{
	(void)parseOffset;
	if (!request.parse(buf.data(), buf.size()))
		return (false);
	if (request.getState() <= HEADER || request.getState() == ERROR)
		return (false);
	
	const Headers &header = request.getHeaders();
	(void)header;
	return true;
}

/**
 * Needs to be extended to HTTP Request parsing later
 * @brief Called after onReadable() has appended bytes
 *
 */
bool ClientConnection::processIncoming()
{
	if (this->state != READ_HEADERS)
		return false;
	HttpRequest request;

	if (headersComplete(inBuffer, parseOffset, request))
	{
		// TODO: parse method/target/Host from inBuffer. For now, placeholders:

		const int local_port = get_local_port(fd.get());
		int vs_idx = -1;
		if (server && local_port > 0)
		{
			vs_idx = server->resolveVirtualServerByPort(local_port, "localhost");
		}
		RouteDecision plan; // defaults HK_ERROR/500
		if (server && vs_idx >= 0)
		{
			Router::makeDecisionForVS(server->getConfig(), vs_idx, "GET", "/", plan);
		}
		else
		{
			plan.kind = RouteDecision::HK_ERROR;
			plan.status = 500;
		}
		std::string resp = makeHelloResponse();
		outBuffer.assign(resp.begin(), resp.end());
		changeState(WRITE);
		return true;
	}
	return false;
}

void ClientConnection::onReadable()
{
	readFromSocket();
	// processIncoming();
}

void ClientConnection::onWritable()
{
	if (state != WRITE || !fd)
		return;

	const char *base = outBuffer.data();
	size_t total = outBuffer.size();

	while (outOffset < total)
	{
		const char *p = base + outOffset;
		size_t left = total - outOffset;

		ssize_t n = ::send(fd.get(), p, left, MSG_NOSIGNAL);
		if (n > 0)
		{
			outOffset += static_cast<size_t>(n);
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		// Error (EPIPE/ECONNRESET/etc.)
		close();
		return;
	}

	// All bytes sent
	close();
}

void ClientConnection::readFromSocket()
{
	if (state != READ_HEADERS || !fd)
		return;

	while (true)
	{
		if (inBuffer.size() >= MAX_INBUFFER)
		{
			close();
			return;
		}

		char buffer[READ_CHUNK];
		ssize_t n = ::recv(fd.get(), buffer, sizeof(buffer), 0);

		if (n > 0)
		{
			size_t spaceLeft = MAX_INBUFFER - inBuffer.size();
			size_t toCopy = static_cast<size_t>(n);
			toCopy = (toCopy > spaceLeft) ? spaceLeft : toCopy;
			inBuffer.insert(inBuffer.end(), buffer, buffer + toCopy);
			if (processIncoming())
				return;
			continue;
		}

		if (n == 0)
		{
			if (processIncoming())
				return;
			close();
			return;
		}
		if (n < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			close();
			return;
		}
	}
}
