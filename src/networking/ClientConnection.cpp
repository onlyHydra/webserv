/* --- ClientConnection.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "ClientConnection.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Router.h"
#include "Server.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include "HEADER_ENTRIES.h"
#include <sys/time.h>

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



u_int64_t ClientConnection::nowMs() {
    struct timeval tv; gettimeofday(&tv, 0);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

void ClientConnection::onTick() {
    // If the current phase has expired, close the connection
    if(state == CLOSE || !fd)
		return;
	if (expired()) {
        close();
    }
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

static std::string makeErrorResponse()
{
	const char *body = "Internal Server Error\n";
	std::string resp;
	resp.reserve(128);
	resp += "HTTP/1.1 500 Internal Server Error\r\n";
	resp += "Content-Length: 22\r\n";
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
static bool headersComplete(const std::vector<char> &buf, size_t &parseOffset, HttpRequest &request)
{
	(void)parseOffset;
	if (!request.parse(buf.data(), buf.size()))
		return (false);
	if (request.getState() <= HEADER || request.getState() == ERROR)
		return (false);
	// changeState(READ_BODY);
	if(request.getHeaders().keyExists(HDR_CONNECTION))
	{	if( request.keepAlive() || request.getHeaders().get(HDR_CONNECTION) == "keep-alive")
			request.setKeepAlive(true);
		else if (request.getHeaders().get(HDR_CONNECTION) == "closed")
			request.setKeepAlive(false);
	}
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
	HttpRequest req;

	if (headersComplete(inBuffer, parseOffset, req))
	{
		// TODO: parse method/target/Host from inBuffer. For now, placeholders:

		const int local_port = get_local_port(fd.get());
		vs_idx = -1;
		if (server && local_port > 0)
		{
			vs_idx = server->resolveVirtualServerByPort(local_port, "localhost");
		}
		// RouteDecision plan; // defaults HK_ERROR/500
		if (server && vs_idx >= 0)
		{
			HttpResponse res;
			// Router::makeDecisionForVS(server->getConfig(), vs_idx, "GET", "/", plan);
			if (!server->getPipeline()->processRequest(server->getConfig(), vs_idx, req, res))
			{
				std::string resp = makeErrorResponse();
				outBuffer.assign(resp.begin(), resp.end());
				changeState(WRITE);
				return false;
			}
		}
		std::string resp = makeHelloResponse();
		outBuffer.assign(resp.begin(), resp.end());
		if (!readPaused && outBuffer.size() >= HIGH_WATER)
    		readPaused = true;
		changeState(WRITE);
		resetDeadlineForWrite();
		return true;
		// else
		// {
		// 	plan.kind = RouteDecision::HK_ERROR;
		// 	plan.status = 500;
		// }
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
			bumpDeadline(HDR_TIMEOUT_MS); 
			size_t remaining = total - outOffset;
			if (readPaused && remaining <= LOW_WATER)
				readPaused = false;
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		// Error (EPIPE/ECONNRESET/etc.)
		close();
		return;
	}

	// All bytes sent
	// close();
	deadline_ms = nowMs() + POST_WRITE_LINGER_MS; 
	return;
}

void ClientConnection::readFromSocket()
{
	if (this->state != READ_HEADERS  || !fd)
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
			bumpDeadline(HDR_TIMEOUT_MS); 
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
