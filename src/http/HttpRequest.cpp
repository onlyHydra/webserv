/* --- HttpRequest.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "HttpRequest.h"
#include <iostream>
#include <sstream>

/**
 * Wether given Method is a valid one
 * @returns true if it is
 */
bool	isMethodValid(const std::string &in)
{
	if (in == "GET" || in == "PUT" || in == "PATCH" || in == "DELETE" || \
		in == "POST")
		return (true);
	return (false);
}

/**
 * Wether given Path is a valid one
 * @returns true if it is
 */
bool	isPathValid(const std::string &in)
{
	if (in[0] != '/')
		return (false);
	return (true);
}

/**
 * Wether given Query is a valid one
 * @returns true if it is
 */
bool	isQueryValid(const std::string &in)
{
	(void)in;
	return (true);
}

/**
 * Wether given Http Version is a valid one
 * @returns true if it is
 */
bool	isHttpVerValid(const std::string &in)
{
	if (in.size() < 1)
		return(false);
	return (true);
}

/**
 * Wether given Header Key is valid
 * @returns true if it is
 */
bool	isKeyValid(const std::string &in)
{
	if (in.size() < 1)
		return(false);
	return (true);
}

/**
 * Wether given Header Key is valid
 * @returns true if it is
 */
bool	isValueValid(const std::string &in)
{
	(void)in;
	return (true);
}

HttpRequest::HttpRequest()
{
	this->method = "";
	this->path = "";
	this->http_version = "";
	this->uri = "";
	this->query = "";
	this->session_id = "";
	this->bodyLength = 0;
	this->buffer = "";
	this->state = START;
}

HttpRequest::~HttpRequest()
{
}

bool	HttpRequest::keepAlive(void) const
{
	return (true);
}

bool	HttpRequest::headerAsSize(string k, size_t &v) const
{
	(void)k;
	(void)v;
	return (true);
}

string	HttpRequest::extension(void) const
{
	return ("");
}

int	HttpRequest::handleLineStart(const std::string &in)
{
	size_t	i = 0;
	size_t	pos = 0;

	pos = in.find(' ', i);
	if (pos == (size_t)-1)
		return (ERR_HTTP_BAD_REQUEST);
	this->method = in.substr(i, pos);
	if (!isMethodValid(this->method))
		return (ERR_HTTP_BAD_REQUEST);

	i = ++pos;
	pos = in.find('?', i);
	if (pos != (size_t)-1)
	{
		this->path = in.substr(i, pos - i);
		if (!isPathValid(this->path))
			return (ERR_HTTP_BAD_REQUEST);
		i = ++pos;
		pos = in.find(' ', i);
		if (pos == (size_t)-1)
			return (ERR_HTTP_BAD_REQUEST);
		this->query = in.substr(i, pos - i);
		if (!isQueryValid(this->query))
			return (ERR_HTTP_BAD_REQUEST);
	}
	else
	{
		pos = in.find(' ', i);
		if (pos == (size_t)-1)
			return (ERR_HTTP_BAD_REQUEST);
		this->path = in.substr(i, pos - i);
		if (!isPathValid(this->path))
			return (ERR_HTTP_BAD_REQUEST);
	}

	i = ++pos;
	pos = in.find("\r\n", i);
	if (pos == (size_t)-1)
		return (ERR_HTTP_BAD_REQUEST);
	this->http_version = in.substr(i, pos - i);
	if (!isHttpVerValid(this->http_version))
		return (ERR_HTTP_BAD_REQUEST);

	i = pos + 2;
	if (in[i])
		return (ERR_HTTP_BAD_REQUEST);
	this->state = HEADER;
	return (0);
}

int	HttpRequest::handleLineHeader(const std::string &in)
{
	size_t	i = 0;
	size_t	pos = 0;
	int		contLength = 0;
	bool	atoiError;
	std::string key;
	std::string value;

	if (in == "\r\n")
	{
		if (this->method == "POST" || this->method == "PUT" || this->method == "PATCH")
			this->state = BODY;
		else
			this->state = OVER;
		return (0);
	}
	pos = in.find(':', i);
	if (pos == (size_t)-1)
		return (ERR_HTTP_BAD_REQUEST);
	key = in.substr(i, pos);
	if (!isKeyValid(key))
		return (ERR_HTTP_BAD_REQUEST);
	i = pos + 1;
	while (in[i] == ' ')
		i++;
	pos = in.find("\r\n", i);
	if (pos == (size_t)-1)
		return (ERR_HTTP_BAD_REQUEST);
	value = in.substr(i, pos - i);
	if (!isValueValid(value))
		return (ERR_HTTP_BAD_REQUEST);
	i = pos + 2;
	if (in[i])
		return (ERR_HTTP_BAD_REQUEST);
	if (!this->headers.set(key, value))
		return (ERR_HTTP_HEADER_LIMIT);
	if (key == "Content-Length")
	{
		contLength = Atoi::atoiHttpReq(value.c_str(), atoiError);
		if (atoiError)
			return (ERR_HTTP_BAD_REQUEST);
		this->bodyLength = contLength;
	}
	return (0);
}

int	HttpRequest::handleLineBody(const std::string &in)
{
	if (this->method != "POST" && this->method != "PUT" && this->method != "PATCH")
		return (ERR_HTTP_BAD_REQUEST);
	if (!this->headers.keyExists("Content-Length"))
		return (ERR_HTTP_BAD_REQUEST);
	if (in.size() + this->body.size() > this->bodyLength)
		return (ERR_HTTP_BAD_REQUEST);
	for (int i = 0; in[i]; i++)
		this->body.push_back(in[i]);
	if (this->body.size() > this->bodyLength)
		return (ERR_HTTP_BAD_REQUEST);
	if (this->body.size() == this->bodyLength)
		this->state = OVER;
	return (0);
}

int	HttpRequest::handleLine(const std::string &in)
{
	if (this->state == OVER || this->state == ERROR)
		return (ERR_HTTP_BAD_REQUEST);
	if (this->state == START)
		return (handleLineStart(in));
	if (this->state == HEADER)
		return (handleLineHeader(in));
	if (this->state == BODY)
		return (handleLineBody(in));
	return (ERR_HTTP_BAD_REQUEST);
}

int	HttpRequest::handleInput(bool &activity)
{
	int	i = 0;
	activity = false;
	std::string line;
	
	if (this->buffer.size() == 0)
		return (0);
	if (this->state != BODY)
	{
		for (; this->buffer[i]; i++)
		{
			if (this->buffer[i] == '\r' && this->buffer[i + 1] == '\n')
				break ;
		}
		if (!this->buffer[i])
			return (0);
		activity = true;
		line = this->buffer.substr(0, i + 2);
		this->buffer.erase(0, i + 2);
	}
	else
	{
		activity = true;
		line = this->buffer;
		buffer.clear();
	}
	return (this->handleLine(line));
}

bool	HttpRequest::parse(const char* data, size_t n)
{
	bool activity = true;
	int status = 0;

	if (this->state == ERROR || this->state == OVER || n < 1)
		return (false);
	this->buffer += string(data, n);
	while (!status && activity)
	{
		status = this->handleInput(activity);
		if (status)
		{
			this->state = ERROR;
			errno = status;
			return (false);
		}
	}
	return (true);
}

bool	HttpRequest::headersDone(void)
{
	if (this->state == BODY || this->state == OVER)
		return (true);
	return (false);
}

string	HttpRequest::getMethod(void) const
{ return (this->method); }

string	HttpRequest::getUri(void) const
{ return (this->uri); }

string	HttpRequest::getPath(void) const
{ return (this->path); }

string	HttpRequest::getQuery(void) const
{ return (this->query); }

string	HttpRequest::getSessId(void) const
{ return (this->session_id); }

size_t	HttpRequest::getBodyLength(void) const
{ return (this->bodyLength); }

string	HttpRequest::getHttpVer(void) const
{ return (this->http_version); }

string	HttpRequest::getBuffer(void) const
{ return (this->buffer); }

const Headers&	HttpRequest::getHeaders(void) const
{ return (this->headers); }

CookieJar HttpRequest::getCookies(void) const
{ return (this->cookies); }

vector<char> HttpRequest::getBody(void) const
{ return (this->body); }

enum HttpRequestState HttpRequest::getState(void) const
{ return (this->state); }

std::ostream &operator<<(std::ostream &out, const HttpRequest &target)
{
	out << \
	target.getMethod() << " " << \
	target.getPath() << " " << \
	target.getHttpVer() << std::endl << \
	"-----------" << std::endl;
	out << target.getHeaders() << std::endl << \
	"-----------" << std::endl;
	out.write(target.getBody().data(), target.getBody().size());
	out << std::endl << \
	"-----------";
	return (out);
}

